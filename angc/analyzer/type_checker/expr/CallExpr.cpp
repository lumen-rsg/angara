//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const CallExpr& expr) {
        // --- Phase 1: Evaluate Callee and Arguments ---
        expr.callee->accept(*this);
        auto callee_type = popType();
        std::vector<std::shared_ptr<Type>> arg_types;
        for (const auto& arg_expr : expr.arguments) {
            arg_expr->accept(*this);
            arg_types.push_back(popType());
        }
        if (m_hadError) { pushAndSave(&expr, m_type_error); return {}; }

        // --- Phase 2: Special Case Dispatch for `spawn` ---
        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            if (var_expr->name.lexeme == "spawn") {
                check_spawn_call(expr, arg_types);
                // The result type of spawn is always Thread.
                pushAndSave(&expr, m_hadError ? m_type_error : m_type_thread);
                return {};
            }
        }

        // --- Phase 3: Main Dispatch based on Callee Type ---
        std::shared_ptr<Type> result_type = m_type_error;

        if (callee_type->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);
            check_function_call(expr, func_type, arg_types);
            if (!m_hadError) {
                result_type = func_type->return_type;
            }
        }
        else if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
            // Use findProperty to also check superclass for inherited constructors
            const auto* init_prop = class_type->findProperty("init");

            if (!init_prop) {
                // No 'init' method found in this class or any superclass.
                if (!arg_types.empty()) {
                    error(expr.paren, "Class '" + class_type->name + "' does not have a constructor that accepts arguments.");
                }
            } else {
                // An 'init' method exists (possibly inherited), use it to validate the call.
                auto init_sig = std::dynamic_pointer_cast<FunctionType>(init_prop->type);
                check_function_call(expr, init_sig, arg_types);
            }

            if (!m_hadError) {
                result_type = std::make_shared<InstanceType>(class_type);
            }
        } else if (callee_type->kind == TypeKind::DATA) { // <-- ADD THIS BLOCK
            auto data_type = std::dynamic_pointer_cast<DataType>(callee_type);
            // Validate the call against the synthesized constructor signature.
            check_function_call(expr, data_type->constructor_type, arg_types);
            if (!m_hadError) {
                // The result of the call is the data type itself.
                result_type = data_type;
            }
        }
        else if (callee_type->kind == TypeKind::ANY) {
            // Allow dynamic calls on 'any' objects.
            // Since we can't check arguments statically, we assume they are correct.
            // This operation is dynamic, so we require an unsafe context.
            if (m_is_in_unsafe_context) {
                // The return type of a dynamic call is always 'any'.
                result_type = m_type_any;
            } else {
                error(expr.paren, "Calling a value of type 'any' is unsafe. Wrap this call in an '@unsafe { ... }' block.");
            }
        }
        else {
            error(expr.paren, "This expression is not callable. Can only call functions and classes.");
        }

        pushAndSave(&expr, result_type);
        return {};
    }

// A helper for the special-case validation logic for `spawn`.
    void TypeChecker::check_spawn_call(
            const CallExpr& call,
            const std::vector<std::shared_ptr<Type>>& arg_types
    ) {
        // Rule 1: spawn() must be called with at least one argument (the function).
        if (arg_types.empty()) {
            error(call.paren, "spawn() requires at least one argument, the function to execute in the new thread.");
            return;
        }

        // Rule 2: The first argument to spawn() must be a function.
        auto closure_type = arg_types[0];
        if (closure_type->kind != TypeKind::FUNCTION) {
            error(call.paren, "The first argument to spawn() must be a function, but got a value of type '" + closure_type->toString() + "'.");
            return;
        }
        auto func_type = std::dynamic_pointer_cast<FunctionType>(closure_type);

        // The arguments passed to spawn (after the function itself) must match the function's parameters.
        size_t num_expected_args = func_type->param_types.size();
        size_t num_actual_args = arg_types.size() - 1;

        // Rule 3: Check arity for the spawned function.
        if (num_actual_args != num_expected_args) {
            error(call.paren, "Incorrect number of arguments for the spawned function. "
                              "The function expects " + std::to_string(num_expected_args) +
                              " argument(s), but " + std::to_string(num_actual_args) + " were provided to spawn().");

            // Add a note pointing to the function being spawned, if it's a direct variable.
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.arguments[0])) {
                if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                    note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                }
            }
            return;
        }

        // Rule 4: Check the type of each argument for the spawned function.
        for (size_t i = 0; i < num_actual_args; ++i) {
            const auto& expected_type = func_type->param_types[i];
            // The actual arguments start from the second element of the `spawn` call.
            const auto& actual_type = arg_types[i + 1];

            if (!check_type_compatibility(expected_type, actual_type)) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + " of spawned function. "
                                                                                          "Expected '" + expected_type->toString() + "', but got '" + actual_type->toString() + "'.");

                // Add the same helpful note.
                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.arguments[0])) {
                    if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                        note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                    }
                }
                return; // Stop after the first error.
            }
        }
    }

// A helper to validate a standard function or method call against a signature.
    void TypeChecker::check_function_call(
            const CallExpr& call,
            const std::shared_ptr<FunctionType>& func_type,
            const std::vector<std::shared_ptr<Type>>& arg_types
    ) {
        size_t num_expected = func_type->param_types.size();
        size_t num_actual = arg_types.size();
        bool arity_ok = true;

        // 1. Check arity (number of arguments)
        if (func_type->is_variadic) {
            // Variadic: Must have at least the fixed args
            if (num_actual < num_expected) arity_ok = false;
        } else {
            // Regular: Cannot have MORE arguments
            if (num_actual > num_expected) {
                arity_ok = false;
            }
            // Can have FEWER arguments only if the missing ones are Optional
            else if (num_actual < num_expected) {
                for (size_t i = num_actual; i < num_expected; ++i) {
                    if (func_type->param_types[i]->kind != TypeKind::OPTIONAL) {
                        arity_ok = false;
                        break;
                    }
                }
            }
        }

        if (!arity_ok) {
            error(call.paren, "Incorrect number of arguments. Function expects " +
                              std::to_string(num_expected) + " argument(s), but got " +
                              std::to_string(num_actual) + ".");

            // Add a helpful note if possible
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.callee)) {
                if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                    note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                }
            }
            return;
        }

        // If an arity error occurred upstream, stop checking to prevent crashes.
        if (m_hadError) return;

        // 2. Check types of the PROVIDED arguments
        // FIX: We must not iterate past 'num_actual'. We cannot check types for
        // optional arguments that were omitted (because there is no expression to check).

        size_t check_limit = num_actual;

        // For variadic functions, we only strictly check the fixed parameters here.
        // (Variadic arguments are usually 'any' or checked dynamically).
        if (func_type->is_variadic && check_limit > num_expected) {
            check_limit = num_expected;
        }

        for (size_t i = 0; i < check_limit; ++i) {
            const auto& expected_type = func_type->param_types[i];
            const auto& actual_type = arg_types[i];
            const auto& arg_expr = call.arguments[i];

            // Special Case: Empty List Literal `[]` matches any `list<T>`
            if (auto list_lit = std::dynamic_pointer_cast<const ListExpr>(arg_expr)) {
                if (list_lit->elements.empty()) {
                    if (expected_type->kind == TypeKind::LIST) {
                        continue; // Match found, skip standard check
                    }
                }
            }

            // Standard Type Compatibility Check
            if (!check_type_compatibility(expected_type, actual_type)) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + ". " +
                                  "Expected '" + expected_type->toString() +
                                  "', but got '" + actual_type->toString() + "'.");
                return; // Stop after first type error
            }
        }
    }

}