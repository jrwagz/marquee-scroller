// mDNS hostname sanitization. Pulled out of marquee.ino so it can be unit-
// tested under tests/native/ without dragging in the entire ESP8266mDNS
// stack. Pure String-in / String-out — no globals, no hardware dependencies.
#pragma once

#include <Arduino.h>

// Convert a free-form label (typically the user-set DEVICE_NAME) into a
// DNS-safe mDNS hostname conforming to RFC 1035 + Apple's Bonjour rules:
//
//  - lowercase only
//  - characters limited to [a-z0-9-]
//  - no leading or trailing hyphen
//  - max 63 characters (DNS label limit)
//
// Runs of non-alphanumeric characters collapse to a single hyphen so
// multi-word names (e.g. "Kitchen Clock") become "kitchen-clock". When the
// sanitized result is empty (input was nothing but punctuation, or empty),
// we fall back to "wagfam-<chipId>" so every device still gets a stable,
// non-colliding label.
String mdnsLabelFor(const String &source, const String &chipId);
