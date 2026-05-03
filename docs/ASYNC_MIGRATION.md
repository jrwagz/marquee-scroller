# Async Web Server Migration: What We Got and What It Unlocks

In the `optimizations` branch we replaced the synchronous
`ESP8266WebServer` + `ESP8266HTTPUpdateServer` + `tzapu/WiFiManager` stack
with `ESPAsyncWebServer` (esphome fork) + `ESPAsyncWiFiManager` + a
manually-implemented OTA upload handler. This document explains what
that buys us — concretely today and as a foundation for what we can
build next — on a chip that still has only ~30 KB of free heap to play
with.

This is a strategy doc, not a how-to. For the playbook of working with
the async server, see [`CLAUDE.md`](../CLAUDE.md) (project rules) and
[`LIVE_DEVICE_TESTING.md`](LIVE_DEVICE_TESTING.md) (test recipes).

---

## What we gained immediately

These are observable on the device today, not theoretical.

### The display path stopped servicing the network

The single biggest behavioral win. Under the sync server,
`scrollMessageWait()` had to call `server.handleClient()` once per
pixel step (~25 ms) just to keep the web server responsive while a
message was scrolling. The same was true of the main loop —
`server.handleClient()` was the last thing on every iteration.

Under async, both calls are gone. HTTP requests are dispatched from
TCP callbacks driven by the lwIP stack in the background. The display
loop is now a pure render path. Two consequences:

- **Cleaner cause-and-effect when things go wrong.** A bug in the scroll
  routine can no longer also break the web server, and vice versa. The
  module boundary is real now.
- **No more "the page hung for two seconds"** during a long scroll.
  Requests are picked up immediately, regardless of what the display is
  doing.

### Concurrent request handling

The sync server processed one request at a time. If the configure page
had four parallel asset loads (CSS, fonts, etc.), each had to wait for
the previous to fully complete. The async server interleaves them
naturally. This isn't observable today — there are no concurrent
requests in the existing UI — but every later UI improvement benefits
without any additional work.

### ~20 KB of flash freed up

| Build | Flash | Static RAM |
| --- | --- | --- |
| master (sync) | 577,223 B | 38,876 B |
| optimizations | 557,553 B | 38,932 B |
| Δ | **−19,670 B (−3.4%)** | +56 B |

We dropped tzapu's WiFiManager (which embedded its own copy of the sync
ESP8266WebServer) and `ESP8266HTTPUpdateServer`. The async libs are
smaller than what they replaced. Static RAM is essentially unchanged.

### Architectural patterns are now in the codebase

These exist as working, tested code that future features can imitate:

| Pattern | Used by | Why it matters |
| ------- | ------- | -------------- |
| **Deferred-work flag** (`weatherRefreshRequested`, `otaFromUrlRequested`, `restartRequested`) | `/pull`, `/api/refresh`, `/saveconfig`, `/updateFromUrl`, `/api/restart`, `/update` reboot | Async handlers must not block. This pattern is how you do anything >100 ms safely. Copy it for any future endpoint that needs HTTPS, OTA, file IO, or anything that touches the network. |
| **`AsyncResponseStream`** | `displayHomePage`, `handleConfigure`, `handleUpdateFromUrl` | Chunked HTML rendering without `String += "..."` allocation explosions. Lets you incrementally build a page without buffering it all in heap. |
| **`AsyncCallbackJsonWebHandler`** | `POST /api/config`, `POST /api/fs/write` | Body-bearing JSON endpoints with parsing handled by the framework, not by reading `server.arg("plain")`. ArduinoJson v7 is built in. |
| **Manual Basic-auth verifier** | `requireWebAuth`, `requireApiAuth`, `/update` upload | Workaround for a lib bug; will keep working even if upstream fixes its check. |

---

## What it realistically unlocks

These are things that would have ranged from awkward to actively painful
on the sync stack, and are now within reach.

### A real frontend (the original "option 2" from the planning conversation)

The hardest part of replacing the W3.CSS-from-CDN page with a proper SPA
was the *backend* shape: chunked sends were fragile, JSON POST was
manual string parsing, the configure form was an opaque
`server.arg(...)` blob. All three are now first-class. A frontend rewrite
is now mostly a frontend project, not a firmware project.

Realistic budget for the bundle:

- Total LittleFS partition: **1 MB** — but `/conf.txt` and `/ota_pending.txt`
  live there too, plus headroom for backups. Call it **~700 KB usable**.
- A Preact + signals SPA with hand-rolled CSS lands around **20–30 KB
  gzipped, 60–80 KB raw**. Easily fits.
- The async server can serve `*.gz` files with `Content-Encoding: gzip`
  out of the box — no manual handling needed. This was awkward on the
  sync stack.

### Live status updates (Server-Sent Events or WebSockets)

`ESPAsyncWebServer` ships with `AsyncEventSource` (SSE) and
`AsyncWebSocket`. Possible features that would have been infeasible
before:

- **Live free-heap / RSSI / scroll-message ticker** on a status page —
  no polling, no jank.
- **Configure-form live preview** — type a brightness value, see the
  matrix change instantly, save when satisfied.
- **OTA progress bar** — chunk progress streams to the browser as the
  upload arrives.

The cost is heap (each open SSE/WS connection holds a TCP buffer), so
we'd budget conservatively — one or two clients in practice for a
kitchen-clock device.

### Off-device tooling against the REST API

The API was already there, but with the async server it now handles
multiple parallel requests gracefully. A few things this enables:

- **A backup script** that can talk to multiple clocks in parallel
  without each blocking the next.
- **A central dashboard** showing all deployed clocks (you already have
  the heartbeat plumbing) — calling `/api/status` on each every few
  minutes is no problem.
- **CI-style health checks** that actually exercise the API as an
  integration test in a containerized environment, hitting a real device
  on the LAN.

### Captive-portal hardening

Under the old stack, `tzapu/WiFiManager` ran its captive portal on its
own internal sync server, separately from the main web server. They were
two different code paths, with two different sets of routes, two
different auth styles, two different heap accounting stories.

Under `ESPAsyncWiFiManager`, the captive portal *is* the same server.
One auth model, one routing table, one set of patterns to learn. Easier
to add custom portal pages, easier to debug, easier to reason about.

---

## What it doesn't change (the floor we're still standing on)

The migration buys us *capabilities*, not *resources*. The chip is the
same chip.

### Heap is still the binding constraint

Free heap baseline went from **~31 KB → ~30 KB** after the migration.
That's a small price for everything we got, but it's not free. The
same rules still apply:

- **BearSSL still needs ~16 KB contiguous** for a TLS handshake. The
  calendar fetch (`WagFamBdayClient`) is the one place this bites
  hardest. Heap fragmentation matters; long-running feature additions
  should prefer `PROGMEM` constants and `F()` over `String += ...`.
- **`AsyncResponseStream` buffers in heap** until `request->send()` is
  called. It works for our 2–3 KB pages today, but a 50 KB SPA-bundle
  HTML response would need to be served from LittleFS as a file (which
  the async server streams), not built in memory.
- **One open WebSocket / SSE connection costs ~2–4 KB**. Think hard
  before allowing more than two simultaneous clients.

### The CPU is still one core at 80 MHz

Async means non-blocking; it does not mean parallel. Every callback runs
on the same task as `loop()`. If we put expensive work in a TCP callback
(say, a 2-second JSON serialization on a 10 KB payload), the display
will visibly stutter and other requests will queue. The deferred-work
flag pattern exists precisely to push expensive work off the TCP path
and back onto the main loop, where it can take its time.

### HTTPS server is still not happening

`ESPAsyncWebServer` on ESP8266 does not do TLS termination. BearSSL is
client-only in this stack. Anything we expose stays HTTP, LAN-only, with
HTTP Basic Auth. That's fine for a kitchen device behind a home router;
it's a non-starter if we ever wanted to expose a clock to the public
internet.

### The deferred-work pattern is mandatory, not optional

Anything an async handler does has to fit within ~100 ms before risking
watchdog issues or heap pressure during BearSSL handshakes. We learned
this the hard way during the migration test — `/api/refresh` and
`/pull` initially crashed the device because they called
`getWeatherData()` (a 10–20 second HTTPS chain) directly from the
handler. Three commits and a hardware reset later, every blocking call
in the codebase is deferred. New code has to follow suit. There is no
"oh, just this once" for a handler that touches HTTPS.

---

## Concrete next-step menu

Things we could realistically build now, ranked by effort vs. user value
on this device:

1. **SPA shell + REST-driven UI** (option 2). Highest value, ~1–2 weeks
   work, foundation already in place.
2. **Status SSE** — push free heap, last-refresh, RSSI to a dashboard
   page. Small isolated win, half a day of work, useful on its own and a
   building block for the SPA.
3. **`/api/logs` ring buffer** with a streaming endpoint. Useful for
   debugging without a serial connection, easy to build with
   `AsyncResponseStream`.
4. **mDNS responder** (`marquee.local` resolution). Trivial code, large
   UX improvement on a multi-device LAN. Already compatible with the
   async stack.
5. **CORS headers on `/api/*`** so an off-device dashboard can hit
   multiple clocks without a proxy. Three lines of code.
6. **Drop the W3.CSS / Font Awesome CDN dependencies** — bundle the
   minimum CSS into LittleFS, serve it gzipped. Removes the requirement
   for clients to have internet access just to see the configure page.
   Probably a side-effect of the SPA work.

The async migration was the boring infrastructure work that makes any
of these tractable. The next push, when there's appetite, should be a
visible-to-users feature that exercises what we've now got.
