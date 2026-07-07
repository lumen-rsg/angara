#include "Chaperone.h"
#include "Stmt.h"
#include "Expr.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include "Token.h"
#include "Type.h"
#include "SymbolTable.h"

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
    // C1: "gone" states must dominate. If a variable is dead (Escaped/Moved/
    // Dropped) on either branch, the merged state is conservatively that dead
    // state. Previously "Live" dominated, which was unsound — a variable that
    // escaped on one branch was treated as still Live after the merge.
    if (a == State::Escaped || b == State::Escaped) return State::Escaped;
    if (a == State::Moved   || b == State::Moved)   return State::Moved;
    if (a == State::Dropped || b == State::Dropped) return State::Dropped;
    if (a == State::Live    || b == State::Live)    return State::Live;
    // Both are Uninit (the only remaining combination).
    return State::Uninit;
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
    // --- Pass 1: collect tracked types from AST ---
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

    // --- Pass 2: read Send/Sync status from TypeChecker semantic types ---
    // The TypeChecker already auto-derives is_sendable/is_sync on ClassType
    // and DataType during its deriveSendAndSync() pass. We just read those
    // flags here instead of duplicating the derivation logic.
    auto& sym_table = ctx.tc.getSymbolTable();  // L2: non-const, resolve() marks symbols used
    for (const auto& stmt : program) {
        if (!stmt) continue;
        std::string type_name;
        if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            type_name = cls->name.lexeme;
        } else if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
            type_name = data->name.lexeme;
        } else continue;

        auto sym = sym_table.resolve(type_name);
        if (!sym || !sym->type) continue;

        bool is_sendable = false;
        bool is_sync = false;
        if (sym->type->kind == TypeKind::CLASS) {
            auto cls_type = std::dynamic_pointer_cast<ClassType>(sym->type);
            if (cls_type) {
                is_sendable = cls_type->is_sendable;
                is_sync = cls_type->is_sync;
            }
        } else if (sym->type->kind == TypeKind::DATA) {
            auto data_type = std::dynamic_pointer_cast<DataType>(sym->type);
            if (data_type) {
                is_sendable = data_type->is_sendable;
                is_sync = data_type->is_sync;
            }
        }
        if (is_sendable) ctx.sendable_types.insert(type_name);
        if (is_sync) ctx.syncable_types.insert(type_name);
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
            // L21: closures and bound methods are NOT tracked as heap objects.
            // While closures are heap-allocated at runtime, they don't hold
            // tracked resources — captured variables are tracked through the
            // captured variables' own state. Treating FUNCTION as a heap type
            // would cause `let g = f` to be an ownership move (S1), breaking
            // closure aliasing.
            return false;
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
	// M13: Generic type parameters are always boxed at runtime (trait-object ABI),
	// so they are conservatively treated as tracked. This is "bounded parametric
	// analysis" — we assume the worst case (T is a tracked heap type) and enforce
	// ownership rules accordingly. Since Angara does not monomorphize, this is
	// both sound and necessary: every generic parameter IS boxed at runtime.
	if (t->kind == TypeKind::TYPE_PARAM) return true;
	// M13: Generic instances (e.g., Box<i64>) are tracked if their base type
	// (e.g., Box) is tracked. Unwrap the GenericInstanceType and recurse.
	if (t->kind == TypeKind::GENERIC_INSTANCE) {
		auto* gi = dynamic_cast<const GenericInstanceType*>(t);
		if (gi && gi->base_type) return isTrackedTypeObj(ctx, *gi->base_type);
		return false;
	}
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
// L21: deferred closure escape analysis.
// ============================================================================

bool Chaperone::checkPendingCaptures(Context& ctx, const std::string& var_name,
                                      StateMap& state, const Token& tok)
{
    // Scan all pending closures for any that capture this variable.
    for (auto& [ft, holders] : ctx.closure_holders) {
        if (holders.empty()) continue;
        auto cap_it = ctx.closure_captures.find(ft);
        if (cap_it == ctx.closure_captures.end()) continue;
        if (!cap_it->second.count(var_name)) continue;

        // Build a human-readable list of holder variable names.
        std::string holder_list;
        for (const auto& h : holders) {
            if (!holder_list.empty()) holder_list += ", ";
            holder_list += "`" + h + "`";
        }

        diag(ctx, tok,
            "\U0001f9ec Escaped molecule \u2014 `" + var_name + "` is captured by "
            "a closure held in " + holder_list + " which may still reference it. "
            "Call the closure before dropping this value, or drop the captured "
            "value via the closure's lifecycle.",
            "E505");

        // Don't erase from closure_captures — the capture set is immutable.
        // Pending status is tracked by closure_holders; when all holders are
        // removed (call/scope exit), the closure is no longer pending.
        // Erasing here would break interprocedural convergence passes where
        // suppress_diag is true: the capture would be gone on the next pass.
        return true;
    }
    return false;
}

// ============================================================================
// Phase 4: Cycle detection
// ============================================================================

void Chaperone::detectCycles(Context& ctx,
    const std::vector<std::shared_ptr<Stmt>>& program)
{
    std::map<std::string, std::set<std::string>> graph;
    // M6: map (owner, target) → Token of the field that creates the edge.
    std::map<std::pair<std::string, std::string>, Token> edge_tokens;


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

    // L22: Collect tracked type names from a field type annotation, recursing
    // into generic type arguments.  For `list<B>` where B is tracked, this
    // adds edge A → B even though `list` itself is not a tracked type.
    // Without this, container-mediated cycles (A → list<B> → A) are missed.
    std::function<void(const ASTType*, std::set<std::string>&)> collect_tracked;
    collect_tracked = [&](const ASTType* t, std::set<std::string>& out) {
        if (!t) return;
        if (auto* g = dynamic_cast<const GenericType*>(t)) {
            std::string bn = g->name.lexeme;
            if (!bn.empty() && ctx.tracked_types.count(bn))
                out.insert(bn);
            for (const auto& arg : g->arguments)
                collect_tracked(arg.get(), out);
        } else if (auto* o = dynamic_cast<const OptionalTypeNode*>(t)) {
            collect_tracked(o->base_type.get(), out);
        } else if (auto* ow = dynamic_cast<const OwnedTypeNode*>(t)) {
            collect_tracked(ow->inner_type.get(), out);
        } else {
            std::string bn = base_name(t);
            if (!bn.empty() && ctx.tracked_types.count(bn))
                out.insert(bn);
        }
    };

    auto check_field = [&](const std::string& owner, const VarDeclStmt* field) {
        if (!field || !field->typeAnnotation) return;
        std::set<std::string> tracked;
        collect_tracked(field->typeAnnotation.get(), tracked);
        for (const auto& ft : tracked) {
            graph[owner].insert(ft);
            // M6: store the field's token for cycle diagnostics.
            edge_tokens[{owner, ft}] = field->name;
        }
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
            // M6: use the token from the edge that closes the cycle, if available.
            Token edge_tok = Token{};
            if (path.size() >= 2) {
                std::string prev = path.back();
                auto et_it = edge_tokens.find({prev, node});
                if (et_it != edge_tokens.end()) edge_tok = et_it->second;
            }
            diag(ctx, edge_tok, "🔗 Tangled molecule — reference cycle: " + cycle +
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
                    TypeChecker& tc, ErrorHandler& eh)
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
    bool converged = false;
    for (int pass = 0; pass < MAX_PASSES; pass++) {
        auto before = ctx.summaries;  // snapshot
        auto before_closures = ctx.closure_summaries;  // M1: also snapshot closure summaries
        for (const auto& fi : functions)
            analyzeFunction(ctx, *fi.func, fi.summary_key);
        // M1: also check closure_summaries for convergence
        if (ctx.summaries == before && ctx.closure_summaries == before_closures) {
            converged = true; break;
        }

        // H4: check for oscillation (a cycle longer than period 1).
        // L1: Use a proper hash-combine instead of simple XOR+shift which
        // loses bits and is prone to collisions.
        size_t h = 0;
        auto hash_combine = [&h](size_t val) {
            h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        for (const auto& [k, v] : ctx.summaries) {
            hash_combine(std::hash<std::string>{}(k));
            for (auto pb : v) hash_combine(static_cast<size_t>(pb));
        }
        // M1: also hash closure summaries
        for (const auto& [k, v] : ctx.closure_summaries) {
            hash_combine(reinterpret_cast<size_t>(k));
            for (auto pb : v) hash_combine(static_cast<size_t>(pb));
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
    if (!converged) {
        // M7: warn when the fixed-point cap is reached without convergence.
        diag(ctx, functions[0].func->name,
            "⚠️ Interprocedural fixed-point did not converge after " +
            std::to_string(MAX_PASSES) + " passes. The current summaries "
            "are used; results may be imprecise. Consider simplifying "
            "ownership patterns or adding @consumes / @escape annotations.",
            "W520");
    }
    ctx.suppress_diag = false;
    // Final diagnostic pass with the converged summaries.
    for (const auto& fi : functions)
        analyzeFunction(ctx, *fi.func, fi.summary_key);

    return eh.errorCount() == 0;
}

} // namespace angara
