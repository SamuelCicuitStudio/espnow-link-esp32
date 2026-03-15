#include "espnow_link/cli_master.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <limits>

#include "espnow_link/address.hpp"
#include "espnow_link/management_controller.hpp"
#include "espnow_link/management_runtime.hpp"
#include "espnow_link/management_service.hpp"
#include "espnow_link/management_utils.hpp"
#include "espnow_link/ota_paths.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/security.hpp"
#include "cli_helpers.hpp"

namespace espnow_link {

using namespace cli_helpers;

namespace {

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
    io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return false;
  }

  clearPagedFetchState();

  const ProfileId profile_id = remote_profile_id_;

  if (profile_id == kProfileUnknown) {
    io_.writeln("[MASTER][CLI] profile unknown; run caps first");
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

bool MasterCli::handleGetIdCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "get.id ")) {
    return false;
  }

  const std::string arg = trim(line.substr(7));
  if (arg.empty()) {
    io_.writeln("[MASTER][CLI] usage: get.id <setting_id>");
    return true;
  }

  const unsigned long id = std::strtoul(arg.c_str(), nullptr, 0);
  if (id == 0 || id > 0xFFFFUL) {
    io_.writeln("[MASTER][CLI] invalid setting_id");
    return true;
  }

  if (!hasRuntimePeer()) {
    io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingGet),
                                         management_utils::buildSettingGetByIdPayload(static_cast<uint16_t>(id)));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] requested setting id=0x%04lX", id);
  } else {
    io_.writeln("[MASTER][CLI] get.id request failed");
  }
  return true;
}

bool MasterCli::handleSetIdCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "set.id ")) {
    return false;
  }

  const std::string body = trim(line.substr(7));
  const size_t eq = body.find('=');
  if (body.empty() || eq == std::string::npos || eq == 0) {
    io_.writeln("[MASTER][CLI] usage: set.id <setting_id>=<value>");
    return true;
  }

  const std::string sid = trim(body.substr(0, eq));
  const std::string value = trim(body.substr(eq + 1));
  const unsigned long id = std::strtoul(sid.c_str(), nullptr, 0);
  if (id == 0 || id > 0xFFFFUL) {
    io_.writeln("[MASTER][CLI] invalid setting_id");
    return true;
  }

  if (!hasRuntimePeer()) {
    io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingSet),
                                         management_utils::buildSettingSetByIdPayload(static_cast<uint16_t>(id), value));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] set.id requested: 0x%04lX=%s", id, value.c_str());
  } else {
    io_.writeln("[MASTER][CLI] set.id request failed");
  }
  return true;
}

bool MasterCli::handleGetCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "get ")) {
    return false;
  }

  const std::string key = trim(line.substr(4));
  if (key.empty()) {
    io_.writeln("[MASTER][CLI] usage: get <setting_key>  (child: v<vid>.<field>)");
    return true;
  }

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingGet),
                                         management_utils::buildSettingGetByKeyPayload(key));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] requested setting %s", key.c_str());
  } else {
    io_.writeln("[MASTER][CLI] get setting request failed");
  }
  return true;
}

bool MasterCli::handleSetCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "set ")) {
    return false;
  }

  const std::string body = trim(line.substr(4));
  if (body.empty() || body.find('=') == std::string::npos) {
    io_.writeln("[MASTER][CLI] usage: set <setting_key>=<value>  (child: v<vid>.<field>)");
    return true;
  }

  const size_t eq = body.find('=');
  const std::string key = trim(body.substr(0U, eq));
  const std::string value = trim(body.substr(eq + 1U));
  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  if (key.empty()) {
    io_.writeln("[MASTER][CLI] usage: set <setting_key>=<value>  (child: v<vid>.<field>)");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingSet),
                                         management_utils::buildSettingSetByKeyPayload(key, value));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] set requested: %s", body.c_str());
  } else {
    io_.writeln("[MASTER][CLI] set setting request failed");
  }
  return true;
}

bool MasterCli::handleTimeCommand(const std::string& line, const std::string& lower) {
  if (lower == "time.get") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::TimeGet));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][CLI] requested slave time");
    } else {
      io_.writeln("[MASTER][CLI] slave time request failed");
    }
    return true;
  }

  if (lower == "time.set.now") {
    const uint64_t now_s = static_cast<uint64_t>(std::time(nullptr));
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::TimeSet),
                                           management_utils::buildTimeSetPayload(now_s));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][CLI] requested slave time set to %lu", static_cast<unsigned long>(now_s));
    } else {
      io_.writeln("[MASTER][CLI] slave time set request failed");
    }
    return true;
  }

  if (startsWith(lower, "time.set ")) {
    const std::string arg = trim(line.substr(9));
    if (arg.empty()) {
      io_.writeln("[MASTER][CLI] usage: time.set <epoch_s>");
      return true;
    }
    const uint64_t epoch_s = static_cast<uint64_t>(std::strtoull(arg.c_str(), nullptr, 10));
    if (epoch_s == 0) {
      io_.writeln("[MASTER][CLI] invalid epoch_s");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::TimeSet),
                                           management_utils::buildTimeSetPayload(epoch_s));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][CLI] requested slave time set to %s", arg.c_str());
    } else {
      io_.writeln("[MASTER][CLI] slave time set request failed");
    }
    return true;
  }

  return false;
}

bool MasterCli::handleAutopullCommand(const std::string& lower) {
  if (!startsWith(lower, "autopull ")) {
    return false;
  }

  const std::string arg = trim(lower.substr(9));
  if (arg == "off") {
    setAutoPull(false, auto_pull_interval_ms_);
    io_.writeln("[MASTER][CLI] autopull disabled");
    return true;
  }
  if (startsWith(arg, "on")) {
    MacAddress runtime_peer{};
    if (!resolveRuntimePeer(runtime_peer)) {
      io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }
    uint32_t interval = auto_pull_interval_ms_;
    const size_t sp = arg.find(' ');
    if (sp != std::string::npos) {
      const unsigned long parsed = std::strtoul(arg.substr(sp + 1).c_str(), nullptr, 10);
      if (parsed >= 300UL) {
        interval = static_cast<uint32_t>(parsed);
      }
    }
    auto_pull_has_target_peer_ = true;
    auto_pull_target_peer_ = runtime_peer;
    setAutoPull(true, interval);
    writef("[MASTER][CLI] autopull enabled peer=%s interval_ms=%lu",
           macToPrintable(runtime_peer).c_str(),
           static_cast<unsigned long>(auto_pull_interval_ms_));
    return true;
  }

  io_.writeln("[MASTER][CLI] usage: autopull on [ms] | autopull off");
  return true;
}

bool MasterCli::handlePushCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "push.")) {
    return false;
  }

  if (!hasRuntimePeer()) {
    io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }
  MacAddress runtime_peer{};
  if (!resolveRuntimePeer(runtime_peer)) {
    io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }
  ChildPushPeerState& child_push_state = ensureChildPushState_(runtime_peer);
  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }

  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.empty()) {
    io_.writeln("[MASTER][CLI] invalid push command");
    return true;
  }

  const std::string cmd = lowerCopy(tokens[0]);
  TelemetryPushCommand push{};

  auto send = [&](const char* label) {
    uint32_t corr = 0;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][CLI] %s sent corr=%lu", label, static_cast<unsigned long>(corr));
    } else {
      io_.writeln("[MASTER][CLI] push command send failed");
    }
    return true;
  };

  if (cmd == "push.pause") {
    push.action = TelemetryPushAction::Pause;
    return send("push.pause");
  }
  if (cmd == "push.resume") {
    push.action = TelemetryPushAction::Resume;
    return send("push.resume");
  }
  if (cmd == "push.stop") {
    child_push_state.semu_mask = 0U;
    child_push_state.remu_mask = 0U;
    push.action = TelemetryPushAction::Stop;
    return send("push.stop");
  }
  if (cmd == "push.get") {
    push.action = TelemetryPushAction::Get;
    return send("push.get");
  }

  auto fillMetricTiming = [&](TelemetryPushMetricConfig& metric,
                              TelemetryPushMode mode,
                              uint32_t interval_ms,
                              float delta_abs,
                              uint32_t gap_ms) {
    metric.enabled = true;
    metric.mode = mode;
    metric.interval_ms = interval_ms;
    metric.min_report_gap_ms = gap_ms;
    metric.use_threshold = (mode != TelemetryPushMode::Periodic);
    metric.delta_abs = (mode == TelemetryPushMode::Periodic) ? 0.0f : delta_abs;
  };

  auto parsePushTiming = [&](size_t mode_index,
                             TelemetryPushMode& out_mode,
                             uint32_t& out_interval_ms,
                             float& out_delta_abs,
                             uint32_t& out_gap_ms) -> bool {
    out_mode = TelemetryPushMode::Hybrid;
    out_interval_ms = 2000;
    out_delta_abs = 0.10f;
    out_gap_ms = 200;

    if (tokens.size() > mode_index) {
      if (!parsePushModeToken(tokens[mode_index], out_mode)) {
        io_.writeln("[MASTER][CLI] invalid push mode (use: hybrid|periodic|change)");
        return false;
      }
    }
    if (tokens.size() > mode_index + 1) {
      if (!parseU32Token(tokens[mode_index + 1], out_interval_ms) || out_interval_ms < 200U) {
        io_.writeln("[MASTER][CLI] invalid interval_ms (>=200)");
        return false;
      }
    }
    if (tokens.size() > mode_index + 2) {
      if (!parseFloatToken(tokens[mode_index + 2], out_delta_abs) || out_delta_abs < 0.0f) {
        io_.writeln("[MASTER][CLI] invalid delta_abs (>=0)");
        return false;
      }
    }
    if (tokens.size() > mode_index + 3) {
      if (!parseU32Token(tokens[mode_index + 3], out_gap_ms) || out_gap_ms < 50U) {
        io_.writeln("[MASTER][CLI] invalid gap_ms (>=50)");
        return false;
      }
    }
    return true;
  };

  auto bitCount8 = [](uint8_t v) -> uint8_t {
    uint8_t c = 0U;
    while (v != 0U) {
      c = static_cast<uint8_t>(c + (v & 0x01U));
      v = static_cast<uint8_t>(v >> 1U);
    }
    return c;
  };

  auto bitCount16 = [](uint16_t v) -> uint8_t {
    uint8_t c = 0U;
    while (v != 0U) {
      c = static_cast<uint8_t>(c + static_cast<uint8_t>(v & 0x0001U));
      v = static_cast<uint16_t>(v >> 1U);
    }
    return c;
  };

  enum class ChildPushProfile : uint8_t {
    Unknown = 0,
    Semu = 1,
    Remu = 2,
  };

  auto resolveRemoteProfileId = [&]() -> ProfileId {
    return remote_profile_id_;
  };

  auto activeChildPushProfile = [&]() -> ChildPushProfile {
    const ProfileId profile_id = resolveRemoteProfileId();
    if (profile_id == kProfileSemu) {
      return ChildPushProfile::Semu;
    }
    if (profile_id == kProfileRemu) {
      return ChildPushProfile::Remu;
    }
    return ChildPushProfile::Unknown;
  };

  auto buildSemuChildPush = [&](uint8_t child_mask,
                                TelemetryPushAction action,
                                TelemetryPushMode mode,
                                uint32_t interval_ms,
                                float delta_abs,
                                uint32_t gap_ms,
                                TelemetryPushCommand& out_cmd) -> bool {
    out_cmd = TelemetryPushCommand{};
    out_cmd.action = action;
    out_cmd.config.stream_id = 1;
    out_cmd.config.mode = mode;
    out_cmd.config.interval_ms = interval_ms;
    out_cmd.config.min_report_gap_ms = gap_ms;
    if (action == TelemetryPushAction::Stop || action == TelemetryPushAction::Pause ||
        action == TelemetryPushAction::Resume || action == TelemetryPushAction::Get) {
      return true;
    }
    for (uint8_t vid = 0U; vid < 8U; ++vid) {
      if ((child_mask & static_cast<uint8_t>(1U << vid)) == 0U) {
        continue;
      }
      TelemetryPushMetricConfig a{};
      a.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".tfl_a_mm";
      fillMetricTiming(a, mode, interval_ms, delta_abs, gap_ms);
      out_cmd.config.metrics.push_back(a);

      TelemetryPushMetricConfig b{};
      b.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".tfl_b_mm";
      fillMetricTiming(b, mode, interval_ms, delta_abs, gap_ms);
      out_cmd.config.metrics.push_back(b);
    }
    return !out_cmd.config.metrics.empty();
  };

  auto buildRemuChildPush = [&](uint16_t child_mask,
                                TelemetryPushAction action,
                                TelemetryPushMode mode,
                                uint32_t interval_ms,
                                float delta_abs,
                                uint32_t gap_ms,
                                TelemetryPushCommand& out_cmd) -> bool {
    out_cmd = TelemetryPushCommand{};
    out_cmd.action = action;
    out_cmd.config.stream_id = 1;
    out_cmd.config.mode = mode;
    out_cmd.config.interval_ms = interval_ms;
    out_cmd.config.min_report_gap_ms = gap_ms;
    if (action == TelemetryPushAction::Stop || action == TelemetryPushAction::Pause ||
        action == TelemetryPushAction::Resume || action == TelemetryPushAction::Get) {
      return true;
    }
    for (uint8_t vid = 0U; vid < 16U; ++vid) {
      if ((child_mask & static_cast<uint16_t>(1U << vid)) == 0U) {
        continue;
      }
      TelemetryPushMetricConfig relay_state{};
      relay_state.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".relay_bitmap";
      fillMetricTiming(relay_state, mode, interval_ms, delta_abs, gap_ms);
      out_cmd.config.metrics.push_back(relay_state);
    }
    return !out_cmd.config.metrics.empty();
  };

  if (cmd == "push.child.start" || cmd == "push.child.update") {
    const ChildPushProfile child_profile = activeChildPushProfile();
    if (child_profile == ChildPushProfile::Unknown) {
      io_.writeln("[MASTER][CLI] child push requires SEMU or REMU target (run caps on selected peer first)");
      return true;
    }
    const uint32_t max_vid = (child_profile == ChildPushProfile::Semu) ? 7U : 15U;
    if (tokens.size() < 2U || tokens.size() > 6U) {
      writef("[MASTER][CLI] usage: push.child.start <vid:0..%u> [hybrid|periodic|change] [interval_ms] [delta_abs] [gap_ms]",
             static_cast<unsigned int>(max_vid));
      return true;
    }
    uint32_t vid = 0U;
    if (!parseU32Token(tokens[1], vid) || vid > max_vid) {
      writef("[MASTER][CLI] invalid child vid (0..%u)", static_cast<unsigned int>(max_vid));
      return true;
    }

    TelemetryPushMode mode = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_mode : child_push_state.remu_mode;
    uint32_t interval_ms = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_interval_ms
                                                                      : child_push_state.remu_interval_ms;
    float delta_abs = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_delta_abs
                                                                 : child_push_state.remu_delta_abs;
    uint32_t gap_ms = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_gap_ms : child_push_state.remu_gap_ms;
    if (!parsePushTiming(2U, mode, interval_ms, delta_abs, gap_ms)) {
      return true;
    }
    uint8_t active_children = 0U;
    if (child_profile == ChildPushProfile::Semu) {
      const uint8_t old_mask = child_push_state.semu_mask;
      const TelemetryPushMode old_mode = child_push_state.semu_mode;
      const uint32_t old_interval = child_push_state.semu_interval_ms;
      const float old_delta = child_push_state.semu_delta_abs;
      const uint32_t old_gap = child_push_state.semu_gap_ms;

      child_push_state.semu_mode = mode;
      child_push_state.semu_interval_ms = interval_ms;
      child_push_state.semu_delta_abs = delta_abs;
      child_push_state.semu_gap_ms = gap_ms;
      child_push_state.semu_mask =
          static_cast<uint8_t>(child_push_state.semu_mask | static_cast<uint8_t>(1U << static_cast<uint8_t>(vid)));

      const TelemetryPushAction action = (old_mask == 0U) ? TelemetryPushAction::Start : TelemetryPushAction::Update;
      if (!buildSemuChildPush(child_push_state.semu_mask,
                              action,
                              child_push_state.semu_mode,
                              child_push_state.semu_interval_ms,
                              child_push_state.semu_delta_abs,
                              child_push_state.semu_gap_ms,
                              push)) {
        io_.writeln("[MASTER][CLI] failed to build child push config");
        child_push_state.semu_mask = old_mask;
        child_push_state.semu_mode = old_mode;
        child_push_state.semu_interval_ms = old_interval;
        child_push_state.semu_delta_abs = old_delta;
        child_push_state.semu_gap_ms = old_gap;
        return true;
      }
      active_children = bitCount8(child_push_state.semu_mask);
    } else {
      const uint16_t old_mask = child_push_state.remu_mask;
      const TelemetryPushMode old_mode = child_push_state.remu_mode;
      const uint32_t old_interval = child_push_state.remu_interval_ms;
      const float old_delta = child_push_state.remu_delta_abs;
      const uint32_t old_gap = child_push_state.remu_gap_ms;

      child_push_state.remu_mode = mode;
      child_push_state.remu_interval_ms = interval_ms;
      child_push_state.remu_delta_abs = delta_abs;
      child_push_state.remu_gap_ms = gap_ms;
      child_push_state.remu_mask =
          static_cast<uint16_t>(child_push_state.remu_mask | static_cast<uint16_t>(1U << static_cast<uint8_t>(vid)));

      const TelemetryPushAction action = (old_mask == 0U) ? TelemetryPushAction::Start : TelemetryPushAction::Update;
      if (!buildRemuChildPush(child_push_state.remu_mask,
                              action,
                              child_push_state.remu_mode,
                              child_push_state.remu_interval_ms,
                              child_push_state.remu_delta_abs,
                              child_push_state.remu_gap_ms,
                              push)) {
        io_.writeln("[MASTER][CLI] failed to build child push config");
        child_push_state.remu_mask = old_mask;
        child_push_state.remu_mode = old_mode;
        child_push_state.remu_interval_ms = old_interval;
        child_push_state.remu_delta_abs = old_delta;
        child_push_state.remu_gap_ms = old_gap;
        return true;
      }
      active_children = bitCount16(child_push_state.remu_mask);
    }
    uint32_t corr = 0U;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (!ok) {
      io_.writeln("[MASTER][CLI] child push command send failed");
      return true;
    }
    writef("[MASTER][CLI] push.child.start sent profile=%s vid=%u active_children=%u metrics=%u corr=%lu",
           (child_profile == ChildPushProfile::Semu) ? "SEMU" : "REMU",
           static_cast<unsigned int>(vid),
           static_cast<unsigned int>(active_children),
           static_cast<unsigned int>(push.config.metrics.size()),
           static_cast<unsigned long>(corr));
    return true;
  }

  if (cmd == "push.child.stop") {
    const ChildPushProfile child_profile = activeChildPushProfile();
    if (child_profile == ChildPushProfile::Unknown) {
      io_.writeln("[MASTER][CLI] child stop requires SEMU or REMU target (run caps on selected peer first)");
      return true;
    }
    const uint32_t max_vid = (child_profile == ChildPushProfile::Semu) ? 7U : 15U;
    if (tokens.size() != 2U) {
      writef("[MASTER][CLI] usage: push.child.stop <vid:0..%u>", static_cast<unsigned int>(max_vid));
      return true;
    }
    uint32_t vid = 0U;
    if (!parseU32Token(tokens[1], vid) || vid > max_vid) {
      writef("[MASTER][CLI] invalid child vid (0..%u)", static_cast<unsigned int>(max_vid));
      return true;
    }
    uint8_t active_children = 0U;
    if (child_profile == ChildPushProfile::Semu) {
      const uint8_t bit = static_cast<uint8_t>(1U << static_cast<uint8_t>(vid));
      if ((child_push_state.semu_mask & bit) == 0U) {
        writef("[MASTER][CLI] child %u push already stopped", static_cast<unsigned int>(vid));
        return true;
      }
      const uint8_t old_mask = child_push_state.semu_mask;
      child_push_state.semu_mask = static_cast<uint8_t>(child_push_state.semu_mask & static_cast<uint8_t>(~bit));
      if (child_push_state.semu_mask == 0U) {
        push = TelemetryPushCommand{};
        push.action = TelemetryPushAction::Stop;
        push.config.stream_id = 1;
      } else if (!buildSemuChildPush(child_push_state.semu_mask,
                                     TelemetryPushAction::Update,
                                     child_push_state.semu_mode,
                                     child_push_state.semu_interval_ms,
                                     child_push_state.semu_delta_abs,
                                     child_push_state.semu_gap_ms,
                                     push)) {
        child_push_state.semu_mask = old_mask;
        io_.writeln("[MASTER][CLI] failed to build child push update");
        return true;
      }
      active_children = bitCount8(child_push_state.semu_mask);
    } else {
      const uint16_t bit = static_cast<uint16_t>(1U << static_cast<uint8_t>(vid));
      if ((child_push_state.remu_mask & bit) == 0U) {
        writef("[MASTER][CLI] child %u push already stopped", static_cast<unsigned int>(vid));
        return true;
      }
      const uint16_t old_mask = child_push_state.remu_mask;
      child_push_state.remu_mask = static_cast<uint16_t>(child_push_state.remu_mask & static_cast<uint16_t>(~bit));
      if (child_push_state.remu_mask == 0U) {
        push = TelemetryPushCommand{};
        push.action = TelemetryPushAction::Stop;
        push.config.stream_id = 1;
      } else if (!buildRemuChildPush(child_push_state.remu_mask,
                                     TelemetryPushAction::Update,
                                     child_push_state.remu_mode,
                                     child_push_state.remu_interval_ms,
                                     child_push_state.remu_delta_abs,
                                     child_push_state.remu_gap_ms,
                                     push)) {
        child_push_state.remu_mask = old_mask;
        io_.writeln("[MASTER][CLI] failed to build child push update");
        return true;
      }
      active_children = bitCount16(child_push_state.remu_mask);
    }

    uint32_t corr = 0U;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (!ok) {
      io_.writeln("[MASTER][CLI] child stop command send failed");
      return true;
    }
    writef("[MASTER][CLI] push.child.stop sent profile=%s vid=%u active_children=%u corr=%lu",
           (child_profile == ChildPushProfile::Semu) ? "SEMU" : "REMU",
           static_cast<unsigned int>(vid),
           static_cast<unsigned int>(active_children),
           static_cast<unsigned long>(corr));
    return true;
  }

  if (cmd == "push.start" || cmd == "push.update") {
    if (tokens.size() > 5U) {
      io_.writeln("[MASTER][CLI] usage: push.start [hybrid|periodic|change] [interval_ms] [delta_abs] [gap_ms]");
      return true;
    }

    TelemetryPushMode mode = TelemetryPushMode::Hybrid;
    uint32_t interval_ms = 2000;
    float delta_abs = 0.10f;
    uint32_t gap_ms = 200;
    if (!parsePushTiming(1U, mode, interval_ms, delta_abs, gap_ms)) {
      return true;
    }

    const IProfileDefinition* profile = nullptr;
    const ProfileId remote_profile_id = resolveRemoteProfileId();
    if (remote_profile_id != kProfileUnknown) {
      profile = ProfileRegistry::instance().find(remote_profile_id);
    }
    if (profile == nullptr) {
      profile = manager_.localProfile();
    }
    if (profile == nullptr || profile->telemetryMetrics().empty()) {
      io_.writeln("[MASTER][CLI] telemetry profile unknown; run caps first");
      return true;
    }

    push.action = (cmd == "push.start") ? TelemetryPushAction::Start : TelemetryPushAction::Update;
    push.config.stream_id = 1;
    push.config.mode = mode;
    push.config.interval_ms = interval_ms;
    push.config.min_report_gap_ms = gap_ms;
    push.config.metrics.reserve(profile->telemetryMetrics().size());

    for (const auto& spec : profile->telemetryMetrics()) {
      if (spec.key == nullptr || spec.key[0] == '\0') {
        continue;
      }
      TelemetryPushMetricConfig metric{};
      if (spec.metric_id != 0U) {
        metric.has_metric_index = true;
        metric.metric_index = spec.metric_id;
      } else {
        metric.key = spec.key;
      }
      fillMetricTiming(metric, mode, interval_ms, delta_abs, gap_ms);
      push.config.metrics.push_back(metric);
    }

    if (push.config.metrics.empty()) {
      io_.writeln("[MASTER][CLI] no telemetry metrics in profile");
      return true;
    }

    uint32_t corr = 0;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      child_push_state.semu_mask = 0U;
      child_push_state.remu_mask = 0U;
      writef("[MASTER][CLI] %s sent mode=%s metrics=%u interval_ms=%lu delta=%.3f gap_ms=%lu corr=%lu",
             cmd.c_str(),
             (mode == TelemetryPushMode::Periodic)
                 ? "periodic"
                 : ((mode == TelemetryPushMode::OnChange) ? "change" : "hybrid"),
             static_cast<unsigned int>(push.config.metrics.size()),
             static_cast<unsigned long>(interval_ms),
             static_cast<double>(delta_abs),
             static_cast<unsigned long>(gap_ms),
             static_cast<unsigned long>(corr));
    } else {
      io_.writeln("[MASTER][CLI] push command send failed");
    }
    return true;
  }

  if (cmd == "push.one" || cmd == "push.id") {
    if (tokens.size() != 6U) {
      io_.writeln("[MASTER][CLI] usage:");
      io_.writeln("  push.one <metric_key> <hybrid|periodic|change> <interval_ms> <delta_abs> <gap_ms>");
      io_.writeln("  push.id <metric_id> <hybrid|periodic|change> <interval_ms> <delta_abs> <gap_ms>");
      return true;
    }

    TelemetryPushMode mode = TelemetryPushMode::Hybrid;
    uint32_t interval_ms = 2000;
    float delta_abs = 0.10f;
    uint32_t gap_ms = 200;
    if (!parsePushTiming(2U, mode, interval_ms, delta_abs, gap_ms)) {
      return true;
    }

    TelemetryPushMetricConfig metric{};
    if (cmd == "push.id") {
      uint16_t metric_id = 0;
      if (!parseU16Token(tokens[1], metric_id) || metric_id == 0) {
        io_.writeln("[MASTER][CLI] invalid metric_id");
        return true;
      }
      metric.has_metric_index = true;
      metric.metric_index = metric_id;
    } else {
      metric.key = trim(tokens[1]);
      if (metric.key.empty()) {
        io_.writeln("[MASTER][CLI] invalid metric_key");
        return true;
      }
    }
    fillMetricTiming(metric, mode, interval_ms, delta_abs, gap_ms);

    push.action = TelemetryPushAction::Start;
    push.config.stream_id = 1;
    push.config.mode = mode;
    push.config.interval_ms = interval_ms;
    push.config.min_report_gap_ms = gap_ms;
    push.config.metrics.push_back(metric);

    uint32_t corr = 0;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      child_push_state.semu_mask = 0U;
      child_push_state.remu_mask = 0U;
      if (cmd == "push.id") {
        writef("[MASTER][CLI] push.id sent metric_id=0x%04X mode=%s interval_ms=%lu delta=%.3f gap_ms=%lu corr=%lu",
               static_cast<unsigned int>(metric.metric_index),
               (mode == TelemetryPushMode::Periodic)
                   ? "periodic"
                   : ((mode == TelemetryPushMode::OnChange) ? "change" : "hybrid"),
               static_cast<unsigned long>(interval_ms),
               static_cast<double>(delta_abs),
               static_cast<unsigned long>(gap_ms),
               static_cast<unsigned long>(corr));
      } else {
        writef("[MASTER][CLI] push.one sent metric=%s mode=%s interval_ms=%lu delta=%.3f gap_ms=%lu corr=%lu",
               metric.key.c_str(),
               (mode == TelemetryPushMode::Periodic)
                   ? "periodic"
                   : ((mode == TelemetryPushMode::OnChange) ? "change" : "hybrid"),
               static_cast<unsigned long>(interval_ms),
               static_cast<double>(delta_abs),
               static_cast<unsigned long>(gap_ms),
               static_cast<unsigned long>(corr));
      }
    } else {
      io_.writeln("[MASTER][CLI] push command send failed");
    }
    return true;
  }

  io_.writeln("[MASTER][CLI] unknown push command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

bool MasterCli::handleTestAndLocalCommands(const std::string& lower) {
  if (lower == "test.all" || lower == "selftest" || lower == "comm.test") {
    enum class StepPolicy : uint8_t {
      Run = 0,
      SkipRequiresInput,
      SkipDestructive,
      SkipProfileChildOnly,
      SkipHeavy,
      SkipRequiresActive,
    };
    struct TestStep {
      const char* area;
      const char* cmd;
      bool needs_target;
      StepPolicy policy;
    };

    static constexpr TestStep kSteps[] = {
        {"master", "status", false, StepPolicy::Run},
        {"master", "paired", false, StepPolicy::Run},
        {"master", "cli status", false, StepPolicy::Run},
        {"test/diag", "radio.drytest", false, StepPolicy::Run},

        {"desc/profile", "desc", true, StepPolicy::Run},
        {"desc/profile", "caps", true, StepPolicy::Run},
        {"desc/profile", "telem", true, StepPolicy::Run},
        {"desc/profile", "telem.now", true, StepPolicy::Run},
        {"desc/profile", "telem.now.child 0", true, StepPolicy::SkipProfileChildOnly},
        {"desc/profile", "live", true, StepPolicy::Run},
        {"desc/profile", "ping", true, StepPolicy::Run},
        {"desc/profile", "audio ping", true, StepPolicy::Run},

        {"settings", "settings", true, StepPolicy::Run},
        {"settings", "settings.full", true, StepPolicy::Run},
        {"settings", "settings.raw", true, StepPolicy::Run},
        {"settings", "get <setting_key>", true, StepPolicy::SkipRequiresInput},
        {"settings", "get.id <setting_id>", true, StepPolicy::SkipRequiresInput},
        {"settings", "set <setting_key>=<value>", true, StepPolicy::SkipDestructive},
        {"settings", "set.id <setting_id>=<value>", true, StepPolicy::SkipDestructive},

        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.start hybrid 2000 0.1 200", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.update hybrid 2000 0.1 200", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.pause", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.resume", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.stop", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.one <key> <mode> <int> <d> <g>", true, StepPolicy::SkipRequiresInput},
        {"push", "push.id <id> <mode> <int> <d> <g>", true, StepPolicy::SkipRequiresInput},
        {"push", "push.child.start <vid> [mode] [int] [d] [g]", true, StepPolicy::SkipProfileChildOnly},
        {"push", "push.child.stop <vid>", true, StepPolicy::SkipProfileChildOnly},
        {"push", "autopull on 2000", true, StepPolicy::Run},
        {"push", "autopull off", false, StepPolicy::Run},

        {"time", "time.get", true, StepPolicy::Run},
        {"time", "time.set <epoch_s>", true, StepPolicy::SkipDestructive},
        {"time", "time.set.now", true, StepPolicy::SkipDestructive},
        {"time", "time.local", false, StepPolicy::Run},

        {"lifecycle", "restart master", false, StepPolicy::SkipDestructive},
        {"lifecycle", "reset master", false, StepPolicy::SkipDestructive},
        {"lifecycle", "restart slave", true, StepPolicy::SkipDestructive},
        {"lifecycle", "reset slave", true, StepPolicy::SkipDestructive},

        {"test/diag", "comm.test.status", true, StepPolicy::Run},
        {"test/diag", "comm.test.report", true, StepPolicy::Run},
        {"test/diag", "radio.drytest", false, StepPolicy::Run},
        {"test/diag", "metrics", false, StepPolicy::Run},
        {"test/diag", "metrics.reset", false, StepPolicy::Run},
        {"test/diag", "queue", false, StepPolicy::Run},

        {"log", "log", false, StepPolicy::Run},
        {"log", "log error", false, StepPolicy::Run},
        {"log", "log info", false, StepPolicy::Run},
        {"log", "log debug", false, StepPolicy::Run},

        {"logger.local", "logger.status", false, StepPolicy::Run},
        {"logger.local", "logger.enable", false, StepPolicy::Run},
        {"logger.local", "logger.disable", false, StepPolicy::Run},
        {"logger.local", "logger.clear", false, StepPolicy::SkipDestructive},
        {"logger.local", "logger.read 0 64", false, StepPolicy::Run},

        {"logger.remote", "logger.remote.status", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.enable", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.disable", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.clear", true, StepPolicy::SkipDestructive},
        {"logger.remote", "logger.remote.read 0 64", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.pull 64", true, StepPolicy::SkipHeavy},
        {"logger.remote", "logger.remote.stop", true, StepPolicy::SkipRequiresActive},
        {"channel/chain", "channel.runtime.status", false, StepPolicy::Run},
        {"channel/chain", "channel.sync <1..14>", false, StepPolicy::SkipDestructive},
        {"channel/chain", "chain.loop.status", false, StepPolicy::Run},
        {"channel/chain", "chain.loop.on", false, StepPolicy::SkipDestructive},
        {"channel/chain", "chain.loop.off", false, StepPolicy::SkipDestructive},

        {"storage.local", "sd.info", false, StepPolicy::Run},
        {"storage.local", "sd.pwd", false, StepPolicy::Run},
        {"storage.local", "sd.ls /", false, StepPolicy::Run},
        {"storage.local", "sd.cd /", false, StepPolicy::Run},
        {"storage.local", "sd.up", false, StepPolicy::Run},
        {"storage.local", "sd.stat /", false, StepPolicy::Run},
        {"storage.local", "sd.format", false, StepPolicy::SkipDestructive},

        {"storage.remote", "sd.remote.info", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.pwd", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.ls /", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.cd /", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.up", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.stat /", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.format", true, StepPolicy::SkipDestructive},
    };

    MacAddress runtime_peer{};
    const bool has_runtime_peer = resolveRuntimePeer(runtime_peer);
    const std::string target_peer_text = has_runtime_peer ? macToPrintable(runtime_peer) : "none";
    std::string target_prefix{};
    if (has_runtime_peer) {
      target_prefix = target_peer_text;
      target_prefix.push_back(' ');
    }

    const bool child_profile =
        (remote_profile_id_ == kProfileSemu) || (remote_profile_id_ == kProfileRemu);

    auto policyReason = [&](StepPolicy policy) -> const char* {
      switch (policy) {
        case StepPolicy::SkipRequiresInput:
          return "requires explicit argument(s)";
        case StepPolicy::SkipDestructive:
          return "destructive/mutating command";
        case StepPolicy::SkipProfileChildOnly:
          return "child telemetry command (SEMU/REMU only)";
        case StepPolicy::SkipHeavy:
          return "heavy/long-running operation";
        case StepPolicy::SkipRequiresActive:
          return "requires active runtime state";
        case StepPolicy::Run:
        default:
          return "";
      }
    };

    auto logLevelLabel = [](CliLogLevel level) -> const char* {
      switch (level) {
        case CliLogLevel::Error:
          return "error";
        case CliLogLevel::Debug:
          return "debug";
        case CliLogLevel::Info:
        default:
          return "info";
      }
    };

    const CliLogLevel saved_log_level = log_level_;
    if (log_level_ != CliLogLevel::Debug) {
      log_level_ = CliLogLevel::Debug;
    }

    io_.writeln("[MASTER][TEST] test.all dispatch sweep started");
    writef("  target=%s profile_id=0x%04X",
           target_peer_text.c_str(),
           static_cast<unsigned int>(remote_profile_id_));
    io_.writeln("  mode=non-destructive (dangerous commands are skipped)");
    io_.writeln("  detail=dispatch pass/fail now; async remote failures still print later as [MASTER][MGMT]/[MASTER][DESC]");

    auto hasMgmtQueuePending = [&]() -> bool {
      if (management_transport_ == nullptr) {
        return false;
      }
      return (management_transport_->pendingRequestCount() != 0U) ||
             (management_transport_->pendingResponseCount() != 0U) ||
             (management_transport_->pendingEventCount() != 0U);
    };

    auto pumpInfra = [&](uint32_t budget_ms) {
      const uint32_t start_ms = nowMs();
      while (static_cast<uint32_t>(nowMs() - start_ms) < budget_ms) {
        const uint32_t now = nowMs();
        if (management_runtime_ != nullptr) {
          management_runtime_->tick(now, 4U, 16U, 32U);
        }
        pumpManagementMailbox();
        pumpDescriptorQueue(now);
        if (!hasMgmtQueuePending() && descriptor_request_queue_.empty()) {
          break;
        }
      }
    };

    auto waitPagedFetch = [&](const char* label, uint32_t timeout_ms) -> bool {
      if (paged_fetch_kind_ == PagedFetchKind::None) {
        return true;
      }
      const uint32_t start_ms = nowMs();
      while (paged_fetch_kind_ != PagedFetchKind::None &&
             static_cast<uint32_t>(nowMs() - start_ms) < timeout_ms) {
        pumpInfra(40U);
      }
      if (paged_fetch_kind_ != PagedFetchKind::None) {
        writef("[MASTER][TEST][WARN] paged fetch still active after timeout: %s", label);
        return false;
      }
      return true;
    };

    uint32_t run_total = 0U;
    uint32_t run_pass = 0U;
    uint32_t run_fail = 0U;
    uint32_t skipped = 0U;

    for (size_t i = 0; i < (sizeof(kSteps) / sizeof(kSteps[0])); ++i) {
      const TestStep& step = kSteps[i];
      const std::string step_cmd(step.cmd);

      if (step_cmd == "settings.full" && remote_profile_id_ == kProfileUnknown) {
        pumpInfra(600U);
        if (remote_profile_id_ == kProfileUnknown) {
          ++skipped;
          writef("[MASTER][TEST][SKIP] #%u %s :: %s (profile still unknown after caps sync)",
                 static_cast<unsigned int>(i + 1U),
                 step.area,
                 step.cmd);
          continue;
        }
      }

      if (step_cmd == "radio.drytest" &&
          (hasMgmtQueuePending() || !descriptor_request_queue_.empty())) {
        ++skipped;
        writef("[MASTER][TEST][SKIP] #%u %s :: %s (management queues are busy)",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd);
        continue;
      }

      if (step.policy == StepPolicy::SkipRequiresActive &&
          step_cmd == "logger.remote.stop" &&
          !remote_log_pull_active_) {
        ++skipped;
        writef("[MASTER][TEST][SKIP] #%u %s :: %s (no active remote pull)",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd);
        continue;
      }

      if (step.policy != StepPolicy::Run) {
        if (step.policy == StepPolicy::SkipProfileChildOnly && child_profile) {
          // SEMU/REMU selected: run child-only steps when explicit args are fixed in step text.
        } else if (step.policy == StepPolicy::SkipRequiresActive &&
                   step_cmd == "logger.remote.stop" &&
                   remote_log_pull_active_) {
          // Active pull is present: run stop command as part of flow validation.
        } else {
          ++skipped;
          writef("[MASTER][TEST][SKIP] #%u %s :: %s (%s)",
                 static_cast<unsigned int>(i + 1U),
                 step.area,
                 step.cmd,
                 policyReason(step.policy));
          continue;
        }
      }

      if (step.needs_target && !has_runtime_peer) {
        ++skipped;
        writef("[MASTER][TEST][SKIP] #%u %s :: %s (target not selected)",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd);
        continue;
      }

      const std::string cmd_line = step.needs_target ? (target_prefix + step.cmd) : std::string(step.cmd);
      const CommandDispatchResult r = handleLineEx(cmd_line);

      ++run_total;
      bool ok = r.parsed && r.handled;
      if (ok && r.submitted) {
        ok = r.accepted;
      } else if (ok && !r.submitted && r.status != ManagementStatus::Ok) {
        ok = r.accepted;
      }

      if (ok) {
        ++run_pass;
        writef("[MASTER][TEST][PASS] #%u %s :: %s submitted=%s cmd=0x%04X req=%lu status=%s",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd,
               r.submitted ? "yes" : "no",
               static_cast<unsigned int>(r.cmd_id),
               static_cast<unsigned long>(r.req_id),
               management_utils::managementStatusToString(r.status));
      } else {
        ++run_fail;
        writef("[MASTER][TEST][FAIL] #%u %s :: %s submitted=%s accepted=%s status=%s stage=%s",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd,
               r.submitted ? "yes" : "no",
               r.accepted ? "yes" : "no",
               management_utils::managementStatusToString(r.status),
               r.reject_stage.empty() ? "n/a" : r.reject_stage.c_str());
      }

      if (step_cmd == "caps" || step_cmd == "telem" ||
          step_cmd == "settings" || step_cmd == "settings.raw") {
        (void)waitPagedFetch(step.cmd, 2500U);
      } else {
        pumpInfra(120U);
      }
    }

    log_level_ = saved_log_level;
    writef("[MASTER][TEST] summary run=%lu pass=%lu fail=%lu skipped=%lu",
           static_cast<unsigned long>(run_total),
           static_cast<unsigned long>(run_pass),
           static_cast<unsigned long>(run_fail),
           static_cast<unsigned long>(skipped));
    writef("[MASTER][TEST] log level restored=%s", logLevelLabel(log_level_));
    return true;
  }

  if (lower == "comm.test.status" || lower == "comm.test.report") {
    const char* probe_kind = "none";
    if (probe_pending_kind_ == ProbePendingKind::Live) {
      probe_kind = "live";
    } else if (probe_pending_kind_ == ProbePendingKind::Ping) {
      probe_kind = "ping";
    }
    MacAddress runtime_peer{};
    const bool has_runtime_peer = resolveRuntimePeer(runtime_peer);
    io_.writeln("[MASTER][CLI] comm.test status (this build uses selftest queue mode):");
    writef("  paired=%s peer=%s",
           manager_.isPaired() ? "yes" : "no",
           has_runtime_peer ? macToPrintable(runtime_peer).c_str() : "none");
    writef("  probe_pending=%s age_ms=%lu",
           probe_kind,
           (probe_pending_kind_ != ProbePendingKind::None) ? static_cast<unsigned long>(nowMs() - probe_sent_ms_) : 0UL);
    writef("  autopull=%s interval_ms=%lu live_online=%s",
           auto_pull_enabled_ ? "on" : "off",
           static_cast<unsigned long>(auto_pull_interval_ms_),
           auto_pull_.slaveOnline() ? "yes" : "no");
    io_.writeln("  trigger full flow with: test.all");
    printQueueStatus();
    return true;
  }

  if (lower == "radio.drytest") {
    if (management_ == nullptr) {
      io_.writeln("[MASTER][RADIO][DRYTEST] management service unavailable");
      return true;
    }
    if (management_transport_ == nullptr || management_runtime_ == nullptr) {
      io_.writeln("[MASTER][RADIO][DRYTEST] management runtime transport unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }

    auto stateLabel = [](ManagementService::RadioTransitionState s) -> const char* {
      switch (s) {
        case ManagementService::RadioTransitionState::Idle:
          return "idle";
        case ManagementService::RadioTransitionState::Quiescing:
          return "quiescing";
        case ManagementService::RadioTransitionState::Paused:
          return "paused";
        case ManagementService::RadioTransitionState::Resuming:
          return "resuming";
        case ManagementService::RadioTransitionState::Failed:
          return "failed";
        default:
          return "unknown";
      }
    };

    const size_t tx_req = management_transport_->pendingRequestCount();
    const size_t tx_resp = management_transport_->pendingResponseCount();
    const size_t tx_evt = management_transport_->pendingEventCount();
    const size_t svc_req = management_->pendingRequestCount();
    const size_t svc_resp = management_->pendingResponseCount();
    const size_t svc_evt = management_->pendingEventCount();
    if (tx_req != 0U || tx_resp != 0U || tx_evt != 0U || svc_req != 0U || svc_resp != 0U || svc_evt != 0U) {
      writef("[MASTER][RADIO][DRYTEST] busy queues tx(req=%u resp=%u evt=%u) svc(req=%u resp=%u evt=%u); rerun when idle",
             static_cast<unsigned int>(tx_req),
             static_cast<unsigned int>(tx_resp),
             static_cast<unsigned int>(tx_evt),
             static_cast<unsigned int>(svc_req),
             static_cast<unsigned int>(svc_resp),
             static_cast<unsigned int>(svc_evt));
      return true;
    }

    ManagementService::RadioTransitionStatus before{};
    management_->radioTransitionStatusGet(before);
    writef("[MASTER][RADIO][DRYTEST] pre active=%s state=%s epoch=%lu",
           before.active ? "yes" : "no",
           stateLabel(before.state),
           static_cast<unsigned long>(before.radio_epoch));
    if (before.active) {
      io_.writeln("[MASTER][RADIO][DRYTEST] abort: transition already active");
      return true;
    }

    const bool begin_ok = management_->beginRadioTransition();
    ManagementService::RadioTransitionStatus after_begin{};
    management_->radioTransitionStatusGet(after_begin);
    writef("[MASTER][RADIO][DRYTEST] begin=%s active=%s state=%s epoch=%lu",
           begin_ok ? "ok" : "fail",
           after_begin.active ? "yes" : "no",
           stateLabel(after_begin.state),
           static_cast<unsigned long>(after_begin.radio_epoch));
    if (!begin_ok) {
      io_.writeln("[MASTER][RADIO][DRYTEST] FAIL begin transition");
      return true;
    }

    uint32_t dry_req_id = 0U;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool submit_ok = submitRuntimeTargeted_(mgmt,
                                                  static_cast<uint16_t>(ManagementCommandId::DiscoveryStart),
                                                  management_utils::buildDiscoveryStartPayload(1000U),
                                                  &dry_req_id,
                                                  1000U,
                                                  false);
    correlation_id_ = mgmt.nextReqId();

    writef("[MASTER][RADIO][DRYTEST] blocked_cmd=DiscoveryStart submit=%s req=%lu path=%s",
           submit_ok ? "ok" : "fail",
           static_cast<unsigned long>(dry_req_id),
           "queue_runtime");

    bool status_seen = false;
    ManagementStatus status_value = ManagementStatus::InternalError;
    const uint32_t deadline_ms = nowMs() + 1500U;
    while (!status_seen && static_cast<int32_t>(nowMs() - deadline_ms) < 0) {
      management_runtime_->tick(nowMs(), 4U, 16U, 32U);

      ManagementResponse resp{};
      while (management_transport_->pollResponse(resp)) {
        if (resp.req_id == dry_req_id) {
          status_seen = true;
          status_value = resp.status;
          break;
        }
      }
      if (status_seen) {
        break;
      }

      ManagementEvent evt{};
      while (management_transport_->pollEvent(evt)) {
        if (evt.req_id == dry_req_id) {
          status_seen = true;
          status_value = evt.status;
          break;
        }
      }
    }

    const bool end_ok = management_->endRadioTransition();
    ManagementService::RadioTransitionStatus after_end{};
    management_->radioTransitionStatusGet(after_end);
    writef("[MASTER][RADIO][DRYTEST] end=%s active=%s state=%s epoch=%lu",
           end_ok ? "ok" : "fail",
           after_end.active ? "yes" : "no",
           stateLabel(after_end.state),
           static_cast<unsigned long>(after_end.radio_epoch));

    const bool blocked_ok = submit_ok &&
                            status_seen &&
                            status_value == ManagementStatus::BusyRadioTransition;
    writef("[MASTER][RADIO][DRYTEST] blocked_status seen=%s value=%s(0x%04X)",
           status_seen ? "yes" : "no",
           management_utils::managementStatusToString(status_value),
           static_cast<unsigned int>(status_value));

    const bool pass = begin_ok && blocked_ok && end_ok && !after_end.active;
    io_.writeln(pass ? "[MASTER][RADIO][DRYTEST] PASS" : "[MASTER][RADIO][DRYTEST] FAIL");
    return true;
  }

  if (lower == "time.local") {
    const uint64_t now_s = static_cast<uint64_t>(std::time(nullptr));
    writef("[MASTER][TIME] local_epoch_s=%llu", static_cast<unsigned long long>(now_s));
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  return false;
}

bool MasterCli::handleRestartResetCommands(const std::string& lower) {
  if (lower == "restart master" || lower == "reset master") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    const bool reset = (lower == "reset master");
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool submitted = reset
                               ? submitRuntimeTargeted_(mgmt,
                                                        static_cast<uint16_t>(ManagementCommandId::ResetMasterRequest),
                                                        {},
                                                        nullptr,
                                                        0U,
                                                        false)
                               : submitRuntimeTargeted_(mgmt,
                                                        static_cast<uint16_t>(ManagementCommandId::RestartMasterRequest),
                                                        {},
                                                        nullptr,
                                                        0U,
                                                        false);
    correlation_id_ = mgmt.nextReqId();
    if (submitted) {
      io_.writeln("[MASTER][CLI] master restart/reset requested via management");
    } else {
      io_.writeln("[MASTER][CLI] master restart/reset request failed (management queue full)");
    }
    return true;
  }

  if (lower == "restart slave" || lower == "reset slave") {
    if (!hasRuntimePeer()) {
      io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool sent = (lower == "restart slave")
                          ? submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::RestartSlaveRequest))
                          : submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::ResetSlaveRequest));
    correlation_id_ = mgmt.nextReqId();
    MacAddress runtime_peer{};
    (void)resolveRuntimePeer(runtime_peer);

    writef("[MASTER][CLI] %s slave request cmd=%s peer=%s",
           (lower == "reset slave") ? "reset" : "restart",
           sent ? "sent" : "send-failed/queue-full",
           macToPrintable(runtime_peer).c_str());
    return true;
  }

  return false;
}
  
bool MasterCli::handleLogLevelCommand(const std::string& lower) {
  if (lower == "log") {
    const char* lvl = (log_level_ == CliLogLevel::Debug)
                          ? "debug"
                          : (log_level_ == CliLogLevel::Info ? "info" : "error");
    writef("[MASTER][CLI] log level=%s", lvl);
    return true;
  }

  if (lower == "log error") {
    log_level_ = CliLogLevel::Error;
    io_.writeln("[MASTER][CLI] log level=error");
    return true;
  }
  if (lower == "log info") {
    log_level_ = CliLogLevel::Info;
    io_.writeln("[MASTER][CLI] log level=info");
    return true;
  }
  if (lower == "log debug") {
    log_level_ = CliLogLevel::Debug;
    io_.writeln("[MASTER][CLI] log level=debug");
    return true;
  }

  return false;
}

bool MasterCli::handleCliMetaCommands(const std::string& lower) {
  if (lower == "help") {
    printHelp();
    return true;
  }

  if (startsWith(lower, "help ")) {
    const std::string topic = trim(lower.substr(5));
    if (topic.empty()) {
      printHelp();
      return true;
    }
    if (!printTopicHelp(topic)) {
      writef("[MASTER][CLI] unknown help topic: %s", topic.c_str());
      io_.writeln("  run: help");
      io_.writeln("  topics: core paired pairing target topology desc settings push time control test log logger sd ota");
    }
    return true;
  }

  static constexpr const char* kHelpSuffix = " help";
  if (lower.size() > std::strlen(kHelpSuffix) &&
      lower.compare(lower.size() - std::strlen(kHelpSuffix), std::strlen(kHelpSuffix), kHelpSuffix) == 0) {
    const std::string topic = trim(lower.substr(0U, lower.size() - std::strlen(kHelpSuffix)));
    if (topic.empty()) {
      printHelp();
      return true;
    }
    if (!printTopicHelp(topic)) {
      writef("[MASTER][CLI] unknown help topic: %s", topic.c_str());
      io_.writeln("  run: help");
      io_.writeln("  topics: core paired pairing target topology desc settings push time control test log logger sd ota");
    }
    return true;
  }

  if (handleLogLevelCommand(lower)) {
    return true;
  }

  if (lower == "queue") {
    printQueueStatus();
    return true;
  }

  if (lower == "cli status") {
    const char* lvl = (log_level_ == CliLogLevel::Debug)
                          ? "debug"
                          : (log_level_ == CliLogLevel::Info ? "info" : "error");
    writef("[MASTER][CLI] enabled=%s key=%s log=%s",
           cli_enabled_ ? "yes" : "no",
           enable_key_.c_str(),
           lvl);
    printQueueStatus();
    return true;
  }

  if (lower == "cli on") {
    if (!setCliEnabled(true, true)) {
      io_.writeln("[MASTER][CLI] failed to persist enable state");
    }
    io_.writeln("[MASTER][CLI] enabled");
    return true;
  }

  if (lower == "metrics") {
    const ManagerRuntimeMetrics& m = manager_.runtimeMetrics();
    io_.writeln("[MASTER][METRICS] counters:");
    writef("  tick_count=%llu", static_cast<unsigned long long>(m.tick_count));
    writef("  rx_frames=%llu rx_bytes=%llu", static_cast<unsigned long long>(m.rx_frames), static_cast<unsigned long long>(m.rx_bytes));
    writef("  tx_frames=%llu tx_bytes=%llu tx_failures=%llu",
           static_cast<unsigned long long>(m.tx_frames),
           static_cast<unsigned long long>(m.tx_bytes),
           static_cast<unsigned long long>(m.tx_failures));
    io_.writeln("[MASTER][METRICS] timing_us:");
    writef("  tick last=%lu max=%lu avg=%lu",
           static_cast<unsigned long>(m.tick_last_us),
           static_cast<unsigned long>(m.tick_max_us),
           static_cast<unsigned long>((m.tick_count == 0) ? 0 : (m.tick_total_us / m.tick_count)));
    writef("  rx   last=%lu max=%lu avg=%lu",
           static_cast<unsigned long>(m.rx_handler_last_us),
           static_cast<unsigned long>(m.rx_handler_max_us),
           static_cast<unsigned long>((m.rx_frames == 0) ? 0 : (m.rx_handler_total_us / m.rx_frames)));
    writef("  tx   last=%lu max=%lu avg=%lu",
           static_cast<unsigned long>(m.tx_send_last_us),
           static_cast<unsigned long>(m.tx_send_max_us),
           static_cast<unsigned long>((m.tx_frames == 0) ? 0 : (m.tx_send_total_us / m.tx_frames)));

    if (management_runtime_ != nullptr) {
      const ManagementRuntime::Stats& ms = management_runtime_->stats();
      io_.writeln("[MASTER][METRICS][MGMT] runtime:");
      writef("  submitted=%lu dropped_req=%lu",
             static_cast<unsigned long>(ms.submitted_requests),
             static_cast<unsigned long>(ms.dropped_requests));
      writef("  dropped_req.service_rejected=%lu",
             static_cast<unsigned long>(ms.dropped_requests_service_rejected));
      writef("  dispatched_resp=%lu dropped_resp=%lu",
             static_cast<unsigned long>(ms.dispatched_responses),
             static_cast<unsigned long>(ms.dropped_responses));
      writef("  dropped_resp.no_route=%lu dropped_resp.transport_rejected=%lu",
             static_cast<unsigned long>(ms.dropped_responses_no_route),
             static_cast<unsigned long>(ms.dropped_responses_transport_rejected));
      writef("  dispatched_evt=%lu dropped_evt=%lu transports=%u",
             static_cast<unsigned long>(ms.dispatched_events),
             static_cast<unsigned long>(ms.dropped_events),
             static_cast<unsigned int>(management_runtime_->transportCount()));
      writef("  dropped_evt.no_route=%lu dropped_evt.transport_rejected=%lu",
             static_cast<unsigned long>(ms.dropped_events_no_route),
             static_cast<unsigned long>(ms.dropped_events_transport_rejected));
    }
    if (management_transport_ != nullptr) {
      io_.writeln("[MASTER][METRICS][MGMT] cli_queue:");
      writef("  req=%u resp=%u evt=%u",
             static_cast<unsigned int>(management_transport_->pendingRequestCount()),
             static_cast<unsigned int>(management_transport_->pendingResponseCount()),
             static_cast<unsigned int>(management_transport_->pendingEventCount()));
      const ManagementQueueTransport::QueueStats& req_stats = management_transport_->requestStats();
      const ManagementQueueTransport::QueueStats& resp_stats = management_transport_->responseStats();
      const ManagementQueueTransport::QueueStats& evt_stats = management_transport_->eventStats();
      writef("  req_stats enq=%lu rej_new=%lu drop_oldest=%lu",
             static_cast<unsigned long>(req_stats.enqueued),
             static_cast<unsigned long>(req_stats.rejected_new),
             static_cast<unsigned long>(req_stats.dropped_oldest));
      writef("  resp_stats enq=%lu rej_new=%lu drop_oldest=%lu",
             static_cast<unsigned long>(resp_stats.enqueued),
             static_cast<unsigned long>(resp_stats.rejected_new),
             static_cast<unsigned long>(resp_stats.dropped_oldest));
      writef("  evt_stats enq=%lu rej_new=%lu drop_oldest=%lu",
             static_cast<unsigned long>(evt_stats.enqueued),
             static_cast<unsigned long>(evt_stats.rejected_new),
             static_cast<unsigned long>(evt_stats.dropped_oldest));
    }
    if (management_ != nullptr) {
      io_.writeln("[MASTER][METRICS][MGMT] service_queue:");
      writef("  req=%u resp=%u evt=%u",
             static_cast<unsigned int>(management_->pendingRequestCount()),
             static_cast<unsigned int>(management_->pendingResponseCount()),
             static_cast<unsigned int>(management_->pendingEventCount()));
    }
    return true;
  }

  if (lower == "metrics.reset") {
    manager_.resetRuntimeMetrics();
    if (management_runtime_ != nullptr) {
      management_runtime_->resetStats();
    }
    if (management_transport_ != nullptr) {
      management_transport_->resetStats();
    }
    io_.writeln("[MASTER][METRICS] reset");
    return true;
  }

  if (lower == "cli off") {
    if (!setCliEnabled(false, true)) {
      io_.writeln("[MASTER][CLI] failed to persist enable state");
    }
    return true;
  }

  return false;
}
bool MasterCli::handleListAndStatusCommands(const std::string& lower) {
  if (lower == "list") {
    constexpr uint32_t kListWindowMs = 10000U;
    discovered_.clear();
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }

    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    uint32_t req_id = 0U;
    if (!submitRuntimeTargeted_(mgmt,
                                static_cast<uint16_t>(ManagementCommandId::DiscoveryStart),
                                management_utils::buildDiscoveryStartPayload(kListWindowMs),
                                &req_id,
                                0U,
                                false)) {
      io_.writeln("[MASTER][CLI] discovery window start failed");
      return true;
    }
    correlation_id_ = mgmt.nextReqId();
    collect_discovery_ = true;
    list_window_active_ = true;
    list_window_deadline_ms_ = nowMs() + kListWindowMs;
    writef("[MASTER][CLI] discovery window started (10s) req=%lu",
           static_cast<unsigned long>(req_id));
    return true;
  }

  if (lower == "paired" || lower == "paired.list") {
    std::vector<MacAddress> persisted{};
    manager_.getPersistedPeers(persisted);
    MacAddress active{};
    const bool has_active = resolveRuntimePeer(active);
    writef("[MASTER][CLI] paired_slots=%u/14",
           static_cast<unsigned int>(persisted.size()));
    if (persisted.empty()) {
      io_.writeln("[MASTER][CLI] no persisted paired peer");
      return true;
    }
    io_.writeln("[MASTER][CLI] persisted paired peers:");
    for (size_t i = 0; i < persisted.size(); ++i) {
      const bool is_active = has_active && persisted[i] == active;
      writef("  %u) %s%s",
             static_cast<unsigned int>(i),
             macToPrintable(persisted[i]).c_str(),
             is_active ? "  [active]" : "");
    }
    return true;
  }

  if (lower == "status") {
    const size_t paired_slots = manager_.persistedPairCount();
    writef("[MASTER][CLI] paired=%s",
           manager_.isPaired() ? "yes" : "no");
    writef("[MASTER][CLI] paired_slots=%u/14",
           static_cast<unsigned int>(paired_slots));

    io_.writeln("[MASTER][CLI] target_mode=explicit_selector_only");

    MacAddress runtime_target{};
    if (resolveRuntimePeer(runtime_target)) {
      writef("[MASTER][CLI] runtime_target=%s", macToPrintable(runtime_target).c_str());
    } else {
      io_.writeln("[MASTER][CLI] runtime_target=none");
    }

    if (list_window_active_) {
      const uint32_t now_ms = nowMs();
      const uint32_t remaining_ms = (list_window_deadline_ms_ > now_ms)
                                        ? (list_window_deadline_ms_ - now_ms)
                                        : 0U;
      writef("[MASTER][CLI] discovery_window=active remaining_ms=%lu",
             static_cast<unsigned long>(remaining_ms));
    } else {
      io_.writeln("[MASTER][CLI] discovery_window=idle");
    }

    printQueueStatus();
    if (logEnabled(CliLogLevel::Debug)) {
      writef("[MASTER][CLI] queue_budget_per_tick=%u", static_cast<unsigned int>(descriptor_send_budget_per_tick_));
    }
    return true;
  }

  return false;
}

bool MasterCli::handlePairingCommands(const std::string& line, const std::string& lower) {
  if (startsWith(lower, "pair")) {
    std::string arg = trim(line.substr(4));
    if (arg.empty()) {
      io_.writeln("[MASTER][CLI] usage: pair <index|mac>  (index: discovered first, then persisted)");
      return true;
    }
    MacAddress target{};
    bool ok = false;
    bool all_digits = true;
    for (char c : arg) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        all_digits = false;
        break;
      }
    }
    if (all_digits) {
      const int idx = std::atoi(arg.c_str());
      if (idx >= 0) {
        ok = peerByIndex(static_cast<size_t>(idx), target);
        if (!ok) {
          std::vector<MacAddress> persisted{};
          manager_.getPersistedPeers(persisted);
          if (static_cast<size_t>(idx) < persisted.size()) {
            target = persisted[static_cast<size_t>(idx)];
            ok = true;
          }
        }
      }
    } else {
      ok = parseMac(arg, target);
    }
    if (!ok) {
      io_.writeln("[MASTER][CLI] invalid peer selector");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool submitted = submitRuntimeTargeted_(mgmt,
                                                  static_cast<uint16_t>(ManagementCommandId::PairRequest),
                                                  std::vector<uint8_t>(target.begin(), target.end()),
                                                  nullptr,
                                                  0U,
                                                  false);
    correlation_id_ = mgmt.nextReqId();
    if (submitted) {
      writef("[MASTER][CLI] pair requested with %s", macToPrintable(target).c_str());
      auto_pull_.resetState();
    } else {
      io_.writeln("[MASTER][CLI] pair request failed");
    }
    return true;
  }

  if (lower == "unpair") {
    if (!hasRuntimePeer()) {
      io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool submitted = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::UnpairRequest));
    correlation_id_ = mgmt.nextReqId();
    if (submitted) {
      io_.writeln("[MASTER][CLI] unpair requested");
    } else {
      io_.writeln("[MASTER][CLI] unpair failed");
    }
    return true;
  }

  if (lower == "remove" || startsWith(lower, "remove ")) {
    std::string arg = trim(line.substr(6));
    MacAddress target{};
    bool has_target = false;

    if (arg.empty() || arg == "slave" || arg == "peer") {
      has_target = resolveRuntimePeer(target);
    } else {
      bool all_digits = true;
      for (char c : arg) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
          all_digits = false;
          break;
        }
      }
      if (all_digits) {
        const int idx = std::atoi(arg.c_str());
        if (idx >= 0) {
          has_target = peerByIndex(static_cast<size_t>(idx), target);
          if (!has_target) {
            std::vector<MacAddress> persisted{};
            manager_.getPersistedPeers(persisted);
            if (static_cast<size_t>(idx) < persisted.size()) {
              target = persisted[static_cast<size_t>(idx)];
              has_target = true;
            }
          }
        }
      } else {
        has_target = parseMac(arg, target);
      }
    }

    if (!has_target) {
      io_.writeln("[MASTER][CLI] usage: remove [index|AA:BB:CC:DD:EE:FF|slave]  (index: discovered first, then persisted)");
      return true;
    }

    MacAddress runtime_peer{};
    const bool is_runtime_peer = resolveRuntimePeer(runtime_peer) && (runtime_peer == target);
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool removed = submitRuntimeTargeted_(mgmt,
                                                static_cast<uint16_t>(ManagementCommandId::RemovePeerRequest),
                                                management_utils::buildMacPayload(target),
                                                nullptr,
                                                0U,
                                                false);
    correlation_id_ = mgmt.nextReqId();
    if (removed) {
      if (is_runtime_peer) {
        clearPeerSessionState_();
      }
      writef("[MASTER][CLI] remove peer=%s requested (management)", macToPrintable(target).c_str());
    } else {
      io_.writeln("[MASTER][CLI] remove request failed (management queue full)");
    }
    return true;
  }

  return false;
}

bool MasterCli::handleTopologyCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "topology.")) {
    return false;
  }
  const bool is_topology_edit_cmd =
      startsWith(lower, "topology.edit.") || startsWith(lower, "topology.file.show");
  if (!is_topology_edit_cmd && management_transport_ == nullptr) {
    io_.writeln("[MASTER][TOPO] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }

  std::string cmd_line = line;
  std::string cmd_lower = lower;
  bool force_local = false;
  static constexpr const char* kLocalPrefix = "topology.local.";
  if (startsWith(cmd_lower, kLocalPrefix)) {
    const size_t p = std::strlen(kLocalPrefix);
    cmd_line = "topology." + cmd_line.substr(p);
    cmd_lower = "topology." + cmd_lower.substr(p);
    force_local = true;
  }

  auto makeController = [&]() {
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    return mgmt;
  };
  auto submitTopo = [&](ManagementController& mgmt,
                        ManagementCommandId command_id,
                        const std::vector<uint8_t>& payload = {},
                        uint32_t* out_req_id = nullptr) -> bool {
    return submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(command_id),
                                  payload,
                                  out_req_id,
                                  0U,
                                  !force_local);
  };

  auto normalizeTopoPath = [](const std::string& raw) -> std::string {
    const std::string p = trim(raw);
    if (p.empty()) {
      return std::string();
    }
    if (p[0] == '/') {
      return p;
    }
    return std::string("/o/s/") + p;
  };

  auto loadSnapshotFromPayload = [&](const std::vector<uint8_t>& payload,
                                     ManagementTopologySnapshotPayload& out_snapshot) -> bool {
    out_snapshot = ManagementTopologySnapshotPayload{};
    if (!management_utils::parseTopologyStagePayload(payload, out_snapshot)) {
      io_.writeln("[MASTER][TOPO] invalid topology payload");
      return false;
    }
    return true;
  };

  auto loadSnapshotFromHex = [&](const std::string& hex_blob,
                                 ManagementTopologySnapshotPayload& out_snapshot) -> bool {
    std::vector<uint8_t> payload{};
    if (!parseHexPayload(hex_blob, payload)) {
      io_.writeln("[MASTER][TOPO] invalid hex payload");
      return false;
    }
    return loadSnapshotFromPayload(payload, out_snapshot);
  };

  auto loadSnapshotFromFile = [&](const std::string& path_token,
                                  ManagementTopologySnapshotPayload& out_snapshot) -> bool {
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO] local storage backend unavailable");
      return false;
    }
    const std::string path = normalizeTopoPath(path_token);
    if (path.empty()) {
      io_.writeln("[MASTER][TOPO] usage: topology.stage.file <path>");
      return false;
    }
    std::vector<uint8_t> payload{};
    std::string msg;
    if (!readBinaryFileLocal(*ota_push_storage_, path, payload, msg)) {
      writef("[MASTER][TOPO] file read failed path=%s reason=%s", path.c_str(), msg.c_str());
      return false;
    }
    if (!loadSnapshotFromPayload(payload, out_snapshot)) {
      writef("[MASTER][TOPO] payload decode failed path=%s", path.c_str());
      return false;
    }
    return true;
  };

  struct TopologyEditNodeState {
    ChainNodeType type = ChainNodeType::Sensor;
    MacAddress mac{};
    int32_t vi = -1;
  };
  struct TopologyEditState {
    bool initialized = false;
    uint32_t topology_version = 1U;
    std::vector<uint32_t> seeds{};
    std::vector<TopologyEditNodeState> chain{};
  };
  static TopologyEditState topology_edit{};
  if (!topology_edit.initialized) {
    topology_edit.initialized = true;
    topology_edit.topology_version = static_cast<uint32_t>(std::time(nullptr));
    if (topology_edit.topology_version == 0U) {
      topology_edit.topology_version = 1U;
    }
  }

  auto chainTypeToken = [](ChainNodeType type) -> const char* {
    switch (type) {
      case ChainNodeType::Sensor:
        return "S";
      case ChainNodeType::Relay:
        return "R";
      case ChainNodeType::SemuChild:
        return "SM";
      case ChainNodeType::RemuChild:
        return "RM";
    }
    return "?";
  };

  auto parseI32Token = [](const std::string& token, int32_t& out) -> bool {
    if (token.empty()) {
      return false;
    }
    char* end = nullptr;
    const long v = std::strtol(token.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
      return false;
    }
    if (v < static_cast<long>(std::numeric_limits<int32_t>::min()) ||
        v > static_cast<long>(std::numeric_limits<int32_t>::max())) {
      return false;
    }
    out = static_cast<int32_t>(v);
    return true;
  };

  auto parseSeedCsv = [&](const std::string& csv, std::vector<uint32_t>& out) -> bool {
    out.clear();
    std::string item;
    for (size_t i = 0; i <= csv.size(); ++i) {
      const bool at_end = (i == csv.size());
      const char c = at_end ? ',' : csv[i];
      if (c == ',') {
        const std::string token = trim(item);
        item.clear();
        if (token.empty()) {
          return false;
        }
        uint32_t seed = 0U;
        if (!parseU32Token(token, seed) || seed == 0U) {
          return false;
        }
        out.push_back(seed);
        continue;
      }
      item.push_back(c);
    }
    return !out.empty();
  };

  auto resolveTopologyPeerToken = [&](const std::string& token, MacAddress& out_mac) -> bool {
    if (parseMac(token, out_mac)) {
      return true;
    }
    char* end = nullptr;
    const long idx = std::strtol(token.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || idx < 0) {
      return false;
    }
    std::vector<MacAddress> paired{};
    manager_.getPersistedPeers(paired);
    if (static_cast<size_t>(idx) >= paired.size()) {
      return false;
    }
    out_mac = paired[static_cast<size_t>(idx)];
    return true;
  };

  auto countRelayBlocks = [&](const std::vector<TopologyEditNodeState>& chain) -> size_t {
    size_t group_count = 0U;
    bool in_relay_block = false;
    for (const auto& node : chain) {
      const bool relay = isChainRelay(node.type);
      if (relay && !in_relay_block) {
        ++group_count;
      }
      in_relay_block = relay;
    }
    return group_count;
  };

  auto resolveEditorSeeds = [&](size_t group_count,
                                std::vector<uint32_t>& out_seeds,
                                bool& auto_seeded) {
    auto_seeded = false;
    out_seeds.clear();
    if (group_count == 0U) {
      return;
    }
    out_seeds.reserve(group_count);
    for (size_t i = 0; i < group_count; ++i) {
      uint32_t seed = static_cast<uint32_t>(101U + i);
      if (i < topology_edit.seeds.size() && topology_edit.seeds[i] != 0U) {
        seed = topology_edit.seeds[i];
      } else {
        auto_seeded = true;
      }
      out_seeds.push_back(seed);
    }
    if (topology_edit.seeds.size() != group_count) {
      auto_seeded = true;
    }
  };

  auto buildEditorJson = [&](const std::vector<uint32_t>& seeds) -> std::string {
    std::string json;
    json += "{\n";
    json += "  \"v\": 2,\n";
    json += "  \"topo_ver\": " + std::to_string(topology_edit.topology_version) + ",\n";
    json += "  \"seed\": [";
    for (size_t i = 0; i < seeds.size(); ++i) {
      if (i > 0U) {
        json += ", ";
      }
      json += std::to_string(seeds[i]);
    }
    json += "],\n";
    json += "  \"chain\": [\n";
    for (size_t i = 0; i < topology_edit.chain.size(); ++i) {
      const auto& node = topology_edit.chain[i];
      json += "    { \"t\": \"";
      json += chainTypeToken(node.type);
      json += "\", \"m\": \"";
      json += macToPrintable(node.mac);
      json += "\", \"vi\": ";
      json += std::to_string(node.vi);
      json += " }";
      if (i + 1U < topology_edit.chain.size()) {
        json += ",";
      }
      json += "\n";
    }
    json += "  ]\n";
    json += "}\n";
    return json;
  };

  auto buildValidatedEditorJson = [&](std::string& out_json,
                                      std::vector<uint32_t>& out_resolved_seeds,
                                      uint32_t& out_topology_version,
                                      size_t& out_group_count,
                                      size_t& out_target_count,
                                      std::string& out_error) -> bool {
    out_json.clear();
    out_resolved_seeds.clear();
    out_topology_version = topology_edit.topology_version;
    out_group_count = 0U;
    out_target_count = 0U;
    out_error.clear();

    out_group_count = countRelayBlocks(topology_edit.chain);
    bool auto_seeded = false;
    resolveEditorSeeds(out_group_count, out_resolved_seeds, auto_seeded);
    (void)auto_seeded;
    out_json = buildEditorJson(out_resolved_seeds);

    uint32_t parsed_topology_version = out_topology_version;
    std::vector<TopologyDeployTarget> targets{};
    std::string parse_error;
    if (!parseTopologyChainJson(out_json,
                                parsed_topology_version,
                                targets,
                                parsed_topology_version,
                                parse_error,
                                nullptr)) {
      out_error = parse_error;
      return false;
    }
    out_topology_version = parsed_topology_version;
    out_target_count = targets.size();
    return true;
  };

  auto loadEditorFromJson = [&](const std::string& json_text, std::string& out_error) -> bool {
    out_error.clear();

    uint32_t schema_version = 0U;
    if (!extractJsonU32Value(json_text, "v", schema_version) || schema_version != 2U) {
      out_error = "schema version invalid (expected v=2)";
      return false;
    }

    uint32_t topology_version = static_cast<uint32_t>(std::time(nullptr));
    if (topology_version == 0U) {
      topology_version = 1U;
    }
    uint32_t topo_ver_from_json = 0U;
    if (extractJsonU32Value(json_text, "topo_ver", topo_ver_from_json) &&
        topo_ver_from_json != 0U) {
      topology_version = topo_ver_from_json;
    }

    std::vector<uint32_t> seeds{};
    if (!parseJsonU32Array(json_text, "seed", seeds)) {
      out_error = "seed array missing or invalid";
      return false;
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

    std::vector<TopologyEditNodeState> chain{};
    chain.reserve(chain_items.size());
    for (const std::string& item : chain_items) {
      std::string type_token;
      std::string mac_token;
      int32_t vi = -1;
      if (!extractJsonStringValue(item, "t", type_token) ||
          !extractJsonStringValue(item, "m", mac_token) ||
          !extractJsonI32Value(item, "vi", vi)) {
        out_error = "chain node missing t/m/vi";
        return false;
      }

      TopologyEditNodeState node{};
      if (!parseChainNodeType(type_token, node.type)) {
        out_error = "chain node type invalid";
        return false;
      }
      if (!parseMac(mac_token, node.mac)) {
        out_error = "chain node mac invalid";
        return false;
      }
      node.vi = vi;

      if ((node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) && node.vi != -1) {
        out_error = "physical node vi must be -1";
        return false;
      }
      if (node.type == ChainNodeType::SemuChild && (node.vi < 0 || node.vi > 7)) {
        out_error = "semu vi out of range (0..7)";
        return false;
      }
      if (node.type == ChainNodeType::RemuChild && (node.vi < 0 || node.vi > 15)) {
        out_error = "remu vi out of range (0..15)";
        return false;
      }
      chain.push_back(node);
    }

    uint32_t validated_topology_version = topology_version;
    std::vector<TopologyDeployTarget> targets{};
    std::string parse_error;
    if (!parseTopologyChainJson(json_text,
                                validated_topology_version,
                                targets,
                                validated_topology_version,
                                parse_error,
                                nullptr)) {
      out_error = parse_error;
      return false;
    }

    topology_edit.topology_version = validated_topology_version;
    topology_edit.seeds = seeds;
    topology_edit.chain = chain;
    return true;
  };

  if (cmd_lower == "topology.edit.help") {
    io_.writeln("[MASTER][TOPO][EDIT] commands:");
    io_.writeln("  topology.edit.new [topo_ver] [seed_csv]");
    io_.writeln("  topology.edit.add <S|R|SM|RM> <paired_index|MAC> [vi]");
    io_.writeln("  topology.edit.del <chain_pos>");
    io_.writeln("  topology.edit.clear");
    io_.writeln("  topology.edit.show");
    io_.writeln("  topology.edit.validate");
    io_.writeln("  topology.edit.save [path]");
    io_.writeln("  topology.edit.load [path]");
    io_.writeln("  topology.file.show [path]");
    io_.writeln("[MASTER][TOPO][EDIT] rules: start/end with S|SM, no adjacent S|SM, R and RM adjacency allowed");
    return true;
  }

  if (cmd_lower == "topology.file.show" || startsWith(cmd_lower, "topology.file.show ")) {
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO][EDIT] local storage backend unavailable");
      return true;
    }
    std::string path_token = trim(cmd_line.substr(std::strlen("topology.file.show")));
    if (path_token.empty()) {
      path_token = "/o/s/topology_chain.json";
    }
    const std::string path = normalizeTopoPath(path_token);
    std::string json_text;
    std::string read_msg;
    if (!readTextFileLocal(*ota_push_storage_, path, json_text, read_msg)) {
      writef("[MASTER][TOPO][EDIT] file show failed path=%s reason=%s",
             path.c_str(),
             read_msg.c_str());
      return true;
    }
    writef("[MASTER][TOPO][EDIT] file=%s bytes=%u",
           path.c_str(),
           static_cast<unsigned int>(json_text.size()));
    io_.writeln(json_text);
    return true;
  }

  if (cmd_lower == "topology.edit.new" || startsWith(cmd_lower, "topology.edit.new ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() > 3U) {
      io_.writeln("[MASTER][TOPO][EDIT] usage: topology.edit.new [topo_ver] [seed_csv]");
      return true;
    }
    topology_edit.chain.clear();
    topology_edit.seeds.clear();
    topology_edit.topology_version = static_cast<uint32_t>(std::time(nullptr));
    if (topology_edit.topology_version == 0U) {
      topology_edit.topology_version = 1U;
    }

    if (tok.size() >= 2U) {
      uint32_t topo_ver = 0U;
      if (!parseU32Token(tok[1], topo_ver) || topo_ver == 0U) {
        io_.writeln("[MASTER][TOPO][EDIT] invalid topo_ver");
        return true;
      }
      topology_edit.topology_version = topo_ver;
    }
    if (tok.size() >= 3U) {
      std::vector<uint32_t> parsed_seeds{};
      if (!parseSeedCsv(tok[2], parsed_seeds)) {
        io_.writeln("[MASTER][TOPO][EDIT] invalid seed_csv (example: 101,102,103)");
        return true;
      }
      topology_edit.seeds = parsed_seeds;
    }
    writef("[MASTER][TOPO][EDIT] new editor topo_ver=%lu seeds=%u",
           static_cast<unsigned long>(topology_edit.topology_version),
           static_cast<unsigned int>(topology_edit.seeds.size()));
    return true;
  }

  if (cmd_lower == "topology.edit.clear") {
    topology_edit.chain.clear();
    writef("[MASTER][TOPO][EDIT] chain cleared topo_ver=%lu seeds=%u",
           static_cast<unsigned long>(topology_edit.topology_version),
           static_cast<unsigned int>(topology_edit.seeds.size()));
    return true;
  }

  if (startsWith(cmd_lower, "topology.edit.add ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() < 3U || tok.size() > 4U) {
      io_.writeln("[MASTER][TOPO][EDIT] usage: topology.edit.add <S|R|SM|RM> <paired_index|MAC> [vi]");
      return true;
    }

    TopologyEditNodeState node{};
    if (!parseChainNodeType(tok[1], node.type)) {
      io_.writeln("[MASTER][TOPO][EDIT] invalid type (use S|R|SM|RM)");
      return true;
    }
    if (!resolveTopologyPeerToken(tok[2], node.mac)) {
      io_.writeln("[MASTER][TOPO][EDIT] invalid peer selector (use paired index or MAC)");
      return true;
    }

    node.vi = -1;
    if (tok.size() >= 4U) {
      if (!parseI32Token(tok[3], node.vi)) {
        io_.writeln("[MASTER][TOPO][EDIT] invalid vi");
        return true;
      }
    }
    if ((node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay)) {
      if (tok.size() >= 4U || node.vi != -1) {
        io_.writeln("[MASTER][TOPO][EDIT] S/R nodes require vi=-1 and no explicit vi argument");
        return true;
      }
    }
    if (node.type == ChainNodeType::SemuChild) {
      if (tok.size() < 4U || node.vi < 0 || node.vi > 7) {
        io_.writeln("[MASTER][TOPO][EDIT] SM requires vi in range 0..7");
        return true;
      }
    }
    if (node.type == ChainNodeType::RemuChild) {
      if (tok.size() < 4U || node.vi < 0 || node.vi > 15) {
        io_.writeln("[MASTER][TOPO][EDIT] RM requires vi in range 0..15");
        return true;
      }
    }

    topology_edit.chain.push_back(node);
    writef("[MASTER][TOPO][EDIT] add pos=%u type=%s peer=%s vi=%d",
           static_cast<unsigned int>(topology_edit.chain.size() - 1U),
           chainTypeToken(node.type),
           macToPrintable(node.mac).c_str(),
           static_cast<int>(node.vi));
    return true;
  }

  if (startsWith(cmd_lower, "topology.edit.del ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() != 2U) {
      io_.writeln("[MASTER][TOPO][EDIT] usage: topology.edit.del <chain_pos>");
      return true;
    }
    uint32_t pos = 0U;
    if (!parseU32Token(tok[1], pos)) {
      io_.writeln("[MASTER][TOPO][EDIT] invalid chain_pos");
      return true;
    }
    if (pos >= topology_edit.chain.size()) {
      writef("[MASTER][TOPO][EDIT] chain_pos out of range (size=%u)",
             static_cast<unsigned int>(topology_edit.chain.size()));
      return true;
    }
    topology_edit.chain.erase(topology_edit.chain.begin() + pos);
    writef("[MASTER][TOPO][EDIT] deleted chain_pos=%u now_nodes=%u",
           static_cast<unsigned int>(pos),
           static_cast<unsigned int>(topology_edit.chain.size()));
    return true;
  }

  if (cmd_lower == "topology.edit.show") {
    const size_t relay_groups = countRelayBlocks(topology_edit.chain);
    writef("[MASTER][TOPO][EDIT] topo_ver=%lu nodes=%u relay_groups=%u seeds=%u",
           static_cast<unsigned long>(topology_edit.topology_version),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups),
           static_cast<unsigned int>(topology_edit.seeds.size()));
    if (!topology_edit.seeds.empty()) {
      std::string csv;
      for (size_t i = 0; i < topology_edit.seeds.size(); ++i) {
        if (i > 0U) {
          csv += ",";
        }
        csv += std::to_string(topology_edit.seeds[i]);
      }
      writef("[MASTER][TOPO][EDIT] seeds_csv=%s", csv.c_str());
    }
    for (size_t i = 0; i < topology_edit.chain.size(); ++i) {
      const auto& node = topology_edit.chain[i];
      writef("  %u) t=%s mac=%s vi=%d",
             static_cast<unsigned int>(i),
             chainTypeToken(node.type),
             macToPrintable(node.mac).c_str(),
             static_cast<int>(node.vi));
    }
    return true;
  }

  if (cmd_lower == "topology.edit.validate") {
    std::string json_text;
    std::vector<uint32_t> resolved_seeds{};
    uint32_t validated_topology_version = topology_edit.topology_version;
    size_t relay_groups = 0U;
    size_t target_count = 0U;
    std::string error;
    if (!buildValidatedEditorJson(json_text,
                                  resolved_seeds,
                                  validated_topology_version,
                                  relay_groups,
                                  target_count,
                                  error)) {
      writef("[MASTER][TOPO][EDIT] invalid: %s", error.c_str());
      return true;
    }
    const bool seeds_changed = (resolved_seeds != topology_edit.seeds);
    topology_edit.seeds = resolved_seeds;
    topology_edit.topology_version = validated_topology_version;
    writef("[MASTER][TOPO][EDIT] valid topo_ver=%lu nodes=%u relay_groups=%u targets=%u seeds=%u",
           static_cast<unsigned long>(validated_topology_version),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups),
           static_cast<unsigned int>(target_count),
           static_cast<unsigned int>(topology_edit.seeds.size()));
    if (seeds_changed) {
      io_.writeln("[MASTER][TOPO][EDIT] note: seeds auto-materialized to match relay block count");
    }
    return true;
  }

  if (cmd_lower == "topology.edit.save" || startsWith(cmd_lower, "topology.edit.save ")) {
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO][EDIT] local storage backend unavailable");
      return true;
    }
    std::string path_token = trim(cmd_line.substr(std::strlen("topology.edit.save")));
    if (path_token.empty()) {
      path_token = "/o/s/topology_chain.json";
    }
    const std::string path = normalizeTopoPath(path_token);

    std::string json_text;
    std::vector<uint32_t> resolved_seeds{};
    uint32_t validated_topology_version = topology_edit.topology_version;
    size_t relay_groups = 0U;
    size_t target_count = 0U;
    std::string error;
    if (!buildValidatedEditorJson(json_text,
                                  resolved_seeds,
                                  validated_topology_version,
                                  relay_groups,
                                  target_count,
                                  error)) {
      writef("[MASTER][TOPO][EDIT] save blocked: invalid chain (%s)", error.c_str());
      return true;
    }
    topology_edit.seeds = resolved_seeds;
    topology_edit.topology_version = validated_topology_version;

    std::string write_msg;
    if (!writeTextFileLocal(*ota_push_storage_, path, json_text, write_msg)) {
      writef("[MASTER][TOPO][EDIT] save failed path=%s reason=%s",
             path.c_str(),
             write_msg.c_str());
      return true;
    }
    writef("[MASTER][TOPO][EDIT] saved path=%s topo_ver=%lu nodes=%u relay_groups=%u targets=%u",
           path.c_str(),
           static_cast<unsigned long>(topology_edit.topology_version),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups),
           static_cast<unsigned int>(target_count));
    return true;
  }

  if (cmd_lower == "topology.edit.load" || startsWith(cmd_lower, "topology.edit.load ")) {
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO][EDIT] local storage backend unavailable");
      return true;
    }
    std::string path_token = trim(cmd_line.substr(std::strlen("topology.edit.load")));
    if (path_token.empty()) {
      path_token = "/o/s/topology_chain.json";
    }
    const std::string path = normalizeTopoPath(path_token);

    std::string json_text;
    std::string read_msg;
    if (!readTextFileLocal(*ota_push_storage_, path, json_text, read_msg)) {
      writef("[MASTER][TOPO][EDIT] load failed path=%s reason=%s",
             path.c_str(),
             read_msg.c_str());
      return true;
    }

    std::string load_error;
    if (!loadEditorFromJson(json_text, load_error)) {
      writef("[MASTER][TOPO][EDIT] load parse failed path=%s reason=%s",
             path.c_str(),
             load_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][EDIT] loaded path=%s topo_ver=%lu nodes=%u seeds=%u",
           path.c_str(),
           static_cast<unsigned long>(topology_edit.topology_version),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(topology_edit.seeds.size()));
    return true;
  }

  if (cmd_lower == "topology.status") {
    ManagementController mgmt = makeController();
    uint32_t req_id = 0U;
    const bool ok = submitTopo(mgmt, ManagementCommandId::TopologyStatusGet, {}, &req_id);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][TOPO] status requested req=%lu", static_cast<unsigned long>(req_id));
    } else {
      io_.writeln("[MASTER][TOPO] status request failed");
    }
    return true;
  }

  if (cmd_lower == "topology.commit") {
    ManagementController mgmt = makeController();
    uint32_t req_id = 0U;
    const bool ok = submitTopo(mgmt, ManagementCommandId::TopologyCommit, {}, &req_id);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][TOPO] commit requested req=%lu", static_cast<unsigned long>(req_id));
    } else {
      io_.writeln("[MASTER][TOPO] commit request failed");
    }
    return true;
  }

  if (startsWith(cmd_lower, "topology.slots")) {
    bool committed = true;
    const std::string arg = trim(cmd_lower.substr(std::strlen("topology.slots")));
    if (!arg.empty()) {
      if (arg == "staged") {
        committed = false;
      } else if (arg == "committed") {
        committed = true;
      } else {
        io_.writeln("[MASTER][TOPO] usage: topology.slots [committed|staged]");
        return true;
      }
    }
    ManagementController mgmt = makeController();
    uint32_t req_id = 0U;
    const bool ok =
        submitTopo(mgmt, ManagementCommandId::TopologySlotsGet, management_utils::buildTopologySlotsGetPayload(committed), &req_id);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][TOPO] slots requested req=%lu state=%s",
             static_cast<unsigned long>(req_id),
             committed ? "committed" : "staged");
    } else {
      io_.writeln("[MASTER][TOPO] slots request failed");
    }
    return true;
  }

  if (startsWith(cmd_lower, "topology.trigger ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() < 3U || tok.size() > 6U) {
      io_.writeln("[MASTER][TOPO] usage: topology.trigger <idx> <forward|reverse|1|2> [delay_ms] [hold_ms] [src_vid]");
      return true;
    }
    char* idx_end = nullptr;
    const long idx_long = std::strtol(tok[1].c_str(), &idx_end, 10);
    if (idx_end == nullptr || *idx_end != '\0' ||
        idx_long < -static_cast<long>(EspNowManager::kTopologyMaxSlots) ||
        idx_long > static_cast<long>(EspNowManager::kTopologyMaxSlots) || idx_long == 0L) {
      writef("[MASTER][TOPO] invalid target index (allowed -%u..-1 or 1..%u)",
             static_cast<unsigned int>(EspNowManager::kTopologyMaxSlots),
             static_cast<unsigned int>(EspNowManager::kTopologyMaxSlots));
      return true;
    }

    const std::string dir = management_utils::lowerAscii(tok[2]);
    uint8_t direction = 0U;
    if (dir == "1" || dir == "f" || dir == "fwd" || dir == "forward") {
      direction = 1U;
    } else if (dir == "2" || dir == "r" || dir == "rev" || dir == "reverse") {
      direction = 2U;
    } else {
      io_.writeln("[MASTER][TOPO] invalid direction (use forward|reverse|1|2)");
      return true;
    }

    uint16_t delay_ms = 0U;
    uint16_t hold_ms = 0U;
    uint8_t src_vid = 0xFFU;
    if (tok.size() >= 4U && !parseU16Token(tok[3], delay_ms)) {
      io_.writeln("[MASTER][TOPO] invalid delay_ms");
      return true;
    }
    if (tok.size() >= 5U && !parseU16Token(tok[4], hold_ms)) {
      io_.writeln("[MASTER][TOPO] invalid hold_ms");
      return true;
    }
    if (tok.size() >= 6U) {
      uint32_t parsed = 0U;
      if (!parseU32Token(tok[5], parsed) || parsed > 0xFFU) {
        io_.writeln("[MASTER][TOPO] invalid src_vid");
        return true;
      }
      src_vid = static_cast<uint8_t>(parsed & 0xFFU);
    }

    ManagementTopologyTriggerSendPayload trigger{};
    trigger.target_index = static_cast<int8_t>(idx_long);
    trigger.direction = direction;
    trigger.delay_ms = delay_ms;
    trigger.hold_ms = hold_ms;
    trigger.source_virtual_index = src_vid;

    ManagementController mgmt = makeController();
    uint32_t req_id = 0U;
    const bool ok = submitTopo(mgmt,
                               ManagementCommandId::TopologyTriggerSend,
                               management_utils::buildTopologyTriggerSendPayload(trigger),
                               &req_id);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][TOPO] trigger requested req=%lu idx=%d dir=%u delay=%u hold=%u src_vid=%u",
             static_cast<unsigned long>(req_id),
             static_cast<int>(trigger.target_index),
             static_cast<unsigned int>(trigger.direction),
             static_cast<unsigned int>(trigger.delay_ms),
             static_cast<unsigned int>(trigger.hold_ms),
             static_cast<unsigned int>(trigger.source_virtual_index));
    } else {
      io_.writeln("[MASTER][TOPO] trigger request failed");
    }
    return true;
  }

  auto sendStage = [&](const ManagementTopologySnapshotPayload& snapshot,
                       uint32_t* out_stage_req = nullptr) -> bool {
    ManagementController mgmt = makeController();
    uint32_t req_id = 0U;
    const bool ok =
        submitTopo(mgmt, ManagementCommandId::TopologyStageSet, management_utils::buildTopologyStagePayload(snapshot), &req_id);
    correlation_id_ = mgmt.nextReqId();
    if (out_stage_req != nullptr) {
      *out_stage_req = req_id;
    }
    if (ok) {
      writef("[MASTER][TOPO] stage requested req=%lu version=%lu slots=%u groups=%u",
             static_cast<unsigned long>(req_id),
             static_cast<unsigned long>(snapshot.topology_version),
             static_cast<unsigned int>(snapshot.slots.size()),
             static_cast<unsigned int>(snapshot.groups.size()));
    } else {
      io_.writeln("[MASTER][TOPO] stage request failed");
    }
    return ok;
  };

  if (startsWith(cmd_lower, "topology.stage.hex ")) {
    const std::string hex_blob = trim(cmd_line.substr(std::strlen("topology.stage.hex ")));
    ManagementTopologySnapshotPayload snapshot{};
    if (!loadSnapshotFromHex(hex_blob, snapshot)) {
      return true;
    }
    (void)sendStage(snapshot, nullptr);
    return true;
  }

  if (startsWith(cmd_lower, "topology.stage.file ")) {
    const std::string path = trim(cmd_line.substr(std::strlen("topology.stage.file ")));
    ManagementTopologySnapshotPayload snapshot{};
    if (!loadSnapshotFromFile(path, snapshot)) {
      return true;
    }
    (void)sendStage(snapshot, nullptr);
    return true;
  }

  auto sendApply = [&](const ManagementTopologySnapshotPayload& snapshot) {
    ManagementController mgmt = makeController();
    uint32_t stage_req = 0U;
    uint32_t commit_req = 0U;
    const bool staged =
        submitTopo(mgmt, ManagementCommandId::TopologyStageSet, management_utils::buildTopologyStagePayload(snapshot), &stage_req);
    const bool committed = staged && submitTopo(mgmt, ManagementCommandId::TopologyCommit, {}, &commit_req);
    correlation_id_ = mgmt.nextReqId();
    if (staged && committed) {
      writef("[MASTER][TOPO] apply requested stage_req=%lu commit_req=%lu version=%lu",
             static_cast<unsigned long>(stage_req),
             static_cast<unsigned long>(commit_req),
             static_cast<unsigned long>(snapshot.topology_version));
    } else if (!staged) {
      io_.writeln("[MASTER][TOPO] apply failed: stage submit failed");
    } else {
      io_.writeln("[MASTER][TOPO] apply failed: commit submit failed");
    }
    return staged && committed;
  };

  if (startsWith(cmd_lower, "topology.apply.hex ")) {
    const std::string hex_blob = trim(cmd_line.substr(std::strlen("topology.apply.hex ")));
    ManagementTopologySnapshotPayload snapshot{};
    if (!loadSnapshotFromHex(hex_blob, snapshot)) {
      return true;
    }
    (void)sendApply(snapshot);
    return true;
  }

  if (startsWith(cmd_lower, "topology.apply.file ")) {
    const std::string path = trim(cmd_line.substr(std::strlen("topology.apply.file ")));
    ManagementTopologySnapshotPayload snapshot{};
    if (!loadSnapshotFromFile(path, snapshot)) {
      return true;
    }
    (void)sendApply(snapshot);
    return true;
  }

  if (startsWith(cmd_lower, "topology.plan.file ")) {
    if (force_local) {
      io_.writeln("[MASTER][TOPO] topology.local.plan.file is not supported");
      return true;
    }
    const std::string path_token = trim(cmd_line.substr(std::strlen("topology.plan.file ")));
    const std::string normalized_path = normalizeTopoPath(path_token);
    if (normalized_path.empty()) {
      io_.writeln("[MASTER][TOPO] usage: topology.plan.file <path>");
      return true;
    }
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO] dry plan failed: local storage unavailable");
      return true;
    }

    std::string json_text;
    std::string read_msg;
    if (!readTextFileLocal(*ota_push_storage_, normalized_path, json_text, read_msg)) {
      writef("[MASTER][TOPO] dry plan read failed path=%s reason=%s",
             normalized_path.c_str(),
             read_msg.c_str());
      return true;
    }

    size_t first_non_ws = 0U;
    while (first_non_ws < json_text.size() &&
           std::isspace(static_cast<unsigned char>(json_text[first_non_ws])) != 0) {
      ++first_non_ws;
    }
    if (first_non_ws >= json_text.size() || json_text[first_non_ws] != '{') {
      writef("[MASTER][TOPO] dry plan expects JSON chain file path=%s", normalized_path.c_str());
      return true;
    }

    uint32_t topology_version = static_cast<uint32_t>(std::time(nullptr));
    std::vector<TopologyDeployTarget> targets{};
    TopologyChainPlanDebug debug{};
    std::string parse_error;
    if (!parseTopologyChainJson(json_text, topology_version, targets, topology_version, parse_error, &debug)) {
      writef("[MASTER][TOPO] dry plan parse failed path=%s reason=%s",
             normalized_path.c_str(),
             parse_error.c_str());
      return true;
    }

    auto bytesToHex = [](const uint8_t* data, size_t len) -> std::string {
      static const char* kHex = "0123456789ABCDEF";
      std::string out;
      out.reserve(len * 2U);
      for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        out.push_back(kHex[(b >> 4) & 0x0FU]);
        out.push_back(kHex[b & 0x0FU]);
      }
      return out;
    };
    auto chainTypeToken = [](ChainNodeType type) -> const char* {
      switch (type) {
        case ChainNodeType::Sensor:
          return "S";
        case ChainNodeType::Relay:
          return "R";
        case ChainNodeType::SemuChild:
          return "SM";
        case ChainNodeType::RemuChild:
          return "RM";
      }
      return "?";
    };
    auto chainNodeLabel = [&](const ChainNode& node) -> std::string {
      std::string out = chainTypeToken(node.type);
      if (node.virtual_index >= 0) {
        out += "(" + std::to_string(node.virtual_index) + ")";
      }
      return out;
    };
    auto peerRoleLabel = [](uint8_t role_code) -> const char* {
      if (role_code == static_cast<uint8_t>(kProfileSens & 0xFFU)) return "S";
      if (role_code == static_cast<uint8_t>(kProfileSemu & 0xFFU)) return "SM";
      if (role_code == static_cast<uint8_t>(kProfileRelay & 0xFFU)) return "R";
      if (role_code == static_cast<uint8_t>(kProfileRemu & 0xFFU)) return "RM";
      if (role_code == static_cast<uint8_t>(kProfilePms & 0xFFU)) return "PMS";
      return "?";
    };

    const MacAddress icm_mac = manager_.localMac();
    writef("[MASTER][TOPO][PLAN] file=%s topo_ver=%lu nodes=%u groups=%u targets=%u",
           normalized_path.c_str(),
           static_cast<unsigned long>(topology_version),
           static_cast<unsigned int>(debug.nodes.size()),
           static_cast<unsigned int>(debug.groups.size()),
           static_cast<unsigned int>(targets.size()));
    writef("[MASTER][TOPO][PLAN] icm_mac=%s", macToPrintable(icm_mac).c_str());
    io_.writeln("[MASTER][TOPO][PLAN] mode=dry-run (no RF send)");

    if (!debug.groups.empty() && !debug.nodes.empty()) {
      std::string chain_line = chainNodeLabel(debug.nodes[debug.groups.front().left_separator]);
      for (const auto& group : debug.groups) {
        chain_line += " -> [";
        bool first = true;
        for (size_t relay_idx : group.relay_nodes) {
          if (!first) {
            chain_line += ",";
          }
          chain_line += chainNodeLabel(debug.nodes[relay_idx]);
          first = false;
        }
        chain_line += "] -> ";
        chain_line += chainNodeLabel(debug.nodes[group.right_separator]);
      }
      writef("[MASTER][TOPO][CHAIN] %s", chain_line.c_str());
    }

    io_.writeln("[MASTER][TOPO][SEEDS]");
    for (size_t i = 0; i < debug.groups.size(); ++i) {
      const auto& group = debug.groups[i];
      const uint32_t seed_u32 = (i < debug.seed_u32.size()) ? debug.seed_u32[i] : 0U;
      const std::string seed_hex = bytesToHex(group.seed.data(), group.seed.size());
      writef("  gid=%u seed_u32=%lu seed32=%s",
             static_cast<unsigned int>(group.group_id),
             static_cast<unsigned long>(seed_u32),
             seed_hex.c_str());
    }

    std::sort(targets.begin(), targets.end(), [](const TopologyDeployTarget& a, const TopologyDeployTarget& b) {
      return a.target < b.target;
    });

    for (const auto& target : targets) {
      std::vector<ManagementTopologySlotPayload> slots{};
      slots.reserve(target.snapshot.slots.size());
      uint8_t neg_count = 0U;
      uint8_t pos_count = 0U;
      std::vector<uint8_t> groups_used{};
      groups_used.reserve(target.snapshot.groups.size());
      for (const auto& slot : target.snapshot.slots) {
        if (!slot.enabled) {
          continue;
        }
        slots.push_back(slot);
        if (slot.relative_index < 0) {
          ++neg_count;
        } else if (slot.relative_index > 0) {
          ++pos_count;
        }
        if (std::find(groups_used.begin(), groups_used.end(), slot.group_id) == groups_used.end()) {
          groups_used.push_back(slot.group_id);
        }
      }
      std::sort(groups_used.begin(), groups_used.end());
      std::sort(slots.begin(),
                slots.end(),
                [](const ManagementTopologySlotPayload& a, const ManagementTopologySlotPayload& b) {
                  return a.relative_index < b.relative_index;
                });

      std::map<MacAddress, uint8_t> peer_key_gid{};
      for (const auto& slot : slots) {
        const auto it = peer_key_gid.find(slot.peer);
        if (it == peer_key_gid.end() || slot.group_id < it->second) {
          peer_key_gid[slot.peer] = slot.group_id;
        }
      }

      const bool over_cap = slots.size() > static_cast<size_t>(kChainLateralLinksPerPhysical);
      writef("[MASTER][TOPO][DEVICE] mac=%s slots=%u neg=%u pos=%u status=%s",
             macToPrintable(target.target).c_str(),
             static_cast<unsigned int>(slots.size()),
             static_cast<unsigned int>(neg_count),
             static_cast<unsigned int>(pos_count),
             over_cap ? "OVER_CAP" : "OK");

      std::vector<uint8_t> local_vis{};
      for (const auto& slot : slots) {
        if (slot.local_virtual_index == 0xFFU) {
          continue;
        }
        if (std::find(local_vis.begin(), local_vis.end(), slot.local_virtual_index) == local_vis.end()) {
          local_vis.push_back(slot.local_virtual_index);
        }
      }
      std::sort(local_vis.begin(), local_vis.end());
      const bool has_multiple_sensor_views = local_vis.size() > 1U;

      auto formatIndexMap = [](uint8_t local_neg, uint8_t local_pos) -> std::string {
        std::string idx_map = "[";
        bool first_idx = true;
        for (int idx = static_cast<int>(local_neg); idx >= 1; --idx) {
          if (!first_idx) {
            idx_map += ", ";
          }
          idx_map += std::to_string(-idx);
          first_idx = false;
        }
        for (int idx = 1; idx <= static_cast<int>(local_pos); ++idx) {
          if (!first_idx) {
            idx_map += ", ";
          }
          idx_map += std::to_string(idx);
          first_idx = false;
        }
        idx_map += "]";
        return idx_map;
      };
      auto formatGroupList = [](const std::vector<uint8_t>& gids) -> std::string {
        std::string out = "[";
        for (size_t i = 0; i < gids.size(); ++i) {
          if (i > 0U) {
            out += ", ";
          }
          out += std::to_string(gids[i]);
        }
        out += "]";
        return out;
      };
      auto buildLmkHex = [&](const MacAddress& local_mac,
                             const MacAddress& peer_mac,
                             uint8_t key_gid) -> std::string {
        std::array<uint8_t, 32> group_seed{};
        bool has_group_seed = false;
        for (const auto& g : target.snapshot.groups) {
          if (g.enabled && g.group_id == key_gid) {
            group_seed = g.seed;
            has_group_seed = true;
            break;
          }
        }
        if (!has_group_seed) {
          return "MISSING_SEED";
        }
        LmkKey lmk{};
        if (!deriveLmkFromTopologySeed(group_seed, icm_mac, local_mac, peer_mac, key_gid, lmk)) {
          return "DERIVE_FAIL";
        }
        return bytesToHex(lmk.data(), lmk.size());
      };

      if (!has_multiple_sensor_views) {
        writef("  index_map=%s", formatIndexMap(neg_count, pos_count).c_str());
        writef("  groups=%s", formatGroupList(groups_used).c_str());
        io_.writeln("  secure_add:");
        for (const auto& slot : slots) {
          const auto key_it = peer_key_gid.find(slot.peer);
          const uint8_t key_gid = (key_it == peer_key_gid.end()) ? slot.group_id : key_it->second;
          const std::string lmk_hex = buildLmkHex(target.target, slot.peer, key_gid);
          writef("    rid=%+d peer=%s peer_role=%s slot_gid=%u key_gid=%u local_vi=%u peer_vi=%u lmk=%s",
                 static_cast<int>(slot.relative_index),
                 macToPrintable(slot.peer).c_str(),
                 peerRoleLabel(slot.peer_role),
                 static_cast<unsigned int>(slot.group_id),
                 static_cast<unsigned int>(key_gid),
                 static_cast<unsigned int>(slot.local_virtual_index),
                 static_cast<unsigned int>(slot.peer_virtual_index),
                 lmk_hex.c_str());
        }
      } else {
        for (uint8_t local_vi : local_vis) {
          std::vector<ManagementTopologySlotPayload> view_slots{};
          view_slots.reserve(slots.size());
          std::vector<uint8_t> view_groups{};
          uint8_t view_neg = 0U;
          uint8_t view_pos = 0U;
          for (const auto& slot : slots) {
            if (slot.local_virtual_index != local_vi) {
              continue;
            }
            view_slots.push_back(slot);
            if (slot.relative_index < 0) {
              ++view_neg;
            } else if (slot.relative_index > 0) {
              ++view_pos;
            }
            if (std::find(view_groups.begin(), view_groups.end(), slot.group_id) == view_groups.end()) {
              view_groups.push_back(slot.group_id);
            }
          }
          std::sort(view_groups.begin(), view_groups.end());
          std::sort(view_slots.begin(),
                    view_slots.end(),
                    [](const ManagementTopologySlotPayload& a, const ManagementTopologySlotPayload& b) {
                      return a.relative_index < b.relative_index;
                    });

          std::map<MacAddress, uint8_t> view_peer_key_gid{};
          for (const auto& slot : view_slots) {
            const auto it = view_peer_key_gid.find(slot.peer);
            if (it == view_peer_key_gid.end() || slot.group_id < it->second) {
              view_peer_key_gid[slot.peer] = slot.group_id;
            }
          }

          writef("  [TOPO][SENSOR_VIEW] sensor=SM(%u) slots=%u neg=%u pos=%u groups=%s",
                 static_cast<unsigned int>(local_vi),
                 static_cast<unsigned int>(view_slots.size()),
                 static_cast<unsigned int>(view_neg),
                 static_cast<unsigned int>(view_pos),
                 formatGroupList(view_groups).c_str());
          writef("    index_map=%s", formatIndexMap(view_neg, view_pos).c_str());

          std::string before_text = "[";
          std::string after_text = "[";
          bool first_before = true;
          bool first_after = true;
          for (const auto& slot : view_slots) {
            std::string item;
            item.reserve(48U);
            char rid_buf[8] = {0};
            std::snprintf(rid_buf, sizeof(rid_buf), "%+d", static_cast<int>(slot.relative_index));
            item += rid_buf;
            item += ":";
            item += macToPrintable(slot.peer);
            item += "(";
            item += peerRoleLabel(slot.peer_role);
            if (slot.peer_virtual_index != 0xFFU) {
              item += ",vi=" + std::to_string(static_cast<unsigned int>(slot.peer_virtual_index));
            }
            item += ")";
            if (slot.relative_index < 0) {
              if (!first_before) {
                before_text += ", ";
              }
              before_text += item;
              first_before = false;
            } else {
              if (!first_after) {
                after_text += ", ";
              }
              after_text += item;
              first_after = false;
            }
          }
          before_text += "]";
          after_text += "]";
          writef("    before=%s", before_text.c_str());
          writef("    after=%s", after_text.c_str());

          io_.writeln("    secure_add:");
          for (const auto& slot : view_slots) {
            const auto key_it = view_peer_key_gid.find(slot.peer);
            const uint8_t key_gid = (key_it == view_peer_key_gid.end()) ? slot.group_id : key_it->second;
            const std::string lmk_hex = buildLmkHex(target.target, slot.peer, key_gid);
            writef("      rid=%+d peer=%s peer_role=%s slot_gid=%u key_gid=%u local_vi=%u peer_vi=%u lmk=%s",
                   static_cast<int>(slot.relative_index),
                   macToPrintable(slot.peer).c_str(),
                   peerRoleLabel(slot.peer_role),
                   static_cast<unsigned int>(slot.group_id),
                   static_cast<unsigned int>(key_gid),
                   static_cast<unsigned int>(slot.local_virtual_index),
                   static_cast<unsigned int>(slot.peer_virtual_index),
                   lmk_hex.c_str());
          }
        }
      }

      std::vector<MacAddress> peers = target.peers;
      std::sort(peers.begin(), peers.end());
      std::string peers_text;
      for (size_t i = 0; i < peers.size(); ++i) {
        if (i > 0U) {
          peers_text += ", ";
        }
        peers_text += macToPrintable(peers[i]);
      }
      writef("  peers=%s", peers_text.c_str());
    }

    io_.writeln("[MASTER][TOPO][PLAN] done dry-run only");
    return true;
  }

  if (startsWith(cmd_lower, "topology.deploy.file ")) {
    if (force_local) {
      io_.writeln("[MASTER][TOPO] topology.local.deploy.file is not supported");
      return true;
    }
    const std::string path_token = trim(cmd_line.substr(std::strlen("topology.deploy.file ")));
    const std::string normalized_path = normalizeTopoPath(path_token);
    if (normalized_path.empty()) {
      io_.writeln("[MASTER][TOPO] usage: topology.deploy.file <path>");
      return true;
    }

    bool handled_chain_json = false;
    if (ota_push_storage_ != nullptr) {
      std::string json_text;
      std::string read_msg;
      if (readTextFileLocal(*ota_push_storage_, normalized_path, json_text, read_msg)) {
        size_t first_non_ws = 0U;
        while (first_non_ws < json_text.size() &&
               std::isspace(static_cast<unsigned char>(json_text[first_non_ws])) != 0) {
          ++first_non_ws;
        }
        if (first_non_ws < json_text.size() && json_text[first_non_ws] == '{') {
          handled_chain_json = true;
          uint32_t topology_version = static_cast<uint32_t>(std::time(nullptr));
          std::vector<TopologyDeployTarget> targets{};
          std::string parse_error;
          if (!parseTopologyChainJson(json_text, topology_version, targets, topology_version, parse_error, nullptr)) {
            writef("[MASTER][TOPO] chain json parse failed path=%s reason=%s",
                   normalized_path.c_str(),
                   parse_error.c_str());
            return true;
          }

          std::vector<MacAddress> paired_peers{};
          manager_.getPersistedPeers(paired_peers);
          if (paired_peers.empty()) {
            io_.writeln("[MASTER][TOPO] no paired peers to deploy");
            return true;
          }

          uint32_t queued = 0U;
          uint32_t failed = 0U;
          uint32_t skipped_not_paired = 0U;
          for (const auto& target : targets) {
            const bool is_paired = std::find(paired_peers.begin(),
                                             paired_peers.end(),
                                             target.target) != paired_peers.end();
            if (!is_paired) {
              ++skipped_not_paired;
              writef("[MASTER][TOPO] skip target=%s reason=not_paired",
                     macToPrintable(target.target).c_str());
              continue;
            }

            writef("[MASTER][TOPO] target=%s slots=%u groups=%u peers=%u",
                   macToPrintable(target.target).c_str(),
                   static_cast<unsigned int>(target.snapshot.slots.size()),
                   static_cast<unsigned int>(target.snapshot.groups.size()),
                   static_cast<unsigned int>(target.peers.size()));
            for (const auto& neighbor : target.peers) {
              writef("[MASTER][TOPO]   peer=%s", macToPrintable(neighbor).c_str());
            }

            ManagementController mgmt(*management_transport_);
            mgmt.setNextReqId(correlation_id_);
            uint32_t stage_req = 0U;
            uint32_t commit_req = 0U;
            const bool staged = submitRuntimeTargeted_(mgmt,
                                                       static_cast<uint16_t>(ManagementCommandId::TopologyStageSet),
                                                       management_utils::buildTopologyStagePayload(target.snapshot),
                                                       &stage_req,
                                                       0U,
                                                       false,
                                                       &target.target);
            bool committed = false;
            if (staged) {
              committed = submitRuntimeTargeted_(mgmt,
                                                 static_cast<uint16_t>(ManagementCommandId::TopologyCommit),
                                                 {},
                                                 &commit_req,
                                                 0U,
                                                 false,
                                                 &target.target);
            }
            correlation_id_ = mgmt.nextReqId();
            if (staged && committed) {
              ++queued;
              writef("[MASTER][TOPO] deploy queued peer=%s stage_req=%lu commit_req=%lu",
                     macToPrintable(target.target).c_str(),
                     static_cast<unsigned long>(stage_req),
                     static_cast<unsigned long>(commit_req));
            } else {
              ++failed;
              writef("[MASTER][TOPO] deploy submit failed peer=%s",
                     macToPrintable(target.target).c_str());
            }
          }

          writef("[MASTER][TOPO] deploy summary targets=%u paired=%u queued=%u failed=%u skipped_not_paired=%u topo_ver=%lu",
                 static_cast<unsigned int>(targets.size()),
                 static_cast<unsigned int>(targets.size() - skipped_not_paired),
                 static_cast<unsigned int>(queued),
                 static_cast<unsigned int>(failed),
                 static_cast<unsigned int>(skipped_not_paired),
                 static_cast<unsigned long>(topology_version));
          return true;
        }
      }
    }

    if (!handled_chain_json) {
      io_.writeln("[MASTER][TOPO] deploy file must be topology chain json");
      return true;
    }
    return true;
  }

  io_.writeln("[MASTER][TOPO] unknown topology command (use: help topology)");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}


bool MasterCli::handleDescriptorShortCommands(const std::string& lower) {
  if (lower == "live enable") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LiveMonitorEnable),
                                           {},
                                           nullptr,
                                           0U,
                                           false);
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][LIVE] monitor enable requested" : "[MASTER][LIVE] monitor enable request failed");
    return true;
  }
  if (lower == "live disable") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LiveMonitorDisable),
                                           {},
                                           nullptr,
                                           0U,
                                           false);
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][LIVE] monitor disable requested" : "[MASTER][LIVE] monitor disable request failed");
    return true;
  }
  if (lower == "live status") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    live_monitor_status_pending_ = false;
    live_monitor_status_req_id_ = 0U;
    uint32_t req_id = 0;
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LiveMonitorStatusGet),
                                           {},
                                           &req_id,
                                           0U,
                                           false);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      live_monitor_status_pending_ = true;
      live_monitor_status_req_id_ = req_id;
    }
    io_.writeln(ok ? "[MASTER][LIVE] monitor status requested" : "[MASTER][LIVE] monitor status request failed");
    return true;
  }
  if (lower == "desc") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::DescGet));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][CLI] desc requested");
    } else {
      io_.writeln("[MASTER][CLI] descriptor request failed");
    }
    return true;
  }
  if (lower == "caps") {
    (void)startPagedFetch(PagedFetchKind::Capabilities, 6, "[MASTER][CLI] caps paged fetch started");
    return true;
  }
  if (lower == "telem") {
    (void)startPagedFetch(PagedFetchKind::Telemetry, 6, "[MASTER][CLI] telemetry paged fetch started");
    return true;
  }
  if (startsWith(lower, "telem.now.child ")) {
    const ProfileId profile_id = remote_profile_id_;
    uint8_t max_vid = 15U;
    const char* profile_label = "REMU";
    if (profile_id == kProfileSemu) {
      max_vid = 7U;
      profile_label = "SEMU";
    } else if (profile_id == kProfileRemu) {
      max_vid = 15U;
      profile_label = "REMU";
    } else {
      io_.writeln("[MASTER][CLI] telem.now.child expects SEMU/REMU target (run caps on selected peer first)");
      return true;
    }
    const std::vector<std::string> tokens = splitTokens(lower);
    if (tokens.size() != 3U) {
      writef("[MASTER][CLI] usage: telem.now.child <vid:0..%u>", static_cast<unsigned int>(max_vid));
      return true;
    }
    uint32_t vid = 0U;
    if (!parseU32Token(tokens[2], vid) || vid > max_vid) {
      writef("[MASTER][CLI] invalid child vid (0..%u)", static_cast<unsigned int>(max_vid));
      return true;
    }
    semu_telem_child_filter_active_ = true;
    semu_telem_child_filter_vid_ = static_cast<uint8_t>(vid);
    semu_telem_child_filter_max_vid_ = max_vid;
    const std::string queued_msg =
        std::string("[MASTER][CLI] requested ") + profile_label + " child telemetry vid=" +
        std::to_string(static_cast<unsigned int>(semu_telem_child_filter_vid_)) + " (+global)";
    const bool ok = startPagedFetch(PagedFetchKind::TelemetrySnapshot, 6, queued_msg.c_str());
    if (ok) {
      // queued message emitted by startPagedFetch
    } else {
      semu_telem_child_filter_active_ = false;
      io_.writeln("[MASTER][CLI] child telemetry request failed");
    }
    return true;
  }
  if (lower == "telem.now") {
    semu_telem_child_filter_active_ = false;
    semu_telem_child_filter_max_vid_ = 7U;
    const bool ok = startPagedFetch(PagedFetchKind::TelemetrySnapshot, 6, "[MASTER][CLI] requested live telemetry");
    if (ok) {
      // queued message emitted by startPagedFetch
    } else {
      io_.writeln("[MASTER][CLI] live telemetry request failed");
    }
    return true;
  }
  if (lower == "live") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::LiveGet));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][CLI] requested liveness");
    } else {
      io_.writeln("[MASTER][CLI] liveness request failed");
    }
    return true;
  }
  if (lower == "ping") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::PingGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][PING] request queued" : "[MASTER][PING] request failed");
    return true;
  }
  if (lower == "audio ping") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::AudioPingRequest));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][AUDIO] ping request queued" : "[MASTER][AUDIO] ping request failed");
    return true;
  }
  return false;
}

bool MasterCli::handleEventCommands(const std::string& lower) {
  if (lower == "event.list") {
    printMandatoryEvents();
    return true;
  }

  if (lower == "event.clear") {
    mandatory_events_.clear();
    io_.writeln("[MASTER][EVENT] cache cleared");
    return true;
  }

  return false;
}

bool MasterCli::handleSettingsCommands(const std::string& lower) {
  if (lower == "settings") {
    (void)startPagedFetch(PagedFetchKind::Settings, 4, "[MASTER][CLI] settings paged fetch started");
    return true;
  }

  if (lower == "settings.full") {
    (void)requestFullSettingsByProfile();
    return true;
  }

  if (lower == "settings.raw") {
    (void)startPagedFetch(PagedFetchKind::Settings, 4, "[MASTER][CLI] settings.raw paged fetch started");
    return true;
  }

  return false;
}

bool MasterCli::handleStorageCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "sd.")) {
    return false;
  }

  auto printStorageError = [&](const std::string& fallback, const std::string& msg) {
    DescriptorResponse d{};
    d.type = DescriptorResponseType::Error;
    d.message = msg.empty() ? fallback : msg;
    printDescriptorResponse(d);
  };

  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.empty()) {
    io_.writeln("[MASTER][SD] invalid command");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "parse");
    return true;
  }

  if (lower == "sd.remote.pwd") {
    writef("[MASTER][SD][REMOTE] cwd=%s", remote_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (lower == "sd.remote.info") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::StorageInfoGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][SD][REMOTE] info requested" : "[MASTER][SD][REMOTE] info request failed");
    return true;
  }
  if (startsWith(lower, "sd.remote.ls")) {
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][SD][REMOTE] usage: sd.remote.ls [path]");
      return true;
    }
    const std::string path = (tokens.size() == 2U) ? tokens[1] : remote_storage_cwd_;
    const std::string resolved = resolveFsPath(remote_storage_cwd_, path);
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageList),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] ls requested path=%s", resolved.c_str());
    } else {
      io_.writeln("[MASTER][SD][REMOTE] ls request failed");
    }
    return true;
  }
  if (startsWith(lower, "sd.remote.stat ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][REMOTE] usage: sd.remote.stat <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(remote_storage_cwd_, tokens[1]);
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageStat),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] stat requested path=%s", resolved.c_str());
    } else {
      io_.writeln("[MASTER][SD][REMOTE] stat request failed");
    }
    return true;
  }
  if (startsWith(lower, "sd.remote.cd ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][REMOTE] usage: sd.remote.cd <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(remote_storage_cwd_, tokens[1]);
    remote_storage_cd_pending_ = resolved;
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageStat),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] cd requested path=%s", resolved.c_str());
    } else {
      remote_storage_cd_pending_.clear();
      io_.writeln("[MASTER][SD][REMOTE] cd request failed");
    }
    return true;
  }
  if (lower == "sd.remote.up") {
    const std::string resolved = parentFsPath(remote_storage_cwd_);
    remote_storage_cd_pending_ = resolved;
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageStat),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] up requested path=%s", resolved.c_str());
    } else {
      remote_storage_cd_pending_.clear();
      io_.writeln("[MASTER][SD][REMOTE] up request failed");
    }
    return true;
  }
  if (lower == "sd.remote.format") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::StorageFormat));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][SD][REMOTE] format requested");
    } else {
      io_.writeln("[MASTER][SD][REMOTE] format request failed");
    }
    return true;
  }

  if (lower == "sd.pwd") {
    writef("[MASTER][SD][LOCAL] cwd=%s", local_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (local_storage_ == nullptr) {
    io_.writeln("[MASTER][SD][LOCAL] storage explorer unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  if (lower == "sd.info") {
    StorageInfo info{};
    std::string msg;
    if (!local_storage_->getStorageInfo(info, msg)) {
      printStorageError("local storage info unavailable", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!info.cwd.empty()) {
      local_storage_cwd_ = normalizeFsPath(info.cwd);
    }
    DescriptorResponse d{};
    d.type = DescriptorResponseType::StorageInfo;
    d.storage_info = info;
    d.message = msg;
    printDescriptorResponse(d);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (startsWith(lower, "sd.ls")) {
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][SD][LOCAL] usage: sd.ls [path]");
      return true;
    }
    const std::string path = (tokens.size() == 2U) ? tokens[1] : local_storage_cwd_;
    const std::string resolved = resolveFsPath(local_storage_cwd_, path);
    std::vector<StorageEntry> entries;
    std::string canonical;
    std::string parent;
    std::string msg;
    if (!local_storage_->listStoragePath(resolved, canonical, parent, entries, msg)) {
      printStorageError("local storage list failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    DescriptorResponse d{};
    d.type = DescriptorResponseType::StorageList;
    d.storage_path = canonical.empty() ? resolved : canonical;
    d.storage_parent_path = parent;
    d.storage_entries = std::move(entries);
    d.message = msg;
    printDescriptorResponse(d);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (startsWith(lower, "sd.stat ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][LOCAL] usage: sd.stat <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(local_storage_cwd_, tokens[1]);
    StorageStat st{};
    std::string msg;
    if (!local_storage_->statStoragePath(resolved, st, msg)) {
      printStorageError("local storage stat failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    DescriptorResponse d{};
    d.type = DescriptorResponseType::StorageStat;
    d.storage_path = resolved;
    d.storage_stat = st;
    d.message = msg;
    printDescriptorResponse(d);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (startsWith(lower, "sd.cd ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][LOCAL] usage: sd.cd <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(local_storage_cwd_, tokens[1]);
    StorageStat st{};
    std::string msg;
    if (!local_storage_->statStoragePath(resolved, st, msg)) {
      printStorageError("local storage cd failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!st.exists || !st.is_dir) {
      io_.writeln("[MASTER][SD][LOCAL] cd failed: target is not directory");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
      return true;
    }
    local_storage_cwd_ = normalizeFsPath(st.path.empty() ? resolved : st.path);
    writef("[MASTER][SD][LOCAL] cwd=%s", local_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (lower == "sd.up") {
    const std::string resolved = parentFsPath(local_storage_cwd_);
    StorageStat st{};
    std::string msg;
    if (!local_storage_->statStoragePath(resolved, st, msg)) {
      printStorageError("local storage up failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!st.exists || !st.is_dir) {
      io_.writeln("[MASTER][SD][LOCAL] up failed: parent is not directory");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
      return true;
    }
    local_storage_cwd_ = normalizeFsPath(st.path.empty() ? resolved : st.path);
    writef("[MASTER][SD][LOCAL] cwd=%s", local_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (lower == "sd.format") {
    io_.writeln("[MASTER][SD][LOCAL] format started (erasing and rebuilding layout)...");
    bool restore_logger = false;
    bool logger_enabled_before = false;
    if (logger_ != nullptr) {
      logger_enabled_before = logger_->enabled();
      if (logger_enabled_before) {
        logger_->setEnabled(false);
        restore_logger = true;
      }
    }

    std::string msg;
    const bool ok = local_storage_->formatStorage(msg);

    if (restore_logger && logger_ != nullptr) {
      logger_->setEnabled(logger_enabled_before);
    }

    if (!ok) {
      printStorageError("local storage format failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    io_.writeln(msg.empty() ? "[MASTER][SD][LOCAL] format done" : msg);
    local_storage_cwd_ = "/";
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  io_.writeln("[MASTER][SD] unknown storage command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

bool MasterCli::handleOtaCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "ota.")) {
    return false;
  }

  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.empty()) {
    io_.writeln("[MASTER][OTA] invalid command");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "parse");
    return true;
  }

  const std::string first_token = lowerCopy(tokens[0]);
  std::string cmd = first_token;
  std::string cmd_line = lower;

  const bool is_archive_cmd =
      startsWith(cmd, "ota.archive.") || startsWith(cmd, "ota.arc.");
  const bool is_prepare_cmd = startsWith(cmd, "ota.prepare");

  if (cmd == "ota.archive" || cmd == "ota.arc") {
    io_.writeln("[MASTER][OTA] archive commands:");
    io_.writeln("  ota.arc.save [master|slave]          (save running firmware)");
    io_.writeln("  ota.arc.save.staged [master|slave]   (save /o/s/fw.bin + fw.json)");
    io_.writeln("  ota.arc.list [master|slave]");
    io_.writeln("  ota.arc.verify <id6hex> [master|slave]");
    io_.writeln("  ota.arc.restore <id6hex> [master|slave]");
    io_.writeln("  ota.arc.delete <id6hex> [master|slave]");
    io_.writeln("  ota.arc.clear [master|slave]");
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (is_archive_cmd) {
    const std::string prefix = startsWith(cmd, "ota.archive.") ? "ota.archive." : "ota.arc.";
    std::string action = cmd.substr(prefix.size());
    if (action == "save_staged") {
      action = "save.staged";
    }

    std::vector<std::string> args;
    args.reserve(tokens.size());
    for (size_t i = 1U; i < tokens.size(); ++i) {
      args.push_back(tokens[i]);
    }

    char role = 'm';
    std::string id;
    const auto parseRoleArg = [&](size_t idx, bool& ok) {
      ok = true;
      if (idx >= args.size()) {
        return;
      }
      if (!otaArchiveNormalizeRole(args[idx], role)) {
        ok = false;
      }
    };

    bool parse_ok = true;
    if (action == "save" || action == "save.staged" || action == "list" || action == "clear") {
      if (args.size() > 1U) {
        parse_ok = false;
      } else if (!args.empty()) {
        parseRoleArg(0U, parse_ok);
      }
    } else if (action == "verify" || action == "restore" || action == "delete") {
      if (args.empty() || args.size() > 2U) {
        parse_ok = false;
      } else {
        id = otaArchiveNormalizeId(args[0]);
        if (id.empty()) {
          parse_ok = false;
        }
        if (args.size() == 2U) {
          parseRoleArg(1U, parse_ok);
        }
      }
    } else {
      parse_ok = false;
    }

    if (!parse_ok) {
      io_.writeln("[MASTER][OTA] usage:");
      io_.writeln("  ota.arc.save [master|slave]");
      io_.writeln("  ota.arc.save.staged [master|slave]");
      io_.writeln("  ota.arc.list [master|slave]");
      io_.writeln("  ota.arc.verify <id6hex> [master|slave]");
      io_.writeln("  ota.arc.restore <id6hex> [master|slave]");
      io_.writeln("  ota.arc.delete <id6hex> [master|slave]");
      io_.writeln("  ota.arc.clear [master|slave]");
      return true;
    }

    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    bool ok = false;
    if (action == "list") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveList),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    } else if (action == "save") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveRunning),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    } else if (action == "save.staged") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveStaged),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    } else if (action == "restore") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveRestore),
                                  management_utils::buildOtaArchivePayload(role, id, false));
    } else if (action == "verify") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveVerify),
                                  management_utils::buildOtaArchivePayload(role, id, false));
    } else if (action == "delete") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveDelete),
                                  management_utils::buildOtaArchivePayload(role, id, false));
    } else if (action == "clear") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveClear),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    }
    correlation_id_ = mgmt.nextReqId();
    if (!ok) {
      io_.writeln("[MASTER][OTA] archive request failed");
      return true;
    }
    writef("[MASTER][OTA] archive requested action=%s role=%c%s%s",
           action.c_str(),
           role,
           id.empty() ? "" : " id=",
           id.empty() ? "" : id.c_str());
    return true;
  }

  if (is_prepare_cmd) {
    if (tokens.size() != 1U) {
      io_.writeln("[MASTER][OTA] usage: ota.prepare");
      return true;
    }
    if (otaPrepareRemote()) {
      io_.writeln("[MASTER][OTA] remote prepare requested (OTA.CLEAR in)");
    } else {
      io_.writeln("[MASTER][OTA] remote prepare request failed");
    }
    return true;
  }

  if (startsWith(cmd, "ota.stage")) {
    io_.writeln("[MASTER][OTA] ota.stage removed");
    io_.writeln("[MASTER][OTA] place firmware directly in /o/s and use ota.push <name>");
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (cmd == "ota.local.clear.images") {
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][OTA] local clear unavailable (no local OTA storage backend bound)");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    std::string msg;
    if (!ota_push_storage_->begin(msg)) {
      writef("[MASTER][OTA] local clear failed: storage not ready (%s)", msg.c_str());
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!ota_push_storage_->removePath(ota_paths::kImage, msg)) {
      writef("[MASTER][OTA] local clear failed: %s", msg.empty() ? "remove failed" : msg.c_str());
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!ota_push_storage_->ensureDir(ota_paths::kImage, msg)) {
      writef("[MASTER][OTA] local clear failed: %s", msg.empty() ? "mkdir failed" : msg.c_str());
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    writef("[MASTER][OTA] local images cleared: %s", ota_paths::kImage);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (cmd == "ota.clear.images") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaClearScope),
                                           management_utils::buildStringPayloadU16("img"));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][OTA] clear requested scope=img");
    } else {
      io_.writeln("[MASTER][OTA] clear request failed");
    }
    return true;
  }

  if (cmd == "ota.push.abort") {
    const bool had_active = ota_push_active_;
    ota_update_pipeline_active_ = false;
    ota_update_prepare_pending_ = false;
    ota_update_prepare_corr_id_ = 0U;
    ota_update_wait_boot_notice_ = false;
    ota_update_image_name_.clear();
    ota_update_req_id_ = 0U;
    ota_update_has_target_peer_ = false;
    ota_update_target_peer_ = {};
    ota_update_staged_path_.clear();
    ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
    if (had_active) {
      stopOtaPush("aborted by operator", false);
    } else {
      io_.writeln("[MASTER][OTA] ota.push is not active (sending remote abort anyway)");
    }
    if (hasRuntimePeer()) {
      if (management_transport_ == nullptr) {
        io_.writeln("[MASTER][CLI] management path unavailable");
        captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
        return true;
      }
      ManagementController mgmt(*management_transport_);
      mgmt.setNextReqId(correlation_id_);
      const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaPushAbort));
      correlation_id_ = mgmt.nextReqId();
      if (ok) {
        io_.writeln("[MASTER][OTA] management abort requested");
      } else {
        io_.writeln("[MASTER][OTA] management abort request failed");
      }
    } else if (!had_active) {
      io_.writeln("[MASTER][OTA] target not selected");
    }
    if (!dispatch_snapshot_.seen) {
      captureDispatchSnapshot_(had_active,
                               0U,
                               0U,
                               had_active ? ManagementStatus::Ok : ManagementStatus::BadPayload,
                               had_active ? "" : "target");
    }
    return true;
  }
  if (startsWith(cmd_line, "ota.update.from.arc ")) {
    if (tokens.size() < 3U || tokens.size() > 4U) {
      io_.writeln("[MASTER][OTA] usage: ota.update.from.arc <id6hex> [chunk_bytes<=220] [master|slave]");
      return true;
    }

    const std::string id = otaArchiveNormalizeId(tokens[1]);
    if (id.empty()) {
      io_.writeln("[MASTER][OTA] invalid archive id");
      return true;
    }

    constexpr uint16_t kOtaUpdateDefaultChunk = 180U;
    uint16_t chunk = kOtaUpdateDefaultChunk;
    char role = 's';
    bool chunk_set = false;
    bool role_set = false;
    for (size_t i = 2U; i < tokens.size(); ++i) {
      const std::string tok = trim(tokens[i]);
      if (tok.empty()) {
        continue;
      }

      char parsed_role = 0;
      if (otaArchiveNormalizeRole(tok, parsed_role)) {
        if (role_set) {
          io_.writeln("[MASTER][OTA] duplicate role argument");
          return true;
        }
        role = parsed_role;
        role_set = true;
        continue;
      }

      char* endp = nullptr;
      const unsigned long parsed = std::strtoul(tok.c_str(), &endp, 10);
      if (endp != nullptr && *endp == '\0') {
        if (chunk_set) {
          io_.writeln("[MASTER][OTA] duplicate chunk argument");
          return true;
        }
        if (parsed < 32UL || parsed > 220UL) {
          io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
          return true;
        }
        chunk = static_cast<uint16_t>(parsed);
        chunk_set = true;
        continue;
      }

      io_.writeln("[MASTER][OTA] usage: ota.update.from.arc <id6hex> [chunk_bytes<=220] [master|slave]");
      return true;
    }

    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][OTA] archive update unavailable (no local OTA storage backend bound)");
      return true;
    }
    std::string msg;
    if (!ota_push_storage_->begin(msg)) {
      writef("[MASTER][OTA] archive update failed: storage not ready (%s)", msg.c_str());
      return true;
    }

    std::vector<OtaArchiveEntryLocal> entries;
    if (!otaArchiveLoadManifest(*ota_push_storage_, role, entries, msg)) {
      writef("[MASTER][OTA] archive update failed: manifest load failed (%s)", msg.c_str());
      return true;
    }
    auto it = std::find_if(entries.begin(), entries.end(), [&](const OtaArchiveEntryLocal& e) {
      return e.id == id;
    });
    if (it == entries.end()) {
      writef("[MASTER][OTA] archive update failed: id not found (%s)", id.c_str());
      return true;
    }
    if (!it->target_role.empty()) {
      const char entry_role = (it->target_role == "slave") ? 's' : 'm';
      if (entry_role != role) {
        writef("[MASTER][OTA] archive update failed: role mismatch id=%s target=%s arg_role=%c",
               id.c_str(),
               it->target_role.c_str(),
               role);
        return true;
      }
    }

    const std::string bucket = otaArchiveBucketPath(role);
    const std::string stage_bin = std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedBinName;
    const std::string stage_meta = std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedMetaName;

    if (!ota_push_storage_->copySdToSpiffs(bucket + "/" + it->bin_name, stage_bin, msg)) {
      writef("[MASTER][OTA] archive update failed: restore bin failed (%s)", msg.c_str());
      return true;
    }
    if (!ota_push_storage_->copySdToSpiffs(bucket + "/" + it->meta_name, stage_meta, msg)) {
      writef("[MASTER][OTA] archive update failed: restore metadata failed (%s)", msg.c_str());
      return true;
    }
    writef("[MASTER][OTA] archive restore complete id=%s role=%c -> %s",
           id.c_str(),
           role,
           stage_bin.c_str());

    if (role == 'm') {
      std::string out_message;
      if (!otaUpdateMaster(stage_bin, &out_message)) {
        writef("[MASTER][OTA] archive update failed: %s",
               out_message.empty() ? "master update start failed" : out_message.c_str());
        return true;
      }
      writef("[MASTER][OTA] master update started from archive id=%s path=%s",
             id.c_str(),
             stage_bin.c_str());
      return true;
    }

    if (!hasRuntimePeer()) {
      io_.writeln("[MASTER][OTA] target not selected");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }

    std::string out_message;
    if (!otaUpdateRemote(stage_bin, chunk, &out_message)) {
      writef("[MASTER][OTA] archive update pipeline start failed: %s",
             out_message.empty() ? "failed" : out_message.c_str());
      return true;
    }

    writef("[MASTER][OTA] update pipeline started from archive id=%s path=%s chunk=%u",
           id.c_str(),
           stage_bin.c_str(),
           static_cast<unsigned int>(chunk));
    io_.writeln("[MASTER][OTA] management-owned steps: prepare -> push -> apply -> wait boot-complete");
    return true;
  }
  if (startsWith(cmd_line, "ota.update.master")) {
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.update.master [local_path]");
      return true;
    }
    std::string path_arg;
    if (tokens.size() == 2U) {
      path_arg = trim(tokens[1]);
      if (path_arg.empty()) {
        io_.writeln("[MASTER][OTA] usage: ota.update.master [local_path]");
        return true;
      }
    }
    std::string out_message;
    if (!otaUpdateMaster(path_arg, &out_message)) {
      writef("[MASTER][OTA] master update failed: %s",
             out_message.empty() ? "request failed" : out_message.c_str());
      return true;
    }
    writef("[MASTER][OTA] master update requested%s%s",
           out_message.empty() ? "" : ": ",
           out_message.empty() ? "" : out_message.c_str());
    return true;
  }
  if (startsWith(cmd_line, "ota.update ")) {
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][OTA] usage: ota.update <local_path> [chunk_bytes<=220]");
      return true;
    }
    constexpr uint16_t kOtaUpdateDefaultChunk = 180U;
    uint16_t chunk = kOtaUpdateDefaultChunk;
    if (tokens.size() == 3U) {
      const unsigned long parsed = std::strtoul(tokens[2].c_str(), nullptr, 10);
      if (parsed < 32UL || parsed > 220UL) {
        io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
        return true;
      }
      chunk = static_cast<uint16_t>(parsed);
    }
    const std::string local_arg = trim(tokens[1]);
    if (local_arg.empty()) {
      io_.writeln("[MASTER][OTA] usage: ota.update <local_path> [chunk_bytes<=220]");
      return true;
    }
    std::string path;
    const bool has_sep = (local_arg.find('/') != std::string::npos) || (local_arg.find('\\') != std::string::npos);
    if (!has_sep) {
      path = std::string(ota_paths::kStaging) + "/" + shortOtaName(local_arg);
    } else {
      path = resolveFsPath(local_storage_cwd_, local_arg);
    }

    if (!hasRuntimePeer()) {
      io_.writeln("[MASTER][OTA] target not selected");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }

    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    std::string out_message;
    if (!otaUpdateRemote(path, chunk, &out_message)) {
      writef("[MASTER][OTA] update pipeline start failed: %s",
             out_message.empty() ? "failed" : out_message.c_str());
      return true;
    }

    writef("[MASTER][OTA] update pipeline started path=%s chunk=%u",
           path.c_str(),
           static_cast<unsigned int>(chunk));
    io_.writeln("[MASTER][OTA] management-owned steps: prepare -> push -> apply -> wait boot-complete");
    return true;
  }
  if (startsWith(cmd_line, "ota.push ")) {
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][OTA] usage: ota.push <local_path> [chunk_bytes<=220]");
      return true;
    }
    uint16_t chunk = ota_push_chunk_bytes_;
    if (tokens.size() == 3U) {
      const unsigned long parsed = std::strtoul(tokens[2].c_str(), nullptr, 10);
      if (parsed < 32UL || parsed > 220UL) {
        io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
        return true;
      }
      chunk = static_cast<uint16_t>(parsed);
    }
    const std::string local_arg = trim(tokens[1]);
    if (local_arg.empty()) {
      io_.writeln("[MASTER][OTA] usage: ota.push <local_path> [chunk_bytes<=220]");
      return true;
    }
    std::string path;
    const bool has_sep = (local_arg.find('/') != std::string::npos) || (local_arg.find('\\') != std::string::npos);
    if (!has_sep) {
      path = std::string(ota_paths::kStaging) + "/" + shortOtaName(local_arg);
    } else {
      path = resolveFsPath(local_storage_cwd_, local_arg);
    }
    ota_update_pipeline_active_ = false;
    ota_update_prepare_pending_ = false;
    ota_update_prepare_corr_id_ = 0U;
    ota_update_wait_boot_notice_ = false;
    ota_update_image_name_.clear();
    ota_update_req_id_ = 0U;
    ota_update_has_target_peer_ = false;
    ota_update_target_peer_ = {};
    ota_update_staged_path_.clear();
    ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
    (void)otaPushStaged(path, chunk);
    return true;
  }

  if (cmd == "ota.info") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaStatusGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] status requested" : "[MASTER][OTA] status request failed");
    return true;
  }
  if (cmd == "ota.manifest") {
    (void)startPagedFetch(PagedFetchKind::OtaManifest, 8, "[MASTER][OTA] manifest paged fetch queued");
    return true;
  }
  if (cmd == "ota.manifest.rebuild") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaManifestRebuild));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] manifest rebuild requested" : "[MASTER][OTA] manifest rebuild request failed");
    return true;
  }
  if (cmd == "ota.capacity") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaCapacityGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] capacity requested" : "[MASTER][OTA] capacity request failed");
    return true;
  }
  if (cmd == "ota.gate") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaGateGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] gate status requested" : "[MASTER][OTA] gate request failed");
    return true;
  }
  if (startsWith(cmd_line, "ota.clear ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.clear <in|img|man|all>");
      return true;
    }
    std::string scope = lowerCopy(trim(tokens[1]));
    if (scope != "in" && scope != "img" && scope != "man" && scope != "all") {
      io_.writeln("[MASTER][OTA] invalid clear scope (use in|img|man|all)");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaClearScope),
                                           management_utils::buildStringPayloadU16(scope));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][OTA] clear requested scope=%s", scope.c_str());
    } else {
      io_.writeln("[MASTER][OTA] clear request failed");
    }
    return true;
  }
  if (startsWith(cmd_line, "ota.apply ")) {
    if (tokens.size() < 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.apply <image_id|image_name>");
      return true;
    }
    const std::string target = trim(line.substr(line.find(' ') + 1));
    if (target.empty()) {
      io_.writeln("[MASTER][OTA] usage: ota.apply <image_id|image_name>");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaApply),
                                           management_utils::buildStringPayloadU16(target));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][OTA] apply requested target=%s", target.c_str());
    } else {
      io_.writeln("[MASTER][OTA] apply request failed");
    }
    return true;
  }
  if (startsWith(cmd, "ota.rollback")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.rollback <master|slave>");
      return true;
    }
    const std::string target = lowerCopy(trim(tokens[1]));
    if (target == "slave") {
      if (management_transport_ == nullptr) {
        io_.writeln("[MASTER][CLI] management path unavailable");
        captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
        return true;
      }
      ManagementController mgmt(*management_transport_);
      mgmt.setNextReqId(correlation_id_);
      const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaRollback));
      correlation_id_ = mgmt.nextReqId();
      if (ok) {
        io_.writeln("[MASTER][OTA] slave rollback requested");
      } else {
        io_.writeln("[MASTER][OTA] slave rollback request failed");
      }
      return true;
    }
    if (target == "master") {
      if (actions_ == nullptr) {
        io_.writeln("[MASTER][OTA] master rollback unavailable (no actions hook)");
        captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
        return true;
      }
      std::string msg;
      const bool ok = actions_->requestMasterRollback(&msg);
      if (ok) {
        writef("[MASTER][OTA] master rollback requested%s%s",
               msg.empty() ? "" : ": ",
               msg.empty() ? "" : msg.c_str());
      } else {
        writef("[MASTER][OTA] master rollback failed%s%s",
               msg.empty() ? "" : ": ",
               msg.empty() ? "" : msg.c_str());
      }
      captureDispatchSnapshot_(ok,
                               0U,
                               0U,
                               ok ? ManagementStatus::Ok : ManagementStatus::InternalError,
                               ok ? "" : "execution");
      return true;
    }
    io_.writeln("[MASTER][OTA] usage: ota.rollback <master|slave>");
    return true;
  }

  io_.writeln("[MASTER][OTA] unknown ota command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

bool MasterCli::handleLoggerCommands(const std::string& line, const std::string& lower) {
  (void)line;
  if (!startsWith(lower, "logger.") &&
      !startsWith(lower, "channel.") &&
      !startsWith(lower, "chain.loop.")) {
    return false;
  }

  std::string cmd = lower;

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }

  auto runMgmt = [&](ManagementCommandId cmd,
                     const std::vector<uint8_t>& payload,
                     ManagementResponse& out_resp,
                     bool& out_has_response) -> bool {
    out_has_response = false;
    ManagementController mgmt_controller(*management_transport_);
    mgmt_controller.setNextReqId(correlation_id_);

    const uint16_t cmd_id = static_cast<uint16_t>(cmd);
    const bool requires_runtime_target =
        (cmd == ManagementCommandId::LogRemoteStatusGet ||
         cmd == ManagementCommandId::LogRemoteRead ||
         cmd == ManagementCommandId::LogRemoteClear ||
         cmd == ManagementCommandId::LogRemoteControlSet);

    uint32_t req_id = 0U;
    bool accepted = false;
    if (requires_runtime_target) {
      if (!hasRuntimePeer()) {
        io_.writeln("[MASTER][LOGGER] target not selected");
        captureDispatchSnapshot_(false, cmd_id, 0U, ManagementStatus::BadPayload, "target");
        return true;
      }
      accepted = submitRuntimeTargeted_(mgmt_controller, cmd_id, payload, &req_id);
    } else {
      accepted = submitRuntimeTargeted_(mgmt_controller,
                                        cmd_id,
                                        payload,
                                        &req_id,
                                        0U,
                                        false);
    }

    if (!accepted) {
      io_.writeln("[MASTER][LOGGER] submit rejected");
      return true;
    }
    correlation_id_ = mgmt_controller.nextReqId();

    const uint32_t now_ms = nowMs();
    if (management_runtime_ != nullptr) {
      management_runtime_->tick(now_ms);
    }

    ManagementResponse resp{};
    while (management_transport_->pollResponse(resp)) {
      if (resp.source == ManagementSource::Cli &&
          resp.cmd_id == cmd_id &&
          resp.req_id == req_id) {
        out_resp = std::move(resp);
        out_has_response = true;
        break;
      }
    }
    if (!out_has_response) {
      io_.writeln("[MASTER][LOGGER] management response pending");
    }
    return true;
  };

  if (cmd == "logger.remote.status") {
    if (remote_log_pull_active_) {
      io_.writeln("[MASTER][LOGGER][REMOTE] pull is active; stop it first or wait for completion");
      return true;
    }
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteStatusGet, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote status requested");
        } else {
          writef("[MASTER][LOGGER] remote status request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (cmd == "logger.remote.enable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteControlSet,
                management_utils::buildLogControlPayload(true),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote enable requested");
        } else {
          writef("[MASTER][LOGGER] remote enable request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (cmd == "logger.remote.disable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteControlSet,
                management_utils::buildLogControlPayload(false),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote disable requested");
        } else {
          writef("[MASTER][LOGGER] remote disable request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (cmd == "logger.remote.clear") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteClear, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote clear requested");
        } else {
          writef("[MASTER][LOGGER] remote clear request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (startsWith(cmd, "logger.remote.read ")) {
    if (remote_log_pull_active_) {
      io_.writeln("[MASTER][LOGGER][REMOTE] pull is active; stop it first or wait for completion");
      return true;
    }
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][LOGGER] usage: logger.remote.read <offset> [max_bytes<=128]");
      return true;
    }
    uint32_t offset = 0;
    uint32_t max_bytes = 96;
    if (!parseU32Token(tokens[1], offset)) {
      io_.writeln("[MASTER][LOGGER] invalid offset");
      return true;
    }
    if (tokens.size() >= 3U) {
      if (!parseU32Token(tokens[2], max_bytes) || max_bytes == 0U) {
        io_.writeln("[MASTER][LOGGER] invalid max_bytes");
        return true;
      }
    }
    if (max_bytes > 128U) {
      max_bytes = 128U;
    }

    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteRead,
                management_utils::buildLogReadPayload(offset, static_cast<uint16_t>(max_bytes)),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          writef("[MASTER][LOGGER] remote read requested offset=%lu max_bytes=%lu",
                 static_cast<unsigned long>(offset),
                 static_cast<unsigned long>(max_bytes));
        } else {
          writef("[MASTER][LOGGER] remote read request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (startsWith(cmd, "logger.remote.pull")) {
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][LOGGER][REMOTE] usage: logger.remote.pull [chunk_bytes<=128]");
      return true;
    }
    uint32_t chunk_size = 128U;
    if (tokens.size() == 2U) {
      if (!parseU32Token(tokens[1], chunk_size) || chunk_size == 0U || chunk_size > 128U) {
        io_.writeln("[MASTER][LOGGER][REMOTE] invalid chunk_bytes (1..128)");
        return true;
      }
    }
    const bool started = startRemoteLogPull(static_cast<uint16_t>(chunk_size));
    if (!dispatch_snapshot_.seen) {
      captureDispatchSnapshot_(started,
                               0U,
                               0U,
                               started ? ManagementStatus::Ok : ManagementStatus::BadPayload,
                               started ? "" : "validation");
    }
    return true;
  }

  if (cmd == "logger.remote.stop") {
    if (!remote_log_pull_active_) {
      io_.writeln("[MASTER][LOGGER][REMOTE] no active pull");
      // Treat stop-without-active as idempotent no-op for automation/test flows.
      captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
      return true;
    }
    stopRemoteLogPull("stopped by user", false);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (cmd == "channel.runtime.status") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChannelRuntimeGet, {}, resp, has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status != ManagementStatus::Ok) {
        writef("[MASTER][CHANNEL] runtime status failed: %s",
               management_utils::managementStatusToString(resp.status));
        return true;
      }
      ManagementRuntimeChannelStatusPayload status{};
      if (!management_utils::parseRuntimeChannelStatusPayload(resp.payload, status)) {
        io_.writeln("[MASTER][CHANNEL] runtime status parse failed");
        return true;
      }
      writef("[MASTER][CHANNEL] current=%u peers=%u",
             static_cast<unsigned int>(status.current_channel),
             static_cast<unsigned int>(status.entries.size()));
      for (const auto& e : status.entries) {
        writef("  peer=%s channel=%u key=%s",
               macToPrintable(e.peer).c_str(),
               static_cast<unsigned int>(e.channel),
               e.key.c_str());
      }
      return true;
    }
    return true;
  }

  if (startsWith(cmd, "channel.sync ")) {
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][CHANNEL] usage: channel.sync <1..14>");
      return true;
    }
    uint32_t channel_u32 = 0U;
    if (!parseU32Token(tokens[1], channel_u32) || channel_u32 < 1U || channel_u32 > 14U) {
      io_.writeln("[MASTER][CHANNEL] invalid channel (1..14)");
      return true;
    }
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChannelSyncAll,
                management_utils::buildChannelSyncAllPayload(static_cast<uint8_t>(channel_u32)),
                resp,
                has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status == ManagementStatus::OkDeferred || resp.status == ManagementStatus::Ok) {
        writef("[MASTER][CHANNEL] sync started target=%u",
               static_cast<unsigned int>(channel_u32));
      } else {
        writef("[MASTER][CHANNEL] sync rejected: %s",
               management_utils::managementStatusToString(resp.status));
      }
      return true;
    }
    return true;
  }

  if (cmd == "chain.loop.status") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChainLoopControlSet, {}, resp, has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status != ManagementStatus::Ok) {
        writef("[MASTER][CHAIN] status failed: %s",
               management_utils::managementStatusToString(resp.status));
        return true;
      }
      bool enabled = false;
      bool has_value = false;
      if (!management_utils::parseChainLoopControlPayload(resp.payload, has_value, enabled) || !has_value) {
        io_.writeln("[MASTER][CHAIN] status parse failed");
        return true;
      }
      writef("[MASTER][CHAIN] loop_auto=%s", enabled ? "on" : "off");
      return true;
    }
    return true;
  }

  if (cmd == "chain.loop.on" || cmd == "chain.loop.off") {
    const bool enabled = (cmd == "chain.loop.on");
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChainLoopControlSet,
                management_utils::buildChainLoopControlPayload(enabled),
                resp,
                has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status == ManagementStatus::OkDeferred || resp.status == ManagementStatus::Ok) {
        writef("[MASTER][CHAIN] loop_auto apply started target=%s", enabled ? "on" : "off");
      } else {
        writef("[MASTER][CHAIN] loop_auto apply rejected: %s",
               management_utils::managementStatusToString(resp.status));
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.status") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalStatusGet, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status != ManagementStatus::Ok) {
          writef("[MASTER][LOGGER] status failed: %s",
                 management_utils::managementStatusToString(resp.status));
          return true;
        }
        bool available = false;
        bool enabled = false;
        uint8_t min_level = 0;
        LogStorageStats stats{};
        if (!management_utils::parseLogStatusPayload(resp.payload, available, enabled, min_level, stats)) {
          io_.writeln("[MASTER][LOGGER] status parse failed");
          return true;
        }
        writef("[MASTER][LOGGER] enabled=%s min_level=%u store=%s used=%lu dropped=%lu records=%lu rotations=%lu",
               enabled ? "yes" : "no",
               static_cast<unsigned int>(min_level),
               available ? "ready" : "unavailable",
               static_cast<unsigned long>(stats.bytes_used),
               static_cast<unsigned long>(stats.bytes_dropped),
               static_cast<unsigned long>(stats.records_appended),
               static_cast<unsigned long>(stats.rotations));
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.enable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalControlSet,
                management_utils::buildLogControlPayload(true),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok) {
          io_.writeln("[MASTER][LOGGER] enabled");
        } else {
          writef("[MASTER][LOGGER] enable failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.disable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalControlSet,
                management_utils::buildLogControlPayload(false),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok) {
          io_.writeln("[MASTER][LOGGER] disabled");
        } else {
          writef("[MASTER][LOGGER] disable failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.clear") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalClear, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok) {
          io_.writeln("[MASTER][LOGGER] cleared");
        } else {
          writef("[MASTER][LOGGER] clear failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (startsWith(cmd, "logger.read ")) {
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][LOGGER] usage: logger.read <offset> [max_bytes]");
      return true;
    }
    uint32_t offset = 0;
    uint32_t max_bytes = 128;
    if (!parseU32Token(tokens[1], offset)) {
      io_.writeln("[MASTER][LOGGER] invalid offset");
      return true;
    }
    if (tokens.size() >= 3U) {
      if (!parseU32Token(tokens[2], max_bytes) || max_bytes == 0U) {
        io_.writeln("[MASTER][LOGGER] invalid max_bytes");
        return true;
      }
    }
    if (max_bytes > 512U) {
      max_bytes = 512U;
    }

    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalRead,
                management_utils::buildLogReadPayload(offset, static_cast<uint16_t>(max_bytes)),
                resp,
                has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status != ManagementStatus::Ok) {
        writef("[MASTER][LOGGER] read failed: %s",
               management_utils::managementStatusToString(resp.status));
        return true;
      }
      uint32_t offset_resp = 0;
      uint32_t total_size = 0;
      std::vector<uint8_t> chunk{};
      if (!management_utils::parseLogReadResponsePayload(resp.payload, offset_resp, total_size, chunk)) {
        io_.writeln("[MASTER][LOGGER] read parse failed");
        return true;
      }

      writef("[MASTER][LOGGER] chunk offset=%lu total=%lu bytes=%u",
             static_cast<unsigned long>(offset_resp),
             static_cast<unsigned long>(total_size),
             static_cast<unsigned int>(chunk.size()));
      if (chunk.empty()) {
        io_.writeln("  (empty)");
        return true;
      }
      for (size_t i = 0; i < chunk.size(); i += 16U) {
        char linebuf[96] = {0};
        int p = std::snprintf(linebuf, sizeof(linebuf), "  %08lX: ",
                              static_cast<unsigned long>(offset_resp + static_cast<uint32_t>(i)));
        const size_t end = std::min<size_t>(i + 16U, chunk.size());
        for (size_t j = i; j < end && p > 0 && static_cast<size_t>(p) < sizeof(linebuf); ++j) {
          p += std::snprintf(linebuf + p, sizeof(linebuf) - static_cast<size_t>(p), "%02X ", chunk[j]);
        }
        io_.writeln(std::string(linebuf));
      }
      return true;
    }
    return true;
  }

  io_.writeln("[MASTER][LOGGER] unknown logger command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

void MasterCli::clearPagedFetchState() {
  paged_fetch_kind_ = PagedFetchKind::None;
  paged_fetch_page_size_ = 6;
  paged_fetch_next_cursor_ = 0;
  paged_fetch_expected_cursor_ = 0;
  paged_fetch_snapshot_locked_ = false;
  paged_fetch_snapshot_id_ = 0;
  paged_fetch_total_count_ = 0;
  paged_fetch_has_target_peer_ = false;
  paged_fetch_target_peer_ = {};
  paged_fetch_restart_count_ = 0;
  paged_caps_cache_.clear();
  paged_telem_cache_.clear();
  paged_telem_samples_cache_.clear();
  paged_settings_cache_.clear();
  paged_ota_manifest_cache_.clear();
  paged_caps_seen_keys_.clear();
  paged_telem_seen_ids_.clear();
  paged_telem_seen_keys_.clear();
  paged_telem_samples_seen_ids_.clear();
  paged_telem_samples_seen_keys_.clear();
  paged_settings_seen_ids_.clear();
  paged_settings_seen_keys_.clear();
  paged_ota_seen_ids_.clear();
  paged_ota_seen_names_.clear();
}

bool MasterCli::enqueuePagedFetchPage(uint16_t cursor) {
  if (!paged_fetch_has_target_peer_) {
    return false;
  }
  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    return enqueueDescriptorQuery("CAPS.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    return enqueueDescriptorQuery("TELEM.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    return enqueueDescriptorQuery("TELEM.PULL.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    return enqueueDescriptorQuery("SETTINGS.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    return enqueueDescriptorQuery(
        "OTA.MANIFEST.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
        &paged_fetch_target_peer_);
  }
  return false;
}

bool MasterCli::startPagedFetch(PagedFetchKind kind, uint8_t page_size, const char* queued_msg) {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return false;
  }

  clearPagedFetchState();
  paged_fetch_kind_ = kind;
  paged_fetch_page_size_ = (page_size == 0) ? 6 : page_size;
  if (paged_fetch_page_size_ > 16U) {
    paged_fetch_page_size_ = 16U;
  }
  paged_fetch_next_cursor_ = 0;
  paged_fetch_expected_cursor_ = 0;
  paged_fetch_has_target_peer_ = true;
  paged_fetch_target_peer_ = target_peer;

  if (!enqueuePagedFetchPage(0)) {
    io_.writeln("[MASTER][CLI] paged fetch request failed");
    clearPagedFetchState();
    return false;
  }

  io_.writeln(queued_msg);
  return true;
}

void MasterCli::printPagedFetchSummary() const {
  const char* kind = "unknown";
  size_t received = 0;
  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    kind = "caps";
    received = paged_caps_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    kind = "telem";
    received = paged_telem_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    kind = "telem.now";
    received = paged_telem_samples_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    kind = "settings";
    received = paged_settings_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    kind = "ota.manifest";
    received = paged_ota_manifest_cache_.size();
  }
  const size_t expected = static_cast<size_t>(paged_fetch_total_count_);
  const size_t missing = (received < expected) ? (expected - received) : 0U;
  writef("[MASTER][CLI] paged fetch %s complete snapshot=%lu total=%u received=%u missing=%u page_size=%u",
         kind,
         static_cast<unsigned long>(paged_fetch_snapshot_id_),
         static_cast<unsigned int>(paged_fetch_total_count_),
         static_cast<unsigned int>(received),
         static_cast<unsigned int>(missing),
         static_cast<unsigned int>(paged_fetch_page_size_));
}

bool MasterCli::handlePagedDescriptorResponse(const DescriptorResponse& d) {
  if (paged_fetch_kind_ == PagedFetchKind::None) {
    return false;
  }

  const DescriptorResponseType expected =
      (paged_fetch_kind_ == PagedFetchKind::Capabilities)
          ? DescriptorResponseType::Capabilities
          : (paged_fetch_kind_ == PagedFetchKind::Telemetry
                 ? DescriptorResponseType::Telemetry
                 : (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot
                        ? DescriptorResponseType::TelemetrySnapshot
                        : (paged_fetch_kind_ == PagedFetchKind::Settings ? DescriptorResponseType::Settings
                                                                         : DescriptorResponseType::OtaManifest)));

  if (d.type != expected) {
    return false;
  }

  if (!d.is_paged) {
    if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
      // Compatibility path: older nodes may return non-paged live telemetry.
      printDescriptorResponse(d);
      clearPagedFetchState();
      return true;
    }
    io_.writeln("[MASTER][CLI] paged fetch failed: non-paged response");
    clearPagedFetchState();
    return true;
  }

  if (d.cursor != paged_fetch_expected_cursor_) {
    if (paged_fetch_restart_count_ >= 1U) {
      io_.writeln("[MASTER][CLI] paged fetch failed: cursor mismatch");
      clearPagedFetchState();
      return true;
    }
    ++paged_fetch_restart_count_;
    paged_caps_cache_.clear();
    paged_telem_cache_.clear();
    paged_telem_samples_cache_.clear();
    paged_settings_cache_.clear();
    paged_ota_manifest_cache_.clear();
    paged_caps_seen_keys_.clear();
    paged_telem_seen_ids_.clear();
    paged_telem_seen_keys_.clear();
    paged_telem_samples_seen_ids_.clear();
    paged_telem_samples_seen_keys_.clear();
    paged_settings_seen_ids_.clear();
    paged_settings_seen_keys_.clear();
    paged_ota_seen_ids_.clear();
    paged_ota_seen_names_.clear();
    paged_fetch_snapshot_locked_ = false;
    paged_fetch_expected_cursor_ = 0;
    paged_fetch_next_cursor_ = 0;
    if (paged_fetch_page_size_ > 1U) {
      paged_fetch_page_size_ = static_cast<uint8_t>(paged_fetch_page_size_ / 2U);
      if (paged_fetch_page_size_ == 0U) {
        paged_fetch_page_size_ = 1U;
      }
    }
    if (!enqueuePagedFetchPage(0)) {
      io_.writeln("[MASTER][CLI] paged fetch restart failed");
      clearPagedFetchState();
    } else {
      io_.writeln("[MASTER][CLI] paged fetch restarted (cursor mismatch)");
    }
    return true;
  }

  if (!paged_fetch_snapshot_locked_) {
    paged_fetch_snapshot_locked_ = true;
    paged_fetch_snapshot_id_ = d.snapshot_id;
    paged_fetch_total_count_ = d.total_count;
  } else if (paged_fetch_kind_ != PagedFetchKind::TelemetrySnapshot &&
             paged_fetch_snapshot_id_ != d.snapshot_id) {
    if (paged_fetch_restart_count_ >= 1U) {
      io_.writeln("[MASTER][CLI] paged fetch failed: snapshot changed repeatedly");
      clearPagedFetchState();
      return true;
    }
    ++paged_fetch_restart_count_;
    paged_caps_cache_.clear();
    paged_telem_cache_.clear();
    paged_telem_samples_cache_.clear();
    paged_settings_cache_.clear();
    paged_ota_manifest_cache_.clear();
    paged_caps_seen_keys_.clear();
    paged_telem_seen_ids_.clear();
    paged_telem_seen_keys_.clear();
    paged_telem_samples_seen_ids_.clear();
    paged_telem_samples_seen_keys_.clear();
    paged_settings_seen_ids_.clear();
    paged_settings_seen_keys_.clear();
    paged_ota_seen_ids_.clear();
    paged_ota_seen_names_.clear();
    paged_fetch_snapshot_locked_ = false;
    paged_fetch_expected_cursor_ = 0;
    paged_fetch_next_cursor_ = 0;
    if (!enqueuePagedFetchPage(0)) {
      io_.writeln("[MASTER][CLI] paged fetch restart failed");
      clearPagedFetchState();
    } else {
      io_.writeln("[MASTER][CLI] paged fetch restarted (snapshot changed)");
    }
    return true;
  }

  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    for (const auto& cap : d.capabilities) {
      const std::string dedupe_key = cap.key + '\x1F' + cap.description;
      if (paged_caps_seen_keys_.insert(dedupe_key).second) {
        paged_caps_cache_.push_back(cap);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    for (const auto& t : d.telemetry) {
      bool inserted = false;
      if (t.metric_id != 0U) {
        inserted = paged_telem_seen_ids_.insert(t.metric_id).second;
      } else {
        inserted = paged_telem_seen_keys_.insert(t.key).second;
      }
      if (inserted) {
        paged_telem_cache_.push_back(t);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    for (const auto& s : d.telemetry_samples) {
      bool inserted = false;
      if (s.metric_id != 0U) {
        inserted = paged_telem_samples_seen_ids_.insert(s.metric_id).second;
      } else {
        inserted = paged_telem_samples_seen_keys_.insert(s.key).second;
      }
      if (inserted) {
        paged_telem_samples_cache_.push_back(s);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    for (const auto& s : d.settings) {
      bool inserted = false;
      if (s.setting_id != 0U) {
        inserted = paged_settings_seen_ids_.insert(s.setting_id).second;
      } else {
        inserted = paged_settings_seen_keys_.insert(s.key).second;
      }
      if (inserted) {
        paged_settings_cache_.push_back(s);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    for (const auto& e : d.ota_manifest) {
      bool inserted = false;
      if (e.file_id != 0U) {
        inserted = paged_ota_seen_ids_.insert(e.file_id).second;
      } else {
        inserted = paged_ota_seen_names_.insert(e.file_name).second;
      }
      if (inserted) {
        paged_ota_manifest_cache_.push_back(e);
      }
    }
  }

  if (!d.done) {
    if (d.next_cursor <= d.cursor) {
      if (paged_fetch_restart_count_ >= 1U) {
        io_.writeln("[MASTER][CLI] paged fetch failed: invalid next cursor");
        clearPagedFetchState();
        return true;
      }
      ++paged_fetch_restart_count_;
      paged_caps_cache_.clear();
      paged_telem_cache_.clear();
      paged_telem_samples_cache_.clear();
      paged_settings_cache_.clear();
      paged_ota_manifest_cache_.clear();
      paged_caps_seen_keys_.clear();
      paged_telem_seen_ids_.clear();
      paged_telem_seen_keys_.clear();
      paged_telem_samples_seen_ids_.clear();
      paged_telem_samples_seen_keys_.clear();
      paged_settings_seen_ids_.clear();
      paged_settings_seen_keys_.clear();
      paged_ota_seen_ids_.clear();
      paged_ota_seen_names_.clear();
      paged_fetch_snapshot_locked_ = false;
      paged_fetch_expected_cursor_ = 0;
      paged_fetch_next_cursor_ = 0;
      if (paged_fetch_page_size_ > 1U) {
        paged_fetch_page_size_ = static_cast<uint8_t>(paged_fetch_page_size_ / 2U);
        if (paged_fetch_page_size_ == 0U) {
          paged_fetch_page_size_ = 1U;
        }
      }
      if (!enqueuePagedFetchPage(0)) {
        io_.writeln("[MASTER][CLI] paged fetch restart failed");
        clearPagedFetchState();
      } else {
        io_.writeln("[MASTER][CLI] paged fetch restarted (invalid next cursor)");
      }
      return true;
    }
    paged_fetch_next_cursor_ = d.next_cursor;
    paged_fetch_expected_cursor_ = d.next_cursor;
    if (!enqueuePagedFetchPage(paged_fetch_next_cursor_)) {
      io_.writeln("[MASTER][CLI] paged fetch next page request failed");
      clearPagedFetchState();
    }
    return true;
  }

  size_t received = 0U;
  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    received = paged_caps_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    received = paged_telem_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    received = paged_telem_samples_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    received = paged_settings_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    received = paged_ota_manifest_cache_.size();
  }
  const size_t expected_total = static_cast<size_t>(paged_fetch_total_count_);
  if (received != expected_total) {
    if (paged_fetch_restart_count_ >= 1U || paged_fetch_page_size_ == 1U) {
      writef("[MASTER][CLI] paged fetch failed: incomplete dataset total=%u received=%u",
             static_cast<unsigned int>(expected_total),
             static_cast<unsigned int>(received));
      clearPagedFetchState();
      return true;
    }
    ++paged_fetch_restart_count_;
    paged_caps_cache_.clear();
    paged_telem_cache_.clear();
    paged_telem_samples_cache_.clear();
    paged_settings_cache_.clear();
    paged_ota_manifest_cache_.clear();
    paged_caps_seen_keys_.clear();
    paged_telem_seen_ids_.clear();
    paged_telem_seen_keys_.clear();
    paged_telem_samples_seen_ids_.clear();
    paged_telem_samples_seen_keys_.clear();
    paged_settings_seen_ids_.clear();
    paged_settings_seen_keys_.clear();
    paged_ota_seen_ids_.clear();
    paged_ota_seen_names_.clear();
    paged_fetch_snapshot_locked_ = false;
    paged_fetch_expected_cursor_ = 0;
    paged_fetch_next_cursor_ = 0;
    if (paged_fetch_page_size_ > 1U) {
      paged_fetch_page_size_ = static_cast<uint8_t>(paged_fetch_page_size_ / 2U);
      if (paged_fetch_page_size_ == 0U) {
        paged_fetch_page_size_ = 1U;
      }
    }
    if (!enqueuePagedFetchPage(0)) {
      io_.writeln("[MASTER][CLI] paged fetch restart failed");
      clearPagedFetchState();
    } else {
      writef("[MASTER][CLI] paged fetch restarted (incomplete dataset; new page_size=%u)",
             static_cast<unsigned int>(paged_fetch_page_size_));
    }
    return true;
  }

  DescriptorResponse merged = d;
  merged.is_paged = false;
  merged.done = true;
  merged.returned_count = 0;
  merged.cursor = 0;
  merged.next_cursor = 0;
  merged.total_count = 0;
  merged.snapshot_id = 0;

  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    merged.capabilities = paged_caps_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    merged.telemetry = paged_telem_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    merged.telemetry_samples = paged_telem_samples_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    merged.settings = paged_settings_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    merged.ota_manifest = paged_ota_manifest_cache_;
  }

  printDescriptorResponse(merged);
  printPagedFetchSummary();
  clearPagedFetchState();
  return true;
}


bool MasterCli::logEnabled(CliLogLevel level) const {
  return static_cast<uint8_t>(log_level_) >= static_cast<uint8_t>(level);
}

bool MasterCli::enqueueDescriptorQuery(const std::string& cmd, const MacAddress* target_peer) {
  MacAddress target{};
  if (target_peer != nullptr) {
    target = *target_peer;
  } else if (!resolveRuntimePeer(target)) {
    io_.writeln("[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return false;
  }
  if (cmd.empty()) {
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "parse");
    return false;
  }
  if (descriptor_request_queue_.size() >= descriptor_queue_max_) {
    ++descriptor_queue_drop_count_;
    if (logEnabled(CliLogLevel::Error)) {
      writef("[MASTER][CLI][QUEUE] drop full depth=%u max=%u cmd=%s",
             static_cast<unsigned int>(descriptor_request_queue_.size()),
             static_cast<unsigned int>(descriptor_queue_max_),
             cmd.c_str());
    }
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::QueueFull, "queue");
    return false;
  }
  DescriptorRequestQueueItem item{};
  item.cmd = cmd;
  item.enqueued_ms = nowMs();
  item.has_target_peer = true;
  item.target_peer = target;
  descriptor_request_queue_.push_back(std::move(item));
  captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
  return true;
}

bool MasterCli::executeDescriptorQueryNow(const std::string& cmd, const MacAddress* target_peer) {
  MacAddress resolved_target{};
  if (target_peer != nullptr) {
    resolved_target = *target_peer;
  } else if (!resolveRuntimePeer(resolved_target)) {
    return false;
  }

  auto parsePageCmd = [](const std::string& raw, const char* prefix, uint16_t& out_cursor, uint8_t& out_size) -> bool {
    if (!startsWith(raw, prefix)) {
      return false;
    }
    const std::string body = trim(raw.substr(std::strlen(prefix)));
    const size_t sp = body.find(' ');
    if (sp == std::string::npos) {
      return false;
    }
    const std::string c = trim(body.substr(0, sp));
    const std::string s = trim(body.substr(sp + 1));
    if (c.empty() || s.empty()) {
      return false;
    }
    const unsigned long cursor = std::strtoul(c.c_str(), nullptr, 10);
    const unsigned long size = std::strtoul(s.c_str(), nullptr, 10);
    if (cursor > 0xFFFFUL || size > 0xFFUL) {
      return false;
    }
    out_cursor = static_cast<uint16_t>(cursor);
    out_size = static_cast<uint8_t>(size);
    return true;
  };

  auto parseLogReadCmd = [](const std::string& raw, uint32_t& out_offset, uint16_t& out_max_bytes) -> bool {
    if (!startsWith(raw, "LOGGER.READ ")) {
      return false;
    }
    const std::string body = trim(raw.substr(12));
    if (body.empty()) {
      return false;
    }
    const size_t sp = body.find(' ');
    std::string o = body;
    std::string m;
    if (sp != std::string::npos) {
      o = trim(body.substr(0, sp));
      m = trim(body.substr(sp + 1));
    }
    if (o.empty()) {
      return false;
    }
    const unsigned long offset = std::strtoul(o.c_str(), nullptr, 10);
    out_offset = static_cast<uint32_t>(offset);
    out_max_bytes = 96;
    if (!m.empty()) {
      const unsigned long mb = std::strtoul(m.c_str(), nullptr, 10);
      if (mb == 0U || mb > 0xFFFFU) {
        return false;
      }
      out_max_bytes = static_cast<uint16_t>(mb);
    }
    if (out_max_bytes > 128U) {
      out_max_bytes = 128U;
    }
    return true;
  };

  auto parseStoragePathCmd = [](const std::string& raw,
                                const char* prefix,
                                std::string& out_path) -> bool {
    if (!startsWith(raw, prefix)) {
      return false;
    }
    out_path = trim(raw.substr(std::strlen(prefix)));
    return !out_path.empty();
  };

  auto parseOtaArgCmd = [](const std::string& raw, const char* prefix, std::string& out_arg) -> bool {
    if (!startsWith(raw, prefix)) {
      return false;
    }
    out_arg = trim(raw.substr(std::strlen(prefix)));
    return !out_arg.empty();
  };

  bool sent = false;
  const bool mgmt_available = (management_transport_ != nullptr);
  if (!mgmt_available) {
    if (logEnabled(CliLogLevel::Error)) {
      io_.writeln("[MASTER][CLI] management path unavailable");
    }
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return false;
  }
  uint16_t page_cursor = 0;
  uint8_t page_size = 0;
  uint32_t log_offset = 0;
  uint16_t log_max_bytes = 0;
  std::string storage_path;
  std::string ota_arg;
  auto buildPagePayload = [](uint16_t cursor, uint8_t size) {
    std::vector<uint8_t> payload{};
    management_utils::appendU16Le(payload, cursor);
    payload.push_back(size);
    return payload;
  };
  auto sendMgmtRaw = [&](ManagementCommandId cmd_id, const std::vector<uint8_t>& payload = {}) -> bool {
    if (!mgmt_available) return false;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool accepted = submitRuntimeTargeted_(mgmt,
                                                 static_cast<uint16_t>(cmd_id),
                                                 payload,
                                                 nullptr,
                                                 0U,
                                                 false,
                                                 &resolved_target);
    correlation_id_ = mgmt.nextReqId();
    return accepted;
  };
  if (parsePageCmd(cmd, "CAPS.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::CapsPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "TELEM.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::TelemSchemaPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "TELEM.PULL.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::TelemPull,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "SETTINGS.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::SettingsPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "OTA.MANIFEST.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::OtaManifestPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (cmd == "DESC.GET") {
    sent = sendMgmtRaw(ManagementCommandId::DescGet);
  } else if (cmd == "CAPS.GET") {
    sent = sendMgmtRaw(ManagementCommandId::CapsGet);
  } else if (cmd == "TELEM.GET") {
    sent = sendMgmtRaw(ManagementCommandId::TelemSchemaGet);
  } else if (cmd == "TELEM.PULL") {
    sent = sendMgmtRaw(ManagementCommandId::TelemPull);
  } else if (cmd == "LIVE.GET") {
    sent = sendMgmtRaw(ManagementCommandId::LiveGet);
    if (sent) {
      auto_pull_.markLivenessRequested(nowMs());
      probe_pending_kind_ = ProbePendingKind::Live;
      probe_sent_ms_ = nowMs();
    }
  } else if (cmd == "PING.GET") {
    sent = sendMgmtRaw(ManagementCommandId::PingGet);
    if (sent) {
      auto_pull_.markLivenessRequested(nowMs());
      probe_pending_kind_ = ProbePendingKind::Ping;
      probe_sent_ms_ = nowMs();
    }
  } else if (cmd == "AUDIO.PING") {
    sent = sendMgmtRaw(ManagementCommandId::AudioPingRequest);
  } else if (cmd == "TIME.GET") {
    sent = sendMgmtRaw(ManagementCommandId::TimeGet);
  } else if (startsWith(cmd, "TIME.SET ")) {
    const std::string arg = cmd.substr(9);
    const uint64_t epoch_s = static_cast<uint64_t>(std::strtoull(arg.c_str(), nullptr, 10));
    sent = sendMgmtRaw(ManagementCommandId::TimeSet, management_utils::buildTimeSetPayload(epoch_s));
  } else if (cmd == "SETTINGS.GET") {
    sent = sendMgmtRaw(ManagementCommandId::SettingsGet);
  } else if (startsWith(cmd, "SETTING.GET ")) {
    const std::string key = cmd.substr(12);
    sent = sendMgmtRaw(ManagementCommandId::SettingGet, management_utils::buildSettingGetByKeyPayload(key));
  } else if (startsWith(cmd, "SETTING.SET ")) {
    const std::string body = cmd.substr(12);
    const size_t eq = body.find('=');
    if (eq == std::string::npos || eq == 0) {
      return false;
    }
    const std::string key = body.substr(0, eq);
    const std::string value = trim(body.substr(eq + 1));
    sent = sendMgmtRaw(ManagementCommandId::SettingSet,
                       management_utils::buildSettingSetByKeyPayload(key, value));
  } else if (cmd == "LOGGER.STATUS") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteStatusGet);
  } else if (cmd == "LOGGER.CLEAR") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteClear);
  } else if (cmd == "LOGGER.ENABLE") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteControlSet,
                       management_utils::buildLogControlPayload(true));
  } else if (cmd == "LOGGER.DISABLE") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteControlSet,
                       management_utils::buildLogControlPayload(false));
  } else if (parseLogReadCmd(cmd, log_offset, log_max_bytes)) {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteRead,
                       management_utils::buildLogReadPayload(log_offset, log_max_bytes));
  } else if (cmd == "STORAGE.INFO") {
    sent = sendMgmtRaw(ManagementCommandId::StorageInfoGet);
  } else if (parseStoragePathCmd(cmd, "STORAGE.LIST ", storage_path)) {
    sent = sendMgmtRaw(ManagementCommandId::StorageList,
                       management_utils::buildStringPayloadU16(storage_path));
  } else if (parseStoragePathCmd(cmd, "STORAGE.STAT ", storage_path)) {
    sent = sendMgmtRaw(ManagementCommandId::StorageStat,
                       management_utils::buildStringPayloadU16(storage_path));
  } else if (cmd == "STORAGE.FORMAT") {
    sent = sendMgmtRaw(ManagementCommandId::StorageFormat);
  } else if (cmd == "OTA.STATUS") {
    sent = sendMgmtRaw(ManagementCommandId::OtaStatusGet);
  } else if (cmd == "OTA.MANIFEST.GET") {
    sent = sendMgmtRaw(ManagementCommandId::OtaManifestGet);
  } else if (cmd == "OTA.MANIFEST.REBUILD") {
    sent = sendMgmtRaw(ManagementCommandId::OtaManifestRebuild);
  } else if (parseOtaArgCmd(cmd, "OTA.CLEAR ", ota_arg)) {
    sent = sendMgmtRaw(ManagementCommandId::OtaClearScope,
                       management_utils::buildStringPayloadU16(ota_arg));
  } else if (cmd == "OTA.CAPACITY") {
    sent = sendMgmtRaw(ManagementCommandId::OtaCapacityGet);
  } else if (cmd == "OTA.GATE") {
    sent = sendMgmtRaw(ManagementCommandId::OtaGateGet);
  } else if (parseOtaArgCmd(cmd, "OTA.APPLY ", ota_arg)) {
    sent = sendMgmtRaw(ManagementCommandId::OtaApply,
                       management_utils::buildStringPayloadU16(ota_arg));
  }
  return sent;
}

bool MasterCli::sendDescriptorQuery(const std::string& cmd) {
  return enqueueDescriptorQuery(cmd);
}

void MasterCli::pumpDescriptorQueue(uint32_t now_ms) {
  if (descriptor_request_queue_.empty()) {
    return;
  }

  uint8_t budget = descriptor_send_budget_per_tick_;
  while (budget > 0 && !descriptor_request_queue_.empty()) {
    DescriptorRequestQueueItem item = std::move(descriptor_request_queue_.front());
    descriptor_request_queue_.pop_front();
    const bool sent =
        executeDescriptorQueryNow(item.cmd, item.has_target_peer ? &item.target_peer : nullptr);
    if (sent) {
      ++descriptor_queue_sent_count_;
      if (logEnabled(CliLogLevel::Debug)) {
        writef("[MASTER][CLI][QUEUE] sent age_ms=%lu cmd=%s",
               static_cast<unsigned long>(now_ms - item.enqueued_ms),
               item.cmd.c_str());
      }
    } else {
      ++descriptor_queue_fail_count_;
      if (logEnabled(CliLogLevel::Error)) {
        writef("[MASTER][CLI][QUEUE] send_failed age_ms=%lu cmd=%s",
               static_cast<unsigned long>(now_ms - item.enqueued_ms),
               item.cmd.c_str());
      }
    }
    --budget;
  }
}

void MasterCli::pumpManagementMailbox() {
  if (management_transport_ == nullptr) {
    return;
  }
  if (!cli_enabled_) {
    ManagementResponse drop_resp{};
    while (management_transport_->pollResponse(drop_resp)) {}
    ManagementEvent drop_evt{};
    while (management_transport_->pollEvent(drop_evt)) {}
    return;
  }
  auto readU16Le = [](const uint8_t* p, size_t n, uint16_t& out) -> bool {
    if (p == nullptr || n < 2U) return false;
    out = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8U);
    return true;
  };
  auto readU32Le = [](const uint8_t* p, size_t n, uint32_t& out) -> bool {
    if (p == nullptr || n < 4U) return false;
    out = static_cast<uint32_t>(p[0]) |
          (static_cast<uint32_t>(p[1]) << 8U) |
          (static_cast<uint32_t>(p[2]) << 16U) |
          (static_cast<uint32_t>(p[3]) << 24U);
    return true;
  };
  auto responsePeerContext = [&](const ManagementResponse& resp) -> std::string {
    if (!resp.has_requested_peer && !resp.has_executed_peer && !resp.activation_performed) {
      return std::string();
    }
    const std::string requested = resp.has_requested_peer ? macToPrintable(resp.requested_peer) : "-";
    const std::string executed = resp.has_executed_peer ? macToPrintable(resp.executed_peer) : "-";
    std::string out = " req_peer=" + requested +
                      " exec_peer=" + executed +
                      " activated=" + (resp.activation_performed ? std::string("yes") : std::string("no"));
    if (resp.activation_performed) {
      out += " act_ms=" + std::to_string(static_cast<unsigned int>(resp.activation_latency_ms));
    }
    return out;
  };
  auto eventPeerContext = [&](const ManagementEvent& evt) -> std::string {
    if (!evt.has_requested_peer && !evt.has_executed_peer && !evt.activation_performed) {
      return std::string();
    }
    const std::string requested = evt.has_requested_peer ? macToPrintable(evt.requested_peer) : "-";
    const std::string executed = evt.has_executed_peer ? macToPrintable(evt.executed_peer) : "-";
    std::string out = " req_peer=" + requested +
                      " exec_peer=" + executed +
                      " activated=" + (evt.activation_performed ? std::string("yes") : std::string("no"));
    if (evt.activation_performed) {
      out += " act_ms=" + std::to_string(static_cast<unsigned int>(evt.activation_latency_ms));
    }
    return out;
  };
  ManagementResponse resp{};
  while (management_transport_->pollResponse(resp)) {
    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::LiveMonitorStatusGet)) {
      const bool show_live_status =
          live_monitor_status_pending_ &&
          resp.source == ManagementSource::Cli &&
          resp.req_id == live_monitor_status_req_id_;
      if (show_live_status) {
        live_monitor_status_pending_ = false;
        live_monitor_status_req_id_ = 0U;
        if (resp.status != ManagementStatus::Ok && resp.status != ManagementStatus::OkDeferred) {
          writef("[MASTER][LIVE] monitor status failed: %s",
                 management_utils::managementStatusToString(resp.status));
        } else {
          ManagementLiveMonitorStatusPayload live_status{};
          if (management_utils::parseLiveMonitorStatusPayload(resp.payload, live_status)) {
            auto ignoreLabel = [&](uint16_t mask) -> const char* {
              if (mask == 0U) return "none";
              if ((mask & 0x0001U) != 0U && (mask & ~0x0001U) == 0U) return "ota_push";
              if ((mask & 0x0002U) != 0U && (mask & ~0x0002U) == 0U) return "ota_update";
              if ((mask & 0x0004U) != 0U && (mask & ~0x0004U) == 0U) return "critical_cmd";
              if ((mask & 0x0008U) != 0U && (mask & ~0x0008U) == 0U) return "master_update_guard";
              return "mixed";
            };
            io_.writeln("[MASTER][LIVE] status");
            writef("  monitor : %s", live_status.enabled ? "ON" : "OFF");
            writef("  tracked : %u", static_cast<unsigned int>(live_status.tracked_paired_count));
            writef("  online  : %u", static_cast<unsigned int>(live_status.online_count));
            writef("  offline : %u", static_cast<unsigned int>(live_status.offline_count));
            writef("  ignore  : %s", ignoreLabel(live_status.ignore_reason_mask));
          } else {
            io_.writeln("[MASTER][LIVE] monitor status payload decode failed");
          }
        }
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyStatusGet) &&
        (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred)) {
      auto topoStateLabel = [](uint8_t state) -> const char* {
        switch (state) {
          case 1U: return "staged";
          case 2U: return "committed";
          default: return "none";
        }
      };
      ManagementTopologyStatusPayload topo{};
      if (management_utils::parseTopologyStatusPayload(resp.payload, topo)) {
        io_.writeln("[MASTER][TOPO] status");
        writef("  schema         : v%u", static_cast<unsigned int>(topo.schema_version));
        writef("  staged         : %s", topo.has_staged ? "yes" : "no");
        writef("  committed      : %s", topo.has_committed ? "yes" : "no");
        writef("  staged_ver     : %lu", static_cast<unsigned long>(topo.staged_version));
        writef("  committed_ver  : %lu", static_cast<unsigned long>(topo.committed_version));
        writef("  staged_state   : %s", topoStateLabel(topo.staged_state));
        writef("  committed_state: %s", topoStateLabel(topo.committed_state));
        writef("  staged_slots   : %u", static_cast<unsigned int>(topo.staged_slot_count));
        writef("  committed_slots: %u", static_cast<unsigned int>(topo.committed_slot_count));
        writef("  staged_groups  : %u", static_cast<unsigned int>(topo.staged_group_count));
        writef("  committed_groups: %u", static_cast<unsigned int>(topo.committed_group_count));
      } else if (resp.status == ManagementStatus::OkDeferred || resp.payload.empty()) {
        io_.writeln("[MASTER][TOPO] status request accepted (remote/async)");
      } else {
        io_.writeln("[MASTER][TOPO] status payload decode failed");
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologySlotsGet) &&
        (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred)) {
      uint8_t state = 0U;
      std::vector<ManagementTopologySlotPayload> slots{};
      if (management_utils::parseTopologySlotsPayload(resp.payload, state, slots)) {
        const char* state_label = (state == 1U) ? "staged" : (state == 2U) ? "committed" : "none";
        uint32_t enabled = 0U;
        for (const auto& s : slots) {
          if (s.enabled) ++enabled;
        }
        writef("[MASTER][TOPO] slots state=%s total=%u enabled=%u",
               state_label,
               static_cast<unsigned int>(slots.size()),
               static_cast<unsigned int>(enabled));
        for (const auto& s : slots) {
          if (!s.enabled) {
            continue;
          }
          writef("  slot=%u peer=%s role=%u gid=%u rid=%d lvid=%u pvid=%u axis=%d dly=%u hold=%u",
                 static_cast<unsigned int>(s.slot_index),
                 macToPrintable(s.peer).c_str(),
                 static_cast<unsigned int>(s.peer_role),
                 static_cast<unsigned int>(s.group_id),
                 static_cast<int>(s.relative_index),
                 static_cast<unsigned int>(s.local_virtual_index),
                 static_cast<unsigned int>(s.peer_virtual_index),
                 static_cast<int>(s.axis_order),
                 static_cast<unsigned int>(s.delay_ms),
                 static_cast<unsigned int>(s.hold_ms));
        }
      } else if (resp.status == ManagementStatus::OkDeferred || resp.payload.empty()) {
        io_.writeln("[MASTER][TOPO] slots request accepted (remote/async)");
      } else {
        io_.writeln("[MASTER][TOPO] slots payload decode failed");
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyTriggerSend) &&
        (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred)) {
      ManagementTopologyTriggerSendResponsePayload trigger_resp{};
      if (management_utils::parseTopologyTriggerSendResponsePayload(resp.payload, trigger_resp)) {
        writef("[MASTER][TOPO] trigger queued seq=%u",
               static_cast<unsigned int>(trigger_resp.seq));
      } else if (resp.status == ManagementStatus::OkDeferred || resp.payload.empty()) {
        io_.writeln("[MASTER][TOPO] trigger request accepted (async)");
      }
    }

    const bool is_archive_cmd =
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveList) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveRunning) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveStaged) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveVerify) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveRestore) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveDelete) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveClear);
    if (is_archive_cmd) {
      std::string arc_message;
      (void)management_utils::parseStringPayloadU16(resp.payload, arc_message);
      if (!arc_message.empty()) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          writef("[MASTER][OTA][ARC] %s", arc_message.c_str());
        } else {
          writef("[MASTER][OTA][ARC] failed: %s", arc_message.c_str());
        }
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaPushStart) &&
        ota_push_active_ &&
        resp.req_id == ota_push_corr_id_) {
      if (resp.status != ManagementStatus::Ok &&
          resp.status != ManagementStatus::OkDeferred) {
        stopOtaPush("management ota.push start rejected", false);
      }
    }

    const std::string peer_ctx = responsePeerContext(resp);
    if (resp.status == ManagementStatus::InternalError ||
        resp.status == ManagementStatus::Timeout ||
        resp.status == ManagementStatus::DeniedByPolicy ||
        resp.status == ManagementStatus::NotPaired ||
        resp.status == ManagementStatus::CapacityLimitReached ||
        resp.status == ManagementStatus::TopologyNotStaged ||
        resp.status == ManagementStatus::TopologyVersionStale ||
        resp.status == ManagementStatus::TopologyApplyFailed ||
        resp.status == ManagementStatus::SourceNotActiveMaster ||
        resp.status == ManagementStatus::UnsupportedCommand ||
        resp.status == ManagementStatus::BadPayload) {
      if (logEnabled(CliLogLevel::Error)) {
        writef("[MASTER][MGMT][RESP] req=%lu cmd=0x%04X status=0x%04X%s",
               static_cast<unsigned long>(resp.req_id),
               static_cast<unsigned int>(resp.cmd_id),
               static_cast<unsigned int>(resp.status),
               peer_ctx.c_str());
      }
    } else if (logEnabled(CliLogLevel::Debug)) {
      writef("[MASTER][MGMT][RESP] req=%lu cmd=0x%04X status=0x%04X%s",
             static_cast<unsigned long>(resp.req_id),
             static_cast<unsigned int>(resp.cmd_id),
             static_cast<unsigned int>(resp.status),
             peer_ctx.c_str());
    }
  }

  ManagementEvent evt{};
  while (management_transport_->pollEvent(evt)) {
    if (evt.event_id == ManagementEventId::PeerLivenessTransition) {
      ManagementPeerLivenessTransitionPayload transition{};
      if (management_utils::parsePeerLivenessTransitionPayload(evt.payload, transition)) {
        const char* state_label = (transition.state == 0U) ? "ONLINE" : "OFFLINE";
        writef("[MASTER][LIVE] peer=%s %s",
               macToPrintable(transition.peer).c_str(),
               state_label);
      }
      continue;
    }

    if (evt.event_id == ManagementEventId::TopologyTriggerReceived ||
        evt.event_id == ManagementEventId::TopologyTriggerRejected ||
        evt.event_id == ManagementEventId::TopologyTriggerAck) {
      ManagementTopologyTriggerEventPayload trigger_evt{};
      if (management_utils::parseTopologyTriggerEventPayload(evt.payload, trigger_evt)) {
        if (evt.event_id == ManagementEventId::TopologyTriggerAck) {
          writef("[MASTER][TOPO][ACK] peer=%s ack_seq=%u result=%u reason=%u",
                 macToPrintable(trigger_evt.peer).c_str(),
                 static_cast<unsigned int>(trigger_evt.ack_seq),
                 static_cast<unsigned int>(trigger_evt.result),
                 static_cast<unsigned int>(trigger_evt.reason));
        } else if (evt.event_id == ManagementEventId::TopologyTriggerRejected) {
          writef("[MASTER][TOPO][RX] peer=%s seq=%u REJECT reason=%u",
                 macToPrintable(trigger_evt.peer).c_str(),
                 static_cast<unsigned int>(trigger_evt.seq),
                 static_cast<unsigned int>(trigger_evt.reason));
        } else {
          const char* state = (trigger_evt.state == 2U) ? "DUP" : "OK";
          writef("[MASTER][TOPO][RX] peer=%s seq=%u %s dir=%u delay=%u hold=%u",
                 macToPrintable(trigger_evt.peer).c_str(),
                 static_cast<unsigned int>(trigger_evt.seq),
                 state,
                 static_cast<unsigned int>(trigger_evt.direction),
                 static_cast<unsigned int>(trigger_evt.delay_ms),
                 static_cast<unsigned int>(trigger_evt.hold_ms));
        }
      }
      continue;
    }

    if (logEnabled(CliLogLevel::Debug)) {
      const std::string peer_ctx = eventPeerContext(evt);
      writef("[MASTER][MGMT][EVT] id=0x%04X cmd=0x%04X req=%lu status=0x%04X%s",
             static_cast<unsigned int>(evt.event_id),
             static_cast<unsigned int>(evt.cmd_id),
             static_cast<unsigned long>(evt.req_id),
             static_cast<unsigned int>(evt.status),
             peer_ctx.c_str());
    }

    if (evt.event_id == ManagementEventId::CmdFail &&
        evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaPushStart) &&
        ota_push_active_ &&
        evt.req_id == ota_push_corr_id_) {
      stopOtaPush("management ota.push failed", false);
      continue;
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaUpdateStart)) {
      if (ota_update_req_id_ == 0U || evt.req_id != ota_update_req_id_) {
        continue;
      }
      if (evt.event_id == ManagementEventId::CmdDone) {
        io_.writeln("[MASTER][OTA] update pipeline complete");
        if (ota_update_has_target_peer_ && enqueueDescriptorQuery("DESC.GET", &ota_update_target_peer_)) {
          io_.writeln("[MASTER][OTA] auto verify queued: desc");
        }
        ota_update_req_id_ = 0U;
        ota_update_has_target_peer_ = false;
        ota_update_target_peer_ = {};
      } else if (evt.event_id == ManagementEventId::CmdFail) {
        writef("[MASTER][OTA] update pipeline failed (status=%s)",
               management_utils::managementStatusToString(evt.status));
        ota_update_req_id_ = 0U;
        ota_update_has_target_peer_ = false;
        ota_update_target_peer_ = {};
      }
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyCommit) &&
        (evt.event_id == ManagementEventId::CmdDone || evt.event_id == ManagementEventId::CmdFail)) {
      ManagementTopologyDeploySummaryPayload deploy{};
      if (management_utils::parseTopologyDeploySummaryPayload(evt.payload, deploy)) {
        writef("[MASTER][TOPO] deploy summary queued=%lu failed=%lu",
               static_cast<unsigned long>(deploy.queued_peers),
               static_cast<unsigned long>(deploy.failed_peers));
      }
      continue;
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::ChannelSyncAll) &&
        (evt.event_id == ManagementEventId::CmdDone || evt.event_id == ManagementEventId::CmdFail)) {
      ManagementChannelSyncAllResultPayload result{};
      if (management_utils::parseChannelSyncAllResultPayload(evt.payload, result)) {
        writef("[MASTER][CHANNEL] sync %s target=%u acked=%u/%u",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed",
               static_cast<unsigned int>(result.channel),
               static_cast<unsigned int>(result.acked_peers),
               static_cast<unsigned int>(result.total_peers));
      } else {
        writef("[MASTER][CHANNEL] sync %s",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed");
      }
      continue;
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::ChainLoopControlSet) &&
        (evt.event_id == ManagementEventId::CmdDone || evt.event_id == ManagementEventId::CmdFail)) {
      ManagementChainLoopResultPayload result{};
      if (management_utils::parseChainLoopResultPayload(evt.payload, result)) {
        writef("[MASTER][CHAIN] loop_auto %s state=%s acked=%u/%u",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed",
               result.enabled ? "on" : "off",
               static_cast<unsigned int>(result.acked_peers),
               static_cast<unsigned int>(result.total_peers));
      } else {
        writef("[MASTER][CHAIN] loop_auto %s",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed");
      }
      continue;
    }

    if (evt.event_id == ManagementEventId::OtaTransferReady ||
        evt.event_id == ManagementEventId::OtaBootComplete) {
      if (evt.payload.size() >= 17U) {
        MandatoryEventItem item{};
        std::memcpy(item.peer.data(), evt.payload.data(), 6U);
        (void)readU16Le(evt.payload.data() + 6U, evt.payload.size() - 6U, item.event_id);
        item.severity = evt.payload[8U];
        uint32_t event_value_u32 = 0U;
        uint32_t event_ts_u32 = 0U;
        (void)readU32Le(evt.payload.data() + 9U, evt.payload.size() - 9U, event_value_u32);
        (void)readU32Le(evt.payload.data() + 13U, evt.payload.size() - 13U, event_ts_u32);
        item.event_value = static_cast<int32_t>(event_value_u32);
        item.event_ts_s = event_ts_u32;
        item.corr_id = evt.req_id;
        item.rx_ms = nowMs();
        mandatory_events_.push_back(item);
        if (mandatory_events_.size() > 32U) {
          mandatory_events_.erase(mandatory_events_.begin());
        }
      }

      if (evt.event_id == ManagementEventId::OtaBootComplete) {
        io_.writeln("[MASTER][OTA] slave reports update completed after reboot");
        const MacAddress* verify_peer = ota_push_has_target_peer_
                                            ? &ota_push_target_peer_
                                            : (ota_update_has_target_peer_ ? &ota_update_target_peer_ : nullptr);
        if (verify_peer != nullptr && enqueueDescriptorQuery("DESC.GET", verify_peer)) {
          io_.writeln("[MASTER][OTA] auto verify queued: desc");
        }
        if (ota_update_wait_boot_notice_) {
          ota_update_prepare_pending_ = false;
          ota_update_prepare_corr_id_ = 0U;
          ota_update_staged_path_.clear();
          ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
          ota_update_wait_boot_notice_ = false;
          ota_update_pipeline_active_ = false;
          ota_update_image_name_.clear();
          ota_update_req_id_ = 0U;
          ota_update_has_target_peer_ = false;
          ota_update_target_peer_ = {};
          io_.writeln("[MASTER][OTA] update pipeline complete");
        }
      } else if (evt.event_id == ManagementEventId::OtaTransferReady) {
        uint32_t transfer_corr = 0U;
        if (evt.payload.size() >= 13U) {
          (void)readU32Le(evt.payload.data() + 9U, evt.payload.size() - 9U, transfer_corr);
        }
        if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
          writef("[MASTER][OTA] slave finalize event received corr=%lu",
                 static_cast<unsigned long>(transfer_corr));
        }
        if (ota_push_active_ &&
            transfer_corr == ota_push_corr_id_ &&
            ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
          if (ota_update_image_name_.empty()) {
            ota_update_image_name_ = "u.bin";
          }
          stopOtaPush("complete", true);
        }
      }
      continue;
    }

    if (evt.event_id == ManagementEventId::OtaTransferStatus) {
      constexpr uint8_t kOtaStatusKindChunkAck = 0x01;
      constexpr uint8_t kOtaStatusKindChunkNack = 0x02;
      constexpr uint8_t kOtaStatusKindFinalizeOk = 0x03;
      constexpr uint8_t kOtaStatusKindFinalizeFail = 0x04;
      if (evt.payload.size() < 13U) {
        continue;
      }
      const uint32_t transfer_corr = evt.req_id;
      const uint8_t kind = evt.payload[6U];
      uint32_t offset = 0U;
      uint16_t status_code = 0U;
      (void)readU32Le(evt.payload.data() + 7U, evt.payload.size() - 7U, offset);
      (void)readU16Le(evt.payload.data() + 11U, evt.payload.size() - 11U, status_code);
      if (ota_push_active_ && transfer_corr == ota_push_corr_id_) {
        ota_push_last_activity_ms_ = nowMs();
        if (kind == kOtaStatusKindChunkAck) {
          ota_push_offset_ = std::min<uint32_t>(offset, ota_push_size_bytes_);
          if (!ota_push_begin_ack_seen_ && offset == 0U) {
            ota_push_begin_ack_seen_ = true;
            if (ota_push_phase_ == OtaPushPhase::WaitBeginStatus) {
              ota_push_phase_ = OtaPushPhase::Streaming;
              io_.writeln("[MASTER][OTA] begin acknowledged by slave status; streaming chunks...");
            }
          }
          if (ota_push_size_bytes_ > 0U && offset >= ota_push_size_bytes_) {
            ota_push_phase_ = OtaPushPhase::WaitEndStatus;
          }
        } else if (kind == kOtaStatusKindChunkNack) {
          ota_push_offset_ = std::min<uint32_t>(offset, ota_push_size_bytes_);
          if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
            writef("[MASTER][OTA] nack received offset=%lu code=0x%04X",
                   static_cast<unsigned long>(ota_push_offset_),
                   static_cast<unsigned int>(status_code));
          }
        } else if (kind == kOtaStatusKindFinalizeOk) {
          if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
            stopOtaPush("complete", true);
          }
        } else if (kind == kOtaStatusKindFinalizeFail) {
          if (cli_enabled_ && logEnabled(CliLogLevel::Error)) {
            writef("[MASTER][OTA] finalize fail code=0x%04X offset=%lu",
                   static_cast<unsigned int>(status_code),
                   static_cast<unsigned long>(offset));
          }
          if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
            stopOtaPush("slave finalize failed", false);
          }
        }
      }
      continue;
    }
  }
}

void MasterCli::printQueueStatus() const {
  writef("[MASTER][CLI] queue depth=%u max=%u sent=%lu fail=%lu drop=%lu",
         static_cast<unsigned int>(descriptor_request_queue_.size()),
         static_cast<unsigned int>(descriptor_queue_max_),
         static_cast<unsigned long>(descriptor_queue_sent_count_),
         static_cast<unsigned long>(descriptor_queue_fail_count_),
         static_cast<unsigned long>(descriptor_queue_drop_count_));
}

void MasterCli::setAutoPull(bool enabled, uint32_t interval_ms) {
  auto_pull_enabled_ = enabled;
  if (!enabled) {
    auto_pull_has_target_peer_ = false;
    auto_pull_target_peer_ = {};
  }
  if (interval_ms >= 300U) {
    auto_pull_interval_ms_ = interval_ms;
  }
  auto_pull_.setEnabled(enabled, nowMs(), auto_pull_interval_ms_);
}

void MasterCli::handleLine(const std::string& raw) {
  (void)handleLineEx(raw);
}

MasterCli::CommandDispatchResult MasterCli::handleLineEx(const std::string& raw) {
  CommandDispatchResult result{};
  resetDispatchSnapshot_();
  command_target_override_active_ = false;
  command_target_override_peer_ = {};
  const std::string line = trim(raw);
  if (line.empty()) {
    return result;
  }
  result.parsed = true;

  auto clearTargetOverride = [&]() {
    command_target_override_active_ = false;
    command_target_override_peer_ = {};
  };

  std::string command_line = line;
  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.size() >= 2U) {
    const std::string selector = trim(tokens[0]);
    bool selector_used = false;
    bool selector_valid = false;
    MacAddress selected_peer{};

    bool digits_only = !selector.empty();
    for (char c : selector) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        digits_only = false;
        break;
      }
    }
    if (digits_only) {
      selector_used = true;
      const unsigned long idx = std::strtoul(selector.c_str(), nullptr, 10);
      std::vector<MacAddress> persisted{};
      manager_.getPersistedPeers(persisted);
      if (idx < persisted.size()) {
        selected_peer = persisted[static_cast<size_t>(idx)];
        selector_valid = true;
      }
    } else {
      MacAddress mac{};
      if (parseMac(selector, mac)) {
        selector_used = true;
        if (manager_.hasPersistedPair(mac)) {
          selected_peer = mac;
          selector_valid = true;
        }
      }
    }

    if (selector_used) {
      if (!selector_valid) {
        io_.writeln("[MASTER][CLI] invalid target selector (use <paired_index> or paired MAC)");
        result.handled = true;
        result.accepted = false;
        result.status = ManagementStatus::BadPayload;
        result.reject_stage = "target";
        clearTargetOverride();
        return result;
      }
      const size_t first_sep = line.find_first_of(" \t");
      if (first_sep == std::string::npos) {
        io_.writeln("[MASTER][CLI] missing command after target selector");
        result.handled = true;
        result.accepted = false;
        result.status = ManagementStatus::BadPayload;
        result.reject_stage = "target";
        clearTargetOverride();
        return result;
      }
      command_line = trim(line.substr(first_sep + 1U));
      if (command_line.empty()) {
        io_.writeln("[MASTER][CLI] missing command after target selector");
        result.handled = true;
        result.accepted = false;
        result.status = ManagementStatus::BadPayload;
        result.reject_stage = "target";
        clearTargetOverride();
        return result;
      }
      command_target_override_active_ = true;
      command_target_override_peer_ = selected_peer;
    }
  }

  const std::string lower = lowerCopy(command_line);

  auto isLocalOnlyHandledCommand = [&](const std::string& lower_cmd) -> bool {
    if (lower_cmd == "help" ||
        startsWith(lower_cmd, "help ") ||
        (lower_cmd.size() > 5U && lower_cmd.compare(lower_cmd.size() - 5U, 5U, " help") == 0) ||
        lower_cmd == "log" ||
        lower_cmd == "log error" ||
        lower_cmd == "log info" ||
        lower_cmd == "log debug" ||
        lower_cmd == "queue" ||
        lower_cmd == "cli status" ||
        lower_cmd == "cli on" ||
        lower_cmd == "cli off" ||
        lower_cmd == "metrics" ||
        lower_cmd == "metrics.reset" ||
        lower_cmd == "paired" ||
        lower_cmd == "paired.list" ||
        lower_cmd == "status" ||
        lower_cmd == "time.local" ||
        startsWith(lower_cmd, "autopull ") ||
        lower_cmd == "event.list" ||
        lower_cmd == "event.clear" ||
        lower_cmd == "radio.drytest" ||
        lower_cmd == "comm.test.status" ||
        lower_cmd == "comm.test.report") {
      return true;
    }
    return false;
  };

  auto applyDispatchSnapshot = [&](const std::string& lower_cmd) {
    if (dispatch_snapshot_.seen) {
      result.submitted = true;
      result.accepted = dispatch_snapshot_.accepted;
      result.cmd_id = dispatch_snapshot_.cmd_id;
      result.req_id = dispatch_snapshot_.req_id;
      result.status = dispatch_snapshot_.status;
      if (dispatch_snapshot_.reject_stage != nullptr && dispatch_snapshot_.reject_stage[0] != '\0') {
        result.reject_stage = dispatch_snapshot_.reject_stage;
      } else {
        result.reject_stage.clear();
      }
      return;
    }
    result.submitted = false;
    if (!result.handled || result.status != ManagementStatus::InternalError) {
      return;
    }
    if (isLocalOnlyHandledCommand(lower_cmd)) {
      result.accepted = true;
      result.status = ManagementStatus::Ok;
      result.reject_stage.clear();
    } else {
      result.accepted = false;
      result.status = ManagementStatus::BadPayload;
      result.reject_stage = "validation";
    }
  };

  if (handleCliMetaCommands(lower)) {
    result.handled = true;
    applyDispatchSnapshot(lower);
    clearTargetOverride();
    return result;
  }

  if (!cli_enabled_) {
    result.handled = true;
    result.accepted = false;
    result.status = ManagementStatus::DeniedByPolicy;
    result.reject_stage = "cli_disabled";
    clearTargetOverride();
    return result;
  }

  using Handler = bool (*)(MasterCli*, const std::string&, const std::string&);
  static constexpr Handler kHandlers[] = {
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleListAndStatusCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handlePairingCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleTopologyCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleDescriptorShortCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleTimeCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleAutopullCommand(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handlePushCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleEventCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleSettingsCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleStorageCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleOtaCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleLoggerCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleGetIdCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleSetIdCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleGetCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleSetCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleTestAndLocalCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleRestartResetCommands(lower_cmd);
      },
  };

  for (auto fn : kHandlers) {
    if (fn(this, command_line, lower)) {
      result.handled = true;
      applyDispatchSnapshot(lower);
      clearTargetOverride();
      return result;
    }
  }

  io_.writeln("[MASTER][CLI] unknown command (type: help)");
  result.handled = false;
  result.accepted = false;
  result.status = ManagementStatus::UnsupportedCommand;
  result.reject_stage = "parse";
  clearTargetOverride();
  return result;
}

void MasterCli::tick(uint32_t now_ms) {
  pumpManagementMailbox();
  if (!cli_enabled_) {
    return;
  }
  pumpDescriptorQueue(now_ms);

  if (probe_pending_kind_ != ProbePendingKind::None) {
    constexpr uint32_t kProbeTimeoutMs = 3500U;
    if (static_cast<int32_t>(now_ms - probe_sent_ms_) >=
        static_cast<int32_t>(kProbeTimeoutMs)) {
      const ProbePendingKind pending = probe_pending_kind_;
      probe_pending_kind_ = ProbePendingKind::None;
      probe_sent_ms_ = 0;
      if (pending == ProbePendingKind::Ping) {
        io_.writeln("[MASTER][PING] timeout; no response from slave");
      } else {
        io_.writeln("[MASTER][LIVE] request timeout; no response from slave");
      }
    }
  }

  if (remote_log_pull_active_ &&
      (!remote_log_pull_has_target_peer_ || !manager_.hasPersistedPair(remote_log_pull_target_peer_))) {
    stopRemoteLogPull("target unavailable during pull", false);
  } else if (remote_log_pull_active_) {
    constexpr uint32_t kRemotePullIdleTimeoutMs = 5000U;
    if (static_cast<int32_t>(now_ms - remote_log_pull_last_activity_ms_) >=
        static_cast<int32_t>(kRemotePullIdleTimeoutMs)) {
      stopRemoteLogPull(remote_log_pull_waiting_status_
                            ? "timeout waiting remote logger status"
                            : "timeout waiting remote logger chunk",
                        false);
    }
  }

  pumpOtaPush(now_ms);

  const MacAddress poll_peer = auto_pull_target_peer_;
  const bool can_poll = auto_pull_has_target_peer_;
  MasterAutoPullTickResult r = auto_pull_.tick(&pull_,
                                                poll_peer,
                                                auto_pull_enabled_ && can_poll,
                                                now_ms,
                                                correlation_id_);
  if (r.offline_timeout) {
    io_.writeln("[MASTER][LIVE] timeout waiting for liveness response; slave offline");
  }
  if (r.offline_stale) {
    io_.writeln("[MASTER][LIVE] stale liveness; slave offline");
  }

  if (list_window_active_ && static_cast<int32_t>(now_ms - list_window_deadline_ms_) >= 0) {
    list_window_active_ = false;
    collect_discovery_ = false;
    io_.writeln("[MASTER][CLI] discovery window finished");
    printDiscovered();
  }
}


}  // namespace espnow_link

