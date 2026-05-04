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

#define BASE_VERSION "3.10.0-wagfam"
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
void handlePull(AsyncWebServerRequest *request);
void handleSaveConfig(AsyncWebServerRequest *request);
void handleSystemReset(AsyncWebServerRequest *request);
void handleForgetWifi(AsyncWebServerRequest *request);
void handleConfigure(AsyncWebServerRequest *request);
void getWeatherData();
void redirectHome(AsyncWebServerRequest *request);
void handleNotFound(AsyncWebServerRequest *request);
void sendHeader(AsyncResponseStream *response);
void sendFooter(AsyncResponseStream *response);
void displayHomePage(AsyncWebServerRequest *request);
void configModeCallback (AsyncWiFiManager *myWiFiManager);
void flashLED(int number, int delayTime);
String getTempSymbol(bool forWeb = false);
String getSpeedSymbol();
String getPressureSymbol();
int8_t getWifiQuality();
String getTimeTillUpdate();
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

static const char WEB_ACTIONS1[] PROGMEM = "<a class='w3-bar-item w3-button' href='/'><i class='fas fa-home'></i> Home</a>"
                        "<a class='w3-bar-item w3-button' href='/configure'><i class='fas fa-cog'></i> Configure</a>";

static const char WEB_ACTIONS2[] PROGMEM = "<a class='w3-bar-item w3-button' href='/pull'><i class='fas fa-cloud-download-alt'></i> Refresh Data</a>";

static const char WEB_ACTION3[] PROGMEM = "</a><a class='w3-bar-item w3-button' href='/systemreset' onclick='return confirm(\"Do you want to reset to default weather settings?\")'><i class='fas fa-undo'></i> Reset Settings</a>"
                       "<a class='w3-bar-item w3-button' href='/forgetwifi' onclick='return confirm(\"Do you want to forget to WiFi connection?\")'><i class='fas fa-wifi'></i> Forget WiFi</a>"
                       "<a class='w3-bar-item w3-button' href='/update'><i class='fas fa-wrench'></i> Firmware Update</a>";

static const char CHANGE_FORM1[] PROGMEM = "<form class='w3-container' action='/saveconfig' method='post'><h2>Configure:</h2>"
                      "<label>WagFam Calendar Data Source</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='wagFamDataSource' value='%WAGFAMDATASOURCE%' maxlength='256'>"
                      "<label>WagFam Calendar API Key</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='wagFamApiKey' value='%WAGFAMAPIKEY%' maxlength='128'>"
                      "<hr>";

static const char CHANGE_FORM2[] PROGMEM = "<label>OpenWeatherMap API Key (get from <a href='https://openweathermap.org/' target='_BLANK'>here</a>)</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='openWeatherMapApiKey' value='%WEATHERKEY%' maxlength='70'>"
                      "<p><label>%CITYNAME1% (<a href='https://openweathermap.org/find' target='_BLANK'><i class='fas fa-search'></i> Search for City ID</a>)</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='city1' value='%CITY1%' onkeypress='return isNumberKey(event)'></p>"
                      "<p><input name='metric' class='w3-check w3-margin-top' type='checkbox' %CHECKED%> Use Metric (Celsius)</p>"
                      "<p><input name='showdate' class='w3-check w3-margin-top' type='checkbox' %DATE_CHECKED%> Display Date</p>"
                      "<p><input name='showcity' class='w3-check w3-margin-top' type='checkbox' %CITY_CHECKED%> Display City Name</p>"
                      "<p><input name='showhighlow' class='w3-check w3-margin-top' type='checkbox' %HIGHLOW_CHECKED%> Display Current High/Low Temperatures</p>"
                      "<p><input name='showcondition' class='w3-check w3-margin-top' type='checkbox' %CONDITION_CHECKED%> Display Weather Condition</p>"
                      "<p><input name='showhumidity' class='w3-check w3-margin-top' type='checkbox' %HUMIDITY_CHECKED%> Display Humidity</p>"
                      "<p><input name='showwind' class='w3-check w3-margin-top' type='checkbox' %WIND_CHECKED%> Display Wind</p>"
                      "<p><input name='showpressure' class='w3-check w3-margin-top' type='checkbox' %PRESSURE_CHECKED%> Display Barometric Pressure</p>"
                      "<p><input name='is24hour' class='w3-check w3-margin-top' type='checkbox' %IS_24HOUR_CHECKED%> Use 24 Hour Clock (military time)</p>";

static const char CHANGE_FORM3[] PROGMEM = "<p><input name='isPM' class='w3-check w3-margin-top' type='checkbox' %IS_PM_CHECKED%> Show PM indicator (only 12h format)</p>"
                      "<p>Display Brightness <input class='w3-border w3-margin-bottom' name='ledintensity' type='number' min='0' max='15' value='%INTENSITYOPTIONS%'></p>"
                      "<p>Display Scroll Speed <select class='w3-option w3-padding' name='scrollspeed'>%SCROLLOPTIONS%</select></p>"
                      "<p>Minutes Between Refresh Data <select class='w3-option w3-padding' name='refresh'>%OPTIONS%</select></p>"
                      "<p>Minutes Between Scrolling Data <input class='w3-border w3-margin-bottom' name='refreshDisplay' type='number' min='1' max='10' value='%REFRESH_DISPLAY%'></p>";

static const char CHANGE_FORM4[] PROGMEM = "<p><button class='w3-button w3-block w3-green w3-section w3-padding' type='submit'>Save</button></p></form>"
                      "<script>function isNumberKey(e){var h=e.which?e.which:event.keyCode;return!(h>31&&(h<48||h>57))}</script>";

static const char UPDATE_FORM[] PROGMEM = "<form class='w3-container' action='/updateFromUrl' method='get'><h2>Firmware Update Options:</h2>"
                      "<p><label>Firmware Update URL (optional)</label><input class='w3-input w3-border w3-margin-bottom' type='url' name='firmwareUrl' placeholder='http://example.com/firmware.bin' maxlength='256' required></p>"
                      "<p><button class='w3-button w3-block w3-blue w3-section w3-padding' type='submit'>Update from URL</button></p>"
                      "<p><small>Note: You can also use the <a href='/update'>Firmware Update</a> page to upload a file directly.</small></p>"
                      "<p><small>Need to push the SPA bundle (LittleFS image) to a deployed device? Use <a href='/updatefs'>LittleFS Upload</a>.</small></p></form>";

// LittleFS-upload form rendered by GET /updatefs. This is the OTA path for the
// SPA bundle on already-deployed devices — without it, /spa can only be
// installed via serial flash. The Update library accepts U_FS images and writes
// them directly to the LittleFS partition; on success the device reboots and
// remounts the new FS. Wipes /conf.txt — same caveat as `make uploadfs`.
static const char UPDATEFS_FORM[] PROGMEM = "<form class='w3-container' method='POST' action='/updatefs' enctype='multipart/form-data'><h2>LittleFS Upload (SPA bundle):</h2>"
                      "<p><label>LittleFS Image (littlefs.bin)</label><input class='w3-input w3-border w3-margin-bottom' type='file' name='updatefs' accept='.bin' required></p>"
                      "<p><button class='w3-button w3-block w3-blue w3-section w3-padding' type='submit'>Upload &amp; Flash FS</button></p>"
                      "<p><small><strong>Warning:</strong> this wipes the entire LittleFS partition, including <code>/conf.txt</code> "
                      "(web password, calendar URL, API keys). You'll need to reconfigure WiFi and settings after the device reboots.</small></p>"
                      "<p><small>Looking for the firmware sketch upload? Use <a href='/update'>Firmware Update</a>.</small></p></form>";

// LittleFS partition bounds — defined by the linker for the configured flash
// layout (4MB FS:1MB on d1_mini in this project; see platformio.ini). Used by
// /updatefs to size the U_FS update so it spans the entire partition.
extern "C" uint32_t _FS_start;
extern "C" uint32_t _FS_end;

// File-upload form rendered by GET /update. ESP8266HTTPUpdateServer used to register
// both GET (form) and POST (upload) on /update; when we dropped that lib in the async
// migration, only the POST half was reimplemented, so GET fell through to onNotFound
// and redirected to "/". This form posts the firmware binary back to POST /update.
static const char UPLOAD_FORM[] PROGMEM = "<form class='w3-container' method='POST' action='/update' enctype='multipart/form-data'><h2>Firmware Upload:</h2>"
                      "<p><label>Firmware Binary (.bin)</label><input class='w3-input w3-border w3-margin-bottom' type='file' name='update' accept='.bin' required></p>"
                      "<p><button class='w3-button w3-block w3-blue w3-section w3-padding' type='submit'>Upload &amp; Flash</button></p>"
                      "<p><small>The device will reboot automatically when the upload completes.</small></p></form>";

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
  server.on("/", HTTP_GET, displayHomePage);
  server.on("/pull", HTTP_GET, handlePull);
  server.on("/systemreset", HTTP_GET, handleSystemReset);
  server.on("/forgetwifi", HTTP_GET, handleForgetWifi);
  server.on("/configure", HTTP_GET, handleConfigure);
  server.on("/saveconfig", HTTP_POST, handleSaveConfig);
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

  // GET /update — render the file-upload form. This used to be served implicitly
  // by ESP8266HTTPUpdateServer; the async migration only reimplemented POST.
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
    response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
    response->addHeader(F("Pragma"), F("no-cache"));
    response->addHeader(F("Expires"), F("-1"));
    sendHeader(response);
    response->print(FPSTR(UPLOAD_FORM));
    sendFooter(response);
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

  // GET /updatefs — render the LittleFS upload form. Pair to POST /updatefs;
  // this is the OTA path for shipping the SPA bundle to deployed devices
  // without serial flashing (issue #63 follow-up).
  server.on("/updatefs", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
    response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
    response->addHeader(F("Pragma"), F("no-cache"));
    response->addHeader(F("Expires"), F("-1"));
    sendHeader(response);
    response->print(FPSTR(UPDATEFS_FORM));
    sendFooter(response);
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
  // Cache for 10 min — short enough that a UI bugfix lands within a reasonable
  // window after reflashing the FS.
  server.serveStatic("/spa", LittleFS, "/spa/")
    .setDefaultFile("index.html")
    .setCacheControl("public, max-age=600");

  // notFound dispatch — a request for /spa/* that didn't match serveStatic
  // means the LittleFS doesn't have the bundle (the most common cause:
  // user OTA-flashed firmware but never flashed LittleFS). Render an
  // explanatory page rather than silently redirecting to /, which makes it
  // look like the SPA is broken instead of just absent (issue #63).
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

void handlePull(AsyncWebServerRequest *request) {
  // Queue refresh for the main loop (async handlers can't block on HTTPS calls
  // without tripping the watchdog). Home page renders with current data; the
  // user can reload after ~30s to see the freshly fetched results.
  weatherRefreshRequested = true;
  displayHomePage(request);
}

void handleSaveConfig(AsyncWebServerRequest *request) {
  WAGFAM_DATA_URL = request->arg("wagFamDataSource");
  WAGFAM_API_KEY = request->arg("wagFamApiKey");
  bdayClient.updateBdayClient(WAGFAM_API_KEY,WAGFAM_DATA_URL);
  APIKEY = request->arg("openWeatherMapApiKey");
  geoLocation = request->arg("city1");
  IS_24HOUR = request->hasArg("is24hour");
  IS_PM = request->hasArg("isPM");
  SHOW_DATE = request->hasArg("showdate");
  SHOW_CITY = request->hasArg("showcity");
  SHOW_CONDITION = request->hasArg("showcondition");
  SHOW_HUMIDITY = request->hasArg("showhumidity");
  SHOW_WIND = request->hasArg("showwind");
  SHOW_PRESSURE = request->hasArg("showpressure");
  SHOW_HIGHLOW = request->hasArg("showhighlow");
  IS_METRIC = request->hasArg("metric");
  displayIntensity = constrain(request->arg("ledintensity").toInt(), 0, 15);
  minutesBetweenDataRefresh = max(1, (int)request->arg("refresh").toInt());
  minutesBetweenScrolling = max(1, (int)request->arg("refreshDisplay").toInt());
  displayScrollSpeed = max(1, (int)request->arg("scrollspeed").toInt());
  weatherClient.setMetric(IS_METRIC);
  weatherClient.setGeoLocation(geoLocation);
  matrix.fillScreen(LOW); // show black
  savePersistentConfig();
  weatherRefreshRequested = true; // deferred to main loop (see flag comment)
  redirectHome(request);
}

void handleSystemReset(AsyncWebServerRequest *request) {
  Serial.println("Reset System Configuration");
  if (LittleFS.remove(CONFIG)) {
    redirectHome(request);
    ESP.restart();
  }
}

void handleForgetWifi(AsyncWebServerRequest *request) {
  //ESPAsyncWiFiManager — local instance only used to clear stored credentials
  redirectHome(request);
  AsyncWiFiManager wifiManager(&server, &dnsServer);
  wifiManager.resetSettings();
  ESP.restart();
}

void handleConfigure(AsyncWebServerRequest *request) {
  digitalWrite(externalLight, LOW);

  AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
  response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
  response->addHeader(F("Pragma"), F("no-cache"));
  response->addHeader(F("Expires"), F("-1"));

  sendHeader(response);

  String form = FPSTR(CHANGE_FORM1);
  form.replace("%WAGFAMDATASOURCE%", WAGFAM_DATA_URL);
  form.replace("%WAGFAMAPIKEY%", WAGFAM_API_KEY);
  response->print(form);


  form = FPSTR(CHANGE_FORM2);
  form.replace("%WEATHERKEY%", APIKEY);

  String cityName = "";
  if (weatherClient.getCity() != "") {
    cityName = weatherClient.getCity() + ", " + weatherClient.getCountry();
  }
  form.replace("%CITYNAME1%", cityName);
  form.replace("%CITY1%", geoLocation);
  String isDateChecked = "";
  if (SHOW_DATE) {
    isDateChecked = "checked='checked'";
  }
  form.replace("%DATE_CHECKED%", isDateChecked);
  String isCityChecked = "";
  if (SHOW_CITY) {
    isCityChecked = "checked='checked'";
  }
  form.replace("%CITY_CHECKED%", isCityChecked);
  String isConditionChecked = "";
  if (SHOW_CONDITION) {
    isConditionChecked = "checked='checked'";
  }
  form.replace("%CONDITION_CHECKED%", isConditionChecked);
  String isHumidityChecked = "";
  if (SHOW_HUMIDITY) {
    isHumidityChecked = "checked='checked'";
  }
  form.replace("%HUMIDITY_CHECKED%", isHumidityChecked);
  String isWindChecked = "";
  if (SHOW_WIND) {
    isWindChecked = "checked='checked'";
  }
  form.replace("%WIND_CHECKED%", isWindChecked);
  String isPressureChecked = "";
  if (SHOW_PRESSURE) {
    isPressureChecked = "checked='checked'";
  }
  form.replace("%PRESSURE_CHECKED%", isPressureChecked);

  String isHighlowChecked = "";
  if (SHOW_HIGHLOW) {
    isHighlowChecked = "checked='checked'";
  }
  form.replace("%HIGHLOW_CHECKED%", isHighlowChecked);

  String is24hourChecked = "";
  if (IS_24HOUR) {
    is24hourChecked = "checked='checked'";
  }
  form.replace("%IS_24HOUR_CHECKED%", is24hourChecked);
  String checked = "";
  if (IS_METRIC) {
    checked = "checked='checked'";
  }
  form.replace("%CHECKED%", checked);
  response->print(form);

  form = FPSTR(CHANGE_FORM3);
  String isPmChecked = "";
  if (IS_PM) {
    isPmChecked = "checked='checked'";
  }
  form.replace("%IS_PM_CHECKED%", isPmChecked);
  form.replace("%INTENSITYOPTIONS%", String(displayIntensity));
  String dSpeed = String(displayScrollSpeed);
  String scrollOptions = "<option value='35'>Slow</option><option value='25'>Normal</option><option value='15'>Fast</option><option value='10'>Very Fast</option>";
  scrollOptions.replace(dSpeed + "'", dSpeed + "' selected" );
  form.replace("%SCROLLOPTIONS%", scrollOptions);
  String minutes = String(minutesBetweenDataRefresh);
  String options = "<option>5</option><option>10</option><option>15</option><option>20</option><option>30</option><option>60</option>";
  options.replace(">" + minutes + "<", " selected>" + minutes + "<");
  form.replace("%OPTIONS%", options);
  form.replace("%REFRESH_DISPLAY%", String(minutesBetweenScrolling));

  response->print(form);

  response->print(FPSTR(CHANGE_FORM4));

  // Firmware update form
  response->print(FPSTR(UPDATE_FORM));

  sendFooter(response);

  request->send(response);
  digitalWrite(externalLight, HIGH);
}

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

void handleUpdateFromUrl(AsyncWebServerRequest *request) {
  String firmwareUrl = request->arg("firmwareUrl");

  AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
  response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
  response->addHeader(F("Pragma"), F("no-cache"));
  response->addHeader(F("Expires"), F("-1"));

  sendHeader(response);

  int has_error = 0;
  String error_message = "";

  if (firmwareUrl == "") {
    error_message = PSTR("Error: No firmware URL provided");
    Serial.printf_P(PSTR("%s\n"),error_message);
    has_error = 1;
  }

  // Validate URL format (only HTTP is supported)
  if (!firmwareUrl.startsWith("http://")) {
    error_message = PSTR("Error: Invalid URL format. Must start with http:// HTTPS is NOT SUPPORTED\n");
    Serial.printf_P(PSTR("%s\n"),error_message);
    has_error = 1;
  }

  if (has_error != 0) {
    response->print("<p>ERROR: "+error_message+"</p>");
    sendFooter(response);
    request->send(response);
    return;
  }

  response->print("<p>STARTING UPDATE from "+firmwareUrl+"</p><p>The device will reboot when the update completes.</p>");
  sendFooter(response);
  request->send(response);

  // Defer the actual flash to the main loop — async handlers can't block on
  // the 20–30s ESPhttpUpdate.update() call without crashing the event loop.
  pendingOtaUrl = firmwareUrl;
  otaFromUrlRequested = true;
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

  // Check for a remote firmware update pushed via the calendar config
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

  Serial.println("Version: " + String(VERSION));
  Serial.println();
  digitalWrite(externalLight, HIGH);
}

void redirectHome(AsyncWebServerRequest *request) {
  // Send them back to the Root Directory
  AsyncWebServerResponse *response = request->beginResponse(302, F("text/plain"), F(""));
  response->addHeader(F("Location"), F("/"));
  response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
  response->addHeader(F("Pragma"), F("no-cache"));
  response->addHeader(F("Expires"), F("-1"));
  request->send(response);
}

// 404 dispatch. /spa* requests that fall through here mean the SPA bundle
// isn't on LittleFS — almost always because the user OTA-flashed firmware
// without flashing LittleFS too (issue #63). Returning a 404 with deploy
// instructions makes the failure mode self-explanatory; everything else
// falls through to the legacy redirect-home behavior.
void handleNotFound(AsyncWebServerRequest *request) {
  if (request->url().startsWith("/spa")) {
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
      "<li><strong>Easiest (no serial cable):</strong> <a href='/updatefs'>upload <code>littlefs.bin</code> via OTA</a> — "
      "browser-only, but wipes <code>/conf.txt</code></li>"
      "<li>From a checkout: <code>make uploadfs</code> (serial flash, also wipes <code>/conf.txt</code>)</li>"
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
  redirectHome(request);
}

void sendHeader(AsyncResponseStream *response) {
  String html = "<!DOCTYPE HTML>";
  html += "<html><head><title>Marquee Scroller</title><link rel='icon' href='data:;base64,='>";
  html += "<meta http-equiv='Content-Type' content='text/html; charset=UTF-8' />";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>";
  html += "<link rel='stylesheet' href='https://www.w3schools.com/lib/w3-theme-blue-grey.css'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.8.1/css/all.min.css'>";
  html += "</head><body>";
  response->print(html);
  html = "<nav class='w3-sidebar w3-bar-block w3-card' style='margin-top:88px' id='mySidebar'>";
  html += "<div class='w3-container w3-theme-d2'>";
  html += "<span onclick='closeSidebar()' class='w3-button w3-display-topright w3-large'><i class='fas fa-times'></i></span>";
  html += "<div class='w3-left'><img src='https://openweathermap.org/img/w/" + weatherClient.getIcon() + ".png' alt='" + weatherClient.getWeatherDescription() + "'></div>";
  html += "<div class='w3-padding'>Menu</div></div>";
  response->print(html);

  response->print(FPSTR(WEB_ACTIONS1));
  response->print(FPSTR(WEB_ACTIONS2));
  response->print(FPSTR(WEB_ACTION3));

  html = "</nav>";
  html += "<header class='w3-top w3-bar w3-theme'><button class='w3-bar-item w3-button w3-xxxlarge w3-hover-theme' onclick='openSidebar()'><i class='fas fa-bars'></i></button><h2 class='w3-bar-item'>WagFam CalClock</h2></header>";
  html += "<script>";
  html += "function openSidebar(){document.getElementById('mySidebar').style.display='block'}function closeSidebar(){document.getElementById('mySidebar').style.display='none'}closeSidebar();";
  html += "</script>";
  html += "<br><div class='w3-container w3-large' style='margin-top:88px'>";
  response->print(html);
}

void sendFooter(AsyncResponseStream *response) {
  int8_t rssi = getWifiQuality();
  String html = "<br><br><br>";
  html += "</div>";
  html += "<footer class='w3-container w3-bottom w3-theme w3-margin-top'>";
  html += "<i class='far fa-paper-plane'></i> Version: " + String(VERSION) + "<br>";
  html += "<i class='far fa-clock'></i> Next Update: " + getTimeTillUpdate() + "<br>";
  html += "<i class='fas fa-rss'></i> Signal Strength: ";
  html += String(rssi) + "%";
  html += "</footer>";
  html += "</body></html>";
  response->print(html);
}

void displayHomePage(AsyncWebServerRequest *request) {
  digitalWrite(externalLight, LOW);
  String html = "";

  AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
  response->addHeader(F("Cache-Control"), F("no-cache, no-store"));
  response->addHeader(F("Pragma"), F("no-cache"));
  response->addHeader(F("Expires"), F("-1"));
  sendHeader(response);

  // Send Over the Main Wagner Family Data Section first
  if (WAGFAM_DATA_URL == "") {
    html += "<div class='w3-cell-row' style='width:100%'><p>Please <a href='/configure'>Configure</a> WagFam Calendar Data Source</p></div>";
  }
  if (WAGFAM_API_KEY == "") {
    html += "<div class='w3-cell-row' style='width:100%'><p>Please <a href='/configure'>Configure</a> WagFam Calendar API Key</p></div>";
  }

  if (bdayClient.getNumMessages() == 0) {
    html += "<div class='w3-cell-row' style='width:100%'><h2>No Upcoming Events!</h2></div>";
  } else {
    html += "<div class='w3-cell-row' style='width:100%'><h2>Upcoming Events</h2></div>";
    html += "<div class='w3-cell-row' style='width:100%'><ul>";
    for (int i = 0; i < bdayClient.getNumMessages(); i++) {
      html += "<li>" + bdayClient.getMessage(i) + "</li>";
    }
    html += "</ul></div>";
  }
  html += "<hr>";
  response->print(html);
  html = "";


  // Next send over the Weather Data Section
  String temperature = String(weatherClient.getTemperature(),0);

  if ((temperature.indexOf(".") != -1) && (temperature.length() >= (temperature.indexOf(".") + 2))) {
    temperature.remove(temperature.indexOf(".") + 2);
  }

  String time = getDayName(weekday()) + ", " + getMonthName(month()) + " " + day() + ", " + hourFormat12() + ":" + zeroPad(minute()) + " " + getAmPm(isPM());

  if (weatherClient.getCity() == "") {
    html += "<p>Please <a href='/configure'>Configure Weather</a> API</p>";
    if (weatherClient.getErrorMessage() != "") {
      html += "<p>Weather Error: <strong>" + weatherClient.getErrorMessage() + "</strong></p>";
    }
  } else {
    html += "<div class='w3-cell-row' style='width:100%'><h2>Weather for " + weatherClient.getCity() + ", " + weatherClient.getCountry() + "</h2></div><div class='w3-cell-row'>";
    html += "<div class='w3-cell w3-left w3-medium' style='width:120px'>";
    html += "<img src='https://openweathermap.org/img/w/" + weatherClient.getIcon() + ".png' alt='" + weatherClient.getWeatherDescription() + "'><br>";
    html += String(weatherClient.getHumidity(),0) + "% Humidity<br>";
    html += weatherClient.getWindDirectionText() + " / " + String(weatherClient.getWindSpeed(),0) + " <span class='w3-tiny'>" + getSpeedSymbol() + "</span> Wind<br>";
    html += String(weatherClient.getPressure()) + " Pressure<br>";
    html += "</div>";
    html += "<div class='w3-cell w3-container' style='width:100%'><p>";
    html += weatherClient.getWeatherCondition() + " (" + weatherClient.getWeatherDescription() + ")<br>";
    html += temperature + " " + getTempSymbol(true) + "<br>";
    html += String(weatherClient.getTemperatureHigh(),0) + "/" + String(weatherClient.getTemperatureLow(),0) + " " + getTempSymbol(true) + "<br>";
    html += time + "<br>";
    html += "</p></div></div><hr>";
  }


  response->print(html);
  sendFooter(response);
  request->send(response);
  digitalWrite(externalLight, HIGH);
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

String getTimeTillUpdate() {
  char hms[10];
  long timeToUpdate = (((minutesBetweenDataRefresh * 60) + lastRefreshDataTimestamp) - now());

  int hours = numberOfHours(timeToUpdate);
  int minutes = numberOfMinutes(timeToUpdate);
  int seconds = numberOfSeconds(timeToUpdate);
  sprintf_P(hms, PSTR("%d:%02d:%02d"), hours, minutes, seconds);

  return String(hms);
}

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