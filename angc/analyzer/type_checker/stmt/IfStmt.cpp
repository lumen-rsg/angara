#include "TypeChecker.h"
#include "Expr.h"
namespace angara {

    // Detect `varExpr != nil` or `varExpr == nil` (also `nil != varExpr`).
    // Returns the VarExpr if the condition is a nil-comparison against a
    // variable; sets `is_not_nil` to true for `!=`, false for `==`.
    static const VarExpr* detectNilCheck(const std::shared_ptr<Expr>& cond, bool& is_not_nil) {
        is_not_nil = false;
        auto bin_sp = std::dynamic_pointer_cast<const Binary>(cond);
        if (!bin_sp) return nullptr;
        const Binary* bin = bin_sp.get();
        bool bang = (bin->op.type == TokenType::BANG_EQUAL);
        bool eq   = (bin->op.type == TokenType::EQUAL_EQUAL);
        if (!bang && !eq) return nullptr;
        is_not_nil = bang;  // != nil means "is not nil"

        // left = var, right = nil
        if (auto ve_sp = std::dynamic_pointer_cast<const VarExpr>(bin->left)) {
            if (auto lit_sp = std::dynamic_pointer_cast<const Literal>(bin->right)) {
                if (lit_sp->token.type == TokenType::NIL) return ve_sp.get();
            }
        }
        // left = nil, right = var  (nil != x is the same as x != nil)
        if (auto ve_sp = std::dynamic_pointer_cast<const VarExpr>(bin->right)) {
            if (auto lit_sp = std::dynamic_pointer_cast<const Literal>(bin->left)) {
                if (lit_sp->token.type == TokenType::NIL) return ve_sp.get();
            }
        }
        return nullptr;
    }

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

        // TS-5: nil-check narrowing. `if (x != nil)` narrows x to its
        // unwrapped type in the then-branch; `if (x == nil)` narrows in the
        // else-branch. This lets you call methods on optionals after a nil
        // check without `if let` or @unsafe.
        bool is_not_nil = false;
        if (auto* nil_var = detectNilCheck(stmt->condition, is_not_nil)) {
            stmt->condition->accept(*this);
            popType();  // condition is bool; discard

            auto symbol = m_symbols.resolve(nil_var->name.lexeme);
            if (symbol && symbol->type && symbol->type->kind == TypeKind::OPTIONAL) {
                auto opt = std::dynamic_pointer_cast<OptionalType>(symbol->type);
                if (opt && opt->wrapped_type) {
                    // Narrow to unwrapped type in the branch where x is non-nil.
                    const Symbol* sym_ptr = symbol.get();
                    auto unwrapped = opt->wrapped_type;
                    auto then_narrow = is_not_nil;
                    auto else_narrow = !is_not_nil;

                    if (then_narrow) m_narrowed_types[sym_ptr] = unwrapped;
                    stmt->thenBranch->accept(*this, stmt->thenBranch);
                    if (then_narrow) m_narrowed_types.erase(sym_ptr);

                    if (stmt->elseBranch) {
                        if (else_narrow) m_narrowed_types[sym_ptr] = unwrapped;
                        stmt->elseBranch->accept(*this, stmt->elseBranch);
                        if (else_narrow) m_narrowed_types.erase(sym_ptr);
                    }
                    return;
                }
            }
            // Fall through if not an optional or symbol not found.
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
