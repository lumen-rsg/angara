#include "TypeChecker.h"
#include <set>
#include <functional>
namespace angara {

    std::any TypeChecker::visit(const MatchExpr& expr) {
        expr.condition->accept(*this);
        auto condition_type = popType();

        if (condition_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // Match is allowed on: enums, integers, string, bool, char, tuples
        // LANG-8: also accept GenericInstanceType with EnumType base
        // LANG-10: also accept TupleType for structural destructuring
        bool is_enum = condition_type->kind == TypeKind::ENUM;
        std::shared_ptr<GenericInstanceType> generic_enum_instance;
        if (!is_enum && condition_type->kind == TypeKind::GENERIC_INSTANCE) {
            auto gi = std::dynamic_pointer_cast<GenericInstanceType>(condition_type);
            if (gi && gi->base_type->kind == TypeKind::ENUM) {
                is_enum = true;
                generic_enum_instance = gi;
                condition_type = gi->base_type;  // use the base EnumType for variant lookups
            }
        }
        bool is_value = isInteger(condition_type) ||
                        condition_type->toString() == "string" ||
                        condition_type->toString() == "bool" ||
                        isChar(condition_type);
        bool is_tuple = condition_type->kind == TypeKind::TUPLE;

        if (!is_enum && !is_value && !is_tuple) {
            error(expr.keyword,
                "Match expressions can only be used with enum, integer, string, bool, char, or tuple types, but got '" +
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

                // LANG-10: tuple pattern (e.g., (1, x, _))
                if (auto tuple_pat = std::dynamic_pointer_cast<const TupleExpr>(pat)) {
                    if (!is_tuple) {
                        error(expr.keyword,
                            "Tuple patterns can only be used when matching on a tuple type, "
                            "not '" + condition_type->toString() + "'.",
                            "E411");
                        continue;
                    }
                    auto tuple_type = std::dynamic_pointer_cast<TupleType>(condition_type);
                    if (tuple_type->element_types.size() != tuple_pat->elements.size()) {
                        error(expr.keyword,
                            "Tuple pattern arity mismatch. The tuple type has " +
                            std::to_string(tuple_type->element_types.size()) +
                            " element(s), but the pattern has " +
                            std::to_string(tuple_pat->elements.size()) + ".",
                            "E412");
                        continue;
                    }
                    // Recursively validate each sub-pattern against its element type.
                    // We temporarily swap condition_type for recursive validation,
                    // then restore it.
                    auto saved_condition_type = condition_type;
                    auto saved_is_value = is_value;
                    auto saved_is_tuple = is_tuple;
                    auto saved_is_enum = is_enum;
                    for (size_t i = 0; i < tuple_pat->elements.size(); ++i) {
                        const auto& sub_pat = tuple_pat->elements[i];
                        auto elem_type = tuple_type->element_types[i];
                        condition_type = elem_type;
                        is_value = isInteger(elem_type) ||
                                   elem_type->toString() == "string" ||
                                   elem_type->toString() == "bool" ||
                                   isChar(elem_type);
                        is_tuple = elem_type->kind == TypeKind::TUPLE;
                        is_enum = elem_type->kind == TypeKind::ENUM;

                        // Validate the sub-pattern against the element type
                        if (auto sub_ve = std::dynamic_pointer_cast<const VarExpr>(sub_pat)) {
                            if (sub_ve->name.lexeme == "_") {
                                // wildcard — always valid
                            }
                            // else: variable binding — handled in Step 2
                        } else if (auto sub_lit = std::dynamic_pointer_cast<const Literal>(sub_pat)) {
                            // Literal sub-pattern — validate against element type
                            if (!is_value) {
                                error(sub_lit->token,
                                    "Literal patterns in tuple elements can only match integer, string, bool, or char types.",
                                    "E402");
                            }
                            // Type compatibility check
                            std::shared_ptr<Type> lit_type = m_type_error;
                            switch (sub_lit->token.type) {
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
                            if (lit_type && lit_type->kind != TypeKind::ERROR &&
                                !sameType(lit_type, elem_type) &&
                                elem_type->toString() != lit_type->toString()) {
                                error(sub_lit->token,
                                    "Literal of type '" + lit_type->toString() +
                                    "' does not match the tuple element type '" +
                                    elem_type->toString() + "'.",
                                    "E403");
                            }
                        } else if (auto sub_nested = std::dynamic_pointer_cast<const NestedPattern>(sub_pat)) {
                            // Nested constructor pattern inside tuple — validate
                            // (reuses existing nested pattern logic below)
                            // We skip full validation here; it's handled in Step 2
                        } else if (auto sub_tuple = std::dynamic_pointer_cast<const TupleExpr>(sub_pat)) {
                            // Nested tuple pattern — recursively validate
                            if (!is_tuple) {
                                error(expr.keyword,
                                    "Nested tuple pattern used on non-tuple element type '" +
                                    elem_type->toString() + "'.",
                                    "E411");
                            }
                            // Recursion would happen if this code were structured recursively
                        } else {
                            // Constructor or other pattern — validate
                            sub_pat->accept(*this);
                            auto sub_pat_type = popType();
                            if (sub_pat_type->kind != TypeKind::ERROR &&
                                sub_pat_type->kind != TypeKind::FUNCTION &&
                                sub_pat_type->kind != TypeKind::ENUM &&
                                !is_value) {
                                error(expr.keyword,
                                    "Invalid pattern in tuple element " + std::to_string(i) + ".",
                                    "E407");
                            }
                        }
                    }
                    // Restore condition type context
                    condition_type = saved_condition_type;
                    is_value = saved_is_value;
                    is_tuple = saved_is_tuple;
                    is_enum = saved_is_enum;
                    continue;
                }

                // Nested constructor pattern (e.g., Ok(Some(v)))
                if (auto nested = std::dynamic_pointer_cast<const NestedPattern>(pat)) {
                    // Resolve the outer constructor
                    nested->constructor->accept(*this);
                    auto outer_type = popType();

                    std::string variant_name = "[unknown]";
                    if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(nested->constructor)) {
                        variant_name = get_expr->name.lexeme;
                    } else if (auto ve = std::dynamic_pointer_cast<const VarExpr>(nested->constructor)) {
                        variant_name = ve->name.lexeme;
                    }

                    if (outer_type->kind == TypeKind::FUNCTION) {
                        auto func_type = std::dynamic_pointer_cast<FunctionType>(outer_type);
                        if (enum_type && !sameType(func_type->return_type, enum_type)) {
                            error(expr.keyword,
                                "Variant '" + variant_name + "' does not belong to enum '" +
                                enum_type->name + "'.",
                                "E367");
                        } else {
                            covered_variants.insert(variant_name);
                        }
                    } else if (outer_type->kind == TypeKind::ENUM) {
                        if (enum_type && sameType(outer_type, enum_type)) {
                            covered_variants.insert(variant_name);
                        }
                    } else if (outer_type->kind != TypeKind::ERROR) {
                        error(expr.keyword,
                            "Invalid pattern in match case. Expected an enum variant, literal, or '_'.",
                            "E407");
                    }
                    // Sub-patterns are handled in Step 2 (variable declaration)
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
                    // Determine the payload types for each alternative and
                    // cross-check that all alternatives agree on the types
                    // for the same-named bound variables.
                    std::vector<std::shared_ptr<Type>> resolved_types;

                    for (size_t ai = 0; ai < case_item.patterns.size(); ++ai) {
                        const auto& pat = case_item.patterns[ai];
                        const auto& alt_vars = ai < case_item.alt_variables.size()
                            ? case_item.alt_variables[ai] : case_item.variables;

                        // Skip wildcard patterns
                        if (auto ve = std::dynamic_pointer_cast<const VarExpr>(pat)) {
                            if (ve->name.lexeme == "_") continue;
                        }

                        // For nested patterns, descend to the innermost constructor
                        // pattern (the last NestedPattern), not into its sub-patterns.
                        const std::shared_ptr<Expr>* inner_pat = &pat;
                        const NestedPattern* last_nested = nullptr;
                        if (auto np = std::dynamic_pointer_cast<const NestedPattern>(*inner_pat)) {
                            last_nested = np.get();
                            while (!np->subpatterns.empty()) {
                                auto next = std::dynamic_pointer_cast<const NestedPattern>(np->subpatterns.back());
                                if (!next) break;
                                np = next;
                                last_nested = np.get();
                            }
                            // Use the constructor of the innermost NestedPattern
                            inner_pat = &last_nested->constructor;
                        }

                        // Extract variant name from the innermost pattern
                        std::string vname;
                        if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(*inner_pat)) {
                            vname = get_expr->name.lexeme;
                        } else if (auto ve2 = std::dynamic_pointer_cast<const VarExpr>(*inner_pat)) {
                            vname = ve2->name.lexeme;
                        } else {
                            continue;
                        }

                        auto vit = enum_type->variants.find(vname);
                        // If not found in the current enum, the nested pattern may
                        // refer to a sub-enum — look through the outer variant's
                        // return type.
                        if (vit == enum_type->variants.end() &&
                            std::dynamic_pointer_cast<const NestedPattern>(pat)) {
                            // Walk the nested pattern to find the sub-enum type.
                            // Stop BEFORE the innermost pattern — that's where the
                            // bindings' variant lives.
                            auto nested = std::dynamic_pointer_cast<const NestedPattern>(pat);
                            std::shared_ptr<Type> cur_enum = enum_type;
                            while (nested) {
                                // Check if the next level is the leaf (not a NestedPattern)
                                bool next_is_leaf = true;
                                if (!nested->subpatterns.empty()) {
                                    auto& next = nested->subpatterns.back();
                                    if (std::dynamic_pointer_cast<const NestedPattern>(next)) {
                                        next_is_leaf = false;
                                    }
                                }

                                if (next_is_leaf) break;  // stop here — cur_enum is the right enum

                                std::string outer_vname;
                                if (auto ge = std::dynamic_pointer_cast<const GetExpr>(nested->constructor))
                                    outer_vname = ge->name.lexeme;
                                else if (auto ve = std::dynamic_pointer_cast<const VarExpr>(nested->constructor))
                                    outer_vname = ve->name.lexeme;
                                else break;

                                auto etype = std::dynamic_pointer_cast<EnumType>(cur_enum);
                                if (!etype) break;
                                auto ovit = etype->variants.find(outer_vname);
                                if (ovit == etype->variants.end()) break;
                                if (ovit->second->param_types.empty()) break;
                                cur_enum = ovit->second->param_types[0];
                                nested = std::dynamic_pointer_cast<const NestedPattern>(nested->subpatterns.back());
                            }
                            if (cur_enum && cur_enum->kind == TypeKind::ENUM) {
                                auto sub_enum = std::dynamic_pointer_cast<EnumType>(cur_enum);
                                vit = sub_enum->variants.find(vname);
                            }
                        }
                        if (vit == enum_type->variants.end() &&
                            std::dynamic_pointer_cast<const NestedPattern>(pat)) {
                            // Already tried sub-enum lookup above; just skip.
                            continue;
                        }
                        if (vit == enum_type->variants.end()) continue;

                        auto alt_types = vit->second->param_types;

                        // Substitute generic type params through the generic instance
                        if (generic_enum_instance) {
                            std::vector<std::shared_ptr<Type>> sub_types;
                            for (const auto& pt : alt_types) {
                                sub_types.push_back(generic_enum_instance->substitute(pt));
                            }
                            alt_types = std::move(sub_types);
                        }

                        // Check binding count
                        if (alt_vars.size() != alt_types.size()) {
                            error(alt_vars.empty() ? case_item.variables[0] : alt_vars[0],
                                "Wrong number of bindings for variant '" + vname + "'. Expected " +
                                std::to_string(alt_types.size()) + " but got " +
                                std::to_string(alt_vars.size()) + ".",
                                "E405");
                            continue;
                        }

                        // First alternative: set the resolved types
                        if (resolved_types.empty()) {
                            resolved_types = alt_types;
                        } else {
                            // Cross-check: same-named variables must have same types
                            for (size_t vi = 0; vi < alt_vars.size() && vi < case_item.variables.size(); ++vi) {
                                if (alt_vars[vi].lexeme != case_item.variables[vi].lexeme) continue;
                                if (!sameType(resolved_types[vi], alt_types[vi])) {
                                    error(alt_vars[vi],
                                        "Or-pattern variable '" + alt_vars[vi].lexeme +
                                        "' has incompatible types across alternatives: '" +
                                        resolved_types[vi]->toString() + "' vs '" +
                                        alt_types[vi]->toString() + "'.",
                                        "E406");
                                }
                            }
                        }
                    }

                    if (!resolved_types.empty()) {
                        for (size_t i = 0; i < case_item.variables.size() && i < resolved_types.size(); ++i) {
                            m_symbols.declare(case_item.variables[i], resolved_types[i], true);
                        }
                    }
                } else if (is_tuple) {
                    // LANG-10: declare bound variables for tuple patterns.
                    // Walk the pattern tree recursively to find each variable's
                    // position in the tuple type tree.
                    auto tuple_type = std::dynamic_pointer_cast<TupleType>(condition_type);
                    const auto& first_pat = case_item.patterns[0];

                    // Recursive helper: find a variable name in a pattern tree
                    // and return its corresponding type from the type tree.
                    std::function<std::shared_ptr<Type>(const std::shared_ptr<Expr>&, std::shared_ptr<Type>, const std::string&)> findVarType;
                    findVarType = [&](const std::shared_ptr<Expr>& pat, std::shared_ptr<Type> ty, const std::string& name) -> std::shared_ptr<Type> {
                        if (!pat || !ty) return nullptr;
                        if (auto* ve = dynamic_cast<const VarExpr*>(pat.get())) {
                            if (ve->name.lexeme == name) return ty;
                            return nullptr;
                        }
                        if (auto* tp = dynamic_cast<const TupleExpr*>(pat.get())) {
                            auto* tt = dynamic_cast<const TupleType*>(ty.get());
                            if (!tt || tp->elements.size() != tt->element_types.size()) return nullptr;
                            for (size_t i = 0; i < tp->elements.size(); ++i) {
                                auto result = findVarType(tp->elements[i], tt->element_types[i], name);
                                if (result) return result;
                            }
                        }
                        return nullptr;
                    };

                    for (size_t i = 0; i < case_item.variables.size(); ++i) {
                        auto var_type = findVarType(first_pat, tuple_type, case_item.variables[i].lexeme);
                        if (var_type) {
                            m_symbols.declare(case_item.variables[i], var_type, true);
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
        } else if (is_tuple && !has_wildcard) {
            // LANG-10: tuple matches are not exhaustively checked by element —
            // require a wildcard arm.
            error(expr.keyword,
                "Match on tuple type '" + condition_type->toString() +
                "' is not exhaustive. Add a wildcard case '_' to handle remaining values.",
                "E410");
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
