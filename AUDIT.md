# Angara — Remaining Work

> Compacted from the 2026-06-18 audit against `stable`.  
> All resolved items removed; only outstanding work remains.

---

## Open items

| ID | Sev | Issue | Location |
|---|---|---|---|
| - [x] **TOOL-1** | High | No package manager / registry / versioning / lockfile. `dependencies = [...]` is just `-l` flags. | `BuildSystem.cpp`, `CompilerDriver.cpp`; new: `angc/backend/package/*`, `angc/includes/{PackageManager,Version,Lockfile,RegistryClient}.h` |

---

## LIB deferrals

| ID | Issue |
|---|---|
| LIB-1 | ~~AMQP/MQTT TLS~~ (done: `modules/net/amqp.c`, `modules/net/mqtt.c`) — `amqps://` / `mqtts://` now configure TLS on rabbitmq-c / mosquitto connections with `verify`, `ca_bundle`, `client_cert`, `client_key` options. |
| LIB-10 | ~~CLI/arg parser~~ (done: `modules/data/cli.c`); ~~YAML~~ (done: `modules/data/yaml.c`); ~~protobuf~~ (done: `modules/data/protobuf.c`); ~~msgpack~~ (done: `modules/data/msgpack.c`); ~~big integers~~ (done: `modules/math/bigint.c`). |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
