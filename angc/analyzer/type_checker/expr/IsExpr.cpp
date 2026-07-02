#include "TypeChecker.h"
namespace angara {

    // TS-7c: decide whether `object is target` is a *plausible* downcast — i.e.
    // the target could possibly describe the object's runtime type. Conservative:
    // returns true when it can't prove the two are unrelated (so it never
    // produces a false positive on a legal dynamic check).
    static bool isPlausibleIsTarget(const std::shared_ptr<Type>& object_type,
                                    const std::shared_ptr<Type>& target_type) {
        if (!object_type || !target_type) return true;
        if (object_type->kind == TypeKind::ANY || target_type->kind == TypeKind::ANY) return true;
        if (sameType(object_type, target_type)) return true;

        // optional<T> is T  /  optional<T> is optional<T>
        if (object_type->kind == TypeKind::OPTIONAL) {
            auto inner = std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type;
            if (sameType(target_type, inner)) return true;
            if (target_type->kind == TypeKind::OPTIONAL) {
                auto tinner = std::dynamic_pointer_cast<OptionalType>(target_type)->wrapped_type;
                if (sameType(tinner, inner)) return true;
            }
            return false;
        }

        // Class instance is-an ancestor class: walk the superclass chain.
        // (ClassType doesn't store its traits/contracts, only the AST does, so we
        // cannot disprove `x is SomeTrait` here — allow it to avoid false positives.)
        if (target_type->kind == TypeKind::TRAIT || target_type->kind == TypeKind::CONTRACT) {
            return object_type->kind == TypeKind::INSTANCE ||
                   object_type->kind == TypeKind::GENERIC_INSTANCE;
        }

        std::shared_ptr<ClassType> cls;
        if (object_type->kind == TypeKind::INSTANCE) {
            cls = std::dynamic_pointer_cast<InstanceType>(object_type)->class_type;
        } else if (object_type->kind == TypeKind::CLASS) {
            cls = std::dynamic_pointer_cast<ClassType>(object_type);
        }
        if (cls) {
            std::shared_ptr<ClassType> target_cls;
            if (target_type->kind == TypeKind::INSTANCE) {
                target_cls = std::dynamic_pointer_cast<InstanceType>(target_type)->class_type;
            } else if (target_type->kind == TypeKind::CLASS) {
                target_cls = std::dynamic_pointer_cast<ClassType>(target_type);
            }
            // TS-4: ancestor match by canonical pointer identity, not bare name
            // (a same-named class in another module is NOT an ancestor).
            for (auto cur = cls; cur; cur = cur->superclass) {
                if (target_cls && cur.get() == target_cls.get()) return true;
            }
            return false;
        }

        // Generic instance base check.
        if (object_type->kind == TypeKind::GENERIC_INSTANCE && target_type->kind == TypeKind::GENERIC_INSTANCE) {
            auto o = std::dynamic_pointer_cast<GenericInstanceType>(object_type);
            auto t = std::dynamic_pointer_cast<GenericInstanceType>(target_type);
            return sameType(o->base_type, t->base_type);
        }

        return false;
    }

    std::any TypeChecker::visit(const IsExpr& expr) {
        expr.object->accept(*this);
        auto object_type = popType();

        auto target_type = resolveType(expr.type);

        // TS-7c: in safe code, reject `x is UnrelatedType` where the target
        // cannot possibly describe the object's runtime type. @unsafe opts out.
        if (!m_is_in_unsafe_context && object_type &&
            object_type->kind != TypeKind::ERROR && target_type &&
            target_type->kind != TypeKind::ERROR &&
            !isPlausibleIsTarget(object_type, target_type)) {
            error(expr.keyword, "'" + object_type->toString() + "' can never be a '" +
                                 target_type->toString() + "' — this 'is' check is always false. "
                                 "Use an @unsafe block to suppress this if intentional.", "E384");
        }

        pushAndSave(&expr, m_type_bool);
        return {};
    }

}
