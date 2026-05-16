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
} from "../api";
import type {
  TasmotaDevice,
  TasmotaSchedule,
  TasmotaActionStr,
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
