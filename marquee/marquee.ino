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

/**********************************************
  Edit Settings.h for personalization
***********************************************/

#include "Settings.h"
#include "SecurityHelpers.h"

#define BASE_VERSION "4.0.0-wagfam"
#ifdef BUILD_SUFFIX
#define VERSION BASE_VERSION BUILD_SUFFIX
#else
#define VERSION BASE_VERSION
#endif

#define HOSTNAME "CLOCK-"
#define CONFIG "/conf.txt"
#define OTA_PENDING_FILE "/ota_pending.txt"
#define OTA_CONFIRM_MS (5UL * 60UL * 1000UL)  // 5 minutes of stable uptime to confirm an update

//declaring prototypes
void setup();
void loop();
void processEverySecond();
void processEveryMinute();
String hourMinutes(bool isRefresh);
char secondsIndicator(bool isRefresh);
void getWeatherData();
void redirectToSpa(AsyncWebServerRequest *request);
void handleNotFound(AsyncWebServerRequest *request);
void configModeCallback (AsyncWiFiManager *myWiFiManager);
void flashLED(int number, int delayTime);
String getTempSymbol(bool forWeb = false);
String getSpeedSymbol();
String getPressureSymbol();
int8_t getWifiQuality();
int getMinutesFromLastRefresh();
void savePersistentConfig();
void readPersistentConfig();
void scrollMessageWait(const String &msg);
void centerPrint(const String &msg, bool extraStuff = false);
String EncodeUrlSpecialChars(const char *msg);
void performAutoUpdate(const String &firmwareUrl);
void checkOtaRollback();
static void doOtaFlash(const String &firmwareUrl);

void handleUpdateFromUrl(AsyncWebServerRequest *request);

// Security helpers

// REST API handlers
void handleApiStatus(AsyncWebServerRequest *request);
void handleApiConfigGet(AsyncWebServerRequest *request);
void handleApiConfigPost(AsyncWebServerRequest *request, JsonVariant &json);
void handleApiRestart(AsyncWebServerRequest *request);
void handleApiRefresh(AsyncWebServerRequest *request);
void handleApiOtaStatus(AsyncWebServerRequest *request);
void handleApiWeather(AsyncWebServerRequest *request);
void handleApiEvents(AsyncWebServerRequest *request);
void handleApiSystemReset(AsyncWebServerRequest *request);
void handleApiForgetWifi(AsyncWebServerRequest *request);
void handleApiFsRead(AsyncWebServerRequest *request);
void handleApiFsWrite(AsyncWebServerRequest *request, JsonVariant &json);
void handleApiFsDelete(AsyncWebServerRequest *request);
void handleApiFsList(AsyncWebServerRequest *request);
void handleApiSpaUpdateFromUrl(AsyncWebServerRequest *request, JsonVariant &json);
static void doOtaFsFlash(const String &fsUrl);

// LED Settings
int spacer = 1;  // dots between letters
int width = 5 + spacer; // The font width is 5 pixels + spacer
bool displayDirty = true; // true = framebuffer needs redraw before next write

// Matrix panel
Max72xxPanel matrix = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);

// Time
int lastMinute;
int lastSecond;
int displayRefreshCount = 1;
uint32_t firstTimeSync;
uint32_t lastRefreshDataTimestamp;
String displayTime;

// WagFam Calendar Client
WagFamBdayClient bdayClient(WAGFAM_API_KEY, WAGFAM_DATA_URL);
int bdayMessageIndex = 0;
WagFamBdayClient::configValues serverConfig = {};
String DEVICE_NAME = "";     // Human-friendly name assigned by server (not user-editable)
String SPA_VERSION = "unknown"; // Read from /spa/version.json at boot; "unknown" if absent
bool spaUpdateAvailable = false;
String pendingSpaFsUrl = "";
bool otaFsFromUrlRequested = false;
String pendingOtaFsUrl = "";
uint32_t todayDisplayMilliSecond = 0;
uint32_t todayDisplayStartingLED = 0;

// Weather Client
OpenWeatherMapClient weatherClient(APIKEY, IS_METRIC);
// (some) Default Weather Settings
boolean SHOW_DATE = false;
boolean SHOW_CITY = true;
boolean SHOW_CONDITION = false;
boolean SHOW_HUMIDITY = false;
boolean SHOW_WIND = false;
boolean SHOW_PRESSURE = false;
boolean SHOW_HIGHLOW = false;

AsyncWebServer server(WEBSERVER_PORT);
DNSServer dnsServer;  // Used by ESPAsyncWiFiManager for captive-portal DNS during AP mode

// Minimal HTML wrapper for the surviving non-SPA pages (/update,
// /updatefs, /updateFromUrl). These are file-upload forms / status
// pages that can't reasonably live inside the SPA — the SPA can't host
// a multipart firmware upload, and an inline OTA-by-URL form requires
// a separate GET endpoint. Each page is < 1 KB and self-contained:
// no W3.CSS / Font Awesome CDN dependency, no shared sendHeader chrome.
//
// Pages that USED to use sendHeader/sendFooter:
//   /                  → removed (now redirects to /spa/)
//   /configure         → removed (replaced by SPA Settings tab)
//   /update     GET    → now uses MINIMAL_PAGE_OPEN (this constant)
//   /updatefs   GET    → now uses MINIMAL_PAGE_OPEN
//   /updateFromUrl GET → now uses MINIMAL_PAGE_OPEN
static const char MINIMAL_PAGE_OPEN[] PROGMEM =
  "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>WagFam CalClock</title>"
  "<style>"
  "body{font-family:system-ui,-apple-system,sans-serif;max-width:42rem;"
  "margin:2rem auto;padding:0 1rem;line-height:1.55;color:#111}"
  "h1,h2{font-weight:600}h1{font-size:1.4rem;margin-top:0}"
  "h2{font-size:1.1rem}"
  "input,button,a.btn{font:inherit}"
  "input[type=file],input[type=url]{display:block;margin:.4em 0;width:100%;"
  "padding:.45em .55em;border:1px solid #ccc;border-radius:4px;"
  "box-sizing:border-box}"
  "button,a.btn{display:inline-block;background:#2563eb;color:#fff;border:0;"
  "border-radius:4px;cursor:pointer;padding:.55em 1.1em;text-decoration:none}"
  "button:hover,a.btn:hover{opacity:.85}"
  "code{background:#eee;padding:.1em .3em;border-radius:3px;font-size:.85em}"
  "small{color:#666;font-size:.85em}"
  ".back{margin-top:1.5rem;font-size:.85em}"
  "</style></head><body>";

// Footer fragment with a back-to-SPA link. Same pattern across every
// non-SPA page so users always have a way back.
static const char MINIMAL_PAGE_CLOSE[] PROGMEM =
  "<p class='back'><a href='/spa/'>← Back to the dashboard</a></p>"
  "</body></html>";

// LittleFS partition bounds — defined by the linker for the configured flash
// layout (4MB FS:1MB on d1_mini in this project; see platformio.ini). Used by
// /updatefs to size the U_FS update so it spans the entire partition.
extern "C" uint32_t _FS_start;
extern "C" uint32_t _FS_end;

// OTA auto-update state
String OTA_SAFE_URL = "";       // URL of last confirmed firmware; rollback target for next update
uint32_t otaConfirmAt = 0;      // non-zero: millis() target after which the pending update is confirmed
String otaPendingNewUrl = "";   // URL of firmware pending confirmation (becomes OTA_SAFE_URL on confirm)

// Async-handler deferred-work flag. AsyncWebServer handlers run in the TCP
// event loop and must NOT block — getWeatherData() makes HTTPS calls that
// can take 10–20s, long enough to trip the soft watchdog and crash the
// device with an Exception reset. Handlers that want a refresh set this
// flag and return immediately; processEverySecond() picks it up and runs
// the actual fetch in the main loop.
volatile bool weatherRefreshRequested = false;

// Deferred OTA-from-URL request. /updateFromUrl validates the URL and sends
// the response, then sets these. processEverySecond() picks it up and runs
// the (blocking) flash from the main loop. Same async-handler-cant-block
// rationale as weatherRefreshRequested.
volatile bool otaFromUrlRequested = false;
String pendingOtaUrl = "";

// /conf.txt contents saved across a /updatefs flash. Read before LittleFS.end(),
// written back after Update.end() + LittleFS.begin() so the user does not lose
// API keys and display settings when upgrading the SPA bundle.
String fsUpdateConfBackup = "";

// Deferred restart. /api/restart and the /update post-flash path set the
// flag with a "restart not before" deadline. The main loop pulls the
// trigger after the deadline, giving the async TCP layer time to flush the
// response (an in-handler delay() + ESP.restart() can drop the response
// mid-flight, leaving the client with a recv-failure even though the
// reboot succeeded).
volatile bool restartRequested = false;
volatile uint32_t restartAtMs = 0;

uint32_t lastRestartMs = 0;     // Rate-limit restart requests

// Change the externalLight to the pin you wish to use if other than the Built-in LED
int externalLight = LED_BUILTIN; // LED_BUILTIN is is the built in LED on the Wemos

void setup() {
  Serial.begin(115200);
  // Use LittleFS directly (not SPIFFS — they are separate objects; SPIFFS
  // refers to the old SPIFFS format and would silently wipe a valid LittleFS
  // partition on mount failure). Disable autoFormat so a freshly-flashed
  // littlefs.bin is never wiped; format explicitly only on blank/corrupt flash.
  LittleFS.setConfig(LittleFSConfig(false));
  if (!LittleFS.begin()) {
    Serial.println(F("[FS] mount failed - formatting (blank or corrupt flash)"));
    LittleFS.format();
    LittleFS.begin();
  }
  delay(10);

  // Initialize digital pin for LED
  pinMode(externalLight, OUTPUT);

  //New Line to clear from start garbage
  Serial.println();

  readPersistentConfig();

  {
    File vf = LittleFS.open("/spa/version.json", "r");
    if (vf) {
      JsonDocument vdoc;
      if (!deserializeJson(vdoc, vf) && vdoc["spa_version"].is<const char *>()) {
        SPA_VERSION = vdoc["spa_version"].as<String>();
      }
      vf.close();
    }
    Serial.println(F("[SPA] version: ") + SPA_VERSION);
  }

  Serial.println("Number of LED Displays: " + String(numberOfHorizontalDisplays));
  // initialize dispaly
  matrix.setIntensity(0); // Use a value between 0 and 15 for brightness

  int maxPos = numberOfHorizontalDisplays * numberOfVerticalDisplays;
  for (int i = 0; i < maxPos; i++) {
    matrix.setRotation(i, ledRotation);
    matrix.setPosition(i, maxPos - i - 1, 0);
  }

  Serial.println(F("matrix created"));
  matrix.fillScreen(LOW); // show black
  centerPrint(F("hello"));

  for (int inx = 0; inx <= 15; inx++) {
    matrix.setIntensity(inx);
    delay(100);
  }
  for (int inx = 15; inx >= 0; inx--) {
    matrix.setIntensity(inx);
    delay(60);
  }
  delay(1000);
  matrix.setIntensity(displayIntensity);

  scrollMessageWait(F("Welcome to the Wagner Family Calendar Clock!!!"));

  //ESPAsyncWiFiManager — shares our AsyncWebServer for the captive portal.
  //Local initialization. Once its business is done, there is no need to keep it around.
  AsyncWiFiManager wifiManager(&server, &dnsServer);

  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);

  //Custom Station (client) Static IP Configuration - Set custom IP for your Network (IP, Gateway, Subnet mask)
  //wifiManager.setSTAStaticIPConfig(IPAddress(192,168,0,99), IPAddress(192,168,0,1), IPAddress(255,255,255,0));

  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  if (!wifiManager.autoConnect((const char *)hostname.c_str())) {// new addition
    delay(3000);
    WiFi.disconnect(true);
    ESP.reset();
    delay(5000);
  }

  // print the received signal strength:
  Serial.print(F("Signal Strength (RSSI): "));
  Serial.print(getWifiQuality());
  Serial.println("%");

  // Check for a pending OTA confirmation or crash-loop rollback
  checkOtaRollback();

  // Web Server is always enabled.
  // (AsyncWebServer collects all request headers by default — no collectHeaders() call needed.)
  // Legacy routes — redirect to the SPA. /pull and /saveconfig kept their
  // POST/GET methods on the redirect side so existing scripts/bookmarks
  // don't 405; the SPA Actions tab + Settings tab cover the actual
  // behavior. handleNotFound() also catches arbitrary unknown paths.
  server.on("/", HTTP_GET, redirectToSpa);
  server.on("/configure", HTTP_GET, redirectToSpa);
  server.on("/pull", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Preserve "force a refresh" behavior — set the deferred flag, then
    // redirect to the SPA where the user can watch /api/status.
    weatherRefreshRequested = true;
    redirectToSpa(request);
  });
  server.on("/systemreset", HTTP_GET, redirectToSpa);
  server.on("/forgetwifi", HTTP_GET, redirectToSpa);
  server.on("/saveconfig", HTTP_POST, redirectToSpa);
  server.on("/updateFromUrl", HTTP_GET, handleUpdateFromUrl);

  // REST API endpoints
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/restart", HTTP_POST, handleApiRestart);
  server.on("/api/refresh", HTTP_POST, handleApiRefresh);
  server.on("/api/ota/status", HTTP_GET, handleApiOtaStatus);
  // SPA-parity routes — feed the new Home tab + Actions cards. See
  // docs/SPA_PARITY.md for the parity matrix this closes.
  server.on("/api/weather", HTTP_GET, handleApiWeather);
  server.on("/api/events", HTTP_GET, handleApiEvents);
  server.on("/api/system-reset", HTTP_POST, handleApiSystemReset);
  server.on("/api/forget-wifi", HTTP_POST, handleApiForgetWifi);
  server.on("/api/fs/read", HTTP_GET, handleApiFsRead);
  server.on("/api/fs/delete", HTTP_DELETE, handleApiFsDelete);
  server.on("/api/fs/list", HTTP_GET, handleApiFsList);

  // JSON-body POST endpoints. AsyncWebServer doesn't populate request->arg("plain")
  // for JSON bodies, so these go through AsyncCallbackJsonWebHandler which parses
  // the body into a JsonVariant before invoking the callback.
  {
    auto *configPost = new AsyncCallbackJsonWebHandler("/api/config", handleApiConfigPost);
    configPost->setMethod(HTTP_POST);
    configPost->setMaxContentLength(2048);
    server.addHandler(configPost);
  }
  {
    auto *fsWritePost = new AsyncCallbackJsonWebHandler("/api/fs/write", handleApiFsWrite);
    fsWritePost->setMethod(HTTP_POST);
    fsWritePost->setMaxContentLength(8192);
    server.addHandler(fsWritePost);
  }
  {
    auto *spaUpdatePost = new AsyncCallbackJsonWebHandler("/api/spa/update-from-url", handleApiSpaUpdateFromUrl);
    spaUpdatePost->setMethod(HTTP_POST);
    spaUpdatePost->setMaxContentLength(512);
    server.addHandler(spaUpdatePost);
  }

  // GET /update — file-upload form. Self-contained page (no SPA chrome,
  // no W3.CSS CDN). Posts back to POST /update for the multipart flash.
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
    response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
    response->print(FPSTR(MINIMAL_PAGE_OPEN));
    response->print(F(
      "<h1>Firmware Upload</h1>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update' accept='.bin' required>"
      "<button type='submit'>Upload &amp; Flash</button></form>"
      "<p><small>The device will reboot automatically when the upload completes. "
      "OTA does not touch LittleFS; for SPA-bundle updates use "
      "<a href='/updatefs'>LittleFS Upload</a>.</small></p>"));
    response->print(FPSTR(MINIMAL_PAGE_CLOSE));
    request->send(response);
  });

  // Update.runAsync(true) lets the flash routine cooperate with the async event loop.
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(200, F("text/plain"), ok ? F("OK") : F("FAIL"));
      response->addHeader(F("Connection"), F("close"));
      request->send(response);
      if (ok) {
        // Defer restart so the response (and TCP FIN) actually gets to the client
        // before the network stack tears down.
        restartAtMs = millis() + 1000;
        restartRequested = true;
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf_P(PSTR("[OTA] Upload start: %s\n"), filename.c_str());
        Update.runAsync(true);
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace, U_FLASH)) {
          Update.printError(Serial);
        }
      }
      if (!Update.hasError()) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf_P(PSTR("[OTA] Upload complete: %u bytes\n"), (unsigned)(index + len));
        } else {
          Update.printError(Serial);
        }
      }
    });

  // GET /updatefs — LittleFS upload form. OTA path for the SPA bundle
  // on already-deployed devices (issue #63 follow-up).
  server.on("/updatefs", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
    response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
    response->print(FPSTR(MINIMAL_PAGE_OPEN));
    response->print(F(
      "<h1>LittleFS Upload (SPA bundle)</h1>"
      "<form method='POST' action='/updatefs' enctype='multipart/form-data'>"
      "<input type='file' name='updatefs' accept='.bin' required>"
      "<button type='submit'>Upload &amp; Flash FS</button></form>"
      "<p><small>Pushes a <code>littlefs.bin</code> image to the LittleFS "
      "partition and reboots. <code>/conf.txt</code> is preserved. "
      "Looking for the firmware sketch upload? Use "
      "<a href='/update'>Firmware Update</a>.</small></p>"));
    response->print(FPSTR(MINIMAL_PAGE_CLOSE));
    request->send(response);
  });

  // POST /updatefs — accept a LittleFS image and flash it to the FS partition
  // via Update.begin(fsSize, U_FS). Mirrors POST /update but writes to the
  // filesystem partition instead of the sketch partition. Same auth +
  // deferred-restart pattern. The FS is ended before the flash starts because
  // the Update lib writes raw bytes to the partition; on success the device
  // reboots and remounts the new FS in setup(). On failure LittleFS.begin() is
  // called to remount the (still-old) FS so the device stays usable.
  server.on("/updatefs", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(200, F("text/plain"),
        ok ? F("OK — device will reboot to mount the new FS") : F("FAIL — see serial log"));
      response->addHeader(F("Connection"), F("close"));
      request->send(response);
      if (ok) {
        // Mount the new FS and restore /conf.txt so the user's API keys and
        // settings survive the SPA bundle upgrade.
        LittleFS.begin();
        if (!fsUpdateConfBackup.isEmpty()) {
          File confFile = LittleFS.open("/conf.txt", "w");
          if (confFile) {
            confFile.print(fsUpdateConfBackup);
            confFile.close();
            Serial.printf_P(PSTR("[OTAFS] Restored /conf.txt (%u bytes)\n"),
                            (unsigned)fsUpdateConfBackup.length());
          } else {
            Serial.println(F("[OTAFS] WARN: could not write /conf.txt to new FS"));
          }
          fsUpdateConfBackup = "";
        }
        LittleFS.end();
        restartAtMs = millis() + 1000;
        restartRequested = true;
      } else {
        // Re-mount the (still-old) FS so subsequent reads of /conf.txt etc. work.
        LittleFS.begin();
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf_P(PSTR("[OTAFS] Upload start: %s\n"), filename.c_str());
        // Save /conf.txt before unmounting so it survives the FS wipe.
        fsUpdateConfBackup = "";
        File confFile = LittleFS.open("/conf.txt", "r");
        if (confFile) {
          fsUpdateConfBackup = confFile.readString();
          confFile.close();
          Serial.printf_P(PSTR("[OTAFS] Saved /conf.txt (%u bytes) for restore\n"),
                          (unsigned)fsUpdateConfBackup.length());
        }
        // Unmount before writing raw bytes to the partition.
        LittleFS.end();
        Update.runAsync(true);
        uint32_t fsSize = (uint32_t)((size_t)&_FS_end - (size_t)&_FS_start);
        if (!Update.begin(fsSize, U_FS)) {
          Update.printError(Serial);
          // begin() failed — try to remount so the device isn't stranded.
          LittleFS.begin();
        }
      }
      if (!Update.hasError()) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf_P(PSTR("[OTAFS] Upload complete: %u bytes\n"), (unsigned)(index + len));
        } else {
          Update.printError(Serial);
        }
      }
    });

  // SPA frontend — bundle is built into data/spa/ by `make webui` and flashed
  // to LittleFS. AsyncWebServer's serveStatic auto-detects .gz siblings and
  // serves them with Content-Encoding: gzip when the client advertises it.
  // setDefaultFile serves /spa/index.html for bare /spa and /spa/ requests
  // via the _isDir path in AsyncStaticWebHandler::_getFile() — no explicit
  // redirect routes needed for those.
  // Cache for 10 min — short enough that a UI bugfix lands within a reasonable
  // window after reflashing the FS.
  server.serveStatic("/spa", LittleFS, "/spa/")
    .setDefaultFile("index.html")
    .setCacheControl("public, max-age=600");

  // notFound dispatch — any /spa/* request that didn't match serveStatic
  // (specific file not found or SPA not installed). handleNotFound checks
  // LittleFS for /spa/index.html: if present it redirects there (client-side
  // routing); if absent it renders the "SPA not installed" error page.
  server.onNotFound(handleNotFound);
  // Start the server
  server.begin();
  Serial.println(F("Server started"));
  // Print the IP address
  String webAddress = "http://" + WiFi.localIP().toString() + ":" + String(WEBSERVER_PORT) + "/";
  Serial.println("Use this URL : " + webAddress);
  scrollMessageWait(" v" + String(VERSION) + "  IP: " + WiFi.localIP().toString() + "  ");

  // Start NTP , although it can't do anything while in config mode or when no WiFi AP connected
  timeNTPsetup();

  flashLED(1, 500);
}

//************************************************************
// Main Loop
//************************************************************
void loop() {

  if (lastSecond != second()) {
    lastSecond = second();
    displayTime = hourMinutes(false); // rebuild once per second, not every frame
    displayDirty = true;
    processEverySecond();
  }

  if (lastMinute != minute()) {
    lastMinute = minute();
    processEveryMinute();
  }

  // Only redraw when content has changed. The animated event-day border
  // requires per-frame updates, so always redraw when it is active.
  if (displayDirty || WAGFAM_EVENT_TODAY) {
    matrix.fillScreen(LOW);
    centerPrint(displayTime, true);
    displayDirty = false;
  }

  // AsyncWebServer runs in the background via TCP callbacks; no handleClient()
  // call needed in the main loop.
}

void processEverySecond() {
  // Confirm a pending OTA update after stable uptime
  if (otaConfirmAt != 0 && millis() >= otaConfirmAt) {
    Serial.println(F("[OTA] Update confirmed stable"));
    LittleFS.remove(OTA_PENDING_FILE);
    OTA_SAFE_URL = otaPendingNewUrl;
    otaConfirmAt = 0;
    otaPendingNewUrl = "";
    savePersistentConfig();
  }

  //Get some Weather Data to serve, or honor a deferred refresh request from
  //an async HTTP handler (/pull, /api/refresh) that couldn't block the event loop.
  if (weatherRefreshRequested
      || (getMinutesFromLastRefresh() >= minutesBetweenDataRefresh)
      || lastRefreshDataTimestamp == 0) {
    weatherRefreshRequested = false;
    getWeatherData();
  }

  // Honor a deferred restart. The deadline gives the async TCP layer time to
  // flush the response before we tear down the network stack with ESP.restart().
  if (restartRequested && millis() >= restartAtMs) {
    Serial.println(F("[loop] Deferred restart firing"));
    ESP.restart();
  }

  // Honor a deferred OTA request from /updateFromUrl. Done from the main loop
  // because doOtaFlash() blocks for 20–30s and would crash the async event loop.
  if (otaFromUrlRequested && pendingOtaUrl.length() > 0) {
    String url = pendingOtaUrl;
    otaFromUrlRequested = false;
    pendingOtaUrl = "";
    digitalWrite(externalLight, LOW);
    matrix.fillScreen(LOW);
    scrollMessageWait(F("   ...Updating..."));
    centerPrint(F("..."));
    Serial.printf_P(PSTR("[OTA] Starting deferred update from URL: %s\n"), url.c_str());
    doOtaFlash(url);
    // Only reached on failure — doOtaFlash reboots the device on success.
    digitalWrite(externalLight, HIGH);
  }

  // Honor a deferred SPA FS update from /api/spa/update-from-url.
  // Uses manual streaming + Update.begin(U_FS) so /conf.txt can be preserved
  // (ESPhttpUpdate.updateSpiffs() reboots internally before we can restore it).
  if (otaFsFromUrlRequested && pendingOtaFsUrl.length() > 0) {
    String url = pendingOtaFsUrl;
    otaFsFromUrlRequested = false;
    pendingOtaFsUrl = "";
    digitalWrite(externalLight, LOW);
    matrix.fillScreen(LOW);
    scrollMessageWait(F("   ...Updating SPA..."));
    centerPrint(F("..."));
    Serial.printf_P(PSTR("[OTAFS] Starting deferred FS update from URL: %s\n"), url.c_str());
    doOtaFsFlash(url);
    // Only reached on failure — doOtaFsFlash reboots the device on success.
    digitalWrite(externalLight, HIGH);
  }
}

void processEveryMinute() {
  if (weatherClient.getErrorMessage() != "") {
    scrollMessageWait(weatherClient.getErrorMessage());
    return;
  }

  matrix.fillScreen(LOW); // show black

  displayRefreshCount --;
  // Check to see if we need to Scroll some Data
  if ((displayRefreshCount <= 0) && weatherClient.getWeatherDataValid() && (weatherClient.getErrorMessage().length() == 0)) {
    displayRefreshCount = minutesBetweenScrolling;
    String msg = " ";
    String temperature = String(weatherClient.getTemperature(),0);
    String description = weatherClient.getWeatherDescription();
    description.toUpperCase();

    if (SHOW_DATE) {
      msg += getDayName(weekday()) + ", ";
      msg += getMonthName(month()) + " " + day() + "  ";
    }
    if (SHOW_CITY) {
      msg += weatherClient.getCity() + "  ";
      // Only show the temperature if the city is shown also
      msg += temperature + getTempSymbol() + "  ";
    }

    //show high/low temperature
    if (SHOW_HIGHLOW) {
      msg += "High/Low:" + String(weatherClient.getTemperatureHigh(),0) + "/" + String(weatherClient.getTemperatureLow(),0) + " " + getTempSymbol() + " ";
    }

    if (SHOW_CONDITION) {
      msg += description + "  ";
    }
    if (SHOW_HUMIDITY) {
      msg += "Humidity:" + String(weatherClient.getHumidity()) + "%  ";
    }
    if (SHOW_WIND) {
      String windspeed = String(weatherClient.getWindSpeed(),0);
      windspeed.trim();
      msg += "Wind: " + weatherClient.getWindDirectionText() + " " + windspeed + getSpeedSymbol() + "  ";
    }
    //line to show barometric pressure
    if (SHOW_PRESSURE) {
      msg += "Pressure:" + String(weatherClient.getPressure()) + getPressureSymbol() + "  ";
    }

    // WAGFAM Calendar Specific display
    msg += " " + bdayClient.getMessage(bdayMessageIndex) + " ";
    bdayMessageIndex += 1;
    if (bdayMessageIndex >= bdayClient.getNumMessages()) {
      bdayMessageIndex = 0;
    }
    scrollMessageWait(msg);
  }
}

String hourMinutes(bool isRefresh) {
  if (IS_24HOUR) {
    return spacePad(hour()) + secondsIndicator(isRefresh) + zeroPad(minute());
  } else {
    return spacePad(hourFormat12()) + secondsIndicator(isRefresh) + zeroPad(minute());
  }
}

char secondsIndicator(bool isRefresh) {
  char rtnValue = ':';
  if (!isRefresh && ((second() % 2) == 0)) {
    rtnValue = ' ';
  }
  return rtnValue;
}

// Legacy handlers (handlePull, handleSaveConfig, handleSystemReset,
// handleForgetWifi, handleConfigure) and the legacy chrome (sendHeader,
// sendFooter, displayHomePage) were removed in Phase D of the
// SPA-parity migration. The SPA at /spa/ covers every feature they
// provided:
//
//   /pull          → POST /api/refresh + Home tab
//   /saveconfig    → POST /api/config (Settings tab)
//   /systemreset   → POST /api/system-reset (Actions tab)
//   /forgetwifi    → POST /api/forget-wifi (Actions tab)
//   /configure     → SPA Settings tab
//   /              → SPA Home tab
//
// The legacy paths now redirect to /spa/ via redirectToSpa() — see
// setup() and handleNotFound() below.

// Shared OTA flash core used by both handleUpdateFromUrl() and performAutoUpdate().
// Writes a rollback record to LittleFS, calls ESPhttpUpdate, then cleans up on failure.
// Never returns on success — the device reboots automatically after a successful flash.
static void doOtaFlash(const String &firmwareUrl) {
  File f = LittleFS.open(OTA_PENDING_FILE, "w");
  f.println("safeUrl=" + OTA_SAFE_URL);
  f.println("newUrl=" + firmwareUrl);
  f.println("boots=0");
  f.close();

  WiFiClient client;
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, firmwareUrl);

  // Only reached on failure — remove pending record so next boot is clean
  LittleFS.remove(OTA_PENDING_FILE);
  Serial.printf_P(PSTR("[OTA] Flash failed (code %d): %s\n"),
    ret, ESPhttpUpdate.getLastErrorString().c_str());
}

// GET /updateFromUrl — bare URL renders the form; with ?firmwareUrl=...
// validates and queues the OTA flash for the main loop. Self-contained
// page (no SPA chrome). Linked from the SPA Actions tab.
void handleUpdateFromUrl(AsyncWebServerRequest *request) {
  String firmwareUrl = request->arg("firmwareUrl");

  AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
  response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
  response->print(FPSTR(MINIMAL_PAGE_OPEN));

  if (firmwareUrl == "") {
    response->print(F(
      "<h1>Firmware Update from URL</h1>"
      "<form method='get' action='/updateFromUrl'>"
      "<input type='url' name='firmwareUrl' placeholder='http://example.com/firmware.bin' required>"
      "<button type='submit'>Update from URL</button></form>"
      "<p><small><strong>HTTP only</strong> — ESPhttpUpdate doesn't speak "
      "TLS. Prefer <a href='/update'>direct file upload</a> for "
      "TLS-protected sources.</small></p>"));
  } else if (!firmwareUrl.startsWith("http://")) {
    Serial.println(F("[OTA] /updateFromUrl: rejected non-HTTP URL"));
    response->print(F(
      "<h1>Firmware Update from URL</h1>"
      "<p><strong>Error:</strong> URL must start with <code>http://</code>. "
      "<code>https://</code> is not supported.</p>"
      "<p><a class='btn' href='/updateFromUrl'>Try again</a></p>"));
  } else {
    response->print(F("<h1>Firmware Update from URL</h1><p>Starting update from <code>"));
    response->print(firmwareUrl);
    response->print(F(
      "</code></p><p>The device will reboot when the update completes. "
      "Watch <a href='/spa/'>/spa/</a> Status tab for the new version.</p>"));
    // Defer the actual flash to the main loop — async handlers can't block on
    // the 20–30s ESPhttpUpdate.update() call without crashing the event loop.
    pendingOtaUrl = firmwareUrl;
    otaFromUrlRequested = true;
  }

  response->print(FPSTR(MINIMAL_PAGE_CLOSE));
  request->send(response);
}

// Trigger an OTA update from the given HTTP URL (called from the auto-update path).
// Writes a rollback record to LittleFS before flashing so crash-loop recovery is possible.
// On ESPhttpUpdate success the device reboots; this function only returns on failure.
void performAutoUpdate(const String &firmwareUrl) {
  Serial.printf_P(PSTR("[OTA] Auto-update from: %s\n"), firmwareUrl.c_str());

  matrix.fillScreen(LOW);
  scrollMessageWait(F("   ...Auto Updating..."));
  centerPrint(F("..."));

  doOtaFlash(firmwareUrl);

  // Only reached on failure
  Serial.printf_P(PSTR("[OTA] Auto-update failed.\n"));
}

// Streams a LittleFS image from fsUrl, writes it to the FS partition via
// Update.begin(U_FS), then restores /conf.txt on the new FS before rebooting.
// Call only from the main loop — the HTTP download blocks for several seconds.
// Never returns on success — the device reboots after a successful flash.
static void doOtaFsFlash(const String &fsUrl) {
  // Back up /conf.txt before unmounting LittleFS.
  String confBackup = "";
  {
    File cf = LittleFS.open("/conf.txt", "r");
    if (cf) {
      confBackup = cf.readString();
      cf.close();
      Serial.printf_P(PSTR("[OTAFS] Saved /conf.txt (%u bytes)\n"),
                      (unsigned)confBackup.length());
    }
  }

  // Open the HTTP connection and validate the response.
  WiFiClient httpClient;
  HTTPClient http;
  http.begin(httpClient, fsUrl);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf_P(PSTR("[OTAFS] HTTP %d — aborting\n"), code);
    http.end();
    return;
  }
  int contentLength = http.getSize();
  Serial.printf_P(PSTR("[OTAFS] Content-Length: %d\n"), contentLength);

  // Unmount LittleFS before writing raw bytes to the partition.
  LittleFS.end();
  uint32_t fsPartSize = (uint32_t)((size_t)&_FS_end - (size_t)&_FS_start);
  uint32_t updateSize = (contentLength > 0) ? (uint32_t)contentLength : fsPartSize;
  if (!Update.begin(updateSize, U_FS)) {
    Update.printError(Serial);
    http.end();
    LittleFS.begin();
    return;
  }

  // Stream the HTTP body into the Update writer.
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[512];
  uint32_t written = 0;
  while (http.connected() && (contentLength < 0 || (int)written < contentLength)) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = min(avail, sizeof(buf));
      size_t got = stream->readBytes(buf, toRead);
      if (Update.write(buf, got) != got) {
        Update.printError(Serial);
        http.end();
        LittleFS.begin();
        return;
      }
      written += got;
    }
    yield();
  }
  http.end();
  Serial.printf_P(PSTR("[OTAFS] Streamed %u bytes\n"), written);

  if (!Update.end(true)) {
    Update.printError(Serial);
    LittleFS.begin();
    return;
  }

  // Remount the new FS and restore /conf.txt.
  LittleFS.begin();
  if (!confBackup.isEmpty()) {
    File cf = LittleFS.open("/conf.txt", "w");
    if (cf) {
      cf.print(confBackup);
      cf.close();
      Serial.printf_P(PSTR("[OTAFS] Restored /conf.txt (%u bytes)\n"),
                      (unsigned)confBackup.length());
    } else {
      Serial.println(F("[OTAFS] WARN: could not write /conf.txt to new FS"));
    }
  }
  LittleFS.end();

  Serial.println(F("[OTAFS] Flash complete — rebooting"));
  ESP.restart();
}

// Called once in setup() after WiFi connects.
// If a previous OTA update did not complete its 5-minute confirmation window,
// this function detects the crash loop and re-flashes the last known-good firmware.
void checkOtaRollback() {
  if (!LittleFS.exists(OTA_PENDING_FILE)) return;

  String safeUrl = "";
  String newUrl = "";
  int boots = 0;

  File f = LittleFS.open(OTA_PENDING_FILE, "r");
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String k = line.substring(0, eq);
    String v = line.substring(eq + 1);
    if (k == "safeUrl") safeUrl = v;
    else if (k == "newUrl") newUrl = v;
    else if (k == "boots") boots = v.toInt();
  }
  f.close();

  boots++;

  if (boots >= 2) {
    // Two unconfirmed boots — assume crash loop; roll back to safe firmware
    LittleFS.remove(OTA_PENDING_FILE);
    if (safeUrl != "" && safeUrl.startsWith("http://")) {
      Serial.println("[OTA] Crash-loop detected — rolling back to: " + safeUrl);
      matrix.fillScreen(LOW);
      scrollMessageWait(F("   ...Rolling Back..."));
      centerPrint(F("..."));
      WiFiClient client;
      ESPhttpUpdate.update(client, safeUrl);
      // If we get here the rollback itself failed; continue with current firmware
      Serial.println(F("[OTA] Rollback failed; continuing with current firmware"));
    } else {
      Serial.println(F("[OTA] Crash-loop detected but no safe URL; cannot roll back"));
    }
    return;
  }

  // First boot after an update — record it and start the confirmation timer
  File fw = LittleFS.open(OTA_PENDING_FILE, "w");
  fw.println("safeUrl=" + safeUrl);
  fw.println("newUrl=" + newUrl);
  fw.println("boots=" + String(boots));
  fw.close();

  otaConfirmAt = millis() + OTA_CONFIRM_MS;
  otaPendingNewUrl = newUrl;
  Serial.println(F("[OTA] Update pending confirmation (5 min of stable uptime required)"));
}

//***********************************************************************
void getWeatherData() //client function to send/receive GET request data.
{
  digitalWrite(externalLight, LOW);
  matrix.fillScreen(LOW); // show black
  Serial.println();

  // pull the weather data
  if (firstTimeSync != 0) {
    centerPrint(displayTime, true);
  } else {
    centerPrint("...");
  }
  matrix.drawPixel(0, 7, HIGH);
  matrix.drawPixel(0, 6, HIGH);
  matrix.drawPixel(0, 5, HIGH);
  matrix.write();

  weatherClient.updateWeather();
  if (weatherClient.getErrorMessage() != "") {
    scrollMessageWait(weatherClient.getErrorMessage());
  } else {
    // Set current timezone (adapts to DST when region supports that)
    // when time was potentially changed, stop quick auto sync
    if (set_timeZoneSec(weatherClient.getTimeZoneSeconds())) {
      // Stop automatic NTP sync and do it explicitly below
      setSyncProvider(NULL);
    }
  }
  lastRefreshDataTimestamp = now();

  Serial.printf("Timestatus=%d\n", timeStatus());  // status timeNeedsSync(1) is NEVER set
  Serial.println("Updating Time...");
  //Update the Time
  matrix.drawPixel(0, 4, HIGH);
  matrix.drawPixel(0, 3, HIGH);
  matrix.drawPixel(0, 2, HIGH);
  matrix.write();

  // Explicitly get the NTP time
  time_t t = getNtpTime();
  if (t > TIME_VALID_MIN) {
    // warning: adding ctime() causes 5kB extra codesize!
    //Serial.printf_P(PSTR("setTime %u=%s"), uint32_t(t), ctime(&t));
    Serial.printf_P(PSTR("setTime %u\n"), uint32_t(t));
    setTime(t);
  }
  if (firstTimeSync == 0) {
    firstTimeSync = now();
    if (firstTimeSync > TIME_VALID_MIN) {
      setSyncInterval(222); // used for testing, value doesn't really matter
      Serial.printf_P(PSTR("firstTimeSync is: %d\n"), firstTimeSync);
    } else {
      // on a failed ntp sync we have seen that firstTimeSync was set to a low value: reset firstTimeSync
      firstTimeSync = 0;
    }
  }

  DeviceInfo devInfo;
  devInfo.chipId = String(ESP.getChipId(), HEX);
  devInfo.version = VERSION;
  devInfo.uptimeMs = millis();
  devInfo.freeHeap = ESP.getFreeHeap();
  devInfo.rssi = WiFi.RSSI();
  devInfo.utcOffsetSec = weatherClient.getTimeZoneSeconds();
  serverConfig = bdayClient.updateData(devInfo);
  bool needToSave = false;
  // SEC-12: Validate server-pushed dataSourceUrl must use HTTPS
  if (serverConfig.dataSourceUrlValid) {
    if (serverConfig.dataSourceUrl.startsWith("https://")) {
      WAGFAM_DATA_URL = serverConfig.dataSourceUrl;
      lastRefreshDataTimestamp = 0;
      needToSave = true;
    } else {
      Serial.println(F("[SEC] Rejected non-HTTPS dataSourceUrl from server"));
    }
  }
  if (serverConfig.apiKeyValid) {
    WAGFAM_API_KEY = serverConfig.apiKey;
    lastRefreshDataTimestamp = 0; // this should force a data pull, since with a new API_KEY that's required
    needToSave = true;
  }
  if (serverConfig.eventTodayValid) {
    WAGFAM_EVENT_TODAY = serverConfig.eventToday;
    needToSave = true;
  }
  if (serverConfig.deviceNameValid) {
    DEVICE_NAME = serverConfig.deviceName;
    needToSave = true;
  }
  if (needToSave) {
    Serial.println("Saving new config received from server");
    savePersistentConfig();
  }

  // Check for a remote firmware update pushed via the calendar config.
  //
  // Build with `-DWAGFAM_AUTO_UPDATE_DISABLED=1` to skip this branch. Useful
  // when running a locally-built firmware that the calendar server doesn't
  // know about yet (the server's published latestVersion would otherwise
  // not match VERSION, and the device would auto-revert to the server's
  // build at the next calendar refresh — losing local work). Default
  // behavior is unchanged: the flag must be set explicitly to disable.
#ifdef WAGFAM_AUTO_UPDATE_DISABLED
  Serial.println(F("[OTA] Auto-update disabled at build time (WAGFAM_AUTO_UPDATE_DISABLED)"));
#else
  if (serverConfig.latestVersionValid && serverConfig.firmwareUrlValid
      && serverConfig.latestVersion != String(VERSION)
      && serverConfig.firmwareUrl.startsWith("http://")
      && otaConfirmAt == 0
      && millis() > OTA_CONFIRM_MS) {
    // SEC-03: Validate firmware URL domain — accept if it's in the compile-time
    // allowlist (WAGFAM_TRUSTED_FIRMWARE_DOMAINS) OR matches the calendar source.
    if (!isTrustedFirmwareDomain(serverConfig.firmwareUrl, WAGFAM_DATA_URL, WAGFAM_TRUSTED_FIRMWARE_DOMAINS)) {
      Serial.println(F("[SEC] Rejected firmware URL — domain not allowlisted and does not match calendar source"));
    } else {
    Serial.println("[OTA] Server version: " + serverConfig.latestVersion + ", current: " + String(VERSION));
    performAutoUpdate(serverConfig.firmwareUrl);
    // If we return here the update failed; continue normal operation
    }
  }
#endif // WAGFAM_AUTO_UPDATE_DISABLED

  // Check for a remote SPA update pushed via the calendar config
  if (serverConfig.latestSpaVersionValid && serverConfig.spaFsUrlValid
      && serverConfig.latestSpaVersion != SPA_VERSION
      && serverConfig.spaFsUrl.startsWith("http://")) {
    spaUpdateAvailable = true;
    pendingSpaFsUrl = serverConfig.spaFsUrl;
    Serial.println("[SPA] Update available: server=" + serverConfig.latestSpaVersion + ", current=" + SPA_VERSION);
  } else {
    spaUpdateAvailable = false;
    pendingSpaFsUrl = "";
  }

  Serial.println("Version: " + String(VERSION));
  Serial.println();
  digitalWrite(externalLight, HIGH);
}

// 302 → /spa/. Used as the catch-all for legacy paths (`/`, `/configure`,
// `/pull`, `/systemreset`, `/forgetwifi`, `/saveconfig`) and as the
// default branch of handleNotFound() for non-/spa unknown URLs. Anyone
// hitting a removed legacy route lands on the new SPA.
void redirectToSpa(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(302, F("text/plain"), F(""));
  response->addHeader(F("Location"), F("/spa/"));
  response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
  request->send(response);
}

// 404 dispatch. /spa* requests that fall through here mean the SPA bundle
// isn't on LittleFS — almost always because the user OTA-flashed firmware
// without flashing LittleFS too (issue #63). Returning a 404 with deploy
// instructions makes the failure mode self-explanatory; everything else
// falls through to the legacy redirect-home behavior.
void handleNotFound(AsyncWebServerRequest *request) {
  if (request->url().startsWith("/spa")) {
    if (LittleFS.exists(F("/spa/index.html")) && request->url() != "/spa/index.html") {
      // SPA is installed — a client-side route that doesn't exist as a real
      // file fell through. Redirect to the SPA root and let the JS router
      // handle it. Guard against /spa/index.html itself to avoid a redirect
      // loop (AsyncCallbackWebHandler prefix-matches /spa against /spa/*).
      request->redirect("/spa/index.html");
      return;
    }
    AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
    response->setCode(404);
    response->addHeader(F("Cache-Control"), F("no-store"));
    response->print(F(
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<title>SPA bundle not installed</title>"
      "<style>body{font-family:system-ui,sans-serif;max-width:42rem;margin:2rem auto;padding:0 1rem;line-height:1.55}"
      "code{background:#eee;padding:.1em .35em;border-radius:3px}h1{font-size:1.4rem}</style></head><body>"
      "<h1>SPA bundle not installed (HTTP 404)</h1>"
      "<p>The firmware is registering <code>/spa</code> as a static-file route, "
      "but no files exist at <code>/spa/index.html</code> on the device's LittleFS partition.</p>"
      "<p><strong>Most likely cause:</strong> you OTA-flashed firmware via "
      "<code>/update</code> or <code>/updateFromUrl</code>. OTA only updates the "
      "firmware sketch — it does not touch the LittleFS partition where the SPA lives.</p>"
      "<p><strong>To install the SPA bundle</strong>, do one of:</p>"
      "<ul>"
      "<li><strong>Easiest (no serial cable):</strong> <a href='/updatefs'>upload <code>littlefs.bin</code> via OTA</a></li>"
      "<li>From a checkout: <code>make uploadfs</code> (serial flash)</li>"
      "<li>From a release, manual serial flash: download <code>littlefs.bin</code> and run "
      "<code>esptool.py write_flash 0x300000 littlefs.bin</code></li>"
      "<li>From a release, fresh install: use <code>merged.bin</code> "
      "(<code>esptool.py write_flash 0x0 merged.bin</code>) — single image, both sketch + SPA</li>"
      "</ul>"
      "<p>See <a href='https://github.com/jrwagz/marquee-scroller/blob/master/docs/WEBUI.md'>docs/WEBUI.md</a> for details.</p>"
      "<p>The legacy UI is still available at <a href='/'>/</a> and <a href='/configure'>/configure</a>.</p>"
      "</body></html>"));
    request->send(response);
    return;
  }
  redirectToSpa(request);
}

void configModeCallback (AsyncWiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println("Wifi Manager");
  Serial.println("Please connect to AP");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.println("To setup Wifi Configuration");
  scrollMessageWait("Please Connect to AP: " + String(myWiFiManager->getConfigPortalSSID()));
  centerPrint("wifi");
}

void flashLED(int number, int delayTime) {
  for (int inx = 0; inx < number; inx++) {
    delay(delayTime);
    digitalWrite(externalLight, LOW);
    delay(delayTime);
    digitalWrite(externalLight, HIGH);
    delay(delayTime);
  }
}

String getTempSymbol(bool forWeb) {
  // Note: The forWeb degrees character is an UTF8 double byte character!
  return String((forWeb) ? "°" : String(char(247))) + String((IS_METRIC) ? 'C' : 'F');
}

String getSpeedSymbol() {
  return String((IS_METRIC) ? "kmh" : "mph");
}

String getPressureSymbol() {
  return String((IS_METRIC) ? "mb" : "inHg");
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) {
    return 0;
  } else if (dbm >= -50) {
    return 100;
  } else {
    return 2 * (dbm + 100);
  }
}

// getTimeTillUpdate() removed in Phase D. The legacy footer was the only
// caller; /api/status now exposes next_refresh_in_sec (computed inline)
// and the SPA Status tab formats it client-side.

int getMinutesFromLastRefresh() {
  int minutes = (now() - lastRefreshDataTimestamp) / 60;
  return minutes;
}

void savePersistentConfig() {
  // Save config to LittleFS for playback on power up.
  File f = LittleFS.open(CONFIG, "w");
  if (!f) {
    Serial.println("File open failed!");
  } else {
    Serial.println("Saving settings now...");
    f.println("WAGFAM_DATA_URL=" + WAGFAM_DATA_URL);
    f.println("WAGFAM_API_KEY=" + WAGFAM_API_KEY);
    f.println("WAGFAM_EVENT_TODAY=" + String(WAGFAM_EVENT_TODAY));
    f.println("APIKEY=" + APIKEY);
    f.println("CityID=" + geoLocation);
    f.println("ledIntensity=" + String(displayIntensity));
    f.println("scrollSpeed=" + String(displayScrollSpeed));
    f.println("is24hour=" + String(IS_24HOUR));
    f.println("isPM=" + String(IS_PM));
    f.println("isMetric=" + String(IS_METRIC));
    f.println("refreshRate=" + String(minutesBetweenDataRefresh));
    f.println("minutesBetweenScrolling=" + String(minutesBetweenScrolling));
    f.println("SHOW_CITY=" + String(SHOW_CITY));
    f.println("SHOW_CONDITION=" + String(SHOW_CONDITION));
    f.println("SHOW_HUMIDITY=" + String(SHOW_HUMIDITY));
    f.println("SHOW_WIND=" + String(SHOW_WIND));
    f.println("SHOW_PRESSURE=" + String(SHOW_PRESSURE));
    f.println("SHOW_HIGHLOW=" + String(SHOW_HIGHLOW));
    f.println("SHOW_DATE=" + String(SHOW_DATE));
    f.println("OTA_SAFE_URL=" + OTA_SAFE_URL);
    f.println("DEVICE_NAME=" + DEVICE_NAME);
  }
  f.close();
  // Apply current settings to hardware and clients directly.
  // (Previously this called readPersistentConfig(), creating mutual recursion
  // and an infinite loop if the filesystem write ever failed.)
  matrix.setIntensity(displayIntensity);
  weatherClient.setWeatherApiKey(APIKEY);
  weatherClient.setMetric(IS_METRIC);
  weatherClient.setGeoLocation(geoLocation);
  bdayClient.updateBdayClient(WAGFAM_API_KEY, WAGFAM_DATA_URL);
}

void readPersistentConfig() {
  if (LittleFS.exists(CONFIG) == false) {
    Serial.println("Settings File does not yet exists.");
    savePersistentConfig();
    return;
  }
  File fr = LittleFS.open(CONFIG, "r");
  while (fr.available()) {
    String line = fr.readStringUntil('\n');
    line.trim(); // strip \r and leading/trailing whitespace
    int eqPos = line.indexOf('=');
    if (eqPos <= 0) continue; // skip blank or malformed lines
    String key = line.substring(0, eqPos);
    String value = line.substring(eqPos + 1);

    if (key == "WAGFAM_DATA_URL") {
      WAGFAM_DATA_URL = value;
      Serial.println(F("WAGFAM_DATA_URL: [set]"));
    } else if (key == "WAGFAM_API_KEY") {
      WAGFAM_API_KEY = value;
      Serial.println("WAGFAM_API_KEY: [set]");
    } else if (key == "WAGFAM_EVENT_TODAY") {
      WAGFAM_EVENT_TODAY = value.toInt();
      Serial.println("WAGFAM_EVENT_TODAY: " + String(WAGFAM_EVENT_TODAY));
    } else if (key == "APIKEY") {
      APIKEY = value;
      Serial.println("APIKEY: [set]");
    } else if (key == "CityID") {
      geoLocation = value;
      Serial.println("CityID: " + geoLocation);
    } else if (key == "is24hour") {
      IS_24HOUR = value.toInt();
      Serial.println("IS_24HOUR=" + String(IS_24HOUR));
    } else if (key == "isPM") {
      IS_PM = value.toInt();
      Serial.println("IS_PM=" + String(IS_PM));
    } else if (key == "isMetric") {
      IS_METRIC = value.toInt();
      Serial.println("IS_METRIC=" + String(IS_METRIC));
    } else if (key == "refreshRate") {
      minutesBetweenDataRefresh = value.toInt();
      if (minutesBetweenDataRefresh == 0) minutesBetweenDataRefresh = 15;
      Serial.println("minutesBetweenDataRefresh=" + String(minutesBetweenDataRefresh));
    } else if (key == "minutesBetweenScrolling") {
      displayRefreshCount = 1;
      minutesBetweenScrolling = value.toInt();
      Serial.println("minutesBetweenScrolling=" + String(minutesBetweenScrolling));
    } else if (key == "ledIntensity") {
      displayIntensity = value.toInt();
      Serial.println("displayIntensity=" + String(displayIntensity));
    } else if (key == "scrollSpeed") {
      displayScrollSpeed = value.toInt();
      Serial.println("displayScrollSpeed=" + String(displayScrollSpeed));
    } else if (key == "SHOW_CITY") {
      SHOW_CITY = value.toInt();
      Serial.println("SHOW_CITY=" + String(SHOW_CITY));
    } else if (key == "SHOW_CONDITION") {
      SHOW_CONDITION = value.toInt();
      Serial.println("SHOW_CONDITION=" + String(SHOW_CONDITION));
    } else if (key == "SHOW_HUMIDITY") {
      SHOW_HUMIDITY = value.toInt();
      Serial.println("SHOW_HUMIDITY=" + String(SHOW_HUMIDITY));
    } else if (key == "SHOW_WIND") {
      SHOW_WIND = value.toInt();
      Serial.println("SHOW_WIND=" + String(SHOW_WIND));
    } else if (key == "SHOW_PRESSURE") {
      SHOW_PRESSURE = value.toInt();
      Serial.println("SHOW_PRESSURE=" + String(SHOW_PRESSURE));
    } else if (key == "SHOW_HIGHLOW") {
      SHOW_HIGHLOW = value.toInt();
      Serial.println("SHOW_HIGHLOW=" + String(SHOW_HIGHLOW));
    } else if (key == "SHOW_DATE") {
      SHOW_DATE = value.toInt();
      Serial.println("SHOW_DATE=" + String(SHOW_DATE));
    } else if (key == "OTA_SAFE_URL") {
      OTA_SAFE_URL = value;
      Serial.print(F("OTA_SAFE_URL: "));
      Serial.println(OTA_SAFE_URL != "" ? "[set]" : "[empty]");
    } else if (key == "DEVICE_NAME") {
      DEVICE_NAME = value;
      Serial.println("DEVICE_NAME: " + DEVICE_NAME);
    }
  }
  fr.close();
  matrix.setIntensity(displayIntensity);
  weatherClient.setWeatherApiKey(APIKEY);
  weatherClient.setMetric(IS_METRIC);
  weatherClient.setGeoLocation(geoLocation);
  bdayClient.updateBdayClient(WAGFAM_API_KEY,WAGFAM_DATA_URL);
}

void scrollMessageWait(const String &msg) {
  for ( int i = 0 ; i < width * (int)msg.length() + matrix.width() - 1 - spacer; i++ ) {
    matrix.fillScreen(LOW);

    int letter = i / width;
    int x = (matrix.width() - 1) - i % width;
    int y = (matrix.height() - 8) / 2; // center the text vertically

    while ( x + width - spacer >= 0 && letter >= 0 ) {
      if ( letter < (int)msg.length() ) {
        matrix.drawChar(x, y, msg[letter], HIGH, LOW, 1);
      }

      letter--;
      x -= width;
    }

    matrix.write(); // Send bitmap to display
    delay(displayScrollSpeed);
  }
  matrix.setCursor(0, 0);
}

void centerPrint(const String &msg, boolean extraStuff) {
  int x = (matrix.width() - (msg.length() * width)) / 2;

  // Print the static portions of the display before the main Message
  if (extraStuff) {
    // We will have a shifting left-right on/off pattern on the two side and the bottom row
    // only displayed when there is an event happening on a given day.
    if (WAGFAM_EVENT_TODAY) {
      todayDisplayMilliSecond = millis() % (TODAY_DISPLAY_DOT_SPACING * TODAY_DISPLAY_DOT_SPEED_MS);
      todayDisplayStartingLED = todayDisplayMilliSecond / TODAY_DISPLAY_DOT_SPEED_MS;
      for (int i = 0; i < (matrix.height()*2+matrix.width()-2); i++) {
        if ((i % TODAY_DISPLAY_DOT_SPACING) == todayDisplayStartingLED) {
          if (i < matrix.height()) {
            // Far left matrix boundary
            matrix.drawPixel(0,i,HIGH);
          } else if (i < (matrix.height()+matrix.width()-2)) {
            // Bottom matrix boundary
            matrix.drawPixel(i-(matrix.height()-1),(matrix.height()-1),HIGH);
          } else {
            // Far right matrix boundary
            matrix.drawPixel(matrix.width()-1,((matrix.height()-1)-(i-(matrix.height()+matrix.width()-2))),HIGH);
          }
        }
      }
    }

    // Draw the PM pixel after the shifting pattern, so that we ensure that it's still on
    if (!IS_24HOUR && IS_PM && isPM()) {
      matrix.drawPixel(matrix.width() - 1, 6, HIGH);
    }
  }

  matrix.setCursor(x, 0);
  matrix.print(msg);
  matrix.write();
}

// ── REST API ────────────────────────────────────────────────────────────────

static void sendJsonResponse(AsyncWebServerRequest *request, int code, JsonDocument &doc) {
  AsyncResponseStream *response = request->beginResponseStream(F("application/json"));
  response->setCode(code);
  serializeJson(doc, *response);
  request->send(response);
}

static void sendJsonOk(AsyncWebServerRequest *request, const char *message = "ok") {
  JsonDocument doc;
  doc["status"] = message;
  sendJsonResponse(request, 200, doc);
}

static void sendJsonError(AsyncWebServerRequest *request, int code, const char *message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJsonResponse(request, code, doc);
}

void handleApiStatus(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["version"] = VERSION;
  doc["spa_version"] = SPA_VERSION;
  doc["uptime_ms"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["heap_fragmentation"] = ESP.getHeapFragmentation();
  doc["chip_id"] = String(ESP.getChipId(), HEX);
  doc["device_name"] = DEVICE_NAME;
  doc["flash_size"] = ESP.getFlashChipRealSize();
  doc["sketch_size"] = ESP.getSketchSize();
  doc["free_sketch_space"] = ESP.getFreeSketchSpace();
  doc["reset_reason"] = ESP.getResetReason();

  // Refresh schedule, mirroring the legacy footer "Next Update" countdown.
  // last_refresh_unix is 0 before the first successful fetch — clients should
  // treat both fields as advisory and not assume a non-zero countdown.
  doc["last_refresh_unix"] = (uint32_t)lastRefreshDataTimestamp;
  long timeToUpdate = (((minutesBetweenDataRefresh * 60) + lastRefreshDataTimestamp) - now());
  doc["next_refresh_in_sec"] = timeToUpdate;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = WiFi.SSID();
  wifi["ip"] = WiFi.localIP().toString();
  wifi["rssi"] = WiFi.RSSI();
  wifi["quality_pct"] = getWifiQuality();

  JsonObject ota = doc["ota"].to<JsonObject>();
  ota["confirm_at"] = otaConfirmAt;
  ota["pending_url"] = otaPendingNewUrl;
  ota["safe_url"] = OTA_SAFE_URL;
  ota["pending_file_exists"] = LittleFS.exists(OTA_PENDING_FILE);

  doc["spa_update_available"] = spaUpdateAvailable;
  doc["spa_fs_url"] = pendingSpaFsUrl;

  sendJsonResponse(request, 200, doc);
}

void handleApiConfigGet(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["wagfam_data_url"] = WAGFAM_DATA_URL;
  doc["wagfam_api_key"] = WAGFAM_API_KEY;
  doc["wagfam_event_today"] = WAGFAM_EVENT_TODAY;
  doc["owm_api_key"] = APIKEY;
  doc["geo_location"] = geoLocation;
  doc["is_24hour"] = IS_24HOUR;
  doc["is_pm"] = IS_PM;
  doc["is_metric"] = IS_METRIC;
  doc["display_intensity"] = displayIntensity;
  doc["display_scroll_speed"] = displayScrollSpeed;
  doc["minutes_between_data_refresh"] = minutesBetweenDataRefresh;
  doc["minutes_between_scrolling"] = minutesBetweenScrolling;
  doc["show_date"] = SHOW_DATE;
  doc["show_city"] = SHOW_CITY;
  doc["show_condition"] = SHOW_CONDITION;
  doc["show_humidity"] = SHOW_HUMIDITY;
  doc["show_wind"] = SHOW_WIND;
  doc["show_pressure"] = SHOW_PRESSURE;
  doc["show_highlow"] = SHOW_HIGHLOW;
  doc["ota_safe_url"] = OTA_SAFE_URL;
  doc["device_name"] = DEVICE_NAME;

  sendJsonResponse(request, 200, doc);
}

void handleApiConfigPost(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    sendJsonError(request, 400, "expected JSON object");
    return;
  }

  // Partial update: only set fields that are present in the request body
  if (json["wagfam_data_url"].is<const char*>()) WAGFAM_DATA_URL = json["wagfam_data_url"].as<String>();
  if (json["wagfam_api_key"].is<const char*>()) WAGFAM_API_KEY = json["wagfam_api_key"].as<String>();
  if (json["wagfam_event_today"].is<bool>()) WAGFAM_EVENT_TODAY = json["wagfam_event_today"].as<bool>();
  if (json["owm_api_key"].is<const char*>()) APIKEY = json["owm_api_key"].as<String>();
  if (json["geo_location"].is<const char*>()) geoLocation = json["geo_location"].as<String>();
  if (json["is_24hour"].is<bool>()) IS_24HOUR = json["is_24hour"].as<bool>();
  if (json["is_pm"].is<bool>()) IS_PM = json["is_pm"].as<bool>();
  if (json["is_metric"].is<bool>()) IS_METRIC = json["is_metric"].as<bool>();
  if (json["display_intensity"].is<int>()) displayIntensity = constrain(json["display_intensity"].as<int>(), 0, 15);
  if (json["display_scroll_speed"].is<int>()) displayScrollSpeed = max(1, json["display_scroll_speed"].as<int>());
  if (json["minutes_between_data_refresh"].is<int>()) minutesBetweenDataRefresh = max(1, json["minutes_between_data_refresh"].as<int>());
  if (json["minutes_between_scrolling"].is<int>()) minutesBetweenScrolling = max(1, json["minutes_between_scrolling"].as<int>());
  if (json["show_date"].is<bool>()) SHOW_DATE = json["show_date"].as<bool>();
  if (json["show_city"].is<bool>()) SHOW_CITY = json["show_city"].as<bool>();
  if (json["show_condition"].is<bool>()) SHOW_CONDITION = json["show_condition"].as<bool>();
  if (json["show_humidity"].is<bool>()) SHOW_HUMIDITY = json["show_humidity"].as<bool>();
  if (json["show_wind"].is<bool>()) SHOW_WIND = json["show_wind"].as<bool>();
  if (json["show_pressure"].is<bool>()) SHOW_PRESSURE = json["show_pressure"].as<bool>();
  if (json["show_highlow"].is<bool>()) SHOW_HIGHLOW = json["show_highlow"].as<bool>();

  savePersistentConfig();
  sendJsonOk(request, "config updated");
}

void handleApiRestart(AsyncWebServerRequest *request) {
  // Rate-limit restarts to prevent reboot-loop DoS
  if (lastRestartMs != 0 && (millis() - lastRestartMs) < 60000) {
    sendJsonError(request, 429, "restart rate limited (60s cooldown)");
    return;
  }
  lastRestartMs = millis();
  sendJsonOk(request, "restarting");
  // Defer the actual restart so the async TCP layer can transmit the response.
  restartAtMs = millis() + 1000;
  restartRequested = true;
}

void handleApiRefresh(AsyncWebServerRequest *request) {
  // Queue work for the main loop — see weatherRefreshRequested comment.
  // Caller can poll /api/status to see lastRefreshDataTimestamp move.
  weatherRefreshRequested = true;
  sendJsonOk(request, "refresh queued");
}

void handleApiOtaStatus(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["pending_file_exists"] = LittleFS.exists(OTA_PENDING_FILE);
  doc["confirm_at"] = otaConfirmAt;
  doc["pending_url"] = otaPendingNewUrl;
  doc["safe_url"] = OTA_SAFE_URL;

  if (LittleFS.exists(OTA_PENDING_FILE)) {
    File f = LittleFS.open(OTA_PENDING_FILE, "r");
    JsonObject file = doc["file_contents"].to<JsonObject>();
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      int eq = line.indexOf('=');
      if (eq <= 0) continue;
      file[line.substring(0, eq)] = line.substring(eq + 1);
    }
    f.close();
  }

  sendJsonResponse(request, 200, doc);
}

// POST /api/spa/update-from-url — queue a SPA (LittleFS image) OTA from a URL.
// Accepts {"url": "http://..."} and defers the flash to the main loop so the
// async handler can return before the multi-second download begins.
// /conf.txt is preserved: doOtaFsFlash() reads it before unmounting and writes
// it back to the new FS before rebooting (Part 4 of issue #72).
void handleApiSpaUpdateFromUrl(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    sendJsonError(request, 400, "expected JSON object");
    return;
  }
  String url = json["url"] | "";
  if (url.isEmpty()) {
    sendJsonError(request, 400, "missing 'url' field");
    return;
  }
  if (!url.startsWith("http://")) {
    sendJsonError(request, 400, "url must start with http:// — HTTPS not supported");
    return;
  }
  if (otaFsFromUrlRequested) {
    sendJsonError(request, 409, "SPA update already in progress");
    return;
  }

  pendingOtaFsUrl = url;
  otaFsFromUrlRequested = true;

  JsonDocument doc;
  doc["status"] = "update queued";
  doc["url"] = url;
  sendJsonResponse(request, 202, doc);
}

// Mirror of the legacy home page weather card. Pulled from the
// WeatherClient cache (refreshed every minutesBetweenDataRefresh from
// the main loop). `data_valid=false` means the SPA should render the
// "Weather not configured / fetch failed" card and surface error_message.
void handleApiWeather(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["data_valid"] = weatherClient.getWeatherDataValid();
  doc["city"] = weatherClient.getCity();
  doc["country"] = weatherClient.getCountry();
  doc["temperature"] = weatherClient.getTemperature();
  doc["temp_high"] = weatherClient.getTemperatureHigh();
  doc["temp_low"] = weatherClient.getTemperatureLow();
  doc["humidity"] = weatherClient.getHumidity();
  doc["pressure"] = weatherClient.getPressure();
  doc["wind_speed"] = weatherClient.getWindSpeed();
  doc["wind_direction_deg"] = weatherClient.getWindDirection();
  doc["wind_direction_text"] = weatherClient.getWindDirectionText();
  doc["condition"] = weatherClient.getWeatherCondition();
  doc["description"] = weatherClient.getWeatherDescription();
  doc["icon"] = weatherClient.getIcon();
  doc["weather_id"] = weatherClient.getWeatherId();
  doc["is_metric"] = IS_METRIC;
  doc["temp_symbol"] = IS_METRIC ? "C" : "F";
  doc["speed_symbol"] = IS_METRIC ? "kmh" : "mph";
  doc["pressure_symbol"] = IS_METRIC ? "mb" : "inHg";
  doc["error_message"] = weatherClient.getErrorMessage();
  sendJsonResponse(request, 200, doc);
}

// Mirror of the legacy home page upcoming-events list. Returns the
// messages array sourced from the WagFamBdayClient cache. SPA renders
// the "No upcoming events" placeholder when count == 0.
void handleApiEvents(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["count"] = bdayClient.getNumMessages();
  JsonArray messages = doc["messages"].to<JsonArray>();
  for (int i = 0; i < bdayClient.getNumMessages(); i++) {
    messages.add(bdayClient.getMessage(i));
  }
  doc["calendar_url_configured"] = WAGFAM_DATA_URL != "";
  doc["calendar_key_configured"] = WAGFAM_API_KEY != "";
  sendJsonResponse(request, 200, doc);
}

// Replaces the legacy GET /systemreset. Delete /conf.txt and reboot
// into compile-time defaults from Settings.h. Restart is deferred via
// the existing restartRequested flag so the response (and the TCP FIN)
// reach the client before the network stack tears down.
void handleApiSystemReset(AsyncWebServerRequest *request) {
  Serial.println(F("[API] System reset requested — clearing /conf.txt"));
  bool ok = LittleFS.remove(CONFIG);
  JsonDocument doc;
  doc["status"] = ok ? "ok" : "config_already_absent";
  doc["restart_in_ms"] = 1000;
  sendJsonResponse(request, 200, doc);
  restartAtMs = millis() + 1000;
  restartRequested = true;
}

// Replaces the legacy GET /forgetwifi. Use a local AsyncWiFiManager
// instance to clear the stored credentials (same pattern as the legacy
// route — we don't carry a long-lived instance because the captive
// portal only runs at boot when no creds are stored).
void handleApiForgetWifi(AsyncWebServerRequest *request) {
  Serial.println(F("[API] Forget WiFi requested"));
  JsonDocument doc;
  doc["status"] = "ok";
  doc["restart_in_ms"] = 1000;
  sendJsonResponse(request, 200, doc);
  AsyncWiFiManager wifiManager(&server, &dnsServer);
  wifiManager.resetSettings();
  restartAtMs = millis() + 1000;
  restartRequested = true;
}

void handleApiFsRead(AsyncWebServerRequest *request) {
  String path = request->arg("path");
  if (path == "") { sendJsonError(request, 400, "missing 'path' parameter"); return; }
  if (!LittleFS.exists(path)) { sendJsonError(request, 404, "file not found"); return; }

  File f = LittleFS.open(path, "r");
  if (f.size() > 4096) {
    f.close();
    sendJsonError(request, 413, "file too large (max 4KB)");
    return;
  }
  String content = f.readString();
  f.close();

  JsonDocument doc;
  doc["path"] = path;
  doc["size"] = content.length();
  doc["content"] = content;
  sendJsonResponse(request, 200, doc);
}

void handleApiFsWrite(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) { sendJsonError(request, 400, "expected JSON object"); return; }

  const char *path = json["path"];
  const char *content = json["content"];
  if (!path || !content) { sendJsonError(request, 400, "missing 'path' or 'content'"); return; }

  // SEC-06: Block writes to critical system files
  if (isProtectedPath(path, CONFIG, OTA_PENDING_FILE)) {
    sendJsonError(request, 403, "protected path — use /api/config instead");
    return;
  }

  File f = LittleFS.open(path, "w");
  if (!f) { sendJsonError(request, 500, "failed to open file for writing"); return; }
  f.print(content);
  f.close();

  JsonDocument resp;
  resp["status"] = "written";
  resp["path"] = path;
  resp["size"] = (int)strlen(content);
  sendJsonResponse(request, 200, resp);
}

void handleApiFsDelete(AsyncWebServerRequest *request) {
  String path = request->arg("path");
  if (path == "") { sendJsonError(request, 400, "missing 'path' parameter"); return; }
  if (!LittleFS.exists(path)) { sendJsonError(request, 404, "file not found"); return; }

  // SEC-06: Block deletion of critical system files
  if (isProtectedPath(path.c_str(), CONFIG, OTA_PENDING_FILE)) {
    sendJsonError(request, 403, "protected path");
    return;
  }

  LittleFS.remove(path);
  sendJsonOk(request, "deleted");
}

static void listFilesRecursive(const String &path, JsonArray &files) {
  Dir dir = LittleFS.openDir(path);
  while (dir.next()) {
    String entryPath = path;
    if (!entryPath.endsWith("/")) entryPath += "/";
    entryPath += dir.fileName();
    if (dir.isDirectory()) {
      listFilesRecursive(entryPath, files);
    } else {
      JsonObject entry = files.add<JsonObject>();
      entry["name"] = entryPath;
      entry["size"] = dir.fileSize();
    }
  }
}

void handleApiFsList(AsyncWebServerRequest *request) {
  JsonDocument doc;
  JsonArray files = doc["files"].to<JsonArray>();

  listFilesRecursive("/", files);

  sendJsonResponse(request, 200, doc);
}

// ── End REST API ────────────────────────────────────────────────────────────

String EncodeUrlSpecialChars(const char *msg) {
const static char special[] = {'\x20','\x22','\x23','\x24','\x25','\x26','\x2B','\x3B','\x3C','\x3D','\x3E','\x3F','\x40'};
  String encoded;
  int inIdx;
  char ch, hex;
  bool convert;
  const int inLen = strlen(msg);
  //Serial.printf_P(PSTR("EncodeURL in:  %s\n"), msg);
  encoded.reserve(inLen+128);

  for (inIdx=0; inIdx < inLen; inIdx++) {
    ch = msg[inIdx];
    convert = false;
    if (ch < ' ') {
      convert = true; // this includes 0x80-0xFF !
    }
    // find ch in table
    for (int i=0; i < (int)sizeof(special) && !convert; i++) {
      if (special[i] == ch) convert = true;
    }
    if (convert) {
      // convert character to "%HEX"
      encoded += '%';
      hex = (ch >> 4) & 0x0F;
      hex += '0';
      if (hex > '9') hex += 7;
      encoded += hex;
      hex = ch & 0x0F;
      hex += '0';
      if (hex > '9') hex += 7;
      encoded += hex;
    }
    else {
      encoded += ch;
    }
  }
  //Serial.printf_P(PSTR("EncodeURL out: %s\n"), encoded.c_str());
  return encoded;
}