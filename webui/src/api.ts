import type { StatusData, ConfigData } from "./types";

const POST_HEADERS = {
  "Content-Type": "application/json",
  "X-Requested-With": "XMLHttpRequest",
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

export const patchConfig = (
  patch: Partial<ConfigData>,
): Promise<{ status: string }> =>
  apiFetch<{ status: string }>("/api/config", {
    method: "POST",
    headers: POST_HEADERS,
    body: JSON.stringify(patch),
  });

export const postRefresh = (): Promise<{ status: string }> =>
  apiFetch<{ status: string }>("/api/refresh", {
    method: "POST",
    headers: POST_HEADERS,
  });

export const postRestart = (): Promise<{ status: string }> =>
  apiFetch<{ status: string }>("/api/restart", {
    method: "POST",
    headers: POST_HEADERS,
  });
