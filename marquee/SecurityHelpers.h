#ifndef SECURITY_HELPERS_H
#define SECURITY_HELPERS_H

#include <Arduino.h>

bool isProtectedPath(const char *path, const char *configPath, const char *otaPendingPath);
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
