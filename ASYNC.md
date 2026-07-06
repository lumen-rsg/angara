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
| **S** | Real suspension — state machine dispatch, separate `$resume` function, waker chain (cascading resume without event loop), frame-based local storage, `free` finalizer for frame cleanup | ✅ Done (Stage S-1) | — |
| **S-2** | Event loop integration (`loop.create_timer`, `loop.run_until`, Runtime API for future state/result/loop, `Future<T>` type DSL support, end-to-end timer suspend/resume) | ✅ Done | — |
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
| `angc/includes/LLVMBackend.h` | 3, 4, 5, S | `cgAwait`, `codegenAsyncFuncDecl`, `collectAwaitStates*`, async state members, `m_async_local_slots`, `codegenAsyncResumeFunc`, `collectAsyncLocals`, waker field ptrs |
| `angc/backend/llvm/TopLevel.cpp` | 2, 4, 5, S | `codegenAsyncFuncDecl` (wrapper + resume split), `codegenAsyncResumeFunc` (state machine with switch dispatch), `collectAwaitStates*`, `collectAsyncLocals*` |
| `angc/backend/llvm/stmt/StmtCodegen.cpp` | 4, S | `cgReturn` async path (waker cascade), `cgVarDecl` async frame-based storage, `cgDrop` async frame slot support |
| `angc/backend/llvm/expr/ExprCodegen.cpp` | 3, 5, S | `cgAwait` (check-branch-suspend, waker registration, resume block dispatch) |
| `angc/backend/llvm/LLVMBackend.cpp` | S | `loadVar`/`storeVar` async frame slot path |
| `angc/analyzer/printer/ASTPrinter.cpp` | 2, 3 | `[async]` tag, `AwaitExpr` printing |
| `angc/analyzer/printer/ASTPrinter.h` | 3 | `visit(AwaitExpr)` |
| `angc/analyzer/printer/Formatter.cpp` | 2, 3 | `async` prefix, `await` formatting |
| `angc/includes/Formatter.h` | 3 | `visit(AwaitExpr)` |
| `modules/io/async.c` → `modules/io/eventloop.c` | Module | Renamed, `Angara_async_*` → `Angara_eventloop_*`, `ANGARA_MODULE_INIT(async)` → `ANGARA_MODULE_INIT(eventloop)` |
| `Makefile` | Module | Updated module target and dependency list |
| `tests/lang/positive/32_async_callbacks.an` | Module | `attach async` → `attach eventloop`, `async.Loop()` → `eventloop.Loop()` |
| `tests/lang/positive/33_async_await.an` | S | **New.** Async/await chain tests (single, multi-await, degenerate no-await) |
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

## Runtime architecture (Stage S-1 — implemented)

### Wrapper + resume split

```
Wrapper (__ang_<mod>_foo):
  1. malloc frame
  2. Init header (state=0, waker_fn=null, ...)
  3. Store params in frame fields
  4. Call foo$resume(frame_ptr)
  5. Wrap frame → NativeInstance(free finalizer) → return Future

Resume (__ang_<mod>_foo$resume):
  1. switch frame.state → jump to correct state block
  2. State 0: run code before first await
  3. At await N:
     - Evaluate future, extract child frame
     - Load child.state
     - If resolved (-1): extract child.result, branch to resume block N
     - If pending: store state N, store {&resume, own_frame} in child's waker fields, return
  4. On completion: store result, set state=-1, cascade waker if set, return
```

### Waker chain (cascading resume, no event loop needed)

When parent awaits child and child is pending:
1. Parent stores `{&parent$resume, parent_frame}` in **child's** frame (waker_fn, waker_ctx)
2. Parent stores its state index in own frame, returns (suspends)

When child completes:
1. Child sets `state = -1`, stores result
2. Child checks own `waker_fn` — if set, calls `waker_fn(waker_ctx)` → resumes parent
3. Parent resumes from saved state, extracts child's result, continues

### Frame layout (standardized header)

```
Field 0: i32 state          (-1=resolved, 0..N=await state)
Field 1: obj result         (return value when resolved)
Field 2: obj awaited        (future being awaited during suspend)
Field 3: ptr waker_fn       (resume function to call on completion)
Field 4: ptr waker_ctx      (frame pointer to pass to waker_fn)
Field 5+: params + local slots (frame-based storage, survives suspend)
```

The header (fields 0-4) is standardized — any async function can access another's waker fields by bitcasting to the common header prefix.

## What's next: event loop integration (Stage S-2)

When a leaf future (I/O, timer) completes via the event loop:
1. Event loop callback resolves the leaf future (state=-1, result set)
2. Leaf future's waker cascades to parent → parent resumes → may complete → cascades further
3. For explicit driving: `loop.run_until(future)` polls the loop until a specific future resolves
4. For I/O futures: `schedule_waker` on the event loop to avoid deep recursive waker chains

## Test coverage

| Area | Tests | Status |
|------|-------|--------|
| C++ unit tests | 197 | All pass |
| Chaperone tests | 52 | All pass |
| Lang tests | 63 | All pass |
| Async leak (E501) | Manual | ✅ Caught |
| Async use-after-free (E502) | Manual | ✅ Caught |
| Async use-after-move (E507) | Manual | ✅ Caught |
| Single await | Manual | ✅ `10` printed |
| Multi-await chain | Manual | ✅ `120` printed (double³(10)) |
| `await` outside async (E419) | Manual | ✅ Caught |
| `await` non-Future (E420) | Manual | ✅ Caught |
| Stage S: real suspension | `34_async_suspend.an` | ✅ Pass (timer suspend/resume, 42) |
| Stage S: async chain test | `33_async_await.an` | ✅ Pass |

*Last updated: 2026-07-06*
