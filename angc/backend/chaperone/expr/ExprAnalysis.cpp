#include "Chaperone.h"
#include "Stmt.h"
#include "Expr.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include "Token.h"
#include "ASTTypes.h"

#include <functional>

namespace angara {

// Expression half of collectVarRefs (used to find closure captures).
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
    if (auto* tup = dynamic_cast<const TupleExpr*>(expr.get())) { for (const auto& e : tup->elements) collectExprVarRefs(e, out); return; }  // LANG-10
    if (auto* lo = dynamic_cast<const LogicalExpr*>(expr.get())) { collectExprVarRefs(lo->left, out); collectExprVarRefs(lo->right, out); return; }
    if (auto* su = dynamic_cast<const SubscriptExpr*>(expr.get())) { collectExprVarRefs(su->object, out); collectExprVarRefs(su->index, out); return; }
    if (auto* re = dynamic_cast<const RecordExpr*>(expr.get())) { for (const auto& v : re->values) collectExprVarRefs(v, out); return; }
    if (auto* te = dynamic_cast<const TernaryExpr*>(expr.get())) { collectExprVarRefs(te->condition, out); collectExprVarRefs(te->thenBranch, out); collectExprVarRefs(te->elseBranch, out); return; }
    if (auto* ie = dynamic_cast<const IsExpr*>(expr.get())) { collectExprVarRefs(ie->object, out); return; }
    if (auto* ce = dynamic_cast<const CastExpr*>(expr.get())) { collectExprVarRefs(ce->object, out); return; }
    if (auto* de = dynamic_cast<const DerefExpr*>(expr.get())) { collectExprVarRefs(de->right, out); return; }
    if (auto* re = dynamic_cast<const RangeExpr*>(expr.get())) { collectExprVarRefs(re->left, out); collectExprVarRefs(re->right, out); return; }
    if (auto* is = dynamic_cast<const InterpStringExpr*>(expr.get())) {
        for (const auto& [lit, sub] : is->segments) if (sub) collectExprVarRefs(sub, out);
        return;
    }
    if (auto* me = dynamic_cast<const MatchExpr*>(expr.get())) {
        collectExprVarRefs(me->condition, out);
        for (const auto& cs : me->cases) {
            for (const auto& pat : cs.patterns) collectExprVarRefs(pat, out);
            if (cs.guard) collectExprVarRefs(*cs.guard, out);
            if (cs.body) collectExprVarRefs(cs.body, out);
        }
        return;
    }
}


// E505 helper
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


// analyzeExpr
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
        // S3: if this is a ref<T> being read, check its referent is still live.
        // Reading a ref whose referent was dropped/moved is a dangling borrow.
        auto bit = ctx.borrows.find(ve->name.lexeme);
        if (bit != ctx.borrows.end()) {
            auto rit = state.find(bit->second);
            if (rit != state.end() &&
                (rit->second == State::Dropped || rit->second == State::Moved)) {
                diag(ctx, ve->name,
                    "🔗 Dangling borrow — `" + ve->name.lexeme + "` is a ref to `" +
                    bit->second + "`, whose ownership has been released. The ref "
                    "reads freed memory. Keep the referent live while the ref is used.",
                    "E509");
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

            // S3 borrow vs S1 move: if the TARGET is a ref<T>, assigning a
            // tracked Live var to it is a BORROW — the source stays Live (it
            // still owns the object), and we record ref→referent so a later
            // drop/move of the referent flags the dangling ref (E509).
            if (rhs_is_move_source && isRefVarExpr(ctx, *tgt)) {
                ctx.borrows[tgt->name.lexeme] = move_src;  // ref points at referent
                return;  // borrow — source stays Live, no leak on the target
            }

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

    // TupleExpr (LANG-10): same as ListExpr — walk all elements, check escape.
    if (auto* tup = dynamic_cast<const TupleExpr*>(expr.get())) {
        for (const auto& elem : tup->elements) {
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

    // MatchExpr: walk the condition + all patterns + all case bodies + guards.
    if (auto* match = dynamic_cast<const MatchExpr*>(expr.get())) {
        analyzeExpr(ctx, match->condition, state);
        for (const auto& cs : match->cases) {
            for (const auto& pat : cs.patterns) analyzeExpr(ctx, pat, state);
            if (cs.guard) analyzeExpr(ctx, *cs.guard, state);
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

} // namespace angara
