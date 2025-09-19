//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::pass_5_generate_main(const std::vector<std::shared_ptr<Stmt>>& statements,
                                       const std::string& module_name,
                                       const std::vector<std::string>& all_module_names) {

        m_current_out = &m_main_body;
        m_indent_level = 0;

        *m_current_out << "// --- C Entry Point ---\n";
        *m_current_out << "int main(int argc, const char* argv[]) {\n";
        m_indent_level++;

        indent(); *m_current_out << "angara_runtime_init();\n\n";
        indent(); (*m_current_out) << "// --- Initialize All Modules ---\n";
        for (const std::string& mod_name : all_module_names) {
            indent();
            (*m_current_out) << "Angara_" << mod_name << "_init_globals();\n";
        }
        (*m_current_out) << "\n";

        // indent(); *m_current_out << "Angara_" << module_name << "_init_globals();\n\n";

        auto main_symbol = m_type_checker.m_symbols.resolve("main");
        auto main_func_type = std::dynamic_pointer_cast<FunctionType>(main_symbol->type);

        indent(); *m_current_out << "// Call the user's Angara main function\n";
        // The global variable 'main' was created and initialized in init_globals.
        if (main_func_type->param_types.empty()) {
            (*m_current_out) << "    AngaraObject result = angara_call(g_angara_main_closure, 0, NULL);\n";
        } else {
            *m_current_out << "    AngaraObject args_list = angara_list_new();\n";
            *m_current_out << "    for (int i = 0; i < argc; i++) {\n";
            *m_current_out << "        angara_list_push(args_list, angara_string_from_c(argv[i]));\n";
            *m_current_out << "    }\n";
            *m_current_out << "    AngaraObject result = angara_call(g_angara_main_closure, 1, &args_list);\n";
            *m_current_out << "    angara_decref(args_list);\n";
        }

        *m_current_out << "\n";
        indent(); *m_current_out << "int exit_code = (int)AS_I64(result);\n";
        indent(); *m_current_out << "angara_decref(result);\n\n";
        indent(); *m_current_out << "angara_runtime_shutdown();\n";
        indent(); *m_current_out << "return exit_code;\n";

        m_indent_level--;
        *m_current_out << "}\n";
    }

}