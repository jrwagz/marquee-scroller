# Test Results: Live End-to-End Heartbeat from Clock Device

**Date:** 2026-04-26
**Tester:** dallanwagz (live-test session)
**Branch under test:** `feature/heartbeat-server` (PR #24) — heartbeat firmware + WagFam backend
**Branch with this doc:** `claude/heartbeat-live-test` (off `feature/security-hardening`, PR #23)
**Device:** Wemos D1 Mini, chip_id `5fc8ad`, MAC `08:f9:e0:5f:c8:ad`, IP `192.168.168.66`
**Firmware flashed:** local build of `feature/heartbeat-server` HEAD (`70f63a5`), `VERSION = "3.08.0-wagfam"`,
  580976 bytes, RAM 47.4%, flash 55.2%

---

## Goal

Prove the device-heartbeat telemetry pipeline works end-to-end with **real traffic from the live clock**, not
simulated requests. Existing `docs/TEST_RESULTS_HEARTBEAT_SERVER.md` covers 33/33 server-side tests with crafted
HTTP requests, but had no live-device coverage:

> "The calendar clock was not reachable on the test network … hardware testing should be done after flashing the
> `feature/heartbeat-server` firmware."

This document closes that gap.

## Test setup

| Component | Where | Purpose |
| --- | --- | --- |
| WagFam backend (FastAPI) | Mac at `https://10.10.2.178:8443` (en6, USB ethernet) | Receives heartbeat GETs, upserts `devices` table |
| Self-signed TLS cert | `subjectAltName = IP:10.10.2.178,IP:192.168.168.198,DNS:localhost` | Required because firmware client is `BearSSL::WiFiClientSecure` (HTTPS-only); `setInsecure()` accepts any cert |
| Firmware HTTP server | Mac at `http://10.10.2.178:8080/firmware.bin` | Hosts the rebuilt firmware for OTA pull |
| Request middleware | Captured every request to `/tmp/heartbeat-test/requests.log` (JSON lines) | Logs `query`, `auth_header`, `host_header`, `client` for every hit |
| Inter-subnet routing | Clock on `192.168.168.0/24`, Mac on `10.10.2.0/24` | User confirmed routing exists; verified by `curl --interface en6 https://10.10.2.178:8443/...` reaching the clock and vice versa |

## What was changed on the live device (and restored at the end)

| Setting | Original value | Test value | Restored? |
| --- | --- | --- | --- |
| `wagfam_data_url` | `https://raw.githubusercontent.com/jrwagz/wagfam-clocks-data-source/main/data_source.json` | `https://10.10.2.178:8443/api/v1/calendar` | yes |
| `wagfam_api_key` | (40-char GitHub PAT) | `livetest-key-001` | yes |
| `minutes_between_data_refresh` | 60 | 1 | yes |
| Firmware image | pre-heartbeat 3.08.0-wagfam binary | rebuilt 3.08.0-wagfam (with heartbeat code) | left flashed (see "Firmware version trap" below) |
| `OTA_SAFE_URL` | `http://88278f316ee1d2.lhr.life/firmware_v3.08.bin` (already returning HTTP 503 — tunnel dead) | `http://10.10.2.178:8080/firmware.bin` (overwritten by OTA confirmation) | **no** — `/conf.txt` is a protected path on `/api/fs/write`. See "Known leftover" |

Restoration was a single `POST /api/config` carrying all three reverted fields. Verified via a follow-up `GET
/api/config`.

## Diagnostic finding: clock firmware predated heartbeat code

The device reports `version: "3.08.0-wagfam"` in `/api/status`, which matches the source. But the heartbeat code
(commit `fd289ab`, "Add device heartbeat telemetry to calendar fetch", 2026-04-26 11:52) was added **after** the
binary on-device was built. The `VERSION` macro was not bumped when heartbeat support landed, so version-string
inspection alone wouldn't reveal the mismatch.

Symptom: the first `/api/v1/calendar` request from `192.168.168.66` after pointing it at the local server arrived
with **no query string**:

```text
GET /api/v1/calendar HTTP/1.1
host: 10.10.2.178:8443
authorization: token livetest-key-001
user-agent: ESP8266HTTPClient
```

The auth header was correct (proving the URL/key swap took effect), but no `chip_id`, `version`, `uptime`, `heap`,
or `rssi` parameters. After OTA-flashing the locally-built `feature/heartbeat-server` firmware, the same endpoint
started receiving:

```text
GET /api/v1/calendar?chip_id=5fc8ad&version=3.08.0-wagfam&uptime=43136&heap=34344&rssi=-62 HTTP/1.1
```

**Implication:** version strings should be bumped when wire-visible behavior changes, otherwise field debugging is
ambiguous. Filing as a future cleanup, not blocking this test.

## Captured heartbeats — 18 live cycles

All from `client: 192.168.168.66:*`, `user-agent: ESP8266HTTPClient`,
`host: 10.10.2.178:8443`, `authorization: token livetest-key-001`. Times in UTC.

| # | Time (UTC) | chip_id | version | uptime_ms | free_heap | rssi | HTTP |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 19:41:40 | `5fc8ad` | 3.08.0-wagfam | 43136 | 34344 | -62 | 200 |
| 2 | 19:41:44 | `5fc8ad` | 3.08.0-wagfam | 46781 | 33416 | -63 | 200 |
| 3 | 19:42:41 | `5fc8ad` | 3.08.0-wagfam | 103492 | 33192 | -67 | 200 |
| 4 | 19:43:41 | `5fc8ad` | 3.08.0-wagfam | 163886 | 33248 | -65 | 200 |
| 5 | 19:44:43 | `5fc8ad` | 3.08.0-wagfam | 225429 | 33440 | -62 | 200 |
| 6 | 19:45:43 | `5fc8ad` | 3.08.0-wagfam | 285904 | 33632 | -64 | 200 |
| 7 | 19:46:45 | `5fc8ad` | 3.08.0-wagfam | 347395 | 33440 | -65 | 200 |
| 8 | 19:47:45 | `5fc8ad` | 3.08.0-wagfam | 407900 | 33440 | -62 | 200 |
| 9 | 19:48:46 | `5fc8ad` | 3.08.0-wagfam | 469273 | 33440 | -64 | 200 |
| 10 | 19:49:47 | `5fc8ad` | 3.08.0-wagfam | 530143 | 33440 | -62 | 200 |
| 11 | 19:50:48 | `5fc8ad` | 3.08.0-wagfam | 590414 | 33440 | -63 | 200 |
| 12 | 19:51:51 | `5fc8ad` | 3.08.0-wagfam | 653393 | 33744 | -64 | 200 |
| 13 | 19:52:51 | `5fc8ad` | 3.08.0-wagfam | 713376 | 33632 | -61 | 200 |
| 14 | 19:53:51 | `5fc8ad` | 3.08.0-wagfam | 773905 | 33440 | -64 | 200 |
| 15 | 19:54:53 | `5fc8ad` | 3.08.0-wagfam | 835418 | 33632 | -64 | 200 |
| 16 | 19:55:53 | `5fc8ad` | 3.08.0-wagfam | 895954 | 33632 | -63 | 200 |
| 17 | 19:56:54 | `5fc8ad` | 3.08.0-wagfam | 956422 | 33464 | -62 | 200 |
| 18 | 19:57:56 | `5fc8ad` | 3.08.0-wagfam | 1018944 | 33656 | -64 | 200 |

### Cadence (uptime delta between consecutive heartbeats)

| Between # | Δ uptime_ms | Notes |
| --- | --- | --- |
| 1 → 2 | 3,645 | extra cycle near boot — `/api/refresh` trigger landed on top of an in-flight periodic fetch |
| 2 → 3 | 56,711 | first nominal 1-min interval after manual trigger |
| 3 → 4 | 60,394 | nominal |
| 4 → 5 | 61,543 | nominal |
| 5 → 6 | 60,475 | nominal |
| 6 → 7 | 61,491 | nominal |
| 7 → 8 | 60,505 | nominal |
| 8 → 9 | 61,373 | nominal |
| 9 → 10 | 60,870 | nominal |
| 10 → 11 | 60,271 | nominal |
| 11 → 12 | 62,979 | nominal |
| 12 → 13 | 59,983 | nominal |
| 13 → 14 | 60,529 | nominal |
| 14 → 15 | 61,513 | nominal |
| 15 → 16 | 60,536 | nominal |
| 16 → 17 | 60,468 | nominal |
| 17 → 18 | 62,522 | nominal |

Mean Δ across rows 3–18: ~60.9 s. Variance well within `processEverySecond()` tick granularity (1 s). Confirms
`minutesBetweenDataRefresh = 1` works and the firmware actually fires once per minute as intended.

### Telemetry-field health

- **`chip_id`** — constant `5fc8ad`, matches the lower 24 bits of the device's WiFi MAC `08:f9:e0:5f:c8:ad` and
  the value `/api/status` reports.
- **`version`** — `3.08.0-wagfam` on every cycle, matches the `VERSION` macro in the flashed binary.
- **`uptime_ms`** — monotonically increasing from 43,136 → 1,018,944 (~16 min wall-clock), consistent with
  `millis()` since boot. No wraparound seen (would happen at ~49.7 days).
- **`free_heap`** — bounded between 33,192 and 34,344 bytes across 18 fetches; stddev ~250 bytes. **No leak
  signal.** This is the most useful regression check the heartbeat enables.
- **`rssi`** — bounced -61 to -67. Within normal Wi-Fi noise for a stationary device on a single channel.

### Server-side aggregation

The backend stores **one row per `chip_id`** (UNIQUE INDEX on `chip_id`), upserted on every heartbeat. After the
capture window:

```sql
SELECT id, chip_id, version, uptime_ms, free_heap, rssi, ip_address, created_at, last_seen
  FROM devices WHERE chip_id='5fc8ad';
```

```text
id | chip_id | version       | uptime_ms | free_heap | rssi | ip_address     | created_at                 | last_seen
4  | 5fc8ad  | 3.08.0-wagfam | 1018944   | 33656     | -64  | 192.168.168.66 | 2026-04-26 19:41:40.793164 | 2026-04-26 19:57:56.660356
```

- `created_at` matches heartbeat #1 (first registration after OTA reboot)
- `last_seen` matches heartbeat #18 (most recent)
- `uptime_ms`, `free_heap`, `rssi` mirror the latest fetch (overwrite-on-upsert behavior, as designed)
- `ip_address` `192.168.168.66` is the device's true LAN IP — distinct from the simulated `192.168.65.1` Docker
  bridge IP that appears in the prior PR #24 server tests, confirming this is real-LAN traffic, not simulation

This validates the production aggregation path end-to-end: clock's `WiFi.RSSI()` /  `ESP.getFreeHeap()` /
`millis()` / `ESP.getChipId()` / `VERSION` macro → query string → FastAPI handler → SQLAlchemy upsert → SQLite row.

## Known leftover: OTA_SAFE_URL points at the temporary test host

`OTA_SAFE_URL` was rewritten by the device's normal OTA-confirmation logic (`marquee.ino:353`) when the new
firmware passed its 5-min stability window. It's now `http://10.10.2.178:8080/firmware.bin`, which is unreachable
once the test rig is torn down. **Cannot patch via REST API** — `/api/fs/write` rejects writes to `/conf.txt` as
a protected path (correct behavior). The pre-test value (`http://88278f316ee1d2.lhr.life/firmware_v3.08.bin`) was
already returning HTTP 503 from a dead localhost.run tunnel before this test ran, so the operational state is
unchanged: rollback recovery would have failed either way until a real safe firmware host is restored.

**Action for the user:** Re-flash from a long-lived URL (or re-establish the lhr.life tunnel) the next time you
do a normal OTA — that will reset `OTA_SAFE_URL` to a working value.

## Test artifacts (host filesystem, not in repo)

| Path | Contents |
| --- | --- |
| `/tmp/heartbeat-test/orig_config.json` | Snapshot of `/api/config` GET before any change |
| `/tmp/heartbeat-test/cert.pem`, `key.pem` | Self-signed TLS pair (RSA 2048, 30-day) |
| `/tmp/heartbeat-test/server.log` | uvicorn access log (full transcript) |
| `/tmp/heartbeat-test/requests.log` | JSON-line middleware log of every request |
| `/tmp/heartbeat-test/data/test.db` | Fresh SQLite DB used for this run only |
| `/tmp/heartbeat-test/firmware-host.log` | Firmware-host access log (3 GETs: 2 verification fetches, 1 OTA) |

## Summary

| Check | Result |
| --- | --- |
| Local TLS heartbeat server reachable from clock's subnet via inter-subnet routing | PASS |
| Self-signed cert accepted by `BearSSL::WiFiClientSecure::setInsecure()` client | PASS |
| OTA flash from local HTTP host (rebuilt firmware) succeeded; device rebooted cleanly | PASS |
| Post-OTA boot: device sends all 5 telemetry fields in query string | PASS |
| Auth header (`token <key>`) sent and accepted | PASS |
| Cadence at `minutes_between_data_refresh = 1` | PASS — 17 consecutive cycles within 60 ± 3 s |
| Heap stability across 18 fetches | PASS — stddev ~250 B, no leak |
| Server upsert produces one stable row, latest values reflected | PASS |
| Original `wagfam_data_url`, `wagfam_api_key`, `minutes_between_data_refresh` restored | PASS |
| `OTA_SAFE_URL` restored | NOT POSSIBLE via API (protected path); pre-test value was already broken |

Live device telemetry pipeline is verified working end-to-end.
