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

All firmware source lives in [marquee/](marquee/):

| File | Role |
| --- | --- |
| [marquee.ino](marquee/marquee.ino) | Main sketch: `setup()`, `loop()`, web server handlers, display rendering |
| [Settings.h](marquee/Settings.h) | Hardware pin config + all `#include` directives; default values for first-run only |
| [OpenWeatherMapClient.h/.cpp](marquee/OpenWeatherMapClient.h) | Fetches weather from OpenWeatherMap API using ArduinoJson |
| [WagFamBdayClient.h/.cpp](marquee/WagFamBdayClient.h) | Fetches family calendar JSON over HTTPS; parses messages and remote config |
| [SecurityHelpers.h/.cpp](marquee/SecurityHelpers.h) | Firmware URL validation, path protection, domain extraction |
| [timeNTP.h/.cpp](marquee/timeNTP.h) | NTP time sync; exposes `timeNTPsetup()`, `getNtpTime()`, and `set_timeZoneSec()` |
| [timeStr.h/.cpp](marquee/timeStr.h) | Time formatting helpers (zero-pad, day/month names, etc.) |

The backend server lives in [server/](server/) — see [Backend Server](#backend-server) below.

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
| Global variables (settings) | 95–189 |
| PROGMEM HTML constants | 133–179 |
| `setup()` | 194 |
| `loop()` | 327 |
| `processEverySecond()` | 353 |
| `processEveryMinute()` | 370 |
| `handleSaveConfig()` | 450 |
| `handleConfigure()` | 506 |
| `doOtaFlash()` | 619 |
| `handleUpdateFromUrl()` | 635 |
| `performAutoUpdate()` | 690 |
| `checkOtaRollback()` | 706 |
| `getWeatherData()` | 760 |
| `sendHeader()` / `sendFooter()` | 886 / 916 |
| `displayHomePage()` | 930 |
| `savePersistentConfig()` | 1067 |
| `readPersistentConfig()` | 1108 |
| `scrollMessageWait()` | 1202 |
| `centerPrint()` | 1226 |
| Security helpers (`requireWebAuth`, etc.) | 1266 |
| REST API handlers | 1275–1546 |

## Configuration Storage

Runtime config is persisted via LittleFS (aliased as `SPIFFS` in the sketch) at `/conf.txt` as `key=value` pairs.
The functions `savePersistentConfig()` and `readPersistentConfig()` in [marquee.ino](marquee/marquee.ino)
own all reads and writes to this file. [Settings.h](marquee/Settings.h) contains compile-time defaults only —
changes there require a filesystem erase to take effect.

`WAGFAM_EVENT_TODAY` and `DEVICE_NAME` are not user-configurable via the web form — they are set
exclusively by the server's `config.eventToday` and `config.deviceName` fields in the calendar
JSON response and persisted across reboots via `/conf.txt`.

### Adding a New Config Key

1. Declare a global variable in `marquee.ino` (near line 95)
2. Add `f.println("KEY=" + String(value))` in `savePersistentConfig()` (~line 1067)
3. Add an `if (line.indexOf("KEY=") >= 0)` block in `readPersistentConfig()` (~line 1108)
4. Add a form field in one of the `CHANGE_FORM*` PROGMEM constants (~line 142)
5. Read from `server.arg("fieldName")` in `handleSaveConfig()` (~line 450)

## Development Practices

Lessons from code review — these are non-obvious enough to state explicitly:

**Never suppress a lint rule to make CI pass.** Adding a new `RuleXX: false` to
`.markdownlint.yaml` is always wrong. Find the line that violates the rule and fix it.
The only disabled rules in this repo are those with deliberate permanent policy reasons
(MD033, MD024, MD041) — they were disabled intentionally, not to unblock a failure.

**Don't duplicate logic when a shared helper exists or is obvious.**
When writing a second code path that does the same core operation as an existing one,
extract a shared function. The `doOtaFlash()` / `handleUpdateFromUrl()` /
`performAutoUpdate()` split is the canonical example: all three callers differ in how
they present to the user, but the flash core (write rollback record → call ESPhttpUpdate
→ clean up on failure) is identical and belongs in one place.

**Write unit tests for new parsing logic.** `tests/native/test_wagfam_parser/` already
tests `WagFamBdayClient`'s parsing. If you add a new field to the `configValues` struct
or change the `value()` callback, add corresponding tests in the same commit. The rule
of thumb: any new branch in a `JsonListener` callback needs a test.

**Keep test stubs in sync with production code.** When adding a new method call to
production code that has stubs in `tests/native/stubs/`, update the stub in the same
change. If production code calls `client->setBufferSizes(2048, 512)`, the
`WiFiClientSecureBearSSL.h` stub needs `void setBufferSizes(int, int) {}`.

**Update docs in the same commit as the code change.** If a change makes a statement
in `CLAUDE.md` or `docs/` inaccurate, fix the doc in the same commit — not later.
Specifically: when a bug in `docs/CODE_REVIEW.md` is fixed, remove it from that file.

**Back assertions about external services with evidence.** Any claim about how a
third-party service behaves (e.g. "GitHub raw ignores query params", "ESPhttpUpdate
follows redirects", "BearSSL accepts self-signed certs with `setInsecure()`") must
be backed by either a one-time empirical capture (logged in the PR/commit that
adds the claim) or a citation to that service's official documentation. Internal-
knowledge claims rot silently when the upstream changes; written-down evidence
does not. **Don't keep network-dependent regression tests in CI for these claims —
they flake.** For example, the heartbeat params claim ("GitHub raw ignores
`chip_id`, `version`, `uptime`, `heap`, `rssi` and DOES interpret `?token=`") was
verified empirically during PR #25 review (see commit `bca26e6` and the
[review thread](https://github.com/jrwagz/marquee-scroller/pull/25#discussion_r3144204226)
for the captured sha256s and HTTP codes); the matching `test_github_raw_compat.py`
was removed in a follow-up because raw.githubusercontent.com's CDN-cache shards
returned racy results when the target file was being updated. Re-verify
out-of-band if you change the heartbeat parameter set or move to a different
static-JSON host — don't add it back to the suite.

---

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

- Font: 5px wide + 1px spacer = 6px per character (`width` variable, line 97)
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
      "firmwareUrl": "http://example.com/firmware.bin",
      "deviceName": "Kitchen Clock"
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
- `deviceName` is a human-friendly label assigned by the server; stored on the device but not
  user-editable (like `WAGFAM_EVENT_TODAY`)

### Device Heartbeat

Each calendar fetch includes device telemetry as query parameters:

```text
GET /data_source.json?chip_id=5fc8ad&version=3.08.0-wagfam&uptime=1234567&heap=32496&rssi=-62
```

This lets a backend identify and monitor all deployed clocks without any additional
connections. Static JSON hosts ignore the current set of params gracefully — this
was empirically verified during PR #25 review with identical sha256 + HTTP 200 across
all 5 current param names against `raw.githubusercontent.com` (see commit `bca26e6`
and the [review thread](https://github.com/jrwagz/marquee-scroller/pull/25#discussion_r3144204226)).

**Caveat from that verification:** `raw.githubusercontent.com` *does* interpret
`?token=…` as an auth attempt, returning HTTP 404 on a private repo when a bad
token is sent. **Never name a future heartbeat parameter `token`.** Re-verify
out-of-band if the parameter set changes or the calendar URL moves to a different
static-JSON host. The corresponding regression test was removed because the CDN
returned race-flaky results when the target file was being updated; see the
"Back assertions about external services" rule above for the policy.

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

## REST API

See the [REST API section in README.md](README.md#rest-api) for the full endpoint table and
curl examples. All endpoints live under `/api/`, require HTTP Basic Auth, and return JSON.
Handlers are registered in `setup()` starting at line 296 of `marquee.ino`, with
implementations at lines 1275–1546.

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

## Known Issues

See `docs/CODE_REVIEW.md` for the full open-issues list. Top items by impact:

- `getWindDirectionText()` allocates 16 `String` objects on the stack every call — use `PROGMEM` array
- `cleanText()` does 35+ sequential `replace()` calls — heap fragmentation risk on long strings
- `savePersistentConfig()` always tail-calls `readPersistentConfig()` — fragile mutual recursion
- Config parser uses `indexOf("KEY=")` without `else if` — key collision risk if a URL contains
  another key name
- HTML page builders use `html +=` — replace static fragments with `server.sendContent(F("..."))`

## SecurityHelpers Module

`SecurityHelpers.h/.cpp` provides security functions extracted from `marquee.ino`:

- `isProtectedPath(path)` — prevents API writes/deletes to `/conf.txt` and `/ota_pending.txt`
- `extractDomain(url)` — parses domain from URLs (handles scheme-less, userinfo, port, query)
- `isTrustedFirmwareDomain(firmwareUrl, calendarUrl)` — validates OTA firmware URLs against
  a compile-time domain allowlist (`WAGFAM_TRUSTED_FIRMWARE_DOMAINS` in `Settings.h`) plus the
  calendar source domain
- `isInTrustedDomainList(domain, list)` — checks domain membership in comma-separated allowlist

These are tested in `tests/native/test_security_helpers/`.

## Build Provenance

`scripts/build_version.py` is a PlatformIO pre-build script (declared in `platformio.ini` as
`extra_scripts = pre:scripts/build_version.py`) that injects a `BUILD_SUFFIX` into the
firmware `VERSION` macro at compile time:

- Local builds: `3.08.0-wagfam-<username>-<YYYYMMDD>-<hash>`
- CI builds: `3.08.0-wagfam-<hash>`
- Arduino IDE (no script): falls back to `BASE_VERSION` alone

Tested in `tests/scripts/test_build_version.py` with 100% coverage enforced by CI.

## Backend Server

The `server/` directory contains a FastAPI backend that replaces the static GitHub-hosted
JSON file. It provides:

- **Calendar API** (`GET /api/v1/calendar`) — generates calendar messages from
  `data/wagfam_calendar_data.json` and returns the same JSON format the clocks expect
- **Device heartbeat** — records `chip_id`, `version`, `uptime`, `heap`, `rssi` from
  query params on each calendar fetch; auto-registers new devices on first contact
- **Device management** (`GET /api/v1/devices`, `POST /api/v1/devices/{chip_id}/update_name`)
  — admin endpoints for listing devices and setting human-friendly names
- **Per-device config** — includes `deviceName` and `eventToday` in the calendar response
  config block

Auth uses a shared secret via `Authorization: token <key>` or `Bearer <key>`.
See `server/README.md` (if present) or `server/pyproject.toml` for setup.
Calendar event logic is adapted from `jrwagz/wagfam-clocks-data-source` — see
`docs/CALENDAR_ADAPTATION.md` for provenance details.

## What Was Removed from Upstream (Qrome/marquee-scroller)

These modules existed in the upstream repo and were deleted in this fork:

- `NewsApiClient` — news headline fetching (this is what `WagFamBdayClient` replaced)
- `OctoPrintClient` — 3D printer status
- `PiHoleClient` — Pi-hole DNS blocker stats
- Bitcoin price display (was already removed in upstream v3.0)
- TimeZoneDB API calls (timezone now derived from OWM response)

`sources.json` was a leftover from the upstream news feature and has been deleted.
