# OTA Rollback Test Results

> **Date:** 2026-04-26
> **Device:** Wemos D1 Mini (chip 5fc8ad) at 192.168.168.66
> **Starting firmware:** 3.08.0-wagfam (feature/security-hardening branch)
> **Tester:** dallanwagz + Claude Code

## Background

PR #20 introduced the boot-confirmation rollback system for OTA updates. During hardware
testing, 5 of 6 OTA scenarios were verified — but **Test 5 (crash-loop rollback)** could
not be completed because there was no way to remotely restart the device. The rollback path
requires two reboots within the 5-minute confirmation window, and the device was on a
different subnet with no physical access.

PR #23 added the REST API, including `POST /api/restart`. This endpoint enables the
previously-untestable rollback scenario to be triggered remotely.

This document covers the **full end-to-end OTA test suite** with complete rollback coverage.

---

## Test Infrastructure

### Firmware Builds

Three firmwares built from the `feature/security-hardening` branch — identical code, only
the `VERSION` macro changed:

| Firmware | VERSION | Size |
| --- | --- | --- |
| firmware_v3.08.bin | 3.08.0-wagfam | 580,224 bytes |
| firmware_v3.09.bin | 3.09.0-wagfam | 580,224 bytes |
| firmware_v3.10.bin | 3.10.0-wagfam | 580,224 bytes |

### Network Setup

The device is on a guest WiFi subnet (192.168.168.x) isolated from the dev machine
(192.168.1.x). Firmware files were served via:

- Local HTTP server: `python3 -m http.server 8888` in the test_firmware directory
- Public tunnel: `ssh -R 80:localhost:8888 nokey@localhost.run`
- Tunnel URL: `http://88278f316ee1d2.lhr.life`

ESPhttpUpdate requires HTTP (not HTTPS). The localhost.run tunnel accepts HTTP connections
and proxies them to the local server.

### Authentication

All API calls used HTTP Basic Auth: `admin:[DEVICE-PASSWORD]`

---

## Test 1: URL Update v3.08 to v3.09

**Goal:** Verify manual OTA update via `/updateFromUrl` endpoint.

**Input:**

```text
GET /updateFromUrl?firmwareUrl=http://88278f316ee1d2.lhr.life/firmware_v3.09.bin
Auth: admin:[DEVICE-PASSWORD]
```

**Result:** PASS

- Device returned: `STARTING UPDATE from http://88278f316ee1d2.lhr.life/firmware_v3.09.bin`
- Device went offline for ~126s (firmware download + flash + reboot + WiFi reconnect)
- Came back on version `3.09.0-wagfam`
- `ota_pending.txt` created with `boots=1`, `confirm_at=329755` (5-min timer started)
- `safe_url` preserved from previous state (old dead tunnel URL)
- `pending_url` set to the v3.09 firmware URL

**Post-update status:**

```json
{
    "version": "3.09.0-wagfam",
    "uptime_ms": 87379,
    "reset_reason": "Software/System restart",
    "ota": {
        "confirm_at": 329755,
        "pending_url": "http://88278f316ee1d2.lhr.life/firmware_v3.09.bin",
        "safe_url": "http://1ac81b1457a5d4.lhr.life/firmware_v3.09.bin",
        "pending_file_exists": true
    }
}
```

---

## Test 2: Boot Confirmation (5-Minute Timer)

**Goal:** Verify that after 5 minutes of stable uptime, the pending OTA record is confirmed.

**Method:** Polled `/api/status` every 60 seconds.

**Result:** PASS

| Poll | Uptime | Remaining | Pending |
| --- | --- | --- | --- |
| 1 | 100,387 ms | 229s | true |
| 2 | 160,717 ms | 169s | true |
| 3 | 221,007 ms | 108s | true |
| 4 | 281,378 ms | 48s | true |
| 5 | 341,636 ms | 0s | false |

**Post-confirmation status:**

```json
{
    "version": "3.09.0-wagfam",
    "uptime_ms": 341636,
    "ota": {
        "confirm_at": 0,
        "pending_url": "",
        "safe_url": "http://88278f316ee1d2.lhr.life/firmware_v3.09.bin",
        "pending_file_exists": false
    }
}
```

**Observations:**

- `ota_pending.txt` deleted on confirmation
- `safe_url` promoted to the v3.09 firmware URL (was the old dead tunnel)
- `confirm_at` reset to 0
- This safe_url now becomes the rollback target for the next update

---

## Test 3: URL Update v3.09 to v3.10

**Goal:** Set up the rollback scenario. After this update, `safe_url` points to v3.09
and `pending_url` points to v3.10.

**Input:**

```text
GET /updateFromUrl?firmwareUrl=http://88278f316ee1d2.lhr.life/firmware_v3.10.bin
Auth: admin:[DEVICE-PASSWORD]
```

**Result:** PASS

- Device went offline for ~126s, came back on `3.10.0-wagfam`
- `ota_pending.txt` created with confirmation timer running
- `safe_url = .../firmware_v3.09.bin` (the rollback target)
- `pending_url = .../firmware_v3.10.bin` (the firmware being validated)

**Post-update status:**

```json
{
    "version": "3.10.0-wagfam",
    "uptime_ms": 86230,
    "ota": {
        "confirm_at": 329611,
        "pending_url": "http://88278f316ee1d2.lhr.life/firmware_v3.10.bin",
        "safe_url": "http://88278f316ee1d2.lhr.life/firmware_v3.09.bin",
        "pending_file_exists": true
    }
}
```

---

## Test 4: Crash-Loop Rollback via Remote Restart

**Goal:** Verify that two unconfirmed boots trigger automatic rollback to `safe_url`.
This was the previously-untestable scenario from PR #20.

**Pre-conditions at trigger time:**

- Version: 3.10.0-wagfam
- Uptime: 98,136 ms (well before 5-min confirmation at 329,611 ms)
- `pending_file_exists: true`
- `safe_url: http://88278f316ee1d2.lhr.life/firmware_v3.09.bin`

**Input:**

```text
POST /api/restart
Auth: admin:[DEVICE-PASSWORD]
Headers: X-Requested-With: test
```

**Expected sequence:**

1. Device reboots (first reboot was the post-flash boot, this is the second)
2. `checkOtaRollback()` reads `/ota_pending.txt`, increments `boots` from 1 to 2
3. `boots >= 2` triggers rollback: `ESPhttpUpdate(safeUrl)` downloads v3.09
4. Device flashes v3.09 and reboots into it
5. On this clean boot, `/ota_pending.txt` is absent, so normal operation resumes

**Result:** PASS

- Restart API returned: `{"status": "restarting"}`
- Device was offline for 92 seconds total (reboot + rollback download + flash + second reboot)
- Came back online on **version 3.09.0-wagfam** — rollback successful
- `ota_pending.txt` absent (clean state)
- `safe_url` still points to v3.09 firmware

**Post-rollback status:**

```json
{
    "version": "3.09.0-wagfam",
    "uptime_ms": 35288,
    "free_heap": 33680,
    "heap_fragmentation": 1,
    "reset_reason": "Software/System restart",
    "ota": {
        "confirm_at": 0,
        "pending_url": "",
        "safe_url": "http://88278f316ee1d2.lhr.life/firmware_v3.09.bin",
        "pending_file_exists": false
    }
}
```

**Key observations:**

- The entire rollback cycle (reboot + crash-loop detection + firmware download + flash +
  final reboot) completed in 92 seconds — faster than a normal single update (~130s)
  because the rollback firmware download starts immediately on boot without waiting for
  WiFi manager timeout
- `heap_fragmentation: 1` — fresh boot after rollback has minimal fragmentation
- `confirm_at: 0` — no pending timer, device is in stable confirmed state
- The rollback firmware (v3.09) was NOT put into pending state — the device trusts
  the safe_url as already confirmed

---

## Test 5: Recovery Verification

**Goal:** Confirm the device is fully functional after rollback.

**Result:** PASS

| Check | Status |
| --- | --- |
| Config readable via `/api/config` | All keys present and correct |
| OWM API key | [set] |
| WagFam data URL | [set] |
| Web password | [set] |
| OTA status clean | No pending file, no confirmation timer |
| Filesystem intact | `/conf.txt` (543 bytes) — only file present |
| Data refresh via `/api/refresh` | Completed successfully |

---

## Test 6: Restore to v3.08

**Goal:** Return device to the original firmware version.

**Input:**

```text
GET /updateFromUrl?firmwareUrl=http://88278f316ee1d2.lhr.life/firmware_v3.08.bin
Auth: admin:[DEVICE-PASSWORD]
```

**Result:** PASS

- Device flashed and came back on `3.08.0-wagfam` after ~126s
- 5-minute boot confirmation completed normally
- `safe_url` promoted to v3.08 firmware URL
- Device fully operational with production config restored

**Final confirmed state:**

```json
{
    "version": "3.08.0-wagfam",
    "uptime_ms": 338177,
    "ota": {
        "confirm_at": 0,
        "pending_url": "",
        "safe_url": "http://88278f316ee1d2.lhr.life/firmware_v3.08.bin",
        "pending_file_exists": false
    }
}
```

---

## Summary

| Test | Description | Result |
| --- | --- | --- |
| 1 | URL update v3.08 to v3.09 | PASS |
| 2 | Boot confirmation (5-min stable uptime) | PASS |
| 3 | URL update v3.09 to v3.10 | PASS |
| 4 | **Crash-loop rollback via `/api/restart`** | **PASS** |
| 5 | Recovery verification after rollback | PASS |
| 6 | Restore to v3.08 | PASS |

**6/6 tests passed.** The crash-loop rollback (Test 4) — previously blocked by the lack
of a remote restart capability — is now fully verified. The boot-confirmation rollback
system works as designed: two unconfirmed boots within the 5-minute window trigger an
automatic re-flash of the safe firmware URL.

### Timing Summary

| Operation | Duration |
| --- | --- |
| Normal OTA flash + reboot | ~130s |
| Rollback cycle (reboot + detect + download + flash + reboot) | ~92s |
| Boot confirmation timer | 300s (5 minutes) |

### Closes

This test suite provides full coverage for the OTA rollback system described in
`docs/OTA_STRATEGY.md`. The previously-untestable crash-loop rollback path (noted as
"partially verified" in PR #20) is now confirmed working end-to-end on hardware.
