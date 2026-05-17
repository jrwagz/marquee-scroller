// Clock family tagging helpers. Pulled out of marquee.ino so the wire-to-
// display mapping can be unit-tested under tests/native/ without dragging
// in WiFi / LittleFS / web-server globals. Pure String-in / String-out —
// no hardware dependencies.
#pragma once

#include <Arduino.h>

// True iff `wire` is one of the families the firmware knows how to render.
// The server agrees on this allowlist (see wagfam-server check-in handler);
// anything else is treated as "unset" rather than rendered as-is so a
// server-side typo can't display garbled text on the matrix.
bool isKnownFamily(const String &wire);

// Map the lowercase wire value ("butterfield" / "wagner" / "") to the
// capitalized display form rendered by the bootup welcome message and the
// SPA header. Unknown / empty returns "" so callers can fall back to their
// generic label.
String familyDisplay(const String &wire);
