# Code Review Remediation Plan

> Issues are taken from `docs/CODE_REVIEW.md`. Changes are grouped by file.
> Status column is updated as work completes.

---

## Changes by File

### `marquee/OpenWeatherMapClient.cpp`

| # | Issue | Status |
|---|-------|--------|
| 1 | Operator precedence bug: `int len = measureJson(jdoc) <= 150` | Done |
| 2 | `getWindDirectionText()` allocates 16 `String` objects on the stack | Done |

### `marquee/WagFamBdayClient.cpp`

| # | Issue | Status |
|---|-------|--------|
| 3 | `cleanText()` — add `reserve()` to reduce heap reallocs | Done |
| 6 | Remove unconditional debug `Serial.println` from `value()` | Done |

### `marquee/marquee.ino`

| # | Issue | Status |
|---|-------|--------|
| 4 | `savePersistentConfig()` mutual recursion — replace terminal `readPersistentConfig()` call | Done |
| 5 | Config parser fragility — replace `indexOf("KEY=")` with first-`=` split | Done |
| 7 | `handleSaveConfig()` — add bounds checking on numeric inputs | Done |
| 8 | `http://` OWM icon URLs in `sendHeader()` and `displayHomePage()` | Done |
| 11 | `displayTime` rebuilt every frame — move `hourMinutes()` to `processEverySecond()` | Done |
| 13 | Add dirty flag so `matrix.write()` only fires when display content changes | Done |
| 14 | `matrix.shutdown(false)` leftover in `processEveryMinute()` | Done |
| 18 | `todayDisplayMilliSecond` / `todayDisplayStartingLED` declared as `int`, should be `uint32_t` | Done |
| 23 | Missing `getWifiQuality()` forward declaration | Done |

### `.github/workflows/lint-test-build.yaml`

| # | Issue | Status |
|---|-------|--------|
| 17 | `actions/checkout@v6` → `@v4`, `upload-artifact@v7` → `@v4` | Done |

### Repository Cleanup

| # | Issue | Status |
|---|-------|--------|
| 15 | Delete `sources.json` (unused leftover from upstream news feature) | Done |
| 16 | Delete stale pre-built `.bin` files (v3.03, current is v3.07.0-wagfam) | Done |

---

## What Was Intentionally Left Alone

- **Issue 19** (`getTimeTillUpdate` uses `sprintf_P`) — Correct and harmless on ESP8266; cosmetic only.
- **Issue 20** (`typedef struct` in `WagFamBdayClient.h`) — Harmless C idiom; changing it has no
  functional benefit.
- **Issue 21** (`handleUpdateFromUrl` post-failure UI) — Requires architectural change to the HTTP
  response lifecycle; deferred.
- **Issue 24** (CDN-hosted CSS/icons) — Architectural trade-off, not a bug.
- **Issue 12** (`firstTimeSync`/`lastRefreshDataTimestamp` 32-bit truncation) — Safe until 2038;
  changing the type to `time_t` (64-bit on ESP8266) would double storage and change arithmetic
  throughout `getTimeTillUpdate()`. Deferred to a future focused change.
- **Issue 10** (Config form uses GET for API keys) — Low risk on a home LAN; changing to POST
  requires updating both the HTML form and `handleSaveConfig()`. Deferred.
