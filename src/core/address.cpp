#include "espnow_link/address.hpp"

#include <cstdio>

namespace espnow_link {

bool parseHexNibble(char c, uint8_t& out) {
  if (c >= '0' && c <= '9') {
    out = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    out = static_cast<uint8_t>(c - 'A' + 10);
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    out = static_cast<uint8_t>(c - 'a' + 10);
    return true;
  }
  return false;
}

bool parseMac(const char* text, MacAddress& out) {
  if (text == nullptr) {
    return false;
  }

  const char* s = text;
  for (size_t i = 0; i < out.size(); ++i) {
    const size_t p = i * 3;
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (!parseHexNibble(s[p], hi) || !parseHexNibble(s[p + 1], lo)) {
      return false;
    }
    out[i] = static_cast<uint8_t>((hi << 4) | lo);

    if (i < out.size() - 1 && s[p + 2] != ':') {
      return false;
    }
  }

  return s[17] == '\0';
}

bool parseMac(const std::string& text, MacAddress& out) { return parseMac(text.c_str(), out); }

std::string macToString(const MacAddress& mac) {
  char buf[18] = {0};
  std::snprintf(buf,
                sizeof(buf),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5]);
  return std::string(buf);
}

}  // namespace espnow_link
