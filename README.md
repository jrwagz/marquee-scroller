# WagFam CalClock

A personal fork of [Marquee Scroller](https://github.com/Tronixstuff/marquee-scroller) by David Payne,
customized as a family calendar + weather clock for a Wemos D1 Mini (ESP8266) with a MAX7219 LED matrix display.

## Features

- Clock display with NTP time sync (12h or 24h, with optional PM indicator)
- Local weather from OpenWeatherMap (temperature, conditions, wind, humidity, pressure, high/low)
- **WagFam Calendar** — scrolls upcoming family events/birthdays fetched from a private JSON endpoint;
  animated border on event days
- Configured entirely through a web interface (no re-flashing required for settings changes)
- OTA firmware updates via web interface (file upload or URL)
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

This wipes `/conf.txt` (web password, calendar URL, API keys), so you'll need to reconfigure
after flashing.

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

Once connected to WiFi, the device displays its IP address. Open `http://<ip>/` in a browser to access the web interface.

On first boot, a random web password is generated and printed to the serial console. It can be
changed from the Configure page. The username for HTTP Basic Auth is **admin**.

## Configuration

All settings are managed through the web interface and persisted to the device filesystem.
Editing `Settings.h` only affects defaults for a completely fresh start (filesystem erased).

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
      "deviceName": "Kitchen Clock"
    }
  },
  { "message": "Justin's Birthday - 3 days away" },
  { "message": "Family Dinner - this Saturday" }
]
```

- Up to 10 messages are supported; they cycle through the marquee scroll
- The `config` block is optional; if present, `eventToday: 1` enables an animated dot border around the clock display
  for the day
- The `config` block can also remotely update `dataSourceUrl`, `apiKey`, and `deviceName` on the device

### Device Heartbeat

Each calendar fetch includes device telemetry as URL query parameters (`chip_id`, `version`,
`uptime`, `heap`, `rssi`, and `utc_offset_sec`). The UTC offset in seconds is derived
automatically from the OpenWeatherMap response — no extra configuration needed. A backend
can use these to identify and monitor all deployed clocks. Static JSON hosts ignore the query
params.

### Geo Location

Enter a city ID, city name + country code (`Chicago,US`), or GPS coordinates (`lat,lon`) —
whatever format OpenWeatherMap accepts.

## Firmware Update from URL

From the Configure page, a firmware URL (HTTP only — HTTPS is not supported) can be entered to trigger an OTA update
directly from a hosted `.bin` file. The device will restart automatically on success.

## Web Interface Routes

| Route | Description |
| ----- | ----------- |
| `/` | Home — shows upcoming events and current weather (legacy UI) |
| `/spa/` | Preact SPA frontend (served from LittleFS, see [`docs/WEBUI.md`](docs/WEBUI.md)) |
| `/configure` | Settings form (legacy UI) |
| `/saveconfig` | Saves configuration (POST) |
| `/pull` | Forces immediate data refresh |
| `/update` | OTA firmware upload (file) |
| `/updateFromUrl` | OTA firmware update from URL |
| `/updatefs` | OTA LittleFS image upload — for SPA bundle refresh without serial cable |
| `/systemreset` | Resets settings to defaults |
| `/forgetwifi` | Clears saved WiFi credentials |

## REST API

All `/api/*` endpoints require HTTP Basic Auth (`admin` / your web password) and return JSON.

| Endpoint | Method | Description |
| --- | --- | --- |
| `/api/status` | GET | Device health (version, uptime, heap, WiFi, OTA state) |
| `/api/config` | GET | Read all config |
| `/api/config` | POST | Partial config update (JSON body) |
| `/api/restart` | POST | Reboot device |
| `/api/refresh` | POST | Force weather + calendar data refresh |
| `/api/ota/status` | GET | OTA rollback file state |
| `/api/fs/read` | GET | Read file (`?path=/conf.txt`) |
| `/api/fs/write` | POST | Write file (`{"path":"/x","content":"y"}`) |
| `/api/fs/delete` | DELETE | Delete file (`?path=/x`) |
| `/api/fs/list` | GET | List all filesystem files |

Example — read config:

```bash
curl -u admin:password http://<device-ip>/api/config
```

Example — update brightness to 10:

```bash
curl -u admin:password -X POST -H 'Content-Type: application/json' \
  -d '{"display_intensity": 10}' http://<device-ip>/api/config
```
