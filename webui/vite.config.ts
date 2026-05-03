import { defineConfig, loadEnv } from "vite";
import preact from "@preact/preset-vite";
import { compression } from "vite-plugin-compression2";

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
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  const clockHost = env.CLOCK_HOST || "";
  const clockAuth = env.CLOCK_AUTH || "";
  return {
    base: "/spa/",
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
