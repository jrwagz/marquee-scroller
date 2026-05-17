#include "FamilyHelpers.h"

bool isKnownFamily(const String &wire) {
  return wire == "butterfield" || wire == "wagner";
}

String familyDisplay(const String &wire) {
  if (wire == "butterfield") return F("Butterfield");
  if (wire == "wagner") return F("Wagner");
  return String();
}
