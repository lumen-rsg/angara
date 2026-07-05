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
/// Reports diagnostics for leaks, use-after-free, double-free, dangling
/// borrows, and reference cycles. Exception paths are handled inline in the
/// ThrowStmt/TryStmt handlers (E501 on throw; `finally {}` discharges the
/// obligation) — there is no auto-unwind; cleanup is the programmer's job.
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
        // The tracked-parameter names of the function currently being analyzed.
        // Leak checks on return/throw must EXCLUDE these — a parameter is
        // borrowed (the caller owns it); flagging it as a leak is a false
        // positive. Reset per function in analyzeFunction.
        std::set<std::string> current_params;
        std::map<std::string, FunctionSummary> summaries;
        // H2: closure call summaries. Keyed by FunctionType pointer — each LambdaExpr
        // creates a unique FunctionType that flows through variable assignments,
        // giving a stable lookup key for closure call sites.
        std::map<const struct Type*, FunctionSummary> closure_summaries;
        bool in_unsafe = false;
        // During the interprocedural fixed-point convergence passes, suppress
        // diagnostics (they'd duplicate); emit only on the final pass.
        bool suppress_diag = false;
        // S8: names a surrounding `finally {}` guarantees to drop. A throw
        // inside such a try discharges its leak obligation for these names
        // (the finally runs on the throw path), so they aren't false-flagged.
        std::set<std::string> finally_protected;
        // S3: borrow relationships — ref_name → referent_name. When the
        // referent is dropped/moved while a ref still aliases it, the ref
        // dangles (E509). Tracked within the current function's analysis.
        std::map<std::string, std::string> borrows;
        // M2: names of variables that were Live before entering the current
        // loop body. Used to detect conditional destruction inside loops:
        // if a pre-loop-Live variable is destroyed on one branch of an if/else
        // but not the other, it won't be available on the next iteration.
        std::set<std::string> loop_pre_live;
        // Phase B: names of variables whose type is a built-in heap-allocated
        // type (string, list, record, etc.) as opposed to a class or owned data.
        // Leak diagnostics for these variables use W521 (warning) instead of
        // E501 (error), giving developers visibility without breaking compilation.
        std::set<std::string> builtin_heap_vars;

        Context(const TypeChecker& t, ErrorHandler& e)
            : tc(t), eh(e) {}
    };

    // --- Helper: report as error (normal) or warning (inside @unsafe) ---
    static void diag(Context& ctx, const Token& tok,
                     const std::string& msg, const std::string& code);

    // --- Helper: always emit a warning (Phase B: built-in type leaks) ---
    static void warn(Context& ctx, const Token& tok,
                     const std::string& msg, const std::string& code);

    // --- Phase 1: Collect tracked types ---
    static void collectTrackedTypes(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& program);

    // --- Phase 2: Per-function data-flow analysis ---
    /// @param summary_key  Qualified key for summary storage (e.g., "funcName"
    ///                      or "ClassName.methodName"). C3: prevents collision
    ///                      between same-named methods in different classes.
    static void analyzeFunction(Context& ctx, const struct FuncStmt& func,
                                const std::string& summary_key);

    /// Analyze a block of statements, threading the state map through.
    /// Returns the state map at the end of the block.
    /// @param terminates  Set to true if the block ends with return/throw/break/continue.
    /// @param declared_names  If non-null, filled with names of variables declared
    ///                         inside this block (for scope cleanup).
    static StateMap analyzeBlock(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& statements,
        StateMap state, bool& terminates,
        std::set<std::string>* declared_names = nullptr);

    /// Like analyzeBlock but enforces lexical scoping: variables declared inside
    /// the block are removed from the state map (or restored to their pre-block
    /// value if they shadow an outer variable) when the block exits.
    static StateMap analyzeScopedBlock(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& statements,
        StateMap state, bool& terminates);

    /// Like analyzeStmt but enforces lexical scoping for a single statement
    /// (used when a branch/body is a single statement instead of a block).
    static void analyzeScopedStmt(Context& ctx,
        const std::shared_ptr<Stmt>& stmt,
        StateMap& state, bool& terminates);

    /// Analyze a single statement. Mutates `state` in place.
    /// Sets `terminates` if control flow doesn't fall through.
    /// @param declared_names  If non-null, filled with names of variables declared
    ///                         by this statement (for scope cleanup).
    static void analyzeStmt(Context& ctx,
        const std::shared_ptr<Stmt>& stmt,
        StateMap& state, bool& terminates,
        std::set<std::string>* declared_names = nullptr);

    /// Walk an expression tree and report E502 for any reference to a
    /// variable in the Dropped state (use-after-free). Also transitions
    /// tracked argument states based on callee summaries (interprocedural).
    static void analyzeExpr(Context& ctx,
        const std::shared_ptr<struct Expr>& expr,
        StateMap& state);

    /// E505: flag a tracked Live value escaping into an untracked container.
    /// @param container_type  Optional resolved type of the container; if tracked,
    ///                         the check is skipped (Phase B: lists/records are tracked).
    static void checkEscapeIntoContainer(Context& ctx,
        const std::shared_ptr<struct Expr>& elem, StateMap& state,
        const struct Type* container_type = nullptr);

    /// Collect every VarExpr name referenced in a statement / expression tree
    /// (used to find closure captures).
    static void collectVarRefs(const std::shared_ptr<struct Stmt>& stmt,
        std::set<std::string>& out);
    static void collectExprVarRefs(const std::shared_ptr<struct Expr>& expr,
        std::set<std::string>& out);

    // --- Exception paths: throw-path leak detection (E501) + finally discharge ---
    // Handled inline in analyzeStmt's ThrowStmt and TryStmt handlers — no
    // separate phase. The v5 model reports leaks on throw (the programmer
    // cleans up via finally {}); the old auto-unwind was removed (cde8e23).

    // --- Phase 4: Cycle detection ---
    static void detectCycles(Context& ctx,
        const std::vector<std::shared_ptr<Stmt>>& program);

    // --- Type helpers ---
    static bool isTrackedType(Context& ctx, const std::string& type_name);
    /// Whether a resolved Type object is tracked (unwraps optionals).
    static bool isTrackedTypeObj(Context& ctx, const struct Type& type);
    static bool isTrackedVar(Context& ctx, const struct VarDeclStmt& var);
    /// Whether a VarExpr is a ref<T> (a non-owning borrow), via the type checker.
    static bool isRefVarExpr(Context& ctx, const struct VarExpr& ve);
    /// Whether a resolved Type object is a heap-allocated type (tracked or
    /// built-in) — used to allow `drop` on any heap value, not just tracked ones.
    static bool isHeapAllocatedType(Context& ctx, const struct Type& type);
    /// Whether a resolved (non-optional, non-ref) Type is a built-in heap type
    /// (string, list, record, etc.) as opposed to a class or owned data.
    static bool isBuiltinHeapType(const struct Type& type);
};

} // namespace angara
