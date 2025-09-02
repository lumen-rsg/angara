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

    std::string CTranspiler::getCType(const std::shared_ptr<Type>& angaraType) {
        if (!angaraType) {
            return "void /* unknown type */";
        }

        // For any type that isn't explicitly void, the C representation
        // is the AngaraObject struct. The specific type information is stored
        // inside that struct's 'type' tag.
        if (angaraType->toString() == "void") {
            return "void";
        }

        return "AngaraObject";
    }

    // --- Helper to transpile a function signature ---
    // (Add its declaration to CTranspiler.h)
    void CTranspiler::transpileFunctionSignature(const FuncStmt& stmt) {
        // --- THIS IS THE FIX ---
        // 1. Get the function's symbol from the symbol table using its name.
        auto symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
        if (!symbol || symbol->type->kind != TypeKind::FUNCTION) {
            // This should be impossible if the Type Checker passed.
            // It indicates a bug in our compiler logic.
            std::cerr << "/ ERROR: Could not resolve function type for " << stmt.name.lexeme << " */";
            return;
        }
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
        // --- END OF FIX ---

        *m_current_out << getCType(func_type->return_type) << " " << stmt.name.lexeme << "(";
        for (size_t i = 0; i < stmt.params.size(); ++i) {
            // We get the C type from the resolved FunctionType, but the parameter
            // name from the AST.
            *m_current_out << getCType(func_type->param_types[i]) << " " << stmt.params[i].name.lexeme;
            if (i < stmt.params.size() - 1) {
                *m_current_out << ", ";
            }
        }
        *m_current_out << ")";
    }


    void CTranspiler::pass_2_generate_function_declarations(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_function_declarations;
        m_indent_level = 0;

        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                // Generate the prototype, e.g., "AngaraObject add(AngaraObject, AngaraObject);"
                transpileFunctionSignature(*func_stmt);
                *m_current_out << ";\n";
            }
        }
    }

    void CTranspiler::pass_3_generate_function_implementations(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_function_implementations;
        m_indent_level = 0;

        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                // 1. Generate the same signature as the forward declaration.
                transpileFunctionSignature(*func_stmt);
                *m_current_out << " {\n";
                m_indent_level++;

                // 2. Transpile the body.
                if (func_stmt->body) {
                    for (const auto& body_stmt : *func_stmt->body) {
                        transpileStmt(body_stmt);
                    }
                }

                // 3. Handle implicit returns. The Type Checker guarantees that a non-void
                //    function must have an explicit return, so we only need to handle
                //    the case where a void function might not have one.
                auto symbol = m_type_checker.m_symbols.resolve(func_stmt->name.lexeme);
                auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
                if (func_type->return_type->toString() == "void") {
                    // Check if the last statement was a return. This is complex.
                    // For now, we can just add a return, it's often harmless.
                    indent();
                    *m_current_out << "return;\n";
                }


                m_indent_level--;
                *m_current_out << "}\n\n";
            }
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
        } else if (auto if_stmt = std::dynamic_pointer_cast<const IfStmt>(stmt)) {
            transpileIfStmt(*if_stmt);
        } else if (auto while_stmt = std::dynamic_pointer_cast<const WhileStmt>(stmt)) {
            transpileWhileStmt(*while_stmt);
        } else if (auto for_stmt = std::dynamic_pointer_cast<const ForStmt>(stmt)) {
            transpileForStmt(*for_stmt);
        } else if (auto ret_stmt = std::dynamic_pointer_cast<const ReturnStmt>(stmt)) {
            transpileReturnStmt(*ret_stmt);
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
        } else if (auto unary = std::dynamic_pointer_cast<const Unary>(expr)) {
            return transpileUnary(*unary);
        } else if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr)) {
            return var->name.lexeme;
        } else if (auto grouping = std::dynamic_pointer_cast<const Grouping>(expr)) {
            return transpileGrouping(*grouping);
        } else if (auto logical = std::dynamic_pointer_cast<const LogicalExpr>(expr)) {
            return transpileLogical(*logical);
        } else if (auto update = std::dynamic_pointer_cast<const UpdateExpr>(expr)) {
            return transpileUpdate(*update);
        } else if (auto ternary = std::dynamic_pointer_cast<const TernaryExpr>(expr)) {
            return transpileTernary(*ternary);
        }
        else if (auto list = std::dynamic_pointer_cast<const ListExpr>(expr)) { // <-- ADD THIS
            return transpileListExpr(*list);
        } else if (auto record = std::dynamic_pointer_cast<const RecordExpr>(expr)) { // <-- ADD THIS
            return transpileRecordExpr(*record);
        } else if (auto call = std::dynamic_pointer_cast<const CallExpr>(expr)) {
            return transpileCallExpr(*call);
        } else if (auto assign = std::dynamic_pointer_cast<const AssignExpr>(expr)) {
            return transpileAssignExpr(*assign);
        }
        // ... other else if for Call, Get, etc. ...
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
// in CTranspiler.cpp

std::string CTranspiler::transpileBinary(const Binary& expr) {
    // 1. Get the pre-computed types of the operands from the Type Checker.
    auto lhs_type = m_type_checker.m_expression_types.at(expr.left.get());
    auto rhs_type = m_type_checker.m_expression_types.at(expr.right.get());

    // 2. Recursively transpile the left and right sub-expressions.
    std::string lhs_str = transpileExpr(expr.left);
    std::string rhs_str = transpileExpr(expr.right);

    std::string op = expr.op.lexeme;

    // --- Case 1: Numeric Operations ---
    if (isNumeric(lhs_type) && isNumeric(rhs_type)) {
        switch (expr.op.type) {
            // --- Comparisons ---
            // The result is always a C `bool`, which we re-box into an AngaraObject.
            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL:
                // We promote both operands to a C double for a safe comparison
                // using our smart AS_F64 macro.
                return "create_bool((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";

            // --- Arithmetic ---
            // The result is a number, so we wrap it in the appropriate create_* function.
            case TokenType::PLUS:
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
                if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    std::string expression = "(AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + "))";
                    return "create_f64(" + expression + ")";
                } else {
                    std::string expression = "(AS_I64(" + lhs_str + ") " + op + " AS_I64(" + rhs_str + "))";
                    return "create_i64(" + expression + ")";
                }

            case TokenType::PERCENT:
                 if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    std::string expression = "fmod(AS_F64(" + lhs_str + "), AS_F64(" + rhs_str + "))";
                    return "create_f64(" + expression + ")";
                } else {
                    std::string expression = "(AS_I64(" + lhs_str + ") % AS_I64(" + rhs_str + "))";
                    return "create_i64(" + expression + ")";
                }
            default:
                break; // Should not be reached
        }
    }

    // --- Case 2: String Operations ---
    if (lhs_type->toString() == "string" && rhs_type->toString() == "string") {
        if (expr.op.type == TokenType::PLUS) {
            return "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
        }
        if (expr.op.type == TokenType::EQUAL_EQUAL) {
             return "angara_string_equals(" + lhs_str + ", " + rhs_str + ")";
        }
         if (expr.op.type == TokenType::BANG_EQUAL) {
             return "create_bool(!AS_BOOL(angara_string_equals(" + lhs_str + ", " + rhs_str + ")))";
        }
    }

    // Fallback for any unhandled or invalid binary operations.
    return "create_nil() /* unsupported binary op */";
}

    std::string CTranspiler::transpileGrouping(const Grouping& expr) {
        // A grouping expression in Angara is also a grouping expression in C.
        return "(" + transpileExpr(expr.expression) + ")";
    }

    std::string CTranspiler::transpileLogical(const LogicalExpr& expr) {
        // The Type Checker has already verified that the operands are "truthy".
        // C's `&&` and `||` operators have the correct short-circuiting behavior.
        std::string lhs = "angara_is_truthy(" + transpileExpr(expr.left) + ")";
        std::string rhs = "angara_is_truthy(" + transpileExpr(expr.right) + ")";
        std::string op = expr.op.lexeme;

        // The result of the C expression is a C bool (0 or 1), which we must
        // re-box into an AngaraObject.
        return "create_bool((" + lhs + ") " + op + " (" + rhs + "))";
    }

    std::string CTranspiler::transpileUpdate(const UpdateExpr& expr) {
        std::string target_str = transpileExpr(expr.target);
        std::string op = expr.op.lexeme; // "++" or "--"

        // This is complex because the result of the C expression (e.g., `i++`) is a raw
        // number, but we need an AngaraObject. A C helper function is the cleanest way.

        // For now, let's assume the expression is not used for its value,
        // which is the common case in `for` loops. This is a simplification.

        // TODO: refine with AngaraObject approach.

        if (expr.isPrefix) {
            return "(" + op + target_str + ")";
        } else {
            return "(" + target_str + op + ")";
        }
    }

    std::string CTranspiler::transpileTernary(const TernaryExpr& expr) {
        std::string cond_str = "angara_is_truthy(" + transpileExpr(expr.condition) + ")";
        std::string then_str = transpileExpr(expr.thenBranch);
        std::string else_str = transpileExpr(expr.elseBranch);

        // C's ternary operator is a perfect match.
        return "(" + cond_str + " ? " + then_str + " : " + else_str + ")";
    }

    std::string CTranspiler::transpileUnary(const Unary& expr) {
        // 1. Get the pre-computed type of the operand from the Type Checker's results.
        //    This is crucial for knowing whether to generate integer or float negation.
        auto operand_type = m_type_checker.m_expression_types.at(expr.right.get());

        // 2. Recursively transpile the operand expression.
        std::string operand_str = transpileExpr(expr.right);

        switch (expr.op.type) {
            case TokenType::BANG: {
                // Transpiles `!some_expression`.
                // The logic is: get the truthiness of the Angara object, invert the
                // resulting C bool, and then re-box it as a new Angara bool object.
                return "create_bool(!angara_is_truthy(" + operand_str + "))";
            }

            case TokenType::MINUS: {
                // Transpiles `-some_expression`.
                // The Type Checker has already guaranteed the operand is a number.
                // We just need to generate the correct C code for its specific type.
                if (isFloat(operand_type)) {
                    // Unbox as a double, negate, and re-box as a new f64 AngaraObject.
                    return "create_f64(-AS_F64(" + operand_str + "))";
                } else {
                    // Unbox as an int64, negate, and re-box as a new i64 AngaraObject.
                    return "create_i64(-AS_I64(" + operand_str + "))";
                }
            }

            default:
                // This should be unreachable if the parser and type checker are correct.
                    return "create_nil() /* unsupported unary op */";
        }
    }

    std::string CTranspiler::transpileListExpr(const ListExpr& expr) {
        // Generates: angara_list_new_with_elements(count, (AngaraObject[]){...})

        // 1. Transpile all the element expressions first.
        std::stringstream elements_ss;
        for (size_t i = 0; i < expr.elements.size(); ++i) {
            elements_ss << transpileExpr(expr.elements[i]);
            if (i < expr.elements.size() - 1) {
                elements_ss << ", ";
            }
        }

        // 2. Assemble the call to the runtime constructor function.
        return "angara_list_new_with_elements(" +
               std::to_string(expr.elements.size()) + ", " +
               "(AngaraObject[]){" + elements_ss.str() + "})";
    }


    // --- NEW HELPER: transpileRecordExpr ---
    // (Add its declaration to CTranspiler.h)
    std::string CTranspiler::transpileRecordExpr(const RecordExpr& expr) {
        // Generates: angara_record_new_with_fields(count, (AngaraObject[]){"key1", val1, "key2", val2, ...})

        // 1. Transpile all key/value pairs.
        std::stringstream kvs_ss;
        for (size_t i = 0; i < expr.keys.size(); ++i) {
            // Key (always a C string)
            kvs_ss << "angara_string_from_c(\"" << expr.keys[i].lexeme << "\")";
            kvs_ss << ", ";
            // Value (recursively transpiled expression)
            kvs_ss << transpileExpr(expr.values[i]);

            if (i < expr.keys.size() - 1) {
                kvs_ss << ", ";
            }
        }

        // 2. Assemble the call to the runtime constructor function.
        return "angara_record_new_with_fields(" +
               std::to_string(expr.keys.size()) + ", " +
               "(AngaraObject[]){" + kvs_ss.str() + "})";
    }

// And stubs for all the others...
    void CTranspiler::transpileIfStmt(const IfStmt& stmt) {
    // 1. Transpile the condition expression and wrap it in our truthiness check.
    std::string condition_str = "angara_is_truthy(" + transpileExpr(stmt.condition) + ")";

    indent();
    (*m_current_out) << "if (" << condition_str << ") ";

    // 2. Transpile the 'then' block.
    transpileStmt(stmt.thenBranch);

    // 3. Recursively handle 'else' and 'orif' (else if) branches.
    if (stmt.elseBranch) {
        indent();
        (*m_current_out) << "else ";
        // The parser structure `else if` into a nested IfStmt, so we just
        // transpile it as a generic statement and our dispatcher will call
        // this function again.
        transpileStmt(stmt.elseBranch);
    }
}

void CTranspiler::transpileWhileStmt(const WhileStmt& stmt) {
    std::string condition_str = "angara_is_truthy(" + transpileExpr(stmt.condition) + ")";

    indent();
    (*m_current_out) << "while (" << condition_str << ") ";

    // Transpile the body of the loop.
    transpileStmt(stmt.body);
}

    void CTranspiler::transpileForStmt(const ForStmt& stmt) {
        indent();
        // In C, the scope of a for-loop initializer is the loop itself.
        // So we don't need an extra `{}` block unless the body isn't one.
        (*m_current_out) << "for (";

        // --- 1. Initializer ---
        if (stmt.initializer) {
            // The initializer is a full statement. We need to generate its code
            // but without the trailing semicolon and newline. We can achieve this
            // by temporarily redirecting the output stream.
            std::stringstream init_ss;
            std::stringstream* temp_out = m_current_out;
            m_current_out = &init_ss;
            m_indent_level = 0; // No indent inside the for()

            transpileStmt(stmt.initializer);

            // Restore original stream and level
            m_current_out = temp_out;
            m_indent_level = 1; // Assuming we are inside main

            std::string init_str = init_ss.str();
            // Trim whitespace, semicolon, and newline from the end
            size_t last = init_str.find_last_not_of(" \n\r\t;");
            (*m_current_out) << (last == std::string::npos ? "" : init_str.substr(0, last + 1));
        }
        (*m_current_out) << "; ";

        // --- 2. Condition ---
        if (stmt.condition) {
            (*m_current_out) << "angara_is_truthy(" << transpileExpr(stmt.condition) << ")";
        }
        (*m_current_out) << "; ";

        // --- 3. Increment ---
        if (stmt.increment) {
            (*m_current_out) << transpileExpr(stmt.increment);
        }
        (*m_current_out) << ") ";

        // --- 4. Body ---
        transpileStmt(stmt.body);
    }

    void CTranspiler::transpileReturnStmt(const ReturnStmt& stmt) {
        indent();
        (*m_current_out) << "return";
        if (stmt.value) {
            (*m_current_out) << " " << transpileExpr(stmt.value);
        }
        (*m_current_out) << ";\n";
    }

    std::string CTranspiler::transpileCallExpr(const CallExpr& expr) {
        auto callee_str = transpileExpr(expr.callee);

        // Handle our built-in functions
        if (callee_str == "print") {
            std::stringstream args_ss;
            args_ss << "(AngaraObject[]){";
            for (size_t i = 0; i < expr.arguments.size(); ++i) {
                args_ss << transpileExpr(expr.arguments[i]);
                if (i < expr.arguments.size() - 1) args_ss << ", ";
            }
            args_ss << "}";
            return "angara_print(" + std::to_string(expr.arguments.size()) + ", " + args_ss.str() + ")";
        }
        if (callee_str == "len") {
            return "angara_len(" + transpileExpr(expr.arguments[0]) + ")";
        }
        if (callee_str == "typeof") {
            // typeof returns a string, which needs to be wrapped in an AngaraObject
            return "angara_string_from_c(" + transpileExpr(expr.arguments[0]) + "/* TODO: typeof result */)";
        }

        // Transpile arguments for a regular function call
        std::string args_str;
        for (size_t i = 0; i < expr.arguments.size(); ++i) {
            args_str += transpileExpr(expr.arguments[i]);
            if (i < expr.arguments.size() - 1) args_str += ", ";
        }

        return callee_str + "(" + args_str + ")";
    }

    std::string CTranspiler::transpileAssignExpr(const AssignExpr& expr) {
        auto rhs_str = transpileExpr(expr.value);
        auto lhs_str = transpileExpr(expr.target);
        // TODO: Handle compound assignment like '+='
        return "(" + lhs_str + " = " + rhs_str + ")";
    }

} // namespace angara