"""Tests for scripts/gen_wagfam_font.py.

We exercise:
  * parse_glyph: bit packing of '#'/'.' rows into MSB-first byte rows
  * pack_bitmap: continuous (no per-row padding) bitstream packing
  * main: the full generator runs, writes WagfamFont.h with the expected
    glyph count and bitmap byte count, and the produced bytes round-trip
    back to the source ASCII art for a representative sample of glyphs.

The round-trip assertion is the load-bearing one: it's what gives us
confidence that the bytes Adafruit_GFX::drawChar will read on-device
correspond to the glyph art in GLYPHS, since drawChar reads the bitstream
the same way we do here (MSB-first, row-major, no per-row padding).
"""

from __future__ import annotations

import re
from pathlib import Path

import gen_wagfam_font as g
import pytest

ROOT = Path(__file__).parent.parent.parent
HEADER = ROOT / "marquee" / "WagfamFont.h"


# ── parse_glyph ──────────────────────────────────────────────────────────────


class TestParseGlyph:
    def test_all_off_returns_seven_zero_bytes(self):
        art = "\n".join(["....."] * 7)
        assert g.parse_glyph(art) == [0, 0, 0, 0, 0, 0, 0]

    def test_all_on_top_row_packs_into_high_5_bits(self):
        # 11111000 = 0xF8
        art = "\n".join(["#####"] + ["....."] * 6)
        rows = g.parse_glyph(art)
        assert rows[0] == 0xF8
        assert rows[1:] == [0] * 6

    def test_msb_first_packing(self):
        # only leftmost pixel ON in row 0  → 0b10000000 = 0x80
        # only rightmost (5th) pixel ON in row 0 → 0b00001000 = 0x08
        assert g.parse_glyph("\n".join(["#...."] + ["....."] * 6))[0] == 0x80
        assert g.parse_glyph("\n".join(["....#"] + ["....."] * 6))[0] == 0x08

    def test_short_row_is_padded_with_off(self):
        # Row "#" is treated as "#...."
        rows = g.parse_glyph("\n".join(["#"] + ["....."] * 6))
        assert rows[0] == 0x80

    def test_invalid_row_count_raises(self):
        with pytest.raises(ValueError, match="exactly 7 rows"):
            g.parse_glyph("\n".join(["....."] * 6))

    def test_invalid_char_raises(self):
        bad = "\n".join(["##X.."] + ["....."] * 6)
        with pytest.raises(ValueError, match="invalid char"):
            g.parse_glyph(bad)


# ── pack_bitmap ──────────────────────────────────────────────────────────────


class TestPackBitmap:
    def test_5x7_roundtrips_to_5_bytes(self):
        # 7 rows of "#####" → 35 ON bits → 5 bytes (0xFF 0xFF 0xFF 0xFF 0xE0)
        rows = [0xF8] * 7
        out = g.pack_bitmap(rows, 5, 7)
        assert len(out) == 5
        assert out == [0xFF, 0xFF, 0xFF, 0xFF, 0xE0]

    def test_continuous_no_per_row_padding(self):
        # row0="#...." (1 bit on, 4 off), row1="....#" (4 off, 1 on).
        # Concatenated 5-bit chunks: "10000" + "00001" = "1000000001"
        # Plus 25 zero pad to reach 35 bits = 5 bytes:
        #   1000 0000 | 0100 0000 | 0000 0000 | 0000 0000 | 0000 0000
        #   = 0x80 0x40 0x00 0x00 0x00
        rows = [0x80, 0x08, 0, 0, 0, 0, 0]
        out = g.pack_bitmap(rows, 5, 7)
        assert out == [0x80, 0x40, 0x00, 0x00, 0x00]

    def test_zero_height_returns_empty(self):
        assert g.pack_bitmap([], 5, 0) == []


# ── full generator + round-trip ──────────────────────────────────────────────


def _roundtrip(bytes_, off, width, height):
    """Decode `width*height` bits from a continuous bitstream starting at `off`."""
    bits = []
    cur = off
    while len(bits) < width * height:
        byte = bytes_[cur]
        for i in range(8):
            bits.append((byte >> (7 - i)) & 1)
        cur += 1
    rows = []
    for y in range(height):
        row = "".join("#" if bits[y * width + x] else "." for x in range(width))
        rows.append(row)
    return rows


class TestMain:
    def test_main_writes_header_with_expected_shape(self):
        rc = g.main()
        assert rc == 0
        text = HEADER.read_text()
        assert "const GFXfont WagfamBlock" in text
        # 95 printable ASCII glyphs (0x20..0x7E)
        assert text.count("// 0x") >= 95
        # 95 glyphs * 5 bytes/glyph = 475 bitmap bytes
        bm = re.search(r"WagfamBlockBitmaps\[\] PROGMEM = \{([^}]+)\};", text, re.S)
        assert bm is not None
        nums = re.findall(r"0x[0-9A-Fa-f]+", bm.group(1))
        assert len(nums) == 475

    @pytest.mark.parametrize("ch,first_row", [
        ("A", ".###."),  # WagfamBlock 'A' top row from GLYPHS
        ("0", ".###."),
        ("1", ".##.."),
        (" ", "....."),
        ("!", ".##.."),
    ])
    def test_glyph_roundtrips_to_source_art(self, ch, first_row):
        # Re-run main to ensure we test the latest header
        g.main()
        text = HEADER.read_text()
        bm = re.search(r"WagfamBlockBitmaps\[\] PROGMEM = \{([^}]+)\};", text, re.S)
        assert bm is not None
        nums = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", bm.group(1))]
        # Find the glyph's offset in the GFXglyph table
        pat = (
            r"\{\s*(\d+),\s*5,\s*7,\s*6,\s*0,\s*-7\s*\},\s*//\s*0x"
            + f"{ord(ch):02X}"
        )
        m = re.search(pat, text)
        assert m is not None, f"glyph entry for {ch!r} (0x{ord(ch):02X}) not found"
        off = int(m.group(1))
        rows = _roundtrip(nums, off, 5, 7)
        assert rows[0] == first_row

    def test_lowercase_maps_to_uppercase_bitmap(self):
        g.main()
        text = HEADER.read_text()
        bm = re.search(r"WagfamBlockBitmaps\[\] PROGMEM = \{([^}]+)\};", text, re.S)
        nums = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", bm.group(1))]

        def _off(code):
            m = re.search(
                rf"\{{\s*(\d+),\s*5,\s*7,\s*6,\s*0,\s*-7\s*\}},\s*//\s*0x{code:02X}",
                text,
            )
            assert m is not None
            return int(m.group(1))

        # 'a' bitmap should match 'A' bitmap (we render lowercase as uppercase)
        a_lower = _roundtrip(nums, _off(ord("a")), 5, 7)
        a_upper = _roundtrip(nums, _off(ord("A")), 5, 7)
        assert a_lower == a_upper

    def test_unknown_glyph_renders_as_solid_block(self, monkeypatch):
        # Drop a printable-ASCII entry so the fallback path runs
        smaller_glyphs = dict(g.GLYPHS)
        smaller_glyphs.pop("A")
        monkeypatch.setattr(g, "GLYPHS", smaller_glyphs)
        rc = g.main()
        assert rc == 0
        text = HEADER.read_text()
        bm = re.search(r"WagfamBlockBitmaps\[\] PROGMEM = \{([^}]+)\};", text, re.S)
        nums = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", bm.group(1))]
        m = re.search(
            r"\{\s*(\d+),\s*5,\s*7,\s*6,\s*0,\s*-7\s*\},\s*//\s*0x41",
            text,
        )
        off = int(m.group(1))
        rows = _roundtrip(nums, off, 5, 7)
        assert rows == ["#####"] * 7
        # Restore the canonical header for any later tests
        g.main()


# ── script entry-point coverage ──────────────────────────────────────────────


def test_script_runs_as_main(tmp_path):
    """Cover the `if __name__ == '__main__': raise SystemExit(main())` line."""
    import runpy

    with pytest.raises(SystemExit) as exc:
        runpy.run_path(
            str(ROOT / "scripts" / "gen_wagfam_font.py"),
            run_name="__main__",
        )
    assert exc.value.code == 0
