// Footer — shows the SPA version baked into the JS bundle, fetches the
// device's API-reported spa_version, and prompts a force-refresh when they
// disagree. The bundle version is injected at build time via Vite `define`
// (see vite.config.ts and the `__SPA_VERSION__` global declared below).
//
// Why this exists: when LittleFS is reflashed on the device, the in-flight
// JS bundle in any open browser tab is *still the old one* until the
// browser revalidates. Until that happens, /api/status.spa_version will
// report the new version while the cached bundle silently runs the old
// code — confusing during diagnosis. The footer makes the gap visible.

import { useEffect } from "preact/hooks";
import { signal } from "@preact/signals";
import { getStatus } from "../api";

declare const __SPA_VERSION__: string;

const bundleVersion = __SPA_VERSION__;

const deviceVersion = signal<string | null>(null);
const fetchFailed = signal(false);

async function refreshDeviceVersion() {
  try {
    const s = await getStatus();
    deviceVersion.value = s.spa_version || null;
    fetchFailed.value = false;
  } catch {
    fetchFailed.value = true;
  }
}

// Detect the user's OS so we can show the right force-refresh shortcut as
// the *first* hint. Other platforms still get listed below. Falls back to
// generic if UA sniffing fails.
function detectPlatform(): "mac" | "windows" | "linux" | "ios" | "android" | "other" {
  const ua = (typeof navigator !== "undefined" && navigator.userAgent) || "";
  if (/iPad|iPhone|iPod/i.test(ua)) return "ios";
  if (/Android/i.test(ua)) return "android";
  if (/Macintosh|Mac OS X/i.test(ua)) return "mac";
  if (/Windows/i.test(ua)) return "windows";
  if (/Linux/i.test(ua)) return "linux";
  return "other";
}

function ForceRefreshInstructions() {
  const plat = detectPlatform();
  return (
    <details class="refresh-instructions">
      <summary>How to force-refresh</summary>
      <ul>
        <li class={plat === "mac" ? "primary-platform" : ""}>
          <strong>macOS</strong> (Chrome / Firefox / Edge / Safari):
          press <kbd>Cmd</kbd> + <kbd>Shift</kbd> + <kbd>R</kbd>
        </li>
        <li class={plat === "windows" || plat === "linux" ? "primary-platform" : ""}>
          <strong>Windows / Linux</strong> (Chrome / Firefox / Edge):
          press <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>R</kbd>
          {" "}(or <kbd>Ctrl</kbd> + <kbd>F5</kbd>)
        </li>
        <li class={plat === "ios" ? "primary-platform" : ""}>
          <strong>iOS Safari</strong>: Settings → Safari → Clear History
          and Website Data, then reopen this page. (Private-tab reload
          also bypasses the cache.)
        </li>
        <li class={plat === "android" ? "primary-platform" : ""}>
          <strong>Android Chrome</strong>: ⋮ menu → History → Clear
          browsing data → "Cached images and files", then reload.
        </li>
        <li>
          Or: open DevTools (<kbd>F12</kbd>), right-click the reload
          button, and pick <em>Empty Cache and Hard Reload</em>.
        </li>
      </ul>
    </details>
  );
}

export function Footer() {
  useEffect(() => {
    void refreshDeviceVersion();
  }, []);

  const dv = deviceVersion.value;
  // Mismatch: both known and different. We don't warn when device reports
  // "unknown" (older firmware, or version.json missing on the FS) — that's
  // a different problem and would be noisy.
  const mismatch =
    dv !== null && dv !== "" && dv !== "unknown" && dv !== bundleVersion;

  return (
    <footer class="spa-footer">
      {mismatch && (
        <div class="version-mismatch">
          <p>
            <strong>⚠ Cached SPA is out of date.</strong> This browser is
            running <code>{bundleVersion}</code> but the device reports{" "}
            <code>{dv}</code>. Force-refresh to load the latest bundle.
          </p>
          <ForceRefreshInstructions />
        </div>
      )}
      <div class="footer-line">
        <span>
          SPA bundle: <code>{bundleVersion}</code>
        </span>
        <span>
          {fetchFailed.value ? (
            <span class="muted">device version unavailable</span>
          ) : dv === null ? (
            <span class="muted">checking device…</span>
          ) : (
            <>
              device: <code>{dv || "unknown"}</code>
            </>
          )}
        </span>
        <button
          class="link-btn"
          onClick={() => void refreshDeviceVersion()}
          title="Re-poll /api/status"
        >
          recheck
        </button>
      </div>
    </footer>
  );
}
