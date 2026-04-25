#pragma once
#include "Arduino.h"
#include "ESP8266WiFi.h"

#define HTTP_CODE_OK               200
#define HTTP_CODE_MOVED_PERMANENTLY 301

class HTTPClient {
public:
    bool begin(WiFiClient&, const String&) { return false; }
    void addHeader(const String&, const String&) {}
    int GET() { return -1; }
    int getSize() { return 0; }
    WiFiClient* getStreamPtr() { return nullptr; }
    bool connected() { return false; }
    void end() {}
    String errorToString(int) { return String(""); }
};
