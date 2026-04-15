#include "LLVMBackend.h"

namespace angara {

llvm::Value* LLVMBackend::codegenAssignExpr(const AssignExpr& expr) {
    llvm::Value* val = codegenExpr(expr.value);
    const std::string& op = expr.op.lexeme;

    if (op != "=") {
        // Compound assignment: +=, -=, etc.
        llvm::Value* old = codegenExpr(expr.target);
        auto old_type = m_type_checker.m_expression_types.at(expr.target.get());

        if (op == "+=") {
            if (old_type->toString() == "string")
                val = callRuntimeFunc("angara_string_concat", {old, val});
            else if (isFloat(old_type))
                val = callRuntimeFunc("angara_create_f64",
                    {m_builder->CreateFAdd(extractF64(old), extractF64(val))});
            else
                val = callRuntimeFunc("angara_create_i64",
                    {m_builder->CreateAdd(extractI64(old), extractI64(val))});
        } else if (op == "-=") {
            if (isFloat(old_type))
                val = callRuntimeFunc("angara_create_f64",
                    {m_builder->CreateFSub(extractF64(old), extractF64(val))});
            else
                val = callRuntimeFunc("angara_create_i64",
                    {m_builder->CreateSub(extractI64(old), extractI64(val))});
        } else if (op == "*=") {
            if (isFloat(old_type))
                val = callRuntimeFunc("angara_create_f64",
                    {m_builder->CreateFMul(extractF64(old), extractF64(val))});
            else
                val = callRuntimeFunc("angara_create_i64",
                    {m_builder->CreateMul(extractI64(old), extractI64(val))});
        } else if (op == "/=") {
            if (isFloat(old_type))
                val = callRuntimeFunc("angara_create_f64",
                    {m_builder->CreateFDiv(extractF64(old), extractF64(val))});
            else
                val = callRuntimeFunc("angara_create_i64",
                    {m_builder->CreateSDiv(extractI64(old), extractI64(val))});
        }
    }

    // Store into target
    if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
        storeVariable(sanitizeName(var->name.lexeme), val);
    } else if (auto sub = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
        llvm::Value* obj = codegenExpr(sub->object);
        llvm::Value* idx = codegenExpr(sub->index);
        callRuntimeFunc("angara_list_set", {obj, idx, val});
    } else if (auto get = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
        llvm::Value* obj = codegenExpr(get->object);
        auto obj_type = m_type_checker.m_expression_types.at(get->object.get());
        if (obj_type->kind == TypeKind::RECORD)
            callRuntimeFunc("angara_record_set", {obj,
                m_builder->CreateGlobalStringPtr(get->name.lexeme), val});
    }
    return val;
}

llvm::Value* LLVMBackend::codegenUpdateExpr(const UpdateExpr& expr) {
    llvm::Value* old = codegenExpr(expr.target);
    auto old_type = m_type_checker.m_expression_types.at(expr.target.get());

    llvm::Value* one = isFloat(old_type)
        ? callRuntimeFunc("angara_create_f64",
            {llvm::ConstantFP::get(llvm::Type::getDoubleTy(*m_context), 1.0)})
        : createAngaraI64(1);

    llvm::Value* inc;
    if (isFloat(old_type))
        inc = callRuntimeFunc("angara_create_f64",
            {m_builder->CreateFAdd(extractF64(old), extractF64(one))});
    else
        inc = callRuntimeFunc("angara_create_i64",
            {m_builder->CreateAdd(extractI64(old), extractI64(one))});

    if (expr.op.type == TokenType::MINUS_MINUS) {
        if (isFloat(old_type))
            inc = callRuntimeFunc("angara_create_f64",
                {m_builder->CreateFSub(extractF64(old), extractF64(one))});
        else
            inc = callRuntimeFunc("angara_create_i64",
                {m_builder->CreateSub(extractI64(old), extractI64(one))});
    }

    // Store updated value
    if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr.target))
        storeVariable(sanitizeName(var->name.lexeme), inc);
    else if (auto sub = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
        llvm::Value* obj = codegenExpr(sub->object);
        llvm::Value* idx = codegenExpr(sub->index);
        callRuntimeFunc("angara_list_set", {obj, idx, inc});
    }

    return expr.isPrefix ? inc : old;
}

} // namespace angara