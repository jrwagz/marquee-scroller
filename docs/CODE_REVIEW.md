# WagFam CalClock — Open Issues

> **Note:** This is a living list of known issues that have not yet been resolved.
> Fixed items have been removed. See git history for the original full review.

---

## 🟠 High-Priority

### 1. `getWindDirectionText()` — 16 `String` Objects on the Stack

**File:** `marquee/OpenWeatherMapClient.cpp:267`

```cpp
String arr[] = {"N", "NNE", "NE", "ENE", "E", ...};  // 16 String objects
return arr[(val % 16)];
```

16 heap allocations + stack pressure on every call to `getWindDirectionText()`. Use a
`static const char* const dirs[] PROGMEM` array instead and index directly.

---

### 2. `WagFamBdayClient::cleanText()` — Heap Fragmentation

**File:** `marquee/WagFamBdayClient.cpp`

35+ sequential `String::replace()` calls each potentially trigger a heap realloc.
`text.reserve(text.length() + 64)` at the top helps; a single-pass UTF-8 scan with a
pre-allocated output buffer would eliminate the fragmentation entirely.

---

### 3. `savePersistentConfig()` / `readPersistentConfig()` — Mutual Recursion

**File:** `marquee/marquee.ino`

`savePersistentConfig()` always calls `readPersistentConfig()` at the end, which
calls `savePersistentConfig()` on first boot (no file yet). Not infinite, but wasteful
and fragile if the write fails silently. Remove the `readPersistentConfig()` tail-call.

---

### 4. Config Parsing — Key Collision Risk

**File:** `marquee/marquee.ino`

The parser uses `line.indexOf("KEY=")` with no `else if` chain, so every line is
tested against every key. A URL value containing another key name (e.g.,
`?is24hour=1` in `WAGFAM_DATA_URL`) silently corrupts that setting. Fix: split each
line on the first `=` only and use an `if/else if` chain.

---

## 🟡 Medium-Priority

### 5. HTML Page Builders — String Concatenation Fragmentation

**File:** `marquee/marquee.ino` (`sendHeader`, `displayHomePage`, `handleConfigure`)

Dozens of `html +=` calls per page build grow the `String` repeatedly. Replace static
fragments with direct `server.sendContent(F("..."))` calls.

---

### 6. OWM Weather Icon Uses `http://`

**File:** `marquee/marquee.ino`

`<img src='http://openweathermap.org/img/w/...'>` — OWM's CDN now serves icons over
HTTPS. Modern browsers may block the HTTP image. Change to `https://`.

---

### 7. Config Form Submits API Keys via GET

**File:** `marquee/marquee.ino`

`<form method='get'>` exposes API keys in the browser address bar and server logs.
Change to `method='post'`; `server.arg()` works transparently for both.

---

## 🟢 Low-Priority / Cleanup

### 8. `displayTime` Rebuilt Every Frame

**File:** `marquee/marquee.ino`

`hourMinutes()` allocates a new `String` every loop iteration (~thousands/sec). Move
the call into `processEverySecond()` — time only changes once per second anyway.

---

### 9. `firstTimeSync` / `lastRefreshDataTimestamp` — `uint32_t` vs `time_t`

**File:** `marquee/marquee.ino`

`now()` returns 64-bit `time_t` on ESP8266; storing it in `uint32_t` truncates it.
Safe until 2038, but worth a comment or a type change to `time_t`.

---

### 10. `centerPrint()` Rewrites Display Every Frame

**File:** `marquee/marquee.ino`

`matrix.write()` sends 256 bits over SPI on every loop iteration even when nothing
changed. Add a dirty flag; skip the write unless the content changed (or event-day
animation is running).

---

### 11. `matrix.shutdown(false)` Called Every Minute Unnecessarily

**File:** `marquee/marquee.ino`

`processEveryMinute()` calls `matrix.shutdown(false)` even though `shutdown(true)` is
never called. This is a leftover from a removed display-sleep feature. Remove it.

---

### 12. `todayDisplayMilliSecond` / `todayDisplayStartingLED` — `int` vs `uint32_t`

**File:** `marquee/marquee.ino`

Both are assigned from `millis() % (...)` which returns `uint32_t`. Signed `int` is
technically UB if the result exceeds `INT_MAX`. Change to `uint32_t`.

---

### 13. `sources.json` — Dead File

**File:** `sources.json`

~42 KB leftover from the upstream NewsAPI feature (removed in this fork). Delete it.

---

### 14. Stale Pre-built Binaries in Root

**Files:** `marquee.ino.d1_mini_3.03.bin`, `marquee.ino.d1_mini_wide_3.03.bin`

Built from v3.03; current source is v3.08.0-wagfam. CI generates fresh artifacts.
Delete both files from the root.
