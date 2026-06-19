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
        setDebugLoc(0, 0);
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
        setDebugLoc(0, 0);
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

    if (s.initializer) {
        v = cg(s.initializer);
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
        auto* a = allocLocal(fn, s.name.lexeme, var_type);
        namedVals[s.name.lexeme] = a;
        if (var_type) {
            namedTypes[s.name.lexeme] = var_type;
            namedKinds[s.name.lexeme] = isUnboxableType(var_type)
                ? localKindForType(var_type) : LocalKind::BOXED;
        } else {
            namedKinds[s.name.lexeme] = LocalKind::BOXED;
        }
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
    auto sv = namedVals;
    auto stv = namedTypes;
    auto skv = namedKinds;
    auto* iter = cg(s.collection);
    auto* len = callRtByName("__ang_len",{iter});
    auto* cnt = getI64(len);
    auto* ac = allocLocal(fn, s.name.lexeme);
    namedVals[s.name.lexeme] = ac;
    namedKinds[s.name.lexeme] = LocalKind::BOXED;
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
    // Raw i64 counter — no GC root needed (never holds heap pointers)
    llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().getFirstInsertionPt());
    auto* ia = tmp.CreateAlloca(llvm::Type::getInt64Ty(*ctx), nullptr, "__fi");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),0), ia);
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    auto* i = builder->CreateLoad(llvm::Type::getInt64Ty(*ctx), ia, "i");
    builder->CreateCondBr(builder->CreateICmpSLT(i, cnt), bd, en);
    builder->SetInsertPoint(bd);
    builder->CreateStore(callRtByName("__ang_list_get",{iter, makeI64(i)}), ac);
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
        if (m_exc_chain_save) emitGcPopFrame();
        builder->CreateRet(raw);
    } else {
        if (m_exc_chain_save) emitGcPopFrame();
        builder->CreateRet(s.value ? cg(s.value) : makeNil());
    }
}

void LLVMBackend::cgThrow(const ThrowStmt& s) {
    // v5: Chaperone exception unwinding — auto-drop live tracked variables
    // before the longjmp. The Chaperone populated m_drop_plan for this throw.
    auto it = m_drop_plan.find(static_cast<const void*>(&s));
    if (it != m_drop_plan.end()) {
        for (const auto& name : it->second) {
            auto var_it = namedVals.find(name);
            if (var_it == namedVals.end()) continue;
            auto* val = builder->CreateLoad(objType, var_it->second, "unwind_val");
            auto* payload = builder->CreateExtractValue(val, {1});
            auto* ptr_i64 = builder->CreateBitCast(payload, llvm::Type::getInt64Ty(*ctx));
            auto* obj_ptr = builder->CreateIntToPtr(ptr_i64, llvm::PointerType::get(*ctx, 0));
            callRtByName("__ang_gc_finalize", {obj_ptr});
            callRtByName("__ang_gc_free", {obj_ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0)});
            builder->CreateStore(makeNil(), var_it->second);
        }
    }
    // The expression (e.g. Exception("msg")) already creates the exception
    // object via __ang_exception_new in cgCall, so just throw it directly.
    callRtByName("__ang_throw", {cg(s.expression)});
}

void LLVMBackend::cgTry(const TryStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* tryBB = llvm::BasicBlock::Create(*ctx,"try_body",fn);
    auto* catchBB = llvm::BasicBlock::Create(*ctx,"catch",fn);
    auto* afterAll = llvm::BasicBlock::Create(*ctx,"after_try",fn);

    auto* frameType = llvm::StructType::create(*ctx,
        {llvm::ArrayType::get(llvm::Type::getInt8Ty(*ctx),512),
         llvm::PointerType::get(*ctx, 0)}, "EF");
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
        builder->CreateBr(afterAll);
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
        builder->CreateBr(afterAll);
    }

    builder->SetInsertPoint(afterAll);
}

void LLVMBackend::cgDrop(const DropStmt& s) {
    auto it = namedVals.find(s.name.lexeme);
    if (it == namedVals.end()) return;

    auto* alloca = it->second;
    auto* val = builder->CreateLoad(objType, alloca, "drop_val");

    // Extract the heap pointer from the AngaraObject payload.
    auto* payload = builder->CreateExtractValue(val, {1});
    auto* ptr_i64 = builder->CreateBitCast(payload, llvm::Type::getInt64Ty(*ctx));
    auto* obj_ptr = builder->CreateIntToPtr(ptr_i64, llvm::PointerType::get(*ctx, 0));

    // Call finalize (no-op stub for now — real finalizers come with Stage 3).
    callRtByName("__ang_gc_finalize", {obj_ptr});

    // Free via the Allocator.
    callRtByName("__ang_gc_free", {obj_ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx), 0)});

    // Invalidate the variable (store nil — the Chaperone pass in Stage 3 will
    // enforce that it's not used after this point).
    builder->CreateStore(makeNil(), alloca);
}

}
