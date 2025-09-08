//
// Created by cv2 on 8/31/25.
//

#include "TypeChecker.h"
#include <stdexcept>

namespace angara {

// --- Constructor and Main Entry Point ---

    const SymbolTable& TypeChecker::getSymbolTable() const {
        return m_symbols;
    }

    std::shared_ptr<ModuleType> TypeChecker::getModuleType() const {
        return m_module_type;
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
        m_type_void = std::make_shared<PrimitiveType>("void");
        m_type_nil = std::make_shared<PrimitiveType>("nil");
        m_type_any = std::make_shared<PrimitiveType>("any");
        m_type_error = std::make_shared<PrimitiveType>("<error>");
        m_type_thread = std::make_shared<ThreadType>();
        m_type_mutex = std::make_shared<MutexType>();
        m_module_type = std::make_shared<ModuleType>(module_name);

        auto print_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{}, // No fixed parameters
                m_type_void,
                true // <-- Set the variadic flag to true
        );
        // Note: A better model for variadics is a special flag in FunctionType.
        // TODO: refine.
        // For now, this is a good approximation.
        m_symbols.declare(Token(TokenType::IDENTIFIER, "print", 0, 0), print_type, true);
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
            m_type_void
        );

        // Define the signature for `spawn` itself: function(function() -> void) -> Thread
        auto spawn_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{worker_fn_type},
            std::make_shared<ThreadType>() // Returns our new Thread type
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "spawn", 0, 0), spawn_type, true);

        auto mutex_constructor_type = std::make_shared<FunctionType>(
            std::vector<std::shared_ptr<Type>>{},
            m_type_mutex
        );
        m_symbols.declare(Token(TokenType::IDENTIFIER, "Mutex", 0, 0), mutex_constructor_type, true);

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

    // --- NEW HELPER ---
    bool TypeChecker::isUnsignedInteger(const std::shared_ptr<Type>& type) {
        if (type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "u8" || name == "u16" || name == "u32" || name == "u64";
    }

// --- NEW HELPER ---
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

        // --- THIS IS THE FIX ---
        // 2. If the trait was marked with 'export', add it to the module's public API.
        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = trait_type;
        }
        // --- END OF FIX ---

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
            std::shared_ptr<Type> return_type = m_type_void;
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
        // This helper only resolves and declares the function signature for Pass 2.

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



        std::shared_ptr<Type> return_type = m_type_void;
        if (stmt.returnType) {
            return_type = resolveType(stmt.returnType);
        }

        auto function_type = std::make_shared<FunctionType>(param_types, return_type);

        // 1. Declare the function in the current symbol table so it can be used.
        if (!m_symbols.declare(stmt.name, function_type, true)) {
            error(stmt.name, "A symbol with this name already exists in this scope.");
            // We can often continue even if the name is a duplicate.
        }

        // --- THIS IS THE FIX ---
        // 2. If the function was marked with 'export', add it to the module's public API.
        if (stmt.is_exported) {
            // We only allow top-level functions to be exported, not methods inside classes.
            // The parser should enforce this, but we can double-check here.
            if (m_current_class != nullptr) {
                error(stmt.name, "Cannot use 'export' on a method inside a class. Make the class public instead.");
            } else {
                m_module_type->exports[stmt.name.lexeme] = function_type;
            }
        }
        // --- END OF FIX ---

        // --- THIS IS THE FIX ---
        // If the function is EXPORTED or if its NAME IS "main", add it to the public API.
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

        // --- THIS IS THE FIX ---
        // 2. If the class was marked with 'export', add it to the module's public API.
        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = class_type;
        }
        // --- END OF FIX ---

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

                // --- NEW: INHERITANCE CYCLE DETECTION ---
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
                class_type->fields[field_decl->name.lexeme] = {field_type, field_member->access, field_decl->is_const};

            } else if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                const auto& method_decl = method_member->declaration;

                // Resolve the method's full signature into a FunctionType.
                std::vector<std::shared_ptr<Type>> param_types;
                for (const auto& p : method_decl->params) {
                    param_types.push_back(resolveType(p.type));
                }
                std::shared_ptr<Type> return_type = m_type_void;
                if (method_decl->returnType) {
                    return_type = resolveType(method_decl->returnType);
                }
                auto method_type = std::make_shared<FunctionType>(param_types, return_type);

                if (class_type->methods.count(method_decl->name.lexeme) || class_type->fields.count(method_decl->name.lexeme)) {
                    error(method_decl->name, "A member with this name already exists in the class.");
                }
                class_type->methods[method_decl->name.lexeme] = {method_type, method_member->access, false}; // Methods aren't const
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

    // --- PASS 1: Declare all top-level type names (unchanged) ---
    for (const auto& stmt : statements) {
        if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
            auto class_type = std::make_shared<ClassType>(class_stmt->name.lexeme);
            if (!m_symbols.declare(class_stmt->name, class_type, true)) { /* error */ }
        } else if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
            auto trait_type = std::make_shared<TraitType>(trait_stmt->name.lexeme);
            if (!m_symbols.declare(trait_stmt->name, trait_type, true)) { /* error */ }
        }
    }
    if (m_hadError) return false;


    // --- PASS 2: Define all headers and signatures ---

    // --- STAGE 2a: Define TRAIT headers FIRST ---
    for (const auto& stmt : statements) {
        if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
            defineTraitHeader(*trait_stmt);
        }
    }
    if (m_hadError) return false;

    // --- STAGE 2b: Define CLASS headers NEXT ---
    for (const auto& stmt : statements) {
        if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
            defineClassHeader(*class_stmt);
        }
    }
    if (m_hadError) return false;

    // --- STAGE 2c: Define global FUNCTION headers LAST ---
    for (const auto& stmt : statements) {
        if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
            // We need to ensure we don't process methods, which are also FuncStmts
            // but are part of a class. The simplest way is to check if it's already in the symbol table.
            // A better way would be to check its parent in the AST if we had that link.

            // TODO - refine
            if (m_symbols.resolve(func_stmt->name.lexeme) == nullptr) {
                 defineFunctionHeader(*func_stmt);
            }
        }
    }
    if (m_hadError) return false;


    // --- PASS 3: Check all implementation code (unchanged) ---
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

    std::shared_ptr<Type> TypeChecker::resolveType(const std::shared_ptr<ASTType>& ast_type) {
        if (!ast_type) {
            return m_type_error;
        }

        // --- Case 1: A simple type name (e.g., 'i64', 'Player') ---
        if (auto simple = std::dynamic_pointer_cast<const SimpleType>(ast_type)) {
            const std::string& name = simple->name.lexeme;

            // Check for built-in primitive types first
            if (name == "i8") return m_type_i8;
            if (name == "i16") return m_type_i16;
            if (name == "i32") return m_type_i32;
            if (name == "i64" || name == "int") return m_type_i64;
            if (name == "u8") return m_type_u8;
            if (name == "u16") return m_type_u16;
            if (name == "u32") return m_type_u32;
            if (name == "u64" || name == "uint") return m_type_u64;
            if (name == "f32") return m_type_f32;
            if (name == "f64" || name == "float") return m_type_f64;
            if (name == "bool") return m_type_bool;
            if (name == "string") return m_type_string;
            if (name == "void") return m_type_void;
            if (name == "any") return m_type_any;
            if (name == "Thread") return m_type_thread; // <-- ADD THIS

            // If it's not a primitive, it must be a user-defined type (class or trait).
            // We look it up in the symbol table.
            auto symbol = m_symbols.resolve(name);
            if (symbol) {
                if (symbol->type->kind == TypeKind::CLASS) {
                    // When a class name is used as a type annotation, it refers to
                    // an INSTANCE of that class.
                    return std::make_shared<InstanceType>(std::dynamic_pointer_cast<ClassType>(symbol->type));
                }
                if (symbol->type->kind == TypeKind::TRAIT) {
                    return symbol->type; // Traits can be used as types directly.
                }
            }

            // If it's not a primitive and not a declared class/trait, it's an error.
            error(simple->name, "Unknown type name '" + name + "'.");
            return m_type_error;
        }

        // --- Case 2: A generic type (e.g., 'list<T>', 'record<K:V>') ---
        if (auto generic = std::dynamic_pointer_cast<const GenericType>(ast_type)) {
            const std::string& base_name = generic->name.lexeme;

            if (base_name == "list") {
                if (generic->arguments.size() != 1) {
                    error(generic->name, "The 'list' type requires exactly one generic argument (e.g., list<i64>).");
                    return m_type_error;
                }
                auto element_type = resolveType(generic->arguments[0]);
                if (element_type->kind == TypeKind::ERROR) return m_type_error;
                return std::make_shared<ListType>(element_type);
            }

            error(generic->name, "Unknown generic type '" + base_name + "'.");
            return m_type_error;
        }

        // --- Handle the FunctionTypeExpr AST node ---
        if (auto func_type_expr = std::dynamic_pointer_cast<const FunctionTypeExpr>(ast_type)) {
            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& p_ast_type : func_type_expr->param_types) {
                param_types.push_back(resolveType(p_ast_type));
            }

            auto return_type = resolveType(func_type_expr->return_type);

            // Return our internal FunctionType representation.
            return std::make_shared<FunctionType>(param_types, return_type);
        }

        // --- Handle the RecordTypeExpr AST node ---
        if (auto record_type_expr = std::dynamic_pointer_cast<const RecordTypeExpr>(ast_type)) {
            std::map<std::string, std::shared_ptr<Type>> fields;
            for (const auto& field_def : record_type_expr->fields) {
                const std::string& field_name = field_def.name.lexeme;
                if (fields.count(field_name)) {
                    error(field_def.name, "Duplicate field name '" + field_name + "' in record type definition.");
                    // Continue checking other fields even if one is a duplicate.
                }
                fields[field_name] = resolveType(field_def.type);
            }
            return std::make_shared<RecordType>(fields);
        }

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


    // --- Logic for type inference and error checking ---
    if (!declared_type && initializer_type) {
        // Infer type from initializer
        declared_type = initializer_type;
    } else if (declared_type && !initializer_type) {
        // This is fine, e.g., `let x as i64;`
    } else if (!declared_type && !initializer_type) {
        error(stmt->name, "Cannot declare a variable without a type annotation or an initializer.");
        declared_type = m_type_error;
    } else if (declared_type && initializer_type) {
        // Both exist. This is where we compare them.

        bool types_match = (declared_type->toString() == initializer_type->toString());

        // --- THIS IS THE FIX ---
        // Special Rule: An empty list literal (inferred as `list<any>`) can be
        // assigned to a variable of any specific list type.
        if (!types_match && initializer_type->toString() == "list<any>" && declared_type->kind == TypeKind::LIST) {
            // Check if the initializer was actually an empty list expression.
            if (auto list_expr = std::dynamic_pointer_cast<const ListExpr>(stmt->initializer)) {
                if (list_expr->elements.empty()) {
                    types_match = true;
                }
            }
        }
        // --- END OF FIX ---

        // (You can also add the numeric literal conversion rules here if they aren't already present)
        if (!types_match && isInteger(declared_type) && initializer_type->toString() == "i64") {
            types_match = true;
        }

        if (!types_match) {
            error(stmt->name, "Type mismatch. Variable is annotated as '" +
                declared_type->toString() + "' but is initialized with a value of type '" +
                initializer_type->toString() + "'.");
            declared_type = m_type_error;
        }
    }

    // Store the final canonical type for this variable declaration
    m_variable_types[stmt.get()] = declared_type;


    // Declare the symbol in the current scope
    if (!m_symbols.declare(stmt->name, declared_type, stmt->is_const)) {
        error(stmt->name, "A variable with this name already exists in this scope.");
    }

        // --- THIS IS THE FIX ---
        // If the variable is exported, add it to the module's public API.
        // We only do this for global variables (scope depth 0).
        if (stmt->is_exported) {
            if (m_symbols.getScopeDepth() > 0) { // Assumes you add a getScopeDepth() helper
                error(stmt->name, "'export' can only be used on top-level declarations.");
            } else {
                m_module_type->exports[stmt->name.lexeme] = declared_type;
            }
        }
        // --- END OF FIX ---

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

    std::any TypeChecker::visit(const VarExpr& expr) {
        auto symbol = m_symbols.resolve(expr.name.lexeme);
        if (!symbol) {
            error(expr.name, "Undefined variable '" + expr.name.lexeme + "'.");
            pushAndSave(&expr, m_type_error);
        } else {
            // --- THIS IS THE FIX ---
            // Save the result of the resolution for the transpiler.
            m_variable_resolutions[&expr] = symbol;
            // --- END OF FIX ---
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
        pushAndSave(&expr, result_type); // <-- THE FIX
        return {};
    }

    std::any TypeChecker::visit(const Binary& expr) {
        // 1. Visit operands.
        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        // 2. Default to an error type. We will only change this if a rule passes.
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
            case TokenType::PERCENT: // Modulo is now part of this group
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    // If either operand is a float, the result is a float (f64).
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        // Otherwise, both are integers, the result is an integer (i64).
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
                    result_type = m_type_bool; // All comparisons result in a bool.
                } else {
                    error(expr.op, "Operands for comparison must be numbers.");
                }
                break;

            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL:
                if (left_type->toString() == right_type->toString()) {
                    result_type = m_type_bool;
                } else {
                    // Allow comparing any number to any other number
                    if (isNumeric(left_type) && isNumeric(right_type)) {
                        result_type = m_type_bool;
                    } else {
                        error(expr.op, "Cannot compare two different types: '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.");
                    }
                }
                break;

            default:
                // This case should not be reachable if the parser is correct.
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
        // Case 1: The list is empty.
        if (expr.elements.empty()) {
            // An empty list `[]` has no intrinsic element type. We cannot infer it
            // without more context (e.g., `let x as list<i64> = []`).
            // We assign it a special `list<any>` type. The logic in variable
            // declarations and assignments is responsible for handling this special case.
            auto empty_list_type = std::make_shared<ListType>(m_type_any);
            pushAndSave(&expr, empty_list_type);
            return {};
        }

        // Case 2: The list has elements. We must infer the type.

        // 1. Determine the type of the *first* element. This becomes our candidate type.
        expr.elements[0]->accept(*this);
        auto list_element_type = popType();

        if (list_element_type->kind == TypeKind::ERROR) {
            // If the first element has an error, the whole list is bad.
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 2. Iterate through the rest of the elements (if any).
        for (size_t i = 1; i < expr.elements.size(); ++i) {
            expr.elements[i]->accept(*this);
            auto current_element_type = popType();

            // Rule: All elements must be of the same type.
            if (current_element_type->toString() != list_element_type->toString()) {
                error(expr.bracket, "List elements must all be of the same type. This list was inferred to be of type 'list<" +
                                    list_element_type->toString() + ">', but an element of type '" +
                                    current_element_type->toString() + "' was found.");
                pushAndSave(&expr, m_type_error);
                return {};
            }
        }

        // 3. If we get here, all elements were the same type. The final, inferred
        //    type of this expression is a ListType containing the element type.
        auto final_list_type = std::make_shared<ListType>(list_element_type);
        pushAndSave(&expr, final_list_type);

        return {};
    }

    void TypeChecker::visit(std::shared_ptr<const IfStmt> stmt) {
        // 1. Type check the condition expression.
        stmt->condition->accept(*this);
        auto condition_type = popType();

        // 2. Enforce the rule: the condition must be a 'bool'.
        if (!isTruthy(condition_type)) {
            error(stmt->keyword, "If statement condition must be of type 'bool', but got '" +
                                 condition_type->toString() + "'.");
        }

        // 3. Type check the 'then' branch.
        stmt->thenBranch->accept(*this, stmt->thenBranch);

        // 4. Type check the 'else' branch, if it exists.
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
        stmt->body->accept(*this, stmt->body);
    }

    void TypeChecker::visit(std::shared_ptr<const ForStmt> stmt) {
        // 1. A C-style for loop introduces a new scope for its initializer.
        m_symbols.enterScope();

        // 2. Type check the initializer, if it exists.
        if (stmt->initializer) {
            stmt->initializer->accept(*this, stmt->initializer);
        }

        // --- THIS IS THE CRITICAL FIX ---
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
        // --- END OF CRITICAL FIX ---

        // 5. Type check the loop body.
        stmt->body->accept(*this, stmt->body);

        // 6. Exit the scope, destroying the initializer variable.
        m_symbols.exitScope();
    }

    void TypeChecker::visit(std::shared_ptr<const ForInStmt> stmt) {
        // 1. The for..in loop also introduces a new scope.
        m_symbols.enterScope();

        // 2. Type check the collection expression.
        stmt->collection->accept(*this);
        auto collection_type = popType();

        std::shared_ptr<Type> item_type = m_type_error; // The inferred type of 'item'.

        // 3. Enforce the iteration rules. The collection must be iterable (a list or string for now).
        if (collection_type->kind == TypeKind::LIST) {
            // If the collection is list<T>, then the item's type is T.
            auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);
            item_type = list_type->element_type;
        } else if (collection_type->toString() == "string") {
            // If the collection is a string, then the item is also a string (a single character).
            item_type = m_type_string;
        } else {
            error(stmt->name, "The 'for..in' loop can only iterate over a list or a string, but got '" +
                              collection_type->toString() + "'.");
        }

        // 4. Declare the loop variable ('item') in the new scope with its inferred type.
        if (!m_symbols.declare(stmt->name, item_type, false)) {
            // This should be unreachable if the parser prevents duplicate declarations.
            error(stmt->name, "A variable with this name already exists in this scope.");
        }

        // 5. Now that 'item' is in scope, we can type check the loop body.
        stmt->body->accept(*this, stmt->body);

        // 6. Exit the scope.
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

            // Check if the returned value's type matches the function's signature.
            if (actual_return_type->toString() != expected_return_type->toString()) {
                error(stmt->keyword, "Type mismatch. This function is declared to return '" +
                                     expected_return_type->toString() + "', but is returning a value of type '" +
                                     actual_return_type->toString() + "'.");
            }
        } else {
            // No value is returned ('return;'). This is only valid if the function
            // is supposed to return 'nil'.
            if (expected_return_type->toString() != "void") {
                error(stmt->keyword, "This function must return a value of type '" +
                                     expected_return_type->toString() + "'.");
            }
        }
    }



    void TypeChecker::visit(std::shared_ptr<const AttachStmt> stmt) {
        std::string module_path = stmt->modulePath.lexeme;
        std::shared_ptr<ModuleType> module_type = m_driver.resolveModule(module_path, stmt->modulePath);
        if (!module_type) return;

        // --- THIS IS THE FIX ---
        // Instead of declaring one variable for the module, we merge its exports
        // into the current scope.
        for (const auto& [name, type] : module_type->exports) {
            // We need to create a dummy token for the declaration.
            Token symbol_token(TokenType::IDENTIFIER, name, stmt->modulePath.line, 0);

            // We need to decide if these are const. Let's assume they are.
            if (!m_symbols.declare(symbol_token, type, true)) {
                error(symbol_token, "A symbol named '" + name + "' from module '" + module_type->name +
                                    "' conflicts with an existing symbol in this scope.");
            }
        }

        m_module_resolutions[stmt.get()] = module_type;
    }

    void TypeChecker::visit(std::shared_ptr<const ThrowStmt> stmt) {
        // 1. Type check the expression whose value is being thrown.
        stmt->expression->accept(*this);

        // 2. The value of the expression is used, but its type doesn't affect
        //    the flow of the type checker, so we pop it.
        popType();
    }

    void TypeChecker::visit(std::shared_ptr<const TryStmt> stmt) {
        // 1. Type check the 'try' block. Any errors inside it will be reported.
        stmt->tryBlock->accept(*this, stmt->tryBlock);

        // 2. Now, handle the 'catch' block. It introduces a new scope.
        m_symbols.enterScope();

        // 3. Declare the exception variable (e.g., 'e') in this new scope.
        //    We assign it the special 'any' type. This is the safest and most
        //    flexible option, as we can't know at compile time what type of
        //    value might be thrown at runtime.
        if (!m_symbols.declare(stmt->catchName, m_type_any, false)) {
            // This should be unreachable if the parser works correctly.
            error(stmt->catchName, "A variable with this name already exists in this scope.");
        }

        // 4. With the exception variable in scope, we can now type check the 'catch' block.
        stmt->catchBlock->accept(*this, stmt->catchBlock);

        // 5. Exit the scope for the catch block.
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

        // 2. Determine the type of the target being assigned to (LHS).
        expr.target->accept(*this);
        auto lhs_type = popType();

        // 3. If either sub-expression had an error, stop immediately.
        if (rhs_type->kind == TypeKind::ERROR || lhs_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

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
            }
        }
        else if (auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            // Subscript assignments (`list[i] = ...`) are always considered mutable for now.
            // No const check is needed here.
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

    std::any TypeChecker::visit(const CallExpr& expr) {
        // 1. Type check the callee to see what is being called.
        expr.callee->accept(*this);
        auto callee_type = popType();

        // 2. Type check all the arguments that are being passed.
        std::vector<std::shared_ptr<Type>> arg_types;
        for (const auto& arg_expr : expr.arguments) {
            arg_expr->accept(*this);
            arg_types.push_back(popType());
        }

        // Stop immediately if the callee or any argument had a type error.
        if (callee_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }
        for (const auto& arg_type : arg_types) {
            if (arg_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error);
                return {};
            }
        }

        std::shared_ptr<Type> result_type = m_type_error; // Default to error

        // --- Case 1: Calling a function ---

        if (callee_type->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);

            // --- THIS IS THE REFINED LOGIC ---
            size_t num_fixed_params = func_type->param_types.size();
            if (func_type->is_variadic) {
                // For a variadic function, the number of arguments must be
                // AT LEAST the number of fixed parameters.
                if (arg_types.size() < num_fixed_params) {
                    error(expr.paren, "Incorrect number of arguments. Function expects at least " +
                                      std::to_string(num_fixed_params) + ", but got " +
                                      std::to_string(arg_types.size()) + ".");
                    result_type = m_type_error;
                }
            } else {
                // For non-variadic functions, the number must match exactly.
                if (arg_types.size() != num_fixed_params) {
                    error(expr.paren, "Incorrect number of arguments. Function expects " +
                                      std::to_string(num_fixed_params) + ", but got " +
                                      std::to_string(arg_types.size()) + ".");
                    result_type = m_type_error;
                }
            }
            // --- END OF REFINED LOGIC ---

            // Rule: Check the type of each *fixed* argument.
            if (result_type->kind != TypeKind::ERROR) {
                for (size_t i = 0; i < num_fixed_params; ++i) {
                    if (arg_types[i]->toString() != func_type->param_types[i]->toString()) {
                        // ... error reporting ...
                        result_type = m_type_error;
                        break;
                    }
                }
            }
            // Note: The types of the variadic arguments are not checked here.
            // They are effectively treated as 'any'.

            if (result_type->kind != TypeKind::ERROR) {
                result_type = func_type->return_type;
            }
        }
            // --- Case 2: Calling a class (constructing an instance) ---
        else if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);

            // Find the constructor ('init' method).
            auto init_it = class_type->methods.find("init");
            if (init_it == class_type->methods.end()) {
                // No explicit 'init' method, so we expect zero arguments for the default constructor.
                if (!arg_types.empty()) {
                    error(expr.paren, "Class '" + class_type->name + "' has no 'init' method and cannot be called with arguments.");
                    result_type = m_type_error;
                }
            } else {
                // An 'init' method exists. Validate the call against its signature.
                auto init_sig = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);

                if (arg_types.size() != init_sig->param_types.size()) {
                    error(expr.paren, "Incorrect number of arguments for constructor of '" + class_type->name +
                                      "'. Expected " + std::to_string(init_sig->param_types.size()) +
                                      ", but got " + std::to_string(arg_types.size()) + ".");
                    result_type = m_type_error;
                } else {
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        if (arg_types[i]->toString() != init_sig->param_types[i]->toString()) {
                            error(expr.paren, "Type mismatch for constructor argument " + std::to_string(i + 1) +
                                              ". Expected '" + init_sig->param_types[i]->toString() +
                                              "', but got '" + arg_types[i]->toString() + "'.");
                            result_type = m_type_error;
                            break;
                        }
                    }
                }
            }

            // If no errors, the result of constructing a class is an instance of that class.
            if (result_type->kind != TypeKind::ERROR) {
                result_type = std::make_shared<InstanceType>(class_type);
            }

        } else {
            // The callee is not a function or a class.
            error(expr.paren, "This expression is not callable. Can only call functions and classes.");
            result_type = m_type_error;
        }

        pushAndSave(&expr, result_type);
        return {};
    }

    std::any TypeChecker::visit(const GetExpr& expr) {
    // 1. Type check the object on the left of the dot.
    expr.object->accept(*this);
    auto object_type = popType();

    if (object_type->kind == TypeKind::ERROR) {
        pushAndSave(&expr, m_type_error);
        return {};
    }

    const std::string& property_name = expr.name.lexeme;
    std::shared_ptr<Type> result_type = m_type_error; // Default to error

    // --- A single, clean if/else if chain for all property access ---

    if (object_type->kind == TypeKind::INSTANCE) {
        auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
        const ClassType::MemberInfo* prop_info = instance_type->class_type->findProperty(property_name);

        if (!prop_info) {
            error(expr.name, "Instance of class '" + instance_type->toString() + "' has no property named '" + property_name + "'.");
        } else {
            if (prop_info->access == AccessLevel::PRIVATE && (m_current_class == nullptr || m_current_class->name != instance_type->class_type->name)) {
                error(expr.name, "Property '" + property_name + "' is private and cannot be accessed from this context.");
            } else {
                result_type = prop_info->type;
            }
        }
    }
    // --- THIS IS THE FINAL FIX ---
    else if (object_type->kind == TypeKind::MODULE) {
        auto module_type = std::dynamic_pointer_cast<ModuleType>(object_type);

        // Look for the property in the module's public API (its exports).
        auto member_it = module_type->exports.find(property_name);

        if (member_it == module_type->exports.end()) {
            error(expr.name, "Module '" + module_type->name + "' has no exported member named '" + property_name + "'.");
        } else {
            // Success! The result is the type of the exported symbol.
            result_type = member_it->second;
        }
    }
    // --- END OF FINAL FIX ---
    else if (object_type->kind == TypeKind::LIST) {
        if (property_name == "push") {
            auto list_type = std::dynamic_pointer_cast<ListType>(object_type);
            result_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{list_type->element_type},
                m_type_void
            );
        } else {
            error(expr.name, "Type 'list' has no property named '" + property_name + "'.");
        }
    }
    else if (object_type->kind == TypeKind::THREAD) {
        if (property_name == "join") {
            result_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_any);
        } else {
            error(expr.name, "Type 'Thread' has no property named '" + property_name + "'.");
        }
    }
    else if (object_type->kind == TypeKind::MUTEX) {
        if (property_name == "lock" || property_name == "unlock") {
            result_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_void);
        } else {
            error(expr.name, "Type 'Mutex' has no property named '" + property_name + "'.");
        }
    }
    else {
        // Update the error message to be fully comprehensive.
        error(expr.name, "Only instances, modules, lists, threads, or mutexes have properties. Cannot access property on type '" + object_type->toString() + "'.");
    }

    pushAndSave(&expr, result_type);
    return {};
}

    std::any TypeChecker::visit(const LogicalExpr& expr) {
        // 1. Type check the left-hand side.
        expr.left->accept(*this);
        auto left_type = popType();

        // 2. Type check the right-hand side.
        expr.right->accept(*this);
        auto right_type = popType();

        std::shared_ptr<Type> result_type = m_type_error;

        // 3. Prevent cascading errors.
        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- THE CORE RULE ---
        // 4. Enforce that both operands must be of type 'bool'.
        if (!isTruthy(left_type) || !isTruthy(right_type)) {
            error(expr.op, "Operands for a logical operator ('&&', '||') must be a boolean or a number. "
                           "Got '" + left_type->toString() + "' and '" + right_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 5. If the types are correct, the result of a logical expression is always a 'bool'.
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

            // Rule: Record keys must be strings.
            if (index_type->toString() != "string") {
                error(expr.bracket, "Record key must be a string, but got '" +
                                    index_type->toString() + "'.");
            } else {
                // Rule: The key must be a string LITERAL for compile-time checking.
                if (auto key_literal = std::dynamic_pointer_cast<const Literal>(expr.index)) {
                    const std::string& key_name = key_literal->token.lexeme;

                    auto field_it = record_type->fields.find(key_name);
                    if (field_it == record_type->fields.end()) {
                        error(key_literal->token, "Record of type '" + record_type->toString() +
                                                  "' has no field named '" + key_name + "'.");
                    } else {
                        // Success! The result type is the field's type.
                        result_type = field_it->second;
                    }
                } else {
                    error(expr.bracket, "Record fields can only be accessed with a string literal key for static type checking.");
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
        // --- RULE 1: Must be inside a class ---
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'super' outside of a class method.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 2: The class must have a superclass ---
        if (m_current_class->superclass == nullptr) {
            error(expr.keyword, "Cannot use 'super' in a class with no superclass.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 3: The method must exist on the superclass ---
        const std::string& method_name = expr.method.lexeme;
        auto method_it = m_current_class->superclass->methods.find(method_name);

        if (method_it == m_current_class->superclass->methods.end()) {
            error(expr.method, "The superclass '" + m_current_class->superclass->name +
                               "' has no method named '" + method_name + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // Check for private access on the superclass method
        if (method_it->second.access == AccessLevel::PRIVATE) {
            error(expr.method, "Superclass method '" + method_name + "' is private and cannot be accessed.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 4: Success! The type is the method's FunctionType ---
        pushAndSave(&expr, method_it->second.type);
        return {};
    }
} // namespace angara