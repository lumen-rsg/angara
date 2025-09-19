//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileEnumConstructors(const EnumStmt& stmt, bool generate_prototype_only) {
        auto enum_type = std::dynamic_pointer_cast<EnumType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
        std::string enum_name = enum_type->name;
        std::string c_struct_name = "Angara_" + enum_name;

        for (const auto& variant_pair : enum_type->variants) {
            const auto& variant_name = variant_pair.first;
            const auto& variant_sig = variant_pair.second;
            std::string c_func_name = "Angara_" + enum_name + "_" + variant_name;

            // --- Generate Signature ---
            // A constructor always returns a generic AngaraObject.
            (*m_current_out) << "AngaraObject " << c_func_name << "(";
            for (size_t i = 0; i < variant_sig->param_types.size(); ++i) {
                (*m_current_out) << getCType(variant_sig->param_types[i]) << " arg" << i;
                if (i < variant_sig->param_types.size() - 1) (*m_current_out) << ", ";
            }
            (*m_current_out) << ")";

            if (generate_prototype_only) {
                (*m_current_out) << ";\n";
                continue;
            }

            // --- Generate Body ---
            (*m_current_out) << " {\n";
            m_indent_level++;

            indent();
            (*m_current_out) << c_struct_name << "* data = (" << c_struct_name << "*)malloc(sizeof(" << c_struct_name << "));\n";
            indent();
            (*m_current_out) << "if (data == NULL) { angara_throw_error(\"Out of memory creating enum instance.\"); }\n";

            indent();
            (*m_current_out) << "data->obj.type = OBJ_ENUM_INSTANCE; data->obj.ref_count = 1;\n";
            indent();
            (*m_current_out) << "data->tag = " << c_struct_name << "_Tag_" << variant_name << ";\n";

            // Assign the payload, if one exists.
            if (!variant_sig->param_types.empty()) {
                indent();
                // This simplified version assumes a single parameter.
                (*m_current_out) << "data->payload." << sanitize_name(variant_name) << " = arg0;\n";
                // A full implementation would need to handle incref-ing if the payload is an AngaraObject.
            }

            indent();
            (*m_current_out) << "return (AngaraObject){ VAL_OBJ, { .obj = (Object*)data } };\n";
            m_indent_level--;
            (*m_current_out) << "}\n\n";
        }
    }

    void CTranspiler::transpileEnumStructs(const EnumStmt& stmt) {
        std::string enum_name = stmt.name.lexeme;
        std::string c_base_name = "Angara_" + enum_name;
        auto enum_type = std::dynamic_pointer_cast<EnumType>(m_type_checker.m_symbols.resolve(enum_name)->type);

        // 1. Generate the tag enum
        (*m_current_out) << "typedef enum {\n";
        m_indent_level++;
        for (const auto& variant_pair : enum_type->variants) {
            indent();
            (*m_current_out) << c_base_name << "_Tag_" << variant_pair.first << ",\n";
        }
        m_indent_level--;
        (*m_current_out) << "} " << c_base_name << "_Tag;\n\n";

        // 2. Generate the payload union. Only include variants that have data.
        (*m_current_out) << "typedef union {\n";
        m_indent_level++;
        for (const auto& variant_pair : enum_type->variants) {
            if (!variant_pair.second->param_types.empty()) {
                // This simplified version assumes a variant has at most one parameter.
                // A full implementation would use a nested struct for multi-param variants.
                auto param_type = variant_pair.second->param_types[0];
                indent();
                (*m_current_out) << getCType(param_type) << " " << sanitize_name(variant_pair.first) << ";\n";
            }
        }
        m_indent_level--;
        (*m_current_out) << "} " << c_base_name << "_Payload;\n\n";

        // 3. Generate the main struct for an enum instance
        (*m_current_out) << "typedef struct " << c_base_name << " {\n";
        m_indent_level++;
        indent(); (*m_current_out) << "Object obj;\n";
        indent(); (*m_current_out) << c_base_name << "_Tag tag;\n";
        indent(); (*m_current_out) << c_base_name << "_Payload payload;\n";
        m_indent_level--;
        (*m_current_out) << "} " << c_base_name << ";\n\n";
    }

}