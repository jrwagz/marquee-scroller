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

For OTA flashing without recompiling (built from v3.03 — current source is v3.07.0-wagfam):

- `marquee.ino.d1_mini_3.03.bin` — standard 4×1 LED display
- `marquee.ino.d1_mini_wide_3.03.bin` — double-wide 8×1 LED display

Upload via the web interface at `http://<device-ip>/update`.

## Initial Setup

On first boot (or after "Forget WiFi"), the device creates a WiFi AP named `CLOCK-<chip-id>`.
Connect to it with a phone or laptop to enter your WiFi credentials.

Once connected to WiFi, the device displays its IP address. Open `http://<ip>/` in a browser to access the web interface.

Default web UI credentials: **admin / password**

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
      "eventToday": "1"
    }
  },
  { "message": "Justin's Birthday - 3 days away" },
  { "message": "Family Dinner - this Saturday" }
]
```

- Up to 10 messages are supported; they cycle through the marquee scroll
- The `config` block is optional; if present, `eventToday: 1` enables an animated dot border around the clock display
  for the day
- The `config` block can also remotely update `dataSourceUrl` and `apiKey` on the device

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
| `/saveconfig` | Saves configuration (GET with form params) |
| `/pull` | Forces immediate data refresh |
| `/update` | OTA firmware upload (file) |
| `/updateFromUrl` | OTA firmware update from URL |
| `/systemreset` | Resets settings to defaults |
| `/forgetwifi` | Clears saved WiFi credentials |
