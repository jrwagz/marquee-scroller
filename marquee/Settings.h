/** The MIT License (MIT)

Copyright (c) 2018 David Payne

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/******************************************************************************
 * This is designed for the Wemos D1 ESP8266
 * Wemos D1 Mini:  https://amzn.to/2qLyKJd
 * MAX7219 Dot Matrix Module 4-in-1 Display For Arduino
 * Matrix Display:  https://amzn.to/2HtnQlD
 ******************************************************************************/
/******************************************************************************
 * NOTE: The settings here are the default settings for the first loading.
 * After loading you will manage changes to the settings via the Web Interface.
 * If you want to change settings again in the settings.h, you will need to
 * erase the file system on the Wemos or use the “Reset Settings” option in
 * the Web Interface.
 ******************************************************************************/

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <DNSServer.h>
#include <ESPAsyncWiFiManager.h> // --> https://github.com/alanswx/ESPAsyncWiFiManager (async-native fork of tzapu/WiFiManager)
#include <ESP8266httpUpdate.h>
#include "FS.h"
#include <LittleFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h> // --> https://github.com/adafruit/Adafruit-GFX-Library
#include <Max72xxPanel.h> // --> https://github.com/markruys/arduino-Max72xxPanel
#include <pgmspace.h>
#include "OpenWeatherMapClient.h"
#include "timeNTP.h"
#include "timeStr.h"
#include "WagFamBdayClient.h"

//******************************
// Start Settings
//******************************


String WAGFAM_DATA_URL = ""; // URL to Pull WagFam Calendar Data from
String WAGFAM_API_KEY = ""; // Authorization token to use to authenticate to access the DATA_URL, only used if provided
boolean WAGFAM_EVENT_TODAY = false; // Whether or not an event is happening today

// SEC-03: Compile-time allowlist of domains that are *always* trusted as firmware
// sources, regardless of the calendar URL's domain. Comma-separated list of bare
// hostnames (no scheme, no port, no path). The runtime check (isTrustedFirmwareDomain)
// also still accepts a same-domain match against the active WAGFAM_DATA_URL — this
// macro just adds an OR-clause so production deploys can host firmware on a fixed
// CDN even if the calendar comes from a different host. Override at build time:
//   pio run -e default --build-flag '-DWAGFAM_TRUSTED_FIRMWARE_DOMAINS="\"cdn.example.com,releases.example.com\""'
#ifndef WAGFAM_TRUSTED_FIRMWARE_DOMAINS
#define WAGFAM_TRUSTED_FIRMWARE_DOMAINS ""
#endif

// Issue #99: ECDSA-P256 PUBLIC key for verifying signed config updates pushed
// via the calendar response. 130 hex chars = uncompressed point (04 || X || Y,
// 65 bytes). The matching PRIVATE key lives only on the server as
// WAGFAM_CONFIG_SIGNING_PRIVATE_KEY_HEX — forging a signature requires it.
//
// Why public-key crypto: the firmware bin is publicly hosted, so any secret
// baked into it can be extracted with `strings firmware.bin | grep`. Public
// keys are public by definition; leaking this from the binary is a non-event.
// See app/services/config_signing.py on the server for the full rationale.
//
// Empty default means the firmware ignores configUpdate fields entirely —
// fail closed. CI build env should generate the public key from the server's
// private key (the server has a `derive_public_key_hex` helper) and pass it
// in via the build flag, e.g.:
//   pio run --build-flag '-DWAGFAM_CONFIG_PUBLIC_KEY="\"0485b4...\""'
#ifndef WAGFAM_CONFIG_PUBLIC_KEY
#define WAGFAM_CONFIG_PUBLIC_KEY ""
#endif

int TODAY_DISPLAY_DOT_SPACING = 5;  // How far apart the dots for the Today display are spaced
int TODAY_DISPLAY_DOT_SPEED_MS = 333; // How many milliseconds between dot moves for the today display


String APIKEY = ""; // Your API Key from http://openweathermap.org/
// Default GEO Location (use http://openweathermap.org/find to find location name being "cityname,countrycode" or "city ID" or GPS "latitude,longitude")
String geoLocation = "";
boolean IS_METRIC = false; // false = Imperial and true = Metric
boolean IS_24HOUR = false; // 23:00 millitary 24 hour clock
boolean IS_PM = true; // Show PM indicator on Clock when in AM/PM mode
const int WEBSERVER_PORT = 80; // The port you can access this device on over HTTP
// Device always provide a web interface via http://[ip]:[port]/
int minutesBetweenDataRefresh = 15;  // Time in minutes between data refresh (default 15 minutes)
int minutesBetweenScrolling = 1; // Time in minutes between scrolling data (default 1 minutes and max is 10)
int displayScrollSpeed = 25; // In milliseconds -- Configurable by the web UI (slow = 35, normal = 25, fast = 15, very fast = 5)
// The : will always flash on and off as a seconds indicator


// Display Settings
// CLK -> D5 (SCK)
// CS  -> D6
// DIN -> D7 (MOSI)
const int pinCS = D6; // Attach CS to this pin, DIN to MOSI and CLK to SCK (cf http://arduino.cc/en/Reference/SPI )
int displayIntensity = 4;  //(This can be set from 0 - 15)
const int numberOfHorizontalDisplays = 4; // default 4 for standard 4 x 1 display Max size of 16
const int numberOfVerticalDisplays = 1; // default 1 for a single row height
/* set ledRotation for LED Display panels (3 is default)
0: no rotation
1: 90 degrees clockwise
2: 180 degrees
3: 90 degrees counter clockwise (default)
*/
int ledRotation = 3;

//******************************
// End Settings
//******************************
