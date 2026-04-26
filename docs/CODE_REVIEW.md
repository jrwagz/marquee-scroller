# WagFam CalClock — Expert Code Review

> **Reviewer persona:** Senior C++ developer with extensive ESP8266/embedded experience.
> **Date:** April 2026 (against v3.07.0-wagfam)
>
> Issues are grouped by severity. Each entry includes the file/line, a description,
> and a recommended fix.

---

## Summary

The codebase is functional and well-adapted for its purpose. The upstream Qrome base
was cleanly gutted of unused features (news, OctoPrint, Pi-hole) and replaced with
the `WagFamBdayClient`. The more recent commits by `rob040` substantially improved the
weather client and NTP code. Overall code quality is **good for the platform**, but
there are several bugs, memory risks, and accumulated cruft worth addressing.

---

## 🔴 Critical Bugs

### 1. Operator Precedence Bug in `OpenWeatherMapClient.cpp`

**File:** `marquee/OpenWeatherMapClient.cpp:199`

```cpp
// BUGGY:
if (int len = measureJson(jdoc) <= 150) {
```

Due to C++ operator precedence, `<=` binds tighter than `=`. This parses as:

```cpp
if (int len = (measureJson(jdoc) <= 150)) {
```

So `len` is set to `0` (false) or `1` (true), not to the actual JSON size.
The sanity check is effectively checking "is `len` non-zero (i.e. is JSON size ≤ 150)?"
which is the **opposite** of the intended behavior. Short/error responses slip through.
The error message extraction `jdoc[F("message")]` also runs even on valid responses
when JSON size > 150 (because `len = 1` which is truthy).

**Fix:** Use C++17 `if`-with-initializer:

```cpp
if (int len = (int)measureJson(jdoc); len <= 150) {
    Serial.println(F("Error incomplete message, size ") + String(len));
    ...
}
```

Or simply:

```cpp
size_t jsonLen = measureJson(jdoc);
if (jsonLen <= 150) {
    ...
}
```

---

### 2. `getWindDirectionText()` — Stack Allocation of 16 `String` Objects

**File:** `marquee/OpenWeatherMapClient.cpp:267`

```cpp
String arr[] = {"N", "NNE", "NE", "ENE", "E", ...};  // 16 String objects
return arr[(val % 16)];
```

Allocating 16 `String` objects on the stack is dangerous on ESP8266. Each Arduino
`String` is at minimum 12 bytes of struct + the heap allocation for the content.
This creates 16 heap allocations + stack pressure on every call. On a 4KB stack,
this is risky and contributes to heap fragmentation.

**Fix:** Use a `PROGMEM` char array and index directly:

```cpp
String OpenWeatherMapClient::getWindDirectionText() {
  static const char* const dirs[] PROGMEM = {
    "N","NNE","NE","ENE","E","ESE","SE","SSE",
    "S","SSW","SW","WSW","W","WNW","NW","NNW"
  };
  int val = floor((weather.windDirection / 22.5) + 0.5);
  return String(dirs[val % 16]);
}
```

---

## 🟠 High-Priority Issues

### 3. `WagFamBdayClient::cleanText()` — Heap Fragmentation Bomb

**File:** `marquee/WagFamBdayClient.cpp:194–237`

`cleanText()` performs **35+ sequential `String::replace()` calls** on the same
`String`. Each `replace()` may resize the string, triggering heap realloc. On a string
that needs many substitutions, this creates a chain of tiny reallocs that fragment the
ESP8266's heap. Over time, this can lead to failed allocations and crashes.

The function also allocates the input string by value (`String text`), so it starts
with an extra copy.

**Fix:** Use a lookup table approach with a single-pass output buffer:

```cpp
String WagFamBdayClient::cleanText(const String &text) {
  String out;
  out.reserve(text.length());
  for (int i = 0; i < (int)text.length(); i++) {
    unsigned char c = (unsigned char)text[i];
    // Handle ASCII directly
    if (c < 0x80) { out += (char)c; continue; }
    // Handle UTF-8 multi-byte sequences as known replacements
    // ... (map UTF-8 sequences to ASCII equivalents)
  }
  return out;
}
```

Or at minimum, pre-reserve the string to avoid repeated reallocs:

```cpp
String WagFamBdayClient::cleanText(String text) {
  text.reserve(text.length() + 64);  // Reserve extra room for replacements
  text.replace(...);
  // ...
}
```

---

### 4. `savePersistentConfig()` / `readPersistentConfig()` Mutual Recursion

**File:** `marquee/marquee.ino:877,908`

`savePersistentConfig()` always calls `readPersistentConfig()` at the end.
`readPersistentConfig()` calls `savePersistentConfig()` if the file doesn't exist.

```
savePersistentConfig()
└── at end: readPersistentConfig()   ← always called

readPersistentConfig()
└── if no file: savePersistentConfig() ← bootstrap path
    └── which calls readPersistentConfig() again ← one extra level
```

This is not infinite recursion (the second call to `readPersistentConfig` finds the
file and exits normally), but it is confusing and wastes stack space. If the filesystem
write in `savePersistentConfig()` fails silently (e.g., filesystem full), the bootstrap
path creates an infinite loop.

**Fix:** Remove the `readPersistentConfig()` call from the end of `savePersistentConfig()`.
The caller (e.g., `handleSaveConfig()`) already applies settings directly before calling
`savePersistentConfig()`. If re-application is needed, extract it into a separate
`applyConfig()` function.

---

### 5. Config Parsing Fragility — `lastIndexOf` Doesn't Fully Protect Against Collisions

**File:** `marquee/marquee.ino:908–1008`

The config parser uses `line.indexOf("KEY=")` to match a line, then
`line.lastIndexOf("KEY=")` to find where the value starts. While `lastIndexOf` helps
when the value contains the key string, this parser can still misbehave:

- If a URL value contains another key's name (e.g., `WAGFAM_DATA_URL` contains
  `"is24hour=1"` as a query parameter), that line will match the `is24hour` check
  and corrupt `IS_24HOUR`.
- The matching is order-independent (no `else if`), so every line is tested against
  every key.

**Fix:** Parse each line by splitting on the **first** `=` only:

```cpp
int eqPos = line.indexOf('=');
if (eqPos <= 0) continue;
String key = line.substring(0, eqPos);
String value = line.substring(eqPos + 1);
value.trim();

if (key == "WAGFAM_DATA_URL") { WAGFAM_DATA_URL = value; }
else if (key == "APIKEY") { APIKEY = value; }
// ... etc.
```

This is the standard key=value parsing approach and has no collision risk.

---

### 6. Debug `Serial.println` Always Fires in `WagFamBdayClient::value()`

**File:** `marquee/WagFamBdayClient.cpp:170`

```cpp
void WagFamBdayClient::value(String value) {
  ...
  Serial.println(currentKey + "=" + value);  // Always prints!
}
```

This prints **every** parsed JSON key-value pair to Serial, including the API key and
data source URL if they're in the config block. This is a debugging artifact that:

- Leaks credentials to anyone with Serial access
- Generates unnecessary `String` allocations (fragmentation) on every JSON value
- Produces large amounts of noise in the Serial output

**Fix:** Remove the line or guard it with `#ifdef DEBUG`.

---

## 🟡 Medium-Priority Issues

### 7. `sendHeader()` / `displayHomePage()` — Heap Fragmentation via String Concatenation

**File:** `marquee/marquee.ino:697, 741`

Both functions build HTML by repeatedly appending to a `String` variable:

```cpp
html += "<div class='w3-cell-row'...>";
html += "<img src='...>" ;
// ... dozens more +=
```

Each `+=` that exceeds the current allocation triggers a `realloc()`. On a heap with
~50KB available after startup, repeatedly growing strings causes fragmentation.

The existing pattern of using `server.sendContent(html)` then `html = ""` (flushing and
resetting every few lines) helps, but within each chunk, many `+=` operations still occur.

**Fix:** Use `server.sendContent(F("literal"))` for every static string directly instead
of accumulating into a `String`. For dynamic content, use `String(value)` inline or
`sprintf_P` into a small char buffer.

---

### 8. `displayHomePage()` — OWM Weather Icon via HTTP (Mixed Content)

**File:** `marquee/marquee.ino:792`

```cpp
html += "<img src='http://openweathermap.org/img/w/" + weatherClient.getIcon() + ".png'...>";
```

Also in `sendHeader()`:

```cpp
html += "<div class='w3-left'><img src='http://openweathermap.org/img/w/..." ...>";
```

Both use `http://` for the OWM weather icon. The web page itself is served over HTTP,
so this isn't a mixed-content issue in the strict sense, but it's worth noting that
OWM's icon CDN has moved to HTTPS. Modern browsers may block HTTP images.

**Fix:** Change `http://` to `https://` for OWM icon URLs. (Served by the browser, so
no ESP8266 TLS overhead is involved.)

---

### 9. `handleSaveConfig()` — No Input Validation / Bounds Checking

**File:** `marquee/marquee.ino:378`

```cpp
displayIntensity = server.arg("ledintensity").toInt();      // Range: 0-15
minutesBetweenDataRefresh = server.arg("refresh").toInt();  // Must be > 0
displayScrollSpeed = server.arg("scrollspeed").toInt();     // Must be > 0
minutesBetweenScrolling = server.arg("refreshDisplay").toInt();
```

`toInt()` returns 0 for non-numeric input. If `minutesBetweenDataRefresh` becomes 0,
the device will attempt to refresh data on every single second, hammering the OWM API
and likely causing WDT resets or rate-limit bans. Similarly, `displayScrollSpeed = 0`
causes a `delay(0)` in the scroll loop, which isn't harmful but is unintended.

`readPersistentConfig()` has a check for `minutesBetweenDataRefresh == 0` (fixes it to 15),
but `handleSaveConfig()` doesn't apply the same guard.

**Fix:** Add bounds clamping:

```cpp
displayIntensity = constrain(server.arg("ledintensity").toInt(), 0, 15);
minutesBetweenDataRefresh = max(1, server.arg("refresh").toInt());
displayScrollSpeed = max(1, server.arg("scrollspeed").toInt());
minutesBetweenScrolling = max(1, server.arg("refreshDisplay").toInt());
```

---

### 10. Config Form Uses GET with API Keys in URL

**File:** `marquee/marquee.ino:113`

```html
<form class='w3-container' action='/saveconfig' method='get'>
```

API keys are submitted as GET parameters, appearing in:

- Browser address bar
- Browser history
- Router/firewall logs
- Any web proxy logs

For a home device on a private LAN, this is low-risk, but it's a bad habit.

**Fix:** Change `method='get'` to `method='post'` and update `handleSaveConfig()` to
use `server.arg()` (which works for both GET and POST in `ESP8266WebServer`).

---

### 11. `displayTime` Rebuilt Every Frame Unnecessarily

**File:** `marquee/marquee.ino:281`

```cpp
displayTime = hourMinutes(false);  // Called every loop iteration
```

`hourMinutes()` constructs a new `String` on every frame. Since the time only changes
once per second, this creates ~1000 unnecessary `String` allocations per second.

**Fix:** Rebuild `displayTime` only in `processEverySecond()`:

```cpp
void processEverySecond() {
  displayTime = hourMinutes(false);
  ...
}
```

And in `loop()`, just use `displayTime` directly (already done — just stop calling
`hourMinutes` in `loop()`).

---

### 12. `firstTimeSync` and `lastRefreshDataTimestamp` — Type Truncation

**File:** `marquee/marquee.ino:79–80`

```cpp
uint32_t firstTimeSync;
uint32_t lastRefreshDataTimestamp;
```

`now()` returns `time_t` which is **64-bit** on ESP8266 (as noted in `timeNTP.cpp`).
Storing it in `uint32_t` truncates the upper 32 bits. This is fine until 2038 (when
Unix time exceeds 32-bit range), but the comparison `t > TIME_VALID_MIN` with a
64-bit `t` and 32-bit truncated values could produce unexpected results if `t` is
ever negative or very large.

**Fix:** Change to `time_t` or `uint64_t` for correctness, or add a comment explaining
the intentional truncation and why it's safe for this use case.

---

### 13. `centerPrint()` Redraws Display Every Frame Even When Unchanged

**File:** `marquee/marquee.ino:1038`

`matrix.write()` does a full SPI DMA transfer (32×8 = 256 bits across 4 MAX7219 chips)
on every single frame. The content only changes once per second (when the colon
flashes). This wastes SPI bandwidth and CPU.

**Fix:** Track a "dirty" flag. Only call `matrix.write()` when the content changes:

```cpp
bool displayDirty = true;

void loop() {
  String newTime = hourMinutes(false);
  if (newTime != displayTime || WAGFAM_EVENT_TODAY) {
    displayTime = newTime;
    matrix.fillScreen(LOW);
    centerPrint(displayTime, true);
    displayDirty = false;
  }
  server.handleClient();
  ArduinoOTA.handle();
}
```

(The animated event border does require redrawing every frame, so `WAGFAM_EVENT_TODAY`
would always be dirty when active — but non-event days get a huge reduction in SPI
traffic.)

---

### 14. `processEveryMinute()` Double-Calls `matrix.shutdown(false)` / `fillScreen()`

**File:** `marquee/marquee.ino:298`

```cpp
matrix.shutdown(false);
matrix.fillScreen(LOW);
```

`matrix.shutdown(false)` is called unconditionally every minute even though the display
was never shut down (no code calls `matrix.shutdown(true)`). This is a no-op but implies
there was once a display sleep feature that was removed. The leftover call can be removed.

---

## 🟢 Low-Priority / Cleanup

### 15. `sources.json` — Dead File from Upstream

**File:** `sources.json` (root of repo)

This is a large (~42KB) JSON file listing hundreds of news sources. It is a leftover
from the upstream Qrome repo's NewsAPI feature, which was removed in this fork.
It has no function in the current firmware. Delete it.

---

### 16. Stale Pre-built Binaries in Root

**Files:** `marquee.ino.d1_mini_3.03.bin`, `marquee.ino.d1_mini_wide_3.03.bin`

These are compiled binaries for v3.03 (the current source is v3.07.0-wagfam).
They are out-of-date and will cause confusion. The CI pipeline generates fresh
binaries as artifacts on every build.

**Recommendation:** Delete both `.bin` files from the root of the repo. The CI
artifact output at `artifacts/` is the right place for distributable binaries.

---

### 17. CI Workflow Uses Non-Existent Action Versions

**File:** `.github/workflows/lint-test-build.yaml`

```yaml
uses: actions/checkout@v6       # Does not exist; current stable is @v4
uses: actions/upload-artifact@v7 # Does not exist; current stable is @v4
uses: actions/attest@v4          # May not exist; check GitHub Marketplace
```

These will fail as soon as GitHub Actions tries to resolve them. The current stable
major versions are `actions/checkout@v4` and `actions/upload-artifact@v4`.

**Fix:** Update to `@v4` for both.

---

### 18. `todayDisplayMilliSecond` / `todayDisplayStartingLED` — Wrong Types

**File:** `marquee/marquee.ino:87–88`

```cpp
int todayDisplayMilliSecond = 0;
int todayDisplayStartingLED = 0;
```

Used as:

```cpp
todayDisplayMilliSecond = millis() % (TODAY_DISPLAY_DOT_SPACING * TODAY_DISPLAY_DOT_SPEED_MS);
```

`millis()` returns `uint32_t`. The modulo result is always non-negative, but `int` is
signed. On systems where `int` is 16-bit (not ESP8266, but worth noting), this would
overflow. More practically, if the modulo expression yields a value > `INT_MAX`, the
cast to `int` is undefined behavior.

**Fix:** Change to `uint32_t`.

---

### 19. `NTP setSyncInterval(20)` — Aggressive Initial Sync Rate

**File:** `marquee/timeNTP.cpp:47`

```cpp
setSyncInterval(20);
```

This asks TimeLib to call `getNtpTime()` every 20 seconds until time is valid. NTP pool
servers have rate limits (generally 1 request per 4 seconds per server is acceptable,
but even that can get you rate-limited). 20-second polling is fine, but combining it
with the explicit `getNtpTime()` calls in `getWeatherData()` means the device may be
hitting the NTP server every 20 seconds continuously until the weather refresh interval.

**Recommendation:** Increase the initial sync interval to 60 seconds, or add a guard
in `getNtpTime()` to skip if a sync was done recently.

---

### 20. `getTimeTillUpdate()` Uses `sprintf_P` Unnecessarily on ESP8266

**File:** `marquee/marquee.ino:860`

```cpp
char hms[10];
sprintf_P(hms, PSTR("%d:%02d:%02d"), hours, minutes, seconds);
return String(hms);
```

`sprintf_P` + `PSTR` is the correct pattern on AVR (where PROGMEM is in a separate
address space). On ESP8266, `PROGMEM` is mapped into the normal address space and
`sprintf_P` is equivalent to `sprintf`. There is no harm, but it's slightly misleading.
The pattern is correct and can be left as-is; this is purely cosmetic.

---

### 21. `WagFamBdayClient.h` — `typedef struct` Inside Class

**File:** `marquee/WagFamBdayClient.h:34`

```cpp
typedef struct {
  boolean dataSourceUrlValid;
  ...
} configValues;
```

`typedef struct { } name;` is a C idiom. In C++, `struct name { };` is idiomatic and
equivalent. The `typedef` is harmless but unnecessary in C++.

---

### 22. `handleUpdateFromUrl()` — No Post-Failure UI Recovery

**File:** `marquee/marquee.ino:530`

If `ESPhttpUpdate.update()` fails, the function logs the error to Serial, but the HTTP
response was already sent and the connection closed before the update was attempted
(`server.sendContent(""); server.client().stop()`). The user sees "STARTING UPDATE" and
then nothing — the page doesn't reload and there's no error visible in the browser.

**Fix:** Since the response is already committed, the user would need to be instructed
to navigate to `/` or `/configure` manually. Consider displaying the matrix-scrolled
error message (which already happens via `scrollMessageWait`) or adding a meta-refresh
in the HTML that redirects to `/` after 5 seconds.

---

### 23. Missing `getWifiQuality()` Prototype

**File:** `marquee/marquee.ino`

`getWifiQuality()` is called in `setup()` (line ~215) but its forward declaration is not
in the prototype list at lines 36–65. The Arduino build system auto-generates prototypes,
so this compiles fine, but it's inconsistent with the rest of the declared prototypes.

---

### 24. Web UI Loads Resources from External CDNs

**File:** `marquee/marquee.ino:697`

```html
<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>
<link rel='stylesheet' href='https://www.w3schools.com/lib/w3-theme-blue-grey.css'>
<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.8.1/css/all.min.css'>
```

The web UI requires outbound internet access to load CSS and icons. If the device is on
an isolated network or CDN has an outage, the UI will appear unstyled. This is a
common embedded web UI trade-off (avoiding storing CSS on the device), but worth noting.

---

## Positive Observations

These are things done well that should be preserved:

- **`F()` / `FPSTR()` usage:** PROGMEM string constants for all web form HTML is
  correct and important for the ESP8266 memory budget.

- **Chunked HTTP responses:** Using `setContentLength(CONTENT_LENGTH_UNKNOWN)` +
  multiple `sendContent()` calls avoids holding a giant HTML string in RAM.

- **Streaming JSON parser for calendar:** Using `JsonStreamingParser` instead of
  `ArduinoJson` for the calendar client avoids a full JSON doc allocation for a
  potentially large payload.

- **Remote config push:** The ability for the server to update `dataSourceUrl`,
  `apiKey`, and `eventToday` without a device reflash is clever and practical.

- **`EncodeUrlSpecialChars()`:** Correctly URL-encodes city names for the OWM API
  without using a third-party library.

- **Event-day animated border:** The marching-dot animation using `millis()` modulo
  is frame-rate independent and correct.

- **Timezone from OWM:** Using the OWM API response's `timezone` field (UTC offset
  in seconds including DST) is more reliable than querying a separate timezone DB.

- **`WAGFAM_EVENT_TODAY` not user-configurable:** Correct — this value is ephemeral
  and should only come from the authoritative server.

---

## Recommended Priority Order for Fixes

| # | Issue | Effort | Impact |
|---|-------|--------|--------|
| 1 | Operator precedence bug in OWM client | Low | High |
| 3 | `cleanText()` heap fragmentation | Medium | High |
| 4 | `savePersistentConfig` recursion | Low | Medium |
| 5 | Config parsing fragility | Low | Medium |
| 6 | Debug Serial.println in value() | Low | Medium |
| 9 | Input validation in handleSaveConfig | Low | Medium |
| 2 | `getWindDirectionText()` stack alloc | Low | Medium |
| 17 | CI action versions | Low | High |
| 15 | Delete sources.json | Trivial | Low |
| 11 | `displayTime` rebuilt every frame | Low | Low |
| 13 | Dirty flag for matrix.write() | Medium | Low |
| 16 | Delete stale .bin files | Trivial | Low |
