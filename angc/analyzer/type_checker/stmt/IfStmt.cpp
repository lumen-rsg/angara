#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const IfStmt> stmt) {
        if (stmt->declaration) {
            if (!stmt->declaration->initializer) {
                error(stmt->declaration->name, "Compiler error: 'if let' declaration is missing an initializer.", "E256");
                return;
            }

            stmt->declaration->initializer->accept(*this);
            auto initializer_type = popType();

            if (initializer_type->kind == TypeKind::ERROR) return;

            if (initializer_type->kind != TypeKind::OPTIONAL && initializer_type->kind != TypeKind::ANY) {
                error(stmt->declaration->name, "'if let' requires an optional type (e.g., 'string?') or 'any', but got a non-optional value of type '" + initializer_type->toString() + "'.", "E257");
            } else {
                m_symbols.enterScope();

                std::shared_ptr<Type> unwrapped_type;
                if (stmt->declaration->typeAnnotation) {
                    unwrapped_type = resolveType(stmt->declaration->typeAnnotation);
                } else if (initializer_type->kind == TypeKind::OPTIONAL) {
                    unwrapped_type = std::dynamic_pointer_cast<OptionalType>(initializer_type)->wrapped_type;
                } else {
                    unwrapped_type = m_type_any;
                }

                m_symbols.declare(stmt->declaration->name, unwrapped_type, true);

                stmt->thenBranch->accept(*this, stmt->thenBranch);

                exitScopeAndWarn();
            }

            if (stmt->elseBranch) {
                stmt->elseBranch->accept(*this, stmt->elseBranch);
            }
            return;
        }

        if (auto is_expr = std::dynamic_pointer_cast<const IsExpr>(stmt->condition)) {
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(is_expr->object)) {

                stmt->condition->accept(*this);
                popType();
                if (m_hadError) return;

                auto original_symbol = m_symbols.resolve(var_expr->name.lexeme);
                if (original_symbol) {
                    auto narrowed_type = resolveType(is_expr->type);

                    m_narrowed_types[original_symbol.get()] = narrowed_type;
                    stmt->thenBranch->accept(*this, stmt->thenBranch);
                    m_narrowed_types.erase(original_symbol.get());
                } else {
                    stmt->thenBranch->accept(*this, stmt->thenBranch);
                }

                if (stmt->elseBranch) {
                    stmt->elseBranch->accept(*this, stmt->elseBranch);
                }
                return;
            }
        }

        stmt->condition->accept(*this);
        auto condition_type = popType();

        if (!isTruthy(condition_type)) {
            error(stmt->keyword, "If statement condition must be a truthy type (bool or number), but got '" +
                                 condition_type->toString() + "'.", "E258");
        }

        stmt->thenBranch->accept(*this, stmt->thenBranch);
        if (stmt->elseBranch) {
            stmt->elseBranch->accept(*this, stmt->elseBranch);
        }
    }

}
