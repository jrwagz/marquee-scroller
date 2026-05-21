# WagFam CalClock — Code Flow Reference

> **Audience:** Developers who want to understand exactly how the firmware runs from
> power-on through normal operation. Code references use `file:line` format.
>
> **Note on line numbers:** marquee.ino has been heavily refactored since this doc
> was written (Phase D removed the legacy server-rendered routes; auth + CSRF were
> stripped). The cited line numbers are approximate — search for the function name
> if the exact line is off. The control-flow descriptions are still accurate, with
> the exception of any references to removed handlers (`handleSaveConfig`,
> `handleConfigure`, `handlePull`, `handleSystemReset`, `handleForgetWifi`,
> `displayHomePage`, `sendHeader`, `sendFooter`) and removed auth machinery
> (CSRF token generation, web password generation, `setAuthentication`,
> `requireApiAuth`). See `docs/archive/SPA_PARITY.md` for the migration map.

---

## Table of Contents

1. [Boot Sequence (`setup()`)](#boot-sequence-setup)
2. [Main Loop (`loop()`)](#main-loop-loop)
3. [Per-Second Work (`processEverySecond()`)](#per-second-work-processeverysecond)
4. [Per-Minute Work (`processEveryMinute()`)](#per-minute-work-processeveryminute)
5. [Data Fetch (`getWeatherData()`)](#data-fetch-getweatherdata)
6. [Weather Client (`OpenWeatherMapClient::updateWeather()`)](#weather-client)
7. [Calendar Client (`WagFamBdayClient::updateData()`)](#calendar-client)
8. [Display: Frame Rendering (`centerPrint()`)](#display-frame-rendering-centerprint)
9. [Display: Scroll Ticker (`scrollMessageWait()`)](#display-scroll-ticker-scrollmessagewait)
10. [Config: Save and Load](#config-save-and-load)
11. [Web Handlers](#web-handlers)
12. [Time Sync Flow](#time-sync-flow)

---

## Boot Sequence (`setup()`)

**File:** `marquee/marquee.ino:220`

```text
setup()
│
├── Serial.begin(115200)              // 115200 baud serial debug output
├── LittleFS.begin()                  // Mount LittleFS filesystem
├── pinMode(LED_BUILTIN, OUTPUT)      // Setup status LED
│
├── readPersistentConfig()            // Load /conf.txt into global variables
│   └── (If file missing: calls savePersistentConfig() to create it with defaults)
│
├── Read /spa/version.json           // Populate SPA_VERSION (or "unknown" if absent)
│
├── matrix.setIntensity(0)            // Start display off (intensity 0)
├── matrix.setRotation/setPosition   // Set up panel physical layout (4 panels, rotated CCW)
├── centerPrint("hello")              // Show "hello" at startup
├── Brightness ramp up 0→15, then 0  // Splash animation (delays 100ms/step)
├── delay(1000)
├── matrix.setIntensity(displayIntensity)  // Set to user-configured brightness
│
├── scrollMessageWait("Welcome to the Wagner Family Calendar Clock!!!")
│
├── WiFiManager.autoConnect()         // AP fallback if not connected
│   └── On failure: WiFi.disconnect(), ESP.reset()
│
├── checkOtaRollback()               // Crash-loop detection + rollback
│
├── server.on("/", redirectToSpa)     // Legacy paths 302 → /spa/
├── server.on("/api/...")             // Register REST API handlers
├── server.on("/update", ...)         // /update + /updatefs upload handlers (no auth)
├── server.serveStatic("/spa", LittleFS, "/spa/")  // SPA bundle from LittleFS
├── server.onNotFound(handleNotFound) // SPA-aware 404 fallback
├── server.begin()                   // Start HTTP server on port 80
│
├── scrollMessageWait(" vVERSION  IP: x.x.x.x  ")  // Announce IP on display
│
├── timeNTPsetup()                   // Open UDP socket, set 20s sync interval
│
├── if WAGFAM_ENROLL_URL non-empty AND WAGFAM_API_KEY empty:
│   └── enrollmentMode = true        // Enter enrollment mode (issue #125)
│       └── loop() will call runEnrollmentLoop() instead of the normal clock
│
└── flashLED(1, 500)                 // Blink status LED once
```

**After `setup()` completes**, the device is on WiFi, serving HTTP, and ready.
Time is not yet synced (NTP first sync happens on the first `getWeatherData()` call).

When `enrollmentMode` is true the device skips the clock, Tasmota, and weather
paths in `loop()` and instead runs the enrollment polling loop — see
[Main Loop](#main-loop-loop) below.

---

## Main Loop (`loop()`)

**File:** `marquee/marquee.ino:327`

This runs continuously, as fast as the MCU allows (thousands of times per second
when not scrolling).

```text
loop()
│
├── MDNS.update()                    // Pump mDNS (always, even in enrollment mode)
│
├── if enrollmentMode:               // Issue #125 — no calendar API key
│   ├── runEnrollmentLoop()          // Poll server, scroll setup code, apply bundle
│   └── return                       // Normal clock, Tasmota, weather all skipped
│
├── TasmotaDiscovery::tick()
│
├── if second() changed → processEverySecond()
│
├── if minute() changed → processEveryMinute()
│
├── displayTime = hourMinutes(false)
│   └── Returns e.g. " 3:45" (12h) or "15:45" (24h)
│   └── The colon is replaced with ' ' every other second (flashing effect)
│
├── matrix.fillScreen(LOW)           // Clear display framebuffer
└── centerPrint(displayTime, true)   // Draw time + extras, then SPI flush
```

(The web server is `AsyncWebServer`; HTTP requests are handled in the background
via TCP callbacks, so `server.handleClient()` is *not* called from `loop()`.)

When `enrollmentMode` is true the clock does **not** show the time; `runEnrollmentLoop()`
scrolls `Setup Code: <code>` (or `Enrolling...` before the first server response) on the
LED. mDNS and the async web server stay live, so the SPA is still accessible. The device
exits this mode only by `ESP.restart()` — either after a successful authorized bundle
(which saves the API key and reboots) or when a calendar API key is set manually via the
SPA Settings tab.

The display is **redrawn every loop iteration** in normal mode. `matrix.write()` (inside `centerPrint`)
does a full SPI transfer on every frame. This is fast on ESP8266 but could be optimized
to only redraw when the content actually changes.

### `hourMinutes()` — `marquee.ino:428`

Builds the 5-character time string:

- 24h mode: `zeroPad(hour()) + ':' + zeroPad(minute())`  → e.g. `"03:45"`
- 12h mode: `spacePad(hourFormat12()) + ':' + zeroPad(minute())` → e.g. `" 3:45"`
- The `:` becomes a space every even second (flashing colon effect) when `isRefresh=false`

---

## Per-Second Work (`processEverySecond()`)

**File:** `marquee/marquee.ino:353`

```text
processEverySecond()
│
└── if (minutesSinceLastRefresh >= minutesBetweenDataRefresh) OR (never fetched yet)
    └── getWeatherData()
```

The trigger condition is checked every second. `getWeatherData()` itself takes several
seconds to complete (HTTP requests), so the time gap between refreshes is effectively
`minutesBetweenDataRefresh` minutes plus however long the fetch takes.

---

## Per-Minute Work (`processEveryMinute()`)

**File:** `marquee/marquee.ino:370`

Decides whether to scroll the message ticker. Runs once per minute.

```text
processEveryMinute()
│
├── if weatherClient has error → scrollMessageWait(errorMessage) and return
│
├── matrix.shutdown(false)           // Wake display if it was sleeping
├── matrix.fillScreen(LOW)
│
├── displayRefreshCount--
├── if (displayRefreshCount <= 0) AND weather data is valid:
│   ├── displayRefreshCount = minutesBetweenScrolling  // reset countdown
│   │
│   ├── Build scroll message string:
│   │   ├── " " (leading space)
│   │   ├── [if SHOW_DATE] "Monday, Apr 25  "
│   │   ├── [if SHOW_CITY] "Chicago  75F  "
│   │   ├── [if SHOW_HIGHLOW] "High/Low:80/65 F "
│   │   ├── [if SHOW_CONDITION] "CLEAR  "
│   │   ├── [if SHOW_HUMIDITY] "Humidity:42%  "
│   │   ├── [if SHOW_WIND] "Wind: SW 12 mph  "
│   │   ├── [if SHOW_PRESSURE] "Pressure:30inHg  "
│   │   └── bdayClient.getMessage(bdayMessageIndex) + " "
│   │       └── Cycles through messages[0..numMessages-1], wraps to 0
│   │
│   └── scrollMessageWait(msg)       // Scroll the assembled message
```

**`displayRefreshCount`** starts at 1 and is reloaded to `minutesBetweenScrolling`
after each scroll. So if `minutesBetweenScrolling = 1`, the ticker scrolls every minute.
If `minutesBetweenScrolling = 3`, it scrolls every 3 minutes.

---

## Data Fetch (`getWeatherData()`)

**File:** `marquee/marquee.ino:760`

This is the main orchestrator for all external data. It runs synchronously (blocking).

```text
getWeatherData()
│
├── Flash status LED on
├── Show current time or "..." on display
├── Draw 3 "loading" pixels on left edge of display
├── matrix.write()
│
├── weatherClient.updateWeather()    // HTTP GET to openweathermap.org
│   └── On error: display error message, return
│
├── set_timeZoneSec(weatherClient.getTimeZoneSeconds())
│   └── Updates timeZoneSec in timeNTP.cpp
│   └── If timezone changed: setSyncProvider(NULL) to stop auto sync
│
├── Update "loading" pixels (3 more)
│
├── getNtpTime()                     // UDP to 1.pool.ntp.org
│   └── Returns Unix time (adjusted for timezone)
├── if (t > TIME_VALID_MIN=2025-01-01):
│   └── setTime(t)                   // Set TimeLib clock
├── Update firstTimeSync if this is first successful sync
│
├── bdayClient.updateData(devInfo)    // HTTPS GET to WAGFAM_DATA_URL
│   └── Appends heartbeat params via buildHeartbeatQuery():
│       chip_id, version, uptime, heap, rssi, utc_offset_sec, lan_ip, mdns_name
│   └── Streams JSON through streaming parser
│   └── Fills messages[] and returns configValues
│
├── Apply remote config if received:
│   ├── WAGFAM_DATA_URL updated?  → save + set lastRefreshDataTimestamp=0
│   ├── WAGFAM_API_KEY updated?   → save + set lastRefreshDataTimestamp=0
│   ├── WAGFAM_EVENT_TODAY updated? → save
│   └── DEVICE_NAME updated?      → save
│
├── Auto-OTA check:
│   └── If latestVersion != VERSION and firmwareUrl is trusted → performAutoUpdate()
│
├── lastRefreshDataTimestamp = now()
│
└── Flash status LED off
```

---

## Weather Client

**File:** `marquee/OpenWeatherMapClient.cpp:91`

`OpenWeatherMapClient::updateWeather()` makes a raw HTTP/1.1 request:

```text
updateWeather()
│
├── Validate API key and geo location type
├── Build GET request string:
│   └── "GET /data/2.5/weather?id=XXXX&units=imperial&APPID=KEY HTTP/1.1"
│   └── (or ?lat=X&lon=Y  or ?q=CityName)
│
├── WiFiClient.connect("api.openweathermap.org", 80)
├── Send GET + Host + User-Agent + Connection headers
├── Wait up to 2000ms for response data
├── Read and check HTTP status line ("HTTP/1.1 200 OK")
├── Skip to end of HTTP headers ("\r\n\r\n")
│
├── deserializeJson(jdoc, weatherClient)  // ArduinoJson v7
│   └── Reads directly from TCP stream
│
├── Sanity check: measureJson(jdoc) > 150  // Detect truncated response
│   ⚠️ BUG: operator precedence error here (see CODE_REVIEW.md)
│
├── Extract fields from JSON:
│   city, country, temp, humidity, wind, pressure, conditions,
│   weatherId, icon, high/low, timezone, sunrise, sunset
│
├── Convert units if needed:
│   ├── Metric: wind m/s → km/h  (× 3.6)
│   └── Imperial: pressure hPa → inHg  (× 0.02953)
│
└── weather.isValid = true
```

**Error handling:** On connection failure or parse error, `dataGetRetryCount` is
incremented. After `dataGetRetryCountError` (10) consecutive failures, `weather.isValid`
is set to `false` and the scroll ticker shows the error message instead.

---

## Calendar Client

**File:** `marquee/WagFamBdayClient.cpp:38`

`WagFamBdayClient::updateData()` uses HTTPS with a streaming JSON parser:

```text
updateData()
│
├── Create BearSSL::WiFiClientSecure with setInsecure()
├── HTTPClient.begin(*client, myJsonSourceUrl)
├── Add "Authorization: token KEY" header (if key configured)
├── HTTPClient.GET()
│
├── On HTTP_CODE_OK:
│   ├── Read response in 128-byte chunks
│   ├── Feed each byte to JsonStreamingParser
│   │   └── Parser calls back to startDocument/key/value/endObject/etc.
│   └── Process continues until response drained
│
└── Return currentConfig (updated during parsing if config block found)
```

### JSON Parser Callbacks

The `WagFamBdayClient` implements `JsonListener`:

```text
startDocument() → messageCounter = 0, inConfig = false

key(k)          → currentKey = k

startObject()   → if currentKey == "config": inConfig = true

value(v)
├── if inConfig:
│   ├── "dataSourceUrl" → currentConfig.dataSourceUrl = v
│   ├── "apiKey"        → currentConfig.apiKey = v
│   └── "eventToday"    → currentConfig.eventToday = v.toInt()
└── if currentKey == "message" (and not in config):
    └── messages[messageCounter++] = cleanText(v)

endObject()     → if inConfig: inConfig = false
```

### `cleanText()`

Replaces Unicode lookalike characters (curly quotes, accented letters, em-dash, etc.)
with their ASCII equivalents so they render correctly on the 7-segment-style LED font.
This is a brute-force series of ~35 `String::replace()` calls.

---

## Display: Frame Rendering (`centerPrint()`)

**File:** `marquee/marquee.ino:1226`

```text
centerPrint(msg, extraStuff)
│
├── x = (matrix.width() - (msg.length() * width)) / 2
│   └── Centers the text horizontally
│
├── if extraStuff:
│   │
│   ├── if WAGFAM_EVENT_TODAY:
│   │   └── Animated marching-dot border:
│   │       ├── dotPos = (millis() % (SPACING * SPEED_MS)) / SPEED_MS
│   │       └── For each perimeter position i (0..height*2+width-3):
│   │           ├── if (i % SPACING) == dotPos:
│   │           │   ├── i < height         → left column pixel
│   │           │   ├── i < height+width-1 → bottom row pixel
│   │           │   └── else               → right column pixel
│   │
│   └── if !IS_24HOUR AND IS_PM AND isPM():
│       └── matrix.drawPixel(matrix.width()-1, 6, HIGH)  // PM indicator
│
├── matrix.setCursor(x, 0)
├── matrix.print(msg)               // Draws chars to framebuffer
└── matrix.write()                  // SPI flush to MAX7219 hardware
```

The display is a 32×8 grid. `matrix.width()` = 32, `matrix.height()` = 8.
Text is drawn at `y=0` (top of the 8-pixel-tall display).

---

## Display: Scroll Ticker (`scrollMessageWait()`)

**File:** `marquee/marquee.ino:1240`

```text
scrollMessageWait(msg)
│
└── for i = 0 to (width * msg.length() + matrix.width() - 1 - spacer):
    │
    ├── matrix.fillScreen(LOW)         // Clear (AsyncWebServer runs in background)
    │
    ├── letter = i / width             // Which character is at the left edge
    ├── x = (matrix.width() - 1) - (i % width)  // Pixel offset within that character
    │
    ├── while (x + width - spacer >= 0 AND letter >= 0):
    │   ├── if letter < msg.length():
    │   │   └── matrix.drawChar(x, y, msg[letter], HIGH, LOW, 1)
    │   ├── letter--
    │   └── x -= width
    │
    ├── matrix.write()
    └── delay(displayScrollSpeed)      // 25ms per pixel step = normal speed
```

The inner `while` loop draws multiple characters per frame — the ones currently
visible in the 32-pixel-wide window.

**Total duration** for a message of N characters:
`(6 * N + 32 - 2) * displayScrollSpeed` milliseconds.
At 25ms/step, a 30-character message takes about (180 + 30) * 25ms ≈ 5.25 seconds.

---

## Config: Save and Load

### `savePersistentConfig()` — `marquee.ino:1067`

1. Open `/conf.txt` for writing (overwrites entire file)
2. Write all settings as `KEY=VALUE\n` lines
3. Call `readPersistentConfig()` (re-reads and re-applies the values just written)

### `readPersistentConfig()` — `marquee.ino:1108`

1. If `/conf.txt` doesn't exist: call `savePersistentConfig()` (creates it) and return
2. Read line by line using `fr.readStringUntil('\n')`
3. For each line, use `line.indexOf("KEY=")` to identify the setting
4. Extract value as `line.substring(line.lastIndexOf("KEY=") + KEY_LEN)`
5. After all lines: apply settings to clients (`matrix`, `weatherClient`, `bdayClient`)

**Why `lastIndexOf` instead of `indexOf`?** As a guard against a value that happens to
contain the key string (e.g. if a URL contains `"APIKEY="`). However, there are still
edge cases — see `CODE_REVIEW.md`.

---

## Web Handlers

The legacy server-rendered handlers (`handleSaveConfig`, `handleConfigure`,
`handlePull`, `handleSystemReset`, `handleForgetWifi`, `displayHomePage`,
`sendHeader`, `sendFooter`) were removed in Phase D (PR #80). The legacy
paths now 302-redirect to `/spa/` via `redirectToSpa()`. Their replacements
in the SPA call REST API endpoints — see [`README.md` — REST API](../README.md#rest-api)
for the full table.

### `handleApiConfigPost()` — body-bearing JSON

1. Wired through `AsyncCallbackJsonWebHandler` so the body is parsed by the framework
2. Iterate the JSON object, applying any recognised key to its global setting variable
3. Call `bdayClient.updateBdayClient()`, `weatherClient.setMetric()`,
   `weatherClient.setGeoLocation()` as needed
4. Call `savePersistentConfig()` (write to file)
5. Set `weatherRefreshRequested = true` (deferred — `getWeatherData()` runs from `loop()`)
6. Respond with the updated config as JSON

### `handleUpdateFromUrl()` — `marquee.ino:747`

1. Validate URL starts with `http://` (HTTPS not supported)
2. Validate URL against the firmware-domain allowlist (`isTrustedFirmwareDomain`)
3. Stash the URL in `pendingOtaUrl`, set `otaFromUrlRequested = true`,
   respond with the "STARTING UPDATE" page (the actual flash is deferred —
   `doOtaFlash()` runs from `loop()` because `ESPhttpUpdate.update()` would
   block the async event loop for 20–30s)
4. From the deferred path: `doOtaFlash()` writes the rollback record, then
   calls `ESPhttpUpdate.update(client, firmwareUrl)` — device reboots on success

---

## Time Sync Flow

```text
timeNTPsetup()                       // Called once in setup()
├── Udp.begin(8888)
└── setSyncProvider(getNtpTime)      // TimeLib will call getNtpTime every 20s
    setSyncInterval(20)

[First getWeatherData() call]
├── set_timeZoneSec(tzOffsetSeconds)
│   ├── timeZoneSec = tzOffsetSeconds (in timeNTP.cpp)
│   └── setSyncProvider(getNtpTime)  // Reinit with new timezone
├── setSyncProvider(NULL)             // Disable auto-sync temporarily
├── t = getNtpTime()                 // Explicit UDP NTP call
└── if t valid: setTime(t)

[firstTimeSync set]
└── setSyncInterval(222)             // Auto-sync every 222 seconds going forward

[Each subsequent getWeatherData()]
├── set_timeZoneSec() may update if DST changes
└── Explicit getNtpTime() + setTime()
```

**Key point:** The timezone offset comes from OpenWeatherMap, not from a timezone
database or user config. OWM returns the current UTC offset including DST, so DST
transitions are handled automatically after the next weather refresh.

---

## Appendix: Variable Quick Reference

| Where to look | What you'll find |
| ---------------- | ----------------- |
| `marquee.ino:95–189` | All global settings variables (APIKEY, geoLocation, IS_*, SHOW_*, security, OTA) |
| `marquee.ino:133–179` | All PROGMEM HTML string constants |
| `Settings.h` | Library includes and compile-time hardware defaults |
| `WagFamBdayClient.h` | `DeviceInfo` struct (heartbeat telemetry) + `buildHeartbeatQuery()` inline |
| `WagFamBdayClient.h` | `configValues` struct returned by `updateData()` |
| `OpenWeatherMapClient.h:31–55` | `weather` struct (all weather fields) |
