#include "profile_catalog/slaves/pms/pms_profile.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace app_owned {

namespace {

constexpr const char* kPmsProfileIdText = "1";
constexpr const char* kPmsSchemaRev = "1";
constexpr const char* kPmsSchemaHash = "pms001";

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

std::vector<espnow_link::ProfileTelemetryMetricSpec> buildPmsTelemetrySpec() {
  std::vector<espnow_link::ProfileTelemetryMetricSpec> out;
  out.reserve(7);
  espnow_link::ProfileTelemetryMetricSpec t{};
  t.metric_id = 0x01;
  t.key = PCAT_PMS_MET_WALLV;
  out.push_back(t);
  t.metric_id = 0x02;
  t.key = PCAT_PMS_MET_BATTV;
  out.push_back(t);
  t.metric_id = 0x03;
  t.key = PCAT_PMS_MET_WALLI;
  out.push_back(t);
  t.metric_id = 0x04;
  t.key = PCAT_PMS_MET_BATTI;
  out.push_back(t);
  t.metric_id = 0x05;
  t.key = PCAT_PMS_MET_PSRC;
  out.push_back(t);
  t.metric_id = 0x06;
  t.key = PCAT_PMS_MET_TRIP;
  out.push_back(t);
  t.metric_id = 0x07;
  t.key = PCAT_PMS_MET_RCUT;
  out.push_back(t);
  return out;
}

std::vector<espnow_link::ProfileSettingSpec> buildPmsSettingSpec() {
  std::vector<espnow_link::ProfileSettingSpec> out;
  out.reserve(21);
  espnow_link::ProfileSettingSpec s{};
  s.setting_id = 0x0001;
  s.key = PCAT_PMS_SET_DNAME;
  out.push_back(s);
  s.setting_id = 0x0002;
  s.key = PCAT_PMS_SET_CHAN;
  out.push_back(s);
  s.setting_id = 0x0101;
  s.key = PCAT_PMS_SET_PMOD;
  out.push_back(s);
  s.setting_id = 0x0102;
  s.key = PCAT_PMS_SET_LOOPAUTO;
  out.push_back(s);
  s.setting_id = 0x0103;
  s.key = PCAT_PMS_SET_TRIPI;
  out.push_back(s);
  s.setting_id = 0x0104;
  s.key = PCAT_PMS_SET_VCAL;
  out.push_back(s);
  s.setting_id = 0x0105;
  s.key = PCAT_PMS_SET_ICAL;
  out.push_back(s);
  s.setting_id = 0x0106;
  s.key = PCAT_PMS_SET_BUZEN;
  out.push_back(s);
  s.setting_id = 0x0107;
  s.key = PCAT_PMS_SET_LEDFB;
  out.push_back(s);
  s.setting_id = 0x010B;
  s.key = PCAT_PMS_SET_RGBIDL;
  out.push_back(s);
  s.setting_id = 0x010C;
  s.key = PCAT_PMS_SET_RGBALT;
  out.push_back(s);
  s.setting_id = 0x010D;
  s.key = PCAT_PMS_SET_RGBBRT;
  out.push_back(s);
  s.setting_id = 0x0108;
  s.key = PCAT_PMS_SET_FANMD;
  out.push_back(s);
  s.setting_id = 0x0109;
  s.key = PCAT_PMS_SET_C48EN;
  out.push_back(s);
  s.setting_id = 0x010A;
  s.key = PCAT_PMS_SET_CHGEN;
  out.push_back(s);
  s.setting_id = 0x0201;
  s.key = PCAT_PMS_SET_VBOVP;
  out.push_back(s);
  s.setting_id = 0x0202;
  s.key = PCAT_PMS_SET_VBUVP;
  out.push_back(s);
  s.setting_id = 0x0203;
  s.key = PCAT_PMS_SET_IBOCP;
  out.push_back(s);
  s.setting_id = 0x0204;
  s.key = PCAT_PMS_SET_BAOVP;
  out.push_back(s);
  s.setting_id = 0x0205;
  s.key = PCAT_PMS_SET_BAUVP;
  out.push_back(s);
  s.setting_id = 0x0206;
  s.key = PCAT_PMS_SET_BIOCP;
  out.push_back(s);
  return out;
}

std::vector<espnow_link::ProfileEventSpec> buildPmsEventSpec() {
  std::vector<espnow_link::ProfileEventSpec> out;
  out.reserve(3);
  espnow_link::ProfileEventSpec e{};
  e.event_id = 0xE001;
  e.key = PCAT_PMS_EVT_TRIPCH;
  out.push_back(e);
  e.event_id = 0xE002;
  e.key = PCAT_PMS_EVT_PWRFLT;
  out.push_back(e);
  e.event_id = 0xE003;
  e.key = PCAT_PMS_EVT_RESET;
  out.push_back(e);
  return out;
}

}  // namespace

PmsAppDescriptorProvider::PmsAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                                                   const espnow_link::MacAddress& local_mac,
                                                   espnow_link::ITimeSink* time_sink,
                                                   espnow_link::IStorageExplorerProvider* storage,
                                                   espnow_link::OtaDescriptorAdapter* ota,
                                                   const PmsDescriptorAppConfig& cfg)
    : nvs_(nvs),
      local_mac_(local_mac),
      time_sink_(time_sink),
      storage_(storage),
      ota_(ota),
      cfg_(cfg) {}

bool PmsAppDescriptorProvider::getDeviceDescriptor(espnow_link::DeviceDescriptor& out) {
  out.device_type = cfg_.device_type.empty() ? PCAT_PMS_DEV_TYPE : cfg_.device_type;

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
  out.device_name = loadString_(PCAT_PMS_KEY_DNAME, cfg_.default_device_name.c_str());
  out.hw_version = cfg_.hw_version.empty() ? PCAT_PMS_HW_VER : cfg_.hw_version;
  out.sw_version = cfg_.sw_version.empty() ? PCAT_PMS_SW_VER : cfg_.sw_version;
  out.build_id = cfg_.build_id.empty() ? PCAT_PMS_BUILD_ID : cfg_.build_id;
  return true;
}

bool PmsAppDescriptorProvider::getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) {
  out.clear();
  out.push_back({"pfid", kPmsProfileIdText});
  out.push_back({"pfnm", PCAT_PMS_DEV_TYPE});
  out.push_back({"schrv", kPmsSchemaRev});
  out.push_back({"schsh", kPmsSchemaHash});
  out.push_back({"metid", "1"});
  out.push_back({"setid", "1"});
  out.push_back({"evid", "1"});
  out.push_back({"setmap", PCAT_PMS_SETMAP});
  out.push_back({"metmap", PCAT_PMS_METMAP});
  out.push_back({"evmap", PCAT_PMS_EVMAP});
  out.push_back({"cmdset", "gdesc,gcaps,gtel,pull,glive,gtime,stime,gset,sset,ota,log,sd"});
  out.push_back({"pair", "Secure pair handshake"});
  out.push_back({"unpair", "Bidirectional unpair with ack"});
  out.push_back({"mslv", "Slave descriptor is app-owned and consumed by master registry"});
  out.push_back({"desc", "Descriptor/schema queries backed by app definitions"});
  out.push_back({"setrw", "Settings expose type/range metadata in descriptor"});
  out.push_back({"tpull", "On-demand telemetry pull"});
  out.push_back({"tpush", "Push stream with metric ID mapping"});
  out.push_back({"live", "Liveness snapshot pull"});
  out.push_back({"tsync", "Remote RTC set/get"});
  out.push_back({"ota", "OTA transfer/apply via storage backend"});
  return true;
}

bool PmsAppDescriptorProvider::getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) {
  out.clear();

  espnow_link::TelemetryDescriptor t{};
  t.metric_id = 0x01;
  t.key = PCAT_PMS_MET_WALLV;
  t.unit = "V";
  t.description = PCAT_PMS_DESC_MET_WALLV;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x02;
  t.key = PCAT_PMS_MET_BATTV;
  t.unit = "V";
  t.description = PCAT_PMS_DESC_MET_BATTV;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x03;
  t.key = PCAT_PMS_MET_WALLI;
  t.unit = "A";
  t.description = PCAT_PMS_DESC_MET_WALLI;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x04;
  t.key = PCAT_PMS_MET_BATTI;
  t.unit = "A";
  t.description = PCAT_PMS_DESC_MET_BATTI;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x05;
  t.key = PCAT_PMS_MET_PSRC;
  t.unit = "";
  t.description = PCAT_PMS_DESC_MET_PSRC;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x06;
  t.key = PCAT_PMS_MET_TRIP;
  t.unit = "bool";
  t.description = PCAT_PMS_DESC_MET_TRIP;
  out.push_back(t);

  t = espnow_link::TelemetryDescriptor{};
  t.metric_id = 0x07;
  t.key = PCAT_PMS_MET_RCUT;
  t.unit = "bool";
  t.description = PCAT_PMS_DESC_MET_RCUT;
  out.push_back(t);

  return true;
}

bool PmsAppDescriptorProvider::getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) {
  out.clear();
  return appendTelemetryFromRuntime_(out);
}

bool PmsAppDescriptorProvider::getLiveness(espnow_link::LivenessStatus& out) {
  out.online = true;
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  out.state = "ready";
  return true;
}

bool PmsAppDescriptorProvider::getTime(espnow_link::TimeStatus& out) {
  out.epoch_s = static_cast<uint64_t>(time(nullptr));
#if defined(ARDUINO)
  out.uptime_ms = millis();
#else
  out.uptime_ms = 0U;
#endif
  return true;
}

bool PmsAppDescriptorProvider::setTime(uint64_t epoch_s, std::string& out_message) {
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

bool PmsAppDescriptorProvider::getSettings(std::vector<espnow_link::SettingDescriptor>& out) {
  out.clear();

  espnow_link::SettingDescriptor s{};
  s.setting_id = 0x0001;
  s.key = PCAT_PMS_SET_DNAME;
  s.value_type = espnow_link::SettingValueType::String;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_DNAME;
  s.current_value = loadString_(PCAT_PMS_KEY_DNAME, cfg_.default_device_name.c_str());
  s.default_value = cfg_.default_device_name;
  s.description = PCAT_PMS_DESC_SET_DNAME;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0002;
  s.key = PCAT_PMS_SET_CHAN;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_CHAN;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_CHAN, static_cast<uint16_t>(PCAT_PMS_SET_CHAN_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_CHAN_DEF));
  s.description = PCAT_PMS_DESC_SET_CHAN;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0101;
  s.key = PCAT_PMS_SET_PMOD;
  s.value_type = espnow_link::SettingValueType::String;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_PMOD;
  s.current_value = loadString_(PCAT_PMS_KEY_PMOD, PCAT_PMS_SET_PMOD_DEF);
  s.default_value = PCAT_PMS_SET_PMOD_DEF;
  s.description = PCAT_PMS_DESC_SET_PMOD;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0102;
  s.key = PCAT_PMS_SET_LOOPAUTO;
  s.value_type = espnow_link::SettingValueType::Bool;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_LOOPA;
  s.current_value = loadBool_(PCAT_PMS_KEY_LOOPA, PCAT_PMS_SET_LOOPAUTO_DEF != 0) ? "1" : "0";
  s.default_value = (PCAT_PMS_SET_LOOPAUTO_DEF != 0) ? "1" : "0";
  s.description = PCAT_PMS_DESC_SET_LOOPAUTO;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0103;
  s.key = PCAT_PMS_SET_TRIPI;
  s.value_type = espnow_link::SettingValueType::Float;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_TRIPI;
  s.current_value = formatFloat_(loadFloat_(PCAT_PMS_KEY_TRIPI, PCAT_PMS_SET_TRIPI_DEF));
  s.default_value = formatFloat_(PCAT_PMS_SET_TRIPI_DEF);
  s.description = PCAT_PMS_DESC_SET_TRIPI;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0104;
  s.key = PCAT_PMS_SET_VCAL;
  s.value_type = espnow_link::SettingValueType::Float;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_VCAL;
  s.current_value = formatFloat_(loadFloat_(PCAT_PMS_KEY_VCAL, PCAT_PMS_SET_VCAL_DEF));
  s.default_value = formatFloat_(PCAT_PMS_SET_VCAL_DEF);
  s.description = PCAT_PMS_DESC_SET_VCAL;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0105;
  s.key = PCAT_PMS_SET_ICAL;
  s.value_type = espnow_link::SettingValueType::Float;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_ICAL;
  s.current_value = formatFloat_(loadFloat_(PCAT_PMS_KEY_ICAL, PCAT_PMS_SET_ICAL_DEF));
  s.default_value = formatFloat_(PCAT_PMS_SET_ICAL_DEF);
  s.description = PCAT_PMS_DESC_SET_ICAL;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0106;
  s.key = PCAT_PMS_SET_BUZEN;
  s.value_type = espnow_link::SettingValueType::Bool;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_BUZEN;
  s.current_value = loadBool_(PCAT_PMS_KEY_BUZEN, PCAT_PMS_SET_BUZEN_DEF != 0) ? "1" : "0";
  s.default_value = (PCAT_PMS_SET_BUZEN_DEF != 0) ? "1" : "0";
  s.description = PCAT_PMS_DESC_SET_BUZEN;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0107;
  s.key = PCAT_PMS_SET_LEDFB;
  s.value_type = espnow_link::SettingValueType::Bool;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_LEDFB;
  s.current_value = loadBool_(PCAT_PMS_KEY_LEDFB, PCAT_PMS_SET_LEDFB_DEF != 0) ? "1" : "0";
  s.default_value = (PCAT_PMS_SET_LEDFB_DEF != 0) ? "1" : "0";
  s.description = PCAT_PMS_DESC_SET_LEDFB;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x010B;
  s.key = PCAT_PMS_SET_RGBIDL;
  s.value_type = espnow_link::SettingValueType::String;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_RGBIDL;
  s.current_value = loadString_(PCAT_PMS_KEY_RGBIDL, PCAT_PMS_SET_RGBIDL_DEF);
  s.default_value = PCAT_PMS_SET_RGBIDL_DEF;
  s.description = PCAT_PMS_DESC_SET_RGBIDL;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x010C;
  s.key = PCAT_PMS_SET_RGBALT;
  s.value_type = espnow_link::SettingValueType::String;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_RGBALT;
  s.current_value = loadString_(PCAT_PMS_KEY_RGBALT, PCAT_PMS_SET_RGBALT_DEF);
  s.default_value = PCAT_PMS_SET_RGBALT_DEF;
  s.description = PCAT_PMS_DESC_SET_RGBALT;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x010D;
  s.key = PCAT_PMS_SET_RGBBRT;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_RGBBRT;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_RGBBRT, static_cast<uint16_t>(PCAT_PMS_SET_RGBBRT_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_RGBBRT_DEF));
  s.description = PCAT_PMS_DESC_SET_RGBBRT;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0108;
  s.key = PCAT_PMS_SET_FANMD;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_FANMD;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_FANMD, static_cast<uint16_t>(PCAT_PMS_SET_FANMD_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_FANMD_DEF));
  s.description = PCAT_PMS_DESC_SET_FANMD;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0109;
  s.key = PCAT_PMS_SET_C48EN;
  s.value_type = espnow_link::SettingValueType::Bool;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_C48EN;
  s.current_value = loadBool_(PCAT_PMS_KEY_C48EN, PCAT_PMS_SET_C48EN_DEF != 0) ? "1" : "0";
  s.default_value = (PCAT_PMS_SET_C48EN_DEF != 0) ? "1" : "0";
  s.description = PCAT_PMS_DESC_SET_C48EN;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x010A;
  s.key = PCAT_PMS_SET_CHGEN;
  s.value_type = espnow_link::SettingValueType::Bool;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_CHGEN;
  s.current_value = loadBool_(PCAT_PMS_KEY_CHGEN, PCAT_PMS_SET_CHGEN_DEF != 0) ? "1" : "0";
  s.default_value = (PCAT_PMS_SET_CHGEN_DEF != 0) ? "1" : "0";
  s.description = PCAT_PMS_DESC_SET_CHGEN;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0201;
  s.key = PCAT_PMS_SET_VBOVP;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_VBOVP;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_VBOVP, static_cast<uint16_t>(PCAT_PMS_SET_VBOVP_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_VBOVP_DEF));
  s.description = PCAT_PMS_DESC_SET_VBOVP;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0202;
  s.key = PCAT_PMS_SET_VBUVP;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_VBUVP;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_VBUVP, static_cast<uint16_t>(PCAT_PMS_SET_VBUVP_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_VBUVP_DEF));
  s.description = PCAT_PMS_DESC_SET_VBUVP;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0203;
  s.key = PCAT_PMS_SET_IBOCP;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_IBOCP;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_IBOCP, static_cast<uint16_t>(PCAT_PMS_SET_IBOCP_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_IBOCP_DEF));
  s.description = PCAT_PMS_DESC_SET_IBOCP;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0204;
  s.key = PCAT_PMS_SET_BAOVP;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_BAOVP;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_BAOVP, static_cast<uint16_t>(PCAT_PMS_SET_BAOVP_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_BAOVP_DEF));
  s.description = PCAT_PMS_DESC_SET_BAOVP;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0205;
  s.key = PCAT_PMS_SET_BAUVP;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_BAUVP;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_BAUVP, static_cast<uint16_t>(PCAT_PMS_SET_BAUVP_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_BAUVP_DEF));
  s.description = PCAT_PMS_DESC_SET_BAUVP;
  out.push_back(s);

  s = espnow_link::SettingDescriptor{};
  s.setting_id = 0x0206;
  s.key = PCAT_PMS_SET_BIOCP;
  s.value_type = espnow_link::SettingValueType::Int;
  s.writable = true;
  s.nvs_key = PCAT_PMS_KEY_BIOCP;
  s.current_value =
      std::to_string(static_cast<unsigned long>(loadU16_(PCAT_PMS_KEY_BIOCP, static_cast<uint16_t>(PCAT_PMS_SET_BIOCP_DEF))));
  s.default_value = std::to_string(static_cast<unsigned long>(PCAT_PMS_SET_BIOCP_DEF));
  s.description = PCAT_PMS_DESC_SET_BIOCP;
  out.push_back(s);

  return true;
}

bool PmsAppDescriptorProvider::getSetting(const std::string& key, espnow_link::SettingDescriptor& out) {
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

bool PmsAppDescriptorProvider::getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&pmsProfileDefinition(), setting_id);
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

bool PmsAppDescriptorProvider::setSetting(const std::string& key, const std::string& value, std::string& out_message) {
  if (key == PCAT_PMS_SET_DNAME) {
    if (value.empty()) {
      out_message = "dname cannot be empty";
      return false;
    }
    const bool ok = nvs_.putString(PCAT_PMS_KEY_DNAME, value);
    out_message = ok ? "dname updated" : "dname persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }

  if (key == PCAT_PMS_SET_CHAN) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_CHAN,
                                       PCAT_PMS_SET_CHAN,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_CHAN_MIN),
                                       static_cast<long>(PCAT_PMS_SET_CHAN_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_VBOVP) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_VBOVP,
                                       PCAT_PMS_SET_VBOVP,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_VBOVP_MIN),
                                       static_cast<long>(PCAT_PMS_SET_VBOVP_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_VBUVP) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_VBUVP,
                                       PCAT_PMS_SET_VBUVP,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_VBUVP_MIN),
                                       static_cast<long>(PCAT_PMS_SET_VBUVP_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_IBOCP) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_IBOCP,
                                       PCAT_PMS_SET_IBOCP,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_IBOCP_MIN),
                                       static_cast<long>(PCAT_PMS_SET_IBOCP_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_BAOVP) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_BAOVP,
                                       PCAT_PMS_SET_BAOVP,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_BAOVP_MIN),
                                       static_cast<long>(PCAT_PMS_SET_BAOVP_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_BAUVP) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_BAUVP,
                                       PCAT_PMS_SET_BAUVP,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_BAUVP_MIN),
                                       static_cast<long>(PCAT_PMS_SET_BAUVP_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_BIOCP) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_BIOCP,
                                       PCAT_PMS_SET_BIOCP,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_BIOCP_MIN),
                                       static_cast<long>(PCAT_PMS_SET_BIOCP_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }

  if (key == PCAT_PMS_SET_PMOD) {
    if (!(value == "normal" || value == "eco" || value == "safe")) {
      out_message = "pmod expects normal|eco|safe";
      return false;
    }
    const bool ok = nvs_.putString(PCAT_PMS_KEY_PMOD, value);
    out_message = ok ? "pmod updated" : "pmod persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }

  if (key == PCAT_PMS_SET_FANMD) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_FANMD,
                                       PCAT_PMS_SET_FANMD,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_FANMD_MIN),
                                       static_cast<long>(PCAT_PMS_SET_FANMD_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }

  if (key == PCAT_PMS_SET_LOOPAUTO) {
    bool parsed = false;
    if (!parseBool(value, parsed)) {
      out_message = "LoopAuto expects bool";
      return false;
    }
    const bool ok = nvs_.putBool(PCAT_PMS_KEY_LOOPA, parsed);
    out_message = ok ? "LoopAuto updated" : "LoopAuto persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_BUZEN) {
    bool parsed = false;
    if (!parseBool(value, parsed)) {
      out_message = "buzzer_enable expects bool";
      return false;
    }
    const bool ok = nvs_.putBool(PCAT_PMS_KEY_BUZEN, parsed);
    out_message = ok ? "buzzer_enable updated" : "buzzer_enable persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_LEDFB) {
    bool parsed = false;
    if (!parseBool(value, parsed)) {
      out_message = "led_feedback_enable expects bool";
      return false;
    }
    const bool ok = nvs_.putBool(PCAT_PMS_KEY_LEDFB, parsed);
    out_message = ok ? "led_feedback_enable updated" : "led_feedback_enable persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_RGBIDL) {
    if (value.empty()) {
      out_message = "rgb_idle_color cannot be empty";
      return false;
    }
    const bool ok = nvs_.putString(PCAT_PMS_KEY_RGBIDL, value);
    out_message = ok ? "rgb_idle_color updated" : "rgb_idle_color persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_RGBALT) {
    if (value.empty()) {
      out_message = "rgb_alert_color cannot be empty";
      return false;
    }
    const bool ok = nvs_.putString(PCAT_PMS_KEY_RGBALT, value);
    out_message = ok ? "rgb_alert_color updated" : "rgb_alert_color persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_RGBBRT) {
    const bool ok = persistU16Setting_(PCAT_PMS_KEY_RGBBRT,
                                       PCAT_PMS_SET_RGBBRT,
                                       std::strtol(value.c_str(), nullptr, 10),
                                       static_cast<long>(PCAT_PMS_SET_RGBBRT_MIN),
                                       static_cast<long>(PCAT_PMS_SET_RGBBRT_MAX),
                                       out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_C48EN) {
    bool parsed = false;
    if (!parseBool(value, parsed)) {
      out_message = "chain_48v_enable expects bool";
      return false;
    }
    const bool ok = nvs_.putBool(PCAT_PMS_KEY_C48EN, parsed);
    out_message = ok ? "chain_48v_enable updated" : "chain_48v_enable persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_CHGEN) {
    bool parsed = false;
    if (!parseBool(value, parsed)) {
      out_message = "charger_enable expects bool";
      return false;
    }
    const bool ok = nvs_.putBool(PCAT_PMS_KEY_CHGEN, parsed);
    out_message = ok ? "charger_enable updated" : "charger_enable persist failed";
    return finalizeSettingChange_(key, value, ok, out_message);
  }

  if (key == PCAT_PMS_SET_TRIPI) {
    const bool ok = persistFloatSetting_(
        PCAT_PMS_KEY_TRIPI, PCAT_PMS_SET_TRIPI, value, PCAT_PMS_SET_TRIPI_MIN, PCAT_PMS_SET_TRIPI_MAX, out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_VCAL) {
    const bool ok = persistFloatSetting_(
        PCAT_PMS_KEY_VCAL, PCAT_PMS_SET_VCAL, value, PCAT_PMS_SET_VCAL_MIN, PCAT_PMS_SET_VCAL_MAX, out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }
  if (key == PCAT_PMS_SET_ICAL) {
    const bool ok = persistFloatSetting_(
        PCAT_PMS_KEY_ICAL, PCAT_PMS_SET_ICAL, value, PCAT_PMS_SET_ICAL_MIN, PCAT_PMS_SET_ICAL_MAX, out_message);
    return finalizeSettingChange_(key, value, ok, out_message);
  }

  out_message = "unknown setting";
  return false;
}

bool PmsAppDescriptorProvider::setSettingById(uint16_t setting_id,
                                              const std::string& value,
                                              std::string& out_message) {
  const espnow_link::ProfileSettingSpec* spec = espnow_link::findProfileSettingById(&pmsProfileDefinition(), setting_id);
  if (spec == nullptr || spec->key == nullptr || spec->key[0] == '\0') {
    out_message = "setting id not found";
    return false;
  }
  return setSetting(spec->key, value, out_message);
}

bool PmsAppDescriptorProvider::authorizeLoggerClear(std::string& out_message) {
  if (loadString_(PCAT_PMS_KEY_PMOD, PCAT_PMS_SET_PMOD_DEF) == "safe") {
    out_message = "logger clear denied in safe mode";
    return false;
  }
  out_message.clear();
  return true;
}

bool PmsAppDescriptorProvider::authorizeLoggerSetEnabled(bool enable, std::string& out_message) {
  (void)enable;
  if (loadString_(PCAT_PMS_KEY_PMOD, PCAT_PMS_SET_PMOD_DEF) == "safe") {
    out_message = "logger control denied in safe mode";
    return false;
  }
  out_message.clear();
  return true;
}

bool PmsAppDescriptorProvider::getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->getStorageInfo(out, out_message);
}

bool PmsAppDescriptorProvider::listStoragePath(const std::string& path,
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

bool PmsAppDescriptorProvider::statStoragePath(const std::string& path,
                                               espnow_link::StorageStat& out,
                                               std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->statStoragePath(path, out, out_message);
}

bool PmsAppDescriptorProvider::formatStorage(std::string& out_message) {
  if (storage_ == nullptr) {
    out_message = "storage explorer unavailable";
    return false;
  }
  return storage_->formatStorage(out_message);
}

bool PmsAppDescriptorProvider::getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaStatus(out, out_message);
}

bool PmsAppDescriptorProvider::getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaManifest(out, out_message);
}

bool PmsAppDescriptorProvider::rebuildOtaManifest(std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->rebuildOtaManifest(out_message);
}

bool PmsAppDescriptorProvider::clearOtaScope(const std::string& scope, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->clearOtaScope(scope, out_message);
}

bool PmsAppDescriptorProvider::getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaCapacity(out, out_message);
}

bool PmsAppDescriptorProvider::getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->getOtaGateInfo(out, out_message);
}

bool PmsAppDescriptorProvider::applyOtaImage(const std::string& target, std::string& out_message) {
  if (ota_ == nullptr) {
    out_message = "ota unavailable";
    return false;
  }
  return ota_->applyOtaImage(target, out_message);
}

uint16_t PmsAppDescriptorProvider::loadU16_(const char* key, uint16_t fallback) const {
  uint16_t out = fallback;
  (void)nvs_.getU16(key, out);
  return out;
}

bool PmsAppDescriptorProvider::loadBool_(const char* key, bool fallback) const {
  bool out = fallback;
  (void)nvs_.getBool(key, out);
  return out;
}

float PmsAppDescriptorProvider::loadFloat_(const char* key, float fallback) const {
  float out = fallback;
  (void)nvs_.getFloat(key, out);
  return out;
}

std::string PmsAppDescriptorProvider::loadString_(const char* key, const char* fallback) const {
  std::string out;
  if (!nvs_.getString(key, out) || out.empty()) {
    return std::string(fallback != nullptr ? fallback : "");
  }
  return out;
}

std::string PmsAppDescriptorProvider::formatFloat_(float value) const {
  char buf[24] = {0};
  std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
  return std::string(buf);
}

bool PmsAppDescriptorProvider::persistU16Setting_(const char* key,
                                                  const char* setting_name,
                                                  long value,
                                                  long min_v,
                                                  long max_v,
                                                  std::string& out_message) {
  if (value < min_v || value > max_v) {
    out_message = std::string(setting_name) + " out of range";
    return false;
  }
  const bool ok = nvs_.putU16(key, static_cast<uint16_t>(value));
  out_message = ok ? std::string(setting_name) + " updated" : std::string(setting_name) + " persist failed";
  return ok;
}

bool PmsAppDescriptorProvider::persistFloatSetting_(const char* key,
                                                    const char* setting_name,
                                                    const std::string& value,
                                                    float min_v,
                                                    float max_v,
                                                    std::string& out_message) {
  float parsed = 0.0f;
  if (!parseFloat(value, parsed) || parsed < min_v || parsed > max_v) {
    out_message = std::string(setting_name) + " out of range";
    return false;
  }
  const bool ok = nvs_.putFloat(key, parsed);
  out_message = ok ? std::string(setting_name) + " updated" : std::string(setting_name) + " persist failed";
  return ok;
}

bool PmsAppDescriptorProvider::finalizeSettingChange_(const std::string& key,
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

bool PmsAppDescriptorProvider::appendTelemetryFromRuntime_(std::vector<espnow_link::TelemetrySample>& out) {
  if (cfg_.read_telemetry == nullptr) {
    return false;
  }

  PmsRuntimeTelemetrySnapshot snap{};
  if (!cfg_.read_telemetry(cfg_.runtime_user, snap) || !snap.valid) {
    return false;
  }

  std::string source = snap.power_source;
  if (!(source == "wall" || source == "battery")) {
    source = "wall";
  }

  out.push_back(makeSample(0x01, PCAT_PMS_MET_WALLV, formatFloat_(snap.wall_v), "V"));
  out.push_back(makeSample(0x02, PCAT_PMS_MET_BATTV, formatFloat_(snap.batt_v), "V"));
  out.push_back(makeSample(0x03, PCAT_PMS_MET_WALLI, formatFloat_(snap.wall_i), "A"));
  out.push_back(makeSample(0x04, PCAT_PMS_MET_BATTI, formatFloat_(snap.batt_i), "A"));
  out.push_back(makeSample(0x05, PCAT_PMS_MET_PSRC, source, ""));
  out.push_back(makeSample(0x06, PCAT_PMS_MET_TRIP, snap.trip ? "1" : "0", "bool"));
  out.push_back(makeSample(0x07, PCAT_PMS_MET_RCUT, snap.relay_cut ? "1" : "0", "bool"));
  return true;
}

espnow_link::ProfileId PmsAppProfileDefinition::profileId() const {
  return kAppProfilePms;
}

const char* PmsAppProfileDefinition::profileName() const {
  return PCAT_PMS_DEV_TYPE;
}

espnow_link::CodecId PmsAppProfileDefinition::defaultCodecId() const {
  return espnow_link::kCodecIdCompactIndexed;
}

bool PmsAppProfileDefinition::supportsCodec(espnow_link::CodecId codec_id) const {
  return espnow_link::isBuiltInCodecId(codec_id);
}

const std::vector<espnow_link::ProfileTelemetryMetricSpec>& PmsAppProfileDefinition::telemetryMetrics() const {
  static const std::vector<espnow_link::ProfileTelemetryMetricSpec> kTelemetry = buildPmsTelemetrySpec();
  return kTelemetry;
}

const std::vector<espnow_link::ProfileSettingSpec>& PmsAppProfileDefinition::settings() const {
  static const std::vector<espnow_link::ProfileSettingSpec> kSettings = buildPmsSettingSpec();
  return kSettings;
}

const std::vector<espnow_link::ProfileEventSpec>& PmsAppProfileDefinition::events() const {
  static const std::vector<espnow_link::ProfileEventSpec> kEvents = buildPmsEventSpec();
  return kEvents;
}

const PmsAppProfileDefinition& pmsProfileDefinition() {
  static const PmsAppProfileDefinition kDef{};
  return kDef;
}

SlaveSchemaPackage makePmsSlaveSchemaPackage(PmsAppDescriptorProvider& provider, espnow_link::CodecId codec_id) {
  SlaveSchemaPackage pkg{};
  pkg.profile = &pmsProfileDefinition();
  pkg.descriptor = &provider;
  pkg.telemetry_push = &provider;
  pkg.profile_id = kAppProfilePms;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned


