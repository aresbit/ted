# mk/common.mk - Generic build rules

# Suppress built-in rules and variables for reproducibility
MAKEFLAGS += --no-builtin-rules --no-builtin-variables

# Out-of-tree build support: make O=/path/to/build
BUILD_DIR := $(if $(O),$(O),.build)

# Verbosity control
ifeq ($(V),1)
    Q :=
else
    Q := @
    MAKEFLAGS += --no-print-directory
endif

# Create build directory
$(BUILD_DIR):
	@mkdir -p $@

# Collect all objects for a target (simple version)
# Uses flat structure: src/foo.c -> .build/foo.o
define src-to-obj
$(BUILD_DIR)/$(notdir $(1:.c=.o))
endef

define collect-objs
$(foreach src,$(filter %.c,$($(1)_files-y)),$(call src-to-obj,$(src)))
endef

# Global include paths (collected from all targets)
GLOBAL_INCLUDES := include src tests ports externals

# Global extra CFLAGS (for SDL2, etc.) - set by Makefile before including this

# Dependency generation flags
DEPFLAGS = -MMD -MP -MF $(BUILD_DIR)/$(notdir $*).d

# Configuration file dependency - rebuild when config changes
# Use recursive assignment (=) so $(wildcard) evaluates at rule execution time,
# not parse time. This handles 'make distclean check' correctly when .config
# exists initially but gets deleted by distclean before check runs.
CONFIG_DEPS = $(wildcard .config) src/iui_config.h

# Pattern rules for different source directories
# PREREQ_GENERATED can be set by main Makefile for code generation dependencies
# CONFIG_DEPS ensures rebuild when configuration changes (port selection, etc.)
$(BUILD_DIR)/%.o: src/%.c $(CONFIG_DEPS) | $(BUILD_DIR) $(PREREQ_GENERATED)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) $(GLOBAL_EXTRA_CFLAGS) $(DEPFLAGS) \
	    $(addprefix -I,$(GLOBAL_INCLUDES)) \
	    -c -o $@ $<

$(BUILD_DIR)/%.o: tests/%.c $(CONFIG_DEPS) | $(BUILD_DIR) $(PREREQ_GENERATED)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) $(GLOBAL_EXTRA_CFLAGS) $(DEPFLAGS) \
	    $(addprefix -I,$(GLOBAL_INCLUDES)) \
	    -c -o $@ $<

$(BUILD_DIR)/%.o: ports/%.c $(CONFIG_DEPS) | $(BUILD_DIR) $(PREREQ_GENERATED)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) $(GLOBAL_EXTRA_CFLAGS) $(DEPFLAGS) \
	    $(addprefix -I,$(GLOBAL_INCLUDES)) \
	    -c -o $@ $<

# Static library rule generator
define target.a-rules
$(1)_objs := $(call collect-objs,$(1))

$(1): TARGET_NAME := $(1)
$(1): $$($(1)_objs)
	@echo "  AR      $$@"
	$(Q)$$(AR) rcs $$@ $$^

endef

# Executable rule generator
define target-rules
$(1)_objs := $(call collect-objs,$(1))

$(1): TARGET_NAME := $(1)
$(1): $$($(1)_objs) $$($(1)_depends-y)
	@echo "  LD      $$@"
	$(Q)$$(CC) $$(CFLAGS) -o $$@ $$($(1)_objs) $$($(1)_ldflags-y) $$(LDFLAGS)

endef

# Expand all targets
$(foreach T,$(target.a-y),$(eval $(call target.a-rules,$T)))
$(foreach T,$(target-y),$(eval $(call target-rules,$T)))

# Include generated dependency files (ignore if missing)
-include $(wildcard $(BUILD_DIR)/*.d)

# Clean rule
clean-build:
	rm -rf $(BUILD_DIR)
	rm -f $(target.a-y) $(target-y)

.PHONY: clean-build
