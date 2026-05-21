#include <unity.h>

// Stubs must be first — they're found via -I tests/native/stubs
#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "ESP8266HTTPClient.h"
#include "WiFiClientSecureBearSSL.h"

// Library source — found via -I lib/json-streaming-parser
#include "JsonStreamingParser.cpp"
#include "JsonListener.cpp"

// Production source — found via -I marquee. Exercises EnrollmentClient::parse(),
// the pure JSON-response parser. poll() (the HTTPS path) is compiled here too
// but never called — it needs a live network.
#include "EnrollmentClient.cpp"

void setUp() {}
void tearDown() {}

// ── First contact: minting response carries secret + code ───────────────────

void test_first_contact_returns_secret_and_code() {
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"status\":\"pending\","
        "\"enrollment_secret\":\"a1b2c3d4e5f6\","
        "\"enrollment_code\":\"K7M2QP\"}");
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("pending", r.status.c_str());
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6", r.secret.c_str());
    TEST_ASSERT_EQUAL_STRING("K7M2QP", r.code.c_str());
}

// ── Subsequent pending poll: code only, secret never re-echoed ──────────────

void test_pending_poll_returns_code_without_secret() {
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"status\":\"pending\",\"enrollment_code\":\"K7M2QP\"}");
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("pending", r.status.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.secret.c_str());
    TEST_ASSERT_EQUAL_STRING("K7M2QP", r.code.c_str());
}

// ── Authorized: signed config bundle in the nested "config" object ──────────

void test_authorized_returns_signed_config_bundle() {
    // configUpdatePayload is itself an (escaped) JSON string on the wire; the
    // parser hands it back unescaped — exactly the bytes the server signed and
    // verifyConfigUpdateSignature() must hash.
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"status\":\"authorized\",\"config\":{"
        "\"configUpdateVersion\":7,"
        "\"configUpdatePayload\":\"{\\\"wagfam_api_key\\\":\\\"tok\\\"}\","
        "\"configUpdateSignature\":\"c2lnYmFzZTY0\"}}");
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("authorized", r.status.c_str());
    TEST_ASSERT_EQUAL(7, r.configUpdateVersion);
    TEST_ASSERT_EQUAL_STRING("{\"wagfam_api_key\":\"tok\"}",
                             r.configUpdatePayload.c_str());
    TEST_ASSERT_EQUAL_STRING("c2lnYmFzZTY0", r.configUpdateSignature.c_str());
}

// ── Config keys must not leak to the top level, and vice versa ──────────────

void test_config_block_before_status_still_parses() {
    // Order independence: "config" object closes before "status" appears.
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"config\":{\"configUpdateVersion\":3},\"status\":\"authorized\"}");
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("authorized", r.status.c_str());
    TEST_ASSERT_EQUAL(3, r.configUpdateVersion);
}

void test_object_nested_inside_config_does_not_displace_bundle_fields() {
    // Regression guard: a nested object inside "config" must not relocate the
    // bundle fields. configUpdateVersion / Payload / Signature appear *after*
    // the nested "meta" object and must still be read as config children.
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"status\":\"authorized\",\"config\":{"
        "\"meta\":{\"issuer\":\"admin\",\"n\":1},"
        "\"configUpdateVersion\":5,"
        "\"configUpdatePayload\":\"P\","
        "\"configUpdateSignature\":\"S\"}}");
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(5, r.configUpdateVersion);
    TEST_ASSERT_EQUAL_STRING("P", r.configUpdatePayload.c_str());
    TEST_ASSERT_EQUAL_STRING("S", r.configUpdateSignature.c_str());
}

void test_bundle_fields_at_top_level_are_not_read_as_bundle() {
    // configUpdate* are trusted-payload fields — they count only inside the
    // "config" object. At the top level they must be ignored.
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"status\":\"authorized\","
        "\"configUpdateVersion\":9,"
        "\"configUpdatePayload\":\"X\","
        "\"configUpdateSignature\":\"Y\"}");
    TEST_ASSERT_TRUE(r.ok); // status is still recognized
    TEST_ASSERT_EQUAL(0, r.configUpdateVersion);
    TEST_ASSERT_EQUAL_STRING("", r.configUpdatePayload.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.configUpdateSignature.c_str());
}

// ── Defensive: authorized with no bundle ────────────────────────────────────

void test_authorized_without_config_bundle() {
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"status\":\"authorized\"}");
    TEST_ASSERT_TRUE(r.ok); // status is recognized
    TEST_ASSERT_EQUAL(0, r.configUpdateVersion);
    TEST_ASSERT_EQUAL_STRING("", r.configUpdatePayload.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.configUpdateSignature.c_str());
}

// ── Unrecognized status (e.g. server-side "revoked") ────────────────────────

void test_unknown_status_is_not_ok() {
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"status\":\"revoked\"}");
    TEST_ASSERT_FALSE(r.ok); // ok only for pending / authorized
    TEST_ASSERT_EQUAL_STRING("revoked", r.status.c_str());
}

// ── Malformed input ─────────────────────────────────────────────────────────

void test_malformed_json_is_not_ok() {
    EnrollmentClient::Result r = EnrollmentClient::parse("{not valid json");
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("", r.status.c_str());
}

void test_empty_body_is_not_ok() {
    EnrollmentClient::Result r = EnrollmentClient::parse("");
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("", r.status.c_str());
}

void test_json_array_is_not_ok() {
    // The endpoint returns an object; an array (or any non-object) is rejected.
    EnrollmentClient::Result r = EnrollmentClient::parse("[{\"status\":\"pending\"}]");
    TEST_ASSERT_FALSE(r.ok);
}

void test_missing_status_is_not_ok() {
    EnrollmentClient::Result r = EnrollmentClient::parse(
        "{\"enrollment_code\":\"K7M2QP\"}");
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("K7M2QP", r.code.c_str()); // the code still parsed
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_first_contact_returns_secret_and_code);
    RUN_TEST(test_pending_poll_returns_code_without_secret);
    RUN_TEST(test_authorized_returns_signed_config_bundle);
    RUN_TEST(test_config_block_before_status_still_parses);
    RUN_TEST(test_object_nested_inside_config_does_not_displace_bundle_fields);
    RUN_TEST(test_bundle_fields_at_top_level_are_not_read_as_bundle);
    RUN_TEST(test_authorized_without_config_bundle);
    RUN_TEST(test_unknown_status_is_not_ok);
    RUN_TEST(test_malformed_json_is_not_ok);
    RUN_TEST(test_empty_body_is_not_ok);
    RUN_TEST(test_json_array_is_not_ok);
    RUN_TEST(test_missing_status_is_not_ok);
    return UNITY_END();
}
