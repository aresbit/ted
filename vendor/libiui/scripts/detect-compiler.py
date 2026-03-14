#!/usr/bin/env python3
"""
Compiler Detection Script for libiui Build System

Detects the compiler type (GCC, Clang, Emscripten) based on CROSS_COMPILE
and CC environment variables, following Linux kernel Kbuild conventions.

Usage:
    python3 detect-compiler.py [OPTIONS]

Options:
    --is COMPILER    Check if compiler matches (GCC/Clang/Emscripten)
                     Returns exit code 0 if match, 1 otherwise
    --have-emcc      Check if emcc is available in PATH
                     Returns exit code 0 if found, 1 otherwise

Exit codes:
    0: Success/match
    1: Failure/no match
"""

import os
import shlex
import shutil
import subprocess
import sys


def get_compiler_path():
    """
    Determine compiler path based on CROSS_COMPILE and CC variables.
    """
    cross_compile = os.environ.get("CROSS_COMPILE", "")
    cc = os.environ.get("CC", "")

    if cc:
        return cc
    elif cross_compile:
        return f"{cross_compile}gcc"
    else:
        return "cc"


def get_compiler_version(compiler):
    """Get compiler version string."""
    try:
        cmd_argv = shlex.split(compiler) if isinstance(compiler, str) else [compiler]
        result = subprocess.run(
            cmd_argv + ["--version"], capture_output=True, text=True, timeout=5
        )
        return result.stdout if result.returncode == 0 else ""
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError):
        return ""


def detect_compiler_type(version_output):
    """Detect compiler type from version string."""
    lower = version_output.lower()

    if "emcc" in lower:
        return "Emscripten"
    if "clang" in lower:
        return "Clang"
    if "gcc" in lower or "free software foundation" in lower:
        return "GCC"
    return "Unknown"


def check_emcc_available():
    """Check if emcc is available in PATH."""
    return shutil.which("emcc") is not None


def main():
    # Handle --have-emcc option
    if "--have-emcc" in sys.argv:
        if check_emcc_available():
            print("y")
            sys.exit(0)
        else:
            print("n")
            sys.exit(1)

    # Handle --is option
    check_is = "--is" in sys.argv
    expected_type = None

    if check_is:
        try:
            is_index = sys.argv.index("--is")
            if is_index + 1 < len(sys.argv):
                expected_type = sys.argv[is_index + 1]
            else:
                print("Error: --is requires a compiler type argument", file=sys.stderr)
                sys.exit(1)
        except (ValueError, IndexError):
            print("Error: --is requires a compiler type argument", file=sys.stderr)
            sys.exit(1)

    compiler = get_compiler_path()
    version_output = get_compiler_version(compiler)

    if not version_output:
        # For --is checks, output 'n' for Kconfig; otherwise 'Unknown'
        if check_is:
            print("n")
        else:
            print("Unknown", file=sys.stderr)
        sys.exit(1)

    compiler_type = detect_compiler_type(version_output)

    if check_is:
        matches = compiler_type.lower() == expected_type.lower()
        print("y" if matches else "n")
        sys.exit(0 if matches else 1)

    print(compiler_type)
    sys.exit(0 if compiler_type != "Unknown" else 1)


if __name__ == "__main__":
    main()
