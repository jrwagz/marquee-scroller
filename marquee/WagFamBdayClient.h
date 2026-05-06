/** The MIT License (MIT)

Copyright (c) 2023 Justin Wagner

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

#pragma once
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <JsonListener.h>
#include <JsonStreamingParser.h> // --> https://github.com/squix78/json-streaming-parser

struct DeviceInfo {
    String chipId;
    String version;
    uint32_t uptimeMs;
    uint32_t freeHeap;
    int32_t rssi;
    int32_t utcOffsetSec; // UTC offset in seconds from OWM (e.g. -21600 for UTC-6)
};

class WagFamBdayClient: public JsonListener {

  public:
    typedef struct {
      boolean dataSourceUrlValid;
      String dataSourceUrl;
      boolean apiKeyValid;
      String apiKey;
      boolean eventTodayValid;
      boolean eventToday;
      boolean latestVersionValid;
      String latestVersion;
      boolean firmwareUrlValid;
      String firmwareUrl;
      boolean deviceNameValid;
      String deviceName;
      boolean latestSpaVersionValid;
      String latestSpaVersion;
      boolean spaFsUrlValid;
      String spaFsUrl;
      // Issue #99: troll message override. When non-empty, marquee.ino
      // displays this string exclusively and skips the calendar messages.
      boolean trollMessageValid;
      String trollMessage;
      // Issue #99: signed remote config update. All three fields arrive
      // together; the handler in marquee.ino verifies the HMAC-SHA256 of
      // configUpdatePayload (raw bytes) against configUpdateSignature
      // (base64) using WAGFAM_CONFIG_HMAC_KEY, and only applies if
      // configUpdateVersion is strictly greater than the last applied.
      int configUpdateVersion;
      String configUpdatePayload;
      String configUpdateSignature;
    } configValues;

    WagFamBdayClient(String ApiKey, String JsonDataSourceUrl);
    void updateBdayClient(String ApiKey, String JsonDataSourceUrl);
    WagFamBdayClient::configValues updateData(const DeviceInfo& device);
    void updateDataSource(String JsonDataSourceUrl);

    String getMessage(int index);
    int getNumMessages();
    String cleanText(String text);
    WagFamBdayClient::configValues getLastConfig() const { return currentConfig; }

    virtual void whitespace(char c);
    virtual void startDocument();
    virtual void key(String key);
    virtual void value(String value);
    virtual void endArray();
    virtual void endObject();
    virtual void endDocument();
    virtual void startArray();
    virtual void startObject();

  private:
    String myJsonSourceUrl = "";
    String myApiKey = "";

    String currentKey = "";
    int messageCounter = 0;

    // Support up to 10 messages queued up for display
    String messages[10];

    bool inConfig = false;
    configValues currentConfig = {};

};
