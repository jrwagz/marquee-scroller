#include "TasmotaDiscovery.h"

#include <AsyncPing.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <TimeLib.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>

namespace TasmotaDiscovery {

namespace {

DiscoveryProgress g_progress = {
  .state = DiscoveryState::Idle,
  .id = 0,
  .startedAtMs = 0,
  .completedAtMs = 0,
  .pingsSent = 0,
  .pingsResponded = 0,
  .httpProbed = 0,
  .tasmotaFound = 0,
  .currentHostByte = 1,
  .baseIp = IPAddress(),
};

DiscoveredDevice g_results[MAX_RESULTS];
int g_resultCount = 0;
uint32_t g_idCounter = 0;

// Belt-and-suspenders accounting: capture (IP, MAC) pairs *as the ping
// sweep produces them*, not by walking lwIP's ARP cache at the end. The
// ESP8266's ETHARP_TABLE_SIZE defaults to 10, so by the time a /24 sweep
// finishes the cache has evicted ~80% of the entries. Snapshotting on
// every ping reply is the only way to record them all without rebuilding
// lwIP with a bigger table. Sized for typical home LANs (64 hosts);
// extras past this cap are silently dropped.
constexpr int MAX_ARP_ENTRIES = 64;
constexpr int MAC_STR_MAX = 18;  // "xx:xx:xx:xx:xx:xx" + null

struct ArpEntry {
  char ip[IP_STR_MAX];
  char mac[MAC_STR_MAX];
};
ArpEntry g_arp[MAX_ARP_ENTRIES];
int g_arpCount = 0;

AsyncPing g_pinger;
bool g_pingInFlight = false;
// When a ping completes successfully, we stash the IP here so the next
// tick fires an HTTP probe. Keeps the AsyncPing callback short.
IPAddress g_pendingProbeIp = IPAddress(0u);
bool g_hasPendingProbe = false;

// Originally 200ms — empirically that was too aggressive over WiFi; pings
// to known-alive hosts (gateway, Tasmota at .241, Mac at .198) all reported
// as timeouts. Bumping to 500ms catches typical LAN reply latency
// (~5-50ms) plus the ESP8266 WiFi-stack queue delay.
constexpr uint16_t PING_TIMEOUT_MS = 500;
constexpr uint16_t HTTP_PROBE_TIMEOUT_MS = 1000;

// ── ARP capture ───────────────────────────────────────────────────────────
//
// Called from the ping callback when a host replies. Prefers the MAC the
// AsyncPing library already captured from its own etharp_find_addr at
// ICMP-reply time (resp.mac in the AsyncPingResponse) — that lookup
// happens ~1 ms earlier than our user callback fires, so the ARP cache
// is one fewer ping-cycle stale. Falls back to a fresh etharp_find_addr
// if the library failed to capture (resp.mac was NULL).
//
// Walking the global ARP table at end-of-scan doesn't work because
// ETHARP_TABLE_SIZE defaults to 10 — earlier replies get evicted long
// before tick() reaches Done. Per-reply capture sidesteps that, and
// reading resp.mac sidesteps the 1ms eviction window our own lookup
// would otherwise sit on top of.

void captureArpForIp(const IPAddress &ip, const struct eth_addr *libMac) {
  if (g_arpCount >= MAX_ARP_ENTRIES) return;

  const struct eth_addr *mac = libMac;

  // Fallback: AsyncPing's lookup failed at packet-arrival time. Try once
  // more from our callback context — better than dropping the entry.
  struct eth_addr *fallback = nullptr;
  if (!mac) {
    ip4_addr_t target;
    IP4_ADDR(&target, ip[0], ip[1], ip[2], ip[3]);
    const ip4_addr_t *resolved = nullptr;
    if (etharp_find_addr(netif_default, &target, &fallback, &resolved) < 0) return;
    mac = fallback;
  }
  if (!mac) return;

  ArpEntry &e = g_arp[g_arpCount];
  snprintf(e.ip, sizeof(e.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  snprintf(e.mac, sizeof(e.mac), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac->addr[0], mac->addr[1], mac->addr[2],
           mac->addr[3], mac->addr[4], mac->addr[5]);
  g_arpCount++;
}

void writeArpFile(uint32_t scan_id) {
  File f = LittleFS.open(ARP_FILE, "w");
  if (!f) {
    Serial.println(F("[discover] FAILED to open ARP file for write"));
    return;
  }
  JsonDocument doc;
  doc["scan_id"] = scan_id;
  doc["completed_at_unix"] = (uint32_t)now();
  JsonArray arr = doc["entries"].to<JsonArray>();
  for (int i = 0; i < g_arpCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ip"] = g_arp[i].ip;
    o["mac"] = g_arp[i].mac;
  }
  serializeJson(doc, f);
  f.close();
  Serial.printf_P(PSTR("[discover] wrote %d ARP entr(y/ies) to %s\n"),
                  g_arpCount, ARP_FILE);
}

// ── HTTP probe ────────────────────────────────────────────────────────────
//
// Issues GET /cm?cmnd=Status%200 (the "everything" status). On a Tasmota
// device this returns a big JSON with both Status (DeviceName /
// FriendlyName) and StatusNET (Hostname). On a non-Tasmota device it
// returns whatever — most likely 404 or HTML, both of which fail the
// JSON parse and the "Status.DeviceName present" check.

bool probeTasmota(const char *ip, DiscoveredDevice *out) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_PROBE_TIMEOUT_MS);
  String url = String("http://") + ip + "/cm?cmnd=Status%200";
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  // Tasmota Status 0 response wraps everything in top-level keys.
  // Required: Status.DeviceName (or FriendlyName). Helpful: StatusNET.Hostname.
  JsonVariant status = doc["Status"];
  if (status.isNull()) return false;
  String name;
  if (status["DeviceName"].is<const char *>()) {
    name = status["DeviceName"].as<String>();
  }
  if (name.length() == 0 && status["FriendlyName"].is<JsonArray>()) {
    JsonArray fn = status["FriendlyName"].as<JsonArray>();
    if (fn.size() > 0) name = fn[0].as<String>();
  }
  if (name.length() == 0) return false;

  strlcpy(out->ip, ip, IP_STR_MAX);
  strlcpy(out->name, name.c_str(), NAME_STR_MAX);

  String hostname;
  JsonVariant net = doc["StatusNET"];
  if (!net.isNull() && net["Hostname"].is<const char *>()) {
    hostname = net["Hostname"].as<String>();
  }
  strlcpy(out->hostname, hostname.c_str(), HOST_STR_MAX);
  return true;
}

bool addResultIfNew(const DiscoveredDevice &d) {
  for (int i = 0; i < g_resultCount; i++) {
    if (strcmp(g_results[i].ip, d.ip) == 0) {
      // Already have this IP — update name/hostname in case mDNS got us
      // a partial entry that scan now fills in.
      if (d.name[0]) strlcpy(g_results[i].name, d.name, NAME_STR_MAX);
      if (d.hostname[0]) strlcpy(g_results[i].hostname, d.hostname, HOST_STR_MAX);
      return false;
    }
  }
  if (g_resultCount >= MAX_RESULTS) return false;
  g_results[g_resultCount++] = d;
  return true;
}

// ── mDNS path ─────────────────────────────────────────────────────────────

// Synchronous mDNS query — blocks ~3s. Called once at the start of a
// discovery run. Each hit is added immediately to results; the friendly
// name + hostname come from the mDNS record so no HTTP probe needed for
// these (we could probe to enrich, but ESP8266mDNS doesn't always give
// us enough info to know if it's worth it; keep it simple).
void runMdnsQuery() {
  int n = MDNS.queryService("tasmota", "tcp");
  for (int i = 0; i < n && g_resultCount < MAX_RESULTS; i++) {
    IPAddress ip = MDNS.IP(i);
    String host = MDNS.hostname(i);
    DiscoveredDevice d = {};
    strlcpy(d.ip, ip.toString().c_str(), IP_STR_MAX);
    strlcpy(d.hostname, host.c_str(), HOST_STR_MAX);
    d.source = "mdns";
    // FriendlyName isn't in the mDNS record; HTTP-probe to fill it in.
    // This adds ~300ms per mDNS hit but enriches the entry meaningfully.
    DiscoveredDevice enriched = d;
    if (probeTasmota(d.ip, &enriched)) {
      enriched.source = "mdns";
      addResultIfNew(enriched);
      g_progress.httpProbed++;
      g_progress.tasmotaFound++;
    } else {
      // mDNS said it's there but HTTP probe failed — still add it so the
      // user sees something. They can edit the name manually.
      addResultIfNew(d);
      g_progress.tasmotaFound++;
    }
  }
}

}  // namespace

bool start() {
  if (g_progress.state != DiscoveryState::Idle &&
      g_progress.state != DiscoveryState::Done) {
    return false;
  }
  IPAddress local = WiFi.localIP();
  if (local == IPAddress(0u)) return false;

  g_resultCount = 0;
  g_arpCount = 0;
  g_progress.state = DiscoveryState::MdnsQuery;
  g_progress.id = ++g_idCounter;
  g_progress.startedAtMs = millis();
  g_progress.completedAtMs = 0;
  g_progress.pingsSent = 0;
  g_progress.pingsResponded = 0;
  g_progress.httpProbed = 0;
  g_progress.tasmotaFound = 0;
  g_progress.currentHostByte = 1;
  g_progress.baseIp = IPAddress(local[0], local[1], local[2], 0);
  g_pingInFlight = false;
  g_hasPendingProbe = false;
  return true;
}

void tick() {
  // mDNS phase is synchronous — collapse it into one tick. (Blocks ~3s
  // once per run; not worth state-machine-ifying further.)
  if (g_progress.state == DiscoveryState::MdnsQuery) {
    runMdnsQuery();
    g_progress.state = DiscoveryState::PingSweep;
    return;
  }

  if (g_progress.state != DiscoveryState::PingSweep) return;

  // Deferred HTTP probe from the previous ping reply.
  if (g_hasPendingProbe) {
    g_hasPendingProbe = false;
    char ipStr[IP_STR_MAX];
    g_pendingProbeIp.toString().toCharArray(ipStr, IP_STR_MAX);
    DiscoveredDevice d = {};
    g_progress.httpProbed++;
    if (probeTasmota(ipStr, &d)) {
      d.source = "scan";
      if (addResultIfNew(d)) g_progress.tasmotaFound++;
    }
    return;
  }

  if (g_pingInFlight) return;

  if (g_progress.currentHostByte > 254) {
    g_progress.state = DiscoveryState::Done;
    g_progress.completedAtMs = millis();
    Serial.printf_P(PSTR("[discover] done — %d Tasmota(s) on %d responders\n"),
                    (int)g_progress.tasmotaFound, (int)g_progress.pingsResponded);

    // Persist results to LittleFS. Source of truth for the planned
    // upload-to-server feature: the server periodically fetches this
    // file, snapshots it per-clock, and on a replacement clock pushes
    // it back down via a future config-block field so the user's
    // inventory survives a flash wipe / hardware swap.
    File f = LittleFS.open(DISCOVERED_FILE, "w");
    if (f) {
      JsonDocument doc;
      doc["scan_id"] = g_progress.id;
      doc["completed_at_unix"] = (uint32_t)now();
      doc["completed_at_ms"] = g_progress.completedAtMs;
      doc["pings_sent"] = g_progress.pingsSent;
      doc["pings_responded"] = g_progress.pingsResponded;
      JsonArray arr = doc["results"].to<JsonArray>();
      for (int i = 0; i < g_resultCount; i++) {
        const auto &d = g_results[i];
        JsonObject o = arr.add<JsonObject>();
        o["ip"] = d.ip;
        o["name"] = d.name;
        o["hostname"] = d.hostname;
        o["source"] = d.source ? d.source : "scan";
      }
      serializeJson(doc, f);
      f.close();
      Serial.printf_P(PSTR("[discover] wrote %d device(s) to %s\n"),
                      g_resultCount, DISCOVERED_FILE);
    } else {
      Serial.println(F("[discover] FAILED to open discovered file for write"));
    }

    writeArpFile(g_progress.id);
    return;
  }

  IPAddress local = WiFi.localIP();
  if (g_progress.currentHostByte == local[3]) {
    g_progress.currentHostByte++;
    return;
  }

  IPAddress target(g_progress.baseIp[0], g_progress.baseIp[1],
                   g_progress.baseIp[2], (uint8_t)g_progress.currentHostByte);
  g_pingInFlight = true;
  g_progress.pingsSent++;

  g_pinger.on(true, [target](const AsyncPingResponse &resp) -> bool {
    // resp.answer is the "got a reply this round" boolean. resp.timeout is
    // the configured timeout in ms (poorly named in the library — it is
    // *not* a "did we time out" flag), so checking it was a no-op bug that
    // made every ping look like a failure.
    if (resp.answer) {
      g_progress.pingsResponded++;
      g_pendingProbeIp = target;
      g_hasPendingProbe = true;
      captureArpForIp(target, resp.mac);
    }
    g_progress.currentHostByte++;
    g_pingInFlight = false;
    return true;
  });

  g_pinger.begin(target, 1, PING_TIMEOUT_MS);
}

DiscoveryProgress getProgress() { return g_progress; }
int resultCount() { return g_resultCount; }
const DiscoveredDevice &getResult(int i) { return g_results[i]; }

bool probeOne(const char *ip, DiscoveredDevice *out) {
  DiscoveredDevice d = {};
  if (!probeTasmota(ip, &d)) return false;
  d.source = "manual";
  *out = d;
  return true;
}

String readPersistedResults() {
  File f = LittleFS.open(DISCOVERED_FILE, "r");
  if (!f) return String();
  String s = f.readString();
  f.close();
  return s;
}

String readPersistedArpTable() {
  File f = LittleFS.open(ARP_FILE, "r");
  if (!f) return String();
  String s = f.readString();
  f.close();
  return s;
}

}  // namespace TasmotaDiscovery
