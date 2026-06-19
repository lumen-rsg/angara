#include "TypeChecker.h"
namespace angara {

std::any TypeChecker::visit(const GetExpr& expr) {
    expr.object->accept(*this);
    auto object_type = popType();

    if (object_type->kind == TypeKind::ERROR) {
        pushAndSave(&expr, m_type_error);
        return {};
    }

    bool is_optional_chain = (expr.op.type == TokenType::QUESTION_DOT);
    std::shared_ptr<Type> unwrapped_object_type = object_type;

    if (object_type->kind == TypeKind::OPTIONAL) {
        unwrapped_object_type = std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type;
    }

    // v5: ref<T> — transparently unwrap for field/method access.
    if (object_type->kind == TypeKind::REF) {
        unwrapped_object_type = std::dynamic_pointer_cast<RefType>(object_type)->inner_type;
    }

    if (object_type->kind == TypeKind::OPTIONAL && !is_optional_chain) {
        error(expr.op, "Cannot access property on an optional type '" + object_type->toString() + "' — use '?.' for safe access, or unwrap the value first.", "E333");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    const std::string& property_name = expr.name.lexeme;
    std::shared_ptr<Type> property_type = m_type_error;

    if (unwrapped_object_type->kind == TypeKind::DATA) {
        auto data_type = std::dynamic_pointer_cast<DataType>(unwrapped_object_type);

        if (property_name == "clone") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{},
                data_type
            );
        } else if (property_name == "deep_clone") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{},
                data_type
            );
        }
        else {
            auto field_it = data_type->fields.find(property_name);
            if (field_it == data_type->fields.end()) {
                error(expr.name, "Data type '" + data_type->name + "' has no field named '" + property_name + "'.", "E334");
            } else {
                property_type = field_it->second.type;
                // Foreign data i8[N] fields auto-convert to string at codegen time
                if (data_type->is_foreign && property_type->kind == TypeKind::FIXED_ARRAY) {
                    property_type = m_type_string;
                }
            }
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::GENERIC_INSTANCE) {
        auto generic_instance = std::dynamic_pointer_cast<GenericInstanceType>(unwrapped_object_type);
        auto base_data = std::dynamic_pointer_cast<DataType>(generic_instance->base_type);

        if (base_data) {
            if (property_name == "clone") {
                property_type = std::make_shared<FunctionType>(
                    std::vector<std::shared_ptr<Type>>{},
                    unwrapped_object_type
                );
            } else if (property_name == "deep_clone") {
                property_type = std::make_shared<FunctionType>(
                    std::vector<std::shared_ptr<Type>>{},
                    unwrapped_object_type
                );
            } else {
                auto field_it = base_data->fields.find(property_name);
                if (field_it == base_data->fields.end()) {
                    error(expr.name, "Data type '" + base_data->name + "' has no field named '" + property_name + "'.", "E334");
                } else {
                    // Substitute type parameters with concrete types
                    property_type = generic_instance->substitute(field_it->second.type);
                }
            }
        } else {
            error(expr.op, "Cannot access properties on generic instance of '" + generic_instance->base_type->toString() + "'.", "E345");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::INSTANCE) {
        auto instance_type = std::dynamic_pointer_cast<InstanceType>(unwrapped_object_type);
        const ClassType::MemberInfo* prop_info = instance_type->class_type->findProperty(property_name);
        if (!prop_info) {
            error(expr.name, "Class '" + instance_type->toString() + "' has no property or method named '" + property_name + "'.", "E335");

            std::vector<std::string> candidates;
            for(const auto& [name, member] : instance_type->class_type->fields) candidates.push_back(name);
            for(const auto& [name, member] : instance_type->class_type->methods) candidates.push_back(name);
            find_and_report_suggestion(expr.name, candidates);
        } else {
            if (prop_info->access == AccessLevel::PRIVATE && (m_current_class == nullptr || m_current_class->name != instance_type->class_type->name)) {
                error(expr.name, "Property '" + property_name + "' is private and cannot be accessed from outside the class.", "E336");
            } else {
                property_type = prop_info->type;
            }
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::ENUM) {
        auto enum_type = std::dynamic_pointer_cast<EnumType>(unwrapped_object_type);
        auto variant_it = enum_type->variants.find(property_name);

        if (variant_it == enum_type->variants.end()) {
            error(expr.name, "Enum '" + enum_type->name + "' has no variant named '" + property_name + "'.", "E337");
        } else {
            auto variant_constructor_type = std::dynamic_pointer_cast<FunctionType>(variant_it->second);

            if (variant_constructor_type->param_types.empty()) {
                property_type = variant_constructor_type->return_type;
            } else {
                property_type = variant_constructor_type;
            }
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::MODULE) {
        auto module_type = std::dynamic_pointer_cast<ModuleType>(unwrapped_object_type);
        auto member_it = module_type->exports.find(property_name);
        if (member_it == module_type->exports.end()) {
            error(expr.name, "Module '" + module_type->name + "' has no exported member named '" + property_name + "'.", "E338");

            std::vector<std::string> candidates;
            for(const auto& [name, type] : module_type->exports) candidates.push_back(name);
            find_and_report_suggestion(expr.name, candidates);

        } else {
            property_type = member_it->second;
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::LIST) {
        auto list_type = std::dynamic_pointer_cast<ListType>(unwrapped_object_type);
        if (property_name == "push") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{list_type->element_type},
                m_type_nil
            );
        } else if (property_name == "remove_at") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_i64},
                list_type->element_type
            );
        } else if (property_name == "remove") {
            property_type = std::make_shared<FunctionType>(
               std::vector<std::shared_ptr<Type>>{list_type->element_type},
               m_type_bool
           );
        } else if (property_name == "deep_clone") {
            property_type = std::make_shared<FunctionType>(
              std::vector<std::shared_ptr<Type>>{},
              unwrapped_object_type
          );
        } else if (property_name == "length" || property_name == "len" || property_name == "size" || property_name == "count") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{},
                m_type_i64
            );
        }
        else {
            error(expr.name, "Type 'list' has no property or method named '" + property_name + "'. Available: push, remove_at, remove, deep_clone, length.", "E339");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::RECORD) {
        if (property_name == "remove") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_string},
                m_type_bool
            );
        } else if (property_name == "keys") {
            auto list_of_strings = std::make_shared<ListType>(m_type_string);
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{},
                list_of_strings
            );
        } else if (property_name == "clone") {
            property_type = std::make_shared<FunctionType>(
               std::vector<std::shared_ptr<Type>>{},
               unwrapped_object_type
           );
        }
        else if (property_name == "deep_clone") {
            property_type = std::make_shared<FunctionType>(
               std::vector<std::shared_ptr<Type>>{},
               unwrapped_object_type
           );
        }
        else {
            error(expr.name, "Type 'record' has no property named '" + property_name + "'. Use subscript '[]' to access fields, or one of: remove, keys, clone, deep_clone.", "E340");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::THREAD) {
        if (property_name == "join") {
            property_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_any);
        } else {
            error(expr.name, "Type 'Thread' has no property named '" + property_name + "'. Available: join.", "E341");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::MUTEX) {
        if (property_name == "lock" || property_name == "unlock") {
            property_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_nil);
        } else {
            error(expr.name, "Type 'Mutex' has no property named '" + property_name + "'. Available: lock, unlock.", "E342");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::EXCEPTION) {
        auto exception_type = std::dynamic_pointer_cast<ExceptionType>(unwrapped_object_type);
        auto field_it = exception_type->fields.find(property_name);
        if (field_it == exception_type->fields.end()) {
            error(expr.name, "Type 'Exception' has no property named '" + property_name + "'.", "E343");
        } else {
            property_type = field_it->second.type;
        }
    }
    else {
        error(expr.op, "Type '" + object_type->toString() + "' has no accessible properties or methods.", "E344");
    }

    if (property_type->kind == TypeKind::ERROR) {
        pushAndSave(&expr, m_type_error);
    } else if (is_optional_chain || object_type->kind == TypeKind::OPTIONAL) {
        pushAndSave(&expr, std::make_shared<OptionalType>(property_type));
    } else {
        pushAndSave(&expr, property_type);
    }

    return {};
}

}
