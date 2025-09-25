//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara{

    std::string CTranspiler::transpileExpr(const std::shared_ptr<Expr>& expr) {
            if (auto literal = std::dynamic_pointer_cast<const Literal>(expr)) {
                return transpileLiteral(*literal);
            } else if (auto binary = std::dynamic_pointer_cast<const Binary>(expr)) {
                return transpileBinary(*binary);
            } else if (auto unary = std::dynamic_pointer_cast<const Unary>(expr)) {
                return transpileUnary(*unary);
            } else if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr)) {
                return transpileVarExpr(*var);
            } else if (auto grouping = std::dynamic_pointer_cast<const Grouping>(expr)) {
                return transpileGrouping(*grouping);
            } else if (auto logical = std::dynamic_pointer_cast<const LogicalExpr>(expr)) {
                return transpileLogical(*logical);
            } else if (auto update = std::dynamic_pointer_cast<const UpdateExpr>(expr)) {
                return transpileUpdate(*update);
            } else if (auto ternary = std::dynamic_pointer_cast<const TernaryExpr>(expr)) {
                return transpileTernary(*ternary);
            } else if (auto list = std::dynamic_pointer_cast<const ListExpr>(expr)) {
                return transpileListExpr(*list);
            } else if (auto record = std::dynamic_pointer_cast<const RecordExpr>(expr)) {
                return transpileRecordExpr(*record);
            } else if (auto call = std::dynamic_pointer_cast<const CallExpr>(expr)) {
                return transpileCallExpr(*call);
            } else if (auto assign = std::dynamic_pointer_cast<const AssignExpr>(expr)) {
                return transpileAssignExpr(*assign);
            } else if (auto get = std::dynamic_pointer_cast<const GetExpr>(expr)) {
                return transpileGetExpr(*get);
            } else if (auto call = std::dynamic_pointer_cast<const CallExpr>(expr)) {
                return transpileCallExpr(*call);
            } else if (auto this_expr = std::dynamic_pointer_cast<const ThisExpr>(expr)) {
                return transpileThisExpr(*this_expr);
            } else if (auto super = std::dynamic_pointer_cast<const SuperExpr>(expr)) {
                return transpileSuperExpr(*super);
            } else if (auto subscript  = std::dynamic_pointer_cast<const SubscriptExpr>(expr)) {
                return transpileSubscriptExpr(*subscript);
            } else if (auto is_expr = std::dynamic_pointer_cast<const IsExpr>(expr)) {
                return transpileIsExpr(*is_expr);
            } else if (auto match_expr = std::dynamic_pointer_cast<const MatchExpr>(expr)) {
                return transpileMatchExpr(*match_expr); // <-- ADD THIS
            }
            return "/* unknown expr */";
        }

}