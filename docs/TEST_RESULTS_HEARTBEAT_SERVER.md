# Test Results: Device Heartbeat & WagFam Backend Server

**Date:** 2026-04-26
**Tester:** dallanwagz
**Branch:** `feature/heartbeat-server` (PR #24)
**Server version:** 1.0.0
**Environment:** Docker container (`local` profile, SQLite) on macOS, LAN 192.168.168.0/24

---

## Build Verification

| Check | Result | Details |
| --- | --- | --- |
| `make lint` | PASS | 0 markdownlint errors (11 files) |
| `make test` | PASS | 84/84 native C++ tests (4.6s) |
| `make build` | PASS | Flash 55.2%, RAM 47.4% |
| `cd server && make test` | PASS | 28/28 pytest tests (0.07s) |
| `docker compose build` | PASS | Image built successfully |
| `docker compose --profile local up` | PASS | Container starts, uvicorn listening on :8000 |

---

## Server E2E Tests

All tests run against the Docker container at `localhost:8000` with `WAGFAM_API_KEY=test-key-123`.

### Test 1: Health Check

```text
GET /health
```

**Result:** PASS

```json
{"status": "ok", "version": "1.0.0"}
```

### Test 2: Calendar Without Heartbeat

```text
GET /api/v1/calendar
```

**Result:** PASS — Returns real calendar data for today (2026-04-26).

```json
[
    {"config": {"eventToday": 0}},
    {"message": "6 days: Kvitka's 14th Birthday (2/May)"},
    {"message": "7 days: Dallan's 34th Birthday (3/May)"},
    {"message": "10 days: Emily's 46th Birthday (6/May)"}
]
```

### Test 3: Calendar With Heartbeat (Device Auto-Registration)

```text
GET /api/v1/calendar?chip_id=5fc8ad&version=3.08.0-wagfam&uptime=1234567&heap=32496&rssi=-62
```

**Result:** PASS — Calendar returned, device auto-registered.

### Test 4: Device Auto-Registered

```text
GET /api/v1/devices (Authorization: token test-key-123)
```

**Result:** PASS — Device appears with all telemetry fields populated.

```json
[{
    "id": 1,
    "chip_id": "5fc8ad",
    "name": null,
    "version": "3.08.0-wagfam",
    "uptime_ms": 1234567,
    "free_heap": 32496,
    "rssi": -62,
    "ip_address": "192.168.65.1",
    "last_seen": "2026-04-26T18:02:01.176852",
    "created_at": "2026-04-26T18:02:01.176852"
}]
```

### Test 5: Set Device Name

```text
POST /api/v1/devices/5fc8ad/update_name {"name": "Kitchen Clock"}
```

**Result:** PASS — Name set to "Kitchen Clock". (Endpoint was originally
`PATCH /api/v1/devices/{chip_id}` and was renamed in response to PR #25 review
feedback to make it explicit that only the name can be modified — telemetry
fields are owned by the heartbeat path.)

### Test 6: Calendar Includes deviceName After Naming

```text
GET /api/v1/calendar?chip_id=5fc8ad&version=3.08.0-wagfam&uptime=2000000&heap=31000&rssi=-55
```

**Result:** PASS — `deviceName` appears in config block.

```json
{"config": {"eventToday": 0, "deviceName": "Kitchen Clock"}}
```

### Test 7: Telemetry Updated on Subsequent Heartbeat

**Result:** PASS — `uptime_ms`, `free_heap`, `rssi`, and `last_seen` all updated. `name` preserved.

| Field | Before | After |
| --- | --- | --- |
| `uptime_ms` | 1234567 | 2000000 |
| `free_heap` | 32496 | 31000 |
| `rssi` | -62 | -55 |
| `name` | Kitchen Clock | Kitchen Clock |

### Test 8: Get Single Device

```text
GET /api/v1/devices/5fc8ad
```

**Result:** PASS — Returns full device record.

### Test 9: Device Not Found (404)

```text
GET /api/v1/devices/nonexistent
```

**Result:** PASS — `{"detail": "Device not found"}` with HTTP 404.

### Test 10: Auth Required (401)

```text
GET /api/v1/devices (no auth header)
```

**Result:** PASS — `{"detail": "Invalid or missing token"}` with HTTP 401.

### Test 11: Wrong Token (401)

```text
GET /api/v1/devices (Authorization: token wrong-key)
```

**Result:** PASS — `{"detail": "Invalid or missing token"}` with HTTP 401.

### Test 12: Bearer Auth Accepted

```text
GET /api/v1/devices (Authorization: Bearer test-key-123)
```

**Result:** PASS — Returns device list. Both `token` and `Bearer` prefixes work.

### Test 13: Multiple Device Registration

Registered 3 devices via heartbeat, each with different chip_id/version.

**Result:** PASS — All 3 devices appear in device list.

```text
  5fc8ad | 3.08.0-wagfam | name=Kitchen Clock
  aabbcc | 3.07.0-wagfam | name=None
  ddeeff | 3.09.0-wagfam | name=None
```

### Test 14: Response Format Validation

Programmatically validated the calendar response structure:

- Response is a JSON array
- First element contains `config` with `eventToday` (integer)
- Remaining elements contain `message` (string)

**Result:** PASS — All assertions passed.

### Test 15: Data Persistence Across Container Restart

```bash
docker compose --profile local restart
```

**Result:** PASS — All 3 devices preserved, device names intact.

### Test 16: Full Clock Lifecycle

Simulated: first contact → admin names device → next heartbeat → verify.

**Result:** PASS

1. First contact: device registered with `config: {eventToday: 0}` (no deviceName yet)
2. Admin sets name: `"Living Room"`
3. Next heartbeat: config includes `"deviceName": "Living Room"`
4. Telemetry updated: version changed from 3.07.0 to 3.08.0, uptime/heap/rssi updated

### Test 17: Calendar Content Accuracy

Verified messages match expected format for today's date (2026-04-26):

- Kvitka's 14th Birthday in 6 days (May 2)
- Dallan's 34th Birthday in 7 days (May 3)
- Emily's 46th Birthday in 10 days (May 6)

**Result:** PASS — All dates and ages correct.

### Test 18: Partial Heartbeat Params

| Params Sent | Fields Populated |
| --- | --- |
| `chip_id` only | All telemetry fields `null` |
| `chip_id` + `version` | version set, others `null` |

**Result:** PASS — Graceful handling of partial data.

### Test 19: Concurrent Heartbeats (10 simultaneous)

Fired 10 concurrent calendar requests with different chip_ids.

**Result:** PASS — All 10 devices registered, no errors, no data corruption.

### Test 20: Device List Ordering

**Result:** PASS — Devices ordered by `last_seen` descending (most recent first).

### Test 21: Container Health

**Result:** PASS — Zero error/exception/traceback lines in container logs after all tests.

### Test 22: LAN Accessibility

```text
GET http://192.168.168.198:8000/health
```

**Result:** PASS — Server reachable from LAN IP, not just localhost.

### Test 23: Simulated Clock HTTP Request

Full clock-format request via LAN IP with auth header:

```text
GET http://192.168.168.198:8000/api/v1/calendar?chip_id=realclock1&version=3.08.0-wagfam&uptime=86400000&heap=31500&rssi=-65
Authorization: token test-key-123
```

**Result:** PASS — Calendar JSON returned, device registered.

### Test 24: Response Content-Type

**Result:** PASS — `content-type: application/json`

### Test 25: Long chip_id (Edge Case)

Sent 100-character chip_id.

**Result:** PASS — HTTP 200, no crash.

### Test 26: Special Characters in Version

Sent `version=3.08.0-wagfam%2Btest` (URL-encoded `+`).

**Result:** PASS — Stored as `3.08.0-wagfam+test`.

### Test 27: Rapid Repeated Heartbeats

5 rapid-fire heartbeats from same chip_id with incrementing telemetry.

**Result:** PASS — Final values match last heartbeat. All assertions passed.

---

## Summary

| Category | Tests | Passed | Failed |
| --- | --- | --- | --- |
| Build verification | 6 | 6 | 0 |
| Health & calendar | 4 | 4 | 0 |
| Heartbeat & registration | 7 | 7 | 0 |
| Auth | 3 | 3 | 0 |
| Device CRUD | 4 | 4 | 0 |
| Edge cases & stress | 6 | 6 | 0 |
| Infrastructure | 3 | 3 | 0 |
| **Total** | **33** | **33** | **0** |

## Not Tested (Requires Hardware)

- Flash firmware with heartbeat code and verify serial output shows query params
- Verify clock receives `deviceName` from server and stores in `/conf.txt`
- Verify `/api/status` on clock shows `device_name` field
- End-to-end: clock → server → DB → admin names device → clock displays name

> **Note:** The calendar clock was not reachable on the test network (192.168.168.0/24).
> All ESP devices found were Tasmota/ESPHome — the marquee clock may be powered off
> or on a different subnet. Hardware testing should be done after flashing the
> `feature/heartbeat-server` firmware.
