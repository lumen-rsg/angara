//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::enumDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expect enum name.");
        consume(TokenType::LEFT_BRACE, "Expect '{' before enum body.");

        std::vector<std::shared_ptr<EnumVariant>> variants;

        // The first variant does not need a leading comma.
        if (!check(TokenType::RIGHT_BRACE)) {
            do {
                Token variant_name = consume(TokenType::IDENTIFIER, "Expect enum variant name.");

                std::vector<EnumVariantParam> params;
                // Check for associated data types in parentheses.
                if (match({TokenType::LEFT_PAREN})) {
                    if (!check(TokenType::RIGHT_PAREN)) {
                        do {
                            // Each parameter is just a type.
                            params.push_back({type()});
                        } while (match({TokenType::COMMA}));
                    }
                    consume(TokenType::RIGHT_PAREN, "Expect ')' after enum variant parameters.");
                }
                variants.push_back(std::make_shared<EnumVariant>(variant_name, std::move(params)));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after enum body.");
        return std::make_shared<EnumStmt>(std::move(name), std::move(variants));
    }

}