#include "Parser.h"
namespace angara {

    std::shared_ptr<Expr> Parser::assignment() {
        std::shared_ptr<Expr> expr = range();  // LANG-1: range binds tighter than assignment

        if (match({TokenType::EQUAL, TokenType::PLUS_EQUAL, TokenType::MINUS_EQUAL,
                   TokenType::STAR_EQUAL, TokenType::SLASH_EQUAL,
                   TokenType::PERCENT_EQUAL, TokenType::AMPERSAND_EQUAL,
                   TokenType::PIPE_EQUAL, TokenType::CARET_EQUAL,
                   TokenType::LSHIFT_EQUAL, TokenType::RSHIFT_EQUAL})) {

            Token op = previous();
            if (++m_recursionDepth > 256) {
                throw error(op, "Maximum recursion depth exceeded — too many chained assignments.", "E248");
            }
            std::shared_ptr<Expr> value = assignment();
            --m_recursionDepth;

            // LANG-10: destructuring assignment — (a, b) = expr
            if (auto* tuple = dynamic_cast<TupleExpr*>(expr.get())) {
                // Validate all elements are simple variable names
                for (const auto& elem : tuple->elements) {
                    if (!dynamic_cast<VarExpr*>(elem.get())) {
                        throw error(op, "Destructuring assignment targets must be simple variable names.", "E214");
                    }
                }
                // Check for duplicate names
                for (size_t i = 0; i < tuple->elements.size(); ++i) {
                    auto* vi = static_cast<VarExpr*>(tuple->elements[i].get());
                    for (size_t j = i + 1; j < tuple->elements.size(); ++j) {
                        auto* vj = static_cast<VarExpr*>(tuple->elements[j].get());
                        if (vi->name.lexeme == vj->name.lexeme) {
                            throw error(vi->name, "Duplicate variable name '" + vi->name.lexeme + "' in destructuring assignment.", "E214");
                        }
                    }
                }
                return std::make_shared<AssignExpr>(std::move(expr), op, std::move(value));
            }

            if (dynamic_cast<VarExpr *>(expr.get()) ||
                dynamic_cast<GetExpr *>(expr.get()) ||
                dynamic_cast<SubscriptExpr *>(expr.get())) {
                return std::make_shared<AssignExpr>(std::move(expr), op, std::move(value));
                }

            error(op, "Cannot assign to this expression. Only variables ('x'), field accesses ('obj.field'), subscripts ('arr[i]'), and tuple destructuring ('(a, b)') can be assigned to.", "E214");
                   }

        return expr;
    }

}
