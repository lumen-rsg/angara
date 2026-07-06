#include "TypeChecker.h"
#include <stdexcept>
#include "ErrorHandler.h"
namespace angara {
    using angara::isInteger;
    using angara::isFloat;
    using angara::isNumeric;
    using angara::isUnsignedInteger;

    const SymbolTable& TypeChecker::getSymbolTable() const {
        return m_symbols;
    }

    std::shared_ptr<ModuleType> TypeChecker::getModuleType() const {
        return m_module_type;
    }

    std::shared_ptr<Symbol> TypeChecker::resolve_and_narrow(const VarExpr& expr) {
        auto symbol = m_symbols.resolve(expr.name.lexeme);
        if (!symbol) return nullptr;

        if (const auto it = m_narrowed_types.find(symbol.get()); it != m_narrowed_types.end()) {
            Symbol narrowed_symbol = *symbol;
            narrowed_symbol.type = it->second;
            return std::make_shared<Symbol>(narrowed_symbol);
        }

        return symbol;
    }

    TypeChecker::TypeChecker(CompilerDriver& driver, ErrorHandler& errorHandler, const std::string& module_name)
    : m_errorHandler(errorHandler), m_driver(driver) {
        m_type_i8 = std::make_shared<PrimitiveType>("i8");
        m_type_i16 = std::make_shared<PrimitiveType>("i16");
        m_type_i32 = std::make_shared<PrimitiveType>("i32");
        m_type_i64 = std::make_shared<PrimitiveType>("i64");
        m_type_u8 = std::make_shared<PrimitiveType>("u8");
        m_type_u16 = std::make_shared<PrimitiveType>("u16");
        m_type_u32 = std::make_shared<PrimitiveType>("u32");
        m_type_u64 = std::make_shared<PrimitiveType>("u64");
        m_type_f32 = std::make_shared<PrimitiveType>("f32");
        m_type_f64 = std::make_shared<PrimitiveType>("f64");
        m_type_bool = std::make_shared<PrimitiveType>("bool");
        m_type_string = std::make_shared<PrimitiveType>("string");
        m_type_char = std::make_shared<PrimitiveType>("char");  // LANG-4
        m_type_nil = std::make_shared<NilType>();
        m_type_any = std::make_shared<AnyType>();
        m_type_error = std::make_shared<PrimitiveType>("<error>");
        m_type_thread = std::make_shared<ThreadType>();
        m_type_mutex = std::make_shared<MutexType>();
        m_module_type = std::make_shared<ModuleType>(module_name);
        m_type_exception = std::make_shared<ExceptionType>();


        const auto len_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_any},
                m_type_i64
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "len", 0, 0), len_type, true);

        const auto typeof_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_any},
                m_type_string
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "typeof", 0, 0), typeof_type, true);

        auto worker_fn_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{},
            m_type_nil
        );

        const auto spawn_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{}, std::make_shared<AnyType>(), true
            )},
            m_type_thread,
            true
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "spawn", 0, 0), spawn_type, true);

        auto mutex_constructor_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{},
            m_type_mutex
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "Mutex", 0, 0), mutex_constructor_type, true);

        auto string_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_string
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "string", 0, 0), string_conv_type, true);

        auto i64_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_i64
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "i64", 0, 0), i64_conv_type, true);
        m_symbols.declare(Token(TokenType::IDENTIFIER, "int", 0, 0), i64_conv_type, true);

        auto f64_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_f64
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "f64", 0, 0), f64_conv_type, true);
        m_symbols.declare(Token(TokenType::IDENTIFIER, "float", 0, 0), f64_conv_type, true);

        auto bool_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_bool
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "bool", 0, 0), bool_conv_type, true);

        // LANG-4: char(x) conversion — narrows an integer/code-point to char.
        auto char_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_char
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "char", 0, 0), char_conv_type, true);

        auto exception_constructor_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_string},
                m_type_exception
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "Exception", 0, 0), exception_constructor_type, true);

        const auto println_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any},
            m_type_nil,
            true  // variadic
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "println", 0, 0), println_type, true);

        const auto print_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any},
            m_type_nil,
            true  // variadic
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "print", 0, 0), print_type, true);

    }

    void TypeChecker::pushAndSave(const Expr* expr, const std::shared_ptr<Type>& type) {
        m_type_stack.push(type);
        m_expression_types[expr] = type;
    }

    bool TypeChecker::isTruthy(const std::shared_ptr<Type>& type) {
        if (type->kind == TypeKind::ERROR) {
            return false;
        }

        return true;
    }


bool TypeChecker::check(const std::vector<std::shared_ptr<Stmt>>& statements) {
    m_hadError = false;

        for (const auto& stmt : statements) {
            if (auto attach_stmt = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
                resolveAttach(*attach_stmt);
            }
        }
        if (m_hadError) return false;

        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                auto class_type = std::make_shared<ClassType>(class_stmt->name.lexeme);
                class_type->home_module = m_module_type->name;  // TS-4
                if (auto conflicting_symbol = m_symbols.declare(class_stmt->name, class_type, true)) {
                    error(class_stmt->name, "Symbol '" + class_stmt->name.lexeme + "' is already declared.", "E243");
                    note(conflicting_symbol->declaration_token, "previous declaration was here.");
                }
            } else if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
                auto trait_type = std::make_shared<TraitType>(trait_stmt->name.lexeme);
                trait_type->home_module = m_module_type->name;  // TS-4
                if (auto conflicting_symbol = m_symbols.declare(trait_stmt->name, trait_type, true)) {
                    error(trait_stmt->name, "Symbol '" + trait_stmt->name.lexeme + "' is already declared.", "E244");
                    note(conflicting_symbol->declaration_token, "previous declaration was here.");
                }
            } else if (auto contract_stmt = std::dynamic_pointer_cast<const ContractStmt>(stmt)) {
                auto contract_type = std::make_shared<ContractType>(contract_stmt->name.lexeme);
                contract_type->home_module = m_module_type->name;  // TS-4
                if (auto conflicting_symbol = m_symbols.declare(contract_stmt->name, contract_type, true)) {
                    error(contract_stmt->name, "Symbol '" + contract_stmt->name.lexeme + "' is already declared.", "E245");
                    note(conflicting_symbol->declaration_token, "previous declaration was here.");
                }
            }
            else if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                auto data_type = std::make_shared<DataType>(data_stmt->name.lexeme);
                data_type->home_module = m_module_type->name;  // TS-4
                if (auto conflicting = m_symbols.declare(data_stmt->name, data_type, true)) {
                    error(data_stmt->name, "Symbol '" + data_stmt->name.lexeme + "' is already declared.", "E246");
                    note(conflicting->declaration_token, "previous declaration was here.");
                }
            } else if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                auto enum_type = std::make_shared<EnumType>(enum_stmt->name.lexeme);
                enum_type->home_module = m_module_type->name;  // TS-4
                if (auto conflicting = m_symbols.declare(enum_stmt->name, enum_type, true)) {
                    error(enum_stmt->name, "Symbol '" + enum_stmt->name.lexeme + "' is already declared.", "E247");
                    note(conflicting->declaration_token, "previous declaration was here.");
                }
            }
        }
    if (m_hadError) return false;

    // Pass 1a2: define type aliases (after nominal types declared, before headers)
    for (const auto& stmt : statements) {
        if (auto alias_stmt = std::dynamic_pointer_cast<const TypeAliasStmt>(stmt)) {
            defineTypeAliasHeader(*alias_stmt);
        }
    }
    if (m_hadError) return false;

        for (const auto& stmt : statements) {
            if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                defineEnumHeader(*enum_stmt);
            }
        }

    for (const auto& stmt : statements) {
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            defineDataHeader(*data_stmt);
        }
    }

    for (const auto& stmt : statements) {
        if (auto contract_stmt = std::dynamic_pointer_cast<const ContractStmt>(stmt)) {
            defineContractHeader(*contract_stmt);
        }
    }
    if (m_hadError) return false;

    for (const auto& stmt : statements) {
        if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
            defineTraitHeader(*trait_stmt);
        }
    }
    if (m_hadError) return false;

    for (const auto& stmt : statements) {
        if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                defineClassHeader(*class_stmt);
        }
    }
    if (m_hadError) return false;

    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            if (m_symbols.resolve(func_stmt->name.lexeme) == nullptr) {
                 defineFunctionHeader(*func_stmt);
            }
        }
    }
    if (m_hadError) return false;

    for (const auto& stmt : statements) {
        stmt->accept(*this, stmt);
    }

    return !m_hadError;
}

    void TypeChecker::error(const Token& token, const std::string& message, const std::string& code) {
        m_hadError = true;
        m_errorHandler.report(token, message, code);
    }

    void TypeChecker::warning(const Token& token, const std::string& message, const std::string& code) {
        m_errorHandler.warning(token, message, code);
    }

    void TypeChecker::note(const Token& token, const std::string& message) {
        m_errorHandler.note(token, message);
    }

    void TypeChecker::exitScopeAndWarn() {
        auto unused = m_symbols.exitScope();
        for (const auto& sym : unused) {
            if (sym->depth > 0 && sym->type->kind != TypeKind::FUNCTION &&
                sym->type->kind != TypeKind::MODULE &&
                sym->name != "this") {
                warning(sym->declaration_token,
                    "Unused variable '" + sym->name + "'.", "W003");
            }
        }
    }

    std::shared_ptr<Type> TypeChecker::popType() {
        if (m_type_stack.empty()) {
            return m_type_error;
        }
        auto type = m_type_stack.top();
        m_type_stack.pop();
        return type;
    }



std::shared_ptr<Type> TypeChecker::resolveType(const std::shared_ptr<ASTType>& ast_type) {
    if (!ast_type) {
        return m_type_error;
    }

    if (auto optional_ast_node = std::dynamic_pointer_cast<const OptionalTypeNode>(ast_type)) {
        auto wrapped_semantic_type = resolveType(optional_ast_node->base_type);
        if (wrapped_semantic_type->kind == TypeKind::ERROR) {
            return m_type_error;
        }
        return std::make_shared<OptionalType>(wrapped_semantic_type);
    }

    if (auto simple = std::dynamic_pointer_cast<const SimpleType>(ast_type)) {
        const std::string& name = simple->name.lexeme;

        if (name == "i64" || name == "int") return m_type_i64;
        if (name == "i32") return m_type_i32;
        if (name == "i16") return m_type_i16;
        if (name == "i8") return m_type_i8;
        if (name == "u64") return m_type_u64;
        if (name == "u32") return m_type_u32;
        if (name == "u16") return m_type_u16;
        if (name == "u8") return m_type_u8;
        if (name == "f64" || name == "float") return m_type_f64;
        if (name == "f32") return m_type_f32;
        if (name == "bool") return m_type_bool;
        if (name == "string") return m_type_string;
        if (name == "char") return m_type_char;  // LANG-4
        if (name == "nil") return m_type_nil;
        if (name == "any") return m_type_any;
        if (name == "Thread") return m_type_thread;
        if (name == "Exception") return m_type_exception;
        if (name == "Mutex") return m_type_mutex;
        if (name == "void") return std::make_shared<VoidType>();

        if (name == "record") {
            return std::make_shared<RecordType>(std::map<std::string, std::shared_ptr<Type>>{});
        }

        if (name == "list") {
            return std::make_shared<ListType>(m_type_any);
        }

        auto tp_it = m_active_type_params.find(name);
        if (tp_it != m_active_type_params.end()) {
            return tp_it->second;
        }

        auto symbol = m_symbols.resolve(name);
        if (symbol) {
            if (symbol->type->kind == TypeKind::CLASS) {
                return std::make_shared<InstanceType>(std::dynamic_pointer_cast<ClassType>(symbol->type));
            }
            // For DATA, TRAIT, CONTRACT, ENUM, and type aliases (which resolve to any type)
            return symbol->type;
        }

        error(simple->name, "Unknown type '" + name + "'.", "E250");
        std::vector<std::string> candidates;
        for (const auto& scope : m_symbols.getScopes()) {
            for (const auto& [sym_name, sym] : scope) {
                if (sym->type->kind == TypeKind::CLASS || sym->type->kind == TypeKind::ENUM ||
                    sym->type->kind == TypeKind::DATA || sym->type->kind == TypeKind::TRAIT ||
                    sym->type->kind == TypeKind::CONTRACT)
                {
                    candidates.push_back(sym_name);
                }
            }
        }
        find_and_report_suggestion(simple->name, candidates);
        return m_type_error;
    }

    if (auto generic = std::dynamic_pointer_cast<const GenericType>(ast_type)) {
        const std::string& base_name = generic->name.lexeme;

        if (base_name == "list") {
            if (generic->arguments.size() != 1) {
                error(generic->name, "Type 'list' expects exactly one type argument (e.g., 'list<i64>').", "E251");
                return m_type_error;
            }
            auto element_type = resolveType(generic->arguments[0]);
            if (element_type->kind == TypeKind::ERROR) return m_type_error;
            return std::make_shared<ListType>(element_type);
        }

        // v5: ref<T> — non-owning reference.
        if (base_name == "ref") {
            if (generic->arguments.size() != 1) {
                error(generic->name, "Type 'ref' expects exactly one type argument (e.g., 'ref<Buffer>').", "E251");
                return m_type_error;
            }
            auto inner_type = resolveType(generic->arguments[0]);
            if (inner_type->kind == TypeKind::ERROR) return m_type_error;
            return std::make_shared<RefType>(inner_type);
        }

        // LIB-4: Future<T> — asynchronous computation result.
        if (base_name == "Future") {
            if (generic->arguments.size() != 1) {
                error(generic->name, "Type 'Future' expects exactly one type argument (e.g., 'Future<string>').", "E251");
                return m_type_error;
            }
            auto inner_type = resolveType(generic->arguments[0]);
            if (inner_type->kind == TypeKind::ERROR) return m_type_error;
            return std::make_shared<FutureType>(inner_type);
        }

        // SIMD-5: vector types — vec2<f32>, vec3<f64>, vec4<i32>, vec8<f32>
        if (base_name == "vec2" || base_name == "vec3" ||
            base_name == "vec4" || base_name == "vec8") {
            int size = std::stoi(base_name.substr(3));
            if (generic->arguments.size() != 1) {
                error(generic->name, "Type '" + base_name + "' expects exactly one type argument (e.g., '" +
                      base_name + "<f32>').", "E251");
                return m_type_error;
            }
            auto elem_type = resolveType(generic->arguments[0]);
            if (elem_type->kind == TypeKind::ERROR) return m_type_error;
            // Only primitive numeric types are valid vector element types.
            if (elem_type->kind != TypeKind::PRIMITIVE ||
                (!isInteger(elem_type) && !isFloat(elem_type))) {
                error(generic->name, "Vector element type must be a primitive numeric type "
                      "(f32, f64, i32, i64, u32, u64), but got '" +
                      elem_type->toString() + "'.", "E420");
                return m_type_error;
            }
            return std::make_shared<VectorType>(elem_type, size);
        }

        auto symbol = m_symbols.resolve(base_name);
        if (symbol) {
            auto& base_type = symbol->type;

            if (base_type->kind == TypeKind::DATA) {
                auto data_type = std::dynamic_pointer_cast<DataType>(base_type);
                if (data_type->is_generic()) {
                    if (generic->arguments.size() != data_type->type_params.size()) {
                        error(generic->name, "Generic type '" + base_name + "' expects " +
                              std::to_string(data_type->type_params.size()) +
                              " type argument(s), but got " +
                              std::to_string(generic->arguments.size()) + ".", "E252");
                        return m_type_error;
                    }

                    std::map<std::string, std::shared_ptr<Type>> type_args;
                    for (size_t i = 0; i < generic->arguments.size(); ++i) {
                        auto arg_type = resolveType(generic->arguments[i]);
                        if (arg_type->kind == TypeKind::ERROR) return m_type_error;
                        // TS-2: enforce the param's bound, if any.
                        const std::string& pname = data_type->type_params[i];
                        auto bound_it = data_type->type_param_bounds.find(pname);
                        if (bound_it != data_type->type_param_bounds.end() &&
                            !conformsToTrait(arg_type, bound_it->second)) {
                            error(generic->name,
                                  "Type argument '" + arg_type->toString() + "' does not satisfy the bound '" +
                                  pname + ": " + bound_it->second->toString() + "'.", "E391");
                        }
                        type_args[pname] = arg_type;
                    }

                    return std::make_shared<GenericInstanceType>(data_type, std::move(type_args));
                }
            }

            if (base_type->kind == TypeKind::CLASS) {
                auto class_type = std::dynamic_pointer_cast<ClassType>(base_type);
                if (class_type->is_generic()) {
                    if (generic->arguments.size() != class_type->type_params.size()) {
                        error(generic->name, "Generic type '" + base_name + "' expects " +
                              std::to_string(class_type->type_params.size()) +
                              " type argument(s), but got " +
                              std::to_string(generic->arguments.size()) + ".", "E253");
                        return m_type_error;
                    }

                    std::map<std::string, std::shared_ptr<Type>> type_args;
                    for (size_t i = 0; i < generic->arguments.size(); ++i) {
                        auto arg_type = resolveType(generic->arguments[i]);
                        if (arg_type->kind == TypeKind::ERROR) return m_type_error;
                        // TS-2: enforce the param's bound, if any.
                        const std::string& pname = class_type->type_params[i];
                        auto bound_it = class_type->type_param_bounds.find(pname);
                        if (bound_it != class_type->type_param_bounds.end() &&
                            !conformsToTrait(arg_type, bound_it->second)) {
                            error(generic->name,
                                  "Type argument '" + arg_type->toString() + "' does not satisfy the bound '" +
                                  pname + ": " + bound_it->second->toString() + "'.", "E391");
                        }
                        type_args[pname] = arg_type;
                    }

                    return std::make_shared<GenericInstanceType>(class_type, std::move(type_args));
                }
            }

            // LANG-8: generic enum (e.g., Result<i64, string>)
            if (base_type->kind == TypeKind::ENUM) {
                auto enum_type = std::dynamic_pointer_cast<EnumType>(base_type);
                if (enum_type->is_generic()) {
                    if (generic->arguments.size() != enum_type->type_params.size()) {
                        error(generic->name, "Generic type '" + base_name + "' expects " +
                              std::to_string(enum_type->type_params.size()) +
                              " type argument(s), but got " +
                              std::to_string(generic->arguments.size()) + ".", "E411");
                        return m_type_error;
                    }

                    std::map<std::string, std::shared_ptr<Type>> type_args;
                    for (size_t i = 0; i < generic->arguments.size(); ++i) {
                        auto arg_type = resolveType(generic->arguments[i]);
                        if (arg_type->kind == TypeKind::ERROR) return m_type_error;
                        // TS-2: enforce the param's bound, if any.
                        const std::string& pname = enum_type->type_params[i];
                        auto bound_it = enum_type->type_param_bounds.find(pname);
                        if (bound_it != enum_type->type_param_bounds.end() &&
                            !conformsToTrait(arg_type, bound_it->second)) {
                            error(generic->name,
                                  "Type argument '" + arg_type->toString() + "' does not satisfy the bound '" +
                                  pname + ": " + bound_it->second->toString() + "'.", "E391");
                        }
                        type_args[pname] = arg_type;
                    }

                    return std::make_shared<GenericInstanceType>(enum_type, std::move(type_args));
                }
            }
        }

        error(generic->name, "Unknown generic type '" + base_name + "'. Only generic types with a '<...>' suffix are valid here.", "E254");
        return m_type_error;
    }

    if (auto record_type_expr = std::dynamic_pointer_cast<const RecordTypeExpr>(ast_type)) {
        std::map<std::string, std::shared_ptr<Type>> fields;
        for (const auto& field_def : record_type_expr->fields) {
            const std::string& field_name = field_def.name.lexeme;
            if (fields.contains(field_name)) {
                error(field_def.name, "Duplicate field '" + field_name + "' in record type.", "E255");
            }
            fields[field_name] = resolveType(field_def.type);
        }
        return std::make_shared<RecordType>(fields);
    }

    if (auto func_type_expr = std::dynamic_pointer_cast<const FunctionTypeExpr>(ast_type)) {
        std::vector<std::shared_ptr<Type>> param_types;
        for (const auto& p_ast_type : func_type_expr->param_types) {
            param_types.push_back(resolveType(p_ast_type));
        }
        auto return_type = resolveType(func_type_expr->return_type);
        return std::make_shared<FunctionType>(param_types, return_type);
    }

    if (auto fixed_arr = std::dynamic_pointer_cast<const FixedArrayTypeExpr>(ast_type)) {
        auto elem_type = resolveType(fixed_arr->element_type);
        if (elem_type->kind == TypeKind::ERROR) return m_type_error;
        return std::make_shared<FixedArrayType>(elem_type, fixed_arr->size);
    }

    // SIMD-1: unboxed dynamic array (e.g., f64[], i64[])
    if (auto raw_arr = std::dynamic_pointer_cast<const RawArrayTypeExpr>(ast_type)) {
        auto elem_type = resolveType(raw_arr->element_type);
        if (elem_type->kind == TypeKind::ERROR) return m_type_error;
        // Only primitive elements are supported for raw arrays (Phase 1).
        // Reject list<T>, record, etc. — they can't be stored unboxed.
        if (elem_type->kind != TypeKind::PRIMITIVE &&
            elem_type->kind != TypeKind::TYPE_PARAM) {
            // Allow TYPE_PARAM through (it will be resolved later in generics);
            // reject everything else that can't be stored contiguously.
            if (elem_type->kind == TypeKind::LIST ||
                elem_type->kind == TypeKind::RECORD ||
                elem_type->kind == TypeKind::FUNCTION ||
                elem_type->kind == TypeKind::CLASS ||
                elem_type->kind == TypeKind::TRAIT ||
                elem_type->kind == TypeKind::INSTANCE ||
                elem_type->kind == TypeKind::DATA ||
                elem_type->kind == TypeKind::ENUM ||
                elem_type->kind == TypeKind::TUPLE ||
                elem_type->kind == TypeKind::RAW_ARRAY) {
                error(raw_arr->bracket, "Raw arrays only support primitive element types (i8-u64, f32-f64). Use list<T> for complex types.", "E114");
                return m_type_error;
            }
        }
        return std::make_shared<RawArrayType>(elem_type);
    }

    if (auto ptr_expr = std::dynamic_pointer_cast<const PointerTypeExpr>(ast_type)) {
        auto pointee = resolveType(ptr_expr->pointee_type);
        if (pointee->kind == TypeKind::ERROR) return m_type_error;
        auto ptr_type = std::make_shared<PointerType>(pointee, ptr_expr->depth);
        ptr_type->byval = ptr_expr->byval;
        return ptr_type;
    }

    if (auto owned = std::dynamic_pointer_cast<const OwnedTypeNode>(ast_type)) {
        auto inner = resolveType(owned->inner_type);
        if (inner->kind == TypeKind::ERROR) return m_type_error;
        if (inner->kind != TypeKind::PRIMITIVE) {
            return inner;
        }
        auto prim = std::make_shared<PrimitiveType>(
            std::dynamic_pointer_cast<PrimitiveType>(inner)->name);
        prim->is_owned = true;
        return prim;
    }

    // LANG-10: tuple type — (T1, T2, ...)
    if (auto tuple_type_expr = std::dynamic_pointer_cast<const TupleTypeExpr>(ast_type)) {
        std::vector<std::shared_ptr<Type>> element_types;
        for (const auto& elem_ast : tuple_type_expr->element_types) {
            auto elem_type = resolveType(elem_ast);
            if (elem_type->kind == TypeKind::ERROR) return m_type_error;
            element_types.push_back(elem_type);
        }
        return std::make_shared<TupleType>(std::move(element_types));
    }

    return m_type_error;
}


    void TypeChecker::find_and_report_suggestion(const Token& bad_token, const std::vector<std::string>& candidates) {
        const std::string& misspelled = bad_token.lexeme;
        std::string best_guess;

        size_t min_distance = std::min((size_t)3, (misspelled.length() / 3) + 1);

        for (const auto& candidate : candidates) {
            size_t dist = levenshtein_distance(misspelled, candidate);
            if (dist < min_distance) {
                min_distance = dist;
                best_guess = candidate;
            }
        }

        if (!best_guess.empty()) {
            note(bad_token, "did you mean '" + best_guess + "'?");
        }
    }

    void TypeChecker::extract_type_args(
        const std::shared_ptr<Type>& pattern,
        const std::shared_ptr<Type>& concrete,
        std::map<std::string, std::shared_ptr<Type>>& inferred
    ) {
        if (pattern->kind == TypeKind::TYPE_PARAM) {
            auto tp = std::dynamic_pointer_cast<TypeParameterType>(pattern);
            if (!inferred.count(tp->name)) {
                inferred[tp->name] = concrete;
            }
        } else if (pattern->kind == TypeKind::GENERIC_INSTANCE && concrete->kind == TypeKind::GENERIC_INSTANCE) {
            auto p_gen = std::dynamic_pointer_cast<GenericInstanceType>(pattern);
            auto c_gen = std::dynamic_pointer_cast<GenericInstanceType>(concrete);
            for (const auto& [name, p_arg] : p_gen->type_args) {
                auto it = c_gen->type_args.find(name);
                if (it != c_gen->type_args.end()) {
                    extract_type_args(p_arg, it->second, inferred);
                }
            }
        } else if (pattern->kind == TypeKind::LIST && concrete->kind == TypeKind::LIST) {
            extract_type_args(
                std::dynamic_pointer_cast<ListType>(pattern)->element_type,
                std::dynamic_pointer_cast<ListType>(concrete)->element_type,
                inferred
            );
        } else if (pattern->kind == TypeKind::OPTIONAL && concrete->kind == TypeKind::OPTIONAL) {
            extract_type_args(
                std::dynamic_pointer_cast<OptionalType>(pattern)->wrapped_type,
                std::dynamic_pointer_cast<OptionalType>(concrete)->wrapped_type,
                inferred
            );
        } else if (pattern->kind == TypeKind::REF && concrete->kind == TypeKind::REF) {
            extract_type_args(
                std::dynamic_pointer_cast<RefType>(pattern)->inner_type,
                std::dynamic_pointer_cast<RefType>(concrete)->inner_type,
                inferred
            );
        } else if (pattern->kind == TypeKind::FUTURE && concrete->kind == TypeKind::FUTURE) {
            extract_type_args(
                std::dynamic_pointer_cast<FutureType>(pattern)->inner_type,
                std::dynamic_pointer_cast<FutureType>(concrete)->inner_type,
                inferred
            );
        } else if (pattern->kind == TypeKind::TUPLE && concrete->kind == TypeKind::TUPLE) {
            auto p_tup = std::dynamic_pointer_cast<TupleType>(pattern);
            auto c_tup = std::dynamic_pointer_cast<TupleType>(concrete);
            if (p_tup->element_types.size() == c_tup->element_types.size()) {
                for (size_t i = 0; i < p_tup->element_types.size(); ++i) {
                    extract_type_args(p_tup->element_types[i], c_tup->element_types[i], inferred);
                }
            }
        } else if (pattern->kind == TypeKind::FUNCTION && concrete->kind == TypeKind::FUNCTION) {
            auto p_fn = std::dynamic_pointer_cast<FunctionType>(pattern);
            auto c_fn = std::dynamic_pointer_cast<FunctionType>(concrete);
            if (p_fn->param_types.size() == c_fn->param_types.size()) {
                for (size_t i = 0; i < p_fn->param_types.size(); ++i) {
                    extract_type_args(p_fn->param_types[i], c_fn->param_types[i], inferred);
                }
                extract_type_args(p_fn->return_type, c_fn->return_type, inferred);
            }
        } else if (pattern->kind == TypeKind::RECORD && concrete->kind == TypeKind::RECORD) {
            auto p_rec = std::dynamic_pointer_cast<RecordType>(pattern);
            auto c_rec = std::dynamic_pointer_cast<RecordType>(concrete);
            for (const auto& [name, p_field_type] : p_rec->fields) {
                auto it = c_rec->fields.find(name);
                if (it != c_rec->fields.end()) {
                    extract_type_args(p_field_type, it->second, inferred);
                }
            }
        } else if (pattern->kind == TypeKind::POINTER && concrete->kind == TypeKind::POINTER) {
            auto p_ptr = std::dynamic_pointer_cast<PointerType>(pattern);
            auto c_ptr = std::dynamic_pointer_cast<PointerType>(concrete);
            if (p_ptr->depth == c_ptr->depth) {
                extract_type_args(p_ptr->pointee_type, c_ptr->pointee_type, inferred);
            }
        } else if (pattern->kind == TypeKind::FIXED_ARRAY && concrete->kind == TypeKind::FIXED_ARRAY) {
            auto p_arr = std::dynamic_pointer_cast<FixedArrayType>(pattern);
            auto c_arr = std::dynamic_pointer_cast<FixedArrayType>(concrete);
            if (p_arr->size == c_arr->size) {
                extract_type_args(p_arr->element_type, c_arr->element_type, inferred);
            }
        } else if (pattern->kind == TypeKind::RAW_ARRAY && concrete->kind == TypeKind::RAW_ARRAY) {
            extract_type_args(
                std::dynamic_pointer_cast<RawArrayType>(pattern)->element_type,
                std::dynamic_pointer_cast<RawArrayType>(concrete)->element_type,
                inferred
            );
        } else if (pattern->kind == TypeKind::VECTOR && concrete->kind == TypeKind::VECTOR) {
            auto p_vec = std::dynamic_pointer_cast<VectorType>(pattern);
            auto c_vec = std::dynamic_pointer_cast<VectorType>(concrete);
            if (p_vec->size == c_vec->size) {
                extract_type_args(p_vec->element_type, c_vec->element_type, inferred);
            }
        }
    }

}