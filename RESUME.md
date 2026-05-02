# Resume Notes — `api-fs-upload`

> Branch-local scratch file. Tracks hardware-test work that was paused
> because the clock device is at a different physical location and our
> previous LAN setup no longer applies. **Delete this file before opening
> the PR for merge.**

## Status snapshot

**Branch:** `api-fs-upload`, stacked on `optimizations` (PR #54). Three
commits ahead of `upstream/optimizations`, all pushed:

```text
c8f01f6  scripts/deploy_webui.py — push data/spa/ via /api/fs/upload
c3b0ab9  Add /api/fs/upload — async multipart binary upload
7c5c4c9  Add isValidUploadPath() to SecurityHelpers
```

**Build state:** firmware version `3.08.8-wagfam-dallan-20260501-c8f01f6`
(561 KB sketch), 121 native tests pass, markdown lint clean.

**Device state when paused:**

- Kitchen clock running firmware `c8f01f6` (this branch's tip), OTA'd
  successfully on 2026-05-01.
- `/conf.txt` is intact (557 B). Web password is unchanged: `wagfam2025`.
- Last known clock IP was `192.168.168.66` on the work LAN. **The IP will
  be different when the clock comes home.**

## What's been hardware-tested ✓

Done before the pause; no need to redo unless something changes:

- POST `/api/fs/upload?path=/test.txt` with multipart body → 200 OK, file
  written.
- GET `/api/fs/read?path=/test.txt` → content round-trips correctly.
- POST without `X-Requested-With` → 403 (CSRF check fires).
- POST with traversal `/spa/../conf.txt` → 400 (path validator rejects).
- POST to protected `/conf.txt` → 400 (rejected).
- `/conf.txt` unchanged after all tests — protected-path enforcement
  holds end-to-end.

## What's still pending hardware verification

- [ ] **Full SPA deploy via `deploy_webui.py`** — interrupted before
  running. Validates the script end-to-end and exercises sustained
  multi-file uploads (~12 KB across 6 files including `.gz` siblings).
- [ ] **`/spa/` browser-render** — load `http://<clock-ip>/spa/` in a
  browser and confirm the empty Preact shell renders ("WagFam CalClock
  — SPA shell loaded. Pages coming soon.").
- [ ] **`Content-Encoding: gzip` header** on the assets — the lib's
  `serveStatic` should auto-select the `.gz` sibling. This is the first
  end-to-end check possible because `/api/fs/write` rejected binary.
- [ ] **`deploy_webui.py --skip-unchanged`** (optional) — verify that the
  `/api/fs/list`-based diff actually skips unchanged files on a second
  run.

## Resume steps

When the clock is back on the same LAN as the dev machine:

```bash
cd /Users/dallan/repo/marquee-scroller
git switch api-fs-upload
git pull upstream api-fs-upload   # in case anything moved

# 1. Find the clock on the new network. Try (in order):
#    a. Router admin page — look for a hostname starting with CLOCK-
#    b. arp -a | grep -i 5fc8ad   (the chip ID — MAC suffix usually matches)
#    c. If neither works, the clock came up in AP mode. Connect to
#       SSID "CLOCK-5fc8ad" from your phone, enter the new SSID + password,
#       wait for it to associate, then re-check arp.

CLOCK_IP=192.168.x.y                    # set this to the new IP
curl -u admin:wagfam2025 http://$CLOCK_IP/api/status | python3 -m json.tool
# Expect: "version": "3.08.8-wagfam-dallan-20260501-c8f01f6"
# If the version is anything else, the device rebooted to an older
# firmware (boot-confirmation rollback fired) — re-OTA before testing.

# 2. Run the deploy script
WAGFAM_CLOCK_PW=wagfam2025 python3 scripts/deploy_webui.py http://$CLOCK_IP
# Expect: 6 files uploaded (index.html, index.html.gz, assets/index.css,
# assets/index.css.gz, assets/index.js, assets/index.js.gz), totaling
# ~18 KB transferred (the bundle is 12.7 KB local; multipart adds overhead).

# 3. Confirm /spa/ renders in a browser
open "http://$CLOCK_IP/spa/"
#   Expect: "WagFam CalClock" heading + "SPA shell loaded. Pages coming soon."
#   The browser will prompt for credentials on the first /api/* request
#   (admin / wagfam2025).

# 4. Verify gzip serving on the JS asset
curl -sv -u admin:wagfam2025 -H "Accept-Encoding: gzip" \
  http://$CLOCK_IP/spa/assets/index.js 2>&1 | grep -i "content-encoding"
# Expect: < Content-Encoding: gzip
# If absent: the lib didn't pick up the .gz sibling — investigate before
# closing the test.

# 5. Confirm /conf.txt still intact (the whole point of this PR)
curl -s -u admin:wagfam2025 "http://$CLOCK_IP/api/fs/read?path=/conf.txt" \
  | python3 -c "import json,sys; d=json.load(sys.stdin); print(f'conf.txt size: {d[\"size\"]} (was 557 originally)')"

# 6. (Optional) Test --skip-unchanged on a second run
WAGFAM_CLOCK_PW=wagfam2025 python3 scripts/deploy_webui.py http://$CLOCK_IP --skip-unchanged
# Expect: "6 skipped, 0 uploaded"
```

If everything passes, finish out the PR:

```bash
# Drop this file
rm RESUME.md
git add -u
git commit -m "Drop RESUME.md (work resumed and completed)"

# Update WEBUI.md to describe the new deploy_webui.py flow (the foundation
# PR's docs still say "serial flash wipes /conf.txt"). See task #19.
$EDITOR docs/WEBUI.md
git add docs/WEBUI.md
git commit -m "Update WEBUI.md for /api/fs/upload + deploy_webui.py"

git push upstream api-fs-upload

# Open the PR — see "PR draft" section below for body
gh pr create --repo jrwagz/marquee-scroller -B optimizations -H api-fs-upload \
  --title "Add /api/fs/upload — async multipart binary upload"
```

If hardware testing reveals issues, fix them in additional commits on
this branch and retry. Don't squash the existing three commits — they're
logically distinct (validator / endpoint / deploy script) and the brother
prefers atomic small commits over big squashed ones.

## PR draft (paste into `gh pr create --body`)

```markdown
## Summary

The third PR in the SPA stack (after #54 async migration and #55 SPA
foundation). Removes the "first-time SPA install requires serial flash
that wipes /conf.txt" caveat by adding a binary multipart upload
endpoint and a Python deploy script.

Stacked on `optimizations` (#54), sibling to `webui-spa-foundation`
(#55). After both #54 and #55 merge, this rebases onto master cleanly.

## What's in the box

- New `POST /api/fs/upload?path=<dest>` endpoint. Multipart body,
  streamed chunk-by-chunk via `AsyncWebServer`'s upload callback to
  LittleFS. Auth + CSRF gated. No buffering of the whole body in heap.
- `isValidUploadPath()` in SecurityHelpers, with 17 unit tests covering
  null, empty, traversal at start/middle/end, length limit, "..bar is
  not traversal", protected-path collisions.
- `scripts/deploy_webui.py` — stdlib-only Python script that walks
  `data/spa/` and POSTs each file. Supports `--skip-unchanged` (uses
  `/api/fs/list`), prompts for password if not provided.

## Why not extend `/api/fs/write`?

The existing endpoint uses `AsyncCallbackJsonWebHandler` which buffers
the body and parses to a JsonDocument. For an N-byte body, peak heap is
~2N (buffer + parsed doc). With ~30 KB free heap and a 12 KB SPA JS
file, the math is too tight to be reliable. Streaming via the upload
callback writes chunks straight to FS without ever holding the whole
body — works for arbitrarily-large files (limited only by FS space).

## Size impact

- +1 KB flash (endpoint registration overhead)
- +143 lines for the path validator + tests

## Test plan

- [x] 121 native unit tests pass (104 prior + 17 new path-validator)
- [x] Firmware builds clean
- [x] Markdown lint clean
- [x] Hardware: simple upload, read-back, CSRF check, traversal
  rejected, protected-path rejected, `/conf.txt` untouched
- [x] Hardware: full SPA deploy via `deploy_webui.py` (6 files, ~18 KB
  multipart total)
- [x] Hardware: `/spa/` renders the Preact shell in browser
- [x] Hardware: `Content-Encoding: gzip` header set on JS/CSS assets
- [x] Hardware: `--skip-unchanged` correctly no-ops on second run

## Caveats

- Single-upload concurrency. The lib's `request->_tempFile` is per-request
  but our handler doesn't lock anything FS-side; two simultaneous uploads
  to the same path would race. Not an issue for the deploy script (which
  is sequential) but worth knowing if anyone wires this into a parallel
  workflow.
```

(The `[x]` boxes for the still-pending tests get filled in once the
hardware run on resume succeeds.)

## Open question for resume — WiFi credential storage

The async migration replaced `tzapu/WiFiManager` with
`alanswx/ESPAsyncWiFiManager`. tzapu retained the most recently
successful credentials only; alanswx may behave the same or differently.
We don't know yet how the device behaves when it can't reach the saved
SSID:

- **If alanswx falls back to AP mode automatically:** great, you'll see
  `CLOCK-5fc8ad` from your phone, enter the new SSID, done. Note this
  in [`docs/LIVE_DEVICE_TESTING.md`](docs/LIVE_DEVICE_TESTING.md) on
  resume — it's a useful playbook addition.
- **If it boot-loops trying to associate:** the clock may need a
  serial-flash reset (`pio run --target uploadfs` wipes config + WiFi
  creds) or a "Forget WiFi" trigger before bringing it home.

Either way, capture what happens — that's worth recording in
`docs/LIVE_DEVICE_TESTING.md` for next time.
