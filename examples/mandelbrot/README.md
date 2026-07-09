# Mandelbrot Explorer

Interactive Mandelbrot set explorer built with the Angara gui module (Dear ImGui + GLFW).

## Features

- **Pan**: click and drag on the fractal to explore
- **Zoom**: scroll wheel to zoom in/out
- **Colour schemes**: Classic, Fire, Rainbow, and Grayscale
- **Smooth iteration slider**: 50–2000 (higher = more detail, slower render)
- **Reset view** button to jump back to the default framing

## Build & Run

```sh
# From this directory:
make
make run
```

Or from the repo root:

```sh
make -C examples/mandelbrot run
```

## Controls

| Action | Input |
|--------|-------|
| Pan | Left-click + drag on the fractal |
| Zoom | Scroll wheel over the fractal |
| Iterations | Slider in Controls panel |
| Colour scheme | Combo dropdown in Controls panel |
| Reset view | Button in Controls panel |

## How It Works

Each pixel is tested for membership in the Mandelbrot set: starting from
z₀ = 0, iterate zₙ₊₁ = zₙ² + c until |z| > 2 or the iteration limit is
reached. Smooth colouring (log₂ of the escape magnitude) produces
banding-free gradients. The result is drawn pixel-by-pixel via `gui.rect()`
on the background draw list.

Change `_grid_w`, `_grid_h` and `_cell` constants at the top of
`mandelbrot.an` to trade resolution for render speed.
