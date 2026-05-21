// Device-enrollment client for the wagfam-server `/api/v1/enroll` endpoint
// (issue #125, companion to jrwagz/wagfam-server#62).
//
// A factory-fresh clock with no stored calendar API key polls this endpoint,
// shows the server-minted code on its LED, and — once an admin authorizes the
// device from the admin SPA — receives a signed config bundle carrying the
// calendar URL + auth key. The bundle reuses the issue #99 config-update
// format, so marquee.ino verifies it with the same verifyConfigUpdateSignature()
// and applies it with the same applyConfigJson().
//
// EnrollmentClient is a JsonListener (same pattern as WagFamBdayClient). The
// parser tracks object/array nesting depth, so a field is classified strictly
// by where it sits in the document — bundle fields are read only as direct
// children of "config", classification fields only at the top level. parse()
// is pure (no network, no globals) and is unit-tested under tests/native/.
#pragma once

#include <Arduino.h>
#include <JsonListener.h>            // base class
#include "WagFamBdayClient.h"        // DeviceInfo + buildHeartbeatQuery()
// The HTTPS stack (ESP8266WiFi / HTTPClient / BearSSL) and JsonStreamingParser
// are implementation details — included in EnrollmentClient.cpp, not here.

class EnrollmentClient : public JsonListener {
 public:
  // Parsed `/api/v1/enroll` response.
  struct Result {
    bool ok = false; // body parsed into a recognized status
    String status;   // "pending" | "authorized" | "" (parse failure)

    // `secret` is returned only on the first-contact response and again after
    // a server-side enrollment reset; `code` is echoed on every pending
    // response. Both are empty when the field is absent.
    String secret;
    String code;

    // Signed config bundle — populated only when status == "authorized".
    // Same shape as the calendar response's config-update fields (issue #99).
    int configUpdateVersion = 0;
    String configUpdatePayload;
    String configUpdateSignature;
  };

  // Perform one enrollment poll. `enrollUrl` is the bare endpoint (e.g.
  // https://host/api/v1/enroll); chip_id, telemetry, and — when non-empty —
  // `secret` are appended as query params. HTTPS via BearSSL with
  // setInsecure() (issue #125: the bundle's ECDSA signature, not TLS, is the
  // integrity guarantee). Returns a Result with ok == false on any
  // network/HTTP/parse failure.
  static Result poll(const String &enrollUrl, const DeviceInfo &device,
                     const String &secret);

  // Parse an `/api/v1/enroll` JSON response body. Pure — no network, no
  // globals. Returns ok == false on malformed input or an unrecognized status.
  static Result parse(const String &body);

  // JsonListener callbacks — public because JsonStreamingParser drives them;
  // not meant to be called directly (use parse()).
  void whitespace(char c) override {}
  void startDocument() override {}
  void key(String key) override;
  void value(String value) override;
  void startArray() override;
  void endArray() override;
  void startObject() override;
  void endObject() override;
  void endDocument() override;

 private:
  Result result_;
  String currentKey_;
  int objectDepth_ = 0;  // count of currently-open objects
  int arrayDepth_ = 0;   // count of currently-open arrays
  int configDepth_ = -1; // objectDepth_ at which "config"'s children sit; -1 = outside
};
