# Binary Waterfall

Angara GUI demo — reads a file's raw bytes and renders them as a scrolling "waterfall" of coloured pixels inside a Dear ImGui window.

Each byte value (0x00–0xFF) is mapped to a colour:
- **Greyscale**: byte → shade of grey (0x00 = black, 0xFF = white)
- **Heatmap**: byte → blue → cyan → green → yellow → red gradient

## Prerequisites

- Angara compiler (`angc`) built from the repo root (`make`).
- `gui` module built (`make modules` from the repo root).
- OpenGL-capable display with GL/system dev headers installed.

## Build & Run

```sh
# From this directory:
make
make run
```

Or from the repo root:

```sh
make -C examples/binary_waterfall run
```

## Customisation

Edit the `FILE_PATH` constant in `binary_waterfall.an` to point to any file on disk. On Linux, the default `/proc/self/exe` reads the process's own binary image — a colourful ELF visualisation.

Adjust `WIDTH` and `HEIGHT` constants to change the pixel-grid resolution.

## Controls

- **Heatmap** checkbox — toggle between greyscale and heatmap colouring
- **Speed** slider — rows scrolled per frame (1–20)
- **FPS** display — current frame rate
