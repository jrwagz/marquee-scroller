# WebUI: Preact SPA Frontend

The `webui/` directory holds the source for a single-page app served from
the device's LittleFS partition under `/spa/`. The async server's
`serveStatic` handler delivers the bundle with `Content-Encoding: gzip`
when the client supports it.

This is the foundation; real pages (status dashboard, configure form,
filesystem browser, etc.) ship in subsequent PRs against the existing
REST API.

## Stack

- **[Vite 5](https://vitejs.dev/)** — bundler + dev server
- **[Preact 10](https://preactjs.com/)** — React-compatible UI framework, ~3 KB
- **[`@preact/signals`](https://preactjs.com/guide/v10/signals/)** — reactive state primitive
- **TypeScript 5** (strict) — types catch typos before they reach the device
- **[`vite-plugin-compression2`](https://github.com/nonzzz/vite-plugin-compression)** — emits gzipped siblings during build
- **No CSS framework** — hand-rolled utility CSS. Flash budget rules out Tailwind / MUI / similar.

Bundle target: **20–30 KB gzipped** for the entire app once it's full of
features. The empty shell currently weighs **5.6 KB gzipped**.

## Local dev workflow

All commands run from the repo root.

### One-time setup

Nothing to install on the host. The `make webui*` targets run the
toolchain inside `node:20-alpine` via Docker, mirroring the project's
existing pattern (`make lint-markdown`, `make test-native`, etc.).

### Build

```bash
make webui            # builds data/spa/ — both raw and gzipped siblings
make webui-typecheck  # tsc -b --noEmit
make webui-clean      # nukes data/spa/, webui/node_modules/, webui/.vite/
```

`make webui` outputs file sizes (raw + gzipped) at the end so growth is
noisy in the build log.

### Live dev server (Vite, against a real device)

For iterating on UI without reflashing every change, run Vite's dev
server with HMR and proxy `/api/*` to the device:

```bash
cd webui
echo "CLOCK_HOST=http://192.168.168.66" > .env.local
echo "CLOCK_AUTH=admin:wagfam2025" >> .env.local
npm run dev
```

(Use the host's local Node if you have one, or `make` a tiny target if
you find yourself doing this often.) The dev server serves the SPA at
`http://localhost:5173/spa/` and proxies API calls back to the clock.

> The dev workflow is **not** a substitute for hardware-flashing the
> bundle and exercising it on the device. Browser-side behaviors that
> only show up over the device's slower network (TLS handshake timing,
> heap pressure during concurrent fetches) won't surface in dev. Always
> finish with a hardware test before merging UI changes.

## Deploying to the device

The default OTA flash (`/update`, `/updateFromUrl`) only updates the
firmware sketch. **It does not touch LittleFS.** So the SPA bundle gets
to the device through one of two paths:

### Option A: Serial flash (full LittleFS replace)

`make uploadfs` (which builds the SPA first, then runs
`pio run --target uploadfs`). **Caveat: this wipes the entire LittleFS
partition**, including `/conf.txt` (calendar URL, API keys, OTA state).
After a serial flash you'll need to reconnect WiFi and reconfigure
from scratch. Use this only when OTA is unavailable or you need to
force a clean filesystem.

```bash
make uploadfs                                   # autodetected port
make uploadfs UPLOAD_PORT=/dev/cu.usbserial-XXXX
```

### Option B: OTA flash via `/updatefs` (preserves /conf.txt)

Once the device is running firmware ≥ 3.09.3-wagfam, the LittleFS
image (`littlefs.bin`) can be flashed over the network — no serial
cable needed. The handler backs up `/conf.txt` before the flash and
restores it after the new filesystem mounts, so settings survive the
SPA refresh.

- Browser: open `http://<device-ip>/updatefs`, choose `littlefs.bin`,
  click **Upload & Flash FS**. The device reboots into the new FS.
- API: `POST /api/spa/update-from-url` with `{"url":"http://..."}`
  pulls the image from a URL, with the same backup/restore behaviour.
- SPA Actions tab also exposes the URL-fetch flow (and the SPA-update
  banner offers a one-click upgrade when the calendar server advertises
  a newer SPA version).

This is the recommended path for SPA refreshes after the initial
install.

### Option C: Single-file API upload (`POST /api/fs/write`)

For ad-hoc per-file pushes (e.g. dropping a debug HTML file onto the
device), `POST /api/fs/write` accepts a JSON body with `path` +
`content`. Caveats:

- The handler's `setMaxContentLength` is **8 KB** — anything larger
  (after JSON-encoding overhead) is rejected. Most SPA chunks exceed
  this, so this endpoint is **not** suitable for bundle deploys.
- The body is parsed as a JSON string, so binary `.gz` files can't be
  uploaded directly.
- `/conf.txt` and `/ota_pending.txt` are protected and rejected with 403.

### Building the LittleFS image without flashing

```bash
make buildfs   # produces .pio/build/default/littlefs.bin
make artifacts # copies it to artifacts/littlefs.bin alongside firmware.bin
```

CI bundles `littlefs.bin` into release artifacts so users can serial-flash
it themselves.

## Architecture notes

### Why serve from LittleFS instead of PROGMEM

PROGMEM-embedded HTML/JS would survive a firmware OTA without needing a
separate LittleFS update — that's tempting. But:

- **Flash cost**: A 30 KB bundle in PROGMEM is 30 KB of code-section
  flash, forever. We have ~100 KB of headroom on a 1 MB sketch
  partition; spending 30 KB on a UI that updates separately from
  firmware is wasteful.
- **Update cadence**: UI changes are more frequent than firmware
  changes once the system is stable. Decoupling them means a UI tweak
  doesn't require shipping new firmware.
- **The async server's `serveStatic` is free**: file system + gzip
  serving + cache headers all handled by the lib. No bespoke handler.

### Why no hash-based filenames

Vite's default `[name]-[hash].js` pattern is for HTTP cache busting.
We control `Cache-Control` from the server (10 min in this PR) and the
device serves only its own LittleFS contents — there's no CDN to
invalidate. Stable filenames simplify the deploy script and make
on-device debugging (`/api/fs/list`, `/api/fs/read`) tractable.

### Why both raw and gzipped output

`AsyncStaticWebHandler` serves the `.gz` sibling when the client
advertises `Accept-Encoding: gzip` (every modern browser does), and
falls back to the raw file otherwise. Shipping both costs ~3× the
LittleFS space of gzip-only but lets us debug weird MIME / encoding
issues by hitting the raw asset directly via curl. At 12 KB total
overhead today, the cost is irrelevant. We can switch to
gzip-only once we trust the pipeline more.

## Bundle budget

If `make webui` emits a chunk above **60 KB raw**, Vite warns. Treat that
as a build break — we want to keep the full UI under that. Inspect the
treeshake report (`vite build --report` if added) and the rollup
visualizer when figuring out what's growing.

Realistic ceiling on this device: **~700 KB usable LittleFS** after the
config, OTA pending, and headroom. The async server's `serveStatic` will
happily serve a 200 KB bundle, but at that point we'd be eating into
runtime heap during peak load. Stay small.

## Future work (separate PRs)

1. ~~**Status dashboard page**~~ — landed in PR #59. Polls `/api/status`
   every 30 s; SSE upgrade still open.
2. ~~**Configure form**~~ — landed in PRs #59 + #62. All settings
   (display, refresh, weather toggles, OWM/calendar sources) are now
   exposed in the SPA Settings tab.
3. ~~**Drop legacy `/` and `/configure` routes + PROGMEM strings**~~ —
   landed in PR #80 (Phase D). Saved ~12 KB flash + ~3 KB RAM. The
   legacy paths now 302-redirect to `/spa/`.
4. ~~**SPA bundle OTA refresh**~~ — landed in 3.09.3-wagfam: `/updatefs`
   (browser upload) + `POST /api/spa/update-from-url` + the SPA
   "available SPA version" banner with a one-click upgrade flow. SPA
   refreshes no longer require a serial cable.
5. **Filesystem browser** — leans on `/api/fs/list`, `/api/fs/read`,
   `/api/fs/write`, `/api/fs/delete`. Still open.
6. **Binary file upload endpoint** (`/api/fs/upload`) — multipart
   streaming. The full SPA bundle is now refreshed via `/updatefs`
   (the LittleFS image), so this is no longer the critical path; it
   would still be useful for one-off file pushes that don't justify a
   full FS image.
