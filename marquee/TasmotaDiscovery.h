// TasmotaDiscovery — auto-detect Tasmota switches on the LAN.
//
// Strategy is "all of the above" so we catch devices regardless of their
// configuration:
//
//   1. mDNS query for _tasmota._tcp. Tasmota only registers this when
//      SetOption55 is on (off by default), so this finds the opted-in
//      subset — but for those, it's instantaneous.
//
//   2. Ping sweep of the local /24. AsyncPing one host at a time so the
//      loop stays responsive; takes ~50s for a full /24. Populates LWIP's
//      ARP cache as a side effect.
//
//   3. HTTP probe of every IP that responded to ping. GET /cm?cmnd=Status
//      with a short timeout; if the response parses as Tasmota JSON
//      (presence of "Status.DeviceName"), record the device with its
//      friendly name + hostname (via Status 5 / StatusNET).
//
// Steps 2 and 3 are interleaved: each ping that responds immediately
// kicks off an HTTP probe for that IP before moving to the next ping.
// Total time for a /24 with 20 devices alive: ~57s (254 × 200ms ICMP +
// 20 × 300ms HTTP).
//
// Output: an in-RAM array of DiscoveredDevice — read by the SPA's
// "Auto-detect" button on the Devices card.

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <IPAddress.h>

namespace TasmotaDiscovery {

constexpr int MAX_RESULTS = 32;
constexpr int NAME_STR_MAX = 32;
constexpr int IP_STR_MAX = 16;
constexpr int HOST_STR_MAX = 32;

struct DiscoveredDevice {
  char ip[IP_STR_MAX];
  char name[NAME_STR_MAX];       // FriendlyName / DeviceName from Status
  char hostname[HOST_STR_MAX];   // network hostname from Status 5 (e.g.
                                 // "office-light-6908"), helpful when
                                 // DeviceName is the default "Tasmota"
  const char *source;            // "mdns" or "scan" — caller may want to
                                 // distinguish; points to a static literal
};

enum class DiscoveryState : uint8_t {
  Idle,
  MdnsQuery,   // running the mDNS lookup
  PingSweep,   // ICMP-pinging the /24 (interleaved with HTTP probes)
  Done,
};

struct DiscoveryProgress {
  DiscoveryState state;
  uint32_t id;             // monotonic per-boot
  uint32_t startedAtMs;
  uint32_t completedAtMs;
  uint16_t pingsSent;      // 0..254
  uint16_t pingsResponded;
  uint16_t httpProbed;
  uint16_t tasmotaFound;
  uint16_t currentHostByte;
  IPAddress baseIp;
};

// Kick off a discovery run. Returns false if one is already in flight or
// WiFi isn't joined.
bool start();

// Drive the state machine. Called from main loop every iteration.
void tick();

// Current state — safe to read at any time.
DiscoveryProgress getProgress();

// Read results. Stable after state == Done; partial results during
// PingSweep are visible too.
int resultCount();
const DiscoveredDevice &getResult(int i);

// One-shot synchronous probe of a known IP. Used by the "test a specific
// IP" button in the UI when the user wants to verify a manually-entered
// address looks Tasmota-shaped without running the full discovery. Returns
// true and populates *out on success.
bool probeOne(const char *ip, DiscoveredDevice *out);

// Persistence — last completed scan is written to LittleFS at
// /tasmota_discovered.json when the state machine transitions
// PingSweep → Done. The file format is a JSON object with `scan_id`,
// `completed_at_unix`, and `results` (array of {ip, name, hostname,
// source}). This is the source of truth for the planned upload-to-server
// feature: the server fetches it, persists per-clock, and pushes it back
// down on a hardware replacement so the user doesn't lose their inventory.
constexpr const char *DISCOVERED_FILE = "/tasmota_discovered.json";

// Read the persisted scan results (raw JSON). Returns empty string if no
// scan has been written yet. Caller passes to the SPA as-is.
String readPersistedResults();

}  // namespace TasmotaDiscovery
