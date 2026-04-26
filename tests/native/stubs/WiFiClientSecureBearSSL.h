#pragma once
#include <memory>
#include "ESP8266WiFi.h"

namespace BearSSL {
    class WiFiClientSecure : public WiFiClient {
    public:
        void setInsecure() {}
        void setBufferSizes(int, int) {}
        void setFingerprint(const char*) {}
        void setCACert(const char*) {}
    };
}
