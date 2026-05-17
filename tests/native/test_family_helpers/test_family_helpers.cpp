#include <unity.h>
#include "Arduino.h"

#include "FamilyHelpers.cpp"

void setUp() {}
void tearDown() {}

// ── isKnownFamily ───────────────────────────────────────────────────────────

void test_known_family_wagner() {
    TEST_ASSERT_TRUE(isKnownFamily("wagner"));
}

void test_known_family_butterfield() {
    TEST_ASSERT_TRUE(isKnownFamily("butterfield"));
}

void test_unknown_family_rejected() {
    TEST_ASSERT_FALSE(isKnownFamily("hogwarts"));
}

void test_empty_string_not_known() {
    TEST_ASSERT_FALSE(isKnownFamily(""));
}

void test_case_sensitivity_lowercase_only() {
    // Wire form is lowercase ASCII per the contract. Caller (marquee.ino's
    // applyServerConfig block) lowercases before checking; the helper itself
    // is strict so a caller forgetting to lowercase doesn't silently pass.
    TEST_ASSERT_FALSE(isKnownFamily("Wagner"));
    TEST_ASSERT_FALSE(isKnownFamily("WAGNER"));
    TEST_ASSERT_FALSE(isKnownFamily("Butterfield"));
}

// ── familyDisplay ───────────────────────────────────────────────────────────

void test_display_wagner_capitalized() {
    TEST_ASSERT_EQUAL_STRING("Wagner", familyDisplay("wagner").c_str());
}

void test_display_butterfield_capitalized() {
    TEST_ASSERT_EQUAL_STRING("Butterfield", familyDisplay("butterfield").c_str());
}

void test_display_empty_for_empty_wire() {
    TEST_ASSERT_EQUAL_STRING("", familyDisplay("").c_str());
}

void test_display_empty_for_unknown_value() {
    // Unknown wire value falls back to "" so the caller can show its
    // generic label (welcome message, app header) instead of rendering a
    // garbled string the user can't identify.
    TEST_ASSERT_EQUAL_STRING("", familyDisplay("hogwarts").c_str());
}

void test_display_empty_for_null_literal_string() {
    // Defensive: the JSON parser only sees string values, but if some
    // future code path passes through the literal "null" we still bail.
    TEST_ASSERT_EQUAL_STRING("", familyDisplay("null").c_str());
}

void test_display_empty_for_wrongcase_input() {
    // Display mapping is strict on case — same reason as isKnownFamily.
    TEST_ASSERT_EQUAL_STRING("", familyDisplay("Wagner").c_str());
    TEST_ASSERT_EQUAL_STRING("", familyDisplay("BUTTERFIELD").c_str());
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_known_family_wagner);
    RUN_TEST(test_known_family_butterfield);
    RUN_TEST(test_unknown_family_rejected);
    RUN_TEST(test_empty_string_not_known);
    RUN_TEST(test_case_sensitivity_lowercase_only);

    RUN_TEST(test_display_wagner_capitalized);
    RUN_TEST(test_display_butterfield_capitalized);
    RUN_TEST(test_display_empty_for_empty_wire);
    RUN_TEST(test_display_empty_for_unknown_value);
    RUN_TEST(test_display_empty_for_null_literal_string);
    RUN_TEST(test_display_empty_for_wrongcase_input);

    return UNITY_END();
}
