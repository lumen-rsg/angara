//
// Created by cv2 on 9/1/25.
//

#include "CTranspiler.h"
#include <stdexcept>

namespace angara {

    CTranspiler::CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler)
            : m_type_checker(type_checker), m_errorHandler(errorHandler) {}

    template <typename T, typename U>
    bool isa(const std::shared_ptr<U>& ptr) {
        return std::dynamic_pointer_cast<const T>(ptr) != nullptr;
    }


//    template <typename T>
//    bool isa(const std::shared_ptr<const Expr>& expr) {
//     return std::dynamic_pointer_cast<const T>(expr) != nullptr;
//    }

    void CTranspiler::indent() {
        for (int i = 0; i < m_indent_level; ++i) {
            m_out << "  "; // 2 spaces per indent level
        }
    }

    std::string CTranspiler::generate(const std::vector<std::shared_ptr<Stmt>>& statements) {
        // Preamble for the C file
        m_globals << "#include \"angara_runtime.h\"\n\n";

        // --- Pass 1: Generate all top-level definitions ---
        for (const auto& stmt : statements) {
            // --- THIS IS THE FIX ---
            // Pass the shared_ptr 'stmt' directly, not the dereferenced '*stmt'.
            if (isa<ClassStmt>(stmt) || isa<FuncStmt>(stmt)) {
                stmt->accept(*this, stmt);
            }
            // --- END OF FIX ---
        }

        // --- Pass 2: Generate the code for the main function ---
        m_out << "\nint main(void) {\n";
        m_indent_level++;
        for (const auto& stmt : statements) {
            // --- APPLY THE SAME FIX HERE ---
            if (!isa<ClassStmt>(stmt) && !isa<FuncStmt>(stmt) && !isa<TraitStmt>(stmt)) {
                stmt->accept(*this, stmt);
            }
        }
        indent();
        m_out << "return 0;\n";
        m_indent_level--;
        m_out << "}\n";

        // Combine the global definitions with the main function code.
        return m_globals.str() + m_out.str();
    }

// --- Helper to get C type names ---

    std::string CTranspiler::getCType(const std::shared_ptr<Type>& angaraType) {
        if (angaraType->toString() == "void") {
            return "void";
        }
        // For every other type, the variable holding it in C is of type AngaraObject.
        return "AngaraObject";
    }

    void CTranspiler::transpileListDeclaration(const VarDeclStmt& stmt, const ListExpr& list_expr) {
        // Generates:
        // AngaraObject myList = angara_list_new();
        // {
        //   angara_list_push(myList, ...);
        //   angara_list_push(myList, ...);
        // }

        indent();
        m_out << "AngaraObject " << stmt.name.lexeme << " = angara_list_new();\n";

        indent();
        m_out << "{\n"; // Use a block to contain temporary element variables if needed
        m_indent_level++;

        for (const auto& element_expr : list_expr.elements) {
            indent();
            auto element_str = std::any_cast<std::string>(element_expr->accept(*this));
            m_out << "angara_list_push(" << stmt.name.lexeme << ", " << element_str << ");\n";
        }

        m_indent_level--;
        indent();
        m_out << "}\n";
    }

    std::string CTranspiler::transpileConstructorCall(const ClassType& class_type, const std::vector<std::shared_ptr<Expr>>& args) {
        std::string c_struct_name = "Angara_" + class_type.name;

        std::stringstream ss;
        // 1. Allocate the instance
        ss << "((AngaraObject){VAL_OBJ, {.obj = init_instance(sizeof(" << c_struct_name << "), &g_" << class_type.name << ")}})";

        // We need to store this in a temporary, call init, and then use the temp.
        // This requires a more advanced transpiler that can manage temporary variables.
        // TODO: refine

        // Simpler approach for now:
        std::string init_args;
        for (const auto& arg : args) {
            init_args += ", " + std::any_cast<std::string>(arg->accept(*this));
        }

        return "Angara_" + class_type.name + "_new(" + init_args.substr(2) + ")";
    }

    void CTranspiler::transpileRecordDeclaration(const VarDeclStmt& stmt, const RecordExpr& record_expr) {
        // Generates:
        // AngaraObject myRecord = angara_record_new();
        // {
        //   angara_record_set(myRecord, "key1", ...);
        //   angara_record_set(myRecord, "key2", ...);
        // }

        indent();
        m_out << "AngaraObject " << stmt.name.lexeme << " = angara_record_new();\n";

        indent();
        m_out << "{\n";
        m_indent_level++;

        for (size_t i = 0; i < record_expr.keys.size(); ++i) {
            indent();
            const std::string& key_name = record_expr.keys[i].lexeme;
            auto value_str = std::any_cast<std::string>(record_expr.values[i]->accept(*this));

            m_out << "angara_record_set(" << stmt.name.lexeme << ", "
                  << "\"" << key_name << "\", " // Keys are C strings
                  << value_str << ");\n";
        }

        m_indent_level--;
        indent();
        m_out << "}\n";
    }

    std::any CTranspiler::visit(const RecordExpr& expr) {
        // Similar to ListExpr, we only support this in variable declarations for now.
        return "/* complex record literal not yet supported here */";
    }


// --- VISITORS ---

// --- Literal: Now creates runtime objects ---
    std::any CTranspiler::visit(const Literal& expr) {
        auto type = m_type_checker.m_expression_types.at(&expr);

        if (type->toString() == "i64") {
            return "create_i64(" + expr.token.lexeme + "LL)";
        }
        if (type->toString() == "f64") {
            return "create_f64(" + expr.token.lexeme + ")";
        }
        if (type->toString() == "bool") {
            return "create_bool(" + expr.token.lexeme + ")";
        }
        if (type->toString() == "string") {
            return "angara_string_from_c(\"" + expr.token.lexeme + "\")";
        }
        if (type->toString() == "nil") {
            return "create_nil()";
        }
        return "create_nil() /* unknown literal */";
    }

    void CTranspiler::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        // --- NEW: Special handling for list/record initializers ---
        if (stmt->initializer) {
            if (auto list_expr = std::dynamic_pointer_cast<const ListExpr>(stmt->initializer)) {
                transpileListDeclaration(*stmt, *list_expr);
                return;
            }
            if (auto record_expr = std::dynamic_pointer_cast<const RecordExpr>(stmt->initializer)) {
                transpileRecordDeclaration(*stmt, *record_expr);
                return;
            }
        }
        // --- END OF NEW ---

        // --- Fallback to existing logic for simple initializers ---
        indent();
        auto symbol = m_type_checker.m_symbols.resolve(stmt->name.lexeme);
        auto c_type = getCType(symbol->type);

        if (stmt->is_const) m_out << "const ";
        m_out << c_type << " " << stmt->name.lexeme;

        if (stmt->initializer) {
            m_out << " = " << std::any_cast<std::string>(stmt->initializer->accept(*this));
        } else {
            m_out << " = create_nil()";
        }
        m_out << ";\n";
    }

    void CTranspiler::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        std::cout << "[ ang::comp ] :: visiting ExpressionStmt: " << std::endl;
        indent();
        auto expr_str = std::any_cast<std::string>(stmt->expression->accept(*this));
        m_out << expr_str << ";\n";
        std::cout << "[ ang::comp ] :: closing ExpressionStmt. " << std::endl;
    }

    std::any CTranspiler::visit(const Binary& expr) {
        auto lhs_type = m_type_checker.m_expression_types.at(expr.left.get());
        auto rhs_type = m_type_checker.m_expression_types.at(expr.right.get());

        auto lhs_str = std::any_cast<std::string>(expr.left->accept(*this));
        auto rhs_str = std::any_cast<std::string>(expr.right->accept(*this));

        // --- THIS IS THE FIX ---

        // Handle numeric operations
        if (isNumeric(lhs_type) && isNumeric(rhs_type)) {
            std::string op = expr.op.lexeme;

            switch (expr.op.type) {
                // Comparisons: The result is always a C `bool`, re-boxed into an AngaraObject.
                case TokenType::GREATER:
                case TokenType::GREATER_EQUAL:
                case TokenType::LESS:
                case TokenType::LESS_EQUAL:
                case TokenType::EQUAL_EQUAL:
                case TokenType::BANG_EQUAL:
                    return "create_bool((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";

                    // Arithmetic: The result is a number, re-boxed.
                case TokenType::PLUS:
                case TokenType::MINUS:
                case TokenType::STAR:
                case TokenType::SLASH: {
                    if (isFloat(lhs_type) || isFloat(rhs_type)) {
                        std::string expression = "(AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + "))";
                        return "create_f64(" + expression + ")";
                    } else {
                        std::string expression = "(AS_I64(" + lhs_str + ") " + op + " AS_I64(" + rhs_str + "))";
                        return "create_i64(" + expression + ")";
                    }
                }
                case TokenType::PERCENT: {
                    if (isFloat(lhs_type) || isFloat(rhs_type)) {
                        std::string expression = "fmod(AS_F64(" + lhs_str + "), AS_F64(" + rhs_str + "))";
                        return "create_f64(" + expression + ")";
                    } else {
                        std::string expression = "(AS_I64(" + lhs_str + ") % AS_I64(" + rhs_str + "))";
                        return "create_i64(" + expression + ")";
                    }
                }
                default: break;
            }
        }

        // Handle string concatenation
        if (lhs_type->toString() == "string" && rhs_type->toString() == "string" && expr.op.type == TokenType::PLUS) {
            // TODO: This requires a runtime function, e.g., angara_string_concat(lhs, rhs)
        }

        return "create_nil() /* unsupported binary op */";
    }

    std::any CTranspiler::visit(const VarExpr& expr) {
        std::cout << "[ ang::comp ] :: visiting and passing VarExpr. " << std::endl;
        // The C translation of a variable expression is just its name.
        return expr.name.lexeme;
    }

    std::any CTranspiler::visit(const Unary& expr) {
        std::cout << "[ ang::comp ] :: visiting UnaryExpr: " << std::endl;
        // 1. Recursively transpile the right-hand side expression.
        auto rhs_str = std::any_cast<std::string>(expr.right->accept(*this));

        // 2. Prepend the C equivalent of the unary operator.
        std::string op = expr.op.lexeme;
        if (expr.op.type == TokenType::BANG) {
            op = "!"; // Angara '!' is the same as C '!'
        } else if (expr.op.type == TokenType::MINUS) {
            op = "-"; // Angara '-' is the same as C '-'
        }

        // Parentheses are important to preserve order of operations, e.g., -(x + y)

        std::cout << "[ ang::comp ] :: closing UnaryExpr. " << std::endl;
        return std::string(op + "(" + rhs_str + ")");
    }

    std::any CTranspiler::visit(const Grouping& expr) {
        // Recursively transpile the inner expression and wrap it in parentheses.
        auto inner_str = std::any_cast<std::string>(expr.expression->accept(*this));
        return std::string("(" + inner_str + ")");
    }

    std::any CTranspiler::visit(const ListExpr &expr) {

        std::cout << "[ang::comp] :: not implemented :: ListExpr." << std::endl;
        exit(EXIT_FAILURE);

        return std::any();
    }


// --- AssignExpr: Now generates a function call ---
    std::any CTranspiler::visit(const AssignExpr& expr) {
        auto rhs_str = std::any_cast<std::string>(expr.value->accept(*this));

        if (auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            // Simple variable assignment remains the same.
            return std::string("(" + var_target->name.lexeme + " = " + rhs_str + ")");
        }
        else if (auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            // This is now a function call: angara_list_set(list, index, value)
            auto object_str = std::any_cast<std::string>(subscript_target->object->accept(*this));
            auto index_str = std::any_cast<std::string>(subscript_target->index->accept(*this));

            auto collection_type = m_type_checker.m_expression_types.at(subscript_target->object.get());

            if (collection_type->kind == TypeKind::LIST) {
                return "angara_list_set(" + object_str + ", " + index_str + ", " + rhs_str + ")";
            }
            // TODO: Add case for records
        } else if (auto get_target = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
            // We can just transpile the GetExpr to get the LHS C code.
            auto lhs_str = std::any_cast<std::string>(get_target->accept(*this));
            return std::string("(" + lhs_str + " = " + rhs_str + ")");
        }

        return "/* invalid assignment */";
    }

    std::any CTranspiler::visit(const UpdateExpr& expr) {
        // This visitor transpiles i++ (postfix) and ++i (prefix).
        auto target_str = std::any_cast<std::string>(expr.target->accept(*this));
        std::string op = expr.op.lexeme; // Will be "++" or "--"

        // The C syntax for pre/post increment/decrement is identical to Angara's.
        if (expr.isPrefix) {
            // e.g., ++i
            return std::string("(" + op + target_str + ")");
        } else {
            // e.g., i++
            return std::string("(" + target_str + op + ")");
        }
    }


    std::any CTranspiler::visit(const CallExpr& expr) {
        // 1. Get the type of the thing being called.
        auto callee_type = m_type_checker.m_expression_types.at(expr.callee.get());

        // 2. Transpile all arguments into a single comma-separated string.
        std::string args_str;
        for (size_t i = 0; i < expr.arguments.size(); ++i) {
            args_str += std::any_cast<std::string>(expr.arguments[i]->accept(*this));
            if (i < expr.arguments.size() - 1) {
                args_str += ", ";
            }
        }

        // --- Case 1: Class Constructor Call ---
        // e.g., Point(3, 4)
        if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
            // A constructor call is transpiled into a call to the ClassName_new function.
            return "Angara_" + class_type->name + "_new(" + args_str + ")";
        }

        // --- Case 2: Instance Method Call ---
        // e.g., p1.move(10, 20)
        if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
            // Transpile the object the method is being called on (e.g., 'p1').
            auto object_str = std::any_cast<std::string>(get_expr->object->accept(*this));
            const std::string& method_name = get_expr->name.lexeme;

            // Get the object's type to find its class name.
            auto object_type = std::dynamic_pointer_cast<InstanceType>(
                    m_type_checker.m_expression_types.at(get_expr->object.get())
            );
            std::string class_name = object_type->class_type->name;

            // Assemble the call: ClassName_methodName(instance, arg1, arg2, ...)
            std::string final_args = object_str; // The instance is the first implicit argument.
            if (!args_str.empty()) {
                final_args += ", " + args_str;
            }
            return "Angara_" + class_name + "_" + method_name + "(" + final_args + ")";
        }

        // --- Case 3: Regular Function Call ---
        // e.g., print("hello"), add(1, 2)
        auto callee_str = std::any_cast<std::string>(expr.callee->accept(*this));

        // Handle built-ins
        if (callee_str == "print") {
            return "angara_print(" + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
        }
        if (callee_str == "len") {
            return "angara_len(" + args_str + ")";
        }
        // ... other built-ins ...

        // Default to a direct function call for user-defined global functions.
        return callee_str + "(" + args_str + ")";
    }

    std::any CTranspiler::visit(const GetExpr& expr) {
        // Transpiles `object.property`.
        auto object_str = std::any_cast<std::string>(expr.object->accept(*this));
        const std::string& prop_name = expr.name.lexeme;

        // Get the type of the object to know what kind of struct it is.
        auto object_type = m_type_checker.m_expression_types.at(expr.object.get());

        if (object_type->kind == TypeKind::INSTANCE) {
            auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
            std::string c_struct_name = "Angara_" + instance_type->class_type->name;

            // In C, we cast the generic AngaraObject to the specific struct pointer,
            // then access the field with the -> operator.
            return std::string("(((struct " + c_struct_name + "*)AS_INSTANCE(" + object_str + "))->" + prop_name + ")");
        }

        // TODO: Handle module property access later.
        return "/* unknown get expr */";
    }

    std::any CTranspiler::visit(const LogicalExpr& expr) {

        std::cout << "[ ang::comp ] :: visiting LogicalExpr: " << std::endl;
        // Logical operators are a special case of BinaryExpr, but the C translation
        // is identical. C's `&&` and `||` operators already have the correct
        // precedence and short-circuiting behavior we need.
        auto lhs_str = std::any_cast<std::string>(expr.left->accept(*this));
        auto rhs_str = std::any_cast<std::string>(expr.right->accept(*this));
        std::string op = expr.op.lexeme; // Will be "&&" or "||"
        std::cout << "[ ang::comp ] :: closing LogicalExpr. " << std::endl;
        return std::string("(" + lhs_str + " " + op + " " + rhs_str + ")");
    }

// --- SubscriptExpr: Now generates a function call ---
    std::any CTranspiler::visit(const SubscriptExpr& expr) {
        auto object_str = std::any_cast<std::string>(expr.object->accept(*this));
        auto index_str = std::any_cast<std::string>(expr.index->accept(*this));

        // We get the type of the collection to decide which function to call.
        auto collection_type = m_type_checker.m_expression_types.at(expr.object.get());

        if (collection_type->kind == TypeKind::LIST) {
            // The index for a list is an AngaraObject i64, not a raw C int.
            return "angara_list_get(" + object_str + ", " + index_str + ")";
        }
        // TODO: Add case for records (angara_record_get)

        return "create_nil() /* unknown subscript */";
    }

    std::any CTranspiler::visit(const TernaryExpr& expr) {
        // Transpiles `cond ? then : else`.
        // C's ternary operator `?:` works exactly like ours.

        auto cond_str = std::any_cast<std::string>(expr.condition->accept(*this));
        auto then_str = std::any_cast<std::string>(expr.thenBranch->accept(*this));
        auto else_str = std::any_cast<std::string>(expr.elseBranch->accept(*this));

        // We must use our angara_is_truthy helper for the condition.
        return std::string("(angara_is_truthy(" + cond_str + ") ? " + then_str + " : " + else_str + ")");
    }

    std::any CTranspiler::visit(const ThisExpr& expr) {
        // Inside a method, the first parameter is always the instance object.
        // In our `transpileMethod` helper, we named this parameter 'this_obj'.
        return std::string("this_obj");
    }

    std::any CTranspiler::visit(const SuperExpr& expr) {
        // Transpiling `super.method()` is complex. It means we need to call
        // the superclass's implementation of a method.
        // A common C translation for this is a direct function call to a mangled name.

        // We need to know the superclass name at this point.
        // This requires another lookup from the Type Checker's results.
        // auto type = m_type_checker.m_expression_types.at(&expr);
        // auto func_type = std::dynamic_pointer_cast<FunctionType>(type);

        // For now, let's generate a placeholder that shows the intent.
        // e.g., super.draw() on a Player (child of Entity) becomes Entity_draw(this);

        // This is a placeholder that will need to be refined when we do the full
        // class transpilation.

        // TODO: refine

        return std::string("/* super." + expr.method.lexeme + " */");
    }

    void CTranspiler::visit(std::shared_ptr<const IfStmt> stmt) {
        // 1. Transpile the condition expression.
        auto condition_str = std::any_cast<std::string>(stmt->condition->accept(*this));

        indent();
        m_out << "if (angara_is_truthy(" << condition_str << ")) "; // <-- Use the helper

        // 2. Transpile the 'then' block.
        // Our visit(BlockStmt) will handle the `{ ... }` and indentation.
        stmt->thenBranch->accept(*this, stmt->thenBranch);

        // 3. Handle 'orif' (else if) and 'else' branches.
        if (stmt->elseBranch) {
            indent();
            m_out << "else ";
            // The parser cleverly makes 'orif' chains into nested IfStmts.
            // So, by just calling accept() on the elseBranch, we recursively
            // handle the entire 'else if (...) { ... } else { ... }' chain.
            stmt->elseBranch->accept(*this, stmt->elseBranch);
        }
    }

    void CTranspiler::visit(std::shared_ptr<const EmptyStmt> stmt) {
        // literally does nothing. just like me when.. i don't even know anymore.
    }

    void CTranspiler::visit(std::shared_ptr<const WhileStmt> stmt) {
        auto condition_str = std::any_cast<std::string>(stmt->condition->accept(*this));

        indent();
        m_out << "while (angara_is_truthy(" << condition_str << ")) ";

        // Transpile the body block.
        stmt->body->accept(*this, stmt->body);
    }

    void CTranspiler::visit(std::shared_ptr<const ForStmt> stmt) {
        indent();
        m_out << "{\n"; // Use a block to contain the loop variable's scope
        m_indent_level++;

        indent();
        m_out << "for (";

        // 1. Initializer
        if (stmt->initializer) {
            // The initializer is a full statement, so we need to generate it
            // *before* the loop and get its expression part for the for().
            // This is complex. Let's simplify.
            // For `let i = 0;`, we need to generate `AngaraObject i = create_i64(0);`
            // and put just the declaration in the `for`. This is tricky.

            // A simpler translation:
            if (auto init_stmt = std::dynamic_pointer_cast<const VarDeclStmt>(stmt->initializer)) {
                auto symbol = m_type_checker.m_symbols.resolve(init_stmt->name.lexeme);
                m_out << "AngaraObject " << init_stmt->name.lexeme << " = "
                      << std::any_cast<std::string>(init_stmt->initializer->accept(*this));
            } else if (auto expr_stmt = std::dynamic_pointer_cast<const ExpressionStmt>(stmt->initializer)) {
                m_out << std::any_cast<std::string>(expr_stmt->expression->accept(*this));
            }
        }
        m_out << "; ";

        // 2. Condition
        if (stmt->condition) {
            m_out << "angara_is_truthy(" << std::any_cast<std::string>(stmt->condition->accept(*this)) << ")";
        }
        m_out << "; ";

        // 3. Increment
        if (stmt->increment) {
            m_out << std::any_cast<std::string>(stmt->increment->accept(*this));
        }
        m_out << ") ";

        // 4. Body
        stmt->body->accept(*this, stmt->body);

        m_indent_level--;
        indent();
        m_out << "}\n";
    }

    void CTranspiler::visit(std::shared_ptr<const ForInStmt> stmt) {
        std::cout << "[ang::comp] :: not implemented :: ForInStmt." << std::endl;
        exit(EXIT_FAILURE);
    }

    void CTranspiler::visit(std::shared_ptr<const FuncStmt> stmt) {
        std::cout << "visiting FuncStmt" << std::endl;
        // Fetch the function's full type from the type checker.
        auto symbol = m_type_checker.m_symbols.resolve(stmt->name.lexeme);
        std::cout << "fetching symbol: " << symbol->name << std::endl;
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
        std::cout << "fetching func_type: " << func_type.get()->toString() << std::endl;

        // Get the C return type.
        auto c_return_type = getCType(func_type->return_type);
        std::cout << "fetching return_type: " << c_return_type << std::endl;

        // --- Generate the C function signature ---
        m_out << "\n"; // Add some space before the function.
        m_out << c_return_type << " " << stmt->name.lexeme << "(";

        // Add parameters
        for (size_t i = 0; i < stmt->params.size(); ++i) {
            const auto& param = stmt->params[i];
            auto param_c_type = getCType(func_type->param_types[i]);
            m_out << param_c_type << " " << param.name.lexeme;
            if (i < stmt->params.size() - 1) {
                m_out << ", ";
            }
        }

        std::cout << "parameters added." << std::endl;

        m_out << ") {\n";
        m_indent_level++;

        std::cout << "transpiling body..." << std::endl;

        // --- Transpile the function body ---
        for (const auto& body_stmt : *stmt->body) {
            body_stmt->accept(*this, body_stmt);
        }

        // TODO: Handle implicit returns for functions that don't end with a return statement.

        m_indent_level--;
        m_out << "}\n\n";
    }

    void CTranspiler::visit(std::shared_ptr<const ReturnStmt> stmt) {
        indent();
        m_out << "return";

        if (stmt->value) {
            m_out << " ";
            auto value_str = std::any_cast<std::string>(stmt->value->accept(*this));
            m_out << value_str;
        }

        m_out << ";\n";
    }

    void CTranspiler::visit(std::shared_ptr<const AttachStmt> stmt) {
        std::cout << "[ang::comp] :: not implemented :: AttachStmt." << std::endl;
        exit(EXIT_FAILURE);
    }

    void CTranspiler::visit(std::shared_ptr<const ThrowStmt> stmt) {
        std::cout << "[ang::comp] :: not implemented :: ThrowStmt." << std::endl;
        exit(EXIT_FAILURE);
    }

    void CTranspiler::visit(std::shared_ptr<const TryStmt> stmt) {
        std::cout << "[ang::comp] :: not implemented :: TryStmt." << std::endl;
        exit(EXIT_FAILURE);
    }

// in CTranspiler.cpp

    void CTranspiler::visit(std::shared_ptr<const ClassStmt> stmt) {
        // This visitor runs in Pass 1 and writes to the global scope.
        auto class_type = std::dynamic_pointer_cast<ClassType>(
                m_type_checker.m_symbols.resolve(stmt->name.lexeme)->type
        );
        std::string c_struct_name = "Angara_" + stmt->name.lexeme;

        // --- 1. Generate the Struct Definition ---
        m_globals << "typedef struct " << c_struct_name << " " << c_struct_name << ";\n"; // Forward-declare for self-references
        m_globals << "struct " << c_struct_name << " {\n";
        m_indent_level++;
        indent(m_globals); m_globals << "AngaraInstance base;\n";
        if (class_type->superclass) {
            indent(m_globals); m_globals << "struct Angara_" << class_type->superclass->name << " parent;\n";
        }
        for (const auto& [name, info] : class_type->fields) {
            indent(m_globals);
            m_globals << getCType(info.type) << " " << name << ";\n";
        }
        m_indent_level--;
        m_globals << "};\n\n";

        // --- 2. Generate Forward Declarations for all Methods and the _new function ---
        // This is crucial for allowing methods to call each other.
        auto init_method_it = class_type->methods.find("init");
        if (init_method_it != class_type->methods.end()) {
            auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_method_it->second.type);
            m_globals << "AngaraObject Angara_" << class_type->name << "_new(";
            // Constructor params (no 'this')
            for (size_t i = 0; i < init_func_type->param_types.size(); ++i) {
                m_globals << getCType(init_func_type->param_types[i]) << (i == init_func_type->param_types.size() - 1 ? "" : ", ");
            }
            m_globals << ");\n";
        }
        for (const auto& member : stmt->members) {
            if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                transpileMethodSignature(class_type->name, *method_member->declaration, m_globals);
                m_globals << ";\n";
            }
        }
        m_globals << "\n";

        // --- 3. Generate the _new Constructor Function ---
        if (init_method_it != class_type->methods.end()) {
            auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_method_it->second.type);
            auto init_method_ast = findMethodAst(*stmt, "init"); // Helper to find the init FuncStmt

            m_globals << "AngaraObject Angara_" << class_type->name << "_new(";
            for (size_t i = 0; i < init_method_ast->params.size(); ++i) {
                m_globals << getCType(init_func_type->param_types[i]) << " " << init_method_ast->params[i].name.lexeme << (i == init_method_ast->params.size() - 1 ? "" : ", ");
            }
            m_globals << ") {\n";
            m_indent_level++;

            indent(m_globals);
            m_globals << "AngaraObject this_obj = (AngaraObject){VAL_OBJ, {.obj = init_instance(sizeof(" << c_struct_name << "), NULL)}};\n"; // We'll get the class object later

            indent(m_globals);
            m_globals << "Angara_" << class_type->name << "_init(this_obj";
            for (const auto& param : init_method_ast->params) {
                m_globals << ", " << param.name.lexeme;
            }
            m_globals << ");\n";

            indent(m_globals); m_globals << "return this_obj;\n";
            m_indent_level--;
            m_globals << "}\n\n";
        }

        // --- 4. Generate Method Implementations ---
        for (const auto& member : stmt->members) {
            if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                transpileMethod(class_type->name, *method_member->declaration);
            }
        }
    }

// --- NEW HELPER: transpileMethod ---
// (Add its declaration to CTranspiler.h)
    void CTranspiler::transpileMethod(const ClassType& klass, const FuncStmt& stmt) {
        auto symbol = klass.methods.at(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol.type);

        m_out << getCType(func_type->return_type) << " "
              << "Angara_" << klass.name << "_" << stmt.name.lexeme << "(";

        // Method signature
        m_out << "AngaraObject this_obj";
        for (size_t i = 0; i < stmt.params.size(); ++i) {
            m_out << ", " << getCType(func_type->param_types[i]) << " " << stmt.params[i].name.lexeme;
        }
        m_out << ") {\n";
        m_indent_level++;

        // Cast 'this' to the correct struct type for field access
        indent();
        m_out << "struct Angara_" << klass.name << "* this = (struct Angara_" << klass.name << "*)AS_INSTANCE(this_obj);\n";

        // Transpile the body
        for (const auto& body_stmt : *stmt.body) {
            body_stmt->accept(*this, body_stmt);
        }

        m_indent_level--;
        m_out << "}\n\n";
    }

    void CTranspiler::visit(std::shared_ptr<const TraitStmt> stmt) {
        std::cout << "[ang::comp] :: not implemented :: TraitStmt." << std::endl;
        exit(EXIT_FAILURE);
    }


    void CTranspiler::visit(std::shared_ptr<const BlockStmt> stmt) {
        indent();
        m_out << "{\n";
        m_indent_level++;

        for (const auto& statement : stmt->statements) {
            statement->accept(*this, statement);
        }

        m_indent_level--;
        indent();
        m_out << "}\n";
    }

}
