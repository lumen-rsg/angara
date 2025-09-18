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
        auto it = m_narrowed_types.find(symbol.get());
        if (it != m_narrowed_types.end()) {
            // It does! Create a temporary, "fake" symbol on the stack
            // that has the same properties but with the new, narrowed type.
            Symbol narrowed_symbol = *symbol;
            narrowed_symbol.type = it->second;
            return std::make_shared<Symbol>(narrowed_symbol);
        }

        // 3. No narrowing applies. Return the original symbol.
        return symbol;
    }

    TypeChecker::TypeChecker(CompilerDriver& driver, ErrorHandler& errorHandler, std::string module_name)
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
        auto len_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_any},
                m_type_i64
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "len", 0, 0), len_type, true);

        // func typeof(any) -> string;
        auto typeof_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_any},
                m_type_string
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "typeof", 0, 0), typeof_type, true);

        auto worker_fn_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{}, // No parameters
            m_type_nil
        );

        // Define the signature for `spawn` itself: function(function() -> void) -> Thread
        auto spawn_type = std::make_shared<FunctionType>(
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

    void TypeChecker::pushAndSave(const Expr* expr, std::shared_ptr<Type> type) {
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

    void TypeChecker::defineTraitHeader(const TraitStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto trait_type = std::dynamic_pointer_cast<TraitType>(symbol->type);

        // 2. If the trait was marked with 'export', add it to the module's public API.
        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = trait_type;
        }

        // We are defining a trait's header, so we ARE in a trait.
        m_is_in_trait = true;

        for (const auto& method_stmt : stmt.methods) {
            // We need to resolve the signature of this method prototype.
            // We can't use defineFunctionHeader because it declares a global symbol.
            // Let's do it manually.
            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& p : method_stmt->params) {
                param_types.push_back(resolveType(p.type));
            }
            std::shared_ptr<Type> return_type = m_type_nil;
            if (method_stmt->returnType) {
                return_type = resolveType(method_stmt->returnType);
            }
            auto method_type = std::make_shared<FunctionType>(param_types, return_type);

            if (trait_type->methods.count(method_stmt->name.lexeme)) {
                error(method_stmt->name, "Duplicate method in trait.");
            } else {
                trait_type->methods[method_stmt->name.lexeme] = method_type;
            }
        }

        m_is_in_trait = false;
    }



    void TypeChecker::defineFunctionHeader(const FuncStmt& stmt) {
        std::vector<std::shared_ptr<Type>> param_types;

        if (stmt.has_this) {
            if (m_current_class == nullptr) {
                error(stmt.name, "Cannot use 'this' in a non-method function.");
            }
        }

        for (const auto& p : stmt.params) {
            if (p.type) {
                param_types.push_back(resolveType(p.type));
            } else {
                error(p.name, "Missing type annotation for parameter '" + p.name.lexeme + "'.");
                param_types.push_back(m_type_error);
            }
        }

        std::shared_ptr<Type> return_type = m_type_nil;
        if (stmt.returnType) {
            return_type = resolveType(stmt.returnType);
        }

        auto function_type = std::make_shared<FunctionType>(param_types, return_type);

        // --- THE FIX IS HERE ---
        if (auto conflicting_symbol = m_symbols.declare(stmt.name, function_type, true)) {
            error(stmt.name, "re-declaration of symbol '" + stmt.name.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }
        // --- END FIX ---

        if (stmt.is_exported || stmt.name.lexeme == "main") {
            if (m_current_class != nullptr) {
                error(stmt.name, "'export' can only be used on top-level declarations.");
            } else {
                m_module_type->exports[stmt.name.lexeme] = function_type;
            }
        }
    }


    void TypeChecker::defineClassHeader(const ClassStmt& stmt) {
        // 1. Fetch the placeholder ClassType that was created in Pass 1.
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        // This should never fail if Pass 1 ran correctly.
        auto class_type = std::dynamic_pointer_cast<ClassType>(symbol->type);

        // 2. If the class was marked with 'export', add it to the module's public API.
        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = class_type;
        }

        // Set the current class context so 'this' can be resolved if needed
        // (e.g., for a method signature that returns the instance type).
        m_current_class = class_type;

        // 2. Resolve and link the superclass, if one exists.
        if (stmt.superclass) {
            auto super_symbol = m_symbols.resolve(stmt.superclass->name.lexeme);
            if (!super_symbol) {
                error(stmt.superclass->name, "Undefined superclass '" + stmt.superclass->name.lexeme + "'.");
            } else if (super_symbol->type->kind != TypeKind::CLASS) {
                error(stmt.superclass->name, "'" + stmt.superclass->name.lexeme + "' is not a class and cannot be inherited from.");
            } else {
                auto superclass_type = std::dynamic_pointer_cast<ClassType>(super_symbol->type);

                // --- INHERITANCE CYCLE DETECTION ---
                auto current = superclass_type;
                bool has_cycle = false;
                while (current) {
                    if (current->name == class_type->name) {
                        error(stmt.name, "Inheritance cycle detected: class '" + class_type->name + "' cannot inherit from itself.");
                        has_cycle = true;
                        break;
                    }
                    current = current->superclass;
                }

                // Only link the superclass if no cycle was found.
                if (!has_cycle) {
                    class_type->superclass = superclass_type;
                }
            }
        }

        // 3. Resolve member signatures (fields and methods).
        for (const auto& member : stmt.members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                const auto& field_decl = field_member->declaration;
                auto field_type = m_type_error; // Default to error type

                // For the header pass, we can only get a field's type from its annotation.
                // We cannot inspect the initializer yet, as it might depend on other
                // types or functions that haven't been fully defined.
                if (field_decl->typeAnnotation) {
                    field_type = resolveType(field_decl->typeAnnotation);
                } else {
                    // This is a style rule: in the header, all fields must have explicit types.
                    error(field_decl->name, "A class field must have an explicit type annotation. Type inference from initializers is done in a later pass.");
                }

                if (class_type->fields.count(field_decl->name.lexeme)) {
                    error(field_decl->name, "A member with this name already exists in the class.");
                }
                class_type->fields[field_decl->name.lexeme] = {field_type, field_member->access, field_decl->name, field_decl->is_const};

            } else if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                const auto& method_decl = method_member->declaration;

                // Resolve the method's full signature into a FunctionType.
                std::vector<std::shared_ptr<Type>> param_types;
                for (const auto& p : method_decl->params) {
                    param_types.push_back(resolveType(p.type));
                }
                std::shared_ptr<Type> return_type = m_type_nil;
                if (method_decl->returnType) {
                    return_type = resolveType(method_decl->returnType);
                }
                auto method_type = std::make_shared<FunctionType>(param_types, return_type);

                if (class_type->methods.count(method_decl->name.lexeme) || class_type->fields.count(method_decl->name.lexeme)) {
                    error(method_decl->name, "A member with this name already exists in the class.");
                }
                class_type->methods[method_decl->name.lexeme] = {method_type, method_member->access, method_decl->name, false}; // Methods aren't const
            }
        }

            // --- NEW: VALIDATE SIGNED CONTRACTS ---
    for (const auto& contract_expr : stmt.contracts) {
        auto contract_symbol = m_symbols.resolve(contract_expr->name.lexeme);
        if (!contract_symbol) {
            error(contract_expr->name, "Undefined contract '" + contract_expr->name.lexeme + "'.");
            continue;
        }
        if (contract_symbol->type->kind != TypeKind::CONTRACT) {
            error(contract_expr->name, "'" + contract_expr->name.lexeme + "' is not a contract.");
            continue;
        }
        auto contract_type = std::dynamic_pointer_cast<ContractType>(contract_symbol->type);

        // Check for required FIELDS
        for (const auto& [name, required_field] : contract_type->fields) {
            const auto* class_prop = class_type->findProperty(name);
            if (!class_prop) {
                error(stmt.name, "Class '" + stmt.name.lexeme + "' does not fulfill contract '" + contract_type->name + "' because it is missing required field '" + name + "'.");
                note(required_field.declaration_token, "requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_type->methods.count(name)) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires a field named '" + name + "', but class '" + stmt.name.lexeme + "' implements it as a method.");
                note(required_field.declaration_token, "requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_prop->access != AccessLevel::PUBLIC) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires field '" + name + "' to be public, but it is private in class '" + stmt.name.lexeme + "'.");
                note(required_field.declaration_token, "requirement '" + name + "' is defined here.");
            }
            if (class_prop->is_const != required_field.is_const) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires field '" + name + "' to be '" + (required_field.is_const ? "const" : "let") + "', but it is not in class '" + stmt.name.lexeme + "'.");
                note(required_field.declaration_token, "requirement '" + name + "' is defined here.");
            }
            if (class_prop->type->toString() != required_field.type->toString()) {
                error(stmt.name, "Type mismatch for field '" + name + "' required by contract '" + contract_type->name + "'. Expected '" + required_field.type->toString() + "', but got '" + class_prop->type->toString() + "'.");
                note(required_field.declaration_token, "requirement '" + name + "' is defined here.");
            }
        }

        // Check for required METHODS
        for (const auto& [name, required_method] : contract_type->methods) {
            const auto* class_prop = class_type->findProperty(name);
            if (!class_prop) {
                error(stmt.name, "Class '" + stmt.name.lexeme + "' does not fulfill contract '" + contract_type->name + "' because it is missing required method '" + name + "'.");
                note(required_method.declaration_token, "requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_type->fields.count(name)) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires a method named '" + name + "', but class '" + stmt.name.lexeme + "' implements it as a field.");
                note(required_method.declaration_token, "requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_prop->access != AccessLevel::PUBLIC) {
                 error(stmt.name, "Contract '" + contract_type->name + "' requires method '" + name + "' to be public, but it is private in class '" + stmt.name.lexeme + "'.");
                note(required_method.declaration_token, "requirement '" + name + "' is defined here.");
            }
            auto required_func_type = std::dynamic_pointer_cast<FunctionType>(required_method.type);
            auto class_func_type = std::dynamic_pointer_cast<FunctionType>(class_prop->type);
            if (!class_func_type->equals(*required_func_type)) {
                error(stmt.name, "The signature of method '" + name + "' in class '" + stmt.name.lexeme + "' does not match the signature required by contract '" + contract_type->name + "'.\n  Required: " + required_func_type->toString() + "\n  Found:    " + class_func_type->toString());
                note(required_method.declaration_token, "requirement '" + name + "' is defined here.");
            }
        }
    }

        // 4. Resolve and validate traits.
        for (const auto& trait_expr : stmt.traits) {
            auto trait_symbol = m_symbols.resolve(trait_expr->name.lexeme);
            if (!trait_symbol) {
                error(trait_expr->name, "Undefined trait '" + trait_expr->name.lexeme + "'.");
                continue; // Move to the next trait
            }
            if (trait_symbol->type->kind != TypeKind::TRAIT) {
                error(trait_expr->name, "'" + trait_expr->name.lexeme + "' is not a trait.");
                continue;
            }
            auto trait_type = std::dynamic_pointer_cast<TraitType>(trait_symbol->type);

            // Check if the class implements all methods required by the trait.
            for (const auto& [name, required_sig] : trait_type->methods) {
                auto method_it = class_type->methods.find(name);
                if (method_it == class_type->methods.end()) {
                    error(stmt.name, "Class '" + stmt.name.lexeme + "' does not implement required trait method '" + name + "'.");
                } else {
                    // Method exists, now check the signature.
                    auto implemented_sig_info = method_it->second;
                    auto implemented_sig = std::dynamic_pointer_cast<FunctionType>(implemented_sig_info.type);

                    if (!implemented_sig->equals(*required_sig)) {
                        error(stmt.name, "The signature of method '" + name + "' in class '" + stmt.name.lexeme +
                            "' does not match the signature required by trait '" + trait_type->name + "'.\n" +
                            "  Required: " + required_sig->toString() + "\n" +
                            "  Found:    " + implemented_sig->toString());

                    }
                }
            }
        }

        m_current_class = nullptr; // Unset context

    }

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
            else if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) { // <-- ADD THIS
                auto data_type = std::make_shared<DataType>(data_stmt->name.lexeme);
                if (auto conflicting = m_symbols.declare(data_stmt->name, data_type, true)) {
                    error(data_stmt->name, "re-declaration of symbol '" + data_stmt->name.lexeme + "'.");
                    note(conflicting->declaration_token, "previous declaration was here.");
                }
            }
        }
    if (m_hadError) return false;

    // --- PASS 2: Define all headers and signatures (Order is important!) ---

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

    // --- AST Visitor Implementations ---

// =======================================================================
// REPLACE the entire resolveType function in TypeChecker.cpp with this
// new, correct, and final version.
// =======================================================================

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
            if (fields.count(field_name)) {
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

void TypeChecker::visit(std::shared_ptr<const VarDeclStmt> stmt) {
    std::shared_ptr<Type> initializer_type = nullptr;
    if (stmt->initializer) {
        stmt->initializer->accept(*this);
        initializer_type = popType();
    }

    std::shared_ptr<Type> declared_type = nullptr;
    if (stmt->typeAnnotation) {
        declared_type = resolveType(stmt->typeAnnotation);
    }

    // --- Logic for type inference and error checking (unchanged) ---
    if (!declared_type && initializer_type) {
        declared_type = initializer_type;
    } else if (declared_type && !initializer_type) {
        // This is fine
    } else if (!declared_type && !initializer_type) {
        error(stmt->name, "Cannot declare a variable without a type annotation or an initializer.");
        declared_type = m_type_error;
    } else if (declared_type && initializer_type) {
        bool types_match = (declared_type->toString() == initializer_type->toString());
        if (!types_match && initializer_type->toString() == "list<any>" && declared_type->kind == TypeKind::LIST) {
            if (auto list_expr = std::dynamic_pointer_cast<const ListExpr>(stmt->initializer)) {
                if (list_expr->elements.empty()) {
                    types_match = true;
                }
            }
        }
        if (!types_match && isInteger(declared_type) && initializer_type->toString() == "i64") {
            types_match = true;
        }
        if (!types_match && initializer_type->kind == TypeKind::ANY) {
            types_match = true; // It's always safe to assign 'any' to a typed variable.
        }

        // Rule 2: It's safe to assign a generic record '{}' from a native
        // function to a specifically typed record variable. This is a type assertion.
        if (!types_match && declared_type->kind == TypeKind::RECORD && initializer_type->kind == TypeKind::RECORD) {
            auto initializer_record = std::dynamic_pointer_cast<RecordType>(initializer_type);
            if (initializer_record->fields.empty()) {
                types_match = true;
            }
        }

        if (!types_match) {
            error(stmt->name, "Type mismatch. Variable is annotated as '" +
                declared_type->toString() + "' but is initialized with a value of type '" +
                initializer_type->toString() + "'.");
            declared_type = m_type_error;
        }

        if (!types_match && declared_type->kind == TypeKind::OPTIONAL) {
            auto optional_type = std::dynamic_pointer_cast<OptionalType>(declared_type);
            // You can assign a T to a T?
            if (optional_type->wrapped_type->toString() == initializer_type->toString()) {
                types_match = true;
            }
            // You can assign `nil` to a T?
            if (initializer_type->kind == TypeKind::NIL) {
                types_match = true;
            }
        }
    }

    m_variable_types[stmt.get()] = declared_type;

    // --- THE FIX IS HERE ---
    // Declare the symbol in the current scope
    if (auto conflicting_symbol = m_symbols.declare(stmt->name, declared_type, stmt->is_const)) {
        error(stmt->name, "re-declaration of variable '" + stmt->name.lexeme + "'.");
        note(conflicting_symbol->declaration_token, "previous declaration was here.");
    }
    // --- END FIX ---

    if (stmt->is_exported) {
        if (m_symbols.getScopeDepth() > 0) {
            error(stmt->name, "'export' can only be used on top-level declarations.");
        } else {
            m_module_type->exports[stmt->name.lexeme] = declared_type;
        }
    }
}

    std::any TypeChecker::visit(const Literal& expr) {
        std::shared_ptr<Type> type = m_type_error;
        // Determine the type of the literal and push it onto our type stack.
        switch (expr.token.type) {
            case TokenType::NUMBER_INT:   type = m_type_i64; break;
            case TokenType::NUMBER_FLOAT: type = m_type_f64; break;
            case TokenType::STRING:       type = m_type_string; break;
            case TokenType::TRUE:
            case TokenType::FALSE:        type = m_type_bool; break;
            case TokenType::NIL:          type = m_type_nil; break;
            default:
                // Should be unreachable if the parser is correct.
                type = m_type_error;
                break;
        }
        pushAndSave(&expr, type);
        return {}; // The actual return value is unused.
    }

    std::any TypeChecker::visit(const IsExpr& expr) {
        // The `is` operator itself doesn't need narrowing; it just produces a boolean.
        // We visit the sub-expressions to ensure they are valid.
        expr.object->accept(*this);
        popType(); // We don't need the object's type here.

        // We don't need to "visit" the RHS type, just resolve it.
        resolveType(expr.type);

        // The result of an `is` expression is always a boolean.
        pushAndSave(&expr, m_type_bool);
        return {};
    }

    std::any TypeChecker::visit(const VarExpr& expr) {
        // Use our new helper that understands type narrowing.
        auto symbol = resolve_and_narrow(expr);

        if (!symbol) {
            error(expr.name, "Undefined variable '" + expr.name.lexeme + "'.");
            pushAndSave(&expr, m_type_error);
        } else {
            // Save the original resolution for the transpiler (it doesn't care about narrowing).
            m_variable_resolutions[&expr] = m_symbols.resolve(expr.name.lexeme);
            // Push the potentially narrowed type for type checking.
            pushAndSave(&expr, symbol->type);
        }
        return {};
    }

    std::any TypeChecker::visit(const Unary& expr) {
        expr.right->accept(*this);
        auto right_type = popType();

        std::shared_ptr<Type> result_type = m_type_error;
        switch (expr.op.type) {
            case TokenType::MINUS:
                if (isNumeric(right_type)) result_type = right_type;
                else error(expr.op, "Operand for '-' must be a number.");
                break;
            case TokenType::BANG:
                if (right_type->toString() == "bool") result_type = m_type_bool;
                else error(expr.op, "Operand for '!' must be a boolean.");
                break;
        }
        pushAndSave(&expr, result_type);
        return {};
    }

// in TypeChecker.cpp

    std::any TypeChecker::visit(const Binary& expr) {
        // 1. Visit operands to get their types.
        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        // 2. Default to an error type. We only change this if a rule passes.
        std::shared_ptr<Type> result_type = m_type_error;

        // 3. Bail out early if sub-expressions had errors.
        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 4. Check the types based on the operator.
        switch (expr.op.type) {
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
            case TokenType::PERCENT:
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else {
                    error(expr.op, "Operands for this arithmetic operator must be numbers.");
                }
                break;

            case TokenType::PLUS:
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else if (left_type->toString() == "string" && right_type->toString() == "string") {
                    result_type = m_type_string;
                } else {
                    error(expr.op, "'+' operator can only be used on two numbers or two strings.");
                }
                break;

            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    result_type = m_type_bool;
                } else {
                    error(expr.op, "Operands for comparison must be numbers.");
                }
                break;

            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL: {
                // The comparison is valid if:
                // 1. The types are exactly the same.
                // 2. One of the types is 'any' (or nil, which can be compared to anything).
                // 3. Both types are numeric (allowing i64 == f64).
                // --- NEW RULE ---
                // Two instances of the same data type can be compared.
                if (left_type->kind == TypeKind::DATA && right_type->kind == TypeKind::DATA) {
                    if (left_type->toString() == right_type->toString()) {
                        result_type = m_type_bool;
                    } else {
                        error(expr.op, "Cannot compare instances of two different data types: '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.");
                    }
                }
                else if (left_type->toString() == right_type->toString() ||
                    left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY ||
                    left_type->kind == TypeKind::NIL || right_type->kind == TypeKind::NIL ||
                    (isNumeric(left_type) && isNumeric(right_type)))
                {
                    result_type = m_type_bool;
                } else {
                    error(expr.op, "Cannot compare two different types: '" +
                                   left_type->toString() + "' and '" + right_type->toString() + "'.");
                }
                break;
            }

            default:
                error(expr.op, "Unknown binary operator.");
                break;
        }

        // 5. Push the single, definitive result type for this expression.
        pushAndSave(&expr, result_type);
        return {};
    }


    std::any TypeChecker::visit(const Grouping& expr) {
        expr.expression->accept(*this);
        auto inner_type = popType();
        pushAndSave(&expr, inner_type);
        return {};
    }

    bool TypeChecker::isNumeric(const std::shared_ptr<Type>& type) {
        return isInteger(type) || isFloat(type);
    }

    std::any TypeChecker::visit(const ListExpr& expr) {
        // Case 1: The list is empty. Its type is `list<any>`.
        if (expr.elements.empty()) {
            auto empty_list_type = std::make_shared<ListType>(m_type_any);
            pushAndSave(&expr, empty_list_type);
            return {};
        }

        // Case 2: The list has elements. We must infer the common element type.

        // 1. Determine the type of the *first* element. This is our initial candidate.
        expr.elements[0]->accept(*this);
        auto common_element_type = popType();

        // 2. Iterate through the rest of the elements and update the common type.
        for (size_t i = 1; i < expr.elements.size(); ++i) {
            expr.elements[i]->accept(*this);
            auto current_element_type = popType();

            // If the types don't match, the new common type becomes 'any'.
            if (common_element_type->toString() != current_element_type->toString()) {
                // We can add more sophisticated rules here later, e.g., if you have
                // an i64 and an f64, the common type could be f64. For now, any
                // mismatch defaults to 'any'.
                common_element_type = m_type_any;
            }

            // If the common type is already 'any', we can stop checking,
            // as 'any' is the "top type" and won't change.
            if (common_element_type->kind == TypeKind::ANY) {
                // We still need to process the rest of the elements to populate
                // the expression types map, but we don't need to check their types.
                for (size_t j = i + 1; j < expr.elements.size(); ++j) {
                    expr.elements[j]->accept(*this);
                    popType();
                }
                break; // Exit the main checking loop.
            }
        }

        // 3. The final, inferred type of this expression is a ListType of the common type.
        auto final_list_type = std::make_shared<ListType>(common_element_type);
        pushAndSave(&expr, final_list_type);

        return {};
    }

    void TypeChecker::visit(std::shared_ptr<const IfStmt> stmt) {
        // --- Case 1: Handle `if let ...` for optional unwrapping ---
        if (stmt->declaration) {
            // 1a. The declaration must have an initializer. The parser should guarantee this.
            if (!stmt->declaration->initializer) {
                error(stmt->declaration->name, "Compiler Error: 'if let' declaration is missing an initializer.");
                return;
            }

            // 1b. Type check the initializer expression.
            stmt->declaration->initializer->accept(*this);
            auto initializer_type = popType();

            if (initializer_type->kind == TypeKind::ERROR) return;

            // 1c. The initializer MUST be an optional type.
            if (initializer_type->kind != TypeKind::OPTIONAL) {
                error(stmt->declaration->name, "The value for an 'if let' statement must be an optional type (e.g., 'string?'), but got a non-optional value of type '" + initializer_type->toString() + "'.");
            } else {
                // It is an optional, proceed with checking the 'then' branch.
                m_symbols.enterScope();

                // Declare the new variable with the UNWRAPPED type inside the new scope.
                auto unwrapped_type = std::dynamic_pointer_cast<OptionalType>(initializer_type)->wrapped_type;
                // The binding is implicitly constant.
                m_symbols.declare(stmt->declaration->name, unwrapped_type, true);

                // Now, check the 'then' branch. Inside this block, the new variable
                // is in scope and has the safe, unwrapped type.
                stmt->thenBranch->accept(*this, stmt->thenBranch);

                m_symbols.exitScope(); // The new variable goes out of scope here.
            }

            // Check the 'else' branch normally. The unwrapped variable is not in scope here.
            if (stmt->elseBranch) {
                stmt->elseBranch->accept(*this, stmt->elseBranch);
            }
            return; // We have handled the entire `if let` statement.
        }


        // --- Case 2: Handle `if ... is ...` for type narrowing ---
        if (auto is_expr = std::dynamic_pointer_cast<const IsExpr>(stmt->condition)) {
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(is_expr->object)) {
                // The condition is a type check on a variable.

                // 2a. Type check the condition itself to make sure it's valid.
                stmt->condition->accept(*this);
                popType(); // We don't need the boolean result, just the validation.
                if (m_hadError) return;

                auto original_symbol = m_symbols.resolve(var_expr->name.lexeme);
                if (original_symbol) {
                    auto narrowed_type = resolveType(is_expr->type);

                    // 2b. Apply the narrowed type for the 'then' branch.
                    m_narrowed_types[original_symbol.get()] = narrowed_type;
                    stmt->thenBranch->accept(*this, stmt->thenBranch);
                    // 2c. CRITICAL: Remove the narrowing after the 'then' branch is done.
                    m_narrowed_types.erase(original_symbol.get());
                } else {
                    // This should have been caught when visiting the condition, but for safety:
                    stmt->thenBranch->accept(*this, stmt->thenBranch);
                }

                // 2d. Check the 'else' branch normally, without the narrowing.
                if (stmt->elseBranch) {
                    stmt->elseBranch->accept(*this, stmt->elseBranch);
                }
                return; // We have handled the entire `if is` statement.
            }
        }


        // --- Case 3: Handle a regular `if` statement with a boolean condition ---
        stmt->condition->accept(*this);
        auto condition_type = popType();

        if (!isTruthy(condition_type)) {
            error(stmt->keyword, "If statement condition must be a boolean or truthy value, but got '" +
                                 condition_type->toString() + "'.");
        }

        // Check both branches normally.
        stmt->thenBranch->accept(*this, stmt->thenBranch);
        if (stmt->elseBranch) {
            stmt->elseBranch->accept(*this, stmt->elseBranch);
        }
    }

    void TypeChecker::visit(std::shared_ptr<const EmptyStmt> stmt) {
        // *literally does nothing. just like me when. idk xd*
    }

    void TypeChecker::visit(std::shared_ptr<const WhileStmt> stmt) {
        // 1. Type check the condition expression.
        stmt->condition->accept(*this);
        auto condition_type = popType();

        // 2. Enforce the rule: the condition must be a 'bool'.
        if (!isTruthy(condition_type)) {
            error(Token(), "While loop condition must be of type 'bool', but got '" +
                           condition_type->toString() + "'.");
        }

        // 3. Type check the loop body.
        m_loop_depth++; // <-- ENTER a loop
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--; // <-- EXIT a loop
    }

    void TypeChecker::visit(std::shared_ptr<const ForStmt> stmt) {
        // 1. A C-style for loop introduces a new scope for its initializer.
        m_symbols.enterScope();

        // 2. Type check the initializer, if it exists.
        if (stmt->initializer) {
            stmt->initializer->accept(*this, stmt->initializer);
        }

        // 3. Type check the condition, if it exists.
        if (stmt->condition) {
            stmt->condition->accept(*this);
            auto condition_type = popType();
            // Enforce that the condition is a "truthy" type.
            if (!isTruthy(condition_type)) {
                error(stmt->keyword, "For loop condition must be a truthy type (bool or number), but got '" +
                                     condition_type->toString() + "'.");
            }
        }

        // 4. Type check the increment, if it exists.
        if (stmt->increment) {
            stmt->increment->accept(*this);
            popType(); // The resulting value of the increment expression is not used.
        }

        m_loop_depth++; // <-- ENTER a loop
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--; // <-- EXIT a loop

        // 6. Exit the scope, destroying the initializer variable.
        m_symbols.exitScope();
    }

    void TypeChecker::visit(std::shared_ptr<const ForInStmt> stmt) {
        // 1. First, type check the expression that provides the collection.
        stmt->collection->accept(*this);
        auto collection_type = popType();

        // 2. Determine the type of the items IN the collection.
        std::shared_ptr<Type> item_type = m_type_error; // Default to error
        if (collection_type->kind == TypeKind::LIST) {
            // If it's a list<T>, the item type is T.
            item_type = std::dynamic_pointer_cast<ListType>(collection_type)->element_type;
        } else if (collection_type->toString() == "string") {
            // If it's a string, the item type is also string (for each character).
            item_type = m_type_string;
        } else {
            error(stmt->name, "The 'for..in' loop can only iterate over a list or a string, but got '" +
                              collection_type->toString() + "'.");
        }

        // 3. The entire loop introduces a new scope for the loop variable.
        m_symbols.enterScope();

        // 4. Declare the loop variable (e.g., 'ability_obj') inside this new scope.
        //    It is a constant for each iteration.
        if (auto conflicting_symbol = m_symbols.declare(stmt->name, item_type, true)) {
            // This case should be logically impossible if enterScope() works correctly,
            // as the scope has just been created. But we handle it for robustness.
            error(stmt->name, "compiler internal error: re-declaration of loop variable '" + stmt->name.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        // 5. Now that the loop variable is in scope, type check the body of the loop.
        //    The body executes within the new scope.
        m_loop_depth++;
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--;

        // 6. When the loop is finished, exit its scope, destroying the loop variable.
        m_symbols.exitScope();
    }

    void TypeChecker::visit(std::shared_ptr<const FuncStmt> stmt) {
        // This visitor is called in Pass 2 to check the body of a function.
        // The signature was already processed in Pass 1.

        // A function with no body (a trait method) has no implementation to check.
        if (!stmt->body) {
            return;
        }

        // 1. Fetch the full FunctionType from the symbol table (created in Pass 1).
        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        // Note: for methods, the name is not in the global scope. We need to look it up
        // in the current class context.
        std::shared_ptr<FunctionType> func_type;
        if (m_current_class && m_current_class->methods.count(stmt->name.lexeme)) {
            func_type = std::dynamic_pointer_cast<FunctionType>(m_current_class->methods.at(stmt->name.lexeme).type);
        } else if (symbol && symbol->type->kind == TypeKind::FUNCTION) {
            func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
        } else {
            return; // Error was already reported in Pass 1
        }

        // 2. Enter a new scope for the function's body.
        m_symbols.enterScope();
        m_function_return_types.push(func_type->return_type);

        // 3. If it's a method, declare 'this'.
        if (stmt->has_this && m_current_class) {
            Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
            m_symbols.declare(this_token, std::make_shared<InstanceType>(m_current_class), true);
        }

        // 4. Declare all parameters as local variables.
        for (size_t i = 0; i < stmt->params.size(); ++i) {
            m_symbols.declare(stmt->params[i].name, func_type->param_types[i], true);
        }

        // 5. Type-check every statement in the function's body.
        for (const auto& bodyStmt : (*stmt->body)) {
            bodyStmt->accept(*this, bodyStmt);
        }


        // 6. Restore the context.
        m_function_return_types.pop();
        m_symbols.exitScope();
    }

    void TypeChecker::visit(std::shared_ptr<const ReturnStmt> stmt) {
        if (m_function_return_types.empty()) {
            error(stmt->keyword, "Cannot use 'return' outside of a function.");
            return;
        }

        auto expected_return_type = m_function_return_types.top();

        if (stmt->value) {
            // A value is being returned.
            stmt->value->accept(*this);
            auto actual_return_type = popType();

            // --- THE FIX: Use our central compatibility checker ---
            if (!check_type_compatibility(expected_return_type, actual_return_type)) {
                error(stmt->keyword, "Type mismatch. This function is declared to return '" +
                                     expected_return_type->toString() + "', but is returning a value of type '" +
                                     actual_return_type->toString() + "'.");
            }

        } else {
            // No value is being returned ('return;').
            // This is only valid if the function is supposed to return `nil`.
            // A `nil` return is a value, and must use `return nil;`.
            // So, this case is actually an error unless the expected type is also nil,
            // which check_type_compatibility would handle. Let's make this clearer.

            // A `return;` is semantically equivalent to `return nil;`.
            if (!check_type_compatibility(expected_return_type, m_type_nil)) {
                error(stmt->keyword, "This function must return a value of type '" +
                                     expected_return_type->toString() + "'. An empty 'return;' is only valid for functions that return 'nil'.");
            }
        }
    }


    void TypeChecker::visit(std::shared_ptr<const AttachStmt> stmt) {

    }


    void TypeChecker::visit(std::shared_ptr<const ThrowStmt> stmt) {
        stmt->expression->accept(*this);
        auto thrown_type = popType();

        // --- THE FIX ---
        // Rule: You can only throw objects of type Exception.
        if (thrown_type->kind != TypeKind::EXCEPTION) {
            error(stmt->keyword, "Can only throw objects of type 'Exception', but got '" + thrown_type->toString() + "'.");
        }
    }

    void TypeChecker::visit(std::shared_ptr<const TryStmt> stmt) {
        // 1. Type check the 'try' block.
        stmt->tryBlock->accept(*this, stmt->tryBlock);

        // 2. Now, handle the 'catch' block. It introduces a new scope.
        m_symbols.enterScope();

        // 3. Declare the exception variable (e.g., 'e') in this new scope.
        if (auto conflicting_symbol = m_symbols.declare(stmt->catchName, m_type_any, false)) {
            // This is technically unreachable if the parser works correctly, but it's good practice.
            error(stmt->catchName, "re-declaration of variable '" + stmt->catchName.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        // 4. With the exception variable in scope, type check the 'catch' block.
        stmt->catchBlock->accept(*this, stmt->catchBlock);

        // 5. --- THE FIX ---
        // Exit the scope for the catch block, destroying the exception variable 'e'.
        m_symbols.exitScope();
    }

    void TypeChecker::visit(std::shared_ptr<const ClassStmt> stmt) {
        // PASS 2: Check the implementation details (initializers and method bodies).

        // 1. Fetch the full ClassType from the symbol table. This was populated in Pass 1.
        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        auto class_type = std::dynamic_pointer_cast<ClassType>(symbol->type);

        // 2. Set the context to indicate we are "inside" this class.
        auto enclosing_class = m_current_class;
        m_current_class = class_type;

        // 3. Enter a new scope for the class's body.
        m_symbols.enterScope();

        // 4. Declare 'this' so it's available to initializers and methods.
        Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
        m_symbols.declare(this_token, std::make_shared<InstanceType>(class_type), true);

        // 5. Check all implementation code.
        for (const auto& member : stmt->members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                // Check the field's initializer expression, if it has one.
                if (field_member->declaration->initializer) {
                    const auto& field_name = field_member->declaration->name.lexeme;
                    auto expected_type = class_type->fields.at(field_name).type;

                    field_member->declaration->initializer->accept(*this);
                    auto initializer_type = popType();

                    // Validate that the initializer's type matches the field's declared type.
                    if (expected_type->kind != TypeKind::ERROR &&
                        initializer_type->kind != TypeKind::ERROR) {

                        bool types_match = (expected_type->toString() == initializer_type->toString());
                        // Add our special case for empty lists
                        if (!types_match && initializer_type->toString() == "list<any>" && expected_type->kind == TypeKind::LIST) {
                            types_match = true;
                        }

                        if (!types_match) {
                            error(field_member->declaration->name, "Type mismatch in field initializer. Field '" + field_name +
                                                                   "' is type '" + expected_type->toString() +
                                                                   "' but initializer is type '" + initializer_type->toString() + "'.");
                        }
                    }
                }
            } else if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                // For methods, we just need to visit their FuncStmt node.
                // Our refactored visit(FuncStmt) will handle checking the body.
                visit(method_member->declaration);
            }
        }

        // 6. Restore the context.
        m_symbols.exitScope();
        m_current_class = enclosing_class;
    }

    void TypeChecker::visit(std::shared_ptr<const TraitStmt> stmt) {

    }

    void TypeChecker::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        // 1. Recursively type check the inner expression.
        stmt->expression->accept(*this);

        // 2. The type of that expression is now on our stack. Since the value is
        //    not used, we pop its type to keep our analysis stack clean.
        popType();
    }

    void TypeChecker::visit(std::shared_ptr<const BlockStmt> stmt) {
        // 1. Enter a new lexical scope.
        m_symbols.enterScope();

        // 2. Type check every statement inside the block.
        for (const auto& statement : stmt->statements) {
            if (statement) {
                statement->accept(*this, statement);
            }
        }

        // 3. Exit the lexical scope, destroying all variables declared within it.
        m_symbols.exitScope();
    }

    std::any TypeChecker::visit(const AssignExpr& expr) {
        // 1. Determine the type of the value being assigned (RHS).
        expr.value->accept(*this);
        auto rhs_type = popType();

        if (auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            // We need to get the types of the collection and index before proceeding.
            subscript_target->object->accept(*this);
            auto collection_type = popType();
            subscript_target->index->accept(*this);
            auto index_type = popType();

            if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error); return {};
            }

            if (collection_type->kind == TypeKind::LIST) {
                auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);
                if (!isInteger(index_type)) {
                    error(subscript_target->bracket, "List index for assignment must be an integer, but got '" + index_type->toString() + "'.");
                }
                if (list_type->element_type->toString() != rhs_type->toString()) {
                    error(expr.op, "Type mismatch. Cannot assign value of type '" + rhs_type->toString() + "' to an element of a list of type '" + list_type->toString() + "'.");
                }
            }
            else if (collection_type->kind == TypeKind::RECORD) {
                if (index_type->toString() != "string") {
                    error(subscript_target->bracket, "Record key for assignment must be a string, but got '" + index_type->toString() + "'.");
                } else {
                    if (auto key_literal = std::dynamic_pointer_cast<const Literal>(subscript_target->index)) {
                        // STATIC ASSIGNMENT: Key is known.
                        auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
                        auto field_it = record_type->fields.find(key_literal->token.lexeme);
                        if (field_it == record_type->fields.end()) {
                            error(key_literal->token, "Record of type '" + record_type->toString() + "' has no statically-known field named '" + key_literal->token.lexeme + "'. Use a variable key to add a new field.");
                        } else if (field_it->second->toString() != rhs_type->toString()) {
                            error(expr.op, "Type mismatch. Cannot assign value of type '" + rhs_type->toString() + "' to field '" + key_literal->token.lexeme + "' of type '" + field_it->second->toString() + "'.");
                        }
                    }
                    // DYNAMIC ASSIGNMENT (key is a variable): This is allowed.
                }
            }

            // Subscript assignment expression evaluates to the RHS value.
            pushAndSave(&expr, rhs_type);
            return {};
        }

        // 2. Determine the type of the target being assigned to (LHS).
        expr.target->accept(*this);
        auto lhs_type = popType();

        // 3. If either sub-expression had an error, stop immediately.
        if (rhs_type->kind == TypeKind::ERROR || lhs_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- THE FIX: Centralized Type Compatibility Check ---
        bool types_match = (lhs_type->toString() == rhs_type->toString());

        // Rule: A T or a `nil` can be assigned to a T?
        if (!types_match && lhs_type->kind == TypeKind::OPTIONAL) {
            auto optional_type = std::dynamic_pointer_cast<OptionalType>(lhs_type);
            if (optional_type->wrapped_type->toString() == rhs_type->toString() || rhs_type->kind == TypeKind::NIL) {
                types_match = true;
            }
        }

        // Rule: Anything can be assigned to `any`.
        if (!types_match && lhs_type->kind == TypeKind::ANY) {
            types_match = true;
        }

        // Rule: An `any` can be assigned to a typed variable (runtime cast assertion).
        if (!types_match && rhs_type->kind == TypeKind::ANY) {
            types_match = true;
        }

        // ... (other special rules for empty list, etc.) ...

        if (!types_match) {
            error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                           rhs_type->toString() + "' to a target of type '" +
                           lhs_type->toString() + "'.");
        }
        // --- END FIX ---

        // 4. Check for type compatibility. The LHS type must match the RHS type.
        if (lhs_type->toString() != rhs_type->toString()) {
            // Special case for assigning an empty list `[]` to a typed list variable.
            if (auto list_expr = std::dynamic_pointer_cast<const ListExpr>(expr.value)) {

                if (list_expr->elements.empty() && lhs_type->kind == TypeKind::LIST) {
                    // This is valid, so we skip the error.
                } else {
                    error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                                   rhs_type->toString() + "' to a target of type '" +
                                   lhs_type->toString() + "'.");
                }
            } else {
                error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                               rhs_type->toString() + "' to a target of type '" +
                               lhs_type->toString() + "'.");

            }
        }

        // 5. Check for const-ness and other assignment rules based on the target's kind.
        if (auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            auto symbol = m_symbols.resolve(var_target->name.lexeme);
            if (symbol && symbol->is_const) {
                error(var_target->name, "Cannot assign to 'const' variable '" + symbol->name + "'.");
                note(symbol->declaration_token, "'" + symbol->name + "' was declared 'const' here.");
            }
        }

        else if (auto get_target = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
            // We need to re-evaluate the object type to get the ClassType.
            get_target->object->accept(*this);
            auto object_type = popType();

            if (object_type->kind == TypeKind::INSTANCE) {
                auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const std::string& field_name = get_target->name.lexeme;

                // Use our recursive helper to find the field in the inheritance chain.
                const ClassType::MemberInfo* field_info = instance_type->class_type->findProperty(field_name);

                if (field_info == nullptr) {
                    // The GetExpr visitor would have already caught this, but we check again for safety.
                    // Note: findProperty looks for methods too, we should only allow assigning to fields.
                    error(get_target->name, "Instance of class '" + instance_type->toString() +
                                            "' has no field named '" + field_name + "'.");
                } else {
                    // Check if the found property is actually a field.
                    if (instance_type->class_type->methods.count(field_name)) {
                        error(get_target->name, "Cannot assign to a method. '" + field_name + "' is a method, not a field.");
                    } else {
                        // It's a field. Now check if it's const.
                        if (field_info->is_const) {
                            error(get_target->name, "Cannot assign to 'const' field '" + field_name + "'.");
                        }
                    }
                }
            }
        }

        // An assignment expression evaluates to the assigned value.
        pushAndSave(&expr, rhs_type);
        return {};
    }

    std::any TypeChecker::visit(const UpdateExpr& expr) {
        // An update expression (++, --) is a combination of reading and assigning.

        // 1. Determine the type of the target.
        expr.target->accept(*this);
        auto target_type = popType();

        // 2. Enforce the rule: the target must be a numeric type.
        if (!isNumeric(target_type)) {
            error(expr.op, "Operand for increment/decrement must be a number, but got '" +
                           target_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- Check for Mutability ---
        if (auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            auto symbol = m_symbols.resolve(var_target->name.lexeme);
            if (symbol && symbol->is_const) { // <-- Check for symbol existence first
                error(expr.op, "Cannot modify 'const' variable '" + symbol->name + "'.");
            }
        } else {
            error(expr.op, "Invalid target for increment/decrement.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 3. The result type of update expression is the same as the target's type.
        pushAndSave(&expr, target_type);
        return {};
    }

    bool TypeChecker::check_type_compatibility(
            const std::shared_ptr<Type>& expected,
            const std::shared_ptr<Type>& actual
    ) {
        if (expected->toString() == actual->toString()) return true;

        // Rule: Anything is compatible with 'any'.
        if (expected->kind == TypeKind::ANY || actual->kind == TypeKind::ANY) return true;

        // Rule: A T or a `nil` is compatible with a T?
        if (expected->kind == TypeKind::OPTIONAL) {
            auto optional_type = std::dynamic_pointer_cast<OptionalType>(expected);
            if (optional_type->wrapped_type->toString() == actual->toString() || actual->kind == TypeKind::NIL) {
                return true;
            }
        }

        // Rule: A generic record `{}` is compatible with a specific record `{...}`.
        if (expected->kind == TypeKind::RECORD && actual->kind == TypeKind::RECORD) {
            if (std::dynamic_pointer_cast<RecordType>(expected)->fields.empty()) {
                return true;
            }
        }

        return false; // Not compatible
    }

    // A helper to validate a standard function or method call against a signature.
    void TypeChecker::check_function_call(
            const CallExpr& call,
            const std::shared_ptr<FunctionType>& func_type,
            const std::vector<std::shared_ptr<Type>>& arg_types
    ) {
        size_t num_expected = func_type->param_types.size();
        size_t num_actual = arg_types.size();

        // 1. Check arity (number of arguments)
        if (func_type->is_variadic) {
            // For variadic functions, the user must supply at least the required parameters.
            if (num_actual < num_expected) {
                error(call.paren, "Incorrect number of arguments. Function expects at least " +
                                  std::to_string(num_expected) + " argument(s), but got " +
                                  std::to_string(num_actual) + ".");
                // Add a note pointing to the function's definition.
                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.callee)) {
                    if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                        note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                    }
                }
            }
        } else {
            // For regular functions, the number of arguments must match exactly.
            if (num_actual != num_expected) {
                error(call.paren, "Incorrect number of arguments. Function expects " +
                                  std::to_string(num_expected) + " argument(s), but got " +
                                  std::to_string(num_actual) + ".");
                // Add the same helpful note.
                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.callee)) {
                    if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                        note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                    }
                } else if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(call.callee)) {
                    // Handle notes for method calls, e.g., my_instance.method()
                    // This requires more complex logic to find the method's declaration token
                    // in the ClassType, which will be added later. TODO
                }
            }
        }

        // If an arity error occurred, stop checking.
        if (m_hadError) return;

        // 2. Check types of the fixed parameters (unchanged from previous logic)
        for (size_t i = 0; i < num_expected; ++i) {
            if (!check_type_compatibility(func_type->param_types[i], arg_types[i])) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + ". " +
                                  "Expected '" + func_type->param_types[i]->toString() +
                                  "', but got '" + arg_types[i]->toString() + "'.");
                return; // Stop after first type error
            }
        }
    }

    void TypeChecker::visit(std::shared_ptr<const DataStmt> stmt) {
        // All the important work (defining the type, fields, and constructor)
        // was done in Pass 1 and Pass 2 (defineDataHeader).
        // In this pass, there is no executable code to check.
        // We just need to make sure we don't try to visit the field VarDeclStmts
        // again as if they were local variables.
    }

    void TypeChecker::defineDataHeader(const DataStmt& stmt) {
    // 1. Get the placeholder DataType we created in Pass 1.
    auto symbol = m_symbols.resolve(stmt.name.lexeme);
    auto data_type = std::dynamic_pointer_cast<DataType>(symbol->type);

    if (stmt.is_exported) {
        m_module_type->exports[stmt.name.lexeme] = data_type;
    }

    // 2. Populate the fields and build the constructor signature.
    std::vector<std::shared_ptr<Type>> ctor_params;
    Token dummy_token;

    for (const auto& field_decl : stmt.fields) {
        if (data_type->fields.count(field_decl->name.lexeme)) {
            error(field_decl->name, "Duplicate field '" + field_decl->name.lexeme + "' in data block.");
            continue;
        }

        std::shared_ptr<Type> field_type;
        if (field_decl->typeAnnotation) {
            field_type = resolveType(field_decl->typeAnnotation);
        } else if (field_decl->initializer) {
            // Type inference from default values is an advanced feature.
            // For now, let's require explicit types for data blocks.
            error(field_decl->name, "Fields in a 'data' block must have an explicit type annotation.");
            field_type = m_type_error;
        } else {
            error(field_decl->name, "Fields in a 'data' block must have an explicit type annotation.");
            field_type = m_type_error;
        }

        if (field_decl->initializer) {
            error(field_decl->name, "Fields in a 'data' block cannot have default initializers. Initialization is done via the constructor.");
        }

        // Add to the fields map. All fields are implicitly public.
        data_type->fields[field_decl->name.lexeme] = {field_type, AccessLevel::PUBLIC, dummy_token, field_decl->is_const};

        // Add this field's type to the constructor's parameter list.
        ctor_params.push_back(field_type);
    }

    // 3. Create and store the constructor's FunctionType.
    // The return type is the data type itself.
    data_type->constructor_type = std::make_shared<FunctionType>(ctor_params, data_type);
}

// A helper for the special-case validation logic for `spawn`.
    void TypeChecker::check_spawn_call(
            const CallExpr& call,
            const std::vector<std::shared_ptr<Type>>& arg_types
    ) {
        // Rule 1: spawn() must be called with at least one argument (the function).
        if (arg_types.empty()) {
            error(call.paren, "spawn() requires at least one argument, the function to execute in the new thread.");
            return;
        }

        // Rule 2: The first argument to spawn() must be a function.
        auto closure_type = arg_types[0];
        if (closure_type->kind != TypeKind::FUNCTION) {
            error(call.paren, "The first argument to spawn() must be a function, but got a value of type '" + closure_type->toString() + "'.");
            return;
        }
        auto func_type = std::dynamic_pointer_cast<FunctionType>(closure_type);

        // The arguments passed to spawn (after the function itself) must match the function's parameters.
        size_t num_expected_args = func_type->param_types.size();
        size_t num_actual_args = arg_types.size() - 1;

        // Rule 3: Check arity for the spawned function.
        if (num_actual_args != num_expected_args) {
            error(call.paren, "Incorrect number of arguments for the spawned function. "
                              "The function expects " + std::to_string(num_expected_args) +
                              " argument(s), but " + std::to_string(num_actual_args) + " were provided to spawn().");

            // Add a note pointing to the function being spawned, if it's a direct variable.
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.arguments[0])) {
                if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                    note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                }
            }
            return;
        }

        // Rule 4: Check the type of each argument for the spawned function.
        for (size_t i = 0; i < num_actual_args; ++i) {
            const auto& expected_type = func_type->param_types[i];
            // The actual arguments start from the second element of the `spawn` call.
            const auto& actual_type = arg_types[i + 1];

            if (!check_type_compatibility(expected_type, actual_type)) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + " of spawned function. "
                                                                                          "Expected '" + expected_type->toString() + "', but got '" + actual_type->toString() + "'.");

                // Add the same helpful note.
                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.arguments[0])) {
                    if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                        note(symbol->declaration_token, "function '" + symbol->name + "' is defined here.");
                    }
                }
                return; // Stop after the first error.
            }
        }
    }

    std::any TypeChecker::visit(const GetExpr& expr) {
    // 1. First, recursively type check the object on the left of the operator.
    expr.object->accept(*this);
    auto object_type = popType();

    // Bail out early if the object itself had a type error.
    if (object_type->kind == TypeKind::ERROR) {
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // 2. Determine if this is an optional chain (`?.`) or a regular access (`.`).
    bool is_optional_chain = (expr.op.type == TokenType::QUESTION_DOT);
    std::shared_ptr<Type> unwrapped_object_type = object_type;

    // 3. Handle the optionality of the object.
    if (object_type->kind == TypeKind::OPTIONAL) {
        // The object is optional (e.g., `Player?`). We can proceed with either `.` or `?.`.
        // We will work with the type it wraps (e.g., `Player`).
        unwrapped_object_type = std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type;
    } else if (is_optional_chain) {
        // This is the case `non_optional?.field`. It's redundant but safe.
        // TODO: Add a compiler warning/note here for good style.
    }

    // A regular access (`.`) on an optional type is a compile-time error.
    // This prevents accidental null pointer errors.
    if (object_type->kind == TypeKind::OPTIONAL && !is_optional_chain) {
        error(expr.op, "Cannot access property on an optional type '" + object_type->toString() + "'. Use the optional chaining operator '?.' instead.");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // 4. Find the property on the (now unwrapped) type.
    const std::string& property_name = expr.name.lexeme;
    std::shared_ptr<Type> property_type = m_type_error; // Default to error

    // --- Dispatch based on the kind of the unwrapped type ---

    if (unwrapped_object_type->kind == TypeKind::DATA) {
        auto data_type = std::dynamic_pointer_cast<DataType>(unwrapped_object_type);
        auto field_it = data_type->fields.find(property_name);
        if (field_it == data_type->fields.end()) {
            error(expr.name, "Data block of type '" + data_type->name + "' has no field named '" + property_name + "'.");
        } else {
            property_type = field_it->second.type;
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::INSTANCE) {
        auto instance_type = std::dynamic_pointer_cast<InstanceType>(unwrapped_object_type);
        const ClassType::MemberInfo* prop_info = instance_type->class_type->findProperty(property_name);
        if (!prop_info) {
            error(expr.name, "Instance of class '" + instance_type->toString() + "' has no property named '" + property_name + "'.");
        } else {
            // Check for private access
            if (prop_info->access == AccessLevel::PRIVATE && (m_current_class == nullptr || m_current_class->name != instance_type->class_type->name)) {
                error(expr.name, "Property '" + property_name + "' is private and cannot be accessed from this context.");
            } else {
                property_type = prop_info->type;
            }
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::MODULE) {
        auto module_type = std::dynamic_pointer_cast<ModuleType>(unwrapped_object_type);
        auto member_it = module_type->exports.find(property_name);
        if (member_it == module_type->exports.end()) {
            error(expr.name, "Module '" + module_type->name + "' has no exported member named '" + property_name + "'.");
        } else {
            property_type = member_it->second;
        }
    }
    else if (object_type->kind == TypeKind::MODULE) {
        auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);
        auto member_it = module_type->exports.find(property_name);
        if (member_it != module_type->exports.end()) {
            property_type = member_it->second;
            // --- THE FIX ---
            if (module_type->is_native) {
                m_used_native_symbols.insert({module_type, property_name, property_type});
            }
        }
    }
    else if (object_type->kind == TypeKind::LIST) {
        auto list_type = std::dynamic_pointer_cast<ListType>(object_type);
        if (property_name == "push") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{list_type->element_type},
                m_type_nil
            );
        } else if (property_name == "remove_at") { // <-- ADD THIS
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_i64},
                list_type->element_type // Returns the element type
            );
        } else if (property_name == "remove") { // <-- ADD THIS
            property_type = std::make_shared<FunctionType>(
               std::vector<std::shared_ptr<Type>>{list_type->element_type},
               m_type_bool // Returns true or false
           );
        } else {
            error(expr.name, "Type 'list' has no property named '" + property_name + "'.");
        }
    }
    else if (object_type->kind == TypeKind::RECORD) {
        // <-- ADD THIS ENTIRE BLOCK
        if (property_name == "remove") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_string},
                m_type_bool // Returns true or false
            );
        } else if (property_name == "keys") {
            auto list_of_strings = std::make_shared<ListType>(m_type_string);
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{},
                list_of_strings // Returns list<string>
            );
        } else {
            error(expr.name, "Type 'record' has no property named '" + property_name + "'. Use subscript `[]` to access fields.");
        }
    }
    else if (object_type->kind == TypeKind::THREAD) {
        if (property_name == "join") {
            property_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_any);
        } else {
            error(expr.name, "Type 'Thread' has no property named '" + property_name + "'.");
        }
    }
    else if (object_type->kind == TypeKind::MUTEX) {
        if (property_name == "lock" || property_name == "unlock") {
            property_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_nil);
        } else {
            error(expr.name, "Type 'Mutex' has no property named '" + property_name + "'.");
        }
    }
    else {
        error(expr.op, "Type '" + object_type->toString() + "' has no properties that can be accessed.");
    }

    // --- 5. Determine the Final Result Type ---
    if (property_type->kind == TypeKind::ERROR) {
        // If the property lookup failed, the result is an error.
        pushAndSave(&expr, m_type_error);
    } else if (is_optional_chain || object_type->kind == TypeKind::OPTIONAL) {
        // If this was an optional chain OR if the original object was optional,
        // the result of the access is also optional.
        pushAndSave(&expr, std::make_shared<OptionalType>(property_type));
    } else {
        // Otherwise, it's a regular access on a non-optional type.
        pushAndSave(&expr, property_type);
    }

    return {};
}

    std::any TypeChecker::visit(const CallExpr& expr) {
        // --- Phase 1: Evaluate Callee and Arguments ---
        expr.callee->accept(*this);
        auto callee_type = popType();
        std::vector<std::shared_ptr<Type>> arg_types;
        for (const auto& arg_expr : expr.arguments) {
            arg_expr->accept(*this);
            arg_types.push_back(popType());
        }
        if (m_hadError) { pushAndSave(&expr, m_type_error); return {}; }

        // --- Phase 2: Special Case Dispatch for `spawn` ---
        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            if (var_expr->name.lexeme == "spawn") {
                check_spawn_call(expr, arg_types);
                // The result type of spawn is always Thread.
                pushAndSave(&expr, m_hadError ? m_type_error : m_type_thread);
                return {};
            }
        }

        // --- Phase 3: Main Dispatch based on Callee Type ---
        std::shared_ptr<Type> result_type = m_type_error;

        if (callee_type->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);
            check_function_call(expr, func_type, arg_types);
            if (!m_hadError) {
                result_type = func_type->return_type;
            }
        }
        else if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
            auto init_it = class_type->methods.find("init");

            if (init_it == class_type->methods.end()) {
                // No 'init' method found. This class can only be constructed with zero arguments.
                if (!arg_types.empty()) {
                    error(expr.paren, "Class '" + class_type->name + "' does not have a constructor that accepts arguments.");
                    // We can add a note pointing to the class definition.
                    // This requires getting the token from the ClassType, which is an enhancement for later.
                    // For now, the error message is very clear. TODO
                }
            } else {
                // An 'init' method exists, use it to validate the call.
                auto init_sig = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);
                check_function_call(expr, init_sig, arg_types);
            }

            if (!m_hadError) {
                result_type = std::make_shared<InstanceType>(class_type);
            }
        } else if (callee_type->kind == TypeKind::DATA) { // <-- ADD THIS BLOCK
            auto data_type = std::dynamic_pointer_cast<DataType>(callee_type);
            // Validate the call against the synthesized constructor signature.
            check_function_call(expr, data_type->constructor_type, arg_types);
            if (!m_hadError) {
                // The result of the call is the data type itself.
                result_type = data_type;
            }
        }
        else {
            error(expr.paren, "This expression is not callable. Can only call functions and classes.");
        }

        pushAndSave(&expr, result_type);
        return {};
    }

    std::any TypeChecker::visit(const LogicalExpr& expr) {
    // --- Case 1: Handle the Nil Coalescing Operator `??` ---
    if (expr.op.type == TokenType::QUESTION_QUESTION) {
        // 1a. Type check the left-hand side (the optional value).
        expr.left->accept(*this);
        auto lhs_type = popType();

        // 1b. Type check the right-hand side (the default value).
        expr.right->accept(*this);
        auto rhs_type = popType();

        // Bail out early on sub-expression errors.
        if (lhs_type->kind == TypeKind::ERROR || rhs_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 1c. The LHS must be an optional type.
        if (lhs_type->kind != TypeKind::OPTIONAL) {
            error(expr.op, "The left-hand side of the '??' operator must be an optional type (e.g., 'string?'), but got a non-optional type '" + lhs_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        auto unwrapped_lhs_type = std::dynamic_pointer_cast<OptionalType>(lhs_type)->wrapped_type;

        // 1d. The RHS (default value) must be compatible with the unwrapped type.
        if (!check_type_compatibility(unwrapped_lhs_type, rhs_type)) {
            error(expr.op, "Type mismatch in '??' operator. The default value of type '" + rhs_type->toString() +
                           "' is not compatible with the expected unwrapped type '" + unwrapped_lhs_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 1e. The result of the `??` expression is the non-optional, unwrapped type.
        pushAndSave(&expr, unwrapped_lhs_type);
        return {};
    }

    // --- Case 2: Handle Logical AND (`&&`) and OR (`||`) ---
    // (This logic is now the fallback case)
    expr.left->accept(*this);
    auto left_type = popType();
    expr.right->accept(*this);
    auto right_type = popType();

    if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // Rule: Both operands must be "truthy" (convertible to a boolean).
    // Our isTruthy() check is very permissive, which is fine.
    if (!isTruthy(left_type) || !isTruthy(right_type)) {
        error(expr.op, "Operands for a logical operator ('&&', '||') must be truthy types. "
                       "Got '" + left_type->toString() + "' and '" + right_type->toString() + "'.");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // The result of a logical '&&' or '||' expression is always a boolean.
    pushAndSave(&expr, m_type_bool);
    return {};
}

    std::any TypeChecker::visit(const SubscriptExpr& expr) {
        // 1. Type check the object being subscripted and the index.
        expr.object->accept(*this);
        auto collection_type = popType();
        expr.index->accept(*this);
        auto index_type = popType();

        // 2. Bail out early if a sub-expression had an error.
        if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        std::shared_ptr<Type> result_type = m_type_error; // Default to error

        // --- Case 1: Accessing a list element ---
        if (collection_type->kind == TypeKind::LIST) {
            auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);

            // Rule: List indices must be integers.
            if (!isInteger(index_type)) { // Use our isInteger helper for flexibility
                error(expr.bracket, "List index must be an integer, but got '" +
                                    index_type->toString() + "'.");
            } else {
                // Success! The result type is the list's element type.
                result_type = list_type->element_type;
            }
        }
            // --- Case 2: Accessing a record field ---
        else if (collection_type->kind == TypeKind::RECORD) {
            auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
            if (index_type->toString() != "string") {
                error(expr.bracket, "Record key must be a string, but got '" + index_type->toString() + "'.");
            } else {
                // --- REVISED LOGIC ---
                if (auto key_literal = std::dynamic_pointer_cast<const Literal>(expr.index)) {
                    // STATIC ACCESS: Key is a literal, so we check it at compile time.
                    const std::string& key_name = key_literal->token.lexeme;
                    auto field_it = record_type->fields.find(key_name);
                    if (field_it == record_type->fields.end()) {
                        error(key_literal->token, "Record of type '" + record_type->toString() + "' has no statically-known field named '" + key_name + "'.");
                        result_type = m_type_error;
                    } else {
                        result_type = field_it->second; // Success!
                    }
                } else {
                    // DYNAMIC ACCESS: Key is a variable. The compiler cannot check the key's value.
                    // The result of a dynamic access is always 'any' because we don't know which field will be accessed.
                    result_type = m_type_any;
                }
            }
        }
            // --- Case 3: Accessing a string character ---
        else if (collection_type->toString() == "string") {
            // Rule: String indices must be integers.
            if (!isInteger(index_type)) {
                error(expr.bracket, "String index must be an integer, but got '" +
                                    index_type->toString() + "'.");
            } else {
                // Success! The result of subscripting a string is another string.
                result_type = m_type_string;
            }
        }
        else {
            error(expr.bracket, "Object of type '" + collection_type->toString() + "' is not subscriptable.");
        }

        // Finally, push and save the single, definitive result type for this expression.
        pushAndSave(&expr, result_type);
        return {};
    }

    std::any TypeChecker::visit(const RecordExpr& expr) {
        std::map<std::string, std::shared_ptr<Type>> inferred_fields;

        // 1. Iterate through all the key-value pairs in the literal.
        for (size_t i = 0; i < expr.keys.size(); ++i) {
            const Token& key_token = expr.keys[i];
            const std::string& key_name = key_token.lexeme;
            const auto& value_expr = expr.values[i];

            // 2. Check for duplicate keys in the same literal.
            if (inferred_fields.count(key_name)) {
                error(key_token, "Duplicate field '" + key_name + "' in record literal.");
                // Continue checking the rest of the fields even if one is a duplicate.
            }

            // 3. Recursively type check the value expression to infer its type.
            value_expr->accept(*this);
            auto value_type = popType();

            // 4. Store the inferred field name and type.
            inferred_fields[key_name] = value_type;
        }

        // 5. Create a new internal RecordType from the inferred fields and push it.
        pushAndSave(&expr, std::make_shared<RecordType>(inferred_fields));

        return {};
    }

    std::any TypeChecker::visit(const TernaryExpr& expr) {
        // 1. Type check the condition.
        expr.condition->accept(*this);
        auto condition_type = popType();

        // 2. Type check the 'then' and 'else' branches.
        expr.thenBranch->accept(*this);
        auto then_type = popType();
        expr.elseBranch->accept(*this);
        auto else_type = popType();

        // 3. Prevent cascading errors.
        if (condition_type->kind == TypeKind::ERROR ||
            then_type->kind == TypeKind::ERROR ||
            else_type->kind == TypeKind::ERROR) {

            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 1: Condition must be a boolean ---
        if (!isTruthy(condition_type)) {
            error(Token(), "Ternary condition must be of type 'bool', but got '" +
                           condition_type->toString() + "'."); // Placeholder token
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 2: Then and Else branches must have the same type ---
        if (then_type->toString() != else_type->toString()) {
            error(Token(), "Type mismatch in ternary expression. The 'then' branch has type '" +
                           then_type->toString() + "', but the 'else' branch has type '" +
                           else_type->toString() + "'. Both branches must have the same type.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 3: The result of the expression is the type of the branches ---
        pushAndSave(&expr, then_type);

        return {};
    }

    std::any TypeChecker::visit(const ThisExpr& expr) {
        // --- RULE 1: Check if we are inside a class ---
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'this' outside of a class method.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 2: The type of 'this' is the instance type of the current class ---
        pushAndSave(&expr, std::make_shared<InstanceType>(m_current_class));
        return {};
    }

    std::any TypeChecker::visit(const SuperExpr& expr) {
    // 1. Rule: Must be inside a class.
    if (m_current_class == nullptr) {
        error(expr.keyword, "Cannot use 'super' outside of a class method.");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // 2. Rule: The class must have a superclass.
    if (m_current_class->superclass == nullptr) {
        error(expr.keyword, "Cannot use 'super' in a class with no superclass.");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // --- 3. THE FIX: Dispatch based on the kind of `super` usage ---
    if (!expr.method.has_value()) {
        // Case A: It's a constructor call: `super(...)`.
        // The method being referred to is implicitly "init".
        auto method_it = m_current_class->superclass->methods.find("init");
        if (method_it == m_current_class->superclass->methods.end()) {
            // This means the parent has no constructor, which is a valid case
            // for a default, no-argument super() call. We will let the
            // visit(CallExpr) handle the arity check. We just need to
            // provide a function type for a zero-arg constructor.
            auto default_ctor_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{}, m_type_nil
            );
            pushAndSave(&expr, default_ctor_type);
        } else {
            // The parent has an `init` method. The type of `super` is the type of that method.
            pushAndSave(&expr, method_it->second.type);
        }
    } else {
        // Case B: It's a regular method call: `super.method(...)`.
        // The `expr.method` optional is guaranteed to have a value here.
        const std::string& method_name = expr.method->lexeme;

        // Rule: The method must exist on the superclass.
        const ClassType::MemberInfo* method_info = m_current_class->superclass->findProperty(method_name);

        if (method_info == nullptr || method_info->type->kind != TypeKind::FUNCTION) {
            error(*expr.method, "The superclass '" + m_current_class->superclass->name +
                               "' has no method named '" + method_name + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // Check for private access on the superclass method
        if (method_info->access == AccessLevel::PRIVATE) {
            error(*expr.method, "Superclass method '" + method_name + "' is private and cannot be accessed.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // Success! The type of this expression is the method's FunctionType.
        pushAndSave(&expr, method_info->type);
    }

    return {};
}

    void TypeChecker::visit(std::shared_ptr<const ContractStmt> stmt) {
        // This is called during Pass 3.
        // Contracts only contain declarations, not executable code or initializers,
        // so there is nothing to check in this pass. All validation happens
        // when a class signs the contract in `defineClassHeader`.
    }

    void TypeChecker::defineContractHeader(const ContractStmt& stmt) {
    // 1. Get the placeholder ContractType that was created in Pass 1.
    auto symbol = m_symbols.resolve(stmt.name.lexeme);
    auto contract_type = std::dynamic_pointer_cast<ContractType>(symbol->type);

    // 2. If the contract is exported, add it to the module's public API.
    if (stmt.is_exported) {
        m_module_type->exports[stmt.name.lexeme] = contract_type;
    }

    // 3. Resolve and define all members required by the contract.
    for (const auto& member : stmt.members) {
        if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
            const auto& field_decl = field_member->declaration;
            auto field_type = resolveType(field_decl->typeAnnotation);

            if (contract_type->fields.count(field_decl->name.lexeme) || contract_type->methods.count(field_decl->name.lexeme)) {
                error(field_decl->name, "Duplicate member '" + field_decl->name.lexeme + "' in contract.");
                continue;
            }
            // All contract members are implicitly public.
            contract_type->fields[field_decl->name.lexeme] = {
                field_type,
                field_decl->name, // <-- Store the token
                field_decl->is_const
            };

        } else if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
            const auto& method_decl = method_member->declaration;

            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& p : method_decl->params) {
                param_types.push_back(resolveType(p.type));
            }
            std::shared_ptr<Type> return_type = m_type_nil;
            if (method_decl->returnType) {
                return_type = resolveType(method_decl->returnType);
            }
            auto method_type = std::make_shared<FunctionType>(param_types, return_type);

            if (contract_type->methods.count(method_decl->name.lexeme) || contract_type->fields.count(method_decl->name.lexeme)) {
                error(method_decl->name, "Duplicate member '" + method_decl->name.lexeme + "' in contract.");
                continue;
            }
            contract_type->methods[method_decl->name.lexeme] = {
                method_type,
                method_decl->name, // <-- Store the token
                false
            };
        }
    }
}

void TypeChecker::resolveAttach(const AttachStmt& stmt) {
    std::string module_path = stmt.modulePath.lexeme;
    std::shared_ptr<ModuleType> module_type = m_driver.resolveModule(module_path, stmt.modulePath);

    if (!module_type) {
        // Driver already reported the error (e.g., file not found).
        return;
    }

    m_module_resolutions[&stmt] = module_type;

    // --- Case 1: Selective import (e.g., `attach connect from websocket`) ---
    if (!stmt.names.empty()) {
        for (const auto& name_token : stmt.names) {
            const std::string& name_str = name_token.lexeme;
            auto export_it = module_type->exports.find(name_str);

            if (export_it == module_type->exports.end()) {
                error(name_token, "Module '" + module_type->name + "' has no exported member named '" + name_str + "'.");
            } else {
                // Declare the new symbol, PASSING the origin module.
                if (auto conflicting_symbol = m_symbols.declare(name_token, export_it->second, true, module_type)) {
                    error(name_token, "re-declaration of symbol '" + name_str + "'.");
                    note(conflicting_symbol->declaration_token, "previous declaration was here.");
                }
            }
        }
    }
    // --- Case 2: Whole-module import (e.g., `attach fs`) ---
    else {
        std::string symbol_name;
        Token name_token;

        if (stmt.alias) {
            symbol_name = stmt.alias->lexeme;
            name_token = *stmt.alias;
        } else {
            // For logical names like "fs", the symbol is "fs". For paths, it's the basename.
            symbol_name = CompilerDriver::get_base_name(stmt.modulePath.lexeme);
            name_token = Token(TokenType::IDENTIFIER, symbol_name, stmt.modulePath.line, 0);
        }

        // Declare the module symbol. It has no origin module (it *is* the module).
        if (auto conflicting_symbol = m_symbols.declare(name_token, module_type, true)) {
             error(name_token, "re-declaration of symbol '" + symbol_name + "'.");
             note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }
    }
}

    void TypeChecker::visit(std::shared_ptr<const BreakStmt> stmt) {
        if (m_loop_depth == 0) {
            error(stmt->keyword, "Cannot use 'break' outside of a loop.");
        }
    }
} // namespace angara