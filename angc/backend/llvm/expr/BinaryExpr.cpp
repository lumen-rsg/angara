#include "LLVMBackend.h"

namespace angara {

llvm::Value* LLVMBackend::codegenBinary(const Binary& expr) {
    llvm::Value* lhs = codegenExpr(expr.left);
    llvm::Value* rhs = codegenExpr(expr.right);
    auto lhs_type = m_type_checker.m_expression_types.at(expr.left.get());
    auto rhs_type = m_type_checker.m_expression_types.at(expr.right.get());

    switch (expr.op.type) {
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL: {
            llvm::Value* result;
            if (lhs_type->kind == TypeKind::DATA && rhs_type->kind == TypeKind::DATA) {
                std::string c_struct = "Angara_" + lhs_type->toString() + "_equals";
                result = callRuntimeFunc(c_struct, {lhs, rhs});
                result = callRuntimeFunc("angara_create_bool", {result});
            } else {
                result = callRuntimeFunc("angara_equals", {lhs, rhs});
            }
            if (expr.op.type == TokenType::BANG_EQUAL) {
                llvm::Value* b = extractBool(result);
                b = m_builder->CreateNot(b, "not_eq");
                result = callRuntimeFunc("angara_create_bool", {b});
            }
            return result;
        }
        case TokenType::GREATER: case TokenType::GREATER_EQUAL:
        case TokenType::LESS: case TokenType::LESS_EQUAL: {
            llvm::Value* lf = extractF64(lhs);
            llvm::Value* rf = extractF64(rhs);
            llvm::Value* cmp = nullptr;
            switch (expr.op.type) {
                case TokenType::GREATER: cmp = m_builder->CreateFCmpOGT(lf, rf); break;
                case TokenType::GREATER_EQUAL: cmp = m_builder->CreateFCmpOGE(lf, rf); break;
                case TokenType::LESS: cmp = m_builder->CreateFCmpOLT(lf, rf); break;
                case TokenType::LESS_EQUAL: cmp = m_builder->CreateFCmpOLE(lf, rf); break;
                default: break;
            }
            return callRuntimeFunc("angara_create_bool", {cmp});
        }
        case TokenType::PLUS:
            if (lhs_type->toString() == "string" && rhs_type->toString() == "string")
                return callRuntimeFunc("angara_string_concat", {lhs, rhs});
            if (isFloat(lhs_type) || isFloat(rhs_type)) {
                llvm::Value* r = m_builder->CreateFAdd(extractF64(lhs), extractF64(rhs), "fadd");
                return callRuntimeFunc("angara_create_f64", {r});
            }
            return callRuntimeFunc("angara_create_i64", {
                m_builder->CreateAdd(extractI64(lhs), extractI64(rhs), "iadd")});
        case TokenType::MINUS:
            if (isFloat(lhs_type) || isFloat(rhs_type)) {
                llvm::Value* r = m_builder->CreateFSub(extractF64(lhs), extractF64(rhs), "fsub");
                return callRuntimeFunc("angara_create_f64", {r});
            }
            return callRuntimeFunc("angara_create_i64", {
                m_builder->CreateSub(extractI64(lhs), extractI64(rhs), "isub")});
        case TokenType::STAR:
            if (isFloat(lhs_type) || isFloat(rhs_type)) {
                llvm::Value* r = m_builder->CreateFMul(extractF64(lhs), extractF64(rhs), "fmul");
                return callRuntimeFunc("angara_create_f64", {r});
            }
            return callRuntimeFunc("angara_create_i64", {
                m_builder->CreateMul(extractI64(lhs), extractI64(rhs), "imul")});
        case TokenType::SLASH:
            if (isFloat(lhs_type) || isFloat(rhs_type)) {
                llvm::Value* r = m_builder->CreateFDiv(extractF64(lhs), extractF64(rhs), "fdiv");
                return callRuntimeFunc("angara_create_f64", {r});
            }
            return callRuntimeFunc("angara_create_i64", {
                m_builder->CreateSDiv(extractI64(lhs), extractI64(rhs), "idiv")});
        case TokenType::PERCENT:
            if (isFloat(lhs_type) || isFloat(rhs_type)) {
                llvm::Value* r = m_builder->CreateFRem(extractF64(lhs), extractF64(rhs), "fmod");
                return callRuntimeFunc("angara_create_f64", {r});
            }
            return callRuntimeFunc("angara_create_i64", {
                m_builder->CreateSRem(extractI64(lhs), extractI64(rhs), "imod")});
        default:
            return createAngaraNil();
    }
}

} // namespace angara