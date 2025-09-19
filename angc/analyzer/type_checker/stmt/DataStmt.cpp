//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

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

}