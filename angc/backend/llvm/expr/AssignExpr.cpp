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

    // Store into target — ARC: decref old, incref new
    if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
        const std::string vname = sanitizeName(var->name.lexeme);
        // Decref old value
        llvm::Value* old_val = loadVariable(vname);
        callRuntimeFunc("angara_decref", {old_val});
        // Store new value and incref it
        storeVariable(vname, val);
        callRuntimeFunc("angara_incref", {val});
    } else if (auto sub = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
        llvm::Value* obj = codegenExpr(sub->object);
        auto obj_type = m_type_checker.m_expression_types.at(sub->object.get());
        if (obj_type->kind == TypeKind::RECORD) {
            // Record subscript assignment: obj["key"] = val
            if (auto lit = std::dynamic_pointer_cast<const Literal>(sub->index)) {
                callRuntimeFunc("angara_record_set", {obj,
                    m_builder->CreateGlobalStringPtr(lit->token.lexeme), val});
            } else {
                // Dynamic key — extract string at runtime
                llvm::Value* key = codegenExpr(sub->index);
                auto* tag = extractTypeTag(key);
                // For now, use the obj pointer as key for dynamic case
                callRuntimeFunc("angara_record_set", {obj,
                    m_builder->CreateGlobalStringPtr("unknown"), val});
            }
        } else {
            llvm::Value* idx = codegenExpr(sub->index);
            callRuntimeFunc("angara_list_set", {obj, idx, val});
        }
    } else if (auto get = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
        llvm::Value* obj = codegenExpr(get->object);
        auto obj_type = m_type_checker.m_expression_types.at(get->object.get());
        if (obj_type->kind == TypeKind::RECORD || obj_type->kind == TypeKind::INSTANCE)
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

    // Store updated value — ARC: decref old, incref new
    if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
        const std::string vname = sanitizeName(var->name.lexeme);
        callRuntimeFunc("angara_decref", {old});
        storeVariable(vname, inc);
        callRuntimeFunc("angara_incref", {inc});
    } else if (auto sub = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
        llvm::Value* obj = codegenExpr(sub->object);
        llvm::Value* idx = codegenExpr(sub->index);
        callRuntimeFunc("angara_list_set", {obj, idx, inc});
    }

    return expr.isPrefix ? inc : old;
}

} // namespace angara