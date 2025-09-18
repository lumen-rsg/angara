#include "CTranspiler.h"
#include <stdexcept>

namespace angara {

    template <typename T, typename U>
    inline bool isa(const std::shared_ptr<U>& ptr) {
        return std::dynamic_pointer_cast<const T>(ptr) != nullptr;
    }

    std::string CTranspiler::escape_c_string(const std::string& str) {
        std::stringstream ss;
        for (char c : str) {
            switch (c) {
                case '\\': ss << "\\\\"; break;
                case '"':  ss << "\\\""; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                    // Add other standard C escapes as needed
                default:
                    ss << c;
                    break;
            }
        }
        return ss.str();
    }

    std::string CTranspiler::sanitize_name(const std::string& name) {
        // A simple set of C keywords to avoid.
        static const std::set<std::string> c_keywords = {
                "auto", "break", "case", "char", "const", "continue", "default",
                "do", "double", "else", "enum", "extern", "float", "for", "goto",
                "if", "int", "long", "register", "return", "short", "signed",
                "sizeof", "static", "struct", "switch", "typedef", "union",
                "unsigned", "void", "volatile", "while"
        };
        if (c_keywords.count(name)) {
            return name + "_";
        }
        return name;
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
        if (!angaraType) return "void /* unknown type */";
        // All other reference types are AngaraObject.
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

    (*m_current_out) << "\n// --- Imported Symbol Declarations ---\n";
    for (const auto& stmt : statements) {
        if (auto attach_stmt = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
            auto imported_module_type = m_type_checker.m_module_resolutions.at(attach_stmt.get());
            if (!imported_module_type->is_native) continue;

            (*m_current_out) << "// --- Prototypes for Native Module: " << imported_module_type->name << " ---\n";
            for (const auto& [export_name, type] : imported_module_type->exports) {

                // We only generate prototypes for functions, not for ClassTypes that are also exported.
                if (type->kind != TypeKind::FUNCTION) continue;

                auto func_type = std::dynamic_pointer_cast<FunctionType>(type);
                std::string mangled_name = "Angara_" + imported_module_type->name + "_" + export_name;

                // All native global functions AND constructors use the generic signature.
                (*m_current_out) << "extern AngaraObject " << mangled_name
                                 << "(int arg_count, AngaraObject* args);\n";

                // --- THE FIX IS HERE ---
                // If this function is a constructor, its return type will be an InstanceType.
                // We use this to find the class and generate prototypes for its methods.
                if (func_type->return_type->kind == TypeKind::INSTANCE) {
                    auto instance_type = std::dynamic_pointer_cast<InstanceType>(func_type->return_type);
                    auto class_type = instance_type->class_type;

                    for (const auto& [method_name, method_info] : class_type->methods) {
                        // Method mangled name is Angara_ClassName_MethodName
                        std::string method_mangled_name = "Angara_" + class_type->name + "_" + method_name;
                        // All native methods also use the generic signature.
                        (*m_current_out) << "extern AngaraObject " << method_mangled_name
                                         << "(int arg_count, AngaraObject* args);\n";
                    }
                }
            }
        }
    }

    // --- Add the current module's initializer prototype (Unchanged) ---
    (*m_current_out) << "\n// --- Module Initializer ---\n";
    (*m_current_out) << "void Angara_" << module_name << "_init_globals(void);\n";

}

    void CTranspiler::pass_3_generate_globals_and_implementations(
            const std::vector<std::shared_ptr<Stmt>>& statements,
            const std::string& module_name
    ) {
        // === Stage A: Global Variable & Function Closure Storage ===
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

        // === Stage B: COMPLETE Forward Declarations for ALL Functions/Methods ===
        (*m_current_out) << "\n// --- Internal Forward Declarations ---\n";

        // Pass B.1: Declare all global functions AND their wrappers
        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                std::string mangled_name = "angara_f_" + module_name + "_" + func_stmt->name.lexeme;
                if (func_stmt->name.lexeme == "main") mangled_name = "angara_f_main";

                // Always use 'static' for prototypes of non-exported functions inside the .c file
                std::string linkage = (func_stmt->is_exported || func_stmt->name.lexeme == "main") ? "" : "static ";

                (*m_current_out) << linkage;
                transpileFunctionSignature(*func_stmt, module_name);
                (*m_current_out) << ";\n";
                (*m_current_out) << linkage << "AngaraObject angara_w_" << mangled_name << "(int arg_count, AngaraObject args[]);\n";
            }
        }

        // Pass B.2: Declare all class methods (they are never static in the C file)
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                // Forward declare the _new constructor
                auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
                (*m_current_out) << "AngaraObject Angara_" << class_stmt->name.lexeme << "_new(";
                auto init_it = class_type->methods.find("init");
                if (init_it != class_type->methods.end()) {
                    auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
                    for (size_t i = 0; i < init_func_type->param_types.size(); ++i) {
                        (*m_current_out) << getCType(init_func_type->param_types[i]) << (i == init_func_type->param_types.size() - 1 ? "" : ", ");
                    }
                }
                (*m_current_out) << ");\n";

                // Forward declare all methods
                for (const auto& member : class_stmt->members) {
                    if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                        transpileMethodSignature(class_stmt->name.lexeme, *method_member->declaration);
                        (*m_current_out) << ";\n";
                    }
                }
            }
        }
        (*m_current_out) << "\n";

        // === Stage C: Global Initializer Function ===
        std::string init_func_name = "Angara_" + module_name + "_init_globals";
        (*m_current_out) << "void " << init_func_name << "(void) {\n";
        m_indent_level = 1;
        for (const auto& stmt : statements) {
            if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
                indent();
                (*m_current_out) << module_name << "_" << var_decl->name.lexeme << " = ";
                if (var_decl->initializer) {
                    // If an initializer exists, transpile it.
                    (*m_current_out) << transpileExpr(var_decl->initializer) << ";";
                } else {
                    // If no initializer, the default value is `nil`.
                    (*m_current_out) << "angara_create_nil();";
                }
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

        // === Stage D: Function and Method Implementations ===
        (*m_current_out) << "// --- Function Implementations ---\n";
        for (const auto& stmt : statements) {
            if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                transpileGlobalFunction(*func_stmt, module_name);
            } else if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                m_current_class_name = class_stmt->name.lexeme;
                auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(class_stmt->name.lexeme)->type);
                transpileClassNew(*class_stmt);
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

    void CTranspiler::transpileDataStruct(const DataStmt& stmt) {
        auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
        std::string c_struct_name = "Angara_" + data_type->name;

        // Use a typedef for a clean name in C.
        (*m_current_out) << "typedef struct " << c_struct_name << " " << c_struct_name << ";\n";
        (*m_current_out) << "struct " << c_struct_name << " {\n";
        m_indent_level++;

        // --- THE KEY ---
        // The struct starts with the common Object header, just like a class instance.
        indent(); (*m_current_out) << "Object obj;\n";

        // Define all the fields.
        // We iterate the AST `fields` to preserve the declaration order.
        for (const auto& field_decl : stmt.fields) {
            // We get the canonical, resolved type from the DataType.
            auto field_info = data_type->fields.at(field_decl->name.lexeme);
            indent();
            // getCType will correctly return `AngaraObject` for all fields now.
            (*m_current_out) << getCType(field_info.type) << " "
                             << sanitize_name(field_decl->name.lexeme) << ";\n";
        }

        m_indent_level--;
        (*m_current_out) << "};\n\n";
    }

    void CTranspiler::transpileDataConstructor(const DataStmt& stmt) {
    auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
    std::string c_struct_name = "Angara_" + data_type->name;
    std::string c_func_name = "Angara_data_new_" + data_type->name;

    // --- 1. Generate the function signature (unchanged) ---
    (*m_current_out) << "static inline AngaraObject " << c_func_name << "(";
    const auto& fields = stmt.fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field_decl = fields[i];
        auto field_type = data_type->fields.at(field_decl->name.lexeme).type;
        (*m_current_out) << getCType(field_type) << " " << sanitize_name(field_decl->name.lexeme);
        if (i < fields.size() - 1) {
            (*m_current_out) << ", ";
        }
    }
    (*m_current_out) << ") {\n";
    m_indent_level++;

    // --- 2. Generate the function body ---

    // 2a. Malloc memory for the struct.
    indent();
    (*m_current_out) << c_struct_name << "* data = (" << c_struct_name << "*)malloc(sizeof(" << c_struct_name << "));\n";

    // --- THE FIX: Add a check for malloc failure ---
    indent();
    (*m_current_out) << "if (data == NULL) {\n";
    m_indent_level++;
    indent();
    // Throw a standard error message. This call does not return.
    (*m_current_out) << "angara_throw_error(\"Out of memory: failed to allocate data instance for '" << data_type->name << "'.\");\n";
    m_indent_level--;
    indent();
    (*m_current_out) << "}\n";
    // --- END FIX ---

    // 2b. Initialize the Angara Object header.
    indent();
    (*m_current_out) << "data->obj.type = OBJ_DATA_INSTANCE;\n";
    indent();
    (*m_current_out) << "data->obj.ref_count = 1;\n";

    // 2c. Assign each parameter to its corresponding struct field.
    for (const auto& field_decl : fields) {
        std::string field_name = sanitize_name(field_decl->name.lexeme);
        indent();
        (*m_current_out) << "data->" << field_name << " = " << field_name << ";\n";
    }

    // 2d. "Box" the raw C pointer into a generic AngaraObject and return it.
    indent();
    (*m_current_out) << "return (AngaraObject){ VAL_OBJ, { .obj = (Object*)data } };\n";

    m_indent_level--;
    (*m_current_out) << "}\n\n";
}

// Generates the C code for a data block instantiation.
std::string CTranspiler::transpileDataLiteral(const CallExpr& expr) {
    auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_expression_types.at(expr.callee.get()));
    std::string c_struct_name = "Angara_" + data_type->name;

    std::stringstream ss;
    ss << "((" << c_struct_name << "){ ";
    for (size_t i = 0; i < expr.arguments.size(); ++i) {
        // This assumes order, a more robust version would use designated initializers.
        ss << transpileExpr(expr.arguments[i]) << (i == expr.arguments.size() - 1 ? "" : ", ");
    }
    ss << " })";
    return ss.str();
}

// Generates the C code for a data block equality comparison.
void CTranspiler::transpileDataEqualsImplementation(const DataStmt& stmt) {
    auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
    std::string c_struct_name = "Angara_" + data_type->name;
    std::string func_name = c_struct_name + "_equals";

    (*m_current_out) << "static inline bool " << func_name << "(const " << c_struct_name << "* a, const " << c_struct_name << "* b) {\n";
    m_indent_level++;
    indent();
    (*m_current_out) << "return ";

    if (stmt.fields.empty()) {
        (*m_current_out) << "true;\n";
    } else {
        for (size_t i = 0; i < stmt.fields.size(); ++i) {
            const auto& field = stmt.fields[i];
            std::string field_name = sanitize_name(field->name.lexeme);
            auto field_type = data_type->fields.at(field->name.lexeme).type;

            if (field_type->kind == TypeKind::DATA) {
                // --- THE FIX IS HERE ---
                // Case 1: The field is a nested data struct.
                // Cast the generic obj pointer to the specific struct pointer.
                std::string nested_struct_name = "Angara_" + field_type->toString();
                std::string ptr_a = "((" + nested_struct_name + "*)AS_OBJ(a->" + field_name + "))";
                std::string ptr_b = "((" + nested_struct_name + "*)AS_OBJ(b->" + field_name + "))";

                (*m_current_out) << "Angara_" << field_type->toString() << "_equals(" << ptr_a << ", " << ptr_b << ")";
            } else {
                // Case 2: The field is any other type (primitive wrapped in AngaraObject).
                (*m_current_out) << "AS_BOOL(angara_equals(a->" << field_name << ", b->" << field_name << "))";
            }
            // --- END FIX ---

            if (i < stmt.fields.size() - 1) {
                (*m_current_out) << " &&\n";
                indent();
                (*m_current_out) << "       ";
            }
        }
        (*m_current_out) << ";\n";
    }

    m_indent_level--;
    (*m_current_out) << "}\n\n";
}

// And a helper for the prototype
void CTranspiler::transpileDataEqualsPrototype(const DataStmt& stmt) {
    std::string c_struct_name = "Angara_" + stmt.name.lexeme;
    (*m_current_out) << "static inline bool " << c_struct_name << "_equals(const " << c_struct_name << "* a, const " << c_struct_name << "* b);\n";
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
    if (func_type->return_type->toString() == "nil") {
        if (stmt.body->empty() || !isa<ReturnStmt>(stmt.body->back())) {
             indent();
             (*m_current_out) << "return angara_create_nil();\n";
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
    if (func_type->return_type->toString() == "nil") {
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


    TranspileResult CTranspiler::generate(
            const std::vector<std::shared_ptr<Stmt>>& statements,
            const std::shared_ptr<ModuleType>& module_type, // <-- New parameter
            std::vector<std::string>& all_module_names
    ) {
        if (m_hadError) return {};
        const std::string& module_name = module_type->name; // <-- Get the canonical name
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
        *m_current_out << "#include <stdlib.h>\n\n";

        // --- NEW PASS 0a: Generate DATA struct definitions ---
        (*m_current_out) << "// --- Data Struct Definitions ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataStruct(*data_stmt);
            }
        }

        // --- NEW PASS 0b: Generate DATA equals function prototypes ---
        (*m_current_out) << "\n// --- Data Equals Function Prototypes ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataEqualsPrototype(*data_stmt); // We'll add this helper
            }
        }

        pass_1_generate_structs(statements);
        pass_2_generate_declarations(statements, module_name);

        *m_current_out << "\n#endif //" << header_guard << "\n";


        // --- SOURCE FILE GENERATION ---
        m_current_out = &m_source_out; // Set context for Pass 3 & 4
        m_indent_level = 0;

        *m_current_out << "#include \"" << module_name << ".h\"\n\n";

        // --- THE FIX IS HERE ---
        // PASS 2a: Generate DATA constructor helper implementations
        (*m_current_out) << "// --- Data Constructor Implementations ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataConstructor(*data_stmt);
            }
        }

        // PASS 2b: Generate DATA equals function implementations
        (*m_current_out) << "\n// --- Data Equals Function Implementations ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataEqualsImplementation(*data_stmt);
            }
        }
        // --- END FIX ---

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
                         << "Angara_" << class_name << "_" << sanitize_name(stmt.name.lexeme) << "(";

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
        if (func_type->return_type->toString() == "nil") {
            indent();
            (*m_current_out) << "return angara_create_nil();\n";
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
        } else if (auto break_stmt = std::dynamic_pointer_cast<const BreakStmt>(stmt)) { // <-- ADD THIS
            transpileBreakStmt(*break_stmt);
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
        (*m_current_out) << "AngaraObject " << sanitize_name(stmt.name.lexeme) ;

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
        // This visitor is now much more powerful.
        auto symbol_resolution = m_type_checker.m_variable_resolutions.find(&expr);
        if (symbol_resolution == m_type_checker.m_variable_resolutions.end()) {
            // This could be a reference to a module itself.
            auto symbol = m_type_checker.m_symbols.resolve(expr.name.lexeme);
            if (symbol && symbol->type->kind == TypeKind::MODULE) {
                // It's the module object. The transpiler doesn't need to do anything
                // special with it here; the GetExpr transpiler will handle it.
                return sanitize_name(expr.name.lexeme);
            }
            return "/* unresolved var: " + expr.name.lexeme + " */";
        }

        auto symbol = symbol_resolution->second;
        if (symbol->depth > 0) {
            // It's a local variable or a parameter.
            return sanitize_name(symbol->name);
        } else {
            // It's a global variable in the CURRENT module. Mangle it.
            // The closure for a function `parse` is `g_parse`. A global var `x` is `main_x`.
            if (symbol->type->kind == TypeKind::FUNCTION) {
                return "g_" + sanitize_name(symbol->name);
            }
            return m_current_module_name + "_" + sanitize_name(symbol->name);
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
        } else if (auto is_expr = std::dynamic_pointer_cast<const IsExpr>(expr)) { // <-- ADD THIS
            return transpileIsExpr(*is_expr);
        }
        return "/* unknown expr */";
    }

    std::string CTranspiler::transpileSuperExpr(const SuperExpr& expr) {
        // na.
        return {};
    }

    std::string CTranspiler::transpileLiteral(const Literal& expr) {
        auto type = m_type_checker.m_expression_types.at(&expr);
        if (type->toString() == "i64") return "angara_create_i64(" + expr.token.lexeme + "LL)";
        if (type->toString() == "f64") return "angara_create_f64(" + expr.token.lexeme + ")";
        if (type->toString() == "bool") return "angara_create_bool(" + expr.token.lexeme + ")";
        if (type->toString() == "string") {
            return "angara_string_from_c(\"" + escape_c_string(expr.token.lexeme) + "\")";
        }
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
            case TokenType::BANG_EQUAL: {
                std::string result_str;
                if (lhs_type->kind == TypeKind::DATA && rhs_type->kind == TypeKind::DATA) {
                    std::string c_struct_name = "Angara_" + lhs_type->toString();
                    std::string equals_func = c_struct_name + "_equals";

                    // --- THE FIX IS HERE ---
                    // We must first cast the AngaraObject's internal `obj` pointer to the correct
                    // C struct pointer type *before* taking its address.
                    std::string ptr_a = "((" + c_struct_name + "*)AS_OBJ(" + lhs_str + "))";
                    std::string ptr_b = "((" + c_struct_name + "*)AS_OBJ(" + rhs_str + "))";

                    result_str = equals_func + "(" + ptr_a + ", " + ptr_b + ")";
                    // --- END FIX ---

                    // The result of the _equals helper is a raw C bool. We must re-box it.
                    result_str = "angara_create_bool(" + result_str + ")";

                } else {
                    // All other types use the generic runtime equality function.
                    result_str = "angara_equals(" + lhs_str + ", " + rhs_str + ")";
                }

                if (expr.op.type == TokenType::BANG_EQUAL) {
                    return "angara_create_bool(!AS_BOOL(" + result_str + "))";
                }
                return result_str;
            }

                // --- Comparison Operators (numeric only) ---
            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                // The Type Checker guarantees these are numeric. We promote to float for a safe comparison.
                return "angara_create_bool((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";

                // --- Arithmetic Operators (numeric only) ---
            case TokenType::PLUS:
                // --- THE FIX IS HERE ---
                if (lhs_type->toString() == "string" && rhs_type->toString() == "string") {
                    return "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
                }
                // Fallthrough for numeric types
                if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    return "angara_create_f64((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";
                } else {
                    return "angara_create_i64((AS_I64(" + lhs_str + ") " + op + " AS_I64(" + rhs_str + ")))";
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
        // --- THE FIX: Handle Nil Coalescing Operator `??` ---
        if (expr.op.type == TokenType::QUESTION_QUESTION) {
            std::string lhs_str = transpileExpr(expr.left);
            std::string rhs_str = transpileExpr(expr.right);
            // Generates: (!IS_NIL(lhs) ? lhs : rhs)
            return "(!IS_NIL(" + lhs_str + ") ? " + lhs_str + " : " + rhs_str + ")";
        }

        // --- Existing logic for `&&` and `||` (unchanged) ---
        std::string lhs = "angara_is_truthy(" + transpileExpr(expr.left) + ")";
        std::string rhs = "angara_is_truthy(" + transpileExpr(expr.right) + ")";
        return "create_bool((" + lhs + ") " + expr.op.lexeme + " (" + rhs + "))";
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
        // --- THE FIX ---
        if (expr.elements.empty()) {
            return "angara_list_new()";
        }

        std::stringstream elements_ss;
        for (size_t i = 0; i < expr.elements.size(); ++i) {
            elements_ss << transpileExpr(expr.elements[i]);
            if (i < expr.elements.size() - 1) {
                elements_ss << ", ";
            }
        }
        return "angara_list_new_with_elements(" +
               std::to_string(expr.elements.size()) + ", " +
               "(AngaraObject[]){" + elements_ss.str() + "})";
    }

    std::string CTranspiler::transpileRecordExpr(const RecordExpr& expr) {
        // --- THE FIX ---
        if (expr.keys.empty()) {
            return "angara_record_new()";
        }

        std::stringstream kvs_ss;
        for (size_t i = 0; i < expr.keys.size(); ++i) {
            kvs_ss << "angara_string_from_c(\"" << escape_c_string(expr.keys[i].lexeme) << "\")";
            kvs_ss << ", ";
            kvs_ss << transpileExpr(expr.values[i]);
            if (i < expr.keys.size() - 1) {
                kvs_ss << ", ";
            }
        }
        return "angara_record_new_with_fields(" +
               std::to_string(expr.keys.size()) + ", " +
               "(AngaraObject[]){" + kvs_ss.str() + "})";
    }


    void CTranspiler::transpileIfStmt(const IfStmt& stmt) {
        // --- Case 1: Handle `if let` for optional unwrapping ---
        if (stmt.declaration) {
            // We generate a new C scope to contain the temporary variable.
            indent(); (*m_current_out) << "{\n";
            m_indent_level++;

            // 1. Evaluate the initializer into a temporary variable.
            indent();
            (*m_current_out) << "AngaraObject __tmp_if_let = " << transpileExpr(stmt.declaration->initializer) << ";\n";

            // 2. The condition is a simple nil check on the temporary.
            indent();
            (*m_current_out) << "if (!IS_NIL(__tmp_if_let)) {\n";
            m_indent_level++;

            // 3. If not nil, declare the new variable inside the `if` block.
            indent();
            (*m_current_out) << "const AngaraObject " << sanitize_name(stmt.declaration->name.lexeme)
                             << " = __tmp_if_let;\n";

            // 4. Transpile the 'then' block.
            transpileStmt(stmt.thenBranch);

            m_indent_level--;
            indent(); (*m_current_out) << "}";

            // 5. Transpile the 'else' block, if it exists.
            if (stmt.elseBranch) {
                (*m_current_out) << " else ";
                transpileStmt(stmt.elseBranch);
            } else {
                (*m_current_out) << "\n";
            }

            m_indent_level--;
            indent(); (*m_current_out) << "}\n";

            return; // We have handled the entire `if let` statement.
        }

        // --- Case 2: Handle regular `if` with a boolean condition ---
        std::string condition_str = "angara_is_truthy(" + transpileExpr(stmt.condition) + ")";
        indent();
        (*m_current_out) << "if (" << condition_str << ") ";
        transpileStmt(stmt.thenBranch);

        if (stmt.elseBranch) {
            indent(); // Indent for the 'else' keyword
            (*m_current_out) << "else ";
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



    // =======================================================================
// REPLACE the entire transpileCallExpr function in CTranspiler.cpp with this
// definitive "gold standard" version.
// =======================================================================

std::string CTranspiler::transpileCallExpr(const CallExpr& expr) {
    // 1. Transpile all arguments first, as they are needed in almost every case.
    std::vector<std::string> arg_strs;
    for (const auto& arg : expr.arguments) {
        arg_strs.push_back(transpileExpr(arg));
    }
    std::string args_str = join_strings(arg_strs, ", ");

    auto callee_type = m_type_checker.m_expression_types.at(expr.callee.get());

    // ---
    // Case 1: The callee is a property access, e.g., `object.method(...)`.
    // This is the most complex case, covering methods and module functions.
    // ---
    if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(expr.callee)) {
        std::string object_str = transpileExpr(get_expr->object);
        const std::string& name = get_expr->name.lexeme;
        auto object_type = m_type_checker.m_expression_types.at(get_expr->object.get());

        // A) Method call on a built-in primitive type. This has the highest priority.
        if (object_type->kind == TypeKind::THREAD && name == "join") {
            return "angara_thread_join(" + object_str + ")";
        }
        if (object_type->kind == TypeKind::MUTEX && (name == "lock" || name == "unlock")) {
            // Note: These need to be implemented in the runtime C code.
            // Assuming `angara_mutex_lock(mutex_obj)` and `angara_mutex_unlock(mutex_obj)`.
            return "angara_mutex_" + name + "(" + object_str + ")";
        }
        if (object_type->kind == TypeKind::LIST) {
            if (name == "push") return "angara_list_push(" + object_str + ", " + args_str + ")";
            if (name == "remove_at") return "angara_list_remove_at(" + object_str + ", " + args_str + ")"; // <-- ADD THIS
            if (name == "remove") return "angara_list_remove(" + object_str + ", " + args_str + ")"; // <-- ADD THIS
        }
        if (object_type->kind == TypeKind::RECORD) { // <-- ADD THIS BLOCK
            if (name == "remove") return "angara_record_remove(" + object_str + ", " + args_str + ")";
            if (name == "keys") return "angara_record_keys(" + object_str + ")";
        }

        // A) Method call on a class instance (e.g., `p.move(...)` or `my_counter.increment()`).
        if (object_type->kind == TypeKind::INSTANCE) {
            auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);

            // --- THE FIX ---
            // 1. Find which class in the inheritance hierarchy actually defines this method.
            //    `name` is the method name, e.g., "move".
            const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), name);

            // This should never happen if the TypeChecker is correct, but it's a safe guard.
            if (!owner_class) {
                return "/* <compiler_error_unknown_method> */";
            }

            // 2. Dispatch to the correct C code based on whether the owner class was native or not.
            if (owner_class->is_native) {
                // NATIVE METHOD: Transpile to a generic (argc, argv) call with `self` as the first argument.
                // The mangled name is Angara_ClassName_MethodName, using the OWNER's name.
                std::string final_args = object_str + (args_str.empty() ? "" : ", " + args_str);
                return "Angara_" + owner_class->name + "_" + name + "(" +
                       std::to_string(expr.arguments.size() + 1) + ", (AngaraObject[]){" + final_args + "})";
            } else {
                // ANGARA METHOD: Transpile to a direct, strongly-typed C call.
                // The mangled name is Angara_ClassName_MethodName, using the OWNER's name.
                std::string final_args = object_str + (args_str.empty() ? "" : ", " + args_str);
                return "Angara_" + owner_class->name + "_" + name + "(" + final_args + ")";
            }
        }

        // C) Call on a symbol imported from a module.
        if (object_type->kind == TypeKind::MODULE) {
            auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);
            std::string mangled_name = "Angara_" + module_type->name + "_" + name;
            if (module_type->is_native) {
                // NATIVE GLOBAL FUNCTION or NATIVE CONSTRUCTOR: Always use generic call.
                return mangled_name + "(" + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
            } else {
                // ANGARA symbol from another module: Call its global closure.
                std::string closure_var = "g_" + name;
                return "angara_call(" + closure_var + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
            }
        }
    }

    // ---
    // Case 2: The callee is a simple name in the current module scope.
    // ---
    if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
        const std::string& name = var_expr->name.lexeme;

        // --- NEW: Check if this is a selectively imported native function ---
        auto symbol = m_type_checker.m_variable_resolutions.at(var_expr.get());
        if (symbol && symbol->from_module && symbol->from_module->is_native) {
            // It's a native function! Generate the correct mangled call.
            std::string mangled_name = "Angara_" + symbol->from_module->name + "_" + name;
            return mangled_name + "(" + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
        }

        // A) Check for BUILT-IN global functions first.
        if (name == "len") return "angara_len(" + args_str + ")";
        if (name == "typeof") return "angara_typeof(" + args_str + ")";
        if (name == "string") return "angara_to_string(" + args_str + ")";
        if (name == "i64" || name == "int") return "angara_to_i64(" + args_str + ")";
        if (name == "f64" || name == "float") return "angara_to_f64(" + args_str + ")";
        if (name == "bool") return "angara_to_bool(" + args_str + ")";
        if (name == "Mutex") return "angara_mutex_new()";
        if (name == "Exception") return "angara_exception_new(" + args_str + ")";
        if (name == "spawn") {
            std::string closure_str = transpileExpr(expr.arguments[0]);
            std::vector<std::string> rest_arg_strs;
            for (size_t i = 1; i < expr.arguments.size(); ++i) {
                rest_arg_strs.push_back(transpileExpr(expr.arguments[i]));
            }
            std::string rest_args_str = join_strings(rest_arg_strs, ", ");
            return "angara_spawn_thread(" + closure_str + ", " + std::to_string(rest_arg_strs.size()) + ", (AngaraObject[]){" + rest_args_str + "})";
        }

        if (callee_type->kind == TypeKind::DATA) {
            auto data_type = std::dynamic_pointer_cast<DataType>(callee_type);
            return "Angara_data_new_" + data_type->name + "(" + args_str + ")";
        }

        // B) Check if it's an ANGARA CLASS CONSTRUCTOR.
        if (callee_type->kind == TypeKind::CLASS) {
            return "Angara_" + name + "_new(" + args_str + ")";
        }

        // C) If not a built-in or constructor, it's an ANGARA GLOBAL FUNCTION. Call via closure.
        std::string closure_var = "g_" + name;
        if (name == "main") closure_var = "g_angara_main_closure";
        return "angara_call(" + closure_var + ", " + std::to_string(expr.arguments.size()) + ", (AngaraObject[]){" + args_str + "})";
    }

        if (auto super_expr = std::dynamic_pointer_cast<const SuperExpr>(expr.callee)) {
            // The TypeChecker guarantees `super` is only used inside a class,
            // so `m_current_class_name` will be correctly set.
            if (m_current_class_name.empty()) {
                // This is a safeguard; it should be unreachable if the TypeChecker is correct.
                return "/* <compiler_error_super_outside_class> */";
            }

            // Get the full type information for the current class from the TypeChecker's results.
            auto current_class_symbol = m_type_checker.m_symbols.resolve(m_current_class_name);
            auto current_class_type = std::dynamic_pointer_cast<ClassType>(current_class_symbol->type);
            auto superclass_type = current_class_type->superclass;

            if (!super_expr->method) {
                // Case A: Constructor call `super(...)`. Transpiles to a call to the parent's `init`.
                // The first argument is `this_obj`, which is always in scope inside a method.
                return "Angara_" + superclass_type->name + "_init(this_obj" + (args_str.empty() ? "" : ", " + args_str) + ")";
            } else {
                // Case B: Regular method call `super.method(...)`.
                const std::string& method_name = super_expr->method->lexeme;
                // Transpiles to a direct call to the parent's C method function.
                return "Angara_" + superclass_type->name + "_" + method_name + "(this_obj" + (args_str.empty() ? "" : ", " + args_str) + ")";
            }
        }

    // ---
    // Case 3: Fallback for dynamic calls (e.g., calling a function stored in a variable).
    // ---
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
                std::string index_str = transpileExpr(subscript_target->index);
                return "angara_record_set_with_angara_key(" + object_str + ", " + index_str + ", " + value_str + ")";
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

    std::string CTranspiler::transpileGetExpr_on_instance(const GetExpr& expr, const std::string& object_str) {
        const std::string& prop_name = expr.name.lexeme;
        auto object_type = m_type_checker.m_expression_types.at(expr.object.get());

        if (object_type->kind == TypeKind::OPTIONAL) {
            object_type = std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type;
        }
        auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);

        // 1. Find which class in the hierarchy actually defines this property.
        const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), prop_name);
        if (!owner_class) {
            return "/* <unknown_property> */";
        }

        // 2. Build the access path by traversing the `parent` members.
        std::string access_path = "->";
        const ClassType* current = instance_type->class_type.get();
        while (current && current->name != owner_class->name) {
            // Prepend `parent.` for each level of inheritance we go up.
            access_path += "parent.";
            current = current->superclass.get();
        }
        access_path += sanitize_name(prop_name);

        // 3. The initial cast is to the struct of the INSTANCE being accessed.
        std::string base_struct_name = "Angara_" + instance_type->class_type->name;

        return "((" + base_struct_name + "*)AS_OBJ(" + object_str + "))" + access_path;
    }


std::string CTranspiler::transpileGetExpr(const GetExpr& expr) {
    // 1. Transpile the object on the left of the operator.
    std::string object_str = transpileExpr(expr.object);
    const std::string& prop_name = expr.name.lexeme;

    // 2. Get the pre-computed type of the object from the Type Checker.
    auto object_type = m_type_checker.m_expression_types.at(expr.object.get());

    // 3. Determine the actual type, peeling off one layer of optionality if it exists.
    auto unwrapped_object_type = (object_type->kind == TypeKind::OPTIONAL)
        ? std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type
        : object_type;

    // 4. Generate the raw C code for the property access itself.
    std::string access_str = "/* <invalid_get_expr> */";

    if (unwrapped_object_type->kind == TypeKind::DATA) {
        std::string c_struct_name = "Angara_" + unwrapped_object_type->toString();
        // Cast the generic obj pointer to the specific data struct pointer and access the field.
        access_str = "((struct " + c_struct_name + "*)AS_OBJ(" + object_str + "))->" + sanitize_name(prop_name);
    }
    else if (unwrapped_object_type->kind == TypeKind::INSTANCE) {
        access_str = transpileGetExpr_on_instance(expr, object_str);
    }
    else if (unwrapped_object_type->kind == TypeKind::MODULE) {
        auto module_type = std::dynamic_pointer_cast<ModuleType>(unwrapped_object_type);
        // For a module, "accessing a property" means referring to the exported global variable.
        access_str = module_type->name + "_" + prop_name;
    }

    // 5. If the access was optional, wrap the raw access in a nil-check.
    // An access is considered optional if the `?.` operator was used, OR if the
    // object being accessed was an optional type to begin with.
    if (expr.op.type == TokenType::QUESTION_DOT || object_type->kind == TypeKind::OPTIONAL) {
        // Generates the C ternary: (IS_NIL(obj) ? create_nil() : <the_actual_access>)
        return "(IS_NIL(" + object_str + ") ? angara_create_nil() : " + access_str + ")";
    }

    // 6. If it was a regular access, return the raw access string.
    return access_str;
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
    (*m_current_out) << "AngaraObject __collection_" << sanitize_name(stmt.name.lexeme)  << " = "
                     << transpileExpr(stmt.collection) << ";\n";
    indent();
    (*m_current_out) << "angara_incref(__collection_" << sanitize_name(stmt.name.lexeme)  << ");\n";

    // 2. Create and initialize the hidden __index variable.
    indent();
    (*m_current_out) << "AngaraObject __index_" << sanitize_name(stmt.name.lexeme)  << " = angara_create_i64(0LL);\n";

    // 3. Generate the `while` loop header.
    indent();
    (*m_current_out) << "while (angara_is_truthy(angara_create_bool(AS_I64(__index_" << sanitize_name(stmt.name.lexeme)  << ") < AS_I64(angara_len(__collection_" << sanitize_name(stmt.name.lexeme)  << "))))) {\n";
    m_indent_level++;

    // 4. Generate the `let item = ...` declaration.
    indent();
    (*m_current_out) << "AngaraObject " << sanitize_name(stmt.name.lexeme)  << " = angara_list_get(__collection_"
                     << sanitize_name(stmt.name.lexeme)  << ", __index_" << sanitize_name(stmt.name.lexeme)  << ");\n";

    // 5. Transpile the user's loop body.
    transpileStmt(stmt.body);

    // 6. Generate the increment logic: `__index = __index + 1`.
    indent();
    (*m_current_out) << "{\n";
    m_indent_level++;
    indent(); (*m_current_out) << "AngaraObject __temp_one = angara_create_i64(1LL);\n";
    indent(); (*m_current_out) << "AngaraObject __new_index = angara_create_i64(AS_I64(__index_" << sanitize_name(stmt.name.lexeme)  << ") + AS_I64(__temp_one));\n";
    indent(); (*m_current_out) << "angara_decref(__index_" << sanitize_name(stmt.name.lexeme)  << ");\n";
    indent(); (*m_current_out) << "__index_" << sanitize_name(stmt.name.lexeme)  << " = __new_index;\n";
    m_indent_level--;
    indent(); (*m_current_out) << "}\n";

    // 7. Decref the user's loop variable at the end of the iteration.
    indent(); (*m_current_out) << "angara_decref(" << sanitize_name(stmt.name.lexeme)  << ");\n";

    m_indent_level--;
    indent();
    (*m_current_out) << "}\n";

    // 8. Decref the hidden variables at the end of the scope.
    indent();
    (*m_current_out) << "angara_decref(__collection_" << sanitize_name(stmt.name.lexeme) << ");\n";
    indent();
    (*m_current_out) << "angara_decref(__index_" << sanitize_name(stmt.name.lexeme)  << ");\n";

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
            std::string index_str = transpileExpr(expr.index);
            // This now works for both literals (which become Angara strings) and variables.
            return "angara_record_get_with_angara_key(" + object_str + ", " + index_str + ")";
        }

        // Fallback if the type checker somehow let a non-subscriptable type through.
        return "/* unsupported subscript */";
    }

    void CTranspiler::transpileBreakStmt(const BreakStmt& stmt) {
        indent();
        (*m_current_out) << "break;\n";
    }


    std::string CTranspiler::transpileIsExpr(const IsExpr& expr) {
        std::string object_str = transpileExpr(expr.object);

        // --- THE FIX ---
        // Check if the type being checked is a generic `list`.
        if (auto generic_type = std::dynamic_pointer_cast<const GenericType>(expr.type)) {
            if (generic_type->name.lexeme == "list") {
                if (generic_type->arguments.size() == 1) {
                    // It is! Generate a call to our new, specialized function.
                    auto element_type_ast = generic_type->arguments[0];
                    if (auto simple_element_type = std::dynamic_pointer_cast<const SimpleType>(element_type_ast)) {
                        std::string element_type_name = simple_element_type->name.lexeme;
                        return "angara_is_list_of_type(" + object_str + ", \"" + element_type_name + "\")";
                    }
                }
            }
            // Fallback for other generics like 'record' if we add them later.
            return "angara_is_instance_of(" + object_str + ", \"" + generic_type->name.lexeme + "\")";
        }

        // Fallback for simple types (e.g., `is string`, `is Counter`).
        if (auto simple_type = std::dynamic_pointer_cast<const SimpleType>(expr.type)) {
            std::string type_name_str = simple_type->name.lexeme;
            return "angara_is_instance_of(" + object_str + ", \"" + type_name_str + "\")";
        }

        return "angara_create_bool(false)"; // Should be unreachable
    }


} // namespace angara