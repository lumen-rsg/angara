//
// Unit tests for SymbolTable — scoped name resolution, declaration, shadowing.
// The SymbolTable is the core data structure used by the TypeChecker and
// Chaperone for variable/function/type resolution.
//

#include "test_harness.h"
#include "SymbolTable.h"
#include "CompilerDriver.h"  // for ModuleType

using namespace angara;

// ── Scope management ──

TEST(symboltable_initial_depth_zero) {
    SymbolTable st;
    ASSERT_EQ(st.getScopeDepth(), 0);
}

TEST(symboltable_enter_increases_depth) {
    SymbolTable st;
    st.enterScope();
    ASSERT_EQ(st.getScopeDepth(), 1);
    st.enterScope();
    ASSERT_EQ(st.getScopeDepth(), 2);
}

TEST(symboltable_exit_decreases_depth) {
    SymbolTable st;
    st.enterScope();
    st.enterScope();
    ASSERT_EQ(st.getScopeDepth(), 2);
    st.exitScope();
    ASSERT_EQ(st.getScopeDepth(), 1);
    st.exitScope();
    ASSERT_EQ(st.getScopeDepth(), 0);
}

TEST(symboltable_global_scope_never_popped) {
    SymbolTable st;
    // Popping beyond global scope should be a no-op or safe
    auto unused = st.exitScope();  // pop depth 0 -> stays at 0
    ASSERT_EQ(st.getScopeDepth(), 0);
    // Should still be usable
    auto sym = st.declare(Token{}, std::make_shared<PrimitiveType>("i64"), false);
    ASSERT_TRUE(sym == nullptr);  // declaration succeeded (no conflict)
}

// ── Declaration ──

TEST(symboltable_declare_resolve_basic) {
    SymbolTable st;
    Token tok; tok.lexeme = "x";
    auto conflict = st.declare(tok, std::make_shared<PrimitiveType>("i64"), true);
    ASSERT_TRUE(conflict == nullptr);  // no conflict

    auto resolved = st.resolve("x");
    ASSERT_TRUE(resolved != nullptr);
    ASSERT_EQ(resolved->name, "x");
    ASSERT_EQ(resolved->type->toString(), "i64");
    ASSERT_TRUE(resolved->is_const);
    ASSERT_EQ(resolved->depth, 0);
}

TEST(symboltable_declare_redeclare_same_scope_errors) {
    SymbolTable st;
    Token tok; tok.lexeme = "x";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    Token tok2; tok2.lexeme = "x";
    auto conflict = st.declare(tok2, std::make_shared<PrimitiveType>("string"), true);
    ASSERT_TRUE(conflict != nullptr);  // redeclaration in same scope
    ASSERT_EQ(conflict->name, "x");
}

TEST(symboltable_shadowing_allowed_in_nested_scope) {
    SymbolTable st;
    Token tok; tok.lexeme = "x";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    st.enterScope();
    Token tok2; tok2.lexeme = "x";
    auto conflict = st.declare(tok2, std::make_shared<PrimitiveType>("string"), true);
    ASSERT_TRUE(conflict == nullptr);  // shadowing is allowed

    // Resolve should find the inner one
    auto resolved = st.resolve("x");
    ASSERT_TRUE(resolved != nullptr);
    ASSERT_EQ(resolved->type->toString(), "string");

    st.exitScope();
    // After popping, resolve returns the outer one
    resolved = st.resolve("x");
    ASSERT_EQ(resolved->type->toString(), "i64");
}

// ── Resolution ──

TEST(symboltable_resolve_unknown_returns_null) {
    SymbolTable st;
    auto resolved = st.resolve("nonexistent");
    ASSERT_TRUE(resolved == nullptr);
}

TEST(symboltable_resolve_marks_used) {
    SymbolTable st;
    Token tok; tok.lexeme = "y";
    st.declare(tok, std::make_shared<PrimitiveType>("bool"), false);

    auto sym = st.resolve("y");
    ASSERT_TRUE(sym->used);
}

TEST(symboltable_resolve_walks_scopes_innermost_first) {
    SymbolTable st;
    Token tok; tok.lexeme = "a";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    st.enterScope();
    Token tok2; tok2.lexeme = "b";
    st.declare(tok2, std::make_shared<PrimitiveType>("string"), false);

    // Resolve "a" from outer scope
    auto a = st.resolve("a");
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->type->toString(), "i64");

    // Resolve "b" from inner scope
    auto b = st.resolve("b");
    ASSERT_TRUE(b != nullptr);
    ASSERT_EQ(b->type->toString(), "string");
}

// ── Unused symbol tracking ──

TEST(symboltable_exit_scope_reports_unused) {
    SymbolTable st;
    st.enterScope();
    Token tok; tok.lexeme = "unused_local";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    auto unused = st.exitScope();
    ASSERT_EQ(unused.size(), 1u);
    ASSERT_EQ(unused[0]->name, "unused_local");
}

TEST(symboltable_exit_scope_no_unused_when_resolved) {
    SymbolTable st;
    st.enterScope();
    Token tok; tok.lexeme = "used_local";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);
    auto sym = st.resolve("used_local");  // mark as used
    ASSERT_TRUE(sym != nullptr);

    auto unused = st.exitScope();
    ASSERT_EQ(unused.size(), 0u);
}

TEST(symboltable_global_symbols_always_kept) {
    SymbolTable st;
    Token tok; tok.lexeme = "global_var";
    st.declare(tok, std::make_shared<PrimitiveType>("string"), false);

    auto unused = st.exitScope();  // pops global scope? global is never popped
    ASSERT_EQ(st.getScopeDepth(), 0);
}

// ── findShadowed ──

TEST(symboltable_find_shadowed_finds_outer) {
    SymbolTable st;
    Token tok; tok.lexeme = "x";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    st.enterScope();
    Token tok2; tok2.lexeme = "x";
    st.declare(tok2, std::make_shared<PrimitiveType>("string"), false);

    auto shadowed = st.findShadowed("x");
    ASSERT_TRUE(shadowed != nullptr);
    ASSERT_EQ(shadowed->type->toString(), "i64");  // the outer one
}

TEST(symboltable_find_shadowed_no_shadow_returns_null) {
    SymbolTable st;
    Token tok; tok.lexeme = "only_x";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    auto shadowed = st.findShadowed("only_x");
    ASSERT_TRUE(shadowed == nullptr);  // no shadowing
}

TEST(symboltable_find_shadowed_unknown_returns_null) {
    SymbolTable st;
    auto shadowed = st.findShadowed("nope");
    ASSERT_TRUE(shadowed == nullptr);
}

// ── getGlobalScope ──

TEST(symboltable_get_global_scope) {
    SymbolTable st;
    Token tok; tok.lexeme = "f";
    st.declare(tok, std::make_shared<PrimitiveType>("f64"), false);

    const auto& global = st.getGlobalScope();
    ASSERT_EQ(global.size(), 1u);
    auto it = global.find("f");
    ASSERT_TRUE(it != global.end());
    ASSERT_EQ(it->second->type->toString(), "f64");
}

TEST(symboltable_global_scope_excludes_nested) {
    SymbolTable st;
    Token tok; tok.lexeme = "global";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    st.enterScope();
    Token tok2; tok2.lexeme = "local";
    st.declare(tok2, std::make_shared<PrimitiveType>("string"), false);

    const auto& global = st.getGlobalScope();
    ASSERT_EQ(global.size(), 1u);  // only global, not local
    ASSERT_TRUE(global.find("global") != global.end());
    ASSERT_TRUE(global.find("local") == global.end());
}

// ── Imported symbols ──

TEST(symboltable_imported_symbol_tracks_module) {
    SymbolTable st;
    auto mod = std::make_shared<ModuleType>("mylib");
    Token tok; tok.lexeme = "imported_fn";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false, mod);

    auto sym = st.resolve("imported_fn");
    ASSERT_TRUE(sym != nullptr);
    // is_imported is set externally by the TypeChecker, not by declare().
    // Check that the from_module pointer is stored correctly.
    ASSERT_TRUE(sym->from_module != nullptr);
    ASSERT_EQ(sym->from_module->name, "mylib");
}

TEST(symboltable_local_symbol_not_imported) {
    SymbolTable st;
    Token tok; tok.lexeme = "local_fn";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    auto sym = st.resolve("local_fn");
    ASSERT_TRUE(sym != nullptr);
    ASSERT_TRUE(sym->from_module == nullptr);
}

// ── getScopes ──

TEST(symboltable_get_scopes_returns_all) {
    SymbolTable st;
    Token tok; tok.lexeme = "g";
    st.declare(tok, std::make_shared<PrimitiveType>("i64"), false);

    st.enterScope();
    Token tok2; tok2.lexeme = "l";
    st.declare(tok2, std::make_shared<PrimitiveType>("string"), false);

    const auto& scopes = st.getScopes();
    ASSERT_EQ(scopes.size(), 2u);  // global + inner
}
