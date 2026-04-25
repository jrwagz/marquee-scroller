#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

using boolean = bool;
using byte = uint8_t;

// Bring common std utilities into global scope (Arduino does this implicitly)
using std::min;
using std::max;

#define PROGMEM
#define PSTR(s) (s)

class String {
public:
    std::string _s;

    String() = default;
    String(const String&) = default;
    String& operator=(const String&) = default;
    String(String&&) = default;
    String& operator=(String&&) = default;

    String(const char* s) : _s(s ? s : "") {}             // NOLINT
    String(std::string s) : _s(std::move(s)) {}           // NOLINT
    String(int n) : _s(std::to_string(n)) {}              // NOLINT
    String(unsigned int n) : _s(std::to_string(n)) {}     // NOLINT
    String(long n) : _s(std::to_string(n)) {}             // NOLINT
    String(unsigned long n) : _s(std::to_string(n)) {}    // NOLINT
    String(bool b) : _s(b ? "1" : "0") {}                 // NOLINT
    String(char c) : _s(1, c) {}                          // NOLINT
    String(float n, int dec = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", dec, (double)n);
        _s = buf;
    }
    String(double n, int dec = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", dec, n);
        _s = buf;
    }

    String& operator=(const char* s) { _s = s ? s : ""; return *this; }
    String& operator=(char c) { _s = std::string(1, c); return *this; }

    bool operator==(const String& o) const { return _s == o._s; }
    bool operator==(const char* s) const { return _s == (s ? s : ""); }
    bool operator!=(const String& o) const { return _s != o._s; }
    bool operator!=(const char* s) const { return _s != (s ? s : ""); }
    bool operator<(const String& o) const { return _s < o._s; }

    String operator+(const String& o) const { return String(_s + o._s); }
    String operator+(const char* s) const { return String(_s + (s ? s : "")); }
    String operator+(char c) const { return String(_s + c); }
    String operator+(int n) const { return String(_s + std::to_string(n)); }
    String& operator+=(const String& o) { _s += o._s; return *this; }
    String& operator+=(const char* s) { if (s) _s += s; return *this; }
    String& operator+=(char c) { _s += c; return *this; }

    friend String operator+(const char* lhs, const String& rhs) {
        return String(std::string(lhs ? lhs : "") + rhs._s);
    }

    char operator[](int i) const {
        if (i < 0 || i >= (int)_s.size()) return 0;
        return _s[i];
    }
    char& operator[](int i) { return _s[i]; }

    // Implicit conversions for library compatibility
    operator std::string() const { return _s; }
    operator const char*() const { return _s.c_str(); }

    unsigned int length() const { return (unsigned int)_s.size(); }
    bool isEmpty() const { return _s.empty(); }
    const char* c_str() const { return _s.c_str(); }
    void reserve(unsigned int size) { _s.reserve(size); }

    int indexOf(char c, int from = 0) const {
        if (from < 0) from = 0;
        size_t pos = _s.find(c, (size_t)from);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int indexOf(const char* s, int from = 0) const {
        if (from < 0) from = 0;
        size_t pos = _s.find(s, (size_t)from);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int indexOf(const String& s, int from = 0) const {
        return indexOf(s._s.c_str(), from);
    }

    int lastIndexOf(char c) const {
        size_t pos = _s.rfind(c);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int lastIndexOf(const char* s) const {
        size_t pos = _s.rfind(s);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int lastIndexOf(const String& s) const {
        return lastIndexOf(s._s.c_str());
    }

    String substring(int from, int to = -1) const {
        if (from < 0) from = 0;
        if (from > (int)_s.size()) return String("");
        if (to == -1 || to > (int)_s.size()) return String(_s.substr(from));
        if (to <= from) return String("");
        return String(_s.substr(from, to - from));
    }

    void trim() {
        size_t start = _s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) { _s = ""; return; }
        size_t end = _s.find_last_not_of(" \t\n\r");
        _s = _s.substr(start, end - start + 1);
    }

    void toLowerCase() {
        for (auto& c : _s) c = (char)tolower((unsigned char)c);
    }
    void toUpperCase() {
        for (auto& c : _s) c = (char)toupper((unsigned char)c);
    }

    int toInt() const {
        if (_s.empty()) return 0;
        return (int)strtol(_s.c_str(), nullptr, 10);
    }
    float toFloat() const {
        if (_s.empty()) return 0.0f;
        return (float)strtof(_s.c_str(), nullptr);
    }

    bool startsWith(const char* prefix) const {
        if (!prefix) return false;
        return _s.rfind(prefix, 0) == 0;
    }
    bool startsWith(const String& s) const { return startsWith(s._s.c_str()); }

    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        std::string s(suffix);
        if (s.length() > _s.length()) return false;
        return _s.compare(_s.length() - s.length(), s.length(), s) == 0;
    }
    bool endsWith(const String& s) const { return endsWith(s._s.c_str()); }

    // replace all occurrences — operates on raw bytes, correct for multi-byte UTF-8 sequences
    void replace(const char* from, const char* to) {
        if (!from || !*from) return;
        std::string f(from), t(to ? to : "");
        size_t pos = 0;
        while ((pos = _s.find(f, pos)) != std::string::npos) {
            _s.replace(pos, f.length(), t);
            pos += t.length();
        }
    }
    void replace(const String& from, const String& to) {
        replace(from._s.c_str(), to._s.c_str());
    }

    bool equals(const String& o) const { return _s == o._s; }
    bool equals(const char* s) const { return _s == (s ? s : ""); }
    bool equalsIgnoreCase(const String& o) const {
        if (_s.size() != o._s.size()) return false;
        for (size_t i = 0; i < _s.size(); i++)
            if (tolower((unsigned char)_s[i]) != tolower((unsigned char)o._s[i])) return false;
        return true;
    }

    int compareTo(const String& o) const { return _s.compare(o._s); }
    bool concat(const String& s) { _s += s._s; return true; }
    bool concat(const char* s) { if (s) _s += s; return true; }
};

// FPSTR/F must come after String definition
#define F(s) (s)
#define FPSTR(s) String(s)

// Serial stub — writes to stdout
struct _SerialClass {
    template<typename T> void print(T v) { std::cout << v; }
    void print(const String& s) { std::cout << s._s; }
    template<typename T> void println(T v) { std::cout << v << "\n"; }
    void println(const String& s) { std::cout << s._s << "\n"; }
    void println() { std::cout << "\n"; }
    void begin(int) {}
    bool available() { return false; }
};
static _SerialClass Serial;

inline void delay(unsigned long) {}
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
