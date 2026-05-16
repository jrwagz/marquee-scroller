#include "NetworkVisibility.h"

#include <AsyncPing.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

namespace {

// ───────────────────────────── Phase 2 state ─────────────────────────────

NetScanProgress g_scan = {
  .state = NetScanState::Idle,
  .id = 0,
  .startedAtMs = 0,
  .completedAtMs = 0,
  .pingsSent = 0,
  .pingsResponded = 0,
  .currentHostByte = 1,
  .baseIp = IPAddress(),
  .localIp = IPAddress(),
};

AsyncPing g_pinger;
bool g_pingInFlight = false;
uint32_t g_scanIdCounter = 0;

constexpr uint16_t SCAN_PING_TIMEOUT_MS = 200;
constexpr uint16_t SCAN_PING_COUNT = 1;  // one ping per host; just enough to ARP-poke

// Files
constexpr const char *SCAN_HISTORY_FILE = "/network_scans.txt";

// Trim the given NDJSON file in place to at most `maxLines` lines (keep the
// most recent). Read full → split → keep tail → write back. The files we use
// this for are bounded (NET_SCAN_HISTORY_MAX = 20, NET_PROBE_AUDIT_MAX = 50)
// so worst-case the file is a few KB — fine to read into RAM.
bool trimHistoryFile(const char *path, int maxLines) {
  File f = LittleFS.open(path, "r");
  if (!f) return true;  // missing file is the trivial trimmed state
  String content = f.readString();
  f.close();

  // Count lines; if we're under the cap, no rewrite needed.
  int newlineCount = 0;
  for (size_t i = 0; i < content.length(); i++) {
    if (content[i] == '\n') newlineCount++;
  }
  if (newlineCount <= maxLines) return true;

  // Find the start of the (newlineCount - maxLines + 1)th-from-the-end line.
  int linesToDrop = newlineCount - maxLines;
  int idx = 0;
  for (int dropped = 0; dropped < linesToDrop && idx < (int)content.length(); idx++) {
    if (content[idx] == '\n') dropped++;
  }
  String trimmed = content.substring(idx);

  File out = LittleFS.open(path, "w");
  if (!out) return false;
  out.print(trimmed);
  out.close();
  return true;
}

// Read a file's full contents. Returns empty string on missing file.
String readFile(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) return String();
  String content = f.readString();
  f.close();
  return content;
}

// Append a single line of NDJSON to a file, trimming afterward.
bool appendLineTrimmed(const char *path, const String &line, int maxLines) {
  File f = LittleFS.open(path, "a");
  if (!f) return false;
  f.print(line);
  if (!line.endsWith("\n")) f.print('\n');
  f.close();
  return trimHistoryFile(path, maxLines);
}

}  // namespace

// ─────────────────────────── Phase 2: scan API ───────────────────────────

bool startNetworkScan() {
  if (g_scan.state == NetScanState::Running) return false;

  IPAddress local = WiFi.localIP();
  if (local == IPAddress(0u)) {
    // Not joined yet — nothing to scan.
    return false;
  }

  g_scan.state = NetScanState::Running;
  g_scan.id = ++g_scanIdCounter;
  g_scan.startedAtMs = millis();
  g_scan.completedAtMs = 0;
  g_scan.pingsSent = 0;
  g_scan.pingsResponded = 0;
  g_scan.currentHostByte = 1;
  g_scan.baseIp = IPAddress(local[0], local[1], local[2], 0);
  g_scan.localIp = local;
  g_pingInFlight = false;
  return true;
}

void tickNetworkScan() {
  if (g_scan.state != NetScanState::Running) return;
  if (g_pingInFlight) return;

  // End condition: walked past 254. Note `currentHostByte` was already
  // advanced past the last target by the previous tick's ping callback.
  if (g_scan.currentHostByte > 254) {
    g_scan.state = NetScanState::Done;
    g_scan.completedAtMs = millis();
    appendScanToHistory(g_scan);
    return;
  }

  // Skip our own IP — pinging ourselves would either fail or muddle ARP.
  if (g_scan.currentHostByte == g_scan.localIp[3]) {
    g_scan.currentHostByte++;
    return;
  }

  IPAddress target(g_scan.baseIp[0], g_scan.baseIp[1], g_scan.baseIp[2],
                   (uint8_t)g_scan.currentHostByte);
  g_pingInFlight = true;
  g_scan.pingsSent++;

  // AsyncPing fires the "true" callback on the final ping (success) or
  // when count is exhausted (timeouts). For SCAN_PING_COUNT=1, that's
  // either the single reply or the single timeout.
  g_pinger.on(true, [](const AsyncPingResponse &resp) -> bool {
    if (!resp.timeout && resp.total_recv > 0) {
      g_scan.pingsResponded++;
    }
    g_scan.currentHostByte++;
    g_pingInFlight = false;
    return true;  // stop the AsyncPing session
  });

  g_pinger.begin(target, SCAN_PING_COUNT, SCAN_PING_TIMEOUT_MS);
}

NetScanProgress getNetworkScanProgress() { return g_scan; }

bool appendScanToHistory(const NetScanProgress &p) {
  // Compact JSON — one line per scan.
  JsonDocument doc;
  doc["id"] = p.id;
  doc["started_at_ms"] = p.startedAtMs;
  doc["completed_at_ms"] = p.completedAtMs;
  doc["duration_ms"] = (p.completedAtMs > p.startedAtMs)
                        ? (p.completedAtMs - p.startedAtMs)
                        : 0;
  doc["pings_sent"] = p.pingsSent;
  doc["pings_responded"] = p.pingsResponded;
  doc["base_ip"] = p.baseIp.toString();
  String line;
  serializeJson(doc, line);
  return appendLineTrimmed(SCAN_HISTORY_FILE, line, NET_SCAN_HISTORY_MAX);
}

String readScanHistory() { return readFile(SCAN_HISTORY_FILE); }

// ─────────────────────────── Auth (no-op for now) ─────────────────────────

bool authorizeNetworkRequest(const char * /*operation*/) {
  // V1 — trusted-LAN posture. Adding header-based auth later goes here:
  // read a shared secret from /conf.txt; compare against an
  // `X-Wagfam-Auth` header on the AsyncWebServerRequest. Empty config
  // value continues to mean "no auth required". No call site changes.
  return true;
}
