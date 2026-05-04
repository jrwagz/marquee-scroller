import { useEffect } from "preact/hooks";
import { signal, computed } from "@preact/signals";
import { getStatus, getConfig, patchConfig, postSpaUpdateFromUrl } from "../api";
import type { StatusData } from "../types";

const status = signal<StatusData | null>(null);
const loading = signal(false);
const error = signal<string | null>(null);

type SpaUpdateState = "idle" | "updating" | "error";
const spaUpdateState = signal<SpaUpdateState>("idle");
const spaUpdateStep = signal("");
const spaUpdateError = signal("");

const uptimeStr = computed(() => {
  if (!status.value) return "—";
  const s = Math.floor(status.value.uptime_ms / 1000);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  return `${h}h ${m}m ${sec}s`;
});

const heapColor = computed(() => {
  const h = status.value?.free_heap ?? 0;
  if (h >= 20000) return "var(--green)";
  if (h >= 10000) return "var(--yellow)";
  return "var(--red)";
});

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function pollUntilBack(): Promise<void> {
  // Give the device time to start the flash and initiate reboot.
  await delay(8_000);
  const deadline = Date.now() + 120_000;
  while (Date.now() < deadline) {
    try {
      await getStatus();
      return;
    } catch {
      // device still offline — keep polling
    }
    await delay(3_000);
  }
  throw new Error("Device did not come back online within 2 minutes.");
}

async function doSpaUpdate(fsUrl: string) {
  if (!fsUrl) {
    spaUpdateState.value = "error";
    spaUpdateError.value = "No SPA update URL — try forcing a data refresh first.";
    return;
  }
  spaUpdateState.value = "updating";
  spaUpdateError.value = "";
  try {
    // Step 1 — snapshot current config (belt-and-suspenders; Part 4 already
    // preserves /conf.txt during the FS flash, but re-POST ensures parity).
    spaUpdateStep.value = "Saving current config…";
    const savedConfig = await getConfig();

    // Step 2 — queue the FS flash on the device
    spaUpdateStep.value = "Requesting SPA flash…";
    await postSpaUpdateFromUrl(fsUrl);

    // Step 3 — wait for the device to reboot with the new FS image
    spaUpdateStep.value = "Flashing SPA bundle — device will reboot shortly…";
    await pollUntilBack();

    // Step 4 — re-apply config in case the restore in Part 4 had any issues
    spaUpdateStep.value = "Restoring config…";
    await patchConfig(savedConfig);

    // Step 5 — reload into the freshly-flashed SPA
    spaUpdateStep.value = "Done — reloading…";
    await delay(1_500);
    window.location.reload();
  } catch (e) {
    spaUpdateState.value = "error";
    spaUpdateError.value = String(e);
  }
}

export function StatusPage() {
  useEffect(() => {
    async function load() {
      // Don't poll while an update is in flight — the device is intentionally
      // offline during the flash, and we reload on completion anyway.
      if (spaUpdateState.value !== "idle") return;
      loading.value = true;
      try {
        status.value = await getStatus();
        error.value = null;
      } catch (e) {
        error.value = String(e);
      } finally {
        loading.value = false;
      }
    }
    load();
    const id = setInterval(load, 30_000);
    return () => clearInterval(id);
  }, []);

  const d = status.value;

  return (
    <div>
      {loading.value && !d && <p class="muted">Loading…</p>}
      {error.value && spaUpdateState.value === "idle" && (
        <p class="error-msg">{error.value}</p>
      )}

      {/* SPA update available banner */}
      {d?.spa_update_available && d?.spa_fs_url && spaUpdateState.value === "idle" && (
        <div class="update-banner">
          <div style={{ flex: 1 }}>
            <span class="badge">SPA Update Available</span>
            <span class="muted" style={{ marginLeft: "0.5rem" }}>
              Current: {d.spa_version || "unknown"}
              {d.spa_latest_version && (
                <> {"→ Available: "}{d.spa_latest_version}</>
              )}
            </span>
          </div>
          <button class="btn" onClick={() => doSpaUpdate(d.spa_fs_url!)}>
            Update SPA
          </button>
        </div>
      )}

      {/* SPA update in progress */}
      {spaUpdateState.value === "updating" && (
        <div class="update-banner">
          <span class="spinner" />
          <span style={{ flex: 1 }}>{spaUpdateStep.value}</span>
        </div>
      )}

      {/* SPA update error */}
      {spaUpdateState.value === "error" && (
        <div class="update-banner update-banner-error">
          <span class="error-msg" style={{ flex: 1 }}>{spaUpdateError.value}</span>
          <button
            class="btn btn-danger"
            onClick={() => (spaUpdateState.value = "idle")}
          >
            Dismiss
          </button>
        </div>
      )}

      {d && (
        <div class="stat-grid">
          <StatCard label="Device" value={d.device_name || d.chip_id} />
          <StatCard label="Version" value={d.version} />
          {d.spa_version && (
            <StatCard label="SPA Version" value={d.spa_version} />
          )}
          <StatCard label="Uptime" value={uptimeStr.value} />
          <StatCard label="Reset reason" value={d.reset_reason} />

          <div class="stat-card wide">
            <div class="stat-label">Free heap</div>
            <div class="stat-value" style={{ color: heapColor.value }}>
              {(d.free_heap / 1024).toFixed(1)} KB
              <span class="muted"> · {d.heap_fragmentation}% frag</span>
            </div>
            <div class="progress-bar">
              <div
                class="progress-fill"
                style={{
                  width: `${Math.min(100, (d.free_heap / 40000) * 100)}%`,
                  background: heapColor.value,
                }}
              />
            </div>
          </div>

          <div class="stat-card wide">
            <div class="stat-label">WiFi — {d.wifi.ssid}</div>
            <div class="stat-value">
              {d.wifi.ip}
              <span class="muted"> · {d.wifi.rssi} dBm</span>
            </div>
            <div class="progress-bar">
              <div
                class="progress-fill"
                style={{ width: `${d.wifi.quality_pct}%` }}
              />
            </div>
          </div>

          {d.ota.pending_file_exists && (
            <div class="stat-card wide">
              <span class="badge badge-warn">OTA Pending</span>
              <span class="muted"> Awaiting boot confirmation</span>
            </div>
          )}

          {d.next_refresh_in_sec !== undefined && (
            <div class="stat-card wide sketch-row">
              <span class="stat-label">Next data refresh</span>
              <span class="muted">{formatNextRefresh(d.next_refresh_in_sec)}</span>
            </div>
          )}

          <div class="stat-card wide sketch-row">
            <span class="stat-label">Flash</span>
            <span class="muted">
              sketch {(d.sketch_size / 1024).toFixed(0)} KB ·
              free {(d.free_sketch_space / 1024).toFixed(0)} KB ·
              chip {(d.flash_size / 1024).toFixed(0)} KB
            </span>
          </div>
        </div>
      )}
      <p class="muted refresh-hint">
        {loading.value ? "Refreshing…" : "Auto-refreshes every 30 s"}
      </p>
    </div>
  );
}

// Format a "seconds until next refresh" integer the way the legacy footer
// did — h:mm:ss for any positive value, "now / overdue" when ≤ 0.
function formatNextRefresh(seconds: number): string {
  if (seconds <= 0) return "due now";
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${h}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
}

function StatCard({ label, value }: { label: string; value: string }) {
  return (
    <div class="stat-card">
      <div class="stat-label">{label}</div>
      <div class="stat-value">{value}</div>
    </div>
  );
}
