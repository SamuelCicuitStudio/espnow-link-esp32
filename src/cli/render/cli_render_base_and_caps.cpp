/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Core formatting helpers and base/capability descriptor render paths.
 **************************************************************/
#include "../internal/cli_render_internal.hpp"

namespace espnow_link {

using namespace cli_helpers;

namespace {

double bytesToMb(uint32_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

const char* otaTransferStateName(uint8_t state) {
  switch (state) {
    case 0:
      return "idle";
    case 1:
      return "receiving";
    case 2:
      return "ready";
    case 3:
      return "applying";
    case 4:
      return "failed";
    default:
      return "unknown";
  }
}

const char* otaStatusCodeName(uint16_t code) {
  switch (code) {
    case 0x0000:
      return "ok";
    case 0x0001:
      return "storage_not_ready";
    case 0x0002:
      return "gate_denied";
    case 0x0003:
      return "gate_busy";
    case 0x0004:
      return "gate_prep_failed";
    case 0x0005:
      return "image_too_large";
    case 0x0006:
      return "invalid_state";
    case 0x0007:
      return "invalid_argument";
    case 0x0008:
      return "offset_mismatch";
    case 0x0009:
      return "size_mismatch";
    case 0x000A:
      return "crc_mismatch";
    case 0x000B:
      return "apply_rejected";
    case 0x000C:
      return "apply_failed";
    case 0x000D:
      return "timeout";
    case 0x00FF:
      return "internal_error";
    default:
      return "unknown";
  }
}

const char* otaGateDecisionName(uint8_t decision) {
  switch (decision) {
    case 0:
      return "ready";
    case 1:
      return "denied";
    case 2:
      return "busy";
    case 3:
      return "prep_failed";
    default:
      return "unknown";
  }
}

bool parseChildScopedTelemetryKey(const std::string& key, uint8_t& out_vid) {
  out_vid = 0U;
  if (key.size() < 4U || key[0] != 'v') {
    return false;
  }
  size_t dot = key.find('.');
  if (dot == std::string::npos || dot <= 1U) {
    return false;
  }
  for (size_t i = 1U; i < dot; ++i) {
    const unsigned char c = static_cast<unsigned char>(key[i]);
    if (std::isdigit(c) == 0) {
      return false;
    }
  }
  const unsigned long parsed = std::strtoul(key.substr(1U, dot - 1U).c_str(), nullptr, 10);
  if (parsed > 255UL) {
    return false;
  }
  out_vid = static_cast<uint8_t>(parsed);
  return true;
}

std::string lowerAscii(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool parseDescField(const std::string& desc, const char* key, std::string& out_value) {
  out_value.clear();
  if (key == nullptr || key[0] == '\0' || desc.empty()) {
    return false;
  }
  const std::string needle = std::string(key) + "=";
  const size_t start = desc.find(needle);
  if (start == std::string::npos) {
    return false;
  }
  const size_t value_begin = start + needle.size();
  const size_t value_end = desc.find(';', value_begin);
  if (value_end == std::string::npos) {
    out_value = desc.substr(value_begin);
  } else {
    out_value = desc.substr(value_begin, value_end - value_begin);
  }
  return true;
}

bool parseBoolText(const std::string& value, bool& out_bool) {
  const std::string text = lowerAscii(value);
  if (text == "1" || text == "true" || text == "yes" || text == "on") {
    out_bool = true;
    return true;
  }
  if (text == "0" || text == "false" || text == "no" || text == "off") {
    out_bool = false;
    return true;
  }
  return false;
}

std::string trimEnumLabels(const std::string& enum_text) {
  if (enum_text.empty()) {
    return enum_text;
  }
  std::string out{};
  size_t cursor = 0U;
  bool first = true;
  while (cursor < enum_text.size()) {
    size_t sep = enum_text.find('|', cursor);
    if (sep == std::string::npos) {
      sep = enum_text.size();
    }
    std::string item = enum_text.substr(cursor, sep - cursor);
    size_t colon = item.find(':');
    if (colon != std::string::npos && colon + 1U < item.size()) {
      item = item.substr(colon + 1U);
    }
    if (!item.empty()) {
      if (!first) {
        out += "|";
      }
      out += item;
      first = false;
    }
    cursor = (sep < enum_text.size()) ? (sep + 1U) : sep;
  }
  return out.empty() ? enum_text : out;
}

std::string mapEnumValue(const std::string& enum_text, const std::string& value) {
  if (enum_text.empty() || value.empty()) {
    return value;
  }
  size_t cursor = 0U;
  while (cursor < enum_text.size()) {
    size_t sep = enum_text.find('|', cursor);
    if (sep == std::string::npos) {
      sep = enum_text.size();
    }
    const std::string item = enum_text.substr(cursor, sep - cursor);
    const size_t colon = item.find(':');
    if (colon != std::string::npos) {
      const std::string id_text = item.substr(0U, colon);
      const std::string label = item.substr(colon + 1U);
      if (id_text == value && !label.empty()) {
        return label;
      }
    }
    cursor = (sep < enum_text.size()) ? (sep + 1U) : sep;
  }
  return value;
}

std::string renderSettingType(const SettingDescriptor& s) {
  std::string desc_type{};
  if (parseDescField(s.description, "type", desc_type) && !desc_type.empty()) {
    return desc_type;
  }
  switch (s.value_type) {
    case SettingValueType::Int:
      return "int";
    case SettingValueType::Float:
      return "f32";
    case SettingValueType::Bool:
      return "bool";
    case SettingValueType::String:
    default:
      return "str";
  }
}

std::string renderSettingValue(const SettingDescriptor& s, const std::string& raw_value) {
  std::string out = raw_value;
  std::string enum_text{};
  (void)parseDescField(s.description, "enum", enum_text);
  if (!enum_text.empty()) {
    out = mapEnumValue(enum_text, out);
  }
  if (s.value_type == SettingValueType::Bool) {
    bool flag = false;
    if (parseBoolText(out, flag)) {
      out = flag ? "true" : "false";
    }
  }
  std::string unit{};
  if (parseDescField(s.description, "unit", unit) &&
      !unit.empty() &&
      (s.value_type == SettingValueType::Float)) {
    if (!out.empty()) {
      out += " ";
      out += unit;
    }
  }
  return out;
}

std::string renderSettingNotes(const SettingDescriptor& s) {
  std::string enum_text{};
  if (parseDescField(s.description, "enum", enum_text) && !enum_text.empty()) {
    return trimEnumLabels(enum_text);
  }

  std::string min_text{};
  std::string max_text{};
  const bool has_min = parseDescField(s.description, "min", min_text) && !min_text.empty();
  const bool has_max = parseDescField(s.description, "max", max_text) && !max_text.empty();
  std::string unit{};
  const bool has_unit = parseDescField(s.description, "unit", unit) && !unit.empty();

  if (has_min && has_max) {
    std::string out = min_text + ".." + max_text;
    std::string type_text = renderSettingType(s);
    if (type_text == "str") {
      out = "len " + out;
    } else if (has_unit) {
      out += " ";
      out += unit;
    }
    return out;
  }
  if (has_unit) {
    return std::string("unit ") + unit;
  }
  return std::string();
}

enum class SettingsSection : uint8_t {
  General = 0,
  UiFeedback,
  Protection,
  PushRuntime,
  Topology,
  Provisioning,
  Other,
};

SettingsSection classifySettingSection(const SettingDescriptor& s) {
  const std::string key = lowerAscii(s.key);
  if (key.rfind("lidar.", 0U) == 0U ||
      (s.setting_id >= 0x0A00U && s.setting_id <= 0x0AFFU)) {
    return SettingsSection::Provisioning;
  }
  if (key.rfind("topo_", 0U) == 0U ||
      (s.setting_id >= 0x0900U && s.setting_id <= 0x09FFU)) {
    return SettingsSection::Topology;
  }
  if (key.rfind("push_", 0U) == 0U ||
      (s.setting_id >= 0x0300U && s.setting_id <= 0x03FFU)) {
    return SettingsSection::PushRuntime;
  }
  if (s.setting_id >= 0x0200U && s.setting_id <= 0x02FFU) {
    return SettingsSection::Protection;
  }
  if (key.find("buzzer") != std::string::npos ||
      key.find("led_") != std::string::npos ||
      key.rfind("rgb_", 0U) == 0U ||
      key == "fan_mode" ||
      key == "chain_48v_enable" ||
      key == "charger_enable") {
    return SettingsSection::UiFeedback;
  }
  if (s.setting_id <= 0x01FFU) {
    return SettingsSection::General;
  }
  return SettingsSection::Other;
}

const char* sectionTitle(SettingsSection section) {
  switch (section) {
    case SettingsSection::General:
      return "General";
    case SettingsSection::UiFeedback:
      return "UI / Feedback";
    case SettingsSection::Protection:
      return "Protection";
    case SettingsSection::PushRuntime:
      return "Push / Runtime";
    case SettingsSection::Topology:
      return "Topology";
    case SettingsSection::Provisioning:
      return "Provisioning";
    case SettingsSection::Other:
    default:
      return "Other";
  }
}

std::string fitCell(const std::string& text, size_t width) {
  if (width == 0U) {
    return std::string();
  }
  if (text.size() <= width) {
    return text + std::string(width - text.size(), ' ');
  }
  if (width <= 3U) {
    return text.substr(0U, width);
  }
  return text.substr(0U, width - 3U) + "...";
}

std::string tableBorder(const std::vector<size_t>& widths) {
  std::string out = "+";
  for (size_t w : widths) {
    out += std::string(w + 2U, '-');
    out += "+";
  }
  return out;
}

}  // namespace

void MasterCli::printMandatoryEvents() const {
  if (mandatory_events_.empty()) {
    io_.writeln("[MASTER][EVENT] no mandatory events");
    return;
  }
  io_.writeln("[MASTER][EVENT] mandatory events:");
  for (size_t i = 0; i < mandatory_events_.size(); ++i) {
    const auto& ev = mandatory_events_[i];
    writef("  %u. peer=%s corr=%lu id=%u sev=%u value=%ld ts=%lu rx_ms=%lu",
           static_cast<unsigned int>(i + 1),
           macToPrintable(ev.peer).c_str(),
           static_cast<unsigned long>(ev.corr_id),
           static_cast<unsigned int>(ev.event_id),
           static_cast<unsigned int>(ev.severity),
           static_cast<long>(ev.event_value),
           static_cast<unsigned long>(ev.event_ts_s),
           static_cast<unsigned long>(ev.rx_ms));
  }
}
void MasterCli::printDiscovered() const {
  if (discovered_.empty()) {
    io_.writeln("[MASTER] no discovered slave");
    return;
  }
  io_.writeln("[MASTER] discovered slaves:");
  for (size_t i = 0; i < discovered_.size(); ++i) {
    const char* name = discovered_[i].node_name.empty() ? "unknown" : discovered_[i].node_name.c_str();
    writef("  %u) %s  rssi=%d  name=%s",
           static_cast<unsigned int>(i),
           macToPrintable(discovered_[i].mac).c_str(),
           static_cast<int>(discovered_[i].rssi),
           name);
  }
}


void MasterCli::printSettingLine(const SettingDescriptor& s) {
  const char* type = "string";
  if (s.value_type == SettingValueType::Int) {
    type = "int";
  } else if (s.value_type == SettingValueType::Float) {
    type = "float";
  } else if (s.value_type == SettingValueType::Bool) {
    type = "bool";
  }

  writef("  - id=0x%04X %s (%s, rw=%s)",
         static_cast<unsigned int>(s.setting_id),
         s.key.c_str(),
         type,
         s.writable ? "yes" : "no");
  writef("    nvs=%s current=%s default=%s",
         s.nvs_key.c_str(),
         s.current_value.c_str(),
         s.default_value.c_str());
  if (!s.description.empty()) {
    writef("    desc=%s", s.description.c_str());
  }
}

const char* MasterCli::descriptorSourceLabel(const DescriptorResponse& d) const {
  if (d.message == "profile-schema") {
    return "profile-schema";
  }
  if (d.message == "profile-primary") {
    return "profile-primary";
  }
  if (d.message == "profile-primary+provider") {
    return "profile-primary+provider";
  }
  return "provider";
}

void MasterCli::printDeviceDescriptorResponse(const DescriptorResponse& d) {
  struct DeviceRow {
    const char* key;
    std::string value;
  };
  const DeviceRow rows[] = {
      {"Type", d.device.device_type},
      {"ID", d.device.device_id},
      {"Name", d.device.device_name},
      {"HW", d.device.hw_version},
      {"SW", d.device.sw_version},
      {"Build", d.device.build_id},
  };

  size_t key_width = 4U;
  size_t value_width = 22U;
  for (const auto& row : rows) {
    const size_t key_len = std::strlen(row.key);
    if (key_len > key_width) {
      key_width = key_len;
    }
    if (row.value.size() > value_width) {
      value_width = row.value.size();
    }
  }

  const std::string border =
      "+" + std::string(key_width + 2U, '-') + "+" + std::string(value_width + 2U, '-') + "+";
  io_.writeln("[MASTER][DESC] Device");
  io_.writeln(border);
  for (const auto& row : rows) {
    io_.writeln("| " + fitCell(row.key, key_width) + " | " + fitCell(row.value, value_width) + " |");
  }
  io_.writeln(border);
}

void MasterCli::printCapabilitiesDescriptorResponse(const DescriptorResponse& d) {
  remote_profile_id_ = kProfileUnknown;
  remote_settings_count_ = 0;
  for (const auto& cap : d.capabilities) {
    if (cap.key == "profile_id") {
      const unsigned long profile_id = std::strtoul(cap.description.c_str(), nullptr, 10);
      if (profile_id > 0U && profile_id <= 0xFFFFUL) {
        remote_profile_id_ = static_cast<ProfileId>(profile_id);
      }
    } else if (cap.key == "settings_count") {
      remote_settings_count_ = static_cast<uint16_t>(std::strtoul(cap.description.c_str(), nullptr, 10));
    }
  }
  writef("[MASTER][DESC] Capabilities source=%s snapshot=%lu",
         descriptorSourceLabel(d),
         static_cast<unsigned long>(d.snapshot_id));
  if (d.capabilities.empty()) {
    io_.writeln("  (none)");
    return;
  }

  auto normalizeCsv = [](const std::string& text) -> std::string {
    if (text.find(',') == std::string::npos) {
      return text;
    }
    std::string out{};
    size_t cursor = 0U;
    while (cursor < text.size()) {
      size_t sep = text.find(',', cursor);
      if (sep == std::string::npos) {
        sep = text.size();
      }
      std::string token = text.substr(cursor, sep - cursor);
      while (!token.empty() && token.front() == ' ') {
        token.erase(token.begin());
      }
      while (!token.empty() && token.back() == ' ') {
        token.pop_back();
      }
      if (!token.empty()) {
        if (!out.empty()) {
          out += ", ";
        }
        out += token;
      }
      cursor = (sep < text.size()) ? (sep + 1U) : sep;
    }
    return out.empty() ? text : out;
  };

  std::vector<bool> used(d.capabilities.size(), false);
  auto fetchCapability = [&](std::initializer_list<const char*> keys) -> std::string {
    for (const char* key : keys) {
      if (key == nullptr || key[0] == '\0') {
        continue;
      }
      const std::string wanted = lowerAscii(key);
      for (size_t i = 0; i < d.capabilities.size(); ++i) {
        if (lowerAscii(d.capabilities[i].key) == wanted) {
          used[i] = true;
          return d.capabilities[i].description;
        }
      }
    }
    return std::string();
  };

  struct CapabilityRow {
    std::string key;
    std::string value;
  };

  auto addRowIfValue = [](std::vector<CapabilityRow>& rows, const char* key, const std::string& value) {
    if (key != nullptr && key[0] != '\0' && !value.empty()) {
      rows.push_back(CapabilityRow{key, value});
    }
  };

  std::vector<CapabilityRow> identity{};
  addRowIfValue(identity, "Profile", fetchCapability({"profile", "pfnm"}));
  addRowIfValue(identity, "Profile ID", fetchCapability({"profile_id", "pfid"}));
  addRowIfValue(identity, "Schema Rev", fetchCapability({"schrv"}));
  addRowIfValue(identity, "Schema Hash", fetchCapability({"schsh"}));

  std::vector<CapabilityRow> counts{};
  addRowIfValue(counts, "Telemetry Count", fetchCapability({"telemetry_count"}));
  addRowIfValue(counts, "Settings Count", fetchCapability({"settings_count"}));
  addRowIfValue(counts, "Events Count", fetchCapability({"events_count"}));
  addRowIfValue(counts, "Metric ID", fetchCapability({"metid"}));
  addRowIfValue(counts, "Setting ID", fetchCapability({"setid"}));
  addRowIfValue(counts, "Event ID", fetchCapability({"evid"}));

  std::vector<CapabilityRow> maps{};
  addRowIfValue(maps, "Settings Map", normalizeCsv(fetchCapability({"setmap"})));
  addRowIfValue(maps, "Telemetry Map", normalizeCsv(fetchCapability({"metmap"})));
  addRowIfValue(maps, "Events Map", normalizeCsv(fetchCapability({"evmap"})));
  addRowIfValue(maps, "Commands", normalizeCsv(fetchCapability({"cmdset"})));

  std::vector<CapabilityRow> features{};
  addRowIfValue(features, "Pairing", fetchCapability({"pair"}));
  addRowIfValue(features, "Unpair", fetchCapability({"unpair"}));
  addRowIfValue(features, "Slave Desc", fetchCapability({"mslv"}));
  addRowIfValue(features, "Descriptor", fetchCapability({"desc"}));
  addRowIfValue(features, "Settings RW", fetchCapability({"setrw"}));
  addRowIfValue(features, "Telemetry Pull", fetchCapability({"tpull"}));
  addRowIfValue(features, "Telemetry Push", fetchCapability({"tpush"}));
  addRowIfValue(features, "Live", fetchCapability({"live"}));
  addRowIfValue(features, "Time Sync", fetchCapability({"tsync"}));
  addRowIfValue(features, "OTA", fetchCapability({"ota"}));

  std::vector<CapabilityRow> other{};
  for (size_t i = 0; i < d.capabilities.size(); ++i) {
    if (used[i]) {
      continue;
    }
    other.push_back(CapabilityRow{d.capabilities[i].key, d.capabilities[i].description});
  }

  auto printSection = [&](const char* title, const std::vector<CapabilityRow>& rows) {
    if (rows.empty()) {
      return;
    }
    constexpr size_t kKeyWidth = 15U;
    constexpr size_t kValueWidth = 58U;
    io_.writeln("");
    io_.writeln(std::string("[") + title + "]");
    io_.writeln(fitCell("Key", kKeyWidth) + " | Value");
    io_.writeln(std::string(kKeyWidth, '-') + "+" + std::string(kValueWidth, '-'));
    for (const auto& row : rows) {
      io_.writeln(fitCell(row.key, kKeyWidth) + " | " + fitCell(row.value, kValueWidth));
    }
  };

  printSection("Identity", identity);
  printSection("Counts", counts);
  printSection("Maps", maps);
  printSection("Features", features);
  printSection("Other", other);
}

}  // namespace espnow_link
