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

void test_extract_domain_no_scheme() {
    TEST_ASSERT_EQUAL_STRING("", extractDomain("example.com/path").c_str());
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
        "https://example.com/data.json"));
}

void test_trusted_domain_different_domain() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://evil.com/firmware.bin",
        "https://example.com/data.json"));
}

void test_trusted_domain_subdomain_mismatch() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://cdn.example.com/firmware.bin",
        "https://example.com/data.json"));
}

void test_trusted_domain_same_subdomain() {
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://raw.githubusercontent.com/user/repo/main/firmware.bin",
        "https://raw.githubusercontent.com/user/repo/main/data.json"));
}

void test_trusted_domain_empty_firmware_url() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "",
        "https://example.com/data.json"));
}

void test_trusted_domain_empty_calendar_url() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain(
        "http://example.com/firmware.bin",
        ""));
}

void test_trusted_domain_both_empty() {
    TEST_ASSERT_FALSE(isTrustedFirmwareDomain("", ""));
}

void test_trusted_domain_different_port_same_host() {
    TEST_ASSERT_TRUE(isTrustedFirmwareDomain(
        "http://example.com:8080/firmware.bin",
        "https://example.com:443/data.json"));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_protected_path_matches_config);
    RUN_TEST(test_protected_path_matches_ota_pending);
    RUN_TEST(test_protected_path_rejects_other_file);
    RUN_TEST(test_protected_path_rejects_empty);
    RUN_TEST(test_protected_path_rejects_substring);
    RUN_TEST(test_protected_path_rejects_prefix);

    RUN_TEST(test_extract_domain_https);
    RUN_TEST(test_extract_domain_http);
    RUN_TEST(test_extract_domain_with_port);
    RUN_TEST(test_extract_domain_no_path);
    RUN_TEST(test_extract_domain_empty_string);
    RUN_TEST(test_extract_domain_no_scheme);
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

    return UNITY_END();
}
