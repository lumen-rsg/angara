//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const SuperExpr& expr) {
        // 1. Rule: Must be inside a class.
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'super' outside of a class method.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 2. Rule: The class must have a superclass.
        if (m_current_class->superclass == nullptr) {
            error(expr.keyword, "Cannot use 'super' in a class with no superclass.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- 3. Dispatch based on the kind of `super` usage ---
        if (!expr.method.has_value()) {
            // Case A: It's a constructor call: `super(...)`.
            // The method being referred to is implicitly "init".
            auto method_it = m_current_class->superclass->methods.find("init");
            if (method_it == m_current_class->superclass->methods.end()) {
                // This means the parent has no constructor, which is a valid case
                // for a default, no-argument super() call. We will let the
                // visit(CallExpr) handle the arity check. We just need to
                // provide a function type for a zero-arg constructor.
                auto default_ctor_type = std::make_shared<FunctionType>(
                    std::vector<std::shared_ptr<Type>>{}, m_type_nil
                );
                pushAndSave(&expr, default_ctor_type);
            } else {
                // The parent has an `init` method. The type of `super` is the type of that method.
                pushAndSave(&expr, method_it->second.type);
            }
        } else {
            // Case B: It's a regular method call: `super.method(...)`.
            // The `expr.method` optional is guaranteed to have a value here.
            const std::string& method_name = expr.method->lexeme;

            // Rule: The method must exist on the superclass.
            const ClassType::MemberInfo* method_info = m_current_class->superclass->findProperty(method_name);

            if (method_info == nullptr || method_info->type->kind != TypeKind::FUNCTION) {
                error(*expr.method, "The superclass '" + m_current_class->superclass->name +
                                   "' has no method named '" + method_name + "'.");
                pushAndSave(&expr, m_type_error);
                return {};
            }

            // Check for private access on the superclass method
            if (method_info->access == AccessLevel::PRIVATE) {
                error(*expr.method, "Superclass method '" + method_name + "' is private and cannot be accessed.");
                pushAndSave(&expr, m_type_error);
                return {};
            }

            // Success! The type of this expression is the method's FunctionType.
            pushAndSave(&expr, method_info->type);
        }

        return {};
    }

}