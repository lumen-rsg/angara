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
            auto init_it = class_type->methods.find("init");

            if (init_it == class_type->methods.end()) {
                // No 'init' method found. This class can only be constructed with zero arguments.
                if (!arg_types.empty()) {
                    error(expr.paren, "Class '" + class_type->name + "' does not have a constructor that accepts arguments.");
                    // We can add a note pointing to the class definition.
                    // This requires getting the token from the ClassType, which is an enhancement for later.
                    // For now, the error message is very clear. TODO
                }
            } else {
                // An 'init' method exists, use it to validate the call.
                auto init_sig = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
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

        // 1. Check arity (number of arguments)
        if (func_type->is_variadic) {
            // For variadic functions, the user must supply at least the required parameters.
            if (num_actual < num_expected) {
                error(call.paren, "Incorrect number of arguments. Function expects at least " +
                                  std::to_string(num_expected) + " argument(s), but got " +
                                  std::to_string(num_actual) + ".");
                // Add a note pointing to the function's definition.
                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.callee)) {
                    if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                        note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                    }
                }
            }
        } else {
            // For regular functions, the number of arguments must match exactly.
            if (num_actual != num_expected) {
                error(call.paren, "Incorrect number of arguments. Function expects " +
                                  std::to_string(num_expected) + " argument(s), but got " +
                                  std::to_string(num_actual) + ".");
                // Add the same helpful note.
                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.callee)) {
                    if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                        note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                    }
                } else if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(call.callee)) {
                    // Handle notes for method calls, e.g., my_instance.method()
                    // This requires more complex logic to find the method's declaration token
                    // in the ClassType, which will be added later. TODO
                }
            }
        }

        // If an arity error occurred, stop checking.
        if (m_hadError) return;

        // 2. Check types of the fixed parameters
        for (size_t i = 0; i < num_expected; ++i) {
            const auto& expected_type = func_type->param_types[i];
            const auto& actual_type = arg_types[i];

            // --- THIS IS THE FIX ---
            // Get the original AST node for the argument.
            const auto& arg_expr = call.arguments[i];

            // Check for the special case: is the argument an empty list literal `[]`?
            if (auto list_lit = std::dynamic_pointer_cast<const ListExpr>(arg_expr)) {
                if (list_lit->elements.empty()) {
                    // It is an empty list. Is the expected type ANY kind of list?
                    if (expected_type->kind == TypeKind::LIST) {
                        // Yes. Consider this a match and continue to the next argument.
                        continue;
                    }
                }
            }
            // --- END OF FIX ---

            // If it's not the special case, perform the standard compatibility check.
            if (!check_type_compatibility(expected_type, actual_type)) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + ". " +
                                  "Expected '" + expected_type->toString() +
                                  "', but got '" + actual_type->toString() + "'.");
                return; // Stop after first type error
            }
        }
    }

}