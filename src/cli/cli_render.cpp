#include "espnow_link/cli_master.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <unordered_map>

#include "cli_helpers.hpp"

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

void MasterCli::printTelemetrySchemaDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][DESC] telemetry schema (source=%s):", descriptorSourceLabel(d));
  if (d.telemetry.empty()) {
    io_.writeln("  (none)");
    return;
  }
  for (size_t i = 0; i < d.telemetry.size(); ++i) {
    const auto& t = d.telemetry[i];
    writef("  %u. id=0x%04X %s [%s] range=%.2f..%.2f | %s",
           static_cast<unsigned int>(i + 1),
           static_cast<unsigned int>(t.metric_id),
           t.key.c_str(),
           t.unit.c_str(),
           static_cast<double>(t.min_value),
           static_cast<double>(t.max_value),
           t.description.c_str());
  }
}

void MasterCli::printTelemetrySnapshotDescriptorResponse(const DescriptorResponse& d) {
  io_.writeln("[MASTER][TELEM] live samples");
  const bool child_filter = semu_telem_child_filter_active_;
  const uint8_t child_vid = semu_telem_child_filter_vid_;
  const uint8_t child_max_vid = semu_telem_child_filter_max_vid_;

  std::vector<const TelemetrySample*> filtered_samples{};
  filtered_samples.reserve(d.telemetry_samples.size());
  for (size_t i = 0; i < d.telemetry_samples.size(); ++i) {
    const auto& t = d.telemetry_samples[i];
    if (child_filter) {
      uint8_t sample_vid = 0U;
      const bool is_child_metric = parseChildScopedTelemetryKey(t.key, sample_vid);
      if (is_child_metric && sample_vid > child_max_vid) {
        continue;
      }
      if (is_child_metric && sample_vid != child_vid) {
        continue;
      }
    }
    filtered_samples.push_back(&t);
  }

  bool sensor_like =
      (remote_profile_id_ == kProfileSens) || (remote_profile_id_ == kProfileSemu);
  if (!sensor_like) {
    for (size_t i = 0U; i < filtered_samples.size(); ++i) {
      const TelemetrySample* s = filtered_samples[i];
      if (s == nullptr) continue;
      if (s->key == "tfl_a_mm" || s->key == "tfl_b_mm" ||
          s->key == "tfl_a_flux" || s->key == "tfl_b_flux" ||
          s->key == "tfl_a_temp_c" || s->key == "tfl_b_temp_c" ||
          s->key.find(".tfl_a_mm") != std::string::npos ||
          s->key.find(".tfl_b_mm") != std::string::npos) {
        sensor_like = true;
        break;
      }
    }
  }

  if (sensor_like) {
    bool semu_profile = (remote_profile_id_ == kProfileSemu);
    if (!semu_profile) {
      for (size_t i = 0U; i < filtered_samples.size(); ++i) {
        const TelemetrySample* s = filtered_samples[i];
        if (s == nullptr) continue;
        uint8_t vid = 0U;
        if (parseChildScopedTelemetryKey(s->key, vid)) {
          semu_profile = true;
          break;
        }
      }
    }
    const std::vector<size_t> widths = {3U, 3U, 2U, 11U, 8U, 8U, 9U};
    const std::string border = tableBorder(widths);
    const size_t full_inner_width = (border.size() >= 2U) ? (border.size() - 2U) : 0U;

    auto trimAscii = [](std::string text) -> std::string {
      size_t begin = 0U;
      while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
      }
      size_t end = text.size();
      while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
        --end;
      }
      return text.substr(begin, end - begin);
    };

    auto parseNumber = [&](const std::string& text, double& out_value) -> bool {
      const std::string cleaned = trimAscii(text);
      if (cleaned.empty()) {
        return false;
      }
      char* endp = nullptr;
      out_value = std::strtod(cleaned.c_str(), &endp);
      if (endp == cleaned.c_str()) {
        return false;
      }
      while (endp != nullptr && *endp != '\0') {
        if (std::isspace(static_cast<unsigned char>(*endp)) == 0) {
          return false;
        }
        ++endp;
      }
      return true;
    };

    auto formatValue = [&](const TelemetrySample* sample,
                           const char* unit,
                           int precision,
                           bool integer_mode,
                           bool clamp_negative,
                           const char* missing_value) -> std::string {
      if (sample == nullptr) {
        return std::string(missing_value != nullptr ? missing_value : "-");
      }
      double numeric = 0.0;
      if (parseNumber(sample->value, numeric)) {
        if (clamp_negative && numeric < 0.0) {
          numeric = 0.0;
        }
        char buf[64] = {0};
        if (integer_mode) {
          std::snprintf(buf, sizeof(buf), "%.0f", numeric);
        } else {
          std::snprintf(buf, sizeof(buf), "%.*f", precision, numeric);
        }
        std::string out = buf;
        if (unit != nullptr && unit[0] != '\0') {
          out += " ";
          out += unit;
        }
        return out;
      }

      std::string out = sample->value;
      const std::string unit_text = (unit != nullptr) ? std::string(unit) : std::string();
      if (!unit_text.empty() && out.find(unit_text) == std::string::npos) {
        out += " ";
        out += unit_text;
      }
      return out.empty() ? std::string(missing_value != nullptr ? missing_value : "-") : out;
    };

    auto writeRow = [&](const std::string& c0,
                        const std::string& c1,
                        const std::string& c2,
                        const std::string& c3,
                        const std::string& c4,
                        const std::string& c5,
                        const std::string& c6) {
      io_.writeln("| " + fitCell(c0, widths[0]) + " | " +
                  fitCell(c1, widths[1]) + " | " +
                  fitCell(c2, widths[2]) + " | " +
                  fitCell(c3, widths[3]) + " | " +
                  fitCell(c4, widths[4]) + " | " +
                  fitCell(c5, widths[5]) + " | " +
                  fitCell(c6, widths[6]) + " |");
    };

    std::unordered_map<std::string, const TelemetrySample*> sample_by_key{};
    sample_by_key.reserve(filtered_samples.size() * 2U + 8U);
    for (size_t i = 0U; i < filtered_samples.size(); ++i) {
      const TelemetrySample* s = filtered_samples[i];
      if (s == nullptr) continue;
      sample_by_key[s->key] = s;
    }

    auto findSample = [&](const std::string& key) -> const TelemetrySample* {
      const auto it = sample_by_key.find(key);
      return (it == sample_by_key.end()) ? nullptr : it->second;
    };

    auto findPairMetric = [&](uint8_t vid, std::initializer_list<const char*> suffixes) -> const TelemetrySample* {
      if (semu_profile) {
        for (const char* suffix : suffixes) {
          if (suffix == nullptr || suffix[0] == '\0') continue;
          const std::string scoped_key =
              std::string("v") + std::to_string(static_cast<unsigned int>(vid)) + "." + suffix;
          const TelemetrySample* scoped = findSample(scoped_key);
          if (scoped != nullptr) {
            return scoped;
          }
        }
      }
      for (const char* suffix : suffixes) {
        if (suffix == nullptr || suffix[0] == '\0') continue;
        const TelemetrySample* plain = findSample(std::string(suffix));
        if (plain != nullptr) {
          return plain;
        }
      }
      return nullptr;
    };

    const TelemetrySample* env_temp = findSample("env_temp_c");
    const TelemetrySample* env_hum = findSample("env_hum_pct");
    const TelemetrySample* env_press = findSample("env_press_pa");
    const TelemetrySample* env_lux = findSample("lux");

    if (child_filter && semu_profile) {
      writef("  filter: child=%u + global", static_cast<unsigned int>(child_vid));
    }
    io_.writeln("");
    io_.writeln(border);
    writeRow("ARR", "IDX", "CH", "NAME", "MM", "FLUX", "TEMP");
    io_.writeln(border);
    writeRow("ENV", "[0]", "-", "TEMP", "-", "-", formatValue(env_temp, "C", 3, false, false, "-"));
    writeRow("ENV", "[1]", "-", "HUM", "-", "-", formatValue(env_hum, "%", 3, false, false, "-"));
    writeRow("ENV", "[2]", "-", "PRESS", "-", "-", formatValue(env_press, "Pa", 0, true, false, "-"));
    writeRow("ENV", "[3]", "-", "LUX", formatValue(env_lux, "lux", 0, true, false, "-"), "-", "-");
    io_.writeln(border);

    uint8_t start_vid = 0U;
    uint8_t end_vid = semu_profile ? 7U : 0U;
    if (child_filter && semu_profile) {
      start_vid = (child_vid > 7U) ? 7U : child_vid;
      end_vid = start_vid;
    }

    for (uint8_t vid = start_vid; vid <= end_vid; ++vid) {
      const std::string section_label =
          std::string("V[") + std::to_string(static_cast<unsigned int>(vid)) +
          "] / SENSOR GROUP " + std::to_string(static_cast<unsigned int>(vid));
      io_.writeln("| " + fitCell(section_label, (full_inner_width >= 2U) ? (full_inner_width - 2U) : 0U) + " |");
      io_.writeln(border);

      const TelemetrySample* a_mm = findPairMetric(vid, {"tfl_a_mm", "tf_a_mm"});
      const TelemetrySample* b_mm = findPairMetric(vid, {"tfl_b_mm", "tf_b_mm"});
      const TelemetrySample* a_flux = findPairMetric(vid, {"tfl_a_flux", "tf_a_flux"});
      const TelemetrySample* b_flux = findPairMetric(vid, {"tfl_b_flux", "tf_b_flux"});
      const TelemetrySample* a_temp = findPairMetric(vid, {"tfl_a_temp_c", "tf_a_temp_c"});
      const TelemetrySample* b_temp = findPairMetric(vid, {"tfl_b_temp_c", "tf_b_temp_c"});

      const std::string idx_label =
          std::string("[") + std::to_string(static_cast<unsigned int>(vid)) + "]";
      writeRow("V",
               idx_label,
               "A",
               "tfl_a",
               formatValue(a_mm, "mm", 0, true, true, "0 mm"),
               formatValue(a_flux, "cnt", 0, true, true, "0 cnt"),
               formatValue(a_temp, "C", 3, false, true, "0.000 C"));
      writeRow("V",
               idx_label,
               "B",
               "tfl_b",
               formatValue(b_mm, "mm", 0, true, true, "0 mm"),
               formatValue(b_flux, "cnt", 0, true, true, "0 cnt"),
               formatValue(b_temp, "C", 3, false, true, "0.000 C"));
      io_.writeln(border);
    }

    semu_telem_child_filter_active_ = false;
    return;
  }

  bool pms_like = (remote_profile_id_ == kProfilePms);
  if (!pms_like) {
    for (size_t i = 0U; i < filtered_samples.size(); ++i) {
      const TelemetrySample* s = filtered_samples[i];
      if (s == nullptr) continue;
      if (s->key == "wallv" || s->key == "battv" || s->key == "walli" ||
          s->key == "batti" || s->key == "psrc" || s->key == "trip" ||
          s->key == "rcut") {
        pms_like = true;
        break;
      }
    }
  }

  if (pms_like) {
    const std::vector<size_t> widths = {3U, 3U, 11U, 8U, 24U};
    const std::string border = tableBorder(widths);

    auto trimAscii = [](std::string text) -> std::string {
      size_t begin = 0U;
      while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
      }
      size_t end = text.size();
      while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
        --end;
      }
      return text.substr(begin, end - begin);
    };

    auto parseF64 = [&](const std::string& text, double& out) -> bool {
      const std::string cleaned = trimAscii(text);
      if (cleaned.empty()) return false;
      char* endp = nullptr;
      out = std::strtod(cleaned.c_str(), &endp);
      if (endp == cleaned.c_str()) return false;
      while (endp != nullptr && *endp != '\0') {
        if (std::isspace(static_cast<unsigned char>(*endp)) == 0) return false;
        ++endp;
      }
      return true;
    };

    auto parseU64 = [&](const std::string& text, uint64_t& out) -> bool {
      const std::string cleaned = trimAscii(text);
      if (cleaned.empty()) return false;
      char* endp = nullptr;
      const unsigned long long parsed = std::strtoull(cleaned.c_str(), &endp, 0);
      if (endp == cleaned.c_str()) return false;
      while (endp != nullptr && *endp != '\0') {
        if (std::isspace(static_cast<unsigned char>(*endp)) == 0) return false;
        ++endp;
      }
      out = static_cast<uint64_t>(parsed);
      return true;
    };

    auto parseBoolLike = [&](const std::string& text, bool& out) -> bool {
      const std::string lower = lowerAscii(trimAscii(text));
      if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
        out = true;
        return true;
      }
      if (lower == "0" || lower == "false" || lower == "off" || lower == "no") {
        out = false;
        return true;
      }
      return false;
    };

    auto writeRow = [&](const std::string& c0,
                        const std::string& c1,
                        const std::string& c2,
                        const std::string& c3,
                        const std::string& c4) {
      io_.writeln("| " + fitCell(c0, widths[0]) + " | " +
                  fitCell(c1, widths[1]) + " | " +
                  fitCell(c2, widths[2]) + " | " +
                  fitCell(c3, widths[3]) + " | " +
                  fitCell(c4, widths[4]) + " |");
    };

    std::unordered_map<std::string, const TelemetrySample*> sample_by_key{};
    sample_by_key.reserve(filtered_samples.size() * 2U + 8U);
    for (size_t i = 0U; i < filtered_samples.size(); ++i) {
      const TelemetrySample* s = filtered_samples[i];
      if (s == nullptr) continue;
      sample_by_key[s->key] = s;
    }

    auto findSample = [&](const std::string& key) -> const TelemetrySample* {
      const auto it = sample_by_key.find(key);
      return (it == sample_by_key.end()) ? nullptr : it->second;
    };

    auto formatFixed2 = [&](const TelemetrySample* sample) -> std::string {
      if (sample == nullptr) return "-";
      double value = 0.0;
      if (!parseF64(sample->value, value)) {
        return sample->value.empty() ? std::string("-") : sample->value;
      }
      char buf[48] = {0};
      std::snprintf(buf, sizeof(buf), "%.2f", value);
      return buf;
    };

    auto normalizeSource = [&](const TelemetrySample* sample) -> std::string {
      if (sample == nullptr) return "-";
      std::string src = lowerAscii(trimAscii(sample->value));
      if (src.empty()) return "-";
      if (src == "batt" || src == "bat" || src == "battery") return "battery";
      if (src == "wall" || src == "grid" || src == "ac" || src == "mains") return "wall";
      return src;
    };

    auto boolMetric = [&](const TelemetrySample* sample,
                          const char* true_note) -> std::pair<std::string, std::string> {
      if (sample == nullptr) return {"-", "-"};
      bool flag = false;
      if (!parseBoolLike(sample->value, flag)) {
        uint64_t parsed = 0ULL;
        if (parseU64(sample->value, parsed)) {
          flag = (parsed != 0ULL);
        } else {
          return {sample->value.empty() ? std::string("-") : sample->value, "-"};
        }
      }
      return {flag ? "1" : "0", flag ? std::string(true_note) : std::string("NORMAL")};
    };

    const TelemetrySample* wallv = findSample("wallv");
    const TelemetrySample* battv = findSample("battv");
    const TelemetrySample* walli = findSample("walli");
    const TelemetrySample* batti = findSample("batti");
    const TelemetrySample* psrc = findSample("psrc");
    const TelemetrySample* trip = findSample("trip");
    const TelemetrySample* rcut = findSample("rcut");

    const std::pair<std::string, std::string> trip_render = boolMetric(trip, "TRIPPED");
    const std::pair<std::string, std::string> rcut_render = boolMetric(rcut, "CUT");

    io_.writeln("");
    io_.writeln(border);
    writeRow("GRP", "IDX", "SIGNAL", "VALUE", "STATE / NOTE");
    io_.writeln(border);
    writeRow("SYS", "[0]", "wallv", formatFixed2(wallv), "V");
    writeRow("SYS", "[1]", "battv", formatFixed2(battv), "V");
    io_.writeln(border);
    writeRow("SYS", "[2]", "walli", formatFixed2(walli), "A");
    writeRow("SYS", "[3]", "batti", formatFixed2(batti), "A");
    io_.writeln(border);
    writeRow("SYS", "[4]", "psrc", normalizeSource(psrc), "selected source");
    io_.writeln(border);
    writeRow("SYS", "[5]", "trip", trip_render.first, trip_render.second);
    writeRow("SYS", "[6]", "rcut", rcut_render.first, rcut_render.second);
    io_.writeln(border);

    semu_telem_child_filter_active_ = false;
    return;
  }

  bool relay_like =
      (remote_profile_id_ == kProfileRelay) || (remote_profile_id_ == kProfileRemu);
  if (!relay_like) {
    for (size_t i = 0U; i < filtered_samples.size(); ++i) {
      const TelemetrySample* s = filtered_samples[i];
      if (s == nullptr) continue;
      if (s->key == "relay_bitmap" || s->key == "relay_count" || s->key == "uptime_ms" ||
          s->key == "env_temp_c" || s->key.find(".relay_bitmap") != std::string::npos) {
        relay_like = true;
        break;
      }
    }
  }

  if (relay_like) {
    const bool remu_profile = (remote_profile_id_ == kProfileRemu);
    const std::vector<size_t> widths = {3U, 4U, 20U, 8U, 24U};
    const std::string border = tableBorder(widths);
    std::string strong_border = border;
    for (size_t i = 0U; i < strong_border.size(); ++i) {
      if (strong_border[i] == '-') {
        strong_border[i] = '=';
      }
    }

    auto trimAscii = [](std::string text) -> std::string {
      size_t begin = 0U;
      while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
      }
      size_t end = text.size();
      while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
        --end;
      }
      return text.substr(begin, end - begin);
    };

    auto parseU64 = [&](const std::string& text, uint64_t& out) -> bool {
      const std::string cleaned = trimAscii(text);
      if (cleaned.empty()) return false;
      char* endp = nullptr;
      const unsigned long long parsed = std::strtoull(cleaned.c_str(), &endp, 0);
      if (endp == cleaned.c_str()) return false;
      while (endp != nullptr && *endp != '\0') {
        if (std::isspace(static_cast<unsigned char>(*endp)) == 0) {
          return false;
        }
        ++endp;
      }
      out = static_cast<uint64_t>(parsed);
      return true;
    };

    auto parseBoolLike = [&](const std::string& text, bool& out) -> bool {
      const std::string lower = lowerAscii(trimAscii(text));
      if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
        out = true;
        return true;
      }
      if (lower == "0" || lower == "false" || lower == "off" || lower == "no") {
        out = false;
        return true;
      }
      return false;
    };

    auto parseF64 = [&](const std::string& text, double& out) -> bool {
      const std::string cleaned = trimAscii(text);
      if (cleaned.empty()) return false;
      char* endp = nullptr;
      out = std::strtod(cleaned.c_str(), &endp);
      if (endp == cleaned.c_str()) return false;
      while (endp != nullptr && *endp != '\0') {
        if (std::isspace(static_cast<unsigned char>(*endp)) == 0) {
          return false;
        }
        ++endp;
      }
      return true;
    };

    auto writeRow = [&](const std::string& c0,
                        const std::string& c1,
                        const std::string& c2,
                        const std::string& c3,
                        const std::string& c4) {
      io_.writeln("| " + fitCell(c0, widths[0]) + " | " +
                  fitCell(c1, widths[1]) + " | " +
                  fitCell(c2, widths[2]) + " | " +
                  fitCell(c3, widths[3]) + " | " +
                  fitCell(c4, widths[4]) + " |");
    };

    std::unordered_map<std::string, const TelemetrySample*> sample_by_key{};
    sample_by_key.reserve(filtered_samples.size() * 2U + 8U);
    for (size_t i = 0U; i < filtered_samples.size(); ++i) {
      const TelemetrySample* s = filtered_samples[i];
      if (s == nullptr) continue;
      sample_by_key[s->key] = s;
    }

    auto findSample = [&](const std::string& key) -> const TelemetrySample* {
      const auto it = sample_by_key.find(key);
      return (it == sample_by_key.end()) ? nullptr : it->second;
    };

    uint64_t relay_mask = 0ULL;
    {
      const TelemetrySample* mask_sample = findSample("relay_bitmap");
      if (mask_sample != nullptr) {
        (void)parseU64(mask_sample->value, relay_mask);
      }
    }

    uint32_t relay_count = 0U;
    {
      const TelemetrySample* count_sample = findSample("relay_count");
      uint64_t parsed = 0ULL;
      if (count_sample != nullptr && parseU64(count_sample->value, parsed)) {
        relay_count = static_cast<uint32_t>(parsed);
      }
    }

    std::unordered_map<uint8_t, uint8_t> child_state{};
    child_state.reserve(16U);
    uint8_t max_child_seen = 0U;
    bool saw_child = false;
    for (size_t i = 0U; i < filtered_samples.size(); ++i) {
      const TelemetrySample* s = filtered_samples[i];
      if (s == nullptr) continue;
      uint8_t vid = 0U;
      if (!parseChildScopedTelemetryKey(s->key, vid)) continue;
      const size_t dot = s->key.find('.');
      if (dot == std::string::npos || dot + 1U >= s->key.size()) continue;
      const std::string suffix = s->key.substr(dot + 1U);
      if (suffix != "relay_bitmap") continue;

      uint8_t val = 0U;
      uint64_t parsed_num = 0ULL;
      if (parseU64(s->value, parsed_num)) {
        val = (parsed_num != 0ULL) ? 1U : 0U;
      } else {
        bool flag = false;
        if (parseBoolLike(s->value, flag)) {
          val = flag ? 1U : 0U;
        }
      }
      child_state[vid] = val;
      saw_child = true;
      if (vid > max_child_seen) max_child_seen = vid;
    }

    if (relay_count == 0U) {
      if (saw_child) {
        relay_count = static_cast<uint32_t>(max_child_seen) + 1U;
      } else if (remu_profile) {
        relay_count = 16U;
      } else {
        relay_count = 2U;
      }
    }
    if (relay_count > 32U) relay_count = 32U;

    const TelemetrySample* temp_sample = findSample("env_temp_c");
    const TelemetrySample* uptime_sample = findSample("uptime_ms");
    std::string temp_text = "-";
    if (temp_sample != nullptr) {
      double temp = 0.0;
      if (parseF64(temp_sample->value, temp)) {
        char buf[48] = {0};
        std::snprintf(buf, sizeof(buf), "%.3f", temp);
        temp_text = buf;
      } else {
        temp_text = temp_sample->value;
      }
    }
    std::string uptime_text = "-";
    if (uptime_sample != nullptr) {
      uint64_t uptime = 0ULL;
      if (parseU64(uptime_sample->value, uptime)) {
        uptime_text = std::to_string(static_cast<unsigned long long>(uptime));
      } else {
        uptime_text = uptime_sample->value;
      }
    }

    char mask_hex[32] = {0};
    if (relay_mask <= 0xFFFFULL) {
      std::snprintf(mask_hex, sizeof(mask_hex), "0x%04llX", static_cast<unsigned long long>(relay_mask));
    } else {
      std::snprintf(mask_hex, sizeof(mask_hex), "0x%08llX", static_cast<unsigned long long>(relay_mask));
    }

    io_.writeln("");
    io_.writeln(border);
    writeRow("GRP", "IDX", "SIGNAL", "VALUE", "STATE / NOTE");
    io_.writeln(border);
    writeRow("SYS", "[0]", "relay_mask", mask_hex, "raw system output mask");
    writeRow("SYS", "[1]", "relay_count", std::to_string(static_cast<unsigned long>(relay_count)), "total relay outputs");
    writeRow("SYS", "[2]", "env_temp_c", temp_text, "C");
    writeRow("SYS", "[3]", "uptime_ms", uptime_text, "ms");
    io_.writeln(strong_border);

    uint32_t start_vid = 0U;
    uint32_t end_vid = (relay_count == 0U) ? 0U : (relay_count - 1U);
    if (child_filter && remu_profile) {
      start_vid = static_cast<uint32_t>(child_vid);
      end_vid = static_cast<uint32_t>(child_vid);
    }

    uint32_t rows_printed = 0U;
    for (uint32_t vid = start_vid; vid <= end_vid && vid < 32U; ++vid) {
      uint8_t state = 0U;
      const auto it = child_state.find(static_cast<uint8_t>(vid));
      if (it != child_state.end()) {
        state = it->second ? 1U : 0U;
      } else {
        state = ((relay_mask >> vid) & 0x1ULL) ? 1U : 0U;
      }
      const std::string idx =
          std::string("[") + std::to_string(static_cast<unsigned long>(vid)) + "]";
      writeRow("V",
               idx,
               "relay_output_state",
               std::to_string(static_cast<unsigned int>(state)),
               state ? "ON" : "OFF");
      ++rows_printed;
      if ((rows_printed % 4U) == 0U || vid == end_vid || vid == 31U) {
        io_.writeln(border);
      }
    }

    semu_telem_child_filter_active_ = false;
    return;
  }

  if (filtered_samples.empty()) {
    io_.writeln("  (none)");
    semu_telem_child_filter_active_ = false;
    return;
  }

  if (child_filter) {
    writef("  filter: child=%u + global", static_cast<unsigned int>(child_vid));
  }
  uint32_t printed = 0U;
  for (size_t i = 0; i < filtered_samples.size(); ++i) {
    const auto* t = filtered_samples[i];
    if (t == nullptr) continue;
    ++printed;
    writef("  %u. id=0x%04X %s=%s %s",
           static_cast<unsigned int>(printed),
           static_cast<unsigned int>(t->metric_id),
           t->key.c_str(),
           t->value.c_str(),
           t->unit.c_str());
  }
  if (printed == 0U) {
    io_.writeln("  (none)");
  }
  semu_telem_child_filter_active_ = false;
}

void MasterCli::printLivenessDescriptorResponse(const DescriptorResponse& d) {
  const bool from_ping = (probe_pending_kind_ == ProbePendingKind::Ping);
  const bool from_live_cmd = (probe_pending_kind_ == ProbePendingKind::Live);
  if (from_ping) {
    const uint32_t rtt_ms = (probe_sent_ms_ == 0) ? 0 : (nowMs() - probe_sent_ms_);
    MacAddress resolved_peer{};
    const bool has_peer = resolveRuntimePeer(resolved_peer);
    const std::string peer_label = has_peer ? macToPrintable(resolved_peer) : std::string("unknown");
    writef("[MASTER][PING] pong peer=%s rtt_ms=%lu online=%s",
           peer_label.c_str(),
           static_cast<unsigned long>(rtt_ms),
           d.liveness.online ? "yes" : "no");
  }
  probe_pending_kind_ = ProbePendingKind::None;
  probe_sent_ms_ = 0;
  bool recovered = false;
  auto_pull_.onLivenessResponse(d.liveness.online, nowMs(), recovered);
  if (from_live_cmd) {
    writef("[MASTER][LIVE] online=%s uptime_ms=%lu state=%s",
           d.liveness.online ? "yes" : "no",
           static_cast<unsigned long>(d.liveness.uptime_ms),
           d.liveness.state.c_str());
  }
  if (recovered && from_live_cmd) {
    io_.writeln("[MASTER][LIVE] slave recovered");
  }
}

void MasterCli::printTimeDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][TIME] epoch_s=%llu uptime_ms=%lu",
         static_cast<unsigned long long>(d.time.epoch_s),
         static_cast<unsigned long>(d.time.uptime_ms));
}

void MasterCli::printSettingsDescriptorResponse(const DescriptorResponse& d) {
  const unsigned int total_settings =
      (d.total_count != 0U) ? static_cast<unsigned int>(d.total_count)
                            : static_cast<unsigned int>(d.settings.size());
  writef("[MASTER][SET] Settings snapshot=%lu source=%s total=%u",
         static_cast<unsigned long>(d.snapshot_id),
         descriptorSourceLabel(d),
         total_settings);
  if (d.message == "truncated") {
    writef("[MASTER][SET] note: truncated by payload limit (received=%u, expected=%u). use settings.full",
           static_cast<unsigned int>(d.settings.size()),
           static_cast<unsigned int>(remote_settings_count_));
  }
  if (d.settings.empty()) {
    io_.writeln("  (none)");
    return;
  }

  struct DisplayRow {
    std::string id;
    std::string key;
    std::string value;
    std::string default_value;
    std::string type;
    std::string rw;
    std::string notes;
  };

  const char* headers[7] = {"ID", "Key", "Value", "Default", "Type", "RW", "Range / Notes"};
  const size_t max_widths[7] = {6U, 22U, 20U, 20U, 6U, 2U, 32U};
  const SettingsSection order[] = {
      SettingsSection::General,
      SettingsSection::UiFeedback,
      SettingsSection::Protection,
      SettingsSection::PushRuntime,
      SettingsSection::Topology,
      SettingsSection::Provisioning,
      SettingsSection::Other,
  };

  auto printTable = [&](const char* title, const std::vector<DisplayRow>& rows) {
    io_.writeln("");
    writef("[%s]", title);

    std::vector<size_t> widths = {2U, 3U, 5U, 7U, 4U, 2U, 13U};
    for (size_t i = 0; i < 7U; ++i) {
      const size_t header_len = std::strlen(headers[i]);
      if (header_len > widths[i]) {
        widths[i] = header_len;
      }
    }
    for (const auto& row : rows) {
      const std::string cells[7] = {
          row.id, row.key, row.value, row.default_value, row.type, row.rw, row.notes};
      for (size_t i = 0; i < 7U; ++i) {
        size_t candidate = cells[i].size();
        if (candidate > max_widths[i]) {
          candidate = max_widths[i];
        }
        if (candidate > widths[i]) {
          widths[i] = candidate;
        }
      }
    }

    const std::string border = tableBorder(widths);
    io_.writeln(border);

    auto emitRow = [&](const std::string (&cells)[7]) {
      std::string line = "|";
      for (size_t i = 0; i < 7U; ++i) {
        line += " ";
        line += fitCell(cells[i], widths[i]);
        line += " |";
      }
      io_.writeln(line);
    };

    const std::string header_cells[7] = {headers[0],
                                         headers[1],
                                         headers[2],
                                         headers[3],
                                         headers[4],
                                         headers[5],
                                         headers[6]};
    emitRow(header_cells);
    io_.writeln(border);

    for (const auto& row : rows) {
      const std::string cells[7] = {row.id,
                                    row.key,
                                    row.value,
                                    row.default_value,
                                    row.type,
                                    row.rw,
                                    row.notes};
      emitRow(cells);
    }
    io_.writeln(border);
  };

  for (SettingsSection section : order) {
    std::vector<DisplayRow> rows{};
    rows.reserve(d.settings.size());
    for (const auto& s : d.settings) {
      if (classifySettingSection(s) != section) {
        continue;
      }
      char id_buf[16] = {0};
      std::snprintf(id_buf, sizeof(id_buf), "0x%04X", static_cast<unsigned int>(s.setting_id));
      std::string type = renderSettingType(s);
      std::string enum_text{};
      if (parseDescField(s.description, "enum", enum_text) && !enum_text.empty() && type != "bool") {
        type = "enum";
      }
      rows.push_back(DisplayRow{
          id_buf,
          s.key,
          renderSettingValue(s, s.current_value),
          renderSettingValue(s, s.default_value),
          type,
          s.writable ? "Y" : "N",
          renderSettingNotes(s),
      });
    }
    if (!rows.empty()) {
      printTable(sectionTitle(section), rows);
    }
  }
}

void MasterCli::printSingleSettingDescriptorResponse(const DescriptorResponse& d) {
  writef("[MASTER][DESC] setting (source=%s):", descriptorSourceLabel(d));
  printSettingLine(d.setting);
}

void MasterCli::printLogStatusDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][LOGGER][REMOTE] available=%s enabled=%s level=%u size=%lu dropped=%lu records=%lu rotations=%lu",
         d.logger_available ? "yes" : "no",
         d.logger_enabled ? "yes" : "no",
         static_cast<unsigned int>(d.logger_min_level),
         static_cast<unsigned long>(d.log_bytes_used),
         static_cast<unsigned long>(d.log_bytes_dropped),
         static_cast<unsigned long>(d.log_records_appended),
         static_cast<unsigned long>(d.log_rotations));
}

void MasterCli::printLogChunkDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][LOGGER][REMOTE] chunk offset=%lu total=%lu bytes=%u",
         static_cast<unsigned long>(d.log_chunk_offset),
         static_cast<unsigned long>(d.log_total_size),
         static_cast<unsigned int>(d.log_chunk.size()));

  if (d.log_chunk.empty()) {
    io_.writeln("  (empty)");
    return;
  }

  for (size_t i = 0; i < d.log_chunk.size(); i += 16U) {
    char linebuf[96] = {0};
    int p = std::snprintf(linebuf, sizeof(linebuf), "  %08lX: ",
                          static_cast<unsigned long>(d.log_chunk_offset + static_cast<uint32_t>(i)));
    const size_t end = std::min<size_t>(i + 16U, d.log_chunk.size());
    for (size_t j = i; j < end && p > 0 && static_cast<size_t>(p) < sizeof(linebuf); ++j) {
      p += std::snprintf(linebuf + p, sizeof(linebuf) - static_cast<size_t>(p), "%02X ", d.log_chunk[j]);
    }
    io_.writeln(std::string(linebuf));
  }
}

void MasterCli::printStorageInfoDescriptorResponse(const DescriptorResponse& d) const {
  const double total_mb = bytesToMb(d.storage_info.total_bytes);
  const double used_mb = bytesToMb(d.storage_info.used_bytes);
  const double free_mb = bytesToMb(d.storage_info.free_bytes);
  const double used_pct = (d.storage_info.total_bytes == 0U)
                              ? 0.0
                              : (100.0 * static_cast<double>(d.storage_info.used_bytes) /
                                 static_cast<double>(d.storage_info.total_bytes));

  auto extractNoteField = [](const std::string& note, const char* field) -> std::string {
    const std::string key = std::string(field) + "=";
    const size_t begin = note.find(key);
    if (begin == std::string::npos) {
      return std::string();
    }
    const size_t value_begin = begin + key.size();
    const size_t value_end = note.find(' ', value_begin);
    if (value_end == std::string::npos) {
      return note.substr(value_begin);
    }
    return note.substr(value_begin, value_end - value_begin);
  };

  auto toUpperAscii = [](std::string value) -> std::string {
    for (char& c : value) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
  };

  const char* backend = "UNKNOWN";
  switch (d.storage_info.mode) {
    case StorageBackendMode::Sd:
      backend = "SD CARD";
      break;
    case StorageBackendMode::Spiffs:
      backend = "SPIFFS";
      break;
    case StorageBackendMode::Disabled:
      backend = "DISABLED";
      break;
    case StorageBackendMode::Unknown:
    default:
      backend = "UNKNOWN";
      break;
  }
  const char* state = "UNAVAILABLE";
  if (d.storage_info.available) {
    state = d.storage_info.mounted ? "READY" : "NOT-MOUNTED";
  }
  std::string card_type = toUpperAscii(extractNoteField(d.message, "card_type"));
  if (card_type.empty()) {
    card_type = "UNKNOWN";
  }
  const std::string root = d.storage_info.root_path.empty() ? "/" : d.storage_info.root_path;
  const std::string cwd = d.storage_info.cwd.empty() ? "/" : d.storage_info.cwd;

  writef("[MASTER][STORAGE] %s | %s | %s | ROOT:%s | CWD:%s",
         backend,
         state,
         card_type.c_str(),
         root.c_str(),
         cwd.c_str());
  writef("[MASTER][STORAGE] FREE: %.2f MB | USED: %.2f/%.2f MB",
         free_mb,
         used_mb,
         total_mb);

  constexpr size_t kBarWidth = 20U;
  size_t used_slots = static_cast<size_t>((used_pct * static_cast<double>(kBarWidth) / 100.0) + 0.5);
  if (used_pct > 0.0 && used_slots == 0U) {
    used_slots = 1U;
  }
  if (used_slots > kBarWidth) {
    used_slots = kBarWidth;
  }
  const std::string bar = std::string(used_slots, '#') + std::string(kBarWidth - used_slots, '-');
  writef("[MASTER][STORAGE] USAGE [%s] %.2f%%", bar.c_str(), used_pct);
}

void MasterCli::printStorageListDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][STORAGE] list path=%s parent=%s count=%u",
         d.storage_path.c_str(),
         d.storage_parent_path.c_str(),
         static_cast<unsigned int>(d.storage_entries.size()));
  if (d.storage_entries.empty()) {
    io_.writeln("  (empty)");
    return;
  }
  for (size_t i = 0; i < d.storage_entries.size(); ++i) {
    const auto& e = d.storage_entries[i];
    writef("  %u. %s %s size=%lu",
           static_cast<unsigned int>(i + 1),
           e.is_dir ? "[D]" : "[F]",
           e.name.c_str(),
           static_cast<unsigned long>(e.size_bytes));
  }
}

void MasterCli::printStorageStatDescriptorResponse(const DescriptorResponse& d) {
  const std::string resolved_path = d.storage_stat.path.empty() ? d.storage_path : d.storage_stat.path;
  writef("[MASTER][STORAGE] stat path=%s exists=%s type=%s size=%lu",
         resolved_path.c_str(),
         d.storage_stat.exists ? "yes" : "no",
         d.storage_stat.is_dir ? "dir" : "file",
         static_cast<unsigned long>(d.storage_stat.size_bytes));

  if (!remote_storage_cd_pending_.empty()) {
    const std::string pending = remote_storage_cd_pending_;
    remote_storage_cd_pending_.clear();
    if (d.storage_stat.exists && d.storage_stat.is_dir) {
      remote_storage_cwd_ = resolved_path.empty() ? pending : resolved_path;
      writef("[MASTER][SD][REMOTE] cwd=%s", remote_storage_cwd_.c_str());
    } else {
      writef("[MASTER][SD][REMOTE] cd failed path=%s", pending.c_str());
    }
  }
}

void MasterCli::printOtaStatusDescriptorResponse(const DescriptorResponse& d) const {
  const auto& s = d.ota_status;
  writef("[MASTER][OTA] state=%s(%u) code=%s(0x%04X) size=%lu/%lu crc=0x%08lX/0x%08lX",
         otaTransferStateName(s.transfer_state),
         static_cast<unsigned int>(s.transfer_state),
         otaStatusCodeName(s.status_code),
         static_cast<unsigned int>(s.status_code),
         static_cast<unsigned long>(s.received_size),
         static_cast<unsigned long>(s.expected_size),
         static_cast<unsigned long>(s.actual_crc32),
         static_cast<unsigned long>(s.expected_crc32));
  if (!s.temp_path.empty() || !s.image_path.empty()) {
    writef("[MASTER][OTA] temp=%s image=%s",
           s.temp_path.empty() ? "-" : s.temp_path.c_str(),
           s.image_path.empty() ? "-" : s.image_path.c_str());
  }
  if (!s.persistent_state.empty() && s.persistent_state != "none") {
    writef("[MASTER][OTA] persisted=%s epoch=%lu sw=%s build=%s",
           s.persistent_state.c_str(),
           static_cast<unsigned long>(s.persistent_epoch_s),
           s.confirmed_sw_version.empty() ? "-" : s.confirmed_sw_version.c_str(),
           s.confirmed_build_id.empty() ? "-" : s.confirmed_build_id.c_str());
  }
  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printOtaManifestDescriptorResponse(const DescriptorResponse& d) const {
  if (d.is_paged) {
    writef("[MASTER][OTA] manifest page snapshot=%lu total=%u cursor=%u returned=%u next=%u done=%s",
           static_cast<unsigned long>(d.snapshot_id),
           static_cast<unsigned int>(d.total_count),
           static_cast<unsigned int>(d.cursor),
           static_cast<unsigned int>(d.returned_count),
           static_cast<unsigned int>(d.next_cursor),
           d.done ? "yes" : "no");
  } else {
    writef("[MASTER][OTA] manifest entries=%u", static_cast<unsigned int>(d.ota_manifest.size()));
  }

  if (d.ota_manifest.empty()) {
    io_.writeln("  (empty)");
    if (!d.message.empty()) {
      writef("[MASTER][OTA] note=%s", d.message.c_str());
    }
    return;
  }

  for (size_t i = 0; i < d.ota_manifest.size(); ++i) {
    const auto& e = d.ota_manifest[i];
    writef("  %u. id=%lu name=%s size=%luB crc=0x%08lX state=%s",
           static_cast<unsigned int>(i + 1),
           static_cast<unsigned long>(e.file_id),
           e.file_name.c_str(),
           static_cast<unsigned long>(e.size_bytes),
           static_cast<unsigned long>(e.crc32),
           e.state.c_str());
    writef("     version=%s build=%s created=%lu app_required=%luB",
           e.version.c_str(),
           e.build_id.c_str(),
           static_cast<unsigned long>(e.created_epoch_s),
           static_cast<unsigned long>(e.required_app_bytes));
  }

  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printOtaCapacityDescriptorResponse(const DescriptorResponse& d) const {
  const auto& c = d.ota_capacity;
  writef("[MASTER][OTA] capacity max_fw=%.2fMB last_image=%.2fMB fit=%s",
         bytesToMb(c.max_fw_bytes),
         bytesToMb(c.last_checked_image_bytes),
         c.last_fit ? "yes" : "no");
  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printOtaGateDescriptorResponse(const DescriptorResponse& d) const {
  const auto& g = d.ota_gate;
  writef("[MASTER][OTA] gate=%s(%u) detail=%s",
         otaGateDecisionName(g.decision),
         static_cast<unsigned int>(g.decision),
         g.detail.empty() ? "-" : g.detail.c_str());
  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printAckDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][DESC] ok: %s", d.message.c_str());
}

void MasterCli::printErrorDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][DESC] error: %s", d.message.c_str());
}

void MasterCli::printDescriptorResponse(const DescriptorResponse& d) {
  if (d.type == DescriptorResponseType::Device) {
    printDeviceDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Capabilities) {
    printCapabilitiesDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Telemetry) {
    printTelemetrySchemaDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::TelemetrySnapshot) {
    printTelemetrySnapshotDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Liveness) {
    printLivenessDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Time) {
    printTimeDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Settings) {
    printSettingsDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Setting) {
    printSingleSettingDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::LogStatus) {
    printLogStatusDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::LogChunk) {
    printLogChunkDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::StorageInfo) {
    printStorageInfoDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::StorageList) {
    printStorageListDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::StorageStat) {
    printStorageStatDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaStatus) {
    printOtaStatusDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaManifest) {
    printOtaManifestDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaCapacity) {
    printOtaCapacityDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaGateInfo) {
    printOtaGateDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Ack) {
    printAckDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Error) {
    printErrorDescriptorResponse(d);
  }
}

}  // namespace espnow_link
