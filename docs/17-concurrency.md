# Concurrency

Angara provides thread-based concurrency via `spawn()` and `Mutex`, plus
cooperative concurrency via `async`/`await`. The Chaperone statically verifies
memory safety across thread boundaries.

---

## `spawn()` — Creating Threads

`spawn()` creates a new OS thread and transfers ownership of tracked values to it:

```angara
func worker(buf as Buffer) -> nil {
    buf.write("hello from thread");
    drop buf;
}

export func main() -> i64 {
    let buf = Buffer(1024);
    spawn(worker, buf);     // Ownership of 'buf' transfers to the new thread
    // 'buf' is no longer valid here — don't use it!
    return 0;
}
```

### Ownership Transfer

When you `spawn(func, arg1, arg2, ...)`, ownership of each argument **moves** to
the spawned thread. The Chaperone enforces this:

- **E510** — using or dropping a transferred variable in the parent thread after `spawn()`.
- **E511** — the argument's type is not marked `@sendable` (see below).
- **E515** — a `ref<T>` to the value exists in the parent thread while spawning,
  and the type is not `@sync`.

### Thread Safety Annotations

| Annotation | Purpose |
|---|---|
| `@sendable` | Type can be **transferred** to another thread via `spawn()` |
| `@sync` | Type is safe for **concurrent access** through `ref<T>` across threads |

```angara
@sendable
class Task {
    let id as i64;
    let payload as string;
}

@sync @sendable
class SharedState {
    let mutex as Mutex;
    let counter as i64;
}
```

Mark a type `@sendable` when it (and all its fields) can be safely moved to
another thread. Mark it `@sync` when you have internal synchronisation (e.g.,
a `Mutex`) that makes concurrent `ref<T>` access safe.

Without `@sendable`, passing the type to `spawn()` produces **E511**.

---

## `Mutex` — Mutual Exclusion

Use `Mutex` to protect shared state accessed from multiple threads:

```angara
let mutex as Mutex = Mutex();
let results as list<i64> = [];

mutex.lock();
results.push(42);
mutex.unlock();
```

The Chaperone tracks lock/unlock pairs:
- **E512** — locking a `Mutex` that is already locked (potential deadlock).
- **E513** — unlocking a `Mutex` that is not locked (logic error).

### Mutex + spawn Example

```angara
@sync @sendable
class SafeCounter {
    let lock as Mutex;
    let value as i64;
}

func increment(counter as ref<SafeCounter>) -> nil {
    counter.lock.lock();
    counter.value = counter.value + 1;
    counter.lock.unlock();
}

export func main() -> i64 {
    let ctr = SafeCounter(Mutex(), 0);
    let r as ref<SafeCounter> = &ctr;

    spawn(increment, ctr);      // Ownership moves, but ref is OK (it's @sync)
    // 'r' is still valid here — SafeCounter is @sync
    drop r;
    drop ctr;                   // Wait, ctr was spawned... See E510.
    return 0;
}
```

> **Important:** After `spawn(worker, ctr)`, the parent thread no longer owns `ctr`.
> Drop it in the spawned thread instead.

---

## `Thread` — Manual Thread Handle

The `Thread` type provides lower-level thread control:

```angara
let t as Thread = Thread();
```

For most use cases, `spawn()` is preferred — it integrates with the Chaperone's
ownership tracking.

---

## Async/Await — Cooperative Concurrency

Async functions enable cooperative multitasking without OS threads. See
[04 - Functions](04-functions.md) for the full async/await syntax.

### Key points for concurrency:

- `async func` returns `Future<T>`, which is a tracked type.
- An `await` point may resume on a different thread — all tracked values that
  survive the `await` must be `@sendable`.
- **E516** — a non-Send tracked value is held across an `await` point.
- **E419** — `await` used outside an `async func`.

```angara
@sendable
class Connection {
    let socket as i64;
}

async func handle(conn as Connection) -> string {
    let data = await read_socket(conn.socket);
    return data;
}
```

---

## Chaperone Concurrency Diagnostics

| Code | Name | Meaning |
|------|------|---------|
| **E510** | Thread escape | Using/dropping a tracked value after it was transferred via `spawn()` |
| **E511** | Not sendable | Type is not `@sendable` and cannot be passed to `spawn()` |
| **E512** | Double lock | `Mutex` locked while already locked — deadlock risk |
| **E513** | Double unlock | `Mutex` unlocked without being locked |
| **E515** | Non-Sync ref | `ref<T>` to non-`@sync` type exists when spawning the owned value |
| **E516** | Async escape | Non-Send tracked value held across an `await` point |
| **W514** | Sync ref warning | `ref<T>` to `@sync` type crosses thread boundary — ensure synchronisation |

---

## Kernel-Mode Restrictions

When compiling with `--kernel`, concurrency primitives are restricted:

- **E902** — `spawn()` is not available (kernel has no pthreads).
- **E903** — `Mutex` is not available (uses pthread mutexes).

Use kernel synchronisation primitives (spinlocks, `struct mutex` from
`<linux/mutex.h>`) through FFI in kernel-mode code.

---

For the full memory model, including thread-safety proofs and the `@sendable` /
`@sync` design, see [CHAPERONE.md](../CHAPERONE.md).
