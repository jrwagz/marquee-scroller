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

`pio run --target uploadfs` (or `make uploadfs` if/when we add it).
**Caveat: this wipes the entire LittleFS partition**, including
`/conf.txt` (web password, calendar URL, API keys, OTA state). After a
serial flash you'll need to reconnect WiFi and reconfigure from
scratch. Use this for first-time SPA install or major changes.

```bash
make webui
pio run -e default --target uploadfs --upload-port /dev/cu.usbserial-XXXX
```

### Option B: API-based upload (per-file, preserves /conf.txt)

For incremental updates, push individual files via `POST /api/fs/write`.
This preserves config but currently has caveats:

- The handler's `setMaxContentLength` is **8 KB** — files larger than
  ~7.5 KB after JSON-encoding overhead will be rejected. The empty
  shell's `assets/index.js` is 11.7 KB, already over the limit.
- The body is parsed as a JSON string (`content`) so binary `.gz` files
  can't be uploaded directly.

A proper `/api/fs/upload` (multipart, streamed, accepts binary) is on
the radar but not in this PR. Until it lands, Option A is the
realistic path for installing the full bundle.

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

1. **Status dashboard page** — uses `@preact/signals` to render
   `/api/status` data, refreshed via SSE (when the SSE endpoint lands)
   or polling.
2. **Configure form** — replaces the `CHANGE_FORM*` PROGMEM HTML with a
   typed Preact form against `POST /api/config`.
3. **Filesystem browser** — leans on `/api/fs/list`, `/api/fs/read`,
   `/api/fs/write`, `/api/fs/delete`.
4. **Binary file upload endpoint** (`/api/fs/upload`) — multipart
   streaming, eliminates the serial-flash caveat for SPA updates.
5. **Drop W3.CSS / Font Awesome CDN dependencies** — once the SPA
   covers all features, remove the legacy `/` and `/configure` routes
   and their PROGMEM strings. Reclaims ~5 KB of flash.
