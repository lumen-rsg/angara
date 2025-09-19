//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileCallExpr(const CallExpr& expr) {
        // 1. Transpile all arguments first, as they are needed in almost every case.
        std::vector<std::string> arg_strs;
        for (const auto& arg : expr.arguments) {
            arg_strs.push_back(transpileExpr(arg));
        }
        std::string args_str = CTranspiler::join_strings(arg_strs, ", ");

        auto callee_type = m_type_checker.m_expression_types.at(expr.callee.get());

        // Case 1: The callee is a property access, e.g., `object.method(...)`.
        // This is the most complex case, covering methods and module functions.
        if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
            std::string object_str = transpileExpr(get_expr->object);
            const std::string& name = get_expr->name.lexeme;
            auto object_type = m_type_checker.m_expression_types.at(get_expr->object.get());

            // A) Method call on a built-in primitive type. This has the highest priority.
            if (object_type->kind == TypeKind::THREAD && name == "join") {
                return "angara_thread_join(" + object_str + ")";
            }
            if (object_type->kind == TypeKind::MUTEX && (name == "lock" || name == "unlock")) {
                return "angara_mutex_" + name + "(" + object_str + ")";
            }
            if (object_type->kind == TypeKind::LIST) {
                if (name == "push") return "angara_list_push(" + object_str + ", " + args_str + ")";
                if (name == "remove_at") return "angara_list_remove_at(" + object_str + ", " + args_str + ")"; // <-- ADD THIS
                if (name == "remove") return "angara_list_remove(" + object_str + ", " + args_str + ")"; // <-- ADD THIS
            }
            if (object_type->kind == TypeKind::RECORD) { // <-- ADD THIS BLOCK
                if (name == "remove") return "angara_record_remove(" + object_str + ", " + args_str + ")";
                if (name == "keys") return "angara_record_keys(" + object_str + ")";
            }


            // A) Method call on a class instance (e.g., `p.move(...)` or `my_counter.increment()`).
            if (object_type->kind == TypeKind::INSTANCE) {
                auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);

                // 1. Find which class in the inheritance hierarchy actually defines this method.
                //    `name` is the method name, e.g., "move".
                const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), name);

                // This should never happen if the TypeChecker is correct, but it's a safe guard.
                if (!owner_class) {
                    return "/* <compiler_error_unknown_method> */";
                }

                // 2. Dispatch to the correct C code based on whether the owner class was native or not.
                if (owner_class->is_native) {
                    // NATIVE METHOD: Transpile to a generic (argc, argv) call with `self` as the first argument.
                    // The mangled name is Angara_ClassName_MethodName, using the OWNER's name.
                    std::string final_args = object_str + (args_str.empty() ? "" : ", " + args_str);
                    return "Angara_" + owner_class->name + "_" + name + "(" +
                           std::to_string(expr.arguments.size() + 1) + ", (AngaraObject[]){" + final_args + "})";
                } else {
                    // ANGARA METHOD: Transpile to a direct, strongly-typed C call.
                    // The mangled name is Angara_ClassName_MethodName, using the OWNER's name.
                    std::string final_args = object_str + (args_str.empty() ? "" : ", " + args_str);
                    return "Angara_" + owner_class->name + "_" + name + "(" + final_args + ")";
                }
            }

            // C) Call on a symbol imported from a module.
            if (object_type->kind == TypeKind::MODULE) {
                auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);
                std::string mangled_name = "Angara_" + module_type->name + "_" + name;
                if (module_type->is_native) {
                    // NATIVE GLOBAL FUNCTION or NATIVE CONSTRUCTOR: Always use generic call.
                    return mangled_name + "(" + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
                } else {
                    // ANGARA symbol from another module: Call its global closure.
                    std::string closure_var = "g_" + name;
                    return "angara_call(" + closure_var + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
                }
            }

            if (callee_type->kind == TypeKind::FUNCTION) {
                auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);
                if (func_type->return_type->kind == TypeKind::ENUM) {
                    // This is an enum variant constructor call.
                    // The `transpileGetExpr` on the callee (`WebEvent.KeyPress`) has already
                    // produced the correct C function name (e.g., `Angara_WebEvent_KeyPress`).
                    std::string c_constructor_name = transpileExpr(expr.callee);
                    return c_constructor_name + "(" + args_str + ")";
                }
            }
        }

        // Case 2: The callee is a simple name in the current module scope.
        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            const std::string& name = var_expr->name.lexeme;

            // Check if this is a selectively imported native function
            auto symbol = m_type_checker.m_variable_resolutions.at(var_expr.get());
            if (symbol && symbol->from_module && symbol->from_module->is_native) {
                // It's a native function! Generate the correct mangled call.
                std::string mangled_name = "Angara_" + symbol->from_module->name + "_" + name;
                return mangled_name + "(" + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
            }

            // A) Check for BUILT-IN global functions first.
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
                std::vector<std::string> rest_arg_strs;
                for (size_t i = 1; i < expr.arguments.size(); ++i) {
                    rest_arg_strs.push_back(transpileExpr(expr.arguments[i]));
                }
                std::string rest_args_str = join_strings(rest_arg_strs, ", ");
                return "angara_spawn_thread(" + closure_str + ", " + std::to_string(rest_arg_strs.size()) + ", (AngaraObject[]){" + rest_args_str + "})";
            }

            if (callee_type->kind == TypeKind::DATA) {
                auto data_type = std::dynamic_pointer_cast<DataType>(callee_type);
                return "Angara_data_new_" + data_type->name + "(" + args_str + ")";
            }

            // B) Check if it's an ANGARA CLASS CONSTRUCTOR.
            if (callee_type->kind == TypeKind::CLASS) {
                return "Angara_" + name + "_new(" + args_str + ")";
            }

            // C) If not a built-in or constructor, it's an ANGARA GLOBAL FUNCTION. Call via closure.
            std::string closure_var = "g_" + name;
            if (name == "main") closure_var = "g_angara_main_closure";
            return "angara_call(" + closure_var + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
        }

            if (auto super_expr = std::dynamic_pointer_cast<const SuperExpr>(expr.callee)) {
                // The TypeChecker guarantees `super` is only used inside a class,
                // so `m_current_class_name` will be correctly set.
                if (m_current_class_name.empty()) {
                    // This is a safeguard; it should be unreachable if the TypeChecker is correct.
                    return "/* <compiler_error_super_outside_class> */";
                }

                // Get the full type information for the current class from the TypeChecker's results.
                auto current_class_symbol = m_type_checker.m_symbols.resolve(m_current_class_name);
                auto current_class_type = std::dynamic_pointer_cast<ClassType>(current_class_symbol->type);
                auto superclass_type = current_class_type->superclass;

                if (!super_expr->method) {
                    // Case A: Constructor call `super(...)`. Transpiles to a call to the parent's `init`.
                    // The first argument is `this_obj`, which is always in scope inside a method.
                    return "Angara_" + superclass_type->name + "_init(this_obj" + (args_str.empty() ? "" : ", " + args_str) + ")";
                } else {
                    // Case B: Regular method call `super.method(...)`.
                    const std::string& method_name = super_expr->method->lexeme;
                    // Transpiles to a direct call to the parent's C method function.
                    return "Angara_" + superclass_type->name + "_" + method_name + "(this_obj" + (args_str.empty() ? "" : ", " + args_str) + ")";
                }
            }

        // Case 3: Fallback for dynamic calls (e.g., calling a function stored in a variable).
        std::string callee_str = transpileExpr(expr.callee);
        return "angara_call(" + callee_str + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
    }
}