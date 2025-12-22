//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileCallExpr(const CallExpr& expr) {
        // 1. Transpile provided arguments
        std::vector<std::string> arg_strs;
        for (const auto& arg : expr.arguments) {
            arg_strs.push_back(transpileExpr(arg));
        }

        auto callee_type = m_type_checker.m_expression_types.at(expr.callee.get());

        // 2. Fill missing optional arguments with 'nil'
        if (callee_type->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);
            // Only fill if NOT variadic (variadics handle their own counts)
            if (!func_type->is_variadic) {
                size_t num_expected = func_type->param_types.size();
                while (arg_strs.size() < num_expected) {
                    arg_strs.push_back("angara_create_nil()");
                }
            }
        }
        // Special case for Class constructors (init method)
        else if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
            auto init_it = class_type->methods.find("init");
            if (init_it != class_type->methods.end()) {
                auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
                size_t num_expected = init_func_type->param_types.size();
                while (arg_strs.size() < num_expected) {
                    arg_strs.push_back("angara_create_nil()");
                }
            }
        }

        std::string args_str = CTranspiler::join_strings(arg_strs, ", ");

        // Case 1: The callee is a property access, e.g., `object.method(...)`.
        if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
            std::string object_str = transpileExpr(get_expr->object);
            const std::string& name = get_expr->name.lexeme;
            auto object_type = m_type_checker.m_expression_types.at(get_expr->object.get());

            // Intercept deep_clone universally
            if (name == "deep_clone") {
                return "angara_deep_clone(" + object_str + ")";
            }

            // Built-in primitive types methods
            if (object_type->kind == TypeKind::THREAD && name == "join") {
                return "angara_thread_join(" + object_str + ")";
            }
            if (object_type->kind == TypeKind::MUTEX && (name == "lock" || name == "unlock")) {
                return "angara_mutex_" + name + "(" + object_str + ")";
            }
            if (object_type->kind == TypeKind::LIST) {
                if (name == "push") return "angara_list_push(" + object_str + ", " + args_str + ")";
                if (name == "remove_at") return "angara_list_remove_at(" + object_str + ", " + args_str + ")";
                if (name == "remove") return "angara_list_remove(" + object_str + ", " + args_str + ")";
            }
            if (object_type->kind == TypeKind::RECORD) {
                if (name == "remove") return "angara_record_remove(" + object_str + ", " + args_str + ")";
                if (name == "keys") return "angara_record_keys(" + object_str + ")";
                if (name == "clone") return "angara_record_clone(" + object_str + ")";
            }
            if (object_type->kind == TypeKind::DATA) {
                auto data_type = std::dynamic_pointer_cast<DataType>(object_type);
                if (name == "clone" && !data_type->is_foreign) {
                    std::string c_struct_name = "Angara_" + data_type->name;
                    std::string cast_ptr = "((" + c_struct_name + "*)AS_OBJ(" + object_str + "))";
                    return c_struct_name + "_clone(" + cast_ptr + ")";
                }
            }

            // Instance Methods (Classes)
            if (object_type->kind == TypeKind::INSTANCE) {
                auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), name);

                if (!owner_class) return "/* <compiler_error_unknown_method> */";

                std::string final_args = object_str + (args_str.empty() ? "" : ", " + args_str);

                // FIX: Use arg_strs.size() + 1 (for 'this')
                if (owner_class->is_native) {
                    return "Angara_" + owner_class->name + "_" + name + "(" +
                           std::to_string(arg_strs.size() + 1) + ", (AngaraObject[]){" + final_args + "})";
                } else {
                    return "Angara_" + owner_class->name + "_" + name + "(" + final_args + ")";
                }
            }

            // Module Functions (Imported)
            // Module Functions OR Classes
            if (object_type->kind == TypeKind::MODULE) {
                auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);

                // Native Module
                if (module_type->is_native) {
                    std::string mangled_name = "Angara_" + module_type->name + "_" + name;
                    return mangled_name + "(" + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
                }

                // Angara Module
                // -> Check if it is a Class Constructor (Fix for g_Suite error)
                auto export_it = module_type->exports.find(name);
                if (export_it != module_type->exports.end()) {
                    if (export_it->second->kind == TypeKind::CLASS) {
                        // It's a class constructor call! e.g., unit.Suite("name")
                        return "Angara_" + name + "_new(" + args_str + ")";
                    }
                }

                // -> Otherwise, treat as Global Function closure
                std::string closure_var = "g_" + name;
                return "angara_call(" + closure_var + ", " + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
            }

            // Enum Constructors
            if (callee_type->kind == TypeKind::FUNCTION) {
                auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);
                if (func_type->return_type->kind == TypeKind::ENUM) {
                    std::string c_constructor_name = transpileExpr(expr.callee);
                    return c_constructor_name + "(" + args_str + ")";
                }
            }
        }

        // Case 2: Simple name / Global functions
        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            const std::string& name = var_expr->name.lexeme;

            // Selectively Imported Native Functions
            auto symbol = m_type_checker.m_variable_resolutions.at(var_expr.get());
            if (symbol && symbol->from_module && symbol->from_module->is_native) {
                std::string mangled_name = "Angara_" + symbol->from_module->name + "_" + name;
                // FIX: Use arg_strs.size()
                return mangled_name + "(" + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
            }

            if (symbol->depth > 0) {
                // Local variable (e.g. 'let fn = ...; fn()')
                // Call it directly as an AngaraObject closure
                std::string local_var = sanitize_name(name);
                return "angara_call(" + local_var + ", " + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
            }

            // Built-in globals
            if (name == "len") return "angara_len(" + args_str + ")";
            if (name == "typeof") return "angara_typeof(" + args_str + ")";
            if (name == "string") return "angara_to_string(" + args_str + ")";
            if (name == "i64" || name == "int") return "angara_to_i64(" + args_str + ")";
            if (name == "f64" || name == "float") return "angara_to_f64(" + args_str + ")";
            if (name == "bool") return "angara_to_bool(" + args_str + ")";
            if (name == "Mutex") return "angara_mutex_new()";
            if (name == "Exception") return "angara_exception_new(" + args_str + ")";
            if (name == "spawn") {
                // Spawn is special: arg[0] is closure, rest are thread args
                std::string closure_str = transpileExpr(expr.arguments[0]);
                std::vector<std::string> rest_arg_strs;
                // We use arg_strs (which contains transpiled args + filled nils)
                for (size_t i = 1; i < arg_strs.size(); ++i) {
                    rest_arg_strs.push_back(arg_strs[i]);
                }
                std::string rest_args_str = join_strings(rest_arg_strs, ", ");
                return "angara_spawn_thread(" + closure_str + ", " + std::to_string(rest_arg_strs.size()) + ", (AngaraObject[]){" + rest_args_str + "})";
            }

            // Foreign Functions
            if (symbol && symbol->type->kind == TypeKind::FUNCTION) {
                auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
                if (func_type->is_foreign) {
                    std::stringstream call_ss;
                    if (func_type->return_type->kind != TypeKind::NIL) {
                        call_ss << "angara_from_c_" << func_type->return_type->toString() << "(";
                    }
                    call_ss << name << "(";
                    for (size_t i = 0; i < arg_strs.size(); ++i) {
                        call_ss << "angara_as_c_" << func_type->param_types[i]->toString() << "(" << arg_strs[i] << ")";
                        if (i < arg_strs.size() - 1) call_ss << ", ";
                    }
                    call_ss << ")";
                    if (func_type->return_type->kind != TypeKind::NIL) call_ss << ")";
                    return call_ss.str();
                }
            }

            // Data Constructors
            if (callee_type->kind == TypeKind::DATA) {
                auto data_type = std::dynamic_pointer_cast<DataType>(callee_type);
                return "Angara_data_new_" + data_type->name + "(" + args_str + ")";
            }

            // Class Constructors (Angara defined)
            if (callee_type->kind == TypeKind::CLASS) {
                auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
                if (!class_type->is_native) {
                    return "Angara_" + name + "_new(" + args_str + ")";
                }
                return "/* <compiler_error_direct_native_class_instantiation> */";
            }

            // Standard Angara Global Function (Closure call)
            std::string closure_var = "g_" + name;
            if (name == "main") closure_var = "g_angara_main_closure";
            // FIX: Use arg_strs.size()
            return "angara_call(" + closure_var + ", " + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
        }

        // Super calls
        if (auto super_expr = std::dynamic_pointer_cast<const SuperExpr>(expr.callee)) {
            if (m_current_class_name.empty()) return "/* error */";
            auto current_class_symbol = m_type_checker.m_symbols.resolve(m_current_class_name);
            auto current_class_type = std::dynamic_pointer_cast<ClassType>(current_class_symbol->type);
            auto superclass_type = current_class_type->superclass;

            if (!super_expr->method) {
                return "Angara_" + superclass_type->name + "_init(this_obj" + (args_str.empty() ? "" : ", " + args_str) + ")";
            } else {
                return "Angara_" + superclass_type->name + "_" + super_expr->method->lexeme + "(this_obj" + (args_str.empty() ? "" : ", " + args_str) + ")";
            }
        }

        // Case 3: Fallback for dynamic calls (expressions evaluating to a function)
        std::string callee_str = transpileExpr(expr.callee);
        // FIX: Use arg_strs.size()
        return "angara_call(" + callee_str + ", " + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
    }
}