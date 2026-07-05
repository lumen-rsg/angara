# FEATURE: `ang_api->call()` — invoke Angara closures from native C code

**Created:** 2026-07-05
**Status:** ⬜ not started
**Unblocks:** LIB-1 (TLS), callback-based async (libuv), websocket events,
             proper HTTP middleware, test runners, and all push-based APIs.

---

## Problem

The module C API (`Angara.h`) has no way for native code to call an Angara
function or closure:

```c
// ❌  does not exist
ang_api->call(fn, argc, argv);
```

Every C library with an event/callback model (libuv, libwebsockets, libcurl,
libmicrohttpd, hiredis async, …) requires C → Angara dispatch.  Without
`call()`, every event-driven module must invert into a **polling** model:

```c
// Current workaround — poll, don't push
for (ev in loop.poll(100)) { … }
```

This forces the Angara side to busy-loop or block, prevents composition
(middleware chains, nested handlers), and rules out real async I/O.

---

## Proposed API

Add one function pointer to `struct AngaraAPI`:

```c
struct AngaraAPI {
    // … existing members …

    /// Call an Angara closure/function with the given arguments.
    /// `fn`      — an AngaraObject wrapping a closure, bound-method, or
    ///             top-level function reference.
    /// `argc`    — number of arguments (0..N).
    /// `argv`    — array of AngaraObject arguments.
    /// Returns the AngaraObject result of the call.
    /// If the callee throws, the exception propagates to the nearest
    /// Angara try/catch frame (or terminates if uncaught).
    AngaraObject (*call)(AngaraObject fn, int argc, AngaraObject* argv);
};
```

### Semantics

- **Closures** — captured environment is restored; the body executes as if
  the closure were called from Angara.
- **Bound methods** — `self` is prepended automatically (matching Angara
  method-call semantics).
- **Top-level functions** — a plain function reference is callable.
- **Exceptions** — `throw` from within the callee propagates normally;
  the C caller does NOT need to handle it (unless it wraps the call in a
  setjmp, which is the existing `__ang_try` pattern).
- **Thread safety** — the call runs on the calling thread.  The GC must be
  quiesced or the calling thread must be registered (Chaperone model, TBD).

---

## Compiler-side changes needed

1. **LLVM codegen** — the runtime already has `__ang_call` (used for
   `CallExpr`).  Expose a stable wrapper `__ang_api_call(fn, argc, argv)`
   that:
   - Unboxes the closure / bound-method / function pointer from the
     `AngaraObject` payload.
   - Restores the closure environment.
   - Calls the underlying function with the supplied arguments.
   - Returns the result (single `AngaraObject`).

2. **ModuleAPI.cpp** — wire `__ang_api_call` into the API vtable (similar
   to how `__ang_api_throw_error`, `__ang_api_to_string`, etc. are wired).

3. **GC integration** — the Chaperone pass must be aware that a C thread
   may hold Angara roots during a `call()`.  This is the same problem
   already solved for `AngaraObject` roots tracked in module functions.

---

## What this unblocks

| Feature | Current state | With `call()` |
|---|---|---|
| **LIB-1 (TLS)** | Can't wire OpenSSL callbacks | Full TLS with cert verification |
| **libuv event loop** | Poll-only workaround | Real `on_read`, `on_timer` callbacks |
| **HTTP middleware** | Flat poll loop | Composable `on_request` chains |
| **WebSocket events** | Poll for frames | `on_message`, `on_close` push events |
| **Test runner** | Manual try/catch per test | `runner.test(name, fn)` auto-capture |
| **DB async** | Blocking queries only | `pg.query_async(sql, on_result)` |
| **Timers / intervals** | Poll timerfd | `set_timeout(ms, fn)` |

---

## Risks / unknowns

- **GC thread safety** — the Chaperone pass must handle cross-thread
  roots correctly for `call()` invoked from arbitrary C threads.
  Mitigation: initially require `call()` only from the main/Angara thread,
  document the limitation, and lift it later.
- **ABI stability** — `AngaraObject` layout must remain stable.
- **Exception propagation** — the existing `__ang_throw` → `longjmp`
  path must be safe when the setjmp frame sits in Angara code above the
  C → Angara call boundary.  This already works for module→Angara throws
  (the `throw_error` function); `call()` is the reverse direction but
  uses the same machinery.

---

## Related issues

- LIB-1 (TLS/SSL everywhere) — blocked on this.
- LIB-4 deferral (futures/await) — `call()` enables callback-based
  futures that an `await` desugaring could target.
- TOOL-1 (package manager) — would benefit from async HTTP for registry
  fetches.

---

## Implementation sketch

```
1. angc/backend/llvm/rt/  — add __ang_api_call()
2. angc/includes/Angara.h — add call() to struct AngaraAPI
3. ModuleAPI.cpp          — wire into vtable
4. tests/lang/            — .an test: pass closure to native, call it, check result
```
