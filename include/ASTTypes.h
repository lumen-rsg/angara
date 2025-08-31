//
// Created by cv2 on 8/31/25.
//

#pragma once

#include "Token.h"
#include <memory>
#include <vector>
namespace angara {
    // Forward declarations
    struct SimpleType;
    struct GenericType;
    struct FunctionTypeExpr;
    struct RecordTypeExpr;


    // Visitor pattern for AST Type nodes
    class ASTTypeVisitor {
    public:
        virtual ~ASTTypeVisitor() = default;
        virtual void visit(const SimpleType &type) = 0;
        virtual void visit(const GenericType &type) = 0;
        virtual void visit(const FunctionTypeExpr& type) = 0;
        virtual void visit(const RecordTypeExpr& type) = 0;
    };

    // Base class for all AST Type representations
    struct ASTType {
        virtual ~ASTType() = default;

        virtual void accept(ASTTypeVisitor &visitor) const = 0;
    };

    // A simple struct for a field in a record type annotation
    struct RecordFieldType {
        Token name;
        std::shared_ptr<ASTType> type;
    };

    // Represents a simple type name like 'i64' or 'string'
    struct SimpleType : ASTType {
        const Token name;

        explicit SimpleType(Token name) : name(std::move(name)) {}

        void accept(ASTTypeVisitor &visitor) const override {
            visitor.visit(*this);
        }
    };

    // Represents a generic type like 'list<string>'
    struct GenericType : ASTType {
        const Token name; // The base type, e.g., 'list'
        // The type arguments, e.g., a list containing one 'SimpleType("string")'
        const std::vector<std::shared_ptr<ASTType>> arguments;

        GenericType(Token name, std::vector<std::shared_ptr<ASTType>> args)
                : name(std::move(name)), arguments(std::move(args)) {}

        void accept(ASTTypeVisitor &visitor) const override {
            visitor.visit(*this);
        }
    };

    struct FunctionTypeExpr : ASTType {
        const Token keyword;
        const std::vector<std::shared_ptr<ASTType>> param_types;
        const std::shared_ptr<ASTType> return_type;

        FunctionTypeExpr(Token keyword, std::vector<std::shared_ptr<ASTType>> params, std::shared_ptr<ASTType> ret)
                : keyword(std::move(keyword)),
                  param_types(std::move(params)),
                  return_type(std::move(ret)) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };

    struct RecordTypeExpr : ASTType {
        const Token keyword;
        const std::vector<RecordFieldType> fields;

        RecordTypeExpr(Token keyword, std::vector<RecordFieldType> fields)
                : keyword(std::move(keyword)), fields(std::move(fields)) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };
}