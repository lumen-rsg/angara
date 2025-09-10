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
    void CTranspiler::transpileFunctionSignature(const FuncStmt& stmt, const std::string& module_name) {
        // 1. Get the canonical FunctionType from the Type Checker's results.
        auto symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);

        // 2. Determine the correct mangled name for the C function implementation.
        std::string mangled_name = "angara_f_" + module_name + "_" + stmt.name.lexeme;
        if (stmt.name.lexeme == "main") mangled_name = "angara_f_main";

        // 3. Write the return type and the mangled function name.
        *m_current_out << getCType(func_type->return_type) << " " << mangled_name << "(";

        // 4. Write the parameter list.
        if (stmt.has_this) {
            *m_current_out << "AngaraObject this_obj";
            if (!stmt.params.empty()) {
                *m_current_out << ", ";
            }
        }
        for (size_t i = 0; i < stmt.params.size(); ++i) {
            // Get the C type from the resolved FunctionType, but the name from the AST.
            *m_current_out << getCType(func_type->param_types[i]) << " " << stmt.params[i].name.lexeme;
            if (i < stmt.params.size() - 1) {
                *m_current_out << ", ";
            }
        }

        // Add 'void' for empty C parameter lists for standards compliance.
        if (!stmt.has_this && stmt.params.empty()) {
            *m_current_out << "void";
        }

        // 5. Close the signature.
        *m_current_out << ")";
    }


void CTranspiler::pass_2_generate_declarations(const std::vector<std::shared_ptr<Stmt>>& statements, const std::string& module_name) {
    (*m_current_out) << "\n// --- Global Variable Forward Declarations ---\n";
    for (const auto& stmt : statements) {
        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            if (var_decl->is_exported) {
                (*m_current_out) << "extern AngaraObject " << module_name << "_" << var_decl->name.lexeme << ";\n";
            }
        }
    }

    (*m_current_out) << "\n// --- Function & Closure Forward Declarations ---\n";
    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            if (func_stmt->is_exported || func_stmt->name.lexeme == "main") {
                std::string var_name = "g_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
                (*m_current_out) << "extern AngaraObject " << var_name << ";\n";

                transpileFunctionSignature(*func_stmt, module_name);
                (*m_current_out) << ";\n";

                std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";
                (*m_current_out) << "AngaraObject angara_w_" << mangled_name << "(int arg_count, AngaraObject args[]);\n";
            }
        } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
            if (class_stmt->is_exported) {
                auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
                (*m_current_out) << "\n// --- API for class " << class_stmt->name.lexeme << " ---\n";
                (*m_current_out) << "extern AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";

                (*m_current_out) << "AngaraObject Angara_" << class_stmt->name.lexeme << "_new(";
                auto init_it = class_type->methods.find("init");
                if (init_it != class_type->methods.end()) {
                    auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
                    for (size_t i = 0; i < init_func_type->param_types.size(); ++i) {
                        (*m_current_out) << getCType(init_func_type->param_types[i]);
                        if (i < init_func_type->param_types.size() - 1) (*m_current_out) << ", ";
                    }
                }
                (*m_current_out) << ");\n";

                for (const auto& member : class_stmt->members) {
                    if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                        if (method_member->access == AccessLevel::PUBLIC) {
                            transpileMethodSignature(class_stmt->name.lexeme, *method_member->declaration);
                            (*m_current_out) << ";\n";
                        }
                    }
                }
            }
        }
    }

        (*m_current_out) << "\n// --- Imported Native Function Declarations ---\n";
        for (const auto& stmt : statements) {
            if (auto attach_stmt = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
                // Get the ModuleType that the Type Checker resolved for this attach statement.

                auto module_type = m_type_checker.m_module_resolutions.at(attach_stmt.get());

                // If the attached module was a native library, generate prototypes for its functions.
                if (module_type->is_native) {
                    // If it's a native C module, we need to generate prototypes for its
                    // mangled C function names so our code can call them.
                    (*m_current_out) << "// --- Prototypes for Native Module: " << module_type->name << " ---\n";
                    for (const auto &[export_name, type]: module_type->exports) {
                        if (type->kind == TypeKind::FUNCTION) {
                            auto func_type = std::dynamic_pointer_cast<FunctionType>(type);

                            // Construct the mangled name, e.g., "Angara_fs_read_file"
                            std::string mangled_name = "Angara_" + module_type->name + "_" + export_name;

                            (*m_current_out) << "extern AngaraObject " << mangled_name
                                             << "(int arg_count, AngaraObject args[]);\n";
                        }
                    }
                }
            }
        }

    // --- Add the module initializer prototype ---
    (*m_current_out) << "\n// --- Module Initializer ---\n";
    (*m_current_out) << "void Angara_" << module_name << "_init_globals(void);\n";
}

void CTranspiler::pass_3_generate_globals_and_implementations(const std::vector<std::shared_ptr<Stmt>>& statements, const std::string& module_name) {
    // --- Stage A: Define Storage for all global symbols ---
    (*m_current_out) << "// --- Global Variable & Function Closure Storage ---\n";
    for (const auto& stmt : statements) {
        if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
             (*m_current_out) << "AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";
        } else if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            (*m_current_out) << "AngaraObject " << module_name << "_" << var_decl->name.lexeme << ";\n";
        } else if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            std::string var_name = "g_" + func_stmt->name.lexeme;
            if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
            (*m_current_out) << "AngaraObject " << var_name << ";\n";
        }
    }
    (*m_current_out) << "\n";

        // --- Stage B: Internal Forward Declarations (in the .c file) ---
        (*m_current_out) << "\n// --- Internal Forward Declarations ---\n";
        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                // Determine the C linkage based on the 'export' keyword.
                std::string linkage = (func_stmt->is_exported || func_stmt->name.lexeme == "main") ? "" : "static ";

                // Generate the prototype for the real function with the correct linkage.
                (*m_current_out) << linkage;
                transpileFunctionSignature(*func_stmt, module_name);
                (*m_current_out) << ";\n";

                // Generate the prototype for the wrapper with the correct linkage.
                std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";
                (*m_current_out) << linkage << "AngaraObject angara_w_" << mangled_name << "(int arg_count, AngaraObject args[]);\n";
            }
        }
    (*m_current_out) << "\n";

    // --- Stage C: Global Initializer Function ---
    std::string init_func_name = "Angara_" + module_name + "_init_globals";
    (*m_current_out) << "void " << init_func_name << "(void) {\n";
    m_indent_level = 1;
    for (const auto& stmt : statements) {
         if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            indent();
            (*m_current_out) << module_name << "_" << var_decl->name.lexeme << " = " << transpileExpr(var_decl->initializer) << ";\n";
         } else if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            std::string var_name = "g_" + func_stmt->name.lexeme;
            if (func_stmt->name.lexeme == "main") var_name = "g_angara_main_closure";
            std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
            if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";
            indent();
            (*m_current_out) << var_name << " = angara_closure_new(&angara_w_" << mangled_name << ", " << func_stmt->params.size() << ", false);\n";
         } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
             indent();
             (*m_current_out) << "g_" << class_stmt->name.lexeme << "_class = (AngaraClass){{OBJ_CLASS, 1}, \"" << class_stmt->name.lexeme << "\"};\n";
         }
    }
    m_indent_level = 0;
    (*m_current_out) << "}\n\n";

    // --- Stage D: Function Implementations ---
    (*m_current_out) << "// --- Function Implementations ---\n";
    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            transpileGlobalFunction(*func_stmt, module_name);
        } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
            m_current_class_name = class_stmt->name.lexeme;
            auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);

            // Generate _new constructor implementation
            transpileClassNew(*class_stmt);

            // Generate Method Implementations
            for (const auto& member : class_stmt->members) {
                if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                    transpileMethodBody(*class_type, *method_member->declaration);
                }
            }
            m_current_class_name = "";
        }
    }
}

    void CTranspiler::transpileClassNew(const ClassStmt& stmt) {
    // This helper generates the implementation for the public `_new` constructor function.
    // This function is responsible for allocating memory and calling the user-defined `init`.

    auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
    std::string c_struct_name = "Angara_" + class_type->name;

    // 1. Generate the function signature.
    (*m_current_out) << "AngaraObject Angara_" << class_type->name << "_new(";

    auto init_method_ast = findMethodAst(stmt, "init");
    if (init_method_ast) {
        // If a custom 'init' exists, the '_new' function takes its parameters.
        auto init_info = class_type->methods.at("init");
        auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_info.type);
        for (size_t i = 0; i < init_method_ast->params.size(); ++i) {
             (*m_current_out) << getCType(init_func_type->param_types[i]) << " " << init_method_ast->params[i].name.lexeme << (i == init_method_ast->params.size() - 1 ? "" : ", ");
        }
    } else {
        // If there is no 'init', it's a default constructor with no parameters.
        (*m_current_out) << "void";
    }
    (*m_current_out) << ") {\n";
    m_indent_level++;

    // 2. Generate the function body.
    //    Step 2a: Allocate memory for the instance struct.
    indent();
    (*m_current_out) << c_struct_name << "* instance = (" << c_struct_name
                     << "*)angara_instance_new(sizeof(" << c_struct_name
                     << "), &g_" << class_type->name << "_class);\n";

    //    Step 2b: Box the raw C pointer into a generic AngaraObject.
    indent();
    (*m_current_out) << "AngaraObject this_obj = (AngaraObject){VAL_OBJ, {.obj = (Object*)instance}};\n";

    //    Step 2c: Conditionally call the user-defined `_init` method.
    if (init_method_ast) {
        indent();
        (*m_current_out) << "Angara_" << class_type->name << "_init(this_obj";
        for (const auto& param : init_method_ast->params) {
            (*m_current_out) << ", " << param.name.lexeme;
        }
        (*m_current_out) << ");\n";
    }

    //    Step 2d: Return the new instance.
    indent();
    (*m_current_out) << "return this_obj;\n";

    m_indent_level--;
    (*m_current_out) << "}\n\n";
}

// --- `transpileGlobalFunction` Helper ---
void CTranspiler::transpileGlobalFunction(const FuncStmt& stmt, const std::string& module_name) {
    // This helper is called during Pass 3 to generate the full implementation
    // for a single top-level Angara function.

    // 1. Get the canonical FunctionType from the Type Checker's results.
    auto symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
    auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);

    // 2. Determine the C linkage. Private functions become `static`, making them
    //    invisible to the C linker outside of this file.
    std::string linkage = (stmt.is_exported || stmt.name.lexeme == "main") ? "" : "static ";

    // 3. Determine the mangled name for the C function implementation.
    std::string mangled_impl_name = "angara_f_" + module_name + "_" + stmt.name.lexeme;
    if (stmt.name.lexeme == "main") {
        mangled_impl_name = "angara_f_main";
    }

    // --- Generate the actual, strongly-typed C function ---
    (*m_current_out) << linkage; // Prepend 'static ' if necessary.
    transpileFunctionSignature(stmt, module_name);
    (*m_current_out) << " {\n";
    m_indent_level = 1;

    // Transpile the function's body.
    if (stmt.body) {
        for (const auto& body_stmt : *stmt.body) {
            transpileStmt(body_stmt);
        }
    }

    // Handle implicit returns for functions that should return void.
    if (func_type->return_type->toString() == "void") {
        if (stmt.body->empty() || !isa<ReturnStmt>(stmt.body->back())) {
             indent();
             (*m_current_out) << "return;\n";
        }
    }
    m_indent_level = 0;
    (*m_current_out) << "}\n\n";


    // --- Generate the generic wrapper function ---
    // Naming convention for the generic wrapper.
    std::string mangled_wrapper_name = "angara_w_" + mangled_impl_name;

    // The wrapper must also have `static` linkage if the function is private.
    (*m_current_out) << linkage << "AngaraObject " << mangled_wrapper_name << "(int arg_count, AngaraObject args[]) {\n";
    m_indent_level = 1;
    indent();

    // Generate the call inside the wrapper, handling void vs. non-void returns.
    if (func_type->return_type->toString() == "void") {
        (*m_current_out) << mangled_impl_name << "(";
        for (int i = 0; i < stmt.params.size(); ++i) {
            (*m_current_out) << "args[" << i << "]" << (i == stmt.params.size() - 1 ? "" : ", ");
        }
        (*m_current_out) << ");\n";
        indent();
        (*m_current_out) << "return angara_create_nil();\n";
    } else {
        (*m_current_out) << "return " << mangled_impl_name << "(";
        for (int i = 0; i < stmt.params.size(); ++i) {
            (*m_current_out) << "args[" << i << "]" << (i == stmt.params.size() - 1 ? "" : ", ");
        }
        (*m_current_out) << ");\n";
    }
    m_indent_level = 0;
    (*m_current_out) << "}\n\n";
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

        m_indent_level--;
        (*m_current_out) << "};\n\n";
    }


// --- Main Orchestrator ---


    TranspileResult CTranspiler::generate(const std::vector<std::shared_ptr<Stmt>>& statements, const std::string& module_name, std::vector<std::string>& all_module_names) {
        if (m_hadError) return {};

        this->m_current_module_name = module_name;

        // --- Pass 0: Handle Attachments ---
        m_current_out = &m_header_out; // write prototypes to the header

        // Pass 0: Handle Attachments for the HEADER file
        for (const auto& stmt : statements) {
            if (auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
                auto module_type = m_type_checker.m_module_resolutions.at(attach.get());
                // ONLY include headers for OTHER ANGARA modules.
                if (!module_type->is_native) {
                    m_header_out << "#include \"" << module_type->name << ".h\"\n";
                }
            }
        }
        m_header_out << "\n";
        (*m_current_out) << "\n";

        // --- HEADER FILE GENERATION ---
        m_current_out = &m_header_out; // Set context for Pass 1 & 2
        m_indent_level = 0;

        std::string header_guard = "ANGARA_GEN_" + module_name + "_H";
        *m_current_out << "#ifndef " << header_guard << "\n";
        *m_current_out << "#define " << header_guard << "\n\n";
        *m_current_out << "#include \"angara_runtime.h\"\n\n";

        pass_1_generate_structs(statements);
        pass_2_generate_declarations(statements, module_name);

        *m_current_out << "\n#endif //" << header_guard << "\n";


        // --- SOURCE FILE GENERATION ---
        m_current_out = &m_source_out; // Set context for Pass 3 & 4
        m_indent_level = 0;

        *m_current_out << "#include \"" << module_name << ".h\"\n\n";

        pass_3_generate_globals_and_implementations(statements, module_name);

        // Pass 5: Generate the C main() function if this is the main module.
        auto main_symbol = m_type_checker.m_symbols.resolve("main");
        if (main_symbol) {
            pass_5_generate_main(statements, module_name, all_module_names);
        }

        if (m_hadError) return {};

        // Assemble the final source file from BOTH the main source stream
        // AND the main body stream.
        std::string final_source = m_source_out.str();
        if (main_symbol) {
            final_source += m_main_body.str();
        }

        if (m_hadError) return {};
        return {m_header_out.str(), final_source};
    }


    void CTranspiler::pass_1_generate_structs(const std::vector<std::shared_ptr<Stmt>>& statements) {
        (*m_current_out) << "// --- Struct Definitions ---\n";
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                transpileStruct(*class_stmt);
                (*m_current_out) << "extern AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";
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

    void CTranspiler::pass_5_generate_main(const std::vector<std::shared_ptr<Stmt>>& statements,
                                       const std::string& module_name,
                                       const std::vector<std::string>& all_module_names) {
        m_current_out = &m_main_body;
        m_indent_level = 0;

        *m_current_out << "// --- C Entry Point ---\n";
        *m_current_out << "int main(int argc, const char* argv[]) {\n";
        m_indent_level++;

        indent(); *m_current_out << "angara_runtime_init();\n\n";
        indent(); (*m_current_out) << "// --- Initialize All Modules ---\n";
        for (const std::string& mod_name : all_module_names) {
            indent();
            (*m_current_out) << "Angara_" << mod_name << "_init_globals();\n";
        }
        (*m_current_out) << "\n";

        // Call the initializers for ALL compiled modules.
        // This requires the driver to pass the list of module names.
        // Let's assume a simpler model for now where we only init the main module.
        indent(); *m_current_out << "Angara_" << module_name << "_init_globals();\n\n";

        auto main_symbol = m_type_checker.m_symbols.resolve("main");
        auto main_func_type = std::dynamic_pointer_cast<FunctionType>(main_symbol->type);

        indent(); *m_current_out << "// Call the user's Angara main function\n";
        // The global variable 'main' was created and initialized in init_globals.
        if (main_func_type->param_types.empty()) {
            (*m_current_out) << "    AngaraObject result = angara_call(g_angara_main_closure, 0, NULL);\n";
        } else {
            *m_current_out << "    AngaraObject args_list = angara_list_new();\n";
            *m_current_out << "    for (int i = 0; i < argc; i++) {\n";
            *m_current_out << "        angara_list_push(args_list, angara_string_from_c(argv[i]));\n";
            *m_current_out << "    }\n";
            *m_current_out << "    AngaraObject result = angara_call(g_angara_main_closure, 1, &args_list);\n";
            *m_current_out << "    angara_decref(args_list);\n";
        }

        *m_current_out << "\n";
        indent(); *m_current_out << "int exit_code = (int)AS_I64(result);\n";
        indent(); *m_current_out << "angara_decref(result);\n\n";
        indent(); *m_current_out << "angara_runtime_shutdown();\n";
        indent(); *m_current_out << "return exit_code;\n";

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
            (*m_current_out) << " = angara_create_nil()";
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

    std::string CTranspiler::transpileVarExpr(const VarExpr& expr) {
        // 1. Look up the result of the Type Checker's resolution for this node.
        auto symbol = m_type_checker.m_variable_resolutions.at(&expr);

        // 2. Check the symbol's scope depth.
        if (symbol->depth == 0) {
            // It's a global variable. It needs to be mangled.
            // We assume the transpiler has the current module name.
            return m_current_module_name + "_" + expr.name.lexeme;
        } else {
            // It's a local or a parameter. No mangling needed.
            return expr.name.lexeme;
        }
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
            return transpileVarExpr(*var);
        } else if (auto grouping = std::dynamic_pointer_cast<const Grouping>(expr)) {
            return transpileGrouping(*grouping);
        } else if (auto logical = std::dynamic_pointer_cast<const LogicalExpr>(expr)) {
            return transpileLogical(*logical);
        } else if (auto update = std::dynamic_pointer_cast<const UpdateExpr>(expr)) {
            return transpileUpdate(*update);
        } else if (auto ternary = std::dynamic_pointer_cast<const TernaryExpr>(expr)) {
            return transpileTernary(*ternary);
        }
        else if (auto list = std::dynamic_pointer_cast<const ListExpr>(expr)) {
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
        }
        return "/* unknown expr */";
    }

    std::string CTranspiler::transpileSuperExpr(const SuperExpr& expr) {
        return "/* super." + expr.method.lexeme + " */"; // TODO :: Keep as placeholder for now
    }

    std::string CTranspiler::transpileLiteral(const Literal& expr) {
        auto type = m_type_checker.m_expression_types.at(&expr);
        if (type->toString() == "i64") return "angara_create_i64(" + expr.token.lexeme + "LL)";
        if (type->toString() == "f64") return "angara_create_f64(" + expr.token.lexeme + ")";
        if (type->toString() == "bool") return "angara_create_bool(" + expr.token.lexeme + ")";
        if (type->toString() == "string") return "angara_string_from_c(\"" + expr.token.lexeme + "\")";
        if (type->toString() == "nil") return "angara_create_nil()";
        return "angara_create_nil() /* unknown literal */";
    }

    std::string CTranspiler::transpileBinary(const Binary& expr) {
        // 1. Recursively transpile the left and right sub-expressions.
        std::string lhs_str = transpileExpr(expr.left);
        std::string rhs_str = transpileExpr(expr.right);
        std::string op = expr.op.lexeme;

        // 2. Get the pre-computed types of the operands from the Type Checker.
        auto lhs_type = m_type_checker.m_expression_types.at(expr.left.get());
        auto rhs_type = m_type_checker.m_expression_types.at(expr.right.get());

        // 3. Dispatch based on the operator type.
        switch (expr.op.type) {
            // --- Equality Operators (handle all types) ---
            case TokenType::EQUAL_EQUAL:
                return "angara_equals(" + lhs_str + ", " + rhs_str + ")";
            case TokenType::BANG_EQUAL:
                return "angara_create_bool(!AS_BOOL(angara_equals(" + lhs_str + ", " + rhs_str + ")))";

                // --- Comparison Operators (numeric only) ---
            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                // The Type Checker guarantees these are numeric. We promote to float for a safe comparison.
                return "angara_create_bool((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";

                // --- Arithmetic Operators (numeric only) ---
            case TokenType::PLUS:
                // --- MODIFIED LOGIC FOR '+' ---
                if (lhs_type->toString() == "string") {
                    // If the LHS is a string, the type checker guarantees the RHS is also a string.
                    return "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
                } else {
                    // Otherwise, it's numeric addition.
                    auto rhs_type = m_type_checker.m_expression_types.at(expr.right.get());
                    if (isFloat(lhs_type) || isFloat(rhs_type)) {
                        return "create_f64((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";
                    } else {
                        return "create_i64((AS_I64(" + lhs_str + ") " + op + " AS_I64(" + rhs_str + ")))";
                    }
                }
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
                if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    return "angara_create_f64((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";
                } else {
                    return "angara_create_i64((AS_I64(" + lhs_str + ") " + op + " AS_I64(" + rhs_str + ")))";
                }

            case TokenType::PERCENT:
                if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    return "angara_create_f64(fmod(AS_F64(" + lhs_str + "), AS_F64(" + rhs_str + ")))";
                } else {
                    return "angara_create_i64((AS_I64(" + lhs_str + ") % AS_I64(" + rhs_str + ")))";
                }

            default:
                // String concatenation is handled by the PLUS case if types match.
                // Any other operator on non-numeric types would have been caught by the Type Checker.
                return "angara_create_nil() /* unreachable */";
        }
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
        return "angara_create_bool((" + lhs + ") " + op + " (" + rhs + "))";
    }

    std::string CTranspiler::transpileUpdate(const UpdateExpr& expr) {
        // We can only increment/decrement variables, which are valid l-values.
        // The C code needs to pass the ADDRESS of the variable to the runtime helper.
        std::string target_str = transpileExpr(expr.target);

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
                return "angara_create_bool(!angara_is_truthy(" + operand_str + "))";
            }

            case TokenType::MINUS: {
                // Transpiles `-some_expression`.
                // The Type Checker has already guaranteed the operand is a number.
                // We just need to generate the correct C code for its specific type.
                if (isFloat(operand_type)) {
                    // Unbox as a double, negate, and re-box as a new f64 AngaraObject.
                    return "angara_create_f64(-AS_F64(" + operand_str + "))";
                } else {
                    // Unbox as an int64, negate, and re-box as a new i64 AngaraObject.
                    return "angara_create_i64(-AS_I64(" + operand_str + "))";
                }
            }

            default:
                // This should be unreachable if the parser and type checker are correct.
                    return "angara_create_nil() /* unsupported unary op */";
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

    std::string join_strings(const std::vector<std::string>& elements, const std::string& separator) {
        std::stringstream ss;
        for (size_t i = 0; i < elements.size(); ++i) {
            ss << elements[i];
            if (i < elements.size() - 1) {
                ss << separator;
            }
        }
        return ss.str();
    }

    std::string CTranspiler::transpileCallExpr(const CallExpr& expr) {
    // 1. Transpile all arguments into a clean vector of C code strings.
    std::vector<std::string> arg_strs;
    for (const auto& arg : expr.arguments) {
        arg_strs.push_back(transpileExpr(arg));
    }
    std::string args_str = join_strings(arg_strs, ", ");

    // 2. Determine what kind of thing is being called.

    // --- Case 1: An instance method call (e.g., `p1.move(...)` or `handle.join()`) ---
    if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
        std::string object_str = transpileExpr(get_expr->object);
        const std::string& method_name = get_expr->name.lexeme;
        auto object_type = m_type_checker.m_expression_types.at(get_expr->object.get());

        if (object_type->kind == TypeKind::INSTANCE) {
            auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
            const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), method_name);
            std::string owner_class_name = owner_class ? owner_class->name : "UNKNOWN_CLASS";

            std::string final_args = object_str; // The instance is the first implicit 'this'
            if (!args_str.empty()) {
                final_args += ", " + args_str;
            }
            return "Angara_" + owner_class_name + "_" + method_name + "(" + final_args + ")";
        }
        if (object_type->kind == TypeKind::THREAD) {
            if (method_name == "join") return "angara_thread_join(" + object_str + ")";
        }
        if (object_type->kind == TypeKind::MUTEX) {
            if (method_name == "lock") return "angara_mutex_lock(" + object_str + ")";
            if (method_name == "unlock") return "angara_mutex_unlock(" + object_str + ")";
        }
        if (object_type->kind == TypeKind::LIST) {
             if (method_name == "push") return "angara_list_push(" + object_str + ", " + args_str + ")";
        }

        if (object_type->kind == TypeKind::MODULE) {
            auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);
            if (module_type->is_native) {
                // It's a call to a native C function.
                std::string func_name = get_expr->name.lexeme;
                std::string mangled_func_name = "Angara_" + module_type->name + "_" + get_expr->name.lexeme;
                return mangled_func_name + "(" + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
            }
        }
    }

    // --- Case 2: A direct call to a named entity (e.g., `print(...)`, `Point(...)`, `my_func(...)`) ---
    if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
        const std::string& func_name = var_expr->name.lexeme;

        if (func_name == "main") {
            return "angara_call(angara_main_closure"  ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
        }

        if (func_name == "len") {
            return "angara_len(" + args_str + ")";
        }
        if (func_name == "typeof") {
            return "angara_typeof(" + args_str + ")";
        }
        if (func_name == "spawn") {
            // The argument to spawn is a closure. The C code needs its wrapper function.
             if (auto arg_var = std::dynamic_pointer_cast<const VarExpr>(expr.arguments[0])) {
                std::string wrapper_name = "&wrapper_" + arg_var->name.lexeme;
                auto func_sym = m_type_checker.m_symbols.resolve(arg_var->name.lexeme);
                auto func_type = std::dynamic_pointer_cast<FunctionType>(func_sym->type);
                return "angara_spawn(angara_closure_new(" + wrapper_name + ", " + std::to_string(func_type->param_types.size()) + ", false))";
             }
        }

        if (func_name == "string") {
            // The type checker already verified it has exactly one argument.
            return "angara_to_string(" + args_str + ")";
        }

        if (func_name == "Mutex") {
            return "angara_mutex_new()";
        }

        if (func_name == "i64" || func_name == "int") {
            return "angara_to_i64(" + args_str + ")";
        }
        if (func_name == "f64" || func_name == "float") {
            return "angara_to_f64(" + args_str + ")";
        }
        if (func_name == "bool") {
            return "angara_to_bool(" + args_str + ")";
        }


        // Check if it's a class constructor call
        auto callee_type = m_type_checker.m_expression_types.at(expr.callee.get());
        if (callee_type->kind == TypeKind::CLASS) {
            return "Angara_" + func_name + "_new(" + args_str + ")";
        }

        // If it's not a built-in or a constructor, it must be a global function.
        std::string closure_var = "g_" + func_name;
        if (func_name == "main") closure_var = "g_angara_main_closure"; // Special case for main

        return "angara_call(" + closure_var + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
    }


    // --- Case 3: A method or module call ---
    if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
        // transpileGetExpr will correctly generate the mangled global variable name,
        // e.g., `utils_circle_area`. This is our closure variable.
        std::string callee_var = "g_" + get_expr->name.lexeme;
        return "angara_call(" + callee_var + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
    }

    // Fallback for any other kind of callable expression (e.g., calling a function returned from another function)
    std::string callee_str = transpileExpr(expr.callee);
        return "angara_call(" + callee_str + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
}

std::string CTranspiler::transpileAssignExpr(const AssignExpr& expr) {
    auto rhs_str = transpileExpr(expr.value);
    auto lhs_str = transpileExpr(expr.target);

        if (auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            // This is a special case that doesn't transpile to a simple C assignment.
            // We must generate a call to a runtime setter function.

            std::string object_str = transpileExpr(subscript_target->object);
            std::string value_str = transpileExpr(expr.value);
            auto collection_type = m_type_checker.m_expression_types.at(subscript_target->object.get());

            if (collection_type->kind == TypeKind::LIST) {
                std::string index_str = transpileExpr(subscript_target->index);
                // Generates: angara_list_set(list, index, value);
                return "angara_list_set(" + object_str + ", " + index_str + ", " + value_str + ")";
            }

            if (collection_type->kind == TypeKind::RECORD) {
                // The type checker guarantees the index is a string literal.
                if (auto key_literal = std::dynamic_pointer_cast<const Literal>(subscript_target->index)) {
                    // Generates: angara_record_set(record, "key", value);
                    return "angara_record_set(" + object_str + ", \"" + key_literal->token.lexeme + "\", " + value_str + ")";
                }
            }

            return "/* unsupported subscript assignment */";
        }

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
            full_expression = "angara_create_i64((AS_I64(" + lhs_str + ") " + core_op + " AS_I64(" + rhs_str + ")))";
        } else if (isFloat(target_type)) {
            // e.g., create_f64((AS_F64(x) + AS_F64(y)))
            // Note: This correctly handles the case where one is an int and one is a float
            // because our AS_F64 macro performs the promotion.
            full_expression = "angara_create_f64((AS_F64(" + lhs_str + ") " + core_op + " AS_F64(" + rhs_str + ")))";
        } else if (target_type->toString() == "string" && expr.op.type == TokenType::PLUS_EQUAL) {
            // String concatenation: x = angara_string_concat(x, y)
            full_expression = "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
        } else {
            // Should be unreachable if the Type Checker is correct.
            full_expression = "angara_create_nil() /* unsupported compound assignment */";
        }

        // 4. Return the full assignment expression.
        return "(" + lhs_str + " = " + full_expression + ")";
    }
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
        // 1. Transpile the object on the left of the dot.
        std::string object_str = transpileExpr(expr.object);
        const std::string& prop_name = expr.name.lexeme;

        // 2. Get the pre-computed type of the object from the Type Checker.
        auto object_type = m_type_checker.m_expression_types.at(expr.object.get());

        if (object_type->kind == TypeKind::MODULE) {
            auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);
            auto symbol = module_type->exports.at(expr.name.lexeme);
            // Is the symbol from a native C module or a transpiled Angara module?
            // We need to know this. Let's add a flag to ModuleType.
            if (module_type->is_native) {
                // Native function names are not mangled.
                return expr.name.lexeme;
            } else {
                // Transpiled Angara symbol.
                return module_type->name + "_" + expr.name.lexeme;
            }
        }
        // Case 2: The object is a class instance.
        else if (object_type->kind == TypeKind::INSTANCE) {
            auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
            std::string c_struct_name = "Angara_" + instance_type->class_type->name;

            const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), prop_name);

            std::string access_path = "->";
            const ClassType* current = instance_type->class_type.get();
            while (current && owner_class && current->name != owner_class->name) {
                access_path += "parent.";
                current = current->superclass.get();
            }
            access_path += prop_name;

            return "((" + c_struct_name + "*)AS_INSTANCE(" + object_str + "))" + access_path;
        }

        // Fallback for any other type (should have been caught by Type Checker)
        return "/* unsupported get expr */";
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
        indent(); (*m_current_out) << "g_current_exception = angara_create_nil();\n";

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
    (*m_current_out) << "AngaraObject __index_" << stmt.name.lexeme << " = angara_create_i64(0LL);\n";

    // 3. Generate the `while` loop header.
    indent();
    (*m_current_out) << "while (angara_is_truthy(angara_create_bool(AS_I64(__index_" << stmt.name.lexeme << ") < AS_I64(angara_len(__collection_" << stmt.name.lexeme << "))))) {\n";
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
    indent(); (*m_current_out) << "AngaraObject __temp_one = angara_create_i64(1LL);\n";
    indent(); (*m_current_out) << "AngaraObject __new_index = angara_create_i64(AS_I64(__index_" << stmt.name.lexeme << ") + AS_I64(__temp_one));\n";
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

    std::string CTranspiler::transpileSubscriptExpr(const SubscriptExpr& expr) {
        // 1. Get the pre-computed type of the object being accessed.
        auto collection_type = m_type_checker.m_expression_types.at(expr.object.get());
        std::string object_str = transpileExpr(expr.object);

        // 2. Dispatch based on the collection's type.
        if (collection_type->kind == TypeKind::LIST) {
            std::string index_str = transpileExpr(expr.index);
            return "angara_list_get(" + object_str + ", " + index_str + ")";
        }

        if (collection_type->kind == TypeKind::RECORD) {
            // The type checker guarantees the index is a string literal for records.
            if (auto key_literal = std::dynamic_pointer_cast<const Literal>(expr.index)) {
                // We need the raw lexeme for the C function call.
                return "angara_record_get(" + object_str + ", \"" + key_literal->token.lexeme + "\")";
            }
        }

        // Fallback if the type checker somehow let a non-subscriptable type through.
        return "/* unsupported subscript */";
    }


} // namespace angara