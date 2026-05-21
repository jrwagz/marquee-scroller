#include "EnrollmentClient.h"

#include <memory>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <JsonStreamingParser.h>

// Cap on the response body we will buffer. An authorized bundle is ~600 bytes
// (a short JSON payload + a base64 signature); 2 KB is comfortable headroom
// and bounds heap use if a misbehaving server streams without a Content-Length.
static const size_t ENROLL_MAX_BODY = 2048;

// Wall-clock ceiling on reading the response, so a stalled connection can't
// wedge the enrollment loop.
static const uint32_t ENROLL_READ_TIMEOUT_MS = 10000;

// ── JSON parsing ─────────────────────────────────────────────────────────────
//
// The parser classifies a field by its position in the document, tracked via
// objectDepth_ / arrayDepth_ / configDepth_:
//   - top-level fields (status, enrollment_secret, enrollment_code) are read
//     only at objectDepth_ == 1, outside any array;
//   - bundle fields (configUpdate*) are read only as direct children of the
//     "config" object (objectDepth_ == configDepth_).
// A boolean "in config" flag is not enough — JSON nesting the firmware does
// not control (an object inside "config", a stray "config" key elsewhere)
// could otherwise relocate a trust-sensitive field across the boundary.

void EnrollmentClient::startObject() {
  objectDepth_++;
  // The "config" object's direct children sit one level below its key. Record
  // that depth (only for a top-level "config" key) so bundle fields are picked
  // up there and nowhere else.
  if (configDepth_ == -1 && arrayDepth_ == 0 && objectDepth_ == 2 &&
      currentKey_ == "config") {
    configDepth_ = objectDepth_;
  }
  currentKey_ = "";
}

void EnrollmentClient::endObject() {
  // Closing the "config" object (or anything shallower) leaves config scope.
  if (configDepth_ != -1 && objectDepth_ <= configDepth_) {
    configDepth_ = -1;
  }
  objectDepth_--;
  currentKey_ = "";
}

void EnrollmentClient::startArray() {
  arrayDepth_++;
}

void EnrollmentClient::endArray() {
  arrayDepth_--;
  currentKey_ = "";
}

void EnrollmentClient::key(String key) {
  currentKey_ = key;
}

void EnrollmentClient::value(String value) {
  // The enrollment schema has no arrays — values inside one are never ours.
  if (arrayDepth_ > 0) {
    return;
  }
  if (configDepth_ != -1 && objectDepth_ == configDepth_) {
    // Direct child of "config" — the signed bundle.
    if (currentKey_ == "configUpdateVersion") {
      result_.configUpdateVersion = value.toInt();
    } else if (currentKey_ == "configUpdatePayload") {
      result_.configUpdatePayload = value;
    } else if (currentKey_ == "configUpdateSignature") {
      result_.configUpdateSignature = value;
    }
  } else if (objectDepth_ == 1) {
    // Top-level classification fields.
    if (currentKey_ == "status") {
      result_.status = value;
    } else if (currentKey_ == "enrollment_secret") {
      result_.secret = value;
    } else if (currentKey_ == "enrollment_code") {
      result_.code = value;
    }
  }
  currentKey_ = "";
}

void EnrollmentClient::endDocument() {
  // A response is usable only if it carried a status the firmware acts on.
  result_.ok = (result_.status == "pending" || result_.status == "authorized");
}

EnrollmentClient::Result EnrollmentClient::parse(const String &body) {
  EnrollmentClient listener;
  JsonStreamingParser parser;
  parser.setListener(&listener);
  parser.reset();
  for (unsigned int i = 0; i < body.length(); i++) {
    parser.parse(body[i]);
  }
  return listener.result_;
}

// ── HTTPS poll ───────────────────────────────────────────────────────────────

EnrollmentClient::Result EnrollmentClient::poll(const String &enrollUrl,
                                                const DeviceInfo &device,
                                                const String &secret) {
  Result failure; // ok == false

  if (enrollUrl.length() == 0) {
    Serial.println(F("[ENROLL] no enrollment URL configured"));
    return failure;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  // Issue #125: setInsecure() — consistent with WagFamBdayClient. The config
  // bundle is ECDSA-signed and verified against the embedded public key, so a
  // MITM cannot forge it. The one-time enrollment secret is the only thing
  // protected by transport alone — a documented, accepted residual risk.
  client->setInsecure();
  // Reduce the TLS record buffers from the 16 KB default; the enrollment
  // payload is tiny. Saves ~12-19 KB of heap per poll.
  client->setBufferSizes(2048, 512);

  // Build the URL: the shared heartbeat telemetry (so the admin sees live data
  // on the pending device) plus the secret once we have one.
  String url;
  url.reserve(enrollUrl.length() + 220);
  url = enrollUrl;
  url += (url.indexOf('?') >= 0) ? '&' : '?';
  url += buildHeartbeatQuery(device);
  if (secret.length() > 0) {
    // The server mints the secret with secrets.token_hex() — hex is URL-safe,
    // so no percent-encoding is needed (see wagfam-server#62).
    url += "&secret=";
    url += secret;
  }

  HTTPClient https;
  if (!https.begin(*client, url)) {
    Serial.println(F("[ENROLL] HTTPS begin failed"));
    return failure;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print(F("[ENROLL] GET failed, code "));
    Serial.println(httpCode);
    https.end();
    return failure;
  }

  // Accumulate the (small) response body, then hand it to the shared parser.
  String body;
  body.reserve(768);
  WiFiClient *stream = https.getStreamPtr();
  int len = https.getSize();
  uint32_t startMs = millis();
  while (https.connected() && (len > 0 || len == -1)) {
    if (millis() - startMs > ENROLL_READ_TIMEOUT_MS) {
      Serial.println(F("[ENROLL] response read timed out"));
      https.end();
      return failure;
    }
    size_t avail = stream->available();
    if (avail) {
      char buf[128];
      int c = stream->readBytes(buf, (avail > sizeof(buf)) ? sizeof(buf) : avail);
      for (int i = 0; i < c; i++) {
        body += buf[i];
      }
      if (len > 0) {
        len -= c;
      }
      if (body.length() > ENROLL_MAX_BODY) {
        Serial.println(F("[ENROLL] response body too large — aborting"));
        https.end();
        return failure;
      }
    }
    delay(1);
  }
  https.end();

  return parse(body);
}
