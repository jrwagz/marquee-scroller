import { useEffect } from "preact/hooks";
import { signal, computed } from "@preact/signals";
import { getConfig, patchConfig } from "../api";
import type { ConfigData } from "../types";

type BoolKey = Extract<
  keyof ConfigData,
  | "is_24hour"
  | "is_pm"
  | "is_metric"
  | "show_date"
  | "show_city"
  | "show_condition"
  | "show_humidity"
  | "show_wind"
  | "show_pressure"
  | "show_highlow"
>;

type StringKey = Extract<
  keyof ConfigData,
  | "wagfam_data_url"
  | "wagfam_api_key"
  | "owm_api_key"
  | "geo_location"
>;

const DEFAULTS: ConfigData = {
  wagfam_data_url: "",
  wagfam_api_key: "",
  wagfam_event_today: false,
  owm_api_key: "",
  geo_location: "",
  is_24hour: false,
  is_pm: true,
  is_metric: false,
  display_intensity: 4,
  display_scroll_speed: 25,
  minutes_between_data_refresh: 15,
  minutes_between_scrolling: 1,
  show_date: false,
  show_city: true,
  show_condition: false,
  show_humidity: false,
  show_wind: false,
  show_pressure: false,
  show_highlow: false,
  ota_safe_url: "",
  device_name: "",
};

const config = signal<ConfigData | null>(null);
const draft = signal<Partial<ConfigData>>({});
// Password is held separately because the firmware only updates it when the
// posted value is non-empty (marquee.ino handleApiConfigPost). The form input
// always starts blank — typing replaces, leaving it blank keeps the current.
const newPassword = signal("");
const loadError = signal<string | null>(null);
const saveStatus = signal<"idle" | "saving" | "ok" | "error">("idle");
const saveError = signal<string | null>(null);

const effective = computed<ConfigData>(() => ({
  ...DEFAULTS,
  ...config.value,
  ...draft.value,
}));

const isDirty = computed(
  () => Object.keys(draft.value).length > 0 || newPassword.value.length > 0,
);

function setVal<K extends keyof ConfigData>(key: K, val: ConfigData[K]) {
  if (config.value?.[key] === val) {
    const d = { ...draft.value };
    delete d[key];
    draft.value = d;
  } else {
    draft.value = { ...draft.value, [key]: val };
  }
}

function setBool(key: BoolKey, val: boolean) {
  setVal(key, val);
}

function setStr(key: StringKey, val: string) {
  setVal(key, val);
}

async function save() {
  saveStatus.value = "saving";
  saveError.value = null;
  const payload: Partial<ConfigData> = { ...draft.value };
  if (newPassword.value.length > 0) {
    payload.web_password = newPassword.value;
  }
  try {
    await patchConfig(payload);
    config.value = { ...effective.value };
    draft.value = {};
    newPassword.value = "";
    saveStatus.value = "ok";
    setTimeout(() => {
      saveStatus.value = "idle";
    }, 3000);
  } catch (e) {
    saveError.value = String(e);
    saveStatus.value = "error";
  }
}

export function SettingsPage() {
  useEffect(() => {
    getConfig()
      .then((data) => {
        config.value = data;
        draft.value = {};
        loadError.value = null;
      })
      .catch((e: unknown) => {
        loadError.value = String(e);
      });
  }, []);

  const cfg = effective.value;
  const saving = saveStatus.value === "saving";

  if (!config.value && !loadError.value) {
    return <p class="muted">Loading…</p>;
  }
  if (loadError.value) {
    return <p class="error-msg">{loadError.value}</p>;
  }

  return (
    <div>
      <div class="form-section">
        <h2>Display</h2>

        <div class="form-row">
          <label class="form-label" for="intensity">
            Brightness
          </label>
          <div class="slider-group">
            <input
              id="intensity"
              type="range"
              min="0"
              max="15"
              value={cfg.display_intensity}
              onInput={(e) =>
                setVal(
                  "display_intensity",
                  +(e.target as HTMLInputElement).value,
                )
              }
            />
            <span class="slider-val">{cfg.display_intensity}</span>
          </div>
        </div>

        <div class="form-row">
          <label class="form-label" for="speed">
            Scroll speed
          </label>
          <div class="slider-group">
            <input
              id="speed"
              type="range"
              min="5"
              max="100"
              value={cfg.display_scroll_speed}
              onInput={(e) =>
                setVal(
                  "display_scroll_speed",
                  +(e.target as HTMLInputElement).value,
                )
              }
            />
            <span class="slider-val">{cfg.display_scroll_speed} ms</span>
          </div>
        </div>

        <ToggleRow
          id="24h"
          label="24-hour clock"
          checked={cfg.is_24hour}
          onChange={(v) => setBool("is_24hour", v)}
        />
        {!cfg.is_24hour && (
          <ToggleRow
            id="pm"
            label="Show PM indicator"
            checked={cfg.is_pm}
            onChange={(v) => setBool("is_pm", v)}
          />
        )}
        <ToggleRow
          id="metric"
          label="Metric units (°C / km/h)"
          checked={cfg.is_metric}
          onChange={(v) => setBool("is_metric", v)}
        />
      </div>

      <div class="form-section">
        <h2>Refresh</h2>

        <div class="form-row">
          <label class="form-label" for="data-interval">
            Data interval
          </label>
          <div class="num-group">
            <input
              id="data-interval"
              type="number"
              min="1"
              max="60"
              value={cfg.minutes_between_data_refresh}
              onInput={(e) =>
                setVal(
                  "minutes_between_data_refresh",
                  Math.max(1, +(e.target as HTMLInputElement).value),
                )
              }
            />
            <span class="form-note">min</span>
          </div>
        </div>

        <div class="form-row">
          <label class="form-label" for="scroll-interval">
            Scroll interval
          </label>
          <div class="num-group">
            <input
              id="scroll-interval"
              type="number"
              min="1"
              max="10"
              value={cfg.minutes_between_scrolling}
              onInput={(e) =>
                setVal(
                  "minutes_between_scrolling",
                  Math.max(1, +(e.target as HTMLInputElement).value),
                )
              }
            />
            <span class="form-note">min</span>
          </div>
        </div>
      </div>

      <div class="form-section">
        <h2>Weather items</h2>
        {(
          [
            { key: "show_date", label: "Date" },
            { key: "show_city", label: "City" },
            { key: "show_condition", label: "Condition" },
            { key: "show_humidity", label: "Humidity" },
            { key: "show_wind", label: "Wind" },
            { key: "show_pressure", label: "Pressure" },
            { key: "show_highlow", label: "High / Low" },
          ] as { key: BoolKey; label: string }[]
        ).map(({ key, label }) => (
          <ToggleRow
            key={key}
            id={key}
            label={label}
            checked={cfg[key]}
            onChange={(v) => setBool(key, v)}
          />
        ))}
      </div>

      <div class="form-section">
        <h2>Weather source</h2>
        <TextRow
          id="owm-key"
          label="OpenWeatherMap API key"
          value={cfg.owm_api_key}
          onChange={(v) => setStr("owm_api_key", v)}
          placeholder="get one at openweathermap.org"
        />
        <TextRow
          id="geo"
          label="City / location"
          value={cfg.geo_location}
          onChange={(v) => setStr("geo_location", v)}
          placeholder="city ID, 'Chicago,US', or 'lat,lon'"
        />
      </div>

      <div class="form-section">
        <h2>Calendar source</h2>
        <TextRow
          id="wagfam-url"
          label="Calendar JSON URL"
          value={cfg.wagfam_data_url}
          onChange={(v) => setStr("wagfam_data_url", v)}
          placeholder="https://example.com/data.json"
        />
        <TextRow
          id="wagfam-key"
          label="Calendar API key"
          value={cfg.wagfam_api_key}
          onChange={(v) => setStr("wagfam_api_key", v)}
          placeholder="optional"
        />
      </div>

      <div class="form-section">
        <h2>Security</h2>
        <div class="form-row">
          <label class="form-label" for="webpw">
            Web password
          </label>
          <div class="text-group">
            <input
              id="webpw"
              type="password"
              autocomplete="new-password"
              value={newPassword.value}
              placeholder="leave blank to keep current"
              onInput={(e) => {
                newPassword.value = (e.target as HTMLInputElement).value;
              }}
            />
            <span class="form-note">
              You'll need to re-authenticate after save.
            </span>
          </div>
        </div>
      </div>

      <div class="save-bar">
        <button
          class="btn"
          disabled={!isDirty.value || saving}
          onClick={save}
        >
          {saving ? <><span class="spinner" /> Saving…</> : "Save"}
        </button>
        {saveStatus.value === "ok" && (
          <span class="save-ok">Saved!</span>
        )}
        {saveStatus.value === "error" && (
          <span class="error-msg">{saveError.value}</span>
        )}
        {isDirty.value && saveStatus.value === "idle" && (
          <span class="muted">
            {Object.keys(draft.value).length + (newPassword.value ? 1 : 0)} change
            {Object.keys(draft.value).length + (newPassword.value ? 1 : 0) !== 1 ? "s" : ""} unsaved
          </span>
        )}
      </div>
    </div>
  );
}

function ToggleRow({
  id,
  label,
  checked,
  onChange,
}: {
  id: string;
  label: string;
  checked: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <label class="form-row toggle-row" for={id}>
      <span class="form-label">{label}</span>
      <input
        id={id}
        type="checkbox"
        class="toggle"
        checked={checked}
        onChange={(e) => onChange((e.target as HTMLInputElement).checked)}
      />
    </label>
  );
}

function TextRow({
  id,
  label,
  value,
  onChange,
  placeholder,
}: {
  id: string;
  label: string;
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
}) {
  return (
    <div class="form-row">
      <label class="form-label" for={id}>
        {label}
      </label>
      <div class="text-group">
        <input
          id={id}
          type="text"
          value={value}
          placeholder={placeholder}
          onInput={(e) => onChange((e.target as HTMLInputElement).value)}
        />
      </div>
    </div>
  );
}
