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
    else if (auto* p = dynamic_cast<const BreakStmt*>(s.get())) { setDebugLoc(0, 0); if (loopExit) builder->CreateBr(loopExit); }
    else if (auto* p = dynamic_cast<const ContinueStmt*>(s.get())) { setDebugLoc(0, 0); if (loopContinue) builder->CreateBr(loopContinue); }
    else if (auto* p = dynamic_cast<const ThrowStmt*>(s.get())) { setDebugLoc(p->keyword); cgThrow(*p); }
    else if (auto* p = dynamic_cast<const TryStmt*>(s.get())) { setDebugLoc(p->catchName); cgTry(*p); }
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
        auto* a = allocLocal(fn, s.name.lexeme);
        builder->CreateStore(v, a);
        namedVals[s.name.lexeme] = a;
        if (var_type) namedTypes[s.name.lexeme] = var_type;
    }
}

void LLVMBackend::cgBlock(const BlockStmt& s) {
    auto sv = namedVals;
    auto st = namedTypes;
    for (auto& stmt : s.statements) {
        if (builder->GetInsertBlock()->getTerminator()) break;
        cgStmt(stmt);
    }
    namedVals = sv;
    namedTypes = st;
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
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    builder->CreateCondBr(isTruthy(cg(s.condition)), bd, en);
    builder->SetInsertPoint(bd);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(lp);
    builder->SetInsertPoint(en);
    loopExit = sv; loopContinue = svc; loopDepth--;
}

void LLVMBackend::cgFor(const ForStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto sv = namedVals;
    auto stv = namedTypes;
    if (s.initializer) cgStmt(s.initializer);
    auto* lp = llvm::BasicBlock::Create(*ctx,"fc",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fb",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fe",fn);
    auto* inc = llvm::BasicBlock::Create(*ctx,"finc",fn);
    auto* sv2 = loopExit; auto* svc = loopContinue; loopExit = en; loopContinue = inc; loopDepth++;
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
    loopExit = sv2; loopContinue = svc; loopDepth--; namedVals = sv; namedTypes = stv;
}

void LLVMBackend::cgForIn(const ForInStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto sv = namedVals;
    auto stv = namedTypes;
    auto* iter = cg(s.collection);
    auto* len = callRtByName("__ang_len",{iter});
    auto* cnt = getI64(len);
    auto* ac = allocLocal(fn, s.name.lexeme);
    namedVals[s.name.lexeme] = ac;
    auto* lp = llvm::BasicBlock::Create(*ctx,"fic",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fib",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fie",fn);
    auto* sv2 = loopExit; auto* svc = loopContinue; loopExit = en; loopContinue = lp; loopDepth++;
    auto* ia = allocLocal(fn,"__fi");
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
    loopExit = sv2; loopContinue = svc; loopDepth--; namedVals = sv; namedTypes = stv;
}

void LLVMBackend::cgReturn(const ReturnStmt& s) {
    if (m_inlined_main_ret_alloca) {
        // Inlined main: extract i32 exit code from return value, branch to cleanup
        auto* result = s.value ? cg(s.value) : makeNil();
        auto* raw_i64 = builder->CreateExtractValue(result, {1});
        auto* exit_code = builder->CreateTrunc(raw_i64, llvm::Type::getInt32Ty(*ctx));
        builder->CreateStore(exit_code, m_inlined_main_ret_alloca);
        builder->CreateBr(m_inlined_main_cleanup_bb);
    } else {
        if (m_gc_current_frame) emitGcPopFrame();
        builder->CreateRet(s.value ? cg(s.value) : makeNil());
    }
}

void LLVMBackend::cgThrow(const ThrowStmt& s) {
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
        auto* ea = allocLocal(fn, s.catchName.lexeme);
        builder->CreateStore(exc, ea);
        namedVals[s.catchName.lexeme] = ea;
        cgStmt(s.catchBlock);
        namedVals = sv;
        namedTypes = st;
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(afterAll);
    }

    builder->SetInsertPoint(afterAll);
}

}
