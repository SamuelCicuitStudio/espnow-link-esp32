#pragma once

#include <cctype>
#include <string>

namespace app_owned::buildid {

inline const char* month2_(const char* mmm) {
  if (mmm[0] == 'J' && mmm[1] == 'a' && mmm[2] == 'n') return "01";
  if (mmm[0] == 'F' && mmm[1] == 'e' && mmm[2] == 'b') return "02";
  if (mmm[0] == 'M' && mmm[1] == 'a' && mmm[2] == 'r') return "03";
  if (mmm[0] == 'A' && mmm[1] == 'p' && mmm[2] == 'r') return "04";
  if (mmm[0] == 'M' && mmm[1] == 'a' && mmm[2] == 'y') return "05";
  if (mmm[0] == 'J' && mmm[1] == 'u' && mmm[2] == 'n') return "06";
  if (mmm[0] == 'J' && mmm[1] == 'u' && mmm[2] == 'l') return "07";
  if (mmm[0] == 'A' && mmm[1] == 'u' && mmm[2] == 'g') return "08";
  if (mmm[0] == 'S' && mmm[1] == 'e' && mmm[2] == 'p') return "09";
  if (mmm[0] == 'O' && mmm[1] == 'c' && mmm[2] == 't') return "10";
  if (mmm[0] == 'N' && mmm[1] == 'o' && mmm[2] == 'v') return "11";
  if (mmm[0] == 'D' && mmm[1] == 'e' && mmm[2] == 'c') return "12";
  return "00";
}

inline std::string compactStamp() {
  const char* d = __DATE__;  // "Mmm dd yyyy"
  const char* t = __TIME__;  // "hh:mm:ss"
  std::string out;
  out.reserve(12);
  out.push_back(d[9]);
  out.push_back(d[10]);
  out.append(month2_(d));
  out.push_back((d[4] == ' ') ? '0' : d[4]);
  out.push_back(d[5]);
  out.push_back(t[0]);
  out.push_back(t[1]);
  out.push_back(t[3]);
  out.push_back(t[4]);
  out.push_back(t[6]);
  out.push_back(t[7]);
  return out;
}

inline std::string compactSw(const std::string& sw) {
  std::string out;
  out.reserve(3);
  for (char c : sw) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      out.push_back(c);
      if (out.size() == 3) {
        break;
      }
    }
  }
  while (out.size() < 3) {
    out.push_back('0');
  }
  return out;
}

inline std::string makeShortBuildId(const char* device_tag, const std::string& sw_version) {
  const std::string tag = (device_tag != nullptr && device_tag[0] != '\0') ? device_tag : "dev0";
  return tag + "-" + compactSw(sw_version) + "-" + compactStamp();
}

inline std::string defaultBuildId(const char* explicit_id, const char* device_tag, const std::string& sw_version) {
  if (explicit_id != nullptr && explicit_id[0] != '\0') {
    return std::string(explicit_id);
  }
  return makeShortBuildId(device_tag, sw_version);
}

}  // namespace app_owned::buildid

