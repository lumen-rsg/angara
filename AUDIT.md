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
| LIB-4 | Futures / promises / async-await — requires language-level support (generator resumption, task scheduling). |
| LIB-7 | Native-call arg-type validation — module dispatcher should emit runtime type guards from DSL type-strings. |
| LIB-10 | CLI/arg parser (subcommands, help generation); YAML / protobuf / msgpack serialisation; big integers. |

---

## LANG deferrals

| ID | Issue |
|---|---|
| LANG-6 | No raw/byte strings (`r"..."`, `b"..."`), float exponents (`1e10`), numeric suffixes (`42u8`), octal literals. |
| LANG-8 | No generic enums (`enum Result<T,E>`) — only `data` and `func` are generic. |
| LANG-10 | No tuples / tuple types / multi-return; no destructuring (assign, pattern, or `for (k, v in map)`). |
| LANG-11 | No default arguments; no named arguments. |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
