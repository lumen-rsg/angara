//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

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

}
