import type {
  StatusData,
  ConfigData,
  WeatherData,
  EventsData,
  ActionAck,
} from "./types";

const POST_HEADERS = {
  "Content-Type": "application/json",
} as const;

async function apiFetch<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, init);
  if (!res.ok) {
    const body = await res.text().catch(() => res.statusText);
    throw new Error(`${res.status} ${res.statusText}: ${body}`);
  }
  return res.json() as Promise<T>;
}

export const getStatus = (): Promise<StatusData> =>
  apiFetch<StatusData>("/api/status");

export const getConfig = (): Promise<ConfigData> =>
  apiFetch<ConfigData>("/api/config");

export const getWeather = (): Promise<WeatherData> =>
  apiFetch<WeatherData>("/api/weather");

export const getEvents = (): Promise<EventsData> =>
  apiFetch<EventsData>("/api/events");

export const patchConfig = (
  patch: Partial<ConfigData>,
): Promise<ActionAck> =>
  apiFetch<ActionAck>("/api/config", {
    method: "POST",
    headers: POST_HEADERS,
    body: JSON.stringify(patch),
  });

export const postRefresh = (): Promise<ActionAck> =>
  apiFetch<ActionAck>("/api/refresh", {
    method: "POST",
    headers: POST_HEADERS,
  });

export const postRestart = (): Promise<ActionAck> =>
  apiFetch<ActionAck>("/api/restart", {
    method: "POST",
    headers: POST_HEADERS,
  });

export const postSystemReset = (): Promise<ActionAck> =>
  apiFetch<ActionAck>("/api/system-reset", {
    method: "POST",
    headers: POST_HEADERS,
  });

export const postForgetWifi = (): Promise<ActionAck> =>
  apiFetch<ActionAck>("/api/forget-wifi", {
    method: "POST",
    headers: POST_HEADERS,
  });
