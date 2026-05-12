#include <unity.h>
#include "Arduino.h"

#include "MdnsHelpers.cpp"

void setUp() {}
void tearDown() {}

// ── Happy path ──────────────────────────────────────────────────────────────

void test_simple_lowercase_passes_through() {
    TEST_ASSERT_EQUAL_STRING("kitchen", mdnsLabelFor("kitchen", "5fc8ad").c_str());
}

void test_uppercase_lowercased() {
    TEST_ASSERT_EQUAL_STRING("kitchen", mdnsLabelFor("KITCHEN", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("kitchen", mdnsLabelFor("Kitchen", "5fc8ad").c_str());
}

void test_spaces_become_hyphens() {
    TEST_ASSERT_EQUAL_STRING("kitchen-clock",
                             mdnsLabelFor("Kitchen Clock", "5fc8ad").c_str());
}

void test_multiple_spaces_collapse_to_single_hyphen() {
    TEST_ASSERT_EQUAL_STRING("kitchen-clock",
                             mdnsLabelFor("Kitchen   Clock", "5fc8ad").c_str());
}

void test_punctuation_collapses_to_single_hyphen() {
    // Each run of non-alphanumeric collapses to ONE hyphen — punctuation
    // touching alphanumerics on both sides splits the word. This is
    // documented behavior; users who want no hyphen mid-word should rename.
    TEST_ASSERT_EQUAL_STRING("dallan-s-clock",
                             mdnsLabelFor("Dallan's Clock!", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("foo-bar",
                             mdnsLabelFor("foo___bar", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("foo-bar",
                             mdnsLabelFor("foo.bar", "5fc8ad").c_str());
}

void test_digits_preserved() {
    TEST_ASSERT_EQUAL_STRING("clock2", mdnsLabelFor("Clock2", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("clock-2", mdnsLabelFor("Clock 2", "5fc8ad").c_str());
}


// ── Edge cases ──────────────────────────────────────────────────────────────

void test_leading_punctuation_does_not_produce_leading_hyphen() {
    TEST_ASSERT_EQUAL_STRING("clock", mdnsLabelFor("!!!Clock", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("clock", mdnsLabelFor("   Clock", "5fc8ad").c_str());
}

void test_trailing_punctuation_does_not_produce_trailing_hyphen() {
    TEST_ASSERT_EQUAL_STRING("clock", mdnsLabelFor("Clock!!!", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("clock", mdnsLabelFor("Clock   ", "5fc8ad").c_str());
}

void test_empty_input_falls_back_to_chip_id() {
    TEST_ASSERT_EQUAL_STRING("wagfam-5fc8ad",
                             mdnsLabelFor("", "5fc8ad").c_str());
}

void test_punctuation_only_input_falls_back_to_chip_id() {
    TEST_ASSERT_EQUAL_STRING("wagfam-5fc8ad",
                             mdnsLabelFor("!!!", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("wagfam-5fc8ad",
                             mdnsLabelFor("   ", "5fc8ad").c_str());
}

void test_long_input_truncated_to_63_chars() {
    String long_input = "";
    for (int i = 0; i < 80; i++) long_input += "a";
    String result = mdnsLabelFor(long_input, "5fc8ad");
    TEST_ASSERT_TRUE(result.length() <= 63);
    TEST_ASSERT_EQUAL_INT(63, result.length());
}

void test_truncation_does_not_leave_trailing_hyphen() {
    // 62 chars of 'a', then a space, then more text. Truncating at 63 would
    // land on the hyphen produced by the space — strip it.
    String src = "";
    for (int i = 0; i < 62; i++) src += "a";
    src += " bbb";
    String result = mdnsLabelFor(src, "5fc8ad");
    TEST_ASSERT_NOT_EQUAL('-', result[result.length() - 1]);
}


// ── Real-world device names ─────────────────────────────────────────────────

void test_realistic_device_names() {
    TEST_ASSERT_EQUAL_STRING("dallan-clock",
                             mdnsLabelFor("dallan-clock", "5fc8ad").c_str());
    TEST_ASSERT_EQUAL_STRING("grandma-s-kitchen",
                             mdnsLabelFor("Grandma's Kitchen", "abc123").c_str());
    TEST_ASSERT_EQUAL_STRING("front-door",
                             mdnsLabelFor("Front-Door", "abc123").c_str());
    TEST_ASSERT_EQUAL_STRING("clock-3",
                             mdnsLabelFor("Clock #3", "abc123").c_str());
}


int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_simple_lowercase_passes_through);
    RUN_TEST(test_uppercase_lowercased);
    RUN_TEST(test_spaces_become_hyphens);
    RUN_TEST(test_multiple_spaces_collapse_to_single_hyphen);
    RUN_TEST(test_punctuation_collapses_to_single_hyphen);
    RUN_TEST(test_digits_preserved);

    RUN_TEST(test_leading_punctuation_does_not_produce_leading_hyphen);
    RUN_TEST(test_trailing_punctuation_does_not_produce_trailing_hyphen);
    RUN_TEST(test_empty_input_falls_back_to_chip_id);
    RUN_TEST(test_punctuation_only_input_falls_back_to_chip_id);
    RUN_TEST(test_long_input_truncated_to_63_chars);
    RUN_TEST(test_truncation_does_not_leave_trailing_hyphen);

    RUN_TEST(test_realistic_device_names);

    return UNITY_END();
}
