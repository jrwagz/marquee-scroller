#pragma once
#include <cstdint>

// Minimal TimeLib stub for native compilation
inline int numberOfHours(uint32_t epoch)   { return (int)((epoch % 86400UL) / 3600); }
inline int numberOfMinutes(uint32_t epoch) { return (int)((epoch  % 3600UL) / 60); }
inline int numberOfSeconds(uint32_t epoch) { return (int)(epoch  % 60UL); }
