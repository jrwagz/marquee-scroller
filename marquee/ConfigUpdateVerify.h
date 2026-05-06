#ifndef CONFIG_UPDATE_VERIFY_H
#define CONFIG_UPDATE_VERIFY_H

#include <Arduino.h>

// Verify an ECDSA-P256 signature over a payload string using a public key.
// Used to validate signed remote config updates pushed by the server in the
// calendar response (issue #99).
//
// Why public-key (and not HMAC):
//   The first cut used HMAC-SHA256 with a shared secret baked in via build
//   flag. @jrwagz pointed out (correctly) that the firmware bin is publicly
//   hosted, so `strings firmware.bin | grep` extracts the key trivially —
//   making the signature decorative. ECDSA fixes this by holding only the
//   *public* key on the device; forging a signature requires the private
//   key, which lives only on the server.
//
// Arguments:
//   payload         — the raw bytes the server signed (canonical JSON
//                     string). We hash the bytes-as-received, NOT the parsed
//                     JSON, so server and firmware don't need to agree on a
//                     JSON re-encoder.
//   signatureBase64 — base64 of raw r || s (64 bytes total: 32-byte r,
//                     32-byte s, big-endian). Standard base64 with padding.
//                     Server emits IEEE P1363 raw form — chosen over ASN.1
//                     DER to avoid an additional parser on the device.
//   keyHex          — uncompressed-point public key as 130 hex chars
//                     (`04 || X || Y` where X, Y are 32 bytes each).
//                     Caller is expected to pass `WAGFAM_CONFIG_PUBLIC_KEY`,
//                     the build-flag-baked public key.
//
// Returns true iff:
//   - keyHex is non-empty, exactly 130 hex chars, leading byte 0x04
//   - signatureBase64 decodes to exactly 64 bytes
//   - SHA-256(payload bytes) matches an ECDSA-P256 verify with the public key
//
// Returns false on any failure or malformed input. Logs a one-line reason
// to Serial for debuggability without leaking key or payload contents.
//
// Cost on ESP8266: ~30ms per verify (dominated by the EC scalar mult).
bool verifyConfigUpdateSignature(const String &payload,
                                 const String &signatureBase64,
                                 const char *keyHex);

#endif
