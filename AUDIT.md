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
| LIB-4 | Futures / promises / async-await — language-level support (async func, await, Future<T> type). **[IN PROGRESS]** Basic implementation done (Stages 1–4 + 6): `Future<T>` owned type, `async func` declarations, `await` expressions, Chaperone ownership tracking (E501/E502/E507). Remaining: full state-machine suspension (Stage 5), event-loop integration, combinators (Stage 7). Module renamed `async`→`eventloop` to avoid keyword conflict. |
| LIB-7 | Native-call arg-type validation — module dispatcher should emit runtime type guards from DSL type-strings. |
| LIB-10 | CLI/arg parser (subcommands, help generation); YAML / protobuf / msgpack serialisation; big integers. |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
