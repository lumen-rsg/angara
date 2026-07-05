//
// Created by cv2 on 8/31/25.
//

#pragma once

#include "Token.h"
#include <memory>
#include <vector>

#include "ASTTypes.h"

namespace angara {
    // Forward declarations
    struct SimpleType;
    struct GenericType;
    struct FunctionTypeExpr;
    struct RecordTypeExpr;
    struct OptionalTypeNode;
    struct FixedArrayTypeExpr;
    struct RawArrayTypeExpr;     // SIMD-1: unboxed dynamic array (e.g., f64[])
    struct PointerTypeExpr;
    struct OwnedTypeNode;
    struct TupleTypeExpr;  // LANG-10


    // Visitor pattern for AST Type nodes
    class ASTTypeVisitor {
    public:
        virtual ~ASTTypeVisitor() = default;
        virtual void visit(const SimpleType &type) = 0;
        virtual void visit(const GenericType &type) = 0;
        virtual void visit(const FunctionTypeExpr& type) = 0;
        virtual void visit(const RecordTypeExpr& type) = 0;
        virtual void visit(const OptionalTypeNode& type) = 0;
        virtual void visit(const FixedArrayTypeExpr& type) = 0;
        virtual void visit(const RawArrayTypeExpr& type) = 0;  // SIMD-1
        virtual void visit(const PointerTypeExpr& type) = 0;
        virtual void visit(const OwnedTypeNode& type) = 0;
        virtual void visit(const TupleTypeExpr& type) = 0;  // LANG-10
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
        const Token keyword; // The opening '{' token for location info
        const std::vector<RecordFieldType> fields;

        RecordTypeExpr(Token keyword, std::vector<RecordFieldType> fields)
                : keyword(std::move(keyword)), fields(std::move(fields)) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };

    struct OptionalTypeNode : ASTType {
        // The type that is being made optional, e.g., SimpleType("User")
        const std::shared_ptr<ASTType> base_type;

        explicit OptionalTypeNode(std::shared_ptr<ASTType> base)
            : base_type(std::move(base)) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };

    // Represents a fixed-size array type like i8[256], used in foreign data fields
    struct FixedArrayTypeExpr : ASTType {
        const std::shared_ptr<ASTType> element_type;
        const int size;

        FixedArrayTypeExpr(std::shared_ptr<ASTType> elem, int n)
            : element_type(std::move(elem)), size(n) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };

    // SIMD-1: Represents an unboxed dynamic array type like f64[], i64[]
    // Empty brackets (no size) distinguish this from FixedArrayTypeExpr.
    struct RawArrayTypeExpr : ASTType {
        const Token bracket;  // the '[' token (for error reporting)
        const std::shared_ptr<ASTType> element_type;

        RawArrayTypeExpr(Token bracket, std::shared_ptr<ASTType> elem)
            : bracket(std::move(bracket)), element_type(std::move(elem)) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };

    // Represents a pointer type like *i8, **i8, *void (FFI only)
    // Also represents byval ^Type when byval=true
    struct PointerTypeExpr : ASTType {
        const std::shared_ptr<ASTType> pointee_type;
        const int depth; // 1 for *, 2 for **
        bool byval = false; // true for ^Type (struct-by-value)

        PointerTypeExpr(std::shared_ptr<ASTType> pointee, int d)
            : pointee_type(std::move(pointee)), depth(d) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };

    // Represents @own type modifier for zero-copy string adoption (FFI only)
    struct OwnedTypeNode : ASTType {
        const std::shared_ptr<ASTType> inner_type;

        explicit OwnedTypeNode(std::shared_ptr<ASTType> inner)
            : inner_type(std::move(inner)) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };

    // LANG-10: tuple type annotation, e.g. (i64, string)
    struct TupleTypeExpr : ASTType {
        const Token paren;  // the opening '(' token
        const std::vector<std::shared_ptr<ASTType>> element_types;

        TupleTypeExpr(Token paren, std::vector<std::shared_ptr<ASTType>> types)
            : paren(std::move(paren)), element_types(std::move(types)) {}

        void accept(ASTTypeVisitor& visitor) const override {
            visitor.visit(*this);
        }
    };
}
