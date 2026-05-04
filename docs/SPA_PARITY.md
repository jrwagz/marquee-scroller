# Legacy UI → SPA parity tracker

Goal: full feature parity from the legacy w3.css UI (`/`, `/configure`,
sidebar actions) into the Preact SPA (`/spa/`), so the legacy routes
can be retired and their PROGMEM strings reclaimed (~5 KB flash + the
W3.CSS / Font Awesome CDN dependency).

This doc is the working checklist. It dies when all rows are ✅ and
the legacy routes are removed.

## Source of truth

- Legacy routes audit: `tests/e2e/scripts/legacy_audit.py` (run against
  any device to regenerate `tests/e2e/out/legacy/audit.txt`)
- Legacy source: `marquee.ino` `displayHomePage()`, `handleConfigure()`,
  `handlePull()`, `handleSystemReset()`, `handleForgetWifi()`,
  `sendHeader()`, `sendFooter()`, `CHANGE_FORM*`, `WEB_ACTIONS*`

## Parity matrix

### Home page (`/`)

| Feature | Legacy | SPA | Status |
| --- | --- | --- | --- |
| Upcoming events list (from calendar) | ✅ | ✅ Home tab | ✅ Phase B |
| "Configure WagFam URL/key" warning when empty | ✅ | ✅ Home tab `ConfigWarnings` | ✅ Phase B |
| Weather city + country header | ✅ | ✅ Home tab `WeatherCard` | ✅ Phase B |
| Weather icon image | ✅ | ✅ Home tab (openweathermap.org/img/w/) | ✅ Phase B |
| Humidity / wind / pressure | ✅ | ✅ Home tab | ✅ Phase B |
| Weather condition + description | ✅ | ✅ Home tab | ✅ Phase B |
| Current temperature | ✅ | ✅ Home tab | ✅ Phase B |
| High / Low temperature | ✅ | ✅ Home tab | ✅ Phase B |
| Date + time | ✅ | ⚠️ implicit (browser shows current time) | acceptable — legacy showed device time which is just clock-formatted now |
| Weather error message (when API fails) | ✅ | ✅ Home tab `WeatherCard` error branch | ✅ Phase B |

### Configure page (`/configure`)

| Feature | Legacy | SPA | Status |
| --- | --- | --- | --- |
| WagFam Calendar Data URL | ✅ | ✅ Settings | ✅ |
| WagFam Calendar API Key | ✅ | ✅ Settings | ✅ |
| OpenWeatherMap API Key | ✅ | ✅ Settings | ✅ |
| Geo location (city ID) | ✅ | ✅ Settings | ✅ |
| Live resolved city name display ("Midvale, US") | ✅ | ❌ | minor gap — Phase C polish |
| Metric units toggle | ✅ | ✅ Settings | ✅ |
| Show Date / City / High-Low / Condition / Humidity / Wind / Pressure | ✅ | ✅ Settings | ✅ |
| 24-hour clock toggle | ✅ | ✅ Settings | ✅ |
| PM indicator toggle | ✅ | ✅ Settings | ✅ |
| LED brightness | ✅ 0-15 | ✅ slider | ✅ |
| Scroll speed | ✅ select Slow/Normal/Fast/Very Fast | ✅ slider 5-100ms | ✅ (different UX, same data) |
| Refresh interval | ✅ select 5/10/15/20/30/60 | ✅ free number 1-60 | ✅ |
| Scroll interval | ✅ number 1-10 | ✅ | ✅ |
| Save | ✅ | ✅ | ✅ |
| Firmware Update URL form | ✅ inline | ⚠️ link out to /updateFromUrl | acceptable — full URL flow lives outside SPA |
| Link to /update | ✅ | ✅ Actions | ✅ |
| Link to /updatefs | ✅ | ✅ Actions → LittleFS (firmware-update card) | ✅ Phase B |

### Sidebar actions (every legacy page)

| Feature | Legacy | SPA | Status |
| --- | --- | --- | --- |
| Refresh Data | ✅ /pull | ✅ Actions → Force Refresh | ✅ |
| Reset Settings (delete /conf.txt + restart) | ✅ /systemreset (confirm dialog) | ✅ Actions → Reset Settings (window.confirm + POST `/api/system-reset`) | ✅ Phase B |
| Forget WiFi (clear creds + restart) | ✅ /forgetwifi (confirm dialog) | ✅ Actions → Forget WiFi (window.confirm + POST `/api/forget-wifi`) | ✅ Phase B |
| Firmware Update (sketch) | ✅ /update | ✅ Actions → Upload .bin | ✅ |
| Firmware Update from URL | ✅ /updateFromUrl | ✅ Actions → From URL | ✅ |
| LittleFS Upload | ✅ /updatefs | ✅ Actions → LittleFS | ✅ Phase B |

### Footer (every legacy page)

| Feature | Legacy | SPA | Status |
| --- | --- | --- | --- |
| Version | ✅ | ✅ Status | ✅ |
| Next Update countdown | ✅ "0:07:33" | ✅ Status (`Next data refresh` row) | ✅ Phase B |
| Signal strength | ✅ "100%" | ✅ Status (WiFi card) | ✅ |

## Plan

**Phase A** — firmware: expose missing data
1. Extend `/api/status` with `next_refresh_in_sec` + (optional)
   `time_now_iso` so the SPA can render the countdown without an extra
   round-trip.
2. Add `GET /api/weather` returning the WeatherClient state.
3. Add `GET /api/events` returning the bdayClient messages array.
4. Add `POST /api/system-reset` (deletes `/conf.txt`, schedules restart).
5. Add `POST /api/forget-wifi` (resets WiFi creds, schedules restart).

**Phase B** — SPA: add a Home tab + extend Actions
1. New `Home` tab as default (events + weather + config-needed warnings).
2. Actions tab gets two new cards: Reset Settings, Forget WiFi (both
   with confirm steps; both wired to the new POST routes).
3. Firmware Update card gets a third button: LittleFS Upload (link to
   `/updatefs`).

**Phase C** — minor polish
1. Status tab adds Next Update countdown.
2. Settings tab shows resolved city next to the geo input.

**Phase D** — decommission legacy
1. Remove `displayHomePage`, `handleConfigure`, `handleSaveConfig`,
   `handlePull`, `handleSystemReset`, `handleForgetWifi`,
   `sendHeader`, `sendFooter`.
2. Remove `CHANGE_FORM*` and `WEB_ACTIONS*` PROGMEM constants.
3. Drop W3.CSS + Font Awesome CDN references.
4. Make `/` redirect to `/spa/`.
5. Verify flash savings; reclaim the ~5 KB the docs predicted.

## Verification

Each phase checked off with a Playwright run that exercises the new
SPA path end-to-end and asserts the legacy feature is fully replaced.
See `tests/e2e/scripts/`.
