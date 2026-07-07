//
// Pipeline test harness for Angara compiler unit tests.
//
// Encapsulates the full Lex → Parse → TypeCheck pipeline (and optionally
// Chaperone) so that TypeChecker and Chaperone behavior can be verified
// on real (but minimal) Angara programs from C++ unit tests.
//
// Usage:
//   #include "test_pipeline.h"
//   TEST("my test") {
//       PipelineHarness h("func main() { let x = 42; }");
//       ASSERT_TRUE(h.parse());
//       ASSERT_TRUE(h.typeCheck());
//       ASSERT_EQ(h.errorCount(), 0);
//   }
//

#ifndef ANGARA_TEST_PIPELINE_H
#define ANGARA_TEST_PIPELINE_H

#include "test_harness.h"

#include "Lexer.h"
#include "Parser.h"
#include "ErrorHandler.h"
#include "CompilerDriver.h"
#include "TypeChecker.h"
#include "Chaperone.h"
#include "Stmt.h"
#include "Expr.h"

#include <memory>
#include <string>
#include <vector>

namespace angara::test {

/// Wraps the full compilation pipeline up through type-checking (and
/// optionally Chaperone) for a single in-memory Angara source module.
///
/// Member declaration order is intentional: ErrorHandler and CompilerDriver
/// must outlive TypeChecker, which holds references to both.
class PipelineHarness {
public:
    /// Constructs the harness with the given source code.
    /// @param source      Angara source code to compile.
    /// @param moduleName  Name for the synthetic module (default: "test").
    explicit PipelineHarness(const std::string& source,
                             const std::string& moduleName = "test")
        : m_source(source)
        , m_errorHandler(source)
        , m_driver()
        , m_typeChecker(m_driver, m_errorHandler, moduleName)
        , m_moduleName(moduleName)
    {}

    /// Lexes and parses the source.  Must be called before typeCheck().
    /// @return true if parsing succeeded with no syntax errors.
    bool parse() {
        Lexer lexer(m_source,
                    std::make_shared<std::string>(m_moduleName + ".an"),
                    m_errorHandler);
        m_tokens = lexer.scanTokens();
        if (m_errorHandler.hadError()) {
            m_parseOk = false;
            return false;
        }
        Parser parser(m_tokens, m_errorHandler);
        m_ast = parser.parseStmts();
        m_parseOk = !m_errorHandler.hadError();
        return m_parseOk;
    }

    /// Runs the TypeChecker on the parsed AST.  parse() must be called first.
    /// @return true if type-checking succeeded with no errors.
    bool typeCheck() {
        if (!m_parseOk) return false;
        m_typeCheckOk = m_typeChecker.check(m_ast);
        return m_typeCheckOk;
    }

    /// Runs the Chaperone memory-safety analysis.  typeCheck() must have
    /// succeeded first (Chaperone reads from TypeChecker's populated maps).
    /// @return true if the Chaperone reported no errors.
    bool runChaperone() {
        if (!m_typeCheckOk) return false;
        // Chaperone::run reports diagnostics through m_errorHandler.
        // We track its return value separately so tests can distinguish
        // type-check failures from chaperone failures.
        m_chaperoneOk = Chaperone::run(m_ast, m_typeChecker, m_errorHandler);
        return m_chaperoneOk;
    }

    // ── Accessors ──

    const std::vector<std::shared_ptr<Stmt>>& ast() const { return m_ast; }
    const TypeChecker& typeChecker() const { return m_typeChecker; }
    const ErrorHandler& errors() const { return m_errorHandler; }
    ErrorHandler& errors() { return m_errorHandler; }

    int errorCount()   const { return m_errorHandler.errorCount(); }
    int warningCount() const { return m_errorHandler.warningCount(); }
    bool hadParseError()     const { return !m_parseOk; }
    bool hadTypeCheckError() const { return !m_typeCheckOk; }
    bool hadChaperoneError() const { return !m_chaperoneOk; }

    /// Returns true if the entire pipeline (parse + typecheck) passed.
    bool ok() const { return m_parseOk && m_typeCheckOk; }

private:
    // ── Members (declaration order matters — see class doc) ──

    std::string m_source;
    std::string m_moduleName;
    ErrorHandler m_errorHandler;       // 1st — TypeChecker refs this
    CompilerDriver m_driver;           // 2nd — TypeChecker refs this
    TypeChecker m_typeChecker;         // 3rd — refs m_driver + m_errorHandler

    std::vector<Token> m_tokens;
    std::vector<std::shared_ptr<Stmt>> m_ast;

    bool m_parseOk      = false;
    bool m_typeCheckOk  = false;
    bool m_chaperoneOk  = false;
};

} // namespace angara::test

#endif // ANGARA_TEST_PIPELINE_H
