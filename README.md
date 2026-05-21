# WagFam CalClock

A personal fork of [Marquee Scroller](https://github.com/Tronixstuff/marquee-scroller) by David Payne,
customized as a family calendar + weather clock for a Wemos D1 Mini (ESP8266) with a MAX7219 LED matrix display.

## Features

- Clock display with NTP time sync (12h or 24h, with optional PM indicator)
- Local weather from OpenWeatherMap (temperature, conditions, wind, humidity, pressure, high/low)
- **WagFam Calendar** — scrolls upcoming family events/birthdays fetched from a private JSON endpoint;
  animated border on event days
- Configured entirely through a web interface (no re-flashing required for settings changes)
- OTA firmware updates via web interface (file upload or URL), plus optional
  calendar-driven auto-update for both the firmware sketch and the SPA bundle.
  Gated by `WAGFAM_AUTO_UPDATE_DISABLED` at compile time and an
  `auto_update_enabled` runtime toggle in the SPA Settings tab (issue #95)
- Configurable scroll speed, brightness, and refresh interval

## Hardware

- **Wemos D1 Mini** (ESP8266): <https://amzn.to/3tMl81U>
- **MAX7219 Dot Matrix Module 4-in-1**: <https://amzn.to/2HtnQlD>

### Wiring

| Display | Wemos D1 Mini |
|---------|---------------|
| CLK     | D5 (SCK)      |
| CS      | D6            |
| DIN     | D7 (MOSI)     |
| VCC     | 5V+           |
| GND     | GND           |

## Building and Flashing

### PlatformIO (recommended)

Open the project folder in VS Code with the [PlatformIO](https://platformio.org/)
(or [PIOArduino](https://marketplace.visualstudio.com/items?itemName=pioarduino.pioarduino-ide)) extension installed.

```bash
pio run                          # Build
pio run --target upload          # Flash to device
pio device monitor               # Serial monitor (115200 baud)
```

All library dependencies are declared in `platformio.ini` and downloaded automatically.

Flash size must be set to **4MB (FS:1MB OTA:~1019KB)** — the filesystem is required for storing configuration.

### Arduino IDE (alternative)

1. Install USB CH340G drivers: <https://sparks.gogo.co.nz/ch340.html>
2. Add `http://arduino.esp8266.com/stable/package_esp8266com_index.json` to Additional Board Manager URLs
3. Install esp8266 platform via Boards Manager
4. Select Board: **LOLIN(WEMOS) D1 R2 & mini**
5. Set Flash Size: **4MB (FS:1MB OTA:~1019KB)**

Required libraries (install via Library Manager or download manually):

- [ESPAsyncWiFiManager](https://github.com/alanswx/ESPAsyncWiFiManager) ≥ 0.31.0
- [ESPAsyncWebServer (esphome fork)](https://github.com/esphome/ESPAsyncWebServer) ≥ 3.3.0
- [ESPAsyncTCP (esphome fork)](https://github.com/esphome/ESPAsyncTCP) ≥ 2.0.0
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) ≥ 1.12
- [Time](https://github.com/PaulStoffregen/Time) ≥ 1.6
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) ^7.4

The following libraries are cached locally in `/lib` (no separate install needed):

- `Max72xxPanel` — MAX7219 LED matrix driver
- `JsonStreamingParser` — streaming JSON parser used by WagFam Calendar client

## Pre-built Binaries

Every tagged release on the [Releases page](https://github.com/jrwagz/marquee-scroller/releases)
ships **three** flashable images, for three install scenarios:

| File | What it contains | When to use |
| --- | --- | --- |
| `marquee-scroller-<v>-merged.bin` | Sketch + LittleFS in one image | **First-time install** on a fresh device — one esptool command, no offset arithmetic |
| `marquee-scroller-<v>.bin` | Sketch only | **OTA firmware update** via `/update` — preserves config and SPA bundle |
| `marquee-scroller-<v>-littlefs.bin` | LittleFS only (SPA bundle + defaults) | **OTA SPA install or refresh** via `/updatefs` (≥3.09.3) — or manual serial flash |

### First-time install (recommended)

Download `marquee-scroller-<version>-merged.bin` and flash it at offset `0x0` with esptool:

```bash
esptool.py --port /dev/cu.usbserial-XXXX write_flash 0x0 marquee-scroller-<version>-merged.bin
```

This wipes the entire flash and installs sketch + SPA bundle in one shot. After flashing,
follow the "Initial Setup" section below for WiFi config.

### OTA firmware update

Download `marquee-scroller-<version>.bin` and upload via the web interface at
`http://<device-ip>/update`. Fast path for shipping firmware fixes — preserves your
config and the SPA bundle.

> **Note:** OTA does **not** touch the LittleFS partition where the SPA lives. If you
> visit `/spa/` after an OTA and see the "SPA bundle not installed" page, flash
> `littlefs.bin` as below.

### SPA bundle refresh / install (OTA — no serial cable required)

Once the device is running firmware ≥ 3.09.3-wagfam, the LittleFS image can be flashed
over OTA via the web interface:

1. Download `marquee-scroller-<version>-littlefs.bin`
2. Open `http://<device-ip>/updatefs` in a browser
3. Select the file and click **Upload & Flash FS** — the device reboots into the new FS

Your settings (`/conf.txt` — calendar URL, API keys, display config) are automatically backed
up and restored, so no reconfiguration is needed after the device reboots.

> **Bootstrapping note:** OTA-flashing the LittleFS image requires firmware that
> includes the `/updatefs` route (added in 3.09.3-wagfam). Devices on older firmware
> need a one-time firmware OTA first via `/update`, after which subsequent SPA refreshes
> are pure-browser.

### SPA bundle refresh — serial alternative

If OTA is unavailable, flash `littlefs.bin` over USB at offset `0x300000`
(the LittleFS partition start for the d1_mini 4MB FS:1MB layout):

```bash
esptool.py --port /dev/cu.usbserial-XXXX write_flash 0x300000 marquee-scroller-<version>-littlefs.bin
```

From a source checkout, `make uploadfs` does the same in one step.

## Initial Setup

On first boot (or after "Forget WiFi"), the device creates a WiFi AP named `CLOCK-<chip-id>`.
Connect to it with a phone or laptop to enter your WiFi credentials.

Once connected to WiFi, the device displays its IP address. Open
`http://<ip>/spa/` in a browser to access the web interface (`http://<ip>/`
also works — it 302-redirects to `/spa/`).

### Device enrollment

A factory-fresh clock (no calendar API key stored) self-registers against the
wagfam-server after WiFi connects — it does **not** need to be configured by
hand. The clock polls the server's enrollment endpoint every ~15 seconds and
scrolls a short **setup code** (`Setup Code: ABC234`) on the LED. An admin
matches that code to the pending device in the wagfam-server admin UI and
authorizes it; the server then hands back a signed config bundle (calendar
URL, auth key, name, display settings), the clock applies it and reboots into
normal operation. See [issue #125](https://github.com/jrwagz/marquee-scroller/issues/125)
and [wagfam-server#62](https://github.com/jrwagz/wagfam-server/issues/62).

The enrollment endpoint is baked in at build time via the `WAGFAM_ENROLL_URL`
flag (`platformio.ini`); a build without that flag skips enrollment and runs as
a normal clock. While a clock waits to be authorized you can still configure it
by hand on the SPA **Settings** tab — entering a calendar API key there exits
enrollment mode.

## Configuration

All settings are managed through the web interface and persisted to the device filesystem.
Editing `Settings.h` only affects defaults for a completely fresh start (filesystem erased).

### Scroller font

The marquee message font is selectable on the **Settings → Display** tab (issue #106).
**Fifteen** options ship with the firmware — five short fonts (≤7px tall), five full-height
(8px) hand-designed fonts, and five full-height stylistic variants procedurally derived from
Tall. Together they share the same `marquee/WagfamFont.h` PROGMEM bank generated by
`scripts/gen_wagfam_font.py`.

| ID | Name | Notes |
| --- | --- | --- |
| 0 | Classic | Adafruit_GFX builtin 5×7 (the default) |
| 1 | Block | Custom 5×7, blocky/all-caps — defined in `marquee/WagfamFont.h` |
| 2 | Org | Adafruit_GFX `Org_01` — small retro pixel font |
| 3 | Picopixel | Adafruit_GFX `Picopixel` — variable-width, 6px tall |
| 4 | TomThumb | Adafruit_GFX `TomThumb` — 3×5 micro |
| 5 | Tall | Custom 5×8 standard, distinct lowercase — fills full matrix height |
| 6 | Bold | Custom 5×8 thick double-stroke verticals (uppercase only; lowercase = uppercase) |
| 7 | Slim | Custom 3×8 narrow with distinct lowercase — fits ~8 chars across the display |
| 8 | Outline | Custom 5×8 hollow / signage-style (uppercase only; lowercase = uppercase) |
| 9 | Digi | Custom 5×8 segment/digital-clock vibe with distinct lowercase |
| 10 | Italic | Tall slanted: top half shifted right by 1 column for a leaning feel |
| 11 | Serif | Tall with slab-serif caps spread on the top and bottom rows |
| 12 | Pixel | Tall with a deterministic halftone speckle (~1 in 5 pixels removed) |
| 13 | Inverse | Tall inverted in the top h-1 rows — stylized "block-art" aesthetic, not strict legibility |
| 14 | Stencil | Tall with 1-pixel "bridge" cuts in the middle of every long vertical stroke |

The marquee font selection persists in `/conf.txt` (`scrollFont=` key) and is exposed via
`display_font` on `/api/config`. The six hand-designed custom fonts (Block, Tall, Bold, Slim,
Outline, Digi) and the five derived ones (Italic, Serif, Pixel, Inverse, Stencil) all live
in `marquee/WagfamFont.h`; re-run `scripts/gen_wagfam_font.py` after editing the GLYPHS dicts
or the procedural transforms.

### Clock face style

The clock face has its own selectable rendering — separate from the marquee font, since the
clock only ever shows ten digits + a colon (and an AM/PM marker in 12-hour mode), which lets
each style optimize layout for that constrained alphabet. Selection persists as `clockStyle=`
in `/conf.txt` and is exposed as `display_clock_style` on `/api/config`. Render functions
live in [`marquee/ClockStyles.h`](marquee/ClockStyles.h).

| ID | Name | 12h | 24h | Notes |
| --- | --- | --- | --- | --- |
| 0 | Classic | ✓ | ✓ | Default — Adafruit 5x7 digits, blinking colon, top-right PM dot |
| 1 | Mega | ✓ | ✗ | Bespoke 5x8 digits filling the full matrix height |
| 2 | Banner | ✓ | ✓ | Classic digits + horizontal rule across the bottom row, corner notches up top |
| 3 | Pulse | ✓ | ✓ | Classic digits with an animated heartbeat colon (alternates dot ↔ square every 500 ms) |
| 4 | Stack | ✓ | ✗ | Hour digits in the top half, minute digits in the bottom half, custom 3x4 micro-digits |
| 5 | Frame | ✓ | ✓ | Classic time inside a 1-pixel rectangular border |
| 6 | Suffix | ✓ | ✗ | Classic digits at left, "AM"/"PM" rendered with TomThumb at right |
| 7 | Inverse | ✓ | ✗ | Solid 32x7 plate with the time digits carved out as negative space |
| 8 | Stencil | ✓ | ✓ | Bespoke 5x8 digits with a 1-pixel "bridge" cut at the midline of every vertical stem |
| 9 | Italic | ✓ | ✗ | Bespoke 5x8 digits with the upper half row-shifted right by 1 column |
| 10 | Dotted | ✓ | ✓ | Bespoke 5x8 digits with a `(x*7+y*13)%5==0` halftone speckle |

Styles flagged as 12h-only fall back to **Classic** at render time when the device is in
24-hour mode (no manual switching needed). The SPA Settings tab shows a hint when a 12h-only
style is paired with a 24-hour clock.

### API Keys

| Key | Purpose | Required? |
| --- | ------- | --------- |
| OpenWeatherMap API key | Weather data + timezone offset for NTP | Yes |
| WagFam Calendar URL | JSON endpoint for family events | Optional |
| WagFam API Key | Auth token sent as `Authorization: token <key>` | Optional |

Get a free OpenWeatherMap key at <https://openweathermap.org/>

### WagFam Calendar

The calendar client fetches a JSON array from the configured URL (HTTPS supported). The endpoint should return:

```json
[
  {
    "config": {
      "eventToday": "1",
      "deviceName": "Kitchen Clock",
      "latestVersion": "4.7.0-7918f29",
      "firmwareUrl": "http://files.example.com/marquee-4.7.0-7918f29.bin",
      "latestSpaVersion": "4.7.0-7918f29",
      "spaFsUrl": "http://files.example.com/marquee-4.7.0-7918f29-littlefs.bin",
      "trollMessage": "Go Cougars!",
      "configUpdateVersion": 3,
      "configUpdatePayload": "{\"display_intensity\":8}",
      "configUpdateSignature": "..."
    }
  },
  { "message": "Justin's Birthday - 3 days away" },
  { "message": "Family Dinner - this Saturday" }
]
```

The `config` block fields are all optional; the firmware ignores unknown keys
and tolerates missing ones:

| Field | What the firmware does with it |
| --- | --- |
| `eventToday` | `1` enables an animated dot border around the clock display for the day |
| `deviceName` | Stored on the device and shown in the SPA |
| `family` | Lowercase family tag (`"butterfield"` / `"wagner"` / `null`). When set, personalizes the bootup welcome message and the SPA header. Unknown values are logged and ignored |
| `latestVersion` + `firmwareUrl` | When `latestVersion` differs from the firmware's compiled-in `VERSION`, auto-update fires (subject to compile-time + runtime opt-out flags — see "Features"). HTTP only; HTTPS not supported by ESP8266HTTPUpdate without large heap cost |
| `latestSpaVersion` + `spaFsUrl` | Same model as firmware, but for the LittleFS partition (SPA bundle). `/conf.txt` is preserved across the flash |
| `trollMessage` | When non-empty the clock displays *only* this string and ignores calendar messages. RAM-only on the clock — a power cycle clears it. Issue #99 |
| `configUpdateVersion` + `configUpdatePayload` + `configUpdateSignature` | A signed remote config update. Clock verifies the ECDSA-P256 signature against its embedded public key over the canonical payload bytes; applies iff the version is strictly greater than `LAST_APPLIED_CONFIG_VERSION`. Issue #99 |

Up to 10 calendar `{"message": "..."}` entries are supported; they cycle
through the marquee scroll.

#### Pre-flash SHA256 verification (issue #96 phase A)

When the server publishes `firmwareSha256` (alongside `firmwareUrl`) and/or
`spaFsSha256` (alongside `spaFsUrl`) in the `config` block, the firmware
streams the downloaded bytes through SHA256 and refuses to flash on
mismatch. When the field is absent (older server), the firmware logs
`[OTA] No expected SHA256 from server; skipping verification` and proceeds
— forward-compat with deployments where the server hasn't been upgraded
yet. See `verifyOtaSha256()` in `marquee/marquee.ino` and
[`docs/OTA_STRATEGY.md`](docs/OTA_STRATEGY.md) for the verifier and its
failure modes.

### Device Heartbeat

Each calendar fetch includes device telemetry as URL query parameters (`chip_id`, `version`,
`uptime`, `heap`, `rssi`, `utc_offset_sec`, `lan_ip`, and `mdns_name`). The UTC offset in
seconds is derived automatically from the OpenWeatherMap response. `lan_ip` is the device's
private LAN IP (e.g. `192.168.1.42`) — distinct from the household NAT public IP that a
backend would otherwise see in `X-Forwarded-For`. `mdns_name` is the sanitized Bonjour
hostname (see below). A backend uses these to power a directory page that one-click jumps the
user to `http://<mdns_name>.local/spa/` when they're on the same LAN. Static JSON hosts
ignore the query params.

### Local discovery (mDNS / Bonjour)

The device advertises itself on the local network via mDNS so users don't have to look up
its IP. After WiFi connects, the device publishes:

- An `A` record at `<sanitized-name>.local` — open `http://<sanitized-name>.local/spa/` from
  any device on the same LAN. The name is derived from the server-set `device_name` (e.g.
  "Kitchen Clock" → `kitchen-clock.local`); when no name is set yet the fallback is
  `wagfam-<chip-id>.local` so every clock has a stable label even before naming.
- An `_http._tcp` service record (port 80) so generic HTTP browsers see the device.
- A `_wagfam._tcp` service record with `chip` and `version` TXT entries — intended for a
  future native app that enumerates every clock on the LAN with one Bonjour query.

The current `mdns_name` is also exposed in `GET /api/status` for debugging. mDNS works
reliably on iOS Safari, macOS, and Linux; Android Chrome support is patchy and may need a
fallback to the LAN IP.

### Geo Location

Enter a city ID, city name + country code (`Chicago,US`), or GPS coordinates (`lat,lon`) —
whatever format OpenWeatherMap accepts.

## Firmware Update from URL

From the Configure page, a firmware URL (HTTP only — HTTPS is not supported) can be entered to trigger an OTA update
directly from a hosted `.bin` file. The device will restart automatically on success.

## Web Interface Routes

The Preact SPA at `/spa/` is the primary UI. The legacy w3.css interface
was removed; its routes 302-redirect to `/spa/` so existing bookmarks
keep working.

| Route | Description |
| ----- | ----------- |
| `/spa/` | Preact SPA — Home, Status, Settings, Actions tabs (see [`docs/WEBUI.md`](docs/WEBUI.md)) |
| `/update` | OTA firmware upload form (sketch only — does not touch LittleFS) |
| `/updateFromUrl` | OTA firmware update from URL (HTTP only — no TLS) |
| `/updatefs` | OTA LittleFS image upload — for SPA bundle refresh without serial cable |
| `/`, `/configure`, `/pull`, `/systemreset`, `/forgetwifi`, `/saveconfig` | 302 → `/spa/` (legacy paths; SPA Settings/Actions tabs cover the equivalents) |

## REST API

All `/api/*` endpoints return JSON.

| Endpoint | Method | Description |
| --- | --- | --- |
| `/api/status` | GET | Device health (version, uptime, heap, WiFi, OTA state) |
| `/api/config` | GET | Read all config |
| `/api/config` | POST | Partial config update (JSON body) |
| `/api/restart` | POST | Reboot device |
| `/api/refresh` | POST | Force weather + calendar data refresh |
| `/api/ota/status` | GET | OTA rollback file state |
| `/api/spa/update-from-url` | POST | Flash SPA (LittleFS) image from URL; preserves config (`{"url":"http://..."}`) |
| `/api/fs/read` | GET | Read file (`?path=/conf.txt`) |
| `/api/fs/write` | POST | Write file (`{"path":"/x","content":"y"}`) |
| `/api/fs/delete` | DELETE | Delete file (`?path=/x`) |
| `/api/fs/list` | GET | List all filesystem files |

Example — read config:

```bash
curl http://<device-ip>/api/config
```

Example — update brightness to 10:

```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"display_intensity": 10}' http://<device-ip>/api/config
```
