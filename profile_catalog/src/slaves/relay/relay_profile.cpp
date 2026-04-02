#include "profile_catalog/slaves/relay/relay_profile.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {

namespace {

constexpr const char* kRelayProfileIdText = "2";
constexpr const char* kRelaySchemaRev = "1";
constexpr const char* kRelaySchemaHash = "rly001";

struct TelemetryDef {
  uint16_t id;
  const char* key;
  const char* unit;
  float min_v;
  float max_v;
  const char* desc;
};

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
  bool has_float_range;
  float float_min;
  float float_max;
  const char* enum_values;
};

struct EventDef {
  uint16_t id;
  const char* key;
};

constexpr TelemetryDef kTelemetryDefs[] = {
    {0x01, PCAT_RELAY_MET_BITMAP, "mask", 0.0f, 4294967295.0f, "type=u32;unit=mask;pull=1;push=1"},
    {0x02, PCAT_RELAY_MET_UPTIME, "ms", 0.0f, 4294967295.0f, "type=u32;unit=ms;pull=1;push=1"},
    {0x03, PCAT_RELAY_MET_TEMP, "C", -55.0f, 125.0f, "type=f32;unit=C;pull=1;push=1"},
};

constexpr SettingDef kSettingDefs[] = {
    {0x0001, PCAT_RELAY_SET_DNAME, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_DNAME, PCAT_RELAY_DEF_NAME, "type=str;rw=1;min=1;max=31", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0002, PCAT_RELAY_SET_CHAN, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_CHAN, "1", "type=u32;rw=1;min=1;max=14", true, PCAT_RELAY_SET_CHAN_MIN, PCAT_RELAY_SET_CHAN_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0101, PCAT_RELAY_SET_SPLIT, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_SPLIT, "0", "type=u32;rw=1;min=0;max=255", true, PCAT_RELAY_SET_SPLIT_MIN, PCAT_RELAY_SET_SPLIT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0102, PCAT_RELAY_SET_PULSE, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_PULSE, "500", "type=u32;rw=1;min=0;max=65535", true, PCAT_RELAY_SET_PULSE_MIN, PCAT_RELAY_SET_PULSE_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0103, PCAT_RELAY_SET_HOLD, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_HOLD, "30000", "type=u32;rw=1;min=0;max=65535", true, PCAT_RELAY_SET_HOLD_MIN, PCAT_RELAY_SET_HOLD_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0104, PCAT_RELAY_SET_ILOCK, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_ILOCK, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0105, PCAT_RELAY_SET_RTLIM, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_RTLIM, "80", "type=u32;rw=1;min=0;max=255", true, PCAT_RELAY_SET_RTLIM_MIN, PCAT_RELAY_SET_RTLIM_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0106, PCAT_RELAY_SET_SAMAC, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_SAMAC, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0107, PCAT_RELAY_SET_SBMAC, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_SBMAC, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0108, PCAT_RELAY_SET_R1EN, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_R1EN, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0109, PCAT_RELAY_SET_R2EN, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_R2EN, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0301, PCAT_RELAY_SET_LOOPA, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_LOOPA, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x030B, PCAT_RELAY_SET_OPERS, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_OPERS, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x030A, PCAT_RELAY_SET_FANMD, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_FANMD, "0", "type=u32;rw=1;min=0;max=3;enum=0:auto|1:eco|2:forced|3:stopped", true, PCAT_RELAY_SET_FANMD_MIN, PCAT_RELAY_SET_FANMD_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0308, PCAT_RELAY_SET_BUZEN, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_BUZEN, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x030C, PCAT_RELAY_SET_CLIBD, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_CLIBD, "115200", "type=u32;rw=1;enum=9600|19200|38400|57600|74880|115200|230400|250000|460800|921600", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0309, PCAT_RELAY_SET_LEDFB, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_LEDFB, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0310, PCAT_RELAY_SET_RGBIDL, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_RGBIDL, PCAT_RELAY_SET_RGBIDL_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0311, PCAT_RELAY_SET_RGBALT, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_RGBALT, PCAT_RELAY_SET_RGBALT_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0312, PCAT_RELAY_SET_RGBBRT, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_RGBBRT, "180", "type=u32;rw=1;min=0;max=255", true, PCAT_RELAY_SET_RGBBRT_MIN, PCAT_RELAY_SET_RGBBRT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0302, PCAT_RELAY_SET_PSHEN, espnow_link::SettingValueType::Bool, PCAT_RELAY_KEY_PSHEN, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0303, PCAT_RELAY_SET_PSHMD, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_PSHMD, PCAT_RELAY_SET_PSHMD_DEF, "type=str;rw=1;enum=periodic|change|hybrid", false, 0U, 0U, false, 0.0f, 0.0f, "periodic|change|hybrid"},
    {0x0304, PCAT_RELAY_SET_PSHI, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_PSHI, "2000", "type=u32;rw=1;min=200;max=60000", true, PCAT_RELAY_SET_PSHI_MIN, PCAT_RELAY_SET_PSHI_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0305, PCAT_RELAY_SET_PSHD, espnow_link::SettingValueType::Float, PCAT_RELAY_KEY_PSHD, "0.0", "type=f32;rw=1;min=0.0;max=100000.0", false, 0U, 0U, true, PCAT_RELAY_SET_PSHD_MIN, PCAT_RELAY_SET_PSHD_MAX, nullptr},
    {0x0306, PCAT_RELAY_SET_PSHG, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_PSHG, "200", "type=u32;rw=1;min=50;max=10000", true, PCAT_RELAY_SET_PSHG_MIN, PCAT_RELAY_SET_PSHG_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0307, PCAT_RELAY_SET_PSHS, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_PSHS, PCAT_RELAY_SET_PSHS_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0901, PCAT_RELAY_SET_TOPV, espnow_link::SettingValueType::Int, PCAT_RELAY_KEY_TOPV, "0", "type=u32;rw=1;min=0;max=4294967295", true, 0U, 0xFFFFFFFFU, false, 0.0f, 0.0f, nullptr},
    {0x0902, PCAT_RELAY_SET_TOPS, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_TOPS, "", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0903, PCAT_RELAY_SET_TOPST, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_TOPST, "staged", "type=str;rw=1;enum=staged|committed", false, 0U, 0U, false, 0.0f, 0.0f, "staged|committed"},
    {0x0906, PCAT_RELAY_SET_TOPP, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_TOPP, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0907, PCAT_RELAY_SET_TOPN, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_TOPN, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0908, PCAT_RELAY_SET_TOPA, espnow_link::SettingValueType::String, PCAT_RELAY_KEY_TOPA, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
};

constexpr EventDef kEventDefs[] = {
    {0xE301, "relay_on"},
    {0xE302, "relay_off"},
    {0xE303, "topology_applied"},
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

bool isSupportedCliBaud(const uint32_t baud) {
  static constexpr uint32_t kSupported[] = {
      9600U, 19200U, 38400U, 57600U, 74880U, 115200U, 230400U, 250000U, 460800U, 921600U};
  for (const uint32_t candidate : kSupported) {
    if (baud == candidate) {
      return true;
    }
  }
  return false;
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
    start = sep + 1;
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

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildRelayTelemetrySpec() {
  std::vector<espnow_link::ProfileTelemetryMetricSpec> out;
  out.reserve(sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]));
  for (const auto& m : kTelemetryDefs) {
    espnow_link::ProfileTelemetryMetricSpec t{};
    t.metric_id = m.id;
    t.key = m.key;
    out.push_back(t);
  }
  return out;
}

std::vector<espnow_link::ProfileSettingSpec> buildRelaySettingSpec() {
  std::vector<espnow_link::ProfileSettingSpec> out;
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& s : kSettingDefs) {
    espnow_link::ProfileSettingSpec d{};
    d.setting_id = s.id;
    d.key = s.key;
    out.push_back(d);
  }
  return out;
}

std::vector<espnow_link::ProfileEventSpec> buildRelayEventSpec() {
  std::vector<espnow_link::ProfileEventSpec> out;
  out.reserve(sizeof(kEventDefs) / sizeof(kEventDefs[0]));
  for (const auto& e : kEventDefs) {
    espnow_link::ProfileEventSpec d{};
    d.event_id = e.id;
    d.key = e.key;
    out.push_back(d);
  }
  return out;
}

}  // namespace

RelayAppDescriptorProvider::RelayAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                       const espnow_link::MacAddress& local_mac,
                                                       espnow_link::ITimeSink* time_sink,
                                                       espnow_link::IStorageExplorerProvider* storage,
                                                       espnow_link::OtaDescriptorAdapter* ota,
                                                       const RelayDescriptorAppConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool RelayAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_RELAY_DEV_TYPE : cfg_.device_type;
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
  out.device_name = loadString_(PCAT_RELAY_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_RELAY_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_RELAY_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_RELAY_BUILD_ID : cfg_.build_id;
  return true;
}

bool RelayAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pfid", kRelayProfileIdText});
  out.push_back({"pfnm", PCAT_RELAY_DEV_TYPE});
  out.push_back({"schrv", kRelaySchemaRev});
  out.push_back({"schsh", kRelaySchemaHash});
  out.push_back({"metid", "1"});
  out.push_back({"setid", "1"});
  out.push_back({"evid", "1"});
  out.push_back({"setmap", PCAT_RELAY_SETMAP});
  out.push_back({"metmap", PCAT_RELAY_METMAP});
  out.push_back({"evmap", PCAT_RELAY_EVMAP});
  out.push_back({"cmdset", "gdesc,gcaps,gtel,pull,glive,gtime,stime,gset,sset,ota,log,sd"});
  out.push_back({"pair", "Secure pair handshake"});
  out.push_back({"l2sink", "L2P v1 trigger sink (authorized sources only)"});
  out.push_back({"tpush", "Compact-indexed push compatible schema order"});
  out.push_back({"topology", "Topology commit + allowed source filters"});
  out.push_back({"output_onoff", "Direct output ON/OFF via relay1_enable/relay2_enable"});
  return true;
}

bool RelayAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
  out.clear();
  out.reserve(sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]));
  for (const auto& m : kTelemetryDefs) {
    espnow_link::TelemetryDescriptor t{};
    t.metric_id = m.id;
    t.key = m.key;
    t.unit = m.unit;
    t.min_value = m.min_v;
    t.max_value = m.max_v;
    t.description = m.desc;
    out.push_back(t);
  }
  return true;
}

bool RelayAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();
  return appendTelemetryFromRuntime_(out);
}

bool RelayAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool RelayAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool RelayAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool RelayAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
  out.clear();
  if (!ensureSettingsCache_()) {
    return false;
  }
  out = settings_cache_;
  return true;
}

bool RelayAppDescriptorProvider::rebuildSettingsCache_(std::vector<espnow_link::SettingDescriptor>& out) const {
  out.clear();
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& s : kSettingDefs) {
    espnow_link::SettingDescriptor d{};
    d.setting_id = s.id;
    d.key = s.key;
    d.value_type = s.type;
    d.writable = true;
    d.nvs_key = s.nvs;
    d.default_value = s.def;
    d.description = s.desc;
    switch (s.type) {
      case espnow_link::SettingValueType::Int:
        d.current_value = std::to_string(static_cast<unsigned long>(loadU32_(s.nvs, static_cast<uint32_t>(std::strtoul(s.def, nullptr, 10)))));
        break;
      case espnow_link::SettingValueType::Float:
        d.current_value = formatFloat_(loadFloat_(s.nvs, std::strtof(s.def, nullptr)));
        break;
      case espnow_link::SettingValueType::Bool:
        d.current_value = loadBool_(s.nvs, std::strtoul(s.def, nullptr, 10) != 0U) ? "1" : "0";
        break;
      case espnow_link::SettingValueType::String:
      default:
        d.current_value = loadString_(s.nvs, s.def);
        break;
    }
    out.push_back(d);
  }
  return true;
}

bool RelayAppDescriptorProvider::ensureSettingsCache_() const {
  if (settings_cache_valid_) {
    return true;
  }
  settings_cache_.clear();
  if (!rebuildSettingsCache_(settings_cache_)) {
    settings_cache_.clear();
    settings_cache_valid_ = false;
    return false;
  }
  settings_cache_valid_ = true;
  return true;
}

void RelayAppDescriptorProvider::invalidateSettingsCache_() {
  settings_cache_valid_ = false;
  settings_cache_.clear();
}

bool RelayAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
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

bool RelayAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
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

bool RelayAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
  if (key == PCAT_RELAY_SET_CLIBD) {
    uint32_t baud = 0U;
    if (!parseU32(value, baud) || !isSupportedCliBaud(baud)) {
      out_message = "cli_baud expects one of: 9600|19200|38400|57600|74880|115200|230400|250000|460800|921600";
      return false;
    }
    const bool ok = nvs_.putU32(PCAT_RELAY_KEY_CLIBD, baud);
    out_message = ok ? "cli_baud updated (restart required)" : "cli_baud persist failed";
    if (ok && settings_cache_valid_) {
      settings_cache_valid_ = false;
      (void)ensureSettingsCache_();
    }
    if (cfg_.setting_feedback != nullptr) {
      cfg_.setting_feedback(cfg_.runtime_user, key, value, ok);
    }
    return ok;
  }

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

  switch (def->type) {
    case espnow_link::SettingValueType::String: {
      if (key == PCAT_RELAY_SET_DNAME && value.empty()) {
        out_message = "device_name cannot be empty";
        return false;
      }
      if (!enumContains(def->enum_values, value)) {
        out_message = std::string(def->key) + " enum mismatch";
        return false;
      }
      const bool ok = nvs_.putString(def->nvs, value);
      out_message = ok ? std::string(def->key) + " updated" : std::string(def->key) + " persist failed";
      return finalizeSettingChange_(key, value, ok, out_message);
    }
    case espnow_link::SettingValueType::Bool: {
      bool parsed = false;
      if (!parseBool(value, parsed)) {
        out_message = std::string(def->key) + " expects bool";
        return false;
      }
      const bool is_output_state =
          (key == PCAT_RELAY_SET_R1EN || key == PCAT_RELAY_SET_R2EN);
      if (is_output_state) {
        const bool persist_cfg = loadBool_(PCAT_RELAY_KEY_OPERS, PCAT_RELAY_SET_OPERS_DEF != 0);
        const bool loop_auto = loadBool_(PCAT_RELAY_KEY_LOOPA, PCAT_RELAY_SET_LOOPA_DEF != 0);
        const bool persist_effective = persist_cfg && !loop_auto;
        if (!persist_effective) {
          out_message = std::string(def->key) + " runtime-only update (persist disabled)";
          return finalizeSettingChange_(key, value, true, out_message);
        }
      }
      const bool ok = nvs_.putBool(def->nvs, parsed);
      out_message = ok ? std::string(def->key) + " updated" : std::string(def->key) + " persist failed";
      return finalizeSettingChange_(key, value, ok, out_message);
    }
    case espnow_link::SettingValueType::Int: {
      uint32_t parsed = 0U;
      if (!parseU32(value, parsed)) {
        out_message = std::string(def->key) + " expects integer";
        return false;
      }
      if (def->has_int_range && (parsed < def->int_min || parsed > def->int_max)) {
        out_message = std::string(def->key) + " out of range";
        return false;
      }
      const bool ok = nvs_.putU32(def->nvs, parsed);
      out_message = ok ? std::string(def->key) + " updated" : std::string(def->key) + " persist failed";
      return finalizeSettingChange_(key, value, ok, out_message);
    }
    case espnow_link::SettingValueType::Float: {
      float parsed = 0.0f;
      if (!parseFloat(value, parsed)) {
        out_message = std::string(def->key) + " expects float";
        return false;
      }
      if (def->has_float_range && (parsed < def->float_min || parsed > def->float_max)) {
        out_message = std::string(def->key) + " out of range";
        return false;
      }
      const bool ok = nvs_.putFloat(def->nvs, parsed);
      out_message = ok ? std::string(def->key) + " updated" : std::string(def->key) + " persist failed";
      return finalizeSettingChange_(key, value, ok, out_message);
    }
    default:
      out_message = "unsupported setting type";
      return false;
  }
}

bool RelayAppDescriptorProvider::setSettingById(uint16_t setting_id,
                                                const std::string& value,
                                                std::string& out_message) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&relayProfileDefinition(), setting_id);
  if (spec == nullptr || spec->key == nullptr || spec->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(spec->key, value, out_message);
}

bool RelayAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool RelayAppDescriptorProvider::listStoragePath(const std::string& path,
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

bool RelayAppDescriptorProvider::statStoragePath(const std::string& path,
                                                 espnow_link::StorageStat& out,
                                                 std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool RelayAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool RelayAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool RelayAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool RelayAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool RelayAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool RelayAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool RelayAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool RelayAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

bool RelayAppDescriptorProvider::finalizeSettingChange_(const std::string& key,
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
    std::string apply_message;
    const bool apply_ok = cfg_.apply_setting(cfg_.runtime_user, key, value, apply_message);
    if (!apply_ok) {
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

bool RelayAppDescriptorProvider::appendTelemetryFromRuntime_(std::vector<espnow_link::TelemetrySample>& out) {
  if (cfg_.read_telemetry == nullptr) {
    return false;
  }

  RelayRuntimeTelemetrySnapshot snap{};
  if (!cfg_.read_telemetry(cfg_.runtime_user, snap) || !snap.valid) {
    return false;
  }

#if defined(ARDUINO)
  const uint32_t now = millis();
#else
  const uint32_t now = 0U;
#endif

  out.push_back(
      makeSample(0x01, PCAT_RELAY_MET_BITMAP, std::to_string(static_cast<unsigned long>(snap.relay_bitmap)), "mask"));
  out.push_back(makeSample(0x02, PCAT_RELAY_MET_UPTIME, std::to_string(static_cast<unsigned long>(now)), "ms"));
  out.push_back(makeSample(0x03, PCAT_RELAY_MET_TEMP, formatFloat_(snap.env_temp_c), "C"));
  return true;
}

uint32_t RelayAppDescriptorProvider::loadU32_(const char* key, uint32_t fallback) const {
  uint32_t out = fallback;
  (void)nvs_.getU32(key, out);
  return out;
}

bool RelayAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float RelayAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string RelayAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

std::string RelayAppDescriptorProvider::formatFloat_(float value) const {
  char buf[24] = {0};
  std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
  return std::string(buf);
}

espnow_link::ProfileId RelayAppProfileDefinition::profileId() const {
  return kAppProfileRelay;
}

const char* RelayAppProfileDefinition::profileName() const {
  return PCAT_RELAY_DEV_TYPE;
}

espnow_link::CodecId RelayAppProfileDefinition::defaultCodecId() const {
  return espnow_link::kCodecIdCompactIndexed;
}

bool RelayAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const {
  return espnow_link::isBuiltInCodecId(codec_id);
}

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& RelayAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildRelayTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& RelayAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildRelaySettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& RelayAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildRelayEventSpec();
  return kEvents;
}

const RelayAppProfileDefinition& relayProfileDefinition() {
  static const RelayAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makeRelaySlaveSchemaPackage(RelayAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &relayProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfileRelay;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
