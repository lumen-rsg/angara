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
    if (auto* await_e = dynamic_cast<const AwaitExpr*>(expr.get())) { collectExprVarRefs(await_e->future, out); return; }  // LIB-4
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
    if (auto* np = dynamic_cast<const NestedPattern*>(expr.get())) {
        collectExprVarRefs(np->constructor, out);
        for (const auto& sp : np->subpatterns) collectExprVarRefs(sp, out);
        return;
    }
}


// E505 helper: if `elem` is a tracked Live variable being placed into an
// *untracked* container, flag it.  Phase B: lists and records are now tracked,
// so E505 only fires for truly untracked containers (e.g. tuples).
void Chaperone::checkEscapeIntoContainer(Context& ctx,
    const std::shared_ptr<Expr>& elem, StateMap& state,
    const Type* container_type)
{
    // If the container itself is tracked, the Chaperone can verify that
    // it gets dropped, which cascades to elements — no escape.
    if (container_type && isTrackedTypeObj(ctx, *container_type)) return;

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
    const std::shared_ptr<Expr>& expr, StateMap& state, bool is_callee)
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
            } else if (it->second == State::Escaped &&
                       ctx.thread_escaped.count(ve->name.lexeme)) {
                // M11: variable was transferred to another thread via spawn().
                diag(ctx, ve->name,
                    "🧵 Thread escape — `" + ve->name.lexeme + "` had its ownership "
                    "transferred to another thread via `spawn()`. Using it in the "
                    "parent thread is a data race / use-after-transfer. Access the "
                    "value through the spawned thread's lifecycle instead.",
                    "E510");
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
            } else if (tit->second != State::Uninit) {
                // Non-move assignment (constructor call, function return, etc.)
                // on a variable that was previously holding a tracked allocation:
                // the target now holds a new allocation → Live.  (Uninit targets
                // are non-tracked declarations like `let i as i64`; skip them.)
                tit->second = State::Live;
            }

            // H10: function pointer alias tracking for reassignments.
            // When a variable is reassigned to a direct function reference
            // (e.g. `f = borrow;`), update the alias so indirect calls
            // through the variable resolve to the correct summary.
            if (asgn->value) {
                if (auto* vve = dynamic_cast<const VarExpr*>(asgn->value.get())) {
                    if (ctx.summaries.count(vve->name.lexeme)) {
                        ctx.function_aliases[tgt->name.lexeme] = vve->name.lexeme;
                    }
                }
            }
            return;
        }

        // --- Target is a tuple destructure ((a, b) = ...): per-element ownership ---
        if (auto* tup = dynamic_cast<const TupleExpr*>(asgn->target.get())) {
            // Walk each element of the tuple (each should be a VarExpr).
            for (const auto& elem : tup->elements) {
                if (auto* ve = dynamic_cast<const VarExpr*>(elem.get())) {
                    auto tit = state.find(ve->name.lexeme);
                    if (tit == state.end()) continue;
                    // S7: overwriting a Live tracked variable leaks the old allocation.
                    if (tit->second == State::Live) {
                        diag(ctx, ve->name,
                            "🧬 Unfolded molecule — `" + ve->name.lexeme + "` held a live "
                            "allocation that is overwritten by this destructuring assignment "
                            "without being dropped. Add `drop " + ve->name.lexeme + ";` first.",
                            "E501");
                    }
                    // Non-move assignment on a variable that was previously holding a
                    // tracked allocation: the target now holds a new allocation → Live.
                    // (Uninit targets are non-tracked declarations — skip them.)
                    if (tit->second != State::Uninit) {
                        tit->second = State::Live;
                    }
                }
            }
            if (rhs_is_move_source) {
                state[move_src] = State::Moved;
            }
            return;
        }

        // --- Target is a field (this.f = ...): S6 field ownership move + H8 field overwrite leak ---
        // External field writes are already blocked by the type checker (private
        // fields, E336), so this only fires inside methods/constructors. The
        // sound rule is unique_ptr field semantics: assigning a tracked Live
        // variable to a field MOVES its ownership into the field — the source
        // becomes invalid (E507 on later use). This prevents the double-free
        // where both the field's owning object (cascade-drop) and the source
        // would free the same allocation.
        //
        // H8: track per-field ownership state for `this` fields so that
        // overwriting a field that already holds a tracked allocation is
        // detected as a leak (E501), mirroring the S7 variable-overwrite check.
        // Field state is keyed as "this.<fieldname>" in the StateMap — the '.'
        // character is not valid in Angara identifiers, so there's no collision
        // with variable names. Only fields of tracked types are registered;
        // non-tracked fields (i64, string, etc.) are ignored.
        if (auto* get = dynamic_cast<const GetExpr*>(asgn->target.get())) {
            analyzeExpr(ctx, get->object, state);   // catch UAF on the object

            // H8: build a field-state key for `this.field` and `obj.field` accesses.
            std::string field_key;
            if (dynamic_cast<const ThisExpr*>(get->object.get())) {
                field_key = "this." + get->name.lexeme;
            } else if (auto* ove = dynamic_cast<const VarExpr*>(get->object.get())) {
                field_key = ove->name.lexeme + "." + get->name.lexeme;
            }

            if (!field_key.empty()) {
                auto& expr_types = ctx.tc.getExpressionTypes();
                auto tt = expr_types.find(asgn->target.get());
                bool field_is_tracked = tt != expr_types.end() && tt->second &&
                                        isTrackedTypeObj(ctx, *tt->second);

                if (field_is_tracked) {
                    auto fit = state.find(field_key);
                    // H8: if the field already holds a Live tracked allocation,
                    // overwriting it without a drop is a leak (mirrors S7).
                    if (fit != state.end() && fit->second == State::Live) {
                        diag(ctx, get->name,
                            "\xf0\x9f\xa7\xac Unfolded molecule — field `" +
                            get->name.lexeme + "` held a live allocation that "
                            "is overwritten by this assignment without being "
                            "dropped.",
                            "E501");
                    }
                    state[field_key] = State::Live;
                }
            }

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
        analyzeExpr(ctx, call->callee, state, /*is_callee=*/true);

        // LANG-11: use resolved (reordered + defaults-filled) args when available.
        const auto& resolved_all = ctx.tc.getResolvedArgs();
        auto rit = resolved_all.find(call);
        const auto& effective_args = (rit != resolved_all.end())
                                     ? rit->second : call->arguments;

        // Interprocedural: determine the callee name and summary BEFORE
        // analyzing arguments, so we can detect LambdaExpr arguments in
        // Borrowed positions (L21: closure-as-borrowed-argument).
        // For method calls (o.m()), look up the object's type to build a
        // class-qualified key (e.g., "Buf.init") — C3 prevents name collisions
        // between same-named methods in different classes.
        std::string callee_name;
        std::string method_name;  // fallback for method calls
        if (auto* ve = dynamic_cast<const VarExpr*>(call->callee.get())) {
            callee_name = ve->name.lexeme;
        } else if (auto* get = dynamic_cast<const GetExpr*>(call->callee.get())) {
            method_name = get->name.lexeme;  // e.g. "init"
            // Resolve the object's type to get the class name.
            if (auto* ve2 = dynamic_cast<const VarExpr*>(get->object.get())) {
                auto tit = ctx.tc.getExpressionTypes().find(get->object.get());
                if (tit != ctx.tc.getExpressionTypes().end() && tit->second) {
                    // Drill through INSTANCE → CLASS to get the class name.
                    const Type* t = tit->second.get();
                    if (t->kind == TypeKind::INSTANCE) {
                        auto inst = dynamic_cast<const InstanceType*>(t);
                        if (inst && inst->class_type)
                            callee_name = inst->class_type->name + "." + method_name;
                    }
                }
            }
            if (callee_name.empty())
                callee_name = method_name;  // fallback: bare method name
        }

        // M11: spawn() transfers ownership of tracked arguments to a new thread.
        // Detect spawn() calls early before the summary lookup and transition
        // all tracked args (positions 1..N) to Escaped. The first argument (the
        // closure/function) is not a data transfer. Record escaped vars in
        // thread_escaped so E510 can fire on subsequent use in the parent thread.
        //
        // Also enforces: only @sendable types (or implicitly-sendable data types)
        // can be passed to spawn(). Non-sendable tracked types trigger E511.
        if (callee_name == "spawn") {
            for (size_t i = 0; i < effective_args.size(); i++) {
                analyzeExpr(ctx, effective_args[i], state);
            }
            // Transition tracked Live args (positions 1..N) to Escaped.
            for (size_t i = 1; i < effective_args.size(); i++) {
                if (auto* ve = dynamic_cast<const VarExpr*>(effective_args[i].get())) {
                    auto it = state.find(ve->name.lexeme);
                    if (it != state.end() && it->second == State::Live) {
                        // M11: resolve type name early — needed for both @sendable
                        // and E514 diagnostics.
                        std::string type_name;
                        auto& expr_types = ctx.tc.getExpressionTypes();
                        auto et = expr_types.find(effective_args[i].get());
                        if (et != expr_types.end() && et->second) {
                            const Type* t = et->second.get();
                            if (t->kind == TypeKind::OPTIONAL) {
                                auto ot = dynamic_cast<const OptionalType*>(t);
                                if (ot && ot->wrapped_type)
                                    t = ot->wrapped_type.get();
                            }
                            if (t->kind == TypeKind::INSTANCE) {
                                auto inst = dynamic_cast<const InstanceType*>(t);
                                if (inst && inst->class_type)
                                    type_name = inst->class_type->name;
                                else
                                    type_name = t->toString();
                            } else {
                                type_name = t->toString();
                            }
                        }

                        // M11: @sendable check — the type must be in sendable_types.
                        if (!type_name.empty() &&
                            !ctx.sendable_types.count(type_name) &&
                            et != expr_types.end() && et->second &&
                            isTrackedTypeObj(ctx, *et->second)) {
                            diag(ctx, ve->name,
                                "\xf0\x9f\xa7\xb5 Not sendable — `" + type_name + "` is not marked "
                                "@sendable and cannot be transferred to another thread "
                                "via `spawn()`. Add `@sendable` to the type declaration "
                                "to allow cross-thread ownership transfer.",
                                "E511");
                            continue;  // don't transition to Escaped — blocked
                        }
                        it->second = State::Escaped;
                        ctx.thread_escaped.insert(ve->name.lexeme);

                        // M11: E514 — check for ref<T> data races. If any ref<T>
                        // in the parent borrows this variable, the ref and the
                        // spawned thread can concurrently access the same memory.
                        for (const auto& [ref_name, referent] : ctx.borrows) {
                            if (referent == ve->name.lexeme) {
                                diag(ctx, ve->name,
                                    "\xf0\x9f\xa7\xb5 Shared borrow — `" + ref_name + "` is a `ref<" +
                                    type_name + ">` to `" + ve->name.lexeme + "`, which is being "
                                    "transferred to another thread via `spawn()`. The ref and the "
                                    "spawned thread can access the same memory concurrently — "
                                    "a data race. Drop the ref before spawning, or use a Mutex "
                                    "to synchronize access.",
                                    "E514");
                            }
                        }
                    }
                }
            }
            return;
        }

        // M11: Thread.join() — detect join calls and track the return value.
        // When a Thread's join() returns a tracked type, the result variable
        // becomes Live (ownership returns from the spawned thread).
        // The spawn-to-join association is tracked through the closure summary.
        if (method_name == "join" && !callee_name.empty() && callee_name != "join") {
            // This is a method call: the callee_name is the object's variable name.
            // Walk the join target (the Thread object) for UAF checks, walk args.
            analyzeExpr(ctx, call->callee, state);
            for (const auto& arg : effective_args)
                analyzeExpr(ctx, arg, state);
            // join() returns the spawned function's return type. If tracked,
            // the VarDeclStmt handler will mark the result as Live.
            // No summary-based argument transitions needed.
            return;
        }

        // M11: Mutex lock/unlock tracking.
        // Detect mutex.lock() and mutex.unlock() calls on Mutex-typed objects
        // and track the per-mutex lock state. Double-lock → E512, double-unlock → E513.
        if (method_name == "lock" || method_name == "unlock") {
            // Find the object variable name for the mutex.
            std::string mutex_var_name;
            if (auto* get = dynamic_cast<const GetExpr*>(call->callee.get())) {
                if (auto* ove = dynamic_cast<const VarExpr*>(get->object.get())) {
                    // Verify it's a Mutex type.
                    auto tit = ctx.tc.getExpressionTypes().find(get->object.get());
                    if (tit != ctx.tc.getExpressionTypes().end() && tit->second &&
                        tit->second->kind == TypeKind::MUTEX) {
                        mutex_var_name = ove->name.lexeme;
                    }
                }
            }
            if (!mutex_var_name.empty()) {
                // Walk callee and args for UAF checks.
                analyzeExpr(ctx, call->callee, state);
                for (const auto& arg : effective_args)
                    analyzeExpr(ctx, arg, state);

                bool currently_locked = ctx.mutex_locked[mutex_var_name];
                if (method_name == "lock") {
                    if (currently_locked) {
                        diag(ctx, call->paren,
                            "\xf0\x9f\x94\x92 Double lock — `" + mutex_var_name + "` is already "
                            "locked. Locking it again may cause a deadlock.",
                            "E512");
                    }
                    ctx.mutex_locked[mutex_var_name] = true;
                } else { // unlock
                    if (!currently_locked) {
                        diag(ctx, call->paren,
                            "\xf0\x9f\x94\x93 Double unlock — `" + mutex_var_name + "` is not "
                            "locked. Unlocking an unlocked mutex is a logic error.",
                            "E513");
                    }
                    ctx.mutex_locked[mutex_var_name] = false;
                }
                return;
            }
        }

        // Look up the summary: first by function name, then by closure
        // FunctionType (H2). For method calls, also try the bare method name.
        const FunctionSummary* summary = nullptr;
        FunctionSummary temp_summary;  // H9: built from FunctionType annotations
        auto sum_it = ctx.summaries.find(callee_name);
        if (sum_it == ctx.summaries.end() && !method_name.empty())
            sum_it = ctx.summaries.find(method_name);
        if (sum_it != ctx.summaries.end()) {
            summary = &sum_it->second;
        } else {
            // H2: Check for closure call summary via the callee's FunctionType.
            // Each LambdaExpr creates a unique FunctionType that flows through
            // variable assignments, so looking up by the callee VarExpr's type
            // finds the right summary.
            if (dynamic_cast<const VarExpr*>(call->callee.get())) {
                auto tit = ctx.tc.getExpressionTypes().find(call->callee.get());
                if (tit != ctx.tc.getExpressionTypes().end() && tit->second &&
                    tit->second->kind == TypeKind::FUNCTION) {
                    auto cs_it = ctx.closure_summaries.find(tit->second.get());
                    if (cs_it != ctx.closure_summaries.end())
                        summary = &cs_it->second;
                }
            }
        }

        // If no summary was found by name or closure lookup, try function
        // pointer alias resolution (H10) and then FunctionType annotations (H9).
        if (!summary) {
            // H10: function pointer alias resolution. When the callee is a
            // variable that was assigned a direct function reference
            // (e.g. `let f = consume_buf;` recorded in function_aliases),
            // resolve the indirect call through the alias to use the
            // aliased function's summary.
            if (auto* ve = dynamic_cast<const VarExpr*>(call->callee.get())) {
                auto alias_it = ctx.function_aliases.find(ve->name.lexeme);
                if (alias_it != ctx.function_aliases.end()) {
                    auto sum_it2 = ctx.summaries.find(alias_it->second);
                    if (sum_it2 != ctx.summaries.end()) {
                        summary = &sum_it2->second;
                    }
                }
            }
        }

        // H9: If no summary was found by name, closure, or alias lookup, check
        // the callee's FunctionType for @consumes/@escape annotations.
        if (!summary) {
            auto tit = ctx.tc.getExpressionTypes().find(call->callee.get());
            if (tit != ctx.tc.getExpressionTypes().end() && tit->second &&
                tit->second->kind == TypeKind::FUNCTION) {
                auto* ft = dynamic_cast<const FunctionType*>(tit->second.get());
                if (ft && (!ft->consumes_params.empty() || !ft->escape_params.empty())) {
                    size_t n_params = ft->param_types.size();
                    temp_summary.resize(n_params, ParamBehavior::Borrowed);
                    for (int idx : ft->consumes_params) {
                        if (idx >= 0 && static_cast<size_t>(idx) < n_params)
                            temp_summary[idx] = ParamBehavior::Dropped;
                    }
                    for (int idx : ft->escape_params) {
                        if (idx >= 0 && static_cast<size_t>(idx) < n_params)
                            temp_summary[idx] = ParamBehavior::Escaped;
                    }
                    summary = &temp_summary;
                }
            }
        }

        // Analyze each argument. L21: when a LambdaExpr is passed to a
        // Borrowed parameter position (or to an unknown function, which
        // defaults to Borrow), treat it like an IIFE — the closure is consumed
        // synchronously by the callee and cannot outlive its captures.
        for (size_t i = 0; i < effective_args.size(); i++) {
            bool lambda_is_borrowed = false;
            if (dynamic_cast<const LambdaExpr*>(effective_args[i].get())) {
                if (!summary) {
                    // Unknown function — conservative default is Borrow.
                    lambda_is_borrowed = true;
                } else if (i < summary->size() && (*summary)[i] == ParamBehavior::Borrowed) {
                    lambda_is_borrowed = true;
                }
            }
            analyzeExpr(ctx, effective_args[i], state, /*is_callee=*/lambda_is_borrowed);
        }

        if (summary) {
            // Positional match: call-arg i binds to summary[i] (S5). The summary
            // is aligned to call-arg positions (`this` is not a call arg), so a
            // direct index is correct for any arity.
            for (size_t i = 0; i < effective_args.size() && i < summary->size(); i++) {
                auto behavior = (*summary)[i];
                if (behavior == ParamBehavior::Borrowed) continue;  // stays Live
                auto* arg = effective_args[i].get();
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
        }
        // else: Unknown function — default is BORROW (no state change).
        return;
    }

    // GetExpr (x.field): walk the object, then check field UAF (H8).
    if (auto* get = dynamic_cast<const GetExpr*>(expr.get())) {
        analyzeExpr(ctx, get->object, state);

        // H8: field-level use-after-free / use-after-move detection.
        // Mirrors the VarExpr UAF check (E502/E507) for field keys.
        std::string field_key;
        if (dynamic_cast<const ThisExpr*>(get->object.get())) {
            field_key = "this." + get->name.lexeme;
        } else if (auto* ove = dynamic_cast<const VarExpr*>(get->object.get())) {
            field_key = ove->name.lexeme + "." + get->name.lexeme;
        }

        if (!field_key.empty()) {
            auto it = state.find(field_key);
            if (it != state.end()) {
                if (it->second == State::Dropped) {
                    diag(ctx, get->name,
                        "\xf0\x9f\x92\x80 Dead reference — field `" +
                        get->name.lexeme + "` was dropped but is used here. "
                        "The molecule has already been released.",
                        "E502");
                } else if (it->second == State::Moved) {
                    diag(ctx, get->name,
                        "\xf0\x9f\x93\xa4 Moved molecule — field `" +
                        get->name.lexeme + "` had its ownership transferred "
                        "and is used here.",
                        "E507");
                }
            }
        }
        return;
    }

    // ListExpr: walk all elements. E505 — a tracked Live value placed into
    // an untracked container (Phase B: lists are now tracked, so E505 only
    // fires for untracked element types like tuples).
    if (auto* list = dynamic_cast<const ListExpr*>(expr.get())) {
        // Get the resolved type to check if the container is tracked.
        const Type* container_type = nullptr;
        auto& expr_types = ctx.tc.getExpressionTypes();
        auto et = expr_types.find(expr.get());
        if (et != expr_types.end()) container_type = et->second.get();

        for (const auto& elem : list->elements) {
            analyzeExpr(ctx, elem, state);
            checkEscapeIntoContainer(ctx, elem, state, container_type);
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
        const Type* container_type = nullptr;
        auto& expr_types = ctx.tc.getExpressionTypes();
        auto et = expr_types.find(expr.get());
        if (et != expr_types.end()) container_type = et->second.get();

        for (const auto& val : rec->values) {
            analyzeExpr(ctx, val, state);
            checkEscapeIntoContainer(ctx, val, state, container_type);   // E505
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

    // RangeExpr (a..b): walk both sides. (C1 — previously unhandled.)
    if (auto* range = dynamic_cast<const RangeExpr*>(expr.get())) {
        analyzeExpr(ctx, range->left, state);
        analyzeExpr(ctx, range->right, state);
        return;
    }

    // InterpStringExpr ("hello {name}"): walk each interpolation segment.
    // (C2 — previously unhandled.)
    if (auto* interp = dynamic_cast<const InterpStringExpr*>(expr.get())) {
        for (const auto& [lit, sub] : interp->segments) {
            if (sub) analyzeExpr(ctx, sub, state);
        }
        return;
    }

    // MatchExpr: walk the condition + all patterns + all case bodies + guards.
    if (auto* match = dynamic_cast<const MatchExpr*>(expr.get())) {
        analyzeExpr(ctx, match->condition, state);
        for (const auto& cs : match->cases) {
            for (const auto& pat : cs.patterns) {
                // Skip NestedPattern for now — handled recursively inside
                if (dynamic_cast<const NestedPattern*>(pat.get())) continue;
                analyzeExpr(ctx, pat, state);
            }
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
    //
    // C4: Previously the lambda body was never analyzed for memory safety —
    // leaks, use-after-free, double-drops, etc. inside the closure were invisible.
    // Now we analyze the body with its own state map, seeded with captured
    // variables in Escaped state (they belong to the outer scope).
    if (auto* lam = dynamic_cast<const LambdaExpr*>(expr.get())) {
        std::set<std::string> referenced;
        for (const auto& s : lam->body)
            collectVarRefs(s, referenced);

        // L21: When a LambdaExpr is the callee of a CallExpr (IIFE — immediately-
        // invoked function expression), the closure is called and discarded inline.
        // It cannot outlive the captured variables, so we skip the E505 Escaped
        // transition and treat captures as safe synchronous borrows. The body
        // is still analyzed for memory safety (C4), but captured variables are
        // seeded as Live rather than Escaped in the lambda's state map.
        if (is_callee) {
            // IIFE: captures are safe — no E505, no Escaped transition.
            // Seed captures as Escaped in the lambda state to prevent false
            // E501 leak reports inside the body (the captured vars don't need
            // to be dropped inside the lambda — they belong to the outer scope).
            // Still analyze the body for internal memory errors (leaks of
            // lambda-owned values, UAF, double-drops, etc.).
            StateMap lambda_state;
            for (const auto& name : referenced)
                lambda_state[name] = State::Escaped;

            // H2: seed lambda params.
            auto lam_type_it = ctx.tc.getExpressionTypes().find(expr.get());
            std::set<std::string> saved_params;
            std::swap(saved_params, ctx.current_params);
            if (lam_type_it != ctx.tc.getExpressionTypes().end() && lam_type_it->second &&
                lam_type_it->second->kind == TypeKind::FUNCTION) {
                auto* fn_type = static_cast<const FunctionType*>(lam_type_it->second.get());
                for (size_t i = 0; i < lam->param_names.size() && i < fn_type->param_types.size(); i++) {
                    if (fn_type->param_types[i] && isTrackedTypeObj(ctx, *fn_type->param_types[i])) {
                        lambda_state[lam->param_names[i].lexeme] = State::Live;
                        ctx.current_params.insert(lam->param_names[i].lexeme);
                    }
                }
            }

            bool terminates = false;
            for (const auto& s : lam->body) {
                analyzeStmt(ctx, s, lambda_state, terminates);
                if (terminates) break;
            }
            // Restore current_params.
            std::swap(saved_params, ctx.current_params);
            // IIFE: captured vars did not escape, so outer state unchanged.
            return;
        }

        // Non-IIFE: the closure may outlive captures — emit E505 and transition
        // captured Live variables to Escaped.
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

        // C4: Analyze the lambda body for memory safety. Start with a fresh
        // state map containing captured variables as Escaped (they come from
        // outside). Walk each statement — the individual statement handlers
        // (ReturnStmt, function exit, etc.) will report leaks, use-after-free,
        // and other violations just like they do for regular functions.
        StateMap lambda_state;
        for (const auto& name : referenced) {
            lambda_state[name] = State::Escaped;
        }

        // H2: Seed lambda parameters into lambda_state so drops/escapes/moves
        // of params are tracked during body analysis. Without this, the summary
        // can't see what the closure does to its arguments.
        // Also save/restore ctx.current_params so that lambda params are
        // excluded from E501 leak checks (just like regular function params).
        auto lam_type_it = ctx.tc.getExpressionTypes().find(expr.get());
        std::set<std::string> saved_params;
        std::swap(saved_params, ctx.current_params);
        if (lam_type_it != ctx.tc.getExpressionTypes().end() && lam_type_it->second &&
            lam_type_it->second->kind == TypeKind::FUNCTION) {
            auto* fn_type = static_cast<const FunctionType*>(lam_type_it->second.get());
            for (size_t i = 0; i < lam->param_names.size() && i < fn_type->param_types.size(); i++) {
                if (fn_type->param_types[i] && isTrackedTypeObj(ctx, *fn_type->param_types[i])) {
                    lambda_state[lam->param_names[i].lexeme] = State::Live;
                    ctx.current_params.insert(lam->param_names[i].lexeme);
                }
            }
        }

        bool terminates = false;
        for (const auto& s : lam->body) {
            analyzeStmt(ctx, s, lambda_state, terminates);
            if (terminates) break;
        }

        // Restore the enclosing function's params.
        std::swap(saved_params, ctx.current_params);

        // H2: Build a closure call summary from the lambda body analysis.
        // This enables call sites like f(args) to know what the closure does
        // to its arguments (drop, escape, or borrow).
        if (lam_type_it != ctx.tc.getExpressionTypes().end() && lam_type_it->second &&
            lam_type_it->second->kind == TypeKind::FUNCTION) {
            auto* fn_type = static_cast<const FunctionType*>(lam_type_it->second.get());
            FunctionSummary lam_summary(lam->param_names.size(), ParamBehavior::Borrowed);
            for (size_t i = 0; i < lam->param_names.size() && i < fn_type->param_types.size(); i++) {
                if (!fn_type->param_types[i] || !isTrackedTypeObj(ctx, *fn_type->param_types[i]))
                    continue;
                auto st_it = lambda_state.find(lam->param_names[i].lexeme);
                if (st_it != lambda_state.end()) {
                    if (st_it->second == State::Dropped)
                        lam_summary[i] = ParamBehavior::Dropped;
                    else if (st_it->second == State::Escaped || st_it->second == State::Moved)
                        lam_summary[i] = ParamBehavior::Escaped;
                }
            }
            ctx.closure_summaries[lam_type_it->second.get()] = std::move(lam_summary);
        }
        return;
    }

    // LIB-4: AwaitExpr — consumes the future (move semantics).
    // The future variable transitions from Live to Moved, just like
    // assignment of a tracked variable transfers ownership.
    if (auto* await_expr = dynamic_cast<const AwaitExpr*>(expr.get())) {
        analyzeExpr(ctx, await_expr->future, state);
        // If the awaited expression is a simple variable that is tracked
        // and currently Live, the await consumes it (move semantics).
        if (auto* ve = dynamic_cast<const VarExpr*>(await_expr->future.get())) {
            auto it = state.find(ve->name.lexeme);
            if (it != state.end() && it->second == State::Live) {
                it->second = State::Moved;
            }
        }
        return;
    }

    // Literal, ThisExpr, SuperExpr: no variable references to check.
}

} // namespace angara
