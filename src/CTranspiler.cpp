#include "CTranspiler.h"
#include <stdexcept>

namespace angara {

    template <typename T, typename U>
    inline bool isa(const std::shared_ptr<U>& ptr) {
        return std::dynamic_pointer_cast<const T>(ptr) != nullptr;
    }

// --- Constructor ---
    CTranspiler::CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler)
            : m_type_checker(type_checker), m_errorHandler(errorHandler), m_current_out(&m_main_body) {}

// --- Indent Helper ---
    void CTranspiler::indent() {
        for (int i = 0; i < m_indent_level; ++i) {
            (*m_current_out) << "  ";
        }
    }

// --- Main Orchestrator ---
    std::string CTranspiler::generate(const std::vector<std::shared_ptr<Stmt>>& statements) {
        pass_1_generate_structs(statements);
        pass_2_generate_function_declarations(statements);
        pass_3_generate_function_implementations(statements);
        pass_4_generate_main(statements);

        if (m_hadError) return "";

        std::stringstream final_code;
        final_code << "#include \"angara_runtime.h\"\n\n";
        final_code << m_structs_and_globals.str() << "\n";
        final_code << m_function_declarations.str() << "\n";
        final_code << m_function_implementations.str() << "\n";
        final_code << m_main_body.str() << "\n";
        return final_code.str();
    }

// --- Pass Implementations (Stubs for now) ---
    void CTranspiler::pass_1_generate_structs(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_structs_and_globals;
        // TODO: Loop and find ClassStmts to generate structs
    }

    void CTranspiler::pass_2_generate_function_declarations(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_function_declarations;
        // TODO: Loop and find FuncStmts/ClassStmts to generate function prototypes
    }

    void CTranspiler::pass_3_generate_function_implementations(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_function_implementations;
        // TODO: Loop and find FuncStmts/ClassStmts to generate function bodies
    }

    void CTranspiler::pass_4_generate_main(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_main_body;
        m_indent_level = 0;
        *m_current_out << "int main(void) {\n";
        m_indent_level++;
        for (const auto& stmt : statements) {
            if (!isa<ClassStmt>(stmt) && !isa<FuncStmt>(stmt) && !isa<TraitStmt>(stmt)) {
                transpileStmt(stmt);
            }
        }
        indent(); *m_current_out << "return 0;\n";
        m_indent_level--;
        *m_current_out << "}\n";
    }

// --- Statement Transpilation Helpers ---
    void CTranspiler::transpileStmt(const std::shared_ptr<Stmt>& stmt) {
        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            transpileVarDecl(*var_decl);
        } else if (auto expr_stmt = std::dynamic_pointer_cast<const ExpressionStmt>(stmt)) {
            transpileExpressionStmt(*expr_stmt);
        } else if (auto block = std::dynamic_pointer_cast<const BlockStmt>(stmt)) {
            transpileBlock(*block);
        }
            // ... other else if ...
        else {
            indent();
            (*m_current_out) << "/* unhandled statement */;\n";
        }
    }

    void CTranspiler::transpileVarDecl(const VarDeclStmt& stmt) {
        indent();
        auto var_type = m_type_checker.m_variable_types.at(&stmt);

        if (stmt.is_const) (*m_current_out) << "const ";
        (*m_current_out) << "AngaraObject " << stmt.name.lexeme;

        if (stmt.initializer) {
            (*m_current_out) << " = " << transpileExpr(stmt.initializer);
        } else {
            (*m_current_out) << " = create_nil()";
        }
        (*m_current_out) << ";\n";
    }

    void CTranspiler::transpileExpressionStmt(const ExpressionStmt& stmt) {
        indent();
        (*m_current_out) << transpileExpr(stmt.expression) << ";\n";
    }

    void CTranspiler::transpileBlock(const BlockStmt& stmt) {
        indent(); (*m_current_out) << "{\n";
        m_indent_level++;
        for (const auto& s : stmt.statements) {
            transpileStmt(s);
        }
        m_indent_level--;
        indent(); (*m_current_out) << "}\n";
    }

// --- Expression Transpilation Helpers ---
    std::string CTranspiler::transpileExpr(const std::shared_ptr<Expr>& expr) {
        if (auto literal = std::dynamic_pointer_cast<const Literal>(expr)) {
            return transpileLiteral(*literal);
        } else if (auto binary = std::dynamic_pointer_cast<const Binary>(expr)) {
            return transpileBinary(*binary);
        }
        // ... other else if ...
        return "/* unknown expr */";
    }

    std::string CTranspiler::transpileLiteral(const Literal& expr) {
        auto type = m_type_checker.m_expression_types.at(&expr);
        if (type->toString() == "i64") return "create_i64(" + expr.token.lexeme + "LL)";
        if (type->toString() == "f64") return "create_f64(" + expr.token.lexeme + ")";
        if (type->toString() == "bool") return "create_bool(" + expr.token.lexeme + ")";
        if (type->toString() == "string") return "angara_string_from_c(\"" + expr.token.lexeme + "\")";
        if (type->toString() == "nil") return "create_nil()";
        return "create_nil() /* unknown literal */";
    }

    std::string CTranspiler::transpileBinary(const Binary& expr) {
        // This is just a stub for now. The real one is more complex.
        return "/* binary expr */";
    }

// And stubs for all the others...
    void CTranspiler::transpileIfStmt(const IfStmt& stmt) { /* TODO */ }

} // namespace angara