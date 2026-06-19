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
    /// Maps each ThrowStmt (by pointer) to the tracked variable names that are
    /// Live at the throw point and must be auto-dropped before the longjmp.
    /// The codegen reads this in cgThrow.
    using DropPlan = std::map<const void*, std::vector<std::string>>;

    /// Runs the pass on the entire program.
    /// @param program    Root AST statements (all modules combined).
    /// @param tc         Completed type checker with resolved types.
    /// @param eh         Error handler for diagnostics.
    /// @param drop_plan  Output: throw-stmt → vars to auto-drop (for codegen).
    /// @return True if no errors were found.
    static bool run(const std::vector<std::shared_ptr<Stmt>>& program,
                    const TypeChecker& tc,
                    ErrorHandler& eh,
                    DropPlan& drop_plan);

private:
    // --- Allocation state (the abstract domain) ---
    enum class State {
        Uninit,   // declared but not yet assigned a tracked value
        Live,     // holds a tracked allocation — must drop or transfer
        Dropped,  // explicitly dropped — use-after-free if accessed
        Escaped,  // ownership transferred (return, function call, global)
    };

    /// State map: variable name → current state. Snapshotted at branch points.
    using StateMap = std::map<std::string, State>;

    /// Join two states at a CFG merge point (conservative).
    static State join(State a, State b);

    /// Join two state maps (element-wise).
    static StateMap join_maps(const StateMap& a, const StateMap& b);

    // --- Analysis context ---
    struct Context {
        const TypeChecker& tc;
        ErrorHandler& eh;
        std::set<std::string> tracked_types;  // names of class + owned types
        std::string current_function;
        DropPlan& drop_plan;

        Context(const TypeChecker& t, ErrorHandler& e, DropPlan& dp)
            : tc(t), eh(e), drop_plan(dp) {}
    };

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
    /// variable in the Dropped state (use-after-free).
    static void analyzeExpr(Context& ctx,
        const std::shared_ptr<struct Expr>& expr,
        const StateMap& state);

    // --- Phase 3: Exception unwinding ---
    /// Collect all Live tracked variables in scope, insert DropStmts before
    /// the given ThrowStmt in its enclosing BlockStmt.
    static void unwindAtThrow(Context& ctx,
        const StateMap& state,
        const Token& throw_tok,
        const void* throw_ptr);

    // --- Phase 4: Cycle detection ---
    static void detectCycles(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& program);

    // --- Type helpers ---
    static bool isTrackedType(Context& ctx, const std::string& type_name);
    static bool isTrackedVar(Context& ctx, const struct VarDeclStmt& var);
};

} // namespace angara
