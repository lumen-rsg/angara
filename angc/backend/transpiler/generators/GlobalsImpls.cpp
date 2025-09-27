//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    void CTranspiler::pass_3_generate_globals_and_implementations(
            const std::vector<std::shared_ptr<Stmt>>& statements,
            const std::string& module_name
    ) {
        // === Stage A: Global Variable & Function Closure Storage ===
        (*m_current_out) << "// --- Global Variable & Function Closure Storage ---\n";
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                (*m_current_out) << "AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";
            } else if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
                (*m_current_out) << "AngaraObject " << module_name << "_" << var_decl->name.lexeme << ";\n";
            } else if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                std::string var_name = "g_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
                (*m_current_out) << "AngaraObject " << var_name << ";\n";
            }
        }
        (*m_current_out) << "\n";

        // === Stage B: COMPLETE Forward Declarations for ALL Functions/Methods ===
        (*m_current_out) << "\n// --- Internal Forward Declarations ---\n";

        // Pass B.1: Declare all global functions AND their wrappers
        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";

                // Always use 'static' for prototypes of non-exported functions inside the .c file
                std::string linkage = (func_stmt->is_exported || func_stmt->name.lexeme == "main") ? "" : "static ";

                (*m_current_out) << linkage;
                transpileFunctionSignature(*func_stmt, module_name);
                (*m_current_out) << ";\n";
                (*m_current_out) << linkage << "AngaraObject angara_w_" << mangled_name << "(int arg_count, AngaraObject args[]);\n";
            }
        }

        // Pass B.2: Declare all class methods (they are never static in the C file)
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                // Forward declare the _new constructor
                auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
                (*m_current_out) << "AngaraObject Angara_" << class_stmt->name.lexeme << "_new(";
                auto init_it = class_type->methods.find("init");
                if (init_it != class_type->methods.end()) {
                    auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
                    for (size_t i = 0; i < init_func_type->param_types.size(); ++i) {
                        (*m_current_out) << getCType(init_func_type->param_types[i]) << (i == init_func_type->param_types.size() - 1 ? "" : ", ");
                    }
                }
                (*m_current_out) << ");\n";

                // Forward declare all methods
                for (const auto& member : class_stmt->members) {
                    if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                        transpileMethodSignature(class_stmt->name.lexeme, *method_member->declaration);
                        (*m_current_out) << ";\n";
                    }
                }
            }
        }
        (*m_current_out) << "\n";

        // === Stage C: Global Initializer Function ===
        std::string init_func_name = "Angara_" + module_name + "_init_globals";
        (*m_current_out) << "void " << init_func_name << "(void) {\n";
        m_indent_level = 1;
        for (const auto& stmt : statements) {
            if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
                indent();
                (*m_current_out) << module_name << "_" << var_decl->name.lexeme << " = ";
                if (var_decl->initializer) {
                    // If an initializer exists, transpile it.
                    (*m_current_out) << transpileExpr(var_decl->initializer) << ";";
                } else {
                    // If no initializer, the default value is `nil`.
                    (*m_current_out) << "angara_create_nil();";
                }
            } else if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                if (func_stmt->is_foreign) {
                    continue;
                }
                std::string var_name = "g_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
                std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";
                indent();
                (*m_current_out) << var_name << " = angara_closure_new(&angara_w_" << mangled_name << ", " << func_stmt->params.size() << ", false);\n";
            } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                indent();
                (*m_current_out) << "g_" << class_stmt->name.lexeme << "_class = (AngaraClass){{OBJ_CLASS, 1}, \"" << class_stmt->name.lexeme << "\"};\n";
            }
        }
        m_indent_level = 0;
        (*m_current_out) << "}\n\n";

        // === Stage D: Function and Method Implementations ===
        (*m_current_out) << "// --- Function Implementations ---\n";
        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                if (!func_stmt->is_foreign) {
                    transpileGlobalFunction(*func_stmt, module_name);
                }
            } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                m_current_class_name = class_stmt->name.lexeme;
                auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
                transpileClassNew(*class_stmt);
                for (const auto& member : class_stmt->members) {
                    if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                        transpileMethodBody(*class_type, *method_member->declaration);
                    }
                }
                m_current_class_name = "";
            }
        }
    }

}