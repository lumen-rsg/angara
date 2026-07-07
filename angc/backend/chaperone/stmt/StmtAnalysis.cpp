#include "Chaperone.h"
#include "Stmt.h"
#include "Expr.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include "Token.h"
#include "ASTTypes.h"
#include "SymbolTable.h"

#include <functional>

namespace angara {

// ============================================================================
// Per-function data-flow analysis
// ============================================================================

void Chaperone::analyzeFunction(Context& ctx, const FuncStmt& func,
                                 const std::string& summary_key) {
    ctx.current_function = func.name.lexeme;
    StateMap state;
    ctx.borrows.clear();        // S3: borrow map is per-function (reset each pass)
    ctx.thread_escaped.clear(); // M11: thread-escape set is per-function
    ctx.mutex_locked.clear();   // M11: mutex lock state is per-function
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
            // LANG-10: for destructured parameters, register each element
            // name if its type is tracked.
            const auto& param = func.params[i];
            if (!param.destructure_names.empty() && pt->kind == TypeKind::TUPLE) {
                auto tuple_type = std::dynamic_pointer_cast<TupleType>(pt);
                for (size_t di = 0; di < param.destructure_names.size() && di < tuple_type->element_types.size(); ++di) {
                    auto& et = tuple_type->element_types[di];
                    if (isTrackedTypeObj(ctx, *et)) {
                        state[param.destructure_names[di].lexeme] = State::Live;
                        param_names.insert(param.destructure_names[di].lexeme);
                        ctx.current_params.insert(param.destructure_names[di].lexeme);
                    }
                }
            } else if (isTrackedTypeObj(ctx, *pt)) {
                state[func.params[i].name.lexeme] = State::Live;
                param_names.insert(func.params[i].name.lexeme);
                ctx.current_params.insert(func.params[i].name.lexeme);
                param_tracked[i] = true;
            }
        }
	} else {
		// Fallback: read each param's type annotation directly (methods).
		// base_name unwaps optionals/owned, so optional tracked params register.
		//
		// M13: also check if the param type matches a function type parameter
		// (e.g., `val as T` in a generic method). Generic type params are
		// always boxed at runtime, so conservatively treat them as tracked.
		std::set<std::string> func_type_params;
		for (const auto& tp : func.type_params)
			func_type_params.insert(tp.lexeme);
		for (size_t i = 0; i < func.params.size(); i++) {
			std::string tn = base_name(func.params[i].type.get());
			bool is_generic = !tn.empty() && func_type_params.count(tn) > 0;
			if ((!tn.empty() && ctx.tracked_types.count(tn)) || is_generic) {
				state[func.params[i].name.lexeme] = State::Live;
				param_names.insert(func.params[i].name.lexeme);
				ctx.current_params.insert(func.params[i].name.lexeme);
				param_tracked[i] = true;
			}
		}
	}

    // H5: interprocedural borrow propagation. When a function receives both a
    // ref<T> and a tracked T, the ref may point to the tracked param. Seed
    // ctx.borrows so a later drop of the referent inside this function flags
    // E509 when the ref is subsequently read. Without this, borrow tracking is
    // purely intraprocedural — a callee that drops a referent won't detect the
    // dangling ref.
    //
    // Try resolved FunctionType first (top-level functions), then fall back to
    // AST type annotations (methods, where the FunctionType isn't resolvable by
    // bare name). A ref<T> is represented as GenericType("ref", [T]) in the AST.
    if (sem_sym && sem_sym->type && sem_sym->type->kind == TypeKind::FUNCTION) {
        auto fn_type = std::dynamic_pointer_cast<FunctionType>(sem_sym->type);
        for (size_t i = 0; i < fn_type->param_types.size() && i < func.params.size(); i++) {
            auto& pt_i = fn_type->param_types[i];
            if (!pt_i || pt_i->kind != TypeKind::REF) continue;
            auto* ref_type = dynamic_cast<const RefType*>(pt_i.get());
            if (!ref_type || !ref_type->inner_type) continue;
            for (size_t j = 0; j < fn_type->param_types.size() && j < func.params.size(); j++) {
                if (i == j) continue;
                auto& pt_j = fn_type->param_types[j];
                if (!pt_j || !isTrackedTypeObj(ctx, *pt_j)) continue;
                // Compare the ref's inner type with the tracked param's type.
                // Canonical pointer identity (via sameType) is correct — both
                // resolve to the same ClassType/Datatype declaration.
                if (sameType(ref_type->inner_type, pt_j)) {
                    ctx.borrows[func.params[i].name.lexeme] = func.params[j].name.lexeme;
                    break;  // one match suffices; borrows is a single-valued map
                }
            }
        }
    } else {
        // Fallback: read param types from AST annotations (methods).
        for (size_t i = 0; i < func.params.size(); i++) {
            auto* ast_type = func.params[i].type.get();
            if (!ast_type) continue;
            auto* generic = dynamic_cast<const GenericType*>(ast_type);
            if (!generic || generic->name.lexeme != "ref" || generic->arguments.size() != 1)
                continue;
            std::string inner_name = base_name(generic->arguments[0].get());
            if (inner_name.empty()) continue;
            for (size_t j = 0; j < func.params.size(); j++) {
                if (i == j) continue;
                std::string tn = base_name(func.params[j].type.get());
                if (!tn.empty() && ctx.tracked_types.count(tn) && tn == inner_name) {
                    ctx.borrows[func.params[i].name.lexeme] = func.params[j].name.lexeme;
                    break;
                }
            }
        }
    }

    // H8: cross-function field tracking. For non-constructor methods,
    // seed all tracked fields of `this` as Live. This causes the first
    // field overwrite in a regular method to trigger E501 (leak) unless
    // the field was explicitly dropped first. Constructors (`init`) are
    // exempt — fields start uninitialized and the first assignment is
    // always clean.
    if (func.has_this && func.name.lexeme != "init") {
        auto dot = summary_key.rfind('.');
        if (dot != std::string::npos) {
            std::string cls_name = summary_key.substr(0, dot);
            auto sym = const_cast<SymbolTable&>(
                ctx.tc.getSymbolTable()).resolve(cls_name);
            if (sym && sym->type && sym->type->kind == TypeKind::CLASS) {
                auto cls = std::dynamic_pointer_cast<ClassType>(sym->type);
                if (cls) {
                    for (const auto& [fname, finfo] : cls->fields) {
                        if (finfo.type && isTrackedTypeObj(ctx, *finfo.type)) {
                            state["this." + fname] = State::Live;
                        }
                    }
                }
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
            // H8: skip field-state entries (keys containing '.'). Field state
            // is tracked per-method for overwrite detection only; fields are
            // owned by the object and not leaked at function exit.
            if (name.find('.') != std::string::npos) continue;
            if (st == State::Live && param_names.count(name) == 0) {
                // Only report leaks for locals allocated inside this function,
                // not for borrowed parameters (the caller owns those).
                bool is_builtin = ctx.builtin_heap_vars.count(name) > 0;
                auto msg = "🧬 Unfolded molecule — `" + name + "` is live when function `" +
                           func.name.lexeme + "` exits but was never dropped or returned. "
                           "Add `drop " + name + ";` before the function ends.";
                if (is_builtin)
                    warn(ctx, func.name, msg, "W521");
                else
                    diag(ctx, func.name, msg, "E501");
            }
        }
        // M11: check for mutexes held at function exit.
        for (const auto& [mtx_name, locked] : ctx.mutex_locked) {
            if (locked) {
                warn(ctx, func.name,
                    "\xf0\x9f\x94\x92 Mutex `" + mtx_name + "` is held when function `" +
                    func.name.lexeme + "` exits — potential deadlock. "
                    "Add `" + mtx_name + ".unlock();` before the function returns.",
                    "W521");
            }
        }
    }

    // Phase 4: build the positional function summary for interprocedural
    // analysis. summary[i] aligns to func.params[i] (call-arg positions —
    // `this` is not in params). Untracked params default to Borrowed so the
    // indices line up at the call site (arg i → summary[i]).
    //
    // For foreign functions, annotations (@consumes / @escape) are the only
    // source of summary data. For regular functions, annotations override
    // inference — if @consumes(0) is set, param 0 is Dropped regardless of
    // what the body analysis found.
    FunctionSummary summary(func.params.size(), ParamBehavior::Borrowed);

    // Apply @consumes / @escape annotations (takes precedence over inference).
    for (int idx : func.consumes_params) {
        if (idx >= 0 && static_cast<size_t>(idx) < func.params.size())
            summary[idx] = ParamBehavior::Dropped;
    }
    for (int idx : func.escape_params) {
        if (idx >= 0 && static_cast<size_t>(idx) < func.params.size())
            summary[idx] = ParamBehavior::Escaped;
    }

    // For regular functions (with bodies), fill in remaining params from
    // the data-flow state. Annotations take precedence over inference.
    for (size_t i = 0; i < func.params.size(); i++) {
        // Skip params that already have annotation-based behavior.
        if (func.consumes_params.count(static_cast<int>(i)) ||
            func.escape_params.count(static_cast<int>(i)))
            continue;
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
    StateMap state, bool& terminates,
    std::set<std::string>* declared_names)
{
    terminates = false;
    for (const auto& stmt : statements) {
        if (!stmt) continue;
        analyzeStmt(ctx, stmt, state, terminates, declared_names);
        if (terminates) break;
    }
    return state;
}


Chaperone::StateMap Chaperone::analyzeScopedBlock(Context& ctx,
    const std::vector<std::shared_ptr<Stmt>>& statements,
    StateMap state, bool& terminates)
{
    StateMap pre_state = state;
    std::set<std::string> declared_names;
    state = analyzeBlock(ctx, statements, state, terminates, &declared_names);
    // M1: scope cleanup — remove variables declared inside this block,
    // or restore their pre-block values if they shadow an outer variable.
    for (const auto& name : declared_names) {
        // L21: if this variable held a closure, remove it from the closure's
        // holders (the closure is no longer reachable through this variable).
        auto vtc = ctx.var_to_closure.find(name);
        if (vtc != ctx.var_to_closure.end()) {
            const Type* ft = vtc->second;
            auto holders_it = ctx.closure_holders.find(ft);
            if (holders_it != ctx.closure_holders.end()) {
                holders_it->second.erase(name);
                if (holders_it->second.empty())
                    ctx.closure_holders.erase(holders_it);
            }
            ctx.var_to_closure.erase(vtc);
        }

        auto pre_it = pre_state.find(name);
        if (pre_it != pre_state.end()) {
            state[name] = pre_it->second;
        } else {
            state.erase(name);
        }
    }
    return state;
}


void Chaperone::analyzeScopedStmt(Context& ctx,
    const std::shared_ptr<Stmt>& stmt,
    StateMap& state, bool& terminates)
{
    StateMap pre_state = state;
    std::set<std::string> declared_names;
    analyzeStmt(ctx, stmt, state, terminates, &declared_names);
    // M1: scope cleanup — remove variables declared by this statement,
    // or restore their pre-statement values if they shadow outer variables.
    for (const auto& name : declared_names) {
        // L21: if this variable held a closure, remove it from the closure's holders.
        auto vtc = ctx.var_to_closure.find(name);
        if (vtc != ctx.var_to_closure.end()) {
            const Type* ft = vtc->second;
            auto holders_it = ctx.closure_holders.find(ft);
            if (holders_it != ctx.closure_holders.end()) {
                holders_it->second.erase(name);
                if (holders_it->second.empty())
                    ctx.closure_holders.erase(holders_it);
            }
            ctx.var_to_closure.erase(vtc);
        }

        auto pre_it = pre_state.find(name);
        if (pre_it != pre_state.end()) {
            state[name] = pre_it->second;
        } else {
            state.erase(name);
        }
    }
}


// S8: Recursively collect variable names that are guaranteed to be dropped on
// every control-flow path through a statement. Used to populate the
// `finally_protected` set — a throw inside a try whose finally block
// guarantees to drop a variable should not false-flag that variable as a leak.
//
// The logic is the dual of definitelyReturns(): a drop is guaranteed iff it
// appears on every path.
//   BlockStmt — sequential; drops executed before later statements are
//               guaranteed regardless of what follows.
//   IfStmt with else — intersection of both branches.
//   IfStmt without else, loops — empty (not guaranteed to execute).
//   TryStmt — the finally block's guaranteed drops (finally runs on every path).
static std::set<std::string> collectGuaranteedDrops(const std::shared_ptr<Stmt>& stmt)
{
    std::set<std::string> result;
    if (!stmt) return result;

    if (auto* blk = dynamic_cast<const BlockStmt*>(stmt.get())) {
        for (const auto& s : blk->statements)
            for (const auto& name : collectGuaranteedDrops(s))
                result.insert(name);
        return result;
    }

    if (auto* d = dynamic_cast<const DropStmt*>(stmt.get())) {
        // H8: extract the state-map key from the drop target.
        if (auto* ve = dynamic_cast<const VarExpr*>(d->target.get())) {
            result.insert(ve->name.lexeme);
        } else if (auto* get = dynamic_cast<const GetExpr*>(d->target.get())) {
            std::string obj_name;
            if (dynamic_cast<const ThisExpr*>(get->object.get())) {
                obj_name = "this";
            } else if (auto* ove = dynamic_cast<const VarExpr*>(get->object.get())) {
                obj_name = ove->name.lexeme;
            }
            if (!obj_name.empty()) {
                result.insert(obj_name + "." + get->name.lexeme);
            }
        }
        return result;
    }

    if (auto* ifs = dynamic_cast<const IfStmt*>(stmt.get())) {
        if (!ifs->elseBranch) return result; // no else → not guaranteed
        auto then_drops = collectGuaranteedDrops(ifs->thenBranch);
        auto else_drops = collectGuaranteedDrops(ifs->elseBranch);
        for (const auto& name : then_drops)
            if (else_drops.count(name))
                result.insert(name);
        return result;
    }

    // TryStmt: the finally block runs on every exit path (normal, catch, throw),
    // so its guaranteed drops are always included.
    // The try and catch bodies are NOT guaranteed (try may throw early;
    // catch may not execute).
    if (auto* tr = dynamic_cast<const TryStmt*>(stmt.get())) {
        if (tr->finallyBlock)
            return collectGuaranteedDrops(tr->finallyBlock);
        return result;
    }

    // Loops (while/for/for-in): body may execute zero times → nothing guaranteed.
    // ReturnStmt, ThrowStmt, ExpressionStmt, VarDeclStmt, etc.: contribute no drops.
    return result;
}


void Chaperone::analyzeStmt(Context& ctx,
    const std::shared_ptr<Stmt>& stmt, StateMap& state, bool& terminates,
    std::set<std::string>* declared_names)
{
    terminates = false;
    if (!stmt) return;

    // --- VarDeclStmt ---
    if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
        // M1: track this declaration for scope cleanup.
        if (declared_names) {
            if (!var->destructure_names.empty()) {
                for (const auto& dn : var->destructure_names)
                    declared_names->insert(dn.lexeme);
            } else {
                declared_names->insert(var->name.lexeme);
            }
        }

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
        // L21: before analyzing the new initializer, if this variable already
        // held a closure from a previous declaration, remove it from the old
        // closure's holders (the new initializer will replace it).
        auto old_vtc = ctx.var_to_closure.find(var->name.lexeme);
        if (old_vtc != ctx.var_to_closure.end()) {
            const Type* old_ft = old_vtc->second;
            auto holders_it = ctx.closure_holders.find(old_ft);
            if (holders_it != ctx.closure_holders.end()) {
                holders_it->second.erase(var->name.lexeme);
                if (holders_it->second.empty())
                    ctx.closure_holders.erase(holders_it);
            }
            ctx.var_to_closure.erase(old_vtc);
        }

        if (var->initializer)
            analyzeExpr(ctx, var->initializer, state);

        // L21: closure capture tracking. If the initializer is a LambdaExpr,
        // the LambdaExpr handler stored pending captures in ctx — transfer
        // them into the persistent maps keyed by the new variable name.
        // If the initializer is a VarExpr that holds a closure (let g = f),
        // propagate the closure reference to the new variable.
        // If the initializer is a composite expression (RecordExpr, etc.)
        // containing a LambdaExpr, the pending data is also consumed — the
        // enclosing variable becomes a holder for the nested closure(s).
        if (var->initializer) {
            if (ctx.pending_closure_type) {
                // LambdaExpr (direct or nested in a composite) with pending captures.
                const Type* ft = ctx.pending_closure_type;
                auto init_type_it = ctx.tc.getExpressionTypes().find(var->initializer.get());
                bool should_consume = false;

                if (init_type_it != ctx.tc.getExpressionTypes().end() && init_type_it->second) {
                    if (init_type_it->second.get() == ft) {
                        // Direct LambdaExpr initializer — type matches exactly.
                        should_consume = true;
                    } else if (dynamic_cast<const RecordExpr*>(var->initializer.get())) {
                        // Composite initializer (struct literal) containing a
                        // LambdaExpr field. The pending data was set during
                        // analyzeExpr() on the field values and belongs to this
                        // variable even though the top-level type is not FUNCTION.
                        should_consume = true;
                    }
                }

                if (should_consume) {
                    if (!ctx.closure_captures.count(ft))
                        ctx.closure_captures[ft] = std::move(ctx.pending_closure_captures);
                    else
                        ctx.pending_closure_captures.clear();
                    ctx.closure_holders[ft].insert(var->name.lexeme);
                    ctx.var_to_closure[var->name.lexeme] = ft;
                }
                ctx.pending_closure_type = nullptr;
                ctx.pending_closure_captures.clear();
            } else if (auto* src_ve = dynamic_cast<const VarExpr*>(var->initializer.get())) {
                // VarExpr initializer: let g = f — propagate closure reference.
                auto src_vtc = ctx.var_to_closure.find(src_ve->name.lexeme);
                if (src_vtc != ctx.var_to_closure.end()) {
                    const Type* ft = src_vtc->second;
                    ctx.closure_holders[ft].insert(var->name.lexeme);
                    ctx.var_to_closure[var->name.lexeme] = ft;
                }
            }
        }

        auto it = state.find(var->name.lexeme);
        if (it != state.end() && it->second == State::Live) {
            bool is_builtin = ctx.builtin_heap_vars.count(var->name.lexeme) > 0;
            auto msg = "🧬 Unfolded molecule — `" + var->name.lexeme + "` held a live "
                       "allocation that is now overwritten without being dropped.";
            if (is_builtin)
                warn(ctx, var->name, msg, "W521");
            else
                diag(ctx, var->name, msg, "E501");
        }
        bool tracked = isTrackedVar(ctx, *var);
        state[var->name.lexeme] = tracked ? State::Live : State::Uninit;

        // M12: struct-literal borrow tracking. When a non-owned data struct
        // is initialized with a RecordExpr, any ref<T> fields that are
        // assigned tracked Live variables create borrow relationships. The
        // field (e.g. "c.r") borrows the tracked variable — a later drop of
        // the referent while the ref field still aliases it must flag E509.
        if (!tracked && var->initializer) {
            if (auto* rec = dynamic_cast<const RecordExpr*>(var->initializer.get())) {
                auto& var_types = ctx.tc.getVariableTypes();
                auto vt = var_types.find(var);
                if (vt != var_types.end() && vt->second &&
                    vt->second->kind == TypeKind::DATA) {
                    auto* data_type = dynamic_cast<const DataType*>(vt->second.get());
                    if (data_type) {
                        // Build a name→index map over RecordExpr keys.
                        std::map<std::string, size_t> key_index;
                        for (size_t i = 0; i < rec->keys.size(); i++)
                            key_index[rec->keys[i].lexeme] = i;
                        for (const auto& [fname, finfo] : data_type->fields) {
                            if (!finfo.type || finfo.type->kind != TypeKind::REF)
                                continue;
                            auto ki = key_index.find(fname);
                            if (ki == key_index.end() || ki->second >= rec->values.size())
                                continue;
                            auto* val_ve = dynamic_cast<const VarExpr*>(
                                rec->values[ki->second].get());
                            if (!val_ve) continue;
                            auto sit = state.find(val_ve->name.lexeme);
                            if (sit != state.end() && sit->second == State::Live) {
                                std::string field_key = var->name.lexeme + "." + fname;
                                ctx.borrows[field_key] = val_ve->name.lexeme;
                            }
                        }
                    }
                }
            }
        }

        // Phase B: record whether this variable is a built-in heap type
        // (string, list, record, etc.) vs a class/owned-data.  Leak
        // diagnostics use W521 (warning) for built-in types, E501 (error)
        // for class/owned-data.
        if (tracked) {
            auto& types = ctx.tc.getVariableTypes();
            auto tit = types.find(var);
            if (tit != types.end() && tit->second &&
                isBuiltinHeapType(*tit->second)) {
                ctx.builtin_heap_vars.insert(var->name.lexeme);
            }
        }

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

        // M12: S3 borrow propagation through struct fields. If the new binding
        // is a ref<T> initialized from a field access (GetExpr), record the
        // borrow relationship. Two cases:
        //   1. The field itself is ref<T> → propagate the existing borrow.
        //   2. The field is a tracked allocation → the ref borrows the parent
        //      object (since cascade-drop on the parent frees the field).
        // Without this, `let r as ref<Buf> = c.buf; drop c; r.get()` is a
        // silent dangling ref — the Chaperone sees no relationship.
        if (!tracked && var->initializer) {
            auto& types = ctx.tc.getVariableTypes();
            auto tit = types.find(var);
            if (tit != types.end() && tit->second &&
                tit->second->kind == TypeKind::REF) {
                if (auto* get = dynamic_cast<const GetExpr*>(var->initializer.get())) {
                    // Extract the object (parent) name for borrow tracking.
                    std::string obj_name;
                    if (dynamic_cast<const ThisExpr*>(get->object.get())) {
                        obj_name = "this";
                    } else if (auto* ove = dynamic_cast<const VarExpr*>(get->object.get())) {
                        obj_name = ove->name.lexeme;
                    }
                    if (!obj_name.empty()) {
                        std::string field_key = obj_name + "." + get->name.lexeme;
                        // Case 1: propagate from an existing ref<T> field borrow
                        // (e.g. `let r2 = c.ref_field` where ref_field is ref<T>).
                        auto bit = ctx.borrows.find(field_key);
                        if (bit != ctx.borrows.end()) {
                            ctx.borrows[var->name.lexeme] = bit->second;
                        } else {
                            // Case 2: the field is a tracked allocation owned by
                            // the parent — the ref borrows the parent object
                            // (dropping the parent cascade-drops the field).
                            auto& expr_types = ctx.tc.getExpressionTypes();
                            auto et = expr_types.find(get);
                            if (et != expr_types.end() && et->second &&
                                isTrackedTypeObj(ctx, *et->second)) {
                                // Check the parent is tracked and Live.
                                auto pit = state.find(obj_name);
                                if (pit != state.end() && pit->second == State::Live) {
                                    ctx.borrows[var->name.lexeme] = obj_name;
                                }
                            }
                        }
                    }
                }
            }
        }

        // H10: function pointer alias tracking. When a variable is initialized
        // with a direct function reference (e.g. `let f = borrow;`), record the
        // alias so that indirect calls through the variable resolve to the
        // aliased function's summary instead of defaulting to Borrow.
        if (var->initializer) {
            if (auto* vve = dynamic_cast<const VarExpr*>(var->initializer.get())) {
                if (ctx.summaries.count(vve->name.lexeme)) {
                    ctx.function_aliases[var->name.lexeme] = vve->name.lexeme;
                }
            }
        }
        return;
    }

    // --- DropStmt ---
    if (auto* drop = dynamic_cast<const DropStmt*>(stmt.get())) {
        // H8: determine the state-map key for the drop target.
        //   - `drop x`         → key = "x" (VarExpr)
        //   - `drop this.f`    → key = "this.f" (GetExpr on ThisExpr)
        //   - `drop obj.f`     → key = "obj.f" (GetExpr on VarExpr)
        std::string key;
        const Token* diag_tok = &drop->name;  // source location for diagnostics

        if (auto* ve = dynamic_cast<const VarExpr*>(drop->target.get())) {
            key = ve->name.lexeme;
            diag_tok = &ve->name;
        } else if (auto* get = dynamic_cast<const GetExpr*>(drop->target.get())) {
            // Build the field key: "<object>.<field>".
            // For `this.field`, object is ThisExpr → use "this".
            // For `obj.field`, object is VarExpr → use the variable name.
            std::string obj_name;
            if (dynamic_cast<const ThisExpr*>(get->object.get())) {
                obj_name = "this";
            } else if (auto* ove = dynamic_cast<const VarExpr*>(get->object.get())) {
                obj_name = ove->name.lexeme;
            }
            if (!obj_name.empty()) {
                key = obj_name + "." + get->name.lexeme;
                diag_tok = &get->name;
            }
        }

        if (key.empty()) {
            // Unsupported drop target (e.g., complex expression).
            diag(ctx, drop->name,
                "Cannot drop this expression — drop only supports variables "
                "and field access (`drop x` or `drop this.field`).",
                "E503");
            return;
        }

        auto it = state.find(key);
        if (it == state.end() || it->second == State::Uninit) {
            // Variable/field not tracked — check if it's a heap-allocated
            // type (string, list, record, etc.) that we can still drop.
            bool is_heap_var = false;

            // H8: for field drops (GetExpr target), check the field's resolved
            // type via the TypeChecker. If it's a tracked field that wasn't yet
            // registered in the state map (e.g., assigned in a constructor or
            // previous method call), register it now so we can drop it.
            if (it == state.end() && dynamic_cast<const GetExpr*>(drop->target.get())) {
                auto& expr_types = ctx.tc.getExpressionTypes();
                auto tt = expr_types.find(drop->target.get());
                if (tt != expr_types.end() && tt->second) {
                    if (isTrackedTypeObj(ctx, *tt->second)) {
                        // Tracked field not yet in state map — register and drop.
                        state[key] = State::Dropped;
                        return;
                    } else if (isHeapAllocatedType(ctx, *tt->second)) {
                        is_heap_var = true;
                    }
                }
            }

            // For variables not in the state map (globals, undeclared), resolve
            // via the symbol table. For Uninit locals (declared but not tracked),
            // look up the type via the TypeChecker's variable-types map.
            if (it == state.end()) {
                auto sym = const_cast<SymbolTable&>(
                    ctx.tc.getSymbolTable()).resolve(key);
                if (sym && sym->type) {
                    is_heap_var = isHeapAllocatedType(ctx, *sym->type);
                }
            } else {
                // Variable is Uninit in the state map — it was declared as a
                // local but not tracked (e.g. string, list). Walk the TypeChecker's
                // variable-types map to find its type by name.
                for (const auto& [decl, vtype] : ctx.tc.getVariableTypes()) {
                    if (decl && decl->name.lexeme == key && vtype) {
                        is_heap_var = isHeapAllocatedType(ctx, *vtype);
                        break;
                    }
                }
            }
            if (is_heap_var) {
                // Register and drop it — the codegen will emit finalize+free
                // which cleans up interior buffers via __ang_rt_finalize.
                state[key] = State::Dropped;
            } else {
                diag(ctx, *diag_tok,
                    "\xe2\x9a\xa0\xef\xb8\x8f Cannot drop `" + key + "` — not a tracked allocation.",
                    "E503");
            }
        } else if (it->second == State::Dropped) {
            diag(ctx, *diag_tok,
                "\xe2\x9a\xa0\xef\xb8\x8f Cannot drop `" + key + "` — it was already dropped, "
                "moved, or escaped. It's no longer live and cannot be dropped again.",
                "E503");
        } else if (it->second == State::Escaped) {
            if (ctx.thread_escaped.count(key)) {
                diag(ctx, *diag_tok,
                    "\xe2\x9a\xa0\xef\xb8\x8f Cannot drop `" + key + "` — ownership was "
                    "transferred to another thread via `spawn()`. Dropping it here "
                    "would double-free.",
                    "E503");
            } else {
                diag(ctx, *diag_tok,
                    "\xe2\x9a\xa0\xef\xb8\x8f Cannot drop `" + key + "` — ownership was transferred.",
                    "E503");
            }
        } else if (it->second == State::Moved) {
            diag(ctx, *diag_tok,
                "\xe2\x9a\xa0\xef\xb8\x8f Double denaturation — `" + key + "` had its ownership "
                "moved to another variable; dropping it would double-free. Drop the new owner instead.",
                "E503");
        } else {
            // L21: before dropping, check if any pending closure still captures
            // this variable. If so, emit E505 and transition to Escaped instead
            // of Dropped — the closure still holds a reference.
            if (checkPendingCaptures(ctx, key, state, *diag_tok)) {
                it->second = State::Escaped;
            } else {
                it->second = State::Dropped;
                // L21: if this variable held a closure (directly or via a
                // struct containing a closure), dropping it releases those
                // captures. Remove from tracking so subsequent drops of the
                // captured variables don't false-positive.
                auto vtc = ctx.var_to_closure.find(key);
                if (vtc != ctx.var_to_closure.end()) {
                    const Type* ft = vtc->second;
                    auto holders_it = ctx.closure_holders.find(ft);
                    if (holders_it != ctx.closure_holders.end()) {
                        holders_it->second.erase(key);
                        if (holders_it->second.empty())
                            ctx.closure_holders.erase(holders_it);
                    }
                    ctx.var_to_closure.erase(vtc);
                }
            }
        }
        return;
    }

    // --- ReturnStmt ---
    if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt.get())) {
        if (ret->value) {
            analyzeExpr(ctx, ret->value, state);
            if (auto* ve = dynamic_cast<const VarExpr*>(ret->value.get())) {
                auto it = state.find(ve->name.lexeme);
                if (it != state.end() && it->second == State::Live) {
                    // L21: before escaping the return value, check if any pending
                    // closure still captures it. Emit E505 if so.
                    checkPendingCaptures(ctx, ve->name.lexeme, state, ve->name);
                    it->second = State::Escaped;
                }
                // L21: if a closure variable itself is being returned, its
                // captured variables are now at risk of dangling. Emit E505 for
                // each Live captured variable that isn't also being returned.
                auto vtc = ctx.var_to_closure.find(ve->name.lexeme);
                if (vtc != ctx.var_to_closure.end()) {
                    const Type* ft = vtc->second;
                    auto cap_it = ctx.closure_captures.find(ft);
                    if (cap_it != ctx.closure_captures.end()) {
                        for (const auto& cap_name : cap_it->second) {
                            auto cit = state.find(cap_name);
                            if (cit != state.end() && cit->second == State::Live) {
                                diag(ctx, ve->name,
                                    "\U0001f9ec Escaped molecule \u2014 `" + cap_name +
                                    "` is captured by closure `" + ve->name.lexeme +
                                    "` which is being returned. The closure will outlive "
                                    "the captured value. Drop the captured value via the "
                                    "closure or return it together.",
                                    "E505");
                                cit->second = State::Escaped;
                            }
                        }
                    }
                    // The closure is escaping — remove it from pending tracking.
                    ctx.closure_holders.erase(ft);
                    ctx.var_to_closure.erase(vtc);
                }
            }
        }
        for (auto& [name, st] : state) {
            // H8: skip field-state entries (keys containing '.').
            if (name.find('.') != std::string::npos) continue;
            // Exclude tracked parameters — they're borrowed (caller-owned), not
            // this function's responsibility to drop.
            if (st == State::Live && ctx.current_params.count(name) == 0) {
                bool is_builtin = ctx.builtin_heap_vars.count(name) > 0;
                auto msg = "🧬 Unfolded molecule — `" + name + "` leaks on the return at line " +
                           std::to_string(ret->keyword.line) + ". Add `drop " + name + ";`.";
                if (is_builtin)
                    warn(ctx, ret->keyword, msg, "W521");
                else
                    diag(ctx, ret->keyword, msg, "E501");
            }
        }
        terminates = true;
        return;
    }

    // --- ThrowStmt ---
    if (auto* thr = dynamic_cast<const ThrowStmt*>(stmt.get())) {
        analyzeExpr(ctx, thr->expression, state);

        // L21: if a closure variable is being thrown, its captured variables
        // are now at risk of dangling (the closure escapes via the exception).
        if (auto* ve = dynamic_cast<const VarExpr*>(thr->expression.get())) {
            auto vtc = ctx.var_to_closure.find(ve->name.lexeme);
            if (vtc != ctx.var_to_closure.end()) {
                const Type* ft = vtc->second;
                auto cap_it = ctx.closure_captures.find(ft);
                if (cap_it != ctx.closure_captures.end()) {
                    for (const auto& cap_name : cap_it->second) {
                        auto cit = state.find(cap_name);
                        if (cit != state.end() && cit->second == State::Live) {
                            diag(ctx, ve->name,
                                "\U0001f9ec Escaped molecule \u2014 `" + cap_name +
                                "` is captured by closure `" + ve->name.lexeme +
                                "` which is being thrown. The closure will outlive "
                                "the captured value.",
                                "E505");
                            cit->second = State::Escaped;
                        }
                    }
                }
                ctx.closure_holders.erase(ft);
                ctx.var_to_closure.erase(vtc);
            }
        }
        // v5: report leaks on throw paths as errors. No auto-unwind —
        // the programmer must use `finally {}` for explicit cleanup.
        // S8: a name listed in a surrounding `finally {}`'s drops is
        // discharged (the finally runs on the throw path), so skip it.
        for (auto& [name, st] : state) {
            // H8: skip field-state entries (keys containing '.').
            if (name.find('.') != std::string::npos) continue;
            if (st == State::Live && ctx.finally_protected.count(name) == 0
                && ctx.current_params.count(name) == 0) {  // borrowed params excluded
                bool is_builtin = ctx.builtin_heap_vars.count(name) > 0;
                auto msg = "🧬 Unfolded molecule — `" + name + "` is live when `throw` "
                           "fires at line " + std::to_string(thr->keyword.line) + ". "
                           "Add `drop " + name + ";` before the throw, or wrap in "
                           "try/catch/finally with explicit cleanup.";
                if (is_builtin)
                    warn(ctx, thr->keyword, msg, "W521");
                else
                    diag(ctx, thr->keyword, msg, "E501");
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
                then_state = analyzeScopedBlock(ctx, blk->statements, then_state, then_term);
            else
                analyzeScopedStmt(ctx, ifs->thenBranch, then_state, then_term);
        }
        if (ifs->elseBranch) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(ifs->elseBranch.get()))
                else_state = analyzeScopedBlock(ctx, blk->statements, else_state, else_term);
            else
                analyzeScopedStmt(ctx, ifs->elseBranch, else_state, else_term);
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
            bool in_then = it_then != then_state.end();
            bool in_else = it_else != else_state.end();

            // Case 1: variable exists in both branches with different states.
            if (in_then && in_else && it_then->second != it_else->second) {
                if ((it_then->second == State::Live) != (it_else->second == State::Live)) {
                    // M2: inside a loop body, conditional destruction is a hard error
                    // (E506), not just a warning. If the variable was Live before the
                    // loop, destroying it on only one branch means it won't be available
                    // on the next iteration for that path — use-after-free / double-free.
                    if (!ctx.loop_pre_live.empty() && ctx.loop_pre_live.count(name)) {
                        diag(ctx, ifs->keyword,
                            "🔄 `" + name + "` is conditionally destroyed inside the loop — "
                            "it is dropped/moved/escaped on one branch but not the other. "
                            "It won't be available on the next iteration for that path. "
                            "Ensure it's handled consistently on all branches.",
                            "E506");
                    } else {
                        ctx.eh.warning(ifs->keyword,
                            "🔄 Incomplete fold — `" + name + "` is handled differently on "
                            "the two branches. Add `drop " + name + ";` to the path that's missing it.",
                            "W510");
                    }
                }
            }
            // M1: Case 2 — variable exists in only one branch and is Live there.
            // join_maps would silently promote it to Live in the merged state,
            // causing a confusing E501 later. Warn about the asymmetry.
            if (in_then && !in_else && it_then->second == State::Live) {
                ctx.eh.warning(ifs->keyword,
                    "🔄 Incomplete fold — `" + name + "` is live on the if-branch but "
                    "absent from the else-branch. It will appear live after the merge "
                    "and may leak. Add `drop " + name + ";` on the if-branch.",
                    "W510");
            }
            if (!in_then && in_else && it_else->second == State::Live) {
                ctx.eh.warning(ifs->keyword,
                    "🔄 Incomplete fold — `" + name + "` is live on the else-branch but "
                    "absent from the if-branch. It will appear live after the merge "
                    "and may leak. Add `drop " + name + ";` on the else-branch.",
                    "W510");
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
    //
    // M2: two complementary E506 checks for loop-body destruction:
    //   1. Unconditional: after body analysis, if a pre-Live var is non-Live
    //      in the merged body_state, it's destroyed on all paths → E506.
    //   2. Conditional: at if/else merge points inside the body, if a pre-Live
    //      var is destroyed on one branch but not the other (detected via the
    //      loop_pre_live set), the merge would mask it → E506 at the merge point.
    // Analyze the loop body and return the exact post-body state.
    // Callers use the returned state for condition/increment re-analysis
    // (representing the next iteration's entry state) and explicitly join
    // with the pre-loop state for code after the loop (which may execute
    // zero times).  Previously the condition re-analysis used the joined
    // state, which could mask use-after-free in the condition (LOW1).
    auto analyze_loop_body = [&](const Token& kw, const std::shared_ptr<Stmt>& body) -> StateMap {
        StateMap pre = state;

        // M2: save and populate loop_pre_live for conditional-destruction
        // detection at if/else merge points inside the loop body.
        auto saved_loop_pre_live = std::move(ctx.loop_pre_live);
        ctx.loop_pre_live.clear();
        for (auto& [name, st] : pre) {
            if (st == State::Live) ctx.loop_pre_live.insert(name);
        }

        StateMap body_state = state;
        bool body_term = false;
        if (body) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(body.get()))
                body_state = analyzeScopedBlock(ctx, blk->statements, body_state, body_term);
            else
                analyzeScopedStmt(ctx, body, body_state, body_term);
        }

        // Restore the outer loop's pre_live set (for nested loops).
        ctx.loop_pre_live = std::move(saved_loop_pre_live);

        // Loop-body drop/move/escape check (E506).
        for (auto& [name, st_pre] : pre) {
            if (st_pre == State::Live) {
                auto it2 = body_state.find(name);
                if (it2 != body_state.end() && it2->second != State::Live) {
                    const char* reason = "dropped";
                    if (it2->second == State::Moved) reason = "moved";
                    else if (it2->second == State::Escaped) reason = "escaped";
                    diag(ctx, kw,
                        "🔄 `" + name + "` is " + reason + " inside the loop but "
                        "allocated before it — it won't be available on iteration 2+.",
                        "E506");
                }
            }
        }
        return body_state;
    };

    if (auto* wh = dynamic_cast<const WhileStmt*>(stmt.get())) {
        if (wh->condition) analyzeExpr(ctx, wh->condition, state);   // S2: condition
        StateMap body_state = analyze_loop_body(wh->keyword, wh->body);
        if (wh->condition) analyzeExpr(ctx, wh->condition, body_state);  // re-evaluated each iter
        state = join_maps(state, body_state);  // post-loop: may execute zero times
        return;
    }
    if (auto* fors = dynamic_cast<const ForStmt*>(stmt.get())) {
        if (fors->initializer) { bool t; analyzeStmt(ctx, fors->initializer, state, t); }
        if (fors->condition) analyzeExpr(ctx, fors->condition, state);  // S2: condition
        StateMap body_state = analyze_loop_body(fors->keyword, fors->body);
        if (fors->increment) analyzeExpr(ctx, fors->increment, body_state);  // S2: increment
        if (fors->condition) analyzeExpr(ctx, fors->condition, body_state);  // re-checked each iter
        state = join_maps(state, body_state);  // post-loop: may execute zero times
        return;
    }
    if (auto* forin = dynamic_cast<const ForInStmt*>(stmt.get())) {
        if (forin->collection) analyzeExpr(ctx, forin->collection, state);  // S2: iterable
        StateMap body_state = analyze_loop_body(forin->name, forin->body);
        state = join_maps(state, body_state);  // post-loop: may execute zero times
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
        state = analyzeScopedBlock(ctx, blk->statements, state, blk_term);
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
            // S8: Use flow-sensitive recursive scan to find drops that are
            // guaranteed on every path through the finally block (not just
            // top-level DropStmts). Drops inside nested blocks and if/else
            // branches are recognized; drops inside loops or if-without-else
            // are correctly excluded (not guaranteed to execute).
            std::set<std::string> dropped = collectGuaranteedDrops(tryS->finallyBlock);
            ctx.finally_protected.insert(dropped.begin(), dropped.end());
        }

        StateMap try_state = state;
        bool try_term = false;
        if (tryS->tryBlock) {
            if (auto* blk = dynamic_cast<const BlockStmt*>(tryS->tryBlock.get()))
                try_state = analyzeScopedBlock(ctx, blk->statements, try_state, try_term);
            else
                analyzeScopedStmt(ctx, tryS->tryBlock, try_state, try_term);
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
                catch_state = analyzeScopedBlock(ctx, blk->statements, catch_state, catch_term);
            else
                analyzeScopedStmt(ctx, tryS->catchBlock, catch_state, catch_term);
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
                state = analyzeScopedBlock(ctx, blk->statements, state, finally_term);
            else
                analyzeScopedStmt(ctx, tryS->finallyBlock, state, finally_term);
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
            state = analyzeScopedBlock(ctx, unsafe->block->statements, state, blk_term);
            if (blk_term) terminates = true;
        }
        ctx.in_unsafe = was_unsafe;
        return;
    }
}

} // namespace angara
