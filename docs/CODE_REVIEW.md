# WagFam CalClock — Open Issues

> **Note:** This is a living list of known issues that have not yet been resolved.
> Fixed items have been removed. See git history for the original full review.

---

## 🟠 High-Priority

### 1. `WagFamBdayClient::cleanText()` — Heap Fragmentation

**File:** `marquee/WagFamBdayClient.cpp`

35+ sequential `String::replace()` calls each potentially trigger a heap realloc.
`text.reserve(text.length() + 64)` at the top helps; a single-pass UTF-8 scan with a
pre-allocated output buffer would eliminate the fragmentation entirely.

---

## 🟢 Low-Priority / Cleanup

### 2. `displayTime` Rebuilt Every Frame

**File:** `marquee/marquee.ino`

`hourMinutes()` allocates a new `String` every loop iteration (~thousands/sec). Move
the call into `processEverySecond()` — time only changes once per second anyway.
(Partially mitigated by the `displayDirty` short-circuit in `loop()`, but the string
is still rebuilt before the dirty check.)

---

### 3. `firstTimeSync` / `lastRefreshDataTimestamp` — `uint32_t` vs `time_t`

**File:** `marquee/marquee.ino`

`now()` returns 64-bit `time_t` on ESP8266; storing it in `uint32_t` truncates it.
Safe until 2038, but worth a comment or a type change to `time_t`.
