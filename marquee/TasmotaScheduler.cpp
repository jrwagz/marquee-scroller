#include "TasmotaScheduler.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TimeLib.h>

namespace TasmotaScheduler {

namespace {

// ── in-memory state ───────────────────────────────────────────────────────

TasmotaDevice g_devices[MAX_DEVICES];
int g_deviceCount = 0;

Schedule g_schedules[MAX_SCHEDULES];
int g_scheduleCount = 0;
uint32_t g_nextScheduleId = 1;

constexpr const char *DEVICES_FILE = "/tasmota_devices.json";
constexpr const char *SCHEDULES_FILE = "/tasmota_schedules.json";

// ── cron-field parsing ────────────────────────────────────────────────────
//
// Each field accepts: '*'  |  N  |  N-M  |  N,M,K (lists)  |  N-M/S (steps)
//                     |  */S (step from minVal to maxVal)
// Range checking is done against [minVal, maxVal]; the resulting bitmask
// has bit N set iff value N is allowed. uint64_t comfortably holds minute
// (0-59 = 60 bits), so all five fields share the same return type.

bool parseCronField(const char *spec, uint64_t *out, int minVal, int maxVal) {
  *out = 0;
  if (!spec || !*spec) return false;
  String s(spec);
  s.trim();

  // Split on commas — each piece is a "term".
  int from = 0;
  while (from <= (int)s.length()) {
    int comma = s.indexOf(',', from);
    String term = (comma < 0) ? s.substring(from) : s.substring(from, comma);
    term.trim();
    if (term.length() == 0) return false;

    int rangeStart = minVal;
    int rangeEnd = maxVal;
    int step = 1;

    // Split off "/step" suffix if present.
    int slash = term.indexOf('/');
    String stepStr;
    if (slash >= 0) {
      stepStr = term.substring(slash + 1);
      term = term.substring(0, slash);
      stepStr.trim();
      term.trim();
      step = stepStr.toInt();
      if (step <= 0) return false;
    }

    if (term == "*") {
      rangeStart = minVal;
      rangeEnd = maxVal;
    } else {
      int dash = term.indexOf('-');
      if (dash >= 0) {
        String a = term.substring(0, dash);
        String b = term.substring(dash + 1);
        a.trim(); b.trim();
        if (a.length() == 0 || b.length() == 0) return false;
        rangeStart = a.toInt();
        rangeEnd = b.toInt();
      } else {
        // Bare number.
        // String::toInt returns 0 on parse failure — distinguish "0" from
        // "garbage" by checking digit-ness.
        for (size_t i = 0; i < term.length(); i++) {
          if (!isDigit(term[i])) return false;
        }
        int n = term.toInt();
        rangeStart = n;
        rangeEnd = n;
      }
    }

    if (rangeStart < minVal || rangeEnd > maxVal || rangeStart > rangeEnd) {
      return false;
    }
    for (int v = rangeStart; v <= rangeEnd; v += step) {
      *out |= (uint64_t)1 << v;
    }

    if (comma < 0) break;
    from = comma + 1;
  }

  return true;
}

}  // namespace

bool parseCron(const char *cronStr, CronMask *out) {
  out->valid = false;
  if (!cronStr) return false;

  // Tokenize on whitespace into exactly 5 fields.
  String s(cronStr);
  s.trim();
  String fields[5];
  int fi = 0;
  int from = 0;
  while (from < (int)s.length() && fi < 5) {
    while (from < (int)s.length() && isSpace(s[from])) from++;
    int end = from;
    while (end < (int)s.length() && !isSpace(s[end])) end++;
    if (end > from) {
      fields[fi++] = s.substring(from, end);
    }
    from = end;
  }
  if (fi != 5) return false;

  // Skip any leftover trailing tokens? No — we want exactly 5.
  while (from < (int)s.length() && isSpace(s[from])) from++;
  if (from < (int)s.length()) return false;

  uint64_t m = 0, h = 0, dom = 0, mon = 0, dow = 0;
  if (!parseCronField(fields[0].c_str(), &m, 0, 59)) return false;
  if (!parseCronField(fields[1].c_str(), &h, 0, 23)) return false;
  if (!parseCronField(fields[2].c_str(), &dom, 1, 31)) return false;
  if (!parseCronField(fields[3].c_str(), &mon, 1, 12)) return false;
  // DoW: accept 0-7; fold "7" → "0" (Sunday) per POSIX cron convention.
  if (!parseCronField(fields[4].c_str(), &dow, 0, 7)) return false;
  if (dow & (1ULL << 7)) { dow |= 1ULL; dow &= ~(1ULL << 7); }

  out->minute = m;
  out->hour = (uint32_t)h;
  out->dom = (uint32_t)dom;
  out->month = (uint16_t)mon;
  out->dow = (uint8_t)(dow & 0x7F);
  out->valid = true;
  return true;
}

bool cronMatches(const CronMask &m, int minute, int hour, int dom, int mon,
                 int dowSunZero) {
  if (!m.valid) return false;
  if (!((m.minute >> minute) & 1ULL)) return false;
  if (!((m.hour >> hour) & 1U)) return false;
  if (!((m.dom >> dom) & 1U)) return false;
  if (!((m.month >> mon) & 1U)) return false;
  if (!((m.dow >> dowSunZero) & 1U)) return false;
  return true;
}

bool runCronSelfTest() {
  struct Case {
    const char *cron;
    bool shouldParse;
    int min, hour, dom, mon, dow;
    bool shouldMatch;
  };
  // Each row: parse the cron, then check against (min, hour, dom, mon, dow).
  const Case cases[] = {
    {"* * * * *",       true,  0, 0, 1, 1, 0, true},
    {"0 22 * * *",      true,  0, 22, 5, 6, 3, true},   // 10pm any day
    {"0 22 * * *",      true,  1, 22, 5, 6, 3, false},  // 10:01pm — no
    {"*/15 * * * *",    true,  15, 9, 1, 1, 0, true},   // every 15 min
    {"*/15 * * * *",    true,  16, 9, 1, 1, 0, false},
    {"0 6 * * 1-5",     true,  0, 6, 15, 5, 1, true},   // weekday 6am
    {"0 6 * * 1-5",     true,  0, 6, 15, 5, 0, false},  // Sunday — no
    {"0 6 * * 7",       true,  0, 6, 15, 5, 0, true},   // 7 == Sunday
    {"30 8,12,17 * * *", true, 30, 12, 1, 1, 0, true},
    {"30 8,12,17 * * *", true, 30, 13, 1, 1, 0, false},
    // Syntax errors:
    {"* * * *",         false, 0,0,0,0,0, false},        // 4 fields
    {"60 * * * *",      false, 0,0,0,0,0, false},        // minute out of range
    {"* * * 13 *",      false, 0,0,0,0,0, false},        // month out of range
    {"* * 0 * *",       false, 0,0,0,0,0, false},        // dom must be >= 1
  };
  bool allOk = true;
  for (const auto &c : cases) {
    CronMask m;
    bool parsed = parseCron(c.cron, &m);
    if (parsed != c.shouldParse) {
      Serial.printf_P(PSTR("[cron-selftest] parse(%s) expected %d got %d\n"),
                      c.cron, (int)c.shouldParse, (int)parsed);
      allOk = false;
      continue;
    }
    if (!parsed) continue;  // syntax-error cases don't have a match expectation
    bool matched = cronMatches(m, c.min, c.hour, c.dom, c.mon, c.dow);
    if (matched != c.shouldMatch) {
      Serial.printf_P(PSTR("[cron-selftest] match(%s, m=%d h=%d dom=%d mon=%d dow=%d) "
                            "expected %d got %d\n"),
                      c.cron, c.min, c.hour, c.dom, c.mon, c.dow,
                      (int)c.shouldMatch, (int)matched);
      allOk = false;
    }
  }
  return allOk;
}

// ── action ↔ string ───────────────────────────────────────────────────────

static const char *actionToCmd(TasmotaAction a) {
  switch (a) {
    case TasmotaAction::On:     return "ON";
    case TasmotaAction::Off:    return "OFF";
    case TasmotaAction::Toggle: return "TOGGLE";
  }
  return "OFF";
}

static TasmotaAction actionFromStr(const char *s) {
  if (!s) return TasmotaAction::Off;
  if (!strcasecmp(s, "ON")) return TasmotaAction::On;
  if (!strcasecmp(s, "TOGGLE")) return TasmotaAction::Toggle;
  return TasmotaAction::Off;
}

// ── HTTP — talks to Tasmota /cm endpoint ──────────────────────────────────

static String tasmotaCmd(const char *ip, const String &cmnd) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(3000);
  String url = String("http://") + ip + "/cm?cmnd=" + cmnd;
  if (!http.begin(client, url)) return String();
  int code = http.GET();
  String body;
  if (code == 200) {
    body = http.getString();
  }
  http.end();
  if (code != 200) {
    Serial.printf_P(PSTR("[tasmota] %s → HTTP %d\n"), ip, code);
    return String();
  }
  return body;
}

String setTasmotaPower(const char *ip, const char *action) {
  String body = tasmotaCmd(ip, String("Power%20") + action);
  if (body.length() == 0) return String();
  JsonDocument doc;
  if (deserializeJson(doc, body)) return String();
  return doc["POWER"].as<String>();
}

String readTasmotaPower(const char *ip) {
  String body = tasmotaCmd(ip, "Power");
  if (body.length() == 0) return String();
  JsonDocument doc;
  if (deserializeJson(doc, body)) return String();
  return doc["POWER"].as<String>();
}

// ── deferred probe ────────────────────────────────────────────────────────

namespace {
ProbeCache g_probe = { String(), String(), false, false, 0 };
String g_pendingIp;
bool g_pending = false;
}  // namespace

bool queueProbe(const char *ip) {
  if (g_pending) {
    // Already a different request in flight — refuse to clobber.
    if (g_pendingIp != ip) return false;
    return true;  // same IP, idempotent
  }
  g_pendingIp = ip;
  g_pending = true;
  g_probe.pending = true;
  return true;
}

void drainPendingProbe() {
  if (!g_pending) return;
  // Snapshot the IP and clear the pending flag BEFORE the (~1s) HTTP call.
  // If the call crashes / watchdogs anyway we don't want to come back and
  // immediately retry the same probe on next boot.
  String ip = g_pendingIp;
  g_pending = false;

  String state = readTasmotaPower(ip.c_str());
  g_probe.ip = ip;
  g_probe.power = state;
  g_probe.reachable = state.length() > 0;
  g_probe.pending = false;
  g_probe.lastUpdatedMs = millis();
}

const ProbeCache &getProbeCache() { return g_probe; }

namespace {
String g_pendingSetPowerIp;
String g_pendingSetPowerAction;
}  // namespace

bool queueSetPower(const char *ip, const char *action) {
  if (!ip || !*ip || !action || !*action) return false;
  if (strcasecmp(action, "ON") && strcasecmp(action, "OFF") &&
      strcasecmp(action, "TOGGLE")) {
    return false;
  }
  if (g_pendingSetPowerIp.length() > 0) return false;
  g_pendingSetPowerIp = ip;
  g_pendingSetPowerAction = action;
  // Show "pending" on the same IP so the SPA's existing /api/tasmota/power
  // polling reflects the in-flight action.
  g_probe.ip = g_pendingSetPowerIp;
  g_probe.pending = true;
  return true;
}

void drainPendingSetPower() {
  if (g_pendingSetPowerIp.length() == 0) return;
  String ip = g_pendingSetPowerIp;
  String action = g_pendingSetPowerAction;
  g_pendingSetPowerIp = String();
  g_pendingSetPowerAction = String();

  String state = setTasmotaPower(ip.c_str(), action.c_str());
  g_probe.ip = ip;
  g_probe.power = state;
  g_probe.reachable = state.length() > 0;
  g_probe.pending = false;
  g_probe.lastUpdatedMs = millis();
  Serial.printf_P(PSTR("[tasmota] handler-deferred Power %s → %s = %s\n"),
                  action.c_str(), ip.c_str(),
                  state.length() ? state.c_str() : "(unreachable)");
}

namespace {
uint32_t g_pendingActionScheduleId = 0;  // 0 = none queued
}  // namespace

bool queueRunSchedule(uint32_t id) {
  if (g_pendingActionScheduleId != 0) return false;  // one in flight at a time
  // Walk schedules inline rather than use the anonymous-namespace helper,
  // which isn't reachable from this translation-unit-public function.
  bool found = false;
  for (int i = 0; i < g_scheduleCount; i++) {
    if (g_schedules[i].id == id) { found = true; break; }
  }
  if (!found) return false;
  g_pendingActionScheduleId = id;
  return true;
}

void drainPendingAction() {
  if (g_pendingActionScheduleId == 0) return;
  uint32_t id = g_pendingActionScheduleId;
  g_pendingActionScheduleId = 0;  // clear before HTTP so a crash doesn't loop
  Serial.printf_P(PSTR("[tasmota] handler-deferred run for schedule #%u\n"),
                  (unsigned)id);
  runScheduleNow(id);  // result feeds into the schedule's own state +
                       // serial log; the SPA can poll Power separately to
                       // confirm the action took effect
}

// ── persistence ───────────────────────────────────────────────────────────

void loadFromDisk() {
  // Devices
  g_deviceCount = 0;
  {
    File f = LittleFS.open(DEVICES_FILE, "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f) && doc.is<JsonArray>()) {
        for (JsonObject obj : doc.as<JsonArray>()) {
          if (g_deviceCount >= MAX_DEVICES) break;
          strlcpy(g_devices[g_deviceCount].ip, obj["ip"] | "", IP_STR_MAX);
          strlcpy(g_devices[g_deviceCount].name, obj["name"] | "", NAME_STR_MAX);
          if (g_devices[g_deviceCount].ip[0]) g_deviceCount++;
        }
      }
      f.close();
    }
  }

  // Schedules
  g_scheduleCount = 0;
  g_nextScheduleId = 1;
  {
    File f = LittleFS.open(SCHEDULES_FILE, "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f) && doc.is<JsonArray>()) {
        for (JsonObject obj : doc.as<JsonArray>()) {
          if (g_scheduleCount >= MAX_SCHEDULES) break;
          Schedule &s = g_schedules[g_scheduleCount];
          s.id = obj["id"] | 0;
          strlcpy(s.ip, obj["ip"] | "", IP_STR_MAX);
          strlcpy(s.cronStr, obj["cron"] | "", CRON_STR_MAX);
          strlcpy(s.name, obj["name"] | "", NAME_STR_MAX);
          s.action = actionFromStr(obj["action"] | "OFF");
          s.enabled = obj["enabled"] | true;
          s.lastFiredEpoch = 0;
          parseCron(s.cronStr, &s.mask);  // mark invalid if it fails
          if (s.id == 0 || !s.ip[0]) continue;
          if (s.id >= g_nextScheduleId) g_nextScheduleId = s.id + 1;
          g_scheduleCount++;
        }
      }
      f.close();
    }
  }
}

bool saveToDisk() {
  // Devices
  {
    File f = LittleFS.open(DEVICES_FILE, "w");
    if (!f) return false;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < g_deviceCount; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["ip"] = g_devices[i].ip;
      o["name"] = g_devices[i].name;
    }
    serializeJson(doc, f);
    f.close();
  }
  // Schedules
  {
    File f = LittleFS.open(SCHEDULES_FILE, "w");
    if (!f) return false;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < g_scheduleCount; i++) {
      const Schedule &s = g_schedules[i];
      JsonObject o = arr.add<JsonObject>();
      o["id"] = s.id;
      o["ip"] = s.ip;
      o["cron"] = s.cronStr;
      o["action"] = actionToCmd(s.action);
      o["enabled"] = s.enabled;
      o["name"] = s.name;
    }
    serializeJson(doc, f);
    f.close();
  }
  return true;
}

// ── tick ──────────────────────────────────────────────────────────────────

void tickMinute(time_t nowLocal) {
  int m = ::minute(nowLocal);
  int h = ::hour(nowLocal);
  int dom = ::day(nowLocal);
  int mon = ::month(nowLocal);
  int dow = ::weekday(nowLocal) - 1;  // Time lib: Sun=1..Sat=7; cron wants 0..6

  // De-dup key: epoch minute. If a schedule already fired this minute,
  // skip — even if the loop ticks twice (clock correction, etc.).
  uint32_t epochMinute = (uint32_t)(nowLocal / 60);

  for (int i = 0; i < g_scheduleCount; i++) {
    Schedule &s = g_schedules[i];
    if (!s.enabled || !s.mask.valid) continue;
    if (!cronMatches(s.mask, m, h, dom, mon, dow)) continue;
    if (s.lastFiredEpoch == epochMinute) continue;

    s.lastFiredEpoch = epochMinute;
    Serial.printf_P(PSTR("[tasmota] firing schedule #%u: %s %s → %s\n"),
                    (unsigned)s.id, s.ip, s.cronStr, actionToCmd(s.action));
    String result = setTasmotaPower(s.ip, actionToCmd(s.action));
    if (result.length() == 0) {
      Serial.printf_P(PSTR("[tasmota] schedule #%u FAILED\n"), (unsigned)s.id);
    } else {
      Serial.printf_P(PSTR("[tasmota] schedule #%u ok, state=%s\n"),
                      (unsigned)s.id, result.c_str());
    }
  }
}

// ── device CRUD ───────────────────────────────────────────────────────────

int deviceCount() { return g_deviceCount; }
const TasmotaDevice &getDevice(int i) { return g_devices[i]; }

bool replaceDevices(const TasmotaDevice *devices, int count) {
  if (count < 0) return false;
  if (count > MAX_DEVICES) count = MAX_DEVICES;
  for (int i = 0; i < count; i++) {
    strlcpy(g_devices[i].ip, devices[i].ip, IP_STR_MAX);
    strlcpy(g_devices[i].name, devices[i].name, NAME_STR_MAX);
  }
  g_deviceCount = count;
  return saveToDisk();
}

// ── schedule CRUD ─────────────────────────────────────────────────────────

int scheduleCount() { return g_scheduleCount; }
const Schedule &getSchedule(int i) { return g_schedules[i]; }

static int findScheduleIndexById(uint32_t id) {
  for (int i = 0; i < g_scheduleCount; i++) {
    if (g_schedules[i].id == id) return i;
  }
  return -1;
}

uint32_t createSchedule(const Schedule &s) {
  if (g_scheduleCount >= MAX_SCHEDULES) return 0;
  Schedule &dst = g_schedules[g_scheduleCount];
  dst.id = g_nextScheduleId++;
  strlcpy(dst.ip, s.ip, IP_STR_MAX);
  strlcpy(dst.cronStr, s.cronStr, CRON_STR_MAX);
  strlcpy(dst.name, s.name, NAME_STR_MAX);
  dst.action = s.action;
  dst.enabled = s.enabled;
  dst.lastFiredEpoch = 0;
  if (!parseCron(dst.cronStr, &dst.mask)) return 0;
  g_scheduleCount++;
  if (!saveToDisk()) return 0;
  return dst.id;
}

bool updateSchedule(uint32_t id, const Schedule &s) {
  int idx = findScheduleIndexById(id);
  if (idx < 0) return false;
  Schedule &dst = g_schedules[idx];
  strlcpy(dst.ip, s.ip, IP_STR_MAX);
  strlcpy(dst.cronStr, s.cronStr, CRON_STR_MAX);
  strlcpy(dst.name, s.name, NAME_STR_MAX);
  dst.action = s.action;
  dst.enabled = s.enabled;
  if (!parseCron(dst.cronStr, &dst.mask)) return false;
  return saveToDisk();
}

bool deleteSchedule(uint32_t id) {
  int idx = findScheduleIndexById(id);
  if (idx < 0) return false;
  for (int i = idx; i < g_scheduleCount - 1; i++) {
    g_schedules[i] = g_schedules[i + 1];
  }
  g_scheduleCount--;
  return saveToDisk();
}

int runScheduleNow(uint32_t id) {
  int idx = findScheduleIndexById(id);
  if (idx < 0) return -1;
  const Schedule &s = g_schedules[idx];
  String result = setTasmotaPower(s.ip, actionToCmd(s.action));
  return result.length() > 0 ? 200 : -1;
}

}  // namespace TasmotaScheduler
