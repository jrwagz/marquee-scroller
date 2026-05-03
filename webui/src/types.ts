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
  wifi: WifiStatus;
  ota: OtaStatus;
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
  web_password: string;
}
