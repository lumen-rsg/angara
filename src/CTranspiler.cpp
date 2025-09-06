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

     const ClassType* CTranspiler::findPropertyOwner(const ClassType* klass, const std::string& prop_name) {
        if (!klass) return nullptr;
        if (klass->fields.count(prop_name) || klass->methods.count(prop_name)) {
            return klass;
        }
        return findPropertyOwner(klass->superclass.get(), prop_name);
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

        // --- THIS IS THE FIX ---
        std::string func_name = stmt.name.lexeme;
        if (func_name == "main") {
            func_name = "angara_main"; // Mangle the name
        }
        // --- END OF FIX ---

        *m_current_out << getCType(func_type->return_type) << " " << func_name << "(";
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

    (*m_current_out) << "// --- Function Forward Declarations ---\n";
    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            // ... (this part is correct) ...
        } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {

            // --- THIS IS THE FIX ---
            // ALWAYS generate a forward declaration for the _new function.
            auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
            (*m_current_out) << "AngaraObject Angara_" << class_stmt->name.lexeme << "_new(";

            // Find the init method to determine the constructor parameters.
            auto init_it = class_type->methods.find("init");
            if (init_it != class_type->methods.end()) {
                auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
                for (size_t i = 0; i < init_func_type->param_types.size(); ++i) {
                    (*m_current_out) << getCType(init_func_type->param_types[i]);
                    if (i < init_func_type->param_types.size() - 1) (*m_current_out) << ", ";
                }
            } // If no init method, the parameter list is empty, which is correct.

            (*m_current_out) << ");\n";
            // --- END OF FIX ---

            // Generate prototypes for all other methods (this part is correct).
            for (const auto& member : class_stmt->members) {
                if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                    transpileMethodSignature(class_stmt->name.lexeme, *method_member->declaration);
                    (*m_current_out) << ";\n";
                }
            }
        }
    }
}

    void CTranspiler::transpileGlobalFunction(const FuncStmt& stmt) {
        // This function handles a top-level `func` declaration.

        // 1. Generate the function signature using our existing helper.
        transpileFunctionSignature(stmt);
        (*m_current_out) << " {\n";
        m_indent_level++;

        // 2. Transpile all statements in the function's body.
        if (stmt.body) {
            for (const auto& body_stmt : *stmt.body) {
                transpileStmt(body_stmt);
            }
        }

        // 3. Handle implicit returns for void functions.
        //    Get the canonical type from the symbol table.
        auto symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);

        if (func_type->return_type->toString() == "void") {
            // A simple check: if the last statement in the body wasn't a return, add one.
            // This is a simplification; a truly robust check would require analyzing
            // all possible control flow paths.

            // TODO: refine.

            if (stmt.body->empty() || !isa<ReturnStmt>(stmt.body->back())) {
                indent();
                (*m_current_out) << "return;\n";
            }
        }

        m_indent_level--;
        (*m_current_out) << "}\n\n";

    }

void CTranspiler::pass_3_generate_function_implementations(const std::vector<std::shared_ptr<Stmt>>& statements) {
    m_current_out = &m_function_implementations;
    m_indent_level = 0;


    (*m_current_out) << "\n// --- Global Variable Declarations ---\n";
    for (const auto& stmt : statements) {
        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            (*m_current_out) << "AngaraObject " << var_decl->name.lexeme << ";\n";
        }
    }
    // --- END OF FIX ---

    (*m_current_out) << "// --- Function Implementations ---\n";
    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            m_current_class_name = "";

            // --- First, transpile the actual, strongly-typed C function ---
            transpileGlobalFunction(*func_stmt);

            // --- NEW: Now, generate the generic wrapper for it ---
            auto symbol = m_type_checker.m_symbols.resolve(func_stmt->name.lexeme);
            auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);

            std::string func_name = func_stmt->name.lexeme;
            if (func_name == "main") func_name = "angara_main";

            (*m_current_out) << "// Wrapper for " << func_name << "\n";
            (*m_current_out) << "AngaraObject wrapper_" << func_name << "(int arg_count, AngaraObject args[]) {\n";
            m_indent_level++;
            indent();

            // --- THIS IS THE FIX ---
            // Check if the real function returns void.
            if (func_type->return_type->toString() == "void") {
                // If it's void, just call it and then explicitly return nil.
                (*m_current_out) << func_name << "(";
                for (int i = 0; i < func_stmt->params.size(); ++i) {
                    (*m_current_out) << "args[" << i << "]" << (i == func_stmt->params.size() - 1 ? "" : ", ");
                }
                (*m_current_out) << ");\n";
                indent();
                (*m_current_out) << "return create_nil();\n";
            } else {
                // If it returns a value, the existing logic is correct.
                (*m_current_out) << "return " << func_name << "(";
                for (int i = 0; i < func_stmt->params.size(); ++i) {
                    (*m_current_out) << "args[" << i << "]" << (i == func_stmt->params.size() - 1 ? "" : ", ");
                }
                (*m_current_out) << ");\n";
            }
            // --- END OF FIX ---

            m_indent_level--;
            (*m_current_out) << "}\n\n";

        } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
            m_current_class_name = class_stmt->name.lexeme;
            auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
            std::string c_struct_name = "Angara_" + class_type->name;

            auto init_method_ast = findMethodAst(*class_stmt, "init");

            // 1. Signature
            (*m_current_out) << "AngaraObject Angara_" << class_type->name << "_new(";
            if (init_method_ast) {
                // If init exists, the _new function takes its parameters.
                auto init_info = class_type->methods.at("init");
                auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_info.type);
                for (size_t i = 0; i < init_method_ast->params.size(); ++i) {
                     (*m_current_out) << getCType(init_func_type->param_types[i]) << " " << init_method_ast->params[i].name.lexeme << (i == init_method_ast->params.size() - 1 ? "" : ", ");
                }
            } // If no init, the parameter list is empty.
            (*m_current_out) << ") {\n";
            m_indent_level++;

            // 2. Body
            indent(); (*m_current_out) << c_struct_name << "* instance = (" << c_struct_name << "*)angara_instance_new(sizeof(" << c_struct_name << "), NULL);\n";
            indent(); (*m_current_out) << "AngaraObject this_obj = (AngaraObject){VAL_OBJ, {.obj = (Object*)instance}};\n";

            // Conditionally call the _init method ONLY if it exists.
            if (init_method_ast) {
                indent(); (*m_current_out) << "Angara_" << class_type->name << "_init(this_obj";
                for (const auto& param : init_method_ast->params) {
                    (*m_current_out) << ", " << param.name.lexeme;
                }
                (*m_current_out) << ");\n";
            }

            indent(); (*m_current_out) << "return this_obj;\n";
            m_indent_level--;
            (*m_current_out) << "}\n\n";
            // --- END OF FIX ---

            // --- Generate Method Implementations (this part is correct) ---
            for (const auto& member : class_stmt->members) {
                if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                    transpileMethodBody(*class_type, *method_member->declaration);
                }
            }

            m_current_class_name = "";
        }
    }
}

    void CTranspiler::transpileStruct(const ClassStmt& stmt) {
        // 1. Get the canonical ClassType from the type checker's symbol table.
        //    This was fully populated by the checker's Pass 1 and Pass 2.
        auto class_symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
        auto class_type = std::dynamic_pointer_cast<ClassType>(class_symbol->type);

        std::string c_struct_name = "Angara_" + stmt.name.lexeme;

        (*m_current_out) << "typedef struct " << c_struct_name << " " << c_struct_name << ";\n";
        (*m_current_out) << "struct " << c_struct_name << " {\n";
        m_indent_level++;

        // 2. Add the base header.
        // If there is a superclass, its struct ALREADY contains the base header.
        if (class_type->superclass) {
            indent(); (*m_current_out) << "struct Angara_" << class_type->superclass->name << " parent;\n";
        } else {
            indent(); (*m_current_out) << "AngaraInstance base;\n";
        }

        // 3. Add all fields DEFINED IN THIS CLASS.
        //    We iterate the AST to get the field names in their declared order.
        for (const auto& member : stmt.members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                const auto& field_name = field_member->declaration->name.lexeme;
                // Get the type from the ClassType's field map, not m_variable_types.
                const auto& field_info = class_type->fields.at(field_name);

                indent();
                (*m_current_out) << getCType(field_info.type) << " " << field_name << ";\n";
            }
        }
        // --- END OF FIX ---

        m_indent_level--;
        (*m_current_out) << "};\n\n";
    }

// --- Main Orchestrator ---
// in CTranspiler.cpp

std::string CTranspiler::generate(const std::vector<std::shared_ptr<Stmt>>& statements) {
    // --- Run all the passes (unchanged) ---
    pass_1_generate_structs(statements);
    pass_2_generate_function_declarations(statements);
    pass_3_generate_function_implementations(statements);
    if (m_hadError) return "";

    // --- Assemble the final C file (unchanged preamble) ---
    std::stringstream final_code;
    final_code << "#include \"angara_runtime.h\"\n\n";
    final_code << m_structs_and_globals.str() << "\n";
    final_code << m_function_declarations.str() << "\n";
    final_code << m_function_implementations.str() << "\n";

    // --- Generate the C main() wrapper (UPDATED LOGIC) ---
    final_code << "// --- C Entry Point ---\n";
    final_code << "int main(int argc, const char* argv[]) {\n";
    final_code << "    angara_runtime_init();\n\n";

    // --- NEW: Initialize all global function closures ---
    final_code << "    // Initialize Global Function Closures\n";
    for (const auto& stmt : statements) {

        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            // Generate the INITIALIZATION code inside main.
            final_code << "    " << var_decl->name.lexeme << " = "
                       << transpileExpr(var_decl->initializer) << ";\n";
        }
        if (auto func = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            std::string func_name = func->name.lexeme;
            std::string c_name = (func_name == "main") ? "angara_main" : func_name;

            // Declare a C variable to hold the closure for this function.
            final_code << "    AngaraObject " << func_name << " = angara_closure_new(&wrapper_"
                       << c_name << ", " << func->params.size() << ", false);\n";
        }
    }
    final_code << "\n";

    // Get the signature of the user's main function from the Type Checker.
    auto main_symbol = m_type_checker.m_symbols.resolve("main");
    auto main_func_type = std::dynamic_pointer_cast<FunctionType>(main_symbol->type);

    final_code << "    // Call the user's Angara main function\n";
    if (main_func_type->param_types.empty()) {
        // Call `angara_call` with the 'main' closure object.
        final_code << "    AngaraObject result = angara_call(main, 0, NULL);\n";
    } else {
        // Create the list of C argv strings.
        final_code << "    AngaraObject args_list = angara_list_new();\n";
        final_code << "    for (int i = 0; i < argc; i++) {\n";
        final_code << "        angara_list_push(args_list, angara_string_from_c(argv[i]));\n";
        final_code << "    }\n";
        // Call `angara_call` with the 'main' closure and the args list.
        final_code << "    AngaraObject result = angara_call(main, 1, &args_list);\n";
        final_code << "    angara_decref(args_list);\n";
    }

    final_code << "\n";
    // Decrement the ref count for all the global function closures
    for (const auto& stmt : statements) {
        if (auto func = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
             final_code << "    angara_decref(" << func->name.lexeme << ");\n";
        }
    }
    final_code << "\n";
    final_code << "    int exit_code = (int)AS_I64(result);\n";
    final_code << "    angara_decref(result);\n\n";
    final_code << "    angara_runtime_shutdown();\n";
    final_code << "    return exit_code;\n";
    final_code << "}\n";

    return final_code.str();
}


    void CTranspiler::pass_1_generate_structs(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_structs_and_globals;
        m_indent_level = 0;

        (*m_current_out) << "// --- Struct Definitions ---\n";
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                transpileStruct(*class_stmt);

            // After defining the struct, declare the global C variable that will
            // represent the class object itself at runtime.
            m_structs_and_globals << "AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";
            }
        }
    }

    void CTranspiler::transpileMethodSignature(const std::string& class_name, const FuncStmt& stmt) {
        // Look up the canonical FunctionType from the Type Checker's results.
        auto class_symbol = m_type_checker.m_symbols.resolve(class_name);
        auto class_type = std::dynamic_pointer_cast<ClassType>(class_symbol->type);
        auto method_info = class_type->methods.at(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(method_info.type);

        // Generate the mangled name, e.g., "Angara_Point_move"
        (*m_current_out) << getCType(func_type->return_type) << " "
                         << "Angara_" << class_name << "_" << stmt.name.lexeme << "(";

        // The first parameter is always 'this'.
        if (stmt.has_this) {
            (*m_current_out) << "AngaraObject this_obj";
            if (!stmt.params.empty()) {
                (*m_current_out) << ", ";
            }
        }

        // Add the regular parameters.
        for (size_t i = 0; i < stmt.params.size(); ++i) {
            (*m_current_out) << getCType(func_type->param_types[i]) << " " << stmt.params[i].name.lexeme;
            if (i < stmt.params.size() - 1) {
                (*m_current_out) << ", ";
            }
        }
        (*m_current_out) << ")";
    }

    void CTranspiler::transpileMethodBody(const ClassType& klass, const FuncStmt& stmt) {
        // 1. Generate the full function signature again (this time for the definition).
        transpileMethodSignature(klass.name, stmt);
        (*m_current_out) << " {\n";
        m_indent_level++;

        // 2. If it's a method with 'this', create the typed 'this' pointer.
        if (stmt.has_this) {
            indent();
            (*m_current_out) << "struct Angara_" << klass.name << "* this = (struct Angara_" << klass.name << "*)AS_INSTANCE(this_obj);\n";
        }

        // 3. Transpile all statements in the method's body.
        if (stmt.body) {
            for (const auto& body_stmt : *stmt.body) {
                transpileStmt(body_stmt);
            }
        }

        // 4. Handle implicit returns for void methods.
        auto method_info = klass.methods.at(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(method_info.type);
        if (func_type->return_type->toString() == "void") {
            indent();
            (*m_current_out) << "return;\n";
        }

        m_indent_level--;
        (*m_current_out) << "}\n\n";
    }


    void CTranspiler::pass_4_generate_main(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_current_out = &m_main_body;
        m_indent_level = 0;
        *m_current_out << "int main(void) {\n";
        m_indent_level++;

        indent(); (*m_current_out) << "// --- Initialize Class Objects ---\n";
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                indent();
                (*m_current_out) << "g_" << class_stmt->name.lexeme << "_class = (AngaraClass){"
                                 << "{OBJ_CLASS, 1}, " // Base object header
                                 << "\"" << class_stmt->name.lexeme << "\"" // Name
                                 << "};\n";
            }
        }
        indent(); (*m_current_out) << "\n";

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
        } else if (auto try_stmt = std::dynamic_pointer_cast<const TryStmt>(stmt)) {
            transpileTryStmt(*try_stmt);
        } else if (auto throw_stmt = std::dynamic_pointer_cast<const ThrowStmt>(stmt)) {
            transpileThrowStmt(*throw_stmt);
        } else if (auto for_in_stmt = std::dynamic_pointer_cast<const ForInStmt>(stmt)) { // <-- ADD THIS
            transpileForInStmt(*for_in_stmt);
        }

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
        } else if (auto get = std::dynamic_pointer_cast<const GetExpr>(expr)) { // <-- ADD THIS
            return transpileGetExpr(*get);
        } else if (auto call = std::dynamic_pointer_cast<const CallExpr>(expr)) { // <-- ADD THIS
            return transpileCallExpr(*call);
        } else if (auto this_expr = std::dynamic_pointer_cast<const ThisExpr>(expr)) { // <-- ADD THIS
            return transpileThisExpr(*this_expr);
        } else if (auto super = std::dynamic_pointer_cast<const SuperExpr>(expr)) {
            return transpileSuperExpr(*super);
        }
        // ... other else if for Call, Get, etc. ...
        return "/* unknown expr */";
    }

    std::string CTranspiler::transpileSuperExpr(const SuperExpr& expr) {
        // This transpiles a `super.method` expression. The expression itself
        // doesn't generate a value, but it tells a subsequent CallExpr which
        // C function to call. This is tricky.
        // Let's adjust the CallExpr visitor to handle this.
        return "/* super." + expr.method.lexeme + " */"; // Keep as placeholder for now
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
        // We can only increment/decrement variables, which are valid l-values.
        // The C code needs to pass the ADDRESS of the variable to the runtime helper.
        std::string target_str = transpileExpr(expr.target);

        // --- THIS IS THE FIX ---
        if (expr.op.type == TokenType::PLUS_PLUS) {
            if (expr.isPrefix) {
                // ++i  ->  angara_pre_increment(&i)
                return "angara_pre_increment(&" + target_str + ")";
            } else {
                // i++  ->  angara_post_increment(&i)
                return "angara_post_increment(&" + target_str + ")";
            }
        } else { // MINUS_MINUS
            if (expr.isPrefix) {
                // --i  ->  angara_pre_decrement(&i)
                return "angara_pre_decrement(&" + target_str + ")";
            } else {
                // i--  ->  angara_post_decrement(&i)
                return "angara_post_decrement(&" + target_str + ")";
            }
        }
        // --- END OF FIX ---
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

        // 1. Get the pre-computed type of the thing being called.
        auto callee_type = m_type_checker.m_expression_types.at(expr.callee.get());

        // 2. Transpile all arguments into a single comma-separated string.
        std::string args_str;
        for (size_t i = 0; i < expr.arguments.size(); ++i) {
            args_str += transpileExpr(expr.arguments[i]);
            if (i < expr.arguments.size() - 1) args_str += ", ";
        }

        // --- Case 1: Class Constructor Call, e.g., Point(3, 4) ---
        if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
            // A constructor call is transpiled into a call to our special _new function.
            return "Angara_" + class_type->name + "_new(" + args_str + ")";
        }

        // --- NEW: Handle Mutex() constructor ---
        if (auto callee_var = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            if (callee_var->name.lexeme == "Mutex") {
                return "angara_mutex_new()";
            }
        }
        // --- END OF NEW ---



        // --- Case 2: A Method Call (on an instance OR a built-in type) ---
        if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
            std::string object_str = transpileExpr(get_expr->object);
            const std::string& method_name = get_expr->name.lexeme;

            auto object_type = m_type_checker.m_expression_types.at(get_expr->object.get());

            // --- THIS IS THE FIX ---
            if (object_type->kind == TypeKind::THREAD) {
                // It's a method on our built-in Thread type.
                if (method_name == "join") {
                    return "angara_thread_join(" + object_str + ")";
                }
            }
            // --- END OF FIX ---

            // --- NEW LOGIC for List methods ---
            if (object_type->kind == TypeKind::LIST) {
                if (method_name == "push") {
                    // Transpiles `my_list.push(item)` to `angara_list_push(my_list, item)`
                    return "angara_list_push(" + object_str + ", " + args_str + ")";
                }
            }

            // --- NEW: Handle Mutex methods ---
            else if (object_type->kind == TypeKind::MUTEX) {
                if (method_name == "lock") return "angara_mutex_lock(" + object_str + ")";
                if (method_name == "unlock") return "angara_mutex_unlock(" + object_str + ")";
            }
            // --- END OF NEW ---

            else if (object_type->kind == TypeKind::INSTANCE) {
                // This is the existing logic for class instance methods.
                auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), method_name);
                std::string owner_class_name = owner_class ? owner_class->name : "UNKNOWN_CLASS";

                std::string final_args = object_str;
                if (!args_str.empty()) final_args += ", " + args_str;
                return "Angara_" + owner_class_name + "_" + method_name + "(" + final_args + ")";
            }
        }

        if (auto super_expr = std::dynamic_pointer_cast<const SuperExpr>(expr.callee)) {
            // We must be inside a class to use 'super'.
            if (!m_current_class_name.empty()) {
                // Get the current class's type to find its superclass.
                auto class_symbol = m_type_checker.m_symbols.resolve(m_current_class_name);
                auto class_type = std::dynamic_pointer_cast<ClassType>(class_symbol->type);

                if (class_type->superclass) {
                    std::string superclass_name = class_type->superclass->name;
                    const std::string& method_name = super_expr->method.lexeme;

                    // The first argument to a super call is always 'this'
                    std::string final_args = "this_obj";
                    if (!args_str.empty()) {
                        final_args += ", " + args_str;
                    }
                    // Call the superclass's version of the method: SuperClassName_methodName(this, ...)
                    return "Angara_" + superclass_name + "_" + method_name + "(" + final_args + ")";
                }
            }
            // The Type Checker should have already caught invalid 'super' calls.
            return "/* invalid super call */";
        }


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
            // The transpiler's job is simple: just generate a call to the runtime helper.
            return "angara_typeof(" + args_str + ")";
        }

        if (callee_str == "spawn") {
            // The argument to spawn is a closure. We need to get its C function pointer.
            // This is complex. For now, let's assume it's a global function name.
            if (auto arg_var = std::dynamic_pointer_cast<const VarExpr>(expr.arguments[0])) {
                std::string wrapper_name = "&wrapper_" + arg_var->name.lexeme;
                // We need to create a closure object on the fly.
                return "angara_spawn(angara_closure_new(" + wrapper_name + ", 0, false))";
            }
        }

        // Transpile arguments for a regular function call
        for (size_t i = 0; i < expr.arguments.size(); ++i) {
            args_str += transpileExpr(expr.arguments[i]);
            if (i < expr.arguments.size() - 1) args_str += ", ";
        }

        return callee_str + "(" + args_str + ")";
    }

std::string CTranspiler::transpileAssignExpr(const AssignExpr& expr) {
    auto rhs_str = transpileExpr(expr.value);
    auto lhs_str = transpileExpr(expr.target);

    // --- THIS IS THE FIX ---

    if (expr.op.type == TokenType::EQUAL) {
        // Simple assignment: x = y
        return "(" + lhs_str + " = " + rhs_str + ")";
    } else {
        // Compound assignment: x += y, x -= y, etc.
        // This desugars to: x = x + y

        // 1. Get the core operator string (e.g., "+" from "+=").
        std::string core_op = expr.op.lexeme;
        core_op.pop_back(); // Remove the trailing '='

        // 2. Get the type of the LHS to generate the correct unboxing/reboxing.
        auto target_type = m_type_checker.m_expression_types.at(expr.target.get());

        // 3. Assemble the C expression.
        std::string full_expression;
        if (isInteger(target_type)) {
            // e.g., create_i64((AS_I64(x) + AS_I64(y)))
            full_expression = "create_i64((AS_I64(" + lhs_str + ") " + core_op + " AS_I64(" + rhs_str + ")))";
        } else if (isFloat(target_type)) {
            // e.g., create_f64((AS_F64(x) + AS_F64(y)))
            // Note: This correctly handles the case where one is an int and one is a float
            // because our AS_F64 macro performs the promotion.
            full_expression = "create_f64((AS_F64(" + lhs_str + ") " + core_op + " AS_F64(" + rhs_str + ")))";
        } else if (target_type->toString() == "string" && expr.op.type == TokenType::PLUS_EQUAL) {
            // String concatenation: x = angara_string_concat(x, y)
            full_expression = "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
        } else {
            // Should be unreachable if the Type Checker is correct.
            full_expression = "create_nil() /* unsupported compound assignment */";
        }

        // 4. Return the full assignment expression.
        return "(" + lhs_str + " = " + full_expression + ")";
    }
    // --- END OF FIX ---
}

    const FuncStmt* CTranspiler::findMethodAst(const ClassStmt& class_stmt, const std::string& name) {
        // 1. Iterate through all the members defined in the class's AST node.
        for (const auto& member : class_stmt.members) {
            // 2. Try to cast the generic member to a MethodMember.
            if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                // 3. If it's a method, check if its name matches the one we're looking for.
                if (method_member->declaration->name.lexeme == name) {
                    // 4. Found it. Return a raw pointer to the FuncStmt node.
                    return method_member->declaration.get();
                }
            }
        }
        // 5. If we've searched all members and haven't found it, return nullptr.
        return nullptr;
    }


    std::string CTranspiler::transpileGetExpr(const GetExpr& expr) {
        std::string object_str = transpileExpr(expr.object);
        const std::string& prop_name = expr.name.lexeme;

        auto object_type = std::dynamic_pointer_cast<InstanceType>(
            m_type_checker.m_expression_types.at(expr.object.get())
        );
        std::string c_struct_name = "Angara_" + object_type->class_type->name;

        // --- THIS IS THE FIX ---
        // Find which class in the hierarchy owns the property.
        const ClassType* owner_class = findPropertyOwner(object_type->class_type.get(), prop_name);

        std::string access_path = "->";
        const ClassType* current = object_type->class_type.get();
        while (current && current->name != owner_class->name) {
            access_path += "parent.";
            current = current->superclass.get();
        }
        access_path += prop_name;
        // --- END OF FIX ---

        return "((" + c_struct_name + "*)AS_INSTANCE(" + object_str + "))" + access_path;
    }

    std::string CTranspiler::transpileThisExpr(const ThisExpr& expr) {
        // Inside a method, the first parameter is always the instance object.
        // Our convention is to name this C parameter 'this_obj'.
        return "this_obj";
    }

    void CTranspiler::transpileThrowStmt(const ThrowStmt& stmt) {
        indent();
        (*m_current_out) << "angara_throw(" << transpileExpr(stmt.expression) << ");\n";
    }

    void CTranspiler::transpileTryStmt(const TryStmt& stmt) {
        indent(); (*m_current_out) << "{\n";
        m_indent_level++;
        indent(); (*m_current_out) << "ExceptionFrame __frame;\n";
        indent(); (*m_current_out) << "__frame.prev = g_exception_chain_head;\n";
        indent(); (*m_current_out) << "g_exception_chain_head = &__frame;\n";

        indent(); (*m_current_out) << "if (setjmp(__frame.buffer) == 0) {\n";
        m_indent_level++;

        transpileStmt(stmt.tryBlock);

        m_indent_level--;
        indent(); (*m_current_out) << "}\n";

        indent(); (*m_current_out) << "g_exception_chain_head = __frame.prev; // Pop the frame\n";

        indent(); (*m_current_out) << "if (g_current_exception.type != VAL_NIL) {\n";
        m_indent_level++;

        indent(); (*m_current_out) << "AngaraObject " << stmt.catchName.lexeme << " = g_current_exception;\n";
        indent(); (*m_current_out) << "g_current_exception = create_nil();\n";

        transpileStmt(stmt.catchBlock);

        m_indent_level--;
        indent(); (*m_current_out) << "}\n";
        m_indent_level--;
        indent(); (*m_current_out) << "}\n";
    }

    void CTranspiler::transpileForInStmt(const ForInStmt& stmt) {
    indent(); (*m_current_out) << "{\n"; // Start a new scope
    m_indent_level++;

    // 1. Create and initialize the hidden __collection variable.
    indent();
    (*m_current_out) << "AngaraObject __collection_" << stmt.name.lexeme << " = "
                     << transpileExpr(stmt.collection) << ";\n";
    indent();
    (*m_current_out) << "angara_incref(__collection_" << stmt.name.lexeme << ");\n";

    // 2. Create and initialize the hidden __index variable.
    indent();
    (*m_current_out) << "AngaraObject __index_" << stmt.name.lexeme << " = create_i64(0LL);\n";

    // 3. Generate the `while` loop header.
    indent();
    (*m_current_out) << "while (angara_is_truthy(create_bool(AS_I64(__index_" << stmt.name.lexeme << ") < AS_I64(angara_len(__collection_" << stmt.name.lexeme << "))))) {\n";
    m_indent_level++;

    // 4. Generate the `let item = ...` declaration.
    indent();
    (*m_current_out) << "AngaraObject " << stmt.name.lexeme << " = angara_list_get(__collection_"
                     << stmt.name.lexeme << ", __index_" << stmt.name.lexeme << ");\n";

    // 5. Transpile the user's loop body.
    transpileStmt(stmt.body);

    // 6. Generate the increment logic: `__index = __index + 1`.
    indent();
    (*m_current_out) << "{\n";
    m_indent_level++;
    indent(); (*m_current_out) << "AngaraObject __temp_one = create_i64(1LL);\n";
    indent(); (*m_current_out) << "AngaraObject __new_index = create_i64(AS_I64(__index_" << stmt.name.lexeme << ") + AS_I64(__temp_one));\n";
    indent(); (*m_current_out) << "angara_decref(__index_" << stmt.name.lexeme << ");\n";
    indent(); (*m_current_out) << "__index_" << stmt.name.lexeme << " = __new_index;\n";
    m_indent_level--;
    indent(); (*m_current_out) << "}\n";

    // 7. Decref the user's loop variable at the end of the iteration.
    indent(); (*m_current_out) << "angara_decref(" << stmt.name.lexeme << ");\n";

    m_indent_level--;
    indent();
    (*m_current_out) << "}\n";

    // 8. Decref the hidden variables at the end of the scope.
    indent();
    (*m_current_out) << "angara_decref(__collection_" << stmt.name.lexeme << ");\n";
    indent();
    (*m_current_out) << "angara_decref(__index_" << stmt.name.lexeme << ");\n";

    m_indent_level--;
    indent(); (*m_current_out) << "}\n";
}


} // namespace angara