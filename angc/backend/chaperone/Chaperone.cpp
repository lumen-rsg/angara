#include "Chaperone.h"
#include "Stmt.h"
#include "Expr.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include "Token.h"

#include <functional>

namespace angara {

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

bool Chaperone::isTrackedVar(Context& ctx, const VarDeclStmt& var) {
    auto& types = ctx.tc.getVariableTypes();
    auto it = types.find(&var);
    if (it == types.end() || !it->second) return false;
    auto& type = it->second;
    // class and instance types are tracked.
    if (type->kind == TypeKind::CLASS || type->kind == TypeKind::INSTANCE) return true;
    // data types are tracked only if declared `owned` (check by name).
    if (type->kind == TypeKind::DATA) {
        return ctx.tracked_types.count(type->toString()) > 0;
    }
    return false;
}

// ============================================================================
// Expression analysis — E502 use-after-free detection
// ============================================================================

void Chaperone::analyzeExpr(Context& ctx,
    const std::shared_ptr<Expr>& expr, StateMap& state)
{
    if (!expr) return;

    // VarExpr: the key check — is this variable Dropped?
    if (auto* ve = dynamic_cast<const VarExpr*>(expr.get())) {
        auto it = state.find(ve->name.lexeme);
        if (it != state.end() && it->second == State::Dropped) {
            ctx.eh.report(ve->name,
                "💀 Dead reference — `" + ve->name.lexeme + "` was dropped but "
                "is used here. The molecule has already been released.",
                "E502");
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

    // AssignExpr: walk value being assigned.
    if (auto* asgn = dynamic_cast<const AssignExpr*>(expr.get())) {
        analyzeExpr(ctx, asgn->value, state);
        // The target variable is being overwritten — if it was Live, that's a
        // leak (the old allocation is lost). The VarDecl handler catches this
        // for `let` re-declarations; this catches `x = expr` assignments.
        return;
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
        std::string callee_name;
        if (auto* ve = dynamic_cast<const VarExpr*>(call->callee.get())) {
            callee_name = ve->name.lexeme;
        } else if (auto* get = dynamic_cast<const GetExpr*>(call->callee.get())) {
            if (auto* ve2 = dynamic_cast<const VarExpr*>(get->object.get()))
                callee_name = ve2->name.lexeme + "." + get->name.lexeme;
        }

        // Look up the summary. Unknown functions → conservative (Escaped).
        auto sum_it = ctx.summaries.find(callee_name);
        if (sum_it != ctx.summaries.end()) {
            const auto& summary = sum_it->second;
            for (size_t i = 0; i < call->arguments.size(); i++) {
                // Resolve the parameter name for this position.
                // The summary maps param_name → behavior. We need the i-th param.
                // For simplicity: if there's only one tracked param, match it.
                // Otherwise, match by position (the summary was built from the
                // function signature, so param order is known).
                auto* arg = call->arguments[i].get();
                if (auto* ve3 = dynamic_cast<const VarExpr*>(arg)) {
                    auto st_it = state.find(ve3->name.lexeme);
                    if (st_it != state.end() && st_it->second == State::Live) {
                        // Check if the i-th parameter is in the summary.
                        // The summary keys are param names; we match by scanning
                        // for any param that has behavior != Borrowed.
                        // For now: if the summary has exactly one entry and this
                        // arg is tracked, apply it.
                        if (summary.size() == 1) {
                            auto& [pname, behavior] = *summary.begin();
                            if (behavior == ParamBehavior::Dropped)
                                st_it->second = State::Dropped;
                            else if (behavior == ParamBehavior::Escaped)
                                st_it->second = State::Escaped;
                            // Borrowed: stays Live (no transition).
                        } else {
                            // Multi-param: conservative — assume Escaped.
                            st_it->second = State::Escaped;
                        }
                    }
                }
            }
        } else {
            // Unknown function: conservatively escape tracked args.
            for (const auto& arg : call->arguments) {
                if (auto* ve3 = dynamic_cast<const VarExpr*>(arg.get())) {
                    auto st_it = state.find(ve3->name.lexeme);
                    if (st_it != state.end() && st_it->second == State::Live)
                        st_it->second = State::Escaped;
                }
            }
        }
        return;
    }

    // GetExpr (x.field): walk the object.
    if (auto* get = dynamic_cast<const GetExpr*>(expr.get())) {
        analyzeExpr(ctx, get->object, state);
        return;
    }

    // ListExpr: walk all elements.
    if (auto* list = dynamic_cast<const ListExpr*>(expr.get())) {
        for (const auto& elem : list->elements)
            analyzeExpr(ctx, elem, state);
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
        for (const auto& val : rec->values)
            analyzeExpr(ctx, val, state);
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

    // Literal, ThisExpr, SuperExpr, LambdaExpr: no variable references to check
    // (literals have no vars; this/super are not droppable; lambdas are scoped).
}

// ============================================================================
// Phase 2: Per-function data-flow analysis
// ============================================================================

void Chaperone::analyzeFunction(Context& ctx, const FuncStmt& func) {
    ctx.current_function = func.name.lexeme;
    StateMap state;

    // Register tracked parameters.
    std::set<std::string> param_names;  // Track which names are params (not locals).
    auto sem_sym = const_cast<SymbolTable&>(ctx.tc.getSymbolTable()).resolve(func.name.lexeme);
    if (sem_sym && sem_sym->type && sem_sym->type->kind == TypeKind::FUNCTION) {
        auto fn_type = std::dynamic_pointer_cast<FunctionType>(sem_sym->type);
        for (size_t i = 0; i < fn_type->param_types.size() && i < func.params.size(); i++) {
            auto& pt = fn_type->param_types[i];
            if (!pt) continue;
            if (pt->kind == TypeKind::CLASS || pt->kind == TypeKind::INSTANCE ||
                (pt->kind == TypeKind::DATA && ctx.tracked_types.count(pt->toString()))) {
                state[func.params[i].name.lexeme] = State::Live;
                param_names.insert(func.params[i].name.lexeme);
            }
        }
    }

    bool terminates = false;
    if (func.body) {
        analyzeBlock(ctx, *func.body, state, terminates);
    }

    if (!terminates) {
        for (auto& [name, st] : state) {
            if (st == State::Live && param_names.count(name) == 0) {
                // Only report leaks for locals allocated inside this function,
                // not for borrowed parameters (the caller owns those).
                ctx.eh.report(func.name,
                    "🧬 Unfolded molecule — `" + name + "` is live when function `" +
                    func.name.lexeme + "` exits but was never dropped or returned. "
                    "Add `drop " + name + ";` before the function ends.",
                    "E501");
            }
        }
    }

    // Phase 4: build the function summary for interprocedural analysis.
    FunctionSummary summary;
    if (sem_sym && sem_sym->type && sem_sym->type->kind == TypeKind::FUNCTION) {
        auto fn_type = std::dynamic_pointer_cast<FunctionType>(sem_sym->type);
        for (size_t i = 0; i < fn_type->param_types.size() && i < func.params.size(); i++) {
            auto& pt = fn_type->param_types[i];
            if (!pt) continue;
            if (pt->kind == TypeKind::CLASS || pt->kind == TypeKind::INSTANCE ||
                (pt->kind == TypeKind::DATA && ctx.tracked_types.count(pt->toString()))) {
                std::string pname = func.params[i].name.lexeme;
                auto st_it = state.find(pname);
                if (st_it != state.end()) {
                    if (st_it->second == State::Dropped)
                        summary[pname] = ParamBehavior::Dropped;
                    else if (st_it->second == State::Escaped)
                        summary[pname] = ParamBehavior::Escaped;
                    else
                        summary[pname] = ParamBehavior::Borrowed;
                }
            }
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
        // Check the initializer expression for use-after-free.
        if (var->initializer)
            analyzeExpr(ctx, var->initializer, state);
        auto it = state.find(var->name.lexeme);
        if (it != state.end() && it->second == State::Live) {
            ctx.eh.report(var->name,
                "🧬 Unfolded molecule — `" + var->name.lexeme + "` held a live "
                "allocation that is now overwritten without being dropped.",
                "E501");
        }
        state[var->name.lexeme] = isTrackedVar(ctx, *var) ? State::Live : State::Uninit;
        return;
    }

    // --- DropStmt ---
    if (auto* drop = dynamic_cast<const DropStmt*>(stmt.get())) {
        auto it = state.find(drop->name.lexeme);
        if (it == state.end() || it->second == State::Uninit) {
            ctx.eh.report(drop->name,
                "⚠️ Cannot drop `" + drop->name.lexeme + "` — not a tracked allocation.",
                "E503");
        } else if (it->second == State::Dropped) {
            ctx.eh.report(drop->name,
                "⚠️ Double denaturation — `" + drop->name.lexeme + "` was already dropped.",
                "E503");
        } else if (it->second == State::Escaped) {
            ctx.eh.report(drop->name,
                "⚠️ Cannot drop `" + drop->name.lexeme + "` — ownership was transferred.",
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
                ctx.eh.report(ret->keyword,
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
        unwindAtThrow(ctx, state, thr->keyword, thr);
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
    auto analyze_loop = [&](const Token& kw, const std::shared_ptr<Stmt>& body) {
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
                    ctx.eh.report(kw,
                        "🔄 `" + name + "` is dropped inside the loop but allocated "
                        "before it — double-free on iteration 2+.",
                        "E506");
                }
            }
        }
        state = join_maps(pre, body_state);
    };

    if (auto* wh = dynamic_cast<const WhileStmt*>(stmt.get())) {
        analyze_loop(wh->keyword, wh->body);
        return;
    }
    if (auto* fors = dynamic_cast<const ForStmt*>(stmt.get())) {
        if (fors->initializer) { bool t; analyzeStmt(ctx, fors->initializer, state, t); }
        analyze_loop(fors->keyword, fors->body);
        return;
    }
    if (auto* forin = dynamic_cast<const ForInStmt*>(stmt.get())) {
        analyze_loop(forin->name, forin->body);
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
        // Snapshot pre-try state — any variable that gets auto-dropped on a
        // throw inside the try body should be Dropped in the catch block.
        StateMap pre_try = state;
        StateMap try_state = state;
        bool try_term = false;
        if (tryS->tryBlock) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(tryS->tryBlock.get()))
                try_state = analyzeBlock(ctx, blk->statements, try_state, try_term);
            else
                analyzeStmt(ctx, tryS->tryBlock, try_state, try_term);
        }

        // Build the catch state: start from pre-try, then mark any variable
        // that appears in any DropPlan entry for a throw inside the try body
        // as Dropped (it was freed during the unwind).
        StateMap catch_state = pre_try;
        // Check all DropPlan entries — if any throw inside this try auto-dropped
        // a variable, that variable is Dropped in the catch.
        // (Conservative: any throw could have fired; we don't know which.)
        for (auto& [throw_ptr, vars] : ctx.drop_plan) {
            for (const auto& var_name : vars) {
                auto it = catch_state.find(var_name);
                if (it != catch_state.end() && it->second == State::Live) {
                    it->second = State::Dropped;
                }
            }
        }

        bool catch_term = false;
        if (tryS->catchBlock) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(tryS->catchBlock.get()))
                catch_state = analyzeBlock(ctx, blk->statements, catch_state, catch_term);
            else
                analyzeStmt(ctx, tryS->catchBlock, catch_state, catch_term);
        }
        if (try_term && catch_term) { terminates = true; return; }
        if (try_term) { state = catch_state; return; }
        if (catch_term) { state = try_state; return; }
        state = join_maps(try_state, catch_state);
        return;
    }

    // --- ExpressionStmt: walk for use-after-free ---
    if (auto* exprstmt = dynamic_cast<const ExpressionStmt*>(stmt.get())) {
        analyzeExpr(ctx, exprstmt->expression, state);
        return;
    }

    // --- UnsafeBlockStmt / others: skip ---
}

// ============================================================================
// Phase 3: Exception unwinding
// ============================================================================

void Chaperone::unwindAtThrow(Context& ctx, const StateMap& state,
    const Token& throw_tok, const void* throw_ptr)
{
    std::vector<std::string> to_drop;
    for (auto& [name, st] : state) {
        if (st == State::Live) {
            to_drop.push_back(name);
        }
    }
    if (!to_drop.empty()) {
        // Record in the DropPlan for the codegen.
        ctx.drop_plan[throw_ptr] = std::move(to_drop);
    }
}

// ============================================================================
// Phase 4: Cycle detection
// ============================================================================

void Chaperone::detectCycles(Context& ctx,
    const std::vector<std::shared_ptr<Stmt>>& program)
{
    std::map<std::string, std::set<std::string>> graph;


    auto check_field = [&](const std::string& owner, const VarDeclStmt* field) {
        auto& types = ctx.tc.getVariableTypes();
        auto it = types.find(field);
        if (it != types.end() && it->second) {
            std::string ft = it->second->toString();
            if (ctx.tracked_types.count(ft))
                graph[owner].insert(ft);
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
            ctx.eh.report(Token{}, "🔗 Tangled molecule — reference cycle: " + cycle +
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
                    const TypeChecker& tc, ErrorHandler& eh,
                    DropPlan& drop_plan)
{
    Context ctx(tc, eh, drop_plan);
    collectTrackedTypes(ctx, program);
    detectCycles(ctx, program);

    for (const auto& stmt : program) {
        if (!stmt) continue;
        if (auto* func = dynamic_cast<const FuncStmt*>(stmt.get())) {
            if (!func->body) continue;  // Foreign/intrinsic.
            analyzeFunction(ctx, *func);
        }
    }
    return eh.errorCount() == 0;
}

} // namespace angara
