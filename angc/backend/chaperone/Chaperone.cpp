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

void Chaperone::warn(Context& ctx, const Token& tok,
                     const std::string& msg, const std::string& code) {
    if (ctx.suppress_diag) return;
    ctx.eh.warning(tok, msg, code);
}

// ============================================================================
// State machine helpers
// ============================================================================

Chaperone::State Chaperone::join(State a, State b) {
    if (a == b) return a;
    if (a == State::Live || b == State::Live) return State::Live;
    // Both are "gone" states (Dropped/Escaped/Moved/Uninit in some combination).
    // Conservatively return Dropped — the variable is not available. The
    // diagnostic in the DropStmt handler covers all "gone" reasons (M3).
    return State::Dropped;
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
    // --- Pass 1: collect tracked types and explicit annotations ---
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            ctx.tracked_types.insert(cls->name.lexeme);
            if (cls->is_sendable && !cls->is_unsendable)
                ctx.sendable_types.insert(cls->name.lexeme);
            if (cls->is_sync && !cls->is_unsync)
                ctx.syncable_types.insert(cls->name.lexeme);
        } else if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
            if (data->is_owned) {
                ctx.tracked_types.insert(data->name.lexeme);
                if (data->is_sendable && !data->is_unsendable)
                    ctx.sendable_types.insert(data->name.lexeme);
                if (data->is_sync && !data->is_unsync)
                    ctx.syncable_types.insert(data->name.lexeme);
            } else {
                // M11: non-owned data types are implicitly sendable and syncable.
                ctx.sendable_types.insert(data->name.lexeme);
                ctx.syncable_types.insert(data->name.lexeme);
            }
        }
    }

    // --- Pass 2: auto-derive Send and Sync via fixed-point iteration ---
    // Inherently sendable/syncable types (primitives and built-ins).
    auto isInherentlySendable = [&](const std::string& name) -> bool {
        return name == "i64" || name == "f64" || name == "string" ||
               name == "bool" || name == "char" ||
               name == "Mutex" || name == "Thread" ||
               ctx.sendable_types.count(name);  // already registered
    };
    auto isInherentlySyncable = [&](const std::string& name) -> bool {
        return name == "i64" || name == "f64" || name == "string" ||
               name == "bool" || name == "char" ||
               name == "Mutex" ||
               ctx.syncable_types.count(name);  // already registered
    };

    // Extract base type name from a field's AST type annotation.
    // (same approach as detectCycles)
    std::function<std::string(const ASTType*)> base_name = [&](const ASTType* t) -> std::string {
        if (!t) return "";
        if (auto* s = dynamic_cast<const SimpleType*>(t)) return s->name.lexeme;
        if (auto* g = dynamic_cast<const GenericType*>(t)) return g->name.lexeme;
        if (auto* o = dynamic_cast<const OptionalTypeNode*>(t)) return base_name(o->base_type.get());
        if (auto* ow = dynamic_cast<const OwnedTypeNode*>(t)) return base_name(ow->inner_type.get());
        return "";
    };

    // Collects the tracked field type names for a class or owned data type.
    // For Send: only owned (tracked) field types matter.
    // For Sync: all field types matter (including ref<T> — T must be Sync).
    struct FieldInfo {
        std::set<std::string> owned_fields;   // tracked field type names (for Send)
        std::set<std::string> all_fields;     // all field type names (for Sync)
    };
    std::map<std::string, FieldInfo> type_fields;
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
            if (!data->is_owned) continue;
            FieldInfo& fi = type_fields[data->name.lexeme];
            for (const auto& f : data->fields) {
                if (!f || !f->typeAnnotation) continue;
                std::string ft = base_name(f->typeAnnotation.get());
                if (ft.empty()) continue;
                fi.all_fields.insert(ft);
                if (ctx.tracked_types.count(ft))
                    fi.owned_fields.insert(ft);
            }
        } else if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            FieldInfo& fi = type_fields[cls->name.lexeme];
            for (const auto& member : cls->members) {
                if (auto* fm = dynamic_cast<const FieldMember*>(member.get())) {
                    if (!fm->declaration || !fm->declaration->typeAnnotation) continue;
                    std::string ft = base_name(fm->declaration->typeAnnotation.get());
                    if (ft.empty()) continue;
                    fi.all_fields.insert(ft);
                    if (ctx.tracked_types.count(ft))
                        fi.owned_fields.insert(ft);
                }
            }
        }
    }

    // Fixed-point auto-derivation for Send.
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& stmt : program) {
            if (!stmt) continue;
            std::string type_name;
            bool is_unsendable = false;
            if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
                type_name = cls->name.lexeme;
                is_unsendable = cls->is_unsendable;
            } else if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
                if (!data->is_owned) continue;
                type_name = data->name.lexeme;
                is_unsendable = data->is_unsendable;
            } else continue;

            if (is_unsendable || ctx.sendable_types.count(type_name)) continue;

            auto fi = type_fields.find(type_name);
            if (fi == type_fields.end()) {
                // No fields → vacuously Send.
                ctx.sendable_types.insert(type_name);
                changed = true;
                continue;
            }
            bool all_sendable = true;
            for (const auto& ft : fi->second.owned_fields) {
                if (!isInherentlySendable(ft)) { all_sendable = false; break; }
            }
            if (all_sendable) {
                ctx.sendable_types.insert(type_name);
                changed = true;
            }
        }
    }

    // Fixed-point auto-derivation for Sync.
    changed = true;
    while (changed) {
        changed = false;
        for (const auto& stmt : program) {
            if (!stmt) continue;
            std::string type_name;
            bool is_unsync = false;
            if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
                type_name = cls->name.lexeme;
                is_unsync = cls->is_unsync;
            } else if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
                if (!data->is_owned) continue;
                type_name = data->name.lexeme;
                is_unsync = data->is_unsync;
            } else continue;

            if (is_unsync || ctx.syncable_types.count(type_name)) continue;

            auto fi = type_fields.find(type_name);
            if (fi == type_fields.end()) {
                // No fields → vacuously Sync.
                ctx.syncable_types.insert(type_name);
                changed = true;
                continue;
            }
            // For Sync, ALL fields must be Sync (including ref<T> fields).
            bool all_syncable = true;
            for (const auto& ft : fi->second.all_fields) {
                if (!isInherentlySyncable(ft)) { all_syncable = false; break; }
            }
            if (all_syncable) {
                ctx.syncable_types.insert(type_name);
                changed = true;
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

// Internal: check whether a resolved (non-optional, non-ref) Type is a
// built-in heap-allocated subtype (string, list, record, closure, etc.).
bool Chaperone::isBuiltinHeapType(const Type& t) {
    // Unwrap optionals: string? is a built-in heap type iff string is.
    const Type* inner = &t;
    if (inner->kind == TypeKind::OPTIONAL) {
        auto ot = dynamic_cast<const OptionalType*>(inner);
        if (!ot || !ot->wrapped_type) return false;
        inner = ot->wrapped_type.get();
    }
    switch (inner->kind) {
        case TypeKind::LIST:
        case TypeKind::RECORD:
        case TypeKind::EXCEPTION:
        case TypeKind::THREAD:
        case TypeKind::MUTEX:
        case TypeKind::TRAIT_OBJECT:
        case TypeKind::RAW_ARRAY:
        case TypeKind::VECTOR:
        case TypeKind::FUTURE:  // LIB-4: Future<T> is a heap-allocated owned type
            return true;
        case TypeKind::PRIMITIVE:
            // string is the only heap-allocated primitive
            return inner->toString() == "string";
        case TypeKind::FUNCTION:
            // Closures and bound methods are FUNCTION-kind heap objects.
            return true;
        default:
            return false;
    }
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
    // Phase B: track built-in heap-allocated types (string, list, record, etc.)
    // so the Chaperone can detect leaks.  Leak diagnostics for built-in types
    // are emitted as W521 warnings (not E501 errors) to avoid breaking existing
    // code; class / owned-data leaks remain hard errors.
    if (isBuiltinHeapType(*t)) return true;
    return false;
}

bool Chaperone::isHeapAllocatedType(Context& ctx, const Type& type) {
    // All tracked types are heap-allocated. After Phase B, this covers
    // class, owned data, and all built-in heap types — so the function
    // is now equivalent to isTrackedTypeObj.
    return isTrackedTypeObj(ctx, type);
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
    if (auto* dr = dynamic_cast<const DropStmt*>(stmt.get())) {
        // H8: walk the drop target expression to collect variable references.
        // For `drop x` this captures `x`; for `drop this.field` this captures
        // nothing new (`this` is implicit); for `drop obj.field` this captures `obj`.
        collectExprVarRefs(dr->target, out);
        return;
    }
    // H1: TryStmt was previously unhandled — closures inside try/catch/finally
    // had invisible captures. Recurse into try body, catch body, and finally body.
    if (auto* tr = dynamic_cast<const TryStmt*>(stmt.get())) {
        collectVarRefs(tr->tryBlock, out);
        if (tr->catchBlock) collectVarRefs(tr->catchBlock, out);
        if (tr->finallyBlock) collectVarRefs(tr->finallyBlock, out);
        return;
    }
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
    // Phase B: built-in types (string, list, etc.) get W521 warnings instead
    // of E501 errors.
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
            if (isTrackedVar(ctx, *var)) {
                bool is_builtin = false;
                auto& types = ctx.tc.getVariableTypes();
                auto tit = types.find(var);
                if (tit != types.end() && tit->second &&
                    isBuiltinHeapType(*tit->second))
                    is_builtin = true;
                auto msg = "🧬 Unfolded molecule — `" + var->name.lexeme + "` is a tracked "
                           "allocation at module scope, which has no scope to drop it in. "
                           "It leaks by construction; manage it manually (future: @manual).";
                if (is_builtin)
                    warn(ctx, var->name, msg, "W521");
                else
                    diag(ctx, var->name, msg, "E501");
            }
        }
    }

    // Collect every analyzable function (top-level + class methods) into a
    // flat list. The same FuncStmt can be analyzed multiple times across the
    // fixed-point; we hold raw pointers (the program vector owns the storage).
    // C3: store a qualified summary key alongside each function so method
    // summaries don't collide (e.g., "Buf.init" vs "Conn.init").
    struct FuncInfo {
        const FuncStmt* func;
        std::string summary_key;  // qualified: "funcName" or "ClassName.methodName"
    };
    std::vector<FuncInfo> functions;
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* func = dynamic_cast<const FuncStmt*>(stmt.get())) {
            if (func->body) functions.push_back({func, func->name.lexeme});
        } else if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            for (const auto& member : cls->members) {
                if (auto* mm = dynamic_cast<const MethodMember*>(member.get())) {
                    if (mm->declaration && mm->declaration->body)
                        functions.push_back({mm->declaration.get(),
                            cls->name.lexeme + "." + mm->declaration->name.lexeme});
                }
            }
        }
    }

    // Build summaries for functions with @consumes / @escape annotations
    // (typically foreign functions without bodies). These summaries are
    // static — they don't change across fixed-point iterations — so we
    // insert them once before the loop.
    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* func = dynamic_cast<const FuncStmt*>(stmt.get())) {
            if (func->consumes_params.empty() && func->escape_params.empty())
                continue;
            FunctionSummary summary(func->params.size(), ParamBehavior::Borrowed);
            for (int idx : func->consumes_params) {
                if (idx >= 0 && static_cast<size_t>(idx) < func->params.size())
                    summary[idx] = ParamBehavior::Dropped;
            }
            for (int idx : func->escape_params) {
                if (idx >= 0 && static_cast<size_t>(idx) < func->params.size())
                    summary[idx] = ParamBehavior::Escaped;
            }
            ctx.summaries[func->name.lexeme] = std::move(summary);
        }
    }

    // Interprocedural fixed point (S4). Iterate analyzeFunction until no
    // summary changes, so forward references and (mutual) recursion converge.
    // Diagnostics are suppressed during convergence and emitted only on the
    // final pass, to avoid duplicates. A pass cap guards against divergence
    // (the design doc's convergence-fallback); on non-convergence the last
    // pass's summaries stand and we still report.
    //
    // H4: detect oscillation. If the summary map repeats a state we've seen
    // before (not counting the immediate previous pass, which is convergence),
    // the fixed-point is oscillating and we break with a warning.
    const int MAX_PASSES = 8;
    ctx.suppress_diag = true;
    std::vector<size_t> seen_hashes;  // H4: track summary hashes for oscillation
    for (int pass = 0; pass < MAX_PASSES; pass++) {
        auto before = ctx.summaries;  // snapshot
        for (const auto& fi : functions)
            analyzeFunction(ctx, *fi.func, fi.summary_key);
        if (ctx.summaries == before) break;  // converged

        // H4: check for oscillation (a cycle longer than period 1).
        size_t h = 0;
        for (const auto& [k, v] : ctx.summaries) {
            h ^= std::hash<std::string>{}(k);
            for (auto pb : v) h = (h << 1) ^ static_cast<size_t>(pb);
        }
        auto osc_it = std::find(seen_hashes.begin(), seen_hashes.end(), h);
        if (osc_it != seen_hashes.end() && (seen_hashes.size() < 2 || osc_it != seen_hashes.end() - 1)) {
            // Same hash as a non-immediately-previous pass — oscillation detected.
            ctx.suppress_diag = false;
            diag(ctx, functions[0].func->name,
                "⚠️ Interprocedural fixed-point is oscillating — summaries did not "
                "converge. The last pass's summaries are used; results may be "
                "imprecise. Consider simplifying ownership patterns or adding "
                "@consumes / @escape annotations.",
                "W521");
            ctx.suppress_diag = true;
            break;
        }
        seen_hashes.push_back(h);
    }
    ctx.suppress_diag = false;
    // Final diagnostic pass with the converged summaries.
    for (const auto& fi : functions)
        analyzeFunction(ctx, *fi.func, fi.summary_key);

    return eh.errorCount() == 0;
}

} // namespace angara
