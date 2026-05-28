#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const SuperExpr& expr) {
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'super' outside of a class method.", "E372");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (m_current_class->superclass == nullptr) {
            error(expr.keyword, "Class '" + m_current_class->name + "' has no superclass — 'super' requires 'inherits'.", "E373");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (!expr.method.has_value()) {
            auto method_it = m_current_class->superclass->methods.find("init");
            if (method_it == m_current_class->superclass->methods.end()) {
                auto default_ctor_type = std::make_shared<FunctionType>(
                    std::vector<std::shared_ptr<Type>>{}, m_type_nil
                );
                pushAndSave(&expr, default_ctor_type);
            } else {
                pushAndSave(&expr, method_it->second.type);
            }
        } else {
            const std::string& method_name = expr.method->lexeme;

            const ClassType::MemberInfo* method_info = m_current_class->superclass->findProperty(method_name);

            if (method_info == nullptr || method_info->type->kind != TypeKind::FUNCTION) {
                error(*expr.method, "Superclass '" + m_current_class->superclass->name +
                                   "' has no method named '" + method_name + "'.", "E374");
                pushAndSave(&expr, m_type_error);
                return {};
            }

            if (method_info->access == AccessLevel::PRIVATE) {
                error(*expr.method, "Superclass method '" + method_name + "' is private and cannot be accessed from a subclass.", "E375");
                pushAndSave(&expr, m_type_error);
                return {};
            }

            pushAndSave(&expr, method_info->type);
        }

        return {};
    }

}
