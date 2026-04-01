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
            if (!func_type->is_variadic) {
                size_t num_expected = func_type->param_types.size();
                while (arg_strs.size() < num_expected) {
                    arg_strs.push_back("angara_create_nil()");
                }
            }
        }
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

        // Case 1: The callee is a property access (e.g., rmq.get_channel())
        if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
            std::string object_str = transpileExpr(get_expr->object);
            const std::string& name = get_expr->name.lexeme;
            auto object_type = m_type_checker.m_expression_types.at(get_expr->object.get());

            if (name == "deep_clone") return "angara_deep_clone(" + object_str + ")";

            // Built-in checks (Thread, Mutex, List, Record, Data)
            if (object_type->kind == TypeKind::THREAD && name == "join") return "angara_thread_join(" + object_str + ")";
            if (object_type->kind == TypeKind::MUTEX && (name == "lock" || name == "unlock")) return "angara_mutex_" + name + "(" + object_str + ")";
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
                    return c_struct_name + "_clone((" + c_struct_name + "*)AS_OBJ(" + object_str + "))";
                }
            }

            // Instance Methods
            if (object_type->kind == TypeKind::INSTANCE) {
                auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const ClassType* owner = findPropertyOwner(instance_type->class_type.get(), name);
                if (!owner) return "/* error_unknown_method */";
                std::string final_args = object_str + (args_str.empty() ? "" : ", " + args_str);
                if (owner->is_native) {
                    return "Angara_" + owner->name + "_" + name + "(" + std::to_string(arg_strs.size() + 1) + ", (AngaraObject[]){" + final_args + "})";
                } else {
                    return "Angara_" + owner->name + "_" + name + "(" + final_args + ")";
                }
            }

            // Module Access (Crucial Fix for g_get_channel)
            if (object_type->kind == TypeKind::MODULE) {
                auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);

                if (module_type->is_native) {
                    std::string mangled = "Angara_" + module_type->name + "_" + name;
                    return mangled + "(" + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
                }

                // Check if it is a Class constructor exported from module
                auto export_it = module_type->exports.find(name);
                if (export_it != module_type->exports.end() && export_it->second->kind == TypeKind::CLASS) {
                    return "Angara_" + name + "_new(" + args_str + ")";
                }

                // Standard Angara function closure exported from module
                // FIX: Must include module name in the closure variable name
                std::string closure_var = "g_" + module_type->name + "_" + name;
                return "angara_call(" + closure_var + ", " + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
            }
        }

        // Case 2: Simple Name (e.g., get_channel() or local_fn())
        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            const std::string& name = var_expr->name.lexeme;
            auto symbol = m_type_checker.m_variable_resolutions.at(var_expr.get());

            // Handle Native selectively imported functions
            if (symbol && symbol->from_module && symbol->from_module->is_native) {
                std::string mangled = "Angara_" + symbol->from_module->name + "_" + name;
                return mangled + "(" + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
            }

            // Built-ins
            if (name == "len") return "angara_len(" + args_str + ")";
            if (name == "typeof") return "angara_typeof(" + args_str + ")";
            if (name == "string") return "angara_to_string(" + args_str + ")";
            if (name == "i64" || name == "int") return "angara_to_i64(" + args_str + ")";
            if (name == "f64" || name == "float") return "angara_to_f64(" + args_str + ")";
            if (name == "bool") return "angara_to_bool(" + args_str + ")";
            if (name == "Mutex") return "angara_mutex_new()";
            if (name == "Exception") return "angara_exception_new(" + args_str + ")";
            if (name == "spawn") {
                std::string closure_str = transpileExpr(expr.arguments[0]);
                std::vector<std::string> rest;
                for (size_t i = 1; i < arg_strs.size(); ++i) rest.push_back(arg_strs[i]);
                return "angara_spawn_thread(" + closure_str + ", " + std::to_string(rest.size()) + ", (AngaraObject[]){" + join_strings(rest, ", ") + "})";
            }

            // Global/Local Angara Function Calls
            if (symbol->type->kind == TypeKind::FUNCTION || symbol->type->kind == TypeKind::ANY) {
                // FIX: Use transpileExpr here. It already knows how to namespacing!
                // This will return 'local_fn' for stack vars or 'g_mod_func' for globals.
                std::string callee_name = transpileExpr(expr.callee);
                return "angara_call(" + callee_name + ", " + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
            }

            // Data/Class constructors
            if (callee_type->kind == TypeKind::DATA) return "Angara_data_new_" + name + "(" + args_str + ")";
            if (callee_type->kind == TypeKind::CLASS) return "Angara_" + name + "_new(" + args_str + ")";
        }

        // Case 3: Dynamic Fallback (expr evaluating to fn)
        return "angara_call(" + transpileExpr(expr.callee) + ", " + std::to_string(arg_strs.size()) + ", (AngaraObject[]){" + args_str + "})";
    }

}