#pragma once

// Forward declarations — don't pull in heavy headers here.
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

namespace angara {

// Forward declarations from the AST.
struct Stmt;
struct Expr;
struct FuncStmt;
struct DataStmt;
struct ClassStmt;
class TypeChecker;
class ErrorHandler;
struct Token;

/// The Chaperone pass — compile-time memory verification.
///
/// Runs after the TypeChecker, before Codegen. Analyzes the typed AST and
/// verifies that every tracked allocation (class instance or `owned` type) is
/// explicitly dropped or ownership-transferred on every control-flow path.
/// Auto-inserts drops on exception-unwind paths. Reports diagnostics for leaks,
/// use-after-free, double-free, and reference cycles.
class Chaperone {
public:
    /// Runs the pass on the entire program.
    static bool run(const std::vector<std::shared_ptr<Stmt>>& program,
                    const TypeChecker& tc,
                    ErrorHandler& eh);

private:
    // --- Allocation state (the abstract domain) ---
    enum class State {
        Uninit,   // declared but not yet assigned a tracked value
        Live,     // holds a tracked allocation — must drop or transfer
        Dropped,  // explicitly dropped — use-after-free if accessed
        Escaped,  // ownership transferred (return, function call, global)
        Moved,    // ownership moved to another variable (let x = y / x = y);
                  // using or dropping it is an error (use-after-move / double-free)
    };

    /// State map: variable name → current state. Snapshotted at branch points.
    using StateMap = std::map<std::string, State>;

    /// Join two states at a CFG merge point (conservative).
    static State join(State a, State b);

    /// Join two state maps (element-wise).
    static StateMap join_maps(const StateMap& a, const StateMap& b);

    // --- Function summaries (interprocedural) ---
    // A summary is POSITIONAL: summary[i] is the ownership behavior of the
    // parameter that the i-th call argument binds to. `this` is implicit
    // (never a call argument), so the vector aligns to the call-arg-visible
    // params. This makes call-site matching trivial (arg i → summary[i]) and
    // correct for multi-param functions (S5); the old name-keyed map forced a
    // "single tracked param" shortcut and escaped all args otherwise.
    enum class ParamBehavior { Borrowed, Dropped, Escaped };
    using FunctionSummary = std::vector<ParamBehavior>;

    // --- Analysis context (must be before diag which takes Context&) ---
    struct Context {
        const TypeChecker& tc;
        ErrorHandler& eh;
        std::set<std::string> tracked_types;
        std::string current_function;
        std::map<std::string, FunctionSummary> summaries;
        bool in_unsafe = false;
        // During the interprocedural fixed-point convergence passes, suppress
        // diagnostics (they'd duplicate); emit only on the final pass.
        bool suppress_diag = false;

        Context(const TypeChecker& t, ErrorHandler& e)
            : tc(t), eh(e) {}
    };

    // --- Helper: report as error (normal) or warning (inside @unsafe) ---
    static void diag(Context& ctx, const Token& tok,
                     const std::string& msg, const std::string& code);

    // --- Phase 1: Collect tracked types ---
    static void collectTrackedTypes(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& program);

    // --- Phase 2: Per-function data-flow analysis ---
    static void analyzeFunction(Context& ctx, const struct FuncStmt& func);

    /// Analyze a block of statements, threading the state map through.
    /// Returns the state map at the end of the block.
    /// @param terminates  Set to true if the block ends with return/throw/break/continue.
    static StateMap analyzeBlock(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& statements,
        StateMap state, bool& terminates);

    /// Analyze a single statement. Mutates `state` in place.
    /// Sets `terminates` if control flow doesn't fall through.
    static void analyzeStmt(Context& ctx,
        const std::shared_ptr<Stmt>& stmt,
        StateMap& state, bool& terminates);

    /// Walk an expression tree and report E502 for any reference to a
    /// variable in the Dropped state (use-after-free). Also transitions
    /// tracked argument states based on callee summaries (interprocedural).
    static void analyzeExpr(Context& ctx,
        const std::shared_ptr<struct Expr>& expr,
        StateMap& state);

    // --- Phase 3: Throw-path leak detection (reports E501, no auto-drop) ---
    // (inline in analyzeStmt's ThrowStmt handler)

    // --- Phase 4: Cycle detection ---
    static void detectCycles(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& program);

    // --- Type helpers ---
    static bool isTrackedType(Context& ctx, const std::string& type_name);
    static bool isTrackedVar(Context& ctx, const struct VarDeclStmt& var);
};

} // namespace angara
