//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileClassNew(const ClassStmt& stmt) {
        // This helper generates the implementation for the public `_new` constructor function.
        // This function is responsible for allocating memory and calling the user-defined `init`.

        auto class_type = std::dynamic_pointer_cast<ClassType>(m_type_checker.m_symbols.resolve(stmt.name.lexeme)->type);
        std::string c_struct_name = "Angara_" + class_type->name;

        // 1. Generate the function signature.
        (*m_current_out) << "AngaraObject Angara_" << class_type->name << "_new(";

        auto init_method_ast = findMethodAst(stmt, "init");
        if (init_method_ast) {
            // If a custom 'init' exists, the '_new' function takes its parameters.
            auto init_info = class_type->methods.at("init");
            auto init_func_type = std::dynamic_pointer_cast<FunctionType>(init_info.type);
            for (size_t i = 0; i < init_method_ast->params.size(); ++i) {
                 (*m_current_out) << getCType(init_func_type->param_types[i]) << " " << init_method_ast->params[i].name.lexeme << (i == init_method_ast->params.size() - 1 ? "" : ", ");
            }
        } else {
            // If there is no 'init', it's a default constructor with no parameters.
            (*m_current_out) << "void";
        }
        (*m_current_out) << ") {\n";
        m_indent_level++;

        // 2. Generate the function body.
        //    Step 2a: Allocate memory for the instance struct.
        indent();
        (*m_current_out) << c_struct_name << "* instance = (" << c_struct_name
                         << "*)angara_instance_new(sizeof(" << c_struct_name
                         << "), &g_" << class_type->name << "_class);\n";

        //    Step 2b: Box the raw C pointer into a generic AngaraObject.
        indent();
        (*m_current_out) << "AngaraObject this_obj = (AngaraObject){VAL_OBJ, {.obj = (Object*)instance}};\n";

        //    Step 2c: Conditionally call the user-defined `_init` method.
        if (init_method_ast) {
            indent();
            (*m_current_out) << "Angara_" << class_type->name << "_init(this_obj";
            for (const auto& param : init_method_ast->params) {
                (*m_current_out) << ", " << param.name.lexeme;
            }
            (*m_current_out) << ");\n";
        }

        //    Step 2d: Return the new instance.
        indent();
        (*m_current_out) << "return this_obj;\n";

        m_indent_level--;
        (*m_current_out) << "}\n\n";
    }

}