# OTA Update Strategy

> **Platform:** Wemos D1 Mini (ESP8266) — strategy adopted in v3.08.0-wagfam and still
> in effect (current: v4.0.1-wagfam).
>
> This document explains why ArduinoOTA was removed, how the boot-confirmation rollback
> system works, and how the calendar JSON channel drives remote auto-updates across
> distributed family clocks.

---

## Why ArduinoOTA Was Removed

ArduinoOTA is a LAN-based update mechanism that requires the developer to be on the same
local network as the device. For a clock deployed in a family member's home across the
country, it provides zero value — you cannot reach it remotely.

| Property | ArduinoOTA | ESPhttpUpdate |
| --- | --- | --- |
| Transport | UDP/TCP, LAN only | HTTP, any network |
| Reachable remotely | No | Yes |
| Persistent heap cost | 4–8 KB (always allocated) | 0 (allocated only during update) |
| Flash code size | ~10 KB | ~5 KB |
| Rollback support | None | Firmware-level (this document) |

Removing ArduinoOTA recovers **4–8 KB of persistent heap** available for the entire
uptime of the device. The web-based update paths that remain cover all real use cases:

| Route | Method | Notes |
| --- | --- | --- |
| `/update` | Browser file upload | Sketch only — does NOT touch LittleFS |
| `/updateFromUrl` | HTTP URL download | Triggers ESPhttpUpdate; sketch only |
| Auto (calendar config) | HTTP URL from JSON server | Version-checked; triggers `performAutoUpdate()` |
| `/updatefs` | Browser file upload (LittleFS image) | Refreshes SPA bundle; backs up + restores `/conf.txt` |
| `POST /api/spa/update-from-url` | JSON body with URL | Same backup/restore as `/updatefs`, fetches from URL |

> The `/update*` paths only flash the sketch partition. The SPA bundle and `/conf.txt`
> live on LittleFS and are unaffected. Use `/updatefs` (or its API equivalent) when
> you need to push a new SPA bundle.

---

## The ESP8266 Rollback Challenge

The ESP32 has native dual-bank OTA: it writes the new firmware to a second partition and
only switches after verifying a successful boot. The ESP8266 has no such mechanism —
there is only one firmware slot. A bad update either:

- **Fails during download** — ESPhttpUpdate verifies the MD5 checksum; old firmware stays.
  This is automatically safe.
- **Boots but crashes** — old firmware is gone; the device is stuck in a crash loop until
  the rollback mechanism re-flashes the previous version.
- **Boots but breaks WiFi** — cannot recover remotely; physical reflash required.

The third scenario cannot be recovered from remotely regardless of strategy. The second is
the target: detect a crash loop and re-download the previous known-good firmware.

---

## Failure Mode Analysis

| Scenario | Detection | Recovery |
| --- | --- | --- |
| Download corruption | ESPhttpUpdate MD5 check fails; update rejected | Automatic; old firmware still running |
| New firmware crashes on boot | Reboot loop; pending file not confirmed | Rollback: re-flash safe URL after 2 unconfirmed boots |
| New firmware boots, then WiFi fails | Cannot detect remotely | Physical reflash required |
| New firmware has non-crash bugs | Device stays up; confirmation completes normally | Manual: `/updateFromUrl` with known-good URL |

---

## Boot-Confirmation Rollback Pattern

Before every OTA flash, the firmware writes a **pending record** to LittleFS at
`/ota_pending.txt`:

```ini
safeUrl=http://example.com/v3.07.0.bin
newUrl=http://example.com/v3.08.0.bin
boots=0
```

- `safeUrl` — URL of the **current** confirmed-stable firmware; the rollback target if
  this update fails.
- `newUrl` — URL of the firmware being installed; promoted to `OTA_SAFE_URL` after
  the new firmware is confirmed stable.

On every boot, `checkOtaRollback()` runs after WiFi connects:

1. Read `/ota_pending.txt` — if absent, normal operation
2. Increment the `boots` counter and write it back
3. **If `boots >= 2`**: two unconfirmed boots → assume crash loop → re-flash `safeUrl`
4. **If `boots == 1`**: first boot after update → start 5-minute confirmation timer

After `OTA_CONFIRM_MS` (5 minutes) of stable uptime, `processEverySecond()` fires the
confirmation:

1. Delete `/ota_pending.txt`
2. Promote `newUrl` to `OTA_SAFE_URL` (rollback target for the next update)
3. Persist `OTA_SAFE_URL` to `/conf.txt`

### Rollback Decision Tree

```cpp
Boot
 │
 ├─ /ota_pending.txt absent ──────────────────► Normal operation
 │
 └─ /ota_pending.txt present
      │
      ├─ boots == 1 (first boot after update) ► Write boots=1, start 5-min timer
      │                                          After 5 min: delete file, save new safeUrl
      │
      └─ boots >= 2 (crash loop detected)
           │
           ├─ safeUrl present ──────────────► ESPhttpUpdate(safeUrl) → reboot
           │
           └─ safeUrl empty ────────────────► Log warning, continue with current firmware
```

---

## Remote Version Check via Calendar Config

The calendar JSON endpoint can trigger a firmware update by including `latestVersion`
and `firmwareUrl` in the `config` block:

```json
[
  {
    "config": {
      "latestVersion": "3.08.0-wagfam",
      "firmwareUrl": "http://example.com/marquee-v3.08.0.bin"
    }
  },
  { "message": "Justin's Birthday - 3 days away" }
]
```

When `getWeatherData()` processes the server response, it compares `latestVersion`
against the compiled `VERSION` macro. If they differ and all guard conditions pass,
`performAutoUpdate()` is triggered automatically.

`firmwareUrl` must use `http://` — HTTPS is not supported by `ESPhttpUpdate` on the
ESP8266 without large additional heap cost.

### Auto-Update Guard Conditions

Auto-update is skipped if any of these conditions are true:

| Condition | Reason |
| --- | --- |
| `latestVersion == VERSION` | Already on latest version |
| `firmwareUrl` does not start with `http://` | HTTPS not supported |
| `otaConfirmAt != 0` | A previous update is still pending confirmation |
| `millis() < OTA_CONFIRM_MS` | Device booted too recently; let it stabilize first |

---

## Configuration Keys

One new key is stored in `/conf.txt` to persist OTA state across reboots:

| Key | Type | Purpose |
| --- | --- | --- |
| `OTA_SAFE_URL` | String | URL of the last confirmed-stable firmware; rollback target |

`OTA_SAFE_URL` starts empty on a clean install and is automatically updated after each
successful OTA confirmation. On first update, rollback is unavailable (no safe URL yet);
after confirmation of the first update, rollback becomes available for all future updates.

---

## Heap Impact

| Component | Heap Impact | Notes |
| --- | --- | --- |
| ArduinoOTA (removed) | **−4 to −8 KB permanent** | UDP/TCP sockets and mDNS hooks freed |
| ESPhttpUpdate at idle | 0 | Only allocates during an active update |
| `/ota_pending.txt` read/write | ~100 bytes peak | LittleFS buffers; freed immediately |

Combined with the `setBufferSizes(2048, 512)` fix applied earlier, total heap recovered
during the most memory-intensive window (calendar HTTPS fetch) is now **16–27 KB**.

---

## Sequence: Successful Auto-Update

```text
Server config JSON → latestVersion != VERSION
  → performAutoUpdate(firmwareUrl)
    → write /ota_pending.txt (boots=0, safeUrl=OTA_SAFE_URL, newUrl=firmwareUrl)
    → ESPhttpUpdate.update() → device reboots into new firmware
      → checkOtaRollback() reads boots=0, writes boots=1, starts 5-min timer
      → processEverySecond() fires after 5 min
        → delete /ota_pending.txt
        → OTA_SAFE_URL = firmwareUrl
        → savePersistentConfig()
        → done; next update can roll back to this version
```

## Sequence: Crash-Loop Rollback

```text
Server config JSON → latestVersion != VERSION
  → performAutoUpdate(firmwareUrl)
    → write /ota_pending.txt (boots=0, safeUrl=http://.../v3.07.bin, newUrl=...)
    → ESPhttpUpdate.update() → device reboots into broken firmware
      → checkOtaRollback(): boots=0 → write boots=1
      → firmware crashes before 5-min confirmation
      → device reboots again
        → checkOtaRollback(): boots=1 → boots >= 2
          → ESPhttpUpdate(safeUrl=http://.../v3.07.bin)
          → device reboots into known-good v3.07 firmware
          → /ota_pending.txt absent → normal operation resumes
```
