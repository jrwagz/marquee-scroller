// Clock face render styles — separate registry from the marquee scroller fonts
// because the clock display has different constraints: only ten digits, a
// colon, and (in 12-hour mode) an AM/PM marker. Knowing the exact glyph set
// lets each style optimize layout and use pixels we couldn't on a general
// scrolling font.
//
// Per-frame the main loop dispatches `centerPrint(displayTime, true)` through
// renderClock(); the selected style's `render(...)` clears the matrix, draws
// the time creatively, draws the optional event-day border, and writes the
// frame. Styles with `supports24h == false` are skipped (Classic falls back)
// when the device is in 24-hour mode.
#pragma once

#include <Adafruit_GFX.h>

// Forward declarations from marquee.ino / Settings.h — let render() functions
// read live state without us having to plumb every global through the function
// pointer. Types here MUST match the originals exactly or the linker will pick
// a different symbol — `boolean` (Arduino's uint8_t typedef) for the booleans,
// uint32_t for the dot-walker counters, plain int for the spacing constants.
extern Max72xxPanel matrix;
extern boolean WAGFAM_EVENT_TODAY;
extern boolean IS_PM;
extern boolean IS_24HOUR;
extern int width;            // 6 — Adafruit builtin 5x7 char advance
extern uint32_t todayDisplayMilliSecond;
extern uint32_t todayDisplayStartingLED;
extern int TODAY_DISPLAY_DOT_SPACING;
extern int TODAY_DISPLAY_DOT_SPEED_MS;

// Render callback signature. ``hour12`` is 1..12 (always — the renderer
// converts back to 24h itself if it cares); ``isPM`` is meaningful only when
// the style is 12h-only. ``isRefresh`` mirrors centerPrint's existing flag —
// when the device is mid-data-refresh we hold the colon visible so the frame
// stays readable. ``eventDay`` triggers the dotted border.
typedef void (*ClockRenderFn)(uint8_t hour12, uint8_t hour24, uint8_t minute,
                              bool isPM, bool isRefresh, bool eventDay);

struct ClockStyle {
  const char *name;        // user-facing label
  bool supports12h;        // safe to use when IS_24HOUR == false
  bool supports24h;        // safe to use when IS_24HOUR == true
  ClockRenderFn render;
};


// ── Shared helpers ──────────────────────────────────────────────────────────

// Animated event-day border, lifted from centerPrint() so every style can
// opt in. Same math: walk a "starting LED" cursor around the U-shaped border
// (left edge top→bottom, bottom edge left→right, right edge bottom→top) and
// light up every TODAY_DISPLAY_DOT_SPACING-th pixel.
inline void drawEventDayBorder() {
  todayDisplayMilliSecond = millis() % (TODAY_DISPLAY_DOT_SPACING * TODAY_DISPLAY_DOT_SPEED_MS);
  todayDisplayStartingLED = todayDisplayMilliSecond / TODAY_DISPLAY_DOT_SPEED_MS;
  for (int i = 0; i < (matrix.height() * 2 + matrix.width() - 2); i++) {
    if ((i % TODAY_DISPLAY_DOT_SPACING) == (int)todayDisplayStartingLED) {
      if (i < matrix.height()) {
        matrix.drawPixel(0, i, HIGH);
      } else if (i < (matrix.height() + matrix.width() - 2)) {
        matrix.drawPixel(i - (matrix.height() - 1), matrix.height() - 1, HIGH);
      } else {
        matrix.drawPixel(matrix.width() - 1,
                         (matrix.height() - 1) - (i - (matrix.height() + matrix.width() - 2)),
                         HIGH);
      }
    }
  }
}

// Draw the standard PM dot at the top-right corner — same position as
// centerPrint's existing logic so styles that fall through to Classic don't
// have to duplicate the math.
inline void drawPmDotIfNeeded(bool isPM) {
  if (!IS_24HOUR && IS_PM && isPM) {
    matrix.drawPixel(matrix.width() - 1, 6, HIGH);
  }
}


// ── Style 0: Classic (existing centerPrint behavior) ────────────────────────
//
// Mirrors the original code path so every other style's render() can fall
// back to this when 24h/12h compatibility doesn't match. The fixed-pitch 6px
// `width` advance is read from the global so a future tweak there doesn't
// drift this copy.

inline void renderClassicTime(const char *text, bool isRefresh) {
  matrix.setFont();
  int x = (matrix.width() - ((int)strlen(text) * width)) / 2;
  matrix.setCursor(x, 0);
  matrix.print(text);
  (void)isRefresh;
}

inline void formatHHMM(char *buf, uint8_t h, uint8_t m, bool blinkColon, bool isRefresh) {
  // Two-character hour, blinkable colon, two-character minute. Mirrors
  // hourMinutes() / secondsIndicator() so every style has the same canonical
  // text to start from.
  if (h < 10) { buf[0] = ' '; buf[1] = '0' + h; }
  else        { buf[0] = '0' + (h / 10); buf[1] = '0' + (h % 10); }
  bool colonOn = isRefresh || !blinkColon || ((second() % 2) != 0);
  buf[2] = colonOn ? ':' : ' ';
  buf[3] = '0' + (m / 10);
  buf[4] = '0' + (m % 10);
  buf[5] = '\0';
}

inline void renderClassic(uint8_t hour12, uint8_t hour24, uint8_t minute,
                          bool isPM, bool isRefresh, bool eventDay) {
  char buf[6];
  formatHHMM(buf, IS_24HOUR ? hour24 : hour12, minute, /*blinkColon=*/true, isRefresh);
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
  renderClassicTime(buf, isRefresh);
}


// ── Style 2: Banner (12h + 24h) ─────────────────────────────────────────────
//
// Classic digits centered at row 0, framed by horizontal lines on the very
// top (row 0 stays empty for the digits) and very bottom (row 7) that span
// the full 32-pixel width. To avoid clobbering the digits the bars sit on
// rows 0 and 7; the Classic font draws into rows 0..6, so we only paint the
// bottom bar at row 7. The top is rendered as 2-pixel notches at the ends.

inline void renderBanner(uint8_t hour12, uint8_t hour24, uint8_t minute,
                         bool isPM, bool isRefresh, bool eventDay) {
  char buf[6];
  formatHHMM(buf, IS_24HOUR ? hour24 : hour12, minute, /*blinkColon=*/true, isRefresh);
  // Bottom rule across the whole display.
  for (int x = 0; x < matrix.width(); x++) matrix.drawPixel(x, 7, HIGH);
  // Top corner notches — draw two pixels on each end of row 0 so the digits
  // have visual "brackets" without obscuring the colon/digits area.
  matrix.drawPixel(0, 0, HIGH);
  matrix.drawPixel(1, 0, HIGH);
  matrix.drawPixel(matrix.width() - 2, 0, HIGH);
  matrix.drawPixel(matrix.width() - 1, 0, HIGH);
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
  renderClassicTime(buf, isRefresh);
}


// ── Style 3: Pulse (12h + 24h) ──────────────────────────────────────────────
//
// Same digits as Classic, but the colon is replaced with a pulse: every
// 500 ms it alternates between a small dot (·) at row 3 and a larger square
// (▪) spanning rows 2..4. Reads as a heartbeat between the hours and minutes
// without burning the natural colon-blink that secondsIndicator() does.

inline void renderPulse(uint8_t hour12, uint8_t hour24, uint8_t minute,
                        bool isPM, bool isRefresh, bool eventDay) {
  // Build the time string with a SPACE in the colon slot — we draw the pulse
  // ourselves on top, so the font's colon would just collide.
  char buf[6];
  formatHHMM(buf, IS_24HOUR ? hour24 : hour12, minute, /*blinkColon=*/false, isRefresh);
  buf[2] = ' ';
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
  renderClassicTime(buf, isRefresh);
  // Pulse position: dead-center of the displayed text. The colon character
  // sat at index 2 of the 5-char string, so it occupies cols x..x+5 where
  // x = renderClassicTime's start + 2 * width. We re-derive that here.
  int textWidth = 5 * width;
  int textStart = (matrix.width() - textWidth) / 2;
  int colonX = textStart + 2 * width + 1;  // +1 to land on the colon's center
  bool big = ((millis() / 500) % 2) == 0;
  if (big) {
    matrix.fillRect(colonX, 2, 3, 3, HIGH);
  } else {
    matrix.drawPixel(colonX + 1, 3, HIGH);
  }
}


// ── Style 5: Frame (12h + 24h) ──────────────────────────────────────────────
//
// Time inside a 1-pixel rectangular border. The Classic digits are 7 tall
// so they fit inside a 32x8 box with row 7 as the bottom edge and row 0
// reserved for the top edge — but row 0 is also the top of the digits, so
// we use row 7 as the BOTTOM edge and skip a top edge (the natural "chin"
// of 5x7 digits already creates a visual ceiling). Side edges sit at col 0
// and col 31 from row 0..6.

inline void renderFrame(uint8_t hour12, uint8_t hour24, uint8_t minute,
                        bool isPM, bool isRefresh, bool eventDay) {
  char buf[6];
  formatHHMM(buf, IS_24HOUR ? hour24 : hour12, minute, /*blinkColon=*/true, isRefresh);
  // Side rails
  for (int y = 0; y < matrix.height() - 1; y++) {
    matrix.drawPixel(0, y, HIGH);
    matrix.drawPixel(matrix.width() - 1, y, HIGH);
  }
  // Bottom rail
  for (int x = 0; x < matrix.width(); x++) matrix.drawPixel(x, 7, HIGH);
  // Top corner pips so the frame visually closes
  matrix.drawPixel(0, 0, HIGH);
  matrix.drawPixel(matrix.width() - 1, 0, HIGH);
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
  renderClassicTime(buf, isRefresh);
}


// (Dotted's render function is defined further down once BIG_DIGITS is in
// scope — it's just BIG_DIGITS with the same `(x*7 + y*13) % 5 == 0` halftone
// mask applied per-pixel during draw.)


// ── Bespoke 5x8 "Big" digit bitmaps for Mega, Italic, Inverse ──────────────
//
// Each digit fills the full 8-pixel matrix height (vs Classic's 7). Stored
// row-major: 8 bytes per glyph, 5 bits used (high-5). Indexed by digit.

const uint8_t BIG_DIGITS[10][8] PROGMEM = {
  // 0: rounded oval that fills 8 rows
  {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},
  // 1: thick stem with serif foot
  {0x20, 0x60, 0xA0, 0x20, 0x20, 0x20, 0x20, 0xF8},
  // 2: classic 8-tall 2 with curved top and flat bottom
  {0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0x80, 0xF8},
  // 3: 8-tall S-bend
  {0x70, 0x88, 0x08, 0x30, 0x08, 0x08, 0x88, 0x70},
  // 4: open-top 4 with full crossbar
  {0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10, 0x10},
  // 5: standard 5
  {0xF8, 0x80, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70},
  // 6: closed-bottom 6
  {0x70, 0x80, 0x80, 0xF0, 0x88, 0x88, 0x88, 0x70},
  // 7: triangular 7 with slight curve
  {0xF8, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x40},
  // 8: 8-tall figure-8 (no overlap rows)
  {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x88, 0x70},
  // 9: open-top 9 with descender into row 7
  {0x70, 0x88, 0x88, 0x88, 0x78, 0x08, 0x10, 0xE0},
};

// "Mega" colon: vertically-stacked 1-px dots, full 8 rows tall
const uint8_t BIG_COLON[8] PROGMEM = {
  0x00, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00,
};

// Helper: blit a bitmap glyph (5 cols, 8 rows) at (x, y). Width is 5,
// rendering using the high 5 bits of each row byte. `inverted` flips the
// pixel polarity for the Inverse style.
inline void drawBigGlyph(int x, int y, const uint8_t *glyph, bool inverted) {
  for (int row = 0; row < 8; row++) {
    uint8_t bits = pgm_read_byte(&glyph[row]);
    for (int col = 0; col < 5; col++) {
      bool on = (bits & (0x80 >> col)) != 0;
      if (inverted) on = !on;
      if (on) matrix.drawPixel(x + col, y + row, HIGH);
    }
  }
}


// ── Style 1: Mega12h ────────────────────────────────────────────────────────
//
// "1:23" or "12:45" rendered with the BIG_DIGITS set. 5-col digits + 1-col
// gap + 1-col colon + 1-col gap = 13 cols for a 4-char render ("1:23") or
// 19 cols for "12:45". Centered in the 32-wide display.

inline int megaRenderWidth(uint8_t hour) {
  // hour digit count (1-9 single-digit, 10-12 double-digit) plus 5 for ":MM"
  int hourCols = (hour >= 10 ? 11 : 5);  // 5 + 1 + 5 OR 5
  return hourCols + /*colon*/ 1 + /*gap*/ 1 + /*MM*/ 5 + 5 + 1 /*gap between MM digits*/;
}

inline void renderMega12h(uint8_t hour12, uint8_t hour24, uint8_t minute,
                          bool isPM, bool isRefresh, bool eventDay) {
  (void)hour24;
  // Layout: each glyph is 5 wide; gap of 1 between digits; colon is its own
  // 1-col cell. Build a list of cells, then center the whole string.
  uint8_t cells[5];
  uint8_t cellCount = 0;
  if (hour12 >= 10) {
    cells[cellCount++] = 1;  // digit "1" (always 1 for 12h hours 10-12)
    cells[cellCount++] = hour12 - 10;
  } else {
    cells[cellCount++] = hour12;
  }
  cells[cellCount++] = 0xFF;  // sentinel for colon
  cells[cellCount++] = minute / 10;
  cells[cellCount++] = minute % 10;

  // Total width: 5 per digit cell, 1 per colon cell, 1-pixel gap between
  // each. We compute it on the fly to support 1- vs 2-digit hour widths.
  int totalCols = 0;
  for (uint8_t i = 0; i < cellCount; i++) {
    totalCols += (cells[i] == 0xFF) ? 1 : 5;
    if (i + 1 < cellCount) totalCols += 1;  // gap
  }
  int x = (matrix.width() - totalCols) / 2;

  bool colonOn = isRefresh || ((second() % 2) != 0);
  for (uint8_t i = 0; i < cellCount; i++) {
    if (cells[i] == 0xFF) {
      if (colonOn) {
        matrix.drawPixel(x, 2, HIGH);
        matrix.drawPixel(x, 5, HIGH);
      }
      x += 1 + 1;
    } else {
      drawBigGlyph(x, 0, BIG_DIGITS[cells[i]], /*inverted=*/false);
      x += 5 + 1;
    }
  }
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
}


// ── Style 7: Inverse12h ─────────────────────────────────────────────────────
//
// Fill the entire 32x8 (rows 0..6 — leave row 7 free as a separator from the
// PM dot), then carve out the time digits as OFF pixels using BIG_DIGITS
// inverted. Looks like cut-out kerning on a solid plate.

inline void renderInverse12h(uint8_t hour12, uint8_t hour24, uint8_t minute,
                             bool isPM, bool isRefresh, bool eventDay) {
  (void)hour24;
  matrix.fillRect(0, 0, matrix.width(), 7, HIGH);

  // Same layout machinery as Mega — single source of truth would be nice
  // but the 5-col branch makes a generic helper noisy.
  uint8_t cells[5];
  uint8_t cellCount = 0;
  if (hour12 >= 10) {
    cells[cellCount++] = 1;
    cells[cellCount++] = hour12 - 10;
  } else {
    cells[cellCount++] = hour12;
  }
  cells[cellCount++] = 0xFF;
  cells[cellCount++] = minute / 10;
  cells[cellCount++] = minute % 10;

  int totalCols = 0;
  for (uint8_t i = 0; i < cellCount; i++) {
    totalCols += (cells[i] == 0xFF) ? 1 : 5;
    if (i + 1 < cellCount) totalCols += 1;
  }
  int x = (matrix.width() - totalCols) / 2;

  bool colonOn = isRefresh || ((second() % 2) != 0);
  for (uint8_t i = 0; i < cellCount; i++) {
    if (cells[i] == 0xFF) {
      // Carve out the colon: turn off the two dots in the otherwise solid plate
      if (colonOn) {
        matrix.drawPixel(x, 2, LOW);
        matrix.drawPixel(x, 5, LOW);
      }
      x += 1 + 1;
    } else {
      // Draw the digit's ON pixels as OFF on the solid plate. drawBigGlyph
      // with inverted=true does that; but we also need to LEAVE the off
      // pixels as ON. Since the plate is already filled, we simply draw the
      // original glyph as OFF pixels.
      const uint8_t *g = BIG_DIGITS[cells[i]];
      for (int row = 0; row < 8; row++) {
        uint8_t bits = pgm_read_byte(&g[row]);
        for (int col = 0; col < 5; col++) {
          if (bits & (0x80 >> col)) matrix.drawPixel(x + col, row, LOW);
        }
      }
      x += 5 + 1;
    }
  }
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
}


// ── Style 9: Italic12h ──────────────────────────────────────────────────────
//
// BIG_DIGITS + a per-row right-shift: rows 0..3 shift by 1, rows 4..7 stay
// in place. Same trick the marquee Italic font uses, applied to the 5x8
// digits instead of letters. Reads as a leaning clock.

inline void drawItalicGlyph(int x, int y, const uint8_t *glyph) {
  for (int row = 0; row < 8; row++) {
    uint8_t bits = pgm_read_byte(&glyph[row]);
    int shift = (row < 4) ? 1 : 0;
    for (int col = 0; col < 5; col++) {
      if (bits & (0x80 >> col)) {
        int nx = x + col + shift;
        if (nx < x + 6) matrix.drawPixel(nx, y + row, HIGH);
      }
    }
  }
}

inline void renderItalic12h(uint8_t hour12, uint8_t hour24, uint8_t minute,
                            bool isPM, bool isRefresh, bool eventDay) {
  (void)hour24;
  uint8_t cells[5];
  uint8_t cellCount = 0;
  if (hour12 >= 10) {
    cells[cellCount++] = 1;
    cells[cellCount++] = hour12 - 10;
  } else {
    cells[cellCount++] = hour12;
  }
  cells[cellCount++] = 0xFF;
  cells[cellCount++] = minute / 10;
  cells[cellCount++] = minute % 10;

  // Italic widens each glyph cell to 6 cols (5 + 1 for the slant overhang).
  int totalCols = 0;
  for (uint8_t i = 0; i < cellCount; i++) {
    totalCols += (cells[i] == 0xFF) ? 1 : 6;
    if (i + 1 < cellCount) totalCols += 1;
  }
  int x = (matrix.width() - totalCols) / 2;

  bool colonOn = isRefresh || ((second() % 2) != 0);
  for (uint8_t i = 0; i < cellCount; i++) {
    if (cells[i] == 0xFF) {
      // Italic-y colon: top dot shifted right, bottom dot in place
      if (colonOn) {
        matrix.drawPixel(x + 1, 2, HIGH);
        matrix.drawPixel(x, 5, HIGH);
      }
      x += 1 + 1;
    } else {
      drawItalicGlyph(x, 0, BIG_DIGITS[cells[i]]);
      x += 6 + 1;
    }
  }
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
}


// ── Style 8: Stencil (12h + 24h) ────────────────────────────────────────────
//
// BIG_DIGITS rendered with stencil "bridge" cuts: at row 4 of every digit,
// turn off any column whose original cell is between two ON neighbors (i.e.
// long-vertical-stem detection lite). Reads as cardboard-stencil letters.

inline void drawStencilGlyph(int x, int y, const uint8_t *glyph) {
  // Pre-decode all 8 rows so we can run a midline cut after drawing.
  uint8_t rows[8];
  for (int i = 0; i < 8; i++) rows[i] = pgm_read_byte(&glyph[i]);
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 5; col++) {
      bool on = (rows[row] & (0x80 >> col)) != 0;
      // Cut: if this is the middle row of a 3+ run in this column, drop it.
      // Check by looking at the rows directly above and below.
      if (on && row >= 1 && row <= 6) {
        bool above = (rows[row - 1] & (0x80 >> col)) != 0;
        bool below = (rows[row + 1] & (0x80 >> col)) != 0;
        if (above && below && row == 4) on = false;  // single midline cut
      }
      if (on) matrix.drawPixel(x + col, y + row, HIGH);
    }
  }
}

inline void renderStencil(uint8_t hour12, uint8_t hour24, uint8_t minute,
                          bool isPM, bool isRefresh, bool eventDay) {
  uint8_t hour = IS_24HOUR ? hour24 : hour12;
  // Always two-character hour for 24h, but 12h has the same "short hours"
  // optimization as Mega so "1:23" doesn't waste an extra digit cell.
  uint8_t cells[5];
  uint8_t cellCount = 0;
  if (IS_24HOUR || hour >= 10) {
    cells[cellCount++] = hour / 10;
    cells[cellCount++] = hour % 10;
  } else {
    cells[cellCount++] = hour;
  }
  cells[cellCount++] = 0xFF;
  cells[cellCount++] = minute / 10;
  cells[cellCount++] = minute % 10;

  int totalCols = 0;
  for (uint8_t i = 0; i < cellCount; i++) {
    totalCols += (cells[i] == 0xFF) ? 1 : 5;
    if (i + 1 < cellCount) totalCols += 1;
  }
  int x = (matrix.width() - totalCols) / 2;

  bool colonOn = isRefresh || ((second() % 2) != 0);
  for (uint8_t i = 0; i < cellCount; i++) {
    if (cells[i] == 0xFF) {
      if (colonOn) {
        matrix.drawPixel(x, 2, HIGH);
        matrix.drawPixel(x, 5, HIGH);
      }
      x += 1 + 1;
    } else {
      drawStencilGlyph(x, 0, BIG_DIGITS[cells[i]]);
      x += 5 + 1;
    }
  }
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
}


// ── Style 10: Dotted (12h + 24h) ────────────────────────────────────────────
//
// BIG_DIGITS with a deterministic halftone speckle: for every ON pixel that
// also satisfies `(x*7 + y*13) % 5 == 0` we skip the draw. ~20% of pixels
// drop, leaving the silhouette dense enough to read as a digit but visibly
// stippled. Same mask as the marquee Pixel font.

inline void drawDottedGlyph(int x, int y, const uint8_t *glyph) {
  for (int row = 0; row < 8; row++) {
    uint8_t bits = pgm_read_byte(&glyph[row]);
    for (int col = 0; col < 5; col++) {
      bool on = (bits & (0x80 >> col)) != 0;
      if (!on) continue;
      int px = x + col;
      int py = y + row;
      if (((px * 7 + py * 13) % 5) == 0) continue;
      matrix.drawPixel(px, py, HIGH);
    }
  }
}

inline void renderDotted(uint8_t hour12, uint8_t hour24, uint8_t minute,
                         bool isPM, bool isRefresh, bool eventDay) {
  uint8_t hour = IS_24HOUR ? hour24 : hour12;
  uint8_t cells[5];
  uint8_t cellCount = 0;
  if (IS_24HOUR || hour >= 10) {
    cells[cellCount++] = hour / 10;
    cells[cellCount++] = hour % 10;
  } else {
    cells[cellCount++] = hour;
  }
  cells[cellCount++] = 0xFF;
  cells[cellCount++] = minute / 10;
  cells[cellCount++] = minute % 10;

  int totalCols = 0;
  for (uint8_t i = 0; i < cellCount; i++) {
    totalCols += (cells[i] == 0xFF) ? 1 : 5;
    if (i + 1 < cellCount) totalCols += 1;
  }
  int x = (matrix.width() - totalCols) / 2;

  bool colonOn = isRefresh || ((second() % 2) != 0);
  for (uint8_t i = 0; i < cellCount; i++) {
    if (cells[i] == 0xFF) {
      if (colonOn) {
        matrix.drawPixel(x, 2, HIGH);
        matrix.drawPixel(x, 5, HIGH);
      }
      x += 1 + 1;
    } else {
      drawDottedGlyph(x, 0, BIG_DIGITS[cells[i]]);
      x += 5 + 1;
    }
  }
  if (eventDay) drawEventDayBorder();
  drawPmDotIfNeeded(isPM);
}


// ── Style 4: Stack12h ───────────────────────────────────────────────────────
//
// Hour digit(s) in the top half (rows 0..3) and minute digits in the bottom
// half (rows 4..7), each rendered with a custom 3x4 micro-digit set. A 2-digit
// hour fits as "12" centered horizontally; "1" gets its own centered render.
// A tiny PM arrow goes in the bottom-right corner. 12h-only because at 24h
// the hour can be "23", and the layout means BOTH halves carry 2-digit pairs
// — which still works, but loses the visual asymmetry that makes Stack feel
// distinct from Classic.

const uint8_t MINI_DIGITS[10][4] PROGMEM = {
  // 3x4: each row's high 3 bits = cols 0..2
  {0xE0, 0xA0, 0xA0, 0xE0},  // 0
  {0x40, 0xC0, 0x40, 0xE0},  // 1
  {0xE0, 0x20, 0x40, 0xE0},  // 2
  {0xE0, 0x60, 0x20, 0xE0},  // 3
  {0xA0, 0xA0, 0xE0, 0x20},  // 4
  {0xE0, 0xC0, 0x20, 0xE0},  // 5
  {0xE0, 0x80, 0xE0, 0xE0},  // 6
  {0xE0, 0x20, 0x40, 0x40},  // 7
  {0xE0, 0xE0, 0xA0, 0xE0},  // 8
  {0xE0, 0xE0, 0x20, 0xE0},  // 9
};

inline void drawMiniGlyph(int x, int y, const uint8_t *glyph) {
  for (int row = 0; row < 4; row++) {
    uint8_t bits = pgm_read_byte(&glyph[row]);
    for (int col = 0; col < 3; col++) {
      if (bits & (0x80 >> col)) matrix.drawPixel(x + col, y + row, HIGH);
    }
  }
}

inline void renderStack12h(uint8_t hour12, uint8_t hour24, uint8_t minute,
                           bool isPM, bool isRefresh, bool eventDay) {
  (void)hour24; (void)isRefresh;
  // Top half: hour digits, centered. 1-digit hour = single 3x4 glyph; 2-digit
  // hour ("10", "11", "12") = two 3x4 glyphs with a 1-col gap.
  if (hour12 >= 10) {
    int hourW = 3 + 1 + 3;
    int hx = (matrix.width() - hourW) / 2;
    drawMiniGlyph(hx, 0, MINI_DIGITS[hour12 / 10]);
    drawMiniGlyph(hx + 4, 0, MINI_DIGITS[hour12 % 10]);
  } else {
    int hx = (matrix.width() - 3) / 2;
    drawMiniGlyph(hx, 0, MINI_DIGITS[hour12]);
  }

  // Bottom half: minute (always 2-digit, zero-padded)
  int minW = 3 + 1 + 3;
  int mx = (matrix.width() - minW) / 2;
  drawMiniGlyph(mx, 4, MINI_DIGITS[minute / 10]);
  drawMiniGlyph(mx + 4, 4, MINI_DIGITS[minute % 10]);

  // PM marker: tiny dot pair top-right when isPM
  if (IS_PM && isPM) {
    matrix.drawPixel(matrix.width() - 1, 0, HIGH);
    matrix.drawPixel(matrix.width() - 1, 1, HIGH);
  }
  if (eventDay) drawEventDayBorder();
}


// ── Style 6: Suffix12h ──────────────────────────────────────────────────────
//
// Compact "H:MM AM/PM": Classic-tiny digits at left, 3-letter AM/PM at right
// using the shipped TomThumb 3x5 font for the suffix. 12h-only — at 24h the
// suffix is meaningless and we'd lose the room.

inline void renderSuffix12h(uint8_t hour12, uint8_t hour24, uint8_t minute,
                            bool isPM, bool isRefresh, bool eventDay) {
  (void)hour24;
  // Time string at the LEFT, fixed 4-wide ("H:MM" if hour <10, else 5-wide
  // "HH:MM"). We always draw it at x=0 so the suffix has a known anchor.
  char buf[6];
  int px = 0;
  if (hour12 >= 10) {
    buf[0] = '0' + (hour12 / 10);
    buf[1] = '0' + (hour12 % 10);
    bool colonOn = isRefresh || ((second() % 2) != 0);
    buf[2] = colonOn ? ':' : ' ';
    buf[3] = '0' + (minute / 10);
    buf[4] = '0' + (minute % 10);
    buf[5] = '\0';
    px = 0;
  } else {
    buf[0] = '0' + hour12;
    bool colonOn = isRefresh || ((second() % 2) != 0);
    buf[1] = colonOn ? ':' : ' ';
    buf[2] = '0' + (minute / 10);
    buf[3] = '0' + (minute % 10);
    buf[4] = '\0';
    px = 0;
  }
  matrix.setFont();
  matrix.setCursor(px, 0);
  matrix.print(buf);

  // Suffix: render "AM" or "PM" using TomThumb (~3 wide × 5 tall). Let the
  // GFX font path do the work — it lands the glyphs at baseline=5 nicely on
  // the matrix.
  extern const GFXfont TomThumb;
  matrix.setFont(&TomThumb);
  matrix.setCursor(matrix.width() - 8, 7);
  matrix.print(isPM ? F("PM") : F("AM"));
  matrix.setFont();

  if (eventDay) drawEventDayBorder();
}


// ── Registry ────────────────────────────────────────────────────────────────

static const ClockStyle CLOCK_STYLES[] = {
  // id  name        12h    24h    render
  {  "Classic",   true,  true,  renderClassic    },  // 0
  {  "Mega",      true,  false, renderMega12h    },  // 1
  {  "Banner",    true,  true,  renderBanner     },  // 2
  {  "Pulse",     true,  true,  renderPulse      },  // 3
  {  "Stack",     true,  false, renderStack12h   },  // 4
  {  "Frame",     true,  true,  renderFrame      },  // 5
  {  "Suffix",    true,  false, renderSuffix12h  },  // 6
  {  "Inverse",   true,  false, renderInverse12h },  // 7
  {  "Stencil",   true,  true,  renderStencil    },  // 8
  {  "Italic",    true,  false, renderItalic12h  },  // 9
  {  "Dotted",    true,  true,  renderDotted     },  // 10
};
static const int CLOCK_STYLE_COUNT =
    sizeof(CLOCK_STYLES) / sizeof(CLOCK_STYLES[0]);
