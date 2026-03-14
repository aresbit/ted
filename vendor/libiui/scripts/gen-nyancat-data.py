#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Unified nyancat data generator with configurable compression modes.

Downloads animation data from klange/nyancat repository and applies either:
- Opcode-based RLE compression (baseline, 87% reduction)
- Delta frame encoding (advanced, 91% reduction)

Opcode format (baseline RLE):
  0x0X = SetColor (current color = X, 0-13)
  0x2Y = Repeat Y+1 times (1-16 pixels)
  0x3Y = Repeat (Y+1)*16 times (16-256 pixels)
  0xFF = EndOfFrame

Delta encoding format (--delta mode):
  Frame 0 (baseline):
    0x0X = SetColor (X = color 0-13)
    0x2Y = Repeat (Y+1) times (1-16 pixels)
    0x3Y = Repeat (Y+1)*16 times (16-256 pixels)
    0xFF = EndOfFrame

  Frame 1-11 (delta):
    0x0X = SetColor (X = color 0-13)
    0x1Y = Skip (Y+1) unchanged pixels (1-16)
    0x2Y = Repeat (Y+1) changed pixels (1-16)
    0x3Y = Skip (Y+1)*16 unchanged pixels (16-256)
    0x4Y = Repeat (Y+1)*16 changed pixels (16-256)
    0x5Y = Skip (Y+1)*64 unchanged pixels (64-1024)
    0xFF = EndOfFrame
"""

import argparse
import re
import sys
import urllib.request
from pathlib import Path
from typing import List, Tuple


def download_animation_data(url: str) -> str:
    """Download animation.c from GitHub repository."""
    try:
        with urllib.request.urlopen(url) as response:
            return response.read().decode("utf-8")
    except Exception as e:
        print(f"Error downloading from {url}: {e}", file=sys.stderr)
        sys.exit(1)


def parse_animation_c(content: str) -> List[List[str]]:
    """
    Parse animation.c to extract frame data.

    Returns list of 12 frames, each frame is list of pixel strings.
    """
    frames = []

    # Find all frame arrays (frame0[] through frame11[])
    for frame_num in range(12):
        pattern = rf"const\s+char\s+\*\s*frame{frame_num}\[\]\s*=\s*\{{([^}}]+)\}}"
        match = re.search(pattern, content, re.DOTALL)

        if not match:
            print(
                f"Error: Could not find frame{frame_num}[] in animation.c",
                file=sys.stderr,
            )
            sys.exit(1)

        frame_text = match.group(1)

        # Extract all quoted strings for this frame
        frame_lines = re.findall(r'"([^"]*)"', frame_text)

        # Concatenate all lines into single frame (64 lines × 64 chars = 4096 pixels)
        frame_data = "".join(frame_lines)

        if len(frame_data) != 4096:
            print(
                f"Error: frame{frame_num} has {len(frame_data)} pixels, expected 4096",
                file=sys.stderr,
            )
            sys.exit(1)

        frames.append(list(frame_data))

    return frames


def map_color_to_palette(char: str) -> int:
    """
    Map nyancat color character to palette index.

    Original mapping from klange/nyancat upstream:
    , = dark blue background
    . = white (stars)
    ' = black (border)
    @ = tan (poptart)
    $ = pink (poptart)
    - = red (poptart)
    > = red (rainbow)
    & = orange (rainbow)
    + = yellow (rainbow)
    # = green (rainbow)
    = = light blue (rainbow)
    ; = dark blue (rainbow)
    * = gray (cat face)
    % = pink (cheeks)
    """
    color_map = {
        ",": 0,  # Dark blue background
        ".": 1,  # White (stars)
        "'": 2,  # Black (border)
        "@": 3,  # Tan/Light pink (poptart) -> Light pink/beige
        "%": 4,  # Pink cheeks
        "$": 5,  # Pink poptart -> Hot pink
        "-": 6,  # Red poptart
        ">": 6,  # Red rainbow (same as red poptart)
        "&": 7,  # Orange rainbow
        "+": 8,  # Yellow rainbow
        "#": 9,  # Green rainbow
        "=": 10,  # Light blue rainbow
        ";": 11,  # Dark blue/Purple rainbow -> Purple
        "*": 12,  # Gray cat face
    }
    return color_map.get(char, 0)  # Default to background


def compress_frame_opcode_rle(pixels: List[str]) -> List[int]:
    """
    Compress frame using opcode-based RLE (baseline compression).

    Returns list of opcodes (integers 0-255).
    """
    if len(pixels) != 4096:
        print(f"Error: Frame must have 4096 pixels, got {len(pixels)}", file=sys.stderr)
        sys.exit(1)

    opcodes = []
    i = 0
    current_color = -1

    while i < len(pixels):
        color = map_color_to_palette(pixels[i])

        # Set color if different from current
        if color != current_color:
            opcodes.append(0x00 | color)  # SetColor opcode
            current_color = color

        # Count consecutive pixels of same color
        count = 1
        while (
            i + count < len(pixels) and map_color_to_palette(pixels[i + count]) == color
        ):
            count += 1

        # Encode run length with appropriate opcodes (may need multiple for long runs)
        remaining = count
        while remaining > 0:
            if remaining <= 16:
                # Short repeat: 0x2Y (1-16 pixels)
                opcodes.append(0x20 | (remaining - 1))
                remaining = 0
            elif remaining <= 256:
                # Long repeat: 0x3Y (16-256 pixels in multiples of 16)
                # Emit full chunks of 16
                chunks = min(remaining // 16, 16)  # Max 16 chunks = 256 pixels
                if chunks > 0:
                    opcodes.append(0x30 | (chunks - 1))
                    remaining -= chunks * 16
            else:
                # For very long runs (>256), emit max long repeat (256 pixels)
                opcodes.append(0x30 | 0x0F)  # 16 chunks = 256 pixels
                remaining -= 256

        i += count

    # End of frame marker
    opcodes.append(0xFF)

    return opcodes


def compress_delta_frame(prev_pixels: List[str], curr_pixels: List[str]) -> List[int]:
    """
    Compress delta frame using skip + repeat encoding.

    Returns list of opcodes exploiting temporal coherence.
    """
    if len(prev_pixels) != 4096 or len(curr_pixels) != 4096:
        print("Error: Frames must have 4096 pixels", file=sys.stderr)
        sys.exit(1)

    # Convert to color indices
    prev_colors = [map_color_to_palette(p) for p in prev_pixels]
    curr_colors = [map_color_to_palette(p) for p in curr_pixels]

    opcodes = []
    i = 0
    current_color = -1

    while i < 4096:
        # Count consecutive unchanged pixels
        skip_count = 0
        while (
            i + skip_count < 4096
            and prev_colors[i + skip_count] == curr_colors[i + skip_count]
        ):
            skip_count += 1

        # Encode skip if any unchanged pixels
        if skip_count > 0:
            remaining_skip = skip_count
            while remaining_skip > 0:
                if remaining_skip <= 16:
                    # 0x1Y: Skip 1-16 unchanged pixels
                    opcodes.append(0x10 | (remaining_skip - 1))
                    remaining_skip = 0
                elif remaining_skip <= 256:
                    # 0x3Y: Skip 16-256 unchanged pixels (chunks of 16)
                    chunks = min(remaining_skip // 16, 16)
                    if chunks > 0:
                        opcodes.append(0x30 | (chunks - 1))
                        remaining_skip -= chunks * 16
                elif remaining_skip <= 1024:
                    # 0x5Y: Skip 64-1024 unchanged pixels (chunks of 64)
                    chunks = min(remaining_skip // 64, 16)
                    if chunks > 0:
                        opcodes.append(0x50 | (chunks - 1))
                        remaining_skip -= chunks * 64
                else:
                    # Max skip: 1024 pixels
                    opcodes.append(0x50 | 0x0F)
                    remaining_skip -= 1024

            i += skip_count
            if i >= 4096:
                break

        # Handle changed pixels
        color = curr_colors[i]
        if color != current_color:
            opcodes.append(0x00 | color)  # SetColor
            current_color = color

        # Count consecutive changed pixels of same color
        run_len = 1
        while (
            i + run_len < 4096
            and curr_colors[i + run_len] == color
            and prev_colors[i + run_len] != curr_colors[i + run_len]
        ):
            run_len += 1

        # Encode changed run
        remaining_run = run_len
        while remaining_run > 0:
            if remaining_run <= 16:
                # 0x2Y: Repeat 1-16 changed pixels
                opcodes.append(0x20 | (remaining_run - 1))
                remaining_run = 0
            elif remaining_run <= 256:
                # 0x4Y: Repeat 16-256 changed pixels (chunks of 16)
                chunks = min(remaining_run // 16, 16)
                if chunks > 0:
                    opcodes.append(0x40 | (chunks - 1))
                    remaining_run -= chunks * 16
            else:
                # Max run: 256 pixels
                opcodes.append(0x40 | 0x0F)
                remaining_run -= 256

        i += run_len

    opcodes.append(0xFF)
    return opcodes


def generate_header(
    frames: List[List[str]], output_path: Path, use_delta: bool = False
) -> None:
    """Generate nyancat-data.h with compressed frame data."""

    # Compress all frames
    compressed_frames = []

    if use_delta:
        # Frame 0: baseline RLE
        baseline_opcodes = compress_frame_opcode_rle(frames[0])
        compressed_frames.append(baseline_opcodes)
        print(
            f"Frame  0 (baseline): {len(frames[0])} pixels → {len(baseline_opcodes)} opcodes ({100 - len(baseline_opcodes) * 100 // len(frames[0])}% reduction)"
        )

        # Frames 1-11: delta encoding
        for i in range(1, 12):
            delta_opcodes = compress_delta_frame(frames[i - 1], frames[i])
            compressed_frames.append(delta_opcodes)
            print(
                f"Frame {i:2d} (delta):    {len(frames[i])} pixels → {len(delta_opcodes)} opcodes ({100 - len(delta_opcodes) * 100 // len(frames[i])}% reduction)"
            )
    else:
        # Baseline: opcode-RLE for all frames
        for i, frame in enumerate(frames):
            opcodes = compress_frame_opcode_rle(frame)
            compressed_frames.append(opcodes)
            print(
                f"Frame {i}: {len(frame)} pixels → {len(opcodes)} opcodes ({100 - len(opcodes) * 100 // len(frame)}% reduction)"
            )

    # Calculate frame offsets
    offsets = [0]
    for frame_data in compressed_frames[:-1]:
        offsets.append(offsets[-1] + len(frame_data))

    # Flatten all compressed data
    all_data = []
    for frame_data in compressed_frames:
        all_data.extend(frame_data)

    total_original = 12 * 4096
    total_compressed = len(all_data)
    reduction = 100 - (total_compressed * 100 // total_original)

    print(
        f"\nTotal: {total_original} pixels → {total_compressed} opcodes ({reduction}% reduction)"
    )

    # Generate header file
    compression_type = "delta-RLE" if use_delta else "opcode-RLE"
    with open(output_path, "w") as f:
        f.write(
            f"""// SPDX-License-Identifier: MIT
// Auto-generated nyancat animation data with {compression_type} compression
// DO NOT EDIT - Generated by scripts/gen-nyancat-data.py

#ifndef NYANCAT_DATA_H
#define NYANCAT_DATA_H

#include <stdint.h>

// Frame offset table (12 frames)
static const uint16_t nyancat_frame_offsets[12] = {{
"""
        )

        # Write frame offsets
        for i in range(0, len(offsets), 6):
            chunk = offsets[i : i + 6]
            f.write("    " + ", ".join(f"{offset:5d}" for offset in chunk))
            if i + 6 < len(offsets):
                f.write(",")
            f.write("\n")

        f.write("};\n\n")
        f.write(f"// Compressed animation data ({len(all_data)} bytes)\n")

        if use_delta:
            f.write(f"// Delta encoding format:\n")
            f.write(
                f"//   Frame 0 (baseline): 0x0X=SetColor, 0x2Y=Repeat(1-16), 0x3Y=Repeat*16(16-256)\n"
            )
            f.write(
                f"//   Frame 1-11 (delta):  0x0X=SetColor, 0x1Y=Skip(1-16), 0x2Y=Repeat(1-16),\n"
            )
            f.write(
                f"//                        0x3Y=Skip*16(16-256), 0x4Y=Repeat*16(16-256),\n"
            )
            f.write(
                f"//                        0x5Y=Skip*64(64-1024), 0xFF=EndOfFrame\n"
            )
        else:
            f.write(f"// Opcode format:\n")
            f.write(f"//   0x0X = SetColor (X = color 0-13)\n")
            f.write(f"//   0x2Y = Repeat (Y+1) times (1-16 pixels)\n")
            f.write(f"//   0x3Y = Repeat (Y+1)×16 times (16-256 pixels)\n")
            f.write(f"//   0xFF = EndOfFrame\n")

        f.write(f"static const uint8_t nyancat_compressed_data[{len(all_data)}] = {{\n")

        # Write compressed data (16 bytes per line)
        for i in range(0, len(all_data), 16):
            chunk = all_data[i : i + 16]
            f.write("    " + ", ".join(f"0x{byte:02x}" for byte in chunk))
            if i + 16 < len(all_data):
                f.write(",")
            f.write("\n")

        f.write("};\n\n")
        f.write("#endif // NYANCAT_DATA_H\n")

    print(f"\nGenerated: {output_path}")
    print(f"Header size: {output_path.stat().st_size} bytes")


def decompress_and_verify(frames: List[List[str]], use_delta: bool = False) -> bool:
    """
    Decompress compressed frames and verify against originals.

    Returns True if all frames match.
    """
    print("\n=== Verification Mode ===")

    all_match = True

    if use_delta:
        # Verify delta compression
        prev_frame = None
        for frame_idx, original_frame in enumerate(frames):
            if frame_idx == 0:
                # Baseline frame
                opcodes = compress_frame_opcode_rle(original_frame)
                decompressed = decompress_baseline(opcodes)
                prev_frame = original_frame
            else:
                # Delta frame
                opcodes = compress_delta_frame(prev_frame, original_frame)
                decompressed = decompress_delta(
                    decompress_baseline(compress_frame_opcode_rle(prev_frame)), opcodes
                )
                prev_frame = original_frame

            # Verify
            original_colors = [map_color_to_palette(p) for p in original_frame]
            if len(decompressed) != 4096:
                print(
                    f"Frame {frame_idx}: Length mismatch! Expected 4096, got {len(decompressed)}"
                )
                all_match = False
            else:
                mismatches = sum(
                    1 for a, b in zip(original_colors, decompressed) if a != b
                )
                if mismatches > 0:
                    print(f"Frame {frame_idx}: {mismatches} pixel mismatches")
                    all_match = False
                else:
                    print(
                        f"Frame {frame_idx}: ✓ Perfect match ({len(opcodes)} opcodes)"
                    )
    else:
        # Verify baseline RLE
        for frame_idx, original_frame in enumerate(frames):
            opcodes = compress_frame_opcode_rle(original_frame)
            decompressed = decompress_baseline(opcodes)

            if len(decompressed) != 4096:
                print(
                    f"Frame {frame_idx}: Length mismatch! Expected 4096, got {len(decompressed)}"
                )
                all_match = False
                continue

            original_colors = [map_color_to_palette(p) for p in original_frame]
            mismatches = sum(1 for a, b in zip(original_colors, decompressed) if a != b)

            if mismatches > 0:
                print(f"Frame {frame_idx}: {mismatches} pixel mismatches")
                all_match = False
            else:
                print(f"Frame {frame_idx}: ✓ Perfect match ({len(opcodes)} opcodes)")

    return all_match


def decompress_baseline(opcodes: List[int]) -> List[int]:
    """Decompress baseline RLE opcodes to color indices."""
    decompressed = []
    current_color = 0
    i = 0

    while i < len(opcodes):
        opcode = opcodes[i]
        i += 1

        if opcode == 0xFF:
            break
        elif (opcode & 0xF0) == 0x00:
            current_color = opcode & 0x0F
        elif (opcode & 0xF0) == 0x20:
            count = (opcode & 0x0F) + 1
            decompressed.extend([current_color] * count)
        elif (opcode & 0xF0) == 0x30:
            count = ((opcode & 0x0F) + 1) * 16
            decompressed.extend([current_color] * count)

    return decompressed


def decompress_delta(prev_frame: List[int], opcodes: List[int]) -> List[int]:
    """Decompress delta frame opcodes using previous frame."""
    decompressed = list(prev_frame)  # Start with previous frame
    pos = 0
    current_color = 0
    i = 0

    while i < len(opcodes) and pos < 4096:
        opcode = opcodes[i]
        i += 1

        if opcode == 0xFF:
            break
        elif (opcode & 0xF0) == 0x00:
            current_color = opcode & 0x0F
        elif (opcode & 0xF0) == 0x10:
            pos += (opcode & 0x0F) + 1  # Skip unchanged
        elif (opcode & 0xF0) == 0x20:
            count = (opcode & 0x0F) + 1  # Repeat changed
            for _ in range(count):
                if pos < 4096:
                    decompressed[pos] = current_color
                    pos += 1
        elif (opcode & 0xF0) == 0x30:
            pos += ((opcode & 0x0F) + 1) * 16  # Skip unchanged (long)
        elif (opcode & 0xF0) == 0x40:
            count = ((opcode & 0x0F) + 1) * 16  # Repeat changed (long)
            for _ in range(count):
                if pos < 4096:
                    decompressed[pos] = current_color
                    pos += 1
        elif (opcode & 0xF0) == 0x50:
            pos += ((opcode & 0x0F) + 1) * 64  # Skip unchanged (very long)

    return decompressed


def main():
    parser = argparse.ArgumentParser(
        description="Generate nyancat-data.h with configurable compression"
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=Path("nyancat-data.h"),
        help="Output header file path (default: nyancat-data.h)",
    )
    parser.add_argument(
        "--url",
        default="https://raw.githubusercontent.com/klange/nyancat/master/src/animation.c",
        help="URL to animation.c (default: klange/nyancat master)",
    )
    parser.add_argument(
        "--no-delta",
        action="store_true",
        help="Disable delta frame compression (use baseline RLE instead)",
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Verify compression/decompression without generating output",
    )

    args = parser.parse_args()

    # Download animation data
    print(f"Downloading from: {args.url}")
    content = download_animation_data(args.url)

    # Parse frames
    print("Parsing animation frames...")
    frames = parse_animation_c(content)
    print(f"Parsed {len(frames)} frames, {len(frames[0])} pixels each")

    # Delta is now the default
    use_delta = not args.no_delta

    # Verify mode
    if args.verify:
        success = decompress_and_verify(frames, use_delta)
        sys.exit(0 if success else 1)

    # Generate header
    compression_mode = "delta-RLE" if use_delta else "opcode-RLE"
    print(f"\nCompressing frames with {compression_mode}...")
    generate_header(frames, args.output, use_delta)

    # Run verification
    print("\nVerifying compression...")
    if decompress_and_verify(frames, use_delta):
        print("\n✓ All frames verified successfully")
    else:
        print("\n✗ Verification failed")
        sys.exit(1)


if __name__ == "__main__":
    main()
