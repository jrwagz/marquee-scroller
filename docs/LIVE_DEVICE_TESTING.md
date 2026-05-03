# Live Device Testing Playbook

Recipe for end-to-end testing the firmware against a real clock on the LAN.
Captures the constraints + shortcuts that aren't obvious from reading the code,
distilled from the live-test work documented in
[`docs/TEST_RESULTS_HEARTBEAT_LIVE.md`](TEST_RESULTS_HEARTBEAT_LIVE.md).

## Prerequisites

- A clock device on the LAN running the firmware you want to exercise
  (`/api/status` reports `version`, but **note the version-string trap below**).
- The clock's web admin password. It's auto-generated on first boot
  (`marquee/marquee.ino:267-275`, 8 chars from `abcdefghjkmnpqrstuvwxyz23456789`)
  and printed to serial. If you've lost it, the only recovery is a flash + FS
  erase — there's no auth bypass.
- Docker, `curl`, and (for any firmware rebuild) the PlatformIO toolchain
  (`pio` from `pip install platformio` or VS Code's PIO extension).

## Network reachability gotchas

The clock might be reachable via `arp -a` but unreachable via TCP if the host
machine is multi-homed (Wi-Fi up but stale, Ethernet active on a different
subnet). Symptom: `curl http://<clock-ip>/...` returns "No route to host"
even though `arp -a` shows the right MAC.

Workaround: source-bind the request to the interface that actually has a path
to the LAN. Example from this session:

```bash
# Mac was on en0 (192.168.168.x Wi-Fi, stale) AND en6 (10.10.2.x ethernet).
# Inter-subnet routing existed but the kernel preferred en0's stale route.
curl -s -m 5 --interface en6 -u admin:<pw> http://<clock-ip>/api/status
```

Same idea works with `ping -S <source-ip>` and Python sockets via explicit
`bind(("<source-ip>", 0))` before `connect`. If you spend 5+ minutes on "no
route to host" and `traceroute` succeeds while `curl` fails, this is almost
certainly the issue.

## Testing approach: dial heartbeat to 1 minute, no flash needed

The heartbeat interval is `minutes_between_data_refresh`, persisted in
`/conf.txt`, and exposed via `/api/config`. **You do not need to flash
firmware to change the cadence** — the firmware clamps the value with
`max(1, ...)` (`marquee/marquee.ino:471` and `:1389`), so 1 minute is the
floor and reachable from a single REST call:

```bash
# Save the original first (CRITICAL — restore it at the end)
curl -s -u admin:<pw> http://<clock-ip>/api/config > /tmp/orig_config.json

# Dial down for testing
curl -s -u admin:<pw> \
  -H "X-Requested-With: XMLHttpRequest" \
  -H "Content-Type: application/json" \
  -X POST http://<clock-ip>/api/config \
  -d '{"minutes_between_data_refresh": 1}'
```

The `X-Requested-With` header is required for all POST API endpoints
(`requireApiCsrf`, `marquee.ino:1299-1305`).

## Pointing the clock at a local heartbeat server (TLS required)

The firmware's calendar HTTP client is **HTTPS-only** —
`marquee/WagFamBdayClient.cpp:43` hard-codes `BearSSL::WiFiClientSecure`. You
cannot point the clock at a plain HTTP server. The client uses
`setInsecure()` so any TLS server is acceptable, including self-signed.

The server must return the expected JSON format (see README.md — WagFam Calendar).
Generate a self-signed cert and serve any HTTPS endpoint that returns that JSON:

```bash
# Self-signed cert (any CN/SAN — clock doesn't verify)
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
  -sha256 -days 30 -nodes -subj "/CN=heartbeat-test" \
  -addext "subjectAltName = IP:<your-LAN-IP>,DNS:localhost"

# Point clock at your TLS server (update API key to match server's expected value)
curl -s -u admin:<pw> \
  -H "X-Requested-With: XMLHttpRequest" \
  -H "Content-Type: application/json" \
  -X POST http://<clock-ip>/api/config \
  -d '{"wagfam_data_url":"https://<your-LAN-IP>:8443/api/v1/calendar","wagfam_api_key":"<test-key>"}'

# Force an immediate refresh (don't wait for the periodic timer)
curl -s -u admin:<pw> \
  -H "X-Requested-With: XMLHttpRequest" \
  -X POST http://<clock-ip>/api/refresh
```

The clock's `Authorization: token <key>` header is sent on every request, so
the server's expected API key must match what you POST'd to the clock.

## OTA testing path

`/updateFromUrl` requires HTTP-only firmware URLs (the firmware's
`ESPhttpUpdate` doesn't speak TLS) — host the `firmware.bin` on a plain HTTP
server. This is fine because the OTA update mechanism has integrity validation
on the binary itself + boot-confirmation rollback on top
(see [`docs/OTA_STRATEGY.md`](OTA_STRATEGY.md)).

```bash
# Build firmware locally (use `make build` + `make artifacts` for the same
# bundle CI produces, with the build-version suffix baked in)
pio run                     # produces .pio/build/default/firmware.bin
# or:
make build && make artifacts  # produces artifacts/firmware.bin + VERSION.txt

# Host on plain HTTP — bind to your LAN IP so the clock can reach it
cd artifacts
python3 -m http.server 8080 --bind <your-LAN-IP>

# Trigger flash. Must be GET (the device's UPDATE_FORM uses method='get'
# and the route is registered HTTP_GET) and the URL must be http:// since
# ESPhttpUpdate doesn't speak TLS.
curl -s -u admin:<pw> \
  -G http://<clock-ip>/updateFromUrl \
  --data-urlencode "firmwareUrl=http://<your-LAN-IP>:8080/firmware.bin"
```

The handler responds *immediately* with a "STARTING UPDATE" page (because
the actual flash is deferred to the main loop — async handlers can't
block on the 20–30s `ESPhttpUpdate.update()` call without crashing).
The clock then displays "Updating..." on the matrix, fetches the binary,
reboots in ~30s. `/api/status` afterward shows `ota.confirm_at: <ms>`
(the deadline by which the new firmware is considered "confirmed" —
typically 5 minutes post-boot per `OTA_CONFIRM_MS` in `marquee.ino`).
If the firmware crashes during the confirmation window, boot-confirmation
rollback re-flashes the previous safe URL.

**Alternative: file upload via `/update`** (use this if `/updateFromUrl`
times out — for instance if your LAN host can't reach the clock for some
reason, or you want to flash without exposing an HTTP server):

```bash
curl -s -u admin:<pw> \
  -F "image=@artifacts/firmware.bin" \
  http://<clock-ip>/update
```

The upload arrives chunk-by-chunk via async callbacks, the device flashes
as it arrives, and reboots when done. curl will see a `recv failure` after
~30s — that's just the reboot dropping the connection, not an error.
Verify success by polling `/api/status` for the new `version` string.

### Caveat: `OTA_SAFE_URL` rewrite

When an OTA flash succeeds and confirms, the device's `OTA_SAFE_URL` is
rewritten to the URL you flashed from (`marquee.ino:353`). After your local
test, that URL points at your dev box's HTTP server, which won't be running
later. **You cannot patch this via `/api/fs/write`** — `/conf.txt` is in the
protected-paths list (`marquee.ino:1457` and `SecurityHelpers.cpp:isProtectedPath`)
so the API rejects writes to it.

Mitigation options:

- Re-flash from a long-lived URL (e.g. a `lhr.life` localhost.run tunnel that
  persists, or a real CDN) before tearing down the test rig — the next OTA
  success rewrites the safe URL again.
- Or simply leave it: rollback recovery is a no-op (URL unreachable), but
  rollback recovery is itself only triggered on a future OTA failure, which
  won't happen unless you do another OTA.

This caveat only matters if you flash. Configuration-only tests
(URL/key/interval changes via `/api/config`) leave OTA state untouched.

## Version-string trap

`/api/status` reports `version: "<VERSION-macro>"` — but the `VERSION` macro
is **not** automatically bumped when wire-visible behavior changes. During the
PR #25 live-test work, the device reported `version: "3.08.0-wagfam"` but its
binary predated the heartbeat-code commit (`fd289ab`) by hours, so calendar
fetches were arriving at the local server with no `chip_id` query string at
all. The reported version was identical pre- and post-heartbeat-feature.

**Mitigation:** when you flash a build with a wire-visible change, bump the
`VERSION` macro at `marquee/marquee.ino` in the same commit. Otherwise field
debugging gets ambiguous.

## Restore checklist (always run at the end of a live test)

1. **`/api/config` POST** with the saved original `wagfam_data_url`,
   `wagfam_api_key`, and `minutes_between_data_refresh`. Verify with a
   follow-up GET.
2. **Stop your local TLS server and HTTP firmware host.** Otherwise they sit
   listening forever.
3. **`OTA_SAFE_URL`**: see caveat above — left as-is unless you re-flash from
   a stable URL.
4. **Don't leave `WAGFAM_API_KEY=livetest-key-001` in the device.** It must
   match the production calendar source's auth, otherwise the next periodic
   fetch will 401 and the marquee will stop scrolling family events.

The PR #25 live test left a clean restore on every field except OTA_SAFE_URL —
see `docs/TEST_RESULTS_HEARTBEAT_LIVE.md` for the worked example.

## Quick decision tree: what testing approach?

- **"I want to verify the heartbeat fields the device sends"** → point the
  clock at a local TLS server with the request-logging middleware above. No
  firmware flash needed if the build already has heartbeat code.
- **"I want to verify a code change in the firmware"** → build locally, host
  the bin on a plain HTTP server, OTA via `/updateFromUrl`. Test via either
  `/api/status` directly or by pointing at a local TLS server.
- **"I want to capture cadence behavior"** → dial
  `minutes_between_data_refresh` to 1 via `/api/config`, log requests for
  N+1 minutes, restore the original interval. No flash needed.
- **"The clock is unreachable but `arp -a` shows it"** → source-bind the
  request to the interface with a working route (see Network reachability
  gotchas above).
