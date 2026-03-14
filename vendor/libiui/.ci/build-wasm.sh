#!/usr/bin/env bash

# Build WebAssembly version and prepare deployment artifacts
# Usage: .ci/build-wasm.sh [build|deploy-prep]

set -euo pipefail

source "$(dirname "$0")/common.sh"

MODE="${1:-build}"

case "$MODE" in
build)
	# Bootstrap Kconfig tools first
	make defconfig

	# Apply WASM configuration using proper Kconfig flow
	python3 tools/kconfig/defconfig.py --kconfig configs/Kconfig configs/wasm_defconfig
	python3 tools/kconfig/genconfig.py --header-path src/iui_config.h configs/Kconfig

	# Build with full Emscripten toolchain
	CC=emcc AR=emar RANLIB=emranlib make $PARALLEL
	print_success "WebAssembly build complete"
	;;
deploy-prep)
	# Prepare deployment artifacts
	mkdir -p deploy
	cp assets/web/index.html deploy/
	cp assets/web/iui-wasm.js deploy/

	# Copy generated files (may be in assets/web or root)
	if [ -f assets/web/libiui_example.js ]; then
		cp assets/web/libiui_example.js deploy/
		cp assets/web/libiui_example.wasm deploy/
	elif [ -f libiui_example.js ]; then
		cp libiui_example.js deploy/
		cp libiui_example.wasm deploy/
	else
		print_error "WASM artifacts not found - build may have failed"
		exit 1
	fi

	ls -la deploy/
	print_success "Deployment artifacts prepared"
	;;
*)
	print_error "Unknown mode: $MODE"
	echo "Usage: $0 [build|deploy-prep]"
	exit 1
	;;
esac
