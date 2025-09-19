//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::pass_1_generate_structs(const std::vector<std::shared_ptr<Stmt>>& statements) {
        (*m_current_out) << "// --- Struct Definitions ---\n";
        for (const auto& stmt : statements) {
            if (auto class_stmt = std::dynamic_pointer_cast<const ClassStmt>(stmt)) {
                transpileStruct(*class_stmt);
                (*m_current_out) << "extern AngaraClass g_" << class_stmt->name.lexeme << "_class;\n";
            }
        }
    }

    void CTranspiler::transpileStruct(const ClassStmt& stmt) {
        // 1. Get the canonical ClassType from the type checker's symbol table.
        //    This was fully populated by the checker's Pass 1 and Pass 2.
        auto class_symbol = m_type_checker.m_symbols.resolve(stmt.name.lexeme);
        auto class_type = std::dynamic_pointer_cast<ClassType>(class_symbol->type);

        std::string c_struct_name = "Angara_" + stmt.name.lexeme;

        (*m_current_out) << "typedef struct " << c_struct_name << " " << c_struct_name << ";\n";
        (*m_current_out) << "struct " << c_struct_name << " {\n";
        m_indent_level++;

        // 2. Add the base header.
        // If there is a superclass, its struct ALREADY contains the base header.
        if (class_type->superclass) {
            indent(); (*m_current_out) << "struct Angara_" << class_type->superclass->name << " parent;\n";
        } else {
            indent(); (*m_current_out) << "AngaraInstance base;\n";
        }

        // 3. Add all fields DEFINED IN THIS CLASS.
        //    We iterate the AST to get the field names in their declared order.
        for (const auto& member : stmt.members) {
            if (auto field_member = std::dynamic_pointer_cast<const FieldMember>(member)) {
                const auto& field_name = field_member->declaration->name.lexeme;
                // Get the type from the ClassType's field map, not m_variable_types.
                const auto& field_info = class_type->fields.at(field_name);

                indent();
                (*m_current_out) << getCType(field_info.type) << " " << field_name << ";\n";
            }
        }

        m_indent_level--;
        (*m_current_out) << "};\n\n";
    }

}