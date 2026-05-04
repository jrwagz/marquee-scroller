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
}
