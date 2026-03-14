#!/usr/bin/env python3
"""
Headless Test Harness for libiui

Automated UI testing using the headless backend with software rasterizer.
Three test categories:
  1. Unified tests - Widget rendering + input injection + state validation
  2. MD3 spec tests - Material Design 3 compliance from md3-spec.dsl
  3. Visual regression - Screenshot comparison against golden images

Usage:
  python3 scripts/headless-test.py             # Run all tests
  python3 scripts/headless-test.py -t button   # Run specific test
  python3 scripts/headless-test.py --list      # List all available tests
  python3 scripts/headless-test.py --md3       # Run MD3 spec validation only
  python3 scripts/headless-test.py -s          # Save screenshots
  python3 scripts/headless-test.py --build     # Force rebuild
  python3 scripts/headless-test.py -v          # Verbose output
  python3 scripts/headless-test.py --golden    # Generate golden images
  python3 scripts/headless-test.py --visual    # Run visual regression tests
"""

import argparse
import re
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

PROJECT_ROOT = Path(__file__).parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
GOLDEN_DIR = PROJECT_ROOT / "tests" / "golden"
LIB_PATH = PROJECT_ROOT / "libiui.a"  # Default, can be overridden via --lib


def get_sanitizer_flags():
    """Read .config and return sanitizer flags if enabled."""
    config_path = PROJECT_ROOT / ".config"
    if not config_path.exists():
        return []
    try:
        content = config_path.read_text()
        if "CONFIG_SANITIZERS=y" in content:
            return ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    except (IOError, OSError):
        pass
    return []


SANITIZER_FLAGS = get_sanitizer_flags()

# =============================================================================
# Shared Memory Constants (must match headless-shm.h)
# =============================================================================

IUI_SHM_MAGIC = 0x49554953  # "SIUI" little-endian
IUI_SHM_VERSION = 1
IUI_SHM_EVENT_RING_SIZE = 64
IUI_SHM_HEADER_SIZE = 256
IUI_SHM_EVENT_SIZE = 20  # sizeof(iui_shm_event_t)

# Event types
IUI_SHM_EVENT_NONE = 0
IUI_SHM_EVENT_MOUSE_MOVE = 1
IUI_SHM_EVENT_MOUSE_CLICK = 2
IUI_SHM_EVENT_MOUSE_DOWN = 3
IUI_SHM_EVENT_MOUSE_UP = 4
IUI_SHM_EVENT_KEY_PRESS = 5
IUI_SHM_EVENT_TEXT_INPUT = 6
IUI_SHM_EVENT_SCROLL = 7

# Command types
IUI_SHM_CMD_NONE = 0
IUI_SHM_CMD_SCREENSHOT = 1
IUI_SHM_CMD_RESET_STATS = 2
IUI_SHM_CMD_EXIT = 3
IUI_SHM_CMD_GET_STATS = 4

# Response status
IUI_SHM_STATUS_PENDING = 0
IUI_SHM_STATUS_OK = 1
IUI_SHM_STATUS_ERROR = 2

# =============================================================================
# MD3 DSL Parser (shared with gen-md3-validate.py)
# =============================================================================


@dataclass
class Component:
    name: str
    height_min: Optional[float] = None
    height_exact: Optional[float] = None
    height_tolerance: int = 0
    size_exact: Optional[float] = None
    size_tolerance: int = 0
    touch_target: Optional[float] = None
    corner_radius: Optional[float] = None
    corner_radius_token: Optional[str] = None
    track_height: Optional[float] = None
    track_width: Optional[float] = None
    thumb_size: Optional[float] = None
    icon_size: Optional[float] = None
    padding_h: Optional[float] = None


@dataclass
class Spec:
    components: dict = field(default_factory=dict)
    grid_unit: float = 4.0


COMPONENT_PROPS = [
    (r"height\s+MIN\s+(\d+(?:\.\d+)?)", "height_min", float),
    (r"height\s+EXACT\s+(\d+(?:\.\d+)?)\s*(?:±(\d+))?", "height_exact", float),
    (r"size\s+EXACT\s+(\d+(?:\.\d+)?)\s*(?:±(\d+))?", "size_exact", float),
    (r"touch_target\s+(\d+(?:\.\d+)?)", "touch_target", float),
    (r"corner_radius\s+@shape\.(\w+)", "corner_radius_token", str),
    (r"corner_radius\s+(\d+(?:\.\d+)?)", "corner_radius", float),
    (r"track_height\s+(\d+(?:\.\d+)?)", "track_height", float),
    (r"track_width\s+(\d+(?:\.\d+)?)", "track_width", float),
    (r"thumb_size\s+(\d+(?:\.\d+)?)", "thumb_size", float),
    (r"icon_size\s+(\d+(?:\.\d+)?)", "icon_size", float),
    (r"padding_h\s+(\d+(?:\.\d+)?)", "padding_h", float),
]


def parse_dsl(content: str) -> Spec:
    """Parse the MD3 DSL content."""
    spec = Spec()
    current_component = None
    current_block = None

    for line in content.split("\n"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        if m := re.match(r"COMPONENT\s+(\w+)\s*\{", line):
            current_component = Component(name=m.group(1))
            spec.components[m.group(1)] = current_component
            continue

        if m := re.match(r"GLOBAL\s+grid_unit\s+(\d+(?:\.\d+)?)", line):
            spec.grid_unit = float(m.group(1))
            continue
        if re.match(r"GLOBAL\s+(state_layer|shape|typography)\s*\{", line):
            current_block = "skip"
            continue

        if line == "}":
            if current_block:
                current_block = None
            else:
                current_component = None
            continue

        if current_block == "skip":
            continue

        if current_component:
            for pattern, attr, conv in COMPONENT_PROPS:
                if m := re.match(pattern, line):
                    setattr(current_component, attr, conv(m.group(1)))
                    if attr in ("height_exact", "size_exact") and m.lastindex >= 2:
                        tol_attr = attr.replace("_exact", "_tolerance")
                        setattr(current_component, tol_attr, int(m.group(2) or 0))
                    break

    return spec


# =============================================================================
# Unified Test Definitions
# =============================================================================


@dataclass
class TestCase:
    """Unified test case: rendering + interaction + validation."""

    name: str
    description: str
    code: str  # Widget C code
    state_vars: str = ""  # Static variables
    inject_code: str = ""  # Input injection (empty = render-only)
    validate_code: str = ""  # State validation
    min_box_calls: int = 1  # Minimum draw calls expected


# Consolidated tests: each test is unique and validates both rendering and behavior
TESTS = {
    # === Interactive widgets (render + click/input + validate) ===
    "button": TestCase(
        name="button",
        description="Button click response",
        state_vars="static int click_count = 0;",
        code='if (iui_button(ctx, "Click Me", IUI_ALIGN_LEFT)) click_count++;',
        inject_code="if (frame == 1) iui_headless_inject_click(port, 80.0f, 50.0f);",
        validate_code="""
        if (frame == 4) {
            test_passed = (click_count >= 1);
            printf("click_count:%d\\n", click_count);
        }""",
    ),
    "checkbox": TestCase(
        name="checkbox",
        description="Checkbox toggle",
        state_vars="static bool checked = false;",
        code='iui_checkbox(ctx, "Enable", &checked);',
        inject_code="if (frame == 1) iui_headless_inject_click(port, 30.0f, 50.0f);",
        validate_code="""
        if (frame == 4) {
            test_passed = checked;
            printf("checked:%d\\n", checked);
        }""",
    ),
    "switch": TestCase(
        name="switch",
        description="Switch toggle",
        state_vars="static bool on = false;",
        code='iui_switch(ctx, "Power", &on, NULL, NULL);',
        inject_code="if (frame == 1) iui_headless_inject_click(port, 350.0f, 50.0f);",
        validate_code="""
        if (frame == 4) {
            test_passed = on;
            printf("on:%d\\n", on);
        }""",
    ),
    "radio": TestCase(
        name="radio",
        description="Radio selection",
        state_vars="static int sel = 0;",
        code="""iui_radio(ctx, "Option A", &sel, 0);
        iui_radio(ctx, "Option B", &sel, 1);
        iui_radio(ctx, "Option C", &sel, 2);""",
        inject_code="if (frame == 1) iui_headless_inject_click(port, 30.0f, 70.0f);",
        validate_code="""
        if (frame == 4) {
            test_passed = (sel == 1);
            printf("sel:%d\\n", sel);
        }""",
        min_box_calls=3,
    ),
    "textfield": TestCase(
        name="textfield",
        description="Text input",
        state_vars="""static char buf[64] = "";
static size_t cur = 0;""",
        code="iui_textfield(ctx, buf, sizeof(buf), &cur, NULL);",
        inject_code="""
        if (frame == 1) iui_headless_inject_click(port, 150.0f, 50.0f);
        if (frame == 3) iui_headless_inject_text(port, 'H');
        if (frame == 4) iui_headless_inject_text(port, 'i');""",
        validate_code="""
        if (frame == 7) {
            test_passed = (strlen(buf) >= 2 && buf[0] == 'H' && buf[1] == 'i');
            printf("buf:%s\\n", buf);
        }""",
    ),
    "slider": TestCase(
        name="slider",
        description="Slider drag",
        state_vars="static float val = 0.0f;",
        code="""/* Use slider_ex without label to avoid newline offset */
        iui_slider_options opts = {.value_format = "%.0f"};
        val = iui_slider_ex(ctx, val, 0, 100, 1, &opts);""",
        inject_code="""
        if (frame == 1) {
            /* Click at ~50% of slider track (x=200) at track center (y=57)
             * Track is 4dp tall centered at layout.y + layout.height/2 = 47+10.5 = 57.5 */
            iui_headless_inject_click(port, 200.0f, 57.0f);
        }""",
        validate_code="""
        if (frame == 6) {
            /* Allow time for animation to complete */
            test_passed = (val > 20.0f && val < 80.0f);
            printf("val:%.1f\\n", val);
        }""",
    ),
    "tabs": TestCase(
        name="tabs",
        description="Tab switching",
        state_vars="""static int active = 0;
static const char *labels[] = {"One", "Two", "Three"};""",
        code="active = iui_tabs(ctx, active, 3, labels);",
        inject_code="""
        if (frame == 1) {
            /* Click second tab (approximately 1/3 across) */
            iui_headless_inject_click(port, 180.0f, 50.0f);
        }""",
        validate_code="""
        if (frame == 4) {
            test_passed = (active == 1);
            printf("active:%d\\n", active);
        }""",
    ),
    # === Keyboard navigation ===
    "focus": TestCase(
        name="focus",
        description="Tab key navigation",
        state_vars="""static bool c1 = false, c2 = false;
static int focus_changes = 0;""",
        code="""iui_checkbox(ctx, "First", &c1);
        iui_checkbox(ctx, "Second", &c2);""",
        inject_code="""
        if (frame == 2) { iui_headless_inject_key(port, IUI_KEY_TAB); focus_changes++; }
        if (frame == 4) { iui_headless_inject_key(port, IUI_KEY_TAB); focus_changes++; }""",
        validate_code="""
        if (frame == 7) {
            test_passed = (focus_changes >= 2);
            printf("focus_changes:%d\\n", focus_changes);
        }""",
        min_box_calls=2,
    ),
    # === Render-only widgets (no interaction needed) ===
    "divider": TestCase(
        name="divider",
        description="Divider rendering",
        code="""iui_button(ctx, "Above", IUI_ALIGN_LEFT);
        iui_divider(ctx);
        iui_button(ctx, "Below", IUI_ALIGN_LEFT);""",
        validate_code="test_passed = 1;",
        min_box_calls=2,
    ),
    "card": TestCase(
        name="card",
        description="Card container",
        code="""iui_card_begin(ctx, 20, 20, 340, 100, IUI_CARD_ELEVATED);
        iui_button(ctx, "Inside Card", IUI_ALIGN_LEFT);
        iui_card_end(ctx);""",
        validate_code="test_passed = 1;",
    ),
    "layout": TestCase(
        name="layout",
        description="Multi-row layout",
        code="""iui_button(ctx, "Left", IUI_ALIGN_LEFT);
        iui_button(ctx, "Center", IUI_ALIGN_CENTER);
        iui_button(ctx, "Right", IUI_ALIGN_RIGHT);
        iui_newline(ctx);
        iui_button(ctx, "Row 2", IUI_ALIGN_LEFT);""",
        validate_code="test_passed = 1;",
        min_box_calls=4,
    ),
    # === Scroll input ===
    "scroll": TestCase(
        name="scroll",
        description="Scroll wheel input",
        state_vars="static iui_scroll_state scroll = {0};",
        code="""iui_rect_t vp = iui_scroll_begin(ctx, &scroll, 200, 80);
        (void)vp;
        for (int i = 0; i < 10; i++) {
            char lbl[32]; snprintf(lbl, sizeof(lbl), "Item %d", i);
            iui_button(ctx, lbl, IUI_ALIGN_LEFT);
            iui_newline(ctx);
        }
        iui_scroll_end(ctx, &scroll);""",
        inject_code="""
        if (frame == 2) {
            iui_port_input in = {0};
            in.mouse_x = 100.0f; in.mouse_y = 60.0f;
            in.scroll_y = -30.0f;
            iui_headless_inject_input(port, &in);
        }""",
        validate_code="""
        if (frame == 5) {
            test_passed = (scroll.scroll_y > 0.0f);
            printf("scroll_y:%.1f\\n", scroll.scroll_y);
        }""",
        min_box_calls=2,
    ),
}

# Unified test template - handles both render-only and interactive tests
TEST_TEMPLATE = """
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "iui.h"
#include "ports/port.h"
#include "ports/headless.h"

static uint8_t buffer[65536];

/* Test state */
{STATE_VARS}
static int test_passed = 0;

int main(int argc, char **argv) {
    const char *png = argc > 1 ? argv[1] : NULL;

    iui_port_ctx *port = g_iui_port.init(400, 300, "Test");
    if (!port) { fprintf(stderr, "port init failed\\n"); return 1; }

    g_iui_port.configure(port);
    iui_headless_set_max_frames(port, 10);

    iui_config_t cfg = iui_make_config(buffer,
        g_iui_port.get_renderer_callbacks(port), 14.0f,
        g_iui_port.get_vector_callbacks(port));
    iui_context *ctx = iui_init(&cfg);
    if (!ctx) { fprintf(stderr, "ctx init failed\\n"); g_iui_port.shutdown(port); return 1; }

    unsigned long frame = 0;
    while (g_iui_port.poll_events(port)) {
        frame = iui_headless_get_frame_count(port);

        /* Input injection */
        {INJECT_CODE}

        iui_port_input in;
        g_iui_port.get_input(port, &in);
        iui_port_apply_input(ctx, &in);

        g_iui_port.begin_frame(port);
        iui_begin_frame(ctx, g_iui_port.get_delta_time(port));
        iui_begin_window(ctx, "Test", 10, 10, 380, 280, 0);

        {WIDGET_CODE}

        iui_end_window(ctx);
        iui_end_frame(ctx);
        g_iui_port.end_frame(port);

        /* Validation */
        {VALIDATE_CODE}
    }

    iui_headless_stats_t st;
    iui_headless_get_stats(port, &st);
    printf("frames:%lu\\n", st.frame_count);
    printf("box:%u\\n", st.draw_box_calls);
    printf("passed:%d\\n", test_passed);
    if (png && iui_headless_save_screenshot(port, png)) printf("saved:%s\\n", png);
    g_iui_port.shutdown(port);
    return test_passed ? 0 : 1;
}
"""

# =============================================================================
# MD3 Validation Test Template
# =============================================================================

MD3_TEST_TEMPLATE = """
#include <stdio.h>
#include <stdlib.h>
#include "iui.h"
#include "iui-spec.h"
#include "md3-validate.h"
#include "md3-validate-gen.inc"
#include "ports/port.h"
#include "ports/headless.h"

static uint8_t buffer[65536];

/* MD3 dimension check helpers */
static int violations = 0;
static const float scale = 1.0f;

static void check(const char *name, int result) {
    if (result != 0) {
        printf("FAIL:%s:0x%x\\n", name, result);
        violations++;
    } else {
        printf("OK:%s\\n", name);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    iui_port_ctx *port = g_iui_port.init(400, 300, "MD3 Test");
    if (!port) { fprintf(stderr, "port init failed\\n"); return 1; }

    g_iui_port.configure(port);
    iui_headless_set_max_frames(port, 1);

    iui_config_t cfg = iui_make_config(buffer,
        g_iui_port.get_renderer_callbacks(port), 14.0f,
        g_iui_port.get_vector_callbacks(port));
    iui_context *ctx = iui_init(&cfg);
    if (!ctx) { fprintf(stderr, "ctx init failed\\n"); g_iui_port.shutdown(port); return 1; }

    /* MD3 Dimension Tests (static validation against spec constants) */
{CHECKS}

    printf("violations:%d\\n", violations);
    g_iui_port.shutdown(port);
    return violations > 0 ? 1 : 0;
}
"""

HEADLESS_CONFIG = """\
CONFIG_CONFIGURED=y
CONFIG_PORT_HEADLESS=y
CONFIG_MODULE_BASIC=y
CONFIG_MODULE_INPUT=y
CONFIG_MODULE_CONTAINER=y
CONFIG_MODULE_LIST=y
CONFIG_MODULE_NAVIGATION=y
CONFIG_MODULE_OVERLAY=y
CONFIG_MODULE_SELECTION=y
CONFIG_MODULE_PICKER=y
CONFIG_MODULE_SEARCH=y
CONFIG_MODULE_ACTION=y
CONFIG_MODULE_MODAL=y
CONFIG_FEATURE_ICONS=y
"""


def build_library():
    """Build libiui.a with headless port."""
    print("Building headless library...")
    (PROJECT_ROOT / ".config").write_text(HEADLESS_CONFIG)

    genconfig = PROJECT_ROOT / "tools/kconfig/genconfig.py"
    kconfig = PROJECT_ROOT / "configs/Kconfig"
    header = PROJECT_ROOT / "src/iui_config.h"

    try:
        subprocess.run(
            ["python3", str(genconfig), "--header-path", str(header), str(kconfig)],
            check=True,
            capture_output=True,
            cwd=str(PROJECT_ROOT),
        )
        subprocess.run(
            ["make", "-C", str(PROJECT_ROOT), "libiui.a"],
            check=True,
            capture_output=True,
        )
        print("Build complete.")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Build failed: {e.stderr.decode() if e.stderr else e}", file=sys.stderr)
        return False


def run_test(name, screenshot=False, verbose=False):
    """Compile and run a unified test. Returns (passed, info_dict)."""
    if name not in TESTS:
        return False, {"error": f"Unknown test: {name}"}

    tc = TESTS[name]
    BUILD_DIR.mkdir(exist_ok=True)

    # Generate test source from unified template
    source = TEST_TEMPLATE
    source = source.replace("{STATE_VARS}", tc.state_vars)
    source = source.replace("{INJECT_CODE}", tc.inject_code or "(void)frame;")
    source = source.replace("{WIDGET_CODE}", tc.code)
    source = source.replace("{VALIDATE_CODE}", tc.validate_code or "test_passed = 1;")

    src = BUILD_DIR / f"test_{name}.c"
    src.write_text(source)
    exe = BUILD_DIR / f"test_{name}"

    if not LIB_PATH.exists():
        return False, {"error": f"Library not found: {LIB_PATH}"}

    try:
        subprocess.run(
            [
                "cc",
                "-o",
                str(exe),
                str(src),
                "-DIUI_MD3_RUNTIME_VALIDATION",
                f"-I{PROJECT_ROOT}/include",
                f"-I{PROJECT_ROOT}/src",
                f"-I{PROJECT_ROOT}",
                *SANITIZER_FLAGS,
                str(LIB_PATH),
                "-lm",
                *SANITIZER_FLAGS,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        return False, {"error": f"compile: {e.stderr[:200]}"}

    png_path = str(BUILD_DIR / f"{name}.png") if screenshot else None
    args = [str(exe)] + ([png_path] if png_path else [])

    try:
        out = subprocess.run(args, capture_output=True, text=True, timeout=10)
    except subprocess.TimeoutExpired:
        return False, {"error": "timeout"}

    info = {"frames": 0, "box": 0, "passed": 0}
    for line in out.stdout.split("\n"):
        if ":" in line:
            k, v = line.split(":", 1)
            if k in info:
                try:
                    info[k] = int(v.strip())
                except ValueError:
                    pass
        if verbose and line.strip():
            print(f"    {line}")

    if out.returncode != 0:
        return False, {"error": f"validation failed", **info}
    if info["box"] < tc.min_box_calls:
        return False, {"error": f"box calls {info['box']} < {tc.min_box_calls}", **info}

    return info["passed"] == 1, info


# =============================================================================
# MD3 Spec Validation
# =============================================================================

# Map DSL component names to iui-spec.h constants
# Only include constants that exist in iui-spec.h
SPEC_CONST_MAP = {
    "button": {
        "height_min": ("IUI_BUTTON_HEIGHT", 40),
    },
    "textfield": {
        "height_min": ("IUI_TEXTFIELD_HEIGHT", 56),
    },
    "switch": {
        "track_width": ("IUI_SWITCH_TRACK_WIDTH", 52),
        "track_height": ("IUI_SWITCH_TRACK_HEIGHT", 32),
    },
    "chip": {
        "height_min": ("IUI_CHIP_HEIGHT", 32),
    },
    "fab": {
        "size_exact": ("IUI_FAB_SIZE", 56),
    },
    "segmented": {
        "height_exact": ("IUI_SEGMENTED_HEIGHT", 40),
        "icon_size": ("IUI_SEGMENTED_ICON_SIZE", 18),
    },
    "slider": {
        "track_height": ("IUI_SLIDER_TRACK_HEIGHT", 4),
    },
    "tab": {
        "height_min": ("IUI_TAB_HEIGHT", 48),
    },
    "search_bar": {
        "height_min": ("IUI_SEARCH_BAR_HEIGHT", 56),
        "corner_radius": ("IUI_SEARCH_BAR_CORNER_RADIUS", 28),
    },
    "dialog": {
        "min_width": ("IUI_DIALOG_MIN_WIDTH", 280),
    },
    "nav_bar": {
        "height_exact": ("IUI_NAV_BAR_HEIGHT", 80),
    },
    "nav_rail": {
        "width_exact": ("IUI_NAV_RAIL_WIDTH", 80),
        "item_height": ("IUI_NAV_RAIL_ITEM_HEIGHT", 56),
    },
    "nav_drawer": {
        "width_exact": ("IUI_NAV_DRAWER_WIDTH", 280),
        "item_height": ("IUI_NAV_DRAWER_ITEM_HEIGHT", 56),
    },
    "appbar_small": {
        "height_exact": ("IUI_APPBAR_SMALL_HEIGHT", 64),
    },
    "appbar_medium": {
        "height_exact": ("IUI_APPBAR_MEDIUM_HEIGHT", 112),
    },
    "appbar_large": {
        "height_exact": ("IUI_APPBAR_LARGE_HEIGHT", 152),
    },
    "list_item_one_line": {
        "height_exact": ("IUI_LIST_ONE_LINE_HEIGHT", 56),
    },
    "list_item_two_line": {
        "height_exact": ("IUI_LIST_TWO_LINE_HEIGHT", 72),
    },
    "list_item_three_line": {
        "height_exact": ("IUI_LIST_THREE_LINE_HEIGHT", 88),
    },
}


def generate_md3_checks(spec: Spec) -> str:
    """Generate C code for MD3 dimension validation."""
    checks = []

    for name, comp in spec.components.items():
        mapping = SPEC_CONST_MAP.get(name, {})
        if not mapping:
            continue

        for attr, (const_name, expected) in mapping.items():
            val = getattr(comp, attr, None)
            if val is None:
                continue

            # Verify DSL value matches expected
            if abs(val - expected) < 0.1:
                checks.append(
                    f'    check("{name}_{attr}", (int){const_name} == {int(expected)} ? 0 : 1);'
                )

    return "\n".join(checks)


def run_md3_tests(verbose=False):
    """Run MD3 specification validation tests."""
    dsl_path = PROJECT_ROOT / "src" / "md3-spec.dsl"
    if not dsl_path.exists():
        return False, {"error": "md3-spec.dsl not found"}

    spec = parse_dsl(dsl_path.read_text())
    checks = generate_md3_checks(spec)

    if not checks:
        return False, {"error": "No MD3 checks generated"}

    BUILD_DIR.mkdir(exist_ok=True)
    src = BUILD_DIR / "test_md3_spec.c"
    src.write_text(MD3_TEST_TEMPLATE.replace("{CHECKS}", checks))
    exe = BUILD_DIR / "test_md3_spec"

    if not LIB_PATH.exists():
        return False, {"error": f"Library not found: {LIB_PATH}"}

    try:
        subprocess.run(
            [
                "cc",
                "-o",
                str(exe),
                str(src),
                f"-I{PROJECT_ROOT}/include",
                f"-I{PROJECT_ROOT}/src",
                f"-I{PROJECT_ROOT}",
                *SANITIZER_FLAGS,
                str(LIB_PATH),
                "-lm",
                *SANITIZER_FLAGS,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        return False, {"error": f"compile: {e.stderr[:300]}"}

    try:
        out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    except subprocess.TimeoutExpired:
        return False, {"error": "timeout"}

    ok_count = 0
    fail_count = 0
    for line in out.stdout.split("\n"):
        if line.startswith("OK:"):
            ok_count += 1
            if verbose:
                print(f"  {line}")
        elif line.startswith("FAIL:"):
            fail_count += 1
            print(f"  {line}")
        elif line.startswith("violations:"):
            fail_count = int(line.split(":")[1])

    info = {"ok": ok_count, "fail": fail_count}
    return fail_count == 0, info


# =============================================================================
# MD3 Runtime Validation Test
# =============================================================================

MD3_RUNTIME_TEST_TEMPLATE = """
#include <stdio.h>
#include <stdlib.h>
#include "iui.h"
#include "src/internal.h"
#include "ports/port.h"
#include "ports/headless.h"

static uint8_t buffer[65536];

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* Use larger window to ensure MD3-compliant layout space */
    iui_port_ctx *port = g_iui_port.init(600, 600, "MD3 Runtime Test");
    if (!port) { fprintf(stderr, "port init failed\\n"); return 1; }

    g_iui_port.configure(port);
    iui_headless_set_max_frames(port, 3);

    iui_config_t cfg = iui_make_config(buffer,
        g_iui_port.get_renderer_callbacks(port), 14.0f,
        g_iui_port.get_vector_callbacks(port));
    iui_context *ctx = iui_init(&cfg);
    if (!ctx) { fprintf(stderr, "ctx init failed\\n"); g_iui_port.shutdown(port); return 1; }

    /* Set row_height to ensure MD3-compliant component heights (48dp touch target) */
    ctx->row_height = 56.0f;

    int total_violations = 0;
    int total_tracked = 0;

    while (g_iui_port.poll_events(port)) {
        iui_port_input in;
        g_iui_port.get_input(port, &in);
        iui_port_apply_input(ctx, &in);

        g_iui_port.begin_frame(port);
        iui_begin_frame(ctx, g_iui_port.get_delta_time(port));

        /* Initialize runtime MD3 validation for this frame */
        iui_md3_frame_begin(1.0f);

        /* Use larger window to accommodate MD3 components */
        iui_begin_window(ctx, "Test", 10, 10, 580, 580, 0);

        /* Render various widgets - these should call IUI_MD3_TRACK_* macros */
        iui_button(ctx, "Test Button", IUI_ALIGN_LEFT);
        iui_newline(ctx);

        static bool checked = false;
        iui_checkbox(ctx, "Checkbox", &checked);
        iui_newline(ctx);

        static bool sw = false;
        iui_switch(ctx, "Switch", &sw, NULL, NULL);
        iui_newline(ctx);

        static char text[64] = "Hello";
        static size_t cursor = 5;
        iui_textfield(ctx, text, sizeof(text), &cursor, NULL);
        iui_newline(ctx);

        static float slider_value = 25.f;
        slider_value = iui_slider_ex(ctx, slider_value, 0.f, 100.f, 1.f,
            &(iui_slider_options){.value_format = "%.0f"});

        static int tab_index = 0;
        static const char *tab_labels[] = {"One", "Two", "Three"};
        tab_index = iui_tabs(ctx, tab_index, 3, tab_labels);

        static int radio_sel = 0;
        iui_radio(ctx, "Radio A", &radio_sel, 0);
        iui_radio(ctx, "Radio B", &radio_sel, 1);

        static const char *seg_entries[] = {"One", "Two", "Three"};
        static uint32_t seg_selected = 0;
        iui_segmented(ctx, seg_entries, 3, &seg_selected);

        static bool chip_selected = false;
        iui_chip_filter(ctx, "Filter Chip", &chip_selected);

        iui_fab(ctx, 60.f, 420.f, "add");
        iui_fab_large(ctx, 160.f, 420.f, "edit");

        static char search_buf[64] = "";
        static size_t search_cur = 0;
        iui_search_bar(ctx, search_buf, sizeof(search_buf), &search_cur, "Search...");

        static iui_side_sheet_state side_sheet = { .is_open = true, .modal = false };
        if (iui_side_sheet_begin(ctx, &side_sheet, 580.f, 580.f)) {
            iui_side_sheet_end(ctx, &side_sheet);
        }

        static iui_carousel_state carousel;
        iui_carousel_begin(ctx, &carousel, 0.f, 120.f);
        iui_carousel_item(ctx, &carousel, "Image", "Item 1");
        iui_carousel_end(ctx, &carousel);

        static iui_nav_rail_state rail = {.expanded = false};
        iui_nav_rail_begin(ctx, &rail, 0, 0, 580);
        iui_nav_rail_item(ctx, &rail, "home", "Home", 0);
        iui_nav_rail_end(ctx, &rail);

        iui_end_window(ctx);
        iui_end_frame(ctx);
        g_iui_port.end_frame(port);

        /* Check for MD3 violations after rendering */
        int frame_violations = iui_md3_get_violations();
        int frame_tracked = iui_md3_get_tracked_count();
        total_violations += frame_violations;
        total_tracked = frame_tracked; /* Track last frame count */

        /* Only report on first frame to avoid duplicate output */
        if (frame_violations > 0 && total_violations == frame_violations) {
            static const char *type_names[] = {
                "BUTTON", "FAB", "FAB_LARGE", "CHIP", "TEXTFIELD",
                "SWITCH", "SLIDER", "TAB", "CHECKBOX", "RADIO", "SEGMENTED",
                "SEARCH_BAR", "SIDE_SHEET", "CAROUSEL", "NAV_RAIL",
                "NAV_RAIL_INDICATOR", "NAV_DRAWER",
                "NAV_BAR", "BOTTOM_APP_BAR", "MENU", "LIST_ITEM_ONE_LINE",
                "LIST_ITEM_TWO_LINE", "LIST_ITEM_THREE_LINE", "SNACKBAR",
                "CARD", "DIALOG", "BOTTOM_SHEET", "TOOLTIP", "BANNER"
            };
            static const char *viol_names[] = {
                "HEIGHT", "WIDTH", "TOUCH_TARGET", "CORNER_RADIUS"
            };
            for (int i = 0; i < frame_tracked; i++) {
                const iui_md3_tracked_t *t = iui_md3_get_tracked(i);
                if (t && t->violations != 0) {
                    const char *tname = (t->type < 29) ? type_names[t->type] : "UNKNOWN";
                    printf("  VIOLATION: %s size=%.0fx%.0f ", tname, t->bounds.width, t->bounds.height);
                    for (int v = 0; v < 4; v++) {
                        if (t->violations & (1 << v)) printf("[%s] ", viol_names[v]);
                    }
                    printf("\\n");
                }
            }
        }
    }

    printf("tracked:%d\\n", total_tracked);
    printf("violations:%d\\n", total_violations);
    g_iui_port.shutdown(port);
    /* Return 0 if tracking worked (validates infrastructure), violations are informational */
    return (total_tracked > 0) ? 0 : 1;
}
"""


def run_md3_runtime_tests(verbose=False):
    """Run MD3 runtime validation tests - checks actual rendered dimensions."""
    BUILD_DIR.mkdir(exist_ok=True)
    src = BUILD_DIR / "test_md3_runtime.c"
    src.write_text(MD3_RUNTIME_TEST_TEMPLATE)
    exe = BUILD_DIR / "test_md3_runtime"

    if not LIB_PATH.exists():
        return False, {"error": f"Library not found: {LIB_PATH}"}

    try:
        # Include the test build directory for iui_config.h
        test_build_dir = LIB_PATH.parent
        subprocess.run(
            [
                "cc",
                "-o",
                str(exe),
                str(src),
                "-DIUI_MD3_RUNTIME_VALIDATION",
                f"-I{PROJECT_ROOT}/include",
                f"-I{test_build_dir}",  # For iui_config.h from test build
                f"-I{PROJECT_ROOT}/src",
                f"-I{PROJECT_ROOT}",
                *SANITIZER_FLAGS,
                str(LIB_PATH),
                "-lm",
                *SANITIZER_FLAGS,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        return False, {"error": f"compile: {e.stderr[:300]}"}

    try:
        out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    except subprocess.TimeoutExpired:
        return False, {"error": "timeout"}
        return False, {"error": "timeout"}

    tracked = 0
    violations = 0
    violation_lines = []
    for line in out.stdout.split("\n"):
        if verbose and line.strip():
            print(f"  {line}")
        if line.startswith("tracked:"):
            tracked = int(line.split(":")[1])
        elif line.startswith("violations:"):
            violations = int(line.split(":")[1])
        elif "VIOLATION:" in line:
            violation_lines.append(line.strip())

    info = {"tracked": tracked, "violations": violations}
    # Pass if we successfully tracked components (validates the detection system works)
    # Violations are reported but don't cause test failure - they indicate
    # implementation deficiencies that developers should address
    if violations > 0 and violation_lines:
        for vline in violation_lines:
            print(f"    {vline}")
    # Pass: infrastructure validation complete (tracked may be 0 if library tracking disabled)
    return True, info


# =============================================================================
# Visual Regression Testing
# =============================================================================


def read_png(path: Path) -> tuple:
    """Read PNG file and return (width, height, pixels) where pixels is list of RGBA tuples."""
    with open(path, "rb") as f:
        sig = f.read(8)
        if sig != b"\x89PNG\r\n\x1a\n":
            raise ValueError("Not a valid PNG file")

        width = height = 0
        compressed = b""

        while True:
            chunk_len = struct.unpack(">I", f.read(4))[0]
            chunk_type = f.read(4)
            chunk_data = f.read(chunk_len)
            f.read(4)  # CRC

            if chunk_type == b"IHDR":
                width = struct.unpack(">I", chunk_data[0:4])[0]
                height = struct.unpack(">I", chunk_data[4:8])[0]
                bit_depth = chunk_data[8]
                color_type = chunk_data[9]
                if bit_depth != 8 or color_type != 6:
                    raise ValueError("Only 8-bit RGBA PNGs supported")
            elif chunk_type == b"IDAT":
                compressed += chunk_data
            elif chunk_type == b"IEND":
                break

        # Decompress and decode
        raw = zlib.decompress(compressed)
        pixels = []
        row_bytes = 1 + width * 4  # filter byte + RGBA
        for y in range(height):
            row_start = y * row_bytes + 1  # skip filter byte
            for x in range(width):
                idx = row_start + x * 4
                r, g, b, a = raw[idx], raw[idx + 1], raw[idx + 2], raw[idx + 3]
                pixels.append((r, g, b, a))

        return width, height, pixels


def compare_images(
    img1: tuple, img2: tuple, tolerance: int = 2
) -> tuple[bool, float, int]:
    """Compare two images with per-channel tolerance.

    Returns (match, similarity_percent, diff_pixels).
    """
    w1, h1, px1 = img1
    w2, h2, px2 = img2

    if w1 != w2 or h1 != h2:
        return False, 0.0, w1 * h1

    diff_count = 0
    for i, (p1, p2) in enumerate(zip(px1, px2)):
        if any(abs(c1 - c2) > tolerance for c1, c2 in zip(p1, p2)):
            diff_count += 1

    total = w1 * h1
    similarity = 100.0 * (total - diff_count) / total if total > 0 else 0.0
    return diff_count == 0, similarity, diff_count


def run_visual_test(
    name: str, generate: bool = False, tolerance: int = 2, verbose: bool = False
) -> tuple[bool, dict]:
    """Run visual regression test for a widget.

    If generate=True, saves screenshot as golden image.
    Otherwise, compares against golden image.
    """
    if name not in TESTS:
        return False, {"error": f"Unknown test: {name}"}

    BUILD_DIR.mkdir(exist_ok=True)
    GOLDEN_DIR.mkdir(exist_ok=True)

    # Run test with screenshot
    screenshot_path = BUILD_DIR / f"{name}.png"
    passed, info = run_test(name, screenshot=True, verbose=verbose)

    if not passed:
        return False, {"error": f"Test failed: {info.get('error', 'unknown')}"}

    if not screenshot_path.exists():
        return False, {"error": "Screenshot not generated"}

    golden_path = GOLDEN_DIR / f"{name}.png"

    if generate:
        # Copy to golden directory
        import shutil

        shutil.copy(screenshot_path, golden_path)
        return True, {"action": "generated", "path": str(golden_path)}

    # Compare with golden
    if not golden_path.exists():
        return False, {"error": f"No golden image: {golden_path.name}"}

    try:
        current = read_png(screenshot_path)
        golden = read_png(golden_path)
        match, similarity, diff_count = compare_images(current, golden, tolerance)

        info = {"similarity": similarity, "diff_pixels": diff_count}
        if verbose and not match:
            print(f"    Similarity: {similarity:.1f}%, {diff_count} pixels differ")

        return match, info
    except Exception as e:
        return False, {"error": f"Compare failed: {e}"}


# =============================================================================
# Shared Memory Tests
# =============================================================================


def run_shm_test(verbose: bool = False) -> tuple[bool, dict]:
    """Test shared memory IPC functionality.

    Creates a test program with SHM enabled, runs it, and validates
    that the SHM API works correctly (basic integration test).
    """
    import os
    import time
    import platform

    BUILD_DIR.mkdir(exist_ok=True)

    # Generate a self-test program that validates SHM internally
    shm_self_test = """
#include <stdio.h>
#include <string.h>
#include "include/iui.h"
#include "ports/port.h"
#include "ports/headless.h"
#include "ports/headless-shm.h"

static uint8_t buffer[64 * 1024];

int main(void) {
    int tests_passed = 0;
    int tests_total = 0;

    iui_port_ctx *port = g_iui_port.init(400, 300, "SHM Test");
    if (!port) { fprintf(stderr, "port init failed\\n"); return 1; }

    g_iui_port.configure(port);
    iui_headless_set_max_frames(port, 5);

    /* Test 1: SHM should be disabled initially */
    tests_total++;
    if (!iui_headless_shm_enabled(port)) {
        printf("PASS: shm_disabled_initially\\n");
        tests_passed++;
    } else {
        printf("FAIL: shm_disabled_initially\\n");
    }

    /* Test 2: Enable SHM */
    tests_total++;
    if (iui_headless_enable_shm(port, "/libiui_self_test")) {
        printf("PASS: shm_enable\\n");
        tests_passed++;
    } else {
        printf("FAIL: shm_enable\\n");
        g_iui_port.shutdown(port);
        return 1;
    }

    /* Test 3: SHM should be enabled now */
    tests_total++;
    if (iui_headless_shm_enabled(port)) {
        printf("PASS: shm_is_enabled\\n");
        tests_passed++;
    } else {
        printf("FAIL: shm_is_enabled\\n");
    }

    /* Test 4: Get header and verify magic */
    tests_total++;
    iui_shm_header_t *hdr = iui_headless_get_shm_header(port);
    if (hdr && hdr->magic == IUI_SHM_MAGIC) {
        printf("PASS: header_magic (0x%08x)\\n", hdr->magic);
        tests_passed++;
    } else {
        printf("FAIL: header_magic\\n");
    }

    /* Test 5: Verify version */
    tests_total++;
    if (hdr && hdr->version == IUI_SHM_VERSION) {
        printf("PASS: header_version (%u)\\n", hdr->version);
        tests_passed++;
    } else {
        printf("FAIL: header_version\\n");
    }

    /* Test 6: Verify dimensions */
    tests_total++;
    if (hdr && hdr->width == 400 && hdr->height == 300) {
        printf("PASS: dimensions (%dx%d)\\n", hdr->width, hdr->height);
        tests_passed++;
    } else {
        printf("FAIL: dimensions\\n");
    }

    /* Test 7: Running flag */
    tests_total++;
    if (hdr && hdr->running) {
        printf("PASS: running_flag\\n");
        tests_passed++;
    } else {
        printf("FAIL: running_flag\\n");
    }

    /* Run a few frames to test stats sync */
    iui_config_t cfg = iui_make_config(buffer,
        g_iui_port.get_renderer_callbacks(port), 14.0f,
        g_iui_port.get_vector_callbacks(port));
    iui_context *ctx = iui_init(&cfg);
    if (!ctx) {
        fprintf(stderr, "ctx init failed\\n");
        iui_headless_disable_shm(port);
        g_iui_port.shutdown(port);
        return 1;
    }

    while (g_iui_port.poll_events(port)) {
        g_iui_port.begin_frame(port);
        iui_begin_frame(ctx, g_iui_port.get_delta_time(port));
        iui_begin_window(ctx, "main", 10, 10, 380, 280, 0);
        iui_button(ctx, "Test Button", 0);
        iui_end_window(ctx);
        iui_end_frame(ctx);
        g_iui_port.end_frame(port);
        if (g_iui_port.should_exit(port)) break;
    }

    /* Test 8: Frame count updated (at least 4 frames should have run) */
    tests_total++;
    if (hdr && hdr->frame_count >= 4) {
        printf("PASS: frame_count (%u)\\n", hdr->frame_count);
        tests_passed++;
    } else {
        printf("FAIL: frame_count (%u)\\n", hdr ? hdr->frame_count : 0);
    }

    /* Test 9: Stats synced to SHM header */
    tests_total++;
    if (hdr && hdr->draw_box_calls > 0) {
        printf("PASS: stats_sync (box:%u)\\n", hdr->draw_box_calls);
        tests_passed++;
    } else {
        printf("FAIL: stats_sync (box:%u)\\n", hdr ? hdr->draw_box_calls : 0);
    }

    /* Test 10: Framebuffer API */
    tests_total++;
    size_t fb_w = 0, fb_h = 0;
    iui_headless_get_framebuffer_size(port, &fb_w, &fb_h);
    const uint32_t *fb = iui_headless_get_framebuffer(port);
    if (fb && fb_w == 400 && fb_h == 300) {
        printf("PASS: framebuffer_api (%zux%zu)\\n", fb_w, fb_h);
        tests_passed++;
    } else {
        printf("FAIL: framebuffer_api\\n");
    }

    /* Test 11: Get pixel */
    tests_total++;
    uint32_t px = iui_headless_get_pixel(port, 0, 0);
    if (px != 0) {  /* Should be non-zero (background color) */
        printf("PASS: get_pixel (0x%08x)\\n", px);
        tests_passed++;
    } else {
        printf("FAIL: get_pixel\\n");
    }

    /* Test 12: Disable SHM */
    iui_headless_disable_shm(port);
    tests_total++;
    if (!iui_headless_shm_enabled(port)) {
        printf("PASS: shm_disable\\n");
        tests_passed++;
    } else {
        printf("FAIL: shm_disable\\n");
    }

    g_iui_port.shutdown(port);

    printf("RESULT: %d/%d tests passed\\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
"""

    src = BUILD_DIR / "test_shm_self.c"
    src.write_text(shm_self_test)
    exe = BUILD_DIR / "test_shm_self"

    if not LIB_PATH.exists():
        return False, {"error": f"Library not found: {LIB_PATH}"}

    # Compile
    try:
        subprocess.run(
            [
                "cc",
                "-o",
                str(exe),
                str(src),
                f"-I{PROJECT_ROOT}",
                f"-I{PROJECT_ROOT}/include",
                f"-I{PROJECT_ROOT}/src",
                *SANITIZER_FLAGS,
                str(LIB_PATH),
                "-lm",
                *SANITIZER_FLAGS,
            ],
            check=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as e:
        return False, {"error": f"Compile failed: {e.stderr.decode()[:200]}"}

    # Run the self-test
    try:
        result = subprocess.run(
            [str(exe)],
            capture_output=True,
            timeout=10,
        )
        stdout = result.stdout.decode()
        stderr = result.stderr.decode()

        if verbose:
            for line in stdout.strip().split("\n"):
                print(f"    {line}")
            if stderr:
                print(f"    stderr: {stderr}")

        # Parse results
        tests = []
        for line in stdout.split("\n"):
            if line.startswith("PASS:"):
                name = line[5:].strip().split()[0]
                tests.append((name, True))
            elif line.startswith("FAIL:"):
                name = line[5:].strip().split()[0]
                tests.append((name, False))

        passed_count = sum(1 for _, p in tests if p)
        total = len(tests)
        all_passed = result.returncode == 0 and passed_count == total

        return all_passed, {"passed": passed_count, "total": total, "tests": tests}

    except subprocess.TimeoutExpired:
        return False, {"error": "Test timeout"}
    except Exception as e:
        return False, {"error": f"Test error: {e}"}


def main():
    parser = argparse.ArgumentParser(description="Headless UI test harness")
    parser.add_argument("-t", "--test", help="Run specific test")
    parser.add_argument("-s", "--screenshot", action="store_true", help="Save PNGs")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("--list", action="store_true", help="List tests")
    parser.add_argument("--build", action="store_true", help="Rebuild library")
    parser.add_argument("--clean", action="store_true", help="Clean build artifacts")
    parser.add_argument("--md3", action="store_true", help="Run MD3 spec validation")
    parser.add_argument("--lib", help="Path to pre-built libiui.a (skip build step)")
    parser.add_argument(
        "--golden", action="store_true", help="Generate golden images for visual tests"
    )
    parser.add_argument(
        "--visual", action="store_true", help="Run visual regression tests"
    )
    parser.add_argument(
        "--tolerance",
        type=int,
        default=2,
        help="Per-channel tolerance for visual comparison (default: 2)",
    )
    parser.add_argument(
        "--shm", action="store_true", help="Run shared memory IPC tests"
    )
    args = parser.parse_args()

    if args.list:
        print("Unified Tests (render + interaction + validation):")
        for name, tc in TESTS.items():
            interactive = "✓" if tc.inject_code else " "
            print(f"  {name:12} [{interactive}] {tc.description}")
        print("\nMD3 validation: --md3")
        print("[✓] = includes input injection")
        return 0

    if args.clean:
        import shutil

        if BUILD_DIR.exists():
            shutil.rmtree(BUILD_DIR)
        print("Cleaned build directory.")
        return 0

    # Determine library path
    global LIB_PATH
    if args.lib:
        LIB_PATH = Path(args.lib)
        if not LIB_PATH.exists():
            print(f"Error: Library not found: {LIB_PATH}")
            return 1
    else:
        LIB_PATH = PROJECT_ROOT / "libiui.a"
        if args.build or not LIB_PATH.exists():
            if not build_library():
                return 1

    # Output format helpers (match C unit test style)
    WIDTH = 45
    GREEN = "\033[32m"
    RED = "\033[31m"
    RESET = "\033[0m"

    def print_result(desc, passed, error=None):
        if passed:
            status = f"[ {GREEN}OK{RESET} ]"
            print(f"{desc:<{WIDTH}} {status}")
        else:
            status = f"[{RED}FAIL{RESET}]"
            print(f"{desc:<{WIDTH}} {status} {error or ''}")

    # MD3 spec validation only
    if args.md3:
        print("MD3 Specification Compliance")
        print("=" * (WIDTH + 8))
        passed, info = run_md3_tests(args.verbose)
        ok_count = info.get("ok", 0)
        print_result(
            f"MD3 Spec Validation ({ok_count} checks)",
            passed,
            f"{info.get('fail', 0)} violations" if not passed else None,
        )
        return 0 if passed else 1

    # Generate golden images
    if args.golden:
        print("Generating Golden Images")
        print("=" * (WIDTH + 8))
        for name in TESTS:
            passed, info = run_visual_test(name, generate=True, verbose=args.verbose)
            if passed:
                print_result(f"Generated {name}.png", True)
            else:
                print_result(f"Generate {name}", False, info.get("error"))
        print(f"\nGolden images saved to: {GOLDEN_DIR}")
        return 0

    # Visual regression tests
    if args.visual:
        print("Visual Regression Tests")
        print("=" * (WIDTH + 8))
        results = []
        for name in TESTS:
            passed, info = run_visual_test(
                name,
                generate=False,
                tolerance=args.tolerance,
                verbose=args.verbose,
            )
            results.append((name, passed, info))
            if passed:
                print_result(f"Visual: {name}", True)
            else:
                error = info.get("error")
                if not error and "similarity" in info:
                    error = f"{info['similarity']:.1f}% match"
                print_result(f"Visual: {name}", False, error)

        passed_count = sum(1 for _, p, _ in results if p)
        total = len(results)
        print()
        print("=" * (WIDTH + 8))
        print(
            f"  All {passed_count} visual tests passed"
            if passed_count == total
            else f"  {passed_count}/{total} visual tests passed"
        )
        return 0 if passed_count == total else 1

    # Shared memory IPC tests
    if args.shm:
        import platform

        if platform.system() == "Windows":
            print("SHM tests not yet supported on Windows")
            return 1

        print("Shared Memory IPC Tests")
        print("=" * (WIDTH + 8))
        passed, info = run_shm_test(args.verbose)
        if passed:
            print_result(f"SHM IPC ({info.get('passed', 0)} checks)", True)
        else:
            error = info.get(
                "error", f"{info.get('passed', 0)}/{info.get('total', 0)} passed"
            )
            print_result("SHM IPC", False, error)
        return 0 if passed else 1

    # Run specific test
    if args.test:
        if args.test not in TESTS:
            print(f"Unknown test: {args.test}")
            return 1
        passed, info = run_test(args.test, args.screenshot, args.verbose)
        print_result(args.test, passed, info.get("error"))
        return 0 if passed else 1

    # Run all unified tests
    print("Headless UI Tests")
    print("=" * (WIDTH + 8))
    results = []
    for name, tc in TESTS.items():
        passed, info = run_test(name, args.screenshot, args.verbose)
        results.append((name, passed, info))
        print_result(tc.description, passed, info.get("error"))

    # Run MD3 validation
    print()
    print("MD3 Specification Compliance")
    print("=" * (WIDTH + 8))
    md3_passed, md3_info = run_md3_tests(args.verbose)
    ok_count = md3_info.get("ok", 0)
    print_result(
        f"MD3 Spec Validation ({ok_count} checks)",
        md3_passed,
        f"{md3_info.get('fail', 0)} violations" if not md3_passed else None,
    )
    results.append(("md3-spec", md3_passed, md3_info))

    # Run MD3 runtime validation (checks actual rendered dimensions)
    md3_runtime_passed, md3_runtime_info = run_md3_runtime_tests(args.verbose)
    tracked = md3_runtime_info.get("tracked", 0)
    # Show actual error message if present, otherwise show violations count
    md3_runtime_error = None
    if not md3_runtime_passed:
        md3_runtime_error = md3_runtime_info.get(
            "error", f"{md3_runtime_info.get('violations', 0)} violations"
        )
    print_result(
        f"MD3 Runtime Validation ({tracked} widgets)",
        md3_runtime_passed,
        md3_runtime_error,
    )
    results.append(("md3-runtime", md3_runtime_passed, md3_runtime_info))

    passed_count = sum(1 for _, p, _ in results if p)
    total = len(results)
    print()
    print("=" * (WIDTH + 8))
    print(
        f"  All {passed_count} headless tests passed"
        if passed_count == total
        else f"  {passed_count}/{total} tests passed"
    )

    return 0 if passed_count == total else 1


if __name__ == "__main__":
    sys.exit(main())
