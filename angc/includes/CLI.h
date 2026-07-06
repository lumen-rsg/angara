#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace angara {

// Version constants — used by --version, help text, and LSP.
inline constexpr const char* ANGC_VERSION    = "5.1.0";
inline constexpr const char* BACKEND_VERSION = "4.1.0";
inline constexpr const char* ANGARA_SPEC     = "v3.1.2";

/// Returns the Angara installation directory.
/// Checks $ANGARA_HOME first; falls back to /opt/angara.
inline std::string angara_home() {
    const char* env = std::getenv("ANGARA_HOME");
    return env ? std::string(env) : "/opt/angara";
}

/// Parsed CLI flags (target triple, output, warnings, etc.).
struct CliFlags {
    std::string target;
    std::string sysroot;
    std::string output_name;
    std::vector<std::string> link_files;
    bool dump_ast = false;
    bool dump_ir = false;
    bool emit_llvm = false;
    bool freestanding = false;
    bool nostdlib = false;
    bool release = false;
    bool debug = false;
    bool verbose_flag = false;
    bool wall = false;
    bool werror = false;
    std::vector<std::string> suppress_warnings;
    std::string error_format = "text";
    int jobs = 0;           // TOOL-2: 0 = auto (hardware_concurrency)
    bool force_rebuild = false;  // TOOL-2: ignore incremental cache
};

// Free functions (declared here, defined in CliUtils.cpp / HelpText.cpp / etc.)
std::string get_host_os_triple_suffix();
std::string resolve_target_triple(const std::string& input);
std::string find_local_project_file();
bool is_an_file(const std::string& s);
void print_help();
void print_version();
void list_modules();

/// The Angara compiler CLI. Mirrors the REPL/LSPServer pattern:
/// one public run() entry point, private command handlers.
class CLI {
public:
    int run(int argc, char** argv);

    /// Returns the parsed CLI flags (read-only).
    const CliFlags& getFlags() const { return m_flags; }

private:
    // Shared state
    bool m_verbose = false;
    CliFlags m_flags;

    void parseFlags(std::vector<std::string>& args);
    void verbose(const std::string& msg);

    // Compile commands
    int cmdCheck(const std::string& file);
    int cmdDumpAst(const std::string& file);
    int cmdCompileSingleFile(const std::string& source_file);
    int cmdPathBuild(std::vector<std::string>& args);
    int handleCheck(std::vector<std::string> args);

    // Build commands
    int handleNoArgs();
    int handleInit(const std::vector<std::string>& args);
    int handleRun(const std::vector<std::string>& args);
    int handleClean(const std::vector<std::string>& args);
    int handlePublish(const std::vector<std::string>& args);
    int handlePackage(const std::vector<std::string>& args);

    // Other commands
    int handleFmt(std::vector<std::string> args);
    int handleExplain(std::vector<std::string> args);
    int handleWatch(std::vector<std::string> args);
    int handleTest(std::vector<std::string> args);
    int handleLsp();
    int handleRepl();
};

} // namespace angara
