//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::pass_2_generate_declarations(const std::vector<std::shared_ptr<Stmt>>& statements, const std::string& module_name) {
        (*m_current_out) << "\n// --- Global Variable Forward Declarations ---\n";
        for (const auto& stmt : statements) {
            if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
                if (var_decl->is_exported) {
                    (*m_current_out) << "extern AngaraObject " << module_name << "_" << var_decl->name.lexeme << ";\n";
                }
            }
        }

        (*m_current_out) << "\n// --- Function & Closure Forward Declarations ---\n";
        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                if (func_stmt->is_exported || func_stmt->name.lexeme == "main") {
                    std::string var_name = "g_" + func_stmt->name.lexeme;
                    if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
                    (*m_current_out) << "extern AngaraObject " << var_name << ";\n";

                    transpileFunctionSignature(*func_stmt, module_name);
                    (*m_current_out) << ";\n";

                    std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                    if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";
                    (*m_current_out) << "AngaraObject angara_w_" << mangled_name << "(int arg_count, AngaraObject args[]);\n";
                }
            } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                if (class_stmt->is_exported) {
                    auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
                    (*m_current_out) << "\n// --- API for class " << class_stmt->name.lexeme << " ---\n";
                    (*m_current_out) << "extern AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";

                    (*m_current_out) << "AngaraObject Angara_" << class_stmt->name.lexeme << "_new(";
                    auto init_it = class_type->methods.find("init");
                    if (init_it != class_type->methods.end()) {
                        auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
                        for (size_t i = 0; i < init_func_type->param_types.size(); ++i) {
                            (*m_current_out) << getCType(init_func_type->param_types[i]);
                            if (i < init_func_type->param_types.size() - 1) (*m_current_out) << ", ";
                        }
                    }
                    (*m_current_out) << ");\n";

                    for (const auto& member : class_stmt->members) {
                        if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                            if (method_member->access == AccessLevel::PUBLIC) {
                                transpileMethodSignature(class_stmt->name.lexeme, *method_member->declaration);
                                (*m_current_out) << ";\n";
                            }
                        }
                    }
                }
            }
        }

        (*m_current_out) << "\n// --- Imported Symbol Declarations ---\n";
        for (const auto& stmt : statements) {
            if (auto attach_stmt = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
                auto imported_module_type = m_type_checker.m_module_resolutions.at(attach_stmt.get());
                if (!imported_module_type->is_native) continue;

                (*m_current_out) << "// --- Prototypes for Native Module: " << imported_module_type->name << " ---\n";
                for (const auto& [export_name, type] : imported_module_type->exports) {

                    // We only generate prototypes for functions, not for ClassTypes that are also exported.
                    if (type->kind != TypeKind::FUNCTION) continue;

                    auto func_type = std::dynamic_pointer_cast<FunctionType>(type);
                    std::string mangled_name = "Angara_" + imported_module_type->name + "_" + export_name;

                    // All native global functions AND constructors use the generic signature.
                    (*m_current_out) << "extern AngaraObject " << mangled_name
                                     << "(int arg_count, AngaraObject* args);\n";

                    // If this function is a constructor, its return type will be an InstanceType.
                    // We use this to find the class and generate prototypes for its methods.
                    if (func_type->return_type->kind == TypeKind::INSTANCE) {
                        auto instance_type = std::dynamic_pointer_cast<InstanceType>(func_type->return_type);
                        auto class_type = instance_type->class_type;

                        for (const auto& [method_name, method_info] : class_type->methods) {
                            // Method mangled name is Angara_ClassName_MethodName
                            std::string method_mangled_name = "Angara_" + class_type->name + "_" + method_name;
                            // All native methods also use the generic signature.
                            (*m_current_out) << "extern AngaraObject " << method_mangled_name
                                             << "(int arg_count, AngaraObject* args);\n";
                        }
                    }
                }
            }
        }

        // --- Add the current module's initializer prototype (Unchanged) ---
        (*m_current_out) << "\n// --- Module Initializer ---\n";
        (*m_current_out) << "void Angara_" << module_name << "_init_globals(void);\n";

    }

}