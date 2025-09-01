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

#define VERSION "3.05.2-wagfam"

#define HOSTNAME "CLOCK-"
#define CONFIG "/conf.txt"

//declairing prototypes
void configModeCallback (WiFiManager *myWiFiManager);
int8_t getWifiQuality();

// LED Settings
int spacer = 1;  // dots between letters
int width = 5 + spacer; // The font width is 5 pixels + spacer
Max72xxPanel matrix = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);
float UtcOffset;  //time zone offsets that correspond with the CityID above (offset from GMT)

// Time
TimeDB TimeDB("");
String lastMinute = "xx";
int displayRefreshCount = 1;
long lastEpoch = 0;
long firstEpoch = 0;

// WagFam Calendar Client
WagFamBdayClient bdayClient(WAGFAM_API_KEY, WAGFAM_DATA_URL);
int bdayMessageIndex = 0;
WagFamBdayClient::configValues serverConfig = {};
int todayDisplayMilliSecond = 0;
int todayDisplayStartingLED = 0;

// Weather Client
OpenWeatherMapClient weatherClient(APIKEY, IS_METRIC);
// (some) Default Weather Settings
boolean SHOW_DATE = false;
boolean SHOW_CITY = true;
boolean SHOW_CONDITION = false;
boolean SHOW_HUMIDITY = false;
boolean SHOW_WIND = false;
boolean SHOW_WINDDIR = false;
boolean SHOW_PRESSURE = false;
boolean SHOW_HIGHLOW = false;

ESP8266WebServer server(WEBSERVER_PORT);
ESP8266HTTPUpdateServer serverUpdater;

static const char WEB_ACTIONS1[] PROGMEM = "<a class='w3-bar-item w3-button' href='/'><i class='fas fa-home'></i> Home</a>"
                        "<a class='w3-bar-item w3-button' href='/configure'><i class='fas fa-cog'></i> Configure</a>";

static const char WEB_ACTIONS2[] PROGMEM = "<a class='w3-bar-item w3-button' href='/pull'><i class='fas fa-cloud-download-alt'></i> Refresh Data</a>";

static const char WEB_ACTION3[] PROGMEM = "</a><a class='w3-bar-item w3-button' href='/systemreset' onclick='return confirm(\"Do you want to reset to default weather settings?\")'><i class='fas fa-undo'></i> Reset Settings</a>"
                       "<a class='w3-bar-item w3-button' href='/forgetwifi' onclick='return confirm(\"Do you want to forget to WiFi connection?\")'><i class='fas fa-wifi'></i> Forget WiFi</a>"
                       "<a class='w3-bar-item w3-button' href='/update'><i class='fas fa-wrench'></i> Firmware Update</a>";

static const char CHANGE_FORM1[] PROGMEM = "<form class='w3-container' action='/saveconfig' method='get'><h2>Configure:</h2>"
                      "<label>WagFam Calendar Data Source</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='wagFamDataSource' value='%WAGFAMDATASOURCE%' maxlength='256'>"
                      "<label>WagFam Calendar API Key</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='wagFamApiKey' value='%WAGFAMAPIKEY%' maxlength='128'>"
                      "<hr>";

static const char CHANGE_FORM2[] PROGMEM = "<label>TimeZone DB API Key (get from <a href='https://timezonedb.com/register' target='_BLANK'>here</a>)</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='TimeZoneDB' value='%TIMEDBKEY%' maxlength='60'>"
                      "<label>OpenWeatherMap API Key (get from <a href='https://openweathermap.org/' target='_BLANK'>here</a>)</label>"
                      "<input class='w3-input w3-border w3-margin-bottom' type='text' name='openWeatherMapApiKey' value='%WEATHERKEY%' maxlength='70'>"
                      "<p><label>%CITYNAME1% (<a href='http://openweathermap.org/find' target='_BLANK'><i class='fas fa-search'></i> Search for City ID</a>)</label>"
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
                      "<p><input name='flashseconds' class='w3-check w3-margin-top' type='checkbox' %FLASHSECONDS%> Flash : in the time</p>"
                      "<p>Display Brightness <input class='w3-border w3-margin-bottom' name='ledintensity' type='number' min='0' max='15' value='%INTENSITYOPTIONS%'></p>"
                      "<p>Display Scroll Speed <select class='w3-option w3-padding' name='scrollspeed'>%SCROLLOPTIONS%</select></p>"
                      "<p>Minutes Between Refresh Data <select class='w3-option w3-padding' name='refresh'>%OPTIONS%</select></p>"
                      "<p>Minutes Between Scrolling Data <input class='w3-border w3-margin-bottom' name='refreshDisplay' type='number' min='1' max='10' value='%REFRESH_DISPLAY%'></p>";

const int TIMEOUT = 500; // 500 = 1/2 second
int timeoutCount = 0;

// Change the externalLight to the pin you wish to use if other than the Built-in LED
int externalLight = LED_BUILTIN; // LED_BUILTIN is is the built in LED on the Wemos

void setup() {
  Serial.begin(115200);
  SPIFFS.begin();
  delay(10);

  // Initialize digital pin for LED
  pinMode(externalLight, OUTPUT);

  //New Line to clear from start garbage
  Serial.println();

  readPersistentConfig();

  Serial.println("Number of LED Displays: " + String(numberOfHorizontalDisplays));
  // initialize dispaly
  matrix.setIntensity(0); // Use a value between 0 and 15 for brightness

  int maxPos = numberOfHorizontalDisplays * numberOfVerticalDisplays;
  for (int i = 0; i < maxPos; i++) {
    matrix.setRotation(i, ledRotation);
    matrix.setPosition(i, maxPos - i - 1, 0);
  }

  Serial.println("matrix created");
  matrix.fillScreen(LOW); // show black
  centerPrint("hello");

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

  scrollMessage("Welcome to the Wagner Family Calendar Clock!!!");

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

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
  Serial.print("Signal Strength (RSSI): ");
  Serial.print(getWifiQuality());
  Serial.println("%");

  // OTA Updater is always enabled without password requirement
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();

  // Web Server is always enabled
  server.on("/", displayHomePage);
  server.on("/pull", handlePull);
  server.on("/systemreset", handleSystemReset);
  server.on("/forgetwifi", handleForgetWifi);
  server.on("/configure", handleConfigure);
  server.on("/saveconfig", handleSaveConfig);
  server.onNotFound(redirectHome);
  // Setup the update endpoint and don't require a username/password
  serverUpdater.setup(&server, "/update", "", "");
  // Start the server
  server.begin();
  Serial.println("Server started");
  // Print the IP address
  String webAddress = "http://" + WiFi.localIP().toString() + ":" + String(WEBSERVER_PORT) + "/";
  Serial.println("Use this URL : " + webAddress);
  scrollMessage(" v" + String(VERSION) + "  IP: " + WiFi.localIP().toString() + "  ");

  flashLED(1, 500);
}

//************************************************************
// Main Loop
//************************************************************
void loop() {
  //Get some Weather Data to serve
  if ((getMinutesFromLastRefresh() >= minutesBetweenDataRefresh) || lastEpoch == 0) {
    getWeatherData();
  }

  if (lastMinute != TimeDB.zeroPad(minute())) {
    lastMinute = TimeDB.zeroPad(minute());

    if (weatherClient.getErrorMessage() != "") {
      scrollMessage(weatherClient.getErrorMessage());
      return;
    }

    matrix.shutdown(false);
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
        msg += TimeDB.getDayName() + ", ";
        msg += TimeDB.getMonthName() + " " + day() + "  ";
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
      scrollMessage(msg);
    }
  }

  String currentTime = hourMinutes(false);

  matrix.fillScreen(LOW);
  centerPrint(currentTime, true);

  // Web Server is always enabled
  server.handleClient();
  // OTA Updater is always enabled
  ArduinoOTA.handle();
}

String zeroPad(int value) {
  String rtnValue = String(value);
  if (value < 10) {
    rtnValue = "0" + rtnValue;
  }
  return rtnValue;
}

String hourMinutes(boolean isRefresh) {
  if (IS_24HOUR) {
    return String(hour()) + secondsIndicator(isRefresh) + TimeDB.zeroPad(minute());
  } else {
    return String(hourFormat12()) + secondsIndicator(isRefresh) + TimeDB.zeroPad(minute());
  }
}

String secondsIndicator(boolean isRefresh) {
  String rtnValue = ":";
  if (isRefresh == false && (flashOnSeconds && (second() % 2) == 0)) {
    rtnValue = " ";
  }
  return rtnValue;
}

void handlePull() {
  getWeatherData(); // this will force a data pull for new weather
  displayHomePage();
}

void handleSaveConfig() {
  WAGFAM_DATA_URL = server.arg("wagFamDataSource");
  WAGFAM_API_KEY = server.arg("wagFamApiKey");
  bdayClient.updateBdayClient(WAGFAM_API_KEY,WAGFAM_DATA_URL);
  TIMEDBKEY = server.arg("TimeZoneDB");
  APIKEY = server.arg("openWeatherMapApiKey");
  geoLocation = server.arg("city1");
  flashOnSeconds = server.hasArg("flashseconds");
  IS_24HOUR = server.hasArg("is24hour");
  IS_PM = server.hasArg("isPM");
  SHOW_DATE = server.hasArg("showdate");
  SHOW_CITY = server.hasArg("showcity");
  SHOW_CONDITION = server.hasArg("showcondition");
  SHOW_HUMIDITY = server.hasArg("showhumidity");
  SHOW_WIND = server.hasArg("showwind");
  SHOW_PRESSURE = server.hasArg("showpressure");
  SHOW_HIGHLOW = server.hasArg("showhighlow");
  IS_METRIC = server.hasArg("metric");
  displayIntensity = server.arg("ledintensity").toInt();
  minutesBetweenDataRefresh = server.arg("refresh").toInt();
  minutesBetweenScrolling = server.arg("refreshDisplay").toInt();
  displayScrollSpeed = server.arg("scrollspeed").toInt();
  weatherClient.setMetric(IS_METRIC);
  weatherClient.setGeoLocation(geoLocation);
  matrix.fillScreen(LOW); // show black
  savePersistentConfig();
  getWeatherData(); // this will force a data pull for new weather
  redirectHome();
}

void handleSystemReset() {
  Serial.println("Reset System Configuration");
  if (SPIFFS.remove(CONFIG)) {
    redirectHome();
    ESP.restart();
  }
}

void handleForgetWifi() {
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  redirectHome();
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  ESP.restart();
}

void handleConfigure() {
  digitalWrite(externalLight, LOW);
  String html = "";
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  sendHeader();

  String form = FPSTR(CHANGE_FORM1);
  form.replace("%WAGFAMDATASOURCE%", WAGFAM_DATA_URL);
  form.replace("%WAGFAMAPIKEY%", WAGFAM_API_KEY);
  server.sendContent(form); // Send another chunk of the form


  form = FPSTR(CHANGE_FORM2);
  form.replace("%TIMEDBKEY%", TIMEDBKEY);
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
  server.sendContent(form);

  form = FPSTR(CHANGE_FORM3);
  String isPmChecked = "";
  if (IS_PM) {
    isPmChecked = "checked='checked'";
  }
  form.replace("%IS_PM_CHECKED%", isPmChecked);
  String isFlashSecondsChecked = "";
  if (flashOnSeconds) {
    isFlashSecondsChecked = "checked='checked'";
  }
  form.replace("%FLASHSECONDS%", isFlashSecondsChecked);
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

  server.sendContent(form); // Send another chunk of the form

  sendFooter();

  server.sendContent("");
  server.client().stop();
  digitalWrite(externalLight, HIGH);
}

//***********************************************************************
void getWeatherData() //client function to send/receive GET request data.
{
  digitalWrite(externalLight, LOW);
  matrix.fillScreen(LOW); // show black
  Serial.println();

  // pull the weather data
  if (firstEpoch != 0) {
    centerPrint(hourMinutes(true), true);
  } else {
    centerPrint("...");
  }
  matrix.drawPixel(0, 7, HIGH);
  matrix.drawPixel(0, 6, HIGH);
  matrix.drawPixel(0, 5, HIGH);
  matrix.write();

  weatherClient.updateWeather();
  if (weatherClient.getErrorMessage() != "") {
    scrollMessage(weatherClient.getErrorMessage());
  }

  Serial.println("Updating Time...");
  //Update the Time
  matrix.drawPixel(0, 4, HIGH);
  matrix.drawPixel(0, 3, HIGH);
  matrix.drawPixel(0, 2, HIGH);
  Serial.println("matrix Width:" + String(matrix.width()));
  matrix.write();
  TimeDB.updateConfig(TIMEDBKEY, String(weatherClient.getLat()), String(weatherClient.getLon()));
  time_t currentTime = TimeDB.getTime();
  if(currentTime > 5000 || firstEpoch == 0) {
    setTime(currentTime);
  } else {
    Serial.println("Time update unsuccessful!");
  }
  lastEpoch = now();
  if (firstEpoch == 0) {
    firstEpoch = now();
    Serial.println("firstEpoch is: " + String(firstEpoch));
  }

  serverConfig = bdayClient.updateData();
  bool needToSave = false;
  if (serverConfig.dataSourceUrlValid) {
    WAGFAM_DATA_URL = serverConfig.dataSourceUrl;
    lastEpoch = 0; // this should force a data pull, since with a new URL that's required
    needToSave = true;
  }
  if (serverConfig.apiKeyValid) {
    WAGFAM_API_KEY = serverConfig.apiKey;
    lastEpoch = 0; // this should force a data pull, since with a new API_KEY that's required
    needToSave = true;
  }
  if (serverConfig.eventTodayValid) {
    WAGFAM_EVENT_TODAY = serverConfig.eventToday;
    needToSave = true;
  }
  if (needToSave) {
    Serial.println("Saving new config received from server");
    savePersistentConfig();
  }

  Serial.println("Version: " + String(VERSION));
  Serial.println();
  digitalWrite(externalLight, HIGH);
}

void displayMessage(String message) {
  digitalWrite(externalLight, LOW);

  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHeader();
  server.sendContent(message);
  sendFooter();
  server.sendContent("");
  server.client().stop();

  digitalWrite(externalLight, HIGH);
}

void redirectHome() {
  // Send them back to the Root Directory
  server.sendHeader("Location", String("/"), true);
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");
  server.client().stop();
  delay(1000);
}

void sendHeader() {
  String html = "<!DOCTYPE HTML>";
  html += "<html><head><title>Marquee Scroller</title><link rel='icon' href='data:;base64,='>";
  html += "<meta http-equiv='Content-Type' content='text/html; charset=UTF-8' />";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>";
  html += "<link rel='stylesheet' href='https://www.w3schools.com/lib/w3-theme-blue-grey.css'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.8.1/css/all.min.css'>";
  html += "</head><body>";
  server.sendContent(html);
  html = "<nav class='w3-sidebar w3-bar-block w3-card' style='margin-top:88px' id='mySidebar'>";
  html += "<div class='w3-container w3-theme-d2'>";
  html += "<span onclick='closeSidebar()' class='w3-button w3-display-topright w3-large'><i class='fas fa-times'></i></span>";
  html += "<div class='w3-left'><img src='http://openweathermap.org/img/w/" + weatherClient.getIcon() + ".png' alt='" + weatherClient.getWeatherDescription() + "'></div>";
  html += "<div class='w3-padding'>Menu</div></div>";
  server.sendContent(html);

  server.sendContent(FPSTR(WEB_ACTIONS1));
  server.sendContent(FPSTR(WEB_ACTIONS2));
  server.sendContent(FPSTR(WEB_ACTION3));

  html = "</nav>";
  html += "<header class='w3-top w3-bar w3-theme'><button class='w3-bar-item w3-button w3-xxxlarge w3-hover-theme' onclick='openSidebar()'><i class='fas fa-bars'></i></button><h2 class='w3-bar-item'>WagFam CalClock</h2></header>";
  html += "<script>";
  html += "function openSidebar(){document.getElementById('mySidebar').style.display='block'}function closeSidebar(){document.getElementById('mySidebar').style.display='none'}closeSidebar();";
  html += "</script>";
  html += "<br><div class='w3-container w3-large' style='margin-top:88px'>";
  server.sendContent(html);
}

void sendFooter() {
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
  server.sendContent(html);
}

void displayHomePage() {
  digitalWrite(externalLight, LOW);
  String html = "";

  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHeader();

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
  server.sendContent(html); // Send over the first section
  html = "";


  // Next send over the Weather Data Section
  String temperature = String(weatherClient.getTemperature(),0);

  if ((temperature.indexOf(".") != -1) && (temperature.length() >= (temperature.indexOf(".") + 2))) {
    temperature.remove(temperature.indexOf(".") + 2);
  }

  String time = TimeDB.getDayName() + ", " + TimeDB.getMonthName() + " " + day() + ", " + hourFormat12() + ":" + TimeDB.zeroPad(minute()) + " " + TimeDB.getAmPm();

  if (TIMEDBKEY == "") {
    html += "<p>Please <a href='/configure'>Configure TimeZoneDB</a> with API key.</p>";
  }

  if (weatherClient.getCity() == "") {
    html += "<p>Please <a href='/configure'>Configure Weather</a> API</p>";
    if (weatherClient.getErrorMessage() != "") {
      html += "<p>Weather Error: <strong>" + weatherClient.getErrorMessage() + "</strong></p>";
    }
  } else {
    html += "<div class='w3-cell-row' style='width:100%'><h2>Weather for " + weatherClient.getCity() + ", " + weatherClient.getCountry() + "</h2></div><div class='w3-cell-row'>";
    html += "<div class='w3-cell w3-left w3-medium' style='width:120px'>";
    html += "<img src='http://openweathermap.org/img/w/" + weatherClient.getIcon() + ".png' alt='" + weatherClient.getWeatherDescription() + "'><br>";
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


  server.sendContent(html); // spit out what we got
  html = ""; // fresh start
  sendFooter();
  server.sendContent("");
  server.client().stop();
  digitalWrite(externalLight, HIGH);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println("Wifi Manager");
  Serial.println("Please connect to AP");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.println("To setup Wifi Configuration");
  scrollMessage("Please Connect to AP: " + String(myWiFiManager->getConfigPortalSSID()));
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

String getTempSymbol() {
  return getTempSymbol(false);
}

String getTempSymbol(bool forWeb) {
  String rtnValue = "F";
  if (IS_METRIC) {
    rtnValue = "C";
  }
  if (forWeb) {
    rtnValue = "°" + rtnValue;
  } else {
    rtnValue = char(247) + rtnValue;
  }
  return rtnValue;
}


String getSpeedSymbol() {
  String rtnValue = "mph";
  if (IS_METRIC) {
    rtnValue = "kph";
  }
  return rtnValue;
}

String getPressureSymbol()
{
  String rtnValue = "";
  if (IS_METRIC)
  {
    rtnValue = "mb";
  }
  return rtnValue;
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
  String rtnValue = "";

  long timeToUpdate = (((minutesBetweenDataRefresh * 60) + lastEpoch) - now());

  int hours = numberOfHours(timeToUpdate);
  int minutes = numberOfMinutes(timeToUpdate);
  int seconds = numberOfSeconds(timeToUpdate);

  rtnValue += String(hours) + ":";
  if (minutes < 10) {
    rtnValue += "0";
  }
  rtnValue += String(minutes) + ":";
  if (seconds < 10) {
    rtnValue += "0";
  }
  rtnValue += String(seconds);

  return rtnValue;
}

int getMinutesFromLastRefresh() {
  int minutes = (now() - lastEpoch) / 60;
  return minutes;
}

void savePersistentConfig() {
  // Save decoded message to SPIFFS file for playback on power up.
  File f = SPIFFS.open(CONFIG, "w");
  if (!f) {
    Serial.println("File open failed!");
  } else {
    Serial.println("Saving settings now...");
    f.println("WAGFAM_DATA_URL=" + WAGFAM_DATA_URL);
    f.println("WAGFAM_API_KEY=" + WAGFAM_API_KEY);
    f.println("WAGFAM_EVENT_TODAY=" + String(WAGFAM_EVENT_TODAY));
    f.println("TIMEDBKEY=" + TIMEDBKEY);
    f.println("APIKEY=" + APIKEY);
    f.println("CityID=" + geoLocation);
    f.println("ledIntensity=" + String(displayIntensity));
    f.println("scrollSpeed=" + String(displayScrollSpeed));
    f.println("isFlash=" + String(flashOnSeconds));
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
  }
  f.close();
  readPersistentConfig();
}

void readPersistentConfig() {
  if (SPIFFS.exists(CONFIG) == false) {
    Serial.println("Settings File does not yet exists.");
    savePersistentConfig();
    return;
  }
  File fr = SPIFFS.open(CONFIG, "r");
  String line;
  while (fr.available()) {
    line = fr.readStringUntil('\n');
    if (line.indexOf("WAGFAM_DATA_URL=") >= 0) {
      WAGFAM_DATA_URL = line.substring(line.lastIndexOf("WAGFAM_DATA_URL=") + 16);
      WAGFAM_DATA_URL.trim();
      Serial.println("WAGFAM_DATA_URL: " + WAGFAM_DATA_URL);
    }
    if (line.indexOf("WAGFAM_API_KEY=") >= 0) {
      WAGFAM_API_KEY = line.substring(line.lastIndexOf("WAGFAM_API_KEY=") + 15);
      WAGFAM_API_KEY.trim();
      Serial.println("WAGFAM_API_KEY: " + WAGFAM_API_KEY);
    }
    if (line.indexOf("WAGFAM_EVENT_TODAY=") >= 0) {
      WAGFAM_EVENT_TODAY = line.substring(line.lastIndexOf("WAGFAM_EVENT_TODAY=") + 19).toInt();
      Serial.println("WAGFAM_EVENT_TODAY: " + String(WAGFAM_EVENT_TODAY));
    }
    if (line.indexOf("TIMEDBKEY=") >= 0) {
      TIMEDBKEY = line.substring(line.lastIndexOf("TIMEDBKEY=") + 10);
      TIMEDBKEY.trim();
      Serial.println("TIMEDBKEY: " + TIMEDBKEY);
    }
    if (line.indexOf("APIKEY=") >= 0) {
      APIKEY = line.substring(line.lastIndexOf("APIKEY=") + 7);
      APIKEY.trim();
      Serial.println("APIKEY: " + APIKEY);
    }
    if (line.indexOf("CityID=") >= 0) {
      geoLocation = line.substring(line.lastIndexOf("CityID=") + 7);
      geoLocation.trim();
      Serial.println("CityID: " + geoLocation);
    }
    if (line.indexOf("isFlash=") >= 0) {
      flashOnSeconds = line.substring(line.lastIndexOf("isFlash=") + 8).toInt();
      Serial.println("flashOnSeconds=" + String(flashOnSeconds));
    }
    if (line.indexOf("is24hour=") >= 0) {
      IS_24HOUR = line.substring(line.lastIndexOf("is24hour=") + 9).toInt();
      Serial.println("IS_24HOUR=" + String(IS_24HOUR));
    }
    if (line.indexOf("isPM=") >= 0) {
      IS_PM = line.substring(line.lastIndexOf("isPM=") + 5).toInt();
      Serial.println("IS_PM=" + String(IS_PM));
    }
    if (line.indexOf("isMetric=") >= 0) {
      IS_METRIC = line.substring(line.lastIndexOf("isMetric=") + 9).toInt();
      Serial.println("IS_METRIC=" + String(IS_METRIC));
    }
    if (line.indexOf("refreshRate=") >= 0) {
      minutesBetweenDataRefresh = line.substring(line.lastIndexOf("refreshRate=") + 12).toInt();
      if (minutesBetweenDataRefresh == 0) {
        minutesBetweenDataRefresh = 15; // can't be zero
      }
      Serial.println("minutesBetweenDataRefresh=" + String(minutesBetweenDataRefresh));
    }
    if (line.indexOf("minutesBetweenScrolling=") >= 0) {
      displayRefreshCount = 1;
      minutesBetweenScrolling = line.substring(line.lastIndexOf("minutesBetweenScrolling=") + 24).toInt();
      Serial.println("minutesBetweenScrolling=" + String(minutesBetweenScrolling));
    }
    if (line.indexOf("ledIntensity=") >= 0) {
      displayIntensity = line.substring(line.lastIndexOf("ledIntensity=") + 13).toInt();
      Serial.println("displayIntensity=" + String(displayIntensity));
    }
    if (line.indexOf("scrollSpeed=") >= 0) {
      displayScrollSpeed = line.substring(line.lastIndexOf("scrollSpeed=") + 12).toInt();
      Serial.println("displayScrollSpeed=" + String(displayScrollSpeed));
    }
    if (line.indexOf("SHOW_CITY=") >= 0) {
      SHOW_CITY = line.substring(line.lastIndexOf("SHOW_CITY=") + 10).toInt();
      Serial.println("SHOW_CITY=" + String(SHOW_CITY));
    }
    if (line.indexOf("SHOW_CONDITION=") >= 0) {
      SHOW_CONDITION = line.substring(line.lastIndexOf("SHOW_CONDITION=") + 15).toInt();
      Serial.println("SHOW_CONDITION=" + String(SHOW_CONDITION));
    }
    if (line.indexOf("SHOW_HUMIDITY=") >= 0) {
      SHOW_HUMIDITY = line.substring(line.lastIndexOf("SHOW_HUMIDITY=") + 14).toInt();
      Serial.println("SHOW_HUMIDITY=" + String(SHOW_HUMIDITY));
    }
    if (line.indexOf("SHOW_WIND=") >= 0) {
      SHOW_WIND = line.substring(line.lastIndexOf("SHOW_WIND=") + 10).toInt();
      Serial.println("SHOW_WIND=" + String(SHOW_WIND));
    }
    if (line.indexOf("SHOW_PRESSURE=") >= 0) {
      SHOW_PRESSURE = line.substring(line.lastIndexOf("SHOW_PRESSURE=") + 14).toInt();
      Serial.println("SHOW_PRESSURE=" + String(SHOW_PRESSURE));
    }
    if (line.indexOf("SHOW_HIGHLOW=") >= 0) {
      SHOW_HIGHLOW = line.substring(line.lastIndexOf("SHOW_HIGHLOW=") + 13).toInt();
      Serial.println("SHOW_HIGHLOW=" + String(SHOW_HIGHLOW));
    }
    if (line.indexOf("SHOW_DATE=") >= 0) {
      SHOW_DATE = line.substring(line.lastIndexOf("SHOW_DATE=") + 10).toInt();
      Serial.println("SHOW_DATE=" + String(SHOW_DATE));
    }
  }
  fr.close();
  matrix.setIntensity(displayIntensity);
  weatherClient.setWeatherApiKey(APIKEY);
  weatherClient.setMetric(IS_METRIC);
  weatherClient.setGeoLocation(geoLocation);
  bdayClient.updateBdayClient(WAGFAM_API_KEY,WAGFAM_DATA_URL);
}

void scrollMessage(String msg) {
  msg += " "; // add a space at the end
  for ( int i = 0 ; i < width * msg.length() + matrix.width() - 1 - spacer; i++ ) {
    // Web server is always enabled
    server.handleClient();
    // OTA Updater is always enabled
    ArduinoOTA.handle();
    matrix.fillScreen(LOW);

    int letter = i / width;
    int x = (matrix.width() - 1) - i % width;
    int y = (matrix.height() - 8) / 2; // center the text vertically

    while ( x + width - spacer >= 0 && letter >= 0 ) {
      if ( letter < msg.length() ) {
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

void centerPrint(String msg) {
  centerPrint(msg, false);
}

void centerPrint(String msg, boolean extraStuff) {
  int x = (matrix.width() - (msg.length() * width)) / 2;

  // Print the static portions of the display before the main Message
  if (extraStuff) {
    // We will have a shifting left-right on/off pattern on the two side and the bottom row
    // only displayed when there is an event happening on a given day.
    if (WAGFAM_EVENT_TODAY) {
      todayDisplayMilliSecond = millis() % (TODAY_DISPLAY_DOT_SPACING * TODAY_DISPLAY_DOT_SPEED_MS);
      todayDisplayStartingLED = int(todayDisplayMilliSecond / TODAY_DISPLAY_DOT_SPEED_MS);
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

String EncodeUrlSpecialChars(const char *msg)
{
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