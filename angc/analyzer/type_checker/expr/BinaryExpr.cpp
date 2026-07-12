#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Binary& expr) {
        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        std::shared_ptr<Type> result_type = m_type_error;

        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        switch (expr.op.type) {
            case TokenType::MINUS:
            case TokenType::SLASH:
            case TokenType::PERCENT:
            case TokenType::AMPERSAND:
            case TokenType::PIPE:
            case TokenType::CARET:
            case TokenType::LSHIFT:
            case TokenType::RSHIFT:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_any;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    if ((expr.op.type == TokenType::SLASH || expr.op.type == TokenType::PERCENT)) {
                        if (auto rhs_literal = std::dynamic_pointer_cast<const Literal>(expr.right)) {
                            if (rhs_literal->token.type == TokenType::NUMBER_INT && rhs_literal->token.lexeme == "0") {
                                warning(expr.op, std::string(expr.op.type == TokenType::SLASH
                                    ? "Division by zero."
                                    : "Modulo by zero."), "W001");
                            }
                        }
                    }
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        auto left_name = left_type->toString();
                        auto right_name = right_type->toString();
                        if (left_name == "i64" || right_name == "i64") {
                            result_type = m_type_i64;
                        } else if (left_name == "u64" || right_name == "u64") {
                            result_type = (left_name == "u64") ? left_type : right_type;
                        } else {
                            auto width = [](const std::string& n) -> int {
                                if (n == "i8"  || n == "u8")  return 8;
                                if (n == "i16" || n == "u16") return 16;
                                if (n == "i32" || n == "u32") return 32;
                                return 64;
                            };
                            int lw = width(left_name), rw = width(right_name);
                            if (lw >= rw) result_type = left_type;
                            else result_type = right_type;
                        }
                    }
                } else if (left_type->kind == TypeKind::VECTOR && right_type->kind == TypeKind::VECTOR) {
                    // SIMD-5: element-wise vector arithmetic
                    if (sameType(left_type, right_type)) {
                        result_type = left_type;
                    } else {
                        error(expr.op, "Operator '" + expr.op.lexeme + "' requires both vector operands to have "
                                       "the same type, but got '" + left_type->toString() + "' and '" +
                                       right_type->toString() + "'.", "E421");
                    }
                } else if (left_type->kind == TypeKind::VECTOR && isNumeric(right_type)) {
                    // SIMD-5: vector - scalar, vector / scalar
                    result_type = left_type;
                } else if (isNumeric(left_type) && right_type->kind == TypeKind::VECTOR) {
                    // SIMD-5: scalar - vector (makes sense for subtraction)
                    if (expr.op.type == TokenType::MINUS) {
                        result_type = right_type;
                    } else {
                        error(expr.op, "Operator '" + expr.op.lexeme + "' cannot be used with scalar left and vector "
                                       "right operands.", "E428");
                    }
                } else {
                    // LANG-13: check for user-defined opSub/opDiv/opRem on the left type.
                    std::shared_ptr<ClassType> cls;
                    if (left_type->kind == TypeKind::INSTANCE) {
                        auto inst = std::dynamic_pointer_cast<InstanceType>(left_type);
                        if (inst) cls = inst->class_type;
                    } else if (left_type->kind == TypeKind::CLASS) {
                        cls = std::dynamic_pointer_cast<ClassType>(left_type);
                    }
                    if (cls) {
                        const char* op_name = nullptr;
                        switch (expr.op.type) {
                            case TokenType::MINUS:   op_name = "opSub"; break;
                            case TokenType::SLASH:   op_name = "opDiv"; break;
                            case TokenType::PERCENT: op_name = "opRem"; break;
                            default: break;
                        }
                        if (op_name) {
                            const auto* op_info = cls->findProperty(op_name);
                            if (op_info && op_info->type->kind == TypeKind::FUNCTION) {
                                auto ft = std::dynamic_pointer_cast<FunctionType>(op_info->type);
                                if (ft && ft->param_types.size() == 1) {
                                    result_type = ft->return_type;
                                } else {
                                    error(expr.op, std::string("Method '") + op_name +
                                          "' must have signature 'func " + op_name +
                                          "(self, other) -> T' (1 parameter).", "E429");
                                }
                            }
                        }
                    }
                    // M8: check trait-bound operator on TYPE_PARAM
                    if ((!result_type || result_type->kind == TypeKind::ERROR) &&
                        left_type->kind == TypeKind::TYPE_PARAM) {
                        auto tp = std::dynamic_pointer_cast<TypeParameterType>(left_type);
                        auto bound_it = m_active_type_param_bounds.find(tp->name);
                        if (bound_it != m_active_type_param_bounds.end() && bound_it->second) {
                            const char* op_name = nullptr;
                            switch (expr.op.type) {
                                case TokenType::MINUS:   op_name = "opSub"; break;
                                case TokenType::SLASH:   op_name = "opDiv"; break;
                                case TokenType::PERCENT: op_name = "opRem"; break;
                                default: break;
                            }
                            if (op_name) {
                                auto mit = bound_it->second->methods.find(op_name);
                                if (mit != bound_it->second->methods.end()) {
                                    result_type = mit->second->return_type;
                                }
                            }
                        }
                    }
                    if (!result_type || result_type->kind == TypeKind::ERROR) {
                        error(expr.op, "Operator '" + expr.op.lexeme + "' requires numeric operands, but got '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.", "E352");
                    }
                }
                break;

            case TokenType::STAR:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_any;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else if (left_type->toString() == "string" && isInteger(right_type)) {
                    if (m_is_in_freestanding_mode && !m_has_fs_allocator) {
                        error(expr.op,
                              "String repetition is not available in --freestanding mode "
                              "(strings require heap allocation and the string runtime).",
                              "E915");
                        pushAndSave(&expr, m_type_error);
                        return {};
                    }
                    result_type = m_type_string;
                } else if (left_type->kind == TypeKind::VECTOR && right_type->kind == TypeKind::VECTOR) {
                    // SIMD-5: element-wise vector multiplication
                    if (sameType(left_type, right_type)) {
                        result_type = left_type;
                    } else {
                        error(expr.op, "Vector multiplication requires both operands to have the same type, "
                                       "but got '" + left_type->toString() + "' and '" +
                                       right_type->toString() + "'.", "E422");
                    }
                } else if (left_type->kind == TypeKind::VECTOR && isNumeric(right_type)) {
                    // SIMD-5: scalar broadcast: vec * scalar
                    result_type = left_type;
                } else if (isNumeric(left_type) && right_type->kind == TypeKind::VECTOR) {
                    // SIMD-5: scalar broadcast: scalar * vec
                    result_type = right_type;
                } else {
                    // LANG-13: check for user-defined opMul on the left type.
                    std::shared_ptr<ClassType> cls;
                    if (left_type->kind == TypeKind::INSTANCE) {
                        auto inst = std::dynamic_pointer_cast<InstanceType>(left_type);
                        if (inst) cls = inst->class_type;
                    } else if (left_type->kind == TypeKind::CLASS) {
                        cls = std::dynamic_pointer_cast<ClassType>(left_type);
                    }
                    if (cls) {
                        const auto* op_info = cls->findProperty("opMul");
                        if (op_info && op_info->type->kind == TypeKind::FUNCTION) {
                            auto ft = std::dynamic_pointer_cast<FunctionType>(op_info->type);
                            if (ft && ft->param_types.size() == 1) {
                                result_type = ft->return_type;
                            } else {
                                error(expr.op, "Method 'opMul' must have signature 'func opMul(self, other) -> T' "
                                       "(1 parameter).", "E430");
                            }
                        }
                    }
                    // M8: check trait-bound opMul on TYPE_PARAM
                    if ((!result_type || result_type->kind == TypeKind::ERROR) &&
                        left_type->kind == TypeKind::TYPE_PARAM) {
                        auto tp = std::dynamic_pointer_cast<TypeParameterType>(left_type);
                        auto bound_it = m_active_type_param_bounds.find(tp->name);
                        if (bound_it != m_active_type_param_bounds.end() && bound_it->second) {
                            auto mit = bound_it->second->methods.find("opMul");
                            if (mit != bound_it->second->methods.end()) {
                                result_type = mit->second->return_type;
                            }
                        }
                    }
                    if (!result_type || result_type->kind == TypeKind::ERROR) {
                        error(expr.op, "Operator '*' can only be used with two numbers (arithmetic) or 'string * number' (repetition).", "E353");
                    }
                }
                break;

            case TokenType::PLUS:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_any;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else if (left_type->toString() == "string" && right_type->toString() == "string") {
                    if (m_is_in_freestanding_mode && !m_has_fs_allocator) {
                        error(expr.op,
                              "String concatenation is not available in --freestanding mode "
                              "(strings require heap allocation and the string runtime).",
                              "E915");
                        pushAndSave(&expr, m_type_error);
                        return {};
                    }
                    result_type = m_type_string;
                } else if (left_type->kind == TypeKind::VECTOR && right_type->kind == TypeKind::VECTOR) {
                    // SIMD-5: element-wise vector addition
                    if (sameType(left_type, right_type)) {
                        result_type = left_type;
                    } else {
                        error(expr.op, "Vector addition requires both operands to have the same type, "
                                       "but got '" + left_type->toString() + "' and '" +
                                       right_type->toString() + "'.", "E423");
                    }
                } else if (left_type->kind == TypeKind::VECTOR && isNumeric(right_type)) {
                    // SIMD-5: scalar broadcast: vec + scalar
                    result_type = left_type;
                } else if (isNumeric(left_type) && right_type->kind == TypeKind::VECTOR) {
                    // SIMD-5: scalar broadcast: scalar + vec
                    result_type = right_type;
                } else {
                    // LANG-13: check for user-defined opAdd on the left type.
                    std::shared_ptr<ClassType> cls;
                    if (left_type->kind == TypeKind::INSTANCE) {
                        auto inst = std::dynamic_pointer_cast<InstanceType>(left_type);
                        if (inst) cls = inst->class_type;
                    } else if (left_type->kind == TypeKind::CLASS) {
                        cls = std::dynamic_pointer_cast<ClassType>(left_type);
                    }
                    if (cls) {
                        const auto* op_info = cls->findProperty("opAdd");
                        if (op_info && op_info->type->kind == TypeKind::FUNCTION) {
                            auto ft = std::dynamic_pointer_cast<FunctionType>(op_info->type);
                            if (ft && ft->param_types.size() == 1) {
                                result_type = ft->return_type;
                            } else {
                                error(expr.op, "Method 'opAdd' must have signature 'func opAdd(self, other) -> T' "
                                       "(1 parameter).", "E431");
                            }
                        }
                    }
                    // M8: check trait-bound opAdd on TYPE_PARAM
                    if ((!result_type || result_type->kind == TypeKind::ERROR) &&
                        left_type->kind == TypeKind::TYPE_PARAM) {
                        auto tp = std::dynamic_pointer_cast<TypeParameterType>(left_type);
                        auto bound_it = m_active_type_param_bounds.find(tp->name);
                        if (bound_it != m_active_type_param_bounds.end() && bound_it->second) {
                            auto mit = bound_it->second->methods.find("opAdd");
                            if (mit != bound_it->second->methods.end()) {
                                result_type = mit->second->return_type;
                            }
                        }
                    }
                    if (!result_type || result_type->kind == TypeKind::ERROR) {
                        error(expr.op, "Operator '+' can only be used with two numbers (addition) or two strings (concatenation).", "E354");
                    }
                }
                break;

            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_bool;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    result_type = m_type_bool;
                } else if (left_type->toString() == "string" && right_type->toString() == "string") {
                    result_type = m_type_bool;
                } else {
                    // LANG-13: check for user-defined opCmp method on the left type.
                    // Extract the ClassType from INSTANCE or CLASS.
                    std::shared_ptr<ClassType> cls;
                    if (left_type->kind == TypeKind::INSTANCE) {
                        auto inst = std::dynamic_pointer_cast<InstanceType>(left_type);
                        if (inst) cls = inst->class_type;
                    } else if (left_type->kind == TypeKind::CLASS) {
                        cls = std::dynamic_pointer_cast<ClassType>(left_type);
                    }
                    if (cls) {
                        const auto* op_info = cls->findProperty("opCmp");
                        if (op_info && op_info->type->kind == TypeKind::FUNCTION) {
                            auto ft = std::dynamic_pointer_cast<FunctionType>(op_info->type);
                            if (ft && ft->param_types.size() == 1 && ft->return_type->toString() == "i64") {
                                result_type = m_type_bool;
                            } else {
                                error(expr.op, "Method 'opCmp' must have signature 'func opCmp(self, other) -> i64' "
                                       "(1 parameter, returns i64).", "E420");
                            }
                        }
                    }
                    // M8: check trait-bound opCmp on TYPE_PARAM
                    if ((!result_type || result_type->kind == TypeKind::ERROR) &&
                        left_type->kind == TypeKind::TYPE_PARAM) {
                        auto tp = std::dynamic_pointer_cast<TypeParameterType>(left_type);
                        auto bound_it = m_active_type_param_bounds.find(tp->name);
                        if (bound_it != m_active_type_param_bounds.end() && bound_it->second) {
                            auto mit = bound_it->second->methods.find("opCmp");
                            if (mit != bound_it->second->methods.end()) {
                                result_type = m_type_bool;
                            }
                        }
                    }
                    if (!result_type || result_type->kind == TypeKind::ERROR) {
                        error(expr.op, "Operator '" + expr.op.lexeme + "' requires numeric or string operands, or a type "
                                       "with an 'opCmp' method, but got '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.", "E355");
                    }
                }
                break;

            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL: {
                if (left_type->kind == TypeKind::DATA && right_type->kind == TypeKind::DATA) {
                    if (sameType(left_type, right_type)) {
                        result_type = m_type_bool;
                    } else {
                        error(expr.op, "Cannot compare instances of two different data types: '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.", "E356");
                    }
                }
                else if (left_type->kind == TypeKind::ANY && right_type->kind == TypeKind::ANY) {
                    // any == any is allowed (both sides opt into dynamic comparison).
                    result_type = m_type_bool;
                }
                else if (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY) {
                    // TS-7b: comparing a typed value with `any` is unsound — the
                    // result depends on the hidden runtime type. Allowed inside an
                    // @unsafe block (the programmer takes responsibility); an error
                    // in safe code.
                    if (m_is_in_unsafe_context) {
                        result_type = m_type_bool;
                    } else {
                        error(expr.op, "Cannot compare 'any' with a typed value '" +
                                       (left_type->kind == TypeKind::ANY ? right_type : left_type)->toString() +
                                       "' — the result depends on the runtime type. Use an @unsafe block to opt in.", "E383");
                    }
                }
                else if (sameType(left_type, right_type) ||
                    left_type->kind == TypeKind::NIL || right_type->kind == TypeKind::NIL ||
                    (isNumeric(left_type) && isNumeric(right_type)))
                {
                    result_type = m_type_bool;
                }
                // LANG-13: check for user-defined opEquals method on the left type.
                else {
                    std::shared_ptr<ClassType> cls;
                    if (left_type->kind == TypeKind::INSTANCE) {
                        auto inst = std::dynamic_pointer_cast<InstanceType>(left_type);
                        if (inst) cls = inst->class_type;
                    } else if (left_type->kind == TypeKind::CLASS) {
                        cls = std::dynamic_pointer_cast<ClassType>(left_type);
                    }
                    if (cls) {
                        const auto* op_info = cls->findProperty("opEquals");
                        if (op_info && op_info->type->kind == TypeKind::FUNCTION) {
                            auto ft = std::dynamic_pointer_cast<FunctionType>(op_info->type);
                            if (ft && ft->param_types.size() == 1 && ft->return_type->toString() == "bool") {
                                result_type = m_type_bool;
                            } else {
                                error(expr.op, "Method 'opEquals' must have signature 'func opEquals(self, other) -> bool' "
                                       "(1 parameter, returns bool).", "E421");
                            }
                        }
                    }
                    if (!result_type || result_type->kind == TypeKind::ERROR) {
                        error(expr.op, "Cannot compare types '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.", "E357");
                    }
                }
                break;
            }

            default:
                error(expr.op, "Unknown binary operator '" + expr.op.lexeme + "'.", "E358");
                break;
        }

        pushAndSave(&expr, result_type);
        return {};
    }

}
