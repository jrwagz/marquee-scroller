// Native unit test for `isOriginAllowed`, the pure helper that gates which
// HTTP `Origin` headers are echoed back in `Access-Control-Allow-Origin`
// (issue: jrwagz/wagfam-server upgrade-tool work).
//
// We test the helper here on the host (no ESP8266 hardware needed) because
// it's a tight little string-comparison routine that's easy to get wrong:
//   - the wildcard-vs-exact distinction matters (browsers reject mismatched
//     ACAO when credentials are involved)
//   - whitespace around comma-separated list entries is human-friendly to
//     allow but easy to mishandle
//   - empty inputs must fail closed
// A bug here silently breaks every cross-origin call from the wagfam-server
// upgrade page, which is hard to debug without unit coverage.

#include <unity.h>
#include "Arduino.h"

// Include the pure helper directly. It has no AsyncWebServer dependency,
// which is why it's split out from CorsSupport.cpp — that file pulls in
// ESPAsyncWebServer.h for the runtime header-attaching part, and we don't
// want to stub that just to test the string-matching logic.
#include "OriginAllowlist.cpp"

void setUp() {}
void tearDown() {}

// ── Match cases ─────────────────────────────────────────────────────────────

void test_exact_match_single_entry() {
    TEST_ASSERT_TRUE(isOriginAllowed(
        "https://wagfam-server.azurewebsites.net",
        "https://wagfam-server.azurewebsites.net"));
}

void test_match_first_entry_in_list() {
    TEST_ASSERT_TRUE(isOriginAllowed(
        "https://wagfam-server.azurewebsites.net",
        "https://wagfam-server.azurewebsites.net,http://localhost:8000"));
}

void test_match_last_entry_in_list() {
    TEST_ASSERT_TRUE(isOriginAllowed(
        "http://localhost:8000",
        "https://wagfam-server.azurewebsites.net,http://localhost:8000"));
}

void test_match_middle_entry() {
    TEST_ASSERT_TRUE(isOriginAllowed(
        "http://wagfam-server.azurewebsites.net",
        "https://wagfam-server.azurewebsites.net,"
        "http://wagfam-server.azurewebsites.net,"
        "http://localhost:8000"));
}

void test_whitespace_around_entries_tolerated() {
    // Build flag strings sometimes get padded for readability — make sure
    // we don't fail because of a stray space the editor inserted.
    TEST_ASSERT_TRUE(isOriginAllowed(
        "https://wagfam-server.azurewebsites.net",
        " https://wagfam-server.azurewebsites.net , http://localhost:8000 "));
}

// ── Reject cases ────────────────────────────────────────────────────────────

void test_rejects_unknown_origin() {
    TEST_ASSERT_FALSE(isOriginAllowed(
        "https://evil.example.com",
        "https://wagfam-server.azurewebsites.net,http://localhost:8000"));
}

void test_rejects_substring_match() {
    // "https://wagfam-server.azurewebsites.net" is in the allowlist; an
    // attacker page on "evil.com/wagfam-server.azurewebsites.net" must NOT
    // match it just because the path contains the allowed string.
    TEST_ASSERT_FALSE(isOriginAllowed(
        "https://evil.com/wagfam-server.azurewebsites.net",
        "https://wagfam-server.azurewebsites.net"));
}

void test_rejects_prefix_match() {
    // "https://wagfam-server.azurewebsites.net.evil.com" shares a prefix
    // but is a different host. Must not match.
    TEST_ASSERT_FALSE(isOriginAllowed(
        "https://wagfam-server.azurewebsites.net.evil.com",
        "https://wagfam-server.azurewebsites.net"));
}

void test_rejects_scheme_mismatch() {
    // http:// and https:// are different origins for CORS purposes.
    TEST_ASSERT_FALSE(isOriginAllowed(
        "http://wagfam-server.azurewebsites.net",
        "https://wagfam-server.azurewebsites.net"));
}

void test_rejects_port_mismatch() {
    TEST_ASSERT_FALSE(isOriginAllowed(
        "http://localhost:9000",
        "http://localhost:8000"));
}

// ── Empty / malformed inputs ────────────────────────────────────────────────

void test_empty_origin_fails_closed() {
    // Browsers send no Origin for same-origin requests; we treat that as
    // "don't attach CORS headers" rather than "allow." Same-origin requests
    // don't need CORS anyway.
    TEST_ASSERT_FALSE(isOriginAllowed(
        "",
        "https://wagfam-server.azurewebsites.net"));
}

void test_empty_allowlist_fails_closed() {
    // Operators set the allowlist to "" to disable cross-origin access.
    // No origin should match.
    TEST_ASSERT_FALSE(isOriginAllowed(
        "https://wagfam-server.azurewebsites.net",
        ""));
}

void test_null_allowlist_fails_closed() {
    TEST_ASSERT_FALSE(isOriginAllowed(
        "https://wagfam-server.azurewebsites.net",
        nullptr));
}

void test_allowlist_of_only_commas() {
    // Pathological but possible: e.g., `,,,`. None of the empty entries
    // should match.
    TEST_ASSERT_FALSE(isOriginAllowed(
        "https://wagfam-server.azurewebsites.net",
        ",,,"));
}

void test_trailing_comma_doesnt_break_earlier_match() {
    TEST_ASSERT_TRUE(isOriginAllowed(
        "http://localhost:8000",
        "http://localhost:8000,"));
}

// ── Entry point ─────────────────────────────────────────────────────────────

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_exact_match_single_entry);
    RUN_TEST(test_match_first_entry_in_list);
    RUN_TEST(test_match_last_entry_in_list);
    RUN_TEST(test_match_middle_entry);
    RUN_TEST(test_whitespace_around_entries_tolerated);
    RUN_TEST(test_rejects_unknown_origin);
    RUN_TEST(test_rejects_substring_match);
    RUN_TEST(test_rejects_prefix_match);
    RUN_TEST(test_rejects_scheme_mismatch);
    RUN_TEST(test_rejects_port_mismatch);
    RUN_TEST(test_empty_origin_fails_closed);
    RUN_TEST(test_empty_allowlist_fails_closed);
    RUN_TEST(test_null_allowlist_fails_closed);
    RUN_TEST(test_allowlist_of_only_commas);
    RUN_TEST(test_trailing_comma_doesnt_break_earlier_match);
    return UNITY_END();
}
