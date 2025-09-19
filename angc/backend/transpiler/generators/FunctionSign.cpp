//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileFunctionSignature(const FuncStmt& stmt, const std::string& module_name) {
        // 1. Get the canonical FunctionType from the Type Checker's results.
        auto symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);

        // 2. Determine the correct mangled name for the C function implementation.
        std::string mangled_name = "angara_f_" + module_name + "_" + stmt.name.lexeme;
        if (stmt.name.lexeme == "main") mangled_name = "angara_f_main";

        // 3. Write the return type and the mangled function name.
        *m_current_out << getCType(func_type->return_type) << " " << mangled_name << "(";

        // 4. Write the parameter list.
        if (stmt.has_this) {
            *m_current_out << "AngaraObject this_obj";
            if (!stmt.params.empty()) {
                *m_current_out << ", ";
            }
        }
        for (size_t i = 0; i < stmt.params.size(); ++i) {
            // Get the C type from the resolved FunctionType, but the name from the AST.
            *m_current_out << getCType(func_type->param_types[i]) << " " << stmt.params[i].name.lexeme;
            if (i < stmt.params.size() - 1) {
                *m_current_out << ", ";
            }
        }

        // Add 'void' for empty C parameter lists for standards compliance.
        if (!stmt.has_this && stmt.params.empty()) {
            *m_current_out << "void";
        }

        // 5. Close the signature.
        *m_current_out << ")";
    }

}