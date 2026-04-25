# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@README.md

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

- **Every frame**: Render current time via `centerPrint()`; handle web server and OTA requests
- **Every second** (`processEverySecond`): Calls `getWeatherData()` — which fetches both weather and calendar data together — if `minutesBetweenDataRefresh` has elapsed
- **Every minute** (`processEveryMinute`): Scroll the marquee message (weather data + next calendar message from `bdayClient`)

## Configuration Storage

Runtime config is persisted via LittleFS (aliased as `SPIFFS` in the sketch) at `/conf.txt` as `key=value` pairs. The functions `savePersistentConfig()` and `readPersistentConfig()` in [marquee.ino](marquee/marquee.ino) own all reads and writes to this file. [Settings.h](marquee/Settings.h) contains compile-time defaults only — changes there require a filesystem erase to take effect.

`WAGFAM_EVENT_TODAY` is not user-configurable via the web form — it is set exclusively by the server's `config.eventToday` field in the calendar JSON response and persisted across reboots via `/conf.txt`.

## Key Constraints

- Flash memory is tight on ESP8266 — avoid large string literals on the stack; use the `F()` macro or `PROGMEM` / `FPSTR()` for string constants (see existing `CHANGE_FORM*` and `WEB_ACTIONS*` constants in [marquee.ino](marquee/marquee.ino))
- The LED display font is 5px wide + 1px spacer; `scrollMessageWait()` computes scroll distance from message length
- `WagFamBdayClient` uses BearSSL with `setInsecure()` (no cert validation) — intentional for embedded use
- ArduinoJson v7 is used (`^7.4` in `platformio.ini`) — do not generate v6-style `DynamicJsonDocument` / `StaticJsonDocument` code; use `JsonDocument` instead
