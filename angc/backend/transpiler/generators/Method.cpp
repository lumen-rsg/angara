//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileMethodSignature(const std::string& class_name, const FuncStmt& stmt) {
        // Look up the canonical FunctionType from the Type Checker's results.
        auto class_symbol = m_type_checker.m_symbols.resolve(class_name);
        auto class_type = std::dynamic_pointer_cast<ClassType>(class_symbol->type);
        auto method_info = class_type->methods.at(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(method_info.type);

        // Generate the mangled name, e.g., "Angara_Point_move"
        (*m_current_out) << getCType(func_type->return_type) << " "
                         << "Angara_" << class_name << "_" << sanitize_name(stmt.name.lexeme) << "(";

        // The first parameter is always 'this'.
        if (stmt.has_this) {
            (*m_current_out) << "AngaraObject this_obj";
            if (!stmt.params.empty()) {
                (*m_current_out) << ", ";
            }
        }

        // Add the regular parameters.
        for (size_t i = 0; i < stmt.params.size(); ++i) {
            (*m_current_out) << getCType(func_type->param_types[i]) << " " << stmt.params[i].name.lexeme;
            if (i < stmt.params.size() - 1) {
                (*m_current_out) << ", ";
            }
        }
        (*m_current_out) << ")";
    }

    void CTranspiler::transpileMethodBody(const ClassType& klass, const FuncStmt& stmt) {
        // 1. Generate the full function signature again (this time for the definition).
        transpileMethodSignature(klass.name, stmt);
        (*m_current_out) << " {\n";
        m_indent_level++;

        // 2. If it's a method with 'this', create the typed 'this' pointer.
        if (stmt.has_this) {
            indent();
            (*m_current_out) << "struct Angara_" << klass.name << "* this = (struct Angara_" << klass.name << "*)AS_INSTANCE(this_obj);\n";
        }

        // 3. Transpile all statements in the method's body.
        if (stmt.body) {
            for (const auto& body_stmt : *stmt.body) {
                transpileStmt(body_stmt);
            }
        }

        // 4. Handle implicit returns for void methods.
        auto method_info = klass.methods.at(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(method_info.type);
        if (func_type->return_type->toString() == "nil") {
            indent();
            (*m_current_out) << "return angara_create_nil();\n";
        }

        m_indent_level--;
        (*m_current_out) << "}\n\n";
    }

}