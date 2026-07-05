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
| ~~LANG-11~~ | ~~No default arguments; no named arguments.~~ ✅ Resolved (2026-07-06) |

---

## Known test regressions

These tests fail under the current Chaperone (v5 ownership tracking).  They are **pre-existing** and unrelated to LANG-11.

| Test | Error | Notes |
|---|---|---|
| `tests/lang/positive/08_optionals.an` | E501: `s`, `sn` (both `string?`) leak on return | Adding `drop` compiles but segfaults at runtime — likely a Chaperone lowering bug for optional drop sites. |
| `tests/lang/positive/16_is_downcast.an` | E501: `maybe_name` (`string?`) leaks on return | Same root cause as above. |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
