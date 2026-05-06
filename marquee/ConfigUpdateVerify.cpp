#include "ConfigUpdateVerify.h"

#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_hmac.h>

namespace {

bool hexNibble(char c, uint8_t &out) {
  if (c >= '0' && c <= '9') { out = c - '0'; return true; }
  if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
  if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
  return false;
}

// Decode hex string `hex` of length 2*outLen into `out`. Returns false on any
// non-hex character or wrong length.
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

// Decode base64 `in` (length `inLen`, must be multiple of 4) into `out`.
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

// Constant-time compare. Both buffers must be of length `len`.
bool constTimeEqual(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

} // namespace

bool verifyConfigUpdateSignature(const String &payload,
                                 const String &signatureBase64,
                                 const char *keyHex) {
  if (keyHex == nullptr || keyHex[0] == '\0') {
    Serial.println(F("[CFG] config update rejected: WAGFAM_CONFIG_HMAC_KEY empty"));
    return false;
  }
  size_t keyHexLen = strlen(keyHex);
  if (keyHexLen < 32 || (keyHexLen % 2) != 0) {
    Serial.println(F("[CFG] config update rejected: HMAC key malformed (need >=32 hex chars, even length)"));
    return false;
  }
  size_t keyLen = keyHexLen / 2;
  // Cap at 64 bytes — that's the SHA-256 block size and any HMAC key longer
  // than that gets pre-hashed by the spec; we don't expect users to hand us
  // anything bigger, so reject defensively.
  if (keyLen > 64) {
    Serial.println(F("[CFG] config update rejected: HMAC key too long"));
    return false;
  }
  uint8_t key[64];
  if (!decodeHex(keyHex, key, keyLen)) {
    Serial.println(F("[CFG] config update rejected: HMAC key has non-hex chars"));
    return false;
  }

  uint8_t expected[32];
  int decoded = decodeBase64(signatureBase64.c_str(), signatureBase64.length(),
                             expected, sizeof(expected));
  if (decoded != 32) {
    Serial.println(F("[CFG] config update rejected: signature didn't decode to 32 bytes"));
    return false;
  }

  uint8_t mac[32];
  br_hmac_key_context kc;
  br_hmac_context ctx;
  br_hmac_key_init(&kc, &br_sha256_vtable, key, keyLen);
  br_hmac_init(&ctx, &kc, 0);
  br_hmac_update(&ctx, (const uint8_t *)payload.c_str(), payload.length());
  br_hmac_out(&ctx, mac);

  if (!constTimeEqual(mac, expected, 32)) {
    Serial.println(F("[CFG] config update rejected: signature mismatch"));
    return false;
  }
  return true;
}
