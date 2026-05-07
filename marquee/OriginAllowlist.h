#ifndef ORIGIN_ALLOWLIST_H
#define ORIGIN_ALLOWLIST_H

#include <Arduino.h>

// Pure helper, no AsyncWebServer dependency, exercised in
// tests/native/test_origin_allowlist/.
//
// Returns true iff `origin` matches one entry of the comma-separated
// `allowlist`. Whitespace around list entries is tolerated so the
// build flag string can be human-readable; the comparison itself is
// strict (exact match — browsers compare ACAO byte-for-byte).
//
// Empty origin or empty allowlist always returns false (fail closed).
//
// Lives in its own translation unit so the native test target can
// `#include "OriginAllowlist.cpp"` without dragging in
// ESPAsyncWebServer (which CorsSupport.cpp depends on for the runtime
// header-attaching part).
bool isOriginAllowed(const String &origin, const char *allowlist);

#endif
