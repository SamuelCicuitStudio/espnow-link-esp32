#include "cli_helpers.hpp"

#include <cctype>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "espnow_link/address.hpp"

namespace espnow_link {
namespace cli_helpers {

std::string trim(const std::string& in) {
  size_t a = 0;
  while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a])) != 0) {
    ++a;
  }
  size_t b = in.size();
  while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1])) != 0) {
    --b;
  }
  return in.substr(a, b - a);
}

std::string lowerCopy(const std::string& in) {
  std::string out = in;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string macToPrintable(const MacAddress& mac) {
  return macToString(mac);
}

uint32_t nowMs() {
#if defined(ARDUINO)
  return millis();
#else
  return static_cast<uint32_t>((std::clock() * 1000ULL) / CLOCKS_PER_SEC);
#endif
}

}  // namespace cli_helpers
}  // namespace espnow_link
