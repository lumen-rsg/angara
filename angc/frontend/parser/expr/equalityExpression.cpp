//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {
    std::shared_ptr<Expr> Parser::equality() {
        std::shared_ptr<Expr> expr = comparison();
        while (match({TokenType::BANG_EQUAL, TokenType::EQUAL_EQUAL, TokenType::IS, TokenType::AS})) {
            Token op = previous();

            if (op.type == TokenType::IS) {
                std::shared_ptr<ASTType> type_rhs = type();
                expr = std::make_shared<IsExpr>(std::move(expr), std::move(op), std::move(type_rhs));
            } else if (op.type == TokenType::AS) {
                std::shared_ptr<ASTType> target_type = type();
                expr = std::make_shared<CastExpr>(std::move(expr), std::move(op), std::move(target_type));
            } else {
                std::shared_ptr<Expr> right = comparison();
                expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
            }
        }
        return expr;
    }

}