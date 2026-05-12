#include "OriginAllowlist.h"

namespace {

// Trim ASCII whitespace from both ends. Inline-static so this file can be
// `#include`d directly by the native test target without symbol clashes.
String trimAscii(const String &s) {
  int start = 0;
  int end = s.length();
  while (start < end && (s[start] == ' ' || s[start] == '\t')) start++;
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
  if (start == 0 && end == (int)s.length()) return s;
  return s.substring(start, end);
}

}  // namespace

bool isOriginAllowed(const String &origin, const char *allowlist) {
  if (origin.length() == 0) return false;
  if (allowlist == nullptr || allowlist[0] == '\0') return false;

  String list(allowlist);
  int searchStart = 0;
  while (searchStart < (int)list.length()) {
    int comma = list.indexOf(',', searchStart);
    int end = (comma == -1) ? list.length() : comma;
    String entry = trimAscii(list.substring(searchStart, end));
    if (entry.length() > 0 && entry == origin) {
      return true;
    }
    if (comma == -1) break;
    searchStart = comma + 1;
  }
  return false;
}
