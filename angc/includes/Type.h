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
        REF,     // v5: non-owning reference (ref<T>)
        FUTURE,  // LIB-4: asynchronous computation result (Future<T>)
        VOID,    // C void type (only valid in FFI pointer context or as return type)
        TRAIT_OBJECT, // TS-1: a value viewed through a trait/contract interface (erased impl)
        TUPLE,   // LANG-10: heterogeneous fixed-arity tuple (e.g., (i64, string))
        RAW_ARRAY, // SIMD-1: unboxed dynamic array (e.g., f64[], i64[]) — contiguous raw storage
        VECTOR,   // SIMD-5: fixed-size vector type (e.g., vec4<f32>, vec3<f64>)
        ERROR // A special type to prevent cascading error messages
    };


    struct Type;
    struct EnumType;
    struct EnumVariantType;
    struct ClassType; // Needed for DataType and others
    struct TraitType; // TS-2: needed for type_param_bounds on ClassType/DataType
    struct ContractType; // TS-1: needed for signed_contracts on ClassType
    struct FuncStmt; // TS-1/Phase D: default_bodies on TraitType

    // TS-4: structural type identity. Nominal types (CLASS/DATA/ENUM/TRAIT/
    // CONTRACT) compare by canonical pointer identity (each declaration is
    // constructed once and aliased across modules); compound types recurse on
    // their constituent shared_ptrs. Forward-declared so FunctionType::equals
    // can use it; defined at the end of this file.
    bool sameType(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b);
    // TS-4: qualified display name ("Foo" or "mod::Foo") for collision diagnostics.
    std::string qualifiedName(const Type& t);
    // TS-2: substitute TypeParameterType names with concrete types from `args`,
    // recursing into compound types at any depth. Used to specialize a generic
    // function signature at a call site from inferred type arguments.
    std::shared_ptr<Type> substituteTypeArgs(
        const std::shared_ptr<Type>& type,
        const std::map<std::string, std::shared_ptr<Type>>& args);

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
        bool is_owned = false; // @own annotation for zero-copy string adoption
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

        // For foreign functions: indices of *void params that are userdata slots
        // for callback FUNCTION params. These are auto-filled by the FFI layer
        // and hidden from the user at call sites.
        std::vector<size_t> userdata_param_indices;
        // RT-1: @on_throw value — set on a callback param's FunctionType in
        // defineFunctionHeader so the FFI trampoline knows what C value to
        // return if the callback throws (instead of longjmping across C frames).
        std::optional<int64_t> on_throw_value;

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

            // 2. Check foreign / intrinsic flags
            if (this->is_foreign != other.is_foreign) return false;
            if (this->is_intrinsic != other.is_intrinsic) return false;

            // 3. Check @on_throw value
            if (this->on_throw_value != other.on_throw_value) return false;

            // 4. Check arity
            if (this->param_types.size() != other.param_types.size()) {
                return false;
            }

            // 5. Check each parameter's type (TS-4: structural identity, not string)
            for (size_t i = 0; i < this->param_types.size(); ++i) {
                if (!sameType(this->param_types[i], other.param_types[i])) {
                    return false;
                }
            }

            // 6. Check the return type
            if (!sameType(this->return_type, other.return_type)) {
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
        // TS-2: resolved bounds, e.g. "K" -> TraitType("Hashable") for `<K: Hashable>`.
        std::map<std::string, std::shared_ptr<TraitType>> type_param_bounds;

        // TS-1: the interfaces this class conforms to (resolved at header time
        // from `uses`/`signs`). Retained on the semantic type so conformance is
        // queryable anywhere a ClassType is in hand — precise `is Trait`, trait-
        // object vtable construction, and trait/contract assignability.
        std::vector<std::shared_ptr<TraitType>> adopted_traits;
        std::vector<std::shared_ptr<ContractType>> signed_contracts;

        // TS-4: the declaring module's name. Empty for built-ins. Set at every
        // construction site; preserved across module imports (the shared object
        // is aliased, so this always reflects the original home module). Used for
        // qualified diagnostics ("app::Foo" vs "lib::Foo").
        std::string home_module;

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
        // TS-1/Phase D: default method bodies. A trait method may declare a
        // default implementation (parsed as a FuncStmt with a body); stored
        // here so a class that doesn't override it gets the default in its
        // vtable slot. Empty entry = no default (signature-only).
        std::map<std::string, std::shared_ptr<const FuncStmt>> default_bodies;
        std::string home_module;  // TS-4: declaring module (for qualified diagnostics)

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
               name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
               name == "char";  // LANG-4: char is a 32-bit unsigned int subtype (C/Java model)
    }

    // LANG-4: true for the primitive `char` type. char participates in integer
    // arithmetic/comparison (it's a numeric, code-point-typed integer) but
    // renders as a glyph — codegen consults this to route char-typed values
    // through __ang_char_to_string instead of __ang_to_string.
    inline bool isChar(const std::shared_ptr<Type>& type) {
        if (!type || type->kind != TypeKind::PRIMITIVE) return false;
        return type->toString() == "char";
    }

    inline bool isUnsignedInteger(const std::shared_ptr<Type>& type) {
        if (!type || type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        // char is unsigned (code points are non-negative; range 0..0x10FFFF in
        // principle, modeled as a 32-bit unsigned for int-conv classification).
        return name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
               name == "char";
    }

    // TS-3: classify an integer-to-integer conversion by whether it preserves
    // the value. `Widen` never loses information (e.g. u8 -> i64). `Narrow` may
    // (e.g. i64 -> u8, u64 -> i8). `Identical` means same width and sign.
    enum class IntConv { Identical, Widen, Narrow };

    // Bit width (8/16/32/64) of an integer primitive type, or 0 if not integer.
    inline int intWidth(const std::shared_ptr<Type>& type) {
        if (!isInteger(type)) return 0;
        const auto& name = type->toString();
        if (name == "i8"  || name == "u8")  return 8;
        if (name == "i16" || name == "u16") return 16;
        if (name == "i32" || name == "u32" || name == "char") return 32;  // LANG-4: char is 32-bit
        return 64;  // i64 / u64 / int
    }

    inline IntConv classifyIntConv(const std::shared_ptr<Type>& target,
                                   const std::shared_ptr<Type>& source) {
        if (!target || !source) return IntConv::Narrow;
        if (target->toString() == source->toString()) return IntConv::Identical;
        int tw = intWidth(target), sw = intWidth(source);
        bool t_unsigned = isUnsignedInteger(target);
        bool s_unsigned = isUnsignedInteger(source);
        // A conversion widens iff the target can represent every value of the
        // source: strictly more bits, and (target signed) requires the source
        // be unsigned or at least as wide minus the sign bit.
        bool widen = (tw > sw) || (tw == sw && !(s_unsigned && !t_unsigned));
        // Equal width with differing signedness (e.g. u32 -> i32) narrows: the
        // top half of the unsigned range is not representable.
        if (tw == sw && t_unsigned != s_unsigned) return IntConv::Narrow;
        return widen ? IntConv::Widen : IntConv::Narrow;
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
        std::string home_module;  // TS-4: declaring module (for qualified diagnostics)

        explicit ContractType(std::string name)
            : Type(TypeKind::CONTRACT), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return "contract<" + name + ">"; }
    };

    // TS-1: the static type of a value viewed through a trait/contract interface.
    // Like InstanceType wraps a ClassType, a TraitObjectType wraps the interface
    // a concrete value is being accessed through. The interface is the TRAIT or
    // CONTRACT; impl_type is the concrete (INSTANCE/DATA/...) type underneath
    // (kept for diagnostics; dispatch and field access go via the interface and
    // the runtime trait object's embedded receiver).
    struct TraitObjectType : Type {
        const std::shared_ptr<Type> interface_type;  // TraitType or ContractType
        const std::shared_ptr<Type> impl_type;       // concrete type of the value (may be null)
        TraitObjectType(std::shared_ptr<Type> iface, std::shared_ptr<Type> impl)
            : Type(TypeKind::TRAIT_OBJECT),
              interface_type(std::move(iface)), impl_type(std::move(impl)) {}
        [[nodiscard]] std::string toString() const override {
            return interface_type ? interface_type->toString() : "trait_object";
        }
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

    // v5: Non-owning reference type (ref<T>). The holder can read T's fields
    // but does NOT own it — no drop needed. The Chaperone verifies the
    // referent outlives the ref. At runtime, it's the same boxed AngaraObject
    // (a pointer); ref<T> is a compile-time-only distinction.
    struct RefType : Type {
        const std::shared_ptr<Type> inner_type;
        explicit RefType(std::shared_ptr<Type> inner)
                : Type(TypeKind::REF), inner_type(std::move(inner)) {}

        [[nodiscard]] std::string toString() const override {
            return "ref<" + inner_type->toString() + ">";
        }
    };

    // LIB-4: Future<T> — an asynchronous computation that will eventually
    // produce a value of type T. Futures are owned (heap-allocated) types
    // tracked by the Chaperone. They must be explicitly awaited (which
    // consumes them) or dropped (which cancels the computation).
    struct FutureType : Type {
        const std::shared_ptr<Type> inner_type;
        explicit FutureType(std::shared_ptr<Type> inner)
                : Type(TypeKind::FUTURE), inner_type(std::move(inner)) {}

        [[nodiscard]] std::string toString() const override {
            return "Future<" + inner_type->toString() + ">";
        }
    };

    // LANG-10: heterogeneous fixed-arity tuple type (e.g., (i64, string)).
    // At runtime represented as an AngaraList — the type system enforces
    // fixed arity and positional (not uniform) element types.
    struct TupleType : Type {
        const std::vector<std::shared_ptr<Type>> element_types;
        explicit TupleType(std::vector<std::shared_ptr<Type>> types)
                : Type(TypeKind::TUPLE), element_types(std::move(types)) {}

        [[nodiscard]] std::string toString() const override {
            std::stringstream ss;
            ss << "(";
            for (size_t i = 0; i < element_types.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << element_types[i]->toString();
            }
            ss << ")";
            return ss.str();
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
        bool is_union = false;  // true for "foreign union Name { ... }"

        // --- GENERIC SUPPORT ---
        // Type parameter names for this generic data type (e.g., {"T"} for Box<T>)
        std::vector<std::string> type_params;
        // TS-2: resolved bounds, e.g. "K" -> TraitType("Hashable") for `<K: Hashable>`.
        std::map<std::string, std::shared_ptr<TraitType>> type_param_bounds;
        std::string home_module;  // TS-4: declaring module (for qualified diagnostics)

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
        std::string home_module;  // TS-4: declaring module (for qualified diagnostics)

        // --- GENERIC SUPPORT (LANG-8) ---
        // Type parameter names for this generic enum (e.g., {"T","E"} for Result<T,E>)
        std::vector<std::string> type_params;
        // Resolved bounds, e.g. "T" -> TraitType("Hashable") for `<T: Hashable>`.
        std::map<std::string, std::shared_ptr<TraitType>> type_param_bounds;

        explicit EnumType(std::string name)
            : Type(TypeKind::ENUM), name(std::move(name)) {}

        [[nodiscard]] std::string toString() const override { return name; }

        // Check if this enum is generic (has type parameters)
        [[nodiscard]] bool is_generic() const { return !type_params.empty(); }
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

    // --- UNBOXED DYNAMIC ARRAY TYPE (SIMD-1) ---
    // Represents f64[], i64[], etc. — contiguous raw-value storage on the heap.
    // Unlike list<T> (boxed AngaraObject[] elements), a raw array stores elements
    // as raw primitives (double, int64_t, etc.) directly. Subscript access lowers
    // to GEP+load/store — no function call, no tag dispatch — enabling LLVM
    // auto-vectorization. The runtime representation is AngaraRawArray, a sibling
    // of AngaraList with a typed element buffer instead of AngaraObject[].
    struct RawArrayType : Type {
        const std::shared_ptr<Type> element_type;
        explicit RawArrayType(std::shared_ptr<Type> elem)
            : Type(TypeKind::RAW_ARRAY), element_type(std::move(elem)) {}
        [[nodiscard]] std::string toString() const override {
            return element_type->toString() + "[]";
        }
    };

    // --- VECTOR TYPE (SIMD-5) ---
    // Represents a fixed-size SIMD vector type (e.g., vec4<f32>, vec3<f64>, vec4<i32>).
    // At runtime, vectors are heap-allocated AngaraVector objects (OBJ_VECTOR) with
    // the LLVM vector value stored inline. Element-wise arithmetic maps to LLVM SIMD
    // instructions (fadd, fmul, etc. on <N x float>).
    struct VectorType : Type {
        const std::shared_ptr<Type> element_type;  // primitive type: f32, f64, i32, i64, u32, u64
        const int size;  // number of elements: 2, 3, 4, or 8
        VectorType(std::shared_ptr<Type> elem, int sz)
            : Type(TypeKind::VECTOR), element_type(std::move(elem)), size(sz) {}
        [[nodiscard]] std::string toString() const override {
            return "vec" + std::to_string(size) + "<" + element_type->toString() + ">";
        }
    };

    // FFI pointer type (e.g., *i8, *void, **char) — raw C pointer, stored as i64
    struct PointerType : Type {
        std::shared_ptr<Type> pointee_type;
        int depth; // 1 for *, 2 for **
        bool byval = false; // ^Type — struct passed/returned by value (FFI only)

        PointerType(std::shared_ptr<Type> pointee, int d)
            : Type(TypeKind::POINTER), pointee_type(std::move(pointee)), depth(d) {}

        [[nodiscard]] std::string toString() const override {
            if (byval) return "^" + pointee_type->toString();
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

            // Collect type-param names in declaration order from the base type
            std::vector<std::string> param_order;
            if (auto dt = std::dynamic_pointer_cast<DataType>(base_type))
                param_order = dt->type_params;
            else if (auto ct = std::dynamic_pointer_cast<ClassType>(base_type))
                param_order = ct->type_params;
            else if (auto et = std::dynamic_pointer_cast<EnumType>(base_type))
                param_order = et->type_params;

            bool first = true;
            // Emit in declaration order first
            for (const auto& pname : param_order) {
                auto it = type_args.find(pname);
                if (it != type_args.end()) {
                    if (!first) ss << ", ";
                    ss << it->second->toString();
                    first = false;
                }
            }
            // Emit any remaining args not in param_order (shouldn't normally happen)
            for (const auto& [name, type] : type_args) {
                bool in_order = false;
                for (const auto& pn : param_order) { if (pn == name) { in_order = true; break; } }
                if (!in_order) {
                    if (!first) ss << ", ";
                    ss << type->toString();
                    first = false;
                }
            }
            ss << ">";
            return ss.str();
        }

        // TS-2: substitute type parameters with their concrete types, recursing
        // into compound types at any depth. (Previously this was one level — a
        // field of type `list<T>` or `Box<T>` leaked an unbound TypeParameter.)
        [[nodiscard]] std::shared_ptr<Type> substitute(const std::shared_ptr<Type>& type) const {
            if (!type) return type;

            // Bare type parameter: replace if bound, else unchanged.
            if (type->kind == TypeKind::TYPE_PARAM) {
                auto* tp = dynamic_cast<const TypeParameterType*>(type.get());
                auto it = type_args.find(tp->name);
                if (it != type_args.end()) return it->second;
                return type;
            }

            // Compound types: rebuild with substituted constituents.
            switch (type->kind) {
                case TypeKind::LIST: {
                    auto l = std::dynamic_pointer_cast<ListType>(type);
                    return std::make_shared<ListType>(substitute(l->element_type));
                }
                case TypeKind::OPTIONAL: {
                    auto o = std::dynamic_pointer_cast<OptionalType>(type);
                    return std::make_shared<OptionalType>(substitute(o->wrapped_type));
                }
                case TypeKind::REF: {
                    auto r = std::dynamic_pointer_cast<RefType>(type);
                    return std::make_shared<RefType>(substitute(r->inner_type));
                }
                case TypeKind::FUTURE: {
                    auto f = std::dynamic_pointer_cast<FutureType>(type);
                    return std::make_shared<FutureType>(substitute(f->inner_type));
                }
                case TypeKind::TUPLE: {
                    auto t = std::dynamic_pointer_cast<TupleType>(type);
                    std::vector<std::shared_ptr<Type>> ne;
                    ne.reserve(t->element_types.size());
                    for (const auto& e : t->element_types) ne.push_back(substitute(e));
                    return std::make_shared<TupleType>(std::move(ne));
                }
                case TypeKind::GENERIC_INSTANCE: {
                    auto g = std::dynamic_pointer_cast<GenericInstanceType>(type);
                    std::map<std::string, std::shared_ptr<Type>> new_args;
                    for (const auto& [k, v] : g->type_args) new_args[k] = substitute(v);
                    return std::make_shared<GenericInstanceType>(g->base_type, std::move(new_args));
                }
                case TypeKind::RECORD: {
                    auto r = std::dynamic_pointer_cast<RecordType>(type);
                    std::map<std::string, std::shared_ptr<Type>> new_fields;
                    for (const auto& [k, v] : r->fields) new_fields[k] = substitute(v);
                    return std::make_shared<RecordType>(std::move(new_fields));
                }
                case TypeKind::FUNCTION: {
                    auto f = std::dynamic_pointer_cast<FunctionType>(type);
                    std::vector<std::shared_ptr<Type>> new_params;
                    new_params.reserve(f->param_types.size());
                    for (const auto& p : f->param_types) new_params.push_back(substitute(p));
                    auto new_ret = substitute(f->return_type);
                    auto nf = std::make_shared<FunctionType>(new_params, new_ret, f->is_variadic);
                    nf->is_foreign = f->is_foreign;
                    nf->is_intrinsic = f->is_intrinsic;
                    nf->on_throw_value = f->on_throw_value;  // RT-1
                    return nf;
                }
                case TypeKind::POINTER: {
                    auto p = std::dynamic_pointer_cast<PointerType>(type);
                    auto np = std::make_shared<PointerType>(substitute(p->pointee_type), p->depth);
                    np->byval = p->byval;
                    return np;
                }
                case TypeKind::FIXED_ARRAY: {
                    auto a = std::dynamic_pointer_cast<FixedArrayType>(type);
                    return std::make_shared<FixedArrayType>(substitute(a->element_type), a->size);
                }
                case TypeKind::RAW_ARRAY: {
                    auto a = std::dynamic_pointer_cast<RawArrayType>(type);
                    return std::make_shared<RawArrayType>(substitute(a->element_type));
                }
                case TypeKind::VECTOR: {
                    auto v = std::dynamic_pointer_cast<VectorType>(type);
                    return std::make_shared<VectorType>(substitute(v->element_type), v->size);
                }
                default:
                    // Primitives, nominal types, etc. carry no type params.
                    return type;
            }
        }
    };

    // TS-4: structural type identity. See the forward declaration above.
    inline bool sameType(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
        if (a.get() == b.get()) return true;          // pointer identity (covers null==null, singletons, canonical nominal types)
        if (!a || !b) return false;
        if (a->kind != b->kind) return false;

        switch (a->kind) {
            // Name-only / singleton kinds: toString() equality is sound (no
            // cross-module concern — these are language built-ins).
            case TypeKind::PRIMITIVE:
            case TypeKind::NIL:
            case TypeKind::VOID:
            case TypeKind::ANY:
            case TypeKind::TYPE_PARAM:
            case TypeKind::ERROR:
            case TypeKind::THREAD:
            case TypeKind::MUTEX:
            case TypeKind::MODULE:
            case TypeKind::EXCEPTION:
                return a->toString() == b->toString();

            // Nominal kinds: identity is the canonical declaration object.
            // Each declaration is constructed once and aliased across modules,
            // so pointer identity is the correct nominal-identity test. (Pointer
            // equality was already handled by the fast-path above, so reaching
            // here means the pointers differ → distinct declarations → unequal.)
            case TypeKind::CLASS:
            case TypeKind::DATA:
            case TypeKind::ENUM:
            case TypeKind::TRAIT:
            case TypeKind::CONTRACT:
                return false;

            case TypeKind::INSTANCE: {
                auto la = std::dynamic_pointer_cast<InstanceType>(a);
                auto lb = std::dynamic_pointer_cast<InstanceType>(b);
                // InstanceType is minted per-use; compare the canonical class.
                return la && lb && sameType(la->class_type, lb->class_type);
            }

            case TypeKind::TRAIT_OBJECT: {
                // TS-1: two trait-object views are the same iff they view
                // through the same interface (impl_type is informational).
                auto la = std::dynamic_pointer_cast<TraitObjectType>(a);
                auto lb = std::dynamic_pointer_cast<TraitObjectType>(b);
                return la && lb && sameType(la->interface_type, lb->interface_type);
            }

            case TypeKind::GENERIC_INSTANCE: {
                auto la = std::dynamic_pointer_cast<GenericInstanceType>(a);
                auto lb = std::dynamic_pointer_cast<GenericInstanceType>(b);
                if (!la || !lb) return false;
                if (!sameType(la->base_type, lb->base_type)) return false;
                if (la->type_args.size() != lb->type_args.size()) return false;
                for (const auto& [k, va] : la->type_args) {
                    auto it = lb->type_args.find(k);
                    if (it == lb->type_args.end()) return false;
                    if (!sameType(va, it->second)) return false;
                }
                return true;
            }

            case TypeKind::LIST: {
                auto la = std::dynamic_pointer_cast<ListType>(a);
                auto lb = std::dynamic_pointer_cast<ListType>(b);
                return la && lb && sameType(la->element_type, lb->element_type);
            }

            case TypeKind::OPTIONAL: {
                auto la = std::dynamic_pointer_cast<OptionalType>(a);
                auto lb = std::dynamic_pointer_cast<OptionalType>(b);
                return la && lb && sameType(la->wrapped_type, lb->wrapped_type);
            }

            case TypeKind::REF: {
                auto la = std::dynamic_pointer_cast<RefType>(a);
                auto lb = std::dynamic_pointer_cast<RefType>(b);
                return la && lb && sameType(la->inner_type, lb->inner_type);
            }

            case TypeKind::FUTURE: {
                auto la = std::dynamic_pointer_cast<FutureType>(a);
                auto lb = std::dynamic_pointer_cast<FutureType>(b);
                return la && lb && sameType(la->inner_type, lb->inner_type);
            }

            case TypeKind::TUPLE: {
                auto la = std::dynamic_pointer_cast<TupleType>(a);
                auto lb = std::dynamic_pointer_cast<TupleType>(b);
                if (!la || !lb) return false;
                if (la->element_types.size() != lb->element_types.size()) return false;
                for (size_t i = 0; i < la->element_types.size(); ++i) {
                    if (!sameType(la->element_types[i], lb->element_types[i])) return false;
                }
                return true;
            }

            case TypeKind::FUNCTION: {
                auto la = std::dynamic_pointer_cast<FunctionType>(a);
                auto lb = std::dynamic_pointer_cast<FunctionType>(b);
                if (!la || !lb) return false;
                return la->equals(*lb);
            }

            case TypeKind::RECORD: {
                auto la = std::dynamic_pointer_cast<RecordType>(a);
                auto lb = std::dynamic_pointer_cast<RecordType>(b);
                if (!la || !lb) return false;
                if (la->fields.size() != lb->fields.size()) return false;
                for (const auto& [k, va] : la->fields) {
                    auto it = lb->fields.find(k);
                    if (it == lb->fields.end()) return false;
                    if (!sameType(va, it->second)) return false;
                }
                return true;
            }

            case TypeKind::POINTER: {
                auto la = std::dynamic_pointer_cast<PointerType>(a);
                auto lb = std::dynamic_pointer_cast<PointerType>(b);
                return la && lb && la->depth == lb->depth && la->byval == lb->byval &&
                       sameType(la->pointee_type, lb->pointee_type);
            }

            case TypeKind::FIXED_ARRAY: {
                auto la = std::dynamic_pointer_cast<FixedArrayType>(a);
                auto lb = std::dynamic_pointer_cast<FixedArrayType>(b);
                return la && lb && la->size == lb->size && sameType(la->element_type, lb->element_type);
            }
            case TypeKind::RAW_ARRAY: {
                auto la = std::dynamic_pointer_cast<RawArrayType>(a);
                auto lb = std::dynamic_pointer_cast<RawArrayType>(b);
                return la && lb && sameType(la->element_type, lb->element_type);
            }
            case TypeKind::VECTOR: {
                auto la = std::dynamic_pointer_cast<VectorType>(a);
                auto lb = std::dynamic_pointer_cast<VectorType>(b);
                return la && lb && la->size == lb->size &&
                       sameType(la->element_type, lb->element_type);
            }
        }
        return false;
    }

    // TS-4: qualified display name. Returns the bare name for built-in-like
    // types (home_module empty), or "module::Name" when a nominal type carries
    // a home module. Used in diagnostics where two same-named types collide.
    inline std::string qualifiedName(const Type& t) {
        std::string home;
        std::string base = t.toString();
        switch (t.kind) {
            case TypeKind::CLASS:  home = static_cast<const ClassType&>(t).home_module; break;
            case TypeKind::DATA:   home = static_cast<const DataType&>(t).home_module; break;
            case TypeKind::ENUM:   home = static_cast<const EnumType&>(t).home_module; break;
            case TypeKind::TRAIT:  home = static_cast<const TraitType&>(t).home_module; break;
            case TypeKind::CONTRACT: home = static_cast<const ContractType&>(t).home_module; break;
            default: return base;
        }
        return home.empty() ? base : (home + "::" + base);
    }

    // TS-4: name to show for `t` in a mismatch diagnostic against `other`.
    // Shows the qualified name (e.g. "lib::Foo") only when the two types share
    // the same bare name — i.e. an actual cross-module collision worth
    // disambiguating. Otherwise the bare name (cleaner for the common case).
    inline std::string displayType(const Type& t, const Type& other) {
        if (t.toString() == other.toString()) {
            return qualifiedName(t);
        }
        return t.toString();
    }

    // TS-2: substitute TypeParameterType names with concrete types, recursing.
    inline std::shared_ptr<Type> substituteTypeArgs(
        const std::shared_ptr<Type>& type,
        const std::map<std::string, std::shared_ptr<Type>>& args
    ) {
        if (!type || args.empty()) return type;

        if (type->kind == TypeKind::TYPE_PARAM) {
            auto* tp = dynamic_cast<const TypeParameterType*>(type.get());
            auto it = args.find(tp->name);
            if (it != args.end()) return it->second;
            return type;
        }

        switch (type->kind) {
            case TypeKind::LIST:
                return std::make_shared<ListType>(
                    substituteTypeArgs(std::dynamic_pointer_cast<ListType>(type)->element_type, args));
            case TypeKind::OPTIONAL:
                return std::make_shared<OptionalType>(
                    substituteTypeArgs(std::dynamic_pointer_cast<OptionalType>(type)->wrapped_type, args));
            case TypeKind::REF:
                return std::make_shared<RefType>(
                    substituteTypeArgs(std::dynamic_pointer_cast<RefType>(type)->inner_type, args));
            case TypeKind::FUTURE:
                return std::make_shared<FutureType>(
                    substituteTypeArgs(std::dynamic_pointer_cast<FutureType>(type)->inner_type, args));
            case TypeKind::TUPLE: {
                auto t = std::dynamic_pointer_cast<TupleType>(type);
                std::vector<std::shared_ptr<Type>> ne;
                ne.reserve(t->element_types.size());
                for (const auto& e : t->element_types) ne.push_back(substituteTypeArgs(e, args));
                return std::make_shared<TupleType>(std::move(ne));
            }
            case TypeKind::GENERIC_INSTANCE: {
                auto g = std::dynamic_pointer_cast<GenericInstanceType>(type);
                std::map<std::string, std::shared_ptr<Type>> na;
                for (const auto& [k, v] : g->type_args) na[k] = substituteTypeArgs(v, args);
                return std::make_shared<GenericInstanceType>(g->base_type, std::move(na));
            }
            case TypeKind::RECORD: {
                auto r = std::dynamic_pointer_cast<RecordType>(type);
                std::map<std::string, std::shared_ptr<Type>> nf;
                for (const auto& [k, v] : r->fields) nf[k] = substituteTypeArgs(v, args);
                return std::make_shared<RecordType>(std::move(nf));
            }
            case TypeKind::FUNCTION: {
                auto f = std::dynamic_pointer_cast<FunctionType>(type);
                std::vector<std::shared_ptr<Type>> np;
                np.reserve(f->param_types.size());
                for (const auto& p : f->param_types) np.push_back(substituteTypeArgs(p, args));
                auto nr = substituteTypeArgs(f->return_type, args);
                auto nf = std::make_shared<FunctionType>(np, nr, f->is_variadic);
                nf->is_foreign = f->is_foreign;
                nf->is_intrinsic = f->is_intrinsic;
                return nf;
            }
            case TypeKind::POINTER: {
                auto p = std::dynamic_pointer_cast<PointerType>(type);
                auto np = std::make_shared<PointerType>(substituteTypeArgs(p->pointee_type, args), p->depth);
                np->byval = p->byval;
                return np;
            }
            case TypeKind::FIXED_ARRAY: {
                auto a = std::dynamic_pointer_cast<FixedArrayType>(type);
                return std::make_shared<FixedArrayType>(substituteTypeArgs(a->element_type, args), a->size);
            }
            case TypeKind::RAW_ARRAY: {
                auto a = std::dynamic_pointer_cast<RawArrayType>(type);
                return std::make_shared<RawArrayType>(substituteTypeArgs(a->element_type, args));
            }
            case TypeKind::VECTOR: {
                auto v = std::dynamic_pointer_cast<VectorType>(type);
                return std::make_shared<VectorType>(substituteTypeArgs(v->element_type, args), v->size);
            }
            default:
                return type;
        }
    }

} // namespace angara
