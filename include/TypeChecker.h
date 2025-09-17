//
// Created by cv2 on 8/31/25.
//

#pragma once

#include "Expr.h"
#include "Stmt.h"
#include "ASTTypes.h"
#include "ErrorHandler.h"
#include "SymbolTable.h"
#include "Type.h"
#include <stack>

#include "CompilerDriver.h"

namespace angara {

    struct UsedNativeSymbol {
        std::shared_ptr<ModuleType> from_module;
        std::string symbol_name;
        std::shared_ptr<Type> symbol_type;

        // For sorting and uniqueness
        bool operator<(const UsedNativeSymbol& other) const {
            if (from_module->name != other.from_module->name) return from_module->name < other.from_module->name;
            return symbol_name < other.symbol_name;
        }
    };

    class TypeChecker : public ExprVisitor, public StmtVisitor {
    public:
        TypeChecker(CompilerDriver& driver, ErrorHandler& errorHandler, std::string module_name);

        // The main entry point. Returns true if type checking passes.
        bool check(const std::vector<std::shared_ptr<Stmt>>& statements);

        std::map<const Expr*, std::shared_ptr<Type>> m_expression_types;

        SymbolTable m_symbols;
        std::map<const VarDeclStmt*, std::shared_ptr<Type>> m_variable_types;
        [[nodiscard]] const SymbolTable& getSymbolTable() const;
        [[nodiscard]] std::shared_ptr<ModuleType> getModuleType() const;
        std::map<const VarExpr*, std::shared_ptr<Symbol>> m_variable_resolutions;
        std::map<const AttachStmt*, std::shared_ptr<ModuleType>> m_module_resolutions;
        std::set<UsedNativeSymbol> m_used_native_symbols;
    private:
        // --- Visitor Methods ---
        // Statements (return void)

        std::any visit(const Literal& expr) override;
        std::any visit(const Binary& expr) override;
        std::any visit(const VarExpr& expr) override;
        std::any visit(const Unary& expr) override;
        std::any visit(const Grouping& expr) override;
        std::any visit(const ListExpr& expr) override;
        std::any visit(const AssignExpr &expr) override;
        std::any visit(const UpdateExpr &expr) override;
        std::any visit(const CallExpr &expr) override;
        std::any visit(const GetExpr &expr) override;
        std::any visit(const LogicalExpr &expr) override;
        std::any visit(const SubscriptExpr &expr) override;
        std::any visit(const RecordExpr &expr) override;
        std::any visit(const TernaryExpr &expr) override;
        std::any visit(const ThisExpr &expr) override;
        std::any visit(const SuperExpr &expr) override;

        void visit(std::shared_ptr<const ContractStmt> stmt);

        void defineContractHeader(const ContractStmt &stmt);

        void resolveAttach(const AttachStmt &stmt);

        void visit(std::shared_ptr<const VarDeclStmt> stmt) override;
        void visit(std::shared_ptr<const IfStmt> stmt) override;
        void visit(std::shared_ptr<const EmptyStmt> stmt) override;
        void visit(std::shared_ptr<const WhileStmt> stmt) override;
        void visit(std::shared_ptr<const ForStmt> stmt) override;
        void visit(std::shared_ptr<const ForInStmt> stmt) override;
        void visit(std::shared_ptr<const FuncStmt> stmt) override;
        void visit(std::shared_ptr<const ReturnStmt> stmt) override;
        void visit(std::shared_ptr<const AttachStmt> stmt) override;
        void visit(std::shared_ptr<const ThrowStmt> stmt) override;
        void visit(std::shared_ptr<const TryStmt> stmt) override;
        void visit(std::shared_ptr<const ClassStmt> stmt) override;
        void visit(std::shared_ptr<const TraitStmt> stmt) override;
        void visit(std::shared_ptr<const ExpressionStmt> stmt) override;
        void visit(std::shared_ptr<const BlockStmt> stmt) override;


        // --- Helper Methods ---
        // A helper to get the canonical Type from an ASTType node
        std::shared_ptr<Type> resolveType(const std::shared_ptr<ASTType>& ast_type);

        static bool isNumeric(const std::shared_ptr<Type>& type);
        std::shared_ptr<Type> popType();

        // Error reporting
        void error(const Token& token, const std::string& message);

        void note(const Token &token, const std::string &message);

        bool m_is_in_trait = false;



    private:
        ErrorHandler& m_errorHandler;

        // We use a stack to pass type information up from expressions.
        std::stack<std::shared_ptr<Type>> m_type_stack;

        bool m_hadError = false;
        int m_loop_depth = 0;

        // Pre-create canonical primitive types to avoid repeated allocations
        std::shared_ptr<Type> m_type_i8, m_type_i16, m_type_i32, m_type_i64;
        std::shared_ptr<Type> m_type_u8, m_type_u16, m_type_u32, m_type_u64;
        std::shared_ptr<Type> m_type_f32, m_type_f64;
        std::shared_ptr<Type> m_type_bool;
        std::shared_ptr<Type> m_type_string;
        std::shared_ptr<Type> m_type_nil;
        std::shared_ptr<Type> m_type_any;
        std::shared_ptr<Type> m_type_error;
        std::shared_ptr<Type> m_type_thread;
        std::shared_ptr<Type> m_type_mutex;
        std::shared_ptr<Type> m_type_exception;
        CompilerDriver& m_driver;
        std::shared_ptr<ModuleType> m_module_type;
        std::stack<std::shared_ptr<Type>> m_function_return_types;
        std::shared_ptr<ClassType> m_current_class = nullptr;


        void defineClassHeader(const ClassStmt &stmt);
        void defineFunctionHeader(const FuncStmt &stmt);
        void defineTraitHeader(const TraitStmt &stmt);

        static bool isInteger(const std::shared_ptr<Type> &type);

        static bool isUnsignedInteger(const std::shared_ptr<Type> &type);

        static bool isFloat(const std::shared_ptr<Type> &type);

        void pushAndSave(const Expr *expr, std::shared_ptr<Type> type);

        static bool isTruthy(const std::shared_ptr<Type> &type);

        void visit(std::shared_ptr<const BreakStmt> stmt);
        std::map<const Symbol*, std::shared_ptr<Type>> m_narrowed_types;

        std::shared_ptr<Symbol> resolve_and_narrow(const VarExpr &expr);

        std::any visit(const IsExpr &expr);
    };

} // namespace angara
