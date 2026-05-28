#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const DataStmt> stmt) {
    }

    void TypeChecker::defineDataHeader(const DataStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto data_type = std::dynamic_pointer_cast<DataType>(symbol->type);

        for (const auto& tp : stmt.type_params) {
            data_type->type_params.push_back(tp.lexeme);
        }

        auto saved_type_params = m_active_type_params;
        for (const auto& tp : stmt.type_params) {
            m_active_type_params[tp.lexeme] = std::make_shared<TypeParameterType>(tp.lexeme);
        }

        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = data_type;
        }

        if (stmt.is_foreign) {
            Token dummy_token;
            data_type->is_foreign = true;
            data_type->is_opaque = stmt.is_opaque;
            data_type->is_union = stmt.is_union;
            for (const auto& field_decl : stmt.fields) {
                if (data_type->fields.count(field_decl->name.lexeme)) {
                    error(field_decl->name, "Duplicate field '" + field_decl->name.lexeme + "' in foreign data block '" + stmt.name.lexeme + "'.", "E281");
                    continue;
                }
                auto field_type = resolveType(field_decl->typeAnnotation);
                data_type->fields[field_decl->name.lexeme] = {field_type, AccessLevel::PUBLIC, dummy_token, false};
            }
            // Foreign data types get a zero-arg constructor: utsname() -> allocate zeroed C struct
            data_type->constructor_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{}, data_type
            );
            m_active_type_params = saved_type_params;
            return;
        }

        std::vector<std::shared_ptr<Type>> ctor_params;
        Token dummy_token;

        for (const auto& field_decl : stmt.fields) {
            if (data_type->fields.count(field_decl->name.lexeme)) {
                error(field_decl->name, "Duplicate field '" + field_decl->name.lexeme + "' in data block '" + stmt.name.lexeme + "'.", "E282");
                continue;
            }

            std::shared_ptr<Type> field_type;
            if (field_decl->typeAnnotation) {
                field_type = resolveType(field_decl->typeAnnotation);
            } else {
                error(field_decl->name, "Data block fields must have an explicit type annotation (e.g., 'let x as i64').", "E283");
                field_type = m_type_error;
            }

            if (field_decl->initializer) {
                error(field_decl->name, "Data block fields cannot have default initializers — values are provided through the constructor.", "E284");
            }

            data_type->fields[field_decl->name.lexeme] = {field_type, AccessLevel::PUBLIC, dummy_token, field_decl->is_const};

            ctor_params.push_back(field_type);
        }

        data_type->constructor_type = std::make_shared<FunctionType>(ctor_params, data_type);

        m_active_type_params = saved_type_params;
    }

}
