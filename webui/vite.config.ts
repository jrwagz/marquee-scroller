import { defineConfig, loadEnv } from "vite";
import preact from "@preact/preset-vite";
import { compression } from "vite-plugin-compression2";
import fs from "node:fs";
import path from "node:path";

// Vite config for the WagFam CalClock SPA.
//
// Bundle is shipped onto the device's LittleFS partition under /spa/ and
// served by AsyncWebServer's serveStatic handler. We emit gzipped siblings
// (.gz) so the server can serve Content-Encoding: gzip for ~3-4x size win.
//
// Dev server (npm run dev) proxies /api/* to the configured device IP so we
// can iterate against real device state. Set CLOCK_HOST in .env.local
// (e.g. CLOCK_HOST=http://192.168.168.66) and CLOCK_AUTH (admin:password) to
// enable; otherwise dev mode skips the proxy and you get an empty fetch.

// Bake the SPA version into the JS bundle as __SPA_VERSION__.
// scripts/write_spa_version.py runs before `make webui` and writes
// data/spa/version.json with the same version string the device reads at
// boot and exposes via /api/status.spa_version. Embedding it lets the
// Footer detect when a browser is running a *cached* old bundle whose
// version differs from the device's API-reported one, and prompt the user
// to force-refresh. Falls back to "dev" when version.json is absent (e.g.
// `npm run dev` from a fresh checkout).
function readBundleVersion(): string {
  try {
    const p = path.resolve(__dirname, "..", "data", "spa", "version.json");
    const j = JSON.parse(fs.readFileSync(p, "utf8")) as { spa_version?: string };
    return j.spa_version || "unknown";
  } catch {
    return "dev";
  }
}

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  const clockHost = env.CLOCK_HOST || "";
  const clockAuth = env.CLOCK_AUTH || "";
  const spaVersion = env.SPA_VERSION || readBundleVersion();
  return {
    base: "/spa/",
    define: {
      __SPA_VERSION__: JSON.stringify(spaVersion),
    },
    plugins: [
      preact(),
      compression({
        algorithm: "gzip",
        // Keep the original alongside the .gz so AsyncWebServer can fall back
        // if a client doesn't advertise gzip support (no real-world client
        // doesn't, but the cost is negligible and it simplifies debugging).
        deleteOriginalAssets: false,
      }),
    ],
    build: {
      // Output directly into PlatformIO's LittleFS source dir. `make uploadfs`
      // (and `pio run --target uploadfs`) flashes whatever's in data/.
      outDir: "../data/spa",
      emptyOutDir: true,
      // Keep filenames stable — the device serves these by literal path. No
      // hash-suffixing because we manage cache invalidation via Cache-Control
      // headers on the server side, not via filename.
      rollupOptions: {
        output: {
          entryFileNames: "assets/[name].js",
          chunkFileNames: "assets/[name].js",
          assetFileNames: "assets/[name][extname]",
        },
      },
      // Bundle budget guard. If the bundle ever crosses this, fail the build
      // — flash space is finite and we want noise when it grows.
      chunkSizeWarningLimit: 60, // KB raw, pre-gzip
    },
    server: {
      proxy: clockHost
        ? {
            "/api": {
              target: clockHost,
              changeOrigin: true,
              auth: clockAuth || undefined,
            },
          }
        : undefined,
    },
  };
});
