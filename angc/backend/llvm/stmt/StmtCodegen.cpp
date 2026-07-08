#include "LLVMBackend.h"
#include "RuntimeBuilder.h"

namespace angara {

void LLVMBackend::cgStmt(const std::shared_ptr<Stmt>& s) {
    if (!s) return;
    if (auto* p = dynamic_cast<const VarDeclStmt*>(s.get())) { setDebugLoc(p->name); cgVarDecl(*p); }
    else if (auto* p = dynamic_cast<const ExpressionStmt*>(s.get())) {
        if (auto* ve = dynamic_cast<const VarExpr*>(p->expression.get())) setDebugLoc(ve->name);
        cg(p->expression);
    }
    else if (auto* p = dynamic_cast<const BlockStmt*>(s.get())) cgBlock(*p);
    else if (auto* p = dynamic_cast<const IfStmt*>(s.get())) { setDebugLoc(p->keyword); cgIf(*p); }
    else if (auto* p = dynamic_cast<const WhileStmt*>(s.get())) { setDebugLoc(p->keyword); cgWhile(*p); }
    else if (auto* p = dynamic_cast<const ForStmt*>(s.get())) { setDebugLoc(p->keyword); cgFor(*p); }
    else if (auto* p = dynamic_cast<const ForInStmt*>(s.get())) { setDebugLoc(p->keyword); cgForIn(*p); }
    else if (auto* p = dynamic_cast<const ReturnStmt*>(s.get())) { setDebugLoc(p->keyword); cgReturn(*p); }
    else if (auto* p = dynamic_cast<const BreakStmt*>(s.get())) {
        setDebugLoc(p->keyword);
        if (loopExit) {
            // BUG-5: pop try frames pushed inside this loop before leaving it,
            // so a later throw can't longjmp into a stale frame.
            if (!m_exc_loop_chain_saves.empty()) {
                auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
                builder->CreateStore(builder->CreateLoad(ptr_ty, m_exc_loop_chain_saves.back(), "brk_chain"),
                                     rt->getExceptionChain());
            }
            builder->CreateBr(loopExit);
        }
    }
    else if (auto* p = dynamic_cast<const ContinueStmt*>(s.get())) {
        setDebugLoc(p->keyword);
        if (loopContinue) {
            // BUG-5: continue re-enters the loop body; a try started in this
            // iteration must be popped so the next iteration is clean.
            if (!m_exc_loop_chain_saves.empty()) {
                auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
                builder->CreateStore(builder->CreateLoad(ptr_ty, m_exc_loop_chain_saves.back(), "cont_chain"),
                                     rt->getExceptionChain());
            }
            builder->CreateBr(loopContinue);
        }
    }
    else if (auto* p = dynamic_cast<const ThrowStmt*>(s.get())) { setDebugLoc(p->keyword); cgThrow(*p); }
    else if (auto* p = dynamic_cast<const TryStmt*>(s.get())) { setDebugLoc(p->catchName); cgTry(*p); }
    else if (auto* p = dynamic_cast<const DropStmt*>(s.get())) { setDebugLoc(p->name); cgDrop(*p); }
    else if (auto* p = dynamic_cast<const UnsafeBlockStmt*>(s.get())) {
        if (p->block) {
            for (auto& st : p->block->statements) {
                if (builder->GetInsertBlock()->getTerminator()) break;
                cgStmt(st);
            }
        }
    }
}

void LLVMBackend::cgVarDecl(const VarDeclStmt& s) {
    llvm::Value* v = nullptr;

    // LANG-10: destructuring declaration — evaluate RHS once, extract each element.
    if (!s.destructure_names.empty()) {
        if (s.initializer) {
            v = cg(s.initializer);
        } else {
            v = makeNil();
        }

        auto* fn = builder->GetInsertBlock()->getParent();
        // Store the tuple value into a temp alloca so we can load it repeatedly.
        auto* tmp_alloca = allocLocal(fn, "__tuple_tmp");
        builder->CreateStore(v, tmp_alloca);

        for (size_t i = 0; i < s.destructure_names.size(); ++i) {
            auto* idx_val = makeI64(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), i));
            auto* elem = callRtByName("__ang_list_get", {
                builder->CreateLoad(objType, tmp_alloca), idx_val
            });

            auto* a = allocLocal(fn, sanitize(s.destructure_names[i].lexeme));
            namedVals[sanitize(s.destructure_names[i].lexeme)] = a;
            namedKinds[sanitize(s.destructure_names[i].lexeme)] = LocalKind::BOXED;
            emitDbgDeclare(a, s.destructure_names[i].lexeme, s.destructure_names[i].line,
                          s.destructure_names[i].column, LocalKind::BOXED);
            builder->CreateStore(elem, a);
        }
        return;
    }

    if (s.initializer) {
        // TS-1: if this variable is list<Trait>, flow the element type down so a
        // list literal initializer boxes each element into a trait object.
        auto pre_type = m_type_checker.getVariableTypes().find(&s);
        if (pre_type != m_type_checker.getVariableTypes().end() && pre_type->second &&
            pre_type->second->kind == TypeKind::LIST) {
            m_expected_list_elem_type = std::dynamic_pointer_cast<ListType>(pre_type->second)->element_type;
        }
        v = cg(s.initializer);
        m_expected_list_elem_type.reset();
        // v5: data types copy-on-assign. If this variable is a plain `data`
        // type (not owned, not class — those are tracked for drop), deep-clone
        // the initializer so p2 is independent of p1.
        auto type_it2 = m_type_checker.getVariableTypes().find(&s);
        if (type_it2 != m_type_checker.getVariableTypes().end() && type_it2->second) {
            auto& vt = type_it2->second;
            // TS-1: box into a trait object if the variable is trait/contract-typed.
            v = maybeBoxTraitObject(v, s.initializer.get(), vt);
            if (vt->kind == TypeKind::DATA &&
                m_tracked_types.count(vt->toString()) == 0) {
                v = callRtByName("__ang_deep_clone", {v});
            }
        }
    } else if (s.typeAnnotation) {
        // Check if this is a foreign data type that needs default construction
        auto type_it = m_type_checker.getVariableTypes().find(&s);
        if (type_it != m_type_checker.getVariableTypes().end() &&
            type_it->second->kind == TypeKind::DATA) {
            auto dt = std::dynamic_pointer_cast<DataType>(type_it->second);
            if (dt && dt->is_foreign) {
                auto ctor_it = constructorLookup.find(dt->name);
                if (ctor_it != constructorLookup.end()) {
                    auto* ctor_fn = mod->getFunction(ctor_it->second);
                    if (ctor_fn) {
                        v = builder->CreateCall(ctor_fn, {});
                    }
                }
            }
        }
        if (!v) v = makeNil();
    } else {
        v = makeNil();
    }

    auto type_it = m_type_checker.getVariableTypes().find(&s);
    auto var_type = (type_it != m_type_checker.getVariableTypes().end()) ? type_it->second : nullptr;

    if (var_type && isSizedIntType(var_type)) {
        v = truncateForType(v, var_type);
    }

    if (auto* fn = builder->GetInsertBlock()->getParent()) {
        // LIB-4 Stage S: in async functions, use frame-based storage (no alloca).
        if (m_in_async_function) {
            auto slot_it = m_async_local_slots.find(s.name.lexeme);
            if (slot_it != m_async_local_slots.end()) {
                // L8: use unboxed LocalKind when the variable type allows it
                if (var_type) {
                    namedTypes[s.name.lexeme] = var_type;
                    namedKinds[s.name.lexeme] = isUnboxableType(var_type)
                        ? localKindForType(var_type) : LocalKind::BOXED;
                } else {
                    namedKinds[s.name.lexeme] = LocalKind::BOXED;
                }
                // Store directly to frame slot via storeVar (which routes to frame GEP)
                storeVar(s.name.lexeme, v);
                return;
            }
            // Fallthrough: variable not in pre-scanned slots (shouldn't happen)
        }
        auto* a = allocLocal(fn, s.name.lexeme, var_type);
        namedVals[s.name.lexeme] = a;
        if (var_type) {
            namedTypes[s.name.lexeme] = var_type;
            namedKinds[s.name.lexeme] = isUnboxableType(var_type)
                ? localKindForType(var_type) : LocalKind::BOXED;
        } else {
            namedKinds[s.name.lexeme] = LocalKind::BOXED;
        }
        // RT-2: emit debug info for this local so gdb/lldb can inspect it.
        emitDbgDeclare(a, s.name.lexeme, s.name.line, s.name.column,
                       namedKinds[s.name.lexeme]);
        storeVar(s.name.lexeme, v);
    }
}

void LLVMBackend::cgBlock(const BlockStmt& s) {
    auto sv = namedVals;
    auto st = namedTypes;
    auto sk = namedKinds;
    for (auto& stmt : s.statements) {
        if (builder->GetInsertBlock()->getTerminator()) break;
        cgStmt(stmt);
    }
    namedVals = sv;
    namedTypes = st;
    namedKinds = sk;
}

void LLVMBackend::cgIf(const IfStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* tb = llvm::BasicBlock::Create(*ctx,"then",fn);
    auto* eb = llvm::BasicBlock::Create(*ctx,"else",fn);
    auto* mg = llvm::BasicBlock::Create(*ctx,"ifm",fn);
    builder->CreateCondBr(isTruthy(cg(s.condition)), tb, eb);
    builder->SetInsertPoint(tb);
    cgStmt(s.thenBranch);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(mg);
    builder->SetInsertPoint(eb);
    if (s.elseBranch) cgStmt(s.elseBranch);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(mg);
    builder->SetInsertPoint(mg);
}

void LLVMBackend::cgWhile(const WhileStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* lp = llvm::BasicBlock::Create(*ctx,"wc",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"wb",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"we",fn);
    auto* sv = loopExit; auto* svc = loopContinue; loopExit = en; loopContinue = lp; loopDepth++;
    // BUG-5: snapshot the exception chain at loop entry so break/continue can
    // restore it, popping try frames pushed inside the loop body.
    if (auto* chain_gv = rt->getExceptionChain()) {
        llvm::IRBuilder<> lexc(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
        auto* loop_exc_save = lexc.CreateAlloca(llvm::PointerType::get(*ctx, 0), nullptr, "loop_exc_save");
        builder->CreateStore(builder->CreateLoad(llvm::PointerType::get(*ctx, 0), chain_gv),
                             loop_exc_save);
        m_exc_loop_chain_saves.push_back(loop_exc_save);
    }
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    builder->CreateCondBr(isTruthy(cg(s.condition)), bd, en);
    builder->SetInsertPoint(bd);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(lp);
    builder->SetInsertPoint(en);
    if (rt->getExceptionChain()) m_exc_loop_chain_saves.pop_back();
    loopExit = sv; loopContinue = svc; loopDepth--;
}

void LLVMBackend::cgFor(const ForStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto sv = namedVals;
    auto stv = namedTypes;
    auto skv = namedKinds;
    if (s.initializer) cgStmt(s.initializer);
    auto* lp = llvm::BasicBlock::Create(*ctx,"fc",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fb",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fe",fn);
    auto* inc = llvm::BasicBlock::Create(*ctx,"finc",fn);
    auto* sv2 = loopExit; auto* svc = loopContinue; loopExit = en; loopContinue = inc; loopDepth++;
    // BUG-5: snapshot the exception chain at loop entry (see cgWhile).
    if (auto* chain_gv = rt->getExceptionChain()) {
        llvm::IRBuilder<> lexc(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
        auto* loop_exc_save = lexc.CreateAlloca(llvm::PointerType::get(*ctx, 0), nullptr, "loop_exc_save");
        builder->CreateStore(builder->CreateLoad(llvm::PointerType::get(*ctx, 0), chain_gv),
                             loop_exc_save);
        m_exc_loop_chain_saves.push_back(loop_exc_save);
    }
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    if (s.condition) builder->CreateCondBr(isTruthy(cg(s.condition)), bd, en);
    else builder->CreateBr(bd);
    builder->SetInsertPoint(bd);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(inc);
    }
    builder->SetInsertPoint(inc);
    if (s.increment) cg(s.increment);
    builder->CreateBr(lp);
    builder->SetInsertPoint(en);
    if (rt->getExceptionChain()) m_exc_loop_chain_saves.pop_back();
    loopExit = sv2; loopContinue = svc; loopDepth--; namedVals = sv; namedTypes = stv; namedKinds = skv;
}

void LLVMBackend::cgForIn(const ForInStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();

    // LANG-1: if the iterable is a RangeExpr (0..n), emit a zero-allocation
    // C-style for loop — no list materialization, no __ang_list_get calls.
    if (auto* range = dynamic_cast<const RangeExpr*>(s.collection.get())) {
        auto* start = getI64(cg(range->left));
        auto* end = getI64(cg(range->right));
        bool inclusive = (range->op.type == TokenType::DOT_DOT_DOT);

        auto sv = namedVals;
        auto stv = namedTypes;
        auto skv = namedKinds;
        auto* ac = allocLocal(fn, s.name.lexeme);
        namedVals[s.name.lexeme] = ac;
        namedKinds[s.name.lexeme] = LocalKind::BOXED;

        auto* lp = llvm::BasicBlock::Create(*ctx, "rg_c", fn);
        auto* bd = llvm::BasicBlock::Create(*ctx, "rg_b", fn);
        auto* en = llvm::BasicBlock::Create(*ctx, "rg_e", fn);
        auto* sv2 = loopExit; auto* svc = loopContinue;
        loopExit = en; loopContinue = lp; loopDepth++;

        // BUG-5: snapshot exception chain at loop entry.
        if (auto* chain_gv = rt->getExceptionChain()) {
            llvm::IRBuilder<> lexc(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
            auto* loop_exc_save = lexc.CreateAlloca(llvm::PointerType::get(*ctx, 0), nullptr, "loop_exc_save");
            builder->CreateStore(builder->CreateLoad(llvm::PointerType::get(*ctx, 0), chain_gv),
                                 loop_exc_save);
            m_exc_loop_chain_saves.push_back(loop_exc_save);
        }

        // i = start; loop while i < end (exclusive) or i <= end (inclusive).
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
        auto* ia = tmp.CreateAlloca(llvm::Type::getInt64Ty(*ctx), nullptr, "__rg_i");
        builder->CreateStore(start, ia);
        builder->CreateBr(lp);

        builder->SetInsertPoint(lp);
        auto* i = builder->CreateLoad(llvm::Type::getInt64Ty(*ctx), ia, "rg_i");
        auto* cond = inclusive
            ? builder->CreateICmpSLE(i, end)
            : builder->CreateICmpSLT(i, end);
        builder->CreateCondBr(cond, bd, en);

        builder->SetInsertPoint(bd);
        builder->CreateStore(makeI64(i), ac);
        cgStmt(s.body);
        if (!builder->GetInsertBlock()->getTerminator()) {
            auto* next = builder->CreateAdd(i, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 1), "", false, true);
            builder->CreateStore(next, ia);
            builder->CreateBr(lp);
        }

        builder->SetInsertPoint(en);
        if (rt->getExceptionChain()) m_exc_loop_chain_saves.pop_back();
        loopExit = sv2; loopContinue = svc; loopDepth--;
        namedVals = sv; namedTypes = stv; namedKinds = skv;
        return;
    }

    // --- Existing list iteration path ---
    auto sv = namedVals;
    auto stv = namedTypes;
    auto skv = namedKinds;
    auto* iter = cg(s.collection);
    auto* len = callRtByName("__ang_len",{iter});
    auto* cnt = getI64(len);

    // LANG-10: destructuring for-in — allocate per-name locals
    llvm::AllocaInst* ac = nullptr;
    std::vector<llvm::AllocaInst*> destructure_allocs;
    if (!s.destructure_names.empty()) {
        for (const auto& dn : s.destructure_names) {
            auto* a = allocLocal(fn, sanitize(dn.lexeme));
            namedVals[sanitize(dn.lexeme)] = a;
            namedKinds[sanitize(dn.lexeme)] = LocalKind::BOXED;
            destructure_allocs.push_back(a);
        }
    } else {
        ac = allocLocal(fn, s.name.lexeme);
        namedVals[s.name.lexeme] = ac;
        namedKinds[s.name.lexeme] = LocalKind::BOXED;
    }

    auto* lp = llvm::BasicBlock::Create(*ctx,"fic",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fib",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fie",fn);
    auto* sv2 = loopExit; auto* svc = loopContinue; loopExit = en; loopContinue = lp; loopDepth++;
    // BUG-5: snapshot the exception chain at loop entry (see cgWhile).
    if (auto* chain_gv = rt->getExceptionChain()) {
        llvm::IRBuilder<> lexc(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
        auto* loop_exc_save = lexc.CreateAlloca(llvm::PointerType::get(*ctx, 0), nullptr, "loop_exc_save");
        builder->CreateStore(builder->CreateLoad(llvm::PointerType::get(*ctx, 0), chain_gv),
                             loop_exc_save);
        m_exc_loop_chain_saves.push_back(loop_exc_save);
    }
    // Raw i64 counter — no runtime root needed (never holds heap pointers)
    llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
    auto* ia = tmp.CreateAlloca(llvm::Type::getInt64Ty(*ctx), nullptr, "__fi");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),0), ia);
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    auto* i = builder->CreateLoad(llvm::Type::getInt64Ty(*ctx), ia, "i");
    builder->CreateCondBr(builder->CreateICmpSLT(i, cnt), bd, en);
    builder->SetInsertPoint(bd);
    // LANG-10: destructuring for-in — extract each position from the tuple element
    if (!s.destructure_names.empty()) {
        auto* elem = callRtByName("__ang_list_get",{iter, makeI64(i)});
        for (size_t di = 0; di < s.destructure_names.size(); ++di) {
            auto* sub_elem = callRtByName("__ang_list_get",{elem,
                makeI64(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), di))});
            builder->CreateStore(sub_elem, destructure_allocs[di]);
        }
    } else {
        builder->CreateStore(callRtByName("__ang_list_get",{iter, makeI64(i)}), ac);
    }
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateStore(builder->CreateAdd(i, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),1)), ia);
        builder->CreateBr(lp);
    }
    builder->SetInsertPoint(en);
    if (rt->getExceptionChain()) m_exc_loop_chain_saves.pop_back();
    loopExit = sv2; loopContinue = svc; loopDepth--; namedVals = sv; namedTypes = stv; namedKinds = skv;
}

void LLVMBackend::cgReturn(const ReturnStmt& s) {
    // LIB-4 Stage S: async return — store value in future frame, mark resolved,
    // cascade waker if set, then branch to suspend (which returns).
    if (m_in_async_function) {
        auto* fn_frame = builder->CreateBitCast(m_current_async_frame,
            m_current_async_frame_type->getPointerTo());
        auto* result = s.value ? cg(s.value) : makeNil();
        // Store result in frame (field 1)
        auto* res_ptr = builder->CreateStructGEP(m_current_async_frame_type, fn_frame, 1);
        builder->CreateStore(result, res_ptr);
        // Mark resolved (state = -1)
        auto* state_p = builder->CreateStructGEP(m_current_async_frame_type, fn_frame, 0);
        builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), -1), state_p);

        // Check for waker and cascade
        auto* ptr_ty = llvm::PointerType::get(*ctx, 0);
        auto* wfn_p = builder->CreateStructGEP(m_current_async_frame_type, fn_frame, 3);
        auto* wfn = builder->CreateLoad(ptr_ty, wfn_p, "wfn");
        auto* has_waker = builder->CreateIsNotNull(wfn);

        auto* fn = builder->GetInsertBlock()->getParent();
        auto* wake_bb = llvm::BasicBlock::Create(*ctx, "ret_wake", fn);
        auto* ret_bb = llvm::BasicBlock::Create(*ctx, "ret_suspend", fn);
        builder->CreateCondBr(has_waker, wake_bb, ret_bb);

        builder->SetInsertPoint(wake_bb);
        auto* wctx_p = builder->CreateStructGEP(m_current_async_frame_type, fn_frame, 4);
        auto* wctx = builder->CreateLoad(ptr_ty, wctx_p, "wctx");
        auto* waker_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx), {ptr_ty}, false);
        builder->CreateCall(waker_ty, wfn, {wctx});
        builder->CreateBr(ret_bb);

        builder->SetInsertPoint(ret_bb);
        // Branch to suspend block (which returns from resume function)
        if (m_async_suspend_bb) {
            builder->CreateBr(m_async_suspend_bb);
        } else {
            if (m_exc_chain_save) emitRtPopFrame();
            builder->CreateRetVoid();
        }
        return;
    }

    if (m_inlined_main_ret_alloca) {
        // Inlined main: extract i32 exit code from return value, branch to cleanup
        auto* result = s.value ? cg(s.value) : makeNil();
        auto* raw_i64 = builder->CreateExtractValue(result, {1});
        auto* exit_code = builder->CreateTrunc(raw_i64, llvm::Type::getInt32Ty(*ctx));
        builder->CreateStore(exit_code, m_inlined_main_ret_alloca);
        builder->CreateBr(m_inlined_main_cleanup_bb);
    } else if (m_current_raw_return_kind) {
        // Raw-signature function: unbox the return value
        auto* result = s.value ? cg(s.value) : makeNil();
        auto* raw = unboxToRaw(result, *m_current_raw_return_kind);
        if (m_exc_chain_save) emitRtPopFrame();
        builder->CreateRet(raw);
    } else {
        // RT-3: if the return value is a bare call, signal cgCall to mark it as
        // a tail call (best-effort TCK_Tail; cgCall may promote to TCK_MustTail
        // under the strict boxed+arity gate). The runtime pop frame is a no-op today
        // and must precede the call (not sit between call and ret) for the tail
        // marker to be meaningful.
        if (m_exc_chain_save) emitRtPopFrame();
        bool is_tail = s.value && dynamic_cast<const CallExpr*>(s.value.get());
        if (is_tail) m_pending_tail = llvm::CallInst::TCK_Tail;
        auto* result = s.value ? cg(s.value) : makeNil();
        m_pending_tail.reset();
        builder->CreateRet(result);
    }
}

void LLVMBackend::cgThrow(const ThrowStmt& s) {
    // The expression (e.g. Exception("msg")) already creates the exception
    // object via __ang_exception_new in cgCall, so just throw it directly.
    // v5: no auto-unwind — the Chaperone reports leaks on throw paths as
    // errors; the programmer uses `finally {}` for explicit cleanup.
    callRtByName("__ang_throw", {cg(s.expression)});
}

void LLVMBackend::cgTry(const TryStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* tryBB = llvm::BasicBlock::Create(*ctx,"try_body",fn);
    auto* catchBB = llvm::BasicBlock::Create(*ctx,"catch",fn);
    auto* finallyBB = s.finallyBlock ? llvm::BasicBlock::Create(*ctx,"finally",fn) : nullptr;
    auto* afterAll = llvm::BasicBlock::Create(*ctx,"after_try",fn);

    // H8: Use the single definition from RuntimeBuilder.
    auto* frameType = rt->getExcFrameType();
    auto* frame = builder->CreateAlloca(frameType);

    auto* frame_raw = builder->CreateBitCast(frame, llvm::PointerType::get(*ctx, 0));
    auto* prev_addr = builder->CreateStructGEP(frameType, frame, 1);
    auto* old_chain = builder->CreateLoad(llvm::PointerType::get(*ctx, 0),
        rt->getExceptionChain(), "old_chain");
    builder->CreateStore(old_chain, prev_addr);
    builder->CreateStore(frame_raw, rt->getExceptionChain());

    auto* jmp_buf_ptr = builder->CreateStructGEP(frameType, frame, 0);
    auto* i8_ptr_ty = llvm::PointerType::get(*ctx, 0);
    auto* setjmp_fn = fn->getParent()->getFunction("setjmp");
    auto* sr = builder->CreateCall(
        llvm::FunctionType::get(llvm::Type::getInt32Ty(*ctx), {i8_ptr_ty}, false),
        setjmp_fn,
        {builder->CreateBitCast(jmp_buf_ptr, i8_ptr_ty)}, "setjmp_result");
    if (auto* ci = llvm::dyn_cast<llvm::CallInst>(sr)) {
        ci->addFnAttr(llvm::Attribute::ReturnsTwice);
    }

    builder->CreateCondBr(
        builder->CreateICmpEQ(sr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx),0)),
        tryBB, catchBB);

    builder->SetInsertPoint(tryBB);
    cgStmt(s.tryBlock);
    if (!builder->GetInsertBlock()->getTerminator()) {
        callRtByName("__ang_try_end",{});
        builder->CreateBr(s.finallyBlock ? finallyBB : afterAll);
    }

    builder->SetInsertPoint(catchBB);
    if (s.catchBlock) {
        auto* exc = builder->CreateLoad(objType, rt->getCurrentException(), "exc");
        auto sv = namedVals;
        auto st = namedTypes;
        auto sk = namedKinds;
        auto* ea = allocLocal(fn, s.catchName.lexeme);
        builder->CreateStore(exc, ea);
        namedVals[s.catchName.lexeme] = ea;
        namedKinds[s.catchName.lexeme] = LocalKind::BOXED;
        cgStmt(s.catchBlock);
        namedVals = sv;
        namedTypes = st;
        namedKinds = sk;
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(s.finallyBlock ? finallyBB : afterAll);
    }

    // v5: finally block — runs on both normal and catch paths.
    if (s.finallyBlock) {
        builder->SetInsertPoint(finallyBB);
        cgStmt(s.finallyBlock);
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(afterAll);
        }
    }

    builder->SetInsertPoint(afterAll);
}

void LLVMBackend::cgDrop(const DropStmt& s) {
    // H8: support field drops (`drop this.field`) in addition to variable drops.
    // For field drops, load the field value via __ang_record_get, cascade-drop
    // its sub-fields, finalize+free it, then store nil back via __ang_record_set.
    if (auto* get = dynamic_cast<const GetExpr*>(s.target.get())) {
        // --- Field drop: drop obj.field ---
        auto* obj = cg(get->object);

        // Get the field value from the object.
        auto* field_val = callRtByName("__ang_record_get",
            {obj, builder->CreateGlobalStringPtr(get->name.lexeme, "fld")});

        // Nil guard: if the field is nil, skip deallocation.
        auto* tag = builder->CreateExtractValue(field_val, {0});
        auto* is_nil = builder->CreateICmpEQ(
            tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_NIL));

        auto* fn = builder->GetInsertBlock()->getParent();
        auto* notnil_bb = llvm::BasicBlock::Create(*ctx, "dropf_notnil", fn);
        auto* nil_bb    = llvm::BasicBlock::Create(*ctx, "dropf_nil", fn);
        auto* after_bb  = llvm::BasicBlock::Create(*ctx, "dropf_after", fn);

        builder->CreateCondBr(is_nil, nil_bb, notnil_bb);

        // --- Not-nil path: cascade-drop the field's sub-fields, then finalize+free ---
        builder->SetInsertPoint(notnil_bb);

        auto* payload = builder->CreateExtractValue(field_val, {1});
        auto* ptr_i64 = builder->CreateBitCast(payload, llvm::Type::getInt64Ty(*ctx));
        auto* obj_ptr = builder->CreateIntToPtr(ptr_i64, llvm::PointerType::get(*ctx, 0));

        // Look up the field's type for cascade-dropping its sub-fields.
        auto& expr_types = m_type_checker.getExpressionTypes();
        auto et = expr_types.find(s.target.get());
        if (et != expr_types.end() && et->second) {
            auto& type = et->second;

            auto drop_field = [&](const std::string& field_name) {
                auto* name_gstr = builder->CreateGlobalStringPtr(field_name, "ffld");
                auto* f_val = callRtByName("__ang_record_get", {field_val, name_gstr});
                auto* f_payload = builder->CreateExtractValue(f_val, {1});
                auto* f_ptr_i64 = builder->CreateBitCast(f_payload, llvm::Type::getInt64Ty(*ctx));
                auto* f_obj_ptr = builder->CreateIntToPtr(f_ptr_i64, llvm::PointerType::get(*ctx, 0));
                callRtByName("__ang_rt_finalize", {f_obj_ptr});
                callRtByName("__ang_rt_free",
                    {f_obj_ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0)});
            };

            auto is_heap_field = [&](const std::shared_ptr<Type>& t) -> bool {
                if (!t) return false;
                auto name = t->toString();
                return m_tracked_types.count(name) || m_heap_types.count(name);
            };

            if (type->kind == TypeKind::DATA) {
                auto dt = std::dynamic_pointer_cast<DataType>(type);
                if (dt) {
                    for (auto& [fname, finfo] : dt->fields) {
                        if (is_heap_field(finfo.type))
                            drop_field(fname);
                    }
                }
            } else if (type->kind == TypeKind::CLASS || type->kind == TypeKind::INSTANCE) {
                std::shared_ptr<ClassType> ct;
                if (type->kind == TypeKind::INSTANCE)
                    ct = std::dynamic_pointer_cast<InstanceType>(type)->class_type;
                else
                    ct = std::dynamic_pointer_cast<ClassType>(type);
                if (ct) {
                    for (auto& [fname, finfo] : ct->fields) {
                        if (is_heap_field(finfo.type))
                            drop_field(fname);
                    }
                }
            }
        }

        // Finalize + free the field value itself.
        callRtByName("__ang_rt_finalize", {obj_ptr});
        callRtByName("__ang_rt_free",
            {obj_ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0)});

        builder->CreateBr(after_bb);

        // --- Nil path: nothing to deallocate ---
        builder->SetInsertPoint(nil_bb);
        builder->CreateBr(after_bb);

        // --- After: store nil back to the field ---
        builder->SetInsertPoint(after_bb);
        callRtByName("__ang_record_set",
            {obj, builder->CreateGlobalStringPtr(get->name.lexeme, "fld_set"), makeNil()});
        return;
    }

    // --- Variable drop: existing logic ---
    llvm::Value* val = nullptr;
    llvm::AllocaInst* alloca = nullptr;
    bool is_async_slot = false;

    // LIB-4 Stage S: in async functions, load from frame slot.
    if (m_in_async_function) {
        auto slot_it = m_async_local_slots.find(s.name.lexeme);
        if (slot_it != m_async_local_slots.end()) {
            auto* typed_frame = builder->CreateBitCast(m_current_async_frame,
                llvm::PointerType::get(*ctx, 0));
            auto* field_ptr = builder->CreateStructGEP(m_current_async_frame_type,
                typed_frame, slot_it->second, s.name.lexeme + "_p");
            val = builder->CreateLoad(objType, field_ptr, "drop_val");
            is_async_slot = true;
        }
    }

    if (!val) {
        auto it = namedVals.find(s.name.lexeme);
        if (it == namedVals.end()) return;
        alloca = it->second;
        val = builder->CreateLoad(objType, alloca, "drop_val");
    }

    // Nil guard: if the value is nil (optional types or uninitialised),
    // there is nothing to deallocate.  Skip straight to invalidation.
    auto* tag = builder->CreateExtractValue(val, {0});
    auto* is_nil = builder->CreateICmpEQ(
        tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_NIL));

    auto* fn = builder->GetInsertBlock()->getParent();
    auto* notnil_bb = llvm::BasicBlock::Create(*ctx, "drop_notnil", fn);
    auto* nil_bb    = llvm::BasicBlock::Create(*ctx, "drop_nil", fn);
    auto* after_bb  = llvm::BasicBlock::Create(*ctx, "drop_after", fn);

    builder->CreateCondBr(is_nil, nil_bb, notnil_bb);

    // --- Not-nil path: extract heap pointer and perform the full drop ---
    builder->SetInsertPoint(notnil_bb);

    // H10: Also verify the tag is TAG_OBJ before treating the payload as a
    // heap pointer. Non-heap-allocated values (bare integers, floats, bools,
    // etc.) can reach drop via generic code paths — their payload is data,
    // not a pointer. Calling finalize/free on it would be UB.
    auto* is_obj = builder->CreateICmpEQ(
        tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), TAG_OBJ));
    auto* obj_bb = llvm::BasicBlock::Create(*ctx, "drop_obj", fn);
    builder->CreateCondBr(is_obj, obj_bb, nil_bb);

    builder->SetInsertPoint(obj_bb);

    // Extract the heap pointer from the AngaraObject payload.
    auto* payload = builder->CreateExtractValue(val, {1});
    auto* ptr_i64 = builder->CreateBitCast(payload, llvm::Type::getInt64Ty(*ctx));
    auto* obj_ptr = builder->CreateIntToPtr(ptr_i64, llvm::PointerType::get(*ctx, 0));

    // v5: Drop cascade — for each tracked or heap-allocated field, load it via
    // __ang_record_get and drop it before freeing the parent. Fields are emitted
    // at compile time based on the type declaration; ref<T> fields are NOT
    // cascaded (non-owning).  Built-in heap types (string, list, record, etc.)
    // are included so their interior buffers are freed via __ang_rt_finalize.
    auto type_it = namedTypes.find(s.name.lexeme);
    if (type_it != namedTypes.end() && type_it->second) {
        auto& type = type_it->second;

        auto drop_field = [&](const std::string& field_name) {
            auto* name_gstr = builder->CreateGlobalStringPtr(field_name, "fld");
            auto* field_val = callRtByName("__ang_record_get", {val, name_gstr});
            auto* f_payload = builder->CreateExtractValue(field_val, {1});
            auto* f_ptr_i64 = builder->CreateBitCast(f_payload, llvm::Type::getInt64Ty(*ctx));
            auto* f_obj_ptr = builder->CreateIntToPtr(f_ptr_i64, llvm::PointerType::get(*ctx, 0));
            callRtByName("__ang_rt_finalize", {f_obj_ptr});
            callRtByName("__ang_rt_free",
                {f_obj_ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0)});
        };

        // Helper: returns true if a field type is heap-allocated and needs
        // cascade (finalize + free) when the parent is dropped.
        auto is_heap_field = [&](const std::shared_ptr<Type>& t) -> bool {
            if (!t) return false;
            auto name = t->toString();
            return m_tracked_types.count(name) || m_heap_types.count(name);
        };

        if (type->kind == TypeKind::DATA) {
            auto dt = std::dynamic_pointer_cast<DataType>(type);
            if (dt) {
                for (auto& [fname, finfo] : dt->fields) {
                    if (is_heap_field(finfo.type))
                        drop_field(fname);
                }
            }
        } else if (type->kind == TypeKind::CLASS || type->kind == TypeKind::INSTANCE) {
            std::shared_ptr<ClassType> ct;
            if (type->kind == TypeKind::INSTANCE)
                ct = std::dynamic_pointer_cast<InstanceType>(type)->class_type;
            else
                ct = std::dynamic_pointer_cast<ClassType>(type);
            // H16: walk the superclass chain so inherited heap fields are
            // dropped too. Previously only the immediate class's fields were
            // iterated, leaking any heap allocation held in a parent class's
            // fields.
            for (auto cur = ct; cur; cur = cur->superclass) {
                for (auto& [fname, finfo] : cur->fields) {
                    if (is_heap_field(finfo.type))
                        drop_field(fname);
                }
            }
        }
    }

    // Finalize + free the parent.
    callRtByName("__ang_rt_finalize", {obj_ptr});
    callRtByName("__ang_rt_free",
        {obj_ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0)});

    builder->CreateBr(after_bb);

    // --- Nil path: nothing to deallocate ---
    builder->SetInsertPoint(nil_bb);
    builder->CreateBr(after_bb);

    // --- After: invalidate the variable ---
    builder->SetInsertPoint(after_bb);
    // LIB-4 Stage S: in async functions, store nil to frame slot.
    if (is_async_slot) {
        auto slot_it = m_async_local_slots.find(s.name.lexeme);
        if (slot_it != m_async_local_slots.end()) {
            auto* typed_frame = builder->CreateBitCast(m_current_async_frame,
                llvm::PointerType::get(*ctx, 0));
            auto* field_ptr = builder->CreateStructGEP(m_current_async_frame_type,
                typed_frame, slot_it->second, s.name.lexeme + "_p");
            builder->CreateStore(makeNil(), field_ptr);
            return;
        }
    }
    builder->CreateStore(makeNil(), alloca);
}

}
