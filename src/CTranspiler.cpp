//
// Created by cv2 on 9/1/25.
//

#include "CTranspiler.h"
#include <stdexcept>

namespace angara {

    CTranspiler::CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler)
            : m_type_checker(type_checker), m_errorHandler(errorHandler) {}

    void CTranspiler::indent() {
        for (int i = 0; i < m_indent_level; ++i) {
            m_out << "  "; // 2 spaces per indent level
        }
    }

    std::string CTranspiler::generate(const std::vector<std::shared_ptr<Stmt>>& statements) {
        // --- C PREAMBLE ---
        // Include necessary C headers.
        m_out << "#include <stdio.h>\n";
        m_out << "#include <stdint.h>\n"; // For int64_t, etc.
        m_out << "#include <stdbool.h>\n"; // For bool, true, false
        m_out << "\n";

        // --- MAIN FUNCTION ---
        m_out << "int main(void) {\n";
        m_indent_level++;

        for (const auto& stmt : statements) {
            stmt->accept(*this, stmt);
            if (m_hadError) return "";
        }

        indent();
        m_out << "return 0;\n";
        m_indent_level--;
        m_out << "}\n";

        return m_out.str();
    }

// --- Helper to get C type names ---

    std::string CTranspiler::getCType(const std::shared_ptr<Type>& angaraType) {
        if (!angaraType) {
            return "void /* unknown type */";
        }

        switch (angaraType->kind) {
            case TypeKind::PRIMITIVE: {
                const auto& name = angaraType->toString();

                // --- Signed Integers ---
                if (name == "i8")   return "int8_t";
                if (name == "i16")  return "int16_t";
                if (name == "i32")  return "int32_t";
                if (name == "i64" || name == "int") return "int64_t";

                // --- Unsigned Integers ---
                if (name == "u8")   return "uint8_t";
                if (name == "u16")  return "uint16_t";
                if (name == "u32")  return "uint32_t";
                if (name == "u64" || name == "uint") return "uint64_t";

                // --- Floats ---
                if (name == "f32")   return "float";
                if (name == "f64" || name == "float") return "double";

                // --- Other Primitives ---
                if (name == "bool")   return "bool";
                if (name == "string") return "const char*";
                if (name == "void")   return "void";

                // The 'any' type will require a special struct in our C runtime.
                // For now, we can represent it as a void pointer.
                if (name == "any")    return "void*";

                break;
            }

            case TypeKind::NIL:
                // There is no 'nil' type in C. It's typically represented by a
                // void pointer or a special value. For function returns, it's void.
                return "void";

            case TypeKind::LIST:
                // A list will be a pointer to a custom C struct.
                // e.g., typedef struct { ... } AngaraList;
                return "AngaraList*"; // Placeholder name

            case TypeKind::RECORD:
                // A record will also be a pointer to a custom C struct.
                return "AngaraRecord*"; // Placeholder name

            case TypeKind::FUNCTION:
                // Function pointers in C have a complex syntax.
                // e.g., int (*func_ptr)(int, int)
                // We will need a helper function to build this string.
                // For now, a placeholder is fine.
                return "void* /* function pointer */";

                // TODO: Add cases for CLASS, INSTANCE, etc. They will also be struct pointers.

            default:
                break;
        }

        // Fallback for any unhandled or error types.
        return "void /* unknown type */";
    }


// --- VISITORS ---

    std::any CTranspiler::visit(const Literal& expr) {
        // For literals, we just return their C source representation as a string.
        if (expr.token.type == TokenType::STRING) {
            // Ensure quotes are added for C strings
            return std::string("\"" + expr.token.lexeme + "\"");
        }
        return expr.token.lexeme;
    }

    void CTranspiler::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        indent();

        // Get the variable's type by looking up the symbol.
        auto symbol = m_type_checker.m_symbols.resolve(stmt->name.lexeme);
        auto c_type = getCType(symbol->type);

        if (stmt->is_const) {
            m_out << "const ";
        }
        m_out << c_type << " " << stmt->name.lexeme;

        if (stmt->initializer) {
            m_out << " = ";
            auto init_str = std::any_cast<std::string>(stmt->initializer->accept(*this));
            m_out << init_str;
        }

        m_out << ";\n";
    }

    void CTranspiler::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        indent();
        auto expr_str = std::any_cast<std::string>(stmt->expression->accept(*this));
        m_out << expr_str << ";\n";
    }

    std::any CTranspiler::visit(const Binary &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const VarExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const Unary &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const Grouping &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const ListExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const AssignExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const UpdateExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const CallExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const GetExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const LogicalExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const SubscriptExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const RecordExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const TernaryExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const ThisExpr &expr) {
        return std::any();
    }

    std::any CTranspiler::visit(const SuperExpr &expr) {
        return std::any();
    }

    void CTranspiler::visit(std::shared_ptr<const IfStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const EmptyStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const WhileStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const ForStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const ForInStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const FuncStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const ReturnStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const AttachStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const ThrowStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const TryStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const ClassStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const TraitStmt> stmt) {

    }

    void CTranspiler::visit(std::shared_ptr<const BlockStmt> stmt) {

    }

}
