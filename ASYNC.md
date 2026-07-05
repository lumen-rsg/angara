# Angara Async/Await — Implementation Tracking

> LIB-4: Language-level async/await aligned with the Chaperone memory model.
> Branch: `stable` | Started: 2026-07-06

## Design principles

1. `Future<T>` is an **`owned` type** — heap-allocated, explicitly tracked by the Chaperone, must be `drop`ped if abandoned
2. `await` **consumes** the future via move semantics (the Chaperone's existing `Moved` state)
3. `async func` returns `Future<T>` — the allocation is visible to the Chaperone at the call site
4. **No implicit event loop** — the existing `eventloop.Loop` (née `async.Loop`) drives everything explicitly
5. **Cancellation = drop** — dropping a future cancels its computation; the finalizer cleans up
6. **Hand-rolled state machine codegen** — no LLVM coroutine intrinsics; explicit switch-based state machines

## Language surface

```angara
// Declaring async functions
async func fetch(url as string) -> string {
    let data = await read_socket(sock);   // suspend here
    return data;
}

// Calling from sync code
let future = fetch("http://...");   // future: owned Future<string>
drop future;                        // cancel

// Calling from async code
let result = await fetch(url);      // consumes future (moves to Moved)
```

## Stage progress

| Stage | Description | Status | Commit |
|-------|-------------|--------|--------|
| **1** | `Future<T>` type system (`TypeKind::FUTURE`, `FutureType`, `sameType`, `substituteTypeArgs`, type resolution, Chaperone tracking) | ✅ Done | `c189ac3` |
| **2** | `async` keyword + async function declarations (`TokenType::ASYNC`, `FuncStmt::is_async`, parser dispatcher, `Future<T>` return wrapping, return-type unwrapping for body checking) | ✅ Done | `c189ac3` |
| **3** | `await` expression (`TokenType::AWAIT`, `AwaitExpr` AST node, parser, type checker (E419/E420), Chaperone consumption (`Live→Moved`), synchronous codegen passthrough) | ✅ Done | `c189ac3` |
| **4** | Future frame allocation + async return codegen (`codegenAsyncFuncDecl`, heap frame `{i32 state, objType result}`, `cgReturn` async path, native instance wrapping) | ✅ Done | `c189ac3` |
| **5** | State machine framework + await result extraction (`collectAwaitStates`, extended frame `{state, result, awaited, params...}`, `cgAwait` result extraction via `__ang_api_native_instance_data`, suspend/done blocks, multi-await chains working) | ✅ Done | `ed68e31` |
| **6** | Chaperone async verification (E501 leak in async func, E502 use-after-free on awaited future, E507 use-after-move on double-await) | ✅ Done | `c189ac3` |
| **7** | Combinators (`Future.all`, `Future.race`, `Future.map`) | ⏳ Pending | — |
| **S** | Real suspension (event loop integration, state save/restore across suspend points, waker callbacks, `loop.run_until`) | ⏳ Pending | — |
| **Module** | Rename `async` module → `eventloop` to avoid keyword conflict | ✅ Done | `c189ac3` |

## Files changed

| File | Stage(s) | Changes |
|------|----------|---------|
| `angc/includes/Type.h` | 1 | `TypeKind::FUTURE`, `FutureType` struct, `sameType`, `substituteTypeArgs`, `GenericInstanceType::substitute` |
| `angc/includes/Token.h` | 2, 3 | `ASYNC`, `AWAIT` tokens |
| `angc/frontend/lexer/Lexer.cpp` | 2, 3 | Keyword registration |
| `angc/frontend/parser/Parser.cpp` | 2, 3 | `isKeywordToken` guard |
| `angc/includes/Stmt.h` | 2 | `FuncStmt::is_async` |
| `angc/frontend/parser/declaration/dispatcher.cpp` | 2 | `async func` parsing, E415/E416 |
| `angc/analyzer/type_checker/stmt/FuncStmt.cpp` | 2 | `Future<T>` return wrapping, return-type unwrapping, `m_in_async_function` tracking, E417/E418 |
| `angc/includes/Expr.h` | 3 | `AwaitExpr` struct, `ExprVisitor::visit(AwaitExpr)` |
| `angc/frontend/parser/expr/unaryExpression.cpp` | 3 | `await <expr>` parsing |
| `angc/includes/TypeChecker.h` | 3 | `visit(AwaitExpr)`, `m_in_async_function` |
| `angc/analyzer/type_checker/expr/AwaitExpr.cpp` | 3 | **New.** Validate `Future<T>`, unwrap to `T`, E419/E420 |
| `angc/backend/chaperone/Chaperone.cpp` | 1 | `FUTURE` in `isBuiltinHeapType` |
| `angc/backend/chaperone/expr/ExprAnalysis.cpp` | 3 | `AwaitExpr`: consume future (`Live→Moved`), `collectExprVarRefs` |
| `angc/analyzer/type_checker/TypeChecker.cpp` | 1 | `Future<T>` type resolution |
| `angc/includes/LLVMBackend.h` | 3, 4, 5 | `cgAwait`, `codegenAsyncFuncDecl`, `collectAwaitStates*`, async state members |
| `angc/backend/llvm/TopLevel.cpp` | 2, 4, 5 | `codegenAsyncFuncDecl` (frame allocation, state machine, suspend/done blocks), `collectAwaitStates*` |
| `angc/backend/llvm/stmt/StmtCodegen.cpp` | 4 | `cgReturn` async path |
| `angc/backend/llvm/expr/ExprCodegen.cpp` | 3, 5 | `cgAwait` (result extraction via `__ang_api_native_instance_data`), dispatch |
| `angc/analyzer/printer/ASTPrinter.cpp` | 2, 3 | `[async]` tag, `AwaitExpr` printing |
| `angc/analyzer/printer/ASTPrinter.h` | 3 | `visit(AwaitExpr)` |
| `angc/analyzer/printer/Formatter.cpp` | 2, 3 | `async` prefix, `await` formatting |
| `angc/includes/Formatter.h` | 3 | `visit(AwaitExpr)` |
| `modules/io/async.c` → `modules/io/eventloop.c` | Module | Renamed, `Angara_async_*` → `Angara_eventloop_*`, `ANGARA_MODULE_INIT(async)` → `ANGARA_MODULE_INIT(eventloop)` |
| `Makefile` | Module | Updated module target and dependency list |
| `tests/lang/positive/32_async_callbacks.an` | Module | `attach async` → `attach eventloop`, `async.Loop()` → `eventloop.Loop()` |
| `AUDIT.md` | Doc | Updated LIB-4 status |

## Chaperone integration

| Concept | Chaperone Treatment |
|---------|-------------------|
| `Future<T>` type | Tracked as heap-allocated (in `isBuiltinHeapType`); must be `drop`ped |
| `async func` call | Returns a new `Live` `Future<T>` — caller responsible for it |
| `await future` | Consumes the future: `Live → Moved` (same as move-on-assign) |
| Locals in async func | Analyzed normally — drops, moves, borrows all work as usual |
| E501 (leak) | Detected: owned value not dropped before return in async func |
| E502 (use-after-free) | Detected: `await` on dropped future |
| E507 (use-after-move) | Detected: double-await on same future |

## Runtime architecture (current)

```
Caller                    async func foo()                Future frame
  │                             │                         ┌──────────────┐
  ├─ foo(args) ─────────────────┤                         │ state: i32   │
  │                             ├─ malloc(frame) ────────→│ result: obj  │
  │                             ├─ store params ─────────→│ awaited: obj │
  │                             ├─ run body ──────┐       │ param_0: obj │
  │                             │   await bar()   │       │ ...          │
  │                             │   extract result│←──────┤              │
  │                             │   ...           │       └──────────────┘
  │                             ├─ set state=-1 ──┘
  │                             ├─ wrap frame ───────────→ native instance
  │←── Future<T> ──────────────┤
  │                             │
  │  await fut                  │
  │  extract result ────────────┤→ __ang_api_native_instance_data
  │←── T                        │→ load frame.result
```

## What real suspension will add

When `await` encounters a pending future:
1. Save current state number to frame
2. Save live local variables to frame fields
3. Register waker callback with event loop
4. Return the frame (as Future) to caller

When the awaited future completes:
1. Event loop calls the waker
2. Waker calls `foo$resume(frame_ptr)`
3. Resume function switches on frame.state, jumps to correct basic block
4. Restores locals from frame, continues execution

## Test coverage

| Area | Tests | Status |
|------|-------|--------|
| C++ unit tests | 197 | All pass |
| Chaperone tests | 52 | All pass |
| Lang tests | 63 | All pass |
| Async leak (E501) | Manual | ✅ Caught |
| Async use-after-free (E502) | Manual | ✅ Caught |
| Async use-after-move (E507) | Manual | ✅ Caught |
| Single await | Manual | ✅ `42` printed |
| Multi-await chain | Manual | ✅ `80` printed (double³(10)) |
| `await` outside async (E419) | Manual | ✅ Caught |
| `await` non-Future (E420) | Manual | ✅ Caught |

*Last updated: 2026-07-06*
