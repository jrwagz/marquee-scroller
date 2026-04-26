#include <unity.h>

// Stubs must be first — they're found via -I tests/native/stubs
#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "ESP8266HTTPClient.h"
#include "WiFiClientSecureBearSSL.h"

// Library source — found via -I lib/json-streaming-parser
#include "JsonStreamingParser.cpp"
#include "JsonListener.cpp"

// Production source — found via -I marquee
#include "WagFamBdayClient.cpp"

void setUp() {}
void tearDown() {}

static void feed_json(WagFamBdayClient& client, const char* json) {
    JsonStreamingParser parser;
    parser.setListener(&client);
    parser.reset();
    for (const char* p = json; *p; p++) {
        parser.parse(*p);
    }
}

// ── getNumMessages ──────────────────────────────────────────────────────────

void test_empty_array_gives_zero_messages() {
    WagFamBdayClient client("", "");
    feed_json(client, "[]");
    TEST_ASSERT_EQUAL(0, client.getNumMessages());
}

void test_single_message_parsed() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"message\": \"Happy Birthday!\"}]");
    TEST_ASSERT_EQUAL(1, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("Happy Birthday!", client.getMessage(0).c_str());
}

void test_two_messages_parsed_in_order() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"message\":\"First\"},{\"message\":\"Second\"}]");
    TEST_ASSERT_EQUAL(2, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("First", client.getMessage(0).c_str());
    TEST_ASSERT_EQUAL_STRING("Second", client.getMessage(1).c_str());
}

void test_max_10_messages_enforced() {
    WagFamBdayClient client("", "");
    feed_json(client,
        "[{\"message\":\"M1\"},{\"message\":\"M2\"},{\"message\":\"M3\"},"
        "{\"message\":\"M4\"},{\"message\":\"M5\"},{\"message\":\"M6\"},"
        "{\"message\":\"M7\"},{\"message\":\"M8\"},{\"message\":\"M9\"},"
        "{\"message\":\"M10\"},{\"message\":\"M11 - ignored\"}]");
    TEST_ASSERT_EQUAL(10, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("M1", client.getMessage(0).c_str());
    TEST_ASSERT_EQUAL_STRING("M2", client.getMessage(1).c_str());
    TEST_ASSERT_EQUAL_STRING("M3", client.getMessage(2).c_str());
    TEST_ASSERT_EQUAL_STRING("M4", client.getMessage(3).c_str());
    TEST_ASSERT_EQUAL_STRING("M5", client.getMessage(4).c_str());
    TEST_ASSERT_EQUAL_STRING("M6", client.getMessage(5).c_str());
    TEST_ASSERT_EQUAL_STRING("M7", client.getMessage(6).c_str());
    TEST_ASSERT_EQUAL_STRING("M8", client.getMessage(7).c_str());
    TEST_ASSERT_EQUAL_STRING("M9", client.getMessage(8).c_str());
    TEST_ASSERT_EQUAL_STRING("M10", client.getMessage(9).c_str());
}

void test_config_block_does_not_count_as_message() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"eventToday\":\"1\"}},{\"message\":\"X\"}]");
    TEST_ASSERT_EQUAL(1, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("X", client.getMessage(0).c_str());
}

void test_config_only_gives_zero_messages() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"dataSourceUrl\":\"http://new.example.com\",\"apiKey\":\"newkey\"}}]");
    TEST_ASSERT_EQUAL(0, client.getNumMessages());
}

void test_second_parse_resets_message_counter() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"message\":\"First parse\"}]");
    TEST_ASSERT_EQUAL(1, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("First parse", client.getMessage(0).c_str());
    feed_json(client, "[{\"message\":\"A\"},{\"message\":\"B\"}]");
    TEST_ASSERT_EQUAL(2, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("A", client.getMessage(0).c_str());
    TEST_ASSERT_EQUAL_STRING("B", client.getMessage(1).c_str());
}

void test_whitespace_in_json_handled() {
    WagFamBdayClient client("", "");
    feed_json(client, "[\n  {\n    \"message\": \"Spaced Out\"\n  }\n]");
    TEST_ASSERT_EQUAL(1, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("Spaced Out", client.getMessage(0).c_str());
}

// ── cleanText ───────────────────────────────────────────────────────────────

void test_clean_text_ascii_passthrough() {
    WagFamBdayClient client("", "");
    TEST_ASSERT_EQUAL_STRING("Hello World!", client.cleanText("Hello World!").c_str());
}

void test_clean_text_smart_left_double_quote() {
    // U+201C LEFT DOUBLE QUOTATION MARK = \xe2\x80\x9c
    WagFamBdayClient client("", "");
    TEST_ASSERT_EQUAL_STRING("\"Hello\"", client.cleanText("\xe2\x80\x9cHello\xe2\x80\x9d").c_str());
}

void test_clean_text_eacute_replaced() {
    // U+00E9 é = \xc3\xa9
    WagFamBdayClient client("", "");
    TEST_ASSERT_EQUAL_STRING("eleve", client.cleanText("\xc3\xa9l\xc3\xa8ve").c_str());
}

void test_clean_text_ellipsis_replaced() {
    // U+2026 … = \xe2\x80\xa6
    WagFamBdayClient client("", "");
    TEST_ASSERT_EQUAL_STRING("Wait...OK", client.cleanText("Wait\xe2\x80\xa6OK").c_str());
}

void test_clean_text_en_dash_replaced() {
    // U+2013 – = \xe2\x80\x93
    WagFamBdayClient client("", "");
    TEST_ASSERT_EQUAL_STRING("Jan - Feb", client.cleanText("Jan \xe2\x80\x93 Feb").c_str());
}

void test_clean_text_cedilla_replaced() {
    // U+00E7 ç = \xc3\xa7
    WagFamBdayClient client("", "");
    TEST_ASSERT_EQUAL_STRING("garcon", client.cleanText("gar\xc3\xa7on").c_str());
}

void test_clean_text_multiple_replacements() {
    // Combination: é, … in one string
    WagFamBdayClient client("", "");
    String result = client.cleanText("\xc3\xa9l\xc3\xa8ve\xe2\x80\xa6wait");
    TEST_ASSERT_EQUAL_STRING("eleve...wait", result.c_str());
}

// ── config block parsing ─────────────────────────────────────────────────────

void test_config_event_today_true() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"eventToday\":\"1\"}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.eventTodayValid);
    TEST_ASSERT_TRUE(cfg.eventToday);
}

void test_config_event_today_false() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"eventToday\":\"0\"}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.eventTodayValid);
    TEST_ASSERT_FALSE(cfg.eventToday);
}

void test_config_data_source_url_parsed() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"dataSourceUrl\":\"https://example.com/data.json\"}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.dataSourceUrlValid);
    TEST_ASSERT_EQUAL_STRING("https://example.com/data.json", cfg.dataSourceUrl.c_str());
}

void test_config_api_key_parsed() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"apiKey\":\"secret-token-123\"}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.apiKeyValid);
    TEST_ASSERT_EQUAL_STRING("secret-token-123", cfg.apiKey.c_str());
}

void test_config_latest_version_parsed() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"latestVersion\":\"3.09.0-wagfam\"}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.latestVersionValid);
    TEST_ASSERT_EQUAL_STRING("3.09.0-wagfam", cfg.latestVersion.c_str());
}

void test_config_firmware_url_parsed() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"firmwareUrl\":\"http://example.com/fw.bin\"}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.firmwareUrlValid);
    TEST_ASSERT_EQUAL_STRING("http://example.com/fw.bin", cfg.firmwareUrl.c_str());
}

void test_config_multiple_fields_in_one_block() {
    WagFamBdayClient client("", "");
    feed_json(client,
        "[{\"config\":{"
        "\"eventToday\":\"1\","
        "\"latestVersion\":\"3.09.0-wagfam\","
        "\"firmwareUrl\":\"http://example.com/fw.bin\""
        "}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.eventTodayValid);
    TEST_ASSERT_TRUE(cfg.eventToday);
    TEST_ASSERT_TRUE(cfg.latestVersionValid);
    TEST_ASSERT_EQUAL_STRING("3.09.0-wagfam", cfg.latestVersion.c_str());
    TEST_ASSERT_TRUE(cfg.firmwareUrlValid);
    TEST_ASSERT_EQUAL_STRING("http://example.com/fw.bin", cfg.firmwareUrl.c_str());
}

void test_config_absent_fields_not_valid() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"config\":{\"eventToday\":\"1\"}}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_FALSE(cfg.latestVersionValid);
    TEST_ASSERT_FALSE(cfg.firmwareUrlValid);
    TEST_ASSERT_FALSE(cfg.dataSourceUrlValid);
    TEST_ASSERT_FALSE(cfg.apiKeyValid);
}

void test_config_with_messages_both_parsed() {
    WagFamBdayClient client("", "");
    feed_json(client,
        "[{\"config\":{\"eventToday\":\"1\"}},"
        "{\"message\":\"Happy Birthday!\"}]");
    auto cfg = client.getLastConfig();
    TEST_ASSERT_TRUE(cfg.eventTodayValid);
    TEST_ASSERT_TRUE(cfg.eventToday);
    TEST_ASSERT_EQUAL(1, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("Happy Birthday!", client.getMessage(0).c_str());
}

// ── getMessage bounds check (SEC-14) ────────────────────────────────────────

void test_get_message_negative_index_returns_empty() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"message\":\"Hello\"}]");
    TEST_ASSERT_EQUAL_STRING("", client.getMessage(-1).c_str());
}

void test_get_message_out_of_bounds_returns_empty() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"message\":\"Hello\"}]");
    TEST_ASSERT_EQUAL(1, client.getNumMessages());
    TEST_ASSERT_EQUAL_STRING("", client.getMessage(1).c_str());
    TEST_ASSERT_EQUAL_STRING("", client.getMessage(10).c_str());
    TEST_ASSERT_EQUAL_STRING("", client.getMessage(99).c_str());
}

void test_get_message_valid_index_still_works() {
    WagFamBdayClient client("", "");
    feed_json(client, "[{\"message\":\"Hello\"},{\"message\":\"World\"}]");
    TEST_ASSERT_EQUAL_STRING("Hello", client.getMessage(0).c_str());
    TEST_ASSERT_EQUAL_STRING("World", client.getMessage(1).c_str());
}

// ── cleanText ───────────────────────────────────────────────────────────────

void test_clean_text_smart_single_right_quote() {
    // U+2019 RIGHT SINGLE QUOTATION MARK = \xe2\x80\x99
    WagFamBdayClient client("", "");
    TEST_ASSERT_EQUAL_STRING("it's", client.cleanText("it\xe2\x80\x99s").c_str());
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_empty_array_gives_zero_messages);
    RUN_TEST(test_single_message_parsed);
    RUN_TEST(test_two_messages_parsed_in_order);
    RUN_TEST(test_max_10_messages_enforced);
    RUN_TEST(test_config_block_does_not_count_as_message);
    RUN_TEST(test_config_only_gives_zero_messages);
    RUN_TEST(test_second_parse_resets_message_counter);
    RUN_TEST(test_whitespace_in_json_handled);

    RUN_TEST(test_config_event_today_true);
    RUN_TEST(test_config_event_today_false);
    RUN_TEST(test_config_data_source_url_parsed);
    RUN_TEST(test_config_api_key_parsed);
    RUN_TEST(test_config_latest_version_parsed);
    RUN_TEST(test_config_firmware_url_parsed);
    RUN_TEST(test_config_multiple_fields_in_one_block);
    RUN_TEST(test_config_absent_fields_not_valid);
    RUN_TEST(test_config_with_messages_both_parsed);

    RUN_TEST(test_get_message_negative_index_returns_empty);
    RUN_TEST(test_get_message_out_of_bounds_returns_empty);
    RUN_TEST(test_get_message_valid_index_still_works);

    RUN_TEST(test_clean_text_ascii_passthrough);
    RUN_TEST(test_clean_text_smart_left_double_quote);
    RUN_TEST(test_clean_text_eacute_replaced);
    RUN_TEST(test_clean_text_ellipsis_replaced);
    RUN_TEST(test_clean_text_en_dash_replaced);
    RUN_TEST(test_clean_text_cedilla_replaced);
    RUN_TEST(test_clean_text_multiple_replacements);
    RUN_TEST(test_clean_text_smart_single_right_quote);

    return UNITY_END();
}
