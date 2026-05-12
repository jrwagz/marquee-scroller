"""Tests for scripts/gen_wagfam_font.py.

We exercise:
  * parse_glyph: bit packing of '#'/'.' rows into MSB-first byte rows
  * pack_bitmap: continuous (no per-row padding) bitstream packing
  * main: the full generator runs, writes WagfamFont.h with the expected
    glyph count and bitmap byte count for each of the six fonts, and the
    produced bytes round-trip back to the source ASCII art for a
    representative sample of glyphs in WagfamBlock and WagfamTall.

The round-trip assertion is the load-bearing one: it's what gives us
confidence that the bytes Adafruit_GFX::drawChar will read on-device
correspond to the glyph art in the GLYPHS dicts, since drawChar reads the
bitstream the same way we do here (MSB-first, row-major, no per-row
padding).
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
        assert g.parse_glyph(art, 5, 7) == [0, 0, 0, 0, 0, 0, 0]

    def test_all_on_top_row_packs_into_high_5_bits(self):
        # 11111000 = 0xF8
        art = "\n".join(["#####"] + ["....."] * 6)
        rows = g.parse_glyph(art, 5, 7)
        assert rows[0] == 0xF8
        assert rows[1:] == [0] * 6

    def test_msb_first_packing(self):
        # only leftmost pixel ON in row 0  → 0b10000000 = 0x80
        # only rightmost (5th) pixel ON in row 0 → 0b00001000 = 0x08
        assert g.parse_glyph("\n".join(["#...."] + ["....."] * 6), 5, 7)[0] == 0x80
        assert g.parse_glyph("\n".join(["....#"] + ["....."] * 6), 5, 7)[0] == 0x08

    def test_short_row_is_padded_with_off(self):
        # Row "#" is treated as "#...."
        rows = g.parse_glyph("\n".join(["#"] + ["....."] * 6), 5, 7)
        assert rows[0] == 0x80

    def test_invalid_row_count_raises(self):
        with pytest.raises(ValueError, match="exactly 7 rows"):
            g.parse_glyph("\n".join(["....."] * 6), 5, 7)

    def test_invalid_char_raises(self):
        bad = "\n".join(["##X.."] + ["....."] * 6)
        with pytest.raises(ValueError, match="invalid char"):
            g.parse_glyph(bad, 5, 7)

    def test_8_row_glyph(self):
        # 8-row, 5-wide all-on top row
        art = "\n".join(["#####"] + ["....."] * 7)
        rows = g.parse_glyph(art, 5, 8)
        assert len(rows) == 8
        assert rows[0] == 0xF8

    def test_3_wide_glyph(self):
        # 3-wide narrow font: leftmost pixel ON → bit 7 set → 0x80
        rows = g.parse_glyph("\n".join(["#.."] + ["..."] * 7), 3, 8)
        assert rows[0] == 0x80


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

    def test_5x8_packs_to_5_bytes(self):
        # 8 rows of "#####" → 40 ON bits → 5 full bytes
        rows = [0xF8] * 8
        out = g.pack_bitmap(rows, 5, 8)
        assert len(out) == 5
        assert out == [0xFF, 0xFF, 0xFF, 0xFF, 0xFF]

    def test_3x8_packs_to_3_bytes(self):
        # 8 rows of "###" → 24 ON bits → 3 full bytes
        rows = [0xE0] * 8
        out = g.pack_bitmap(rows, 3, 8)
        assert len(out) == 3
        assert out == [0xFF, 0xFF, 0xFF]


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


def _font_section(text: str, font_name: str) -> tuple[str, list[int]]:
    """Return (full_section_text, bitmap_bytes_as_ints) for a font."""
    bm_match = re.search(
        rf"{font_name}Bitmaps\[\] PROGMEM = \{{([^}}]+)\}};", text, re.S
    )
    assert bm_match is not None, f"{font_name} bitmap block not found"
    nums = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", bm_match.group(1))]
    glyph_match = re.search(
        rf"{font_name}Glyphs\[\] PROGMEM = \{{(.+?)\}};", text, re.S
    )
    assert glyph_match is not None, f"{font_name} glyph block not found"
    return glyph_match.group(1), nums


def _glyph_offset(section: str, code: int, w: int, h: int, adv: int, yo: int) -> int:
    pat = (
        rf"\{{\s*(\d+),\s*{w},\s*{h},\s*{adv},\s*0,\s*{yo}\s*\}},\s*//\s*0x"
        + f"{code:02X}"
    )
    m = re.search(pat, section)
    assert m is not None, f"glyph entry for 0x{code:02X} not found"
    return int(m.group(1))


class TestMain:
    def test_main_writes_header_with_all_eleven_fonts(self):
        rc = g.main()
        assert rc == 0
        text = HEADER.read_text()
        for name in (
            "WagfamBlock", "WagfamTall", "WagfamBold",
            "WagfamSlim", "WagfamOutline", "WagfamDigi",
            "WagfamItalic", "WagfamSerif", "WagfamPixel",
            "WagfamInverse", "WagfamStencil",
        ):
            assert f"const GFXfont {name}" in text, f"missing {name}"

    @pytest.mark.parametrize(
        "font_name,width,height,adv,yo,bytes_per_glyph",
        [
            ("WagfamBlock",   5, 7, 6, -7, 5),  # 5x7=35 bits → 5 bytes
            ("WagfamTall",    5, 8, 6, -8, 5),  # 5x8=40 bits → 5 bytes
            ("WagfamBold",    5, 8, 6, -8, 5),
            ("WagfamSlim",    3, 8, 4, -8, 3),  # 3x8=24 bits → 3 bytes
            ("WagfamOutline", 5, 8, 6, -8, 5),
            ("WagfamDigi",    5, 8, 6, -8, 5),
            ("WagfamItalic",  5, 8, 6, -8, 5),
            ("WagfamSerif",   5, 8, 6, -8, 5),
            ("WagfamPixel",   5, 8, 6, -8, 5),
            ("WagfamInverse", 5, 8, 6, -8, 5),
            ("WagfamStencil", 5, 8, 6, -8, 5),
        ],
    )
    def test_each_font_has_95_glyphs_and_expected_bitmap_size(
        self, font_name, width, height, adv, yo, bytes_per_glyph
    ):
        g.main()
        text = HEADER.read_text()
        section, nums = _font_section(text, font_name)
        # 95 printable ASCII glyphs (0x20..0x7E)
        assert section.count("// 0x") == 95
        # bitmap size = 95 * bytes_per_glyph
        assert len(nums) == 95 * bytes_per_glyph

    @pytest.mark.parametrize("ch,first_row", [
        ("A", ".###."),
        ("0", ".###."),
        ("1", ".##.."),
        (" ", "....."),
        ("!", ".##.."),
    ])
    def test_block_glyph_roundtrips_to_source_art(self, ch, first_row):
        g.main()
        text = HEADER.read_text()
        section, nums = _font_section(text, "WagfamBlock")
        off = _glyph_offset(section, ord(ch), 5, 7, 6, -7)
        rows = _roundtrip(nums, off, 5, 7)
        assert rows[0] == first_row

    @pytest.mark.parametrize("ch,first_row", [
        ("A", "..#.."),  # Tall A is pyramid-topped
        ("0", ".###."),
        ("a", "....."),  # lowercase a starts with empty rows (shorter glyph)
        ("g", "....."),
    ])
    def test_tall_glyph_roundtrips_to_source_art(self, ch, first_row):
        g.main()
        text = HEADER.read_text()
        section, nums = _font_section(text, "WagfamTall")
        off = _glyph_offset(section, ord(ch), 5, 8, 6, -8)
        rows = _roundtrip(nums, off, 5, 8)
        assert rows[0] == first_row

    def test_block_lowercase_falls_back_to_uppercase(self):
        g.main()
        text = HEADER.read_text()
        section, nums = _font_section(text, "WagfamBlock")
        a_lower = _roundtrip(nums, _glyph_offset(section, ord("a"), 5, 7, 6, -7),
                             5, 7)
        a_upper = _roundtrip(nums, _glyph_offset(section, ord("A"), 5, 7, 6, -7),
                             5, 7)
        assert a_lower == a_upper

    def test_tall_lowercase_is_distinct_from_uppercase(self):
        """Tall uses 'distinct' lowercase strategy: 'a' must NOT equal 'A'."""
        g.main()
        text = HEADER.read_text()
        section, nums = _font_section(text, "WagfamTall")
        a_lower = _roundtrip(nums, _glyph_offset(section, ord("a"), 5, 8, 6, -8),
                             5, 8)
        a_upper = _roundtrip(nums, _glyph_offset(section, ord("A"), 5, 8, 6, -8),
                             5, 8)
        assert a_lower != a_upper

    def test_bold_lowercase_falls_back_to_uppercase(self):
        """Bold uses 'fallback' lowercase strategy: 'a' must equal 'A'."""
        g.main()
        text = HEADER.read_text()
        section, nums = _font_section(text, "WagfamBold")
        a_lower = _roundtrip(nums, _glyph_offset(section, ord("a"), 5, 8, 6, -8),
                             5, 8)
        a_upper = _roundtrip(nums, _glyph_offset(section, ord("A"), 5, 8, 6, -8),
                             5, 8)
        assert a_lower == a_upper

    def test_slim_glyphs_are_3_wide(self):
        g.main()
        text = HEADER.read_text()
        section, _ = _font_section(text, "WagfamSlim")
        # Every glyph entry should declare width=3, xAdvance=4
        entries = re.findall(r"\{\s*\d+,\s*(\d+),\s*\d+,\s*(\d+),", section)
        assert entries, "no glyph entries parsed"
        for w, adv in entries:
            assert int(w) == 3, f"non-3-wide glyph in Slim: width={w}"
            assert int(adv) == 4, f"non-4 advance in Slim: adv={adv}"

    @pytest.mark.parametrize(
        "font_name",
        ["WagfamItalic", "WagfamSerif", "WagfamPixel",
         "WagfamInverse", "WagfamStencil"],
    )
    def test_derived_font_differs_from_tall(self, font_name):
        """Each derived font must produce distinct bitmaps from Tall.

        Catches a regression where a transform accidentally short-circuits
        (e.g. a parameter swap that makes the output identical to the input).
        We compare on capital 'A' since every transform changes it visibly.
        """
        g.main()
        text = HEADER.read_text()
        tall_section, tall_bytes = _font_section(text, "WagfamTall")
        derived_section, derived_bytes = _font_section(text, font_name)
        tall_off = _glyph_offset(tall_section, ord("A"), 5, 8, 6, -8)
        derived_off = _glyph_offset(derived_section, ord("A"), 5, 8, 6, -8)
        assert _roundtrip(tall_bytes, tall_off, 5, 8) != \
            _roundtrip(derived_bytes, derived_off, 5, 8), \
            f"{font_name} 'A' is identical to Tall 'A' — transform did nothing"

    def test_derived_fonts_inherit_distinct_lowercase(self):
        """Derived fonts apply the transform to every Tall glyph including
        a-z, so 'a' and 'A' must remain distinct after derivation."""
        g.main()
        text = HEADER.read_text()
        for font_name in ("WagfamItalic", "WagfamSerif", "WagfamPixel",
                          "WagfamStencil"):
            section, nums = _font_section(text, font_name)
            a_lower = _roundtrip(
                nums, _glyph_offset(section, ord("a"), 5, 8, 6, -8), 5, 8)
            a_upper = _roundtrip(
                nums, _glyph_offset(section, ord("A"), 5, 8, 6, -8), 5, 8)
            assert a_lower != a_upper, \
                f"{font_name} 'a' equals 'A' — distinct lowercase lost"

    def test_inverse_inverts_each_pixel_in_top_rows(self):
        """Spot-check inverse_glyph: the source's ON pixels must be OFF in
        the top h-1 rows of the output, and vice versa. Row h-1 is forced
        OFF as the inter-glyph gap (so adjacent inverses don't merge)."""
        src = "\n".join([
            "#####",
            ".....",
            "#####",
            ".....",
            "#####",
            ".....",
            "#####",
            ".....",
        ])
        out = g.inverse_glyph(src, 5, 8)
        rows = out.splitlines()
        assert rows[0] == "....."  # was all-on → all-off
        assert rows[1] == "#####"  # was all-off → all-on
        assert rows[6] == "....."  # was all-on → all-off
        assert rows[7] == "....."  # gap row always off

    def test_stencil_cuts_middle_of_long_vertical_runs(self):
        """A column with one long contiguous ON run gets exactly one OFF
        pixel inserted at the middle of that run — the stencil bridge."""
        # Single column with rows 1..6 ON (length 6, mid = (1+6)//2 = 3)
        src = "\n".join([
            ".....",
            "#....",
            "#....",
            "#....",
            "#....",
            "#....",
            "#....",
            ".....",
        ])
        out = g.stencil_glyph(src, 5, 8).splitlines()
        # Rows 1..6 had col 0 ON; row 3 should now be OFF, others still ON
        assert out[3][0] == "."
        assert out[1][0] == "#" and out[2][0] == "#"
        assert out[4][0] == "#" and out[5][0] == "#" and out[6][0] == "#"

    def test_serif_flares_top_and_bottom_endpoints(self):
        """Serif rule: the top row and h-2 row spread ON pixels by ±1 col
        wherever the neighbor is currently OFF."""
        # A simple vertical bar at col 2, rows 0..6
        src = "\n".join([
            "..#..",
            "..#..",
            "..#..",
            "..#..",
            "..#..",
            "..#..",
            "..#..",
            ".....",
        ])
        out = g.serif_glyph(src, 5, 8).splitlines()
        assert out[0] == ".###."  # top serif spread
        assert out[6] == ".###."  # bottom serif spread (row h-2 = 6)
        assert out[3] == "..#.."  # middle untouched

    def test_pixel_speckle_is_deterministic_and_reduces_density(self):
        """Same input produces the same speckled output; output has fewer
        ON pixels than input (the speckle removes ~1 in 5)."""
        src = "\n".join(["#####"] * 7 + ["....."])  # 35 ON pixels
        out_a = g.pixel_glyph(src, 5, 8)
        out_b = g.pixel_glyph(src, 5, 8)
        assert out_a == out_b
        on_in = sum(row.count("#") for row in src.splitlines())
        on_out = sum(row.count("#") for row in out_a.splitlines())
        assert on_out < on_in, "speckle removed nothing"
        # Sanity: at most a handful of pixels removed (not catastrophic)
        assert (on_in - on_out) <= 12, f"removed {on_in - on_out} of {on_in}"

    def test_unknown_glyph_renders_as_solid_block(self, monkeypatch):
        # Drop a printable-ASCII entry so the fallback path runs.
        # We patch BLOCK_GLYPHS specifically since WagfamBlock uses
        # lowercase fallback — drop 'A' and lowercase 'a' resolves to it,
        # which then misses the dict and renders as a solid 5x7 block.
        smaller_glyphs = dict(g.BLOCK_GLYPHS)
        smaller_glyphs.pop("A")
        monkeypatch.setattr(g, "BLOCK_GLYPHS", smaller_glyphs)
        # FONTS holds a reference to the original BLOCK_GLYPHS dict; rebuild.
        block_idx = next(i for i, f in enumerate(g.FONTS) if f.name == "WagfamBlock")
        g.FONTS[block_idx].glyphs = smaller_glyphs
        rc = g.main()
        assert rc == 0
        text = HEADER.read_text()
        section, nums = _font_section(text, "WagfamBlock")
        off = _glyph_offset(section, ord("A"), 5, 7, 6, -7)
        rows = _roundtrip(nums, off, 5, 7)
        assert rows == ["#####"] * 7
        # Restore the canonical header for any later tests
        g.FONTS[block_idx].glyphs = g.BLOCK_GLYPHS
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
