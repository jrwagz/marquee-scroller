#pragma once
// Minimal stub — allows OpenWeatherMapClient.cpp to compile without the real ArduinoJson
// Only needs to satisfy the compiler; updateWeather() is never called in native tests

struct JsonVariant {
    template<typename T>
    T as() const { return T{}; }

    JsonVariant operator[](const char*) const { return {}; }
    JsonVariant operator[](int) const { return {}; }

    operator float()    const { return 0.0f; }
    operator double()   const { return 0.0; }
    operator int()      const { return 0; }
    operator unsigned() const { return 0; }
    operator bool()     const { return false; }
    operator long()     const { return 0L; }

    // uint32_t — same width as unsigned int on most platforms; avoid ambiguity
    // by providing a named cast helper instead of implicit operator
};

struct JsonDocument {
    JsonVariant operator[](const char*) const { return {}; }
    JsonVariant operator[](int) const { return {}; }
};

struct DeserializationError {
    bool _err = false;
    explicit operator bool() const { return _err; }
};

template<typename TInput>
inline DeserializationError deserializeJson(JsonDocument&, TInput&) { return {}; }
template<typename TInput>
inline DeserializationError deserializeJson(JsonDocument&, const TInput&) { return {}; }

inline int measureJson(const JsonDocument&) { return 0; }
