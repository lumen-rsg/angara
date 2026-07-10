#include "TypeChecker.h"
#include "Expr.h"
#include "Type.h"

namespace angara {

    // Inline assembly. Requires an @unsafe context (E920). The template must be
    // a string literal (E921). `out`/`inout` operands must be assignable lvalues
    // (a bare VarExpr) — E922 otherwise. The optional `-> type` clause gives the
    // asm result type; without it the expression has type nil. Only integer
    // primitive results are permitted (the single output register `$0` is read
    // back as an i64).
    std::any TypeChecker::visit(const AsmExpr& expr) {
        if (!m_is_in_unsafe_context) {
            error(expr.keyword,
                  "Inline assembly is only allowed inside an '@unsafe' block — it bypasses "
                  "the type system and the borrow/escape analysis.",
                  "E920");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (expr.asmString.type != TokenType::STRING) {
            error(expr.asmString,
                  "The assembly template must be a string literal.",
                  "E921");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // Type-check each operand expression and enforce lvalue-ness on outputs.
        for (const auto& op : expr.operands) {
            op.expr->accept(*this);
            auto op_type = popType();
            if (!op_type || op_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error);
                return {};
            }
            // Inputs may be any integer-typed value.
            if (!isInteger(op_type)) {
                error(op.constraint,
                      "An asm operand must be an integer-typed value (got '" +
                      op_type->toString() + "'). Inline-asm operands are passed in registers.",
                      "E923");
                pushAndSave(&expr, m_type_error);
                return {};
            }
            if (op.dir == AsmDir::OUT || op.dir == AsmDir::INOUT) {
                // Must be an assignable lvalue: a bare variable.
                if (!dynamic_cast<const VarExpr*>(op.expr.get())) {
                    error(op.constraint,
                          "An 'out'/'inout' asm operand must be a variable (an assignable lvalue).",
                          "E922");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
            }
        }

        // Resolve the result type (default nil).
        if (expr.resultType) {
            auto rt = resolveType(expr.resultType);
            if (!rt || rt->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error);
                return {};
            }
            // The output register is read back as a machine word; restrict to
            // integer primitive types so the value flows cleanly into Angara.
            if (!isInteger(rt)) {
                error(expr.keyword,
                      "An asm result type must be an integer primitive (got '" +
                      rt->toString() + "').",
                      "E924");
                pushAndSave(&expr, m_type_error);
                return {};
            }
            pushAndSave(&expr, rt);
        } else {
            pushAndSave(&expr, m_type_nil);
        }
        return {};
    }

}
