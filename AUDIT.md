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

## Known test regressions — all resolved ✅

All three pre-existing test regressions have been fixed.  Full test suite: **63/63 language tests**, **52/52 Chaperone tests** pass.

| Test | Root cause | Fix |
|---|---|---|
| `08_optionals.an` | `isBuiltinHeapType` didn't unwrap `OptionalType`, so `string?` wasn't recognised as a built-in heap type → E501 error instead of W521 warning. | Added optional unwrapping to `isBuiltinHeapType` (`Chaperone.cpp:86-91`). |
| `16_is_downcast.an` | Same as above (`string?` → E501). | Same fix. |
| `13_optional_narrowing.an` | `cgDrop` unconditionally dereferenced the payload pointer for cascade-drops and finalize/free, crashing when the optional was `nil` (NULL payload). | Added nil guard in `cgDrop` (`StmtCodegen.cpp:492-500`): checks `tag == TAG_NIL` and skips the heap deallocation when the value is nil. |

---

*To regenerate: the original full audit is in the git history at the commit that introduced `AUDIT.md`.*
