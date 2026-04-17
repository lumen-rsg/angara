# Angara ImGui Demo

A complete example of using [Dear ImGui](https://github.com/ocornut/imgui) from Angara via the `imgui` native module.

## Prerequisites

- **Angara Compiler** (`angc`) — [Install from repo root](../../README.md)
- **GLFW3** — `brew install glfw` (macOS) or `apt install libglfw3-dev` (Linux)
- **OpenGL** — System framework on macOS; `libgl1-mesa-dev` on Linux
- **Dear ImGui** — Auto-downloaded by the Makefile on first build

## Quick Start

```bash
# Build and run
make

# Build only
make build

# Clean
make clean
```

## Usage in Angara Code

```angara
attach imgui;

func main() -> i64 {
    // Create a windowed ImGui context
    let ctx = imgui.Context("My App", 1280, 720);
    ctx.style_colors_dark();

    // Main loop
    while (!ctx.window_should_close()) {
        ctx.poll_events();
        ctx.new_frame();

        // Your ImGui widgets here
        if (ctx.begin("Hello")) {
            ctx.text("Hello from Angara!");
            if (ctx.button("Click Me")) {
                // Handle click
            }
        }
        ctx.end();

        ctx.render();
        ctx.swap_buffers();
    }

    ctx.destroy();
    return 0;
}
```

## Available API

### Constructor

| Function | Description |
|----------|-------------|
| `imgui.Context(title, width?, height?)` | Creates a GLFW window + ImGui context |

### Lifecycle

| Method | Returns | Description |
|--------|---------|-------------|
| `ctx.destroy()` | `nil` | Shuts down and cleans up |
| `ctx.new_frame()` | `nil` | Starts a new ImGui frame |
| `ctx.render()` | `nil` | Renders the frame |
| `ctx.swap_buffers()` | `nil` | Swaps front/back buffers |
| `ctx.poll_events()` | `nil` | Polls input events |
| `ctx.window_should_close()` | `bool` | Check if window should close |
| `ctx.get_display_size()` | `record{width, height}` | Gets framebuffer size |
| `ctx.get_fps()` | `f64` | Current framerate |
| `ctx.set_clear_color(r, g, b, a)` | `nil` | Background color (0.0–1.0) |
| `ctx.set_window_title(title)` | `nil` | Changes window title |

### Windows

| Method | Returns | Description |
|--------|---------|-------------|
| `ctx.begin(name)` | `bool` | Start a window (returns visibility) |
| `ctx.end()` | `nil` | End current window |

### Widgets

| Method | Returns | Description |
|--------|---------|-------------|
| `ctx.text(str)` | `nil` | Display text |
| `ctx.text_colored(str, r, g, b, a)` | `nil` | Display colored text |
| `ctx.button(label)` | `bool` | Clickable button |
| `ctx.checkbox(label, value)` | `bool` | Toggle checkbox |
| `ctx.slider_float(label, val, min, max)` | `f64` | Float slider |
| `ctx.slider_int(label, val, min, max)` | `i64` | Integer slider |
| `ctx.input_text(label, value)` | `string` | Single-line text input |
| `ctx.color_edit(label, r, g, b)` | `record{r, g, b}` | Color picker |
| `ctx.progress_bar(fraction)` | `nil` | Progress bar (0.0–1.0) |

### Layout

| Method | Returns | Description |
|--------|---------|-------------|
| `ctx.same_line()` | `nil` | Place next widget on same line |
| `ctx.separator()` | `nil` | Horizontal separator |
| `ctx.spacing()` | `nil` | Add vertical spacing |
| `ctx.indent()` | `nil` | Increase indent |
| `ctx.unindent()` | `nil` | Decrease indent |
| `ctx.newline()` | `nil` | Force new line |

### Style

| Method | Returns | Description |
|--------|---------|-------------|
| `ctx.style_colors_dark()` | `nil` | Dark theme |
| `ctx.style_colors_light()` | `nil` | Light theme |
| `ctx.style_colors_classic()` | `nil` | Classic theme |
| `ctx.set_font_size(size)` | `nil` | Set base font size |

## Architecture

```
modules/imgui.cpp     ← C++ native module (Angara ABI)
modules/imgui.an      ← Angara type declarations
examples/imgui/       ← This example
  ├── demo.an         ← Example Angara source
  ├── Makefile        ← Build configuration
  └── README.md       ← This file
```

The module wraps Dear ImGui's C++ API into Angara-native functions using the `AngaraClassDef` / `AngaraMethodDef` system, similar to the `websocket` module's approach for native instance classes.