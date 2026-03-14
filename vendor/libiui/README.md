# libiui

A complete [Material Design 3](https://m3.material.io/) (MD3) implementation in pure C.
Around 10K lines of C99. No dependencies beyond libc, not even for fonts.

Until now, Material Design 3 existed only within the walled gardens of Android (Jetpack Compose),
iOS (via Flutter), and web browsers—platforms where megabytes of runtime overhead are acceptable.
Microcontrollers, real-time systems, and resource-constrained environments had no path to modern UI design.

libiui opens that path.
The same visual language powering billions of Android devices now runs on a Cortex-M4 with 64KB RAM,
compiles to WebAssembly for browser deployment, and integrates into game engines without framework dependencies.
One codebase, one API, identical pixels everywhere.

What makes this possible: zero heap allocations (the application provides a fixed buffer),
no widget trees or retained state (pure immediate-mode), native vector graphics (lines, circles, arcs),
a built-in single-line vector font (no FreeType, no external font files),
and complete renderer abstraction (four callbacks—the library never touches graphics APIs).
The result is predictable memory usage, deterministic performance, and trivial porting.

## Full MD3 Compliance with Configurable Features
libiui implements the complete Material Design 3 specification, not an approximation.
Every dimension, color token, state layer opacity,
and motion curve follows the official guidelines published at [m3.material.io](https://m3.material.io/).
The test suite validates 349 cases against the reference implementation in [material-components-android](https://github.com/material-components/material-components-android).

For embedded systems where every byte counts, libiui uses [Kconfig](https://github.com/sysprog21/Kconfiglib) to let you include only what you need.
Disable animations, accessibility features, or vector drawing (lines, circles, arcs) to shrink the footprint.
Enable them when targeting desktop or web where resources are plentiful.
The same codebase scales from a microcontroller to a full-featured desktop application.

| MD3 Aspect | Specification | Configurable |
|------------|---------------|--------------|
| Color system | 32 semantic tokens, light/dark themes | Always included |
| Typography | MD3 type scale with built-in vector font | Always included |
| Motion | Duration tokens, 7 easing curves | `CONFIG_FEATURE_ANIMATION` |
| Elevation | Levels 0-5, dual shadows | Always included |
| State layers | 8%/12%/12%/16%/38% opacity | Always included |
| Dimensions | Button 40dp, FAB 56dp, Chip 32dp, 48dp touch targets | Always included |
| Accessibility | WCAG 2.1 contrast ratios, focus indicators | `CONFIG_FEATURE_ACCESSIBILITY` |
| Vector graphics | Lines, circles, arcs for custom drawing | `CONFIG_FEATURE_VECTOR` |

## Use Cases
Embedded systems: Runs on microcontrollers with limited RAM.
The application controls all memory through a user-provided buffer.
Kconfig lets you disable features like animation, accessibility, or vector graphics to fit tighter constraints.

Game engine integration: The callback-based renderer slots directly into existing rendering pipelines.
No OpenGL, Vulkan, or DirectX assumptions.
You provide four functions; the library handles the rest.

Lightweight desktop tools: Modern Material Design aesthetics without Qt, GTK, or Electron overhead.
Enable all features for a complete MD3 experience in under 100KB.

Kiosk and industrial HMI: Deterministic memory usage and minimal footprint suit resource-constrained deployments.
The immediate-mode architecture guarantees consistent frame times.

## Platform Support
libiui runs anywhere C runs.
The callback-based renderer abstracts away platform specifics:

| Platform | Backend | Description |
|----------|---------|-------------|
| Desktop (Linux/macOS/Windows) | SDL2 | Software rendering with full input handling |
| Web browsers | WebAssembly | Compile with Emscripten, render to Canvas |
| Embedded | Custom | Implement four callbacks for your display driver |
| Game engines | Native | Slot into existing OpenGL/Vulkan/DirectX pipelines |

A single codebase produces native desktop applications, browser-based tools,
and embedded firmware—all with identical MD3 visuals and behavior.

## Design
Immediate-mode architecture: call `iui_button()`, get a boolean, done.
No widget objects, no tree traversal, no state synchronization.
The widget exists only for the duration of the function call.

Strict MD3 compliance, not "inspired by" Material Design.
Button height 40dp, touch targets 48dp, state layers at exactly 8%/12%/12%/16%/38%.
The test suite verifies these values against the official specification.

## Quick Start
```shell
make                 # Build library and demo
make check           # Run 349 API tests
./libiui_example     # Interactive demo
```

## Configuration
libiui uses [Kconfig](https://github.com/sysprog21/Kconfiglib) for build-time feature selection.
This allows embedded developers to include only the features they need, reducing code size and memory usage.

```shell
make defconfig       # SDL2 backend, all features enabled
make config          # Interactive configuration menu
```

Pre-defined configurations:

| Target | Backend | Purpose |
|--------|---------|---------|
| defconfig | SDL2 | Desktop development with full features |

Available configuration options:

| Option | Description |
|--------|-------------|
| `CONFIG_PORT_SDL2` | SDL2 backend for desktop platforms |
| `CONFIG_PORT_HEADLESS` | Headless backend for CI and testing |
| `CONFIG_PORT_WASM` | WebAssembly backend via Emscripten |
| `CONFIG_FEATURE_ACCESSIBILITY` | WCAG contrast checking, screen reader hints |
| `CONFIG_FEATURE_ANIMATION` | MD3 motion system with easing curves |
| `CONFIG_FEATURE_VECTOR` | Line, circle, and arc drawing primitives |

A minimal embedded configuration disables optional features, yielding a smaller binary that still provides full MD3-compliant widgets.
Use `make config` to customize for your target platform.

## Combining Layout and Components
libiui combines flexible layout with MD3 components in a single immediate-mode API:

```c
#include "iui.h"

static uint8_t buffer[65536];
iui_config_t cfg = iui_make_config(buffer, renderer, 16.0f, NULL);
iui_context *ctx = iui_init(&cfg);

void frame(float dt)
{
    iui_begin_frame(ctx, dt);
    iui_begin_window(ctx, "Settings", 100, 100, 400, 300, 0);

    /* Flex layout: sidebar + content */
    iui_flex(ctx, 2, (float[]){120, -1}, 0, 8);

    /* Sidebar with navigation */
    iui_button(ctx, "General", IUI_ALIGN_LEFT);
    iui_button(ctx, "Audio", IUI_ALIGN_LEFT);
    iui_button(ctx, "Video", IUI_ALIGN_LEFT);
    iui_flex_next(ctx);

    /* Content area */
    static bool enabled = true;
    if (iui_checkbox(ctx, "Enable feature", &enabled))
        printf("Toggled: %s\n", enabled ? "on" : "off");

    static float volume = 50.f;
    iui_slider(ctx, "Volume", 0.f, 100.f, 1.f, &volume, "%.0f%%");

    if (iui_button(ctx, "Apply", IUI_ALIGN_LEFT))
        apply_settings();

    iui_flex_end(ctx);
    iui_end_window(ctx);
    iui_end_frame(ctx);
}
```

This pattern enables responsive layouts with proper component spacing and alignment.

## Components

| Category   | Components |
|------------|------------|
| Basic      | Button, Slider, Divider, Segmented |
| Input      | TextField, Checkbox, Radio, Switch, Dropdown |
| Selection  | Chip (assist/filter/input/suggestion) |
| Container  | Card, Scroll, Bottom sheet |
| List       | One/two/three-line items |
| Navigation | Top app bar, Tabs, Navigation rail/bar/drawer |
| Action     | FAB, Icon buttons |
| Feedback   | Progress, Snackbar, Tooltip, Badge, Banner |
| Overlay    | Menu, Dialog, Date/Time picker |
| Data       | Data table with sortable columns |

The file [tests/example.c](tests/example.c) demonstrates all 34 component types.

## Layout

```c
/* Single column */
iui_button(ctx, "First", IUI_ALIGN_LEFT);
iui_button(ctx, "Second", IUI_ALIGN_LEFT);

/* Flex: negative = ratio, positive = fixed px */
iui_flex(ctx, 3, (float[]){-1, 100, -2}, 40, 4);
iui_button(ctx, "1/3", IUI_ALIGN_LEFT);  iui_flex_next(ctx);
iui_button(ctx, "100px", IUI_ALIGN_LEFT); iui_flex_next(ctx);
iui_button(ctx, "2/3", IUI_ALIGN_LEFT);
iui_flex_end(ctx);

/* Grid: 4 cols, 60x40 cells, 4px gap */
iui_grid_begin(ctx, 4, 60, 40, 4);
for (int i = 0; i < 12; i++) {
    iui_button(ctx, labels[i], IUI_ALIGN_CENTER);
    iui_grid_next(ctx);
}
iui_grid_end(ctx);
```

## Vector Graphics
When `CONFIG_FEATURE_VECTOR` is enabled, libiui provides primitives for custom drawing:

```c
/* Draw a line from (x1,y1) to (x2,y2) */
iui_draw_line(ctx, x1, y1, x2, y2, color);

/* Draw a circle at (cx,cy) with radius r */
iui_draw_circle(ctx, cx, cy, r, color);

/* Draw an arc centered at (cx,cy) from angle start to end */
iui_draw_arc(ctx, cx, cy, r, start_angle, end_angle, color);
```

These primitives enable custom icons, charts, gauges, and decorative elements while maintaining the immediate-mode philosophy.

## Input

```c
iui_update_mouse_pos(ctx, x, y);
iui_update_mouse_buttons(ctx, pressed_mask, released_mask);
iui_update_key(ctx, keycode);
iui_update_char(ctx, codepoint);
```

## Testing

```shell
make check                           # 349 API tests
make check SANITIZERS=1              # AddressSanitizer
python3 scripts/headless-test.py     # Automated UI tests
```

## License
libiui is available under a permissive MIT-style license.
Use of this source code is governed by a MIT license that can be found in the [LICENSE](LICENSE) file.
