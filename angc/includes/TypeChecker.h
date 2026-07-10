#pragma once

#include "Expr.h"
#include "Stmt.h"
#include "ASTTypes.h"
#include "ErrorHandler.h"
#include "SymbolTable.h"
#include "Type.h"
#include <stack>

#include "CompilerDriver.h"
#include "StringUtils.h"

namespace angara {

    /// Tracks a native symbol used from an imported module, for linker resolution.
    struct UsedNativeSymbol {
        std::shared_ptr<ModuleType> from_module;
        std::string symbol_name;
        std::shared_ptr<Type> symbol_type;

        bool operator<(const UsedNativeSymbol& other) const {
            if (from_module->name != other.from_module->name) return from_module->name < other.from_module->name;
            return symbol_name < other.symbol_name;
        }
    };

    /// Walks the AST after parsing and verifies that all types are correct before code generation.
    /// Implements a multi-pass approach: resolve imports, declare headers, define signatures, then check bodies.
    class TypeChecker : public ExprVisitor, public StmtVisitor {
    public:
        /// Constructs a TypeChecker for the given compilation module.
        /// @param driver        The compiler driver, used for cross-module resolution.
        /// @param errorHandler  Error reporter for diagnostics.
        /// @param module_name   Name of the module being type-checked.
        TypeChecker(CompilerDriver& driver, ErrorHandler& errorHandler, const std::string& module_name);

        /// Runs the full multi-pass type-checking pipeline over the given top-level statements.
        /// @return True if type checking succeeded with no errors.
        bool check(const std::vector<std::shared_ptr<Stmt>>& statements);

        /// Visits an @unsafe block, enabling dynamic operations within.
        void visit(std::shared_ptr<const UnsafeBlockStmt> stmt) override;

        /// v5: visits a `drop` statement — marks the variable as dropped.
        void visit(std::shared_ptr<const DropStmt> stmt) override;

        /// Returns the symbol table (for use by later compiler stages).
        [[nodiscard]] const SymbolTable& getSymbolTable() const;
        /// L2: Non-const accessor — avoids const_cast in Chaperone and other passes
        /// that need to call resolve() (which marks symbols as used).
        [[nodiscard]] SymbolTable& getSymbolTable() { return m_symbols; }

        /// Returns the module type descriptor (for use by later compiler stages).
        [[nodiscard]] std::shared_ptr<ModuleType> getModuleType() const;

        /// Converts an AST type node into a semantic Type object.
        /// @param ast_type  The AST type annotation to resolve.
        /// @return The resolved semantic type, or an error type if resolution fails.
        std::shared_ptr<Type> resolveType(const std::shared_ptr<ASTType>& ast_type);

        /// Returns the map of expression nodes to their resolved types (for code generation).
        [[nodiscard]] const std::map<const Expr*, std::shared_ptr<Type>>& getExpressionTypes() const { return m_expression_types; }

        /// Returns the map of variable declarations to their resolved types.
        [[nodiscard]] const std::map<const VarDeclStmt*, std::shared_ptr<Type>>& getVariableTypes() const { return m_variable_types; }

        /// Returns the map of variable references to their resolved symbols.
        [[nodiscard]] const std::map<const VarExpr*, std::shared_ptr<Symbol>>& getVariableResolutions() const { return m_variable_resolutions; }

        /// Returns the map of attach statements to their resolved module types.
        [[nodiscard]] const std::map<const AttachStmt*, std::shared_ptr<ModuleType>>& getModuleResolutions() const { return m_module_resolutions; }

        /// TS-1/C4: arg indices to box into trait objects at a generic-fn call.
        [[nodiscard]] const std::map<const CallExpr*, std::vector<std::pair<size_t, std::shared_ptr<TraitType>>>>& getGenericBoxedArgs() const { return m_generic_boxed_args; }

        // LANG-11: resolved (reordered + defaults-filled) call args, keyed by CallExpr.
        [[nodiscard]] const std::map<const CallExpr*, std::vector<std::shared_ptr<Expr>>>& getResolvedArgs() const { return m_resolved_args; }

        /// Returns the set of native symbols used from imported modules.
        [[nodiscard]] const std::set<UsedNativeSymbol>& getUsedNativeSymbols() const { return m_used_native_symbols; }

        /// Returns true if the checker is currently inside an @unsafe block.
        [[nodiscard]] bool isInUnsafeContext() const { return m_is_in_unsafe_context; }

        /// Enables --kernel mode (hard-error try/spawn/Mutex/native-attach).
        void set_kernel_mode(bool v) { m_is_in_kernel_mode = v; }

        /// Enables --freestanding mode (hard-error try/throw/spawn/Mutex/native-
        /// attach: no heap/threading/dynamic-loader on bare metal).
        void set_freestanding_mode(bool v) { m_is_in_freestanding_mode = v; }

    private:
        // --- Expression visitors ---

        std::any visit(const Literal& expr) override;
        std::any visit(const Binary& expr) override;
        std::any visit(const VarExpr& expr) override;
        std::any visit(const Unary& expr) override;
        std::any visit(const Grouping& expr) override;
        std::any visit(const ListExpr& expr) override;
        std::any visit(const AssignExpr &expr) override;
        std::any visit(const UpdateExpr &expr) override;
        std::any visit(const CallExpr &expr) override;
        std::any visit(const GetExpr &expr) override;
        std::any visit(const LogicalExpr &expr) override;
        std::any visit(const SubscriptExpr &expr) override;
        std::any visit(const RecordExpr &expr) override;
        std::any visit(const TernaryExpr &expr) override;
        std::any visit(const ThisExpr &expr) override;
        std::any visit(const SuperExpr &expr) override;
        std::any visit(const MatchExpr& expr) override;
        std::any visit(const LambdaExpr& expr) override;
        std::any visit(const IsExpr &expr) override;
        std::any visit(const CastExpr &expr) override;
        std::any visit(const DerefExpr &expr) override;
        std::any visit(const RangeExpr& expr) override;
        std::any visit(const InterpStringExpr& expr) override;
        std::any visit(const TupleExpr& expr) override;  // LANG-10
        std::any visit(const AwaitExpr& expr) override;  // LIB-4
        std::any visit(const AsmExpr& expr) override;    // inline assembly
        std::any visit(const NestedPattern& expr) override;

        // --- Statement visitors ---

        void visit(std::shared_ptr<const ContractStmt> stmt) override;
        void visit(std::shared_ptr<const VarDeclStmt> stmt) override;
        void visit(std::shared_ptr<const IfStmt> stmt) override;
        void visit(std::shared_ptr<const EmptyStmt> stmt) override;
        void visit(std::shared_ptr<const WhileStmt> stmt) override;
        void visit(std::shared_ptr<const ForStmt> stmt) override;
        void visit(std::shared_ptr<const ForInStmt> stmt) override;
        void visit(std::shared_ptr<const FuncStmt> stmt) override;
        void visit(std::shared_ptr<const ReturnStmt> stmt) override;
        void visit(std::shared_ptr<const AttachStmt> stmt) override;
        void visit(std::shared_ptr<const ThrowStmt> stmt) override;
        void visit(std::shared_ptr<const TryStmt> stmt) override;
        void visit(std::shared_ptr<const ClassStmt> stmt) override;
        void visit(std::shared_ptr<const TraitStmt> stmt) override;
        void visit(std::shared_ptr<const ExpressionStmt> stmt) override;
        void visit(std::shared_ptr<const BlockStmt> stmt) override;
        void visit(std::shared_ptr<const EnumStmt> stmt) override;
        void visit(std::shared_ptr<const DataStmt> stmt) override;
        void visit(std::shared_ptr<const BreakStmt> stmt) override;
        void visit(std::shared_ptr<const ContinueStmt> stmt) override;
        void visit(std::shared_ptr<const TypeAliasStmt> stmt) override;

        // --- Header definition passes ---

        /// Populates the EnumType with its variants and constructor signatures.
        void defineEnumHeader(const EnumStmt &stmt);

        /// Populates the ContractType with its required fields and methods.
        void defineContractHeader(const ContractStmt &stmt);

        /// Populates the TraitType with its required method signatures.
        void defineTraitHeader(const TraitStmt &stmt);

        /// Populates the ClassType with fields, methods, superclass link, and validates contracts/traits.
        void defineClassHeader(const ClassStmt &stmt);

        /// Creates the FunctionType signature and declares it in the symbol table.
        void defineFunctionHeader(const FuncStmt &stmt);

        /// Populates the DataType with fields and constructor signature.
        void defineDataHeader(const DataStmt &stmt);

        /// Resolves an attach/import statement, loading the module and declaring imported symbols.
        void resolveAttach(const AttachStmt &stmt);

        /// Resolves the RHS type and registers the alias name in the symbol table.
        void defineTypeAliasHeader(const TypeAliasStmt &stmt);

        // M11: auto-derive Send/Sync on all ClassType and DataType instances
        // after headers are defined. Runs a fixed-point over field types.
        void deriveSendAndSync(const std::vector<std::shared_ptr<Stmt>>& statements);

        // --- Type helpers ---

        /// M9: Returns a module-qualified key for function-level maps to prevent
        /// collisions between same-named functions in different modules.
        /// Top-level: "module::func", Methods: "module::Class.method"
        [[nodiscard]] std::string qualifiedFunctionKey(const std::string& base_name) const {
            if (m_module_name.empty()) return base_name;
            return m_module_name + "::" + base_name;
        }

        /// Returns true if the type can be used in a boolean context.
        bool isTruthy(const std::shared_ptr<Type> &type);

        /// Pops the top type from the internal type stack (used to pass types up from expressions).
        /// @return The popped type, or an error type if the stack is empty.
        std::shared_ptr<Type> popType();

        /// Pushes a type onto the internal stack and records it in the expression type map.
        void pushAndSave(const Expr *expr, const std::shared_ptr<Type>& type);

        /// Checks whether `actual` can be assigned where `expected` is required.
        /// `narrowing_literal` (optional) is the source expression when it's a bare
        /// integer literal — used by TS-3 to permit in-range literal narrowing.
        bool check_type_compatibility(const std::shared_ptr<Type> &expected, const std::shared_ptr<Type> &actual,
                                      const Literal* narrowing_literal = nullptr);

        /// Checks structural compatibility between a DataType and a RecordType.
        bool check_structural_match(const std::shared_ptr<DataType>& data_type,
                                    const std::shared_ptr<RecordType>& record_type);

        /// TS-3: true if `lit` is an integer literal whose value fits in `target`.
        bool narrowing_literal_fits(const std::shared_ptr<Type>& target,
                                    const std::shared_ptr<Type>& source,
                                    const Literal* lit);

        /// TS-2: does `subject` conform to `trait`? A marker trait (no methods)
        /// is satisfied by any type; a trait with methods requires the subject be
        /// a class instance whose methods match the trait's signatures.
        bool conformsToTrait(const std::shared_ptr<Type>& subject,
                             const std::shared_ptr<TraitType>& trait);

        /// M8: recursively validate a substituted type tree, ensuring that any
        /// RawArray types have only primitive element types (TYPE_PARAM is also
        /// allowed as it will be further substituted). Emits errors via error().
        /// Returns true if the type is valid.
        bool validate_substituted_type(const std::shared_ptr<Type>& type,
                                       const Token& token);

        /// M8: re-check a generic function body with concrete type arguments
        /// substituted for type parameters. Called at each monomorphized call
        /// site to catch body-level type errors that TYPE_PARAM permissiveness
        /// may have hidden during the initial type-check. Returns true if the
        /// body passes re-checking (no new errors).
        bool recheck_generic_body(const std::string& func_name,
                                  const std::map<std::string, std::shared_ptr<Type>>& type_args,
                                  const Token& error_token);

        /// TS-1: does the concrete `subject` type adopt the `iface` (a TraitType
        /// or ContractType)? Returns false for non-instance subjects. Used to
        /// decide whether a concrete value may flow into a trait/contract slot.
        bool adoptsInterface(const std::shared_ptr<Type>& subject,
                             const std::shared_ptr<Type>& iface);

        /// TS-6: true iff every control-flow path through `stmt` ends in a
        /// `return` or `throw` (control cannot fall off the end). Conservative.
        bool definitelyReturns(const std::shared_ptr<const Stmt>& stmt);

        /// Validates a `spawn()` call: first arg must be a function, remaining args match its parameters.
        void check_spawn_call(const CallExpr &call, const std::vector<std::shared_ptr<Type>> &arg_types);

        /// Validates a standard function/method call against its signature (arity + argument types).
        /// @param arg_exprs  Optional: resolved argument expressions (LANG-11 defaults).
        ///                   When provided, used instead of call.arguments for literal narrowing.
        void check_function_call(const CallExpr &call, const std::shared_ptr<FunctionType> &func_type,
                                 const std::vector<std::shared_ptr<Type>> &arg_types,
                                 const std::vector<std::shared_ptr<Expr>>* arg_exprs = nullptr);

        // LANG-11: resolve named arguments + fill defaults. Returns a vector of
        // expressions in parameter order (size == param_count). Missing args that
        // have defaults are filled; missing required args return nullptr (caller
        // reports the error via existing arity checks).
        std::vector<std::shared_ptr<Expr>> resolveCallArgs(
            const CallExpr& call,
            const std::string& callee_key,
            size_t param_count);

        /// Resolves a variable reference, applying type narrowing from `if is` checks if applicable.
        std::shared_ptr<Symbol> resolve_and_narrow(const VarExpr &expr);

        // --- Error reporting ---

        /// Reports a type-checking error.
        void error(const Token& token, const std::string& message, const std::string& code = "");

        /// Reports a type-checking warning.
        void warning(const Token& token, const std::string& message, const std::string& code = "");

        /// Reports a supplementary note attached to the previous diagnostic.
        void note(const Token &token, const std::string &message);

        /// Exits the current scope and warns about any unused local variables.
        void exitScopeAndWarn();

        /// Searches candidates for close matches to a misspelled name and emits a "did you mean?" note.
        void find_and_report_suggestion(const Token& bad_token, const std::vector<std::string>& candidates);

        /// Recursively extracts type parameter bindings by matching a pattern type
        /// (containing TypeParameterType) against a concrete type.
        static void extract_type_args(const std::shared_ptr<Type>& pattern,
                                      const std::shared_ptr<Type>& concrete,
                                      std::map<std::string, std::shared_ptr<Type>>& inferred);

        // --- State ---

        ErrorHandler& m_errorHandler;
        std::stack<std::shared_ptr<Type>> m_type_stack;
        bool m_hadError = false;
        int m_loop_depth = 0;
        bool m_is_in_trait = false;
        bool m_in_async_function = false;  // LIB-4: true while visiting an async func body

        std::shared_ptr<Type> m_type_i8, m_type_i16, m_type_i32, m_type_i64;
        std::shared_ptr<Type> m_type_u8, m_type_u16, m_type_u32, m_type_u64;
        std::shared_ptr<Type> m_type_f32, m_type_f64;
        std::shared_ptr<Type> m_type_bool;
        std::shared_ptr<Type> m_type_string;
        std::shared_ptr<Type> m_type_char;  // LANG-4
        std::shared_ptr<Type> m_type_nil;
        std::shared_ptr<Type> m_type_any;
        std::shared_ptr<Type> m_type_error;
        std::shared_ptr<Type> m_type_thread;
        std::shared_ptr<Type> m_type_mutex;
        std::shared_ptr<TraitType> m_type_send;   // M11: built-in Send marker trait
        std::shared_ptr<TraitType> m_type_sync;   // M11: built-in Sync marker trait
        std::shared_ptr<Type> m_type_exception;
        CompilerDriver& m_driver;
        std::shared_ptr<ModuleType> m_module_type;
        std::string m_module_name;  // M9: stored for qualified function keys
        std::stack<std::shared_ptr<Type>> m_function_return_types;
        std::shared_ptr<ClassType> m_current_class = nullptr;
        std::map<const Symbol*, std::shared_ptr<Type>> m_narrowed_types;
        std::map<std::string, std::shared_ptr<Type>> m_active_type_params;
        // TS-1/C4: active generic-body bounds — type-param name -> the TraitType
        // it's bound to (e.g. T -> Drawable for `func f<T: Drawable>`). Lets a
        // generic body resolve `x.draw()` against the bound and dispatch via the
        // trait-object vtable. Empty for unbounded type params.
        std::map<std::string, std::shared_ptr<TraitType>> m_active_type_param_bounds;
        // TS-1/C4: for a generic-function call with bounded params, the args
        // that must be boxed into trait objects at the call site (so the body
        // receives a trait object and indirect dispatch works without
        // monomorphization). Each entry: (arg index, the bound TraitType).
        // Keyed by the CallExpr pointer.
        std::map<const CallExpr*, std::vector<std::pair<size_t, std::shared_ptr<TraitType>>>> m_generic_boxed_args;
        // TS-1/C4: a function's resolved param-bounds, keyed by function name ->
        // (param index -> bound TraitType). Populated in defineFunctionHeader so
        // call sites can determine which args to box without a Symbol->FuncStmt
        // back-reference.
        std::map<std::string, std::map<size_t, std::shared_ptr<TraitType>>> m_function_bounds;
        // M9: function_name -> (type_param_name -> bound TraitType).
        // Populated in defineFunctionHeader; used by call sites to verify
        // that inferred concrete type args satisfy their declared bounds.
        std::map<std::string, std::map<std::string, std::shared_ptr<TraitType>>> m_function_type_param_bounds;

        // M8: stored generic function ASTs for full body re-checking at call
        // sites. Keyed by function name (or "Class.method" for methods).
        std::map<std::string, std::shared_ptr<const FuncStmt>> m_generic_func_stmts;

        // LANG-11: function_name -> vector of default expressions indexed by param position.
        // nullptr entries mean no default for that parameter.
        std::map<std::string, std::vector<std::shared_ptr<Expr>>> m_function_defaults;
        // LANG-11: function_name -> parameter names (for named argument resolution).
        std::map<std::string, std::vector<std::string>> m_function_param_names;
        // LANG-11: resolved (reordered + defaults-filled) args per call site.
        std::map<const CallExpr*, std::vector<std::shared_ptr<Expr>>> m_resolved_args;

        // --- Mapped state (populated during type checking, read by backend) ---

        std::map<const Expr*, std::shared_ptr<Type>> m_expression_types;
        SymbolTable m_symbols;
        std::map<const VarDeclStmt*, std::shared_ptr<Type>> m_variable_types;
        std::map<const VarExpr*, std::shared_ptr<Symbol>> m_variable_resolutions;
        std::map<const AttachStmt*, std::shared_ptr<ModuleType>> m_module_resolutions;
        std::set<UsedNativeSymbol> m_used_native_symbols;
        bool m_is_in_unsafe_context = false;
        bool m_is_in_kernel_mode = false;   // --kernel: hard-error try/spawn/Mutex/native-attach
        bool m_is_in_freestanding_mode = false;  // --freestanding: hard-error heap/threading/native-attach

        // Downward-flowing expected type for bidirectional inference (e.g., list<i64> -> [] element type)
        std::shared_ptr<Type> m_expected_type;
    };

}
