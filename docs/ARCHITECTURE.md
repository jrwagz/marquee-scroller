# WagFam CalClock — Architecture Reference

> **Audience:** Developers and LLM agents working on this codebase.
> Assumes familiarity with C++ and general embedded/Arduino concepts.
> No deep ESP8266 expertise is required to follow this document.

---

## Table of Contents

1. [What This Project Is](#what-this-project-is)
2. [Hardware](#hardware)
3. [Firmware Lineage](#firmware-lineage)
4. [Directory Layout](#directory-layout)
5. [Module Overview](#module-overview)
6. [Global State](#global-state)
7. [Configuration Persistence](#configuration-persistence)
8. [Data Flow: Weather + Calendar](#data-flow-weather--calendar)
9. [Display Subsystem](#display-subsystem)
10. [Web Server](#web-server)
11. [NTP Time Sync](#ntp-time-sync)
12. [OTA Updates](#ota-updates)
13. [Key Design Constraints](#key-design-constraints)

---

## What This Project Is

WagFam CalClock is a personal family calendar and weather clock built on a Wemos D1 Mini
(ESP8266 microcontroller) driving a 4-panel MAX7219 LED matrix display (32×8 pixels).

It does four things:

- **Shows the time** on the LED display (12h or 24h, with an optional animated event-day border).
- **Scrolls a message ticker** every N minutes: current weather data + upcoming family
  events/birthdays fetched from a private JSON endpoint.
- **Serves a web UI** on port 80 for configuration and manual data refresh.
- **Self-updates** via OTA (file upload, URL download, or auto-update from calendar JSON).

---

## Hardware

| Component | Part | Notes |
| ----------- | ------ | ------- |
| Microcontroller | Wemos D1 Mini (ESP8266) | 80/160 MHz, 4MB flash, 80KB RAM |
| Display | MAX7219 4-in-1 LED Matrix (32×8) | Daisy-chained via SPI |
| Display driver chip | Maxim MAX7219 | One per 8×8 panel; 4 panels total |

### Wiring

| Display Pin | Wemos D1 Mini Pin | SPI Function |
| ------------- | ------------------- | -------------- |
| CLK | D5 | SCK |
| CS | D6 | Chip Select (GPIO12) |
| DIN | D7 | MOSI |
| VCC | 5V | — |
| GND | GND | — |

The `pinCS` constant (`D6`) is defined in `Settings.h`.

### Memory Budget

| Region | Size | Notes |
| -------- | ------ | ------- |
| Flash | 4MB | Code + LittleFS filesystem |
| IRAM | ~32KB | Hot-path code |
| DRAM (heap+stack) | ~80KB | Stack is ~4KB; heap is the rest |
| LittleFS partition | 1MB | Stores `/conf.txt` (settings file) |

Flash is **tight**. Every `String` literal must use `F()`, `FPSTR()`, or `PROGMEM` to keep
string data out of DRAM and in flash. This is enforced throughout the codebase.

---

## Firmware Lineage

This firmware is a fork of [Qrome/marquee-scroller](https://github.com/Qrome/marquee-scroller)
(by David Payne, MIT license).

**Removed from upstream:**

- `NewsApiClient` — news headlines via NewsAPI
- `OctoPrintClient` — 3D printer status
- `PiHoleClient` — Pi-hole DNS stats
- Bitcoin price display (was removed in upstream v3.0)
- TimeZoneDB API dependency

**Added / replaced in this fork:**

- `WagFamBdayClient` — fetches family calendar events/birthdays from a private HTTPS endpoint
- Animated "event-today" dot border drawn around the clock when `WAGFAM_EVENT_TODAY = true`
- Remote config push — the calendar server can remotely update `dataSourceUrl`, `apiKey`,
  `eventToday`, and `deviceName` by embedding a `config` block in the JSON response
- OTA firmware update from URL (HTTP only) with boot-confirmation rollback
- LittleFS OTA flash via `/updatefs` with `/conf.txt` backup/restore (so the SPA bundle
  can be refreshed without losing settings)
- SPA frontend served from LittleFS at `/spa/` (Preact + Vite + signals + TypeScript)
- Compile-time firmware-domain allowlist (`WAGFAM_TRUSTED_FIRMWARE_DOMAINS`) so the
  device only auto-flashes from approved hosts
- Reworked `OpenWeatherMapClient` using raw HTTP (no external weather library)
- Reworked `timeNTP` module with explicit sync control

> **Note on web auth.** A previous iteration of this fork shipped per-device HTTP Basic
> Auth + CSRF tokens. Both were intentionally removed; the device is assumed to be on a
> trusted home network. See [`SECURITY_AUDIT.md`](SECURITY_AUDIT.md) for the historical
> findings — note that several items it lists as **FIXED** were undone by the auth
> removal and need re-evaluation under the current threat model.

---

## Directory Layout

```text
marquee-scroller/
├── marquee/                    # All firmware source
│   ├── marquee.ino             # Main sketch (setup, loop, web handlers, display)
│   ├── Settings.h              # Pin config + #include directives + compile-time defaults
│   ├── OpenWeatherMapClient.h/.cpp  # Weather fetching + JSON parsing
│   ├── WagFamBdayClient.h/.cpp      # Calendar/birthday fetching + streaming JSON parse
│   ├── SecurityHelpers.h/.cpp       # Firmware URL validation, path protection
│   ├── timeNTP.h/.cpp               # NTP time sync
│   └── timeStr.h/.cpp               # Time formatting helpers
├── scripts/                    # PlatformIO build scripts (build_version.py)
├── lib/
│   ├── arduino-Max72xxPanel/   # MAX7219 LED matrix driver (local copy, modified)
│   └── json-streaming-parser/  # Streaming JSON parser (local copy)
├── tests/
│   ├── native/                 # C++ unit tests (PlatformIO native)
│   ├── scripts/                # Python tests for build scripts
│   └── integration/            # Integration tests against live devices
├── docs/                       # This documentation
├── platformio.ini              # PlatformIO build config
├── makefile                    # Docker-based lint, test, and build targets
├── CLAUDE.md                   # LLM agent instructions
└── README.md                   # Human-facing setup guide
```

---

## Module Overview

### `marquee.ino` — The Main Sketch

The largest file (~1800 lines). Owns:

- All **global mutable state** (settings variables, display state, timing counters)
- **`setup()`** — hardware init, WiFi, web server, OTA, first time sync
- **`loop()`** — per-frame display update (the async web server runs in TCP callbacks)
- **`processEverySecond()`** / **`processEveryMinute()`** — timed data refresh and scroll
- **`getWeatherData()`** — orchestrates weather + calendar fetch + NTP sync
- All **REST API handlers** (`handleApi*` functions backing `/api/*` endpoints) plus the
  legacy-path redirects to `/spa/` (`redirectToSpa`) and the SPA-aware 404 fallback
  (`handleNotFound`)
- The OTA paths: `/update` (file upload), `/updateFromUrl` (URL fetch + flash), and
  `/updatefs` (LittleFS image upload with `/conf.txt` backup/restore)
- **`savePersistentConfig()`** / **`readPersistentConfig()`** — LittleFS I/O
- **`scrollMessageWait()`** / **`centerPrint()`** — display rendering

### `Settings.h` — Pin Config + Includes

Pulls in all library headers and declares compile-time constants. Also defines the **default**
values for settings (only used on a completely fresh device with no `/conf.txt`). Changing
values here requires a filesystem erase to take effect.

### `OpenWeatherMapClient` — Weather Data

Fetches current weather from the OpenWeatherMap free API using plain HTTP (port 80).
Uses `ArduinoJson v7` (`JsonDocument`) for parsing. Also derives the local timezone offset
from the weather API response, which is used to set the NTP timezone.

Location can be set as city ID, `lat,lon`, or city name.

**Returns:** temperature, humidity, wind, pressure, conditions, high/low, timezone offset,
sunrise/sunset.

### `WagFamBdayClient` — Family Calendar

Fetches a JSON array from a private HTTPS endpoint using `BearSSL::WiFiClientSecure` with
`setInsecure()` (no certificate verification — intentional for embedded use).

Uses the local **streaming JSON parser** (`JsonStreamingParser`) rather than `ArduinoJson`
because the payload can be arbitrarily large and the streaming parser avoids allocating the
whole document in RAM.

Stores up to 10 display messages. Also parses an optional `config` block that can push
remote config updates back to the device.

### `SecurityHelpers.h/.cpp` — Security Utilities

Extracted from `marquee.ino` to enable unit testing. Provides:

- `isProtectedPath(path)` — prevents API writes/deletes to `/conf.txt` and `/ota_pending.txt`
- `extractDomain(url)` — parses domain from URLs (handles scheme-less, userinfo, port, query)
- `isTrustedFirmwareDomain(firmwareUrl, calendarUrl)` — validates OTA firmware URLs against
  a compile-time domain allowlist (`WAGFAM_TRUSTED_FIRMWARE_DOMAINS` in `Settings.h`) plus the
  calendar source domain
- `isInTrustedDomainList(domain, list)` — checks domain membership in comma-separated allowlist

### `timeNTP.h/.cpp` — Time Sync

Sends a UDP NTP request to `1.pool.ntp.org` and returns Unix time adjusted for the local
timezone (set by `set_timeZoneSec()`). Wraps the `TimeLib` library.

Key subtlety: the timezone offset comes from OpenWeatherMap, not from the device config.
This means DST is handled automatically — the OWM API returns the current UTC offset
including DST.

### `timeStr.h/.cpp` — Formatting Helpers

Stateless utility functions: `getDayName()`, `getMonthName()`, `zeroPad()`, `spacePad()`,
`get24HrColonMin()`, `getAmPm()`. Day/month name strings are stored in `PROGMEM`.

### `lib/arduino-Max72xxPanel` — LED Driver

A local, slightly modified copy of the `markruys/arduino-Max72xxPanel` library. Extends
`Adafruit_GFX` so you can call `drawChar()`, `drawPixel()`, `print()` etc. on the panel
as if it were a canvas. The `write()` method flushes the framebuffer to the hardware via SPI.

### `lib/json-streaming-parser` — Streaming JSON

A character-at-a-time JSON parser by Daniel Eichhorn (squix78). Used by `WagFamBdayClient`
to parse the calendar JSON without holding the full payload in RAM.

---

## Global State

All runtime state lives as global variables in `marquee.ino`. The most important ones:

| Variable | Type | Purpose |
| ---------- | ------ | --------- |
| `APIKEY` | `String` | OpenWeatherMap API key |
| `WAGFAM_DATA_URL` | `String` | Calendar JSON endpoint URL |
| `WAGFAM_API_KEY` | `String` | Bearer token for calendar endpoint |
| `WAGFAM_EVENT_TODAY` | `boolean` | Drives the animated event-day border |
| `DEVICE_NAME` | `String` | Human-friendly name assigned by server |
| `geoLocation` | `String` | Weather location (city ID, lat/lon, or name) |
| `IS_METRIC` | `boolean` | Unit system toggle |
| `IS_24HOUR` | `boolean` | 12h vs 24h clock |
| `IS_PM` | `boolean` | Show PM indicator |
| `minutesBetweenDataRefresh` | `int` | How often to hit weather + calendar APIs |
| `minutesBetweenScrolling` | `int` | How often to scroll the message ticker |
| `displayScrollSpeed` | `int` | Milliseconds per pixel scroll step |
| `displayIntensity` | `int` | LED brightness (0–15) |
| `SHOW_*` | `boolean` | Per-field weather display toggles |
| `displayTime` | `String` | Current time string, rebuilt every frame |
| `lastRefreshDataTimestamp` | `uint32_t` | Unix time of last successful data fetch |
| `firstTimeSync` | `uint32_t` | Unix time of first successful NTP sync |
| `displayRefreshCount` | `int` | Countdown to next scroll; reloaded from config |
| `bdayMessageIndex` | `int` | Current position in the calendar message rotation |
| `weatherClient` | `OpenWeatherMapClient` | Singleton weather client |
| `bdayClient` | `WagFamBdayClient` | Singleton calendar client |
| `matrix` | `Max72xxPanel` | Singleton display driver |
| `server` | `AsyncWebServer` | HTTP server on port 80 (ESPAsyncWebServer-esphome) |
| `dnsServer` | `DNSServer` | Captive-portal DNS used by `ESPAsyncWiFiManager` during AP mode |

**Important:** There is no configuration struct. All settings are individual globals.
When settings are saved or loaded, each one is written/read individually by
`savePersistentConfig()` / `readPersistentConfig()`.

---

## Configuration Persistence

Settings are stored in a text file `/conf.txt` on the LittleFS filesystem (aliased as
`SPIFFS` in the sketch for backward compatibility).

Format: one `key=value` pair per line, terminated with `\n`.

```ini
WAGFAM_DATA_URL=https://example.com/family.json
WAGFAM_API_KEY=ghp_xxxxx
WAGFAM_EVENT_TODAY=0
APIKEY=abc123openweather
CityID=Chicago,US
ledIntensity=4
scrollSpeed=25
is24hour=0
isPM=1
isMetric=0
refreshRate=15
minutesBetweenScrolling=1
SHOW_CITY=1
SHOW_CONDITION=0
SHOW_HUMIDITY=0
SHOW_WIND=0
SHOW_PRESSURE=0
SHOW_HIGHLOW=0
SHOW_DATE=0
```

**Reading:** `readPersistentConfig()` reads line-by-line and uses `line.indexOf("KEY=")` to
identify each setting. After reading, it calls `matrix.setIntensity()`, `weatherClient.set*()`,
and `bdayClient.updateBdayClient()` to apply the values.

**Writing:** `savePersistentConfig()` overwrites the file completely (no partial updates),
then calls `readPersistentConfig()` to re-apply the new values.

**`WAGFAM_EVENT_TODAY`** and **`DEVICE_NAME`** are special: they are **never set from the
web form**. They are only set by the calendar server's `config.eventToday` and
`config.deviceName` fields, then persisted so they survive reboots.

---

## Data Flow: Weather + Calendar

Both data sources are fetched in a single function: `getWeatherData()`. This function is
called when:

- `getMinutesFromLastRefresh() >= minutesBetweenDataRefresh` (normal periodic refresh)
- `lastRefreshDataTimestamp == 0` (first run)
- The user hits `/pull` in the web UI

```text
getWeatherData()
├── weatherClient.updateWeather()          → HTTP GET to api.openweathermap.org
│   └── Parses JSON, stores weather struct
│   └── Returns timezone offset
├── set_timeZoneSec(timezone)              → Updates NTP offset
├── getNtpTime()                           → UDP to 1.pool.ntp.org
│   └── Returns adjusted Unix time
├── setTime(t)                             → Updates TimeLib clock
├── bdayClient.updateData(devInfo)          → HTTPS GET to WAGFAM_DATA_URL
│   └── Appends ?chip_id=&version=&uptime=&heap=&rssi=&utc_offset_sec= (heartbeat)
│   └── Streams JSON through parser
│   └── Fills messages[0..9]
│   └── Returns configValues (remote config)
└── If config received:
    └── Update WAGFAM_DATA_URL / WAGFAM_API_KEY / WAGFAM_EVENT_TODAY / DEVICE_NAME
    └── savePersistentConfig()             → Write /conf.txt
```

**Scroll message assembly** (in `processEveryMinute()`):

1. Start with a space character
2. Optionally append: date, city + temperature, high/low, condition, humidity, wind, pressure
3. Always append: `bdayClient.getMessage(bdayMessageIndex)` (one calendar message per scroll)
4. Increment `bdayMessageIndex`; wrap to 0 when all messages have been shown

---

## Display Subsystem

The display is a 32×8 pixel canvas (4 panels × 8×8 each). `Max72xxPanel` presents it
as an `Adafruit_GFX` canvas, so standard GFX drawing primitives work.

### Frame Loop

Every iteration of `loop()`:

1. `displayTime = hourMinutes(false)` — rebuilds the time string
2. `matrix.fillScreen(LOW)` — clears the framebuffer
3. `centerPrint(displayTime, true)` — draws time + optional extras to framebuffer
4. Inside `centerPrint` when `extraStuff=true`:
   - If `WAGFAM_EVENT_TODAY`: draw animated dot border (marching dots around the perimeter)
   - If 12h mode + IS_PM + current time is PM: draw a single pixel at `(width-1, 6)` as
     the PM indicator
5. `matrix.write()` — SPI flush to hardware (happens inside `centerPrint`)

The loop runs as fast as the MCU can execute it (thousands of times/second).

### Scrolling

`scrollMessageWait(msg)` scrolls text from right to left, one pixel at a time.

- Each step: `matrix.fillScreen(LOW)`, draw visible characters, `matrix.write()`, `delay(displayScrollSpeed)`
- `displayScrollSpeed` is in milliseconds per pixel step (25ms = normal, 15ms = fast)
- The web server runs in the background (`AsyncWebServer` with TCP callbacks); `delay()`
  yields to the system so async TCP makes progress during the scroll without an explicit
  service call

### Animated Event Border

When `WAGFAM_EVENT_TODAY == true`, `centerPrint()` draws a marching-dots pattern along
the three visible perimeter edges of the display (left column, bottom row, right column).

The animation is driven by `millis()`, not by the frame counter, so the speed is consistent
regardless of loop speed:

```text
dotPosition = (millis() % (SPACING * SPEED_MS)) / SPEED_MS
```

Dots travel: left edge top→bottom, bottom edge left→right, right edge bottom→top.

### Font

The built-in Adafruit GFX 5×7 font is used. Each character is 5 pixels wide + 1 pixel
spacer = 6 pixels total. This is the `width` variable. `scrollMessageWait` uses `width` to
compute how many scroll steps are needed for a given message.

---

## Web Server

Routes served by `AsyncWebServer` on port 80 (ESPAsyncWebServer-esphome). The captive
portal during AP mode is served by `ESPAsyncWiFiManager` sharing the same instance.

| Route | Handler | Description |
| ------- | --------- | ------------- |
| `/spa/...` | `serveStatic` (LittleFS) | Preact SPA bundle — Home / Status / Settings / Actions tabs |
| `/`, `/configure`, `/systemreset`, `/forgetwifi`, `/saveconfig` | `redirectToSpa` | 302 → `/spa/` (legacy paths preserved as redirects) |
| `/pull` | inline lambda | Sets `weatherRefreshRequested` then 302 → `/spa/` |
| `/update` | inline `setup()` lambda | Firmware upload (POST + file body, manually wired since `ESP8266HTTPUpdateServer` is sync-only) |
| `/updateFromUrl` | `handleUpdateFromUrl` | Firmware update from HTTP URL |
| `/updatefs` | inline `setup()` lambda | LittleFS image upload — refreshes the SPA bundle without serial flash; `/conf.txt` is backed up and restored across the flash |
| `*` | `handleNotFound` | If `/spa/index.html` exists on LittleFS, redirect to `/spa/index.html` so the SPA router handles client-side routes; otherwise render a "SPA bundle not installed" page that links `/updatefs` |

### REST API (`/api/*`)

See [README.md — REST API](../README.md#rest-api) for the full endpoint table and curl examples.

Handler functions follow the naming convention `handleApi<Resource>[Action]`
(e.g., `handleApiConfigGet`, `handleApiFsWrite`). They are registered in `setup()` alongside
the page routes. The API enables automated testing of config operations, OTA rollback
workflows, and filesystem state inspection without a browser. JSON-body POST endpoints
(`/api/config`, `/api/fs/write`, `/api/spa/update-from-url`) are wired through
`AsyncCallbackJsonWebHandler` because `AsyncWebServer` does not populate
`request->arg("plain")`.

The SPA is built with Vite + Preact + signals + TypeScript and deployed to LittleFS
under `/spa/`. The bundle is shipped both raw and gzipped; `serveStatic` returns the
gzipped sibling when the client advertises `Accept-Encoding: gzip`. There are no CDN
dependencies — the legacy W3.CSS + Font Awesome links were removed in Phase D (PR #80)
along with the server-side HTML rendering.

**Security:** All web and REST API routes are currently open (no authentication). The
device is assumed to be on a trusted home network. See `docs/SECURITY_AUDIT.md` for
the historical audit — note that several items it lists as **FIXED** were undone by
the auth removal and need re-evaluation under the current threat model. Compile-time
firmware-domain allowlisting (`WAGFAM_TRUSTED_FIRMWARE_DOMAINS`) and protected-path
checks for `/api/fs/write` + `/api/fs/delete` are still in effect.

---

## NTP Time Sync

NTP sync is handled by `TimeLib`'s `setSyncProvider` + `setSyncInterval` mechanism,
but the code also performs **explicit** NTP calls during `getWeatherData()`.

Timeline:

1. `timeNTPsetup()` — opens UDP socket on port 8888, sets `getSyncInterval(20)` (every
   20 seconds until first sync)
2. First `getWeatherData()` call — gets timezone from OWM, calls `set_timeZoneSec()`,
   which updates `timeZoneSec` in `timeNTP.cpp` and resets the sync provider
3. Then `getNtpTime()` is called explicitly; if valid (>2025-01-01), `setTime(t)` is called
4. `firstTimeSync` is set; `setSyncInterval(222)` seconds going forward

The timezone offset from OWM is in seconds and includes DST, so the displayed time
adjusts for DST automatically after each weather refresh.

---

## OTA Updates

ArduinoOTA was removed in v3.08.0-wagfam (see `docs/OTA_STRATEGY.md`). Three update paths
remain:

1. **HTTP upload** at `/update` — upload a sketch `.bin` via browser form. Implemented
   inline in `setup()` as an `AsyncWebServer` POST handler with an upload-chunk callback
   that drives `Update.begin/write/end` (with `Update.runAsync(true)`). No
   authentication is enforced.

2. **HTTP URL update** (`handleUpdateFromUrl`) at `/updateFromUrl` — device downloads
   and flashes a `.bin` from a given HTTP URL. **HTTPS is not supported** for this path
   (limitation of `ESP8266httpUpdate`). Firmware URLs are validated against a trusted
   domain allowlist (see `SecurityHelpers`).

3. **Auto-update** (calendar JSON) — when `latestVersion` in the server config differs
   from the compiled `VERSION`, `performAutoUpdate()` triggers an OTA flash with
   boot-confirmation rollback. See `docs/OTA_STRATEGY.md` for the full rollback
   architecture.

4. **LittleFS update** at `/updatefs` — upload a LittleFS image to refresh the SPA
   bundle without a serial cable. Before the flash, `/conf.txt` is backed up; after
   the device reboots into the new filesystem, the backup is restored so settings
   survive the SPA refresh. There is also a JSON-body `POST /api/spa/update-from-url`
   variant that fetches the image from a URL.

---

## Key Design Constraints

| Constraint | Impact |
| ------------ | -------- |
| ~80KB heap | All `String` allocations are at a premium; avoid heap fragmentation |
| 4KB stack | Avoid large local arrays, especially of `String` objects |
| Single-threaded | `delay()` in scroll loop must yield to server/OTA via explicit calls |
| No HTTPS for OTA URL | Firmware download only works over HTTP |
| BearSSL `setInsecure()` | Calendar HTTPS works but doesn't verify server certificate |
| LittleFS aliased as SPIFFS | `#include "FS.h"` + `SPIFFS.begin()` — PlatformIO handles translation |
| ArduinoJson v7 | Use `JsonDocument`, never `DynamicJsonDocument`/`StaticJsonDocument` |
| F() / PROGMEM required | All string literals ≥ ~10 chars should use `F()` or `FPSTR()` |
