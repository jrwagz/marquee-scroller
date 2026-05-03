# Decision Log

This is a running record of architectural decisions made on the project — what
was chosen, what was considered and rejected, and the conditions under which
we'd revisit. The goal is to keep the *why* alongside the code so future
contributors (human or otherwise) don't have to reconstruct it from commits
and PR comments.

Append new entries at the top. Each entry is dated and points at the PR or
commit that landed the decision. Long-form rationale lives in the dedicated
docs (`docs/ARCHITECTURE.md`, `docs/OTA_STRATEGY.md`,
`docs/ASYNC_MIGRATION.md`, `docs/WEBUI.md`); this file links to them rather
than duplicating.

---

## Open Threads

Things that are deferred, conditional, or queued behind something else.
Update or remove entries as they resolve.

- **HA integration via MQTT add-on** — gated on validating the value with
  HA's `rest:` sensor against `/api/status` first (Option 0 from
  [D-2026-05-01-c](#d-2026-05-01-c)). If a week of REST polling shows we'd
  want richer entities, push notifications, or instant-offline detection,
  the firmware MQTT path (`AsyncMqttClient` + HA Discovery topics + LWT) is
  ~1–2 days of work and fits the existing async stack.
- **`/api/fs/upload` (multipart, streaming, binary-capable)** — needed
  before SPA bundle deploys can avoid the serial-flash-wipes-`/conf.txt`
  caveat. Foundation PR ([#55](https://github.com/jrwagz/marquee-scroller/pull/55))
  documents the workaround; this endpoint is the proper fix.
- **Status SSE endpoint + dashboard page** — first feature PR planned on
  top of the SPA foundation. `AsyncEventSource` is built into the lib;
  pushes free heap / RSSI / next-event countdown.
- **Drop legacy `/` and `/configure` PROGMEM HTML** — once the SPA covers
  all UI features, the `CHANGE_FORM*` / `WEB_ACTIONS*` constants and the
  `displayHomePage` / `handleConfigure` chunked-render handlers can go,
  reclaiming ~5 KB of flash.
- **Boot-confirmation rollback distinguishing user-restarts from crashes**
  — known edge case: `/api/restart` within 5 min of a fresh OTA flash
  triggers rollback because the rollback design counts the user-requested
  reboot as one of the two unconfirmed boots. Pre-existing behavior, not
  introduced by the async migration. Fix would require differentiating
  reboot causes (RTC RAM flag survives soft restart; can be set on the
  intentional path).

---

## Decisions

<a id="d-2026-05-01-d"></a>

### D-2026-05-01-d: Stay with current architecture (don't pivot to ESPHome)

**Status:** Decided. No code change required.

**Context:** The async server migration ([#54](https://github.com/jrwagz/marquee-scroller/pull/54))
and SPA foundation ([#55](https://github.com/jrwagz/marquee-scroller/pull/55))
overlap with what ESPHome's built-in `web_server` and `captive_portal`
components offer. Worth asking whether we should have started from ESPHome
instead of building the equivalents ourselves.

**Options considered:**

1. **Full ESPHome rewrite** — get the web server, NTP, WiFi management,
   captive portal, and (via MQTT or native API) Home Assistant integration
   for free.
2. **Stay with current architecture, add MQTT for HA integration** — keep
   custom OTA rollback, runtime config, security model, calendar fetch,
   display logic; add `AsyncMqttClient` as a sibling for HA telemetry.
3. **Stay with current architecture, no HA integration** — status quo.

**Decision:** Option 2 if the appetite for HA integration is real
(see [open thread](#open-threads)); Option 3 otherwise. **Not Option 1.**

**Rationale:** ~80% of what makes this device interesting is application
code that doesn't fit ESPHome's YAML-driven model:

- Runtime config via `/conf.txt` + `/api/config` (ESPHome bakes config into
  firmware; runtime changes require re-flash).
- Boot-confirmation OTA rollback with 5-min stable-uptime window
  (ESPHome's update story is different).
- Custom calendar fetch + remote-config push via `WagFamBdayClient`
  (would be a custom component in ESPHome, harder than direct C++).
- Custom display rendering (scroll timing, event-day border, PM-pixel)
  (would be hundred-line lambdas in YAML).
- Bespoke security model (Basic auth, CSRF token, protected paths).

ESPHome would only have saved the work we're nearly done with anyway
(async server, NTP, WiFi management). Migrating now is a net loss —
"throw away working code to get a different set of problems."

**Conditions to revisit:** If we ever want low-latency button-press →
HA-action flows (where ESPHome's encrypted native binary protocol
matters), or if MQTT ends up being insufficient for some HA pattern we
care about. Not on the radar today.

---

<a id="d-2026-05-01-c"></a>

### D-2026-05-01-c: Direct push to `jrwagz/marquee-scroller`, no fork-PR dance

**Status:** Applied. Active rule.

**Context:** Project was set up in the standard "fork-and-PR" model
(branches on `dallanwagz/marquee-scroller`, PRs across forks targeting
`jrwagz/marquee-scroller` upstream). High friction for routine work
between trusted collaborators (brothers).

**Decision:** Push branches directly to `upstream` (`jrwagz/...`); PRs
are repo-internal. The `dallanwagz/...` fork still exists and `origin`
still points at it, but it's no longer the working remote.

**Rationale:** The fork model exists for "I don't trust this contributor
with write access." That's not the situation here. Eliminates the
`gh pr create --head dallanwagz:<branch>` flag form and the confusing
"Head sha can't be blank" error when bare `gh` commands misroute.

**Reflected in:** `CLAUDE.md` "gh CLI" rule (updated on the
[optimizations](https://github.com/jrwagz/marquee-scroller/pull/54) branch).

---

<a id="d-2026-05-01-b"></a>

### D-2026-05-01-b: SPA frontend with Preact + Vite + TypeScript, served from LittleFS at `/spa/`

**Status:** Foundation merged in [PR #55](https://github.com/jrwagz/marquee-scroller/pull/55)
(stacked on #54). Feature PRs to follow.

**Context:** The legacy UI uses W3.CSS + Font Awesome from CDNs and renders
HTML server-side via `String += "..."` chunked sends. It works but
fragments heap, fails when the device is offline (CDN unreachable), and
is hostile to incremental UI improvements. With the async server in place
([D-2026-05-01-a](#d-2026-05-01-a)), the foundation for a real SPA exists.

**Options considered:**

1. **Modernize legacy UI in place** — keep server-side rendering, replace
   `html += ...` with `AsyncResponseStream` chunks, add modest CSS.
   Cheap but the ceiling is "reasonable 2015-era admin UI."
2. **SPA shell + REST API** — pre-built minified bundle in LittleFS, tiny
   HTML shell, JS calls existing `/api/*` endpoints. Real frontend
   ergonomics. Bundle in flash.
3. **Off-device UI** — host the UI on GitHub Pages or similar, point at
   each clock's REST API from the browser. Best UX, but stops working
   offline and CORS + Basic-auth across origins is awkward.

**Decision:** Option 2.

**Stack:** Preact 10 (3 KB) + `@preact/signals` + Vite 5 + TypeScript
(strict). Hand-rolled utility CSS — no Tailwind/MUI/Bootstrap (flash
budget is tight). Bundle target: 20–30 KB gzipped.

**Rationale:** Option 2's main cost was the backend shape (chunked sends,
JSON body parsing) — both are first-class on the new async stack, so the
backend work was already done by [D-2026-05-01-a](#d-2026-05-01-a).
Remaining work is a frontend project, not a firmware project. Preact
gives React ergonomics at a tenth the size; signals fit the
"fetch from device, mutate, push back" flow. TypeScript catches typos in
the 22-field `/api/config` shape before they reach the device. Option 3
is rejected because LAN-offline is a real failure mode (router reboots,
ISP outages) and we want the device's UI to keep working.

**Reflected in:** [`docs/WEBUI.md`](WEBUI.md) — full pipeline, dev
workflow, bundle budget, deploy paths.

**Note on deploy mechanics:** First-time SPA install requires serial
flash via `pio run --target uploadfs`, which wipes `/conf.txt`. The
`/api/fs/write` endpoint is too restrictive for full bundle deploys
(8 KB JSON limit, string-only `content`). Proper fix is a binary upload
endpoint (see [open thread](#open-threads)).

---

<a id="d-2026-05-01-a"></a>

### D-2026-05-01-a: Migrate web server from `ESP8266WebServer` to `ESPAsyncWebServer`

**Status:** Hardware-tested, [PR #54](https://github.com/jrwagz/marquee-scroller/pull/54)
open.

**Context:** The synchronous `ESP8266WebServer` required
`server.handleClient()` calls in both the main `loop()` and inside
`scrollMessageWait()` (every pixel step) to keep HTTP responsive during
scrolling. The display path was inseparable from the web server. A real
SPA frontend would have wanted gzipped static-asset serving, JSON body
parsing, and chunked HTML sending — all of which were either painful or
absent on the sync stack.

**Options considered:**

1. **Stay sync, build SPA on top** — chunked HTML via
   `setContentLength(CONTENT_LENGTH_UNKNOWN) + sendContent()`, manual
   JSON body parsing via `server.arg("plain")`. Workable, but every later
   feature pays the same friction.
2. **Async migration** — `ESPAsyncWebServer` (esphome fork) +
   `ESPAsyncWiFiManager` (because tzapu/WiFiManager pulls in
   `ESP8266WebServer.h`, whose `HTTP_GET` enum collides with the async
   lib's at link time). Manual `/update` upload handler (no async
   equivalent of `ESP8266HTTPUpdateServer`).

**Decision:** Option 2.

**Rationale:** Display path becomes self-contained
(no more `server.handleClient()` mid-scroll). Concurrent request handling
comes free. ~20 KB of flash freed by dropping tzapu/WiFiManager and the
sync HTTP update server. AsyncResponseStream + `AsyncCallbackJsonWebHandler`
cleanly handle the cases that were painful in sync.

**Surprises found during hardware testing** (each fixed in its own
commit on the same PR; full story in [`docs/ASYNC_MIGRATION.md`](ASYNC_MIGRATION.md)):

1. `ESPAsyncWebServer-esphome` 3.3.0 has a Basic-auth verification bug on
   ESP8266 (newline accounting in `base64_encode_expected_len`). Manual
   verifier (`requestHasValidBasicAuth`) shipped as a workaround; documented
   in `feedback_lib_basic_auth_bug.md` user memory.
2. Async handlers cannot block on HTTPS or OTA — `/api/refresh` and
   `/pull` initially crashed the device with `reset_reason: Exception`.
   Deferred-work flag pattern (`weatherRefreshRequested`,
   `otaFromUrlRequested`, `restartRequested`) shipped as the canonical
   solution; documented in `feedback_async_handlers_must_not_block.md`
   user memory.
3. `ESP.restart()` in handler dropped the response — async TCP needed
   ~1 s to flush before reboot. Fixed by deferring restart to the main
   loop with a 1-second deadline.

**Net change:** Flash 577,223 → 557,553 (−19,670 B); free heap
~31 KB → ~30 KB at runtime. Hardware-verified end-to-end on the kitchen
clock.

**Reflected in:** [`docs/ASYNC_MIGRATION.md`](ASYNC_MIGRATION.md) — full
strategy view of what we got and what it unlocks.

---

## Conventions for entries

- **ID format:** `D-YYYY-MM-DD-<letter>` where the letter discriminates
  multiple decisions made the same day.
- **Sections:** Status / Context / Options considered / Decision /
  Rationale / (optional) Reflected in / Conditions to revisit. Skip
  sections that don't apply.
- **Cross-links:** prefer linking to PRs and longer-form docs over
  duplicating their content. This file is an index, not a replacement.
- **Decisions don't get deleted.** If a decision is superseded by a later
  one, mark its status `Superseded by D-...` and leave the original
  entry. Keeps the historical record honest.
