#ifndef SECURITY_HELPERS_H
#define SECURITY_HELPERS_H

#include <Arduino.h>

bool isProtectedPath(const char *path, const char *configPath, const char *otaPendingPath);
String extractDomain(const String &url);
bool isTrustedFirmwareDomain(const String &firmwareUrl, const String &calendarUrl);

#endif
