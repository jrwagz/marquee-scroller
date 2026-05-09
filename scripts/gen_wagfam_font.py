#!/usr/bin/env python3
"""Generate marquee/WagfamFont.h from human-readable ASCII-art glyphs.

The output is an Adafruit_GFX-compatible GFXfont named ``WagfamBlock``:
fixed 5x7 cell, all uppercase (lowercase = uppercase glyph), printable
ASCII range 0x20-0x7E, drawn with thicker / blockier strokes than the
default Adafruit 5x7 builtin font so that it reads as visually distinct
on a 32x8 LED matrix.

Cell layout: each glyph is rendered into a 5-wide x 7-tall grid using
``#`` for ON pixels, ``.`` for OFF. The script bit-packs each row MSB-first
(matching Adafruit_GFX's row-major scheme) and emits the C header.

The font has the following GFX cursor metrics:
  - yAdvance = 8        (one pixel of leading)
  - per-glyph: width=5 height=7 xAdvance=6 xOffset=0 yOffset=-7
    (cursor sits on the baseline; glyph fills y=-7..-1, leaving y=0 for
    the descender row, which we do not use; this places the visible
    glyph at rows 0..6 of the matrix when print() is called with
    setCursor(x, 7).)

Run:  ./scripts/gen_wagfam_font.py
"""
from __future__ import annotations

import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Glyph definitions: each entry is a 7-line string of '#' (on) and '.' (off),
# 5 columns wide. Columns past the 5th are stripped. Trailing whitespace OK.
#
# Style: blocky, slab-serif-ish at 5x7, designed to look chunky compared to
# Adafruit's stock 5x7 font. Uppercase only — lowercase ASCII (0x61-0x7A)
# maps to the same bitmap as uppercase.
# ---------------------------------------------------------------------------

GLYPHS: dict[str, str] = {
    " ": """
.....
.....
.....
.....
.....
.....
.....
""",
    "!": """
.##..
.##..
.##..
.##..
.##..
.....
.##..
""",
    '"': """
##.##
##.##
##.##
.....
.....
.....
.....
""",
    "#": """
.#.#.
#####
.#.#.
.#.#.
#####
.#.#.
.....
""",
    "$": """
..#..
.####
#.#..
.###.
..#.#
####.
..#..
""",
    "%": """
##..#
##.#.
..#..
.#.##
#..##
.....
.....
""",
    "&": """
.##..
##...
##...
.##.#
#.##.
#..#.
.##.#
""",
    "'": """
.##..
.##..
.##..
.....
.....
.....
.....
""",
    "(": """
..##.
.##..
.##..
.##..
.##..
.##..
..##.
""",
    ")": """
.##..
..##.
..##.
..##.
..##.
..##.
.##..
""",
    "*": """
.....
.#.#.
##.##
.###.
##.##
.#.#.
.....
""",
    "+": """
.....
..#..
..#..
#####
..#..
..#..
.....
""",
    ",": """
.....
.....
.....
.....
.##..
.##..
##...
""",
    "-": """
.....
.....
.....
#####
.....
.....
.....
""",
    ".": """
.....
.....
.....
.....
.....
.##..
.##..
""",
    "/": """
....#
...##
..##.
.##..
.##..
##...
#....
""",
    "0": """
.###.
##.##
##.##
##.##
##.##
##.##
.###.
""",
    "1": """
.##..
###..
.##..
.##..
.##..
.##..
####.
""",
    "2": """
.###.
##.##
...##
..##.
.##..
##...
#####
""",
    "3": """
.###.
##.##
...##
..##.
...##
##.##
.###.
""",
    "4": """
..##.
.###.
##.#.
#####
...#.
...#.
...#.
""",
    "5": """
#####
##...
####.
...##
...##
##.##
.###.
""",
    "6": """
.###.
##...
##...
####.
##.##
##.##
.###.
""",
    "7": """
#####
...##
..##.
.##..
.##..
.##..
.##..
""",
    "8": """
.###.
##.##
##.##
.###.
##.##
##.##
.###.
""",
    "9": """
.###.
##.##
##.##
.####
...##
...##
.###.
""",
    ":": """
.....
.##..
.##..
.....
.##..
.##..
.....
""",
    ";": """
.....
.##..
.##..
.....
.##..
.##..
##...
""",
    "<": """
....#
..##.
.##..
##...
.##..
..##.
....#
""",
    "=": """
.....
.....
#####
.....
#####
.....
.....
""",
    ">": """
#....
.##..
..##.
...##
..##.
.##..
#....
""",
    "?": """
.###.
##.##
...##
..##.
.##..
.....
.##..
""",
    "@": """
.###.
##.##
#.#.#
#.###
#....
##...
.###.
""",
    "A": """
.###.
##.##
##.##
##.##
#####
##.##
##.##
""",
    "B": """
####.
##.##
##.##
####.
##.##
##.##
####.
""",
    "C": """
.####
##...
##...
##...
##...
##...
.####
""",
    "D": """
####.
##.##
##.##
##.##
##.##
##.##
####.
""",
    "E": """
#####
##...
##...
####.
##...
##...
#####
""",
    "F": """
#####
##...
##...
####.
##...
##...
##...
""",
    "G": """
.####
##...
##...
##.##
##.##
##.##
.####
""",
    "H": """
##.##
##.##
##.##
#####
##.##
##.##
##.##
""",
    "I": """
.###.
.##..
.##..
.##..
.##..
.##..
.###.
""",
    "J": """
..###
...##
...##
...##
...##
##.##
.###.
""",
    "K": """
##.##
##.##
####.
###..
####.
##.##
##.##
""",
    "L": """
##...
##...
##...
##...
##...
##...
#####
""",
    "M": """
##.##
#####
#####
#####
##.##
##.##
##.##
""",
    "N": """
##.##
###.#
####.
#####
.####
#.###
##.##
""",
    "O": """
.###.
##.##
##.##
##.##
##.##
##.##
.###.
""",
    "P": """
####.
##.##
##.##
####.
##...
##...
##...
""",
    "Q": """
.###.
##.##
##.##
##.##
##.#.
###.#
.####
""",
    "R": """
####.
##.##
##.##
####.
####.
##.##
##.##
""",
    "S": """
.####
##...
##...
.###.
...##
...##
####.
""",
    "T": """
#####
.##..
.##..
.##..
.##..
.##..
.##..
""",
    "U": """
##.##
##.##
##.##
##.##
##.##
##.##
.###.
""",
    "V": """
##.##
##.##
##.##
##.##
##.##
.###.
..#..
""",
    "W": """
##.##
##.##
##.##
#####
#####
#####
##.##
""",
    "X": """
##.##
##.##
.###.
..#..
.###.
##.##
##.##
""",
    "Y": """
##.##
##.##
##.##
.###.
.##..
.##..
.##..
""",
    "Z": """
#####
...##
..##.
.##..
##...
##...
#####
""",
    "[": """
.###.
.##..
.##..
.##..
.##..
.##..
.###.
""",
    "\\": """
#....
##...
.##..
..##.
..##.
...##
....#
""",
    "]": """
.###.
..##.
..##.
..##.
..##.
..##.
.###.
""",
    "^": """
..#..
.###.
##.##
.....
.....
.....
.....
""",
    "_": """
.....
.....
.....
.....
.....
.....
#####
""",
    "`": """
.##..
..##.
.....
.....
.....
.....
.....
""",
    "{": """
..###
..##.
..##.
###..
..##.
..##.
..###
""",
    "|": """
.##..
.##..
.##..
.##..
.##..
.##..
.##..
""",
    "}": """
###..
.##..
.##..
..###
.##..
.##..
###..
""",
    "~": """
.....
.....
.##.#
#.##.
.....
.....
.....
""",
}


def parse_glyph(art: str) -> list[int]:
    """Parse a 7-line glyph art string into 7 row-bytes (5-bit MSB-first).

    Each row's 5 pixels are packed into the high 5 bits of a single byte;
    Adafruit_GFX's row reader walks bits left-to-right (MSB first) within
    each row.
    """
    rows = [r for r in art.splitlines() if r != ""]
    if len(rows) != 7:
        raise ValueError(f"Glyph must have exactly 7 rows, got {len(rows)}: {rows!r}")
    out: list[int] = []
    for row in rows:
        row = row.ljust(5, ".")[:5]
        bits = 0
        for i, ch in enumerate(row):
            if ch == "#":
                bits |= 1 << (7 - i)  # MSB-first into a byte
            elif ch != ".":
                raise ValueError(f"Glyph row has invalid char {ch!r}: {row!r}")
        out.append(bits)
    return out


def pack_bitmap(rows: list[int], width: int, height: int) -> list[int]:
    """Pack ``height`` rows of ``width`` pixels into a continuous bitstream.

    Each row begins flush against the previous one (no per-row byte padding,
    matching Adafruit_GFX). Trailing bits in the final byte are zero.
    """
    bits: list[int] = []
    for row in rows:
        for i in range(width):
            bits.append(1 if (row >> (7 - i)) & 1 else 0)
    out: list[int] = []
    for i in range(0, len(bits), 8):
        chunk = bits[i : i + 8]
        b = 0
        for j, bit in enumerate(chunk):
            b |= bit << (7 - j)
        out.append(b)
    return out


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    out_path = repo_root / "marquee" / "WagfamFont.h"

    first = 0x20
    last = 0x7E

    glyph_entries: list[tuple[int, int, int, int, int, int, str]] = []
    bitmap_bytes: list[int] = []
    bitmap_offset = 0

    for code in range(first, last + 1):
        ch = chr(code)
        # lowercase falls back to uppercase
        if "a" <= ch <= "z":
            src = ch.upper()
        else:
            src = ch
        if src not in GLYPHS:
            # unknown — render as solid block so missing glyphs are obvious
            art = "\n".join(["#####"] * 7)
        else:
            art = GLYPHS[src]
        rows = parse_glyph(art)
        packed = pack_bitmap(rows, 5, 7)
        bitmap_bytes.extend(packed)
        glyph_entries.append((bitmap_offset, 5, 7, 6, 0, -7, ch))
        bitmap_offset += len(packed)

    # ----- emit header -----
    lines: list[str] = []
    lines.append("// Auto-generated by scripts/gen_wagfam_font.py — do not edit by hand.")
    lines.append("// Re-run that script after editing the GLYPHS dict.")
    lines.append("//")
    lines.append("// WagfamBlock: a chunky 5x7 ALL-CAPS bitmap font for the MAX7219 LED")
    lines.append("// matrix. ASCII printable range 0x20-0x7E; lowercase characters render")
    lines.append("// the uppercase glyph. Designed to read as visually distinct from the")
    lines.append("// Adafruit_GFX builtin 5x7 font when used as one of several selectable")
    lines.append("// scroller fonts (issue #106).")
    lines.append("#pragma once")
    lines.append("#include <Adafruit_GFX.h>")
    lines.append("")
    lines.append("const uint8_t WagfamBlockBitmaps[] PROGMEM = {")
    for i in range(0, len(bitmap_bytes), 12):
        chunk = bitmap_bytes[i : i + 12]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("const GFXglyph WagfamBlockGlyphs[] PROGMEM = {")
    for off, w, h, adv, xo, yo, ch in glyph_entries:
        comment = f"// 0x{ord(ch):02X} {ch!r}"
        lines.append(f"  {{ {off}, {w}, {h}, {adv}, {xo}, {yo} }},  {comment}")
    lines.append("};")
    lines.append("")
    lines.append("const GFXfont WagfamBlock PROGMEM = {")
    lines.append("  (uint8_t *)WagfamBlockBitmaps,")
    lines.append("  (GFXglyph *)WagfamBlockGlyphs,")
    lines.append(f"  0x{first:02X}, 0x{last:02X},  // first, last")
    lines.append("  8                  // yAdvance")
    lines.append("};")
    lines.append("")

    out_path.write_text("\n".join(lines))
    print(f"wrote {out_path}  ({len(bitmap_bytes)} bitmap bytes, "
          f"{len(glyph_entries)} glyphs)")
    # sanity: every glyph claims 5 bytes (5x7 = 35 bits, ceil/8 = 5)
    expected = len(glyph_entries) * 5
    if expected != len(bitmap_bytes):
        print(f"WARNING: bitmap size {len(bitmap_bytes)} != expected {expected}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
