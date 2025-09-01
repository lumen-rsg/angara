#pragma once

#include "Expr.h"   // A fictional header including all AST nodes (Expr.h, Stmt.h, etc.)
#include "Stmt.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
#include <sstream>
#include <string>

namespace angara {

    class CTranspiler {
    public:
        CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler);

        /**
         * @brief The main entry point for transpilation.
         * @param statements The root of the type-checked AST.
         * @return A string containing the complete, compilable C source code.
         */
        std::string generate(const std::vector<std::shared_ptr<Stmt>>& statements);

    private:
        // --- Main Pass Methods ---
        void pass_1_generate_structs(const std::vector<std::shared_ptr<Stmt>>& statements);
        void pass_2_generate_function_declarations(const std::vector<std::shared_ptr<Stmt>>& statements);
        void pass_3_generate_function_implementations(const std::vector<std::shared_ptr<Stmt>>& statements);
        void pass_4_generate_main(const std::vector<std::shared_ptr<Stmt>>& statements);

        // --- Statement Transpilation Helpers ---
        void transpileStmt(const std::shared_ptr<Stmt>& stmt);
        void transpileVarDecl(const VarDeclStmt& stmt);
        void transpileExpressionStmt(const ExpressionStmt& stmt);
        void transpileBlock(const BlockStmt& stmt);
        void transpileIfStmt(const IfStmt& stmt);
        // ... and so on for all statement types ...

        // --- Expression Transpilation Helpers ---
        // These now return a string directly.
        std::string transpileExpr(const std::shared_ptr<Expr>& expr);
        std::string transpileLiteral(const Literal& expr);
        std::string transpileBinary(const Binary& expr);
        // ... and so on for all expression types ...

        // --- Utility Methods ---
        std::string getCType(const std::shared_ptr<Type>& angaraType);
        void indent();

    private:
        TypeChecker& m_type_checker;
        ErrorHandler& m_errorHandler;

        // We now have dedicated streams for different parts of the C file.
        std::stringstream m_structs_and_globals;
        std::stringstream m_function_declarations;
        std::stringstream m_function_implementations;
        std::stringstream m_main_body;

        // We need to track the current output stream.
        std::stringstream* m_current_out;

        int m_indent_level = 0;
        bool m_hadError = false;
    };

} // namespace angara