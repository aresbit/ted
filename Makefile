# TED - Termux Editor Makefile
# Modern C Makefile based on gnaro project template

# Termux compatibility: avoid /tmp permissions
export TMPDIR := $(CURDIR)/tmp

# Project Configuration
NAME := ted
DIGITAL_RAIN_NAME := digital_rain
VERSION := 0.1.0

# Directories
SRC_DIR := src
BUILD_DIR := build
BIN_DIR := bin

# Source files
SRCS := $(filter-out src/digital_rain_main.c, $(wildcard $(SRC_DIR)/*.c))
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DIGITAL_RAIN_SRCS := src/digital_rain.c src/digital_rain_main.c
DIGITAL_RAIN_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(DIGITAL_RAIN_SRCS))
MQJS_DIR := vendor/mquickjs
MQJS_SRCS := $(MQJS_DIR)/mquickjs.c $(MQJS_DIR)/dtoa.c $(MQJS_DIR)/libm.c $(MQJS_DIR)/cutils.c
MQJS_OBJS := $(BUILD_DIR)/mqjs_vm_mquickjs.o $(BUILD_DIR)/mqjs_vm_dtoa.o $(BUILD_DIR)/mqjs_vm_libm.o $(BUILD_DIR)/mqjs_vm_cutils.o
TS_DIR := vendor/tree-sitter
TS_C_DIR := vendor/tree-sitter-c
TS_SRCS := $(TS_DIR)/lib/src/lib.c $(TS_C_DIR)/src/parser.c
TS_OBJS := $(BUILD_DIR)/ts_runtime_lib.o $(BUILD_DIR)/ts_grammar_c.o

# Compiler Configuration
CC := clang
CFLAGS := -std=gnu17 -Wall -Wextra -pedantic -O2 -D_GNU_SOURCE
CFLAGS += -Iinclude -I$(MQJS_DIR) -I$(TS_DIR)/lib/include -I$(TS_C_DIR)/src -fPIC -DSP_PS_DISABLE

# Debug build support
debug ?= 0
ifeq ($(debug),1)
    CFLAGS := -std=gnu17 -Wall -Wextra -g -O0 -D_GNU_SOURCE -DDEBUG
    CFLAGS += -Iinclude -fPIC
endif

# Linker flags
LDFLAGS := -lm

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lpthread
endif

# Install prefix detection
# Override with: make install PREFIX=/your/custom/path
# Termux: check for Termux environment
ifneq ($(TERMUX_VERSION),)
    PREFIX := /data/data/com.termux/files/usr
else
    # Standard Unix paths
    PREFIX := /usr/local
    # If no write permission to /usr/local, use user's home
    ifeq ($(shell test -w $(PREFIX)/bin 2>/dev/null && echo yes || echo no),no)
        PREFIX := $(HOME)/.local
    endif
endif

# Default target
.PHONY: all clean debug format install uninstall smoke deps-mqjs

all: dir deps-mqjs $(BIN_DIR)/$(NAME)

# Create directories
dir:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Link executable
$(BIN_DIR)/$(NAME): $(OBJS) $(MQJS_OBJS) $(TS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"


# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c include/sp.h | dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mqjs_vm_%.o: $(MQJS_DIR)/%.c $(MQJS_DIR)/mquickjs.h $(MQJS_DIR)/mqjs_stdlib.h | dir deps-mqjs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ts_runtime_lib.o: $(TS_DIR)/lib/src/lib.c $(TS_DIR)/lib/include/tree_sitter/api.h | dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ts_grammar_c.o: $(TS_C_DIR)/src/parser.c $(TS_C_DIR)/src/tree_sitter/parser.h | dir
	$(CC) $(CFLAGS) -c $< -o $@

# Build with debug symbols
debug:
	$(MAKE) debug=1

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts"

# Format code with clang-format
format:
	@which clang-format > /dev/null 2>&1 && \
		clang-format -i $(SRCS) $(SRC_DIR)/*.h include/digital_rain.h || \
		echo "clang-format not installed"

# Install binary to PREFIX/bin
install: all
	@install -d $(PREFIX)/bin
	@install -m 755 $(BIN_DIR)/$(NAME) $(PREFIX)/bin/
	@echo "Installed to $(PREFIX)/bin/$(NAME)"
	@echo "Make sure $(PREFIX)/bin is in your PATH"

# Uninstall
uninstall:
	@rm -f $(PREFIX)/bin/$(NAME)
	@echo "Uninstalled $(NAME)"

# Development helpers
run: all
	./$(BIN_DIR)/$(NAME)

# Print build info
info:
	@echo "Project: $(NAME)"
	@echo "Version: $(VERSION)"
	@echo "Sources: $(SRCS)"
	@echo "Objects: $(OBJS)"
	@echo "Compiler: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "Install prefix: $(PREFIX)"

# Smoke regression checks
smoke:
	./scripts/regression.sh

# Build MicroQuickJS vendored binary for :js/:source runtime
deps-mqjs:
	$(MAKE) -C vendor/mquickjs mqjs
