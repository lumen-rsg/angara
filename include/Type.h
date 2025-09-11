#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <sstream>
#include <Token.h>

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
        ERROR // A special type to prevent cascading error messages
    };


    struct Type;

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

        const std::string name;
        std::shared_ptr<ClassType> superclass = nullptr;
        std::map<std::string, MemberInfo> fields;
        std::map<std::string, MemberInfo> methods;

        explicit ClassType(std::string name)
                : Type(TypeKind::CLASS), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return name; }

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
        ExceptionType() : Type(TypeKind::EXCEPTION) {}
        std::string toString() const override { return "Exception"; }
    };

} // namespace angara