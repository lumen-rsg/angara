#include "TypeChecker.h"
namespace angara {

    bool TypeChecker::check_structural_match(
            const std::shared_ptr<DataType>& data_type,
            const std::shared_ptr<RecordType>& record_type
    ) {
        for (const auto& [field_name, data_field_info] : data_type->fields) {
            auto record_field_it = record_type->fields.find(field_name);
            if (record_field_it == record_type->fields.end()) {
                return false;
            }

            if (!check_type_compatibility(data_field_info.type, record_field_it->second)) {
                return false;
            }
        }
        return true;
    }

    // TS-3: returns true if `lit` is an integer literal whose value fits in the
    // integer `target` type, making the narrowing conversion safe. `source` is
    // the literal's own (inferred) type, used to confirm it's an integer literal.
    // Returns false if there's no literal or it doesn't fit.
    bool TypeChecker::narrowing_literal_fits(
            const std::shared_ptr<Type>& target,
            const std::shared_ptr<Type>& source,
            const Literal* lit
    ) {
        if (!lit) return false;
        if (!isInteger(source)) return false;
        // Parse the literal's lexeme (always non-negative in source — a leading
        // '-' is a separate Unary node). Reject hex/other prefixes conservatively.
        const std::string& lex = lit->token.lexeme;
        if (lex.empty()) return false;
        for (char c : lex) { if (c < '0' || c > '9') return false; }
        unsigned long long value = 0;
        try {
            value = std::stoull(lex);
        } catch (...) { return false; }

        int w = intWidth(target);
        bool uns = isUnsignedInteger(target);
        // Max representable value for the target.
        unsigned long long max_val;
        unsigned long long min_val = 0;
        if (uns) {
            max_val = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        } else {
            // signed: max = 2^(w-1) - 1, min = -(2^(w-1)). Non-negative literal
            // only needs to clear the signed max.
            unsigned long long half = (w >= 64) ? (1ULL << 63) : (1ULL << (w - 1));
            max_val = half - 1;
            min_val = half;  // magnitude of the most-negative value (informational)
        }
        (void)min_val;
        return value <= max_val;
    }


bool TypeChecker::check_type_compatibility(
            const std::shared_ptr<Type>& expected,
            const std::shared_ptr<Type>& actual,
            const Literal* narrowing_literal
) {
    // TS-4: structural identity. Nominal types compare by canonical pointer
    // identity (so two same-named types from different modules are distinct);
    // compound types recurse. Supersedes the old toString()== fast-path.
    if (sameType(expected, actual)) return true;

    if (expected->kind == TypeKind::ANY || actual->kind == TypeKind::ANY) return true;

    // TS-2: type-parameter compatibility. Inside a generic body, a type
    // parameter is only compatible with itself (same name). A bare TYPE_PARAM is
    // NOT compatible with a concrete type here — that case is handled at generic
    // *call sites* by inferring the binding and substituting before checking, so
    // by the time we reach this predicate with a concrete actual, the expected
    // has already been substituted to a concrete type. (Previously TYPE_PARAM
    // was compatible with ANYTHING, leaving generic bodies unchecked.)
    if (expected->kind == TypeKind::TYPE_PARAM || actual->kind == TypeKind::TYPE_PARAM) {
        if (expected->kind == TypeKind::TYPE_PARAM && actual->kind == TypeKind::TYPE_PARAM) {
            auto* ep = dynamic_cast<const TypeParameterType*>(expected.get());
            auto* ap = dynamic_cast<const TypeParameterType*>(actual.get());
            return ep->name == ap->name;
        }
        return false;
    }

    // TS-1: trait/contract object assignability. A concrete instance (INSTANCE)
    // may flow into a slot typed as a TRAIT or CONTRACT it adopts (upcast), and
    // into a TRAIT_OBJECT of such an interface. A TRAIT_OBJECT is assignable to
    // its own interface (TRAIT/CONTRACT) and to a TRAIT_OBJECT of the same
    // interface. The reverse (interface → concrete) requires an explicit `as`.
    if (expected->kind == TypeKind::TRAIT || expected->kind == TypeKind::CONTRACT) {
        if (actual->kind == TypeKind::INSTANCE || actual->kind == TypeKind::TRAIT_OBJECT) {
            return adoptsInterface(actual, expected);
        }
    }
    if (expected->kind == TypeKind::TRAIT_OBJECT) {
        auto exp_obj = std::dynamic_pointer_cast<TraitObjectType>(expected);
        // A concrete instance upcast into a trait-object slot.
        if (actual->kind == TypeKind::INSTANCE) {
            return exp_obj && adoptsInterface(actual, exp_obj->interface_type);
        }
        // A trait object viewed through a compatible interface.
        if (actual->kind == TypeKind::TRAIT_OBJECT) {
            auto act_obj = std::dynamic_pointer_cast<TraitObjectType>(actual);
            return exp_obj && act_obj &&
                   adoptsInterface(act_obj->impl_type, exp_obj->interface_type);
        }
    }

    // TS-3: integer conversions. Widening and same-width-same-sign are always
    // allowed. Narrowing (i64->u8, u64->i8, ...) is rejected in safe code unless
    // the source is an integer literal whose value fits the target range, or we
    // are inside an @unsafe block. @unsafe is the universal opt-out.
    if (isInteger(expected) && isInteger(actual)) {
        if (m_is_in_unsafe_context) {
            warning(Token(), "Integer type compatibility check bypassed by @unsafe block.", "W260");
            return true;
        }
        switch (classifyIntConv(expected, actual)) {
            case IntConv::Identical:
                return true;
            case IntConv::Widen:
                return true;
            case IntConv::Narrow:
                // In-range integer literal: allow silently (e.g. `let b as u8 = 200;`).
                if (narrowing_literal_fits(expected, actual, narrowing_literal)) {
                    return true;
                }
                return false;
        }
    }

    if (expected->kind == TypeKind::OPTIONAL) {
        auto optional_type = std::dynamic_pointer_cast<OptionalType>(expected);
        if (check_type_compatibility(optional_type->wrapped_type, actual, nullptr) || actual->kind == TypeKind::NIL) {
            return true;
        }
    }

    if (expected->kind == TypeKind::DATA && actual->kind == TypeKind::RECORD) {
        return check_structural_match(
            std::dynamic_pointer_cast<DataType>(expected),
            std::dynamic_pointer_cast<RecordType>(actual)
        );
    }

    if (expected->kind == TypeKind::RECORD && actual->kind == TypeKind::RECORD) {
        // TS-7a: a bare `record` (empty field map, the dynamic string-keyed-map
        // escape hatch) as the *expected* type accepts any record — that's the
        // idiomatic way to type "some record". But a populated record type is no
        // longer matched by an empty actual, and two populated records must agree
        // field-by-field (same names, pairwise-compatible types). Previously an
        // empty record on EITHER side matched anything. Relaxed inside @unsafe.
        if (m_is_in_unsafe_context) {
            // L16: warn when @unsafe suppresses record type mismatch
            warning(Token(), "Record type compatibility check bypassed by @unsafe block.", "W260");
            return true;
        }
        auto expected_record = std::dynamic_pointer_cast<RecordType>(expected);
        auto actual_record = std::dynamic_pointer_cast<RecordType>(actual);
        if (expected_record->fields.empty()) {
            // Dynamic-map target accepts any record value.
            return true;
        }
        if (actual_record->fields.empty()) {
            // An empty map value does not satisfy a typed record target.
            return false;
        }
        if (expected_record->fields.size() != actual_record->fields.size()) {
            return false;
        }
        for (const auto& [field_name, expected_field_type] : expected_record->fields) {
            auto it = actual_record->fields.find(field_name);
            if (it == actual_record->fields.end()) return false;
            if (!check_type_compatibility(expected_field_type, it->second, nullptr)) return false;
        }
        return true;
    }

    if (expected->kind == TypeKind::LIST && actual->kind == TypeKind::LIST) {
        auto expected_list = std::dynamic_pointer_cast<ListType>(expected);
        auto actual_list = std::dynamic_pointer_cast<ListType>(actual);
        return check_type_compatibility(expected_list->element_type, actual_list->element_type, nullptr);
    }

    // Generic instance vs generic instance: same base type, compatible type args
    if (expected->kind == TypeKind::GENERIC_INSTANCE && actual->kind == TypeKind::GENERIC_INSTANCE) {
        auto expected_gen = std::dynamic_pointer_cast<GenericInstanceType>(expected);
        auto actual_gen = std::dynamic_pointer_cast<GenericInstanceType>(actual);
        if (!sameType(expected_gen->base_type, actual_gen->base_type)) return false;
        for (const auto& [name, expected_arg] : expected_gen->type_args) {
            auto it = actual_gen->type_args.find(name);
            if (it == actual_gen->type_args.end()) return false;
            // LANG-8: allow unbound type params to match anything —
            // they'll be filled in from context (e.g., Result.Ok(42) infers T
            // but leaves E unbound; the expected type Result<i64,string> fills E).
            if (it->second->kind == TypeKind::TYPE_PARAM) continue;
            if (!check_type_compatibility(expected_arg, it->second, nullptr)) return false;
        }
        return true;
    }

    // Generic instance is compatible with its bare base data type
    if (expected->kind == TypeKind::DATA && actual->kind == TypeKind::GENERIC_INSTANCE) {
        auto expected_data = std::dynamic_pointer_cast<DataType>(expected);
        auto actual_gen = std::dynamic_pointer_cast<GenericInstanceType>(actual);
        return sameType(expected_data, actual_gen->base_type);
    }

    // LANG-8: Generic instance is compatible with its bare base enum type
    if (expected->kind == TypeKind::ENUM && actual->kind == TypeKind::GENERIC_INSTANCE) {
        auto expected_enum = std::dynamic_pointer_cast<EnumType>(expected);
        auto actual_gen = std::dynamic_pointer_cast<GenericInstanceType>(actual);
        return sameType(expected_enum, actual_gen->base_type);
    }

    // LANG-8: Bare enum actual is compatible with a generic instance expected type
    if (expected->kind == TypeKind::GENERIC_INSTANCE && actual->kind == TypeKind::ENUM) {
        auto expected_gen = std::dynamic_pointer_cast<GenericInstanceType>(expected);
        auto actual_enum = std::dynamic_pointer_cast<EnumType>(actual);
        return sameType(expected_gen->base_type, actual_enum);
    }

    // v5: ref<T> — implicit conversion from a tracked type to ref<T>.
    // A Buffer is assignable to a ref<Buffer> (taking a non-owning reference).
    if (expected->kind == TypeKind::REF) {
        auto ref_expected = std::dynamic_pointer_cast<RefType>(expected);
        return check_type_compatibility(ref_expected->inner_type, actual, nullptr);
    }

    // SIMD-5: Vector types are compatible if they are structurally identical.
    if (expected->kind == TypeKind::VECTOR && actual->kind == TypeKind::VECTOR) {
        return sameType(expected, actual);
    }

    // LANG-10: tuple type compatibility — same arity, element-wise compatibility.
    if (expected->kind == TypeKind::TUPLE && actual->kind == TypeKind::TUPLE) {
        auto expected_tuple = std::dynamic_pointer_cast<TupleType>(expected);
        auto actual_tuple = std::dynamic_pointer_cast<TupleType>(actual);
        if (expected_tuple->element_types.size() != actual_tuple->element_types.size())
            return false;
        for (size_t i = 0; i < expected_tuple->element_types.size(); ++i) {
            if (!check_type_compatibility(expected_tuple->element_types[i],
                                          actual_tuple->element_types[i], nullptr))
                return false;
        }
        return true;
    }

    return false;
}

    void TypeChecker::visit(std::shared_ptr<const ReturnStmt> stmt) {
        if (m_function_return_types.empty()) {
            error(stmt->keyword, "'return' can only be used inside a function body.", "E264");
            return;
        }

        auto expected_return_type = m_function_return_types.top();

        if (stmt->value) {
            stmt->value->accept(*this);
            auto actual_return_type = popType();

            // TS-3: pass a bare integer literal so in-range narrowing (e.g.
            // `return 5` into a u8 fn) is permitted; out-of-range still errors.
            const Literal* ret_lit = std::dynamic_pointer_cast<const Literal>(stmt->value).get();
            if (!check_type_compatibility(expected_return_type, actual_return_type, ret_lit)) {
                error(stmt->keyword, "Type mismatch. This function is declared to return '" +
                                     displayType(*expected_return_type, *actual_return_type) +
                                     "', but is returning a value of type '" +
                                     displayType(*actual_return_type, *expected_return_type) + "'.", "E265");
            }

        } else {
            if (!check_type_compatibility(expected_return_type, m_type_nil)) {
                error(stmt->keyword, "This function must return a value of type '" +
                                     expected_return_type->toString() + "'. An empty 'return;' is only valid for functions returning 'nil'.", "E266");
            }
        }
    }

}
