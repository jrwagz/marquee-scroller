#include "SecurityHelpers.h"

// SEC-06: Protected filesystem paths that cannot be written/deleted via API
bool isProtectedPath(const char *path, const char *configPath, const char *otaPendingPath) {
  return strcmp(path, configPath) == 0
      || strcmp(path, otaPendingPath) == 0;
}

// SEC-12/SEC-03: Extract domain from a URL for validation.
// Accepts URLs with or without a scheme:
//   "https://example.com/path"   -> "example.com"
//   "http://example.com:8080/x"  -> "example.com"
//   "example.com/path"           -> "example.com"
//   "example.com"                -> "example.com"
//   "example.com:8080"           -> "example.com"
//   ""                           -> ""
// Strips userinfo (user:pass@host), port, and any path/query/fragment.
String extractDomain(const String &url) {
  if (url.length() == 0) return "";

  int start = url.indexOf("://");
  if (start >= 0) {
    start += 3;
  } else {
    start = 0;
  }

  // Skip userinfo if present (user:pass@host)
  int authoritySep = url.indexOf('@', start);
  int firstSlash = url.indexOf('/', start);
  if (authoritySep > 0 && (firstSlash < 0 || authoritySep < firstSlash)) {
    start = authoritySep + 1;
  }

  int end = url.indexOf('/', start);
  if (end < 0) end = url.length();
  int query = url.indexOf('?', start);
  if (query > 0 && query < end) end = query;
  int fragment = url.indexOf('#', start);
  if (fragment > 0 && fragment < end) end = fragment;
  int port = url.indexOf(':', start);
  if (port > 0 && port < end) end = port;

  return url.substring(start, end);
}

// Check whether `domain` is one of the comma-separated entries in `allowlist`.
// Trims surrounding whitespace from each entry. Case-sensitive (DNS isn't, but
// we normalize at call sites instead of guessing here). An empty or null
// allowlist always returns false.
bool isInTrustedDomainList(const String &domain, const char *allowlist) {
  if (domain.length() == 0 || allowlist == nullptr || allowlist[0] == '\0') return false;

  const char *cursor = allowlist;
  while (*cursor) {
    // Skip leading whitespace
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    const char *entryStart = cursor;
    while (*cursor && *cursor != ',') cursor++;
    const char *entryEnd = cursor;
    // Trim trailing whitespace
    while (entryEnd > entryStart && (entryEnd[-1] == ' ' || entryEnd[-1] == '\t')) entryEnd--;

    size_t len = entryEnd - entryStart;
    if (len > 0 && len == (size_t)domain.length()
        && strncmp(entryStart, domain.c_str(), len) == 0) {
      return true;
    }
    if (*cursor == ',') cursor++;
  }
  return false;
}

// SEC-03: Validate firmware URL domain.
// Trusted iff the firmware URL's domain is either listed in the compile-time
// allowlist OR matches the calendar URL's domain. The allowlist exists so that
// production deploys can serve firmware from a fixed CDN that differs from the
// calendar source — see WAGFAM_TRUSTED_FIRMWARE_DOMAINS in Settings.h.
bool isTrustedFirmwareDomain(const String &firmwareUrl, const String &calendarUrl, const char *trustedAllowlist) {
  String fwDomain = extractDomain(firmwareUrl);
  if (fwDomain == "") return false;

  if (isInTrustedDomainList(fwDomain, trustedAllowlist)) return true;

  String calDomain = extractDomain(calendarUrl);
  if (calDomain == "") return false;
  return fwDomain == calDomain;
}
