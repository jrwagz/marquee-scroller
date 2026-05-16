// LAN-visibility UI (Phase 4 of the feature). Surfaces the three backend
// endpoints landed in Phases 1-3:
//
//   GET  /api/network/neighbors        — ARP cache snapshot
//   POST /api/network/scan             — kick off active /24 scan
//   GET  /api/network/scan/state       — current scan progress
//   GET  /api/network/scan/history     — last 20 scans (NDJSON)
//   POST /api/network/probe            — fire HTTP at a LAN device
//   GET  /api/network/probe/audit      — last 50 probes (NDJSON)
//
// Two cards stacked on the page: Neighbors / Scan above, Probe below.
// Probe defaults the URL field to whatever neighbor the user last clicked
// "probe" on so the common path is one click in either panel.

import { useEffect } from "preact/hooks";
import { signal } from "@preact/signals";
import {
  getNetworkNeighbors,
  postNetworkScan,
  getNetworkScanState,
  getNetworkScanHistory,
  postNetworkProbe,
  getNetworkProbeAudit,
} from "../api";
import type {
  NeighborsData,
  ScanProgress,
  ProbeRequest,
  ProbeResult,
} from "../types";

// ── neighbors / scan state ────────────────────────────────────────────────

const neighbors = signal<NeighborsData | null>(null);
const neighborsErr = signal<string | null>(null);

const scan = signal<ScanProgress | null>(null);
const scanErr = signal<string | null>(null);

const scanHistory = signal<string>("");
const scanHistoryLoading = signal(false);

// ── probe state ───────────────────────────────────────────────────────────

const probeUrl = signal("http://192.168.1.1/");
const probeMethod = signal<"GET" | "POST" | "PUT">("GET");
const probeBody = signal("");
const probeTimeoutMs = signal(3000);
const probeResult = signal<ProbeResult | null>(null);
const probeBusy = signal(false);
const probeErr = signal<string | null>(null);

const probeAudit = signal<string>("");

// ── data loaders ──────────────────────────────────────────────────────────

async function reloadNeighbors() {
  try {
    neighbors.value = await getNetworkNeighbors();
    neighborsErr.value = null;
  } catch (e) {
    neighborsErr.value = (e as Error).message;
  }
}

async function reloadScanState() {
  try {
    scan.value = await getNetworkScanState();
    scanErr.value = null;
  } catch (e) {
    scanErr.value = (e as Error).message;
  }
}

async function reloadScanHistory() {
  scanHistoryLoading.value = true;
  try {
    scanHistory.value = await getNetworkScanHistory();
  } catch (e) {
    // Empty history is fine — surface only real errors
    scanHistory.value = "";
    scanErr.value = (e as Error).message;
  } finally {
    scanHistoryLoading.value = false;
  }
}

async function reloadProbeAudit() {
  try {
    probeAudit.value = await getNetworkProbeAudit();
  } catch {
    probeAudit.value = "";
  }
}

async function startScan() {
  try {
    scan.value = await postNetworkScan();
    scanErr.value = null;
  } catch (e) {
    scanErr.value = (e as Error).message;
  }
}

async function sendProbe() {
  probeBusy.value = true;
  probeErr.value = null;
  probeResult.value = null;
  try {
    const req: ProbeRequest = {
      url: probeUrl.value,
      method: probeMethod.value,
      timeout_ms: probeTimeoutMs.value,
    };
    if (probeMethod.value !== "GET") req.body = probeBody.value;
    probeResult.value = await postNetworkProbe(req);
  } catch (e) {
    probeErr.value = (e as Error).message;
  } finally {
    probeBusy.value = false;
    await reloadProbeAudit();
  }
}

// Parse NDJSON line-by-line, defensively — a partial line during a write
// shouldn't crash the page.
function parseNdjson<T>(raw: string): T[] {
  if (!raw) return [];
  const out: T[] = [];
  for (const line of raw.split("\n")) {
    const s = line.trim();
    if (!s) continue;
    try {
      out.push(JSON.parse(s) as T);
    } catch {
      // skip malformed lines
    }
  }
  return out;
}

// ── component ─────────────────────────────────────────────────────────────

export function NetworkPage() {
  useEffect(() => {
    void reloadNeighbors();
    void reloadScanState();
    void reloadScanHistory();
    void reloadProbeAudit();

    // While a scan is running, poll state + neighbors fast so the UI
    // shows progress. When idle/done, slow down to avoid wasting cycles.
    const tick = setInterval(() => {
      const running = scan.value?.state === "running";
      void reloadScanState();
      if (running) void reloadNeighbors();
    }, 1500);

    return () => clearInterval(tick);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <div class="network-page">
      <NeighborsCard />
      <ProbeCard />
    </div>
  );
}

function NeighborsCard() {
  const data = neighbors.value;
  const s = scan.value;
  const running = s?.state === "running";

  return (
    <section class="card">
      <header class="card-header">
        <h2>Neighbors on the LAN</h2>
        <button onClick={() => void reloadNeighbors()} disabled={running}>
          Refresh
        </button>
        <button onClick={() => void startScan()} disabled={running}>
          {running ? "Scanning…" : "Scan /24"}
        </button>
      </header>

      {running && s && (
        <div class="scan-progress">
          Scan #{s.id}: {s.pings_sent} sent · {s.pings_responded} replied ·
          current {s.base_ip.replace(/\.0$/, ".")}{s.current_host_byte}
        </div>
      )}

      {neighborsErr.value && (
        <p class="error">Couldn't read /api/network/neighbors: {neighborsErr.value}</p>
      )}

      {data === null ? (
        <p>Loading…</p>
      ) : data.neighbors.length === 0 ? (
        <p class="muted">
          ARP cache is empty. Start a scan — that ICMP-pings the local /24 so the
          clock can learn its neighbors.
        </p>
      ) : (
        <table class="net-table">
          <thead>
            <tr>
              <th>IP</th>
              <th>MAC</th>
              <th>iface</th>
              <th />
            </tr>
          </thead>
          <tbody>
            {data.neighbors.map((n) => (
              <tr key={n.mac}>
                <td><code>{n.ip}</code></td>
                <td><code>{n.mac}</code></td>
                <td><code>{n.iface}</code></td>
                <td>
                  <button
                    class="link-btn"
                    onClick={() => {
                      probeUrl.value = `http://${n.ip}/`;
                    }}
                  >
                    probe →
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}

      <p class="muted">
        {data?.observed_count ?? 0} / {data?.arp_table_size_max ?? 0} ARP slots used
      </p>

      <details class="history">
        <summary>
          Scan history
          {scanHistoryLoading.value ? " (loading…)" : ""}
        </summary>
        <ScanHistoryList raw={scanHistory.value} />
        <button class="link-btn" onClick={() => void reloadScanHistory()}>
          reload
        </button>
      </details>
    </section>
  );
}

interface ScanHistoryEntry {
  id: number;
  started_at_ms: number;
  completed_at_ms: number;
  duration_ms: number;
  pings_sent: number;
  pings_responded: number;
  base_ip: string;
}

function ScanHistoryList({ raw }: { raw: string }) {
  const entries = parseNdjson<ScanHistoryEntry>(raw);
  if (entries.length === 0) return <p class="muted">No scans recorded yet.</p>;
  // Most recent first.
  entries.reverse();
  return (
    <ul class="scan-history">
      {entries.map((e) => (
        <li key={e.id}>
          <strong>#{e.id}</strong> · {e.base_ip.replace(/\.0$/, ".0/24")} ·{" "}
          {e.pings_responded}/{e.pings_sent} replied · {Math.round(e.duration_ms / 1000)}s
        </li>
      ))}
    </ul>
  );
}

function ProbeCard() {
  const r = probeResult.value;
  return (
    <section class="card">
      <header class="card-header">
        <h2>Probe a LAN device (HTTP)</h2>
      </header>

      <p class="muted">
        Fires an HTTP request from the clock to the target IP. RFC1918 / link-local
        / loopback IPv4 only — DNS names and public IPs are rejected by the
        firmware. GET / POST / PUT supported.
      </p>

      <div class="probe-form">
        <label>
          URL
          <input
            type="url"
            value={probeUrl.value}
            onInput={(e) => (probeUrl.value = (e.target as HTMLInputElement).value)}
            placeholder="http://192.168.1.1/"
          />
        </label>

        <label>
          Method
          <select
            value={probeMethod.value}
            onChange={(e) =>
              (probeMethod.value = (e.target as HTMLSelectElement)
                .value as ProbeRequest["method"])
            }
          >
            <option value="GET">GET</option>
            <option value="POST">POST</option>
            <option value="PUT">PUT</option>
          </select>
        </label>

        <label>
          Timeout (ms)
          <input
            type="number"
            min={500}
            max={10000}
            step={500}
            value={probeTimeoutMs.value}
            onInput={(e) =>
              (probeTimeoutMs.value = Number((e.target as HTMLInputElement).value))
            }
          />
        </label>

        {probeMethod.value !== "GET" && (
          <label class="full">
            Request body
            <textarea
              rows={4}
              value={probeBody.value}
              onInput={(e) =>
                (probeBody.value = (e.target as HTMLTextAreaElement).value)
              }
              placeholder='{"key": "value"}'
            />
          </label>
        )}

        <button onClick={() => void sendProbe()} disabled={probeBusy.value}>
          {probeBusy.value ? "Probing…" : "Send"}
        </button>
      </div>

      {probeErr.value && <p class="error">Probe error: {probeErr.value}</p>}

      {r && (
        <div class={`probe-result ${r.ok ? "ok" : "bad"}`}>
          <div class="probe-result-summary">
            <strong>HTTP {r.http_status}</strong> · {r.elapsed_ms}ms ·{" "}
            body {r.total_body_len} bytes
            {r.body_preview.length < r.total_body_len && " (preview truncated)"}
            {r.error && <> · <span class="error-inline">{r.error}</span></>}
          </div>
          {r.body_preview && (
            <pre class="probe-body-preview">{r.body_preview}</pre>
          )}
        </div>
      )}

      <details class="history">
        <summary>Probe history</summary>
        <ProbeAuditList raw={probeAudit.value} />
        <button class="link-btn" onClick={() => void reloadProbeAudit()}>
          reload
        </button>
      </details>
    </section>
  );
}

interface ProbeAuditEntry {
  at_ms: number;
  url: string;
  method: string;
  body_len: number;
  status: number;
  ok: boolean;
  elapsed_ms: number;
  error?: string;
}

function ProbeAuditList({ raw }: { raw: string }) {
  const entries = parseNdjson<ProbeAuditEntry>(raw);
  if (entries.length === 0) return <p class="muted">No probes recorded yet.</p>;
  entries.reverse();
  return (
    <ul class="probe-audit">
      {entries.map((e, i) => (
        <li key={i} class={e.ok ? "ok" : "bad"}>
          <code>{e.method}</code> <code>{e.url}</code> →{" "}
          <strong>{e.status}</strong> ({e.elapsed_ms}ms)
          {e.error && <> · <span class="error-inline">{e.error}</span></>}
        </li>
      ))}
    </ul>
  );
}
