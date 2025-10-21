//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const IfStmt> stmt) {
        // --- Case 1: Handle `if let ...` for optional unwrapping ---
        if (stmt->declaration) {
            // 1a. The declaration must have an initializer. The parser should guarantee this.
            if (!stmt->declaration->initializer) {
                error(stmt->declaration->name, "Compiler Error: 'if let' declaration is missing an initializer.");
                return;
            }

            // 1b. Type check the initializer expression.
            stmt->declaration->initializer->accept(*this);
            auto initializer_type = popType();

            if (initializer_type->kind == TypeKind::ERROR) return;

            // 1c. The initializer MUST be an optional type.
            if (initializer_type->kind != TypeKind::OPTIONAL && initializer_type->kind != TypeKind::ANY) {
                error(stmt->declaration->name, "The value for an 'if let' statement must be an optional type (e.g., 'string?') or 'any', but got a non-optional value of type '" + initializer_type->toString() + "'.");
            } else {
                // It is a valid type for `if let`. Proceed.
                m_symbols.enterScope();

                // Determine the type of the new, unwrapped variable.
                std::shared_ptr<Type> unwrapped_type;
                if (stmt->declaration->typeAnnotation) {
                    // Case A: `if (let v as string = my_any)`
                    // The user is providing an explicit type assertion. Use that.
                    unwrapped_type = resolveType(stmt->declaration->typeAnnotation);
                } else if (initializer_type->kind == TypeKind::OPTIONAL) {
                    // Case B: `if (let v = my_optional)`
                    // Infer the type from the wrapped type of the optional.
                    unwrapped_type = std::dynamic_pointer_cast<OptionalType>(initializer_type)->wrapped_type;
                } else {
                    // Case C: `if (let v = my_any)`
                    // There is no type annotation, so the unwrapped type is still `any`.
                    unwrapped_type = m_type_any;
                }

                // Declare the new variable with the unwrapped type inside the new scope.
                m_symbols.declare(stmt->declaration->name, unwrapped_type, true);

                // Now, check the 'then' branch.
                stmt->thenBranch->accept(*this, stmt->thenBranch);

                m_symbols.exitScope();
            }

            // Check the 'else' branch normally. The unwrapped variable is not in scope here.
            if (stmt->elseBranch) {
                stmt->elseBranch->accept(*this, stmt->elseBranch);
            }
            return; // We have handled the entire `if let` statement.
        }


        // --- Case 2: Handle `if ... is ...` for type narrowing ---
        if (auto is_expr = std::dynamic_pointer_cast<const IsExpr>(stmt->condition)) {
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(is_expr->object)) {
                // The condition is a type check on a variable.

                // 2a. Type check the condition itself to make sure it's valid.
                stmt->condition->accept(*this);
                popType(); // We don't need the boolean result, just the validation.
                if (m_hadError) return;

                auto original_symbol = m_symbols.resolve(var_expr->name.lexeme);
                if (original_symbol) {
                    auto narrowed_type = resolveType(is_expr->type);

                    // 2b. Apply the narrowed type for the 'then' branch.
                    m_narrowed_types[original_symbol.get()] = narrowed_type;
                    stmt->thenBranch->accept(*this, stmt->thenBranch);
                    // 2c. CRITICAL: Remove the narrowing after the 'then' branch is done.
                    m_narrowed_types.erase(original_symbol.get());
                } else {
                    // This should have been caught when visiting the condition, but for safety:
                    stmt->thenBranch->accept(*this, stmt->thenBranch);
                }

                // 2d. Check the 'else' branch normally, without the narrowing.
                if (stmt->elseBranch) {
                    stmt->elseBranch->accept(*this, stmt->elseBranch);
                }
                return; // We have handled the entire `if is` statement.
            }
        }


        // --- Case 3: Handle a regular `if` statement with a boolean condition ---
        stmt->condition->accept(*this);
        auto condition_type = popType();

        if (!isTruthy(condition_type)) {
            error(stmt->keyword, "If statement condition must be a boolean or truthy value, but got '" +
                                 condition_type->toString() + "'.");
        }

        // Check both branches normally.
        stmt->thenBranch->accept(*this, stmt->thenBranch);
        if (stmt->elseBranch) {
            stmt->elseBranch->accept(*this, stmt->elseBranch);
        }
    }

}