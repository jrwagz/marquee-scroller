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
- **Self-updates** via OTA (Arduino OTA or file/URL upload through the web UI).

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
  and `eventToday` by embedding a `config` block in the JSON response
- OTA firmware update from URL (HTTP only)
- Reworked `OpenWeatherMapClient` using raw HTTP (no external weather library)
- Reworked `timeNTP` module with explicit sync control

---

## Directory Layout

```text
marquee-scroller/
├── marquee/                    # All firmware source
│   ├── marquee.ino             # Main sketch (setup, loop, web handlers, display)
│   ├── Settings.h              # Pin config + #include directives + compile-time defaults
│   ├── OpenWeatherMapClient.h/.cpp  # Weather fetching + JSON parsing
│   ├── WagFamBdayClient.h/.cpp      # Calendar/birthday fetching + streaming JSON parse
│   ├── timeNTP.h/.cpp               # NTP time sync
│   └── timeStr.h/.cpp               # Time formatting helpers
├── lib/
│   ├── arduino-Max72xxPanel/   # MAX7219 LED matrix driver (local copy, modified)
│   └── json-streaming-parser/  # Streaming JSON parser (local copy)
├── docs/                       # This documentation
├── platformio.ini              # PlatformIO build config
├── CLAUDE.md                   # LLM agent instructions
└── README.md                   # Human-facing setup guide
```

---

## Module Overview

### `marquee.ino` — The Main Sketch

The largest file (~1100 lines). Owns:

- All **global mutable state** (settings variables, display state, timing counters)
- **`setup()`** — hardware init, WiFi, web server, OTA, first time sync
- **`loop()`** — per-frame display update + web/OTA service
- **`processEverySecond()`** / **`processEveryMinute()`** — timed data refresh and scroll
- **`getWeatherData()`** — orchestrates weather + calendar fetch + NTP sync
- All **web request handlers** (`handleConfigure`, `handleSaveConfig`, `handlePull`, etc.)
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
| `server` | `ESP8266WebServer` | HTTP server on port 80 |
| `serverUpdater` | `ESP8266HTTPUpdateServer` | Firmware upload handler |

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

**`WAGFAM_EVENT_TODAY`** is special: it is **never set from the web form**. It is only set
by the `config.eventToday` field returned by the calendar server, then persisted so it
survives reboots.

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
├── bdayClient.updateData()                → HTTPS GET to WAGFAM_DATA_URL
│   └── Streams JSON through parser
│   └── Fills messages[0..9]
│   └── Returns configValues (remote config)
└── If config received:
    └── Update WAGFAM_DATA_URL / WAGFAM_API_KEY / WAGFAM_EVENT_TODAY
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
- `server.handleClient()` and `ArduinoOTA.handle()` are called every step so the web
  server and OTA remain responsive during scrolling

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

Routes served by `ESP8266WebServer` on port 80:

| Route | Handler | Description |
| ------- | --------- | ------------- |
| `/` | `displayHomePage` | Shows events, weather, version |
| `/configure` | `handleConfigure` | Renders config form |
| `/saveconfig` | `handleSaveConfig` | Saves form data, triggers refresh |
| `/pull` | `handlePull` | Forces immediate data refresh |
| `/update` | `serverUpdater` | Firmware upload (file, via ESP8266HTTPUpdateServer) |
| `/updateFromUrl` | `handleUpdateFromUrl` | Firmware update from HTTP URL |
| `/systemreset` | `handleSystemReset` | Deletes /conf.txt and reboots |
| `/forgetwifi` | `handleForgetWifi` | Clears WiFi credentials and reboots |
| `*` | `redirectHome` | Catch-all redirect to `/` |

The web UI uses W3.CSS (loaded from CDN) and Font Awesome 5.8 icons (from cdnjs CDN).
Both require internet access to display correctly.

HTML is generated by string concatenation and sent in chunks using
`server.setContentLength(CONTENT_LENGTH_UNKNOWN)` + `server.sendContent()`.
The `CHANGE_FORM*` and `WEB_ACTIONS*` constants are stored in `PROGMEM` to save DRAM.

**Security note:** The configure form uses `method='get'`, which puts API keys in the URL
and server access logs. There is no CSRF protection. This is intentional for a simple
home device with no public exposure.

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

Two OTA mechanisms coexist:

1. **Arduino OTA** (`ArduinoOTA`) — IDE-initiated over-the-air upload on the LAN.
   Always enabled, no password. Hostname: `CLOCK-{chip-id-hex}`.

2. **HTTP upload** (`ESP8266HTTPUpdateServer`) at `/update` — upload a `.bin` file
   via browser form.

3. **HTTP URL update** (`handleUpdateFromUrl`) at `/updateFromUrl` — device downloads
   and flashes a `.bin` from a given HTTP URL. **HTTPS is not supported** for this path
   (limitation of `ESP8266httpUpdate`).

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
