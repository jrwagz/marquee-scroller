#include <unity.h>
#include "Arduino.h"
#include "TimeLib.h"
#include "timeStr.h"
#include "timeStr.cpp"

void setUp() {}
void tearDown() {}

void test_get_day_names() {
    TEST_ASSERT_EQUAL_STRING("Sunday", getDayName(1).c_str());
    TEST_ASSERT_EQUAL_STRING("Monday", getDayName(2).c_str());
    TEST_ASSERT_EQUAL_STRING("Tuesday", getDayName(3).c_str());
    TEST_ASSERT_EQUAL_STRING("Wednesday", getDayName(4).c_str());
    TEST_ASSERT_EQUAL_STRING("Thursday", getDayName(5).c_str());
    TEST_ASSERT_EQUAL_STRING("Friday", getDayName(6).c_str());
    TEST_ASSERT_EQUAL_STRING("Saturday", getDayName(7).c_str());
}

void test_get_day_name_zero_returns_empty() {
    TEST_ASSERT_EQUAL_STRING("", getDayName(0).c_str());
}

void test_get_day_name_eight_returns_empty() {
    TEST_ASSERT_EQUAL_STRING("", getDayName(8).c_str());
}

void test_get_month_names() {
    TEST_ASSERT_EQUAL_STRING("Jan", getMonthName(1).c_str());
    TEST_ASSERT_EQUAL_STRING("Feb", getMonthName(2).c_str());
    TEST_ASSERT_EQUAL_STRING("Mar", getMonthName(3).c_str());
    TEST_ASSERT_EQUAL_STRING("Apr", getMonthName(4).c_str());
    TEST_ASSERT_EQUAL_STRING("May", getMonthName(5).c_str());
    TEST_ASSERT_EQUAL_STRING("June", getMonthName(6).c_str());
    TEST_ASSERT_EQUAL_STRING("July", getMonthName(7).c_str());
    TEST_ASSERT_EQUAL_STRING("Aug", getMonthName(8).c_str());
    TEST_ASSERT_EQUAL_STRING("Sep", getMonthName(9).c_str());
    TEST_ASSERT_EQUAL_STRING("Oct", getMonthName(10).c_str());
    TEST_ASSERT_EQUAL_STRING("Nov", getMonthName(11).c_str());
    TEST_ASSERT_EQUAL_STRING("Dec", getMonthName(12).c_str());
}

void test_get_month_name_zero_returns_empty() {
    TEST_ASSERT_EQUAL_STRING("", getMonthName(0).c_str());
}

void test_get_month_name_thirteen_returns_empty() {
    TEST_ASSERT_EQUAL_STRING("", getMonthName(13).c_str());
}

void test_get_am_pm_returns() {
    TEST_ASSERT_EQUAL_STRING("PM", getAmPm(true).c_str());
    TEST_ASSERT_EQUAL_STRING("AM", getAmPm(false).c_str());
}

void test_space_pad_single_digit() {
    TEST_ASSERT_EQUAL_STRING(" 5", spacePad(5).c_str());
}

void test_space_pad_double_digit() {
    TEST_ASSERT_EQUAL_STRING("10", spacePad(10).c_str());
}

void test_space_pad_zero() {
    TEST_ASSERT_EQUAL_STRING(" 0", spacePad(0).c_str());
}

void test_zero_pad_single_digit() {
    TEST_ASSERT_EQUAL_STRING("05", zeroPad(5u).c_str());
}

void test_zero_pad_double_digit() {
    TEST_ASSERT_EQUAL_STRING("15", zeroPad(15u).c_str());
}

void test_zero_pad_zero() {
    TEST_ASSERT_EQUAL_STRING("00", zeroPad(0u).c_str());
}

void test_zero_pad_with_length_3() {
    TEST_ASSERT_EQUAL_STRING("007", zeroPad((uint32_t)7, (uint8_t)3).c_str());
}

void test_zero_pad_with_length_no_padding_needed() {
    TEST_ASSERT_EQUAL_STRING("42", zeroPad((uint32_t)42, (uint8_t)2).c_str());
}

void test_get_24hr_colon_min_midnight() {
    // epoch 0 = 00:00
    TEST_ASSERT_EQUAL_STRING("00:00", get24HrColonMin(0).c_str());
}

void test_get_24hr_colon_min_noon() {
    // 12 * 3600 = 43200
    TEST_ASSERT_EQUAL_STRING("12:00", get24HrColonMin(43200).c_str());
}

void test_get_24hr_colon_min_with_minutes() {
    // 9 hours + 5 minutes = 32700 seconds
    TEST_ASSERT_EQUAL_STRING("09:05", get24HrColonMin(32700).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_day_names);
    RUN_TEST(test_get_day_name_zero_returns_empty);
    RUN_TEST(test_get_day_name_eight_returns_empty);
    RUN_TEST(test_get_month_names);
    RUN_TEST(test_get_month_name_zero_returns_empty);
    RUN_TEST(test_get_month_name_thirteen_returns_empty);
    RUN_TEST(test_get_am_pm_returns);
    RUN_TEST(test_space_pad_single_digit);
    RUN_TEST(test_space_pad_double_digit);
    RUN_TEST(test_space_pad_zero);
    RUN_TEST(test_zero_pad_single_digit);
    RUN_TEST(test_zero_pad_double_digit);
    RUN_TEST(test_zero_pad_zero);
    RUN_TEST(test_zero_pad_with_length_3);
    RUN_TEST(test_zero_pad_with_length_no_padding_needed);
    RUN_TEST(test_get_24hr_colon_min_midnight);
    RUN_TEST(test_get_24hr_colon_min_noon);
    RUN_TEST(test_get_24hr_colon_min_with_minutes);
    return UNITY_END();
}
