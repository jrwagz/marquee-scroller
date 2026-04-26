# RAM & Heap Resource Analysis

> **Platform:** Wemos D1 Mini (ESP8266) — v3.07.0-wagfam
>
> This document identifies the most RAM-intensive features and flows in the firmware,
> ranks them by impact, and recommends specific reductions to open headroom for new features.

---

## The Memory Budget

The ESP8266 has a hard ceiling of 80KB of DRAM, shared between the heap, the call stack, and
the BSS/data segments for all global variables. The WiFi SDK alone reserves a non-negotiable
~28KB. After WiFi connects and all persistent library objects initialize, the realistic free
heap at steady state is approximately **25–35KB**.

| Region | Size | Notes |
| --- | --- | --- |
| Total DRAM | 80 KB | Hard ceiling |
| WiFi SDK reservation | ~28 KB | Always consumed; not negotiable |
| Main loop stack | ~4 KB | Shared across all nested function calls |
| **Free heap at steady state** | **~25–35 KB** | What your code actually has to work with |

That 25–35KB sounds comfortable until you see what the biggest features cost.

---

## Consumer Rankings

### #1 — BearSSL TLS Context (Calendar HTTPS Fetch)

**Location:** `WagFamBdayClient::updateData()` — runs every `minutesBetweenDataRefresh` minutes.

```cpp
std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
client->setInsecure();
// No setBufferSizes() call — uses library defaults
```

`BearSSL::WiFiClientSecure` with default settings allocates these buffers on the heap every time
`updateData()` is called:

| Buffer | Default Size | Purpose |
| --- | --- | --- |
| RX record buffer | **16,385 bytes** | Receive TLS records |
| TX record buffer | 512–16,384 bytes | Transmit TLS records |
| TLS engine/cipher context | ~5,000–8,000 bytes | Session keys, cipher state |
| **Total peak** | **~22,000–32,000 bytes** | During handshake and fetch |

With a steady-state free heap of 25–35KB, this single allocation **consumes 65–100% of all
available heap** for the entire duration of every calendar refresh. Any `String` operation,
web request handler, or other allocation attempted during this window has near-zero free heap
to work with. This is the primary cause of intermittent crashes and failed fetches.

**This is by far the most impactful issue in the entire codebase.**

#### Fix A — Reduce buffer sizes (one line, keeps HTTPS)

```cpp
std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
client->setInsecure();
client->setBufferSizes(2048, 512);  // add this line
```

The TLS standard allows record payloads up to 16KB, but no server sends a 16KB TLS record for
a small JSON response. A 2KB RX buffer is more than sufficient for a few hundred bytes of JSON.

**Estimated savings: ~12–19KB heap during every calendar fetch.**

#### Fix B — Switch endpoint to plain HTTP (eliminates BearSSL entirely)

If the calendar endpoint can be served over plain HTTP, replacing `BearSSL::WiFiClientSecure`
with a plain `WiFiClient` eliminates the entire TLS context (~20–32KB) on every call. The
BearSSL library code also occupies flash even when idle; removing the dependency shrinks
the firmware binary.

---

### #2 — ArduinoOTA (Persistent Heap Reservation)

> **Status: Removed in v3.08.0-wagfam.** See `docs/OTA_STRATEGY.md` for the replacement
> architecture.

ArduinoOTA permanently held open a UDP socket (port 8266 for discovery) and a TCP server
socket (for transfers), plus mDNS registration hooks. These were allocated at boot and
never freed. Additionally, ArduinoOTA is LAN-only and provides no value for clocks deployed
in remote family homes.

| Cost | Amount |
| --- | --- |
| Persistent heap recovered | ~4–8 KB |
| Per-loop overhead eliminated | One syscall per iteration of `loop()` |

The three OTA paths that replaced it:

| Route | Method | Notes |
| --- | --- | --- |
| `/update` | Browser file upload | Manual; works from any network via VPN/SSH |
| `/updateFromUrl` | HTTP URL from web form | Writes rollback record before flashing |
| Auto (calendar config) | `latestVersion` + `firmwareUrl` in JSON | Version-checked; writes rollback record |

---

### #3 — Web Server HTML Generation (Per-Request Peaks)

**Location:** `sendHeader()`, `displayHomePage()`, `handleConfigure()`.

47 `html +=` operations across those three functions create repeated heap reallocations as
each `String` grows. The per-function breakdown:

| Function | `html +=` ops | Estimated peak String size |
| --- | --- | --- |
| `displayHomePage()` | 23 | ~800 bytes |
| `sendHeader()` | 16 | ~650 bytes before first flush |
| `sendFooter()` | 8 | ~300 bytes |

The configure page also copies PROGMEM chunks to heap via `FPSTR()` before sending:

| PROGMEM Constant | Size (chars) | Peak heap after `replace()` |
| --- | --- | --- |
| `CHANGE_FORM2` | 1,581 | ~1,650 bytes |
| `CHANGE_FORM3` | 644 | ~700 bytes |
| `UPDATE_FORM` | 536 | ~600 bytes |
| `WEB_ACTION3` | 351 | ~400 bytes |

**Peak during a `/configure` page load: ~1,700 bytes** (CHANGE_FORM2 after token replacement).

These peaks are short-lived and only occur when a human is actively using the web UI — not
during normal clock operation. This is a real but lower-urgency concern compared to
the BearSSL issue.

**The actionable optimization** is sending static HTML fragments directly from flash instead
of accumulating them in a `String`:

```cpp
// Current: String allocation + realloc
html += "<br><div class='w3-container w3-large' style='margin-top:88px'>";
server.sendContent(html);

// Better: direct flash read, no heap involved
server.sendContent(F("<br><div class='w3-container w3-large' style='margin-top:88px'>"));
```

Dynamic content (city name, temperature, etc.) still needs String building, but the many
lines of pure static HTML should bypass the heap entirely.

---

### #4 — JsonStreamingParser Stack Frame

**Location:** `WagFamBdayClient::updateData()` — `JsonStreamingParser parser` is a
stack-allocated local variable.

The `JsonStreamingParser` class has a 512-byte internal character buffer (`BUFFER_MAX_LENGTH`)
as a class member. Because the parser is a local variable in `updateData()`, this entire
block lives on the 4KB stack for the duration of every HTTPS fetch:

| Stack item | Size |
| --- | --- |
| `JsonStreamingParser parser` (includes 512-byte buffer) | ~660 bytes |
| `char buff[128]` (read loop buffer) | 128 bytes |
| Other locals and frame overhead | ~50 bytes |
| **Total stack consumed by `updateData()`** | **~840 bytes** |

This is safe for normal operation, but it means `updateData()` is already 840 bytes into
the 4KB stack before any nested calls happen.

`BUFFER_MAX_LENGTH = 512` caps the maximum length of any single JSON string value. For this
use case (short messages, URLs under ~150 characters), this could be reduced to 256 in
`lib/json-streaming-parser/JsonStreamingParser.h` without any functional impact, freeing
256 bytes of stack per `updateData()` call.

---

### #5 — WiFiManager (Boot-Time Only)

**Location:** `setup()` — declared as a local variable, goes out of scope after WiFi connects.

During the captive portal phase, WiFiManager runs its own web server and DNS server
simultaneously, consuming ~20–30KB of heap. Once WiFi connects and `setup()` returns, the
object is destroyed and all its heap is freed.

**This is not a runtime concern** for a deployed device with saved WiFi credentials. The
heap cost is zero after boot.

If even binary code size matters, WiFiManager could be replaced with a hard-coded
`WiFi.begin(ssid, pass)` call plus a `WiFi.disconnect(true)` / `ESP.eraseConfig()` route
in the web UI. Trade-off: you lose the convenient AP-based WiFi setup workflow.

---

## Dead Code and Wasted Resources

These are not significant wins individually but are straightforward to clean up.

| Issue | Location | Waste |
| --- | --- | --- |
| `TIMEOUT` declared, never read | `marquee.ino:150` | 4 bytes BSS |
| `timeoutCount` declared, never read | `marquee.ino:151` | 4 bytes BSS |
| `#include <ESP8266mDNS.h>` — `MDNS.begin()` never called | `Settings.h:43` | ~2–4 KB flash code |
| Duplicate `getWifiQuality()` prototype | `marquee.ino:57 and 65` | Compiler warning |

`ESP8266mDNS` is the most meaningful: the library is linked into the binary (costing flash code
space) for a service that was never started. Removing the `#include` will slim the firmware
binary without any functional change.

---

## Priority Summary

| Priority | Consumer | RAM Recovered | Effort | Status |
| --- | --- | --- | --- | --- |
| 🔴 **Do immediately** | BearSSL buffer reduction (`setBufferSizes`) | 12–19 KB per fetch | 1 line | **Done** |
| 🟠 **High value** | Remove ArduinoOTA | 4–8 KB persistent | ~10 line removal | **Done** |
| 🟡 **Opportunistic** | HTML `+=` → direct `sendContent(F(...))` | 1–2 KB per page request | Medium refactor | Open |
| 🟡 **Easy win** | Reduce `BUFFER_MAX_LENGTH` from 512 → 256 | 256 bytes stack | 1 number change | Open |
| 🟢 **Trivial cleanup** | Remove `#include <ESP8266mDNS.h>` | ~2–4 KB flash | 1 line removal | **Done** |
| 🟢 **Trivial cleanup** | Remove `TIMEOUT` and `timeoutCount` | 8 bytes BSS | 2 line removal | **Done** |
| 🟢 **Trivial cleanup** | Remove duplicate `getWifiQuality()` prototype | 0 bytes (warning fix) | 1 line removal | **Done** |
| ⚪ **Deferred** | Replace WiFiManager with hard-coded WiFi | Boot-time only | Major workflow change | Deferred |

---

## Bottom Line

**Both top fixes are now implemented (v3.08.0-wagfam):**

- `setBufferSizes(2048, 512)` recovers 12–19 KB of heap on every calendar refresh cycle
- Removing ArduinoOTA adds back another 4–8 KB of permanent headroom

Combined, those two changes recover **~16–27 KB of usable heap** — enough breathing room for
additional data sources, more complex calendar logic, or richer display features.

ArduinoOTA was replaced with a full remote auto-update system driven by the calendar config
JSON channel, including boot-confirmation rollback to protect against crash loops. See
`docs/OTA_STRATEGY.md` for details.
