#include "profile_catalog/slaves/alarm/alarm_profile.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {
namespace {

constexpr const char* kAlarmProfileIdText = "35";
constexpr const char* kAlarmSchemaRev = "1";
constexpr const char* kAlarmSchemaHash = "alm002";

enum class FieldType : uint8_t { String, Int, Float, Bool };

struct TelemetryDef {
  uint16_t id;
  const char* key;
  const char* unit;
  float min_v;
  float max_v;
};

struct EventDef {
  uint16_t id;
  const char* key;
};

struct SettingDef {
  uint16_t id;
  const char* key;
  const char* nvs;
  FieldType type;
  const char* def;
  const char* enum_values;
  uint32_t int_min;
  uint32_t int_max;
  bool has_int_range;
  float float_min;
  float float_max;
  bool has_float_range;
};

constexpr TelemetryDef kTelemetryDefs[] = {
    {0x01, PCAT_ALARM_MET_BATTPCT, "%", 0.0f, 100.0f},
    {0x02, PCAT_ALARM_MET_BATTV, "V", 0.0f, 20.0f},
    {0x03, PCAT_ALARM_MET_BATTI, "A", -20.0f, 20.0f},
    {0x04, PCAT_ALARM_MET_REED, "bool", 0.0f, 1.0f},
    {0x05, PCAT_ALARM_MET_MOTION, "bool", 0.0f, 1.0f},
    {0x06, PCAT_ALARM_MET_BREACH, "bool", 0.0f, 1.0f},
    {0x07, PCAT_ALARM_MET_ARMED, "bool", 0.0f, 1.0f},
    {0x08, PCAT_ALARM_MET_UPTIME, "ms", 0.0f, 4294967295.0f},
};

constexpr EventDef kEventDefs[] = {
    {0xE701, PCAT_ALARM_EVT_REEDOPEN},
    {0xE702, PCAT_ALARM_EVT_REEDCLOSE},
    {0xE703, PCAT_ALARM_EVT_MOTION},
    {0xE704, PCAT_ALARM_EVT_BREACHSET},
    {0xE705, PCAT_ALARM_EVT_BREACHCLR},
    {0xE706, PCAT_ALARM_EVT_BATLOW},
    {0xE707, PCAT_ALARM_EVT_BATCRIT},
    {0xE708, PCAT_ALARM_EVT_SLPPEND},
    {0xE709, PCAT_ALARM_EVT_SLPENTER},
    {0xE70A, PCAT_ALARM_EVT_SLPWAKE},
};

#define ALARM_SETTINGS_TABLE(X) \
  X(0x0001, DNAME, String, PCAT_ALARM_DEF_NAME, nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0002, DEVID, String, "ALM_xxxx", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0003, ROLE, String, "alarm", "lock|alarm", 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0004, CHAN, Int, "1", nullptr, 1U, 14U, true, 0.0f, 0.0f, false) \
  X(0x0005, CFGED, Bool, "0", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0006, MSTRMC, String, "00:00:00:00:00:00", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0007, MSLMK, String, "", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0008, SPMAX, Int, "15", nullptr, 1U, 15U, true, 0.0f, 0.0f, false) \
  X(0x0009, SPLIST, String, "[]", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x000A, PRTMO, Int, "15000", nullptr, 1000U, 120000U, true, 0.0f, 0.0f, false) \
  X(0x000B, ARMED, Bool, "0", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x000C, MOTEN, Bool, "0", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x000D, BRCHL, Bool, "0", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x000E, CTIME, Int, "1736121600", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x000F, LTIME, Int, "1736121600", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0010, CAREED, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0011, CACCEL, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0012, CFUEL, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0013, CRGB, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0014, BTLVL, Int, "1", nullptr, 0U, 1U, true, 0.0f, 0.0f, false) \
  X(0x0015, USLVL, Int, "1", nullptr, 0U, 1U, true, 0.0f, 0.0f, false) \
  X(0x0016, RDLVL, Int, "0", nullptr, 0U, 1U, true, 0.0f, 0.0f, false) \
  X(0x0017, BTDBMS, Int, "25", nullptr, 1U, 1000U, true, 0.0f, 0.0f, false) \
  X(0x0018, RDDBMS, Int, "25", nullptr, 1U, 1000U, true, 0.0f, 0.0f, false) \
  X(0x0019, BTHOLD, Int, "3000", nullptr, 100U, 30000U, true, 0.0f, 0.0f, false) \
  X(0x001A, BTCGAP, Int, "450", nullptr, 50U, 5000U, true, 0.0f, 0.0f, false) \
  X(0x001B, BTFMS, Int, "350", nullptr, 50U, 5000U, true, 0.0f, 0.0f, false) \
  X(0x001C, CBUEN, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x001D, CBFMS, Int, "350", nullptr, 50U, 5000U, true, 0.0f, 0.0f, false) \
  X(0x001E, LBATT, Int, "20", nullptr, 0U, 100U, true, 0.0f, 0.0f, false) \
  X(0x001F, EBATT, Int, "6", nullptr, 0U, 100U, true, 0.0f, 0.0f, false) \
  X(0x0020, CBATT, Int, "3", nullptr, 0U, 100U, true, 0.0f, 0.0f, false)
#define ALARM_SETTINGS_TABLE_2(X) \
  X(0x0021, PWEVAL, Int, "30000", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0022, GGTICK, Int, "250", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0023, BSTALE, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0024, SLPIN, Int, "240000", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0025, SLWKUS, Int, "60000000", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0026, SLPCHK, Int, "0", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0027, PSHEN, Bool, "0", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0028, PSHMD, String, "hybrid", "periodic|change|hybrid", 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0029, PSHINT, Int, "2000", nullptr, 200U, 60000U, true, 0.0f, 0.0f, false) \
  X(0x002A, PSHDLT, Float, "0.0", nullptr, 0U, 0U, false, 0.0f, 100000.0f, true) \
  X(0x002B, PSHGAP, Int, "200", nullptr, 50U, 10000U, true, 0.0f, 0.0f, false) \
  X(0x002C, PSHSCP, String, "all", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x002D, RGBLVL, Int, "0", nullptr, 0U, 1U, true, 0.0f, 0.0f, false) \
  X(0x002E, RGBIDL, String, "#00ffaa", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x002F, RGBALT, String, "#ff3366", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0030, RGBBRT, Int, "180", nullptr, 0U, 255U, true, 0.0f, 0.0f, false) \
  X(0x0031, LEDFB, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0032, CBAUD, Int, "115200", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0033, SHTHR, Int, "16", nullptr, 0U, 127U, true, 0.0f, 0.0f, false) \
  X(0x0034, SHODR, Int, "5", nullptr, 0U, 15U, true, 0.0f, 0.0f, false) \
  X(0x0035, SHSCL, Int, "0", nullptr, 0U, 3U, true, 0.0f, 0.0f, false) \
  X(0x0036, SHRES, Int, "2", nullptr, 0U, 2U, true, 0.0f, 0.0f, false) \
  X(0x0037, SHEVT, Int, "0", nullptr, 0U, 3U, true, 0.0f, 0.0f, false) \
  X(0x0038, SHDUR, Int, "2", nullptr, 0U, 127U, true, 0.0f, 0.0f, false) \
  X(0x0039, SHAXS, Int, "42", nullptr, 0U, 255U, true, 0.0f, 0.0f, false) \
  X(0x003A, SHHPM, Int, "0", nullptr, 0U, 3U, true, 0.0f, 0.0f, false) \
  X(0x003B, SHHPC, Int, "0", nullptr, 0U, 3U, true, 0.0f, 0.0f, false) \
  X(0x003C, SHHPE, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x003D, SHLAT, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x003E, SHILV, Int, "1", nullptr, 0U, 1U, true, 0.0f, 0.0f, false) \
  X(0x003F, SHCDMS, Int, "1000", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0040, SHRGAP, Int, "300", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0041, SHCFCT, Int, "1", nullptr, 1U, 10U, true, 0.0f, 0.0f, false) \
  X(0x0042, SHCFMS, Int, "200", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0043, ALMOT, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0044, ALRED, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0045, AENTRY, Int, "0", nullptr, 0U, 60000U, true, 0.0f, 0.0f, false) \
  X(0x0046, AEXIT, Int, "0", nullptr, 0U, 60000U, true, 0.0f, 0.0f, false) \
  X(0x0047, ALHLD, Int, "200", nullptr, 0U, 60000U, true, 0.0f, 0.0f, false) \
  X(0x0048, ALGAP, Int, "1000", nullptr, 0U, 120000U, true, 0.0f, 0.0f, false) \
  X(0x0049, ALRPT, Int, "3000", nullptr, 0U, 600000U, true, 0.0f, 0.0f, false) \
  X(0x004A, ALCLR, Int, "0", nullptr, 0U, 86400U, true, 0.0f, 0.0f, false) \
  X(0x004B, ALMCLR, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x004C, ALCMB, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x004D, ALCMHS, Int, "2000", nullptr, 0U, 120000U, true, 0.0f, 0.0f, false) \
  X(0x004E, ALSIL, Bool, "0", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x004F, ALPAT, String, "pulse_red", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0050, ALBRD, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0051, ALBRM, Bool, "1", nullptr, 0U, 0U, false, 0.0f, 0.0f, false) \
  X(0x0052, ALMGAT, Int, "5000", nullptr, 0U, 120000U, true, 0.0f, 0.0f, false) \
  X(0x0053, ALMWIN, Int, "300", nullptr, 0U, 60000U, true, 0.0f, 0.0f, false) \
  X(0x0054, ALMCNT, Int, "1", nullptr, 1U, 20U, true, 0.0f, 0.0f, false)

#define ALARM_SETTING_ROW(id, suffix, type, def, enums, imin, imax, irange, fmin, fmax, frange) \
  {id, PCAT_ALARM_SET_##suffix, PCAT_ALARM_KEY_##suffix, FieldType::type, def, enums, imin, imax, irange, fmin, fmax, frange},

constexpr SettingDef kSettingDefs[] = {
    ALARM_SETTINGS_TABLE(ALARM_SETTING_ROW)
    ALARM_SETTINGS_TABLE_2(ALARM_SETTING_ROW)
};

#undef ALARM_SETTING_ROW

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

bool isSupportedCliBaud(uint32_t baud) {
  static constexpr uint32_t kSupported[] = {9600U, 19200U, 38400U, 57600U, 74880U, 115200U, 230400U, 250000U, 460800U, 921600U};
  for (const uint32_t value : kSupported) {
    if (value == baud) {
      return true;
    }
  }
  return false;
}

espnow_link::TelemetrySample makeSample(uint16_t metric_id, const char* key, const std::string& value, const char* unit) {
  espnow_link::TelemetrySample sample{};
  sample.metric_id = metric_id;
  sample.key = (key != nullptr) ? key : "";
  sample.value = value;
  sample.unit = (unit != nullptr) ? unit : "";
  return sample;
}

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildAlarmTelemetrySpec() {
  std::vector<espnow_link::ProfileTelemetryMetricSpec> out;
  out.reserve(sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]));
  for (const auto& m : kTelemetryDefs) {
    out.push_back({m.id, m.key});
  }
  return out;
}

std::vector<espnow_link::ProfileSettingSpec> buildAlarmSettingSpec() {
  std::vector<espnow_link::ProfileSettingSpec> out;
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& s : kSettingDefs) {
    out.push_back({s.id, s.key});
  }
  return out;
}

std::vector<espnow_link::ProfileEventSpec> buildAlarmEventSpec() {
  std::vector<espnow_link::ProfileEventSpec> out;
  out.reserve(sizeof(kEventDefs) / sizeof(kEventDefs[0]));
  for (const auto& e : kEventDefs) {
    out.push_back({e.id, e.key});
  }
  return out;
}

}  // namespace

AlarmAppDescriptorProvider::AlarmAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                       const espnow_link::MacAddress& local_mac,
                                                       espnow_link::ITimeSink* time_sink,
                                                       espnow_link::IStorageExplorerProvider* storage,
                                                       espnow_link::OtaDescriptorAdapter* ota,
                                                       const AlarmDescriptorAppConfig& cfg)
    : nvs_(nvs), local_mac_(local_mac), time_sink_(time_sink), storage_(storage), ota_(ota), cfg_(cfg) {}

bool AlarmAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_ALARM_DEV_TYPE : cfg_.device_type;
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
  out.device_name = loadString_(PCAT_ALARM_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_ALARM_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_ALARM_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_ALARM_BUILD_ID : cfg_.build_id;
  return true;
}

bool AlarmAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pfid", kAlarmProfileIdText});
  out.push_back({"pfnm", PCAT_ALARM_DEV_TYPE});
  out.push_back({"schrv", kAlarmSchemaRev});
  out.push_back({"schsh", kAlarmSchemaHash});
  out.push_back({"metid", "1"});
  out.push_back({"setid", "1"});
  out.push_back({"evid", "1"});
  out.push_back({"setmap", PCAT_ALARM_SETMAP});
  out.push_back({"metmap", PCAT_ALARM_METMAP});
  out.push_back({"evmap", PCAT_ALARM_EVMAP});
  out.push_back({"cmdset", "gdesc,gcaps,gtel,pull,glive,gtime,stime,gset,sset,ota,log,sd"});
  out.push_back({"secure_peers", "Configured secure peer ceiling <= 15"});
  return true;
}

bool AlarmAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
  out.clear();
  out.reserve(sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]));
  for (const auto& m : kTelemetryDefs) {
    espnow_link::TelemetryDescriptor t{};
    t.metric_id = m.id;
    t.key = m.key;
    t.unit = m.unit;
    t.min_value = m.min_v;
    t.max_value = m.max_v;
    t.description = "type=runtime";
    out.push_back(t);
  }
  return true;
}

bool AlarmAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();
  return appendTelemetryFromRuntime_(out);
}

bool AlarmAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool AlarmAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool AlarmAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool AlarmAppDescriptorProvider::rebuildSettingsCache_(std::vector<espnow_link::SettingDescriptor>& out) const {
  out.clear();
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& s : kSettingDefs) {
    espnow_link::SettingDescriptor d{};
    d.setting_id = s.id;
    d.key = s.key;
    d.writable = true;
    d.nvs_key = s.nvs;
    d.default_value = s.def;
    d.description = "type=setting";

    switch (s.type) {
      case FieldType::String:
        d.value_type = espnow_link::SettingValueType::String;
        d.current_value = loadString_(s.nvs, s.def);
        break;
      case FieldType::Int:
        d.value_type = espnow_link::SettingValueType::Int;
        d.current_value = std::to_string(static_cast<unsigned long>(loadU32_(s.nvs, static_cast<uint32_t>(std::strtoul(s.def, nullptr, 10)))));
        break;
      case FieldType::Float:
        d.value_type = espnow_link::SettingValueType::Float;
        d.current_value = formatFloat_(loadFloat_(s.nvs, std::strtof(s.def, nullptr)));
        break;
      case FieldType::Bool:
      default:
        d.value_type = espnow_link::SettingValueType::Bool;
        d.current_value = loadBool_(s.nvs, std::strtoul(s.def, nullptr, 10) != 0U) ? "1" : "0";
        break;
    }
    out.push_back(d);
  }
  return true;
}

bool AlarmAppDescriptorProvider::ensureSettingsCache_() const {
  if (settings_cache_valid_) {
    return true;
  }
  settings_cache_.clear();
  if (!rebuildSettingsCache_(settings_cache_)) {
    settings_cache_valid_ = false;
    return false;
  }
  settings_cache_valid_ = true;
  return true;
}

void AlarmAppDescriptorProvider::invalidateSettingsCache_() {
  settings_cache_valid_ = false;
  settings_cache_.clear();
}

bool AlarmAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
  out.clear();
  if (!ensureSettingsCache_()) {
    return false;
  }
  out = settings_cache_;
  return true;
}

bool AlarmAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
  if (!ensureSettingsCache_()) {
    return false;
  }
  for (const auto& s : settings_cache_) {
    if (s.key == key) {
      out = s;
      return true;
    }
  }
  return false;
}

bool AlarmAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
  if (!ensureSettingsCache_()) {
    return false;
  }
  for (const auto& s : settings_cache_) {
    if (s.setting_id == setting_id) {
      out = s;
      return true;
    }
  }
  return false;
}

bool AlarmAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
  const SettingDef* def = nullptr;
  for (const auto& s : kSettingDefs) {
    if (key == s.key) {
      def = &s;
      break;
    }
  }
  if (def == nullptr) {
    out_message = "unknown setting";
    return false;
  }

  if (key == PCAT_ALARM_SET_CBAUD) {
    uint32_t baud = 0U;
    if (!parseU32(value, baud) || !isSupportedCliBaud(baud)) {
      out_message = "cli_baud expects one of: 9600|19200|38400|57600|74880|115200|230400|250000|460800|921600";
      return false;
    }
    const bool ok = nvs_.putU32(def->nvs, baud);
    out_message = ok ? "cli_baud updated (restart required)" : "cli_baud persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }

  bool ok = false;
  if (def->type == FieldType::String) {
    if (!enumContains(def->enum_values, value)) {
      out_message = std::string(def->key) + " enum mismatch";
      return false;
    }
    ok = nvs_.putString(def->nvs, value);
  } else if (def->type == FieldType::Bool) {
    bool parsed = false;
    if (!parseBool(value, parsed)) {
      out_message = std::string(def->key) + " expects bool";
      return false;
    }
    ok = nvs_.putBool(def->nvs, parsed);
  } else if (def->type == FieldType::Float) {
    float parsed = 0.0f;
    if (!parseFloat(value, parsed)) {
      out_message = std::string(def->key) + " expects float";
      return false;
    }
    if (def->has_float_range && (parsed < def->float_min || parsed > def->float_max)) {
      out_message = std::string(def->key) + " out of range";
      return false;
    }
    ok = nvs_.putFloat(def->nvs, parsed);
  } else {
    uint32_t parsed = 0U;
    if (!parseU32(value, parsed)) {
      out_message = std::string(def->key) + " expects integer";
      return false;
    }
    if (def->has_int_range && (parsed < def->int_min || parsed > def->int_max)) {
      out_message = std::string(def->key) + " out of range";
      return false;
    }
    ok = nvs_.putU32(def->nvs, parsed);
  }

  out_message = ok ? std::string(def->key) + " updated" : std::string(def->key) + " persist failed";
  return finalizeSettingChange_(key, value, ok, out_message);
}

bool AlarmAppDescriptorProvider::setSettingById(uint16_t setting_id, const std::string& value, std::string& out_message) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&alarmProfileDefinition(), setting_id);
  if (spec == nullptr || spec->key == nullptr || spec->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(spec->key, value, out_message);
}

bool AlarmAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool AlarmAppDescriptorProvider::listStoragePath(const std::string& path,
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

bool AlarmAppDescriptorProvider::statStoragePath(const std::string& path, espnow_link::StorageStat& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool AlarmAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool AlarmAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool AlarmAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool AlarmAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool AlarmAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool AlarmAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool AlarmAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool AlarmAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

bool AlarmAppDescriptorProvider::finalizeSettingChange_(const std::string& key,
                                                        const std::string& value,
                                                        bool persisted_ok,
                                                        std::string& out_message) {
  if (!persisted_ok) {
    if (cfg_.setting_feedback != nullptr) {
      cfg_.setting_feedback(cfg_.runtime_user, key, value, false);
    }
    return false;
  }

  if (cfg_.apply_setting != nullptr) {
    std::string apply_message{};
    const bool applied = cfg_.apply_setting(cfg_.runtime_user, key, value, apply_message);
    if (!applied) {
      out_message = apply_message.empty() ? (key + " apply failed (persisted)") : apply_message;
      if (cfg_.setting_feedback != nullptr) {
        cfg_.setting_feedback(cfg_.runtime_user, key, value, false);
      }
      return false;
    }
    if (!apply_message.empty()) {
      out_message = apply_message;
    }
  }

  if (cfg_.setting_feedback != nullptr) {
    cfg_.setting_feedback(cfg_.runtime_user, key, value, true);
  }
  if (settings_cache_valid_) {
    settings_cache_valid_ = false;
    (void)ensureSettingsCache_();
  }
  return true;
}

bool AlarmAppDescriptorProvider::appendTelemetryFromRuntime_(std::vector<espnow_link::TelemetrySample>& out) {
  AlarmRuntimeTelemetrySnapshot snapshot{};
  bool have_runtime = false;
  if (cfg_.read_telemetry != nullptr) {
    have_runtime = cfg_.read_telemetry(cfg_.runtime_user, snapshot) && snapshot.valid;
  }

#if defined(ARDUINO)
  const uint32_t now_ms = millis();
#else
  const uint32_t now_ms = 0U;
#endif

  const float battery_pct = have_runtime ? snapshot.battery_soc_pct : 0.0f;
  const float battery_v = have_runtime ? snapshot.battery_v : 0.0f;
  const float battery_i = have_runtime ? snapshot.battery_i : 0.0f;
  const bool reed = have_runtime ? snapshot.reed_state : false;
  const bool motion = have_runtime ? snapshot.motion_state : false;
  const bool breach_state = have_runtime ? snapshot.breach_state : loadBool_(PCAT_ALARM_KEY_BRCHL, false);
  const bool armed_state = have_runtime ? snapshot.armed_state : loadBool_(PCAT_ALARM_KEY_ARMED, false);
  const uint32_t uptime = have_runtime ? snapshot.uptime_ms : now_ms;

  out.push_back(makeSample(0x01, PCAT_ALARM_MET_BATTPCT, formatFloat_(battery_pct), "%"));
  out.push_back(makeSample(0x02, PCAT_ALARM_MET_BATTV, formatFloat_(battery_v), "V"));
  out.push_back(makeSample(0x03, PCAT_ALARM_MET_BATTI, formatFloat_(battery_i), "A"));
  out.push_back(makeSample(0x04, PCAT_ALARM_MET_REED, reed ? "1" : "0", "bool"));
  out.push_back(makeSample(0x05, PCAT_ALARM_MET_MOTION, motion ? "1" : "0", "bool"));
  out.push_back(makeSample(0x06, PCAT_ALARM_MET_BREACH, breach_state ? "1" : "0", "bool"));
  out.push_back(makeSample(0x07, PCAT_ALARM_MET_ARMED, armed_state ? "1" : "0", "bool"));
  out.push_back(makeSample(0x08, PCAT_ALARM_MET_UPTIME, std::to_string(static_cast<unsigned long>(uptime)), "ms"));
  return true;
}

uint32_t AlarmAppDescriptorProvider::loadU32_(const char* key, uint32_t fallback) const {
  uint32_t out = fallback;
  (void)nvs_.getU32(key, out);
  return out;
}

bool AlarmAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float AlarmAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string AlarmAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

std::string AlarmAppDescriptorProvider::formatFloat_(float value) const {
  char buf[24] = {0};
  std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
  return std::string(buf);
}

espnow_link::ProfileId AlarmAppProfileDefinition::profileId() const { return kAppProfileAlarm; }
const char* AlarmAppProfileDefinition::profileName() const { return PCAT_ALARM_DEV_TYPE; }
espnow_link::CodecId AlarmAppProfileDefinition::defaultCodecId() const { return espnow_link::kCodecIdCompactIndexed; }
bool AlarmAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const { return espnow_link::isBuiltInCodecId(codec_id); }

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& AlarmAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildAlarmTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& AlarmAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildAlarmSettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& AlarmAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildAlarmEventSpec();
  return kEvents;
}

const AlarmAppProfileDefinition& alarmProfileDefinition() {
  static const AlarmAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makeAlarmSlaveSchemaPackage(AlarmAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &alarmProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfileAlarm;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
