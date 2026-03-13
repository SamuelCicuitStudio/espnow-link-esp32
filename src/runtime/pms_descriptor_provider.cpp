#include "espnow_link/pms_descriptor_provider.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#include <sys/time.h>
#endif

namespace espnow_link {

PmsDescriptorProvider::PmsDescriptorProvider(PreferencesStore& nvs,
                                             const MacAddress& local_mac,
                                             ITimeSink* time_sink,
                                             IStorageExplorerProvider* storage,
                                             OtaDescriptorAdapter* ota,
                                             const PmsDescriptorProviderConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool PmsDescriptorProvider::getDeviceDescriptor(DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? "PMS" : cfg_.device_type;
  MacAddress device_mac = local_mac_;
#if defined(ARDUINO)
  uint8_t wifi_mac[6] = {0};
  if (esp_wifi_get_mac(WIFI_IF_STA, wifi_mac) == ESP_OK) {
    for (size_t i = 0; i < device_mac.size(); ++i) {
      device_mac[i] = wifi_mac[i];
    }
  }
#endif
  out.device_id = macToString(device_mac);

#if defined(ARDUINO)
  String name;
  if (!nvs_.getString(kSettingNameKey, name) || name.isEmpty()) {
    name = cfg_.default_device_name.c_str();
  }
  out.device_name = std::string(name.c_str());
#else
  out.device_name = cfg_.default_device_name;
#endif

  out.hw_version = cfg_.hw_version.empty() ? "PMS-HW1" : cfg_.hw_version;
  out.sw_version = cfg_.sw_version;
  out.build_id = cfg_.build_id;
  return true;
}

bool PmsDescriptorProvider::getCapabilities(std::vector<CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pair", "Secure unpaired to paired handshake"});
  out.push_back({"unpair", "Bidirectional unpair with ack"});
  out.push_back({"control", "Master pull request response channel"});
  out.push_back({"descriptor", "Descriptor and schema queries"});
  out.push_back({"settings_rw", "Remote set and get settings"});
  out.push_back({"telemetry_pull", "On-demand telemetry responses"});
  out.push_back({"liveness_pull", "On-demand liveness heartbeat"});
  out.push_back({"autopull_hint", "Provides suggested liveness interval"});
  out.push_back({"time_sync", "Slave RTC get/set over descriptor RPC"});
  out.push_back({"reset", "NVS reset key plus deep sleep reboot"});
  return true;
}

bool PmsDescriptorProvider::getTelemetrySchema(std::vector<TelemetryDescriptor>& out) {
  out.clear();

  TelemetryDescriptor t{};
  t.key = "wall_v";
  t.unit = "V";
  t.min_value = 0.0f;
  t.max_value = 0.0f;
  t.description = "";
  out.push_back(t);

  t = TelemetryDescriptor{};
  t.key = "batt_v";
  t.unit = "V";
  t.min_value = 0.0f;
  t.max_value = 0.0f;
  t.description = "";
  out.push_back(t);

  t = TelemetryDescriptor{};
  t.key = "wall_i";
  t.unit = "A";
  t.min_value = 0.0f;
  t.max_value = 0.0f;
  t.description = "";
  out.push_back(t);

  t = TelemetryDescriptor{};
  t.key = "batt_i";
  t.unit = "A";
  t.min_value = 0.0f;
  t.max_value = 0.0f;
  t.description = "";
  out.push_back(t);

  t = TelemetryDescriptor{};
  t.key = "power_source";
  t.unit = "";
  t.min_value = 0.0f;
  t.max_value = 0.0f;
  t.description = "";
  out.push_back(t);

  t = TelemetryDescriptor{};
  t.key = "trip";
  t.unit = "bool";
  t.min_value = 0.0f;
  t.max_value = 0.0f;
  t.description = "";
  out.push_back(t);

  t = TelemetryDescriptor{};
  t.key = "relay_cut";
  t.unit = "bool";
  t.min_value = 0.0f;
  t.max_value = 0.0f;
  t.description = "";
  out.push_back(t);

  return true;
}

bool PmsDescriptorProvider::getTelemetrySnapshot(std::vector<TelemetrySample>& out) {
  out.clear();

#if defined(ARDUINO)
  const uint32_t now = millis();
#else
  const uint32_t now = 0U;
#endif
  const float phase = static_cast<float>(now % 10000U) / 10000.0f;
  const float wall_v = 12.0f + 0.5f * phase;
  const float batt_v = 11.8f + 0.4f * phase;

  const float max_wall_i = static_cast<float>(loadU16_(kSettingIbusOcpMaKey, 10000)) / 1000.0f;
  float wall_i = -1.2f + 2.0f * phase;
  if (wall_i > max_wall_i) {
    wall_i = max_wall_i;
  }

  float batt_i = -0.8f + 1.6f * phase;
  if (batt_i > max_wall_i) {
    batt_i = max_wall_i;
  }

  const bool trip_enabled = (loadString_(kSettingPwrModeKey, "normal") != "safe");
  const bool trip = trip_enabled && (((now / 30000U) % 2U) != 0U);
  const bool relay_cut = trip;
  const char* power_source = (wall_v >= batt_v) ? "wall" : "battery";

  out.push_back({"wall_v", formatFloat_(wall_v), "V"});
  out.push_back({"batt_v", formatFloat_(batt_v), "V"});
  out.push_back({"wall_i", formatFloat_(wall_i), "A"});
  out.push_back({"batt_i", formatFloat_(batt_i), "A"});
  out.push_back({"power_source", power_source, ""});
  out.push_back({"trip", trip ? "1" : "0", "bool"});
  out.push_back({"relay_cut", relay_cut ? "1" : "0", "bool"});
  return true;
}

bool PmsDescriptorProvider::getLiveness(LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0;
#endif
  out.state = "ready";
  return true;
}

bool PmsDescriptorProvider::getTime(TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0;
#endif
  return true;
}

bool PmsDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool PmsDescriptorProvider::getSettings(std::vector<SettingDescriptor>& out) {
  out.clear();

  SettingDescriptor s{};

  s = SettingDescriptor{};
  s.setting_id = kSettingIdDeviceName;
  s.key = "device_name";
  s.value_type = SettingValueType::String;
  s.writable = true;
  s.nvs_key = kSettingNameKey;
  s.current_value = loadName_();
  s.default_value = cfg_.default_device_name;
  s.description = "Human-readable node label";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdChannel;
  s.key = "channel";
  s.value_type = SettingValueType::Int;
  s.writable = true;
  s.nvs_key = kSettingChannelKey;
  s.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingChannelKey, 1)));
  s.default_value = "1";
  s.description = "ESP-NOW channel";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdPwrMode;
  s.key = "pwr_mode";
  s.value_type = SettingValueType::String;
  s.writable = true;
  s.nvs_key = kSettingPwrModeKey;
  s.current_value = loadString_(kSettingPwrModeKey, "normal");
  s.default_value = "normal";
  s.description = "Power behavior mode: normal|eco|safe";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdTripLimitCurrent;
  s.key = "trip_limit_current";
  s.value_type = SettingValueType::Float;
  s.writable = true;
  s.nvs_key = kSettingTripLimitCurrentKey;
  s.current_value = formatFloat_(loadFloat_(kSettingTripLimitCurrentKey, 15.0f));
  s.default_value = "15.00";
  s.description = "Current trip limit in A";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdVCalFactor;
  s.key = "v_cal_factor";
  s.value_type = SettingValueType::Float;
  s.writable = true;
  s.nvs_key = kSettingVCalFactorKey;
  s.current_value = formatFloat_(loadFloat_(kSettingVCalFactorKey, 1.0f));
  s.default_value = "1.00";
  s.description = "Voltage calibration factor";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdICalFactor;
  s.key = "i_cal_factor";
  s.value_type = SettingValueType::Float;
  s.writable = true;
  s.nvs_key = kSettingICalFactorKey;
  s.current_value = formatFloat_(loadFloat_(kSettingICalFactorKey, 1.0f));
  s.default_value = "1.00";
  s.description = "Current calibration factor";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdVbusOvpMv;
  s.key = "vbus_ovp_mv";
  s.value_type = SettingValueType::Int;
  s.writable = true;
  s.nvs_key = kSettingVbusOvpMvKey;
  s.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbusOvpMvKey, 15000)));
  s.default_value = "15000";
  s.description = "VBUS over-voltage protection in mV";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdVbusUvpMv;
  s.key = "vbus_uvp_mv";
  s.value_type = SettingValueType::Int;
  s.writable = true;
  s.nvs_key = kSettingVbusUvpMvKey;
  s.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbusUvpMvKey, 9000)));
  s.default_value = "9000";
  s.description = "VBUS under-voltage protection in mV";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdIbusOcpMa;
  s.key = "ibus_ocp_ma";
  s.value_type = SettingValueType::Int;
  s.writable = true;
  s.nvs_key = kSettingIbusOcpMaKey;
  s.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingIbusOcpMaKey, 10000)));
  s.default_value = "10000";
  s.description = "IBUS over-current protection in mA";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdVbatOvpMv;
  s.key = "vbat_ovp_mv";
  s.value_type = SettingValueType::Int;
  s.writable = true;
  s.nvs_key = kSettingVbatOvpMvKey;
  s.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbatOvpMvKey, 14600)));
  s.default_value = "14600";
  s.description = "VBAT over-voltage protection in mV";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdVbatUvpMv;
  s.key = "vbat_uvp_mv";
  s.value_type = SettingValueType::Int;
  s.writable = true;
  s.nvs_key = kSettingVbatUvpMvKey;
  s.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbatUvpMvKey, 10000)));
  s.default_value = "10000";
  s.description = "VBAT under-voltage protection in mV";
  out.push_back(s);

  s = SettingDescriptor{};
  s.setting_id = kSettingIdIbatOcpMa;
  s.key = "ibat_ocp_ma";
  s.value_type = SettingValueType::Int;
  s.writable = true;
  s.nvs_key = kSettingIbatOcpMaKey;
  s.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingIbatOcpMaKey, 10000)));
  s.default_value = "10000";
  s.description = "IBAT over-current protection in mA";
  out.push_back(s);

  return true;
}

bool PmsDescriptorProvider::getSetting(const std::string& key, SettingDescriptor& out) {
  uint16_t setting_id = 0;
  if (!settingIdFromKey_(key, setting_id)) {
    return false;
  }
  return getSettingById(setting_id, out);
}

bool PmsDescriptorProvider::getSettingById(uint16_t setting_id, SettingDescriptor& out) {
  out = SettingDescriptor{};
  switch (setting_id) {
    case kSettingIdDeviceName:
      out.setting_id = kSettingIdDeviceName;
      out.key = "device_name";
      out.value_type = SettingValueType::String;
      out.writable = true;
      out.nvs_key = kSettingNameKey;
      out.current_value = loadName_();
      out.default_value = cfg_.default_device_name;
      out.description = "Human-readable node label";
      return true;

    case kSettingIdChannel:
      out.setting_id = kSettingIdChannel;
      out.key = "channel";
      out.value_type = SettingValueType::Int;
      out.writable = true;
      out.nvs_key = kSettingChannelKey;
      out.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingChannelKey, 1)));
      out.default_value = "1";
      out.description = "ESP-NOW channel";
      return true;

    case kSettingIdPwrMode:
      out.setting_id = kSettingIdPwrMode;
      out.key = "pwr_mode";
      out.value_type = SettingValueType::String;
      out.writable = true;
      out.nvs_key = kSettingPwrModeKey;
      out.current_value = loadString_(kSettingPwrModeKey, "normal");
      out.default_value = "normal";
      out.description = "Power behavior mode: normal|eco|safe";
      return true;

    case kSettingIdTripLimitCurrent:
      out.setting_id = kSettingIdTripLimitCurrent;
      out.key = "trip_limit_current";
      out.value_type = SettingValueType::Float;
      out.writable = true;
      out.nvs_key = kSettingTripLimitCurrentKey;
      out.current_value = formatFloat_(loadFloat_(kSettingTripLimitCurrentKey, 15.0f));
      out.default_value = "15.00";
      out.description = "Current trip limit in A";
      return true;

    case kSettingIdVCalFactor:
      out.setting_id = kSettingIdVCalFactor;
      out.key = "v_cal_factor";
      out.value_type = SettingValueType::Float;
      out.writable = true;
      out.nvs_key = kSettingVCalFactorKey;
      out.current_value = formatFloat_(loadFloat_(kSettingVCalFactorKey, 1.0f));
      out.default_value = "1.00";
      out.description = "Voltage calibration factor";
      return true;

    case kSettingIdICalFactor:
      out.setting_id = kSettingIdICalFactor;
      out.key = "i_cal_factor";
      out.value_type = SettingValueType::Float;
      out.writable = true;
      out.nvs_key = kSettingICalFactorKey;
      out.current_value = formatFloat_(loadFloat_(kSettingICalFactorKey, 1.0f));
      out.default_value = "1.00";
      out.description = "Current calibration factor";
      return true;

    case kSettingIdVbusOvpMv:
      out.setting_id = kSettingIdVbusOvpMv;
      out.key = "vbus_ovp_mv";
      out.value_type = SettingValueType::Int;
      out.writable = true;
      out.nvs_key = kSettingVbusOvpMvKey;
      out.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbusOvpMvKey, 15000)));
      out.default_value = "15000";
      out.description = "VBUS over-voltage protection in mV";
      return true;

    case kSettingIdVbusUvpMv:
      out.setting_id = kSettingIdVbusUvpMv;
      out.key = "vbus_uvp_mv";
      out.value_type = SettingValueType::Int;
      out.writable = true;
      out.nvs_key = kSettingVbusUvpMvKey;
      out.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbusUvpMvKey, 9000)));
      out.default_value = "9000";
      out.description = "VBUS under-voltage protection in mV";
      return true;

    case kSettingIdIbusOcpMa:
      out.setting_id = kSettingIdIbusOcpMa;
      out.key = "ibus_ocp_ma";
      out.value_type = SettingValueType::Int;
      out.writable = true;
      out.nvs_key = kSettingIbusOcpMaKey;
      out.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingIbusOcpMaKey, 10000)));
      out.default_value = "10000";
      out.description = "IBUS over-current protection in mA";
      return true;

    case kSettingIdVbatOvpMv:
      out.setting_id = kSettingIdVbatOvpMv;
      out.key = "vbat_ovp_mv";
      out.value_type = SettingValueType::Int;
      out.writable = true;
      out.nvs_key = kSettingVbatOvpMvKey;
      out.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbatOvpMvKey, 14600)));
      out.default_value = "14600";
      out.description = "VBAT over-voltage protection in mV";
      return true;

    case kSettingIdVbatUvpMv:
      out.setting_id = kSettingIdVbatUvpMv;
      out.key = "vbat_uvp_mv";
      out.value_type = SettingValueType::Int;
      out.writable = true;
      out.nvs_key = kSettingVbatUvpMvKey;
      out.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingVbatUvpMvKey, 10000)));
      out.default_value = "10000";
      out.description = "VBAT under-voltage protection in mV";
      return true;

    case kSettingIdIbatOcpMa:
      out.setting_id = kSettingIdIbatOcpMa;
      out.key = "ibat_ocp_ma";
      out.value_type = SettingValueType::Int;
      out.writable = true;
      out.nvs_key = kSettingIbatOcpMaKey;
      out.current_value = std::to_string(static_cast<unsigned long>(loadU16_(kSettingIbatOcpMaKey, 10000)));
      out.default_value = "10000";
      out.description = "IBAT over-current protection in mA";
      return true;

    default:
      return false;
  }
}

bool PmsDescriptorProvider::setSetting(const std::string& key,
                                       const std::string& value,
                                       std::string& out_message) {
  if (key == "device_name") {
    if (value.empty()) {
      out_message = "device_name cannot be empty";
      return false;
    }
#if defined(ARDUINO)
    const bool ok = nvs_.putString(kSettingNameKey, String(value.c_str()));
#else
    const bool ok = nvs_.putString(kSettingNameKey, value);
#endif
    out_message = ok ? "device_name updated" : "device_name persist failed";
    return ok;
  }

  if (key == "channel") {
    const long v = std::strtol(value.c_str(), nullptr, 10);
    if (v < 1 || v > 14) {
      out_message = "channel must be 1..14";
      return false;
    }
    const bool ok = nvs_.putU16(kSettingChannelKey, static_cast<uint16_t>(v));
    out_message = ok ? "channel updated" : "channel persist failed";
    return ok;
  }

  if (key == "pwr_mode") {
    if (!(value == "normal" || value == "eco" || value == "safe")) {
      out_message = "pwr_mode expects normal|eco|safe";
      return false;
    }
#if defined(ARDUINO)
    const bool ok = nvs_.putString(kSettingPwrModeKey, String(value.c_str()));
#else
    const bool ok = nvs_.putString(kSettingPwrModeKey, value);
#endif
    out_message = ok ? "pwr_mode updated" : "pwr_mode persist failed";
    return ok;
  }

  if (key == "trip_limit_current") {
    char* endp = nullptr;
    const float v = std::strtof(value.c_str(), &endp);
    if (endp == nullptr || *endp != '\0' || v < 1.0f || v > 200.0f) {
      out_message = "trip_limit_current must be float between 1 and 200";
      return false;
    }
    const bool ok = nvs_.putFloat(kSettingTripLimitCurrentKey, v);
    out_message = ok ? "trip_limit_current updated" : "trip_limit_current persist failed";
    return ok;
  }

  if (key == "v_cal_factor") {
    char* endp = nullptr;
    const float v = std::strtof(value.c_str(), &endp);
    if (endp == nullptr || *endp != '\0' || v < 0.5f || v > 1.5f) {
      out_message = "v_cal_factor must be float between 0.5 and 1.5";
      return false;
    }
    const bool ok = nvs_.putFloat(kSettingVCalFactorKey, v);
    out_message = ok ? "v_cal_factor updated" : "v_cal_factor persist failed";
    return ok;
  }

  if (key == "i_cal_factor") {
    char* endp = nullptr;
    const float v = std::strtof(value.c_str(), &endp);
    if (endp == nullptr || *endp != '\0' || v < 0.5f || v > 1.5f) {
      out_message = "i_cal_factor must be float between 0.5 and 1.5";
      return false;
    }
    const bool ok = nvs_.putFloat(kSettingICalFactorKey, v);
    out_message = ok ? "i_cal_factor updated" : "i_cal_factor persist failed";
    return ok;
  }

  if (key == "vbus_ovp_mv") {
    const long v = std::strtol(value.c_str(), nullptr, 10);
    if (v < 6000 || v > 30000) {
      out_message = "vbus_ovp_mv must be 6000..30000";
      return false;
    }
    const bool ok = nvs_.putU16(kSettingVbusOvpMvKey, static_cast<uint16_t>(v));
    out_message = ok ? "vbus_ovp_mv updated" : "vbus_ovp_mv persist failed";
    return ok;
  }

  if (key == "vbus_uvp_mv") {
    const long v = std::strtol(value.c_str(), nullptr, 10);
    if (v < 4000 || v > 25000) {
      out_message = "vbus_uvp_mv must be 4000..25000";
      return false;
    }
    const bool ok = nvs_.putU16(kSettingVbusUvpMvKey, static_cast<uint16_t>(v));
    out_message = ok ? "vbus_uvp_mv updated" : "vbus_uvp_mv persist failed";
    return ok;
  }

  if (key == "ibus_ocp_ma") {
    const long v = std::strtol(value.c_str(), nullptr, 10);
    if (v < 500 || v > 60000) {
      out_message = "ibus_ocp_ma must be 500..60000";
      return false;
    }
    const bool ok = nvs_.putU16(kSettingIbusOcpMaKey, static_cast<uint16_t>(v));
    out_message = ok ? "ibus_ocp_ma updated" : "ibus_ocp_ma persist failed";
    return ok;
  }

  if (key == "vbat_ovp_mv") {
    const long v = std::strtol(value.c_str(), nullptr, 10);
    if (v < 6000 || v > 30000) {
      out_message = "vbat_ovp_mv must be 6000..30000";
      return false;
    }
    const bool ok = nvs_.putU16(kSettingVbatOvpMvKey, static_cast<uint16_t>(v));
    out_message = ok ? "vbat_ovp_mv updated" : "vbat_ovp_mv persist failed";
    return ok;
  }

  if (key == "vbat_uvp_mv") {
    const long v = std::strtol(value.c_str(), nullptr, 10);
    if (v < 4000 || v > 25000) {
      out_message = "vbat_uvp_mv must be 4000..25000";
      return false;
    }
    const bool ok = nvs_.putU16(kSettingVbatUvpMvKey, static_cast<uint16_t>(v));
    out_message = ok ? "vbat_uvp_mv updated" : "vbat_uvp_mv persist failed";
    return ok;
  }

  if (key == "ibat_ocp_ma") {
    const long v = std::strtol(value.c_str(), nullptr, 10);
    if (v < 500 || v > 60000) {
      out_message = "ibat_ocp_ma must be 500..60000";
      return false;
    }
    const bool ok = nvs_.putU16(kSettingIbatOcpMaKey, static_cast<uint16_t>(v));
    out_message = ok ? "ibat_ocp_ma updated" : "ibat_ocp_ma persist failed";
    return ok;
  }

  out_message = "unknown setting";
  return false;
}

bool PmsDescriptorProvider::setSettingById(uint16_t setting_id,
                                           const std::string& value,
                                           std::string& out_message) {
  const char* key = nullptr;
  if (!settingKeyFromId_(setting_id, key) || key == nullptr) {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(key, value, out_message);
}

bool PmsDescriptorProvider::authorizeLoggerClear(std::string& out_message) {
  if (loadString_(kSettingPwrModeKey, "normal") == "safe") {
    out_message = "logger clear denied in safe mode";
    return false;
  }
  out_message.clear();
  return true;
}

bool PmsDescriptorProvider::authorizeLoggerSetEnabled(bool enable, std::string& out_message) {
  (void)enable;
  if (loadString_(kSettingPwrModeKey, "normal") == "safe") {
    out_message = "logger control denied in safe mode";
    return false;
  }
  out_message.clear();
  return true;
}

bool PmsDescriptorProvider::getStorageInfo(StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool PmsDescriptorProvider::listStoragePath(const std::string& path,
                                            std::string& out_canonical_path,
                                            std::string& out_parent_path,
                                            std::vector<StorageEntry>& out_entries,
                                            std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->listStoragePath(path, out_canonical_path, out_parent_path, out_entries, out_message);
}

bool PmsDescriptorProvider::statStoragePath(const std::string& path, StorageStat& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool PmsDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool PmsDescriptorProvider::getOtaStatus(OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool PmsDescriptorProvider::getOtaManifest(std::vector<OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool PmsDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool PmsDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool PmsDescriptorProvider::getOtaCapacity(OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool PmsDescriptorProvider::getOtaGateInfo(OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool PmsDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

std::string PmsDescriptorProvider::formatFloat_(float v) const {
  char buf[24] = {0};
  std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
  return std::string(buf);
}

std::string PmsDescriptorProvider::loadName_() const {
#if defined(ARDUINO)
  String name;
  if (!nvs_.getString(kSettingNameKey, name) || name.isEmpty()) {
    return cfg_.default_device_name;
  }
  return std::string(name.c_str());
#else
  return cfg_.default_device_name;
#endif
}

uint16_t PmsDescriptorProvider::loadU16_(const char* key, uint16_t fallback) const {
  uint16_t v = fallback;
  (void)nvs_.getU16(key, v);
  return v;
}

std::string PmsDescriptorProvider::loadString_(const char* key, const char* fallback) const {
#if defined(ARDUINO)
  String v;
  if (!nvs_.getString(key, v) || v.isEmpty()) {
    return std::string(fallback);
  }
  return std::string(v.c_str());
#else
  return std::string(fallback);
#endif
}

float PmsDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float v = fallback;
  (void)nvs_.getFloat(key, v);
  return v;
}

bool PmsDescriptorProvider::settingIdFromKey_(const std::string& key, uint16_t& out_id) const {
  if (key == "device_name") {
    out_id = kSettingIdDeviceName;
    return true;
  }
  if (key == "channel") {
    out_id = kSettingIdChannel;
    return true;
  }
  if (key == "pwr_mode") {
    out_id = kSettingIdPwrMode;
    return true;
  }
  if (key == "trip_limit_current") {
    out_id = kSettingIdTripLimitCurrent;
    return true;
  }
  if (key == "v_cal_factor") {
    out_id = kSettingIdVCalFactor;
    return true;
  }
  if (key == "i_cal_factor") {
    out_id = kSettingIdICalFactor;
    return true;
  }
  if (key == "vbus_ovp_mv") {
    out_id = kSettingIdVbusOvpMv;
    return true;
  }
  if (key == "vbus_uvp_mv") {
    out_id = kSettingIdVbusUvpMv;
    return true;
  }
  if (key == "ibus_ocp_ma") {
    out_id = kSettingIdIbusOcpMa;
    return true;
  }
  if (key == "vbat_ovp_mv") {
    out_id = kSettingIdVbatOvpMv;
    return true;
  }
  if (key == "vbat_uvp_mv") {
    out_id = kSettingIdVbatUvpMv;
    return true;
  }
  if (key == "ibat_ocp_ma") {
    out_id = kSettingIdIbatOcpMa;
    return true;
  }
  return false;
}

bool PmsDescriptorProvider::settingKeyFromId_(uint16_t setting_id, const char*& out_key) const {
  switch (setting_id) {
    case kSettingIdDeviceName:
      out_key = "device_name";
      return true;
    case kSettingIdChannel:
      out_key = "channel";
      return true;
    case kSettingIdPwrMode:
      out_key = "pwr_mode";
      return true;
    case kSettingIdTripLimitCurrent:
      out_key = "trip_limit_current";
      return true;
    case kSettingIdVCalFactor:
      out_key = "v_cal_factor";
      return true;
    case kSettingIdICalFactor:
      out_key = "i_cal_factor";
      return true;
    case kSettingIdVbusOvpMv:
      out_key = "vbus_ovp_mv";
      return true;
    case kSettingIdVbusUvpMv:
      out_key = "vbus_uvp_mv";
      return true;
    case kSettingIdIbusOcpMa:
      out_key = "ibus_ocp_ma";
      return true;
    case kSettingIdVbatOvpMv:
      out_key = "vbat_ovp_mv";
      return true;
    case kSettingIdVbatUvpMv:
      out_key = "vbat_uvp_mv";
      return true;
    case kSettingIdIbatOcpMa:
      out_key = "ibat_ocp_ma";
      return true;
    default:
      out_key = nullptr;
      return false;
  }
}

}  // namespace espnow_link
