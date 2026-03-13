#include "profile_catalog/slaves/remu/remu_profile.hpp"
#include "profile_catalog/slaves/relay/relay_keys.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {

namespace {

constexpr const char* kRemuProfileIdText = "5";
constexpr const char* kRemuSchemaRev = "1";
constexpr const char* kRemuSchemaHash = "rem001";

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

struct ChildTelemetryDef {
  const char* suffix;
  const char* unit;
  float min_v;
  float max_v;
  const char* desc;
};

struct ChildSettingDef {
  const char* suffix;
  espnow_link::SettingValueType type;
  const char* nvs_code4;
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
    {0x01, PCAT_REMU_MET_BITMAP, "mask", 0.0f, 4294967295.0f, "type=u32;unit=mask;pull=1;push=1"},
    {0x02, PCAT_REMU_MET_RCOUNT, "count", 0.0f, 255.0f, "type=u8;unit=count;pull=1;push=1"},
    {0x03, PCAT_REMU_MET_TEMP, "C", -55.0f, 125.0f, "type=f32;unit=C;pull=1;push=1"},
};

constexpr SettingDef kSettingDefs[] = {
    {0x0001, PCAT_REMU_SET_DNAME, espnow_link::SettingValueType::String, PCAT_REMU_KEY_DNAME, PCAT_REMU_DEF_NAME, "type=str;rw=1;min=1;max=31", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0002, PCAT_REMU_SET_CHAN, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_CHAN, "1", "type=u32;rw=1;min=1;max=14", true, PCAT_REMU_SET_CHAN_MIN, PCAT_REMU_SET_CHAN_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0101, PCAT_REMU_SET_RCNT, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_RCNT, "16", "type=u32;rw=1;min=1;max=16", true, PCAT_REMU_SET_RCNT_MIN, PCAT_REMU_SET_RCNT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0102, PCAT_REMU_SET_SPLIT, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_SPLIT, "0", "type=u32;rw=1;min=0;max=255", true, PCAT_REMU_SET_SPLIT_MIN, PCAT_REMU_SET_SPLIT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0103, PCAT_REMU_SET_GPLS, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_GPLS, "500", "type=u32;rw=1;min=0;max=65535", true, PCAT_REMU_SET_GPLS_MIN, PCAT_REMU_SET_GPLS_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0104, PCAT_REMU_SET_GHLD, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_GHLD, "30000", "type=u32;rw=1;min=0;max=65535", true, PCAT_REMU_SET_GHLD_MIN, PCAT_REMU_SET_GHLD_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0105, PCAT_REMU_SET_RPTMS, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_RPTMS, "1000", "type=u32;rw=1;min=0;max=65535", true, PCAT_REMU_SET_RPTMS_MIN, PCAT_REMU_SET_RPTMS_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0106, PCAT_REMU_SET_ILOCK, espnow_link::SettingValueType::String, PCAT_REMU_KEY_ILOCK, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0107, PCAT_REMU_SET_SAMAC, espnow_link::SettingValueType::String, PCAT_REMU_KEY_SAMAC, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0108, PCAT_REMU_SET_SBMAC, espnow_link::SettingValueType::String, PCAT_REMU_KEY_SBMAC, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0301, PCAT_REMU_SET_LOOPA, espnow_link::SettingValueType::Bool, PCAT_REMU_KEY_LOOPA, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x030A, PCAT_REMU_SET_FANMD, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_FANMD, "0", "type=u32;rw=1;min=0;max=3;enum=0:auto|1:eco|2:forced|3:stopped", true, PCAT_REMU_SET_FANMD_MIN, PCAT_REMU_SET_FANMD_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0308, PCAT_REMU_SET_BUZEN, espnow_link::SettingValueType::Bool, PCAT_REMU_KEY_BUZEN, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0309, PCAT_REMU_SET_LEDFB, espnow_link::SettingValueType::Bool, PCAT_REMU_KEY_LEDFB, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0310, PCAT_REMU_SET_RGBIDL, espnow_link::SettingValueType::String, PCAT_REMU_KEY_RGBIDL, PCAT_REMU_SET_RGBIDL_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0311, PCAT_REMU_SET_RGBALT, espnow_link::SettingValueType::String, PCAT_REMU_KEY_RGBALT, PCAT_REMU_SET_RGBALT_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0312, PCAT_REMU_SET_RGBBRT, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_RGBBRT, "180", "type=u32;rw=1;min=0;max=255", true, PCAT_REMU_SET_RGBBRT_MIN, PCAT_REMU_SET_RGBBRT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0302, PCAT_REMU_SET_PSHEN, espnow_link::SettingValueType::Bool, PCAT_REMU_KEY_PSHEN, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0303, PCAT_REMU_SET_PSHMD, espnow_link::SettingValueType::String, PCAT_REMU_KEY_PSHMD, PCAT_REMU_SET_PSHMD_DEF, "type=str;rw=1;enum=periodic|change|hybrid", false, 0U, 0U, false, 0.0f, 0.0f, "periodic|change|hybrid"},
    {0x0304, PCAT_REMU_SET_PSHI, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_PSHI, "2000", "type=u32;rw=1;min=200;max=60000", true, PCAT_REMU_SET_PSHI_MIN, PCAT_REMU_SET_PSHI_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0305, PCAT_REMU_SET_PSHD, espnow_link::SettingValueType::Float, PCAT_REMU_KEY_PSHD, "0.0", "type=f32;rw=1;min=0.0;max=100000.0", false, 0U, 0U, true, PCAT_REMU_SET_PSHD_MIN, PCAT_REMU_SET_PSHD_MAX, nullptr},
    {0x0306, PCAT_REMU_SET_PSHG, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_PSHG, "200", "type=u32;rw=1;min=50;max=10000", true, PCAT_REMU_SET_PSHG_MIN, PCAT_REMU_SET_PSHG_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0307, PCAT_REMU_SET_PSHS, espnow_link::SettingValueType::String, PCAT_REMU_KEY_PSHS, PCAT_REMU_SET_PSHS_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0901, PCAT_REMU_SET_TOPV, espnow_link::SettingValueType::Int, PCAT_REMU_KEY_TOPV, "0", "type=u32;rw=1;min=0;max=4294967295", true, 0U, 0xFFFFFFFFU, false, 0.0f, 0.0f, nullptr},
    {0x0902, PCAT_REMU_SET_TOPS, espnow_link::SettingValueType::String, PCAT_REMU_KEY_TOPS, "", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0903, PCAT_REMU_SET_TOPST, espnow_link::SettingValueType::String, PCAT_REMU_KEY_TOPST, "staged", "type=str;rw=1;enum=staged|committed", false, 0U, 0U, false, 0.0f, 0.0f, "staged|committed"},
    {0x0906, PCAT_REMU_SET_TOPP, espnow_link::SettingValueType::String, PCAT_REMU_KEY_TOPP, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0907, PCAT_REMU_SET_TOPN, espnow_link::SettingValueType::String, PCAT_REMU_KEY_TOPN, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0908, PCAT_REMU_SET_TOPA, espnow_link::SettingValueType::String, PCAT_REMU_KEY_TOPA, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
};

constexpr EventDef kEventDefs[] = {
    {0xE501, "relay_triggered"},
    {0xE502, "topology_applied"},
    {0xE503, "source_rejected"},
};

constexpr uint8_t kRemuChildMax = static_cast<uint8_t>(PCAT_REMU_SET_RCNT_MAX);

constexpr ChildTelemetryDef kChildTelemetryDefs[] = {
    {"relay_bitmap", "mask", 0.0f, 1.0f, "type=u8;unit=mask;pull=1;push=1;child=1"},
    {"uptime_ms", "ms", 0.0f, 4294967295.0f, "type=u32;unit=ms;pull=1;push=1;child=1"},
};

constexpr ChildSettingDef kChildSettingDefs[] = {
    {"split_idx", espnow_link::SettingValueType::Int, PCAT_REMU_CKEY_SPLT, "0", "type=u32;rw=1;min=0;max=255;child=1", true, PCAT_RELAY_SET_SPLIT_MIN, PCAT_RELAY_SET_SPLIT_MAX, false, 0.0f, 0.0f, nullptr},
    {"pulse_ms", espnow_link::SettingValueType::Int, PCAT_REMU_CKEY_PULS, "500", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_RELAY_SET_PULSE_MIN, PCAT_RELAY_SET_PULSE_MAX, false, 0.0f, 0.0f, nullptr},
    {"hold_ms", espnow_link::SettingValueType::Int, PCAT_REMU_CKEY_HOLD, "30000", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_RELAY_SET_HOLD_MIN, PCAT_RELAY_SET_HOLD_MAX, false, 0.0f, 0.0f, nullptr},
    {"interlock", espnow_link::SettingValueType::Bool, PCAT_REMU_CKEY_ILOK, "1", "type=bool;rw=1;child=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {"rt_limit_c", espnow_link::SettingValueType::Int, PCAT_REMU_CKEY_RTLM, "80", "type=u32;rw=1;min=0;max=255;child=1", true, PCAT_RELAY_SET_RTLIM_MIN, PCAT_RELAY_SET_RTLIM_MAX, false, 0.0f, 0.0f, nullptr},
    {"sensor_a_mac", espnow_link::SettingValueType::String, PCAT_REMU_CKEY_SAMA, "00:00:00:00:00:00", "type=str;rw=1;child=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {"sensor_b_mac", espnow_link::SettingValueType::String, PCAT_REMU_CKEY_SBMA, "00:00:00:00:00:00", "type=str;rw=1;child=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
};

std::string buildChildScopedKey(uint8_t vid, const char* suffix) {
  return std::string("v") + std::to_string(static_cast<unsigned int>(vid)) + "." + (suffix != nullptr ? suffix : "");
}

std::string buildChildNvsKey(char prefix, uint8_t vid, const char* code4) {
  char out[7] = {0};
  static const char* kHex = "0123456789ABCDEF";
  out[0] = prefix;
  out[1] = kHex[vid & 0x0FU];
  if (code4 != nullptr) {
    std::memcpy(out + 2, code4, 4U);
  }
  out[6] = '\0';
  return std::string(out, 6U);
}

uint16_t buildChildTelemetryId(uint8_t vid, uint8_t field_idx_1based) {
  return static_cast<uint16_t>(0x5000U + (static_cast<uint16_t>(vid) << 4U) + field_idx_1based);
}

uint16_t buildChildSettingId(uint8_t vid, uint8_t field_idx_1based) {
  return static_cast<uint16_t>(0x4000U + (static_cast<uint16_t>(vid) << 5U) + field_idx_1based);
}

bool parseChildScopedKey(const std::string& key,
                         uint8_t max_vid,
                         uint8_t& out_vid,
                         std::string& out_suffix) {
  out_vid = 0U;
  out_suffix.clear();
  if (key.size() < 4U || key[0] != 'v') {
    return false;
  }
  const size_t dot = key.find('.');
  if (dot == std::string::npos || dot <= 1U || dot + 1U >= key.size()) {
    return false;
  }
  const std::string vid_txt = key.substr(1U, dot - 1U);
  char* endp = nullptr;
  const unsigned long parsed = std::strtoul(vid_txt.c_str(), &endp, 10);
  if (endp == nullptr || *endp != '\0' || parsed > static_cast<unsigned long>(max_vid)) {
    return false;
  }
  out_vid = static_cast<uint8_t>(parsed);
  out_suffix = key.substr(dot + 1U);
  return !out_suffix.empty();
}

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

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildRemuTelemetrySpec() {
  static bool built = false;
  static std::vector<std::string> key_store{};
  static std::vector<espnow_link::ProfileTelemetryMetricSpec> cache{};
  if (built) {
    return cache;
  }
  built = true;
  const size_t base_count = sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]);
  const size_t child_count = static_cast<size_t>(kRemuChildMax) * (sizeof(kChildTelemetryDefs) / sizeof(kChildTelemetryDefs[0]));
  key_store.reserve(child_count);
  cache.reserve(base_count + child_count);
  for (const auto& m : kTelemetryDefs) {
    espnow_link::ProfileTelemetryMetricSpec metric{};
    metric.metric_id = m.id;
    metric.key = m.key;
    cache.push_back(metric);
  }
  for (uint8_t vid = 0U; vid < kRemuChildMax; ++vid) {
    for (size_t i = 0; i < (sizeof(kChildTelemetryDefs) / sizeof(kChildTelemetryDefs[0])); ++i) {
      key_store.push_back(buildChildScopedKey(vid, kChildTelemetryDefs[i].suffix));
      espnow_link::ProfileTelemetryMetricSpec metric{};
      metric.metric_id = buildChildTelemetryId(vid, static_cast<uint8_t>(i + 1U));
      metric.key = key_store.back().c_str();
      cache.push_back(metric);
    }
  }
  return cache;
}

std::vector<espnow_link::ProfileSettingSpec> buildRemuSettingSpec() {
  static bool built = false;
  static std::vector<std::string> key_store{};
  static std::vector<espnow_link::ProfileSettingSpec> cache{};
  if (built) {
    return cache;
  }
  built = true;
  const size_t base_count = sizeof(kSettingDefs) / sizeof(kSettingDefs[0]);
  const size_t child_count = static_cast<size_t>(kRemuChildMax) * (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0]));
  key_store.reserve(child_count);
  cache.reserve(base_count + child_count);
  for (const auto& s : kSettingDefs) {
    espnow_link::ProfileSettingSpec setting{};
    setting.setting_id = s.id;
    setting.key = s.key;
    cache.push_back(setting);
  }
  for (uint8_t vid = 0U; vid < kRemuChildMax; ++vid) {
    for (size_t i = 0; i < (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0])); ++i) {
      key_store.push_back(buildChildScopedKey(vid, kChildSettingDefs[i].suffix));
      espnow_link::ProfileSettingSpec setting{};
      setting.setting_id = buildChildSettingId(vid, static_cast<uint8_t>(i + 1U));
      setting.key = key_store.back().c_str();
      cache.push_back(setting);
    }
  }
  return cache;
}

std::vector<espnow_link::ProfileEventSpec> buildRemuEventSpec() {
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

RemuAppDescriptorProvider::RemuAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                     const espnow_link::MacAddress& local_mac,
                                                     espnow_link::ITimeSink* time_sink,
                                                     espnow_link::IStorageExplorerProvider* storage,
                                                     espnow_link::OtaDescriptorAdapter* ota,
                                                     const RemuDescriptorAppConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool RemuAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_REMU_DEV_TYPE : cfg_.device_type;
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
  out.device_name = loadString_(PCAT_REMU_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_REMU_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_REMU_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_REMU_BUILD_ID : cfg_.build_id;
  return true;
}

bool RemuAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pfid", kRemuProfileIdText});
  out.push_back({"pfnm", PCAT_REMU_DEV_TYPE});
  out.push_back({"schrv", kRemuSchemaRev});
  out.push_back({"schsh", kRemuSchemaHash});
  out.push_back({"metid", "1"});
  out.push_back({"setid", "1"});
  out.push_back({"evid", "1"});
  out.push_back({"setmap", PCAT_REMU_SETMAP});
  out.push_back({"metmap", PCAT_REMU_METMAP});
  out.push_back({"evmap", PCAT_REMU_EVMAP});
  out.push_back({"cmdset", "gdesc,gcaps,gtel,pull,glive,gtime,stime,gset,sset,ota,log,sd"});
  out.push_back({"pair", "Secure pair handshake"});
  out.push_back({"l2sink", "L2P v1 virtual trigger sink"});
  out.push_back({"tpush", "Compact-indexed push compatible schema order"});
  out.push_back({"topology", "Topology allowed-source map and commit state"});
  out.push_back({"childset", "Per-child settings key format: v{0..15}.<relay_field>"});
  out.push_back({"childmet", "Per-child telemetry key format: v{0..15}.<relay_metric>"});
  return true;
}

bool RemuAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
  out.clear();
  const size_t base_count = sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]);
  const size_t child_count = static_cast<size_t>(kRemuChildMax) * (sizeof(kChildTelemetryDefs) / sizeof(kChildTelemetryDefs[0]));
  out.reserve(base_count + child_count);
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
  for (uint8_t vid = 0U; vid < kRemuChildMax; ++vid) {
    for (size_t i = 0; i < (sizeof(kChildTelemetryDefs) / sizeof(kChildTelemetryDefs[0])); ++i) {
      const auto& m = kChildTelemetryDefs[i];
      espnow_link::TelemetryDescriptor t{};
      t.metric_id = buildChildTelemetryId(vid, static_cast<uint8_t>(i + 1U));
      t.key = buildChildScopedKey(vid, m.suffix);
      t.unit = m.unit;
      t.min_value = m.min_v;
      t.max_value = m.max_v;
      t.description = m.desc;
      out.push_back(t);
    }
  }
  return true;
}

bool RemuAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();
  return appendTelemetryFromRuntime_(out);
}

bool RemuAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool RemuAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool RemuAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool RemuAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
  out.clear();
  const size_t base_count = sizeof(kSettingDefs) / sizeof(kSettingDefs[0]);
  const size_t child_count = static_cast<size_t>(kRemuChildMax) * (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0]));
  out.reserve(base_count + child_count);
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
  for (uint8_t vid = 0U; vid < kRemuChildMax; ++vid) {
    for (size_t i = 0; i < (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0])); ++i) {
      const auto& s = kChildSettingDefs[i];
      const std::string nvs_key = buildChildNvsKey(PCAT_REMU_CHILD_NVS_PREFIX, vid, s.nvs_code4);
      espnow_link::SettingDescriptor d{};
      d.setting_id = buildChildSettingId(vid, static_cast<uint8_t>(i + 1U));
      d.key = buildChildScopedKey(vid, s.suffix);
      d.value_type = s.type;
      d.writable = true;
      d.nvs_key = nvs_key;
      d.default_value = s.def;
      d.description = s.desc;
      switch (s.type) {
        case espnow_link::SettingValueType::Int:
          d.current_value = std::to_string(static_cast<unsigned long>(
              loadU32_(nvs_key.c_str(), static_cast<uint32_t>(std::strtoul(s.def, nullptr, 10)))));
          break;
        case espnow_link::SettingValueType::Float:
          d.current_value = formatFloat_(loadFloat_(nvs_key.c_str(), std::strtof(s.def, nullptr)));
          break;
        case espnow_link::SettingValueType::Bool:
          d.current_value = loadBool_(nvs_key.c_str(), std::strtoul(s.def, nullptr, 10) != 0U) ? "1" : "0";
          break;
        case espnow_link::SettingValueType::String:
        default:
          d.current_value = loadString_(nvs_key.c_str(), s.def);
          break;
      }
      out.push_back(d);
    }
  }
  return true;
}

bool RemuAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
  std::vector<espnow_link::SettingDescriptor> all;
  if (!getSettings(all)) {
    return false;
  }
  for (const auto& s : all) {
    if (s.key == key) {
      out = s;
      return true;
    }
  }
  return false;
}

bool RemuAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&remuProfileDefinition(), setting_id);
  if (spec == nullptr || spec->key == nullptr || spec->key[0] == '\0') {
    return false;
  }
  if (!getSetting(spec->key, out)) {
    return false;
  }
  if (out.setting_id == 0U) {
    out.setting_id = setting_id;
  }
  if (out.key.empty()) {
    out.key = spec->key;
  }
  return true;
}

bool RemuAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
  const SettingDef* def = nullptr;
  for (const auto& s : kSettingDefs) {
    if (key == s.key) {
      def = &s;
      break;
    }
  }
  if (def == nullptr) {
    uint8_t child_vid = 0U;
    std::string child_suffix{};
    if (!parseChildScopedKey(key, static_cast<uint8_t>(kRemuChildMax - 1U), child_vid, child_suffix)) {
      out_message = "unknown setting";
      return false;
    }
    const ChildSettingDef* child_def = nullptr;
    for (size_t i = 0; i < (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0])); ++i) {
      if (child_suffix == kChildSettingDefs[i].suffix) {
        child_def = &kChildSettingDefs[i];
        break;
      }
    }
    if (child_def == nullptr) {
      out_message = "unknown child setting";
      return false;
    }
    const std::string nvs_key = buildChildNvsKey(PCAT_REMU_CHILD_NVS_PREFIX, child_vid, child_def->nvs_code4);
    const std::string setting_name = buildChildScopedKey(child_vid, child_def->suffix);
    switch (child_def->type) {
      case espnow_link::SettingValueType::String: {
        if (!enumContains(child_def->enum_values, value)) {
          out_message = setting_name + " enum mismatch";
          return false;
        }
        const bool ok = nvs_.putString(nvs_key.c_str(), value);
        out_message = ok ? setting_name + " updated" : setting_name + " persist failed";
        return finalizeSettingChange_(setting_name, value, ok, out_message);
      }
      case espnow_link::SettingValueType::Bool: {
        bool parsed = false;
        if (!parseBool(value, parsed)) {
          out_message = setting_name + " expects bool";
          return false;
        }
        const bool ok = nvs_.putBool(nvs_key.c_str(), parsed);
        out_message = ok ? setting_name + " updated" : setting_name + " persist failed";
        return finalizeSettingChange_(setting_name, value, ok, out_message);
      }
      case espnow_link::SettingValueType::Int: {
        uint32_t parsed = 0U;
        if (!parseU32(value, parsed)) {
          out_message = setting_name + " expects integer";
          return false;
        }
        if (child_def->has_int_range && (parsed < child_def->int_min || parsed > child_def->int_max)) {
          out_message = setting_name + " out of range";
          return false;
        }
        const bool ok = nvs_.putU32(nvs_key.c_str(), parsed);
        out_message = ok ? setting_name + " updated" : setting_name + " persist failed";
        return finalizeSettingChange_(setting_name, value, ok, out_message);
      }
      case espnow_link::SettingValueType::Float: {
        float parsed = 0.0f;
        if (!parseFloat(value, parsed)) {
          out_message = setting_name + " expects float";
          return false;
        }
        if (child_def->has_float_range && (parsed < child_def->float_min || parsed > child_def->float_max)) {
          out_message = setting_name + " out of range";
          return false;
        }
        const bool ok = nvs_.putFloat(nvs_key.c_str(), parsed);
        out_message = ok ? setting_name + " updated" : setting_name + " persist failed";
        return finalizeSettingChange_(setting_name, value, ok, out_message);
      }
      default:
        out_message = "unsupported child setting type";
        return false;
    }
  }

  switch (def->type) {
    case espnow_link::SettingValueType::String: {
      if (key == PCAT_REMU_SET_DNAME && value.empty()) {
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

bool RemuAppDescriptorProvider::setSettingById(uint16_t setting_id,
                                               const std::string& value,
                                               std::string& out_message) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&remuProfileDefinition(), setting_id);
  if (spec == nullptr || spec->key == nullptr || spec->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(spec->key, value, out_message);
}

bool RemuAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool RemuAppDescriptorProvider::listStoragePath(const std::string& path,
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

bool RemuAppDescriptorProvider::statStoragePath(const std::string& path, espnow_link::StorageStat& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool RemuAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool RemuAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool RemuAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool RemuAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool RemuAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool RemuAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool RemuAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool RemuAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

bool RemuAppDescriptorProvider::finalizeSettingChange_(const std::string& key,
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
    if (!apply_message.empty()) {
      out_message = apply_message;
    }
    if (!applied) {
      if (cfg_.setting_feedback != nullptr) {
        cfg_.setting_feedback(cfg_.runtime_user, key, value, false);
      }
      return false;
    }
  }

  if (cfg_.setting_feedback != nullptr) {
    cfg_.setting_feedback(cfg_.runtime_user, key, value, true);
  }
  return true;
}

bool RemuAppDescriptorProvider::appendTelemetryFromRuntime_(std::vector<espnow_link::TelemetrySample>& out) {
  if (cfg_.read_telemetry == nullptr) {
    return false;
  }

  RemuRuntimeTelemetrySnapshot snapshot{};
  if (!cfg_.read_telemetry(cfg_.runtime_user, snapshot) || !snapshot.valid) {
    return false;
  }

#if defined(ARDUINO)
  const uint32_t now = millis();
#else
  const uint32_t now = 0U;
#endif

  uint32_t relay_count = snapshot.relay_count;
  if (relay_count == 0U) {
    relay_count = loadU32_(PCAT_REMU_KEY_RCNT, PCAT_REMU_SET_RCNT_DEF);
  }
  if (relay_count == 0U) {
    relay_count = 1U;
  }
  if (relay_count > kRemuChildMax) {
    relay_count = kRemuChildMax;
  }

  float env_temp_c = snapshot.env_temp_c;
  if (!std::isfinite(env_temp_c)) {
    env_temp_c = 0.0f;
  }

  out.push_back(makeSample(0x01, PCAT_REMU_MET_BITMAP, std::to_string(static_cast<unsigned long>(snapshot.relay_bitmap)), "mask"));
  out.push_back(makeSample(0x02, PCAT_REMU_MET_RCOUNT, std::to_string(static_cast<unsigned long>(relay_count)), "count"));
  out.push_back(makeSample(0x03, PCAT_REMU_MET_TEMP, formatFloat_(env_temp_c), "C"));

  for (uint8_t vid = 0U; vid < static_cast<uint8_t>(relay_count); ++vid) {
    const uint32_t child_state = ((snapshot.relay_bitmap >> vid) & 0x01U);
    const uint32_t child_uptime = now + static_cast<uint32_t>(vid) * 37U;
    out.push_back(makeSample(buildChildTelemetryId(vid, 1U), buildChildScopedKey(vid, "relay_bitmap").c_str(),
                             std::to_string(static_cast<unsigned long>(child_state)), "mask"));
    out.push_back(makeSample(buildChildTelemetryId(vid, 2U), buildChildScopedKey(vid, "uptime_ms").c_str(),
                             std::to_string(static_cast<unsigned long>(child_uptime)), "ms"));
  }
  return true;
}

uint32_t RemuAppDescriptorProvider::loadU32_(const char* key, uint32_t fallback) const {
  uint32_t out = fallback;
  (void)nvs_.getU32(key, out);
  return out;
}

bool RemuAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float RemuAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string RemuAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

std::string RemuAppDescriptorProvider::formatFloat_(float value) const {
  char buf[24] = {0};
  std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
  return std::string(buf);
}

espnow_link::ProfileId RemuAppProfileDefinition::profileId() const {
  return kAppProfileRemu;
}

const char* RemuAppProfileDefinition::profileName() const {
  return PCAT_REMU_DEV_TYPE;
}

espnow_link::CodecId RemuAppProfileDefinition::defaultCodecId() const {
  return espnow_link::kCodecIdCompactIndexed;
}

bool RemuAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const {
  return espnow_link::isBuiltInCodecId(codec_id);
}

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& RemuAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildRemuTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& RemuAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildRemuSettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& RemuAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildRemuEventSpec();
  return kEvents;
}

const RemuAppProfileDefinition& remuProfileDefinition() {
  static const RemuAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makeRemuSlaveSchemaPackage(RemuAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &remuProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfileRemu;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
