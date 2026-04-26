# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@README.md

---

## Quick Start for LLM Agents

Before making changes, read these docs in order:

1. `docs/ARCHITECTURE.md` — what every module does, hardware context, global state map
2. `docs/CODE_FLOW.md` — how execution flows from boot through normal operation
3. `docs/CODE_REVIEW.md` — known bugs and issues (check here before touching any file)

---

## Architecture

All source lives in [marquee/](marquee/):

| File | Role |
| --- | --- |
| [marquee.ino](marquee/marquee.ino) | Main sketch: `setup()`, `loop()`, web server handlers, display rendering |
| [Settings.h](marquee/Settings.h) | Hardware pin config + all `#include` directives; default values for first-run only |
| [OpenWeatherMapClient.h/.cpp](marquee/OpenWeatherMapClient.h) | Fetches weather from OpenWeatherMap API using ArduinoJson |
| [WagFamBdayClient.h/.cpp](marquee/WagFamBdayClient.h) | Fetches family calendar JSON over HTTPS; parses messages and remote config |
| [timeNTP.h/.cpp](marquee/timeNTP.h) | NTP time sync; exposes `timeNTPsetup()`, `getNtpTime()`, and `set_timeZoneSec()` |
| [timeStr.h/.cpp](marquee/timeStr.h) | Time formatting helpers (zero-pad, day/month names, etc.) |

Local library copies (not managed by PlatformIO) are in [lib/](lib/):

- `arduino-Max72xxPanel` — MAX7219 LED matrix driver
- `json-streaming-parser` — streaming JSON parser used by `WagFamBdayClient`

## Main Loop Logic

- **Every frame**: When dirty or event-day border is active, clear display and call `centerPrint(displayTime, true)`;
  service `server.handleClient()`
- **Every second** (`processEverySecond`): Fires OTA confirmation check; calls `getWeatherData()` if
  `minutesBetweenDataRefresh` has elapsed
- **Every minute** (`processEveryMinute`): Scroll the marquee message (weather data + next calendar message from
  `bdayClient`). Controlled by `displayRefreshCount` countdown

### Key Line Numbers in `marquee.ino`

| Function | Line |
| --- | --- |
| Global variables (settings) | 60–98 |
| PROGMEM HTML constants | 104–148 |
| `setup()` | 154 |
| `loop()` | 268 |
| `processEverySecond()` | 291 |
| `processEveryMinute()` | 298 |
| `handleSaveConfig()` | 378 |
| `handleConfigure()` | 423 |
| `handleUpdateFromUrl()` | 530 |
| `getWeatherData()` | 603 |
| `sendHeader()` / `sendFooter()` | 697 / 727 |
| `displayHomePage()` | 741 |
| `savePersistentConfig()` | 877 |
| `readPersistentConfig()` | 908 |
| `scrollMessageWait()` | 1011 |
| `centerPrint()` | 1038 |

## Configuration Storage

Runtime config is persisted via LittleFS (aliased as `SPIFFS` in the sketch) at `/conf.txt` as `key=value` pairs.
The functions `savePersistentConfig()` and `readPersistentConfig()` in [marquee.ino](marquee/marquee.ino)
own all reads and writes to this file. [Settings.h](marquee/Settings.h) contains compile-time defaults only —
changes there require a filesystem erase to take effect.

`WAGFAM_EVENT_TODAY` is not user-configurable via the web form — it is set exclusively by the server's
`config.eventToday` field in the calendar JSON response and persisted across reboots via `/conf.txt`.

### Adding a New Config Key

1. Declare a global variable in `marquee.ino` (near line 60)
2. Add `f.println("KEY=" + String(value))` in `savePersistentConfig()` (~line 877)
3. Add an `if (line.indexOf("KEY=") >= 0)` block in `readPersistentConfig()` (~line 908)
4. Add a form field in one of the `CHANGE_FORM*` PROGMEM constants (~line 113)
5. Read from `server.arg("fieldName")` in `handleSaveConfig()` (~line 378)

## Markdown Style

`make lint` enforces markdownlint on all `.md` files. Rules in effect (`.markdownlint.yaml`):

- **MD013**: max **120 chars** per line for body text and list items; max **100 chars** for headings
  - Table rows and fenced code block content are **exempt**
  - When a line would exceed the limit, wrap at a natural word/clause boundary
  - List item continuation lines must be indented **2 spaces** to stay in the same list item
- MD033 (inline HTML), MD024 (duplicate headings), MD041 (first-line h1) are **disabled**

Run `make lint-markdown` locally to check before committing any `.md` changes.

## Display Subsystem

The display is a 32×8 pixel grid (4 panels of 8×8 each). Key facts:

- Font: 5px wide + 1px spacer = 6px per character (`width` variable, line 70)
- `scrollMessageWait(msg)` scrolls right-to-left, one pixel per step, at `displayScrollSpeed` ms/step
- `centerPrint(msg, extraStuff)` draws a static centered string + optional extras (event border, PM dot)
- `matrix.write()` flushes the framebuffer to hardware via SPI — called inside `centerPrint` and each
  scroll step
- The animated event-day border is drawn in `centerPrint()` when `WAGFAM_EVENT_TODAY == true`

## WagFamBdayClient — Calendar Integration

`WagFamBdayClient` replaced the `NewsApiClient` from the upstream Qrome fork.
It fetches a JSON array from `WAGFAM_DATA_URL` over HTTPS and stores up to 10 `messages[]`.

Expected JSON format:

```json
[
  {
    "config": {
      "eventToday": "1",
      "dataSourceUrl": "...",
      "apiKey": "...",
      "latestVersion": "3.08.0-wagfam",
      "firmwareUrl": "http://example.com/firmware.bin"
    }
  },
  { "message": "Justin birthday - tomorrow" },
  { "message": "Family dinner - this Saturday" }
]
```

- The `config` object is optional and can contain any subset of the fields
- Messages are displayed one per scroll cycle, cycling through `bdayMessageIndex`
- `cleanText()` translates Unicode lookalikes to ASCII for the LED font (35+ `replace()` calls)
- `latestVersion` + `firmwareUrl` trigger an auto-update if version differs from `VERSION` macro;
  see `docs/OTA_STRATEGY.md` for full rollback architecture

## OTA Update Architecture

ArduinoOTA was removed in v3.08.0-wagfam. Updates are now delivered three ways:

| Method | Trigger | Rollback |
| --- | --- | --- |
| Web upload (`/update`) | Manual via browser | No (use `/updateFromUrl` to revert) |
| URL update (`/updateFromUrl`) | Manual via web form | Boot-confirmation rollback |
| Auto-update (calendar JSON) | `latestVersion` != `VERSION` | Boot-confirmation rollback |

**Boot-confirmation rollback:** Before every flash, a `/ota_pending.txt` record is written to LittleFS
with the current safe URL. If the device reboots twice without confirming (5 min stable uptime),
`checkOtaRollback()` re-flashes the previous firmware. See `docs/OTA_STRATEGY.md` for full details.

## Key Constraints

- Flash memory is tight on ESP8266 — avoid large string literals on the stack;
  use the `F()` macro or `PROGMEM` / `FPSTR()` for string constants
  (see existing `CHANGE_FORM*` and `WEB_ACTIONS*` constants in [marquee.ino](marquee/marquee.ino))
- The LED display font is 5px wide + 1px spacer; `scrollMessageWait()` computes scroll distance from message length
- `WagFamBdayClient` uses BearSSL with `setInsecure()` (no cert validation) — intentional for embedded use
- ArduinoJson v7 is used (`^7.4` in `platformio.ini`) — do not generate v6-style
  `DynamicJsonDocument` / `StaticJsonDocument` code; use `JsonDocument` instead
- All `String` operations are expensive — prefer `reserve()` before building strings, avoid repeated `+=`
  in tight loops, and never allocate large `String` arrays on the stack
- `scrollMessageWait()` is blocking but calls `server.handleClient()` each pixel step — web requests
  during scrolling are handled mid-scroll
- `getWeatherData()` is the single orchestration point for both weather AND calendar data refresh — they
  always refresh together; it also triggers auto-OTA if `latestVersion` differs from `VERSION`
- `firmwareUrl` in the calendar config JSON must use `http://` — HTTPS is not supported by ESPhttpUpdate

## Known Bugs (see `docs/CODE_REVIEW.md` for full details)

All previously identified bugs have been resolved. Remaining open items from `docs/CODE_REVIEW.md`:

- HTML `+=` string accumulation in `sendHeader()` / `displayHomePage()` / `handleConfigure()` — not a bug,
  but a memory optimization opportunity (replace static HTML fragments with direct `sendContent(F(...))` calls)

## What Was Removed from Upstream (Qrome/marquee-scroller)

These modules existed in the upstream repo and were deleted in this fork:

- `NewsApiClient` — news headline fetching (this is what `WagFamBdayClient` replaced)
- `OctoPrintClient` — 3D printer status
- `PiHoleClient` — Pi-hole DNS blocker stats
- Bitcoin price display (was already removed in upstream v3.0)
- TimeZoneDB API calls (timezone now derived from OWM response)

`sources.json` was a leftover from the upstream news feature and has been deleted.
