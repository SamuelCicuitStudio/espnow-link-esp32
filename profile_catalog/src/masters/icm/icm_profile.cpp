#include "profile_catalog/masters/icm/icm_profile.hpp"

#include <cstdlib>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {

namespace {

struct SettingDef {
  uint16_t id;
  const char* key;
  espnow_link::SettingValueType type;
  const char* nvs;
  const char* def;
  const char* desc;
  bool has_int_range;
  uint32_t int_min;
  uint32_t int_max;
  const char* enum_values;
};

bool parseBool(const std::string& value, bool& out) {
  if (value == "1" || value == "true" || value == "on" || value == "yes") {
    out = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "off" || value == "no") {
    out = false;
    return true;
  }
  return false;
}

bool parseU32(const std::string& value, uint32_t& out) {
  if (value.empty()) {
    return false;
  }
  char* endp = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &endp, 10);
  if (endp == nullptr || *endp != '\0') {
    return false;
  }
  out = static_cast<uint32_t>(parsed);
  return true;
}

bool enumContains(const char* enum_values, const std::string& value) {
  if (enum_values == nullptr || enum_values[0] == '\0') {
    return true;
  }
  const std::string enums(enum_values);
  size_t start = 0;
  while (start <= enums.size()) {
    const size_t sep = enums.find('|', start);
    const size_t len = (sep == std::string::npos) ? (enums.size() - start) : (sep - start);
    if (value == enums.substr(start, len)) {
      return true;
    }
    if (sep == std::string::npos) {
      break;
    }
    start = sep + 1;
  }
  return false;
}

constexpr SettingDef kSettingDefs[] = {
    {0x0001, PCAT_ICM_SET_DNAME, espnow_link::SettingValueType::String, PCAT_ICM_KEY_DNAME, PCAT_ICM_SET_DNAME_DEF, PCAT_ICM_DESC_SET_DNAME, false, 0U, 0U, nullptr},
    {0x0301, PCAT_ICM_SET_MXPRS, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_MXPRS, "14", PCAT_ICM_DESC_SET_MXPRS, true, PCAT_ICM_SET_MXPRS_MIN, PCAT_ICM_SET_MXPRS_MAX, nullptr},
    {0x0302, PCAT_ICM_SET_LIVEN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_LIVEN, "1", PCAT_ICM_DESC_SET_LIVEN, false, 0U, 0U, nullptr},
    {0x0303, PCAT_ICM_SET_DISCA, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_DISCA, "0", PCAT_ICM_DESC_SET_DISCA, false, 0U, 0U, nullptr},
    {0x0304, PCAT_ICM_SET_QBUDG, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_QBUDG, "48", PCAT_ICM_DESC_SET_QBUDG, true, PCAT_ICM_SET_QBUDG_MIN, PCAT_ICM_SET_QBUDG_MAX, nullptr},
    {0x0308, PCAT_ICM_SET_BUZEN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_BUZEN, "1", PCAT_ICM_DESC_SET_BUZEN, false, 0U, 0U, nullptr},
    {0x0309, PCAT_ICM_SET_LEDFB, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_LEDFB, "1", PCAT_ICM_DESC_SET_LEDFB, false, 0U, 0U, nullptr},
    {0x030A, PCAT_ICM_SET_FANMD, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_FANMD, "0", PCAT_ICM_DESC_SET_FANMD, true, PCAT_ICM_SET_FANMD_MIN, PCAT_ICM_SET_FANMD_MAX, nullptr},

    {0x0401, PCAT_ICM_SET_BLENM, espnow_link::SettingValueType::String, PCAT_ICM_KEY_BLENM, "ICM", "type=str;rw=1;min=1;max=31", false, 0U, 0U, nullptr},
    {0x0402, PCAT_ICM_SET_BLEPK, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_BLEPK, "123456", "type=u32;rw=1;min=100000;max=999999", true, 100000U, 999999U, nullptr},
    {0x0403, PCAT_ICM_SET_PIN6, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_PIN6, "123456", "type=u32;rw=1;min=100000;max=999999", true, 100000U, 999999U, nullptr},
    {0x0404, PCAT_ICM_SET_APSID, espnow_link::SettingValueType::String, PCAT_ICM_KEY_APSID, "ICM_AP", "type=str;rw=1;min=1;max=32", false, 0U, 0U, nullptr},
    {0x0405, PCAT_ICM_SET_APKEY, espnow_link::SettingValueType::String, PCAT_ICM_KEY_APKEY, "12345678", "type=str;rw=1;min=8;max=63", false, 0U, 0U, nullptr},
    {0x0406, PCAT_ICM_SET_STSID, espnow_link::SettingValueType::String, PCAT_ICM_KEY_STSID, "NONENONE", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0407, PCAT_ICM_SET_STKEY, espnow_link::SettingValueType::String, PCAT_ICM_KEY_STKEY, "NONENONE", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0408, PCAT_ICM_SET_WEBUSR, espnow_link::SettingValueType::String, PCAT_ICM_KEY_WEBUSR, "admin", "type=str;rw=1;min=1;max=31", false, 0U, 0U, nullptr},
    {0x0409, PCAT_ICM_SET_WEBPWD, espnow_link::SettingValueType::String, PCAT_ICM_KEY_WEBPWD, "admin123", "type=str;rw=1;min=1;max=63", false, 0U, 0U, nullptr},
    {0x040A, PCAT_ICM_SET_ICMCFG, espnow_link::SettingValueType::String, PCAT_ICM_KEY_ICMCFG, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x040B, PCAT_ICM_SET_SLVCFG, espnow_link::SettingValueType::String, PCAT_ICM_KEY_SLVCFG, "{}", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x040C, PCAT_ICM_SET_FWSEL, espnow_link::SettingValueType::String, PCAT_ICM_KEY_FWSEL, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x040D, PCAT_ICM_SET_SFWSEL, espnow_link::SettingValueType::String, PCAT_ICM_KEY_SFWSEL, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x040E, PCAT_ICM_SET_PRMOD, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_PRMOD, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x040F, PCAT_ICM_SET_TOPINT, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_TOPINT, "8", "type=u32;rw=1;min=1;max=3600", true, 1U, 3600U, nullptr},
    {0x0410, PCAT_ICM_SET_BCRTRY, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_BCRTRY, "3", "type=u32;rw=1;min=0;max=20", true, 0U, 20U, nullptr},
    {0x0411, PCAT_ICM_SET_TSINT, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_TSINT, "120", "type=u32;rw=1;min=1;max=3600", true, 1U, 3600U, nullptr},
    {0x0412, PCAT_ICM_SET_RTCsrc, espnow_link::SettingValueType::String, PCAT_ICM_KEY_RTCsrc, "NTP", "type=str;rw=1;enum=NTP|RTC|MANUAL", false, 0U, 0U, "NTP|RTC|MANUAL"},
    {0x0413, PCAT_ICM_SET_AUTOTS, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_AUTOTS, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0414, PCAT_ICM_SET_TZOFF, espnow_link::SettingValueType::String, PCAT_ICM_KEY_TZOFF, "0", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0415, PCAT_ICM_SET_SYNBOT, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_SYNBOT, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0416, PCAT_ICM_SET_RLYCMD, espnow_link::SettingValueType::String, PCAT_ICM_KEY_RLYCMD, "DIRECT", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0417, PCAT_ICM_SET_MANOVR, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_MANOVR, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0418, PCAT_ICM_SET_PMSAUT, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_PMSAUT, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0419, PCAT_ICM_SET_RLYDEF, espnow_link::SettingValueType::String, PCAT_ICM_KEY_RLYDEF, "OFF", "type=str;rw=1;enum=OFF|ON", false, 0U, 0U, "OFF|ON"},
    {0x041A, PCAT_ICM_SET_TMPTH, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_TMPTH, "65", "type=u32;rw=1;min=0;max=125", true, 0U, 125U, nullptr},
    {0x041B, PCAT_ICM_SET_TMPWRN, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_TMPWRN, "55", "type=u32;rw=1;min=0;max=125", true, 0U, 125U, nullptr},
    {0x041C, PCAT_ICM_SET_BUZMOD, espnow_link::SettingValueType::String, PCAT_ICM_KEY_BUZMOD, "ALERT", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x041D, PCAT_ICM_SET_SAFELK, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_SAFELK, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x041E, PCAT_ICM_SET_FALTAL, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_FALTAL, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x041F, PCAT_ICM_SET_RGBIDL, espnow_link::SettingValueType::String, PCAT_ICM_KEY_RGBIDL, "#00ffaa", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0420, PCAT_ICM_SET_RGBALT, espnow_link::SettingValueType::String, PCAT_ICM_KEY_RGBALT, "#ff3366", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0421, PCAT_ICM_SET_RGBBRT, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_RGBBRT, "180", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x0422, PCAT_ICM_SET_BLKACT, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_BLKACT, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0427, PCAT_ICM_SET_AUTRCN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_AUTRCN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0428, PCAT_ICM_SET_FLTRTY, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_FLTRTY, "5", "type=u32;rw=1;min=0;max=100", true, 0U, 100U, nullptr},
    {0x0429, PCAT_ICM_SET_PRIOVR, espnow_link::SettingValueType::String, PCAT_ICM_KEY_PRIOVR, "CCID", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x042A, PCAT_ICM_SET_ALRNEW, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_ALRNEW, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x042B, PCAT_ICM_SET_WEBUPD, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_WEBUPD, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x042C, PCAT_ICM_SET_USRNM, espnow_link::SettingValueType::String, PCAT_ICM_KEY_USRNM, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x042D, PCAT_ICM_SET_USRSR, espnow_link::SettingValueType::String, PCAT_ICM_KEY_USRSR, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x042E, PCAT_ICM_SET_USRPH, espnow_link::SettingValueType::String, PCAT_ICM_KEY_USRPH, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x042F, PCAT_ICM_SET_USREM, espnow_link::SettingValueType::String, PCAT_ICM_KEY_USREM, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0430, PCAT_ICM_SET_EMLEN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_EMLEN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0431, PCAT_ICM_SET_SMTHST, espnow_link::SettingValueType::String, PCAT_ICM_KEY_SMTHST, "smtp.gmail.com", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0432, PCAT_ICM_SET_SMTPRT, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_SMTPRT, "465", "type=u32;rw=1;min=1;max=65535", true, 1U, 65535U, nullptr},
    {0x0433, PCAT_ICM_SET_SMTUSR, espnow_link::SettingValueType::String, PCAT_ICM_KEY_SMTUSR, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0434, PCAT_ICM_SET_SMTPWD, espnow_link::SettingValueType::String, PCAT_ICM_KEY_SMTPWD, "", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0435, PCAT_ICM_SET_SMTNAM, espnow_link::SettingValueType::String, PCAT_ICM_KEY_SMTNAM, "EasyDriveway ICM", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0436, PCAT_ICM_SET_EMLWLC, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_EMLWLC, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0437, PCAT_ICM_SET_WTHEME, espnow_link::SettingValueType::String, PCAT_ICM_KEY_WTHEME, "dark", "type=str;rw=1;enum=dark|light", false, 0U, 0U, "dark|light"},
    {0x0438, PCAT_ICM_SET_UIRFRS, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_UIRFRS, "1000", "type=u32;rw=1;min=100;max=60000", true, 100U, 60000U, nullptr},
    {0x0439, PCAT_ICM_SET_AUTSAV, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_AUTSAV, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x043A, PCAT_ICM_SET_BCMAC, espnow_link::SettingValueType::String, PCAT_ICM_KEY_BCMAC, "FF:FF:FF:FF:FF:FF", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x043B, PCAT_ICM_SET_PNGINT, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_PNGINT, "10", "type=u32;rw=1;min=1;max=3600", true, 1U, 3600U, nullptr},
    {0x043C, PCAT_ICM_SET_PNGTO, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_PNGTO, "3", "type=u32;rw=1;min=1;max=120", true, 1U, 120U, nullptr},
    {0x043D, PCAT_ICM_SET_RSTDLY, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_RSTDLY, "500", "type=u32;rw=1;min=0;max=60000", true, 0U, 60000U, nullptr},
    {0x043E, PCAT_ICM_SET_MAXNOD, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_MAXNOD, "64", "type=u32;rw=1;min=1;max=1000", true, 1U, 1000U, nullptr},
    {0x043F, PCAT_ICM_SET_REQACK, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_REQACK, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0440, PCAT_ICM_SET_EVTEN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_EVTEN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0441, PCAT_ICM_SET_EVTPOL, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_EVTPOL, "5000", "type=u32;rw=1;min=100;max=120000", true, 100U, 120000U, nullptr},
    {0x0442, PCAT_ICM_SET_EVTOFF, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_EVTOFF, "20000", "type=u32;rw=1;min=100;max=120000", true, 100U, 120000U, nullptr},
    {0x0443, PCAT_ICM_SET_EVTFAIL, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_EVTFAIL, "3", "type=u32;rw=1;min=0;max=20", true, 0U, 20U, nullptr},
    {0x0444, PCAT_ICM_SET_EVTPUSH, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_EVTPUSH, "5000", "type=u32;rw=1;min=100;max=120000", true, 100U, 120000U, nullptr},
    {0x0445, PCAT_ICM_SET_HOSTNM, espnow_link::SettingValueType::String, PCAT_ICM_KEY_HOSTNM, "icm_driveway", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0446, PCAT_ICM_SET_CNTRY, espnow_link::SettingValueType::String, PCAT_ICM_KEY_CNTRY, "US", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0447, PCAT_ICM_SET_STADHC, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_STADHC, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0448, PCAT_ICM_SET_STAIP, espnow_link::SettingValueType::String, PCAT_ICM_KEY_STAIP, "192.168.4.50", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0449, PCAT_ICM_SET_STAGW, espnow_link::SettingValueType::String, PCAT_ICM_KEY_STAGW, "192.168.4.1", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x044A, PCAT_ICM_SET_STASUB, espnow_link::SettingValueType::String, PCAT_ICM_KEY_STASUB, "255.255.255.0", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x044B, PCAT_ICM_SET_STADN1, espnow_link::SettingValueType::String, PCAT_ICM_KEY_STADN1, "1.1.1.1", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x044C, PCAT_ICM_SET_STADN2, espnow_link::SettingValueType::String, PCAT_ICM_KEY_STADN2, "8.8.8.8", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x044D, PCAT_ICM_SET_APCH, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_APCH, "1", "type=u32;rw=1;min=1;max=14", true, 1U, 14U, nullptr},
    {0x044E, PCAT_ICM_SET_APMAX, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_APMAX, "2", "type=u32;rw=1;min=1;max=10", true, 1U, 10U, nullptr},
    {0x044F, PCAT_ICM_SET_APHID, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_APHID, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0450, PCAT_ICM_SET_APAUT, espnow_link::SettingValueType::String, PCAT_ICM_KEY_APAUT, "WPA2", "type=str;rw=1;enum=OPEN|WPA2|WPA3", false, 0U, 0U, "OPEN|WPA2|WPA3"},
    {0x0451, PCAT_ICM_SET_WTXDBM, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_WTXDBM, "19", "type=u32;rw=1;min=0;max=30", true, 0U, 30U, nullptr},
    {0x0452, PCAT_ICM_SET_WSLEEP, espnow_link::SettingValueType::String, PCAT_ICM_KEY_WSLEEP, "NONE", "type=str;rw=1;enum=NONE|MIN|MAX", false, 0U, 0U, "NONE|MIN|MAX"},
    {0x0453, PCAT_ICM_SET_ENCEN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_ENCEN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0454, PCAT_ICM_SET_MDNSEN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_MDNSEN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0455, PCAT_ICM_SET_CAPTEN, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_CAPTEN, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0456, PCAT_ICM_SET_BTXDBM, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_BTXDBM, "3", "type=u32;rw=1;min=0;max=20", true, 0U, 20U, nullptr},
    {0x0457, PCAT_ICM_SET_BADVMS, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_BADVMS, "200", "type=u32;rw=1;min=20;max=10000", true, 20U, 10000U, nullptr},
    {0x0458, PCAT_ICM_SET_OWAIT, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_OWAIT, "3000", "type=u32;rw=1;min=100;max=60000", true, 100U, 60000U, nullptr},
    {0x0459, PCAT_ICM_SET_OEVRG, espnow_link::SettingValueType::Int, PCAT_ICM_KEY_OEVRG, "256", "type=u32;rw=1;min=16;max=4096", true, 16U, 4096U, nullptr},
    {0x045A, PCAT_ICM_SET_OBCFM, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_OBCFM, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x045B, PCAT_ICM_SET_ORFRC, espnow_link::SettingValueType::Bool, PCAT_ICM_KEY_ORFRC, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
};

const SettingDef* findSettingDefByKey(const std::string& key) {
  for (const auto& def : kSettingDefs) {
    if (key == def.key) {
      return &def;
    }
  }
  return nullptr;
}

const SettingDef* findSettingDefById(uint16_t id) {
  for (const auto& def : kSettingDefs) {
    if (id == def.id) {
      return &def;
    }
  }
  return nullptr;
}

espnow_link::TelemetrySample makeSample(uint16_t metric_id,
                                        const char* key,
                                        const std::string& value,
                                        const char* unit) {
  espnow_link::TelemetrySample sample{};
  sample.metric_id = metric_id;
  sample.key = (key != nullptr) ? key : "";
  sample.value = value;
  sample.unit = (unit != nullptr) ? unit : "";
  return sample;
}

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildIcmTelemetrySpec() {
  std::vector<espnow_link::ProfileTelemetryMetricSpec> out;
  out.reserve(7);
  espnow_link::ProfileTelemetryMetricSpec t{};
  t.metric_id = 0x01;
  t.key = PCAT_ICM_MET_PCNT;
  out.push_back(t);
  t.metric_id = 0x02;
  t.key = PCAT_ICM_MET_OCNT;
  out.push_back(t);
  t.metric_id = 0x03;
  t.key = PCAT_ICM_MET_OFFCNT;
  out.push_back(t);
  t.metric_id = 0x04;
  t.key = PCAT_ICM_MET_QDEP;
  out.push_back(t);
  t.metric_id = 0x05;
  t.key = PCAT_ICM_MET_QDRP;
  out.push_back(t);
  t.metric_id = 0x06;
  t.key = PCAT_ICM_MET_LIVEN;
  out.push_back(t);
  t.metric_id = 0x07;
  t.key = PCAT_ICM_MET_TEMP;
  out.push_back(t);
  return out;
}

std::vector<espnow_link::ProfileSettingSpec> buildIcmSettingSpec() {
  std::vector<espnow_link::ProfileSettingSpec> out;
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& def : kSettingDefs) {
    espnow_link::ProfileSettingSpec s{};
    s.setting_id = def.id;
    s.key = def.key;
    out.push_back(s);
  }
  return out;
}

std::vector<espnow_link::ProfileEventSpec> buildIcmEventSpec() {
  std::vector<espnow_link::ProfileEventSpec> out;
  out.reserve(5);
  espnow_link::ProfileEventSpec e{};
  e.event_id = 0xE501;
  e.key = PCAT_ICM_EVT_SLV_ON;
  out.push_back(e);
  e.event_id = 0xE502;
  e.key = PCAT_ICM_EVT_SLV_OFF;
  out.push_back(e);
  e.event_id = 0xE503;
  e.key = PCAT_ICM_EVT_PAIR_CAP;
  out.push_back(e);
  e.event_id = 0xE504;
  e.key = PCAT_ICM_EVT_OTA_ST;
  out.push_back(e);
  e.event_id = 0xE505;
  e.key = PCAT_ICM_EVT_OTA_DONE;
  out.push_back(e);
  return out;
}

}  // namespace

IcmAppDescriptorProvider::IcmAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                   const espnow_link::MacAddress& local_mac,
                                                   espnow_link::ITimeSink* time_sink,
                                                   espnow_link::IStorageExplorerProvider* storage,
                                                   espnow_link::OtaDescriptorAdapter* ota,
                                                   const IcmDescriptorAppConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool IcmAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_ICM_DEV_TYPE : cfg_.device_type;
  espnow_link::MacAddress device_mac = local_mac_;
#if defined(ARDUINO)
  uint8_t wifi_mac[6] = {0};
  if (esp_wifi_get_mac(WIFI_IF_STA, wifi_mac) == ESP_OK) {
    for (size_t i = 0; i < device_mac.size(); ++i) {
      device_mac[i] = wifi_mac[i];
    }
  }
#endif
  out.device_id = espnow_link::macToString(device_mac);
  out.device_name = loadString_(PCAT_ICM_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_ICM_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_ICM_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_ICM_BUILD_ID : cfg_.build_id;
  return true;
}

bool IcmAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"multi_slave_14", "Hard max 14 paired peers, no balancing"});
  out.push_back({"targeted_commands", "Per-slave control via index or mac"});
  out.push_back({"output_onoff", "Direct ON/OFF control for RELAY outputs and REMU child outputs"});
  out.push_back({"liveness_monitor", "Change-only online/offline transitions"});
  out.push_back({"pairing_control", "pair/unpair/remove orchestration"});
  out.push_back({"settings_rw", "App-owned schema and NVS keys"});
  out.push_back({"telemetry_pull", "Local state telemetry"});
  out.push_back({"ota_pipeline", "prepare->push->apply pipeline controls"});
  return true;
}

bool IcmAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
  out.clear();

  espnow_link::TelemetryDescriptor t{};
  t.metric_id = 0x01;
  t.key = PCAT_ICM_MET_PCNT;
  t.unit = "count";
  t.description = PCAT_ICM_DESC_MET_PCNT;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x02;
  t.key = PCAT_ICM_MET_OCNT;
  t.unit = "count";
  t.description = PCAT_ICM_DESC_MET_OCNT;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x03;
  t.key = PCAT_ICM_MET_OFFCNT;
  t.unit = "count";
  t.description = PCAT_ICM_DESC_MET_OFFCNT;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x04;
  t.key = PCAT_ICM_MET_QDEP;
  t.unit = "count";
  t.description = PCAT_ICM_DESC_MET_QDEP;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x05;
  t.key = PCAT_ICM_MET_QDRP;
  t.unit = "count";
  t.description = PCAT_ICM_DESC_MET_QDRP;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x06;
  t.key = PCAT_ICM_MET_LIVEN;
  t.unit = "bool";
  t.description = PCAT_ICM_DESC_MET_LIVEN;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x07;
  t.key = PCAT_ICM_MET_TEMP;
  t.unit = "C";
  t.description = PCAT_ICM_DESC_MET_TEMP;
  out.push_back(t);

  return true;
}

bool IcmAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();

  const uint32_t paired_count = loadU32_(PCAT_ICM_KEY_PCNT, static_cast<uint32_t>(PCAT_ICM_MET_PCNT_MIN));
  const uint32_t online_count = loadU32_(PCAT_ICM_KEY_OCNT, paired_count);
  const uint32_t offline_count = (online_count <= paired_count) ? (paired_count - online_count) : 0U;
  const uint32_t queue_depth = loadU32_(PCAT_ICM_KEY_QDEP, static_cast<uint32_t>(PCAT_ICM_MET_QDEP_MIN));
  const uint32_t queue_drop = loadU32_(PCAT_ICM_KEY_QDRP, static_cast<uint32_t>(PCAT_ICM_MET_QDRP_MIN));
  const bool live_enabled = loadBool_(PCAT_ICM_KEY_LIVEN, PCAT_ICM_SET_LIVEN_DEF != 0);
#if defined(ARDUINO)
  const float temp_c_fallback = 30.0f + static_cast<float>((millis() / 1700U) % 25U) * 0.2f;
#else
  const float temp_c_fallback = 30.0f;
#endif
  const float temp_c = loadFloat_(PCAT_ICM_KEY_TEMP, temp_c_fallback);

  out.push_back(makeSample(0x01, PCAT_ICM_MET_PCNT, std::to_string(static_cast<unsigned long>(paired_count)), "count"));
  out.push_back(makeSample(0x02, PCAT_ICM_MET_OCNT, std::to_string(static_cast<unsigned long>(online_count)), "count"));
  out.push_back(makeSample(0x03, PCAT_ICM_MET_OFFCNT, std::to_string(static_cast<unsigned long>(offline_count)), "count"));
  out.push_back(makeSample(0x04, PCAT_ICM_MET_QDEP, std::to_string(static_cast<unsigned long>(queue_depth)), "count"));
  out.push_back(makeSample(0x05, PCAT_ICM_MET_QDRP, std::to_string(static_cast<unsigned long>(queue_drop)), "count"));
  out.push_back(makeSample(0x06, PCAT_ICM_MET_LIVEN, live_enabled ? "1" : "0", "bool"));
  out.push_back(makeSample(0x07, PCAT_ICM_MET_TEMP, std::to_string(static_cast<double>(temp_c)), "C"));
  return true;
}

bool IcmAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool IcmAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool IcmAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
  if (epoch_s < 946684800ULL || epoch_s > 4102444800ULL) {
    out_message = "time out of range";
    return false;
  }
  if (time_sink_ == nullptr || !time_sink_->setEpochSec(epoch_s)) {
    out_message = "settimeofday failed";
    return false;
  }
  out_message = "time updated";
  return true;
}

bool IcmAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
  out.clear();
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& def : kSettingDefs) {
    espnow_link::SettingDescriptor s{};
    s.setting_id = def.id;
    s.key = def.key;
    s.value_type = def.type;
    s.writable = true;
    s.nvs_key = def.nvs;
    s.default_value = (def.def != nullptr) ? def.def : "";
    s.description = (def.desc != nullptr) ? def.desc : "";
    switch (def.type) {
      case espnow_link::SettingValueType::String:
        s.current_value = loadString_(def.nvs, def.def);
        break;
      case espnow_link::SettingValueType::Bool: {
        bool def_bool = false;
        (void)parseBool((def.def != nullptr) ? def.def : "0", def_bool);
        s.current_value = loadBool_(def.nvs, def_bool) ? "1" : "0";
        break;
      }
      case espnow_link::SettingValueType::Int: {
        uint32_t def_int = 0U;
        (void)parseU32((def.def != nullptr) ? def.def : "0", def_int);
        s.current_value = std::to_string(static_cast<unsigned long>(loadU32_(def.nvs, def_int)));
        break;
      }
      default:
        s.current_value = loadString_(def.nvs, def.def);
        break;
    }
    out.push_back(s);
  }

  return true;
}

bool IcmAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
  const SettingDef* def = findSettingDefByKey(key);
  if (def == nullptr) {
    return false;
  }
  return getSettingById(def->id, out);
}

bool IcmAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
  const SettingDef* def = findSettingDefById(setting_id);
  if (def == nullptr) {
    return false;
  }

  out = espnow_link::SettingDescriptor{};
  out.setting_id = def->id;
  out.key = def->key;
  out.value_type = def->type;
  out.writable = true;
  out.nvs_key = def->nvs;
  out.default_value = (def->def != nullptr) ? def->def : "";
  out.description = (def->desc != nullptr) ? def->desc : "";
  switch (def->type) {
    case espnow_link::SettingValueType::String:
      out.current_value = loadString_(def->nvs, def->def);
      break;
    case espnow_link::SettingValueType::Bool: {
      bool def_bool = false;
      (void)parseBool((def->def != nullptr) ? def->def : "0", def_bool);
      out.current_value = loadBool_(def->nvs, def_bool) ? "1" : "0";
      break;
    }
    case espnow_link::SettingValueType::Int: {
      uint32_t def_int = 0U;
      (void)parseU32((def->def != nullptr) ? def->def : "0", def_int);
      out.current_value = std::to_string(static_cast<unsigned long>(loadU32_(def->nvs, def_int)));
      break;
    }
    default:
      out.current_value = loadString_(def->nvs, def->def);
      break;
  }
  return true;
}

bool IcmAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
  const SettingDef* def = findSettingDefByKey(key);
  if (def == nullptr) {
    out_message = "unknown setting";
    return false;
  }
  switch (def->type) {
    case espnow_link::SettingValueType::String: {
      if (key == PCAT_ICM_SET_DNAME && value.empty()) {
        out_message = "device_name cannot be empty";
        return false;
      }
      if (!enumContains(def->enum_values, value)) {
        out_message = key + " invalid value";
        return false;
      }
      const bool ok = nvs_.putString(def->nvs, value);
      out_message = ok ? (key + " updated") : (key + " persist failed");
      return ok;
    }
    case espnow_link::SettingValueType::Bool: {
      bool parsed = false;
      if (!parseBool(value, parsed)) {
        out_message = key + " expects bool";
        return false;
      }
      const bool ok = nvs_.putBool(def->nvs, parsed);
      out_message = ok ? (key + " updated") : (key + " persist failed");
      return ok;
    }
    case espnow_link::SettingValueType::Int: {
      uint32_t parsed = 0U;
      if (!parseU32(value, parsed)) {
        out_message = key + " expects int";
        return false;
      }
      if (def->has_int_range && (parsed < def->int_min || parsed > def->int_max)) {
        out_message = key + " out of range";
        return false;
      }
      const bool ok = nvs_.putU32(def->nvs, parsed);
      out_message = ok ? (key + " updated") : (key + " persist failed");
      return ok;
    }
    default:
      out_message = "unsupported setting type";
      return false;
  }
}

bool IcmAppDescriptorProvider::setSettingById(uint16_t setting_id,
                                              const std::string& value,
                                              std::string& out_message) {
  const SettingDef* def = findSettingDefById(setting_id);
  if (def == nullptr || def->key == nullptr || def->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(def->key, value, out_message);
}

bool IcmAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool IcmAppDescriptorProvider::listStoragePath(const std::string& path,
                                               std::string& out_canonical_path,
                                               std::string& out_parent_path,
                                               std::vector<espnow_link::StorageEntry>& out_entries,
                                               std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->listStoragePath(path, out_canonical_path, out_parent_path, out_entries, out_message);
}

bool IcmAppDescriptorProvider::statStoragePath(const std::string& path,
                                               espnow_link::StorageStat& out,
                                               std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool IcmAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool IcmAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool IcmAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool IcmAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool IcmAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool IcmAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool IcmAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool IcmAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

uint32_t IcmAppDescriptorProvider::loadU32_(const char* key, uint32_t fallback) const {
  uint32_t out = fallback;
  (void)nvs_.getU32(key, out);
  return out;
}

bool IcmAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float IcmAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string IcmAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

espnow_link::ProfileId IcmAppProfileDefinition::profileId() const {
  return kAppProfileIcm;
}

const char* IcmAppProfileDefinition::profileName() const {
  return PCAT_ICM_DEV_TYPE;
}

espnow_link::CodecId IcmAppProfileDefinition::defaultCodecId() const {
  return espnow_link::kCodecIdCompactIndexed;
}

bool IcmAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const {
  return espnow_link::isBuiltInCodecId(codec_id);
}

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& IcmAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildIcmTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& IcmAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildIcmSettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& IcmAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildIcmEventSpec();
  return kEvents;
}

const IcmAppProfileDefinition& icmProfileDefinition() {
  static const IcmAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makeIcmSlaveSchemaPackage(IcmAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &icmProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfileIcm;
  pkg.codec_id = codec_id;
  return pkg;
}

MasterSchemaPackage makeIcmMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &icmProfileDefinition();
  pkg.profile_id = kAppProfileIcm;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned


