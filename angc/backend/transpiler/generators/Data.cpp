//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileDataEqualsPrototype(const DataStmt& stmt) {
        std::string c_struct_name = "Angara_" + stmt.name.lexeme;
        (*m_current_out) << "static inline bool " << c_struct_name << "_equals(const " << c_struct_name << "* a, const " << c_struct_name << "* b);\n";
    }

    // Generates the C code for a data block equality comparison.
    void CTranspiler::transpileDataEqualsImplementation(const DataStmt& stmt) {
        auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
        std::string c_struct_name = "Angara_" + data_type->name;
        std::string func_name = c_struct_name + "_equals";

        (*m_current_out) << "static inline bool " << func_name << "(const " << c_struct_name << "* a, const " << c_struct_name << "* b) {\n";
        m_indent_level++;
        indent();
        (*m_current_out) << "return ";

        if (stmt.fields.empty()) {
            (*m_current_out) << "true;\n";
        } else {
            for (size_t i = 0; i < stmt.fields.size(); ++i) {
                const auto& field = stmt.fields[i];
                std::string field_name = sanitize_name(field->name.lexeme);
                auto field_type = data_type->fields.at(field->name.lexeme).type;

                if (field_type->kind == TypeKind::DATA) {
                    // Case 1: The field is a nested data struct.
                    // Cast the generic obj pointer to the specific struct pointer.
                    std::string nested_struct_name = "Angara_" + field_type->toString();
                    std::string ptr_a = "((" + nested_struct_name + "*)AS_OBJ(a->" + field_name + "))";
                    std::string ptr_b = "((" + nested_struct_name + "*)AS_OBJ(b->" + field_name + "))";

                    (*m_current_out) << "Angara_" << field_type->toString() << "_equals(" << ptr_a << ", " << ptr_b << ")";
                } else {
                    // Case 2: The field is any other type (primitive wrapped in AngaraObject).
                    (*m_current_out) << "AS_BOOL(angara_equals(a->" << field_name << ", b->" << field_name << "))";
                }

                if (i < stmt.fields.size() - 1) {
                    (*m_current_out) << " &&\n";
                    indent();
                    (*m_current_out) << "       ";
                }
            }
            (*m_current_out) << ";\n";
        }

        m_indent_level--;
        (*m_current_out) << "}\n\n";
    }

    // Generates the C code for a data block instantiation.
    std::string CTranspiler::transpileDataLiteral(const CallExpr& expr) {
        auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_expression_types.at(expr.callee.get()));
        std::string c_struct_name = "Angara_" + data_type->name;

        std::stringstream ss;
        ss << "((" << c_struct_name << "){ ";
        for (size_t i = 0; i < expr.arguments.size(); ++i) {
            // This assumes order, a more robust version would use designated initializers.
            ss << transpileExpr(expr.arguments[i]) << (i == expr.arguments.size() - 1 ? "" : ", ");
        }
        ss << " })";
        return ss.str();
    }

    void CTranspiler::transpileDataConstructor(const DataStmt& stmt) {
        auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
        std::string c_struct_name = "Angara_" + data_type->name;
        std::string c_func_name = "Angara_data_new_" + data_type->name;

        // --- 1. Generate the function signature (unchanged) ---
        (*m_current_out) << "static inline AngaraObject " << c_func_name << "(";
        const auto& fields = stmt.fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto& field_decl = fields[i];
            auto field_type = data_type->fields.at(field_decl->name.lexeme).type;
            (*m_current_out) << getCType(field_type) << " " << sanitize_name(field_decl->name.lexeme);
            if (i < fields.size() - 1) {
                (*m_current_out) << ", ";
            }
        }
        (*m_current_out) << ") {\n";
        m_indent_level++;

        // --- 2. Generate the function body ---

        // 2a. Malloc memory for the struct.
        indent();
        (*m_current_out) << c_struct_name << "* data = (" << c_struct_name << "*)malloc(sizeof(" << c_struct_name << "));\n";

        // --- Add a check for malloc failure ---
        indent();
        (*m_current_out) << "if (data == NULL) {\n";
        m_indent_level++;
        indent();
        // Throw a standard error message. This call does not return.
        (*m_current_out) << "angara_throw_error(\"Out of memory: failed to allocate data instance for '" << data_type->name << "'.\");\n";
        m_indent_level--;
        indent();
        (*m_current_out) << "}\n";

        // 2b. Initialize the Angara Object header.
        indent();
        (*m_current_out) << "data->obj.type = OBJ_DATA_INSTANCE;\n";
        indent();
        (*m_current_out) << "data->obj.ref_count = 1;\n";

        // 2c. Assign each parameter to its corresponding struct field.
        for (const auto& field_decl : fields) {
            std::string field_name = sanitize_name(field_decl->name.lexeme);
            indent();
            (*m_current_out) << "data->" << field_name << " = " << field_name << ";\n";
        }

        // 2d. "Box" the raw C pointer into a generic AngaraObject and return it.
        indent();
        (*m_current_out) << "return (AngaraObject){ VAL_OBJ, { .obj = (Object*)data } };\n";

        m_indent_level--;
        (*m_current_out) << "}\n\n";
    }

    void CTranspiler::transpileDataStruct(const DataStmt& stmt) {
        auto data_type = std::dynamic_pointer_cast<DataType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
        std::string c_struct_name = "Angara_" + data_type->name;

        if (stmt.is_foreign) {
            // A foreign data block in Angara is just a wrapper around a pointer.
            // The real struct is defined in the included C header.
            (*m_current_out) << "typedef struct " << c_struct_name << " {\n";
            m_indent_level++;
            indent(); (*m_current_out) << "Object obj;\n";
            // The payload is a single, opaque pointer to the real C struct.
            indent(); (*m_current_out) << "struct " << stmt.name.lexeme << "* ptr;\n";
            m_indent_level--;
            (*m_current_out) << "} " << c_struct_name << ";\n\n";
            return;
        }

        // Use a typedef for a clean name in C.
        (*m_current_out) << "typedef struct " << c_struct_name << " " << c_struct_name << ";\n";
        (*m_current_out) << "struct " << c_struct_name << " {\n";
        m_indent_level++;

        // The struct starts with the common Object header, just like a class instance.
        indent(); (*m_current_out) << "Object obj;\n";

        // Define all the fields.
        // We iterate the AST `fields` to preserve the declaration order.
        for (const auto& field_decl : stmt.fields) {
            // We get the canonical, resolved type from the DataType.
            auto field_info = data_type->fields.at(field_decl->name.lexeme);
            indent();
            // getCType will correctly return `AngaraObject` for all fields now.
            (*m_current_out) << getCType(field_info.type) << " "
                             << sanitize_name(field_decl->name.lexeme) << ";\n";
        }

        m_indent_level--;
        (*m_current_out) << "};\n\n";
    }

}