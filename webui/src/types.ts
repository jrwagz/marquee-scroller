export interface WifiStatus {
  ssid: string;
  ip: string;
  rssi: number;
  quality_pct: number;
}

export interface OtaStatus {
  confirm_at: number;
  pending_url: string;
  safe_url: string;
  pending_file_exists: boolean;
}

export interface StatusData {
  version: string;
  uptime_ms: number;
  free_heap: number;
  heap_fragmentation: number;
  chip_id: string;
  device_name: string;
  flash_size: number;
  sketch_size: number;
  free_sketch_space: number;
  reset_reason: string;
  // Set by Phase A (firmware ≥ 3.10.1-wagfam). Optional so the SPA still
  // renders against an older firmware that lacks them — UI just hides
  // the "Next Update" countdown row in that case.
  last_refresh_unix?: number;
  next_refresh_in_sec?: number;
  wifi: WifiStatus;
  ota: OtaStatus;
  // Set by issue #72 parts 2-3 (firmware ≥ 3.10.3-wagfam).
  spa_version?: string;
  spa_update_available?: boolean;
  spa_fs_url?: string;
  spa_latest_version?: string;
  // SPA-FS OTA progress for the deferred /api/spa/update-from-url flash.
  // Optional so the SPA still renders against firmware that predates the
  // tracking; in that case the StatusPage falls back to legacy polling.
  spa_ota?: SpaOtaState;
}

export interface SpaOtaState {
  // "idle" | "queued" | "downloading" | "flashing" | "restoring-conf" | "failed"
  status: string;
  error: string;
}

export interface WeatherData {
  data_valid: boolean;
  city: string;
  country: string;
  temperature: number;
  temp_high: number;
  temp_low: number;
  humidity: number;
  pressure: number;
  wind_speed: number;
  wind_direction_deg: number;
  wind_direction_text: string;
  condition: string;
  description: string;
  icon: string;
  weather_id: number;
  is_metric: boolean;
  temp_symbol: string;
  speed_symbol: string;
  pressure_symbol: string;
  error_message: string;
}

export interface EventsData {
  count: number;
  messages: string[];
  calendar_url_configured: boolean;
  calendar_key_configured: boolean;
}

export interface ActionAck {
  status: string;
  restart_in_ms?: number;
}

export interface ConfigData {
  wagfam_data_url: string;
  wagfam_api_key: string;
  wagfam_event_today: boolean;
  owm_api_key: string;
  geo_location: string;
  is_24hour: boolean;
  is_pm: boolean;
  is_metric: boolean;
  display_intensity: number;
  display_scroll_speed: number;
  display_font: number;
  display_clock_style: number;
  minutes_between_data_refresh: number;
  minutes_between_scrolling: number;
  show_date: boolean;
  show_city: boolean;
  show_condition: boolean;
  show_humidity: boolean;
  show_wind: boolean;
  show_pressure: boolean;
  show_highlow: boolean;
  ota_safe_url: string;
  device_name: string;
  // Issue #95: runtime auto-update toggle. `auto_update_enabled` is the
  // user-controllable boolean (default true) shown in Settings. The
  // optional `auto_update_compile_disabled` is read-only metadata: when
  // true, the firmware was built with WAGFAM_AUTO_UPDATE_DISABLED and the
  // runtime toggle is ignored — the UI reflects this with a disabled
  // checkbox + explanatory note. Both are optional so the SPA still
  // renders against firmware that predates the field.
  auto_update_enabled?: boolean;
  auto_update_compile_disabled?: boolean;
}

// ── Tasmota scheduler ──────────────────────────────────────────────────────

export interface TasmotaDevice {
  ip: string;
  name: string;
}

export interface TasmotaDevicesData {
  devices: TasmotaDevice[];
  max: number;
}

export type TasmotaActionStr = "ON" | "OFF" | "TOGGLE";

export interface TasmotaSchedule {
  id: number;
  ip: string;
  cron: string;
  action: TasmotaActionStr;
  enabled: boolean;
  name: string;
  cron_valid: boolean;
}

export interface TasmotaSchedulesData {
  schedules: TasmotaSchedule[];
  max: number;
}

export interface TasmotaScheduleInput {
  ip: string;
  cron: string;
  action: TasmotaActionStr;
  enabled?: boolean;
  name?: string;
}

export interface TasmotaPowerProbeData {
  ip: string;
  power: string;       // "ON" | "OFF" | "" if unreachable
  reachable: boolean;
  // Both fields below are set by the firmware (≥ this PR). Optional so the
  // SPA still typechecks against older /api/tasmota/power responses.
  pending?: boolean;
  queued?: boolean;
  last_updated_ms_ago?: number;
}

export interface TasmotaDiscoveryProgress {
  state: "idle" | "mdns" | "scanning" | "done";
  id: number;
  started_at_ms: number;
  completed_at_ms: number;
  pings_sent: number;
  pings_responded: number;
  http_probed: number;
  tasmota_found: number;
  current_host_byte: number;
  base_ip: string;
}

export interface TasmotaDiscoveredDevice {
  ip: string;
  name: string;
  hostname: string;
  source: "mdns" | "scan" | "manual";
}

export interface TasmotaDiscoveryData {
  progress: TasmotaDiscoveryProgress;
  results: TasmotaDiscoveredDevice[];
}
