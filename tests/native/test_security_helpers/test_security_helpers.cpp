#include <unity.h>
#include "Arduino.h"

#include "SecurityHelpers.cpp"

void setUp() {}
void tearDown() {}

// ── isProtectedPath ──────────────────────────────────────────────────────────

void test_protected_path_matches_config() {
    TEST_ASSERT_TRUE(isProtectedPath("/conf.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_protected_path_matches_ota_pending() {
    TEST_ASSERT_TRUE(isProtectedPath("/ota_pending.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_protected_path_rejects_other_file() {
    TEST_ASSERT_FALSE(isProtectedPath("/test.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_protected_path_rejects_empty() {
    TEST_ASSERT_FALSE(isProtectedPath("", "/conf.txt", "/ota_pending.txt"));
}

void test_protected_path_rejects_substring() {
    TEST_ASSERT_FALSE(isProtectedPath("/conf.txt.bak", "/conf.txt", "/ota_pending.txt"));
}

void test_protected_path_rejects_prefix() {
    TEST_ASSERT_FALSE(isProtectedPath("/conf", "/conf.txt", "/ota_pending.txt"));
}

// ── isValidUploadPath ────────────────────────────────────────────────────────

void test_upload_path_accepts_simple() {
    TEST_ASSERT_TRUE(isValidUploadPath("/spa/index.html", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_accepts_nested() {
    TEST_ASSERT_TRUE(isValidUploadPath("/spa/assets/index.js.gz", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_accepts_root_file() {
    TEST_ASSERT_TRUE(isValidUploadPath("/foo.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_null() {
    TEST_ASSERT_FALSE(isValidUploadPath(nullptr, "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_empty() {
    TEST_ASSERT_FALSE(isValidUploadPath("", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_no_leading_slash() {
    TEST_ASSERT_FALSE(isValidUploadPath("foo.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_trailing_slash() {
    TEST_ASSERT_FALSE(isValidUploadPath("/spa/", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_traversal_at_start() {
    TEST_ASSERT_FALSE(isValidUploadPath("/../etc/passwd", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_traversal_in_middle() {
    TEST_ASSERT_FALSE(isValidUploadPath("/spa/../conf.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_traversal_at_end() {
    TEST_ASSERT_FALSE(isValidUploadPath("/spa/..", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_accepts_double_dot_in_filename() {
    // "..bar" or "foo..bar" is not a traversal — only segment "/.." is.
    TEST_ASSERT_TRUE(isValidUploadPath("/foo..bar", "/conf.txt", "/ota_pending.txt"));
    TEST_ASSERT_TRUE(isValidUploadPath("/spa/..hidden", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_double_slash() {
    TEST_ASSERT_FALSE(isValidUploadPath("/spa//index.html", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_backslash() {
    TEST_ASSERT_FALSE(isValidUploadPath("/spa\\index.html", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_protected_config() {
    TEST_ASSERT_FALSE(isValidUploadPath("/conf.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_protected_ota() {
    TEST_ASSERT_FALSE(isValidUploadPath("/ota_pending.txt", "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_rejects_too_long() {
    // 128 chars including leading slash — too long.
    char buf[130];
    buf[0] = '/';
    for (int i = 1; i < 128; i++) buf[i] = 'a';
    buf[128] = '\0';
    TEST_ASSERT_FALSE(isValidUploadPath(buf, "/conf.txt", "/ota_pending.txt"));
}

void test_upload_path_accepts_at_length_limit() {
    // 127 chars total — accepted.
    char buf[130];
    buf[0] = '/';
    for (int i = 1; i < 127; i++) buf[i] = 'a';
    buf[127] = '\0';
    TEST_ASSERT_TRUE(isValidUploadPath(buf, "/conf.txt", "/ota_pending.txt"));
}

// ── extractDomain ────────────────────────────────────────────────────────────

void test_extract_domain_https() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("https://example.com/path/to/file").c_str());
}

void test_extract_domain_http() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("http://example.com/file.bin").c_str());
}

void test_extract_domain_with_port() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("http://example.com:8080/file").c_str());
}

void test_extract_domain_no_path() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("https://example.com").c_str());
}

void test_extract_domain_empty_string() {
    TEST_ASSERT_EQUAL_STRING("", extractDomain("").c_str());
}

void test_extract_domain_no_scheme_with_path() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("example.com/path").c_str());
}

void test_extract_domain_no_scheme_bare_host() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("example.com").c_str());
}

void test_extract_domain_no_scheme_with_port() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("example.com:8080/file").c_str());
}

void test_extract_domain_strips_userinfo() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("https://user:pass@example.com/path").c_str());
}

void test_extract_domain_strips_query() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("example.com?foo=bar").c_str());
}

void test_extract_domain_strips_fragment() {
    TEST_ASSERT_EQUAL_STRING("example.com",
        extractDomain("example.com#section").c_str());
}

void test_extract_domain_subdomain() {
    TEST_ASSERT_EQUAL_STRING("raw.githubusercontent.com",
        extractDomain("https://raw.githubusercontent.com/user/repo/main/file.json").c_str());
}

void test_extract_domain_ip_address() {
    TEST_ASSERT_EQUAL_STRING("192.168.1.1",
        extractDomain("http://192.168.1.1/config").c_str());
}

void test_extract_domain_ip_with_port() {
    TEST_ASSERT_EQUAL_STRING("192.168.1.1",
        extractDomain("http://192.168.1.1:8080/config").c_str());
}

// ── isTrustedFirmwareDomain ──────────────────────────────────────────────────

void test_trusted_domain_same_domain() {
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://example.com/firmware.bin",
        "https://example.com/data.json", ""));
}

void test_trusted_domain_different_domain() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://evil.com/firmware.bin",
        "https://example.com/data.json", ""));
}

void test_trusted_domain_subdomain_mismatch() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://cdn.example.com/firmware.bin",
        "https://example.com/data.json", ""));
}

void test_trusted_domain_same_subdomain() {
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://raw.githubusercontent.com/user/repo/main/firmware.bin",
        "https://raw.githubusercontent.com/user/repo/main/data.json", ""));
}

void test_trusted_domain_empty_firmware_url() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "",
        "https://example.com/data.json", ""));
}

void test_trusted_domain_empty_calendar_url() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://example.com/firmware.bin",
        "", ""));
}

void test_trusted_domain_both_empty() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain("", "", ""));
}

void test_trusted_domain_different_port_same_host() {
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://example.com:8080/firmware.bin",
        "https://example.com:443/data.json", ""));
}

// ── isInTrustedDomainList ────────────────────────────────────────────────────

void test_allowlist_single_entry_match() {
    TEST_ASSERT_TRUE(isInTrustedDomainList("cdn.example.com", "cdn.example.com"));
}

void test_allowlist_single_entry_miss() {
    TEST_ASSERT_FALSE(isInTrustedDomainList("evil.com", "cdn.example.com"));
}

void test_allowlist_multi_entry_first() {
    TEST_ASSERT_TRUE(isInTrustedDomainList(
        "cdn.example.com", "cdn.example.com,releases.example.com,backup.example.com"));
}

void test_allowlist_multi_entry_middle() {
    TEST_ASSERT_TRUE(isInTrustedDomainList(
        "releases.example.com", "cdn.example.com,releases.example.com,backup.example.com"));
}

void test_allowlist_multi_entry_last() {
    TEST_ASSERT_TRUE(isInTrustedDomainList(
        "backup.example.com", "cdn.example.com,releases.example.com,backup.example.com"));
}

void test_allowlist_with_whitespace() {
    TEST_ASSERT_TRUE(isInTrustedDomainList(
        "releases.example.com", " cdn.example.com , releases.example.com , backup.example.com "));
}

void test_allowlist_empty_string() {
    TEST_ASSERT_FALSE(isInTrustedDomainList("any.com", ""));
}

void test_allowlist_null() {
    TEST_ASSERT_FALSE(isInTrustedDomainList("any.com", nullptr));
}

void test_allowlist_no_substring_match() {
    // 'cdn.example.com' should not match 'example.com' as a substring
    TEST_ASSERT_FALSE(isInTrustedDomainList("example.com", "cdn.example.com"));
}

void test_allowlist_no_prefix_match() {
    TEST_ASSERT_FALSE(isInTrustedDomainList("cdn", "cdn.example.com"));
}

void test_allowlist_empty_query_domain() {
    TEST_ASSERT_FALSE(isInTrustedDomainList("", "cdn.example.com"));
}

// ── isTrustedFirmwareDomain with allowlist ───────────────────────────────────

void test_trusted_allowlist_overrides_calendar_mismatch() {
    // Calendar is on github.com, firmware is on a different (allowlisted) CDN.
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://cdn.example.com/firmware.bin",
        "https://github.com/user/repo/data.json",
        "cdn.example.com"));
}

void test_trusted_allowlist_does_not_match_unlisted() {
    // Firmware host not in allowlist AND not equal to calendar host → reject.
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://evil.com/firmware.bin",
        "https://github.com/user/repo/data.json",
        "cdn.example.com,releases.example.com"));
}

void test_trusted_allowlist_falls_back_to_calendar_match() {
    // Firmware not in allowlist, but matches calendar host → accept.
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://example.com/firmware.bin",
        "https://example.com/data.json",
        "cdn.other.com"));
}

void test_trusted_allowlist_empty_falls_back() {
    // Empty allowlist → behaves like the original calendar-domain-match check.
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://example.com/firmware.bin",
        "https://example.com/data.json",
        ""));
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://other.com/firmware.bin",
        "https://example.com/data.json",
        ""));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_protected_path_matches_config);
    RUN_TEST(test_protected_path_matches_ota_pending);
    RUN_TEST(test_protected_path_rejects_other_file);
    RUN_TEST(test_protected_path_rejects_empty);
    RUN_TEST(test_protected_path_rejects_substring);
    RUN_TEST(test_protected_path_rejects_prefix);

    RUN_TEST(test_upload_path_accepts_simple);
    RUN_TEST(test_upload_path_accepts_nested);
    RUN_TEST(test_upload_path_accepts_root_file);
    RUN_TEST(test_upload_path_rejects_null);
    RUN_TEST(test_upload_path_rejects_empty);
    RUN_TEST(test_upload_path_rejects_no_leading_slash);
    RUN_TEST(test_upload_path_rejects_trailing_slash);
    RUN_TEST(test_upload_path_rejects_traversal_at_start);
    RUN_TEST(test_upload_path_rejects_traversal_in_middle);
    RUN_TEST(test_upload_path_rejects_traversal_at_end);
    RUN_TEST(test_upload_path_accepts_double_dot_in_filename);
    RUN_TEST(test_upload_path_rejects_double_slash);
    RUN_TEST(test_upload_path_rejects_backslash);
    RUN_TEST(test_upload_path_rejects_protected_config);
    RUN_TEST(test_upload_path_rejects_protected_ota);
    RUN_TEST(test_upload_path_rejects_too_long);
    RUN_TEST(test_upload_path_accepts_at_length_limit);

    RUN_TEST(test_extract_domain_https);
    RUN_TEST(test_extract_domain_http);
    RUN_TEST(test_extract_domain_with_port);
    RUN_TEST(test_extract_domain_no_path);
    RUN_TEST(test_extract_domain_empty_string);
    RUN_TEST(test_extract_domain_no_scheme_with_path);
    RUN_TEST(test_extract_domain_no_scheme_bare_host);
    RUN_TEST(test_extract_domain_no_scheme_with_port);
    RUN_TEST(test_extract_domain_strips_userinfo);
    RUN_TEST(test_extract_domain_strips_query);
    RUN_TEST(test_extract_domain_strips_fragment);
    RUN_TEST(test_extract_domain_subdomain);
    RUN_TEST(test_extract_domain_ip_address);
    RUN_TEST(test_extract_domain_ip_with_port);

    RUN_TEST(test_trusted_domain_same_domain);
    RUN_TEST(test_trusted_domain_different_domain);
    RUN_TEST(test_trusted_domain_subdomain_mismatch);
    RUN_TEST(test_trusted_domain_same_subdomain);
    RUN_TEST(test_trusted_domain_empty_firmware_url);
    RUN_TEST(test_trusted_domain_empty_calendar_url);
    RUN_TEST(test_trusted_domain_both_empty);
    RUN_TEST(test_trusted_domain_different_port_same_host);

    RUN_TEST(test_allowlist_single_entry_match);
    RUN_TEST(test_allowlist_single_entry_miss);
    RUN_TEST(test_allowlist_multi_entry_first);
    RUN_TEST(test_allowlist_multi_entry_middle);
    RUN_TEST(test_allowlist_multi_entry_last);
    RUN_TEST(test_allowlist_with_whitespace);
    RUN_TEST(test_allowlist_empty_string);
    RUN_TEST(test_allowlist_null);
    RUN_TEST(test_allowlist_no_substring_match);
    RUN_TEST(test_allowlist_no_prefix_match);
    RUN_TEST(test_allowlist_empty_query_domain);

    RUN_TEST(test_trusted_allowlist_overrides_calendar_mismatch);
    RUN_TEST(test_trusted_allowlist_does_not_match_unlisted);
    RUN_TEST(test_trusted_allowlist_falls_back_to_calendar_match);
    RUN_TEST(test_trusted_allowlist_empty_falls_back);

    return UNITY_END();
}
