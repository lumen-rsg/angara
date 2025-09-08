#pragma once

#include "Expr.h"   // A fictional header including all AST nodes (Expr.h, Stmt.h, etc.)
#include "Stmt.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include <sstream>
#include <string>

namespace angara {

    using TranspileResult = std::pair<std::string, std::string>;

    class CTranspiler {
    public:
        CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler);

        /**
         * @brief The main entry point for transpilation.
         * @param statements The root of the type-checked AST.
         * @return A string containing the complete, compilable C source code.
         */

        TranspileResult generate(const std::vector<std::shared_ptr<Stmt>>& statements, const std::string& module_name, std::vector<std::string>& all_module_names);

    private:
        // --- Main Pass Methods ---
        void pass_1_generate_structs(const std::vector<std::shared_ptr<Stmt>>& statements);

        void transpileMethodSignature(const std::string &class_name, const FuncStmt &stmt);

        void transpileMethodBody(const ClassType &klass, const FuncStmt &stmt);

        void pass_2_generate_declarations(const std::vector<std::shared_ptr<Stmt>>& statements, const std::string& module_name);

        void pass_3_generate_globals_and_implementations(const std::vector<std::shared_ptr<Stmt>> &statements,
                                                         const std::string &module_name);

        void transpileClassNew(const ClassStmt &stmt);

        void pass_4_generate_function_implementations(const std::vector<std::shared_ptr<Stmt>>& statements);

        void transpileStruct(const ClassStmt &stmt);

        void pass_3_generate_globals(const std::vector<std::shared_ptr<Stmt>> &statements,
                                     const std::string &module_name);

        void pass_5_generate_main(const std::vector<std::shared_ptr<Stmt>>& statements,
                          const std::string& module_name,
                          const std::vector<std::string>& all_module_names); // <-- ADD PARAMETER

        // --- Statement Transpilation Helpers ---
        void transpileStmt(const std::shared_ptr<Stmt>& stmt);
        void transpileVarDecl(const VarDeclStmt& stmt);
        void transpileExpressionStmt(const ExpressionStmt& stmt);
        void transpileBlock(const BlockStmt& stmt);

        std::string transpileVarExpr(const VarExpr &expr);

        void transpileIfStmt(const IfStmt& stmt);

        void transpileWhileStmt(const WhileStmt &stmt);

        void transpileForStmt(const ForStmt &stmt);

        void transpileReturnStmt(const ReturnStmt &stmt);

        std::string transpileCallExpr(const CallExpr &expr);

        std::string transpileAssignExpr(const AssignExpr &expr);

        const FuncStmt *findMethodAst(const ClassStmt &class_stmt, const std::string &name);

        std::string transpileGetExpr(const GetExpr &expr);

        std::string transpileThisExpr(const ThisExpr &expr);

        void transpileThrowStmt(const ThrowStmt &stmt);

        void transpileTryStmt(const TryStmt &stmt);

        void transpileForInStmt(const ForInStmt &stmt);


        // --- Expression Transpilation Helpers ---
        // These now return a string directly.
        std::string transpileExpr(const std::shared_ptr<Expr>& expr);

        std::string transpileSuperExpr(const SuperExpr &expr);

        std::string transpileLiteral(const Literal& expr);
        std::string transpileBinary(const Binary& expr);

        std::string transpileGrouping(const Grouping &expr);

        std::string transpileLogical(const LogicalExpr &expr);

        std::string transpileUpdate(const UpdateExpr& expr);

        std::string transpileTernary(const TernaryExpr &expr);

        std::string transpileUnary(const Unary &expr);

        std::string transpileListExpr(const ListExpr &expr);

        std::string transpileRecordExpr(const RecordExpr &expr);
        void transpileGlobalFunction(const FuncStmt& stmt, const std::string& module_name);

        // ... and so on for all expression types ...

        // --- Utility Methods ---
        std::string getCType(const std::shared_ptr<Type>& angaraType);
        void indent();

        void transpileFunctionSignature(const FuncStmt &stmt, const std::string& module_name);
        const ClassType* findPropertyOwner(const ClassType* klass, const std::string& prop_name);

    private:
        TypeChecker& m_type_checker;
        ErrorHandler& m_errorHandler;

        // We now have dedicated streams for different parts of the C file.
        std::stringstream m_structs_and_globals;
        std::stringstream m_function_declarations;
        std::stringstream m_function_implementations;
        std::stringstream m_main_body;

        std::stringstream m_header_out; // For the .h file
        std::stringstream m_source_out; // For the .c file

        // We need to track the current output stream.
        std::stringstream* m_current_out;

        int m_indent_level = 0;
        bool m_hadError = false;

        // Tracks the name of the class whose methods we are currently transpiling.
        // An empty string means we are in the global scope.
        std::string m_current_class_name;
        std::string m_current_module_name;
    };

} // namespace angara