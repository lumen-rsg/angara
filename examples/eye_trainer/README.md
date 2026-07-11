# eye_trainer — Simulated DDR Eye-Pattern Memory Trainer

A bare-metal Angara app that reconstructs the DDR memory-training algorithm
(the kind an RK3588 boot ROM or U-Boot SPL performs) against a **software DDR
PHY model**, and renders the resulting "eye diagram" to the UART.

Runs in a **continuous training loop**: each iteration randomizes the simulated
PHY (new center, new eye width/height — as if signal conditions drifted), so you
see a different eye pattern every ~0.5 seconds. This mimics periodic retraining
that real firmware performs to compensate for temperature/voltage drift.

```
Angara DDR Eye Trainer (simulated PHY)

PHY: center=18,9  eye=8x3
              1111111111222222222233
DQS01234567890123456789012345678901

DQ 06:                #
DQ 07:              ..####.###.
DQ 08:            .###########...
DQ 09:            .#############.
DQ 10:            ..#.#########..
DQ 11:              ######.##..

ITER=1
EYE_CENTER=17,8
EYE_PASSES=50/512
EYE_QUALITY=9%
PHY_TRAIN=OK
--------------------------------------
PHY: center=11,4  eye=9x6       ← new random PHY: wider eye, shifted left
              1111111111222222222233
DQS01234567890123456789012345678901

DQ 00:      ....###..#...
DQ 01:     .#..####.#####.
DQ 02:    #..#########..#..  #
DQ 03:    ..#.###########..
DQ 04:    ..#.############.
DQ 05:    .#.############.#
DQ 06:    .#..########.##..
DQ 07:     ....#########..
DQ 08:      .....##...#..

ITER=2
EYE_CENTER=11,3
EYE_PASSES=88/512
EYE_QUALITY=17%
PHY_TRAIN=OK
--------------------------------------
... (loops forever, each iteration a different eye pattern)
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
stack (nested loops, integer Gaussian math, peek/poke RAM arrays, no-heap UART
rendering). Each loop iteration randomizes the PHY model so you see different
eye shapes — run `make run` and watch the patterns scroll.

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
| Nested `for` loops | DQS × DQ delay sweep (3 levels deep with multi-sampling) |
| Integer Gaussian math | 2D eye-opening model (lookup table, no f64 runtime needed) |
| `peek64`/`poke64` | RAM-backed pass/fail grid (no heap arrays in freestanding) |
| `cntpct` | PRNG seed + busy-wait delay between training iterations |
| Mutable globals | PHY parameters randomized each loop iteration |
| No-heap rendering | `uart_putc` byte-at-a-time + custom decimal formatter |
| Continuous loop | `while`-forever training with separator + 500 ms pause |
| Full boot pipeline | boot.S → `_start` → `main` (loops forever) |

## Build & run

```sh
make          # build eye.img
make run      # boot interactively in QEMU (Ctrl-A X to quit)
make verify   # headless: assert PHY_TRAIN=OK + EYE_CENTER=
```

Same prerequisites as `qemu_virt`: `angc`, `clang` (aarch64), `ld.bfd`,
`llvm-objcopy`, `qemu-system-aarch64`.
