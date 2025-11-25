//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"

namespace angara {

    void CTranspiler::pass_3_generate_globals_and_implementations(
            const std::vector<std::shared_ptr<Stmt>>& statements,
            const std::string& module_name
    ) {
        // ==========================================================
        // Stage A: Global Variable & Function Closure Storage
        // ==========================================================
        (*m_current_out) << "// --- Global Variable & Function Closure Storage ---\n";

        // 1. Data Type Metadata
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                if (!data_stmt->is_foreign) {
                    (*m_current_out) << "AngaraDataInfo g_Angara_" << data_stmt->name.lexeme << "_info;\n";
                }
            }
        }

        // 2. Enum Type Metadata
        for (const auto& stmt : statements) {
            if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                (*m_current_out) << "AngaraEnumInfo g_Angara_" << enum_stmt->name.lexeme << "_info;\n";
            }
        }

        // 3. Classes, Methods, Globals, Functions
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                // Class Metadata
                (*m_current_out) << "AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";

                // Method Closures (for Bound Methods)
                for (const auto& member : class_stmt->members) {
                    if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
                        (*m_current_out) << "AngaraObject g_m_" << class_stmt->name.lexeme << "_" << method->declaration->name.lexeme << ";\n";
                    }
                }
            } else if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
                (*m_current_out) << "AngaraObject " << module_name << "_" << var_decl->name.lexeme << ";\n";
            } else if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                std::string var_name = "g_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
                (*m_current_out) << "AngaraObject " << var_name << ";\n";
            }
        }
        (*m_current_out) << "\n";


        // ==========================================================
        // Stage B: Internal Forward Declarations
        // ==========================================================
        (*m_current_out) << "\n// --- Internal Forward Declarations ---\n";

        // Pass B.1: Declare all global functions AND their wrappers
        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";

                std::string linkage = (func_stmt->is_exported || func_stmt->name.lexeme == "main") ? "" : "static ";

                (*m_current_out) << linkage;
                transpileFunctionSignature(*func_stmt, module_name);
                (*m_current_out) << ";\n";
                (*m_current_out) << linkage << "AngaraObject angara_w_" << mangled_name << "(int arg_count, AngaraObject args[]);\n";
            }
        }

        // Pass B.2: Declare Class Constructors, Methods, AND Wrappers
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                // 1. Forward declare the _new constructor
                //    (Needed because main() or other classes might call it before it's defined)
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

                // 2. Forward declare methods and their wrappers
                for (const auto& member : class_stmt->members) {
                    if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
                        // A. The actual C function (e.g., Angara_Heart_init)
                        //    (Needed because _new calls _init)
                        transpileMethodSignature(class_stmt->name.lexeme, *method->declaration);
                        (*m_current_out) << ";\n";

                        // B. The Generic Wrapper (e.g., angara_w_Angara_Heart_init)
                        //    (Needed for the global closure initialization in Stage C)
                        std::string mangled = "Angara_" + class_stmt->name.lexeme + "_" + method->declaration->name.lexeme;
                        (*m_current_out) << "AngaraObject angara_w_" << mangled << "(int arg_count, AngaraObject args[]);\n";
                    }
                }
            }
        }
        (*m_current_out) << "\n";


        // ==========================================================
        // Stage C: Global Initializer Function
        // ==========================================================
        std::string init_func_name = "Angara_" + module_name + "_init_globals";
        (*m_current_out) << "void " << init_func_name << "(void) {\n";
        m_indent_level = 1;

        // 1. Init Data Metadata
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                if (!data_stmt->is_foreign) {
                    std::string struct_name = "Angara_" + data_stmt->name.lexeme;
                    indent();
                    (*m_current_out) << "g_" << struct_name << "_info.name = \"" << data_stmt->name.lexeme << "\";\n";
                    indent();
                    (*m_current_out) << "g_" << struct_name << "_info.equals_fn = (bool(*)(const void*, const void*))" << struct_name << "_equals;\n";
                    indent();
                    (*m_current_out) << "g_" << struct_name << "_info.deep_clone_fn = (AngaraObject(*)(const void*))" << struct_name << "_deep_clone;\n";
                }
            }
        }

        // 2. Init Enum Metadata
        for (const auto& stmt : statements) {
            if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                std::string struct_name = "Angara_" + enum_stmt->name.lexeme;
                indent();
                (*m_current_out) << "g_" << struct_name << "_info.name = \"" << enum_stmt->name.lexeme << "\";\n";
                indent();
                (*m_current_out) << "g_" << struct_name << "_info.equals_fn = (bool(*)(const void*, const void*))" << struct_name << "_equals;\n";
                indent();
                (*m_current_out) << "g_" << struct_name << "_info.deep_clone_fn = (AngaraObject(*)(const void*))" << struct_name << "_deep_clone;\n";
            }
        }

        // 3. Init Globals, Closures, Classes
        for (const auto& stmt : statements) {
            if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
                indent();
                (*m_current_out) << module_name << "_" << var_decl->name.lexeme << " = ";
                if (var_decl->initializer) {
                    (*m_current_out) << transpileExpr(var_decl->initializer) << ";";
                } else {
                    (*m_current_out) << "angara_create_nil();";
                }
                (*m_current_out) << "\n";
            }
            else if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                if (func_stmt->is_foreign) continue;
                std::string var_name = "g_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
                std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";

                indent();
                (*m_current_out) << var_name << " = angara_closure_new(&angara_w_" << mangled_name << ", " << func_stmt->params.size() << ", false);\n";
            }
            else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                indent();
                (*m_current_out) << "g_" << class_stmt->name.lexeme << "_class = (AngaraClass){{OBJ_CLASS, 1}, \"" << class_stmt->name.lexeme << "\"};\n";

                // Initialize Method Closures for Bound Methods
                for (const auto& member : class_stmt->members) {
                    if (auto method = std::dynamic_pointer_cast<const MethodMember>(member)) {
                        std::string method_name = method->declaration->name.lexeme;
                        std::string mangled = "Angara_" + class_stmt->name.lexeme + "_" + method_name;
                        // Arity = params + 1 (for implicit 'this')
                        int arity = method->declaration->params.size() + 1;

                        indent();
                        (*m_current_out) << "g_m_" << class_stmt->name.lexeme << "_" << method_name
                                         << " = angara_closure_new(&angara_w_" << mangled << ", " << arity << ", false);\n";
                    }
                }
            }
        }
        m_indent_level = 0;
        (*m_current_out) << "}\n\n";


        // ==========================================================
        // Stage D: Function and Method Implementations
        // ==========================================================
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
                        // 1. Transpile the direct C method implementation
                        transpileMethodBody(*class_type, *method_member->declaration);

                        // 2. Generate the Wrapper for this method (Required for Bound Methods)
                        std::string method_name = method_member->declaration->name.lexeme;
                        std::string mangled = "Angara_" + class_stmt->name.lexeme + "_" + method_name;
                        std::string wrapper_name = "angara_w_" + mangled;

                        (*m_current_out) << "AngaraObject " << wrapper_name << "(int arg_count, AngaraObject args[]) {\n";
                        m_indent_level++;
                        indent();

                        // Check return type to conditionally emit 'return'
                        auto method_info = class_type->methods.at(method_name);
                        auto func_type = std::dynamic_pointer_cast<FunctionType>(method_info.type);
                        bool is_void = (func_type->return_type->kind == TypeKind::NIL);

                        if (!is_void) (*m_current_out) << "return ";

                        (*m_current_out) << mangled << "(args[0]"; // args[0] is 'this'
                        for (size_t i = 0; i < method_member->declaration->params.size(); ++i) {
                            (*m_current_out) << ", args[" << (i + 1) << "]";
                        }
                        (*m_current_out) << ");\n";

                        if (is_void) {
                            indent(); (*m_current_out) << "return angara_create_nil();\n";
                        }

                        m_indent_level--;
                        (*m_current_out) << "}\n\n";
                    }
                }
                m_current_class_name = "";
            }
        }
    }

} // namespace angara