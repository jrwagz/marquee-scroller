// TasmotaScheduler — cron-driven Tasmota switch control.
//
// Schedules cron-style rules that fire HTTP commands at Tasmota switches
// on the LAN. Two pieces of persisted state:
//
//   /tasmota_devices.json    Array of registered devices the clock controls:
//                              [{ "ip": "192.168.1.42", "name": "Office" }]
//                            Driven by the SPA's "devices to control" list
//                            (the user-facing "config option that's an array
//                            of IPs"). Schedules reference a device by IP.
//
//   /tasmota_schedules.json  Array of schedules:
//                              [{ "id":1, "ip":"192.168.1.42",
//                                 "cron":"0 22 * * *", "action":"OFF",
//                                 "enabled":true, "name":"bedtime" }]
//                            ID is a monotonic per-clock counter that lets
//                            the SPA update / delete a specific entry.
//
// Cron syntax: standard 5-field `min hour dom mon dow`. Each field supports
// `*`, `n`, `n,m,k`, `n-m`, `*/s`, and `n-m/s`. Day-of-week: 0-7 with both 0
// and 7 meaning Sunday (POSIX). Compatible with crontab-style strings.
//
// Tasmota HTTP API: GET /cm?cmnd=Power%20<action>. No auth on default-config
// Tasmotas; optional `&user=X&password=Y` if locked. We pass user/pass
// per-device when set, blank otherwise.
//
// Auth on the clock side: the new endpoints use the trusted-LAN model
// (no auth) — same as the rest of /api/*. Future header-based gating would
// share the same hook as the LAN-visibility feature.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace TasmotaScheduler {

// ── cron mask: pre-parsed bitsets, one per cron field ─────────────────────
//
// Matching a (min, hour, dom, mon, dow) tuple is a constant-time set of
// bit-tests against these masks. Parse once at config load; match every
// minute against `now()`.

struct CronMask {
  uint64_t minute;  // bits 0-59
  uint32_t hour;    // bits 0-23
  uint32_t dom;     // bits 1-31  (bit 0 unused)
  uint16_t month;   // bits 1-12  (bit 0 unused)
  uint8_t dow;      // bits 0-6   (Sun=0; "7" in input gets folded to 0)
  bool valid;       // false if parseCron failed
};

// Parse a 5-field cron string into a CronMask. Returns false on syntax
// error; *out.valid is also set so callers can stash CronMask in a struct
// and check the flag without keeping the return value.
bool parseCron(const char *cronStr, CronMask *out);

// Does the mask match the given local-time fields?
bool cronMatches(const CronMask &m, int minute, int hour,
                 int domOneIndexed, int monthOneIndexed, int dowSunZero);

// Self-test helper, exposed for unit testing. Returns true iff every test
// case passes; on failure, prints which one to Serial. Cheap to call.
bool runCronSelfTest();

// ── schedule model ────────────────────────────────────────────────────────

constexpr int MAX_DEVICES = 16;
constexpr int MAX_SCHEDULES = 32;
constexpr int CRON_STR_MAX = 64;
constexpr int IP_STR_MAX = 16;
constexpr int NAME_STR_MAX = 32;

enum class TasmotaAction : uint8_t {
  Off,
  On,
  Toggle,
};

struct TasmotaDevice {
  char ip[IP_STR_MAX];
  char name[NAME_STR_MAX];
};

struct Schedule {
  uint32_t id;            // monotonic, assigned at create time
  char ip[IP_STR_MAX];    // target device (free-form; doesn't have to match
                          // a registered device, but the UI nudges it)
  char cronStr[CRON_STR_MAX];
  CronMask mask;          // pre-parsed; rebuilt on every load/save
  TasmotaAction action;
  bool enabled;
  char name[NAME_STR_MAX];
  uint32_t lastFiredEpoch;  // unix timestamp of last fire; prevents
                            // re-firing within the same minute if the loop
                            // ticks twice or the clock corrects forward
};

// ── lifecycle ─────────────────────────────────────────────────────────────

// Load both files from LittleFS into RAM caches and parse all crons.
// Call once at boot from setup().
void loadFromDisk();

// Save both caches back to LittleFS. Used after any mutation.
bool saveToDisk();

// Called once per minute from the main loop (processEveryMinute). Walks
// the enabled schedules and fires HTTP for each that matches the current
// local time + hasn't already fired this minute.
void tickMinute(time_t nowLocal);

// ── device CRUD ───────────────────────────────────────────────────────────

int deviceCount();
const TasmotaDevice &getDevice(int i);
// Replace the whole device list at once — simpler for the SPA than the
// add/remove/update dance and there's at most MAX_DEVICES entries.
// Caller passes count and array; count above MAX_DEVICES is clipped.
bool replaceDevices(const TasmotaDevice *devices, int count);

// ── schedule CRUD ─────────────────────────────────────────────────────────

int scheduleCount();
const Schedule &getSchedule(int i);

// Create a new schedule. `id` is assigned; the input id field is ignored.
// Returns the new id (0 on failure — e.g., MAX_SCHEDULES hit or bad cron).
uint32_t createSchedule(const Schedule &s);

// Update an existing schedule by ID. Returns false if not found or cron
// parse fails.
bool updateSchedule(uint32_t id, const Schedule &s);

// Delete by ID. Returns false if not found.
bool deleteSchedule(uint32_t id);

// Fire a schedule's action immediately (for the SPA's "Test" button).
// Returns the HTTP status code from the Tasmota, or -1 on transport error.
int runScheduleNow(uint32_t id);

// ── direct HTTP probe (no schedule lookup) ────────────────────────────────

// Issue a Power command to a Tasmota device. `action` accepts "ON", "OFF",
// "TOGGLE". Returns the parsed POWER state from the response ("ON" / "OFF")
// on success, empty string on failure. Used by both the schedule executor
// and the SPA's "device state probe" button.
String setTasmotaPower(const char *ip, const char *action);

// Read the current POWER state of a Tasmota at the given IP. Returns
// "ON" / "OFF" on success, empty on failure. Wraps GET /cm?cmnd=Power.
String readTasmotaPower(const char *ip);

}  // namespace TasmotaScheduler
