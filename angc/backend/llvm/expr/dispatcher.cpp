#include "LLVMBackend.h"

namespace angara {

llvm::Value* LLVMBackend::codegenExpr(const std::shared_ptr<Expr>& expr) {
    if (auto e = std::dynamic_pointer_cast<const Literal>(expr))
        return codegenLiteral(*e);
    if (auto e = std::dynamic_pointer_cast<const Binary>(expr))
        return codegenBinary(*e);
    if (auto e = std::dynamic_pointer_cast<const Unary>(expr))
        return codegenUnary(*e);
    if (auto e = std::dynamic_pointer_cast<const Grouping>(expr))
        return codegenGrouping(*e);
    if (auto e = std::dynamic_pointer_cast<const VarExpr>(expr))
        return codegenVarExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const AssignExpr>(expr))
        return codegenAssignExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const UpdateExpr>(expr))
        return codegenUpdateExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const CallExpr>(expr))
        return codegenCallExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const GetExpr>(expr))
        return codegenGetExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const ListExpr>(expr))
        return codegenListExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const LogicalExpr>(expr))
        return codegenLogicalExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const SubscriptExpr>(expr))
        return codegenSubscriptExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const RecordExpr>(expr))
        return codegenRecordExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const TernaryExpr>(expr))
        return codegenTernaryExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const ThisExpr>(expr))
        return codegenThisExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const SuperExpr>(expr))
        return codegenSuperExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const IsExpr>(expr))
        return codegenIsExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const MatchExpr>(expr))
        return codegenMatchExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const SizeofExpr>(expr))
        return codegenSizeofExpr(*e);
    if (auto e = std::dynamic_pointer_cast<const RetypeExpr>(expr))
        return codegenRetypeExpr(*e);
    return createAngaraNil();
}

} // namespace angara