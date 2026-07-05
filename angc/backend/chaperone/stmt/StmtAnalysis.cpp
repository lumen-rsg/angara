#include "Chaperone.h"
#include "Stmt.h"
#include "Expr.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include "Token.h"
#include "ASTTypes.h"

#include <functional>

namespace angara {

// ============================================================================
// Per-function data-flow analysis
// ============================================================================

void Chaperone::analyzeFunction(Context& ctx, const FuncStmt& func,
                                 const std::string& summary_key) {
    ctx.current_function = func.name.lexeme;
    StateMap state;
    ctx.borrows.clear();   // S3: borrow map is per-function (reset each pass)
    ctx.current_params.clear();  // rebuilt below as tracked params register

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
                ctx.current_params.insert(func.params[i].name.lexeme);
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
                ctx.current_params.insert(func.params[i].name.lexeme);
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
    ctx.summaries[summary_key] = std::move(summary);

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
        // LANG-10: destructuring declaration — each name is a sub-element
        // of a tuple (untracked container). Mark all as Uninit.
        if (!var->destructure_names.empty()) {
            if (var->initializer)
                analyzeExpr(ctx, var->initializer, state);
            for (const auto& dn : var->destructure_names) {
                state[dn.lexeme] = State::Uninit;
            }
            return;
        }

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

        // S3 borrow: if the new binding is a ref<T> initialized from a tracked
        // Live var, record ref→referent (no move — the source keeps ownership).
        // A later drop/move of the referent flags the dangling ref (E509).
        if (init_is_move_source && !tracked) {
            auto& types = ctx.tc.getVariableTypes();
            auto tit = types.find(var);
            if (tit != types.end() && tit->second &&
                tit->second->kind == TypeKind::REF) {
                ctx.borrows[var->name.lexeme] = move_src;
            }
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
            // Exclude tracked parameters — they're borrowed (caller-owned), not
            // this function's responsibility to drop.
            if (st == State::Live && ctx.current_params.count(name) == 0) {
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
            if (st == State::Live && ctx.finally_protected.count(name) == 0
                && ctx.current_params.count(name) == 0) {  // borrowed params excluded
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
        // M1: scan both then_state and else_state keys for Live asymmetry.
        std::set<std::string> all_names;
        for (const auto& [name, _] : then_state) all_names.insert(name);
        for (const auto& [name, _] : else_state) all_names.insert(name);
        for (const auto& name : all_names) {
            auto it_then = then_state.find(name);
            auto it_else = else_state.find(name);
            if (it_then != then_state.end() && it_else != else_state.end() &&
                it_then->second != it_else->second) {
                if ((it_then->second == State::Live) != (it_else->second == State::Live)) {
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

} // namespace angara
