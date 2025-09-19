//
// Created by cv2 on 8/31/25.
//

#include "TypeChecker.h"
#include <stdexcept>
#include "ErrorHandler.h"
namespace angara {

// --- Constructor and Main Entry Point ---

    const SymbolTable& TypeChecker::getSymbolTable() const {
        return m_symbols;
    }

    std::shared_ptr<ModuleType> TypeChecker::getModuleType() const {
        return m_module_type;
    }

    std::shared_ptr<Symbol> TypeChecker::resolve_and_narrow(const VarExpr& expr) {
        // 1. First, resolve the symbol normally from the symbol table.
        auto symbol = m_symbols.resolve(expr.name.lexeme);
        if (!symbol) return nullptr;

        // 2. Check if this symbol has a narrowed type in our special map.
        if (const auto it = m_narrowed_types.find(symbol.get()); it != m_narrowed_types.end()) {
            // It does! Create a temporary, "fake" symbol on the stack
            // that has the same properties but with the new, narrowed type.
            Symbol narrowed_symbol = *symbol;
            narrowed_symbol.type = it->second;
            return std::make_shared<Symbol>(narrowed_symbol);
        }

        // 3. No narrowing applies. Return the original symbol.
        return symbol;
    }

    TypeChecker::TypeChecker(CompilerDriver& driver, ErrorHandler& errorHandler, const std::string& module_name)
    : m_errorHandler(errorHandler), m_driver(driver) {
        // Integer Types
        m_type_i8 = std::make_shared<PrimitiveType>("i8");
        m_type_i16 = std::make_shared<PrimitiveType>("i16");
        m_type_i32 = std::make_shared<PrimitiveType>("i32");
        m_type_i64 = std::make_shared<PrimitiveType>("i64");
        m_type_u8 = std::make_shared<PrimitiveType>("u8");
        m_type_u16 = std::make_shared<PrimitiveType>("u16");
        m_type_u32 = std::make_shared<PrimitiveType>("u32");
        m_type_u64 = std::make_shared<PrimitiveType>("u64");
        // Float Types
        m_type_f32 = std::make_shared<PrimitiveType>("f32");
        m_type_f64 = std::make_shared<PrimitiveType>("f64");
        // Other Primitives
        m_type_bool = std::make_shared<PrimitiveType>("bool");
        m_type_string = std::make_shared<PrimitiveType>("string");
        m_type_nil = std::make_shared<NilType>();
        m_type_any = std::make_shared<AnyType>();
        m_type_error = std::make_shared<PrimitiveType>("<error>");
        m_type_thread = std::make_shared<ThreadType>();
        m_type_mutex = std::make_shared<MutexType>();
        m_module_type = std::make_shared<ModuleType>(module_name);
        m_type_exception = std::make_shared<ExceptionType>();


        // func len(any) -> i64;
        const auto len_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_any},
                m_type_i64
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "len", 0, 0), len_type, true);

        // func typeof(any) -> string;
        const auto typeof_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_any},
                m_type_string
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "typeof", 0, 0), typeof_type, true);

        auto worker_fn_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{}, // No parameters
            m_type_nil
        );

        // Define the signature for `spawn` itself: function(function() -> void) -> Thread
        const auto spawn_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{}, std::make_shared<AnyType>(), true // A generic function
            )},
            m_type_thread,
            true // spawn itself is variadic
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "spawn", 0, 0), spawn_type, true);

        auto mutex_constructor_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{},
            m_type_mutex
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "Mutex", 0, 0), mutex_constructor_type, true);

        // func string(any) -> string
        auto string_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_string
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "string", 0, 0), string_conv_type, true);

        // func i64(any) -> i64
        auto i64_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_i64
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "i64", 0, 0), i64_conv_type, true);
        m_symbols.declare(Token(TokenType::IDENTIFIER, "int", 0, 0), i64_conv_type, true); // Alias

        // func f64(any) -> f64
        auto f64_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_f64
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "f64", 0, 0), f64_conv_type, true);
        m_symbols.declare(Token(TokenType::IDENTIFIER, "float", 0, 0), f64_conv_type, true); // Alias

        // func bool(any) -> bool
        auto bool_conv_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_any}, m_type_bool
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "bool", 0, 0), bool_conv_type, true);

        auto exception_constructor_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{m_type_string},
            m_type_exception
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "Exception", 0, 0), exception_constructor_type, true);

        m_module_type = std::make_shared<ModuleType>(module_name);

    }

    void TypeChecker::pushAndSave(const Expr* expr, const std::shared_ptr<Type>& type) {
        m_type_stack.push(type);
        m_expression_types[expr] = type;
    }

    bool TypeChecker::isInteger(const std::shared_ptr<Type>& type) {
        if (type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
               name == "u8" || name == "u16" || name == "u32" || name == "u64";
    }

    bool TypeChecker::isUnsignedInteger(const std::shared_ptr<Type>& type) {
        if (type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "u8" || name == "u16" || name == "u32" || name == "u64";
    }


    bool TypeChecker::isFloat(const std::shared_ptr<Type>& type) {
        if (type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "f32" || name == "f64";
    }

    bool TypeChecker::isTruthy(const std::shared_ptr<Type>& type) {
        // In our new, more flexible system, almost any type can be evaluated
        // in a boolean context. The only exceptions might be types that have
        // no logical "empty" or "zero" state.

        // For now, we can say that every valid type is truthy.
        // The only non-truthy type would be an error type.
        if (type->kind == TypeKind::ERROR) {
            return false;
        }

        // Allow everything: bool, nil, numbers, strings, lists, records, functions, etc.
        return true;
    }

    // in TypeChecker.cpp


bool TypeChecker::check(const std::vector<std::shared_ptr<Stmt>>& statements) {
    m_hadError = false;

        // --- NEW PRE-PASS: Resolve all module attachments FIRST ---
        // This is critical. It populates the symbol table with imported types
        // before any local types are analyzed.
        for (const auto& stmt : statements) {
            if (auto attach_stmt = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
                resolveAttach(*attach_stmt);
            }
        }
        if (m_hadError) return false;

        // --- PASS 1: Declare all top-level type names ---
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                auto class_type = std::make_shared<ClassType>(class_stmt->name.lexeme);
                if (auto conflicting_symbol = m_symbols.declare(class_stmt->name, class_type, true)) {
                    error(class_stmt->name, "re-declaration of symbol '" + class_stmt->name.lexeme + "'.");
                    note(conflicting_symbol->declaration_token, "previous declaration was here.");
                }
            } else if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
                auto trait_type = std::make_shared<TraitType>(trait_stmt->name.lexeme);
                if (auto conflicting_symbol = m_symbols.declare(trait_stmt->name, trait_type, true)) {
                    error(trait_stmt->name, "re-declaration of symbol '" + trait_stmt->name.lexeme + "'.");
                    note(conflicting_symbol->declaration_token, "previous declaration was here.");
                }
            } else if (auto contract_stmt = std::dynamic_pointer_cast<const ContractStmt>(stmt)) {
                auto contract_type = std::make_shared<ContractType>(contract_stmt->name.lexeme);
                if (auto conflicting_symbol = m_symbols.declare(contract_stmt->name, contract_type, true)) {
                    error(contract_stmt->name, "re-declaration of symbol '" + contract_stmt->name.lexeme + "'.");
                    note(conflicting_symbol->declaration_token, "previous declaration was here.");
                }
            }
            else if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                auto data_type = std::make_shared<DataType>(data_stmt->name.lexeme);
                if (auto conflicting = m_symbols.declare(data_stmt->name, data_type, true)) {
                    error(data_stmt->name, "re-declaration of symbol '" + data_stmt->name.lexeme + "'.");
                    note(conflicting->declaration_token, "previous declaration was here.");
                }
            } else if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                auto enum_type = std::make_shared<EnumType>(enum_stmt->name.lexeme);
                if (auto conflicting = m_symbols.declare(enum_stmt->name, enum_type, true)) {
                    error(enum_stmt->name, "re-declaration of symbol '" + enum_stmt->name.lexeme + "'.");
                    note(conflicting->declaration_token, "previous declaration was here.");
                }
            }
        }
    if (m_hadError) return false;

    // --- PASS 2: Define all headers and signatures (Order is important!) ---

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

    // -- STAGE 2a: Define CONTRACT headers FIRST --
    for (const auto& stmt : statements) {
        if (auto contract_stmt = std::dynamic_pointer_cast<const ContractStmt>(stmt)) {
            defineContractHeader(*contract_stmt);
        }
    }
    if (m_hadError) return false;

    // -- STAGE 2b: Define TRAIT headers NEXT --
    for (const auto& stmt : statements) {
        if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
            defineTraitHeader(*trait_stmt);
        }
    }
    if (m_hadError) return false;

    // -- STAGE 2c: Define CLASS headers, which validates traits and contracts --
    for (const auto& stmt : statements) {
        if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
            defineClassHeader(*class_stmt);
        }
    }
    if (m_hadError) return false;

    // -- STAGE 2d: Define global FUNCTION headers LAST --
    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            if (m_symbols.resolve(func_stmt->name.lexeme) == nullptr) {
                 defineFunctionHeader(*func_stmt);
            }
        }
    }
    if (m_hadError) return false;

    // --- PASS 3: Check all implementation code ---
    for (const auto& stmt : statements) {
        stmt->accept(*this, stmt);
    }

    return !m_hadError;
}

    void TypeChecker::error(const Token& token, const std::string& message) {
        // We only report the first error in a sequence to avoid spam.
        if (m_hadError) return;
        m_hadError = true;
        m_errorHandler.report(token, message);
    }

    void TypeChecker::note(const Token& token, const std::string& message) {
        m_errorHandler.note(token, message);
    }

    // Pops a type from our internal type stack.
    // This is used to get the result type of an expression.
    std::shared_ptr<Type> TypeChecker::popType() {
        if (m_type_stack.empty()) {
            // This indicates a bug in the TypeChecker itself.
            throw std::logic_error("Type stack empty during pop.");
        }
        auto type = m_type_stack.top();
        m_type_stack.pop();
        return type;
    }



std::shared_ptr<Type> TypeChecker::resolveType(const std::shared_ptr<ASTType>& ast_type) {
    if (!ast_type) {
        return m_type_error;
    }

    // --- Case 1: The type is an Optional Type, e.g., `User?` ---
    if (auto optional_ast_node = std::dynamic_pointer_cast<const OptionalTypeNode>(ast_type)) {
        // Recursively resolve the type that is being wrapped.
        auto wrapped_semantic_type = resolveType(optional_ast_node->base_type);
        if (wrapped_semantic_type->kind == TypeKind::ERROR) {
            return m_type_error;
        }
        // Return our internal semantic OptionalType.
        return std::make_shared<OptionalType>(wrapped_semantic_type);
    }

    // --- Case 2: A simple type name, e.g., 'i64' or 'User' ---
    if (auto simple = std::dynamic_pointer_cast<const SimpleType>(ast_type)) {
        const std::string& name = simple->name.lexeme;

        // Check for built-in primitive types first.
        if (name == "i64" || name == "int") return m_type_i64;
        if (name == "f64" || name == "float") return m_type_f64;
        if (name == "bool") return m_type_bool;
        if (name == "string") return m_type_string;
        if (name == "nil") return m_type_nil;
        if (name == "any") return m_type_any;
        if (name == "Thread") return m_type_thread;

        // If not a primitive, it must be a user-defined type. Look it up.
        auto symbol = m_symbols.resolve(name);
        if (symbol) {
            if (symbol->type->kind == TypeKind::CLASS) {
                return std::make_shared<InstanceType>(std::dynamic_pointer_cast<ClassType>(symbol->type));
            }
            if (symbol->type->kind == TypeKind::DATA || symbol->type->kind == TypeKind::TRAIT || symbol->type->kind == TypeKind::CONTRACT) {
                return symbol->type;
            }
        }

        error(simple->name, "Unknown type name '" + name + "'.");
        return m_type_error;
    }

    // --- Case 3: A generic type, e.g., 'list<T>' ---
    if (auto generic = std::dynamic_pointer_cast<const GenericType>(ast_type)) {
        const std::string& base_name = generic->name.lexeme;
        if (base_name == "list") {
            if (generic->arguments.size() != 1) {
                error(generic->name, "The 'list' type requires exactly one generic argument.");
                return m_type_error;
            }
            auto element_type = resolveType(generic->arguments[0]);
            if (element_type->kind == TypeKind::ERROR) return m_type_error;
            return std::make_shared<ListType>(element_type);
        }
        error(generic->name, "Unknown generic type '" + base_name + "'.");
        return m_type_error;
    }

    // --- Case 4: An inline record type, e.g., `{ name: string }` ---
    if (auto record_type_expr = std::dynamic_pointer_cast<const RecordTypeExpr>(ast_type)) {
        std::map<std::string, std::shared_ptr<Type>> fields;
        for (const auto& field_def : record_type_expr->fields) {
            const std::string& field_name = field_def.name.lexeme;
            if (fields.contains(field_name)) {
                error(field_def.name, "Duplicate field name '" + field_name + "' in record type definition.");
            }
            fields[field_name] = resolveType(field_def.type);
        }
        return std::make_shared<RecordType>(fields);
    }

    // --- Case 5: A function type, e.g., `function() -> nil` ---
    if (auto func_type_expr = std::dynamic_pointer_cast<const FunctionTypeExpr>(ast_type)) {
        std::vector<std::shared_ptr<Type>> param_types;
        for (const auto& p_ast_type : func_type_expr->param_types) {
            param_types.push_back(resolveType(p_ast_type));
        }
        auto return_type = resolveType(func_type_expr->return_type);
        return std::make_shared<FunctionType>(param_types, return_type);
    }

    // If the AST node type is unknown, it's a compiler bug.
    return m_type_error;
}

    bool TypeChecker::isNumeric(const std::shared_ptr<Type>& type) {
        return isInteger(type) || isFloat(type);
    }

} // namespace angara