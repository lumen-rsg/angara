//
// Created by cv2 on 9/20/25.
//

#include "TypeChecker.h"
#include <set>

namespace angara {

    std::any TypeChecker::visit(const MatchExpr& expr) {
        // 1. Type check the condition being matched.
        expr.condition->accept(*this);
        auto condition_type = popType();

        if (condition_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 2. Rule: The matched expression must be an enum type.
        if (condition_type->kind != TypeKind::ENUM) {
            error(expr.keyword, "Can only match on enum types, but got '" + condition_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }
        auto enum_type = std::dynamic_pointer_cast<EnumType>(condition_type);

        // 3. Check all cases, ensuring they are valid and determining the result type.
        std::shared_ptr<Type> common_result_type = nullptr;
        std::set<std::string> covered_variants;
        bool has_wildcard = false;

        for (const auto& case_item : expr.cases) {
            m_symbols.enterScope();

            // --- NEW: Explicitly check for the wildcard pattern FIRST ---
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(case_item.pattern)) {
                if (var_expr->name.lexeme == "_") {
                    has_wildcard = true;
                    // The wildcard matches anything, so we don't check its type or destructuring.
                    // We just proceed to check the body.
                    case_item.body->accept(*this);
                    auto body_type = popType();

                    if (body_type->kind != TypeKind::ERROR) {
                        if (!common_result_type) {
                            common_result_type = body_type;
                        } else if (common_result_type->toString() != body_type->toString()) {
                            error(expr.keyword, "All arms of a match expression must have the same type. Expected '"
                                                + common_result_type->toString() + "' but this arm has type '"
                                                + body_type->toString() + "'.");
                        }
                    }
                    m_symbols.exitScope();
                    continue; // Skip the rest of the loop and go to the next case.
                }
            }
            // --- END OF WILDCARD HANDLING ---


            // If it's not a wildcard, proceed with normal pattern validation.
            case_item.pattern->accept(*this);
            auto pattern_type = popType();

            if (pattern_type->kind == TypeKind::FUNCTION) {
                // It's a variant constructor, e.g., `WebEvent.KeyPress`.
                auto func_type = std::dynamic_pointer_cast<FunctionType>(pattern_type);

                std::string variant_name = "[unknown]";
                if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(case_item.pattern)) {
                    variant_name = get_expr->name.lexeme;
                }

                // Rule: The variant must belong to the correct enum.
                if (func_type->return_type->toString() != enum_type->name) {
                    error(expr.keyword, "Variant '" + variant_name + "' does not belong to the enum '" + enum_type->name + "'.");
                } else {
                    covered_variants.insert(variant_name);
                }

                // B. Handle destructuring.
                if (case_item.variable) {
                    if (func_type->param_types.empty()) {
                        error(*case_item.variable, "Variant '" + variant_name + "' has no associated data to bind.");
                    } else {
                        auto payload_type = func_type->param_types[0];
                        m_symbols.declare(*case_item.variable, payload_type, true);
                    }
                } else {
                    if (!func_type->param_types.empty()) {
                        error(expr.keyword, "Match case for variant '" + variant_name + "' must bind its value to a variable, e.g., 'case " + variant_name + "(x): ...'.");
                    }
                }
            }

            // C. Type check the case's body expression.
            case_item.body->accept(*this);
            auto body_type = popType();

            if (body_type->kind != TypeKind::ERROR) {
                if (!common_result_type) {
                    common_result_type = body_type;
                } else if (common_result_type->toString() != body_type->toString()) {
                    error(expr.keyword, "All arms of a match expression must have the same type. Expected '"
                                        + common_result_type->toString() + "' but this arm has type '"
                                        + body_type->toString() + "'.");
                }
            }
            m_symbols.exitScope();
        }

        // 4. Exhaustiveness check.
        if (!has_wildcard && covered_variants.size() != enum_type->variants.size()) {
            error(expr.keyword, "Match expression is not exhaustive. Add a wildcard case '_' or handle all variants.");
        }

        // 5. The final type of the whole expression is the common type of its branches.
        pushAndSave(&expr, common_result_type ? common_result_type : m_type_error);
        return {};
    }

}