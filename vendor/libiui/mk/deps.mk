# mk/deps.mk - Unified dependency detection

# Verbosity control: V=1 shows pkg-config errors
ifeq ($(V),1)
    DEVNULL :=
else
    DEVNULL := 2>/dev/null
endif

# pkg-config for cross-compilation
# Set PKG_CONFIG_SYSROOT_DIR and PKG_CONFIG_LIBDIR for target sysroot
PKG_CONFIG ?= pkg-config

# dep(type, packages)
# type: cflags | libs
# packages: space-separated package names
#
# Usage:
#   libiui.a_cflags-$(CONFIG_PORT_SDL2) += $(call dep,cflags,sdl2)
#   TARGET_LIBS += $(call dep,libs,sdl2)
#
# Tries package-specific config tool first (e.g., sdl2-config),
# then falls back to pkg-config.
#
define dep
$(shell \
    for pkg in $(2); do \
        if command -v $${pkg}-config >/dev/null 2>&1; then \
            $${pkg}-config --$(1) $(DEVNULL); \
        elif command -v $(PKG_CONFIG) >/dev/null 2>&1; then \
            $(PKG_CONFIG) --$(1) $$pkg $(DEVNULL); \
        fi; \
    done \
)
endef

# Check if a package exists
# Returns "y" if found, empty otherwise
define pkg-exists
$(shell \
    if command -v $(1)-config >/dev/null 2>&1; then \
        echo y; \
    elif $(PKG_CONFIG) --exists $(1) $(DEVNULL); then \
        echo y; \
    fi \
)
endef
