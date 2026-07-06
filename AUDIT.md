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
| LIB-10 | ~~CLI/arg parser~~ (done: `modules/data/cli.c`); ~~YAML~~ (done: `modules/data/yaml.c`); ~~protobuf~~ (done: `modules/data/protobuf.c`); ~~msgpack~~ (done: `modules/data/msgpack.c`); ~~big integers~~ (done: `modules/math/bigint.c`). |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
