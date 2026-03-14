# mk/toolchain.mk - Compiler and toolchain detection
#
# Auto-selects Emscripten when CONFIG_PORT_WASM is enabled in .config.
# After running 'make wasmconfig' or selecting WASM in 'make config',
# simply run 'make' without needing 'CC=emcc make'.

ifndef _MK_TOOLCHAIN_INCLUDED
_MK_TOOLCHAIN_INCLUDED := 1

# Cross-compilation support
CROSS_COMPILE ?=
SYSROOT ?=

# ============================================================================
# Auto-select Emscripten when CONFIG_PORT_WASM=y
# ============================================================================
#
# If PORT_WASM is selected in .config AND user didn't explicitly set CC,
# automatically switch to emcc.

# Skip auto-selection for clean targets
CLEAN_TARGETS := clean distclean
ifneq ($(filter $(CLEAN_TARGETS),$(MAKECMDGOALS)),)
    _SKIP_EMCC_AUTO := 1
endif

ifeq ($(CONFIG_PORT_WASM),y)
    ifeq ($(origin CC),default)
        # Check if emcc is available
        EMCC_PATH := $(shell which emcc 2>/dev/null)
        ifneq ($(EMCC_PATH),)
            CC := emcc
            ifneq ($(_SKIP_EMCC_AUTO),1)
                $(info [libiui] Auto-selecting Emscripten for WebAssembly build)
            endif
        else
            $(error CONFIG_PORT_WASM requires Emscripten. Please install emcc or run 'make config' to select a different backend.)
        endif
    endif
endif

# Set defaults and apply CROSS_COMPILE prefix if not user-specified.
# Use ifeq/endif instead of ?= because --no-builtin-variables (set in common.mk)
# affects recipe execution but not parsing. With ?=, the builtin CC=cc is seen
# during parsing (so ?= does nothing), but at runtime the builtin is gone.
ifeq ($(origin CC),default)
    CC := $(CROSS_COMPILE)cc
endif
ifeq ($(origin AR),default)
    AR := $(CROSS_COMPILE)ar
endif
ifeq ($(origin RANLIB),default)
    RANLIB := $(CROSS_COMPILE)ranlib
endif
ifeq ($(origin STRIP),default)
    STRIP := $(CROSS_COMPILE)strip
endif

# Host toolchain (for build-time tools)
HOSTCC  ?= cc
HOSTAR  ?= ar

# Detect compiler type from version string
CC_VERSION := $(shell $(CC) --version 2>&1)

CC_IS_EMCC  := $(if $(findstring emcc,$(CC_VERSION)),1,0)
CC_IS_CLANG := $(if $(findstring clang,$(CC_VERSION)),1,0)
# GCC detection: check for both "GCC" and "gcc" (case-insensitive match)
CC_IS_GCC   := $(if $(or $(findstring GCC,$(CC_VERSION)),$(findstring gcc,$(CC_VERSION))),1,0)
# Clang also contains "gcc" in some paths, so exclude if clang detected
ifeq ($(CC_IS_CLANG),1)
    CC_IS_GCC := 0
endif

# Emscripten overrides
ifeq ($(CC_IS_EMCC),1)
    AR     := emar
    RANLIB := emranlib
    STRIP  := emstrip
endif

# ============================================================================
# Kconfig-derived build flags
# ============================================================================

KCONFIG_CFLAGS :=
KCONFIG_LDFLAGS :=

# Optimization
ifeq ($(CONFIG_OPTIMIZE_SIZE),y)
    KCONFIG_CFLAGS += -Os
else
    KCONFIG_CFLAGS += -O2
endif

# Debug symbols
ifeq ($(CONFIG_DEBUG_SYMBOLS),y)
    KCONFIG_CFLAGS += -g
endif

# Link-time optimization
ifeq ($(CONFIG_LTO),y)
    KCONFIG_CFLAGS += -flto
    KCONFIG_LDFLAGS += -flto
endif

# Sanitizers
ifeq ($(CONFIG_SANITIZERS),y)
    KCONFIG_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
    KCONFIG_LDFLAGS += -fsanitize=address,undefined
endif

# ============================================================================
# Sysroot support for cross-compilation
# ============================================================================

ifneq ($(SYSROOT),)
    KCONFIG_CFLAGS += --sysroot=$(SYSROOT)
    KCONFIG_LDFLAGS += --sysroot=$(SYSROOT)
endif

# ============================================================================
# Final flag assembly (preserves user-supplied flags)
# ============================================================================

# Use -std=gnu99 for Emscripten (required by EM_ASM macros)
# Use -std=c99 for native builds
ifeq ($(CC_IS_EMCC),1)
    CFLAGS_BASE := -Wall -Wextra -std=gnu99 -Iinclude
else
    CFLAGS_BASE := -Wall -Wextra -std=c99 -Iinclude
endif

# Use override to preserve user-supplied CFLAGS/LDFLAGS
override CFLAGS := $(CFLAGS_BASE) $(KCONFIG_CFLAGS) $(CFLAGS)
override LDFLAGS := -lm $(KCONFIG_LDFLAGS) $(LDFLAGS)

endif # _MK_TOOLCHAIN_INCLUDED
