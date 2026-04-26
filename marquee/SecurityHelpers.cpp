#include "SecurityHelpers.h"

// SEC-06: Protected filesystem paths that cannot be written/deleted via API
bool isProtectedPath(const char *path, const char *configPath, const char *otaPendingPath) {
  return strcmp(path, configPath) == 0
      || strcmp(path, otaPendingPath) == 0;
}

// SEC-12/SEC-03: Extract domain from a URL for validation
String extractDomain(const String &url) {
  int start = url.indexOf("://");
  if (start < 0) return "";
  start += 3;
  int end = url.indexOf('/', start);
  if (end < 0) end = url.length();
  int port = url.indexOf(':', start);
  if (port > 0 && port < end) end = port;
  return url.substring(start, end);
}

// SEC-03: Validate firmware URL domain against the calendar data source domain
bool isTrustedFirmwareDomain(const String &firmwareUrl, const String &calendarUrl) {
  String fwDomain = extractDomain(firmwareUrl);
  String calDomain = extractDomain(calendarUrl);
  if (fwDomain == "" || calDomain == "") return false;
  return fwDomain == calDomain;
}
