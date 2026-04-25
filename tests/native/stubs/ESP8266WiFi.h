#pragma once
#include "Arduino.h"

class WiFiClient {
public:
    bool connect(const char*, int) { return false; }
    bool connect(const String&, int) { return false; }
    bool connected() { return false; }
    void stop() {}
    bool available() { return false; }
    int read() { return -1; }
    int readBytes(char*, int) { return 0; }
    int readBytesUntil(char, char* buf, int len) {
        if (len > 0) buf[0] = 0;
        return 0;
    }
    bool find(const char*) { return false; }
    void print(const String&) {}
    void print(const char*) {}
    void println(const String&) {}
    void println(const char*) {}
    void println() {}
    void flush() {}
};

class WiFiClientSecure : public WiFiClient {};
