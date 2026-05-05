# Proposal: SPA Auto-Update Mirroring Firmware Auto-Update

**Status:** Draft for review
**Author:** Claude (proposal commissioned by @jrwagz)
**Date:** 2026-05-04

## Summary

Today the device automatically applies firmware updates pushed via the calendar
JSON, gated by a compile-time flag (`WAGFAM_AUTO_UPDATE_DISABLED`). The SPA
side, by contrast, only *detects* an available update and surfaces a manual
"Update SPA" button on the Status page — there is no auto-apply path. This
proposal adds an auto-apply path for the SPA bundle (LittleFS image), gated by
its own compile-time flag, while keeping the manual update button functional
in *every* build configuration. The detection logic, version-comparison flow,
and `/api/spa/update-from-url` endpoint already exist and are reused as-is.

---

## Background: How Firmware Auto-Update Works Today

These are the load-bearing pieces of the current firmware auto-update, captured
here as the baseline this proposal mirrors.

### Compile-time override

`WAGFAM_AUTO_UPDATE_DISABLED` is checked with `#ifdef` at
[marquee/marquee.ino:1049-1067](../../marquee/marquee.ino). When defined, the
auto-apply branch is skipped entirely; the rest of the calendar fetch / version
detection still runs. Set via PlatformIO build flag
`-DWAGFAM_AUTO_UPDATE_DISABLED=1`. Default is *enabled* (flag absent).

### Trigger

Auto-update is checked inside `getWeatherData()` — same call site as the weather
fetch — and therefore fires:

- On every calendar refresh (`minutesBetweenDataRefresh`, default 15 min)
- On a forced refresh from `/api/refresh` or the legacy `/pull`
- Implicitly at boot, since the first refresh runs shortly after WiFi connect

Two additional gates:

- `otaConfirmAt == 0` — prevents kicking off a new flash while a prior flash is
  still in its 5-minute confirmation window
- `millis() > OTA_CONFIRM_MS` (5 min) — prevents flashing again immediately
  after boot, which is what `checkOtaRollback()` relies on to detect crash loops

### Source of truth

The calendar JSON's `config` block carries `latestVersion` and `firmwareUrl`
(see [`WagFamBdayClient.h`](../../marquee/WagFamBdayClient.h) and
[CLAUDE.md → WagFamBdayClient](../../CLAUDE.md)). Each field has a paired
`*Valid` boolean so partial config doesn't clobber existing values. Comparison
against the running build is **simple string equality** against the `VERSION`
macro — no semver parsing.

### Fetch + apply

Auto-apply path: `performAutoUpdate(firmwareUrl)` →
`ESPhttpUpdate.update(client, url)`. The async handler / interval check sets
flags; the actual flash is deferred to the main loop in `processEverySecond()`
at [marquee.ino:601-615](../../marquee/marquee.ino), since
`ESPhttpUpdate.update()` blocks for ~20–30 s and would otherwise stall the
async event loop.

Domain allowlist: `isTrustedFirmwareDomain()` validates the URL against
`WAGFAM_TRUSTED_FIRMWARE_DOMAINS` plus the calendar source domain
(see `SecurityHelpers.cpp`).

### Rollback

Before each flash, `/ota_pending.txt` records `safeUrl=` (last known-good
firmware URL), `newUrl=`, and `boots=0`. `checkOtaRollback()` re-flashes
`safeUrl` if the device reboots twice without confirming 5 minutes of stable
uptime. Confirmation marks the new build as the new safe URL. See
[`docs/OTA_STRATEGY.md`](../OTA_STRATEGY.md).

### User-visible behavior

During flash: LED matrix scrolls "...Updating..." and freezes there until the
device reboots into the new firmware. On the SPA, the Status page already
displays an "SPA Update Available" banner with current vs available versions
and a manual "Update SPA" button (added in PR #91, commit 6763b95) — that
banner is the manual surface this proposal preserves.

### What already exists for SPA

- **Detection:** `spaUpdateAvailable`, `pendingSpaFsUrl`, `pendingSpaVersion`
  set in `getWeatherData()` at [marquee.ino:1069-1081](../../marquee/marquee.ino)
- **Status exposure:** `/api/status` returns `spa_version`, `spa_update_available`,
  `spa_fs_url`, `spa_latest_version`
- **Manual apply endpoint:** `POST /api/spa/update-from-url` queues
  `otaFsFromUrlRequested`, executed in the main loop via `doOtaFsFlash()` at
  [marquee.ino:617-632](../../marquee/marquee.ino), which preserves
  `/conf.txt` across the LittleFS rewrite
- **Manual UI:** Status page banner with "Update SPA" button

What's missing: the *automatic* trigger that calls the manual apply path
without user click.

---

## Proposed SPA Auto-Update Design

### Where SPA assets live

LittleFS partition (`0x300000` for the `d1_mini 4MB FS:1MB OTA:~1019KB`
layout). Update unit is the full LittleFS image, written via
`Update.begin(fsSize, U_FS)`. This is the same image users already flash via
`/updatefs` or `/api/spa/update-from-url`.

### Auto-apply trigger

Inside `getWeatherData()`, immediately after the existing SPA-detection block
(currently lines 1069–1081), add an `#ifndef`-gated block that — when an
update is detected and gates pass — sets `otaFsFromUrlRequested = true` and
`pendingOtaFsUrl = serverConfig.spaFsUrl`. The main loop's existing handler
at lines 617–632 picks it up and runs `doOtaFsFlash()`. **No new flash code
path is introduced; the auto trigger reuses the manual apply path.**

Gating conditions, mirroring firmware:

- `serverConfig.latestSpaVersionValid && serverConfig.spaFsUrlValid`
- `serverConfig.latestSpaVersion != SPA_VERSION` (string equality, matches firmware)
- `serverConfig.spaFsUrl.startsWith("http://")` (HTTP only, matches firmware)
- `otaConfirmAt == 0` and `millis() > OTA_CONFIRM_MS` — *shared* with firmware
  auto-update so the two cannot stomp each other in the same boot window
- `!otaFsFromUrlRequested` — don't re-queue if a flash is already pending
- Domain check via `isTrustedFirmwareDomain(spaFsUrl, WAGFAM_DATA_URL,
  WAGFAM_TRUSTED_FIRMWARE_DOMAINS)` — same allowlist that protects firmware

### Trigger conditions: when does auto-check vs auto-apply happen?

Auto-**check** (detection) is unchanged: every calendar refresh sets
`spaUpdateAvailable`. This is what powers the manual banner and must remain
ungated.

Auto-**apply** fires on the same calendar-refresh cadence as firmware
auto-update — which means within ~15 min of boot, then every 15 min thereafter,
suppressed during the 5-minute boot confirmation window. This matches the
firmware behavior verbatim, which is the point.

### Compile-time override

Introduce `WAGFAM_SPA_AUTO_UPDATE_DISABLED` (separate from the firmware flag —
see [Decision #1](#decisions-i-need-from-you)). Set via PlatformIO
`build_flags = -DWAGFAM_SPA_AUTO_UPDATE_DISABLED=1`. The flag wraps **only**
the auto-trigger block; detection, the API endpoint, and the Status-page
banner remain in the binary.

```cpp
#ifdef WAGFAM_SPA_AUTO_UPDATE_DISABLED
  Serial.println(F("[SPA] Auto-update disabled at build time (WAGFAM_SPA_AUTO_UPDATE_DISABLED)"));
#else
  if (spaUpdateAvailable && pendingSpaFsUrl.length() > 0
      && otaConfirmAt == 0 && millis() > OTA_CONFIRM_MS
      && !otaFsFromUrlRequested
      && isTrustedFirmwareDomain(pendingSpaFsUrl, WAGFAM_DATA_URL, WAGFAM_TRUSTED_FIRMWARE_DOMAINS)) {
    Serial.println("[SPA] Auto-applying SPA update: " + pendingSpaVersion);
    pendingOtaFsUrl = pendingSpaFsUrl;
    otaFsFromUrlRequested = true;
  }
#endif
```

### Version comparison

Reuse the existing string-equality check (`latestSpaVersion != SPA_VERSION`)
already in place at marquee.ino:1071. No semver, no redesign.

### Atomicity & recovery

`doOtaFsFlash()` already:

1. Reads `/conf.txt` into RAM
2. Calls `LittleFS.end()`
3. Streams new image to `Update.begin(fsSize, U_FS)` / `Update.write()`
4. On success, re-mounts LittleFS and writes `/conf.txt` back
5. On failure, attempts to re-mount and restore config

The proposal does **not** touch this code. Open questions about strengthening
it appear in the decisions section ([#5](#decisions-i-need-from-you)).

---

## Compile-Time Override Behavior (Matrix)

| Build configuration | Detection | Manual button | Auto-apply |
| --- | --- | --- | --- |
| Default (neither flag set) | ✅ | ✅ | ✅ firmware + ✅ SPA |
| `-DWAGFAM_AUTO_UPDATE_DISABLED=1` only | ✅ | ✅ | ❌ firmware, ✅ SPA |
| `-DWAGFAM_SPA_AUTO_UPDATE_DISABLED=1` only | ✅ | ✅ | ✅ firmware, ❌ SPA |
| Both flags set | ✅ | ✅ | ❌ both |

Detection and the manual button **never** turn off via compile flag. That's
the load-bearing distinction this proposal makes: the compile flag gates the
*automatic trigger*, not the *capability*.

---

## Manual Update Path (Always Available)

The manual path already exists end-to-end and works regardless of the
auto-update flag. It is preserved unchanged:

- **UI:** The Status page banner (from PR #91) shows "SPA Update Available —
  Current: X.Y.Z → Available: A.B.C" with an "Update SPA" button.
- **On click:** SPA calls `POST /api/spa/update-from-url` with
  `{"url": "<spa_fs_url>"}`. The endpoint queues the deferred flash; the main
  loop picks it up.
- **Progress feedback:** LED matrix scrolls "...Updating SPA..." while the
  flash runs. The SPA in the browser will lose connectivity briefly across
  the LittleFS rewrite + reboot; the user reloads the page to see the new
  version. Improving in-browser progress feedback is
  [Decision #6](#decisions-i-need-from-you).
- **Failure handling:** If `doOtaFsFlash()` fails, `/conf.txt` is restored
  and the device continues running the previous SPA bundle. Failure is
  visible in serial logs; surfacing it in the SPA UI is also
  [Decision #6](#decisions-i-need-from-you).

The manual button must remain visible *whenever* `spa_update_available` is
true in `/api/status`, independent of the compile flag — see Decision #2.

---

## Out of Scope

- Changing the firmware auto-update mechanism itself
- Adding semver comparison (firmware uses string equality; SPA mirrors that)
- Signature/checksum verification of the LittleFS image (separate proposal —
  see Decision #5)
- HTTPS for SPA image downloads (firmware also requires `http://`; aligning
  with that is intentional, but a future TLS proposal would benefit both)
- Reworking the boot-confirmation rollback to cover SPA flashes
  (see Decision #4)
- Per-device opt-out via runtime config (see Decision #3)

---

## Decisions I Need From You

Each item is independently addressable — please leave inline review comments
per number and tick the checkbox in the PR body when resolved.

### 1. Separate flag or shared flag?

- [ ] **Decide between separate `WAGFAM_SPA_AUTO_UPDATE_DISABLED` vs. reuse `WAGFAM_AUTO_UPDATE_DISABLED` for both.**

**Question:** Should SPA auto-update be gated by a *new* compile flag, or
should the existing `WAGFAM_AUTO_UPDATE_DISABLED` cover both firmware and SPA?

**Options:**

- **A. Separate flag** (`WAGFAM_SPA_AUTO_UPDATE_DISABLED`) — independent
  control; users can disable firmware auto-update while still letting SPA
  auto-update flow.
- **B. Reuse one flag** — simpler mental model; "auto-updates off" means all
  auto-updates off.
- **C. Shared flag with a separate opt-in** (e.g. `WAGFAM_AUTO_UPDATE_DISABLED`
  disables both; a secondary `WAGFAM_SPA_AUTO_UPDATE_ONLY` re-enables SPA).

**Recommendation:** A (separate flag). Rationale: firmware auto-update was
disabled in the past specifically to protect *locally-built firmware* from
being clobbered by the server's published version (per the comment at
marquee.ino:1043-1048). That scenario doesn't apply to the SPA — there's no
"locally-built SPA running on a device" foot-gun. Coupling them forces users
who want firmware-build protection to also forgo SPA updates.

**Trade-off:** Two flags is one more knob to document.

### 2. Manual button visibility when auto-update is disabled

- [ ] **Confirm the manual "Update SPA" button stays visible whenever an update is detected, regardless of compile flags.**

**Question:** When `WAGFAM_SPA_AUTO_UPDATE_DISABLED` is set, should the
manual button still appear in the Status banner?

**Options:**

- **A. Always visible when detection fires** — the compile flag controls only
  the automatic trigger.
- **B. Hide button when flag is set** — interpret "auto-update disabled" as
  "all update prompts off."

**Recommendation:** A. The user explicitly asked for this in the task brief
("the user still has the ability to manually decide to update via the SPA
interface"). Documenting it here so it's nailed down before code lands.

**Trade-off:** Minor — this is more about explicit confirmation than a real
choice.

### 3. Runtime opt-out vs. compile-time only

- [ ] **Decide whether to support a runtime "disable SPA auto-update" setting
  (web UI / `/conf.txt`) in addition to the compile flag.**

**Question:** Firmware auto-update is currently compile-time-only. Should
SPA auto-update follow that same constraint, or get a runtime toggle as
well?

**Options:**

- **A. Compile-time only** — strict mirror of firmware, simplest.
- **B. Compile-time + runtime** — runtime setting (in `/conf.txt`, exposed as
  a checkbox on the Settings tab) layered on top of the compile flag. Compile
  flag forces off; runtime toggle controls within builds where compile flag
  is unset.

**Recommendation:** A for v1, since the brief emphasizes mirroring firmware
exactly. If runtime control is desired, propose it as a follow-up that
covers *both* firmware and SPA together rather than diverging here.

**Trade-off:** B gives end-users (not just builders) control; A keeps the
proposal small and matches firmware behavior 1:1.

### 4. Boot-confirmation rollback — extend to SPA flashes?

- [ ] **Decide whether SPA auto-flashes should write an `/ota_pending.txt`-style record and participate in `checkOtaRollback()`.**

**Question:** Firmware flashes are protected by a 5-minute boot-confirmation
rollback (`/ota_pending.txt` + `checkOtaRollback()`). SPA flashes currently
don't participate in that mechanism. Should auto-applied SPA updates extend
it?

**Options:**

- **A. No rollback for SPA** — SPA failures don't brick the device (firmware
  still boots; serial console works; `/api/spa/update-from-url` can be
  re-driven manually with a known-good URL). Status quo.
- **B. Add a SPA-specific rollback record** — track previous-known-good
  `spa_fs_url`, re-flash if device reboot-loops within 5 min of SPA flash.
  Requires a separate pending file (e.g. `/spa_pending.txt`) so it doesn't
  collide with firmware's record.
- **C. Unify both into one OTA record** — single `/ota_pending.txt` covers
  firmware OR SPA flash; bigger refactor of `checkOtaRollback()`.

**Recommendation:** A for v1 — a bad SPA bundle is a soft failure (UI doesn't
load) rather than a hard one (device unbootable), and the user has a
working `/api/spa/update-from-url` lever. Revisit if we see real-world
SPA bricks.

**Trade-off:** A trades resilience for simplicity. B/C better matches the
"mirror firmware" goal but expands scope significantly.

### 5. Integrity verification of the SPA bundle

- [ ] **Decide what (if any) integrity check the SPA fetcher performs before flashing.**

**Question:** Firmware OTA today relies on the trusted-domain allowlist plus
ESPhttpUpdate's content-length / magic-byte sanity. No checksum or signature.
Should auto-applied SPA updates do better, the same, or stay in lockstep
with firmware?

**Options:**

- **A. Match firmware** — domain allowlist only.
- **B. Add SHA256** — calendar JSON publishes `spa_fs_sha256`; device
  verifies before committing the flash.
- **C. Signed images** — Ed25519 sig in calendar JSON, public key baked into
  firmware. Strongest, biggest scope.

**Recommendation:** A for this proposal (matches "mirror firmware exactly").
B is a small, valuable upgrade and could be done as a separate proposal that
upgrades *both* paths (since the same publishing infrastructure handles both
binaries).

**Trade-off:** A is consistent with current firmware. B costs ~3 KB of code
(SHA256 streaming) plus a server-side change. C requires key management.

### 6. Browser-side progress feedback during SPA flash

- [ ] **Decide how aggressively to surface flash progress / failure in the SPA UI.**

**Question:** Once the user clicks "Update SPA" (manual) or auto-update fires
in the background, the device's webserver tears down LittleFS for ~15–30 s,
then reboots. The browser sees a hung connection. What feedback do we want?

**Options:**

- **A. None** — current behavior. User reloads when ready.
- **B. Pre-flash modal** — SPA shows "Updating, page will reload" + a
  countdown + auto-retry of `/api/status` every 2 s until it returns the new
  version, then reload.
- **C. B + auto-update notice** — when auto-update is the trigger, the SPA
  briefly shows a non-blocking toast ("Device is auto-updating SPA…")
  before the disconnect.

**Recommendation:** B. C is harder because the SPA only learns about a
pending auto-apply if it's polling `/api/status` at the moment the flash
fires; a polling cadence of ~30 s would catch most cases.

**Trade-off:** A is zero-cost but rough. B is the obvious win. C is nicer
but adds polling and a small race window.

### 7. Auto-apply suppression after recent firmware flash

- [ ] **Confirm the proposed reuse of `OTA_CONFIRM_MS` and `otaConfirmAt` to suppress concurrent firmware + SPA flashes.**

**Question:** The proposed gating uses the *same* `otaConfirmAt == 0` and
`millis() > OTA_CONFIRM_MS` checks that firmware auto-update uses, so a SPA
flash won't fire during the 5-minute confirmation window after a firmware
flash. Is that the right semantic?

**Options:**

- **A. Share the gates** (proposed). One auto-flash at a time across
  firmware+SPA, simplest.
- **B. Independent gates** — separate `spaOtaConfirmAt` so SPA can flash
  even mid-firmware-confirmation. Risks two reboots back-to-back.
- **C. Strict ordering** — if both firmware and SPA updates are available
  simultaneously, do firmware first, SPA on the *next* refresh cycle.

**Recommendation:** A. Two flashes in quick succession is more likely to
hit the rollback window of the first (since the SPA flash also reboots
the device). Serializing them is simpler and safer.

**Trade-off:** A delays SPA update by up to 15 min when bundled with a
firmware update. Acceptable.

### 8. Telemetry: heartbeat field for "auto-update applied"

- [ ] **Decide whether to add a heartbeat parameter (e.g. `last_auto_apply_ts`)
  so the server knows which devices took the update.**

**Question:** The calendar fetch already sends a heartbeat (chip_id,
version, uptime, heap, rssi, utc_offset_sec). Should we add one indicating
"this device just auto-applied an update"?

**Options:**

- **A. No new field** — server can already infer from `version`/`spa_version`
  shifting between heartbeats. Status quo.
- **B. Add `last_auto_apply_ts` and `last_auto_apply_kind=spa|firmware`** —
  helpful for the server to dashboard auto-update success rates.

**Recommendation:** A. Heartbeat parameter changes need to be empirically
re-verified against `raw.githubusercontent.com` per the
"Back assertions about external services" rule in CLAUDE.md, so a new
field has non-trivial verification cost. Skip unless the server side
actually plans to consume it.

**Trade-off:** B is observability nicely-to-have; A keeps the static-host
compatibility envelope intact.

### 9. Surface to study: anything in firmware auto-update that doesn't translate cleanly?

- [ ] **Review and respond to the firmware-mechanism oddity flagged below.**

During the firmware-mechanism study I noticed one thing that doesn't have a
clean SPA analog and needs your call:

The firmware path uses `ESPhttpUpdate.update(client, url)`, which manages
connection, content-length, and reboot internally. The SPA path uses a
hand-rolled `WiFiClient` stream into `Update.begin(U_FS)` / `Update.write()`
specifically so that `/conf.txt` can be read into RAM before
`LittleFS.end()` and written back after. That asymmetry already exists in
the codebase pre-proposal — but it means that if we ever want to add
SHA256 verification, signing, or progress callbacks, the firmware and SPA
paths will need parallel-but-separate implementations instead of a shared
helper. Worth flagging as a constraint when we look at Decision #5.

**No options — just confirming you're aware** of this asymmetry before
greenlighting any of the upgrade paths in #5.

---

## Appendix: Summary of files this proposal would touch (estimate)

For sizing purposes only — no code is written in this PR.

- `marquee/marquee.ino` — add ~15 lines of `#ifdef`-gated block in
  `getWeatherData()` near line 1081
- `marquee/Settings.h` — comment documenting the new flag (parallel to
  existing `WAGFAM_AUTO_UPDATE_DISABLED` doc)
- `platformio.ini` — no required change; flag is opt-in
- `CLAUDE.md` — short note in the OTA section describing the new flag
- `README.md` — table updated to mention SPA auto-update parity with firmware
- `webui/` — only if Decision #6 picks option B/C (browser progress modal)

No changes proposed to `WagFamBdayClient`, `SecurityHelpers`,
`doOtaFsFlash()`, or the rollback mechanism.
