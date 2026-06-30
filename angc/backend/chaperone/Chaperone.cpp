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

// Expression half of collectVarRefs.
void Chaperone::collectExprVarRefs(const std::shared_ptr<Expr>& expr,
    std::set<std::string>& out)
{
    if (!expr) return;
    if (auto* ve = dynamic_cast<const VarExpr*>(expr.get())) { out.insert(ve->name.lexeme); return; }
    if (auto* b = dynamic_cast<const Binary*>(expr.get())) { collectExprVarRefs(b->left, out); collectExprVarRefs(b->right, out); return; }
    if (auto* u = dynamic_cast<const Unary*>(expr.get())) { collectExprVarRefs(u->right, out); return; }
    if (auto* g = dynamic_cast<const Grouping*>(expr.get())) { collectExprVarRefs(g->expression, out); return; }
    if (auto* a = dynamic_cast<const AssignExpr*>(expr.get())) { collectExprVarRefs(a->target, out); collectExprVarRefs(a->value, out); return; }
    if (auto* upd = dynamic_cast<const UpdateExpr*>(expr.get())) { collectExprVarRefs(upd->target, out); return; }
    if (auto* c = dynamic_cast<const CallExpr*>(expr.get())) {
        collectExprVarRefs(c->callee, out);
        for (const auto& arg : c->arguments) collectExprVarRefs(arg, out);
        return;
    }
    if (auto* gt = dynamic_cast<const GetExpr*>(expr.get())) { collectExprVarRefs(gt->object, out); return; }
    if (auto* l = dynamic_cast<const ListExpr*>(expr.get())) { for (const auto& e : l->elements) collectExprVarRefs(e, out); return; }
    if (auto* lo = dynamic_cast<const LogicalExpr*>(expr.get())) { collectExprVarRefs(lo->left, out); collectExprVarRefs(lo->right, out); return; }
    if (auto* su = dynamic_cast<const SubscriptExpr*>(expr.get())) { collectExprVarRefs(su->object, out); collectExprVarRefs(su->index, out); return; }
    if (auto* re = dynamic_cast<const RecordExpr*>(expr.get())) { for (const auto& v : re->values) collectExprVarRefs(v, out); return; }
    if (auto* te = dynamic_cast<const TernaryExpr*>(expr.get())) { collectExprVarRefs(te->condition, out); collectExprVarRefs(te->thenBranch, out); collectExprVarRefs(te->elseBranch, out); return; }
    if (auto* ie = dynamic_cast<const IsExpr*>(expr.get())) { collectExprVarRefs(ie->object, out); return; }
    if (auto* ce = dynamic_cast<const CastExpr*>(expr.get())) { collectExprVarRefs(ce->object, out); return; }
    if (auto* de = dynamic_cast<const DerefExpr*>(expr.get())) { collectExprVarRefs(de->right, out); return; }
    if (auto* me = dynamic_cast<const MatchExpr*>(expr.get())) {
        collectExprVarRefs(me->condition, out);
        for (const auto& cs : me->cases) if (cs.body) collectExprVarRefs(cs.body, out);
        return;
    }
}

// E505 helper: if `elem` is a bare tracked Live variable, it is escaping into
// an untracked container (list/record literal). Report and transition to
// Escaped so later drops aren't double-frees and the leak is named.
void Chaperone::checkEscapeIntoContainer(Context& ctx,
    const std::shared_ptr<Expr>& elem, StateMap& state)
{
    auto* ve = dynamic_cast<const VarExpr*>(elem.get());
    if (!ve) return;
    auto it = state.find(ve->name.lexeme);
    if (it != state.end() && it->second == State::Live) {
        diag(ctx, ve->name,
            "🧬 Escaped molecule — `" + ve->name.lexeme + "` (a tracked "
            "allocation) is stored into an untracked container. The container "
            "isn't tracked, so the allocation can't be released correctly. Use "
            "an owning container or `@escape` to transfer ownership intentionally.",
            "E505");
        it->second = State::Escaped;
    }
}

void Chaperone::analyzeExpr(Context& ctx,
    const std::shared_ptr<Expr>& expr, StateMap& state)
{
    if (!expr) return;

    // VarExpr: the key checks — use-after-free (Dropped) and use-after-move (Moved).
    if (auto* ve = dynamic_cast<const VarExpr*>(expr.get())) {
        auto it = state.find(ve->name.lexeme);
        if (it != state.end()) {
            if (it->second == State::Dropped) {
                diag(ctx, ve->name,
                    "💀 Dead reference — `" + ve->name.lexeme + "` was dropped but "
                    "is used here. The molecule has already been released.",
                    "E502");
            } else if (it->second == State::Moved) {
                diag(ctx, ve->name,
                    "📤 Moved molecule — `" + ve->name.lexeme + "` had its ownership "
                    "transferred to another variable and is used here. Read the new "
                    "owner instead.",
                    "E507");
            }
        }
        return;
    }

    // Binary: walk both sides.
    if (auto* bin = dynamic_cast<const Binary*>(expr.get())) {
        analyzeExpr(ctx, bin->left, state);
        analyzeExpr(ctx, bin->right, state);
        return;
    }

    // Unary: walk operand.
    if (auto* un = dynamic_cast<const Unary*>(expr.get())) {
        analyzeExpr(ctx, un->right, state);
        return;
    }

    // Grouping: unwrap.
    if (auto* grp = dynamic_cast<const Grouping*>(expr.get())) {
        analyzeExpr(ctx, grp->expression, state);
        return;
    }

    // AssignExpr: handle leaks (S7) and ownership moves (S1).
    if (auto* asgn = dynamic_cast<const AssignExpr*>(expr.get())) {
        // Capture the move source BEFORE walking the value: if the RHS is a
        // bare tracked variable, its current state tells us if this is a move.
        bool rhs_is_move_source = false;
        std::string move_src;
        if (asgn->op.type == TokenType::EQUAL) {
            if (auto* vve = dynamic_cast<const VarExpr*>(asgn->value.get())) {
                auto sit = state.find(vve->name.lexeme);
                if (sit != state.end() && sit->second == State::Live) {
                    rhs_is_move_source = true;
                    move_src = vve->name.lexeme;
                }
            }
        }

        // Walk the RHS (catches use-after-free / use-after-move inside it).
        analyzeExpr(ctx, asgn->value, state);

        // Only a plain `=` can transfer whole-variable ownership. `+=` etc.
        // keep the target's allocation (compound assign).
        if (asgn->op.type != TokenType::EQUAL) return;

        // --- Target is a bare variable (let/assign): whole-variable ownership ---
        if (auto* tgt = dynamic_cast<const VarExpr*>(asgn->target.get())) {
            auto tit = state.find(tgt->name.lexeme);
            if (tit == state.end()) return;

            // S7: overwriting a Live tracked variable leaks the old allocation.
            // (Moved/Dropped/Escaped targets are already invalid — no leak.)
            if (tit->second == State::Live) {
                diag(ctx, tgt->name,
                    "🧬 Unfolded molecule — `" + tgt->name.lexeme + "` held a live "
                    "allocation that is overwritten by this assignment without being "
                    "dropped. Add `drop " + tgt->name.lexeme + ";` first.",
                    "E501");
            }

            // S1 move: if the RHS was a tracked Live var, ownership transfers —
            // the source becomes Moved (invalid), the target becomes Live.
            if (rhs_is_move_source) {
                state[move_src] = State::Moved;
                tit->second = State::Live;
            }
            return;
        }

        // --- Target is a field (this.f = ...): S6 field ownership move ---
        // External field writes are already blocked by the type checker (private
        // fields, E336), so this only fires inside methods/constructors. The
        // sound rule is unique_ptr field semantics: assigning a tracked Live
        // variable to a field MOVES its ownership into the field — the source
        // becomes invalid (E507 on later use). This prevents the double-free
        // where both the field's owning object (cascade-drop) and the source
        // would free the same allocation. (We don't track per-field state, so
        // the field's previous value leaking is out of scope — addressed with
        // field-state tracking later.)
        if (auto* get = dynamic_cast<const GetExpr*>(asgn->target.get())) {
            analyzeExpr(ctx, get->object, state);   // catch UAF on the object
            if (rhs_is_move_source) {
                state[move_src] = State::Moved;
            }
            return;
        }
    }

    // UpdateExpr (x++, ++x): walk the target.
    if (auto* upd = dynamic_cast<const UpdateExpr*>(expr.get())) {
        analyzeExpr(ctx, upd->target, state);
        return;
    }

    // CallExpr: walk callee + all arguments, then transition tracked args
    // based on the callee's interprocedural summary.
    if (auto* call = dynamic_cast<const CallExpr*>(expr.get())) {
        analyzeExpr(ctx, call->callee, state);
        for (const auto& arg : call->arguments)
            analyzeExpr(ctx, arg, state);

        // Interprocedural: determine the callee name.
        // For method calls (o.m()), summaries are keyed by the bare method
        // name (m), so try that; a top-level fn call uses the variable name.
        std::string callee_name;
        std::string method_name;  // fallback for method calls
        if (auto* ve = dynamic_cast<const VarExpr*>(call->callee.get())) {
            callee_name = ve->name.lexeme;
        } else if (auto* get = dynamic_cast<const GetExpr*>(call->callee.get())) {
            method_name = get->name.lexeme;  // e.g. "set"
            if (auto* ve2 = dynamic_cast<const VarExpr*>(get->object.get()))
                callee_name = ve2->name.lexeme + "." + get->name.lexeme;  // "o.set"
            else
                callee_name = method_name;
        }

        // Look up the summary. For method calls, prefer the bare method name.
        auto sum_it = ctx.summaries.find(callee_name);
        if (sum_it == ctx.summaries.end() && !method_name.empty())
            sum_it = ctx.summaries.find(method_name);
        if (sum_it != ctx.summaries.end()) {
            // Positional match: call-arg i binds to summary[i] (S5). The summary
            // is aligned to call-arg positions (`this` is not a call arg), so a
            // direct index is correct for any arity — no more "single tracked
            // param" shortcut or blanket-Escape fallback.
            const auto& summary = sum_it->second;
            for (size_t i = 0; i < call->arguments.size() && i < summary.size(); i++) {
                auto behavior = summary[i];
                if (behavior == ParamBehavior::Borrowed) continue;  // stays Live
                auto* arg = call->arguments[i].get();
                if (auto* ve3 = dynamic_cast<const VarExpr*>(arg)) {
                    auto st_it = state.find(ve3->name.lexeme);
                    if (st_it != state.end() && st_it->second == State::Live) {
                        if (behavior == ParamBehavior::Dropped)
                            st_it->second = State::Dropped;
                        else if (behavior == ParamBehavior::Escaped)
                            st_it->second = State::Escaped;
                    }
                }
            }
        } else {
            // Unknown function (foreign, module, or not yet analyzed):
            // default is BORROW — the argument stays Live. This is correct
            // for ~90% of FFI (io.println, string(), etc.). Functions that
            // consume or escape need @consumes / @escape annotations.
        }
        return;
    }

    // GetExpr (x.field): walk the object.
    if (auto* get = dynamic_cast<const GetExpr*>(expr.get())) {
        analyzeExpr(ctx, get->object, state);
        return;
    }

    // ListExpr: walk all elements. E505 — a tracked Live value placed into a
    // list literal escapes into an untracked container: the container isn't
    // tracked, so the value can't be dropped correctly (leak or double-free).
    if (auto* list = dynamic_cast<const ListExpr*>(expr.get())) {
        for (const auto& elem : list->elements) {
            analyzeExpr(ctx, elem, state);
            checkEscapeIntoContainer(ctx, elem, state);
        }
        return;
    }

    // LogicalExpr (&&, ||): walk both sides.
    if (auto* log = dynamic_cast<const LogicalExpr*>(expr.get())) {
        analyzeExpr(ctx, log->left, state);
        analyzeExpr(ctx, log->right, state);
        return;
    }

    // SubscriptExpr (xs[i]): walk both.
    if (auto* sub = dynamic_cast<const SubscriptExpr*>(expr.get())) {
        analyzeExpr(ctx, sub->object, state);
        analyzeExpr(ctx, sub->index, state);
        return;
    }

    // RecordExpr: walk all values.
    if (auto* rec = dynamic_cast<const RecordExpr*>(expr.get())) {
        for (const auto& val : rec->values) {
            analyzeExpr(ctx, val, state);
            checkEscapeIntoContainer(ctx, val, state);   // E505
        }
        return;
    }

    // TernaryExpr: walk all three.
    if (auto* tern = dynamic_cast<const TernaryExpr*>(expr.get())) {
        analyzeExpr(ctx, tern->condition, state);
        analyzeExpr(ctx, tern->thenBranch, state);
        analyzeExpr(ctx, tern->elseBranch, state);
        return;
    }

    // IsExpr: walk the object.
    if (auto* is = dynamic_cast<const IsExpr*>(expr.get())) {
        analyzeExpr(ctx, is->object, state);
        return;
    }

    // CastExpr: walk the object.
    if (auto* cast = dynamic_cast<const CastExpr*>(expr.get())) {
        analyzeExpr(ctx, cast->object, state);
        return;
    }

    // DerefExpr: walk the right.
    if (auto* deref = dynamic_cast<const DerefExpr*>(expr.get())) {
        analyzeExpr(ctx, deref->right, state);
        return;
    }

    // MatchExpr: walk the condition + all case bodies.
    if (auto* match = dynamic_cast<const MatchExpr*>(expr.get())) {
        analyzeExpr(ctx, match->condition, state);
        for (const auto& cs : match->cases) {
            if (cs.body) analyzeExpr(ctx, cs.body, state);
        }
        return;
    }

    // LambdaExpr: a closure captures any tracked Live variable it references.
    // The closure may outlive the local, so a captured tracked value transitions
    // to Escaped (the closure holds the reference; dropping the local would
    // leave the closure dangling). Without this, use-after-free via a closure
    // that outlives its capture was invisible.
    if (auto* lam = dynamic_cast<const LambdaExpr*>(expr.get())) {
        std::set<std::string> referenced;
        for (const auto& s : lam->body)
            collectVarRefs(s, referenced);
        for (const auto& name : referenced) {
            auto it = state.find(name);
            if (it != state.end() && it->second == State::Live) {
                diag(ctx, lam->keyword,
                    "🧬 Escaped molecule — `" + name + "` is captured by a closure "
                    "that may outlive it. Ownership is treated as transferred; drop "
                    "the captured value via the closure's lifecycle, not the local.",
                    "E505");
                it->second = State::Escaped;
            }
        }
        return;
    }

    // Literal, ThisExpr, SuperExpr: no variable references to check.
}

// ============================================================================
// Phase 2: Per-function data-flow analysis
// ============================================================================

void Chaperone::analyzeFunction(Context& ctx, const FuncStmt& func) {
    ctx.current_function = func.name.lexeme;
    StateMap state;

    // Register tracked parameters. Prefer the resolved FunctionType from the
    // symbol table (top-level functions), but fall back to reading the param's
    // own type annotation — methods aren't resolvable by bare name at the top
    // level, so without the fallback their tracked params would never register.
    // Track per-param tracked-ness positionally (aligned to func.params, which
    // excludes `this` — `this` is parsed but not stored in params) so the
    // summary can be positional too (S5: correct multi-param matching).
    std::set<std::string> param_names;  // which names are params (not locals)
    std::vector<bool> param_tracked(func.params.size(), false);
    std::function<std::string(const ASTType*)> base_name = [&](const ASTType* t) -> std::string {
        if (!t) return "";
        if (auto* s = dynamic_cast<const SimpleType*>(t)) return s->name.lexeme;
        if (auto* g = dynamic_cast<const GenericType*>(t)) return g->name.lexeme;
        if (auto* o = dynamic_cast<const OptionalTypeNode*>(t)) return base_name(o->base_type.get());
        if (auto* ow = dynamic_cast<const OwnedTypeNode*>(t)) return base_name(ow->inner_type.get());
        return "";
    };
    auto sem_sym = const_cast<SymbolTable&>(ctx.tc.getSymbolTable()).resolve(func.name.lexeme);
    if (sem_sym && sem_sym->type && sem_sym->type->kind == TypeKind::FUNCTION) {
        auto fn_type = std::dynamic_pointer_cast<FunctionType>(sem_sym->type);
        for (size_t i = 0; i < fn_type->param_types.size() && i < func.params.size(); i++) {
            auto& pt = fn_type->param_types[i];
            if (!pt) continue;
            if (isTrackedTypeObj(ctx, *pt)) {
                state[func.params[i].name.lexeme] = State::Live;
                param_names.insert(func.params[i].name.lexeme);
                param_tracked[i] = true;
            }
        }
    } else {
        // Fallback: read each param's type annotation directly (methods).
        // base_name unwaps optionals/owned, so optional tracked params register.
        for (size_t i = 0; i < func.params.size(); i++) {
            std::string tn = base_name(func.params[i].type.get());
            if (!tn.empty() && ctx.tracked_types.count(tn)) {
                state[func.params[i].name.lexeme] = State::Live;
                param_names.insert(func.params[i].name.lexeme);
                param_tracked[i] = true;
            }
        }
    }

    bool terminates = false;
    if (func.body) {
        // analyzeBlock takes state by value and returns the threaded map;
        // capture the result so the body's effects (moves, drops) are visible
        // to the leak check and summary below. Discarding it left every param
        // stuck at its entry state, which corrupted the interprocedural summary.
        state = analyzeBlock(ctx, *func.body, state, terminates);
    }

    if (!terminates) {
        for (auto& [name, st] : state) {
            if (st == State::Live && param_names.count(name) == 0) {
                // Only report leaks for locals allocated inside this function,
                // not for borrowed parameters (the caller owns those).
                diag(ctx, func.name,
                    "🧬 Unfolded molecule — `" + name + "` is live when function `" +
                    func.name.lexeme + "` exits but was never dropped or returned. "
                    "Add `drop " + name + ";` before the function ends.",
                    "E501");
            }
        }
    }

    // Phase 4: build the positional function summary for interprocedural
    // analysis. summary[i] aligns to func.params[i] (call-arg positions —
    // `this` is not in params). Untracked params default to Borrowed so the
    // indices line up at the call site (arg i → summary[i]). A Moved param
    // means ownership transferred (e.g. into a field) → Escaped.
    FunctionSummary summary(func.params.size(), ParamBehavior::Borrowed);
    for (size_t i = 0; i < func.params.size(); i++) {
        if (!param_tracked[i]) continue;
        auto st_it = state.find(func.params[i].name.lexeme);
        if (st_it != state.end()) {
            if (st_it->second == State::Dropped)
                summary[i] = ParamBehavior::Dropped;
            else if (st_it->second == State::Escaped || st_it->second == State::Moved)
                summary[i] = ParamBehavior::Escaped;
            else
                summary[i] = ParamBehavior::Borrowed;
        }
    }
    ctx.summaries[func.name.lexeme] = std::move(summary);

    ctx.current_function.clear();
}

Chaperone::StateMap Chaperone::analyzeBlock(Context& ctx,
    const std::vector<std::shared_ptr<Stmt>>& statements,
    StateMap state, bool& terminates)
{
    terminates = false;
    for (const auto& stmt : statements) {
        if (!stmt) continue;
        analyzeStmt(ctx, stmt, state, terminates);
        if (terminates) break;
    }
    return state;
}

void Chaperone::analyzeStmt(Context& ctx,
    const std::shared_ptr<Stmt>& stmt, StateMap& state, bool& terminates)
{
    terminates = false;
    if (!stmt) return;

    // --- VarDeclStmt ---
    if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
        // Capture a move source BEFORE walking the initializer: if the
        // initializer is a bare tracked variable currently Live, `let x = y`
        // MOVES ownership from y to x (S1). Walking first would let the
        // initializer's own sub-expressions mutate state, so snapshot here.
        bool init_is_move_source = false;
        std::string move_src;
        if (var->initializer) {
            if (auto* vve = dynamic_cast<const VarExpr*>(var->initializer.get())) {
                auto sit = state.find(vve->name.lexeme);
                if (sit != state.end() && sit->second == State::Live) {
                    init_is_move_source = true;
                    move_src = vve->name.lexeme;
                }
            }
        }

        // Check the initializer expression for use-after-free / use-after-move.
        if (var->initializer)
            analyzeExpr(ctx, var->initializer, state);

        auto it = state.find(var->name.lexeme);
        if (it != state.end() && it->second == State::Live) {
            diag(ctx, var->name,
                "🧬 Unfolded molecule — `" + var->name.lexeme + "` held a live "
                "allocation that is now overwritten without being dropped.",
                "E501");
        }
        bool tracked = isTrackedVar(ctx, *var);
        state[var->name.lexeme] = tracked ? State::Live : State::Uninit;

        // S1 move: the source of a `let x = y` move is invalidated. Only when
        // the new binding is itself tracked (otherwise y is borrowed/copied,
        // e.g. `data` copy-on-assign — the source keeps its allocation).
        if (init_is_move_source && tracked) {
            state[move_src] = State::Moved;
        }
        return;
    }

    // --- DropStmt ---
    if (auto* drop = dynamic_cast<const DropStmt*>(stmt.get())) {
        auto it = state.find(drop->name.lexeme);
        if (it == state.end() || it->second == State::Uninit) {
            diag(ctx, drop->name,
                "⚠️ Cannot drop `" + drop->name.lexeme + "` — not a tracked allocation.",
                "E503");
        } else if (it->second == State::Dropped) {
            diag(ctx, drop->name,
                "⚠️ Double denaturation — `" + drop->name.lexeme + "` was already dropped.",
                "E503");
        } else if (it->second == State::Escaped) {
            diag(ctx, drop->name,
                "⚠️ Cannot drop `" + drop->name.lexeme + "` — ownership was transferred.",
                "E503");
        } else if (it->second == State::Moved) {
            diag(ctx, drop->name,
                "⚠️ Double denaturation — `" + drop->name.lexeme + "` had its ownership "
                "moved to another variable; dropping it would double-free. Drop the new owner instead.",
                "E503");
        } else {
            it->second = State::Dropped;
        }
        return;
    }

    // --- ReturnStmt ---
    if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt.get())) {
        if (ret->value) {
            analyzeExpr(ctx, ret->value, state);
            if (auto* ve = dynamic_cast<const VarExpr*>(ret->value.get())) {
                auto it = state.find(ve->name.lexeme);
                if (it != state.end() && it->second == State::Live)
                    it->second = State::Escaped;
            }
        }
        for (auto& [name, st] : state) {
            if (st == State::Live) {
                diag(ctx, ret->keyword,
                    "🧬 Unfolded molecule — `" + name + "` leaks on the return at line " +
                    std::to_string(ret->keyword.line) + ". Add `drop " + name + ";`.",
                    "E501");
            }
        }
        terminates = true;
        return;
    }

    // --- ThrowStmt ---
    if (auto* thr = dynamic_cast<const ThrowStmt*>(stmt.get())) {
        analyzeExpr(ctx, thr->expression, state);
        // v5: report leaks on throw paths as errors. No auto-unwind —
        // the programmer must use `finally {}` for explicit cleanup.
        // S8: a name listed in a surrounding `finally {}`'s drops is
        // discharged (the finally runs on the throw path), so skip it.
        for (auto& [name, st] : state) {
            if (st == State::Live && ctx.finally_protected.count(name) == 0) {
                diag(ctx, thr->keyword,
                    "🧬 Unfolded molecule — `" + name + "` is live when `throw` "
                    "fires at line " + std::to_string(thr->keyword.line) + ". "
                    "Add `drop " + name + ";` before the throw, or wrap in "
                    "try/catch/finally with explicit cleanup.",
                    "E501");
            }
        }
        terminates = true;
        return;
    }

    // --- IfStmt ---
    if (auto* ifs = dynamic_cast<const IfStmt*>(stmt.get())) {
        analyzeExpr(ctx, ifs->condition, state);
        StateMap then_state = state, else_state = state;
        bool then_term = false, else_term = false;

        if (ifs->thenBranch) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(ifs->thenBranch.get()))
                then_state = analyzeBlock(ctx, blk->statements, then_state, then_term);
            else
                analyzeStmt(ctx, ifs->thenBranch, then_state, then_term);
        }
        if (ifs->elseBranch) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(ifs->elseBranch.get()))
                else_state = analyzeBlock(ctx, blk->statements, else_state, else_term);
            else
                analyzeStmt(ctx, ifs->elseBranch, else_state, else_term);
        }

        if (then_term && else_term) { terminates = true; return; }
        if (then_term) { state = else_state; return; }
        if (else_term) { state = then_state; return; }

        // Both fall through — merge with asymmetry detection.
        for (auto& [name, st_then] : then_state) {
            auto it2 = else_state.find(name);
            if (it2 != else_state.end() && st_then != it2->second) {
                if ((st_then == State::Live) != (it2->second == State::Live)) {
                    ctx.eh.warning(ifs->keyword,
                        "🔄 Incomplete fold — `" + name + "` is handled differently on "
                        "the two branches. Add `drop " + name + ";` to the path that's missing it.",
                        "W510");
                }
            }
        }
        state = join_maps(then_state, else_state);
        return;
    }

    // --- WhileStmt / ForStmt / ForInStmt (shared loop pattern) ---
    // The body is analyzed once (the E506 check catches drop-without-reassign
    // before the back-edge). The loop's recurring expressions — condition,
    // increment, and the for-in iterable — are also analyzed, since a
    // use-after-free in them is a real bug (S2). Previously only the body
    // was walked, so `drop b; while (b.get() > 0) {}` compiled silently.
    auto analyze_loop_body = [&](const Token& kw, const std::shared_ptr<Stmt>& body) {
        StateMap pre = state;
        StateMap body_state = state;
        bool body_term = false;
        if (body) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(body.get()))
                body_state = analyzeBlock(ctx, blk->statements, body_state, body_term);
            else
                analyzeStmt(ctx, body, body_state, body_term);
        }
        // Loop-body drop check.
        for (auto& [name, st_pre] : pre) {
            if (st_pre == State::Live) {
                auto it2 = body_state.find(name);
                if (it2 != body_state.end() && it2->second == State::Dropped) {
                    diag(ctx, kw,
                        "🔄 `" + name + "` is dropped inside the loop but allocated "
                        "before it — double-free on iteration 2+.",
                        "E506");
                }
            }
        }
        state = join_maps(pre, body_state);
    };

    if (auto* wh = dynamic_cast<const WhileStmt*>(stmt.get())) {
        if (wh->condition) analyzeExpr(ctx, wh->condition, state);   // S2: condition
        analyze_loop_body(wh->keyword, wh->body);
        if (wh->condition) analyzeExpr(ctx, wh->condition, state);   // re-evaluated each iter
        return;
    }
    if (auto* fors = dynamic_cast<const ForStmt*>(stmt.get())) {
        if (fors->initializer) { bool t; analyzeStmt(ctx, fors->initializer, state, t); }
        if (fors->condition) analyzeExpr(ctx, fors->condition, state);  // S2: condition
        analyze_loop_body(fors->keyword, fors->body);
        if (fors->increment) analyzeExpr(ctx, fors->increment, state);  // S2: increment
        if (fors->condition) analyzeExpr(ctx, fors->condition, state);  // re-checked each iter
        return;
    }
    if (auto* forin = dynamic_cast<const ForInStmt*>(stmt.get())) {
        if (forin->collection) analyzeExpr(ctx, forin->collection, state);  // S2: iterable
        analyze_loop_body(forin->name, forin->body);
        return;
    }

    // --- Break / Continue ---
    if (dynamic_cast<const BreakStmt*>(stmt.get()) ||
        dynamic_cast<const ContinueStmt*>(stmt.get())) {
        terminates = true;
        return;
    }

    // --- BlockStmt ---
    if (auto* blk = dynamic_cast<const BlockStmt*>(stmt.get())) {
        bool blk_term = false;
        state = analyzeBlock(ctx, blk->statements, state, blk_term);
        if (blk_term) terminates = true;
        return;
    }

    // --- TryStmt ---
    if (auto* tryS = dynamic_cast<const TryStmt*>(stmt.get())) {
        // S8: if there's a finally, collect the names it drops so throws inside
        // the try body don't false-flag them as leaks (the finally runs on the
        // throw path and discharges the obligation). Snapshot the set to restore
        // on exit (finally protection is scoped to this try).
        std::set<std::string> protected_snapshot = ctx.finally_protected;
        if (tryS->finallyBlock) {
            std::set<std::string> dropped;
            if (auto* fblk = dynamic_cast<const BlockStmt*>(tryS->finallyBlock.get()))
                for (const auto& s : fblk->statements)
                    if (auto* d = dynamic_cast<const DropStmt*>(s.get()))
                        dropped.insert(d->name.lexeme);
            ctx.finally_protected.insert(dropped.begin(), dropped.end());
        }

        StateMap try_state = state;
        bool try_term = false;
        if (tryS->tryBlock) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(tryS->tryBlock.get()))
                try_state = analyzeBlock(ctx, blk->statements, try_state, try_term);
            else
                analyzeStmt(ctx, tryS->tryBlock, try_state, try_term);
        }

        // v5: catch starts from the pre-try state. No auto-unwind —
        // any variable Live in the try that wasn't dropped before the
        // throw is already an E501 leak error (reported by the throw handler).
        // In the catch block, those variables are conservatively Dropped
        // (the throw fired, and the programmer is responsible for cleanup).
        StateMap catch_state = state;
        // Mark variables that were Live at try entry as potentially Dropped
        // in the catch (they may have been dropped before the throw, or leaked).
        for (auto& [name, st] : catch_state) {
            if (st == State::Live) {
                // Check if it's still Live after the try body (not dropped inside).
                auto t_it = try_state.find(name);
                if (t_it != try_state.end() && t_it->second == State::Dropped)
                    catch_state[name] = State::Dropped;
            }
        }

        bool catch_term = false;
        if (tryS->catchBlock) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(tryS->catchBlock.get()))
                catch_state = analyzeBlock(ctx, blk->statements, catch_state, catch_term);
            else
                analyzeStmt(ctx, tryS->catchBlock, catch_state, catch_term);
        }

        // Merge try + catch paths.
        StateMap merged;
        if (try_term && catch_term) {
            merged = state;  // Both terminate — nothing falls through.
        } else if (try_term) {
            merged = catch_state;
        } else if (catch_term) {
            merged = try_state;
        } else {
            merged = join_maps(try_state, catch_state);
        }

        // v5: finally block runs on BOTH the normal (try fell through)
        // and catch paths. Analyze it with the merged state.
        if (tryS->finallyBlock) {
            bool finally_term = false;
            state = merged;
            if (auto* blk = dynamic_cast<const BlockStmt*>(tryS->finallyBlock.get()))
                state = analyzeBlock(ctx, blk->statements, state, finally_term);
            else
                analyzeStmt(ctx, tryS->finallyBlock, state, finally_term);
            // Restore the finally-protected set (scoped to this try).
            ctx.finally_protected = protected_snapshot;
            if (try_term && catch_term && finally_term) { terminates = true; return; }
        } else {
            state = merged;
            if (try_term && catch_term) { terminates = true; }
        }
        return;
    }

    // --- ExpressionStmt: walk for use-after-free ---
    if (auto* exprstmt = dynamic_cast<const ExpressionStmt*>(stmt.get())) {
        analyzeExpr(ctx, exprstmt->expression, state);
        return;
    }

    // --- UnsafeBlockStmt: analyze but report warnings, not errors ---
    if (auto* unsafe = dynamic_cast<const UnsafeBlockStmt*>(stmt.get())) {
        bool was_unsafe = ctx.in_unsafe;
        ctx.in_unsafe = true;
        if (unsafe->block) {
            bool blk_term = false;
            state = analyzeBlock(ctx, unsafe->block->statements, state, blk_term);
            if (blk_term) terminates = true;
        }
        ctx.in_unsafe = was_unsafe;
        return;
    }
}

// ============================================================================
// Phase 3: Exception unwinding
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
