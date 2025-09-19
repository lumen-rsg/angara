#include "CTranspiler.h"
#include <stdexcept>

namespace angara {

    CTranspiler::CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler)
            : m_type_checker(type_checker), m_errorHandler(errorHandler), m_current_out(&m_main_body) {}

    TranspileResult CTranspiler::generate(
            const std::vector<std::shared_ptr<Stmt>>& statements,
            const std::shared_ptr<ModuleType>& module_type, // <-- New parameter
            std::vector<std::string>& all_module_names
    ) {
        if (m_hadError) return {};
        const std::string& module_name = module_type->name; // <-- Get the canonical name
        this->m_current_module_name = module_name;

        // --- Pass 0: Handle Attachments ---
        m_current_out = &m_header_out; // write prototypes to the header

        // Pass 0: Handle Attachments for the HEADER file
        for (const auto& stmt : statements) {
            if (auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
                auto module_type = m_type_checker.m_module_resolutions.at(attach.get());
                // ONLY include headers for OTHER ANGARA modules.
                if (!module_type->is_native) {
                    m_header_out << "#include \"" << module_type->name << ".h\"\n";
                }
            }
        }
        m_header_out << "\n";
        (*m_current_out) << "\n";

        // --- HEADER FILE GENERATION ---
        m_current_out = &m_header_out; // Set context for Pass 1 & 2
        m_indent_level = 0;

        std::string header_guard = "ANGARA_GEN_" + module_name + "_H";
        *m_current_out << "#ifndef " << header_guard << "\n";
        *m_current_out << "#define " << header_guard << "\n\n";
        *m_current_out << "#include \"angara_runtime.h\"\n\n";
        *m_current_out << "#include <stdlib.h>\n\n";

        // --- NEW PASS 0a: Generate DATA struct definitions ---
        (*m_current_out) << "// --- Data Struct Definitions ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataStruct(*data_stmt);
            }
        }

        // --- NEW PASS 0b: Generate DATA equals function prototypes ---
        (*m_current_out) << "\n// --- Data Equals Function Prototypes ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataEqualsPrototype(*data_stmt); // We'll add this helper
            }
        }

        // --- NEW PASS 0c: Generate ENUM struct definitions ---
        (*m_current_out) << "\n// --- Enum Definitions ---\n";
        for (const auto& stmt : statements) {
            if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                transpileEnumStructs(*enum_stmt);
            }
        }

        // --- NEW PASS 0d: Generate ENUM constructor prototypes ---
        (*m_current_out) << "\n// --- Enum Constructor Prototypes ---\n";
        for (const auto& stmt : statements) {
            if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                // This helper will generate prototypes for each variant.
                transpileEnumConstructors(*enum_stmt, true /* generate_prototype_only */);
            }
        }

        pass_1_generate_structs(statements);
        pass_2_generate_declarations(statements, module_name);

        *m_current_out << "\n#endif //" << header_guard << "\n";


        // --- SOURCE FILE GENERATION ---
        m_current_out = &m_source_out; // Set context for Pass 3 & 4
        m_indent_level = 0;

        *m_current_out << "#include \"" << module_name << ".h\"\n\n";

        // PASS 2a: Generate DATA constructor helper implementations
        (*m_current_out) << "// --- Data Constructor Implementations ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataConstructor(*data_stmt);
            }
        }

        // PASS 2b: Generate DATA equals function implementations
        (*m_current_out) << "\n// --- Data Equals Function Implementations ---\n";
        for (const auto& stmt : statements) {
            if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
                transpileDataEqualsImplementation(*data_stmt);
            }
        }

        (*m_current_out) << "\n// --- Enum Constructor Implementations ---\n";
        for (const auto& stmt : statements) {
            if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
                transpileEnumConstructors(*enum_stmt, false /* generate_prototype_only */);
            }
        }

        pass_3_generate_globals_and_implementations(statements, module_name);

        // Pass 5: Generate the C main() function if this is the main module.
        auto main_symbol = m_type_checker.m_symbols.resolve("main");
        if (main_symbol) {
            pass_5_generate_main(statements, module_name, all_module_names);
        }

        if (m_hadError) return {};

        // Assemble the final source file from BOTH the main source stream
        // AND the main body stream.
        std::string final_source = m_source_out.str();
        if (main_symbol) {
            final_source += m_main_body.str();
        }

        if (m_hadError) return {};
        return {m_header_out.str(), final_source};
    }

} // namespace angara