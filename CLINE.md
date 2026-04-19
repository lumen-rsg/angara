# Generics Implementation for Angara

## Branch: `feature/generics`

## Overview
Implementing generics (parameterized types) for Angara using **type erasure** — all generic type parameters resolve to `any` at runtime since Angara uses a boxed value model (`AngaraObject = {i32 tag, i64 payload}`).

## Target Syntax
```angara
data Box<T> { value as T }
data Pair<K, V> { key as K, value as V }
func identity<T>(x as T) -> T { return x }
let box as Box<i64> = Box(42);
```

## Progress

### Phase 1: AST & Type System Foundation
- [ ] Add `TypeParameterType` to `TypeKind` in `Type.h`
- [ ] Add `type_params` field to `DataType`, `ClassType`, `FunctionType` in `Type.h`
- [ ] Add type parameter storage to `DataStmt`, `FuncStmt`, `ClassStmt` in `Stmt.h`
- [ ] Add `TypeParam` AST node or reuse existing `GenericType` parsing

### Phase 2: Parser
- [ ] Parse `<T, U, V>` type parameter lists after names in `data`/`func`/`class`
- [ ] Disambiguate `<` as type params vs comparison operator
- [ ] Parse generic type usage in annotations (already partially handled by `GenericType` AST node)

### Phase 3: Type Checker
- [ ] Register type parameters in scope when entering generic declarations
- [ ] Resolve `T` as `TypeParameterType` during type checking
- [ ] Substitute type params when encountering `Box<i64>` (T → i64)
- [ ] Infer type params at generic function call sites
- [ ] Type safety enforcement for generic field access

### Phase 4: Codegen (LLVM Backend)
- [ ] Handle `TypeParameterType` — erase to `any` / `AngaraObject`
- [ ] Generate constructors for generic data types
- [ ] Handle generic function codegen (single copy, type-erased)
- [ ] Handle generic field access in expressions

### Phase 5: Tests
- [ ] Create `tests/test_generics.an`
- [ ] Test generic data types (Box<T>, Pair<K,V>)
- [ ] Test generic functions (identity<T>)
- [ ] Test type inference at call sites
- [ ] Verify existing tests still pass

## Architecture Decision: Type Erasure
- **No monomorphization** — one copy of code per generic function/type
- Type parameters erased to `any` at codegen level
- Type safety enforced at compile time by type checker
- Runtime representation unchanged (`AngaraObject`)