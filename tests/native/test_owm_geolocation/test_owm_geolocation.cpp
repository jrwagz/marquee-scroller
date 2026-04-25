#include <unity.h>
#include <cmath>

// Stubs — found via -I tests/native/stubs
#include "Arduino.h"
#include "ArduinoJson.h"
#include "ESP8266WiFi.h"
#include "TimeLib.h"

// timeStr is included by OpenWeatherMapClient.cpp
#include "timeStr.cpp"

// EncodeUrlSpecialChars is declared extern in OpenWeatherMapClient.h but lives
// in marquee.ino; provide a stub so the file compiles
String EncodeUrlSpecialChars(const char* msg) {
    return String(msg ? msg : "");
}

#include "OpenWeatherMapClient.cpp"

void setUp() {}
void tearDown() {}

// ── setGeoLocation ───────────────────────────────────────────────────────────

void test_numeric_city_id_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("1234567"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_CITYID, c.getGeoLocationType());
}

void test_short_string_not_city_id_returns_nonzero() {
    // len=3 fails the (len > 3) check for city ID
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_NOT_EQUAL(0, c.setGeoLocation("123"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_UNKNOWN, c.getGeoLocationType());
}

void test_city_name_only_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("London"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_NAME, c.getGeoLocationType());
}

void test_city_name_with_country_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("Chicago,US"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_NAME, c.getGeoLocationType());
}

void test_city_name_with_state_and_country_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("Seattle,WA,US"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_NAME, c.getGeoLocationType());
}

void test_empty_string_returns_error() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_NOT_EQUAL(0, c.setGeoLocation(""));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_UNSET, c.getGeoLocationType());
}

void test_latlon_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("47.606,-122.332"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_LATLON, c.getGeoLocationType());
}

void test_latlon_negative_lat_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("-33.87,151.21"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_LATLON, c.getGeoLocationType());
}

void test_latlon_both_negative_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("-34.603,-58.381"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_LATLON, c.getGeoLocationType());
}

void test_latlon_not_confused_with_city_two_commas() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("Seattle,WA,US"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_NAME, c.getGeoLocationType());
}

void test_city_with_spaces_in_name_returns_0() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("New York,US"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_NAME, c.getGeoLocationType());
}

void test_weather_data_initially_invalid() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_FALSE(c.getWeatherDataValid());
}

void test_geolocation_accepts_city_with_hyphen() {
    OpenWeatherMapClient c("", false);
    TEST_ASSERT_EQUAL(0, c.setGeoLocation("Aix-en-Provence,FR"));
    TEST_ASSERT_EQUAL(OpenWeatherMapClient::LOC_NAME, c.getGeoLocationType());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_numeric_city_id_returns_0);
    RUN_TEST(test_short_string_not_city_id_returns_nonzero);
    RUN_TEST(test_city_name_only_returns_0);
    RUN_TEST(test_city_name_with_country_returns_0);
    RUN_TEST(test_city_name_with_state_and_country_returns_0);
    RUN_TEST(test_empty_string_returns_error);
    RUN_TEST(test_latlon_returns_0);
    RUN_TEST(test_latlon_negative_lat_returns_0);
    RUN_TEST(test_latlon_both_negative_returns_0);
    RUN_TEST(test_latlon_not_confused_with_city_two_commas);
    RUN_TEST(test_city_with_spaces_in_name_returns_0);
    RUN_TEST(test_weather_data_initially_invalid);
    RUN_TEST(test_geolocation_accepts_city_with_hyphen);
    return UNITY_END();
}
