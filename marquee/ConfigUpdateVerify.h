#ifndef CONFIG_UPDATE_VERIFY_H
#define CONFIG_UPDATE_VERIFY_H

#include <Arduino.h>

// Verify an HMAC-SHA256 signature over a payload string using a hex-encoded
// shared secret. Used to validate signed remote config updates pushed by the
// server in the calendar response (issue #99).
//
// Arguments:
//   payload         — the raw bytes the server signed (canonical JSON string).
//                     We sign the bytes-as-received, NOT the parsed JSON, so
//                     server and firmware don't have to agree on a JSON
//                     re-encoder. Caller passes whatever the parser stored.
//   signatureBase64 — the base64-encoded 32-byte HMAC digest sent by the
//                     server. Standard base64 (not URL-safe), with padding.
//   keyHex          — hex-encoded shared secret. Length must be even and
//                     >= 32 chars (>=16 bytes). Caller is expected to be
//                     WAGFAM_CONFIG_HMAC_KEY (or a runtime override).
//
// Returns true iff:
//   - keyHex is non-empty and well-formed (even length, all hex digits)
//   - signatureBase64 decodes to exactly 32 bytes
//   - HMAC-SHA256(decoded key, payload bytes) matches the decoded signature
//     (constant-time compare).
//
// Returns false on any failure or malformed input. Logs a one-line reason
// to Serial so a verification failure is debuggable from a serial monitor
// without leaking the key or payload contents.
bool verifyConfigUpdateSignature(const String &payload,
                                 const String &signatureBase64,
                                 const char *keyHex);

#endif
