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

        // Match is allowed on: enums, integers, string, bool, char
        bool is_enum = condition_type->kind == TypeKind::ENUM;
        bool is_value = isInteger(condition_type) ||
                        condition_type->toString() == "string" ||
                        condition_type->toString() == "bool" ||
                        isChar(condition_type);

        if (!is_enum && !is_value) {
            error(expr.keyword,
                "Match expressions can only be used with enum, integer, string, bool, or char types, but got '" +
                condition_type->toString() + "'.",
                "E365");
            pushAndSave(&expr, m_type_error);
            return {};
        }
        auto enum_type = is_enum ? std::dynamic_pointer_cast<EnumType>(condition_type) : nullptr;

        std::shared_ptr<Type> common_result_type = nullptr;
        std::set<std::string> covered_variants;
        bool has_wildcard = false;

        // For value matches (int/string/bool/char): detect duplicate literal values
        std::set<std::string> seen_literals;

        for (const auto& case_item : expr.cases) {
            m_symbols.enterScope();

            // Determine if this case has an or-pattern with wildcard
            for (const auto& pat : case_item.patterns) {
                if (auto ve = std::dynamic_pointer_cast<const VarExpr>(pat)) {
                    if (ve->name.lexeme == "_") {
                        if (case_item.patterns.size() == 1) has_wildcard = true;
                    }
                }
            }

            for (size_t pi = 0; pi < case_item.patterns.size(); ++pi) {
                const auto& pat = case_item.patterns[pi];

                // Wildcard pattern
                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(pat)) {
                    if (var_expr->name.lexeme == "_") {
                        continue;
                    }
                }

                // Literal pattern (for value matches)
                if (auto lit = std::dynamic_pointer_cast<const Literal>(pat)) {
                    if (!is_value) {
                        error(lit->token,
                            "Literal patterns are only valid when matching on integer, string, bool, or char types, "
                            "not '" + condition_type->toString() + "'.",
                            "E402");
                        continue;
                    }
                    // Determine the literal's type from the token
                    std::shared_ptr<Type> lit_type = m_type_error;
                    switch (lit->token.type) {
                        case TokenType::NUMBER_INT: lit_type = m_type_i64; break;
                        case TokenType::NUMBER_FLOAT: lit_type = m_type_f64; break;
                        case TokenType::STRING:
                        case TokenType::RAW_STRING:
                        case TokenType::BYTE_STRING: lit_type = m_type_string; break;
                        case TokenType::CHAR: lit_type = m_type_char; break;
                        case TokenType::TRUE:
                        case TokenType::FALSE: lit_type = m_type_bool; break;
                        case TokenType::NIL: lit_type = m_type_nil; break;
                        default: break;
                    }
                    if (lit_type && lit_type->kind != TypeKind::ERROR) {
                        if (!sameType(lit_type, condition_type) &&
                            condition_type->toString() != lit_type->toString()) {
                            error(lit->token,
                                "Literal of type '" + lit_type->toString() +
                                "' does not match the match condition type '" +
                                condition_type->toString() + "'.",
                                "E403");
                        }
                    }
                    // Duplicate detection for literal patterns
                    if (pi == 0 && case_item.patterns.size() == 1) {
                        std::string lit_key = lit->token.type == TokenType::NUMBER_INT
                            ? lit->token.lexeme
                            : to_string(lit->token.type) + ":" + lit->token.lexeme;
                        if (seen_literals.count(lit_key)) {
                            error(lit->token,
                                "Duplicate literal pattern '" + lit->token.lexeme + "' in match expression.",
                                "E404");
                        }
                        seen_literals.insert(lit_key);
                    }
                    continue;
                }

                // Constructor pattern (for enum matches)
                pat->accept(*this);
                auto pattern_type = popType();

                if (pattern_type->kind == TypeKind::FUNCTION) {
                    auto func_type = std::dynamic_pointer_cast<FunctionType>(pattern_type);

                    std::string variant_name = "[unknown]";
                    if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(pat)) {
                        variant_name = get_expr->name.lexeme;
                    }

                    if (enum_type && !sameType(func_type->return_type, enum_type)) {
                        error(expr.keyword,
                            "Variant '" + variant_name + "' does not belong to enum '" +
                            enum_type->name + "'.",
                            "E367");
                    } else {
                        covered_variants.insert(variant_name);
                    }
                } else if (pattern_type->kind == TypeKind::ENUM) {
                    // Simple variant without payload (e.g., PageLoad)
                    // The pattern type IS the enum type itself
                    std::string variant_name = "[unknown]";
                    if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(pat)) {
                        variant_name = get_expr->name.lexeme;
                    }
                    if (enum_type && sameType(pattern_type, enum_type)) {
                        covered_variants.insert(variant_name);
                    }
                } else if (pattern_type->kind != TypeKind::ERROR) {
                    error(expr.keyword,
                        "Invalid pattern in match case. Expected an enum variant, literal, or '_'.",
                        "E407");
                }
            }

            // --- Step 2: Declare bound variables ---
            if (!case_item.variables.empty()) {
                if (is_enum && enum_type) {
                    // Determine the payload types from the first constructor pattern
                    std::vector<std::shared_ptr<Type>> payload_types;
                    for (const auto& pat : case_item.patterns) {
                        if (auto ve = std::dynamic_pointer_cast<const VarExpr>(pat)) continue; // skip wildcard
                        if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(pat)) {
                            std::string vname = get_expr->name.lexeme;
                            auto vit = enum_type->variants.find(vname);
                            if (vit != enum_type->variants.end()) {
                                payload_types = vit->second->param_types;
                                break;
                            }
                        } else if (auto ve2 = std::dynamic_pointer_cast<const VarExpr>(pat)) {
                            std::string vname = ve2->name.lexeme;
                            auto vit = enum_type->variants.find(vname);
                            if (vit != enum_type->variants.end()) {
                                payload_types = vit->second->param_types;
                                break;
                            }
                        }
                    }

                    if (!payload_types.empty()) {
                        if (case_item.variables.size() != payload_types.size()) {
                            error(case_item.variables[0],
                                "Wrong number of bindings for variant. Expected " +
                                std::to_string(payload_types.size()) + " but got " +
                                std::to_string(case_item.variables.size()) + ".",
                                "E405");
                        }
                        for (size_t i = 0; i < case_item.variables.size() && i < payload_types.size(); ++i) {
                            m_symbols.declare(case_item.variables[i], payload_types[i], true);
                        }
                    }
                } else if (!is_enum) {
                    error(case_item.variables[0],
                        "Payload bindings are only valid for enum variant patterns, not for value matches.",
                        "E408");
                }
            }

            // --- Step 3: Type-check guard ---
            if (case_item.guard) {
                (*case_item.guard)->accept(*this);
                auto guard_type = popType();
                if (guard_type->kind != TypeKind::ERROR && guard_type->toString() != "bool") {
                    error(expr.keyword,
                        "Match guard must be a boolean expression, but got '" +
                        guard_type->toString() + "'.",
                        "E409");
                }
            }

            // --- Step 4: Type-check body ---
            case_item.body->accept(*this);
            auto body_type = popType();

            if (body_type->kind != TypeKind::ERROR) {
                if (!common_result_type) {
                    common_result_type = body_type;
                } else if (!sameType(common_result_type, body_type)) {
                    error(expr.keyword,
                        "All match arms must return the same type. Expected '" +
                        common_result_type->toString() + "' but this arm has type '" +
                        body_type->toString() + "'.",
                        "E370");
                }
            }
            exitScopeAndWarn();
        }

        // --- Step 5: Exhaustiveness check ---
        if (is_enum && enum_type) {
            if (!has_wildcard && covered_variants.size() != enum_type->variants.size()) {
                error(expr.keyword,
                    "Match expression is not exhaustive. Handle all variants of '" +
                    enum_type->name + "' or add a wildcard case '_'.",
                    "E371");
            }
        } else if (is_value && !has_wildcard) {
            // For bool type, explicit true + false is exhaustive
            // For other value types, require a wildcard
            if (condition_type->toString() != "bool" || seen_literals.size() < 2) {
                error(expr.keyword,
                    "Match on type '" + condition_type->toString() +
                    "' is not exhaustive. Add a wildcard case '_' to handle all remaining values.",
                    "E410");
            }
        }

        pushAndSave(&expr, common_result_type ? common_result_type : m_type_error);
        return {};
    }

}
