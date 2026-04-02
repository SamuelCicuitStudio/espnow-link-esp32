/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Shared parsers/helpers and runtime submit wrappers used by CLI dispatch paths.
 **************************************************************/
#include "../internal/cli_dispatch_internal.hpp"

namespace espnow_link {

using namespace cli_helpers;

namespace {

inline void yieldCliBusyWaitLoop() {
#if defined(ESP_PLATFORM)
  const TickType_t one_tick = pdMS_TO_TICKS(1U);
  vTaskDelay((one_tick > 0U) ? one_tick : 1U);
#elif defined(ARDUINO)
  delay(1U);
#endif
}

std::vector<std::string> splitTokens(const std::string& line) {
  std::vector<std::string> out;
  std::string token;
  for (char c : line) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      if (!token.empty()) {
        out.push_back(token);
        token.clear();
      }
      continue;
    }
    token.push_back(c);
  }
  if (!token.empty()) {
    out.push_back(token);
  }
  return out;
}

bool parseU32Token(const std::string& token, uint32_t& out) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const unsigned long v = std::strtoul(token.c_str(), &end, 0);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

bool parseU16Token(const std::string& token, uint16_t& out) {
  uint32_t tmp = 0;
  if (!parseU32Token(token, tmp) || tmp > 0xFFFFU) {
    return false;
  }
  out = static_cast<uint16_t>(tmp);
  return true;
}

bool parseFloatToken(const std::string& token, float& out) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const float v = std::strtof(token.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  out = v;
  return true;
}

bool parseBoolToken(const std::string& token, bool& out) {
  const std::string t = lowerCopy(trim(token));
  if (t == "1" || t == "true" || t == "yes" || t == "on") {
    out = true;
    return true;
  }
  if (t == "0" || t == "false" || t == "no" || t == "off") {
    out = false;
    return true;
  }
  return false;
}

bool isSupportedIcmCliBaud(uint32_t baud) {
  static constexpr uint32_t kSupported[] = {
    9600U, 19200U, 38400U, 57600U, 74880U, 115200U, 230400U, 250000U, 460800U, 921600U
  };
  for (uint32_t value : kSupported) {
    if (baud == value) {
      return true;
    }
  }
  return false;
}

const char* supportedIcmCliBaudList() {
  return "9600,19200,38400,57600,74880,115200,230400,250000,460800,921600";
}

bool loadPersistedIcmCliBaud(uint32_t& out_baud) {
  out_baud = static_cast<uint32_t>(PCAT_ICM_SET_CLIBD_DEF);
#if defined(ARDUINO)
  Preferences prefs;
  if (!prefs.begin(kSharedNvsNamespace, true, kSharedNvsPartition)) {
    return false;
  }
  if (prefs.isKey(PCAT_ICM_KEY_CLIBD)) {
    out_baud = prefs.getUInt(PCAT_ICM_KEY_CLIBD, out_baud);
  }
  prefs.end();
  return true;
#else
  return false;
#endif
}

bool savePersistedIcmCliBaud(uint32_t baud) {
#if defined(ARDUINO)
  Preferences prefs;
  if (!prefs.begin(kSharedNvsNamespace, false, kSharedNvsPartition)) {
    return false;
  }
  const bool ok = (prefs.putUInt(PCAT_ICM_KEY_CLIBD, baud) == sizeof(uint32_t));
  prefs.end();
  return ok;
#else
  (void)baud;
  return false;
#endif
}

uint8_t countEnabledSnapshotSlotsForVerify(const ManagementTopologySnapshotPayload& snapshot) {
  uint8_t count = 0U;
  for (const auto& slot : snapshot.slots) {
    if (slot.enabled) {
      ++count;
    }
  }
  return count;
}

uint8_t countEnabledSnapshotGroupsForVerify(const ManagementTopologySnapshotPayload& snapshot) {
  uint8_t count = 0U;
  for (const auto& group : snapshot.groups) {
    if (group.enabled) {
      ++count;
    }
  }
  return count;
}

std::string formatHex32ForVerify(uint32_t value) {
  char buf[11] = {0};
  std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(value));
  return std::string(buf);
}

uint32_t computeSnapshotChecksumForVerify(const ManagementTopologySnapshotPayload& snapshot) {
  std::array<ManagementTopologySlotPayload, kManagementTopologyMaxSlots> slots{};
  std::array<ManagementTopologyGroupSeedPayload, kManagementTopologyMaxGroups> groups{};
  for (uint8_t i = 0U; i < kManagementTopologyMaxSlots; ++i) {
    slots[i].slot_index = i;
  }
  for (uint8_t i = 0U; i < kManagementTopologyMaxGroups; ++i) {
    groups[i].group_slot = i;
  }

  for (const auto& slot : snapshot.slots) {
    if (slot.slot_index >= kManagementTopologyMaxSlots) {
      continue;
    }
    slots[slot.slot_index] = slot;
  }
  for (const auto& group : snapshot.groups) {
    if (group.group_slot >= kManagementTopologyMaxGroups) {
      continue;
    }
    groups[group.group_slot] = group;
  }

  const uint8_t enabled_slots = countEnabledSnapshotSlotsForVerify(snapshot);
  const uint8_t enabled_groups = countEnabledSnapshotGroupsForVerify(snapshot);

  uint32_t hash = 2166136261UL;
  auto mix8 = [&](uint8_t v) {
    hash ^= static_cast<uint32_t>(v);
    hash *= 16777619UL;
  };
  auto mix32 = [&](uint32_t v) {
    mix8(static_cast<uint8_t>(v & 0xFFU));
    mix8(static_cast<uint8_t>((v >> 8) & 0xFFU));
    mix8(static_cast<uint8_t>((v >> 16) & 0xFFU));
    mix8(static_cast<uint8_t>((v >> 24) & 0xFFU));
  };

  mix8(snapshot.schema_version);
  mix8(2U);  // committed state
  mix32(snapshot.topology_version);
  mix8(snapshot.index_neg);
  mix8(snapshot.index_pos);
  mix8(enabled_slots);
  mix8(enabled_groups);
  for (const auto& slot : slots) {
    mix8(static_cast<uint8_t>(slot.enabled ? 1U : 0U));
    for (uint8_t b : slot.peer) {
      mix8(b);
    }
    mix8(slot.peer_role);
    mix8(slot.group_id);
    mix8(static_cast<uint8_t>(slot.relative_index));
    mix8(slot.local_virtual_index);
    mix8(slot.peer_virtual_index);
    mix8(static_cast<uint8_t>(slot.axis_order));
    mix8(static_cast<uint8_t>(slot.delay_ms & 0xFFU));
    mix8(static_cast<uint8_t>((slot.delay_ms >> 8) & 0xFFU));
    mix8(static_cast<uint8_t>(slot.hold_ms & 0xFFU));
    mix8(static_cast<uint8_t>((slot.hold_ms >> 8) & 0xFFU));
  }
  for (const auto& group : groups) {
    mix8(static_cast<uint8_t>(group.enabled ? 1U : 0U));
    mix8(group.group_id);
    for (uint8_t b : group.seed) {
      mix8(b);
    }
  }
  return hash;
}

struct ParsedTopologyStatusForVerify {
  ManagementTopologyStatusPayload status{};
  bool committed_state_known = false;
  bool committed_groups_known = false;
  bool index_window_known = false;
  bool committed_checksum_known = false;
};

bool parseTopologyStatusAckMessageForVerify(const std::string& message,
                                            ParsedTopologyStatusForVerify& out) {
  out = ParsedTopologyStatusForVerify{};
  if (trim(message).empty()) {
    return false;
  }

  bool seen_any = false;
  bool seen_index_neg = false;
  bool seen_index_pos = false;
  std::string token{};
  std::vector<std::string> tokens{};
  tokens.reserve(16U);
  for (char c : message) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isspace(uc) != 0 || c == ',' || c == ';') {
      if (!token.empty()) {
        tokens.push_back(token);
        token.clear();
      }
      continue;
    }
    token.push_back(c);
  }
  if (!token.empty()) {
    tokens.push_back(token);
  }
  for (const std::string& raw : tokens) {
    const size_t eq = raw.find('=');
    if (eq == std::string::npos || eq == 0U || eq + 1U >= raw.size()) {
      continue;
    }
    const std::string key = lowerCopy(trim(raw.substr(0U, eq)));
    std::string value = trim(raw.substr(eq + 1U));
    while (!value.empty() && (value.back() == ',' || value.back() == ';')) {
      value.pop_back();
    }
    while (!value.empty() && (value.front() == ',' || value.front() == ';')) {
      value.erase(value.begin());
    }
    if (key.empty() || value.empty()) {
      continue;
    }

    bool parsed_bool = false;
    uint32_t parsed_u32 = 0U;
    if (key == "staged") {
      if (parseBoolToken(value, parsed_bool)) {
        out.status.has_staged = parsed_bool;
        seen_any = true;
      }
    } else if (key == "committed") {
      if (parseBoolToken(value, parsed_bool)) {
        out.status.has_committed = parsed_bool;
        seen_any = true;
      }
    } else if (key == "staged_ver") {
      if (parseU32Token(value, parsed_u32)) {
        out.status.staged_version = parsed_u32;
        seen_any = true;
      }
    } else if (key == "committed_ver") {
      if (parseU32Token(value, parsed_u32)) {
        out.status.committed_version = parsed_u32;
        seen_any = true;
      }
    } else if (key == "staged_slots") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.staged_slot_count = static_cast<uint8_t>(parsed_u32);
        seen_any = true;
      }
    } else if (key == "committed_slots") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.committed_slot_count = static_cast<uint8_t>(parsed_u32);
        seen_any = true;
      }
    } else if (key == "staged_groups") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.staged_group_count = static_cast<uint8_t>(parsed_u32);
        seen_any = true;
      }
    } else if (key == "committed_groups") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.committed_group_count = static_cast<uint8_t>(parsed_u32);
        out.committed_groups_known = true;
        seen_any = true;
      }
    } else if (key == "staged_state") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.staged_state = static_cast<uint8_t>(parsed_u32);
        seen_any = true;
      }
    } else if (key == "committed_state") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.committed_state = static_cast<uint8_t>(parsed_u32);
        out.committed_state_known = true;
        seen_any = true;
      }
    } else if (key == "index_neg") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.index_neg = static_cast<uint8_t>(parsed_u32);
        seen_index_neg = true;
        seen_any = true;
      }
    } else if (key == "index_pos") {
      if (parseU32Token(value, parsed_u32) && parsed_u32 <= 0xFFU) {
        out.status.index_pos = static_cast<uint8_t>(parsed_u32);
        seen_index_pos = true;
        seen_any = true;
      }
    } else if (key == "staged_checksum") {
      if (parseU32Token(value, parsed_u32)) {
        out.status.staged_checksum = parsed_u32;
        seen_any = true;
      }
    } else if (key == "committed_checksum") {
      if (parseU32Token(value, parsed_u32)) {
        out.status.committed_checksum = parsed_u32;
        out.committed_checksum_known = true;
        seen_any = true;
      }
    }
  }

  out.status.schema_version = 1U;
  if (!out.committed_state_known) {
    out.status.committed_state = out.status.has_committed ? 2U : 0U;
  }
  out.index_window_known = seen_index_neg && seen_index_pos;
  if (!out.committed_groups_known && out.status.committed_group_count > 0U) {
    out.committed_groups_known = true;
  }
  return seen_any;
}

bool parseHexPayload(const std::string& hex_text, std::vector<uint8_t>& out) {
  out.clear();
  std::string compact;
  compact.reserve(hex_text.size());
  for (char c : hex_text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isxdigit(uc) != 0) {
      compact.push_back(static_cast<char>(uc));
      continue;
    }
    if (std::isspace(uc) != 0 || c == ':' || c == '-' || c == ',') {
      continue;
    }
    return false;
  }
  if (compact.empty() || (compact.size() % 2U) != 0U) {
    return false;
  }
  out.reserve(compact.size() / 2U);
  auto hexNibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<int>(10 + c - 'a');
    if (c >= 'A' && c <= 'F') return static_cast<int>(10 + c - 'A');
    return -1;
  };
  for (size_t i = 0; i < compact.size(); i += 2U) {
    const int hi = hexNibble(compact[i]);
    const int lo = hexNibble(compact[i + 1U]);
    if (hi < 0 || lo < 0) {
      out.clear();
      return false;
    }
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return true;
}

bool readBinaryFileLocal(IOtaStorageBackend& storage,
                         const std::string& path,
                         std::vector<uint8_t>& out,
                         std::string& out_message) {
  out.clear();
  OtaStorageStat st{};
  std::string msg;
  if (!storage.stat(path, st, msg)) {
    out_message = msg.empty() ? "stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    out_message = "file missing";
    return false;
  }
  if (st.size_bytes > 4096U) {
    out_message = "file too large";
    return false;
  }
  out.resize(st.size_bytes, 0U);
  size_t out_len = 0U;
  if (!storage.readAt(path, 0U, out.data(), out.size(), out_len, msg)) {
    out.clear();
    out_message = msg.empty() ? "read failed" : msg;
    return false;
  }
  if (out_len == 0U) {
    out.clear();
    out_message = "empty file";
    return false;
  }
  out.resize(out_len);
  out_message = "ok";
  return true;
}

bool parsePushModeToken(const std::string& token, TelemetryPushMode& out) {
  const std::string mode = lowerCopy(trim(token));
  if (mode == "periodic") {
    out = TelemetryPushMode::Periodic;
    return true;
  }
  if (mode == "change" || mode == "on_change") {
    out = TelemetryPushMode::OnChange;
    return true;
  }
  if (mode == "hybrid") {
    out = TelemetryPushMode::Hybrid;
    return true;
  }
  return false;
}

std::string normalizeFsPath(const std::string& input) {
  if (input.empty()) {
    return "/";
  }
  std::vector<std::string> parts;
  std::string token;
  auto flush_token = [&]() {
    if (token.empty() || token == ".") {
      token.clear();
      return;
    }
    if (token == "..") {
      if (!parts.empty()) {
        parts.pop_back();
      }
      token.clear();
      return;
    }
    parts.push_back(token);
    token.clear();
  };

  for (char c : input) {
    const char normalized = (c == '\\') ? '/' : c;
    if (normalized == '/') {
      flush_token();
      continue;
    }
    token.push_back(normalized);
  }
  flush_token();

  std::string out = "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out.push_back('/');
    }
    out += parts[i];
  }
  return out;
}

std::string resolveFsPath(const std::string& cwd, const std::string& path) {
  const std::string cleaned = trim(path);
  if (cleaned.empty()) {
    return normalizeFsPath(cwd);
  }
  if (!cleaned.empty() && cleaned[0] == '/') {
    return normalizeFsPath(cleaned);
  }
  return normalizeFsPath(normalizeFsPath(cwd) + "/" + cleaned);
}

std::string parentFsPath(const std::string& path) {
  const std::string n = normalizeFsPath(path);
  if (n == "/") {
    return n;
  }
  const size_t pos = n.find_last_of('/');
  if (pos == std::string::npos || pos == 0U) {
    return "/";
  }
  return n.substr(0, pos);
}

struct OtaArchiveEntryLocal {
  std::string id;
  std::string bin_name;
  std::string meta_name;
  uint32_t size_bytes = 0;
  uint32_t crc32 = 0;
  std::string sw_version;
  std::string build_id;
  std::string target_role;
  std::string source;
  uint32_t created_epoch_s = 0;
};

bool extractJsonStringValue(const std::string& json, const char* key, std::string& out_value) {
  out_value.clear();
  if (key == nullptr || key[0] == '\0') {
    return false;
  }
  const std::string pattern = std::string("\"") + key + "\"";
  const size_t key_pos = json.find(pattern);
  if (key_pos == std::string::npos) {
    return false;
  }
  size_t pos = json.find(':', key_pos + pattern.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return false;
  }
  ++pos;
  std::string out;
  out.reserve(24U);
  bool escaped = false;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (escaped) {
      out.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      out_value = trim(out);
      return !out_value.empty();
    }
    out.push_back(c);
  }
  return false;
}

bool extractJsonU32Value(const std::string& json, const char* key, uint32_t& out_value) {
  out_value = 0U;
  std::string tmp;
  if (!extractJsonStringValue(json, key, tmp)) {
    const std::string pattern = std::string("\"") + key + "\"";
    const size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos) {
      return false;
    }
    size_t pos = json.find(':', key_pos + pattern.size());
    if (pos == std::string::npos) {
      return false;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
      ++pos;
    }
    if (pos >= json.size()) {
      return false;
    }
    size_t end = pos;
    while (end < json.size() &&
           json[end] != ',' &&
           json[end] != '}' &&
           std::isspace(static_cast<unsigned char>(json[end])) == 0) {
      ++end;
    }
    tmp = trim(json.substr(pos, end - pos));
    if (tmp.empty()) {
      return false;
    }
  }
  char* endp = nullptr;
  const unsigned long v = std::strtoul(tmp.c_str(), &endp, 0);
  if (endp == nullptr || *endp != '\0') {
    return false;
  }
  out_value = static_cast<uint32_t>(v);
  return true;
}

bool extractJsonI32Value(const std::string& json, const char* key, int32_t& out_value) {
  out_value = 0;
  std::string tmp;
  if (!extractJsonStringValue(json, key, tmp)) {
    const std::string pattern = std::string("\"") + key + "\"";
    const size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos) {
      return false;
    }
    size_t pos = json.find(':', key_pos + pattern.size());
    if (pos == std::string::npos) {
      return false;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
      ++pos;
    }
    if (pos >= json.size()) {
      return false;
    }
    size_t end = pos;
    while (end < json.size() &&
           json[end] != ',' &&
           json[end] != '}' &&
           std::isspace(static_cast<unsigned char>(json[end])) == 0) {
      ++end;
    }
    tmp = trim(json.substr(pos, end - pos));
    if (tmp.empty()) {
      return false;
    }
  }
  char* endp = nullptr;
  const long v = std::strtol(tmp.c_str(), &endp, 0);
  if (endp == nullptr || *endp != '\0') {
    return false;
  }
  out_value = static_cast<int32_t>(v);
  return true;
}

bool extractJsonArraySlice(const std::string& json, const char* key, std::string& out_slice) {
  out_slice.clear();
  if (key == nullptr || key[0] == '\0') {
    return false;
  }
  const std::string pattern = std::string("\"") + key + "\"";
  const size_t key_pos = json.find(pattern);
  if (key_pos == std::string::npos) {
    return false;
  }
  size_t pos = json.find(':', key_pos + pattern.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != '[') {
    return false;
  }

  const size_t begin = pos;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (; pos < json.size(); ++pos) {
    const char c = json[pos];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '[') {
      ++depth;
      continue;
    }
    if (c == ']') {
      --depth;
      if (depth == 0) {
        out_slice = json.substr(begin + 1U, pos - begin - 1U);
        return true;
      }
      if (depth < 0) {
        return false;
      }
      continue;
    }
  }
  return false;
}

bool parseJsonU32Array(const std::string& json, const char* key, std::vector<uint32_t>& out_values) {
  out_values.clear();
  std::string slice;
  if (!extractJsonArraySlice(json, key, slice)) {
    return false;
  }
  size_t cursor = 0U;
  while (cursor < slice.size()) {
    size_t next = slice.find(',', cursor);
    if (next == std::string::npos) {
      next = slice.size();
    }
    const std::string token = trim(slice.substr(cursor, next - cursor));
    cursor = (next == slice.size()) ? slice.size() : (next + 1U);
    if (token.empty()) {
      continue;
    }
    char* endp = nullptr;
    const unsigned long v = std::strtoul(token.c_str(), &endp, 0);
    if (endp == nullptr || *endp != '\0') {
      return false;
    }
    out_values.push_back(static_cast<uint32_t>(v));
  }
  return !out_values.empty();
}

bool splitJsonArrayObjects(const std::string& array_slice, std::vector<std::string>& out_objects) {
  out_objects.clear();
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  size_t obj_start = std::string::npos;
  for (size_t i = 0; i < array_slice.size(); ++i) {
    const char c = array_slice[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '{') {
      if (depth == 0) {
        obj_start = i;
      }
      ++depth;
      continue;
    }
    if (c == '}') {
      if (depth <= 0) {
        return false;
      }
      --depth;
      if (depth == 0 && obj_start != std::string::npos) {
        out_objects.push_back(array_slice.substr(obj_start, i - obj_start + 1U));
        obj_start = std::string::npos;
      }
      continue;
    }
  }
  return !out_objects.empty() && depth == 0;
}

constexpr uint8_t kChainSeedMaxGroups = 12U;
constexpr uint8_t kChainPhysicalMaxDevices = 14U;
constexpr uint8_t kChainSecureLinksPerPhysical = 14U;
constexpr uint8_t kChainLateralLinksPerPhysical =
    static_cast<uint8_t>(kChainSecureLinksPerPhysical - 1U);
constexpr uint8_t kChainAbsoluteMaxSlots = kChainLateralLinksPerPhysical;

enum class ChainNodeType : uint8_t {
  Sensor = 0,
  Relay = 1,
  SemuChild = 2,
  RemuChild = 3,
};

struct ChainNode {
  ChainNodeType type = ChainNodeType::Sensor;
  MacAddress mac{};
  int32_t virtual_index = -1;
  size_t chain_index = 0U;
};

struct ChainRelayGroup {
  uint8_t group_id = 0U;
  size_t left_separator = 0U;
  size_t right_separator = 0U;
  std::vector<size_t> relay_nodes{};
  std::array<uint8_t, 32> seed{};
};

struct ChainLink {
  size_t left = 0U;
  size_t right = 0U;
  uint8_t group_id = 0U;
};

struct PendingChainSlot {
  MacAddress peer{};
  uint8_t peer_role = 0U;
  uint8_t group_id = 0U;
  uint8_t local_virtual_index = 0xFFU;
  uint8_t peer_virtual_index = 0xFFU;
  int32_t delta = 0;
  int32_t peer_chain_index = 0;
};

struct TopologyDeployTarget {
  MacAddress target{};
  ManagementTopologySnapshotPayload snapshot{};
  std::vector<MacAddress> peers{};
};

struct TopologyChainPlanDebug {
  std::vector<ChainNode> nodes{};
  std::vector<ChainRelayGroup> groups{};
  std::vector<uint32_t> seed_u32{};
};

bool parseChainNodeType(const std::string& token, ChainNodeType& out_type) {
  const std::string t = lowerCopy(trim(token));
  if (t == "s") {
    out_type = ChainNodeType::Sensor;
    return true;
  }
  if (t == "r") {
    out_type = ChainNodeType::Relay;
    return true;
  }
  if (t == "sm") {
    out_type = ChainNodeType::SemuChild;
    return true;
  }
  if (t == "rm") {
    out_type = ChainNodeType::RemuChild;
    return true;
  }
  return false;
}

bool isChainSeparator(ChainNodeType type) {
  return type == ChainNodeType::Sensor || type == ChainNodeType::SemuChild;
}

bool isChainRelay(ChainNodeType type) {
  return type == ChainNodeType::Relay || type == ChainNodeType::RemuChild;
}

uint8_t chainTargetSlotCap(ChainNodeType type) {
  (void)type;
  return kChainLateralLinksPerPhysical;
}

uint8_t chainRoleCode(ChainNodeType type) {
  switch (type) {
    case ChainNodeType::Sensor:
      return static_cast<uint8_t>(kProfileSens & 0xFFU);
    case ChainNodeType::SemuChild:
      return static_cast<uint8_t>(kProfileSemu & 0xFFU);
    case ChainNodeType::Relay:
      return static_cast<uint8_t>(kProfileRelay & 0xFFU);
    case ChainNodeType::RemuChild:
      return static_cast<uint8_t>(kProfileRemu & 0xFFU);
  }
  return static_cast<uint8_t>(kProfilePms & 0xFFU);
}

uint8_t encodeVirtualIndex(int32_t vi) {
  if (vi < 0) {
    return 0xFFU;
  }
  if (vi > 0xFF) {
    return 0xFFU;
  }
  return static_cast<uint8_t>(vi & 0xFF);
}

std::array<uint8_t, 32> expandSeedU32ToSeedBytes(uint32_t seed_u32, uint8_t group_id) {
  std::array<uint8_t, 32> out{};
  uint32_t state = seed_u32 ^ 0x9E3779B9UL ^ (static_cast<uint32_t>(group_id) * 0x45D9F3BUL);
  if (state == 0U) {
    state = 0xA5B35705UL;
  }
  for (size_t i = 0; i < out.size(); ++i) {
    state ^= (state << 13);
    state ^= (state >> 17);
    state ^= (state << 5);
    out[i] = static_cast<uint8_t>(state & 0xFFU);
  }
  bool all_zero = true;
  for (uint8_t b : out) {
    if (b != 0U) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    out[0] = 0xA5U;
  }
  return out;
}

bool parseTopologyChainJson(const std::string& json_text,
                            uint32_t default_topology_version,
                            std::vector<TopologyDeployTarget>& out_targets,
                            uint32_t& out_topology_version,
                            std::string& out_error,
                            TopologyChainPlanDebug* out_debug) {
  out_targets.clear();
  out_error.clear();
  out_topology_version = default_topology_version;
  if (out_debug != nullptr) {
    out_debug->nodes.clear();
    out_debug->groups.clear();
    out_debug->seed_u32.clear();
  }

  uint32_t schema_version = 0U;
  if (!extractJsonU32Value(json_text, "v", schema_version) || schema_version != 2U) {
    out_error = "schema version invalid (expected v=2)";
    return false;
  }

  std::vector<uint32_t> seeds{};
  if (!parseJsonU32Array(json_text, "seed", seeds)) {
    out_error = "seed array missing or invalid";
    return false;
  }

  uint32_t requested_topology_version = 0U;
  if (extractJsonU32Value(json_text, "topo_ver", requested_topology_version) &&
      requested_topology_version != 0U) {
    out_topology_version = requested_topology_version;
  }
  if (out_topology_version == 0U) {
    out_topology_version = 1U;
  }

  std::string chain_slice;
  if (!extractJsonArraySlice(json_text, "chain", chain_slice)) {
    out_error = "chain array missing";
    return false;
  }
  std::vector<std::string> chain_items{};
  if (!splitJsonArrayObjects(chain_slice, chain_items)) {
    out_error = "chain array invalid";
    return false;
  }

  std::vector<ChainNode> nodes{};
  nodes.reserve(chain_items.size());
  std::vector<MacAddress> sensor_macs{};
  std::vector<MacAddress> relay_macs{};
  std::vector<MacAddress> semu_parent_macs{};
  std::vector<MacAddress> remu_parent_macs{};
  std::map<MacAddress, std::array<bool, 8>> semu_vi_seen{};
  std::map<MacAddress, std::array<bool, 16>> remu_vi_seen{};

  auto containsMac = [](const std::vector<MacAddress>& list, const MacAddress& mac) -> bool {
    return std::find(list.begin(), list.end(), mac) != list.end();
  };

  for (size_t i = 0; i < chain_items.size(); ++i) {
    ChainNode node{};
    std::string type_token;
    std::string mac_token;
    int32_t vi = -1;
    if (!extractJsonStringValue(chain_items[i], "t", type_token) ||
        !extractJsonStringValue(chain_items[i], "m", mac_token) ||
        !extractJsonI32Value(chain_items[i], "vi", vi)) {
      out_error = "chain node missing t/m/vi";
      return false;
    }
    if (!parseChainNodeType(type_token, node.type)) {
      out_error = "chain node type invalid";
      return false;
    }
    if (!parseMac(mac_token, node.mac)) {
      out_error = "chain node mac invalid";
      return false;
    }
    node.virtual_index = vi;
    node.chain_index = i;
    if ((node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) && node.virtual_index != -1) {
      out_error = "physical node vi must be -1";
      return false;
    }
    if (node.type == ChainNodeType::SemuChild &&
        (node.virtual_index < 0 || node.virtual_index > 7)) {
      out_error = "semu vi out of range (0..7)";
      return false;
    }
    if (node.type == ChainNodeType::RemuChild &&
        (node.virtual_index < 0 || node.virtual_index > 15)) {
      out_error = "remu vi out of range (0..15)";
      return false;
    }

    switch (node.type) {
      case ChainNodeType::Sensor: {
        if (containsMac(sensor_macs, node.mac)) {
          out_error = "sensor mac duplicated in chain";
          return false;
        }
        if (containsMac(relay_macs, node.mac) ||
            containsMac(semu_parent_macs, node.mac) ||
            containsMac(remu_parent_macs, node.mac)) {
          out_error = "sensor mac overlaps relay/semu/remu mac";
          return false;
        }
        sensor_macs.push_back(node.mac);
      } break;
      case ChainNodeType::Relay: {
        if (containsMac(relay_macs, node.mac)) {
          out_error = "relay mac duplicated in chain";
          return false;
        }
        if (containsMac(sensor_macs, node.mac) ||
            containsMac(semu_parent_macs, node.mac) ||
            containsMac(remu_parent_macs, node.mac)) {
          out_error = "relay mac overlaps sensor/semu/remu mac";
          return false;
        }
        relay_macs.push_back(node.mac);
      } break;
      case ChainNodeType::SemuChild: {
        if (containsMac(sensor_macs, node.mac) ||
            containsMac(relay_macs, node.mac) ||
            containsMac(remu_parent_macs, node.mac)) {
          out_error = "semu parent mac overlaps sensor/relay/remu mac";
          return false;
        }
        if (!containsMac(semu_parent_macs, node.mac)) {
          semu_parent_macs.push_back(node.mac);
        }
        auto& seen = semu_vi_seen[node.mac];
        if (seen[static_cast<size_t>(node.virtual_index)]) {
          out_error = "semu child vi duplicated for same mac";
          return false;
        }
        seen[static_cast<size_t>(node.virtual_index)] = true;
      } break;
      case ChainNodeType::RemuChild: {
        if (containsMac(sensor_macs, node.mac) ||
            containsMac(relay_macs, node.mac) ||
            containsMac(semu_parent_macs, node.mac)) {
          out_error = "remu parent mac overlaps sensor/relay/semu mac";
          return false;
        }
        if (!containsMac(remu_parent_macs, node.mac)) {
          remu_parent_macs.push_back(node.mac);
        }
        auto& seen = remu_vi_seen[node.mac];
        if (seen[static_cast<size_t>(node.virtual_index)]) {
          out_error = "remu child vi duplicated for same mac";
          return false;
        }
        seen[static_cast<size_t>(node.virtual_index)] = true;
      } break;
    }

    nodes.push_back(node);
  }

  const size_t physical_device_count =
      sensor_macs.size() + relay_macs.size() + semu_parent_macs.size() + remu_parent_macs.size();
  if (physical_device_count > kChainPhysicalMaxDevices) {
    out_error = "physical device count exceeds 14";
    return false;
  }

  if (nodes.size() < 3U) {
    out_error = "chain too short";
    return false;
  }
  if (!isChainSeparator(nodes.front().type) || !isChainSeparator(nodes.back().type)) {
    out_error = "chain must start and end with S/SM";
    return false;
  }
  for (size_t i = 1; i < nodes.size(); ++i) {
    if (isChainSeparator(nodes[i - 1U].type) && isChainSeparator(nodes[i].type)) {
      out_error = "adjacent separator nodes are not allowed";
      return false;
    }
  }

  std::vector<ChainRelayGroup> groups{};
  for (size_t i = 0; i < nodes.size();) {
    if (isChainSeparator(nodes[i].type)) {
      ++i;
      continue;
    }
    if (i == 0U || i + 1U >= nodes.size()) {
      out_error = "relay block boundary invalid";
      return false;
    }
    if (!isChainSeparator(nodes[i - 1U].type)) {
      out_error = "relay block missing left separator";
      return false;
    }
    const size_t left_sep = i - 1U;
    const size_t start = i;
    while (i < nodes.size() && isChainRelay(nodes[i].type)) {
      ++i;
    }
    if (i >= nodes.size() || !isChainSeparator(nodes[i].type)) {
      out_error = "relay block missing right separator";
      return false;
    }
    ChainRelayGroup group{};
    group.group_id = static_cast<uint8_t>(groups.size() + 1U);
    group.left_separator = left_sep;
    group.right_separator = i;
    for (size_t n = start; n < i; ++n) {
      group.relay_nodes.push_back(n);
    }
    groups.push_back(group);
  }

  if (groups.empty()) {
    out_error = "no relay blocks in chain";
    return false;
  }
  if (groups.size() > kChainSeedMaxGroups) {
    out_error = "relay block count exceeds 12";
    return false;
  }
  if (seeds.size() != groups.size()) {
    out_error = "seed count must match relay block count";
    return false;
  }
  for (size_t i = 0; i < groups.size(); ++i) {
    groups[i].seed = expandSeedU32ToSeedBytes(seeds[i], groups[i].group_id);
  }

  std::vector<ChainLink> links{};
  for (const auto& group : groups) {
    for (size_t relay_idx : group.relay_nodes) {
      links.push_back({group.left_separator, relay_idx, group.group_id});
      links.push_back({group.right_separator, relay_idx, group.group_id});
    }
  }

  struct TargetBuilder {
    MacAddress mac{};
    ChainNodeType local_type = ChainNodeType::Sensor;
    std::vector<PendingChainSlot> slots{};
  };
  std::vector<TargetBuilder> builders{};
  auto findBuilder = [&](const MacAddress& mac, ChainNodeType local_type) -> TargetBuilder* {
    for (auto& b : builders) {
      if (b.mac == mac) {
        b.local_type = local_type;
        return &b;
      }
    }
    builders.push_back(TargetBuilder{});
    builders.back().mac = mac;
    builders.back().local_type = local_type;
    return &builders.back();
  };

  auto upsertSlot = [&](const ChainNode& local_node,
                        const ChainNode& peer_node,
                        uint8_t group_id) {
    if (local_node.mac == peer_node.mac &&
        local_node.virtual_index == peer_node.virtual_index &&
        local_node.type == peer_node.type) {
      return;
    }
    PendingChainSlot candidate{};
    candidate.peer = peer_node.mac;
    candidate.peer_role = chainRoleCode(peer_node.type);
    candidate.group_id = group_id;
    candidate.local_virtual_index = encodeVirtualIndex(local_node.virtual_index);
    candidate.peer_virtual_index = encodeVirtualIndex(peer_node.virtual_index);
    candidate.delta = static_cast<int32_t>(peer_node.chain_index) -
                      static_cast<int32_t>(local_node.chain_index);
    candidate.peer_chain_index = static_cast<int32_t>(peer_node.chain_index);
    if (candidate.delta == 0) {
      return;
    }

    TargetBuilder* builder = findBuilder(local_node.mac, local_node.type);
    auto same_logical_peer = std::find_if(builder->slots.begin(),
                                          builder->slots.end(),
                                          [&](const PendingChainSlot& s) {
                                            return s.peer == candidate.peer &&
                                                   s.peer_role == candidate.peer_role &&
                                                   s.local_virtual_index == candidate.local_virtual_index &&
                                                   s.peer_virtual_index == candidate.peer_virtual_index;
                                          });
    if (same_logical_peer == builder->slots.end()) {
      builder->slots.push_back(candidate);
      return;
    }
    const int existing_abs = std::abs(same_logical_peer->delta);
    const int candidate_abs = std::abs(candidate.delta);
    if (candidate_abs < existing_abs) {
      *same_logical_peer = candidate;
      return;
    }
    if (candidate_abs == existing_abs &&
        same_logical_peer->local_virtual_index == 0xFFU &&
        candidate.local_virtual_index != 0xFFU) {
      same_logical_peer->local_virtual_index = candidate.local_virtual_index;
      same_logical_peer->group_id = candidate.group_id;
    }
  };

  for (const auto& link : links) {
    const ChainNode& a = nodes[link.left];
    const ChainNode& b = nodes[link.right];
    upsertSlot(a, b, link.group_id);
    upsertSlot(b, a, link.group_id);
  }

  auto findGroupSeed = [&](uint8_t group_id, std::array<uint8_t, 32>& out_seed) -> bool {
    for (const auto& g : groups) {
      if (g.group_id == group_id) {
        out_seed = g.seed;
        return true;
      }
    }
    return false;
  };

  for (const auto& builder : builders) {
    if (builder.slots.empty()) {
      continue;
    }
    std::vector<PendingChainSlot> negatives{};
    std::vector<PendingChainSlot> positives{};
    negatives.reserve(builder.slots.size());
    positives.reserve(builder.slots.size());
    for (const auto& s : builder.slots) {
      if (s.delta < 0) {
        negatives.push_back(s);
      } else if (s.delta > 0) {
        positives.push_back(s);
      }
    }
    auto slotSorter = [](const PendingChainSlot& a, const PendingChainSlot& b) {
      const int a_abs = std::abs(a.delta);
      const int b_abs = std::abs(b.delta);
      if (a_abs != b_abs) return a_abs < b_abs;
      if (a.peer_chain_index != b.peer_chain_index) return a.peer_chain_index < b.peer_chain_index;
      if (a.peer != b.peer) return a.peer < b.peer;
      if (a.peer_role != b.peer_role) return a.peer_role < b.peer_role;
      return a.peer_virtual_index < b.peer_virtual_index;
    };
    std::sort(negatives.begin(), negatives.end(), slotSorter);
    std::sort(positives.begin(), positives.end(), slotSorter);

    const uint8_t role_slot_cap = chainTargetSlotCap(builder.local_type);
    if (negatives.size() > kChainAbsoluteMaxSlots ||
        positives.size() > kChainAbsoluteMaxSlots ||
        (negatives.size() + positives.size()) > role_slot_cap) {
      out_error = "target slot count exceeds lateral cap " + std::to_string(role_slot_cap);
      return false;
    }

    TopologyDeployTarget target{};
    target.target = builder.mac;
    target.snapshot.schema_version = 1U;
    target.snapshot.topology_version = out_topology_version;
    target.snapshot.index_neg = static_cast<uint8_t>(negatives.size());
    target.snapshot.index_pos = static_cast<uint8_t>(positives.size());
    target.snapshot.slots.reserve(negatives.size() + positives.size());

    std::vector<PendingChainSlot> ordered{};
    ordered.reserve(negatives.size() + positives.size());
    ordered.insert(ordered.end(), negatives.begin(), negatives.end());
    ordered.insert(ordered.end(), positives.begin(), positives.end());

    std::vector<uint8_t> used_group_ids{};
    used_group_ids.reserve(ordered.size());
    for (size_t i = 0; i < ordered.size(); ++i) {
      const PendingChainSlot& s = ordered[i];
      ManagementTopologySlotPayload slot{};
      slot.slot_index = static_cast<uint8_t>(i & 0xFFU);
      slot.enabled = true;
      slot.peer = s.peer;
      slot.peer_role = s.peer_role;
      slot.group_id = s.group_id;
      if (i < negatives.size()) {
        slot.relative_index = static_cast<int8_t>(-static_cast<int>(i + 1U));
      } else {
        slot.relative_index = static_cast<int8_t>(static_cast<int>(i - negatives.size() + 1U));
      }
      slot.local_virtual_index = s.local_virtual_index;
      slot.peer_virtual_index = s.peer_virtual_index;
      slot.axis_order = slot.relative_index;
      slot.delay_ms = 0U;
      slot.hold_ms = 0U;
      target.snapshot.slots.push_back(slot);
      if (std::find(used_group_ids.begin(), used_group_ids.end(), s.group_id) == used_group_ids.end()) {
        used_group_ids.push_back(s.group_id);
      }
      if (std::find(target.peers.begin(), target.peers.end(), s.peer) == target.peers.end()) {
        target.peers.push_back(s.peer);
      }
    }

    std::sort(used_group_ids.begin(), used_group_ids.end());
    if (used_group_ids.size() > kChainSeedMaxGroups) {
      out_error = "target group count exceeds 12";
      return false;
    }
    target.snapshot.groups.reserve(used_group_ids.size());
    for (size_t i = 0; i < used_group_ids.size(); ++i) {
      std::array<uint8_t, 32> seed{};
      if (!findGroupSeed(used_group_ids[i], seed)) {
        out_error = "group seed missing for slot";
        return false;
      }
      ManagementTopologyGroupSeedPayload group{};
      group.group_slot = static_cast<uint8_t>(i & 0xFFU);
      group.enabled = true;
      group.group_id = used_group_ids[i];
      group.seed = seed;
      target.snapshot.groups.push_back(group);
    }
    out_targets.push_back(target);
  }

  if (out_targets.empty()) {
    out_error = "no deploy targets produced from chain";
    return false;
  }
  if (out_debug != nullptr) {
    out_debug->nodes = nodes;
    out_debug->groups = groups;
    out_debug->seed_u32 = seeds;
  }
  return true;
}

bool readTextFileLocal(IOtaStorageBackend& storage,
                       const std::string& path,
                       std::string& out_text,
                       std::string& out_message) {
  out_text.clear();
  OtaStorageStat st{};
  std::string msg;
  if (!storage.stat(path, st, msg)) {
    out_message = msg.empty() ? "stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir) {
    out_message = "file missing";
    return false;
  }
  if (st.size_bytes == 0U) {
    out_message = "empty file";
    return false;
  }
  if (st.size_bytes > 16384U) {
    out_message = "file too large";
    return false;
  }
  std::vector<uint8_t> buf(st.size_bytes, 0U);
  size_t out_len = 0U;
  if (!storage.readAt(path, 0U, buf.data(), buf.size(), out_len, msg)) {
    out_message = msg.empty() ? "read failed" : msg;
    return false;
  }
  out_text.assign(reinterpret_cast<const char*>(buf.data()),
                  reinterpret_cast<const char*>(buf.data() + out_len));
  out_message = "ok";
  return true;
}

bool writeTextFileLocal(IOtaStorageBackend& storage,
                        const std::string& path,
                        const std::string& text,
                        std::string& out_message) {
  std::string msg;
  const std::string parent = parentFsPath(path);
  if (!storage.ensureDir(parent, msg)) {
    out_message = msg.empty() ? "mkdir failed" : msg;
    return false;
  }
  if (!storage.truncateFile(path, msg)) {
    out_message = msg.empty() ? "truncate failed" : msg;
    return false;
  }
  if (!text.empty() &&
      !storage.writeAt(path, 0U, reinterpret_cast<const uint8_t*>(text.data()), text.size(), msg)) {
    out_message = msg.empty() ? "write failed" : msg;
    return false;
  }
  out_message = "ok";
  return true;
}

std::string otaArchiveManifestPath(char role) {
  const char suffix = (role == 's') ? 's' : 'm';
  std::string p = ota_paths::kState;
  p += "/a";
  p.push_back(suffix);
  p += ".jsn";
  return p;
}

std::string otaArchiveBucketPath(char role) {
  return (role == 's') ? std::string(ota_paths::kArchiveSlave) : std::string(ota_paths::kArchiveMaster);
}

std::string otaArchiveFormatId(uint32_t value) {
  char b[7] = {0};
  std::snprintf(b, sizeof(b), "%06lX", static_cast<unsigned long>(value & 0xFFFFFFUL));
  return std::string(b);
}

bool otaArchiveNormalizeRole(const std::string& token, char& out_role) {
  const std::string role = lowerCopy(trim(token));
  if (role == "m" || role == "master") {
    out_role = 'm';
    return true;
  }
  if (role == "s" || role == "slave") {
    out_role = 's';
    return true;
  }
  return false;
}

std::string jsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8U);
  for (char c : in) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

bool otaArchiveLoadManifest(IOtaStorageBackend& storage,
                            char role,
                            std::vector<OtaArchiveEntryLocal>& out_entries,
                            std::string& out_message) {
  out_entries.clear();
  const std::string manifest_path = otaArchiveManifestPath(role);
  OtaStorageStat st{};
  std::string msg;
  if (!storage.stat(manifest_path, st, msg)) {
    out_message = msg.empty() ? "manifest stat failed" : msg;
    return false;
  }
  if (!st.exists) {
    out_message = "ok";
    return true;
  }
  if (st.is_dir) {
    std::string rm_msg;
    if (!storage.removePath(manifest_path, rm_msg)) {
      out_message = rm_msg.empty() ? "manifest path is directory" : rm_msg;
      return false;
    }
    out_message = "ok";
    return true;
  }
  if (st.size_bytes == 0U) {
    out_message = "ok";
    return true;
  }

  std::string text;
  if (!readTextFileLocal(storage, manifest_path, text, out_message)) {
    if (out_message == "file missing" || out_message == "empty file") {
      out_message = "ok";
      return true;
    }
    return false;
  }

  size_t pos = 0U;
  while (true) {
    pos = text.find("{\"id\":\"", pos);
    if (pos == std::string::npos) {
      break;
    }
    const size_t end = text.find('}', pos);
    if (end == std::string::npos || end <= pos) {
      break;
    }
    const std::string obj = text.substr(pos, end - pos + 1U);
    OtaArchiveEntryLocal e{};
    if (extractJsonStringValue(obj, "id", e.id)) {
      (void)extractJsonStringValue(obj, "bin", e.bin_name);
      (void)extractJsonStringValue(obj, "meta", e.meta_name);
      (void)extractJsonStringValue(obj, "sw", e.sw_version);
      (void)extractJsonStringValue(obj, "build", e.build_id);
      (void)extractJsonStringValue(obj, "target", e.target_role);
      (void)extractJsonStringValue(obj, "source", e.source);
      (void)extractJsonU32Value(obj, "size", e.size_bytes);
      (void)extractJsonU32Value(obj, "crc", e.crc32);
      (void)extractJsonU32Value(obj, "ts", e.created_epoch_s);
      if (e.bin_name.empty()) {
        e.bin_name = e.id + ".bin";
      }
      if (e.meta_name.empty()) {
        e.meta_name = e.id + ".jsn";
      }
      if (e.target_role.empty()) {
        e.target_role = (role == 's') ? "slave" : "master";
      }
      if (e.source.empty()) {
        e.source = "unknown";
      }
      out_entries.push_back(e);
    }
    pos = end + 1U;
  }
  out_message = "ok";
  return true;
}

bool otaArchiveSaveManifest(IOtaStorageBackend& storage,
                            char role,
                            const std::vector<OtaArchiveEntryLocal>& entries,
                            std::string& out_message) {
  std::string json = "{\"ver\":1,\"role\":\"";
  json.push_back(role);
  json += "\",\"entries\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    if (i > 0U) {
      json += ",";
    }
    char num[32] = {0};
    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(e.size_bytes));
    json += "{\"id\":\"" + jsonEscape(e.id) + "\"";
    json += ",\"bin\":\"" + jsonEscape(e.bin_name) + "\"";
    json += ",\"meta\":\"" + jsonEscape(e.meta_name) + "\"";
    json += ",\"sw\":\"" + jsonEscape(e.sw_version) + "\"";
    json += ",\"build\":\"" + jsonEscape(e.build_id) + "\"";
    json += ",\"target\":\"" + jsonEscape(e.target_role) + "\"";
    json += ",\"source\":\"" + jsonEscape(e.source) + "\"";
    json += ",\"size\":" + std::string(num);
    std::snprintf(num, sizeof(num), "0x%08lX", static_cast<unsigned long>(e.crc32));
    json += ",\"crc\":\"" + std::string(num) + "\"";
    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(e.created_epoch_s));
    json += ",\"ts\":" + std::string(num);
    json += "}";
  }
  json += "]}\n";
  return writeTextFileLocal(storage, otaArchiveManifestPath(role), json, out_message);
}

bool otaArchiveReadStageMeta(IOtaStorageBackend& storage,
                             const std::string& meta_path,
                             std::string& out_sw,
                             std::string& out_build,
                             std::string& out_target_role,
                             std::string& out_message) {
  std::string text;
  if (!readTextFileLocal(storage, meta_path, text, out_message)) {
    return false;
  }
  (void)extractJsonStringValue(text, "sw_version", out_sw);
  (void)extractJsonStringValue(text, "build_id", out_build);
  (void)extractJsonStringValue(text, "target_role", out_target_role);
  out_target_role = lowerCopy(trim(out_target_role));
  if (out_sw.empty()) {
    out_message = "metadata missing sw_version";
    return false;
  }
  if (out_build.empty()) {
    out_message = "metadata missing build_id";
    return false;
  }
  if (out_target_role != "master" && out_target_role != "slave") {
    out_message = "metadata missing/invalid target_role";
    return false;
  }
  out_message = "ok";
  return true;
}

std::string otaArchiveNormalizeId(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isxdigit(uc) == 0) {
      continue;
    }
    out.push_back(static_cast<char>(std::toupper(uc)));
  }
  if (out.size() > 6U) {
    out = out.substr(out.size() - 6U);
  }
  return out;
}

}  // namespace

bool MasterCli::requestFullSettingsByProfile() {
  if (!hasRuntimePeer()) {
    io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return false;
  }

  clearPagedFetchState();

  ProfileId profile_id = kProfileUnknown;
  if (!ensureRuntimeProfileKnown_(profile_id, true) || profile_id == kProfileUnknown) {
    io_.writeln("[MASTER][CLI] profile unresolved; probe queued, retry settings.full");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
    return false;
  }

  const IProfileDefinition* profile = ProfileRegistry::instance().find(profile_id);
  if (profile == nullptr) {
    writef("[MASTER][CLI] unknown profile id in registry: 0x%04X",
           static_cast<unsigned int>(profile_id));
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
    return false;
  }

  size_t queued = 0;
  for (const auto& spec : profile->settings()) {
    if (spec.key == nullptr || spec.key[0] == '\0') {
      continue;
    }
    if (sendDescriptorQuery(std::string("SETTING.GET ") + spec.key)) {
      ++queued;
    }
  }

  writef("[MASTER][CLI] settings.full queued %u/%u requests for profile=%s",
         static_cast<unsigned int>(queued),
         static_cast<unsigned int>(profile->settings().size()),
         profile->profileName());
  if (queued == 0U) {
    io_.writeln("[MASTER][CLI] profile settings empty");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
    return false;
  }
  return true;
}


bool MasterCli::submitRuntimeTargeted_(ManagementController& controller,
                                       uint16_t cmd_id,
                                       const std::vector<uint8_t>& payload,
                                       uint32_t* out_req_id,
                                       uint32_t timeout_ms,
                                       bool apply_runtime_target,
                                       const MacAddress* explicit_target_peer,
                                       uint32_t req_id_override) const {
  ManagementController::SubmitOptions submit_options{};
  submit_options.req_id = req_id_override;
  submit_options.timeout_ms = timeout_ms;
  if (explicit_target_peer != nullptr) {
    submit_options.has_target_peer = true;
    submit_options.target_peer = *explicit_target_peer;
  } else if (apply_runtime_target) {
    MacAddress runtime_peer{};
    if (!resolveRuntimePeer(runtime_peer)) {
      captureDispatchSnapshot_(false,
                               cmd_id,
                               req_id_override,
                               ManagementStatus::BadPayload,
                               "target");
      return false;
    }
    submit_options.has_target_peer = true;
    submit_options.target_peer = runtime_peer;
  }
  const ManagementController::SubmitResult submit_result = controller.submit(cmd_id, payload, submit_options);
  if (submit_result.accepted) {
    noteCliOwnedReqId_(submit_result.req_id);
  }
  captureDispatchSnapshot_(submit_result.accepted,
                           submit_result.cmd_id,
                           submit_result.req_id,
                           submit_result.status,
                           submit_result.reject_stage);
  if (out_req_id != nullptr) {
    *out_req_id = submit_result.req_id;
  }
  return submit_result.accepted;
}

void MasterCli::resetDispatchSnapshot_() const {
  dispatch_snapshot_ = SubmitDispatchSnapshot{};
}

void MasterCli::captureDispatchSnapshot_(bool accepted,
                                         uint16_t cmd_id,
                                         uint32_t req_id,
                                         ManagementStatus status,
                                         const char* reject_stage) const {
  dispatch_snapshot_.seen = true;
  dispatch_snapshot_.accepted = accepted;
  dispatch_snapshot_.cmd_id = cmd_id;
  dispatch_snapshot_.req_id = req_id;
  dispatch_snapshot_.status = status;
  dispatch_snapshot_.reject_stage = (reject_stage != nullptr) ? reject_stage : "";
}

bool MasterCli::submitRuntimePushCommand_(ManagementController& controller,
                                          const TelemetryPushCommand& command,
                                          uint32_t* out_req_id,
                                          uint32_t timeout_ms,
                                          bool apply_runtime_target) const {
  std::vector<uint8_t> payload{};
  if (!encodeTelemetryPushCommand(command, payload)) {
    captureDispatchSnapshot_(false,
                             0U,
                             0U,
                             ManagementStatus::BadPayload,
                             "parse");
    return false;
  }

  ManagementCommandId command_id = ManagementCommandId::PushGet;
  switch (command.action) {
    case TelemetryPushAction::Start:
      command_id = ManagementCommandId::PushStart;
      break;
    case TelemetryPushAction::Update:
      command_id = ManagementCommandId::PushUpdate;
      break;
    case TelemetryPushAction::Pause:
      command_id = ManagementCommandId::PushPause;
      break;
    case TelemetryPushAction::Resume:
      command_id = ManagementCommandId::PushResume;
      break;
    case TelemetryPushAction::Stop:
      command_id = ManagementCommandId::PushStop;
      break;
    case TelemetryPushAction::Get:
    default:
      command_id = ManagementCommandId::PushGet;
      break;
  }

  return submitRuntimeTargeted_(controller,
                                static_cast<uint16_t>(command_id),
                                payload,
                                out_req_id,
                                timeout_ms,
                                apply_runtime_target);
}

}  // namespace espnow_link
