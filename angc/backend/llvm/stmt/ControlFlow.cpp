#include "LLVMBackend.h"

namespace angara {

void LLVMBackend::codegenIfStmt(const IfStmt& stmt) {
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto& ctx = *m_context;

    // Handle `if let` for optional unwrapping
    if (stmt.declaration) {
        llvm::Value* init = codegenExpr(stmt.declaration->initializer);
        llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx, "if.let.then", fn);
        llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ctx, "if.let.else");
        llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "if.let.merge");

        llvm::Value* nil_check = isNil(init);
        llvm::Value* not_nil = m_builder->CreateNot(nil_check);
        m_builder->CreateCondBr(not_nil, then_bb, (stmt.elseBranch ? else_bb : merge_bb));

        m_builder->SetInsertPoint(then_bb);
        const std::string var_name = sanitizeName(stmt.declaration->name.lexeme);
        auto* alloca = createAlloca(fn, var_name);
        m_builder->CreateStore(init, alloca);
        auto saved = m_named_values;
        m_named_values[var_name] = alloca;
        codegenStmt(stmt.thenBranch);
        m_named_values = saved;
        then_bb = m_builder->GetInsertBlock(); // may have changed
        if (!then_bb->getTerminator())
            m_builder->CreateBr(merge_bb);

        if (stmt.elseBranch) {
            fn->insert(fn->end(), else_bb);
            m_builder->SetInsertPoint(else_bb);
            codegenStmt(stmt.elseBranch);
            else_bb = m_builder->GetInsertBlock();
            if (!else_bb->getTerminator())
                m_builder->CreateBr(merge_bb);
        }

        fn->insert(fn->end(), merge_bb);
        m_builder->SetInsertPoint(merge_bb);
        return;
    }

    // Regular `if` with boolean condition
    llvm::Value* cond = codegenExpr(stmt.condition);
    llvm::Value* truthy = isTruthy(cond);

    llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx, "if.then", fn);
    llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ctx, "if.else");
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "if.merge");

    m_builder->CreateCondBr(truthy, then_bb, stmt.elseBranch ? else_bb : merge_bb);

    m_builder->SetInsertPoint(then_bb);
    codegenStmt(stmt.thenBranch);
    then_bb = m_builder->GetInsertBlock();
    if (!then_bb->getTerminator())
        m_builder->CreateBr(merge_bb);

    if (stmt.elseBranch) {
        fn->insert(fn->end(), else_bb);
        m_builder->SetInsertPoint(else_bb);
        codegenStmt(stmt.elseBranch);
        else_bb = m_builder->GetInsertBlock();
        if (!else_bb->getTerminator())
            m_builder->CreateBr(merge_bb);
    }

    fn->insert(fn->end(), merge_bb);
    m_builder->SetInsertPoint(merge_bb);
}

void LLVMBackend::codegenWhileStmt(const WhileStmt& stmt) {
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto& ctx = *m_context;

    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(ctx, "while.cond", fn);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx, "while.body", fn);
    llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(ctx, "while.exit");

    auto* saved_exit = m_loop_exit_block;
    m_loop_exit_block = exit_bb;
    int saved_depth = m_loop_depth;
    m_loop_depth++;

    m_builder->CreateBr(cond_bb);

    m_builder->SetInsertPoint(cond_bb);
    llvm::Value* cond = codegenExpr(stmt.condition);
    llvm::Value* truthy = isTruthy(cond);
    m_builder->CreateCondBr(truthy, body_bb, exit_bb);

    m_builder->SetInsertPoint(body_bb);
    codegenStmt(stmt.body);
    if (!m_builder->GetInsertBlock()->getTerminator())
        m_builder->CreateBr(cond_bb);

    fn->insert(fn->end(), exit_bb);
    m_builder->SetInsertPoint(exit_bb);

    m_loop_exit_block = saved_exit;
    m_loop_depth = saved_depth;
}

void LLVMBackend::codegenForStmt(const ForStmt& stmt) {
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto& ctx = *m_context;

    auto saved_values = m_named_values;

    // Initializer
    if (stmt.initializer) codegenStmt(stmt.initializer);

    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(ctx, "for.cond", fn);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx, "for.body", fn);
    llvm::BasicBlock* inc_bb = llvm::BasicBlock::Create(ctx, "for.inc");
    llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(ctx, "for.exit");

    auto* saved_exit = m_loop_exit_block;
    m_loop_exit_block = exit_bb;

    m_builder->CreateBr(cond_bb);

    // Condition
    m_builder->SetInsertPoint(cond_bb);
    if (stmt.condition) {
        llvm::Value* cond = codegenExpr(stmt.condition);
        llvm::Value* truthy = isTruthy(cond);
        m_builder->CreateCondBr(truthy, body_bb, exit_bb);
    } else {
        m_builder->CreateBr(body_bb);
    }

    // Body
    m_builder->SetInsertPoint(body_bb);
    codegenStmt(stmt.body);
    if (!m_builder->GetInsertBlock()->getTerminator())
        m_builder->CreateBr(inc_bb);

    // Increment
    fn->insert(fn->end(), inc_bb);
    m_builder->SetInsertPoint(inc_bb);
    if (stmt.increment) codegenExpr(stmt.increment);
    m_builder->CreateBr(cond_bb);

    fn->insert(fn->end(), exit_bb);
    m_builder->SetInsertPoint(exit_bb);

    m_loop_exit_block = saved_exit;
    m_named_values = saved_values;
}

void LLVMBackend::codegenForInStmt(const ForInStmt& stmt) {
    // for (name in iterable) { body }
    // Desugars to: get iterator, loop with next
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto& ctx = *m_context;

    llvm::Value* iterable = codegenExpr(stmt.collection);
    llvm::Value* len_val = callRuntimeFunc("angara_len", {iterable});

    // Extract length as i64
    llvm::Value* len_i64 = extractI64(len_val);

    const std::string idx_name = "__for_idx_" + sanitizeName(stmt.name.lexeme);
    const std::string val_name = sanitizeName(stmt.name.lexeme);

    auto saved_values = m_named_values;

    // Alloca for index
    auto* idx_alloca = createAlloca(fn, idx_name);
    m_builder->CreateStore(createAngaraI64(0), idx_alloca);

    // Alloca for loop variable
    auto* val_alloca = createAlloca(fn, val_name);
    m_builder->CreateStore(createAngaraNil(), val_alloca);

    m_named_values[idx_name] = idx_alloca;
    m_named_values[val_name] = val_alloca;

    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(ctx, "forin.cond", fn);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx, "forin.body", fn);
    llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(ctx, "forin.exit");

    auto* saved_exit = m_loop_exit_block;
    m_loop_exit_block = exit_bb;

    m_builder->CreateBr(cond_bb);

    // Condition: idx < len
    m_builder->SetInsertPoint(cond_bb);
    llvm::Value* idx_obj = loadVariable(idx_name);
    llvm::Value* idx_i64 = extractI64(idx_obj);
    llvm::Value* cmp = m_builder->CreateICmpSLT(idx_i64, len_i64, "forin.cmp");
    m_builder->CreateCondBr(cmp, body_bb, exit_bb);

    // Body: get element, store in loop var, execute body, increment
    m_builder->SetInsertPoint(body_bb);
    llvm::Value* elem = callRuntimeFunc("angara_list_get", {iterable, idx_obj});
    m_builder->CreateStore(elem, val_alloca);
    codegenStmt(stmt.body);
    if (!m_builder->GetInsertBlock()->getTerminator()) {
        // Increment index
        llvm::Value* cur = extractI64(loadVariable(idx_name));
        llvm::Value* next = m_builder->CreateAdd(cur,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 1), "idx.inc");
        m_builder->CreateStore(callRuntimeFunc("angara_create_i64", {next}), idx_alloca);
        m_builder->CreateBr(cond_bb);
    }

    fn->insert(fn->end(), exit_bb);
    m_builder->SetInsertPoint(exit_bb);

    m_loop_exit_block = saved_exit;
    m_named_values = saved_values;
}

void LLVMBackend::codegenThrowStmt(const ThrowStmt& stmt) {
    // Create exception and call runtime throw
    llvm::Value* exc = codegenExpr(stmt.expression);
    // For now, use a simple runtime call to throw
    // This will eventually need setjmp/longjmp support
    callRuntimeFunc("angara_throw_error", {extractObj(exc)});

    // After throw, this is unreachable - but add a return for safety
    m_builder->CreateRet(createAngaraNil());
}

void LLVMBackend::codegenTryStmt(const TryStmt& stmt) {
    // Basic try/catch - generate try block, then catch block
    // Full implementation needs setjmp/longjmp integration with the runtime
    // For now, just generate the try body and catch body sequentially
    // The runtime's exception handling will manage the control flow

    auto saved_values = m_named_values;

    // Generate try body
    {
        auto tryBlock = std::dynamic_pointer_cast<const BlockStmt>(stmt.tryBlock);
        if (tryBlock) {
            for (const auto& s : tryBlock->statements)
                codegenStmt(s);
        }
    }

    // Generate catch block
    if (stmt.catchBlock) {
        auto catchBlock = std::dynamic_pointer_cast<const BlockStmt>(stmt.catchBlock);
        if (catchBlock) {
            for (const auto& s : catchBlock->statements)
                codegenStmt(s);
        }
    }

    m_named_values = saved_values;
}

void LLVMBackend::codegenUnsafeBlockStmt(const UnsafeBlockStmt& stmt) {
    // Unsafe blocks are treated as regular blocks for now
    // They'd need special handling for FFI operations
    for (const auto& s : stmt.block->statements)
        codegenStmt(s);
}

} // namespace angara