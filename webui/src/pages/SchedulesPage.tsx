// Schedules tab — Tasmota switch control on a timer.
//
// Two sections stacked on the page:
//
//   1. Devices: the "array of IPs" config. Each row is { ip, name, live }.
//      live state ("ON"/"OFF"/—) is probed asynchronously via /api/tasmota/power
//      so the user can sanity-check connectivity before scheduling. Whole
//      list is PUT-replaced on Save (simplest for finite arrays; max 16).
//
//   2. Schedules: cron-driven actions. Each schedule has a target device IP,
//      a 5-field cron string, an action (ON/OFF/TOGGLE), enable toggle, and
//      a name. Cron picker has two views:
//        - Mortals: HH:MM time picker + DoW checkboxes + 3 preset buttons
//        - Nerds: raw text input
//      The two views share the same string state; toggling "raw" just hides
//      the form helpers. Either way the cron lands as a single string in
//      the backend, which validates with the same parser used at runtime.

import { useEffect } from "preact/hooks";
import { signal } from "@preact/signals";
import {
  getTasmotaDevices,
  putTasmotaDevices,
  getTasmotaSchedules,
  createTasmotaSchedule,
  updateTasmotaSchedule,
  deleteTasmotaSchedule,
  runTasmotaScheduleNow,
  getTasmotaPower,
  startTasmotaDiscovery,
  getTasmotaDiscoveryState,
} from "../api";
import type {
  TasmotaDevice,
  TasmotaSchedule,
  TasmotaActionStr,
  TasmotaDiscoveryData,
} from "../types";

// ── shared state ──────────────────────────────────────────────────────────

const devices = signal<TasmotaDevice[]>([]);
const devicesDirty = signal(false);
const devicesError = signal<string | null>(null);

// Power-state cache, keyed by IP. Refreshed lazily — clicking "probe" or
// adding a new device kicks one off.
const livePower = signal<Record<string, string>>({});

const schedules = signal<TasmotaSchedule[]>([]);
const schedulesError = signal<string | null>(null);

// Auto-discovery state — open/visible while a scan is in flight or results
// are waiting to be imported.
const discoveryOpen = signal(false);
const discoveryData = signal<TasmotaDiscoveryData | null>(null);
const discoveryError = signal<string | null>(null);
// Which IPs the user has checked to import. Keyed by IP so we don't double-add.
const discoveryPicked = signal<Set<string>>(new Set());

// Edit form state (works for both create and update — `editingId` switches).
const editingId = signal<number | null>(null);
const formIp = signal("");
const formCron = signal("0 22 * * *");
const formAction = signal<TasmotaActionStr>("OFF");
const formEnabled = signal(true);
const formName = signal("");
const formRawMode = signal(false);
const formError = signal<string | null>(null);

// ── loaders ───────────────────────────────────────────────────────────────

async function reloadDevices() {
  try {
    const data = await getTasmotaDevices();
    devices.value = data.devices;
    devicesDirty.value = false;
    devicesError.value = null;
  } catch (e) {
    devicesError.value = (e as Error).message;
  }
}

async function reloadSchedules() {
  try {
    const data = await getTasmotaSchedules();
    schedules.value = data.schedules;
    schedulesError.value = null;
  } catch (e) {
    schedulesError.value = (e as Error).message;
  }
}

async function probePower(ip: string) {
  try {
    const data = await getTasmotaPower(ip);
    livePower.value = { ...livePower.value, [ip]: data.reachable ? data.power : "—" };
  } catch {
    livePower.value = { ...livePower.value, [ip]: "—" };
  }
}

async function saveDevices() {
  try {
    const data = await putTasmotaDevices(devices.value);
    devices.value = data.devices;
    devicesDirty.value = false;
    devicesError.value = null;
  } catch (e) {
    devicesError.value = (e as Error).message;
  }
}

// ── auto-discovery ────────────────────────────────────────────────────────

let discoveryPollHandle: ReturnType<typeof setInterval> | null = null;

function stopDiscoveryPoll() {
  if (discoveryPollHandle !== null) {
    clearInterval(discoveryPollHandle);
    discoveryPollHandle = null;
  }
}

async function pollDiscovery() {
  try {
    const data = await getTasmotaDiscoveryState();
    discoveryData.value = data;
    if (data.progress.state === "done" || data.progress.state === "idle") {
      stopDiscoveryPoll();
    }
  } catch (e) {
    discoveryError.value = (e as Error).message;
    stopDiscoveryPoll();
  }
}

async function startDiscovery() {
  discoveryError.value = null;
  discoveryPicked.value = new Set();
  discoveryOpen.value = true;
  try {
    await startTasmotaDiscovery();
  } catch (e) {
    discoveryError.value = (e as Error).message;
    return;
  }
  // Fast poll while running. The full scan takes ~60s on a /24; polling
  // every 2s gives ~30 progress updates, which is enough for the bar.
  void pollDiscovery();
  stopDiscoveryPoll();
  discoveryPollHandle = setInterval(() => void pollDiscovery(), 2000);
}

function toggleDiscoveryPick(ip: string) {
  const next = new Set(discoveryPicked.value);
  if (next.has(ip)) next.delete(ip);
  else next.add(ip);
  discoveryPicked.value = next;
}

function importPickedDiscoveries() {
  const data = discoveryData.value;
  if (!data) return;
  // Add picked IPs to the devices list (skip ones already there).
  const existing = new Set(devices.value.map((d) => d.ip));
  const additions = data.results
    .filter((r) => discoveryPicked.value.has(r.ip) && !existing.has(r.ip))
    .map<TasmotaDevice>((r) => ({
      ip: r.ip,
      name: r.name || r.hostname || "",
    }));
  if (additions.length === 0) {
    discoveryOpen.value = false;
    return;
  }
  devices.value = [...devices.value, ...additions];
  devicesDirty.value = true;
  discoveryOpen.value = false;
  discoveryData.value = null;
  discoveryPicked.value = new Set();
}

// ── cron picker helpers ──────────────────────────────────────────────────

const DOW_LABELS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

// Build a cron string from "mortals' mode" state.
function buildCron(
  hour: number,
  minute: number,
  daysOfWeek: number[],  // 0=Sun..6=Sat
): string {
  let dow = "*";
  if (daysOfWeek.length > 0 && daysOfWeek.length < 7) {
    // Try to detect contiguous ranges for readability; otherwise list.
    const sorted = [...daysOfWeek].sort((a, b) => a - b);
    let contiguous = true;
    for (let i = 1; i < sorted.length; i++) {
      if (sorted[i] !== sorted[i - 1] + 1) {
        contiguous = false;
        break;
      }
    }
    if (contiguous && sorted.length > 1) {
      dow = `${sorted[0]}-${sorted[sorted.length - 1]}`;
    } else {
      dow = sorted.join(",");
    }
  }
  return `${minute} ${hour} * * ${dow}`;
}

// Parse a cron back into the mortals'-mode fields. Returns null if the cron
// is too exotic for the picker (e.g. uses */N, specific months, etc.) —
// caller falls back to raw-text mode.
function parseSimpleCron(s: string): {
  hour: number;
  minute: number;
  daysOfWeek: number[];
} | null {
  const parts = s.trim().split(/\s+/);
  if (parts.length !== 5) return null;
  const [m, h, dom, mon, dow] = parts;
  if (dom !== "*" || mon !== "*") return null;
  if (!/^\d+$/.test(m) || !/^\d+$/.test(h)) return null;
  const minute = Number(m);
  const hour = Number(h);
  if (minute < 0 || minute > 59 || hour < 0 || hour > 23) return null;

  let days: number[];
  if (dow === "*") {
    days = [0, 1, 2, 3, 4, 5, 6];
  } else if (/^\d-\d$/.test(dow)) {
    const [a, b] = dow.split("-").map(Number);
    days = [];
    for (let i = a; i <= b; i++) days.push(i);
  } else if (/^\d(,\d)*$/.test(dow)) {
    days = dow.split(",").map(Number);
  } else {
    return null;
  }
  return { hour, minute, daysOfWeek: days };
}

// ── components ────────────────────────────────────────────────────────────

export function SchedulesPage() {
  useEffect(() => {
    void reloadDevices();
    void reloadSchedules();
  }, []);

  return (
    <div class="schedules-page">
      <DevicesCard />
      {discoveryOpen.value && <DiscoveryCard />}
      <ScheduleFormCard />
      <SchedulesListCard />
    </div>
  );
}

function DevicesCard() {
  const list = devices.value;
  return (
    <section class="card">
      <header class="card-header">
        <h2>Tasmota devices</h2>
        <button onClick={() => void reloadDevices()}>Refresh</button>
        <button onClick={() => void startDiscovery()}>Auto-detect</button>
      </header>
      <p class="muted">
        IPs the clock controls. Schedules reference these by IP. Power state
        is probed live (✓ = reachable + state, "—" = no response).
      </p>

      {devicesError.value && <p class="error">{devicesError.value}</p>}

      <table class="net-table">
        <thead>
          <tr>
            <th>IP</th>
            <th>Name</th>
            <th>State</th>
            <th />
          </tr>
        </thead>
        <tbody>
          {list.map((d, i) => (
            <tr key={i}>
              <td>
                <input
                  type="text"
                  value={d.ip}
                  onInput={(e) => {
                    const next = [...list];
                    next[i] = { ...next[i], ip: (e.target as HTMLInputElement).value };
                    devices.value = next;
                    devicesDirty.value = true;
                  }}
                  placeholder="192.168.1.42"
                />
              </td>
              <td>
                <input
                  type="text"
                  value={d.name}
                  onInput={(e) => {
                    const next = [...list];
                    next[i] = { ...next[i], name: (e.target as HTMLInputElement).value };
                    devices.value = next;
                    devicesDirty.value = true;
                  }}
                  placeholder="(optional)"
                />
              </td>
              <td>
                <code>{livePower.value[d.ip] ?? "?"}</code>{" "}
                <button class="link-btn" onClick={() => void probePower(d.ip)}>
                  probe
                </button>
              </td>
              <td>
                <button
                  class="link-btn"
                  onClick={() => {
                    devices.value = list.filter((_, j) => j !== i);
                    devicesDirty.value = true;
                  }}
                >
                  remove
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

      <div class="card-actions">
        <button
          onClick={() => {
            devices.value = [...list, { ip: "", name: "" }];
            devicesDirty.value = true;
          }}
        >
          + Add device
        </button>
        <button
          class={devicesDirty.value ? "primary" : ""}
          disabled={!devicesDirty.value}
          onClick={() => void saveDevices()}
        >
          {devicesDirty.value ? "Save changes" : "No changes"}
        </button>
      </div>
    </section>
  );
}

function DiscoveryCard() {
  const data = discoveryData.value;
  const progress = data?.progress;
  const isRunning = progress?.state === "mdns" || progress?.state === "scanning";
  const isDone = progress?.state === "done";
  // Scan progress %: based on /24 sweep (1..254). mDNS phase happens first
  // and is fast, so we show 0% during it; ping sweep dominates the bar.
  const pct = progress?.state === "scanning"
    ? Math.min(100, Math.round((progress.pings_sent / 254) * 100))
    : isDone ? 100 : 0;
  const existingIps = new Set(devices.value.map((d) => d.ip));

  return (
    <section class="card">
      <header class="card-header">
        <h2>Auto-detect Tasmota devices</h2>
        <button onClick={() => {
          stopDiscoveryPoll();
          discoveryOpen.value = false;
          discoveryData.value = null;
        }}>
          Close
        </button>
      </header>

      <p class="muted">
        Combined mDNS query + /24 ping sweep + HTTP probe. Catches Tasmotas
        regardless of <code>SetOption55</code> setting. Full scan takes
        ~60s on a typical /24.
      </p>

      {discoveryError.value && (
        <p class="error">{discoveryError.value}</p>
      )}

      {progress && (
        <div class="scan-progress">
          {isRunning ? `Scanning… ${progress.pings_sent}/254 sent, ` : ""}
          {progress.tasmota_found} Tasmota(s) found,
          {" "}{progress.pings_responded} hosts responding to ping.
          <div class="progress-bar">
            <div class="progress-fill" style={{ width: `${pct}%` }} />
          </div>
        </div>
      )}

      {data && data.results.length > 0 && (
        <>
          <table class="net-table">
            <thead>
              <tr>
                <th />
                <th>IP</th>
                <th>Name</th>
                <th>Hostname</th>
                <th>Source</th>
              </tr>
            </thead>
            <tbody>
              {data.results.map((d) => {
                const already = existingIps.has(d.ip);
                const picked = discoveryPicked.value.has(d.ip);
                return (
                  <tr key={d.ip} class={already ? "row-muted" : ""}>
                    <td>
                      <input
                        type="checkbox"
                        checked={picked || already}
                        disabled={already}
                        onChange={() => toggleDiscoveryPick(d.ip)}
                      />
                    </td>
                    <td><code>{d.ip}</code></td>
                    <td>{d.name || <span class="muted">—</span>}</td>
                    <td><code>{d.hostname || ""}</code></td>
                    <td>
                      <span class={`tag tag-${d.source}`}>{d.source}</span>
                      {already && <span class="muted small"> · already added</span>}
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>

          <div class="card-actions">
            <button
              class="primary"
              disabled={discoveryPicked.value.size === 0}
              onClick={importPickedDiscoveries}
            >
              Import {discoveryPicked.value.size} selected
            </button>
          </div>
        </>
      )}

      {isDone && data?.results.length === 0 && (
        <p class="muted">
          No Tasmotas found. If you know a specific IP is a Tasmota,
          add it manually in the Devices table above — auto-detect
          can miss devices behind aggressive firewalls.
        </p>
      )}
    </section>
  );
}

function ScheduleFormCard() {
  const isEdit = editingId.value !== null;

  // Build / re-parse for the mortals' mode controls.
  const simple = parseSimpleCron(formCron.value);
  const canUseMortals = simple !== null;
  const showRaw = formRawMode.value || !canUseMortals;

  const updateMortals = (
    hour: number,
    minute: number,
    daysOfWeek: number[],
  ) => {
    formCron.value = buildCron(hour, minute, daysOfWeek);
  };

  const submit = async () => {
    formError.value = null;
    try {
      const payload = {
        ip: formIp.value,
        cron: formCron.value,
        action: formAction.value,
        enabled: formEnabled.value,
        name: formName.value,
      };
      if (isEdit && editingId.value !== null) {
        await updateTasmotaSchedule(editingId.value, payload);
      } else {
        await createTasmotaSchedule(payload);
      }
      // Reset form
      editingId.value = null;
      formIp.value = "";
      formCron.value = "0 22 * * *";
      formAction.value = "OFF";
      formEnabled.value = true;
      formName.value = "";
      formRawMode.value = false;
      void reloadSchedules();
    } catch (e) {
      formError.value = (e as Error).message;
    }
  };

  return (
    <section class="card">
      <header class="card-header">
        <h2>{isEdit ? "Edit schedule" : "Add a schedule"}</h2>
        {isEdit && (
          <button
            onClick={() => {
              editingId.value = null;
              formIp.value = "";
              formCron.value = "0 22 * * *";
              formAction.value = "OFF";
              formEnabled.value = true;
              formName.value = "";
            }}
          >
            Cancel edit
          </button>
        )}
      </header>

      <div class="schedule-form">
        <label>
          Name (optional)
          <input
            value={formName.value}
            onInput={(e) => (formName.value = (e.target as HTMLInputElement).value)}
            placeholder="Office lights off"
          />
        </label>

        <label>
          Device
          <select
            value={formIp.value}
            onChange={(e) =>
              (formIp.value = (e.target as HTMLSelectElement).value)
            }
          >
            <option value="">— select device —</option>
            {devices.value.map((d) => (
              <option key={d.ip} value={d.ip}>
                {d.ip} {d.name ? `(${d.name})` : ""}
              </option>
            ))}
          </select>
        </label>

        <label>
          Action
          <select
            value={formAction.value}
            onChange={(e) =>
              (formAction.value = (e.target as HTMLSelectElement)
                .value as TasmotaActionStr)
            }
          >
            <option value="OFF">OFF</option>
            <option value="ON">ON</option>
            <option value="TOGGLE">TOGGLE</option>
          </select>
        </label>

        <div class="cron-picker">
          <div class="cron-header">
            <label class="inline">
              <input
                type="checkbox"
                checked={showRaw}
                disabled={!canUseMortals}
                onChange={(e) =>
                  (formRawMode.value = (e.target as HTMLInputElement).checked)
                }
              />
              Raw cron mode (for the nerds)
            </label>
            {!canUseMortals && (
              <span class="muted small">
                (current cron is too exotic for the picker — raw-mode is forced)
              </span>
            )}
          </div>

          {showRaw ? (
            <label>
              Cron string (5 fields: <code>min hour dom month dow</code>)
              <input
                value={formCron.value}
                onInput={(e) =>
                  (formCron.value = (e.target as HTMLInputElement).value)
                }
                placeholder="0 22 * * *"
              />
            </label>
          ) : simple ? (
            <>
              <div class="row">
                <label>
                  Hour
                  <input
                    type="number"
                    min={0}
                    max={23}
                    value={simple.hour}
                    onInput={(e) =>
                      updateMortals(
                        Number((e.target as HTMLInputElement).value) || 0,
                        simple.minute,
                        simple.daysOfWeek,
                      )
                    }
                  />
                </label>
                <label>
                  Minute
                  <input
                    type="number"
                    min={0}
                    max={59}
                    value={simple.minute}
                    onInput={(e) =>
                      updateMortals(
                        simple.hour,
                        Number((e.target as HTMLInputElement).value) || 0,
                        simple.daysOfWeek,
                      )
                    }
                  />
                </label>
              </div>

              <div class="dow-row">
                {DOW_LABELS.map((label, idx) => {
                  const on = simple.daysOfWeek.includes(idx);
                  return (
                    <label key={idx} class="dow-chip">
                      <input
                        type="checkbox"
                        checked={on}
                        onChange={(e) => {
                          const checked = (e.target as HTMLInputElement).checked;
                          const next = checked
                            ? [...simple.daysOfWeek, idx]
                            : simple.daysOfWeek.filter((d) => d !== idx);
                          updateMortals(simple.hour, simple.minute, next);
                        }}
                      />
                      {label}
                    </label>
                  );
                })}
              </div>

              <div class="presets">
                <button
                  onClick={() =>
                    updateMortals(simple.hour, simple.minute, [0, 1, 2, 3, 4, 5, 6])
                  }
                >
                  Daily
                </button>
                <button
                  onClick={() =>
                    updateMortals(simple.hour, simple.minute, [1, 2, 3, 4, 5])
                  }
                >
                  Weekdays
                </button>
                <button
                  onClick={() =>
                    updateMortals(simple.hour, simple.minute, [0, 6])
                  }
                >
                  Weekends
                </button>
              </div>
            </>
          ) : null}

          <p class="muted small">
            Resulting cron: <code>{formCron.value}</code>
          </p>
        </div>

        <label class="inline">
          <input
            type="checkbox"
            checked={formEnabled.value}
            onChange={(e) =>
              (formEnabled.value = (e.target as HTMLInputElement).checked)
            }
          />
          Enabled
        </label>

        {formError.value && <p class="error">{formError.value}</p>}

        <button onClick={() => void submit()} disabled={!formIp.value}>
          {isEdit ? "Save changes" : "Create schedule"}
        </button>
      </div>
    </section>
  );
}

function SchedulesListCard() {
  const list = schedules.value;
  if (schedulesError.value) {
    return (
      <section class="card">
        <h2>Schedules</h2>
        <p class="error">{schedulesError.value}</p>
      </section>
    );
  }
  if (list.length === 0) {
    return (
      <section class="card">
        <h2>Schedules</h2>
        <p class="muted">No schedules yet. Add one using the form above.</p>
      </section>
    );
  }
  return (
    <section class="card">
      <h2>Schedules ({list.length})</h2>
      <table class="net-table">
        <thead>
          <tr>
            <th>Name</th>
            <th>Device</th>
            <th>Cron</th>
            <th>Action</th>
            <th>Enabled</th>
            <th />
          </tr>
        </thead>
        <tbody>
          {list.map((s) => (
            <tr key={s.id} class={s.cron_valid ? "" : "row-bad"}>
              <td>{s.name || <span class="muted">—</span>}</td>
              <td><code>{s.ip}</code></td>
              <td>
                <code>{s.cron}</code>
                {!s.cron_valid && (
                  <span class="error-inline"> (invalid)</span>
                )}
              </td>
              <td><code>{s.action}</code></td>
              <td>{s.enabled ? "✓" : "—"}</td>
              <td>
                <button
                  class="link-btn"
                  onClick={() => {
                    editingId.value = s.id;
                    formIp.value = s.ip;
                    formCron.value = s.cron;
                    formAction.value = s.action;
                    formEnabled.value = s.enabled;
                    formName.value = s.name;
                    formRawMode.value = false;
                  }}
                >
                  edit
                </button>{" "}
                <button
                  class="link-btn"
                  onClick={async () => {
                    try {
                      await runTasmotaScheduleNow(s.id);
                    } catch (e) {
                      alert(`Test failed: ${(e as Error).message}`);
                    }
                  }}
                >
                  test now
                </button>{" "}
                <button
                  class="link-btn"
                  onClick={async () => {
                    if (!confirm(`Delete "${s.name || s.cron}"?`)) return;
                    try {
                      await deleteTasmotaSchedule(s.id);
                      void reloadSchedules();
                    } catch (e) {
                      alert(`Delete failed: ${(e as Error).message}`);
                    }
                  }}
                >
                  delete
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}
