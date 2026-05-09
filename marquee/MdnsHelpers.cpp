#include "MdnsHelpers.h"

String mdnsLabelFor(const String &source, const String &chipId) {
  String out;
  out.reserve(source.length());
  bool prevHyphen = true;  // leading hyphen guard
  for (size_t i = 0; i < source.length(); i++) {
    char c = source[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (ok) {
      out += c;
      prevHyphen = false;
    } else if (!prevHyphen) {
      out += '-';
      prevHyphen = true;
    }
  }
  while (out.length() > 0 && out[out.length() - 1] == '-') {
    out = out.substring(0, out.length() - 1);
  }
  if (out.length() == 0) {
    out = "wagfam-" + chipId;
  }
  if (out.length() > 63) {
    out = out.substring(0, 63);
    while (out.length() > 0 && out[out.length() - 1] == '-') {
      out = out.substring(0, out.length() - 1);
    }
  }
  return out;
}
