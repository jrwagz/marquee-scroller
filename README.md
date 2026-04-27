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

- [WiFiManager](https://github.com/tzapu/WiFiManager) ≥ 2.0.17
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) ≥ 1.12
- [Time](https://github.com/PaulStoffregen/Time) ≥ 1.6
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) ^7.4

The following libraries are cached locally in `/lib` (no separate install needed):

- `Max72xxPanel` — MAX7219 LED matrix driver
- `JsonStreamingParser` — streaming JSON parser used by WagFam Calendar client

## Pre-built Binaries

CI builds generate firmware artifacts on every tagged release. Download `firmware.bin` from the
[Releases page](https://github.com/jrwagz/marquee-scroller/releases) and upload via the web
interface at `http://<device-ip>/update`.

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
`uptime`, `heap`, `rssi`). A backend can use these to identify and monitor all deployed clocks.
Static JSON hosts ignore the query params.

### Geo Location

Enter a city ID, city name + country code (`Chicago,US`), or GPS coordinates (`lat,lon`) —
whatever format OpenWeatherMap accepts.

## Firmware Update from URL

From the Configure page, a firmware URL (HTTP only — HTTPS is not supported) can be entered to trigger an OTA update
directly from a hosted `.bin` file. The device will restart automatically on success.

## Web Interface Routes

| Route | Description |
| ----- | ----------- |
| `/` | Home — shows upcoming events and current weather |
| `/configure` | Settings form |
| `/saveconfig` | Saves configuration (POST) |
| `/pull` | Forces immediate data refresh |
| `/update` | OTA firmware upload (file) |
| `/updateFromUrl` | OTA firmware update from URL |
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
