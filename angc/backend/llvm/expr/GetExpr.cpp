#include "LLVMBackend.h"

namespace angara {

llvm::Value* LLVMBackend::codegenGetExpr(const GetExpr& expr) {
    llvm::Value* obj = codegenExpr(expr.object);
    const std::string& name = expr.name.lexeme;
    auto obj_type = m_type_checker.m_expression_types.at(expr.object.get());

    // Record field access
    if (obj_type->kind == TypeKind::RECORD)
        return callRuntimeFunc("angara_record_get", {obj, m_builder->CreateGlobalStringPtr(name)});

    // List subscript sugar
    if (obj_type->kind == TypeKind::LIST && name == "len")
        return callRuntimeFunc("angara_len", {obj});

    // String properties
    if (obj_type->toString() == "string" && name == "len")
        return callRuntimeFunc("angara_len", {obj});

    // Instance field/method access (returns the object, method calls handled by CallExpr)
    if (obj_type->kind == TypeKind::INSTANCE)
        return obj; // Method dispatch is handled in codegenCallExpr

    // Module export access
    if (obj_type->kind == TypeKind::MODULE) {
        auto mod = std::dynamic_pointer_cast<ModuleType>(obj_type);
        // Check for exported class
        auto exp_it = mod->exports.find(name);
        if (exp_it != mod->exports.end()) {
            if (exp_it->second->kind == TypeKind::CLASS)
                return obj; // Constructor call handled in CallExpr
            // Closure variable
            std::string closure = "g_" + mod->name + "_" + name;
            return loadVariable(closure);
        }
    }

    // Enum variant or data class field
    if (obj_type->kind == TypeKind::DATA)
        return obj; // Handled elsewhere

    return obj;
}

llvm::Value* LLVMBackend::codegenListExpr(const ListExpr& expr) {
    if (expr.elements.empty())
        return callRuntimeFunc("angara_list_new", {});

    std::vector<llvm::Value*> elements;
    for (const auto& el : expr.elements)
        elements.push_back(codegenExpr(el));

    // Allocate array on stack
    auto* arr = m_builder->CreateAlloca(
        llvm::ArrayType::get(m_angara_obj_type, elements.size()));
    for (size_t i = 0; i < elements.size(); i++)
        m_builder->CreateStore(elements[i],
            m_builder->CreateGEP(llvm::ArrayType::get(m_angara_obj_type, elements.size()),
                arr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), 0),
                      llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), i)}));

    return callRuntimeFunc("angara_list_new_with_elements", {
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), elements.size()),
        m_builder->CreateBitCast(arr, llvm::PointerType::get(m_angara_obj_type, 0))});
}

llvm::Value* LLVMBackend::codegenRecordExpr(const RecordExpr& expr) {
    llvm::Value* rec = callRuntimeFunc("angara_record_new", {});
    for (size_t i = 0; i < expr.keys.size(); i++) {
        llvm::Value* val = codegenExpr(expr.values[i]);
        callRuntimeFunc("angara_record_set",
            {rec, m_builder->CreateGlobalStringPtr(expr.keys[i].lexeme), val});
    }
    return rec;
}

llvm::Value* LLVMBackend::codegenSubscriptExpr(const SubscriptExpr& expr) {
    llvm::Value* obj = codegenExpr(expr.object);
    llvm::Value* idx = codegenExpr(expr.index);
    auto obj_type = m_type_checker.m_expression_types.at(expr.object.get());

    if (obj_type->kind == TypeKind::LIST)
        return callRuntimeFunc("angara_list_get", {obj, idx});
    if (obj_type->toString() == "string")
        return callRuntimeFunc("angara_list_get", {obj, idx});

    // Record subscript with string key
    if (obj_type->kind == TypeKind::RECORD) {
        if (auto lit = std::dynamic_pointer_cast<const Literal>(expr.index)) {
            return callRuntimeFunc("angara_record_get",
                {obj, m_builder->CreateGlobalStringPtr(lit->token.lexeme)});
        }
    }

    return callRuntimeFunc("angara_list_get", {obj, idx});
}

llvm::Value* LLVMBackend::codegenIsExpr(const IsExpr& expr) {
    llvm::Value* left = codegenExpr(expr.object);
    llvm::Value* tag = extractTypeTag(left);

    auto check_type = m_type_checker.m_expression_types.at(&expr);
    // The type tag for nil is 0, bool is 1, i64 is 2, f64 is 3, string is 4, etc.
    // We compare the type tag against the expected type's tag value.
    // For now, delegate to a runtime type check
    llvm::Value* result = createAngaraBool(false);

    if (auto type_ptr = std::dynamic_pointer_cast<const PrimitiveType>(check_type)) {
        std::string type_name = type_ptr->name;
        int tag_val = 0;
        if (type_name == "nil") tag_val = 0;
        else if (type_name == "bool") tag_val = 1;
        else if (type_name == "i64" || type_name == "i32" || type_name == "i16" || type_name == "i8") tag_val = 2;
        else if (type_name == "f64" || type_name == "f32") tag_val = 3;
        else if (type_name == "string") tag_val = 4;
        llvm::Value* expected = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), tag_val);
        llvm::Value* cmp = m_builder->CreateICmpEQ(tag, expected, "is_check");
        return callRuntimeFunc("angara_create_bool", {cmp});
    }

    return result;
}

llvm::Value* LLVMBackend::codegenMatchExpr(const MatchExpr& expr) {
    // Match expression: evaluate subject, then check each arm
    llvm::Value* subject = codegenExpr(expr.condition);
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto& ctx = *m_context;

    llvm::BasicBlock* default_bb = llvm::BasicBlock::Create(ctx, "match.default");
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "match.merge");
    std::vector<llvm::BasicBlock*> arm_bbs;
    std::vector<llvm::BasicBlock*> check_bbs;

    for (size_t i = 0; i < expr.cases.size(); i++) {
        check_bbs.push_back(llvm::BasicBlock::Create(ctx, "match.check." + std::to_string(i)));
        arm_bbs.push_back(llvm::BasicBlock::Create(ctx, "match.arm." + std::to_string(i)));
    }

    // Start checking first arm
    llvm::BasicBlock* first_check = check_bbs.empty() ? default_bb : check_bbs[0];
    m_builder->CreateBr(first_check);

    // Generate check blocks
    for (size_t i = 0; i < expr.cases.size(); i++) {
        fn->insert(fn->end(), check_bbs[i]);
        m_builder->SetInsertPoint(check_bbs[i]);

        // For now, use equality check on the pattern
        // Simple literal match
        llvm::Value* pattern_val = codegenExpr(expr.cases[i].pattern);
        llvm::Value* eq = callRuntimeFunc("angara_equals", {subject, pattern_val});
        llvm::Value* truthy = isTruthy(eq);

        llvm::BasicBlock* next = (i + 1 < expr.cases.size()) ? check_bbs[i + 1] : default_bb;
        m_builder->CreateCondBr(truthy, arm_bbs[i], next);
    }

    // Generate arm blocks
    std::vector<llvm::Value*> arm_results;
    std::vector<llvm::BasicBlock*> arm_result_bbs;
    for (size_t i = 0; i < expr.cases.size(); i++) {
        fn->insert(fn->end(), arm_bbs[i]);
        m_builder->SetInsertPoint(arm_bbs[i]);
        llvm::Value* result = codegenExpr(expr.cases[i].body);
        arm_results.push_back(result);
        arm_result_bbs.push_back(arm_bbs[i]);
        m_builder->CreateBr(merge_bb);
    }

    // Default block
    fn->insert(fn->end(), default_bb);
    m_builder->SetInsertPoint(default_bb);
    llvm::Value* default_result = createAngaraNil();
    m_builder->CreateBr(merge_bb);

    // Merge
    fn->insert(fn->end(), merge_bb);
    m_builder->SetInsertPoint(merge_bb);
    auto* phi = m_builder->CreatePHI(m_angara_obj_type, arm_results.size() + 1, "match.result");
    for (size_t i = 0; i < arm_results.size(); i++)
        phi->addIncoming(arm_results[i], arm_result_bbs[i]);
    phi->addIncoming(default_result, default_bb);
    return phi;
}

} // namespace angara