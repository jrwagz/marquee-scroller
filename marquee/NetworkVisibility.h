// NetworkVisibility — LAN-visibility feature for the clock.
//
// Three layers, built across PR phases (one commit each in the originating
// PR so they're individually revertable / bisectable when something breaks
// on hardware):
//
//   Phase 2: active ARP scan. Sequentially ICMP-pings every host in the
//     local /24 (skipping the clock's own IP) so LWIP's ARP cache fills up
//     with whatever responds, then writes a summary entry to
//     /network_scans.txt. Phase 1 reads that cache; Phase 2 fills it.
//
//   Phase 3: HTTP probe. Makes an outbound HTTP request to a target on the
//     LAN. RFC1918 / link-local allowlist enforced firmware-side so the
//     endpoint can't be turned into a generic SSRF gadget. Probes are
//     logged to /network_probes.txt for after-the-fact audit.
//
//   Phase 4: SPA UI consumes both — those endpoints stay backend-only here.
//
// Auth: per the trusted-LAN threat model (see PR #70), the device's API
// is intentionally open to anyone on the LAN. Phases 2-3 both call
// `authorizeNetworkRequest(...)` at the top of every handler — currently
// a no-op returning true. The plumbing exists so that adding header-based
// auth later is a single-function diff (read a shared secret from
// /conf.txt, compare against an X-Wagfam-Auth header). No call-site,
// route, or API-contract change needed when that flip happens.

#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// ───────────────────────────── Phase 2: scan ──────────────────────────────

enum class NetScanState : uint8_t {
  Idle,     // no scan ever run since boot (or last one finished + cleared)
  Running,  // pings in flight
  Done,     // last ping completed; state stays in this terminal value until
            // a fresh startNetworkScan() is called
};

struct NetScanProgress {
  NetScanState state;
  uint32_t id;             // monotonic per-boot scan ID
  uint32_t startedAtMs;
  uint32_t completedAtMs;  // 0 while running
  uint16_t pingsSent;
  uint16_t pingsResponded;
  uint16_t currentHostByte; // 1..254 inclusive while running, > 254 when done
  IPAddress baseIp;        // first three octets of the /24, fourth octet = 0
  IPAddress localIp;       // clock's own IP, skipped during scan
};

// Kick off a new scan. Returns false if a scan is already in flight (no
// concurrent scans — keeps state simple and the network calm).
bool startNetworkScan();

// Drives the scan state machine. Called from the main loop, every iteration.
// No-op when state != Running. Cheap.
void tickNetworkScan();

// Current scan state (whatever the most recent scan looks like, even after
// it's completed). Returned by-value because the struct is small.
NetScanProgress getNetworkScanProgress();

// Persistence — Phase 2 scan history at /network_scans.txt. NDJSON,
// one scan summary per line, trimmed to last NET_SCAN_HISTORY_MAX entries.
constexpr int NET_SCAN_HISTORY_MAX = 20;

// Append the just-completed scan summary. Called automatically by
// tickNetworkScan() on the transition Running → Done; exposed in the header
// for tests / direct access.
bool appendScanToHistory(const NetScanProgress &p);

// Read the history file as a JSON string (the SPA wants this as-is). Returns
// the literal NDJSON content — caller wraps it in a JSON envelope if needed.
String readScanHistory();

// ──────────────────────────── Phase 3: probe ──────────────────────────────

struct NetProbeRequest {
  String url;
  String method;     // "GET" | "POST" | "PUT" (others rejected at handler)
  String body;       // optional, used only for POST/PUT
  uint32_t timeoutMs;
};

struct NetProbeResult {
  bool ok;
  int httpStatus;        // -1 if no response (timeout, refused, etc.)
  String error;          // empty when ok=true
  String bodyPreview;    // capped at NET_PROBE_BODY_PREVIEW_MAX bytes
  int totalBodyLen;
  uint32_t elapsedMs;
};

constexpr int NET_PROBE_BODY_PREVIEW_MAX = 512;
constexpr uint32_t NET_PROBE_TIMEOUT_DEFAULT_MS = 3000;
constexpr uint32_t NET_PROBE_TIMEOUT_MAX_MS = 10000;
constexpr int NET_PROBE_AUDIT_MAX = 50;

// True iff `url`'s host parses as an RFC1918 / link-local IPv4 address.
// Probe handler refuses anything else (no public-internet bounces).
// Hostnames (DNS) are rejected — too easy to point at something nasty;
// IPv4 literal only for now.
bool isRfc1918Url(const String &url);

// Synchronous HTTP probe. Blocks the caller for up to req.timeoutMs.
// Called from the request handler — fine on AsyncWebServer since the
// completion handler runs on the main task; we cap concurrency at 1 in
// the handler so a slow probe can't pile up.
NetProbeResult probeHttp(const NetProbeRequest &req);

// Persist a probe call to /network_probes.txt. NDJSON, trimmed to last
// NET_PROBE_AUDIT_MAX entries. Audit log: every call, success or fail.
bool appendProbeAudit(const NetProbeRequest &req, const NetProbeResult &result);

// Read the audit log as raw NDJSON.
String readProbeAudit();

// ───────────────────────── Auth (no-op for now) ────────────────────────────

// V1: returns true unconditionally — matches the rest of the device API
// under the trusted-LAN threat model. Flipping on real auth later is a
// matter of editing this function to compare a header against a config
// secret; no call site needs to change.
//
// `operation` is a short tag ("scan", "probe", etc.) for future use by
// the eventual auth check (e.g. per-operation policies) and for logging.
bool authorizeNetworkRequest(const char *operation);
