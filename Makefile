# TED - Termux Editor Makefile
# Modern C Makefile based on gnaro project template

# Termux compatibility: avoid /tmp permissions
export TMPDIR := $(CURDIR)/tmp

# Project Configuration
NAME := ted
VERSION := 0.1.0

# Directories
SRC_DIR := src
BUILD_DIR := build
BIN_DIR := bin

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Compiler Configuration
CC := gcc
CFLAGS := -std=gnu17 -Wall -Wextra -pedantic -O2 -D_GNU_SOURCE
CFLAGS += -Iinclude -fPIC -DSP_PS_DISABLE

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

# Default target
.PHONY: all clean debug format install uninstall

all: dir $(BIN_DIR)/$(NAME)

# Create directories
dir:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Link executable
$(BIN_DIR)/$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c include/sp.h | dir
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
		clang-format -i $(SRCS) $(SRC_DIR)/*.h || \
		echo "clang-format not installed"

# Install to /data/data/com.termux/files/usr/bin (Termux)
install: all
	@install -d /data/data/com.termux/files/usr/bin
	@install -m 755 $(BIN_DIR)/$(NAME) /data/data/com.termux/files/usr/bin/
	@echo "Installed to /data/data/com.termux/files/usr/bin/$(NAME)"

# Uninstall
uninstall:
	@rm -f /data/data/com.termux/files/usr/bin/$(NAME)
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
