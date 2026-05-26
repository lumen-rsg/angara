#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ContractStmt> stmt) {
    }

    void TypeChecker::defineContractHeader(const ContractStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto contract_type = std::dynamic_pointer_cast<ContractType>(symbol->type);

        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = contract_type;
        }

        for (const auto& member : stmt.members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                const auto& field_decl = field_member->declaration;
                auto field_type = resolveType(field_decl->typeAnnotation);

                if (contract_type->fields.count(field_decl->name.lexeme) || contract_type->methods.count(field_decl->name.lexeme)) {
                    error(field_decl->name, "Duplicate member '" + field_decl->name.lexeme + "' in contract '" + stmt.name.lexeme + "'.");
                    continue;
                }
                contract_type->fields[field_decl->name.lexeme] = {
                    field_type,
                    field_decl->name,
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
                    error(method_decl->name, "Duplicate member '" + method_decl->name.lexeme + "' in contract '" + stmt.name.lexeme + "'.");
                    continue;
                }
                contract_type->methods[method_decl->name.lexeme] = {
                    method_type,
                    method_decl->name,
                    false
                };
            }
        }
    }

}
