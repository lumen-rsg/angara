#include "TypeChecker.h"
namespace angara {

void TypeChecker::defineClassHeader(const ClassStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto class_type = std::dynamic_pointer_cast<ClassType>(symbol->type);

        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = class_type;
        }

        m_current_class = class_type;

        if (stmt.superclass) {
            auto super_symbol = m_symbols.resolve(stmt.superclass->name.lexeme);
            if (!super_symbol) {
                error(stmt.superclass->name, "Superclass '" + stmt.superclass->name.lexeme + "' is not defined.");
            } else if (super_symbol->type->kind != TypeKind::CLASS) {
                error(stmt.superclass->name, "'" + stmt.superclass->name.lexeme + "' is not a class and cannot be inherited from.");
            } else {
                auto superclass_type = std::dynamic_pointer_cast<ClassType>(super_symbol->type);

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

                if (!has_cycle) {
                    class_type->superclass = superclass_type;
                }
            }
        }

        for (const auto& member : stmt.members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                const auto& field_decl = field_member->declaration;
                auto field_type = m_type_error;

                if (field_decl->typeAnnotation) {
                    field_type = resolveType(field_decl->typeAnnotation);
                } else {
                    error(field_decl->name, "Class field '" + field_decl->name.lexeme + "' requires an explicit type annotation.");
                }

                if (class_type->fields.count(field_decl->name.lexeme)) {
                    error(field_decl->name, "Member '" + field_decl->name.lexeme + "' is already declared in class '" + class_type->name + "'.");
                }
                class_type->fields[field_decl->name.lexeme] = {field_type, field_member->access, field_decl->name, field_decl->is_const};

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

                if (class_type->methods.count(method_decl->name.lexeme) || class_type->fields.count(method_decl->name.lexeme)) {
                    error(method_decl->name, "Member '" + method_decl->name.lexeme + "' is already declared in class '" + class_type->name + "'.");
                }
                class_type->methods[method_decl->name.lexeme] = {method_type, method_member->access, method_decl->name, false};
            }
        }

    for (const auto& contract_expr : stmt.contracts) {
        auto contract_symbol = m_symbols.resolve(contract_expr->name.lexeme);
        if (!contract_symbol) {
            error(contract_expr->name, "Contract '" + contract_expr->name.lexeme + "' is not defined.");
            continue;
        }
        if (contract_symbol->type->kind != TypeKind::CONTRACT) {
            error(contract_expr->name, "'" + contract_expr->name.lexeme + "' is not a contract.");
            continue;
        }
        auto contract_type = std::dynamic_pointer_cast<ContractType>(contract_symbol->type);

        for (const auto& [name, required_field] : contract_type->fields) {
            const auto* class_prop = class_type->findProperty(name);
            if (!class_prop) {
                error(stmt.name, "Class '" + stmt.name.lexeme + "' does not fulfill contract '" + contract_type->name + "' — missing required field '" + name + "'.");
                note(required_field.declaration_token, "Requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_type->methods.count(name)) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires a field named '" + name + "', but class '" + stmt.name.lexeme + "' declares it as a method.");
                note(required_field.declaration_token, "Requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_prop->access != AccessLevel::PUBLIC) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires field '" + name + "' to be public, but it is private in class '" + stmt.name.lexeme + "'.");
                note(required_field.declaration_token, "Requirement '" + name + "' is defined here.");
            }
            if (class_prop->is_const != required_field.is_const) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires field '" + name + "' to be '" + (required_field.is_const ? "const" : "let") + "', but it is not in class '" + stmt.name.lexeme + "'.");
                note(required_field.declaration_token, "Requirement '" + name + "' is defined here.");
            }
            if (class_prop->type->toString() != required_field.type->toString()) {
                error(stmt.name, "Type mismatch for field '" + name + "' required by contract '" + contract_type->name + "'. Expected '" + required_field.type->toString() + "', but got '" + class_prop->type->toString() + "'.");
                note(required_field.declaration_token, "Requirement '" + name + "' is defined here.");
            }
        }

        for (const auto& [name, required_method] : contract_type->methods) {
            const auto* class_prop = class_type->findProperty(name);
            if (!class_prop) {
                error(stmt.name, "Class '" + stmt.name.lexeme + "' does not fulfill contract '" + contract_type->name + "' — missing required method '" + name + "'.");
                note(required_method.declaration_token, "Requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_type->fields.count(name)) {
                error(stmt.name, "Contract '" + contract_type->name + "' requires a method named '" + name + "', but class '" + stmt.name.lexeme + "' declares it as a field.");
                note(required_method.declaration_token, "Requirement '" + name + "' is defined here.");
                continue;
            }
            if (class_prop->access != AccessLevel::PUBLIC) {
                 error(stmt.name, "Contract '" + contract_type->name + "' requires method '" + name + "' to be public, but it is private in class '" + stmt.name.lexeme + "'.");
                note(required_method.declaration_token, "Requirement '" + name + "' is defined here.");
            }
            auto required_func_type = std::dynamic_pointer_cast<FunctionType>(required_method.type);
            auto class_func_type = std::dynamic_pointer_cast<FunctionType>(class_prop->type);
            if (!class_func_type->equals(*required_func_type)) {
                error(stmt.name, "Signature of method '" + name + "' in class '" + stmt.name.lexeme + "' does not match contract '" + contract_type->name + "'.\n  Required: " + required_func_type->toString() + "\n  Found:    " + class_func_type->toString());
                note(required_method.declaration_token, "Requirement '" + name + "' is defined here.");
            }
        }
    }

        for (const auto& trait_expr : stmt.traits) {
            auto trait_symbol = m_symbols.resolve(trait_expr->name.lexeme);
            if (!trait_symbol) {
                error(trait_expr->name, "Trait '" + trait_expr->name.lexeme + "' is not defined.");
                continue;
            }
            if (trait_symbol->type->kind != TypeKind::TRAIT) {
                error(trait_expr->name, "'" + trait_expr->name.lexeme + "' is not a trait.");
                continue;
            }
            auto trait_type = std::dynamic_pointer_cast<TraitType>(trait_symbol->type);

            for (const auto& [name, required_sig] : trait_type->methods) {
                auto method_it = class_type->methods.find(name);
                if (method_it == class_type->methods.end()) {
                    error(stmt.name, "Class '" + stmt.name.lexeme + "' does not implement required trait method '" + name + "'.");
                } else {
                    auto implemented_sig_info = method_it->second;
                    auto implemented_sig = std::dynamic_pointer_cast<FunctionType>(implemented_sig_info.type);

                    if (!implemented_sig->equals(*required_sig)) {
                        error(stmt.name, "Signature of method '" + name + "' in class '" + stmt.name.lexeme +
                            "' does not match trait '" + trait_type->name + "'.\n" +
                            "  Required: " + required_sig->toString() + "\n" +
                            "  Found:    " + implemented_sig->toString());
                    }
                }
            }
        }

        m_current_class = nullptr;

    }

    void TypeChecker::visit(std::shared_ptr<const ClassStmt> stmt) {
        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        auto class_type = std::dynamic_pointer_cast<ClassType>(symbol->type);

        auto enclosing_class = m_current_class;
        m_current_class = class_type;

        m_symbols.enterScope();

        Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
        m_symbols.declare(this_token, std::make_shared<InstanceType>(class_type), true);

        for (const auto& member : stmt->members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                if (field_member->declaration->initializer) {
                    const auto& field_name = field_member->declaration->name.lexeme;
                    auto expected_type = class_type->fields.at(field_name).type;

                    field_member->declaration->initializer->accept(*this);
                    auto initializer_type = popType();

                    if (expected_type->kind != TypeKind::ERROR &&
                        initializer_type->kind != TypeKind::ERROR) {

                        bool types_match = (expected_type->toString() == initializer_type->toString());
                        if (!types_match && initializer_type->toString() == "list<any>" && expected_type->kind == TypeKind::LIST) {
                            types_match = true;
                        }

                        if (!types_match) {
                            error(field_member->declaration->name, "Type mismatch in field initializer. Field '" + field_name +
                                                                   "' is declared as '" + expected_type->toString() +
                                                                   "', but the initializer has type '" + initializer_type->toString() + "'.");
                        }
                    }
                }
            } else if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                visit(method_member->declaration);
            }
        }

        exitScopeAndWarn();
        m_current_class = enclosing_class;
    }

}
