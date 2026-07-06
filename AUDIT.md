# Angara — Remaining Work

> Compacted from the 2026-06-18 audit against `stable`.  
> All resolved items removed; only outstanding work remains.

---

## Open items

| ID | Sev | Issue | Location |
|---|---|---|---|
| - [ ] **TOOL-1** | High | No package manager / registry / versioning / lockfile. `dependencies = [...]` is just `-l` flags. | `BuildSystem.cpp`, `CompilerDriver.cpp` |

---

## LIB deferrals

| ID | Issue |
|---|---|
| LIB-1 | AMQP/MQTT TLS — secure schemes parsed but TLS not configured on rabbitmq-c / mosquitto connections. |
| LIB-4 | Futures / promises / async-await — language-level support (async func, await, Future<T> type). ✅ **DONE.** All stages complete: `Future<T>` type, `async func`/`await` syntax, Chaperone ownership tracking (E501/E502/E507), state-machine codegen with retry blocks + PHI nodes, waker chain for cascading resume, event-loop integration (`loop.create_timer`/`run_until`/`read`/`write`/`accept`), `Future<T>` type DSL (`f<T>`). Combinators deferred to follow-up. |
| LIB-7 | Native-call arg-type validation — module dispatcher should emit runtime type guards from DSL type-strings. |
| LIB-10 | CLI/arg parser (subcommands, help generation); YAML / protobuf / msgpack serialisation; big integers. |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
