#include "profile_catalog/masters/mla/mla_profile.hpp"

#include <cstdlib>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {
namespace {

constexpr const char* kMlaProfileIdText = "36";
constexpr const char* kMlaSchemaRev = "1";
constexpr const char* kMlaSchemaHash = "mla001";

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

struct TelemetryDef {
  uint16_t id;
  const char* key;
  const char* unit;
  float min_v;
  float max_v;
  const char* desc;
};

struct EventDef {
  uint16_t id;
  const char* key;
};

constexpr SettingDef kSettingDefs[] = {
    {0x0001, PCAT_MLA_SET_DNAME, espnow_link::SettingValueType::String, PCAT_MLA_KEY_DNAME, PCAT_MLA_DEF_NAME, "type=str;rw=1;min=1;max=31", false, 0U, 0U, nullptr},
    {0x0002, PCAT_MLA_SET_DEVID, espnow_link::SettingValueType::String, PCAT_MLA_KEY_DEVID, "MLA_xxxx", "type=str;rw=1;min=1;max=31", false, 0U, 0U, nullptr},
    {0x0003, PCAT_MLA_SET_ROLE, espnow_link::SettingValueType::String, PCAT_MLA_KEY_ROLE, "mla", "type=str;rw=1;enum=mla", false, 0U, 0U, "mla"},
    {0x0004, PCAT_MLA_SET_CFGED, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_CFGED, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0005, PCAT_MLA_SET_RSTFL, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_RSTFL, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},

    {0x0006, PCAT_MLA_SET_MMODE, espnow_link::SettingValueType::String, PCAT_MLA_KEY_MMODE, "lock", "type=str;rw=1;enum=lock|alarm", false, 0U, 0U, "lock|alarm"},
    {0x0007, PCAT_MLA_SET_MLSID, espnow_link::SettingValueType::String, PCAT_MLA_KEY_MLSID, "unset", "type=str;rw=1;enum=unset|side|rear", false, 0U, 0U, "unset|side|rear"},
    {0x0008, PCAT_MLA_SET_MLFP, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_MLFP, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0009, PCAT_MLA_SET_NMWF, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_NMWF, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x000A, PCAT_MLA_SET_GTCFG, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_GTCFG, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x000B, PCAT_MLA_SET_LKTM, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_LKTM, "8000", "type=u32;rw=1;min=100;max=120000", true, 100U, 120000U, nullptr},
    {0x000C, PCAT_MLA_SET_LKMG, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_LKMG, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x000D, PCAT_MLA_SET_ALDLY, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_ALDLY, "60000", "type=u32;rw=1;min=0;max=86400000", true, 0U, 86400000U, nullptr},
    {0x000E, PCAT_MLA_SET_MWIN, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_MWIN, "10", "type=u32;rw=1;min=0;max=3600", true, 0U, 3600U, nullptr},
    {0x000F, PCAT_MLA_SET_ALMST, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_ALMST, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},

    {0x0010, PCAT_MLA_SET_PINCOD, espnow_link::SettingValueType::String, PCAT_MLA_KEY_PINCOD, "1234567890", "type=str;rw=1;min=4;max=16", false, 0U, 0U, nullptr},
    {0x0011, PCAT_MLA_SET_USRPNR, espnow_link::SettingValueType::String, PCAT_MLA_KEY_USRPNR, "+0000000000", "type=str;rw=1;min=5;max=24", false, 0U, 0U, nullptr},
    {0x0012, PCAT_MLA_SET_GQC, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_GQC, "0", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x0013, PCAT_MLA_SET_BZPAS, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_BZPAS, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0014, PCAT_MLA_SET_CBQC, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_CBQC, "0", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},

    {0x0015, PCAT_MLA_SET_SBAUD, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SBAUD, "9600", "type=u32;rw=1;min=1200;max=921600", true, 1200U, 921600U, nullptr},
    {0x0016, PCAT_MLA_SET_STMO, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_STMO, "10000", "type=u32;rw=1;min=100;max=120000", true, 100U, 120000U, nullptr},
    {0x0017, PCAT_MLA_SET_SPKMS, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SPKMS, "1200", "type=u32;rw=1;min=10;max=10000", true, 10U, 10000U, nullptr},
    {0x0018, PCAT_MLA_SET_SBWMS, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SBWMS, "3500", "type=u32;rw=1;min=0;max=30000", true, 0U, 30000U, nullptr},
    {0x0019, PCAT_MLA_SET_SPKAH, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_SPKAH, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x001A, PCAT_MLA_SET_SDTRH, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_SDTRH, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x001B, PCAT_MLA_SET_SSLP, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_SSLP, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x001C, PCAT_MLA_SET_SACMS, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SACMS, "1800000", "type=u32;rw=1;min=0;max=86400000", true, 0U, 86400000U, nullptr},

    {0x001D, PCAT_MLA_SET_KPDB, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_KPDB, "20", "type=u32;rw=1;min=1;max=1000", true, 1U, 1000U, nullptr},
    {0x001E, PCAT_MLA_SET_KPRLS, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_KPRLS, "40", "type=u32;rw=1;min=1;max=1000", true, 1U, 1000U, nullptr},
    {0x001F, PCAT_MLA_SET_KPSCN, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_KPSCN, "20", "type=u32;rw=1;min=1;max=5000", true, 1U, 5000U, nullptr},
    {0x0020, PCAT_MLA_SET_KPSTL, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_KPSTL, "50", "type=u32;rw=1;min=1;max=5000", true, 1U, 5000U, nullptr},

    {0x0021, PCAT_MLA_SET_BZAHI, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_BZAHI, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0022, PCAT_MLA_SET_BUZEN, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_BUZEN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0023, PCAT_MLA_SET_RGBLVL, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_RGBLVL, "0", "type=u32;rw=1;min=0;max=1", true, 0U, 1U, nullptr},
    {0x0024, PCAT_MLA_SET_RGBIDL, espnow_link::SettingValueType::String, PCAT_MLA_KEY_RGBIDL, "#00ffaa", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0025, PCAT_MLA_SET_RGBALT, espnow_link::SettingValueType::String, PCAT_MLA_KEY_RGBALT, "#ff3366", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x0026, PCAT_MLA_SET_RGBBRT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_RGBBRT, "180", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x0027, PCAT_MLA_SET_LEDFB, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_LEDFB, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},

    {0x0028, PCAT_MLA_SET_BTLVL, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_BTLVL, "1", "type=u32;rw=1;min=0;max=1", true, 0U, 1U, nullptr},
    {0x0029, PCAT_MLA_SET_USLVL, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_USLVL, "1", "type=u32;rw=1;min=0;max=1", true, 0U, 1U, nullptr},
    {0x002A, PCAT_MLA_SET_BTDBMS, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_BTDBMS, "25", "type=u32;rw=1;min=1;max=1000", true, 1U, 1000U, nullptr},
    {0x002B, PCAT_MLA_SET_BTHOLD, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_BTHOLD, "3000", "type=u32;rw=1;min=100;max=30000", true, 100U, 30000U, nullptr},
    {0x002C, PCAT_MLA_SET_BTCGAP, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_BTCGAP, "450", "type=u32;rw=1;min=50;max=5000", true, 50U, 5000U, nullptr},
    {0x002D, PCAT_MLA_SET_CBUEN, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_CBUEN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x002E, PCAT_MLA_SET_CBFMS, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_CBFMS, "350", "type=u32;rw=1;min=50;max=5000", true, 50U, 5000U, nullptr},
    {0x002F, PCAT_MLA_SET_SHTHR, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHTHR, "16", "type=u32;rw=1;min=0;max=127", true, 0U, 127U, nullptr},
    {0x0030, PCAT_MLA_SET_SHODR, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHODR, "5", "type=u32;rw=1;min=0;max=15", true, 0U, 15U, nullptr},
    {0x0031, PCAT_MLA_SET_SHSCL, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHSCL, "0", "type=u32;rw=1;min=0;max=3", true, 0U, 3U, nullptr},
    {0x0032, PCAT_MLA_SET_SHRES, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHRES, "2", "type=u32;rw=1;min=0;max=2", true, 0U, 2U, nullptr},
    {0x0033, PCAT_MLA_SET_SHEVT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHEVT, "0", "type=u32;rw=1;min=0;max=3", true, 0U, 3U, nullptr},
    {0x0034, PCAT_MLA_SET_SHDUR, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHDUR, "2", "type=u32;rw=1;min=0;max=127", true, 0U, 127U, nullptr},
    {0x0035, PCAT_MLA_SET_SHAXS, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHAXS, "42", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x0036, PCAT_MLA_SET_SHHPM, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHHPM, "0", "type=u32;rw=1;min=0;max=3", true, 0U, 3U, nullptr},
    {0x0037, PCAT_MLA_SET_SHHPC, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHHPC, "0", "type=u32;rw=1;min=0;max=3", true, 0U, 3U, nullptr},
    {0x0038, PCAT_MLA_SET_SHHPE, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_SHHPE, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0039, PCAT_MLA_SET_SHLAT, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_SHLAT, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x003A, PCAT_MLA_SET_SHILV, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SHILV, "1", "type=u32;rw=1;min=0;max=1", true, 0U, 1U, nullptr},

    {0x003B, PCAT_MLA_SET_CHGEN, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_CHGEN, "1", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x003C, PCAT_MLA_SET_CHGMA, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_CHGMA, "1500", "type=u32;rw=1;min=0;max=5000", true, 0U, 5000U, nullptr},
    {0x003D, PCAT_MLA_SET_CHGVM, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_CHGVM, "4208", "type=u32;rw=1;min=3500;max=5000", true, 3500U, 5000U, nullptr},
    {0x003E, PCAT_MLA_SET_IINMA, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_IINMA, "500", "type=u32;rw=1;min=0;max=5000", true, 0U, 5000U, nullptr},
    {0x003F, PCAT_MLA_SET_VINMV, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_VINMV, "4400", "type=u32;rw=1;min=3500;max=5500", true, 3500U, 5500U, nullptr},
    {0x0040, PCAT_MLA_SET_OTGEN, espnow_link::SettingValueType::Bool, PCAT_MLA_KEY_OTGEN, "0", "type=bool;rw=1", false, 0U, 0U, nullptr},
    {0x0041, PCAT_MLA_SET_WDOG, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_WDOG, "40", "type=u32;rw=1;min=0;max=160", true, 0U, 160U, nullptr},
    {0x0042, PCAT_MLA_SET_LBATT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_LBATT, "20", "type=u32;rw=1;min=0;max=100", true, 0U, 100U, nullptr},
    {0x0043, PCAT_MLA_SET_CBATT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_CBATT, "10", "type=u32;rw=1;min=0;max=100", true, 0U, 100U, nullptr},
    {0x0044, PCAT_MLA_SET_BTEVL, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_BTEVL, "100000", "type=u32;rw=1;min=100;max=600000", true, 100U, 600000U, nullptr},

    {0x0045, PCAT_MLA_SET_SDCNT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SDCNT, "0", "type=u32;rw=1;min=0;max=8", true, 0U, 8U, nullptr},
    {0x0046, PCAT_MLA_SET_RDCNT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_RDCNT, "0", "type=u32;rw=1;min=0;max=8", true, 0U, 8U, nullptr},
    {0x0047, PCAT_MLA_SET_SORD, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SORD, "255", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x0048, PCAT_MLA_SET_SOMT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SOMT, "255", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x0049, PCAT_MLA_SET_SOOP, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_SOOP, "255", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x004A, PCAT_MLA_SET_RORD, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_RORD, "255", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x004B, PCAT_MLA_SET_ROMT, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_ROMT, "255", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x004C, PCAT_MLA_SET_ROOP, espnow_link::SettingValueType::Int, PCAT_MLA_KEY_ROOP, "255", "type=u32;rw=1;min=0;max=255", true, 0U, 255U, nullptr},
    {0x004D, PCAT_MLA_SET_SDPOL, espnow_link::SettingValueType::String, PCAT_MLA_KEY_SDPOL, "{}", "type=str;rw=1", false, 0U, 0U, nullptr},
    {0x004E, PCAT_MLA_SET_RDPOL, espnow_link::SettingValueType::String, PCAT_MLA_KEY_RDPOL, "{}", "type=str;rw=1", false, 0U, 0U, nullptr},
};

constexpr TelemetryDef kTelemetryDefs[] = {
    {0x01, PCAT_MLA_MET_PCNT, "count", 0.0f, 15.0f, "type=u16;pull=1;push=1"},
    {0x02, PCAT_MLA_MET_OCNT, "count", 0.0f, 15.0f, "type=u16;pull=1;push=1"},
    {0x03, PCAT_MLA_MET_OFFCNT, "count", 0.0f, 15.0f, "type=u16;pull=1;push=1"},
    {0x04, PCAT_MLA_MET_QDEP, "count", 0.0f, 512.0f, "type=u16;pull=1;push=1"},
    {0x05, PCAT_MLA_MET_QDRP, "count", 0.0f, 65535.0f, "type=u16;pull=1;push=1"},
    {0x06, PCAT_MLA_MET_ARMED, "bool", 0.0f, 1.0f, "type=bool;pull=1;push=1"},
    {0x07, PCAT_MLA_MET_ALMST, "bool", 0.0f, 1.0f, "type=bool;pull=1;push=1"},
    {0x08, PCAT_MLA_MET_BATTPCT, "%", 0.0f, 100.0f, "type=f32;pull=1;push=1"},
    {0x09, PCAT_MLA_MET_BATTV, "V", 0.0f, 20.0f, "type=f32;pull=1;push=1"},
    {0x0A, PCAT_MLA_MET_BATTI, "A", -20.0f, 20.0f, "type=f32;pull=1;push=1"},
    {0x0B, PCAT_MLA_MET_UPTIME, "ms", 0.0f, 4294967295.0f, "type=u32;pull=1;push=1"},
};

constexpr EventDef kEventDefs[] = {
    {0xE801, PCAT_MLA_EVT_SLV_ON},
    {0xE802, PCAT_MLA_EVT_SLV_OFF},
    {0xE803, PCAT_MLA_EVT_PAIRCAP},
    {0xE804, PCAT_MLA_EVT_ARMSET},
    {0xE805, PCAT_MLA_EVT_ARMCLR},
    {0xE806, PCAT_MLA_EVT_BRSET},
    {0xE807, PCAT_MLA_EVT_BRCLR},
    {0xE808, PCAT_MLA_EVT_UNLREQ},
    {0xE809, PCAT_MLA_EVT_UNLDON},
    {0xE80A, PCAT_MLA_EVT_LKDONE},
    {0xE80B, PCAT_MLA_EVT_MDMALT},
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

bool parseFloat(const std::string& value, float& out) {
  if (value.empty()) {
    return false;
  }
  char* endp = nullptr;
  const float parsed = std::strtof(value.c_str(), &endp);
  if (endp == nullptr || *endp != '\0') {
    return false;
  }
  out = parsed;
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
    start = sep + 1U;
  }
  return false;
}

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

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildMlaTelemetrySpec() {
  std::vector<espnow_link::ProfileTelemetryMetricSpec> out;
  out.reserve(sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]));
  for (const auto& def : kTelemetryDefs) {
    out.push_back({def.id, def.key});
  }
  return out;
}

std::vector<espnow_link::ProfileSettingSpec> buildMlaSettingSpec() {
  std::vector<espnow_link::ProfileSettingSpec> out;
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& def : kSettingDefs) {
    out.push_back({def.id, def.key});
  }
  return out;
}

std::vector<espnow_link::ProfileEventSpec> buildMlaEventSpec() {
  std::vector<espnow_link::ProfileEventSpec> out;
  out.reserve(sizeof(kEventDefs) / sizeof(kEventDefs[0]));
  for (const auto& def : kEventDefs) {
    out.push_back({def.id, def.key});
  }
  return out;
}

}  // namespace

MlaAppDescriptorProvider::MlaAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                   const espnow_link::MacAddress& local_mac,
                                                   espnow_link::ITimeSink* time_sink,
                                                   espnow_link::IStorageExplorerProvider* storage,
                                                   espnow_link::OtaDescriptorAdapter* ota,
                                                   const MlaDescriptorAppConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool MlaAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_MLA_DEV_TYPE : cfg_.device_type;
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
  out.device_name = loadString_(PCAT_MLA_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_MLA_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_MLA_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_MLA_BUILD_ID : cfg_.build_id;
  return true;
}

bool MlaAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pfid", kMlaProfileIdText});
  out.push_back({"pfnm", PCAT_MLA_DEV_TYPE});
  out.push_back({"schrv", kMlaSchemaRev});
  out.push_back({"schsh", kMlaSchemaHash});
  out.push_back({"metid", "1"});
  out.push_back({"setid", "1"});
  out.push_back({"evid", "1"});
  out.push_back({"setmap", PCAT_MLA_SETMAP});
  out.push_back({"metmap", PCAT_MLA_METMAP});
  out.push_back({"evmap", PCAT_MLA_EVMAP});
  out.push_back({"cmdset", "gdesc,gcaps,gtel,pull,glive,gtime,stime,gset,sset,ota,log,sd"});
  out.push_back({"secure_peer_policy", "hard max 15 secure peers (link runtime managed)"});
  out.push_back({"link_key_ownership", "PMK/LMK/pairing internals managed by espnow-link runtime"});
  return true;
}

bool MlaAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
  out.clear();
  out.reserve(sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]));
  for (const auto& def : kTelemetryDefs) {
    espnow_link::TelemetryDescriptor metric{};
    metric.metric_id = def.id;
    metric.key = def.key;
    metric.unit = def.unit;
    metric.min_value = def.min_v;
    metric.max_value = def.max_v;
    metric.description = (def.desc != nullptr) ? def.desc : "";
    out.push_back(metric);
  }
  return true;
}

bool MlaAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();

  const uint32_t paired_count = loadU32_(PCAT_MLA_KEY_PCNT, PCAT_MLA_MET_PCNT_MIN);
  const uint32_t online_count = loadU32_(PCAT_MLA_KEY_OCNT, paired_count);
  const uint32_t offline_count = (online_count <= paired_count) ? (paired_count - online_count) : 0U;
  const uint32_t queue_depth = loadU32_(PCAT_MLA_KEY_QDEP, PCAT_MLA_MET_QDEP_MIN);
  const uint32_t queue_drop = loadU32_(PCAT_MLA_KEY_QDRP, PCAT_MLA_MET_QDRP_MIN);
  const bool armed_state = loadBool_(PCAT_MLA_KEY_ARMED, false);
  const bool alarm_state = loadBool_(PCAT_MLA_KEY_ALMST, false);
  const float battery_soc_pct = loadFloat_(PCAT_MLA_KEY_BSOC, 0.0f);
  const float battery_v = loadFloat_(PCAT_MLA_KEY_BVOL, 0.0f);
  const float battery_i = loadFloat_(PCAT_MLA_KEY_BCUR, 0.0f);
#if defined(ARDUINO)
  const uint32_t uptime_ms = loadU32_(PCAT_MLA_KEY_UPMS, millis());
#else
  const uint32_t uptime_ms = loadU32_(PCAT_MLA_KEY_UPMS, 0U);
#endif

  out.push_back(makeSample(0x01, PCAT_MLA_MET_PCNT, std::to_string(static_cast<unsigned long>(paired_count)), "count"));
  out.push_back(makeSample(0x02, PCAT_MLA_MET_OCNT, std::to_string(static_cast<unsigned long>(online_count)), "count"));
  out.push_back(makeSample(0x03, PCAT_MLA_MET_OFFCNT, std::to_string(static_cast<unsigned long>(offline_count)), "count"));
  out.push_back(makeSample(0x04, PCAT_MLA_MET_QDEP, std::to_string(static_cast<unsigned long>(queue_depth)), "count"));
  out.push_back(makeSample(0x05, PCAT_MLA_MET_QDRP, std::to_string(static_cast<unsigned long>(queue_drop)), "count"));
  out.push_back(makeSample(0x06, PCAT_MLA_MET_ARMED, armed_state ? "1" : "0", "bool"));
  out.push_back(makeSample(0x07, PCAT_MLA_MET_ALMST, alarm_state ? "1" : "0", "bool"));
  out.push_back(makeSample(0x08, PCAT_MLA_MET_BATTPCT, std::to_string(static_cast<double>(battery_soc_pct)), "%"));
  out.push_back(makeSample(0x09, PCAT_MLA_MET_BATTV, std::to_string(static_cast<double>(battery_v)), "V"));
  out.push_back(makeSample(0x0A, PCAT_MLA_MET_BATTI, std::to_string(static_cast<double>(battery_i)), "A"));
  out.push_back(makeSample(0x0B, PCAT_MLA_MET_UPTIME, std::to_string(static_cast<unsigned long>(uptime_ms)), "ms"));
  return true;
}

bool MlaAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool MlaAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool MlaAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool MlaAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
  out.clear();
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& def : kSettingDefs) {
    espnow_link::SettingDescriptor setting{};
    setting.setting_id = def.id;
    setting.key = def.key;
    setting.value_type = def.type;
    setting.writable = true;
    setting.nvs_key = def.nvs;
    setting.default_value = (def.def != nullptr) ? def.def : "";
    setting.description = (def.desc != nullptr) ? def.desc : "";
    switch (def.type) {
      case espnow_link::SettingValueType::String:
        setting.current_value = loadString_(def.nvs, def.def);
        break;
      case espnow_link::SettingValueType::Bool: {
        bool def_bool = false;
        (void)parseBool((def.def != nullptr) ? def.def : "0", def_bool);
        setting.current_value = loadBool_(def.nvs, def_bool) ? "1" : "0";
        break;
      }
      case espnow_link::SettingValueType::Int: {
        uint32_t def_int = 0U;
        (void)parseU32((def.def != nullptr) ? def.def : "0", def_int);
        setting.current_value = std::to_string(static_cast<unsigned long>(loadU32_(def.nvs, def_int)));
        break;
      }
      case espnow_link::SettingValueType::Float: {
        float def_float = 0.0f;
        (void)parseFloat((def.def != nullptr) ? def.def : "0", def_float);
        setting.current_value = std::to_string(static_cast<double>(loadFloat_(def.nvs, def_float)));
        break;
      }
      default:
        setting.current_value = loadString_(def.nvs, def.def);
        break;
    }
    out.push_back(setting);
  }
  return true;
}

bool MlaAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
  const SettingDef* def = findSettingDefByKey(key);
  if (def == nullptr) {
    return false;
  }
  return getSettingById(def->id, out);
}

bool MlaAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
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
    case espnow_link::SettingValueType::Float: {
      float def_float = 0.0f;
      (void)parseFloat((def->def != nullptr) ? def->def : "0", def_float);
      out.current_value = std::to_string(static_cast<double>(loadFloat_(def->nvs, def_float)));
      break;
    }
    default:
      out.current_value = loadString_(def->nvs, def->def);
      break;
  }
  return true;
}

bool MlaAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
  const SettingDef* def = findSettingDefByKey(key);
  if (def == nullptr) {
    out_message = "unknown setting";
    return false;
  }

  switch (def->type) {
    case espnow_link::SettingValueType::String: {
      if (key == PCAT_MLA_SET_DNAME && value.empty()) {
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
    case espnow_link::SettingValueType::Float: {
      float parsed = 0.0f;
      if (!parseFloat(value, parsed)) {
        out_message = key + " expects float";
        return false;
      }
      const bool ok = nvs_.putFloat(def->nvs, parsed);
      out_message = ok ? (key + " updated") : (key + " persist failed");
      return ok;
    }
    default:
      out_message = "unsupported setting type";
      return false;
  }
}

bool MlaAppDescriptorProvider::setSettingById(uint16_t setting_id,
                                              const std::string& value,
                                              std::string& out_message) {
  const SettingDef* def = findSettingDefById(setting_id);
  if (def == nullptr || def->key == nullptr || def->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(def->key, value, out_message);
}

bool MlaAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool MlaAppDescriptorProvider::listStoragePath(const std::string& path,
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

bool MlaAppDescriptorProvider::statStoragePath(const std::string& path,
                                               espnow_link::StorageStat& out,
                                               std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool MlaAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool MlaAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool MlaAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool MlaAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool MlaAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool MlaAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool MlaAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool MlaAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

uint32_t MlaAppDescriptorProvider::loadU32_(const char* key, uint32_t fallback) const {
  uint32_t out = fallback;
  (void)nvs_.getU32(key, out);
  return out;
}

bool MlaAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float MlaAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string MlaAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

espnow_link::ProfileId MlaAppProfileDefinition::profileId() const {
  return kAppProfileMla;
}

const char* MlaAppProfileDefinition::profileName() const {
  return PCAT_MLA_DEV_TYPE;
}

espnow_link::CodecId MlaAppProfileDefinition::defaultCodecId() const {
  return espnow_link::kCodecIdCompactIndexed;
}

bool MlaAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const {
  return espnow_link::isBuiltInCodecId(codec_id);
}

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& MlaAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildMlaTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& MlaAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildMlaSettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& MlaAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildMlaEventSpec();
  return kEvents;
}

const MlaAppProfileDefinition& mlaProfileDefinition() {
  static const MlaAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makeMlaSlaveSchemaPackage(MlaAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &mlaProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfileMla;
  pkg.codec_id = codec_id;
  return pkg;
}

MasterSchemaPackage makeMlaMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &mlaProfileDefinition();
  pkg.profile_id = kAppProfileMla;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
