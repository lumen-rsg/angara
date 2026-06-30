#include "Chaperone.h"
#include "Stmt.h"
#include "Expr.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include "Token.h"

#include <functional>

namespace angara {

// Helper: report as error (normal) or warning (inside @unsafe).
// Suppressed entirely during the fixed-point convergence passes (the final
// pass emits); this avoids duplicate diagnostics across iterations.
void Chaperone::diag(Context& ctx, const Token& tok,
                     const std::string& msg, const std::string& code) {
    if (ctx.suppress_diag) return;
    if (ctx.in_unsafe)
        ctx.eh.warning(tok, msg + " [in @unsafe — reported as warning]", code);
    else
        ctx.eh.report(tok, msg, code);
}

// ============================================================================
// State machine helpers
// ============================================================================

Chaperone::State Chaperone::join(State a, State b) {
    if (a == b) return a;
    if (a == State::Live || b == State::Live) return State::Live;
    return State::Dropped;  // Conservative for Dropped≠Escaped, Dropped≠Uninit, etc.
}

Chaperone::StateMap Chaperone::join_maps(const StateMap& a, const StateMap& b) {
    StateMap result = a;
    for (auto& [key, val_b] : b) {
        auto it = result.find(key);
        if (it == result.end()) {
            result[key] = val_b;
        } else {
            it->second = join(it->second, val_b);
        }
    }
    return result;
}

// ============================================================================
// Phase 1: Collect tracked types (class + owned)
// ============================================================================

void Chaperone::collectTrackedTypes(Context& ctx,
    const std::vector<std::shared_ptr<Stmt>>& program)
{
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            ctx.tracked_types.insert(cls->name.lexeme);
        } else if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
            if (data->is_owned) {
                ctx.tracked_types.insert(data->name.lexeme);
            }
        }
    }
}

// ============================================================================
// Type helpers
// ============================================================================

bool Chaperone::isTrackedType(Context& ctx, const std::string& type_name) {
    return ctx.tracked_types.count(type_name) > 0;
}

bool Chaperone::isTrackedTypeObj(Context& ctx, const Type& type) {
    // ref<T> is never tracked (non-owning).
    if (type.kind == TypeKind::REF) return false;
    // Unwrap optionals: Buf? tracked iff Buf tracked.
    const Type* t = &type;
    if (t->kind == TypeKind::OPTIONAL) {
        auto ot = dynamic_cast<const OptionalType*>(t);
        if (!ot || !ot->wrapped_type) return false;
        t = ot->wrapped_type.get();
    }
    if (t->kind == TypeKind::CLASS || t->kind == TypeKind::INSTANCE) return true;
    if (t->kind == TypeKind::DATA) return ctx.tracked_types.count(t->toString()) > 0;
    return false;
}

bool Chaperone::isTrackedVar(Context& ctx, const VarDeclStmt& var) {
    auto& types = ctx.tc.getVariableTypes();
    auto it = types.find(&var);
    if (it == types.end() || !it->second) return false;
    return isTrackedTypeObj(ctx, *it->second);
}

// Resolve a VarExpr's type via the type checker's resolution map and report
// whether it is a ref<T> (a non-owning borrow). Used to distinguish a move
// (`x = tracked`) from a borrow (`ref_x = tracked`).
bool Chaperone::isRefVarExpr(Context& ctx, const VarExpr& ve) {
    auto& resolutions = ctx.tc.getVariableResolutions();
    auto it = resolutions.find(&ve);
    if (it == resolutions.end() || !it->second || !it->second->type) return false;
    return it->second->type->kind == TypeKind::REF;
}

// Recursively collect every VarExpr name referenced in a statement tree.
// Used to find which tracked locals a closure captures.
void Chaperone::collectVarRefs(const std::shared_ptr<Stmt>& stmt,
    std::set<std::string>& out)
{
    if (!stmt) return;
    // Walk the common statement shapes that contain expressions. We don't need
    // full analysis — just every VarExpr leaf. This is structural: any new Stmt
    // kind not handled here simply contributes no refs (conservative-safe for
    // capture detection — better to under-report captures than crash).
    auto walk_expr = [&](const std::shared_ptr<Expr>& e) {
        collectExprVarRefs(e, out);
    };
    if (auto* es = dynamic_cast<const ExpressionStmt*>(stmt.get())) { walk_expr(es->expression); return; }
    if (auto* v = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
        if (v->initializer) walk_expr(v->initializer);
        return;
    }
    if (auto* r = dynamic_cast<const ReturnStmt*>(stmt.get())) { if (r->value) walk_expr(r->value); return; }
    if (auto* t = dynamic_cast<const ThrowStmt*>(stmt.get())) { if (t->expression) walk_expr(t->expression); return; }
    if (auto* blk = dynamic_cast<const BlockStmt*>(stmt.get())) {
        for (const auto& s : blk->statements) collectVarRefs(s, out);
        return;
    }
    if (auto* ifs = dynamic_cast<const IfStmt*>(stmt.get())) {
        walk_expr(ifs->condition);
        collectVarRefs(ifs->thenBranch, out);
        collectVarRefs(ifs->elseBranch, out);
        return;
    }
    if (auto* wh = dynamic_cast<const WhileStmt*>(stmt.get())) {
        if (wh->condition) walk_expr(wh->condition);
        collectVarRefs(wh->body, out);
        return;
    }
    if (auto* fs = dynamic_cast<const ForStmt*>(stmt.get())) {
        if (fs->condition) walk_expr(fs->condition);
        if (fs->increment) walk_expr(fs->increment);
        collectVarRefs(fs->initializer, out);
        collectVarRefs(fs->body, out);
        return;
    }
    if (auto* fi = dynamic_cast<const ForInStmt*>(stmt.get())) {
        if (fi->collection) walk_expr(fi->collection);
        collectVarRefs(fi->body, out);
        return;
    }
    if (auto* dr = dynamic_cast<const DropStmt*>(stmt.get())) { out.insert(dr->name.lexeme); return; }
}

// ============================================================================
  // collectExprVarRefs, checkEscapeIntoContainer, and analyzeExpr live in
  // expr/ExprAnalysis.cpp. analyzeFunction/analyzeBlock/analyzeStmt live in
  // stmt/StmtAnalysis.cpp. The Chaperone is fully static (no instance state);
  // Context is threaded by reference, so the split needs no glue.
  // ============================================================================
  

// ============================================================================
// Exception paths — no separate phase.
//
// The v5 model has no auto-unwind (the old `unwindAtThrow` was removed in
// cde8e23 — it was unsound with refs/cycles: it dropped what the Chaperone
// tracked but couldn't prevent dangling refs in untracked containers).
// Throw-path leak detection (E501) is handled inline in the ThrowStmt handler
// above, and a surrounding `finally {}` discharges the obligation (S8). The
// programmer cleans up explicitly; the Chaperone verifies.
// ============================================================================

// ============================================================================
// Phase 4: Cycle detection
// ============================================================================

void Chaperone::detectCycles(Context& ctx,
    const std::vector<std::shared_ptr<Stmt>>& program)
{
    std::map<std::string, std::set<std::string>> graph;


    // Extract the base type name from a field's type annotation. Cycle
    // detection only needs the name (to compare against tracked_types); we read
    // it directly from the AST rather than resolveType(), which is non-const and
    // would require casting away const on the TypeChecker. Field types live on
    // DataType/ClassType (filled by the type checker), NOT in getVariableTypes()
    // — that map only holds local/let VarDeclStmts. Looking it up there was why
    // E504 never fired (the graph stayed empty).
    std::function<std::string(const ASTType*)> base_name = [&](const ASTType* t) -> std::string {
        if (!t) return "";
        if (auto* s = dynamic_cast<const SimpleType*>(t)) return s->name.lexeme;
        if (auto* g = dynamic_cast<const GenericType*>(t)) return g->name.lexeme;
        if (auto* o = dynamic_cast<const OptionalTypeNode*>(t)) return base_name(o->base_type.get());
        if (auto* ow = dynamic_cast<const OwnedTypeNode*>(t)) return base_name(ow->inner_type.get());
        return "";
    };

    auto check_field = [&](const std::string& owner, const VarDeclStmt* field) {
        if (!field || !field->typeAnnotation) return;
        std::string ft = base_name(field->typeAnnotation.get());
        if (!ft.empty() && ctx.tracked_types.count(ft))
            graph[owner].insert(ft);
    };

    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
            if (data->is_owned)
                for (const auto& f : data->fields)
                    check_field(data->name.lexeme, f.get());
        } else if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            for (const auto& member : cls->members) {
                if (auto* fm = dynamic_cast<const FieldMember*>(member.get()))
                    check_field(cls->name.lexeme, fm->declaration.get());
            }
        }
    }

    std::set<std::string> visiting, visited;
    std::function<bool(const std::string&, std::vector<std::string>&)> dfs =
        [&](const std::string& node, std::vector<std::string>& path) -> bool {
        if (visiting.count(node)) {
            auto it = std::find(path.begin(), path.end(), node);
            std::string cycle;
            for (auto i = it; i != path.end(); ++i) cycle += *i + " → ";
            cycle += node;
            diag(ctx, Token{}, "🔗 Tangled molecule — reference cycle: " + cycle +
                ". Use `borrow<T>` for non-owning back-references.", "E504");
            return true;
        }
        if (visited.count(node)) return false;
        visiting.insert(node);
        path.push_back(node);
        auto it = graph.find(node);
        if (it != graph.end())
            for (const auto& nb : it->second)
                if (dfs(nb, path)) return true;
        path.pop_back();
        visiting.erase(node);
        visited.insert(node);
        return false;
    };

    for (auto& [name, _] : graph) {
        std::vector<std::string> path;
        if (dfs(name, path)) break;  // Report first cycle only.
    }
}

// ============================================================================
// Entry point
// ============================================================================

bool Chaperone::run(const std::vector<std::shared_ptr<Stmt>>& program,
                    const TypeChecker& tc, ErrorHandler& eh)
{
    Context ctx(tc, eh);
    collectTrackedTypes(ctx, program);
    detectCycles(ctx, program);

    // G1: module-level (global) tracked allocations. They have no enclosing
    // scope to drop them in, so a tracked global leaks by construction — the
    // programmer must manage it manually (future: @manual). Report E501 so it's
    // not invisible. (Top-level lets inside the program vector only — class
    // fields are handled by cascade-drops, not here.)
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
            if (isTrackedVar(ctx, *var)) {
                diag(ctx, var->name,
                    "🧬 Unfolded molecule — `" + var->name.lexeme + "` is a tracked "
                    "allocation at module scope, which has no scope to drop it in. "
                    "It leaks by construction; manage it manually (future: @manual).",
                    "E501");
            }
        }
    }

    // Collect every analyzable function (top-level + class methods) into a
    // flat list. The same FuncStmt can be analyzed multiple times across the
    // fixed-point; we hold raw pointers (the program vector owns the storage).
    std::vector<const FuncStmt*> functions;
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* func = dynamic_cast<const FuncStmt*>(stmt.get())) {
            if (func->body) functions.push_back(func);
        } else if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            for (const auto& member : cls->members) {
                if (auto* mm = dynamic_cast<const MethodMember*>(member.get())) {
                    if (mm->declaration && mm->declaration->body)
                        functions.push_back(mm->declaration.get());
                }
            }
        }
    }

    // Interprocedural fixed point (S4). Iterate analyzeFunction until no
    // summary changes, so forward references and (mutual) recursion converge.
    // Diagnostics are suppressed during convergence and emitted only on the
    // final pass, to avoid duplicates. A pass cap guards against divergence
    // (the design doc's convergence-fallback); on non-convergence the last
    // pass's summaries stand and we still report.
    const int MAX_PASSES = 8;
    ctx.suppress_diag = true;
    for (int pass = 0; pass < MAX_PASSES; pass++) {
        auto before = ctx.summaries;  // snapshot
        for (const auto* fn : functions)
            analyzeFunction(ctx, *fn);
        if (ctx.summaries == before) break;  // converged
    }
    ctx.suppress_diag = false;
    // Final diagnostic pass with the converged summaries.
    for (const auto* fn : functions)
        analyzeFunction(ctx, *fn);

    return eh.errorCount() == 0;
}

} // namespace angara
