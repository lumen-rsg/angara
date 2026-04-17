// Angara LLVM Backend — Statement Codegen
#include "LLVMBackend.h"
#include "RuntimeBuilder.h"

namespace angara {

void LLVMBackend::cgStmt(const std::shared_ptr<Stmt>& s) {
    if (!s) return;
    if (auto* p = dynamic_cast<const VarDeclStmt*>(s.get())) cgVarDecl(*p);
    else if (auto* p = dynamic_cast<const ExpressionStmt*>(s.get())) cg(p->expression);
    else if (auto* p = dynamic_cast<const BlockStmt*>(s.get())) cgBlock(*p);
    else if (auto* p = dynamic_cast<const IfStmt*>(s.get())) cgIf(*p);
    else if (auto* p = dynamic_cast<const WhileStmt*>(s.get())) cgWhile(*p);
    else if (auto* p = dynamic_cast<const ForStmt*>(s.get())) cgFor(*p);
    else if (auto* p = dynamic_cast<const ForInStmt*>(s.get())) cgForIn(*p);
    else if (auto* p = dynamic_cast<const ReturnStmt*>(s.get())) cgReturn(*p);
    else if (auto* p = dynamic_cast<const BreakStmt*>(s.get())) { if (loopExit) builder->CreateBr(loopExit); }
    else if (auto* p = dynamic_cast<const ThrowStmt*>(s.get())) cgThrow(*p);
    else if (auto* p = dynamic_cast<const TryStmt*>(s.get())) cgTry(*p);
    else if (auto* p = dynamic_cast<const UnsafeBlockStmt*>(s.get())) { if (p->block) for (auto& st : p->block->statements) cgStmt(st); }
}

void LLVMBackend::cgVarDecl(const VarDeclStmt& s) {
    auto* v = s.initializer ? cg(s.initializer) : makeNil();
    if (auto* fn = builder->GetInsertBlock()->getParent()) {
        auto* a = allocLocal(fn, s.name.lexeme);
        builder->CreateStore(v, a);
        namedVals[s.name.lexeme] = a;
    }
}

void LLVMBackend::cgBlock(const BlockStmt& s) {
    auto sv = namedVals;
    for (auto& st : s.statements) cgStmt(st);
    namedVals = sv;
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
    auto* sv = loopExit; loopExit = en; loopDepth++;
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    builder->CreateCondBr(isTruthy(cg(s.condition)), bd, en);
    builder->SetInsertPoint(bd);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) builder->CreateBr(lp);
    builder->SetInsertPoint(en);
    loopExit = sv; loopDepth--;
}

void LLVMBackend::cgFor(const ForStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto sv = namedVals;
    if (s.initializer) cgStmt(s.initializer);
    auto* lp = llvm::BasicBlock::Create(*ctx,"fc",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fb",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fe",fn);
    auto* sv2 = loopExit; loopExit = en; loopDepth++;
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    if (s.condition) builder->CreateCondBr(isTruthy(cg(s.condition)), bd, en);
    else builder->CreateBr(bd);
    builder->SetInsertPoint(bd);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (s.increment) cg(s.increment);
        builder->CreateBr(lp);
    }
    builder->SetInsertPoint(en);
    loopExit = sv2; loopDepth--; namedVals = sv;
}

void LLVMBackend::cgForIn(const ForInStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto sv = namedVals;
    auto* iter = cg(s.collection);
    auto* len = callRt(rt->getFuncLen(),{iter});
    auto* cnt = getI64(len);
    auto* ac = allocLocal(fn, s.name.lexeme);
    namedVals[s.name.lexeme] = ac;
    auto* lp = llvm::BasicBlock::Create(*ctx,"fic",fn);
    auto* bd = llvm::BasicBlock::Create(*ctx,"fib",fn);
    auto* en = llvm::BasicBlock::Create(*ctx,"fie",fn);
    auto* sv2 = loopExit; loopExit = en; loopDepth++;
    auto* ia = allocLocal(fn,"__fi");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),0), ia);
    builder->CreateBr(lp);
    builder->SetInsertPoint(lp);
    auto* i = builder->CreateLoad(llvm::Type::getInt64Ty(*ctx), ia, "i");
    builder->CreateCondBr(builder->CreateICmpSLT(i, cnt), bd, en);
    builder->SetInsertPoint(bd);
    builder->CreateStore(callRt(rt->getFuncListGet(),{iter, makeI64(i)}), ac);
    cgStmt(s.body);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateStore(builder->CreateAdd(i, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx),1)), ia);
        builder->CreateBr(lp);
    }
    builder->SetInsertPoint(en);
    loopExit = sv2; loopDepth--; namedVals = sv;
}

void LLVMBackend::cgReturn(const ReturnStmt& s) {
    builder->CreateRet(s.value ? cg(s.value) : makeNil());
}

void LLVMBackend::cgThrow(const ThrowStmt& s) {
    callRt(rt->getFuncThrow(), {callRt(rt->getFuncExceptionNew(), {cg(s.expression)})});
}

void LLVMBackend::cgTry(const TryStmt& s) {
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* tryBB = llvm::BasicBlock::Create(*ctx,"try_body",fn);
    auto* catchBB = llvm::BasicBlock::Create(*ctx,"catch",fn);
    auto* afterAll = llvm::BasicBlock::Create(*ctx,"after_try",fn);
    auto* frameType = llvm::StructType::create(*ctx,
        {llvm::ArrayType::get(llvm::Type::getInt8Ty(*ctx),200),
         llvm::PointerType::get(*ctx, 0)}, "EF");
    auto* frame = builder->CreateAlloca(frameType);
    auto* sr = callRt(rt->getFuncTryBegin(),
        {builder->CreateBitCast(frame, llvm::PointerType::get(*ctx, 0))});
    builder->CreateCondBr(
        builder->CreateICmpEQ(sr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx),0)),
        tryBB, catchBB);

    builder->SetInsertPoint(tryBB);
    cgStmt(s.tryBlock);
    if (!builder->GetInsertBlock()->getTerminator()) {
        callRt(rt->getFuncTryEnd(),{});
        builder->CreateBr(afterAll);
    }

    builder->SetInsertPoint(catchBB);
    if (s.catchBlock) {
        auto* exc = builder->CreateLoad(objType, rt->getCurrentException(), "exc");
        auto sv = namedVals;
        auto* ea = allocLocal(fn,"__exc");
        builder->CreateStore(exc, ea);
        namedVals["__exception"] = ea;
        cgStmt(s.catchBlock);
        namedVals = sv;
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(afterAll);
    }

    builder->SetInsertPoint(afterAll);
}

} // namespace angara