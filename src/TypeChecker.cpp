//
// Created by cv2 on 8/31/25.
//

#include "TypeChecker.h"
#include <stdexcept>

namespace angara {

// --- Constructor and Main Entry Point ---

    TypeChecker::TypeChecker(ErrorHandler& errorHandler) : m_errorHandler(errorHandler) {
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
    }

    void TypeChecker::defineTraitHeader(const TraitStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto trait_type = std::dynamic_pointer_cast<TraitType>(symbol->type);

        m_is_in_trait = true; // Set context for visit(FuncStmt)
        for (const auto& method_stmt : stmt.methods) {
            visit(method_stmt); // This will just define the signature, not the body
            auto method_symbol = m_symbols.resolve(method_stmt->name.lexeme);
            if (method_symbol) {
                trait_type->methods[method_symbol->name] = std::dynamic_pointer_cast<FunctionType>(method_symbol->type);
            }
        }
        m_is_in_trait = false; // Unset context
    }


    void TypeChecker::defineFunctionHeader(const FuncStmt& stmt) {
        // This helper only resolves and declares the function signature.
        // It is used for both global functions and methods in Pass 2.
        std::vector<std::shared_ptr<Type>> param_types;
        for (const auto& p : stmt.params) {
            param_types.push_back(resolveType(p.type));
        }
        std::shared_ptr<Type> return_type = m_type_void;
        if (stmt.returnType) {
            return_type = resolveType(stmt.returnType);
        }
        auto function_type = std::make_shared<FunctionType>(param_types, return_type);

        if (!m_symbols.declare(stmt.name, function_type, true)) {
            error(stmt.name, "A symbol with this name already exists in this scope.");
        }
    }


    void TypeChecker::defineClassHeader(const ClassStmt& stmt) {
        // 1. Fetch the placeholder ClassType that was created in Pass 1.
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        // This should never fail if Pass 1 ran correctly.
        auto class_type = std::dynamic_pointer_cast<ClassType>(symbol->type);

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
                // Check for inheritance cycles.
                if (superclass_type->name == class_type->name) {
                    error(stmt.name, "A class cannot inherit from itself.");
                } else {
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
            // TODO ... (resolve trait, check if it's a TraitType, compare method signatures) ...
        }

        // Unset the context before leaving.
        m_current_class = nullptr;
    }



    bool TypeChecker::check(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_hadError = false;

        // --- PASS 1: Declare all top-level type names ---
        std::cerr << "--- TYPECHECKER: PASS 1 (Declarations) ---\n";
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                auto class_type = std::make_shared<ClassType>(class_stmt->name.lexeme);
                if (!m_symbols.declare(class_stmt->name, class_type, true)) {
                    error(class_stmt->name, "A symbol with this name already exists in this scope.");
                } else {
                    std::cerr << "Declared class '" << class_stmt->name.lexeme << "' in global scope.\n";
                }
            } else if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
                auto trait_type = std::make_shared<TraitType>(trait_stmt->name.lexeme);
                if (!m_symbols.declare(trait_stmt->name, trait_type, true)) {
                    error(trait_stmt->name, "A symbol with this name already exists in this scope.");
                } else {
                    std::cerr << "Declared trait '" << trait_stmt->name.lexeme << "' in global scope.\n";
                }
            }
        }

        if (m_hadError) return false;

        // --- PASS 2: Define all headers and signatures ---
        std::cerr << "\n--- TYPECHECKER: PASS 2 (Headers/Signatures) ---\n";
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                defineClassHeader(*class_stmt);
            } else if (auto trait_stmt = std::dynamic_pointer_cast<const TraitStmt>(stmt)) {
                defineTraitHeader(*trait_stmt);
            } else if (auto func_stmt = std::dynamic_pointer_cast<const FuncStmt>(stmt)) {
                defineFunctionHeader(*func_stmt);
            }
        }

        if (m_hadError) return false;

        // --- PASS 3: Check all implementations ---
        std::cerr << "\n--- TYPECHECKER: PASS 3 (Implementations) ---\n";
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

        // 1. Determine the type of the initializer, if it exists.
        if (stmt->initializer) {
            stmt->initializer->accept(*this);
            initializer_type = popType();
            if (initializer_type->kind == TypeKind::ERROR) {
                // Don't proceed if the initializer itself had an error.
                m_symbols.declare(stmt->name, m_type_error, stmt->is_const); // Declare with error type
                return;
            }
        }

        std::shared_ptr<Type> declared_type = nullptr;
        // 2. Determine the type from the annotation, if it exists.
        if (stmt->typeAnnotation) {
            declared_type = resolveType(stmt->typeAnnotation);
        }

        // 3. Compare the types and enforce the rules.
        if (declared_type && initializer_type) {
            // Both an annotation and an initializer exist. They must match.
            if (declared_type->toString() != initializer_type->toString()) {
                error(stmt->name, "Type mismatch. Variable is annotated as '" +
                                  declared_type->toString() + "' but is initialized with a value of type '" +
                                  initializer_type->toString() + "'.");
                declared_type = m_type_error;
            }
        } else if (!declared_type && !initializer_type) {
            // Neither exists. This is an error in a statically-typed language.
            error(stmt->name, "Cannot declare a variable without a type annotation or an initializer.");
            declared_type = m_type_error;
        } else if (!declared_type) {
            // No annotation, but there is an initializer. Infer the type.
            declared_type = initializer_type;
        }
        // (The case where only an annotation exists is fine, the variable is just uninitialized)

        // 4. Declare the new variable in the symbol table.
        if (!m_symbols.declare(stmt->name, declared_type, stmt->is_const)) {
            error(stmt->name, "A variable with this name already exists in this scope.");
        }
    }
    std::any TypeChecker::visit(const Literal& expr) {
        // Determine the type of the literal and push it onto our type stack.
        switch (expr.token.type) {
            case TokenType::NUMBER_INT:   m_type_stack.push(m_type_i64); break;
            case TokenType::NUMBER_FLOAT: m_type_stack.push(m_type_f64); break;
            case TokenType::STRING:       m_type_stack.push(m_type_string); break;
            case TokenType::TRUE:
            case TokenType::FALSE:        m_type_stack.push(m_type_bool); break;
            case TokenType::NIL:          m_type_stack.push(m_type_nil); break;
            default:
                // Should be unreachable if the parser is correct.
                m_type_stack.push(m_type_error);
                break;
        }
        return {}; // The actual return value is unused.
    }
    std::any TypeChecker::visit(const VarExpr& expr) {
        auto symbol = m_symbols.resolve(expr.name.lexeme);
        if (!symbol) {
            error(expr.name, "Undefined variable '" + expr.name.lexeme + "'.");
            m_type_stack.push(m_type_error);
        } else {
            m_type_stack.push(symbol->type);
        }
        return {};
    }
    std::any TypeChecker::visit(const Unary& expr) {
        // 1. Visit the right-hand operand to get its type.
        expr.right->accept(*this);
        auto right_type = popType();

        // 2. Check the type based on the operator.
        switch (expr.op.type) {
            case TokenType::MINUS:
                // The '-' operator requires a numeric type (i64 or f64).
                if (right_type->kind != TypeKind::PRIMITIVE ||
                    (right_type->toString() != "i64" && right_type->toString() != "f64")) {
                    error(expr.op, "Operand for '-' must be a number.");
                    m_type_stack.push(m_type_error);
                } else {
                    // The result of negation is the same numeric type.
                    m_type_stack.push(right_type);
                }
                break;
            case TokenType::BANG:
                // The '!' operator requires a boolean type.
                if (right_type->toString() != "bool") {
                    error(expr.op, "Operand for '!' must be a boolean.");
                    m_type_stack.push(m_type_error);
                } else {
                    // The result of logical not is always a boolean.
                    m_type_stack.push(m_type_bool);
                }
                break;
            default:
                // Should be unreachable.
                m_type_stack.push(m_type_error);
                break;
        }
        return {};
    }
    std::any TypeChecker::visit(const Binary& expr) {
        // 1. Visit both operands to get their types.
        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        // Prevent cascading errors if operands were already invalid.
        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            m_type_stack.push(m_type_error);
            return {};
        }

        // 2. Check the types based on the operator.
        switch (expr.op.type) {
            // --- Arithmetic Operators ---
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
                if (left_type->toString() == "i64" && right_type->toString() == "i64") {
                    m_type_stack.push(m_type_i64); // int op int -> int
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    m_type_stack.push(m_type_f64); // mixed/float op -> float
                } else {
                    error(expr.op, "Operands for arithmetic must be numbers.");
                    m_type_stack.push(m_type_error);
                }
                break;

            case TokenType::PLUS:
                if (left_type->toString() == "i64" && right_type->toString() == "i64") {
                    m_type_stack.push(m_type_i64);
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    m_type_stack.push(m_type_f64);
                } else if (left_type->toString() == "string" && right_type->toString() == "string") {
                    m_type_stack.push(m_type_string); // string + string -> string
                } else {
                    error(expr.op, "'+' operator can only be used on two numbers or two strings.");
                    m_type_stack.push(m_type_error);
                }
                break;

                // --- Comparison Operators ---
            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                if (!(isNumeric(left_type) && isNumeric(right_type))) {
                    error(expr.op, "Operands for comparison must be numbers.");
                    m_type_stack.push(m_type_error);
                } else {
                    m_type_stack.push(m_type_bool); // All comparisons result in a bool.
                }
                break;

                // --- Equality Operators ---
            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL:
                // For now, we'll allow comparing any two types, but they must be the same.
                // TODO: A more advanced type system could have a "Comparable" trait.
                if (left_type->toString() != right_type->toString()) {
                    error(expr.op, "Cannot compare two different types: '" +
                                   left_type->toString() + "' and '" + right_type->toString() + "'.");
                    m_type_stack.push(m_type_error);
                } else {
                    m_type_stack.push(m_type_bool); // Equality check always results in a bool.
                }
                break;

            default:
                m_type_stack.push(m_type_error);
                break;
        }
        return {};
    }
    std::any TypeChecker::visit(const Grouping& expr) {
        expr.expression->accept(*this);
        // The type of the inner expression is already on the stack, so we do nothing.
        return {};
    }
    bool TypeChecker::isNumeric(const std::shared_ptr<Type>& type) {
        if (type->kind != TypeKind::PRIMITIVE) return false;
        const auto& name = type->toString();
        return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
               name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
               name == "f32" || name == "f64";
    }
    std::any TypeChecker::visit(const ListExpr& expr) {
        // If the list is empty, we cannot infer its type. In a more advanced
        // system, it might get a special "empty list" type that can be unified
        // later. For now, this is an ambiguity we can't resolve without a
        // clear type context (e.g., `let x as list<i64> = [];`). We will
        // handle this specific case in visit(VarDeclStmt).
        if (expr.elements.empty()) {
            // We'll create a list of a placeholder "any" type for now.
            m_type_stack.push(std::make_shared<ListType>(m_type_any));
            return {};
        }

        // 1. Determine the type of the *first* element. This will be our
        //    candidate type for the entire list.
        expr.elements[0]->accept(*this);
        auto list_type = popType();

        if (list_type->kind == TypeKind::ERROR) {
            m_type_stack.push(m_type_error);
            return {};
        }

        // 2. Iterate through the rest of the elements and ensure they all
        //    have the same type as the first one.
        for (size_t i = 1; i < expr.elements.size(); ++i) {
            expr.elements[i]->accept(*this);
            auto element_type = popType();

            if (element_type->toString() != list_type->toString()) {
                // We found an element with a different type. Report the error.
                error(expr.bracket, "List elements must all be of the same type. "
                                    "Found '" + element_type->toString() + "' in a list of type '" +
                                    list_type->toString() + "'.");
                m_type_stack.push(m_type_error);
                return {};
            }
        }

        // 3. If we get here, all elements were the same type. Push the final
        //    inferred list type (e.g., list<i64>) onto the stack.
        m_type_stack.push(std::make_shared<ListType>(list_type));
        return {};
    }
/**
 * @brief Type checks an if-orif-else statement.
 *
 * The fundamental rule of an 'if' statement is that its condition MUST evaluate
 * to a boolean value. This is the primary check we perform.
 */
    void TypeChecker::visit(std::shared_ptr<const IfStmt> stmt) {
        // 1. Type check the condition expression.
        stmt->condition->accept(*this);
        auto condition_type = popType();

        // 2. Enforce the rule: the condition must be a 'bool'.
        if (condition_type->toString() != "bool") {
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
        if (condition_type->toString() != "bool") {
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

        // 3. Type check the condition, if it exists, and enforce that it's a bool.
        if (stmt->condition) {
            stmt->condition->accept(*this);
            auto condition_type = popType();
            if (condition_type->toString() != "bool") {
                error(Token(), "For loop condition must be of type 'bool', but got '" +
                               condition_type->toString() + "'.");
            }
        }

        // 4. Type check the increment, if it exists. Its value is discarded, so we pop its type.
        if (stmt->increment) {
            stmt->increment->accept(*this);
            popType();
        }

        // 5. Type check the loop body.
        stmt->body->accept(*this, stmt->body);

        // 6. Exit the scope, destroying the initializer variable (e.g., 'let i').
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
        // This visitor is now part of PASS 3. Its only job is to check the
        // correctness of the function's BODY. The function's signature was
        // already processed and declared in Pass 2 by defineFunctionHeader.

        // 1. A function with no body (like in a trait) has no implementation to check.
        if (!stmt->body) {
            return;
        }

        // 2. Fetch the full FunctionType from the symbol table. This was created in Pass 2.
        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        if (!symbol || symbol->type->kind != TypeKind::FUNCTION) {
            // This should be unreachable if Pass 2 was successful.
            return;
        }
        auto func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);

        // 3. Enter a new scope for the function's body.
        m_symbols.enterScope();

        // 4. Set the context for 'return' statements.
        m_function_return_types.push(func_type->return_type);

        // 5. If it's a method, declare 'this' in the new scope.
        if (stmt->has_this) {
            // The error for 'this' outside a class is caught in Pass 2.
            // Here, we can assume m_current_class is correctly set by visit(ClassStmt).
            if (m_current_class) {
                Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
                m_symbols.declare(this_token, std::make_shared<InstanceType>(m_current_class), true);
            }
        }

        // 6. Declare all parameters as local variables in the new scope.
        for (size_t i = 0; i < stmt->params.size(); ++i) {
            const auto& param_ast = stmt->params[i];
            const auto& param_type = func_type->param_types[i];
            m_symbols.declare(param_ast.name, param_type, true); // Parameters are implicitly const.
        }

        // 7. Finally, type-check every statement in the function's body.
        for (const auto& bodyStmt : (*stmt->body)) {
            bodyStmt->accept(*this, bodyStmt);
        }

        // 8. Restore the context by exiting the scope and popping the return type.
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
        // TODO TEMPORARY: For now, just declare the module name as type 'any'
        // to allow the program to compile. We have no cross-file type safety.
        std::string module_name;
        Token module_token(TokenType::IDENTIFIER, module_name, stmt->modulePath.line, stmt->modulePath.column);
        m_symbols.declare(module_token, m_type_any, false);
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
        // This visitor is now part of PASS 3. Its only job is to check the
        // correctness of field initializers and method bodies. The class's
        // header/signatures were already processed in Pass 2 by defineClassHeader.

        // 1. Fetch the full ClassType from the symbol table. This was populated in Pass 2.
        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        if (!symbol || symbol->type->kind != TypeKind::CLASS) {
            // Should be unreachable if Pass 1 & 2 were successful.
            return;
        }
        auto class_type = std::dynamic_pointer_cast<ClassType>(symbol->type);

        // 2. Set the context to indicate we are "inside" this class.
        //    This is crucial for validating 'this', 'super', and private access.
        auto enclosing_class = m_current_class;
        m_current_class = class_type;

        // 3. Enter a new scope for the class's implementation details.
        m_symbols.enterScope();

        // 4. Declare 'this' in the new scope so it's available to initializers and methods.
        Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
        m_symbols.declare(this_token, std::make_shared<InstanceType>(class_type), true); // 'this' is const

        // --- Pass 3: Check all implementation code ---
        for (const auto& member : stmt->members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                // Check the field's initializer expression, if it has one.
                if (field_member->declaration->initializer) {
                    // Get the field's declared type from our class definition (resolved in Pass 2).
                    const auto& field_name = field_member->declaration->name.lexeme;
                    auto expected_type = class_type->fields.at(field_name).type;

                    // Type check the initializer expression.
                    field_member->declaration->initializer->accept(*this);
                    auto initializer_type = popType();

                    // Validate that the initializer's type matches the field's declared type.
                    if (expected_type->kind != TypeKind::ERROR &&
                        initializer_type->kind != TypeKind::ERROR &&
                        expected_type->toString() != initializer_type->toString()) {

                        // Special case for empty lists `[]`.
                        // An empty list's inferred type is `list<any>`, but it should be
                        // assignable to any `list<T>`.
                        if (auto list_expr = std::dynamic_pointer_cast<const ListExpr>(field_member->declaration->initializer)) {
                            if (list_expr->elements.empty() && expected_type->kind == TypeKind::LIST) {
                                // This is a valid assignment of `[]` to a `list<T>` variable.
                                // Do nothing and let it pass.
                            } else {
                                error(field_member->declaration->name, "Type mismatch in field initializer. Field '" + field_name +
                                                                       "' is type '" + expected_type->toString() +
                                                                       "' but initializer is type '" + initializer_type->toString() + "'.");
                            }
                        } else {
                            error(field_member->declaration->name, "Type mismatch in field initializer. Field '" + field_name +
                                                                   "' is type '" + expected_type->toString() +
                                                                   "' but initializer is type '" + initializer_type->toString() + "'.");
                        }
                    }
                }
            } else if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                // For methods, we just need to visit their FuncStmt node.
                visit(method_member->declaration);
            }
        }

        // 5. Restore the context.
        m_symbols.exitScope();
        m_current_class = enclosing_class;
    }


    void TypeChecker::visit(std::shared_ptr<const TraitStmt> stmt) {
//        // 1. Create a map to hold the resolved method signatures.
//        std::map<std::string, std::shared_ptr<FunctionType>> method_signatures;
//
//        // 2. SET THE CONTEXT: Let the type checker know we are inside a trait.
//        //    We save the old value in case of nested traits (not supported, but good practice).
//        bool enclosing_in_trait_status = m_is_in_trait;
//        m_is_in_trait = true;
//
//        // 3. We enter a temporary scope to check for duplicate method names.
//        m_symbols.enterScope();
//
//        // 4. Iterate through all method AST nodes defined in the trait.
//        for (const auto& method_stmt : stmt->methods) {
//            // Since we are in a trait, we only care about the method's signature,
//            // not its implementation (which shouldn't exist).
//            // We can call the FuncStmt visitor which will do the signature resolution for us.
//            // It will also correctly use the 'm_is_in_trait' flag we just set.
//            method_stmt->accept(*this, method_stmt);
//
//            // After it runs, the function's type is in the symbol table. Let's get it.
//            auto symbol = m_symbols.resolve(method_stmt->name.lexeme);
//            if (symbol && symbol->type->kind == TypeKind::FUNCTION) {
//                // Store the resolved signature for the TraitType.
//                method_signatures[symbol->name] = std::dynamic_pointer_cast<FunctionType>(symbol->type);
//            }
//        }
//
//        m_symbols.exitScope(); // Exit the temporary scope.
//
//        // 5. RESTORE THE CONTEXT
//        m_is_in_trait = enclosing_in_trait_status;
//
//        // 6. Create the final TraitType.
//        auto trait_type = std::make_shared<TraitType>(stmt->name.lexeme);
//
//        // 7. Declare the trait's name in the current (outer) symbol table.
//        if (!m_symbols.declare(stmt->name, trait_type, true)) { // Traits are implicitly const
//            error(stmt->name, "A symbol with this name already exists in this scope.");
//        }
    }

/**
 * @brief Type checks a statement that is just an expression (e.g., a function call).
 *
 * The primary purpose of an expression statement is its side effect. The actual
 * value of the expression is calculated and then immediately discarded. Our type
 * checker reflects this by analyzing the expression and then popping its resulting
 * type off our internal stack, as it's not used anywhere else.
 */
    void TypeChecker::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        // 1. Recursively type check the inner expression.
        stmt->expression->accept(*this);

        // 2. The type of that expression is now on our stack. Since the value is
        //    not used, we pop its type to keep our analysis stack clean.
        popType();
    }

/**
 * @brief Type checks a block of statements (e.g., `{...}`).
 *
 * A block introduces a new lexical scope. The type checker must mirror this
 * by telling the symbol table to enter a new scope before checking the statements
 * within the block, and to exit that scope afterwards.
 */
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
            m_type_stack.push(m_type_error);
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
        m_type_stack.push(rhs_type);
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
            m_type_stack.push(m_type_error);
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
            m_type_stack.push(m_type_error);
            return {};
        }

        // 3. The result type of update expression is the same as the target's type.
        m_type_stack.push(target_type);

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

        // --- Case 1: Calling a function ---
        if (callee_type->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);

            // Rule: Check arity (the number of arguments).
            if (arg_types.size() != func_type->param_types.size()) {
                error(expr.paren, "Incorrect number of arguments. Expected " +
                                  std::to_string(func_type->param_types.size()) + ", but got " +
                                  std::to_string(arg_types.size()) + ".");
                m_type_stack.push(m_type_error);
                return {};
            }

            // Rule: Check the type of each argument against the function's signature.
            for (size_t i = 0; i < arg_types.size(); ++i) {
                if (arg_types[i]->toString() != func_type->param_types[i]->toString()) {
                    error(expr.paren, "Type mismatch for argument " + std::to_string(i + 1) +
                                      ". Expected '" + func_type->param_types[i]->toString() +
                                      "', but got '" + arg_types[i]->toString() + "'.");
                    // We can push an error and return, as one bad argument is enough.
                    m_type_stack.push(m_type_error);
                    return {};
                }
            }

            // If all checks pass, the result of the call expression is the function's return type.
            m_type_stack.push(func_type->return_type);

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
                }
            } else {
                // An 'init' method exists. Validate the call against its signature.
                auto init_sig = std::dynamic_pointer_cast<FunctionType>(init_it->second.type);

                // Arity check
                if (arg_types.size() != init_sig->param_types.size()) {
                    error(expr.paren, "Incorrect number of arguments for constructor of '" + class_type->name +
                                      "'. Expected " + std::to_string(init_sig->param_types.size()) +
                                      ", but got " + std::to_string(arg_types.size()) + ".");
                } else {
                    // Argument type check
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        if (arg_types[i]->toString() != init_sig->param_types[i]->toString()) {
                            error(expr.paren, "Type mismatch for constructor argument " + std::to_string(i + 1) +
                                              ". Expected '" + init_sig->param_types[i]->toString() +
                                              "', but got '" + arg_types[i]->toString() + "'.");
                        }
                    }
                }
            }

            // If all checks pass, the result of constructing a class is an instance of that class.
            m_type_stack.push(std::make_shared<InstanceType>(class_type));

        } else {
            // The callee is not a function or a class.
            error(expr.paren, "This expression is not callable. Can only call functions and classes.");
            m_type_stack.push(m_type_error);
        }

        return {};
    }

    std::any TypeChecker::visit(const GetExpr& expr) {
        expr.object->accept(*this);
        auto object_type = popType();
        const std::string& property_name = expr.name.lexeme;

        if (object_type->kind == TypeKind::INSTANCE) {
            auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
            auto class_type = instance_type->class_type;

            const ClassType::MemberInfo* property_info = class_type->findProperty(property_name);

            if (property_info == nullptr) {
                // The property was not found anywhere in the inheritance chain.
                error(expr.name, "Instance of class '" + class_type->name + "' has no property named '" + property_name + "'.");
                m_type_stack.push(m_type_error);
            } else {
                // The property was found. Now check access.
                if (property_info->access == AccessLevel::PRIVATE) {
                    // Private members are only accessible if we are inside the class
                    // that *defines* them. This is a complex check. A simpler rule
                    // for now is that you can't access them via the '.' operator from outside.
                    // Our current check is sufficient for this.
                    // TODO: refine.
                    if (m_current_class == nullptr || m_current_class->name != class_type->name) { // This check is a simplification
                        error(expr.name, "Property '" + property_name + "' is private and cannot be accessed from here.");
                        m_type_stack.push(m_type_error);
                        return {};
                    }
                }

                // Success! The type of the expression is the type of the property.
                m_type_stack.push(property_info->type);
            }

        }
            // --- TODO: Case 2: Accessing a member of a module ---
            // else if (object_type->kind == TypeKind::MODULE) { ... }
        else {
            // The object is not an instance, so it can't have properties.
            error(expr.name, "Only instances of classes have properties. Cannot access '" +
                             property_name + "' on a value of type '" + object_type->toString() + "'.");
            m_type_stack.push(m_type_error);
        }

        return {};
    }

    std::any TypeChecker::visit(const LogicalExpr& expr) {
        // 1. Type check the left-hand side.
        expr.left->accept(*this);
        auto left_type = popType();

        // 2. Type check the right-hand side.
        expr.right->accept(*this);
        auto right_type = popType();

        // 3. Prevent cascading errors.
        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- THE CORE RULE ---
        // 4. Enforce that both operands must be of type 'bool'.
        if (left_type->toString() != "bool" || right_type->toString() != "bool") {
            error(expr.op, "Both operands for a logical operator ('&&', '||') must be of type 'bool'. "
                           "Got '" + left_type->toString() + "' and '" + right_type->toString() + "'.");
            m_type_stack.push(m_type_error);
            return {};
        }

        // 5. If the types are correct, the result of a logical expression is always a 'bool'.
        m_type_stack.push(m_type_bool);

        return {};
    }

    std::any TypeChecker::visit(const SubscriptExpr& expr) {
        // 1. Type check the object being subscripted (e.g., 'my_list').
        expr.object->accept(*this);
        auto collection_type = popType();

        // 2. Type check the index expression (e.g., 'i').
        expr.index->accept(*this);
        auto index_type = popType();

        if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- Case 1: Accessing a list element ---
        if (collection_type->kind == TypeKind::LIST) {
            auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);

            // Rule: List indices must be integers.
            if (index_type->toString() != "i64") { // Or check isNumeric for more flexibility
                error(expr.bracket, "List index must be an integer, but got '" +
                                    index_type->toString() + "'.");
                m_type_stack.push(m_type_error);
                return {};
            }

            // Success! The result of the expression is the list's element type.
            m_type_stack.push(list_type->element_type);

        }
            // --- Case 2: Accessing a record field ---
        else if (collection_type->kind == TypeKind::RECORD) {
            auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);

            // Rule: Record keys must be strings.
            if (index_type->toString() != "string") {
                error(expr.bracket, "Record key must be a string, but got '" +
                                    index_type->toString() + "'.");
                m_type_stack.push(m_type_error);
                return {};
            }

            // Rule: The key must be a string LITERAL for compile-time checking.
            if (auto key_literal = std::dynamic_pointer_cast<const Literal>(expr.index)) {
                const std::string& key_name = key_literal->token.lexeme;

                // Rule: The field must exist in the record's type definition.
                auto field_it = record_type->fields.find(key_name);
                if (field_it == record_type->fields.end()) {
                    error(key_literal->token, "Record of type '" + record_type->toString() +
                                              "' has no field named '" + key_name + "'.");
                    m_type_stack.push(m_type_error);
                } else {
                    // Success! The result of the expression is the field's type.
                    m_type_stack.push(field_it->second);
                }
            } else {
                error(expr.bracket, "Record fields can only be accessed with a string literal key for static type checking.");
                m_type_stack.push(m_type_error);
            }
        } else {
            error(expr.bracket, "Object of type '" + collection_type->toString() + "' is not subscriptable.");
            m_type_stack.push(m_type_error);
        }

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
        m_type_stack.push(std::make_shared<RecordType>(inferred_fields));

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

            m_type_stack.push(m_type_error);
            return {};
        }

        // --- RULE 1: Condition must be a boolean ---
        if (condition_type->toString() != "bool") {
            error(Token(), "Ternary condition must be of type 'bool', but got '" +
                           condition_type->toString() + "'."); // Placeholder token
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- RULE 2: Then and Else branches must have the same type ---
        if (then_type->toString() != else_type->toString()) {
            error(Token(), "Type mismatch in ternary expression. The 'then' branch has type '" +
                           then_type->toString() + "', but the 'else' branch has type '" +
                           else_type->toString() + "'. Both branches must have the same type.");
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- RULE 3: The result of the expression is the type of the branches ---
        m_type_stack.push(then_type);

        return {};
    }

    std::any TypeChecker::visit(const ThisExpr& expr) {
        // --- RULE 1: Check if we are inside a class ---
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'this' outside of a class method.");
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- RULE 2: The type of 'this' is the instance type of the current class ---
        m_type_stack.push(std::make_shared<InstanceType>(m_current_class));

        return {};
    }

    std::any TypeChecker::visit(const SuperExpr& expr) {
        // --- RULE 1: Must be inside a class ---
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'super' outside of a class method.");
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- RULE 2: The class must have a superclass ---
        if (m_current_class->superclass == nullptr) {
            error(expr.keyword, "Cannot use 'super' in a class with no superclass.");
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- RULE 3: The method must exist on the superclass ---
        const std::string& method_name = expr.method.lexeme;
        auto method_it = m_current_class->superclass->methods.find(method_name);

        if (method_it == m_current_class->superclass->methods.end()) {
            error(expr.method, "The superclass '" + m_current_class->superclass->name +
                               "' has no method named '" + method_name + "'.");
            m_type_stack.push(m_type_error);
            return {};
        }

        // Check for private access on the superclass method
        if (method_it->second.access == AccessLevel::PRIVATE) {
            error(expr.method, "Superclass method '" + method_name + "' is private and cannot be accessed.");
            m_type_stack.push(m_type_error);
            return {};
        }

        // --- RULE 4: Success! The type is the method's FunctionType ---
        m_type_stack.push(method_it->second.type);

        return {};
    }
} // namespace angara