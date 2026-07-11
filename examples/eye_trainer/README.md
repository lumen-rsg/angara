# eye_trainer — Simulated DDR Eye-Pattern Memory Trainer

A bare-metal Angara app that reconstructs the DDR memory-training algorithm
(the kind an RK3588 boot ROM or U-Boot SPL performs) against a **software DDR
PHY model**, and renders the resulting "eye diagram" to the UART.

```
Angara DDR Eye Trainer (simulated PHY)

    DQS0123456789012345678901234567890
  (ones digit repeats; tens on the row above)

DQ  0:                                
DQ  1:                                
DQ  2:                                
DQ  3:                                
DQ  4:                                
DQ  5:           ....                 
DQ  6:         ..####..               
DQ  7:        .########.              
DQ  8:        ##########   ← center   
DQ  9:        .########.              
DQ 10:         ..####..               
DQ 11:           ....                 
DQ 12:                                
...

EYE_CENTER=16,8
EYE_PASSES=210/512
EYE_QUALITY=41%
PHY_TRAIN=OK
```

## What this is

Real DDR training sweeps the DQS (data strobe) and DQ (data) delay taps across
their range and tests read/write at each combination. The region where reads
succeed is the **eye** — the valid sampling window, shaped by analog signal
integrity (jitter, crosstalk, ISI, PCB trace length). The trainer finds the
eye's center and programs the PHY registers there.

This app does exactly that, but the **PHY is a math model**:
- The eye opening is a 2D Gaussian centered at an ideal (DQS, DQ) point.
- A "read" at delay point (dqs, dq) passes with probability = the Gaussian
  value at that point, minus a noise floor (jitter).
- The eye has fuzzy edges — just like real silicon.

The **algorithm** (sweep, detect, center) is faithful to the real process.
The **physics** is faked.

## ⚠️ This is NOT real eye training

QEMU's memory is software-emulated RAM with **perfect signal integrity**.
There is no real DDR PHY, no analog jitter, no crosstalk. This app trains no
real memory. It demonstrates the *algorithm* and exercises the bare-metal
stack (nested loops, f64 math, peek/poke RAM arrays, no-heap UART rendering).

For real RK3588 eye training, you need:
- Real RK3588 hardware (or an SoC-accurate simulator).
- The RK3588 DDR PHY register map (PHY reg read/write, read/write leveling,
  DQS gate training, per-byte-lane delay sweeping).
- U-Boot SPL or a bare-metal payload talking to the real PHY.

The RK3588 eye viewer (e.g. in `rkbin` / vendor U-Boot) works because it reads
back the PHY's internal delay-vs-pass/fail counters from real silicon.

## What it exercises

| Feature | How |
|---------|-----|
| Nested `for` loops | DQS × DQ delay sweep |
| `f64` math | 2D Gaussian eye-opening model |
| `peek64`/`poke64` | RAM-backed pass/fail grid (no heap arrays in freestanding) |
| `cntpct` | Cycle-counter PRNG seed |
| No-heap rendering | `uart_putc` byte-at-a-time + custom decimal formatter |
| Full boot pipeline | boot.S → `_start` → `main` → `halt` |

## Build & run

```sh
make          # build eye.img
make run      # boot interactively in QEMU (Ctrl-A X to quit)
make verify   # headless: assert PHY_TRAIN=OK + EYE_CENTER=
```

Same prerequisites as `qemu_virt`: `angc`, `clang` (aarch64), `ld.bfd`,
`llvm-objcopy`, `qemu-system-aarch64`.
