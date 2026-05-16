#include "NetworkVisibility.h"

#include <AsyncPing.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
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
constexpr const char *PROBE_AUDIT_FILE = "/network_probes.txt";

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

// ─────────────────────────── Phase 3: probe API ───────────────────────────

bool isRfc1918Url(const String &url) {
  // Accept only http://<ipv4>[:port][/...] where <ipv4> is in:
  //   10.0.0.0/8
  //   172.16.0.0/12
  //   192.168.0.0/16
  //   169.254.0.0/16 (link-local)
  //   127.0.0.0/8 (loopback — useful for probing back through SDK paths)
  //
  // Hostnames (DNS names) are rejected on purpose: turning the clock into a
  // proxy for DNS-resolved targets is a much bigger SSRF surface.
  if (!url.startsWith("http://")) return false;

  int hostStart = 7;  // after "http://"
  int hostEnd = url.length();
  // Trim at first '/', '?', or '#'
  for (int i = hostStart; i < (int)url.length(); i++) {
    char c = url[i];
    if (c == '/' || c == '?' || c == '#') { hostEnd = i; break; }
  }
  String hostPort = url.substring(hostStart, hostEnd);
  // Strip :port if present
  int colon = hostPort.indexOf(':');
  String host = (colon >= 0) ? hostPort.substring(0, colon) : hostPort;
  if (host.length() == 0) return false;

  IPAddress addr;
  if (!addr.fromString(host)) {
    // Not a literal IP → reject (covers DNS names + IPv6 brackets).
    return false;
  }
  uint8_t a = addr[0], b = addr[1];
  // 10.0.0.0/8
  if (a == 10) return true;
  // 172.16.0.0/12
  if (a == 172 && (b >= 16 && b <= 31)) return true;
  // 192.168.0.0/16
  if (a == 192 && b == 168) return true;
  // 169.254.0.0/16
  if (a == 169 && b == 254) return true;
  // 127.0.0.0/8
  if (a == 127) return true;
  return false;
}

NetProbeResult probeHttp(const NetProbeRequest &req) {
  NetProbeResult result = {
    .ok = false,
    .httpStatus = -1,
    .error = "",
    .bodyPreview = "",
    .totalBodyLen = 0,
    .elapsedMs = 0,
  };
  uint32_t startedAt = millis();

  if (!isRfc1918Url(req.url)) {
    result.error = F("target must be an RFC1918 IPv4 URL");
    result.elapsedMs = millis() - startedAt;
    return result;
  }

  // Method allowlist. GET/POST/PUT only — the user's stated set; HEAD/DELETE
  // can be added later if needed but each one widens the SSRF blast radius.
  if (req.method != "GET" && req.method != "POST" && req.method != "PUT") {
    result.error = F("method must be GET, POST, or PUT");
    result.elapsedMs = millis() - startedAt;
    return result;
  }

  uint32_t timeoutMs = req.timeoutMs;
  if (timeoutMs == 0) timeoutMs = NET_PROBE_TIMEOUT_DEFAULT_MS;
  if (timeoutMs > NET_PROBE_TIMEOUT_MAX_MS) timeoutMs = NET_PROBE_TIMEOUT_MAX_MS;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(client, req.url)) {
    result.error = F("HTTPClient.begin failed");
    result.elapsedMs = millis() - startedAt;
    return result;
  }

  int code;
  if (req.method == "GET") {
    code = http.GET();
  } else if (req.method == "POST") {
    code = http.POST(req.body);
  } else {  // PUT
    code = http.PUT(req.body);
  }

  result.httpStatus = code;
  if (code < 0) {
    result.error = http.errorToString(code);
    http.end();
    result.elapsedMs = millis() - startedAt;
    return result;
  }
  result.ok = true;

  // Read body — cap at NET_PROBE_BODY_PREVIEW_MAX to stay heap-friendly.
  // HTTPClient's getString() reads the whole body into RAM, so we use
  // getStreamPtr() and read up to the cap. Then drain the rest to keep
  // the connection state sane (cheap if it's a small body).
  WiFiClient *stream = http.getStreamPtr();
  result.totalBodyLen = http.getSize();  // -1 if chunked or unknown
  while (stream && stream->connected() &&
         result.bodyPreview.length() < NET_PROBE_BODY_PREVIEW_MAX) {
    if (!stream->available()) {
      delay(5);
      continue;
    }
    int b = stream->read();
    if (b < 0) break;
    result.bodyPreview += (char)b;
  }
  // If totalBodyLen is unknown, approximate from what we read so the SPA
  // can show *something*. (Truncation is implicit when bodyPreview is
  // smaller than totalBodyLen.)
  if (result.totalBodyLen < 0) {
    result.totalBodyLen = (int)result.bodyPreview.length();
  }

  http.end();
  result.elapsedMs = millis() - startedAt;
  return result;
}

bool appendProbeAudit(const NetProbeRequest &req, const NetProbeResult &result) {
  JsonDocument doc;
  doc["at_ms"] = millis();
  doc["url"] = req.url;
  doc["method"] = req.method;
  doc["body_len"] = req.body.length();
  doc["status"] = result.httpStatus;
  doc["ok"] = result.ok;
  doc["elapsed_ms"] = result.elapsedMs;
  if (result.error.length() > 0) doc["error"] = result.error;
  String line;
  serializeJson(doc, line);
  return appendLineTrimmed(PROBE_AUDIT_FILE, line, NET_PROBE_AUDIT_MAX);
}

String readProbeAudit() { return readFile(PROBE_AUDIT_FILE); }

// ─────────────────────────── Auth (no-op for now) ─────────────────────────

bool authorizeNetworkRequest(const char * /*operation*/) {
  // V1 — trusted-LAN posture. Adding header-based auth later goes here:
  // read a shared secret from /conf.txt; compare against an
  // `X-Wagfam-Auth` header on the AsyncWebServerRequest. Empty config
  // value continues to mean "no auth required". No call site changes.
  return true;
}
