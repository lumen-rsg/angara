# asu — Angara Super User

A minimal **sudo clone** written in pure Angara, demonstrating zero-cost FFI for POSIX system programming.

## What is this?

`asu` uses Angara's `foreign func` declarations to call POSIX APIs directly — no C shim, no shared library, no runtime overhead. Every FFI call compiles to a direct C function call.

The program:

1. Checks if it's running with root privileges (setuid bit)
2. Escalates real UID/GID to root via `setuid(0)` / `setgid(0)`
3. Drops into an interactive shell prompt (`asu#`)
4. Executes commands via `system()` with proper exit code propagation

## Zero-Cost FFI

All POSIX calls are declared as `foreign func`:

```angara
foreign func getuid()  -> i64;
foreign func setuid(uid as i32) -> i32;
foreign func system(cmd as string) -> i32;
```

The compiler generates direct calls — the generated LLVM IR calls `@getuid`, `@setuid`, `@system` with no boxing or wrapper overhead.

## Prerequisites

1. **Angara compiler** (`angc`) — built in the repository root

## Build & Run

```bash
# Build
make

# Install (set the setuid bit — requires sudo)
make install

# Run
make run
# or: ./build/asu
```

## Usage

```
$ ./build/asu
asu — Angara Super User
Running as UID 0
Type 'exit' to quit.

asu# whoami
root
asu# id
uid=0(root) gid=0(wheel) groups=0(wheel),...
asu# exit
$
```

## POSIX APIs Used

| FFI Declaration | C Symbol | Purpose |
|---|---|---|
| `getuid()` | `getuid` | Get real user ID |
| `geteuid()` | `geteuid` | Get effective user ID |
| `setuid(uid)` | `setuid` | Set real user ID |
| `setgid(gid)` | `setgid` | Set real group ID |
| `system(cmd)` | `system` | Execute a shell command |
| `io.read_line()` | — | Read input from stdin |

## Exit Code Behavior

- Returns `0` if all commands exited successfully
- Returns the last command's exit code otherwise
- Returns `128 + signal` if a command was killed by a signal

## Security Notes

This is a **demonstration** — not production-hardened. A real sudo would need:

- PAM authentication before escalating privileges
- Logging of all commands executed
- SELinux/AppArmor policy integration
- Time-based ticket caching
- Argument sanitization

## Project Structure

| File | Description |
|---|---|
| `asu.an` | Pure Angara source — FFI declarations + interactive shell |
| `Makefile` | Build, install, and clean targets |
