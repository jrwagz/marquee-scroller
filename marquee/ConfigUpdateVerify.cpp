#include "ConfigUpdateVerify.h"

#include <bearssl/bearssl_ec.h>
#include <bearssl/bearssl_hash.h>

namespace {

// --- hex / base64 plumbing -------------------------------------------------

bool hexNibble(char c, uint8_t &out) {
  if (c >= '0' && c <= '9') { out = c - '0'; return true; }
  if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
  if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
  return false;
}

// Decode hex string `hex` of length 2*outLen into `out`. Returns false on any
// non-hex character or short input.
bool decodeHex(const char *hex, uint8_t *out, size_t outLen) {
  for (size_t i = 0; i < outLen; i++) {
    uint8_t hi, lo;
    if (!hexNibble(hex[i * 2], hi)) return false;
    if (!hexNibble(hex[i * 2 + 1], lo)) return false;
    out[i] = (hi << 4) | lo;
  }
  return true;
}

// Standard base64 alphabet, returns 0xFF for invalid chars (and 0xFE for '=').
uint8_t b64Lookup(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  if (c == '=') return 0xFE;
  return 0xFF;
}

// Decode base64 `in` (length `inLen`, must be a multiple of 4) into `out`.
// Returns the number of bytes written, or -1 on malformed input.
int decodeBase64(const char *in, size_t inLen, uint8_t *out, size_t outCap) {
  if (inLen % 4 != 0) return -1;
  size_t outIdx = 0;
  for (size_t i = 0; i < inLen; i += 4) {
    uint8_t a = b64Lookup(in[i]);
    uint8_t b = b64Lookup(in[i + 1]);
    uint8_t c = b64Lookup(in[i + 2]);
    uint8_t d = b64Lookup(in[i + 3]);
    if (a == 0xFF || b == 0xFF || c == 0xFF || d == 0xFF) return -1;
    if (a == 0xFE || b == 0xFE) return -1; // '=' not allowed in first two
    if (outIdx + 1 > outCap) return -1;
    out[outIdx++] = (a << 2) | (b >> 4);
    if (c != 0xFE) {
      if (outIdx + 1 > outCap) return -1;
      out[outIdx++] = ((b & 0x0F) << 4) | (c >> 2);
      if (d != 0xFE) {
        if (outIdx + 1 > outCap) return -1;
        out[outIdx++] = ((c & 0x03) << 6) | d;
      }
    } else if (d != 0xFE) {
      return -1; // '=' must come at end
    }
  }
  return (int)outIdx;
}

} // namespace

// ECDSA-P256 verify. Replaces the original HMAC-SHA256 verify after
// @jrwagz's review pointed out that a build-flag-baked HMAC key is
// trivially extractable from the public firmware bin (one `strings | grep`).
//
// With ECDSA we hold only the *public* key in the binary; forging a
// signature requires the private key, which lives only on the server.
//
// Wire format (matches `app/services/config_signing.py` on the server):
//   * `payload`         — canonical-JSON string (raw bytes are signed)
//   * `signatureBase64` — base64 of raw r || s (32 + 32 = 64 bytes)
//   * `keyHex`          — uncompressed-point public key as 130 hex chars,
//                          i.e. `04 || X || Y` where X,Y are 32 bytes each
//
// Steps:
//   1. SHA-256 the payload bytes.
//   2. Base64-decode the signature into a 64-byte buffer.
//   3. Hex-decode the public key into a 65-byte uncompressed-point buffer.
//   4. Hand both to BearSSL's `br_ecdsa_i15_vrfy_raw` with the P-256 impl.
//
// Approx cost on ESP8266: ~30ms per verify (dominated by the EC scalar
// multiplications). Well within our 15-min poll budget.
bool verifyConfigUpdateSignature(const String &payload,
                                 const String &signatureBase64,
                                 const char *keyHex) {
  if (keyHex == nullptr || keyHex[0] == '\0') {
    Serial.println(F("[CFG] config update rejected: WAGFAM_CONFIG_PUBLIC_KEY empty"));
    return false;
  }
  size_t keyHexLen = strlen(keyHex);
  if (keyHexLen != 130) {
    Serial.print(F("[CFG] config update rejected: public key must be 130 hex chars (got "));
    Serial.print(keyHexLen);
    Serial.println(F(")"));
    return false;
  }
  uint8_t pubBytes[65];
  if (!decodeHex(keyHex, pubBytes, 65)) {
    Serial.println(F("[CFG] config update rejected: public key has non-hex chars"));
    return false;
  }
  if (pubBytes[0] != 0x04) {
    // Compressed-point form (0x02/0x03) is allowed by the spec but BearSSL's
    // verify expects uncompressed; we control both ends, so we mandate 0x04.
    Serial.println(F("[CFG] config update rejected: public key must use uncompressed form (0x04 prefix)"));
    return false;
  }

  // Raw (r||s) signature: 64 bytes for P-256.
  uint8_t sig[64];
  int sigLen = decodeBase64(signatureBase64.c_str(), signatureBase64.length(),
                            sig, sizeof(sig));
  if (sigLen != 64) {
    Serial.print(F("[CFG] config update rejected: signature didn't decode to 64 bytes (got "));
    Serial.print(sigLen);
    Serial.println(F(")"));
    return false;
  }

  // Hash the payload — ECDSA signs/verifies a digest, not the raw message.
  uint8_t digest[32];
  br_sha256_context shaCtx;
  br_sha256_init(&shaCtx);
  br_sha256_update(&shaCtx, (const void *)payload.c_str(), payload.length());
  br_sha256_out(&shaCtx, digest);

  br_ec_public_key pk = {
      .curve = BR_EC_secp256r1,
      .q = pubBytes,
      .qlen = 65,
  };

  // br_ecdsa_i15_vrfy_raw returns 1 on success, 0 on bad sig.
  uint32_t ok = br_ecdsa_i15_vrfy_raw(&br_ec_p256_m15,
                                      digest, sizeof(digest),
                                      &pk,
                                      sig, sigLen);
  if (!ok) {
    Serial.println(F("[CFG] config update rejected: ECDSA-P256 signature mismatch"));
    return false;
  }
  return true;
}
