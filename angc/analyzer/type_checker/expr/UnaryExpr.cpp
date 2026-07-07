#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Unary& expr) {
        expr.right->accept(*this);
        auto right_type = popType();

        std::shared_ptr<Type> result_type = m_type_error;
        switch (expr.op.type) {
            case TokenType::MINUS:
                if (isNumeric(right_type)) result_type = right_type;
                else {
                    // LANG-13: check for user-defined opNeg method on the type.
                    std::shared_ptr<ClassType> cls;
                    if (right_type->kind == TypeKind::INSTANCE) {
                        auto inst = std::dynamic_pointer_cast<InstanceType>(right_type);
                        if (inst) cls = inst->class_type;
                    } else if (right_type->kind == TypeKind::CLASS) {
                        cls = std::dynamic_pointer_cast<ClassType>(right_type);
                    }
                    if (cls) {
                        const auto* op_info = cls->findProperty("opNeg");
                        if (op_info && op_info->type->kind == TypeKind::FUNCTION) {
                            auto ft = std::dynamic_pointer_cast<FunctionType>(op_info->type);
                            if (ft && ft->param_types.empty()) {
                                result_type = ft->return_type;
                            } else {
                                error(expr.op, "Method 'opNeg' must have signature 'func opNeg(self) -> T' "
                                       "(no parameters).", "E432");
                            }
                        }
                    }
                    if (!result_type || result_type->kind == TypeKind::ERROR) {
                        error(expr.op, "Operator '-' requires a numeric operand, but got '" + right_type->toString() + "'.", "E359");
                    }
                }
                break;
            case TokenType::BANG:
                if (sameType(right_type, m_type_bool)) result_type = m_type_bool;
                else error(expr.op, "Operator '!' requires a boolean operand, but got '" + right_type->toString() + "'.", "E360");
                break;
            case TokenType::TILDE:
                if (isNumeric(right_type)) result_type = m_type_i64;
                else error(expr.op, "Operator '~' requires a numeric operand, but got '" + right_type->toString() + "'.", "E361");
                break;
            default: ;
        }
        pushAndSave(&expr, result_type);
        return {};
    }

}
