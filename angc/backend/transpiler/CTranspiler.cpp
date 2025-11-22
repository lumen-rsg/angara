#include "CTranspiler.h"
#include <stdexcept>
#include <set>

namespace angara {

CTranspiler::CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler)
        : m_type_checker(type_checker), m_errorHandler(errorHandler), m_current_out(&m_main_body) {}

TranspileResult CTranspiler::generate(
        const std::vector<std::shared_ptr<Stmt>>& statements,
        const std::shared_ptr<ModuleType>& module_type,
        std::vector<std::string>& all_module_names
) {
    if (m_hadError) return {};
    const std::string& module_name = module_type->name;
    this->m_current_module_name = module_name;

    // --- HEADER FILE GENERATION ---
    m_current_out = &m_header_out;
    m_indent_level = 0;

    // Pass 1: Write this module's own include guard FIRST.
    std::string header_guard = "ANGARA_GEN_" + module_name + "_H";
    *m_current_out << "#ifndef " << header_guard << "\n";
    *m_current_out << "#define " << header_guard << "\n\n";

    // Pass 2: Write includes for other Angara module dependencies.
    *m_current_out << "// --- Module Dependencies ---\n";
    for (const auto& stmt : statements) {
        if (auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt)) {
            auto attached_module_type = m_type_checker.m_module_resolutions.at(attach.get());
            if (!attached_module_type->is_native) {
                m_header_out << "#include \"" << attached_module_type->name << ".h\"\n";
            }
        }
    }
    m_header_out << "\n";

    // Pass 3: Write includes for foreign C headers.
    *m_current_out << "// --- Foreign Header Includes ---\n";
    std::set<std::string> included_headers;
    for (const auto& stmt : statements) {
        if (auto header_stmt = std::dynamic_pointer_cast<const ForeignHeaderStmt>(stmt)) {
            const std::string& header_name = header_stmt->header.lexeme;
            if (included_headers.find(header_name) == included_headers.end()) {
                m_header_out << "#include <" << header_name << ">\n";
                included_headers.insert(header_name);
            }
        }
    }
    m_header_out << "\n";

    // Pass 4: Include core runtime headers.
    *m_current_out << "#include \"angara_runtime.h\"\n";
    *m_current_out << "#include <stdlib.h>\n\n";

    // Pass 5: Generate all struct, enum, and type definitions for this module.
    // This single call to pass_1_generate_structs now handles BOTH class and data structs.
    // The redundant, separate loop for data structs has been removed.
    pass_1_generate_structs(statements);

    (*m_current_out) << "\n// --- Data Equals Function Prototypes ---\n";
    for (const auto& stmt : statements) {
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            if (!data_stmt->is_foreign) {
                transpileDataEqualsPrototype(*data_stmt);
            }
        }
    }

    (*m_current_out) << "\n// --- Enum Definitions ---\n";
    for (const auto& stmt : statements) {
        if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
            transpileEnumStructs(*enum_stmt);
        }
    }

    (*m_current_out) << "\n// --- Enum Constructor Prototypes ---\n";
    for (const auto& stmt : statements) {
        if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
            transpileEnumConstructors(*enum_stmt, true /* generate_prototype_only */);
        }
    }

    (*m_current_out) << "\n// --- Enum Equals Prototypes ---\n";
    for (const auto& stmt : statements) {
        if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
            transpileEnumEqualsPrototype(*enum_stmt);
        }
    }

    (*m_current_out) << "\n// --- Data Clone Function Prototypes ---\n";
    for (const auto& stmt : statements) {
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            if (!data_stmt->is_foreign) {
                transpileDataClonePrototype(*data_stmt);
            }
        }
    }

    (*m_current_out) << "\n// --- Deep Clone Prototypes ---\n";
    for (const auto& stmt : statements) {
        // 1. Data Structs
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            if (!data_stmt->is_foreign) {
                transpileDataDeepClonePrototype(*data_stmt);
            }
        }
        // 2. Enums
        else if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
            transpileEnumDeepClonePrototype(*enum_stmt);
        }
    }

    // Pass 6: Generate all public API function prototypes.
    pass_2_generate_declarations(statements, module_name);

    // Pass 7: Close the include guard at the very end.
    *m_current_out << "\n#endif // " << header_guard << "\n";


    // --- SOURCE FILE GENERATION ---
    m_current_out = &m_source_out;
    m_indent_level = 0;

    *m_current_out << "#include \"" << module_name << ".h\"\n\n";

    (*m_current_out) << "// --- Data Constructor Implementations ---\n";
    for (const auto& stmt : statements) {
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            if (!data_stmt->is_foreign) {
                transpileDataConstructor(*data_stmt);
            }
        }
    }

    (*m_current_out) << "\n// --- Data Equals Function Implementations ---\n";
    for (const auto& stmt : statements) {
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            if (!data_stmt->is_foreign) {
                transpileDataEqualsImplementation(*data_stmt);
            }
        }
    }

    (*m_current_out) << "\n// --- Data Clone Function Implementations ---\n";
    for (const auto& stmt : statements) {
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            if (!data_stmt->is_foreign) {
                transpileDataCloneImplementation(*data_stmt);
            }
        }
    }

    (*m_current_out) << "\n// --- Enum Constructor Implementations ---\n";
    for (const auto& stmt : statements) {
        if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
            transpileEnumConstructors(*enum_stmt, false /* generate_prototype_only */);
        }
    }

    (*m_current_out) << "\n// --- Enum Equals Implementations ---\n";
    for (const auto& stmt : statements) {
        if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
            transpileEnumEqualsImplementation(*enum_stmt);
        }
    }

    (*m_current_out) << "\n// --- Deep Clone Implementations ---\n";
    for (const auto& stmt : statements) {
        // 1. Data Structs
        if (auto data_stmt = std::dynamic_pointer_cast<const DataStmt>(stmt)) {
            if (!data_stmt->is_foreign) {
                transpileDataDeepCloneImplementation(*data_stmt);
            }
        }
        // 2. Enums
        else if (auto enum_stmt = std::dynamic_pointer_cast<const EnumStmt>(stmt)) {
            transpileEnumDeepCloneImplementation(*enum_stmt);
        }
    }

    pass_3_generate_globals_and_implementations(statements, module_name);

    auto main_symbol = m_type_checker.m_symbols.resolve("main");
    if (main_symbol) {
        pass_5_generate_main(statements, module_name, all_module_names);
    }

    if (m_hadError) return {};

    std::string final_source = m_source_out.str();
    if (main_symbol) {
        final_source += m_main_body.str();
    }

    if (m_hadError) return {};
    return {m_header_out.str(), final_source};
}

} // namespace angara