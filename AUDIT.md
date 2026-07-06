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
| LIB-4 | Futures / promises / async-await — language-level support (async func, await, Future<T> type). **[IN PROGRESS]** Stages 1–6, S-1, S-2 complete: `Future<T>` type, `async func`/`await`, Chaperone tracking, state-machine codegen with separate `$resume` function, waker chain, event-loop integration (`loop.create_timer`, `loop.run_until`). Remaining: I/O future integration, combinators (Stage 7). |
| LIB-7 | Native-call arg-type validation — module dispatcher should emit runtime type guards from DSL type-strings. |
| LIB-10 | CLI/arg parser (subcommands, help generation); YAML / protobuf / msgpack serialisation; big integers. |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
