#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const AwaitExpr& expr) {
    // LIB-4: await is only valid inside async functions.
    if (!m_in_async_function) {
        error(expr.keyword, "'await' can only be used inside an 'async func' body.", "E419");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // Type-check the future expression.
    expr.future->accept(*this);
    auto future_type = popType();

    if (!future_type || future_type->kind == TypeKind::ERROR) {
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // The awaited expression must be a Future<T>.
    if (future_type->kind != TypeKind::FUTURE) {
        error(expr.keyword,
              "Cannot await a value of type '" + future_type->toString() +
              "'. 'await' requires a 'Future<T>'.",
              "E420");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // The result type is the inner type T of Future<T>.
    auto future = std::dynamic_pointer_cast<FutureType>(future_type);
    pushAndSave(&expr, future->inner_type);
    return {};
    }

}
