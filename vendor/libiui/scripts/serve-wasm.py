#!/usr/bin/env python3
"""
Simple HTTP server for libiui WebAssembly demo.

Usage:
    python3 scripts/serve-wasm.py [--port PORT] [--open]

Options:
    --port PORT     Port to listen on (default: 8000)
    --open          Automatically open browser

The server serves files from assets/web/ with proper MIME types
for WebAssembly content.
"""

import argparse
import http.server
import os
import socketserver
import sys
import webbrowser
from functools import partial

# WebAssembly requires specific MIME types
MIME_TYPES = {
    ".wasm": "application/wasm",
    ".js": "application/javascript",
    ".html": "text/html",
    ".css": "text/css",
    ".json": "application/json",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".svg": "image/svg+xml",
}


class WasmHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP request handler with WebAssembly MIME type support."""

    def __init__(self, *args, directory=None, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def guess_type(self, path):
        """Override to return proper MIME types for WebAssembly."""
        _, ext = os.path.splitext(path)
        if ext.lower() in MIME_TYPES:
            return MIME_TYPES[ext.lower()]
        return super().guess_type(path)

    def end_headers(self):
        """Add CORS headers for local development."""
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def log_message(self, format, *args):
        """Custom log format."""
        print(f"[{self.log_date_time_string()}] {args[0]}")


def main():
    parser = argparse.ArgumentParser(description="Serve libiui WebAssembly demo")
    parser.add_argument(
        "--port", "-p", type=int, default=8000, help="Port to listen on (default: 8000)"
    )
    parser.add_argument(
        "--open", "-o", action="store_true", help="Open browser automatically"
    )
    args = parser.parse_args()

    # Find project root (where this script lives in scripts/)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    web_dir = os.path.join(project_root, "assets", "web")

    # Check if web directory exists
    if not os.path.isdir(web_dir):
        print(f"Error: Web assets directory not found: {web_dir}")
        print("Please run 'make wasm-install' first to build WebAssembly files.")
        sys.exit(1)

    # Check for required files
    required_files = ["index.html", "iui-wasm.js"]
    missing = [
        f for f in required_files if not os.path.exists(os.path.join(web_dir, f))
    ]
    if missing:
        print(f"Error: Missing required files: {', '.join(missing)}")
        sys.exit(1)

    # Check for WASM build artifacts
    wasm_files = ["libiui_example.js", "libiui_example.wasm"]
    missing_wasm = [
        f for f in wasm_files if not os.path.exists(os.path.join(web_dir, f))
    ]
    if missing_wasm:
        print(f"Warning: WebAssembly build files not found: {', '.join(missing_wasm)}")
        print("Please run 'CC=emcc make' to build the WebAssembly version.")
        print()

    # Change to web directory
    os.chdir(web_dir)

    # Create handler with directory
    handler = partial(WasmHTTPRequestHandler, directory=web_dir)

    # Start server
    with socketserver.TCPServer(("", args.port), handler) as httpd:
        url = f"http://localhost:{args.port}"
        print()
        print("=" * 60)
        print("  libiui WebAssembly Development Server")
        print("=" * 60)
        print()
        print(f"  Serving: {web_dir}")
        print(f"  URL:     {url}")
        print()
        print("  Press Ctrl+C to stop")
        print()
        print("=" * 60)
        print()

        if args.open:
            webbrowser.open(url)

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")


if __name__ == "__main__":
    main()
