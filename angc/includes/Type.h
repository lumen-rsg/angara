#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <sstream>
#include "Token.h"

#include "AccessLevel.h"

namespace angara {

    // An enum for the base kinds of types
    enum class TypeKind {
        PRIMITIVE,
        LIST,
        RECORD,
        FUNCTION,
        CLASS,
        TRAIT,
        CONTRACT,
        INSTANCE,
        ANY,
        NIL,
        THREAD,
        MUTEX,
        MODULE,
        EXCEPTION,
        OPTIONAL,
        DATA,
        ENUM,
        FIXED_ARRAY,
        TYPE_PARAM, // A generic type parameter (e.g., T in data Box<T>)
        GENERIC_INSTANCE, // A concrete instantiation of a generic type (e.g., Box<i64>)
        POINTER, // FFI pointer type (e.g., *i8, *void, **char)
        VOID,    // C void type (only valid in FFI pointer context or as return type)
        ERROR // A special type to prevent cascading error messages
    };


    struct Type;
    struct EnumType;
    struct EnumVariantType;
    struct ClassType; // Needed for DataType and others

    // --- BASE TYPE CLASS ---
    struct Type {
        const TypeKind kind;
        explicit Type(TypeKind kind) : kind(kind) {}
        virtual ~Type() = default;

        // For pretty-printing in error messages
        [[nodiscard]] virtual std::string toString() const = 0;
    };

    // --- PRIMITIVE TYPES ---
    struct PrimitiveType : Type {
        const std::string name;
        explicit PrimitiveType(std::string name)
                : Type(TypeKind::PRIMITIVE), name(std::move(name)) {}
        [[nodiscard]] std::string toString() const override { return name; }
    };

    // --- COMPOUND TYPES ---
    struct ListType : Type {
        const std::shared_ptr<Type> element_type;
        explicit ListType(std::shared_ptr<Type> element_type)
                : Type(TypeKind::LIST), element_type(std::move(element_type)) {}
        [[nodiscard]] std::string toString() const override {
            return "list<" + element_type->toString() + ">";
        }
    };

    struct RecordType : Type {
        // A map from field name to the Type of that field.
        const std::map<std::string, std::shared_ptr<Type>> fields;
        explicit RecordType(std::map<std::string, std::shared_ptr<Type>> fields)
                : Type(TypeKind::RECORD), fields(std::move(fields)) {}

        [[nodiscard]] std::string toString() const override {
            std::stringstream ss;
            ss << "{";
            for (auto it = fields.begin(); it != fields.end(); ++it) {
                ss << it->first << ": " << it->second->toString();
                if (std::next(it) != fields.end()) {
                    ss << ", ";
                }
            }
            ss << "}";
            return ss.str();
        }
    };

    struct FunctionType : Type {
        const std::vector<std::shared_ptr<Type>> param_types;
        const std::shared_ptr<Type> return_type;

        const bool is_variadic;
        bool is_foreign = false;
        bool is_intrinsic = false;

        // Update constructor to accept the flag, defaulting to false.
        FunctionType(std::vector<std::shared_ptr<Type>> params, std::shared_ptr<Type> ret, bool is_variadic = false)
                : Type(TypeKind::FUNCTION),
                  param_types(std::move(params)),
                  return_type(std::move(ret)),
                  is_variadic(is_variadic) {}

        std::string toString() const override {
            std::stringstream ss;
            ss << "function(";
            for (size_t i = 0; i < param_types.size(); ++i) {
                ss << param_types[i]->toString();
                if (is_variadic && i == param_types.size() - 1) {
                    ss << "...";
                }
                if (i < param_types.size() - 1) {
                    ss << ", ";
                }
            }
            ss << ") -> " << return_type->toString();
            return ss.str();
        }

        // The 'equals' method also needs a small update.
        bool equals(const FunctionType& other) const {
            // 1. Check variadic flag
            if (this->is_variadic != other.is_variadic) return false;

            // 2. Check arity
            if (this->param_types.size() != other.param_types.size()) {
                return false;
            }

            // 3. Check each parameter's type
            for (size_t i = 0; i < this->param_types.size(); ++i) {
                // We are comparing the string representations.
                if (this->param_types[i]->toString() != other.param_types[i]->toString()) {
                    return false;
                }
            }

            // 4. Check the return type
            if (this->return_type->toString() != other.return_type->toString()) {
                return false;
            }

            return true;
        }
    };


    // --- CLASS-RELATED TYPES ---
    // Represents the type of class itself (the factory)

    struct ClassType : Type {

        struct MemberInfo {
            std::shared_ptr<Type> type;
            AccessLevel access;
            Token declaration_token;
            bool is_const;
        };
        bool is_native;
        const std::string name;
        std::shared_ptr<ClassType> superclass = nullptr;
        std::map<std::string, MemberInfo> fields;
        std::map<std::string, MemberInfo> methods;

        // --- GENERIC SUPPORT ---
        std::vector<std::string> type_params;

        explicit ClassType(std::string name)
                : Type(TypeKind::CLASS), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return name; }

        [[nodiscard]] bool is_generic() const { return !type_params.empty(); }

        [[nodiscard]] const MemberInfo* findProperty(const std::string& prop_name) const {
            // 1. Check the current class's fields.
            auto field_it = fields.find(prop_name);
            if (field_it != fields.end()) {
                return &field_it->second;
            }

            // 2. Check the current class's methods.
            auto method_it = methods.find(prop_name);
            if (method_it != methods.end()) {
                return &method_it->second;
            }

            // 3. If not found, check the superclass (recursive step).
            if (superclass) {
                return superclass->findProperty(prop_name);
            }

            // 4. If we reach the top of the chain, it's not found.
            return nullptr;
        }

    };

    // Represents the type of *instance* of a class
    struct InstanceType : Type {
        // An instance's type is defined by the class it belongs to.
        const std::shared_ptr<ClassType> class_type;
        explicit InstanceType(std::shared_ptr<ClassType> class_type)
                : Type(TypeKind::INSTANCE), class_type(std::move(class_type)) {}
        [[nodiscard]] std::string toString() const override { return class_type->name; }
    };

    struct TraitType : Type {
        const std::string name;
        // A map from method name to that method's FunctionType.
        std::map<std::string, std::shared_ptr<FunctionType>> methods;

        explicit TraitType(std::string name)
                : Type(TypeKind::TRAIT), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return name; }
    };

    inline bool isFloat(const std::shared_ptr<Type>& type) {
        if (!type || type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "f32" || name == "f64";
    }

    inline bool isInteger(const std::shared_ptr<Type>& type) {
        if (!type || type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
               name == "u8" || name == "u16" || name == "u32" || name == "u64";
    }

    inline bool isUnsignedInteger(const std::shared_ptr<Type>& type) {
        if (!type || type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "u8" || name == "u16" || name == "u32" || name == "u64";
    }

    inline bool isNumeric(const std::shared_ptr<Type>& type) {
        return isInteger(type) || isFloat(type);
    }

    struct ThreadType : Type {
        ThreadType() : Type(TypeKind::THREAD) {}
        std::string toString() const override { return "Thread"; }
    };

    struct MutexType : Type {
        MutexType() : Type(TypeKind::MUTEX) {}
        std::string toString() const override { return "Mutex"; }
    };

    struct NilType : Type {
        NilType() : Type(TypeKind::NIL) {}
        std::string toString() const override { return "nil"; }
    };

    struct AnyType : Type {
        AnyType() : Type(TypeKind::ANY) {}
        std::string toString() const override { return "any"; }
    };

    // The semantic representation of a contract.
    struct ContractType : Type {
        const std::string name;
        // A contract defines a set of required fields and methods.
        // We can reuse the MemberInfo struct from ClassType.


        struct MemberInfo {
            std::shared_ptr<Type> type;
            Token declaration_token; // <-- The important addition
            bool is_const;
        };

        std::map<std::string, MemberInfo> fields;
        std::map<std::string, MemberInfo> methods;

        explicit ContractType(std::string name)
            : Type(TypeKind::CONTRACT), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return "contract<" + name + ">"; }
    };

    struct ExceptionType : Type {
        std::map<std::string, ClassType::MemberInfo> fields;

        ExceptionType() : Type(TypeKind::EXCEPTION) {
            // Pre-populate the fields map.
            // The message is a public, constant string.
            auto string_type = std::make_shared<PrimitiveType>("string");
            fields["message"] = {string_type, AccessLevel::PUBLIC, Token(), true};
        }

        [[nodiscard]] std::string toString() const override { return "Exception"; }
    };

    struct OptionalType : Type {
        const std::shared_ptr<Type> wrapped_type;
        explicit OptionalType(std::shared_ptr<Type> wrapped_type)
                : Type(TypeKind::OPTIONAL), wrapped_type(std::move(wrapped_type)) {}

        [[nodiscard]] std::string toString() const override {
            return wrapped_type->toString() + "?";
        }
    };

    struct DataType : Type {
        const std::string name;
        // We can reuse MemberInfo to store field type and const-ness.
        std::map<std::string, ClassType::MemberInfo> fields;

        // Data types also have an implicit constructor. We store its signature here.
        std::shared_ptr<FunctionType> constructor_type;
        bool is_foreign = false;
        bool is_opaque = false; // true for "foreign data FILE;" (no fields)

        // --- GENERIC SUPPORT ---
        // Type parameter names for this generic data type (e.g., {"T"} for Box<T>)
        std::vector<std::string> type_params;

        explicit DataType(std::string name)
            : Type(TypeKind::DATA), name(std::move(name)) {}

        // The string representation for a data type instance is its name.
        [[nodiscard]] std::string toString() const override { return name; }

        // Check if this data type is generic (has type parameters)
        [[nodiscard]] bool is_generic() const { return !type_params.empty(); }
    };


    // Represents the enum type itself, e.g., `WebEvent`.
    // It acts as a namespace for its variants.
    struct EnumType : Type {
        const std::string name;
        // The map now stores the CONSTRUCTOR SIGNATURE for each variant.
        std::map<std::string, std::shared_ptr<FunctionType>> variants;

        explicit EnumType(std::string name)
            : Type(TypeKind::ENUM), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return name; }
    };

    // --- FIXED-SIZE ARRAY TYPE (for foreign data) ---
    // Represents i8[256], u8[16], etc. — maps to inline C arrays.
    struct FixedArrayType : Type {
        std::shared_ptr<Type> element_type;
        int size;

        FixedArrayType(std::shared_ptr<Type> elem, int n)
            : Type(TypeKind::FIXED_ARRAY), element_type(std::move(elem)), size(n) {}

        [[nodiscard]] std::string toString() const override {
            return element_type->toString() + "[" + std::to_string(size) + "]";
        }
    };

    // FFI pointer type (e.g., *i8, *void, **char) — raw C pointer, stored as i64
    struct PointerType : Type {
        std::shared_ptr<Type> pointee_type;
        int depth; // 1 for *, 2 for **

        PointerType(std::shared_ptr<Type> pointee, int d)
            : Type(TypeKind::POINTER), pointee_type(std::move(pointee)), depth(d) {}

        [[nodiscard]] std::string toString() const override {
            return std::string(depth, '*') + pointee_type->toString();
        }
    };

    // C void type — only valid as *void (void*) or as a function return type
    struct VoidType : Type {
        VoidType() : Type(TypeKind::VOID) {}
        [[nodiscard]] std::string toString() const override { return "void"; }
    };

    // --- GENERIC TYPE SYSTEM ---

    // Represents a declared type parameter (e.g., T in `data Box<T>`)
    // This is a placeholder that gets substituted when the generic is instantiated.
    struct TypeParameterType : Type {
        const std::string name; // e.g., "T", "K", "V"

        explicit TypeParameterType(std::string name)
            : Type(TypeKind::TYPE_PARAM), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return name; }
    };

    // Represents a concrete instantiation of a generic type (e.g., Box<i64>, Pair<string, i64>)
    // Stores a reference to the base generic type and the substitution map.
    struct GenericInstanceType : Type {
        // The base generic type (e.g., DataType for Box, ClassType for a generic class)
        std::shared_ptr<Type> base_type;
        // Maps type parameter names to their concrete types (e.g., "T" -> i64)
        std::map<std::string, std::shared_ptr<Type>> type_args;

        GenericInstanceType(std::shared_ptr<Type> base,
                           std::map<std::string, std::shared_ptr<Type>> args)
            : Type(TypeKind::GENERIC_INSTANCE),
              base_type(std::move(base)),
              type_args(std::move(args)) {}

        [[nodiscard]] std::string toString() const override {
            std::stringstream ss;
            // Get the base type name
            ss << base_type->toString() << "<";
            bool first = true;
            for (const auto& [name, type] : type_args) {
                if (!first) ss << ", ";
                ss << type->toString();
                first = false;
            }
            ss << ">";
            return ss.str();
        }

        // Substitute a type parameter with its concrete type.
        // If the type is a TypeParameterType, look it up in type_args.
        // Otherwise return the type unchanged.
        [[nodiscard]] std::shared_ptr<Type> substitute(const std::shared_ptr<Type>& type) const {
            if (!type) return type;
            if (type->kind == TypeKind::TYPE_PARAM) {
                auto* tp = dynamic_cast<const TypeParameterType*>(type.get());
                auto it = type_args.find(tp->name);
                if (it != type_args.end()) return it->second;
            }
            return type;
        }
    };

} // namespace angara
