//
// Created by cv2 on 8/27/25.
//
#include "Token.h"
namespace angara {

    std::string to_string(const TokenType &type) {
        switch (type) {
            // Single-character tokens
            case TokenType::LEFT_PAREN: return "LEFT_PAREN";
            case TokenType::RIGHT_PAREN: return "RIGHT_PAREN";
            case TokenType::LEFT_BRACE: return "LEFT_BRACE";
            case TokenType::RIGHT_BRACE: return "RIGHT_BRACE";
            case TokenType::LEFT_BRACKET: return "LEFT_BRACKET";
            case TokenType::RIGHT_BRACKET: return "RIGHT_BRACKET";
            case TokenType::COMMA: return "COMMA";
            case TokenType::DOT: return "DOT";
            case TokenType::MINUS: return "MINUS";
            case TokenType::PLUS: return "PLUS";
            case TokenType::SLASH: return "SLASH";
            case TokenType::STAR: return "STAR";
            case TokenType::PERCENT: return "PERCENT";
            case TokenType::COLON: return "COLON";
            case TokenType::SEMICOLON: return "SEMICOLON";
            case TokenType::QUESTION: return "QUESTION";
            case TokenType::AT_SIGN: return "AT_SIGN";

            // Two-character operators
            case TokenType::PLUS_PLUS: return "PLUS_PLUS";
            case TokenType::MINUS_MINUS: return "MINUS_MINUS";
            case TokenType::LOGICAL_AND: return "LOGICAL_AND";
            case TokenType::LOGICAL_OR: return "LOGICAL_OR";
            case TokenType::PLUS_EQUAL: return "PLUS_EQUAL";
            case TokenType::MINUS_EQUAL: return "MINUS_EQUAL";
            case TokenType::STAR_EQUAL: return "STAR_EQUAL";
            case TokenType::SLASH_EQUAL: return "SLASH_EQUAL";
            case TokenType::MINUS_GREATER: return "MINUS_GREATER";

            // One or two character tokens
            case TokenType::BANG: return "BANG";
            case TokenType::BANG_EQUAL: return "BANG_EQUAL";
            case TokenType::EQUAL: return "EQUAL";
            case TokenType::EQUAL_EQUAL: return "EQUAL_EQUAL";
            case TokenType::GREATER: return "GREATER";
            case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
            case TokenType::LESS: return "LESS";
            case TokenType::LESS_EQUAL: return "LESS_EQUAL";
            case TokenType::PIPE: return "PIPE";
            case TokenType::AMPERSAND: return "AMPERSAND";
            case TokenType::CARET: return "CARET";
            case TokenType::TILDE: return "TILDE";
            case TokenType::QUESTION_QUESTION: return "QUESTION_QUESTION";
            case TokenType::DOT_DOT: return "DOT_DOT";
            case TokenType::DOT_DOT_DOT: return "DOT_DOT_DOT";
            case TokenType::QUESTION_DOT: return "QUESTION_DOT";

            // Literals
            case TokenType::IDENTIFIER: return "IDENTIFIER";
            case TokenType::STRING: return "STRING";
            case TokenType::NUMBER_INT: return "NUMBER_INT";
            case TokenType::NUMBER_FLOAT: return "NUMBER_FLOAT";

            // Keywords
            case TokenType::LET: return "LET";
            case TokenType::CONST: return "CONST";
            case TokenType::IF: return "IF";
            case TokenType::ELSE: return "ELSE";
            case TokenType::ORIF: return "ORIF";
            case TokenType::FOR: return "FOR";
            case TokenType::WHILE: return "WHILE";
            case TokenType::IN: return "IN";
            case TokenType::FUNC: return "FUNC";
            case TokenType::RETURN: return "RETURN";
            case TokenType::TRUE: return "TRUE";
            case TokenType::FALSE: return "FALSE";
            case TokenType::TRY: return "TRY";
            case TokenType::CATCH: return "CATCH";
            case TokenType::ATTACH: return "ATTACH";
            case TokenType::NIL: return "NIL";
            case TokenType::THROW: return "THROW";
            case TokenType::FROM: return "FROM";
            case TokenType::CLASS: return "CLASS";
            case TokenType::THIS: return "THIS";
            case TokenType::INHERITS: return "INHERITS";
            case TokenType::SUPER: return "SUPER";
            case TokenType::TRAIT: return "TRAIT";
            case TokenType::USES: return "USES";
            case TokenType::STATIC: return "STATIC";
            case TokenType::PRIVATE: return "PRIVATE";
            case TokenType::PUBLIC: return "PUBLIC";
            case TokenType::EXPORT: return "EXPORT";
            case TokenType::CONTRACT: return "CONTRACT";
            case TokenType::SIGNS: return "SIGNS";
            case TokenType::BREAK: return "BREAK";
            case TokenType::CONTINUE: return "CONTINUE";
            case TokenType::IS: return "IS";
            case TokenType::DATA: return "DATA";
            case TokenType::ENUM: return "ENUM";
            case TokenType::MATCH: return "MATCH";
            case TokenType::CASE: return "CASE";
            case TokenType::FOREIGN: return "FOREIGN";
            case TokenType::INTRINSIC: return "INTRINSIC";
            case TokenType::SIZEOF: return "SIZEOF";
            case TokenType::RETYPE: return "RETYPE";

            // Type Keywords
            case TokenType::TYPE_STRING: return "TYPE_STRING";
            case TokenType::TYPE_INT: return "TYPE_INT";
            case TokenType::TYPE_FLOAT: return "TYPE_FLOAT";
            case TokenType::TYPE_BOOL: return "TYPE_BOOL";
            case TokenType::TYPE_LIST: return "TYPE_LIST";
            case TokenType::TYPE_MAP: return "TYPE_MAP";
            case TokenType::TYPE_VOID: return "TYPE_VOID";

            case TokenType::EOF_TOKEN: return "EOF_TOKEN";
            case TokenType::AS: return "AS";
            case TokenType::TYPE_I8: return "TYPE_I8";
            case TokenType::TYPE_I16: return "TYPE_I16";
            case TokenType::TYPE_I32: return "TYPE_I32";
            case TokenType::TYPE_I64: return "TYPE_I64";
            case TokenType::TYPE_U8: return "TYPE_U8";
            case TokenType::TYPE_U16: return "TYPE_U16";
            case TokenType::TYPE_U32: return "TYPE_U32";
            case TokenType::TYPE_U64: return "TYPE_U64";
            case TokenType::TYPE_UINT: return "TYPE_UINT";
            case TokenType::TYPE_F32: return "TYPE_F32";
            case TokenType::TYPE_F64: return "TYPE_F64";
            case TokenType::TYPE_NIL: return "TYPE_NIL";
            case TokenType::TYPE_RECORD: return "TYPE_RECORD";
            case TokenType::TYPE_FUNCTION: return "TYPE_FUNCTION";
            case TokenType::TYPE_ANY: return "TYPE_ANY";
            case TokenType::TYPE_THREAD: return "TYPE_THREAD";
        }
        return "[[UNKNOWN_TOKEN]]";
    }

    Token::Token() {

    }
}