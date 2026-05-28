#include "TypeChecker.h"
#include <set>
namespace angara {

    std::any TypeChecker::visit(const MatchExpr& expr) {
        expr.condition->accept(*this);
        auto condition_type = popType();

        if (condition_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (condition_type->kind != TypeKind::ENUM) {
            error(expr.keyword, "Match expressions can only be used with enum types, but got '" + condition_type->toString() + "'.", "E365");
            pushAndSave(&expr, m_type_error);
            return {};
        }
        auto enum_type = std::dynamic_pointer_cast<EnumType>(condition_type);

        std::shared_ptr<Type> common_result_type = nullptr;
        std::set<std::string> covered_variants;
        bool has_wildcard = false;

        for (const auto& case_item : expr.cases) {
            m_symbols.enterScope();

            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(case_item.pattern)) {
                if (var_expr->name.lexeme == "_") {
                    has_wildcard = true;
                    case_item.body->accept(*this);
                    auto body_type = popType();

                    if (body_type->kind != TypeKind::ERROR) {
                        if (!common_result_type) {
                            common_result_type = body_type;
                        } else if (common_result_type->toString() != body_type->toString()) {
                            error(expr.keyword, "All match arms must return the same type. Expected '" +
                                                common_result_type->toString() + "' but this arm has type '" +
                                                body_type->toString() + "'.", "E366");
                        }
                    }
                    exitScopeAndWarn();
                    continue;
                }
            }

            case_item.pattern->accept(*this);
            auto pattern_type = popType();

            if (pattern_type->kind == TypeKind::FUNCTION) {
                auto func_type = std::dynamic_pointer_cast<FunctionType>(pattern_type);

                std::string variant_name = "[unknown]";
                if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(case_item.pattern)) {
                    variant_name = get_expr->name.lexeme;
                }

                if (func_type->return_type->toString() != enum_type->name) {
                    error(expr.keyword, "Variant '" + variant_name + "' does not belong to enum '" + enum_type->name + "'.", "E367");
                } else {
                    covered_variants.insert(variant_name);
                }

                if (case_item.variable) {
                    if (func_type->param_types.empty()) {
                        error(*case_item.variable, "Variant '" + variant_name + "' has no payload to bind.", "E368");
                    } else {
                        auto payload_type = func_type->param_types[0];
                        m_symbols.declare(*case_item.variable, payload_type, true);
                    }
                } else {
                    if (!func_type->param_types.empty()) {
                        error(expr.keyword, "Match case for variant '" + variant_name + "' must bind its payload to a variable, e.g., 'case " + variant_name + "(x): ...'.", "E369");
                    }
                }
            }

            case_item.body->accept(*this);
            auto body_type = popType();

            if (body_type->kind != TypeKind::ERROR) {
                if (!common_result_type) {
                    common_result_type = body_type;
                } else if (common_result_type->toString() != body_type->toString()) {
                    error(expr.keyword, "All match arms must return the same type. Expected '" +
                                        common_result_type->toString() + "' but this arm has type '" +
                                        body_type->toString() + "'.", "E370");
                }
            }
            exitScopeAndWarn();
        }

        if (!has_wildcard && covered_variants.size() != enum_type->variants.size()) {
            error(expr.keyword, "Match expression is not exhaustive. Handle all variants of '" + enum_type->name + "' or add a wildcard case '_'.", "E371");
        }

        pushAndSave(&expr, common_result_type ? common_result_type : m_type_error);
        return {};
    }

}
