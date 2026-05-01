#ifndef SECURITY_HELPERS_H
#define SECURITY_HELPERS_H

#include <Arduino.h>

bool isProtectedPath(const char *path, const char *configPath, const char *otaPendingPath);

// Validate a path supplied by an API caller for filesystem write/upload.
// Accepts: paths that start with "/", contain no ".." traversal segment,
// no embedded null bytes, no backslashes, length 1..127, and are not in
// the protected-paths list (configPath, otaPendingPath).
//
// Rejects empty paths, paths without leading "/", "..", "//" sequences,
// trailing "/" (directories — we only support file uploads), and any
// path matching the protected list.
//
// The reason for the strict validation: this gates LittleFS writes from
// untrusted HTTP input. A traversal like "/../etc/passwd" is meaningless
// on LittleFS (no real parent dir) but allowing it muddles audit logs
// and risks future misuse if we ever add a fs-relative API.
bool isValidUploadPath(const char *path, const char *configPath, const char *otaPendingPath);

String extractDomain(const String &url);

// Returns true if `domain` appears in a comma-separated `allowlist` string
// (e.g. "cdn.example.com,releases.example.com"). Whitespace around entries
// is trimmed. An empty allowlist always returns false.
bool isInTrustedDomainList(const String &domain, const char *allowlist);

// Trusted iff `firmwareUrl`'s domain is either:
//   (a) listed in the compile-time allowlist (`trustedAllowlist`, see WAGFAM_TRUSTED_FIRMWARE_DOMAINS), OR
//   (b) the same domain as the active calendar source URL.
bool isTrustedFirmwareDomain(const String &firmwareUrl, const String &calendarUrl, const char *trustedAllowlist);

#endif
