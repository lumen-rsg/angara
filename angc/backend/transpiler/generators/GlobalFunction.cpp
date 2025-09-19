//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileGlobalFunction(const FuncStmt& stmt, const std::string& module_name) {
        // This helper is called during Pass 3 to generate the full implementation
        // for a single top-level Angara function.

        // 1. Get the canonical FunctionType from the Type Checker's results.
        auto symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);

        // 2. Determine the C linkage. Private functions become `static`, making them
        //    invisible to the C linker outside of this file.
        std::string linkage = (stmt.is_exported || stmt.name.lexeme == "main") ? "" : "static ";

        // 3. Determine the mangled name for the C function implementation.
        std::string mangled_impl_name = "angara_f_" + module_name + "_" + stmt.name.lexeme;
        if (stmt.name.lexeme == "main") {
            mangled_impl_name = "angara_f_main";
        }

        // --- Generate the actual, strongly-typed C function ---
        (*m_current_out) << linkage; // Prepend 'static ' if necessary.
        transpileFunctionSignature(stmt, module_name);
        (*m_current_out) << " {\n";
        m_indent_level = 1;

        // Transpile the function's body.
        if (stmt.body) {
            for (const auto& body_stmt : *stmt.body) {
                transpileStmt(body_stmt);
            }
        }

        // Handle implicit returns for functions that should return void.
        if (func_type->return_type->toString() == "nil") {
            if (stmt.body->empty() || !isa<ReturnStmt>(stmt.body->back())) {
                 indent();
                 (*m_current_out) << "return angara_create_nil();\n";
            }
        }
        m_indent_level = 0;
        (*m_current_out) << "}\n\n";


        // --- Generate the generic wrapper function ---
        // Naming convention for the generic wrapper.
        std::string mangled_wrapper_name = "angara_w_" + mangled_impl_name;

        // The wrapper must also have `static` linkage if the function is private.
        (*m_current_out) << linkage << "AngaraObject " << mangled_wrapper_name << "(int arg_count, AngaraObject args[]) {\n";
        m_indent_level = 1;
        indent();

        // Generate the call inside the wrapper, handling void vs. non-void returns.
        if (func_type->return_type->toString() == "nil") {
            (*m_current_out) << mangled_impl_name << "(";
            for (int i = 0; i < stmt.params.size(); ++i) {
                (*m_current_out) << "args[" << i << "]" << (i == stmt.params.size() - 1 ? "" : ", ");
            }
            (*m_current_out) << ");\n";
            indent();
            (*m_current_out) << "return angara_create_nil();\n";
        } else {
            (*m_current_out) << "return " << mangled_impl_name << "(";
            for (int i = 0; i < stmt.params.size(); ++i) {
                (*m_current_out) << "args[" << i << "]" << (i == stmt.params.size() - 1 ? "" : ", ");
            }
            (*m_current_out) << ");\n";
        }
        m_indent_level = 0;
        (*m_current_out) << "}\n\n";
    }

}