#include "profile_catalog/slaves/sens/sens_profile.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cctype>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {

namespace {

constexpr const char* kSensProfileIdText = "3";
constexpr const char* kSensSchemaRev = "1";
constexpr const char* kSensSchemaHash = "sns001";
constexpr uint32_t kLidarProvisionAddrMin = 0x08U;
constexpr uint32_t kLidarProvisionAddrMax = 0x77U;

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
    {0x01, PCAT_SENS_MET_TFLA, "mm", -32768.0f, 32767.0f, "type=i16;unit=mm;pull=1;push=1"},
    {0x02, PCAT_SENS_MET_TFLB, "mm", -32768.0f, 32767.0f, "type=i16;unit=mm;pull=1;push=1"},
    {0x03, PCAT_SENS_MET_TEMP, "C", -40.0f, 125.0f, "type=f32;unit=C;pull=1;push=1"},
    {0x04, PCAT_SENS_MET_HUM, "%", 0.0f, 100.0f, "type=f32;unit=%;pull=1;push=1"},
    {0x05, PCAT_SENS_MET_PRES, "Pa", 30000.0f, 120000.0f, "type=f32;unit=Pa;pull=1;push=1"},
    {0x06, PCAT_SENS_MET_LUX, "lux", 0.0f, 4294967295.0f, "type=u32;unit=lux;pull=1;push=1"},
};

constexpr SettingDef kSettingDefs[] = {
    {0x0001, PCAT_SENS_SET_DNAME, espnow_link::SettingValueType::String, PCAT_SENS_KEY_DNAME, PCAT_SENS_DEF_NAME, "type=str;rw=1;min=1;max=31", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0002, PCAT_SENS_SET_CHAN, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_CHAN, "1", "type=u32;rw=1;min=1;max=14", true, PCAT_SENS_SET_CHAN_MIN, PCAT_SENS_SET_CHAN_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0101, PCAT_SENS_SET_PREV, espnow_link::SettingValueType::String, PCAT_SENS_KEY_PREV, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0102, PCAT_SENS_SET_NEXT, espnow_link::SettingValueType::String, PCAT_SENS_KEY_NEXT, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0103, PCAT_SENS_SET_POSR, espnow_link::SettingValueType::String, PCAT_SENS_KEY_POSR, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0104, PCAT_SENS_SET_NEGR, espnow_link::SettingValueType::String, PCAT_SENS_KEY_NEGR, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0201, PCAT_SENS_SET_TFNR, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_TFNR, "200", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_TFNR_MIN, PCAT_SENS_SET_TFNR_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0202, PCAT_SENS_SET_TFFR, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_TFFR, "3200", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_TFFR_MIN, PCAT_SENS_SET_TFFR_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0203, PCAT_SENS_SET_ABSP, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_ABSP, "350", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_ABSP_MIN, PCAT_SENS_SET_ABSP_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0204, PCAT_SENS_SET_ALS0, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_ALS0, "180", "type=u32;rw=1;min=1;max=65535", true, PCAT_SENS_SET_ALS0_MIN, PCAT_SENS_SET_ALS0_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0205, PCAT_SENS_SET_ALS1, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_ALS1, "300", "type=u32;rw=1;min=1;max=65535", true, PCAT_SENS_SET_ALS1_MIN, PCAT_SENS_SET_ALS1_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0210, PCAT_SENS_SET_CFM, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_CFM, "140", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_CFM_MIN, PCAT_SENS_SET_CFM_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0211, PCAT_SENS_SET_STP, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_STP, "1200", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_STP_MIN, PCAT_SENS_SET_STP_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0212, PCAT_SENS_SET_RON, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_RON, "600", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_RON_MIN, PCAT_SENS_SET_RON_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0213, PCAT_SENS_SET_ROF, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_ROF, "0", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_ROF_MIN, PCAT_SENS_SET_ROF_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0214, PCAT_SENS_SET_LCNT, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_LCNT, "3", "type=u32;rw=1;min=0;max=255", true, PCAT_SENS_SET_LCNT_MIN, PCAT_SENS_SET_LCNT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0215, PCAT_SENS_SET_LSTP, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_LSTP, "250", "type=u32;rw=1;min=0;max=65535", true, PCAT_SENS_SET_LSTP_MIN, PCAT_SENS_SET_LSTP_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0220, PCAT_SENS_SET_TFAA, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_TFAA, "16", "type=u32;rw=1;min=0;max=255", true, PCAT_SENS_SET_TFAA_MIN, PCAT_SENS_SET_TFAA_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0221, PCAT_SENS_SET_TFBA, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_TFBA, "17", "type=u32;rw=1;min=0;max=255", true, PCAT_SENS_SET_TFBA_MIN, PCAT_SENS_SET_TFBA_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0222, PCAT_SENS_SET_TFFP, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_TFFP, "0", "type=u32;rw=1;min=0;max=250", true, PCAT_SENS_SET_TFFP_MIN, PCAT_SENS_SET_TFFP_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0301, PCAT_SENS_SET_LOOPA, espnow_link::SettingValueType::Bool, PCAT_SENS_KEY_LOOPA, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x030A, PCAT_SENS_SET_FANMD, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_FANMD, "0", "type=u32;rw=1;min=0;max=3;enum=0:auto|1:eco|2:forced|3:stopped", true, PCAT_SENS_SET_FANMD_MIN, PCAT_SENS_SET_FANMD_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0308, PCAT_SENS_SET_BUZEN, espnow_link::SettingValueType::Bool, PCAT_SENS_KEY_BUZEN, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0309, PCAT_SENS_SET_LEDFB, espnow_link::SettingValueType::Bool, PCAT_SENS_KEY_LEDFB, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0310, PCAT_SENS_SET_RGBIDL, espnow_link::SettingValueType::String, PCAT_SENS_KEY_RGBIDL, PCAT_SENS_SET_RGBIDL_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0311, PCAT_SENS_SET_RGBALT, espnow_link::SettingValueType::String, PCAT_SENS_KEY_RGBALT, PCAT_SENS_SET_RGBALT_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0312, PCAT_SENS_SET_RGBBRT, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_RGBBRT, "180", "type=u32;rw=1;min=0;max=255", true, PCAT_SENS_SET_RGBBRT_MIN, PCAT_SENS_SET_RGBBRT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0302, PCAT_SENS_SET_PSHEN, espnow_link::SettingValueType::Bool, PCAT_SENS_KEY_PSHEN, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0303, PCAT_SENS_SET_PSHMD, espnow_link::SettingValueType::String, PCAT_SENS_KEY_PSHMD, PCAT_SENS_SET_PSHMD_DEF, "type=str;rw=1;enum=periodic|change|hybrid", false, 0U, 0U, false, 0.0f, 0.0f, "periodic|change|hybrid"},
    {0x0304, PCAT_SENS_SET_PSHI, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_PSHI, "2000", "type=u32;rw=1;min=200;max=60000", true, PCAT_SENS_SET_PSHI_MIN, PCAT_SENS_SET_PSHI_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0305, PCAT_SENS_SET_PSHD, espnow_link::SettingValueType::Float, PCAT_SENS_KEY_PSHD, "0.0", "type=f32;rw=1;min=0.0;max=100000.0", false, 0U, 0U, true, PCAT_SENS_SET_PSHD_MIN, PCAT_SENS_SET_PSHD_MAX, nullptr},
    {0x0306, PCAT_SENS_SET_PSHG, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_PSHG, "200", "type=u32;rw=1;min=50;max=10000", true, PCAT_SENS_SET_PSHG_MIN, PCAT_SENS_SET_PSHG_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0307, PCAT_SENS_SET_PSHS, espnow_link::SettingValueType::String, PCAT_SENS_KEY_PSHS, PCAT_SENS_SET_PSHS_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0901, PCAT_SENS_SET_TOPV, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_TOPV, "0", "type=u32;rw=1;min=0;max=4294967295", true, 0U, 0xFFFFFFFFU, false, 0.0f, 0.0f, nullptr},
    {0x0902, PCAT_SENS_SET_TOPS, espnow_link::SettingValueType::String, PCAT_SENS_KEY_TOPS, "", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0903, PCAT_SENS_SET_TOPST, espnow_link::SettingValueType::String, PCAT_SENS_KEY_TOPST, "staged", "type=str;rw=1;enum=staged|committed", false, 0U, 0U, false, 0.0f, 0.0f, "staged|committed"},
    {0x0904, PCAT_SENS_SET_TOPR, espnow_link::SettingValueType::String, PCAT_SENS_KEY_TOPR, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0905, PCAT_SENS_SET_TOPC, espnow_link::SettingValueType::Int, PCAT_SENS_KEY_TOPC, "0", "type=u32;rw=1;min=0;max=4294967295", true, 0U, 0xFFFFFFFFU, false, 0.0f, 0.0f, nullptr},
    {0x0A01, PCAT_SENS_SET_LPRSENS, espnow_link::SettingValueType::String, PCAT_SENS_KEY_LPRQ, PCAT_SENS_SET_LPRSENS_DEF, "type=cmd;rw=1;schema=slot:A|B,source_addr:u8", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0A02, PCAT_SENS_SET_LPRSTA, espnow_link::SettingValueType::String, PCAT_SENS_KEY_LPRS, PCAT_SENS_SET_LPRSTA_DEF, "type=cmd;rw=1;schema=status", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
};

constexpr EventDef kEventDefs[] = {
    {0xE201, "trigger_sent"},
    {0xE202, "topology_applied"},
    {0xE203, "sensor_fault"},
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

std::string trimAscii(const std::string& in) {
  size_t begin = 0U;
  while (begin < in.size() && std::isspace(static_cast<unsigned char>(in[begin])) != 0) {
    ++begin;
  }
  size_t end = in.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(in[end - 1U])) != 0) {
    --end;
  }
  return in.substr(begin, end - begin);
}

std::string toLowerAscii(const std::string& in) {
  std::string out{};
  out.reserve(in.size());
  for (const char ch : in) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return out;
}

bool extractKvToken(const std::string& input, const char* key, std::string& out_value) {
  out_value.clear();
  if (key == nullptr || *key == '\0') return false;
  const std::string wanted = toLowerAscii(trimAscii(std::string(key)));
  if (wanted.empty()) return false;

  size_t cursor = 0U;
  while (cursor < input.size()) {
    size_t next = input.find_first_of(",;", cursor);
    if (next == std::string::npos) next = input.size();
    const std::string token = trimAscii(input.substr(cursor, next - cursor));
    if (!token.empty()) {
      const size_t eq = token.find('=');
      if (eq != std::string::npos) {
        const std::string lhs = toLowerAscii(trimAscii(token.substr(0U, eq)));
        if (lhs == wanted) {
          out_value = trimAscii(token.substr(eq + 1U));
          return !out_value.empty();
        }
      }
    }
    cursor = (next < input.size()) ? (next + 1U) : next;
  }
  return false;
}

bool parseU32Any(const std::string& value, uint32_t& out) {
  const std::string trimmed = trimAscii(value);
  if (trimmed.empty()) return false;
  char* endp = nullptr;
  const unsigned long parsed = std::strtoul(trimmed.c_str(), &endp, 0);
  if (endp == nullptr || *endp != '\0') return false;
  out = static_cast<uint32_t>(parsed);
  return true;
}

bool parseProvisionSlot(const std::string& token, char& out_slot) {
  const std::string normalized = toLowerAscii(trimAscii(token));
  if (normalized == "a") {
    out_slot = 'A';
    return true;
  }
  if (normalized == "b") {
    out_slot = 'B';
    return true;
  }
  return false;
}

bool normalizeSensProvisionValue(const std::string& value, std::string& out_norm, std::string& out_error) {
  out_norm.clear();
  out_error.clear();

  char slot = '\0';
  std::string slot_token{};
  if (extractKvToken(value, "slot", slot_token)) {
    if (!parseProvisionSlot(slot_token, slot)) {
      out_error = "invalid slot (expected A or B)";
      return false;
    }
  } else if (!parseProvisionSlot(value, slot)) {
    out_error = "missing slot (expected slot=A|B)";
    return false;
  }

  uint32_t source_addr = static_cast<uint32_t>(PCAT_SENS_SET_TFAA_DEF);
  std::string source_token{};
  if (extractKvToken(value, "source_addr", source_token) || extractKvToken(value, "source", source_token)) {
    if (!parseU32Any(source_token, source_addr)) {
      out_error = "invalid source_addr";
      return false;
    }
  }
  if (source_addr < kLidarProvisionAddrMin || source_addr > kLidarProvisionAddrMax) {
    out_error = "source_addr out of range";
    return false;
  }

  out_norm = "slot=";
  out_norm.push_back(slot);
  out_norm += ";source_addr=";
  out_norm += std::to_string(static_cast<unsigned long>(source_addr));
  return true;
}

std::string normalizeProvisionStatusValue(const std::string& value) {
  const std::string trimmed = trimAscii(value);
  return trimmed.empty() ? std::string("status=1") : trimmed;
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

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildSensTelemetrySpec() {
  std::vector<espnow_link::ProfileTelemetryMetricSpec> out;
  out.reserve(sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]));
  for (const auto& m : kTelemetryDefs) {
    espnow_link::ProfileTelemetryMetricSpec metric{};
    metric.metric_id = m.id;
    metric.key = m.key;
    out.push_back(metric);
  }
  return out;
}

std::vector<espnow_link::ProfileSettingSpec> buildSensSettingSpec() {
  std::vector<espnow_link::ProfileSettingSpec> out;
  out.reserve(sizeof(kSettingDefs) / sizeof(kSettingDefs[0]));
  for (const auto& s : kSettingDefs) {
    espnow_link::ProfileSettingSpec setting{};
    setting.setting_id = s.id;
    setting.key = s.key;
    out.push_back(setting);
  }
  return out;
}

std::vector<espnow_link::ProfileEventSpec> buildSensEventSpec() {
  std::vector<espnow_link::ProfileEventSpec> out;
  out.reserve(sizeof(kEventDefs) / sizeof(kEventDefs[0]));
  for (const auto& e : kEventDefs) {
    espnow_link::ProfileEventSpec event{};
    event.event_id = e.id;
    event.key = e.key;
    out.push_back(event);
  }
  return out;
}

}  // namespace

SensAppDescriptorProvider::SensAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                     const espnow_link::MacAddress& local_mac,
                                                     espnow_link::ITimeSink* time_sink,
                                                     espnow_link::IStorageExplorerProvider* storage,
                                                     espnow_link::OtaDescriptorAdapter* ota,
                                                     const SensDescriptorAppConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool SensAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_SENS_DEV_TYPE : cfg_.device_type;
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
  out.device_name = loadString_(PCAT_SENS_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_SENS_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_SENS_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_SENS_BUILD_ID : cfg_.build_id;
  return true;
}

bool SensAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pfid", kSensProfileIdText});
  out.push_back({"pfnm", PCAT_SENS_DEV_TYPE});
  out.push_back({"schrv", kSensSchemaRev});
  out.push_back({"schsh", kSensSchemaHash});
  out.push_back({"metid", "1"});
  out.push_back({"setid", "1"});
  out.push_back({"evid", "1"});
  out.push_back({"setmap", PCAT_SENS_SETMAP});
  out.push_back({"metmap", PCAT_SENS_METMAP});
  out.push_back({"evmap", PCAT_SENS_EVMAP});
  out.push_back({"cmdset", "gdesc,gcaps,gtel,pull,glive,gtime,stime,gset,sset,ota,log,sd"});
  out.push_back({"pair", "Secure pair handshake"});
  out.push_back({"l2src", "L2P v1 trigger source (index-based routing)"});
  out.push_back({"tpush", "Compact-indexed push compatible schema order"});
  out.push_back({"topology", "Topology relay-target map and commit state"});
  out.push_back({"lprov", "Lidar provisioning commands via sset: lidar.provision.sens, lidar.provision.status"});
  return true;
}

bool SensAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
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

bool SensAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();
  return appendTelemetryFromRuntime_(out);
}

bool SensAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool SensAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool SensAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool SensAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
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

bool SensAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
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

bool SensAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&sensProfileDefinition(), setting_id);
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

bool SensAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
  if (key == PCAT_SENS_SET_LPRSENS) {
    std::string normalized{};
    if (!normalizeSensProvisionValue(value, normalized, out_message)) {
      return false;
    }
    return finalizeSettingChange_(key, normalized, true, out_message);
  }
  if (key == PCAT_SENS_SET_LPRSTA) {
    const std::string normalized = normalizeProvisionStatusValue(value);
    return finalizeSettingChange_(key, normalized, true, out_message);
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
      if (key == PCAT_SENS_SET_DNAME && value.empty()) {
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

bool SensAppDescriptorProvider::setSettingById(uint16_t setting_id,
                                               const std::string& value,
                                               std::string& out_message) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&sensProfileDefinition(), setting_id);
  if (spec == nullptr || spec->key == nullptr || spec->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(spec->key, value, out_message);
}

bool SensAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool SensAppDescriptorProvider::listStoragePath(const std::string& path,
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

bool SensAppDescriptorProvider::statStoragePath(const std::string& path, espnow_link::StorageStat& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool SensAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool SensAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool SensAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool SensAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool SensAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool SensAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool SensAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool SensAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

uint32_t SensAppDescriptorProvider::loadU32_(const char* key, uint32_t fallback) const {
  uint32_t out = fallback;
  (void)nvs_.getU32(key, out);
  return out;
}

bool SensAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float SensAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string SensAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

std::string SensAppDescriptorProvider::formatFloat_(float value) const {
  char buf[24] = {0};
  std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
  return std::string(buf);
}

bool SensAppDescriptorProvider::finalizeSettingChange_(const std::string& key,
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
      out_message = apply_message.empty() ? (key + " apply failed") : apply_message;
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
  return true;
}

bool SensAppDescriptorProvider::appendTelemetryFromRuntime_(std::vector<espnow_link::TelemetrySample>& out) {
  if (cfg_.read_telemetry == nullptr) {
    return false;
  }

  SensRuntimeTelemetrySnapshot snap{};
  if (!cfg_.read_telemetry(cfg_.runtime_user, snap) || !snap.valid) {
    return false;
  }

  const int32_t tf_a = (snap.tfl_a_mm < -32768) ? -32768 : ((snap.tfl_a_mm > 32767) ? 32767 : snap.tfl_a_mm);
  const int32_t tf_b = (snap.tfl_b_mm < -32768) ? -32768 : ((snap.tfl_b_mm > 32767) ? 32767 : snap.tfl_b_mm);
  const float temp = std::isfinite(snap.env_temp_c) ? snap.env_temp_c : 0.0f;
  const float hum = std::isfinite(snap.env_hum_pct) ? snap.env_hum_pct : 0.0f;
  const float press = std::isfinite(snap.env_press_pa) ? snap.env_press_pa : 0.0f;
  const float luxf = std::isfinite(snap.lux) ? snap.lux : 0.0f;
  const uint32_t lux_u32 = (luxf <= 0.0f) ? 0U : static_cast<uint32_t>(luxf);

  out.push_back(makeSample(0x01, PCAT_SENS_MET_TFLA, std::to_string(tf_a), "mm"));
  out.push_back(makeSample(0x02, PCAT_SENS_MET_TFLB, std::to_string(tf_b), "mm"));
  out.push_back(makeSample(0x03, PCAT_SENS_MET_TEMP, formatFloat_(temp), "C"));
  out.push_back(makeSample(0x04, PCAT_SENS_MET_HUM, formatFloat_(hum), "%"));
  out.push_back(makeSample(0x05, PCAT_SENS_MET_PRES, formatFloat_(press), "Pa"));
  out.push_back(makeSample(0x06, PCAT_SENS_MET_LUX, std::to_string(static_cast<unsigned long>(lux_u32)), "lux"));
  return true;
}

espnow_link::ProfileId SensAppProfileDefinition::profileId() const {
  return kAppProfileSens;
}

const char* SensAppProfileDefinition::profileName() const {
  return PCAT_SENS_DEV_TYPE;
}

espnow_link::CodecId SensAppProfileDefinition::defaultCodecId() const {
  return espnow_link::kCodecIdCompactIndexed;
}

bool SensAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const {
  return espnow_link::isBuiltInCodecId(codec_id);
}

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& SensAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildSensTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& SensAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildSensSettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& SensAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildSensEventSpec();
  return kEvents;
}

const SensAppProfileDefinition& sensProfileDefinition() {
  static const SensAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makeSensSlaveSchemaPackage(SensAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &sensProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfileSens;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
