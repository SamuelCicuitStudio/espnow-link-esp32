#include "profile_catalog/slaves/semu/semu_profile.hpp"
#include "profile_catalog/slaves/sens/sens_keys.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {

namespace {

constexpr const char* kSemuProfileIdText = "4";
constexpr const char* kSemuSchemaRev = "1";
constexpr const char* kSemuSchemaHash = "sem001";

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

constexpr uint8_t kSemuChildMax = static_cast<uint8_t>(PCAT_SEMU_SET_SCNT_MAX);

constexpr ChildTelemetryDef kChildTelemetryDefs[] = {
    {"tfl_a_mm", "mm", -32768.0f, 32767.0f, "type=i16;unit=mm;pull=1;push=1;child=1"},
    {"tfl_b_mm", "mm", -32768.0f, 32767.0f, "type=i16;unit=mm;pull=1;push=1;child=1"},
    {"tfl_a_flux", "count", 0.0f, 65535.0f, "type=u16;unit=count;pull=1;push=1;child=1"},
    {"tfl_b_flux", "count", 0.0f, 65535.0f, "type=u16;unit=count;pull=1;push=1;child=1"},
    {"tfl_a_temp_c", "C", -40.0f, 125.0f, "type=f32;unit=C;pull=1;push=1;child=1"},
    {"tfl_b_temp_c", "C", -40.0f, 125.0f, "type=f32;unit=C;pull=1;push=1;child=1"},
};

constexpr ChildSettingDef kChildSettingDefs[] = {
    {"prev_mac", espnow_link::SettingValueType::String, PCAT_SEMU_CKEY_PREV, "00:00:00:00:00:00", "type=str;rw=1;child=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {"next_mac", espnow_link::SettingValueType::String, PCAT_SEMU_CKEY_NEXT, "00:00:00:00:00:00", "type=str;rw=1;child=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {"pos_relays", espnow_link::SettingValueType::String, PCAT_SEMU_CKEY_POSR, "[]", "type=str;rw=1;child=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {"neg_relays", espnow_link::SettingValueType::String, PCAT_SEMU_CKEY_NEGR, "[]", "type=str;rw=1;child=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {"detect_fall_delta_cm", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_TFNR, "200", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SENS_SET_TFNR_MIN, PCAT_SENS_SET_TFNR_MAX, false, 0.0f, 0.0f, nullptr},
    {"detect_release_delta_cm", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_TFFR, "3200", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SENS_SET_TFFR_MIN, PCAT_SENS_SET_TFFR_MAX, false, 0.0f, 0.0f, nullptr},
    {"ab_spacing_cm", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_ABSP, "5", "type=u32;rw=1;min=0;max=6553;child=1", true, PCAT_SENS_SET_ABSP_CM_MIN, PCAT_SENS_SET_ABSP_CM_MAX, false, 0.0f, 0.0f, nullptr},
    {"tfl_a_calib_mm", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_CALA, "0", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SEMU_SET_CALA_MIN, PCAT_SEMU_SET_CALA_MAX, false, 0.0f, 0.0f, nullptr},
    {"tfl_b_calib_mm", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_CALB, "0", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SEMU_SET_CALB_MIN, PCAT_SEMU_SET_CALB_MAX, false, 0.0f, 0.0f, nullptr},
    {"als_t0_lux", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_ALS0, "180", "type=u32;rw=1;min=1;max=65535;child=1", true, PCAT_SENS_SET_ALS0_MIN, PCAT_SENS_SET_ALS0_MAX, false, 0.0f, 0.0f, nullptr},
    {"als_t1_lux", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_ALS1, "300", "type=u32;rw=1;min=1;max=65535;child=1", true, PCAT_SENS_SET_ALS1_MIN, PCAT_SENS_SET_ALS1_MAX, false, 0.0f, 0.0f, nullptr},
    {"detect_window_ms", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_CFMS, "140", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SENS_SET_CFM_MIN, PCAT_SENS_SET_CFM_MAX, false, 0.0f, 0.0f, nullptr},
    {"detect_clear_hold_ms", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_STMS, "1200", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SENS_SET_STP_MIN, PCAT_SENS_SET_STP_MAX, false, 0.0f, 0.0f, nullptr},
    {"relay_on_ms", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_RONM, "600", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SENS_SET_RON_MIN, PCAT_SENS_SET_RON_MAX, false, 0.0f, 0.0f, nullptr},
    {"relay_off_ms", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_ROFM, "0", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SENS_SET_ROF_MIN, PCAT_SENS_SET_ROF_MAX, false, 0.0f, 0.0f, nullptr},
    {"lead_count", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_LCNT, "3", "type=u32;rw=1;min=0;max=255;child=1", true, PCAT_SENS_SET_LCNT_MIN, PCAT_SENS_SET_LCNT_MAX, false, 0.0f, 0.0f, nullptr},
    {"lead_step_ms", espnow_link::SettingValueType::Int, PCAT_SEMU_CKEY_LSTM, "250", "type=u32;rw=1;min=0;max=65535;child=1", true, PCAT_SENS_SET_LSTP_MIN, PCAT_SENS_SET_LSTP_MAX, false, 0.0f, 0.0f, nullptr},
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

constexpr TelemetryDef kTelemetryDefs[] = {
    {0x01, PCAT_SEMU_MET_TEMP, "C", -40.0f, 125.0f, "type=f32;unit=C;pull=1;push=1"},
    {0x02, PCAT_SEMU_MET_HUM, "%", 0.0f, 100.0f, "type=f32;unit=%;pull=1;push=1"},
    {0x03, PCAT_SEMU_MET_PRES, "Pa", 30000.0f, 120000.0f, "type=f32;unit=Pa;pull=1;push=1"},
    {0x04, PCAT_SEMU_MET_LUX, "lux", 0.0f, 4294967295.0f, "type=u32;unit=lux;pull=1;push=1"},
};

constexpr SettingDef kSettingDefs[] = {
    {0x0001, PCAT_SEMU_SET_DNAME, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_DNAME, PCAT_SEMU_DEF_NAME, "type=str;rw=1;min=1;max=31", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0002, PCAT_SEMU_SET_CHAN, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_CHAN, "1", "type=u32;rw=1;min=1;max=14", true, PCAT_SEMU_SET_CHAN_MIN, PCAT_SEMU_SET_CHAN_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0101, PCAT_SEMU_SET_SCNT, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_SCNT, "8", "type=u32;rw=1;min=1;max=8", true, PCAT_SEMU_SET_SCNT_MIN, PCAT_SEMU_SET_SCNT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0102, PCAT_SEMU_SET_PREV, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_PREV, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0103, PCAT_SEMU_SET_NEXT, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_NEXT, "00:00:00:00:00:00", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0104, PCAT_SEMU_SET_POSR, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_POSR, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0105, PCAT_SEMU_SET_NEGR, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_NEGR, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0110, PCAT_SEMU_SET_VON, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_VON, "600", "type=u32;rw=1;min=0;max=65535", true, PCAT_SEMU_SET_VON_MIN, PCAT_SEMU_SET_VON_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0111, PCAT_SEMU_SET_VLCNT, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_VLCNT, "3", "type=u32;rw=1;min=0;max=255", true, PCAT_SEMU_SET_VLCNT_MIN, PCAT_SEMU_SET_VLCNT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0112, PCAT_SEMU_SET_VLMS, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_VLMS, "250", "type=u32;rw=1;min=0;max=65535", true, PCAT_SEMU_SET_VLMS_MIN, PCAT_SEMU_SET_VLMS_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0113, PCAT_SEMU_SET_VENV, espnow_link::SettingValueType::Bool, PCAT_SEMU_KEY_VENV, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0114, PCAT_SEMU_SET_SLPMS, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_SLPMS, "50", "type=u32;rw=1;min=10;max=1000", true, PCAT_SEMU_SET_SLMS_MIN, PCAT_SEMU_SET_SLMS_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0115, PCAT_SEMU_SET_RINGN, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_RINGN, "64", "type=u32;rw=1;min=1;max=256", true, PCAT_SEMU_SET_RNGN_MIN, PCAT_SEMU_SET_RNGN_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0120, PCAT_SEMU_SET_ALS0, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_ALS0, "180", "type=u32;rw=1;min=1;max=65535", true, PCAT_SEMU_SET_ALS0_MIN, PCAT_SEMU_SET_ALS0_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0121, PCAT_SEMU_SET_ALS1, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_ALS1, "300", "type=u32;rw=1;min=1;max=65535", true, PCAT_SEMU_SET_ALS1_MIN, PCAT_SEMU_SET_ALS1_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0301, PCAT_SEMU_SET_LOOPA, espnow_link::SettingValueType::Bool, PCAT_SEMU_KEY_LOOPA, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x030A, PCAT_SEMU_SET_FANMD, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_FANMD, "0", "type=u32;rw=1;min=0;max=3;enum=0:auto|1:eco|2:forced|3:stopped", true, PCAT_SEMU_SET_FANMD_MIN, PCAT_SEMU_SET_FANMD_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0308, PCAT_SEMU_SET_BUZEN, espnow_link::SettingValueType::Bool, PCAT_SEMU_KEY_BUZEN, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0309, PCAT_SEMU_SET_LEDFB, espnow_link::SettingValueType::Bool, PCAT_SEMU_KEY_LEDFB, "1", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0310, PCAT_SEMU_SET_RGBIDL, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_RGBIDL, PCAT_SEMU_SET_RGBIDL_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0311, PCAT_SEMU_SET_RGBALT, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_RGBALT, PCAT_SEMU_SET_RGBALT_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0312, PCAT_SEMU_SET_RGBBRT, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_RGBBRT, "180", "type=u32;rw=1;min=0;max=255", true, PCAT_SEMU_SET_RGBBRT_MIN, PCAT_SEMU_SET_RGBBRT_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0302, PCAT_SEMU_SET_PSHEN, espnow_link::SettingValueType::Bool, PCAT_SEMU_KEY_PSHEN, "0", "type=bool;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0303, PCAT_SEMU_SET_PSHMD, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_PSHMD, PCAT_SEMU_SET_PSHMD_DEF, "type=str;rw=1;enum=periodic|change|hybrid", false, 0U, 0U, false, 0.0f, 0.0f, "periodic|change|hybrid"},
    {0x0304, PCAT_SEMU_SET_PSHI, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_PSHI, "2000", "type=u32;rw=1;min=200;max=60000", true, PCAT_SEMU_SET_PSHI_MIN, PCAT_SEMU_SET_PSHI_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0305, PCAT_SEMU_SET_PSHD, espnow_link::SettingValueType::Float, PCAT_SEMU_KEY_PSHD, "0.0", "type=f32;rw=1;min=0.0;max=100000.0", false, 0U, 0U, true, PCAT_SEMU_SET_PSHD_MIN, PCAT_SEMU_SET_PSHD_MAX, nullptr},
    {0x0306, PCAT_SEMU_SET_PSHG, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_PSHG, "200", "type=u32;rw=1;min=50;max=10000", true, PCAT_SEMU_SET_PSHG_MIN, PCAT_SEMU_SET_PSHG_MAX, false, 0.0f, 0.0f, nullptr},
    {0x0307, PCAT_SEMU_SET_PSHS, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_PSHS, PCAT_SEMU_SET_PSHS_DEF, "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0901, PCAT_SEMU_SET_TOPV, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_TOPV, "0", "type=u32;rw=1;min=0;max=4294967295", true, 0U, 0xFFFFFFFFU, false, 0.0f, 0.0f, nullptr},
    {0x0902, PCAT_SEMU_SET_TOPS, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_TOPS, "", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0903, PCAT_SEMU_SET_TOPST, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_TOPST, "staged", "type=str;rw=1;enum=staged|committed", false, 0U, 0U, false, 0.0f, 0.0f, "staged|committed"},
    {0x0904, PCAT_SEMU_SET_TOPR, espnow_link::SettingValueType::String, PCAT_SEMU_KEY_TOPR, "[]", "type=str;rw=1", false, 0U, 0U, false, 0.0f, 0.0f, nullptr},
    {0x0905, PCAT_SEMU_SET_TOPC, espnow_link::SettingValueType::Int, PCAT_SEMU_KEY_TOPC, "0", "type=u32;rw=1;min=0;max=4294967295", true, 0U, 0xFFFFFFFFU, false, 0.0f, 0.0f, nullptr},
};

constexpr EventDef kEventDefs[] = {
    {0xE401, "trigger_sent"},
    {0xE402, "topology_applied"},
    {0xE403, "virtual_sensor_fault"},
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

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildSemuTelemetrySpec() {
  static bool built = false;
  static std::vector<std::string> key_store{};
  static std::vector<espnow_link::ProfileTelemetryMetricSpec> cache{};
  if (built) {
    return cache;
  }
  built = true;
  const size_t base_count = sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]);
  const size_t child_count = static_cast<size_t>(kSemuChildMax) * (sizeof(kChildTelemetryDefs) / sizeof(kChildTelemetryDefs[0]));
  key_store.reserve(child_count);
  cache.reserve(base_count + child_count);
  for (const auto& m : kTelemetryDefs) {
    espnow_link::ProfileTelemetryMetricSpec metric{};
    metric.metric_id = m.id;
    metric.key = m.key;
    cache.push_back(metric);
  }
  for (uint8_t vid = 0U; vid < kSemuChildMax; ++vid) {
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

std::vector<espnow_link::ProfileSettingSpec> buildSemuSettingSpec() {
  static bool built = false;
  static std::vector<std::string> key_store{};
  static std::vector<espnow_link::ProfileSettingSpec> cache{};
  if (built) {
    return cache;
  }
  built = true;
  const size_t base_count = sizeof(kSettingDefs) / sizeof(kSettingDefs[0]);
  const size_t child_count = static_cast<size_t>(kSemuChildMax) * (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0]));
  key_store.reserve(child_count);
  cache.reserve(base_count + child_count);
  for (const auto& s : kSettingDefs) {
    espnow_link::ProfileSettingSpec setting{};
    setting.setting_id = s.id;
    setting.key = s.key;
    cache.push_back(setting);
  }
  for (uint8_t vid = 0U; vid < kSemuChildMax; ++vid) {
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

std::vector<espnow_link::ProfileEventSpec> buildSemuEventSpec() {
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

SemuAppDescriptorProvider::SemuAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                     const espnow_link::MacAddress& local_mac,
                                                     espnow_link::ITimeSink* time_sink,
                                                     espnow_link::IStorageExplorerProvider* storage,
                                                     espnow_link::OtaDescriptorAdapter* ota,
                                                     const SemuDescriptorAppConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool SemuAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_SEMU_DEV_TYPE : cfg_.device_type;
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
  out.device_name = loadString_(PCAT_SEMU_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_SEMU_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_SEMU_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_SEMU_BUILD_ID : cfg_.build_id;
  return true;
}

bool SemuAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pfid", kSemuProfileIdText});
  out.push_back({"pfnm", PCAT_SEMU_DEV_TYPE});
  out.push_back({"schrv", kSemuSchemaRev});
  out.push_back({"schsh", kSemuSchemaHash});
  out.push_back({"metid", "1"});
  out.push_back({"setid", "1"});
  out.push_back({"evid", "1"});
  out.push_back({"setmap", PCAT_SEMU_SETMAP});
  out.push_back({"metmap", PCAT_SEMU_METMAP});
  out.push_back({"evmap", PCAT_SEMU_EVMAP});
  out.push_back({"cmdset", "gdesc,gcaps,gtel,pull,glive,gtime,stime,gset,sset,ota,log,sd"});
  out.push_back({"pair", "Secure pair handshake"});
  out.push_back({"l2src", "L2P v1 virtual trigger source"});
  out.push_back({"tpush", "Compact-indexed push compatible schema order"});
  out.push_back({"topology", "Topology relay-target map and commit state"});
  out.push_back({"childset", "Per-child settings key format: v{0..7}.<sens_field>"});
  out.push_back({"childmet", "Per-child telemetry key format: v{0..7}.<sens_metric>"});
  return true;
}

bool SemuAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
  out.clear();
  const size_t base_count = sizeof(kTelemetryDefs) / sizeof(kTelemetryDefs[0]);
  const size_t child_count = static_cast<size_t>(kSemuChildMax) * (sizeof(kChildTelemetryDefs) / sizeof(kChildTelemetryDefs[0]));
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
  for (uint8_t vid = 0U; vid < kSemuChildMax; ++vid) {
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

bool SemuAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();
  return appendTelemetryFromRuntime_(out);
}

bool SemuAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool SemuAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool SemuAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool SemuAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
  out.clear();
  const size_t base_count = sizeof(kSettingDefs) / sizeof(kSettingDefs[0]);
  const size_t child_count = static_cast<size_t>(kSemuChildMax) * (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0]));
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

  for (uint8_t vid = 0U; vid < kSemuChildMax; ++vid) {
    for (size_t i = 0; i < (sizeof(kChildSettingDefs) / sizeof(kChildSettingDefs[0])); ++i) {
      const auto& s = kChildSettingDefs[i];
      const std::string nvs_key = buildChildNvsKey(PCAT_SEMU_CHILD_NVS_PREFIX, vid, s.nvs_code4);
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
          if (std::strcmp(s.suffix, "ab_spacing_cm") == 0) {
            const uint32_t raw_mm = loadU32_(nvs_key.c_str(), static_cast<uint32_t>(PCAT_SEMU_SET_ABSP_DEF));
            d.current_value = std::to_string(static_cast<unsigned long>(raw_mm / 10U));
          } else {
            d.current_value = std::to_string(static_cast<unsigned long>(loadU32_(nvs_key.c_str(), static_cast<uint32_t>(std::strtoul(s.def, nullptr, 10)))));
          }
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

bool SemuAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
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

bool SemuAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&semuProfileDefinition(), setting_id);
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

bool SemuAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
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
    if (!parseChildScopedKey(key, static_cast<uint8_t>(kSemuChildMax - 1U), child_vid, child_suffix)) {
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

    const std::string nvs_key = buildChildNvsKey(PCAT_SEMU_CHILD_NVS_PREFIX, child_vid, child_def->nvs_code4);
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
        if (child_suffix == "ab_spacing_cm") {
          if (parsed < PCAT_SENS_SET_ABSP_CM_MIN || parsed > PCAT_SENS_SET_ABSP_CM_MAX) {
            out_message = setting_name + " out of range";
            return false;
          }
          const uint32_t mm_value = parsed * 10U;
          const bool ok = nvs_.putU32(nvs_key.c_str(), mm_value);
          out_message = ok ? setting_name + " updated" : setting_name + " persist failed";
          return finalizeSettingChange_(setting_name, value, ok, out_message);
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
      if (key == PCAT_SEMU_SET_DNAME && value.empty()) {
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

bool SemuAppDescriptorProvider::setSettingById(uint16_t setting_id,
                                               const std::string& value,
                                               std::string& out_message) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&semuProfileDefinition(), setting_id);
  if (spec == nullptr || spec->key == nullptr || spec->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(spec->key, value, out_message);
}

bool SemuAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool SemuAppDescriptorProvider::listStoragePath(const std::string& path,
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

bool SemuAppDescriptorProvider::statStoragePath(const std::string& path, espnow_link::StorageStat& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool SemuAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool SemuAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool SemuAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool SemuAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool SemuAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool SemuAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool SemuAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool SemuAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

uint32_t SemuAppDescriptorProvider::loadU32_(const char* key, uint32_t fallback) const {
  uint32_t out = fallback;
  (void)nvs_.getU32(key, out);
  return out;
}

bool SemuAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float SemuAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string SemuAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

std::string SemuAppDescriptorProvider::formatFloat_(float value) const {
  char buf[24] = {0};
  std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
  return std::string(buf);
}

bool SemuAppDescriptorProvider::finalizeSettingChange_(const std::string& key,
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
  return true;
}

bool SemuAppDescriptorProvider::appendTelemetryFromRuntime_(std::vector<espnow_link::TelemetrySample>& out) {
  if (cfg_.read_telemetry == nullptr) {
    return false;
  }

  SemuRuntimeTelemetrySnapshot snap{};
  if (!cfg_.read_telemetry(cfg_.runtime_user, snap) || !snap.valid) {
    return false;
  }

  const float temp = std::isfinite(snap.env_temp_c) ? snap.env_temp_c : 0.0f;
  const float hum = std::isfinite(snap.env_hum_pct) ? snap.env_hum_pct : 0.0f;
  const float press = std::isfinite(snap.env_press_pa) ? snap.env_press_pa : 0.0f;
  const float luxf = std::isfinite(snap.lux) ? snap.lux : 0.0f;
  const uint32_t lux_u32 = (luxf <= 0.0f) ? 0U : static_cast<uint32_t>(luxf);

  out.push_back(makeSample(0x01, PCAT_SEMU_MET_TEMP, formatFloat_(temp), "C"));
  out.push_back(makeSample(0x02, PCAT_SEMU_MET_HUM, formatFloat_(hum), "%"));
  out.push_back(makeSample(0x03, PCAT_SEMU_MET_PRES, formatFloat_(press), "Pa"));
  out.push_back(makeSample(0x04, PCAT_SEMU_MET_LUX, std::to_string(static_cast<unsigned long>(lux_u32)), "lux"));

  uint32_t child_count = snap.pair_count;
  if (child_count == 0U) {
    child_count = loadU32_(PCAT_SEMU_KEY_SCNT, PCAT_SEMU_SET_SCNT_DEF);
  }
  if (child_count == 0U) {
    child_count = 1U;
  }
  if (child_count > kSemuChildMax) {
    child_count = kSemuChildMax;
  }

  for (uint8_t vid = 0U; vid < static_cast<uint8_t>(child_count); ++vid) {
    const uint8_t mask = static_cast<uint8_t>(1U << vid);
    const bool pair_valid = (snap.valid_pair_mask == 0U) || ((snap.valid_pair_mask & mask) != 0U);

    int32_t tf_a = pair_valid ? snap.tfl_a_mm[vid] : 0;
    int32_t tf_b = pair_valid ? snap.tfl_b_mm[vid] : 0;
    int32_t flux_a = pair_valid ? snap.tfl_a_flux[vid] : 0;
    int32_t flux_b = pair_valid ? snap.tfl_b_flux[vid] : 0;
    int32_t temp_a_x100 = pair_valid ? snap.tfl_a_temp_c_x100[vid] : 0;
    int32_t temp_b_x100 = pair_valid ? snap.tfl_b_temp_c_x100[vid] : 0;
    if (tf_a < -32768) tf_a = -32768;
    if (tf_a > 32767) tf_a = 32767;
    if (tf_b < -32768) tf_b = -32768;
    if (tf_b > 32767) tf_b = 32767;
    if (flux_a < 0) flux_a = 0;
    if (flux_a > 65535) flux_a = 65535;
    if (flux_b < 0) flux_b = 0;
    if (flux_b > 65535) flux_b = 65535;
    const float temp_a_c = static_cast<float>(temp_a_x100) / 100.0f;
    const float temp_b_c = static_cast<float>(temp_b_x100) / 100.0f;

    out.push_back(makeSample(buildChildTelemetryId(vid, 1U), buildChildScopedKey(vid, "tfl_a_mm").c_str(), std::to_string(tf_a), "mm"));
    out.push_back(makeSample(buildChildTelemetryId(vid, 2U), buildChildScopedKey(vid, "tfl_b_mm").c_str(), std::to_string(tf_b), "mm"));
    out.push_back(makeSample(buildChildTelemetryId(vid, 3U), buildChildScopedKey(vid, "tfl_a_flux").c_str(), std::to_string(flux_a), "count"));
    out.push_back(makeSample(buildChildTelemetryId(vid, 4U), buildChildScopedKey(vid, "tfl_b_flux").c_str(), std::to_string(flux_b), "count"));
    out.push_back(makeSample(buildChildTelemetryId(vid, 5U), buildChildScopedKey(vid, "tfl_a_temp_c").c_str(), formatFloat_(temp_a_c), "C"));
    out.push_back(makeSample(buildChildTelemetryId(vid, 6U), buildChildScopedKey(vid, "tfl_b_temp_c").c_str(), formatFloat_(temp_b_c), "C"));
  }
  return true;
}

espnow_link::ProfileId SemuAppProfileDefinition::profileId() const {
  return kAppProfileSemu;
}

const char* SemuAppProfileDefinition::profileName() const {
  return PCAT_SEMU_DEV_TYPE;
}

espnow_link::CodecId SemuAppProfileDefinition::defaultCodecId() const {
  return espnow_link::kCodecIdCompactIndexed;
}

bool SemuAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const {
  return espnow_link::isBuiltInCodecId(codec_id);
}

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& SemuAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildSemuTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& SemuAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildSemuSettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& SemuAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildSemuEventSpec();
  return kEvents;
}

const SemuAppProfileDefinition& semuProfileDefinition() {
  static const SemuAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makeSemuSlaveSchemaPackage(SemuAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &semuProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfileSemu;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
