#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::enumDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected enum name after 'enum'.", "E159");

        // LANG-8: parse optional type parameters (e.g., enum Result<T,E>)
        std::map<std::string, Token> type_param_bounds;
        auto type_params = parseTypeParams(type_param_bounds);

        consume(TokenType::LEFT_BRACE, "Expected '{' before enum body.", "E160");

        std::vector<std::shared_ptr<EnumVariant>> variants;

        if (!check(TokenType::RIGHT_BRACE)) {
            do {
                if (check(TokenType::RIGHT_BRACE)) break;
                Token variant_name = consume(TokenType::IDENTIFIER, "Expected variant name in enum.", "E161");

                std::vector<EnumVariantParam> params;
                if (match({TokenType::LEFT_PAREN})) {
                    if (!check(TokenType::RIGHT_PAREN)) {
                        do {
                            params.push_back({type()});
                        } while (match({TokenType::COMMA}));
                    }
                    consume(TokenType::RIGHT_PAREN, "Expected ')' after variant payload types.", "E162");
                }
                variants.push_back(std::make_shared<EnumVariant>(variant_name, std::move(params)));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after enum body.", "E163");
        return std::make_shared<EnumStmt>(std::move(name), std::move(variants),
                                          std::move(type_params), std::move(type_param_bounds));
    }

}
