/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Telemetry schema/snapshot/liveness/time render paths.
 **************************************************************/
#include "../internal/cli_render_internal.hpp"
#include "../internal/cli_render_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

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

}  // namespace espnow_link
