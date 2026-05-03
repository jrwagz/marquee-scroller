# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@README.md

---

## Quick Start for LLM Agents

Before making changes, read these docs in order:

1. `docs/ARCHITECTURE.md` — what every module does, hardware context, global state map
2. `docs/CODE_FLOW.md` — how execution flows from boot through normal operation
3. `docs/CODE_REVIEW.md` — known bugs and issues (check here before touching any file)

---

## Architecture

All firmware source lives in [marquee/](marquee/):

| File | Role |
| --- | --- |
| [marquee.ino](marquee/marquee.ino) | Main sketch: `setup()`, `loop()`, web server handlers, display rendering |
| [Settings.h](marquee/Settings.h) | Hardware pin config + all `#include` directives; default values for first-run only |
| [OpenWeatherMapClient.h/.cpp](marquee/OpenWeatherMapClient.h) | Fetches weather from OpenWeatherMap API using ArduinoJson |
| [WagFamBdayClient.h/.cpp](marquee/WagFamBdayClient.h) | Fetches family calendar JSON over HTTPS; parses messages and remote config |
| [SecurityHelpers.h/.cpp](marquee/SecurityHelpers.h) | Firmware URL validation, path protection, domain extraction |
| [timeNTP.h/.cpp](marquee/timeNTP.h) | NTP time sync; exposes `timeNTPsetup()`, `getNtpTime()`, and `set_timeZoneSec()` |
| [timeStr.h/.cpp](marquee/timeStr.h) | Time formatting helpers (zero-pad, day/month names, etc.) |

The SPA frontend lives in [`webui/`](webui/) — Vite + Preact + signals + TypeScript.
See [`docs/WEBUI.md`](docs/WEBUI.md) for the build pipeline and deploy story. Bundle is
shipped to LittleFS under `/spa/` and served by AsyncWebServer's `serveStatic`.

Local library copies (not managed by PlatformIO) are in [lib/](lib/):

- `arduino-Max72xxPanel` — MAX7219 LED matrix driver
- `json-streaming-parser` — streaming JSON parser used by `WagFamBdayClient`

## Main Loop Logic

The web server is `AsyncWebServer` (ESPAsyncWebServer-esphome), which handles HTTP requests
in the background via TCP callbacks — **the main loop never calls `handleClient()`**. The
captive portal during AP mode is handled by `ESPAsyncWiFiManager` sharing the same server
instance.

- **Every frame**: When dirty or event-day border is active, clear display and call `centerPrint(displayTime, true)`
- **Every second** (`processEverySecond`): Fires OTA confirmation check; calls `getWeatherData()` if
  `minutesBetweenDataRefresh` has elapsed
- **Every minute** (`processEveryMinute`): Scroll the marquee message (weather data + next calendar message from
  `bdayClient`). Controlled by `displayRefreshCount` countdown

### Key Line Numbers in `marquee.ino`

These are approximate — search for the symbol if the line is off by a few. The file is ~1600 lines.

| Function | Line |
| --- | --- |
| Global variables (settings) | ~95–189 |
| PROGMEM HTML constants | ~133–179 |
| `setup()` | 194 |
| `loop()` | 375 |
| `processEverySecond()` | 401 |
| `processEveryMinute()` | 418 |
| `handleSaveConfig()` | 498 |
| `handleConfigure()` | 553 |
| `doOtaFlash()` | 663 |
| `handleUpdateFromUrl()` | 679 |
| `performAutoUpdate()` | 732 |
| `checkOtaRollback()` | 748 |
| `getWeatherData()` | 802 |
| `sendHeader()` / `sendFooter()` | 927 / 957 |
| `displayHomePage()` | 971 |
| `savePersistentConfig()` | 1105 |
| `readPersistentConfig()` | 1146 |
| `scrollMessageWait()` | 1240 |
| `centerPrint()` | 1263 |
| Security helpers (`requireWebAuth`, etc.) | 1303 |
| REST API handlers | ~1351–1583 |

## Configuration Storage

Runtime config is persisted via LittleFS (aliased as `SPIFFS` in the sketch) at `/conf.txt` as `key=value` pairs.
The functions `savePersistentConfig()` and `readPersistentConfig()` in [marquee.ino](marquee/marquee.ino)
own all reads and writes to this file. [Settings.h](marquee/Settings.h) contains compile-time defaults only —
changes there require a filesystem erase to take effect.

`WAGFAM_EVENT_TODAY` and `DEVICE_NAME` are not user-configurable via the web form — they are set
exclusively by the server's `config.eventToday` and `config.deviceName` fields in the calendar
JSON response and persisted across reboots via `/conf.txt`.

### Adding a New Config Key

1. Declare a global variable in `marquee.ino` (near line 95)
2. Add `f.println("KEY=" + String(value))` in `savePersistentConfig()` (~line 1067)
3. Add an `if (line.indexOf("KEY=") >= 0)` block in `readPersistentConfig()` (~line 1146)
4. Add a form field in one of the `CHANGE_FORM*` PROGMEM constants (~line 142)
5. Read from `request->arg("fieldName")` in `handleSaveConfig()` (~line 498)

## Development Practices

Lessons from code review — these are non-obvious enough to state explicitly:

**Never suppress a lint rule to make CI pass.** Adding a new `RuleXX: false` to
`.markdownlint.yaml` is always wrong. Find the line that violates the rule and fix it.
The only disabled rules in this repo are those with deliberate permanent policy reasons
(MD033, MD024, MD041) — they were disabled intentionally, not to unblock a failure.

**Don't duplicate logic when a shared helper exists or is obvious.**
When writing a second code path that does the same core operation as an existing one,
extract a shared function. The `doOtaFlash()` / `handleUpdateFromUrl()` /
`performAutoUpdate()` split is the canonical example: all three callers differ in how
they present to the user, but the flash core (write rollback record → call ESPhttpUpdate
→ clean up on failure) is identical and belongs in one place.

**Write unit tests for new parsing logic.** `tests/native/test_wagfam_parser/` already
tests `WagFamBdayClient`'s parsing. If you add a new field to the `configValues` struct
or change the `value()` callback, add corresponding tests in the same commit. The rule
of thumb: any new branch in a `JsonListener` callback needs a test.

**Keep test stubs in sync with production code.** When adding a new method call to
production code that has stubs in `tests/native/stubs/`, update the stub in the same
change. If production code calls `client->setBufferSizes(2048, 512)`, the
`WiFiClientSecureBearSSL.h` stub needs `void setBufferSizes(int, int) {}`.

**Update docs in the same commit as the code change.** If a change makes a statement
in `CLAUDE.md` or `docs/` inaccurate, fix the doc in the same commit — not later.
Specifically: when a bug in `docs/CODE_REVIEW.md` is fixed, remove it from that file.

**Back assertions about external services with evidence.** Any claim about how a
third-party service behaves (e.g. "GitHub raw ignores query params", "ESPhttpUpdate
follows redirects", "BearSSL accepts self-signed certs with `setInsecure()`") must
be backed by either a one-time empirical capture (logged in the PR/commit that
adds the claim) or a citation to that service's official documentation. Internal-
knowledge claims rot silently when the upstream changes; written-down evidence
does not. **Don't keep network-dependent regression tests in CI for these claims —
they flake.** For example, the heartbeat params claim ("GitHub raw ignores
`chip_id`, `version`, `uptime`, `heap`, `rssi` and DOES interpret `?token=`") was
verified empirically during PR #25 review (see commit `bca26e6` and the
[review thread](https://github.com/jrwagz/marquee-scroller/pull/25#discussion_r3144204226)
for the captured sha256s and HTTP codes); the matching `test_github_raw_compat.py`
was removed in a follow-up because raw.githubusercontent.com's CDN-cache shards
returned racy results when the target file was being updated. Re-verify
out-of-band if you change the heartbeat parameter set or move to a different
static-JSON host — don't add it back to the suite.

**`gh` CLI: branches and PRs live directly on `jrwagz/marquee-scroller`.**
As of 2026-05-01, the upstream maintainer (jrwagz) granted direct push access,
so feature branches go to `upstream` rather than the `dallanwagz` fork. PRs are
repo-internal — no cross-fork `--head` form needed.

Templates:

- Push a branch: `git push -u upstream <branch>` (NOT `origin`).
- Open a PR: `gh pr create --repo jrwagz/marquee-scroller -B master -H <branch> ...`

Bare `gh pr create` and `gh repo view` still resolve to `jrwagz/...` (the fork's
parent) because the fork is set up that way — that's now the *correct* default,
but be explicit anyway with `--repo jrwagz/marquee-scroller` so a future
`origin`-pointing fork doesn't silently misroute. The same `--repo` rule applies
to `gh pr view`, `gh pr comment`, `gh issue`, and `gh api repos/...`.

**GitHub Actions: PRs from forks get a read-only `GITHUB_TOKEN` regardless of
`permissions:` declarations.** A `permissions: packages: write` block in a
workflow is a *request*, not a grant — for PRs from forks (every PR on this
repo, since the fork model is dallanwagz → jrwagz), the token stays read-only
and the requested write permission has no effect. Steps that try to push to
ghcr.io, write registry cache, or publish artifact attestations will get HTTP
403 silently mid-run. Gate every write step on `if: github.event_name !=
'pull_request'` so PR validation runs cleanly: the build itself still runs
(validating that the Dockerfile compiles, etc.) but the registry-side writes
skip.

**Dockerized CLI tools must run as the host user, not as container root.**
When wrapping a CLI tool in a container (`mcr.microsoft.com/azure-cli`,
`davidanson/markdownlint-cli2`, etc.) and mounting host-writable directories
(credentials cache, output files), default `docker run` uses the container's
default UID — typically root. Files written end up owned by root on the host,
which breaks the next non-root invocation. The repo's pattern (canonical
example: `make lint-markdown` at `makefile:32-33` and `:57-66`):

1. Generate a tempfile `/etc/passwd` entry for the host UID (`echo
   "$USER:x:$(id -u):$(id -g)::$HOME:/bin/bash" > "$PASSWD_TMP"`).
2. Mount it into the container at `/etc/passwd:ro`.
3. Pass `-u $(id -u):$(id -g)` to `docker run`.
4. Pass `-e HOME=$HOME` and mount the credential dir at the **same path**
   inside the container as on the host (not at `/root/whatever`), so the tool
   reads/writes credentials at a location matching the host path.

The `make lint-markdown` target at `makefile:57-66` is the canonical example of
this pattern. If you copy a `docker run` snippet from external docs that
mounts to `/root/...`, rewrite it to use the host-user pattern before
shipping.

**Multi-PR coordination: supersede when a strict superset exists; keep
independent otherwise.** Two patterns surfaced this session:

- **Supersede**: PR #23 (security hardening) was a strict superset of PR #22
  (REST API), so #22 was closed as superseded once #23 was ready. PR #25
  later did the same with #23 + #24 — one combined upstream PR that
  superseded both unmerged downstream PRs (since both were stuck on auth
  scope). Use this when one PR cleanly contains all of another's commits and
  there's no advantage to merging them separately.
- **Keep independent**: PRs #30 (Azure deploy plan) and #33 (backup endpoints
  closing issue #32) share zero files, target the same `master`, and merge
  cleanly in either order. Each gets its own focused review thread. Use this
  by default — only consolidate when there's a real superset relationship or a
  blocking dependency.

The cost of consolidating that doesn't need to consolidate is huge PR diffs
that are hard to review. The cost of failing to consolidate when you should is
PRs that get stuck behind each other. The supersede call typically gets made
when the dependency PR has been blocked for a while.

**Out-of-scope review feedback: flag, don't silently scope-creep.** When a PR
review comment introduces new feature work that's materially different from
the PR's stated scope, do not fold it in. Reply on the thread acknowledging
the ask, propose tackling it as a separate PR with a one-paragraph plan, and
flag it explicitly to the human user so they can decide whether to greenlight
parallel work. Example: PR #30's review round 3 introduced issue #32 (server
backup/restore) — pulled into PR #33 after explicit user approval rather than
folded into #30. Bigger PRs are exponentially harder to review than focused
ones; reviewers will not thank you for "while you're at it" creep.

**Live-device testing has its own playbook.** The clock firmware has several
non-obvious testing constraints (BearSSL HTTPS-only client, protected
`/conf.txt` path, OTA_SAFE_URL rewrite-on-flash) and several useful
shortcuts (heartbeat interval is REST-API-configurable down to 1 minute
without flashing, source-binding via `curl --interface` works around
multi-homed routing weirdness). Before doing end-to-end firmware testing,
read [`docs/LIVE_DEVICE_TESTING.md`](docs/LIVE_DEVICE_TESTING.md) — saves
significant trial-and-error.

---

## WebUI development

The Preact SPA in `webui/` is built with `make webui` (Dockerized
`node:20-alpine`). Don't add CSS frameworks (Tailwind / MUI / Bootstrap) —
flash + LittleFS budget is tight. Hand-rolled utility CSS only. Don't
add hash-based filenames (Vite default) — stable filenames keep the
deploy script and on-device debugging tractable; cache invalidation
is handled server-side via `Cache-Control`.

The full deploy story (serial flash for first install, API uploads for
updates, the `/api/fs/upload` follow-up that's still TODO) is in
[`docs/WEBUI.md`](docs/WEBUI.md).

## Markdown Style

`make lint` enforces markdownlint on all `.md` files. Rules in effect (`.markdownlint.yaml`):

- **MD013**: max **120 chars** per line for body text and list items; max **100 chars** for headings
  - Table rows and fenced code block content are **exempt**
  - When a line would exceed the limit, wrap at a natural word/clause boundary
  - List item continuation lines must be indented **2 spaces** to stay in the same list item
- MD033 (inline HTML), MD024 (duplicate headings), MD041 (first-line h1) are **disabled**

Run `make lint-markdown` locally to check before committing any `.md` changes.

## Display Subsystem

The display is a 32×8 pixel grid (4 panels of 8×8 each). Key facts:

- Font: 5px wide + 1px spacer = 6px per character (`width` variable, line 97)
- `scrollMessageWait(msg)` scrolls right-to-left, one pixel per step, at `displayScrollSpeed` ms/step
- `centerPrint(msg, extraStuff)` draws a static centered string + optional extras (event border, PM dot)
- `matrix.write()` flushes the framebuffer to hardware via SPI — called inside `centerPrint` and each
  scroll step
- The animated event-day border is drawn in `centerPrint()` when `WAGFAM_EVENT_TODAY == true`

## WagFamBdayClient — Calendar Integration

`WagFamBdayClient` replaced the `NewsApiClient` from the upstream Qrome fork.
It fetches a JSON array from `WAGFAM_DATA_URL` over HTTPS and stores up to 10 `messages[]`.

Expected JSON format:

```json
[
  {
    "config": {
      "eventToday": "1",
      "dataSourceUrl": "...",
      "apiKey": "...",
      "latestVersion": "3.08.0-wagfam",
      "firmwareUrl": "http://example.com/firmware.bin",
      "deviceName": "Kitchen Clock"
    }
  },
  { "message": "Justin birthday - tomorrow" },
  { "message": "Family dinner - this Saturday" }
]
```

- The `config` object is optional and can contain any subset of the fields
- Messages are displayed one per scroll cycle, cycling through `bdayMessageIndex`
- `cleanText()` translates Unicode lookalikes to ASCII for the LED font (35+ `replace()` calls)
- `latestVersion` + `firmwareUrl` trigger an auto-update if version differs from `VERSION` macro;
  see `docs/OTA_STRATEGY.md` for full rollback architecture
- `deviceName` is a human-friendly label assigned by the server; stored on the device but not
  user-editable (like `WAGFAM_EVENT_TODAY`)

### Device Heartbeat

Each calendar fetch includes device telemetry as query parameters:

```text
GET /data_source.json?chip_id=5fc8ad&version=3.08.0-wagfam&uptime=1234567&heap=32496&rssi=-62&utc_offset_sec=-21600
```

The `utc_offset_sec` param is the UTC offset in seconds derived from the OWM weather response
(e.g. `-21600` for UTC-6 / Central Standard Time). It is always included; when OWM has not yet
returned data, it defaults to 0 (UTC). The wagfam server uses it to compute the client's local
"today" so event ordering is correct for the clock's location
(see [wagfam-server PR #16](https://github.com/jrwagz/wagfam-server/pull/16)).

This lets a backend identify and monitor all deployed clocks without any additional
connections. Static JSON hosts ignore the current set of params gracefully — this
was empirically verified during PR #25 review with identical sha256 + HTTP 200 across
the original 5 param names against `raw.githubusercontent.com` (see commit `bca26e6`
and the [review thread](https://github.com/jrwagz/marquee-scroller/pull/25#discussion_r3144204226)).
`utc_offset_sec` was added later and has not been re-verified against static hosts.

**Caveat from that verification:** `raw.githubusercontent.com` *does* interpret
`?token=…` as an auth attempt, returning HTTP 404 on a private repo when a bad
token is sent. **Never name a future heartbeat parameter `token`.** Re-verify
out-of-band if the parameter set changes or the calendar URL moves to a different
static-JSON host. The corresponding regression test was removed because the CDN
returned race-flaky results when the target file was being updated; see the
"Back assertions about external services" rule above for the policy.

## OTA Update Architecture

ArduinoOTA was removed in v3.08.0-wagfam. Updates are now delivered three ways:

| Method | Trigger | Rollback |
| --- | --- | --- |
| Web upload (`/update`) | Manual via browser | No (use `/updateFromUrl` to revert) |
| URL update (`/updateFromUrl`) | Manual via web form | Boot-confirmation rollback |
| Auto-update (calendar JSON) | `latestVersion` != `VERSION` | Boot-confirmation rollback |

**Boot-confirmation rollback:** Before every flash, a `/ota_pending.txt` record is written to LittleFS
with the current safe URL. If the device reboots twice without confirming (5 min stable uptime),
`checkOtaRollback()` re-flashes the previous firmware. See `docs/OTA_STRATEGY.md` for full details.

## REST API

See the [REST API section in README.md](README.md#rest-api) for the full endpoint table and
curl examples. All endpoints live under `/api/`, require HTTP Basic Auth, and return JSON.
Handlers are registered in `setup()` starting at ~line 284 of `marquee.ino`, with
implementations at ~lines 1351–1583. JSON-body POST endpoints (`/api/config`, `/api/fs/write`)
are wired through `AsyncCallbackJsonWebHandler` because `AsyncWebServer` does not populate
`request->arg("plain")` — see the registrations at ~lines 304–316.

## Key Constraints

- Flash memory is tight on ESP8266 — avoid large string literals on the stack;
  use the `F()` macro or `PROGMEM` / `FPSTR()` for string constants
  (see existing `CHANGE_FORM*` and `WEB_ACTIONS*` constants in [marquee.ino](marquee/marquee.ino))
- The LED display font is 5px wide + 1px spacer; `scrollMessageWait()` computes scroll distance from message length
- `WagFamBdayClient` uses BearSSL with `setInsecure()` (no cert validation) — intentional for embedded use
- ArduinoJson v7 is used (`^7.4` in `platformio.ini`) — do not generate v6-style
  `DynamicJsonDocument` / `StaticJsonDocument` code; use `JsonDocument` instead
- All `String` operations are expensive — prefer `reserve()` before building strings, avoid repeated `+=`
  in tight loops, and never allocate large `String` arrays on the stack
- `scrollMessageWait()` is blocking on the main task but does NOT need to pump the web server —
  `AsyncWebServer` runs in the background via TCP callbacks. The `delay(displayScrollSpeed)` between
  pixel steps yields to the system, which is enough for async TCP to make progress
- The web server is `AsyncWebServer`; the captive portal uses `ESPAsyncWiFiManager` sharing the same
  server instance. Do NOT include `<ESP8266WebServer.h>` or `<WiFiManager.h>` (tzapu) — their `HTTP_GET`
  enum constants collide with the async equivalents at link time
- `getWeatherData()` is the single orchestration point for both weather AND calendar data refresh — they
  always refresh together; it also triggers auto-OTA if `latestVersion` differs from `VERSION`
- `firmwareUrl` in the calendar config JSON must use `http://` — HTTPS is not supported by ESPhttpUpdate

## Known Issues

See `docs/CODE_REVIEW.md` for the full open-issues list. Top items by impact:

- `getWindDirectionText()` allocates 16 `String` objects on the stack every call — use `PROGMEM` array
- `cleanText()` does 35+ sequential `replace()` calls — heap fragmentation risk on long strings
- `savePersistentConfig()` always tail-calls `readPersistentConfig()` — fragile mutual recursion
- Config parser uses `indexOf("KEY=")` without `else if` — key collision risk if a URL contains
  another key name
- HTML page builders use `html +=` — replace static fragments with `server.sendContent(F("..."))`

## SecurityHelpers Module

`SecurityHelpers.h/.cpp` provides security functions extracted from `marquee.ino`:

- `isProtectedPath(path)` — prevents API writes/deletes to `/conf.txt` and `/ota_pending.txt`
- `extractDomain(url)` — parses domain from URLs (handles scheme-less, userinfo, port, query)
- `isTrustedFirmwareDomain(firmwareUrl, calendarUrl)` — validates OTA firmware URLs against
  a compile-time domain allowlist (`WAGFAM_TRUSTED_FIRMWARE_DOMAINS` in `Settings.h`, set to
  `"files-jrwagz.azurewebsites.net"` via `platformio.ini` build flags) plus the calendar source domain
- `isInTrustedDomainList(domain, list)` — checks domain membership in comma-separated allowlist

These are tested in `tests/native/test_security_helpers/`.

## Build Provenance

`scripts/build_version.py` is a PlatformIO pre-build script (declared in `platformio.ini` as
`extra_scripts = pre:scripts/build_version.py`) that injects a `BUILD_SUFFIX` into the
firmware `VERSION` macro at compile time:

- Local builds: `3.08.0-wagfam-<username>-<YYYYMMDD>-<hash>`
- CI builds: `3.08.0-wagfam-<hash>`
- Arduino IDE (no script): falls back to `BASE_VERSION` alone

Tested in `tests/scripts/test_build_version.py` with 100% coverage enforced by CI.

## What Was Removed from Upstream (Qrome/marquee-scroller)

These modules existed in the upstream repo and were deleted in this fork:

- `NewsApiClient` — news headline fetching (this is what `WagFamBdayClient` replaced)
- `OctoPrintClient` — 3D printer status
- `PiHoleClient` — Pi-hole DNS blocker stats
- Bitcoin price display (was already removed in upstream v3.0)
- TimeZoneDB API calls (timezone now derived from OWM response)

`sources.json` was a leftover from the upstream news feature and has been deleted.
