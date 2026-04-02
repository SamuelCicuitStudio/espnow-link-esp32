/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Topology command and verification flow dispatch logic.
 **************************************************************/
#include "../internal/cli_dispatch_internal.hpp"
#include "../internal/cli_dispatch_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

bool MasterCli::handleTopologyCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "topology.")) {
    return false;
  }
  const bool is_topology_chain_cmd = startsWith(lower, "topology.chain.");
  const bool is_topology_edit_cmd =
      startsWith(lower, "topology.edit.") || startsWith(lower, "topology.file.show") || is_topology_chain_cmd;
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

  auto printTopologyChainHelp = [&]() {
    io_.writeln("[MASTER][TOPO][CHAIN] fixed path: /o/s/tp.json");
    io_.writeln("[MASTER][TOPO][CHAIN] commands:");
    io_.writeln("  topology.chain.show");
    io_.writeln("  topology.chain.graph");
    io_.writeln("  topology.chain.clear");
    io_.writeln("  topology.chain.add <S|R|SM|RM> <paired_index|MAC> [vi]");
    io_.writeln("  topology.chain.edit <index> <S|R|SM|RM> <paired_index|MAC> [vi]");
    io_.writeln("  topology.chain.del <index>");
    io_.writeln("  topology.chain.move <from_index> <to_index>");
    io_.writeln("  topology.chain.validate");
    io_.writeln("  topology.chain.fix");
    io_.writeln("  topology.chain.apply");
    io_.writeln("  topology.chain.verify [timeout_ms]");
    io_.writeln("  topology.chain.backup");
    io_.writeln("  topology.chain.restore");
    io_.writeln("  topology.chain.set <chain_spec>");
    io_.writeln("  topology.chain.set.help");
    io_.writeln("[MASTER][TOPO][CHAIN] chain_spec: <TYPE>@<PEER>[#<CH>] joined by '>'");
    io_.writeln("[MASTER][TOPO][CHAIN] TYPE=S|R|SM|RM, CH only for SM(0..7)/RM(0..15)");
  };

  static constexpr const char* kFixedChainPath = "/o/s/tp.json";

  auto requireNoChainArgs = [&](const char* usage) -> bool {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() == 1U) {
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] usage: %s", usage);
    return false;
  };

  auto chainRoleLabelLong = [](ChainNodeType type) -> const char* {
    switch (type) {
      case ChainNodeType::Sensor:
        return "SENSOR";
      case ChainNodeType::Relay:
        return "RELAY";
      case ChainNodeType::SemuChild:
        return "SEMU";
      case ChainNodeType::RemuChild:
        return "REMU";
    }
    return "?";
  };

  auto formatChainNodeLong = [&](const ChainNode& node) -> std::string {
    char idx_buf[8];
    std::snprintf(idx_buf, sizeof(idx_buf), "%02u", static_cast<unsigned int>(node.chain_index + 1U));
    std::string text = std::string(idx_buf) + " " + chainRoleLabelLong(node.type);
    if (node.type == ChainNodeType::SemuChild || node.type == ChainNodeType::RemuChild) {
      text += " ch=" + std::to_string(static_cast<int>(node.virtual_index));
    }
    text += " mac=" + macToPrintable(node.mac);
    return text;
  };

  auto loadFixedChainForRead = [&](std::string& out_json,
                                   TopologyChainPlanDebug& out_debug,
                                   uint32_t& out_topology_version,
                                   size_t& out_targets,
                                   std::string& out_error) -> bool {
    out_json.clear();
    out_debug = TopologyChainPlanDebug{};
    out_topology_version = 1U;
    out_targets = 0U;
    out_error.clear();

    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO][CHAIN] local storage backend unavailable");
      out_error = "storage_unavailable";
      return false;
    }

    std::string read_msg;
    if (!readTextFileLocal(*ota_push_storage_, kFixedChainPath, out_json, read_msg)) {
      writef("[MASTER][TOPO][CHAIN] read failed path=%s reason=%s", kFixedChainPath, read_msg.c_str());
      out_error = read_msg;
      return false;
    }

    std::vector<TopologyDeployTarget> targets{};
    if (!parseTopologyChainJson(out_json,
                                out_topology_version,
                                targets,
                                out_topology_version,
                                out_error,
                                &out_debug)) {
      return false;
    }
    out_targets = targets.size();
    return true;
  };

  auto loadEditorLooseFromJson = [&](const std::string& json_text, std::string& out_error) -> bool {
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
    (void)parseJsonU32Array(json_text, "seed", seeds);

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

      if (node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) {
        node.vi = -1;
      } else if (node.type == ChainNodeType::SemuChild) {
        if (vi < 0) vi = 0;
        if (vi > 7) vi = 7;
        node.vi = vi;
      } else {
        if (vi < 0) vi = 0;
        if (vi > 15) vi = 15;
        node.vi = vi;
      }
      chain.push_back(node);
    }

    topology_edit.topology_version = topology_version;
    topology_edit.seeds = seeds;
    topology_edit.chain = chain;
    return true;
  };

  auto loadEditorFromFixedFile = [&](bool allow_loose, std::string& out_error) -> bool {
    out_error.clear();
    if (ota_push_storage_ == nullptr) {
      out_error = "storage_unavailable";
      return false;
    }
    std::string json_text;
    std::string read_msg;
    if (!readTextFileLocal(*ota_push_storage_, kFixedChainPath, json_text, read_msg)) {
      out_error = read_msg.empty() ? "read failed" : read_msg;
      return false;
    }
    if (loadEditorFromJson(json_text, out_error)) {
      return true;
    }
    if (allow_loose) {
      std::string loose_error;
      if (loadEditorLooseFromJson(json_text, loose_error)) {
        return true;
      }
    }
    return false;
  };

  auto persistEditorRawToFixed = [&](size_t& out_relay_groups, std::string& out_error) -> bool {
    out_relay_groups = countRelayBlocks(topology_edit.chain);
    std::vector<uint32_t> resolved_seeds{};
    bool auto_seeded = false;
    resolveEditorSeeds(out_relay_groups, resolved_seeds, auto_seeded);
    (void)auto_seeded;
    topology_edit.seeds = resolved_seeds;
    const std::string json_text = buildEditorJson(resolved_seeds);
    if (ota_push_storage_ == nullptr) {
      out_error = "storage_unavailable";
      return false;
    }
    std::string write_msg;
    if (!writeTextFileLocal(*ota_push_storage_, kFixedChainPath, json_text, write_msg)) {
      out_error = write_msg.empty() ? "write failed" : write_msg;
      return false;
    }
    out_error.clear();
    return true;
  };

  auto persistEditorValidatedToFixed = [&](size_t& out_relay_groups,
                                           size_t& out_targets,
                                           std::string& out_error) -> bool {
    out_relay_groups = 0U;
    out_targets = 0U;
    std::string json_text;
    std::vector<uint32_t> resolved_seeds{};
    uint32_t validated_topology_version = topology_edit.topology_version;
    if (!buildValidatedEditorJson(json_text,
                                  resolved_seeds,
                                  validated_topology_version,
                                  out_relay_groups,
                                  out_targets,
                                  out_error)) {
      return false;
    }
    topology_edit.seeds = resolved_seeds;
    topology_edit.topology_version = validated_topology_version;
    if (ota_push_storage_ == nullptr) {
      out_error = "storage_unavailable";
      return false;
    }
    std::string write_msg;
    if (!writeTextFileLocal(*ota_push_storage_, kFixedChainPath, json_text, write_msg)) {
      out_error = write_msg.empty() ? "write failed" : write_msg;
      return false;
    }
    out_error.clear();
    return true;
  };

  auto parseChainIndexToken = [&](const std::string& token, size_t chain_size, size_t& out_pos) -> bool {
    out_pos = 0U;
    if (token.empty() || chain_size == 0U) {
      return false;
    }
    char* end = nullptr;
    const long idx = std::strtol(token.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
      return false;
    }
    if (idx >= 1L && static_cast<size_t>(idx) <= chain_size) {
      out_pos = static_cast<size_t>(idx - 1L);
      return true;
    }
    if (idx >= 0L && static_cast<size_t>(idx) < chain_size) {
      out_pos = static_cast<size_t>(idx);
      return true;
    }
    return false;
  };

  auto parseChainSpec = [&](const std::string& chain_spec,
                            std::vector<TopologyEditNodeState>& out_chain,
                            std::string& out_error) -> bool {
    out_chain.clear();
    out_error.clear();
    const std::string spec = trim(chain_spec);
    if (spec.empty()) {
      out_error = "chain_spec is empty";
      return false;
    }

    size_t cursor = 0U;
    size_t node_pos = 0U;
    while (cursor <= spec.size()) {
      const size_t sep = spec.find('>', cursor);
      const std::string raw_node =
          (sep == std::string::npos) ? spec.substr(cursor) : spec.substr(cursor, sep - cursor);
      const std::string node_text = trim(raw_node);
      if (node_text.empty()) {
        out_error = "node " + std::to_string(node_pos + 1U) + ": empty segment";
        return false;
      }

      const size_t at = node_text.find('@');
      if (at == std::string::npos || at == 0U || at + 1U >= node_text.size()) {
        out_error = "node " + std::to_string(node_pos + 1U) + ": expected <TYPE>@<PEER>[#<CH>]";
        return false;
      }
      const std::string type_token = trim(node_text.substr(0U, at));
      std::string peer_and_channel = trim(node_text.substr(at + 1U));
      if (peer_and_channel.empty()) {
        out_error = "node " + std::to_string(node_pos + 1U) + ": missing peer";
        return false;
      }

      TopologyEditNodeState node{};
      if (!parseChainNodeType(type_token, node.type)) {
        out_error = "node " + std::to_string(node_pos + 1U) + ": invalid type";
        return false;
      }

      std::string peer_token = peer_and_channel;
      bool has_channel = false;
      int32_t parsed_channel = -1;
      const size_t hash = peer_and_channel.find('#');
      if (hash != std::string::npos) {
        peer_token = trim(peer_and_channel.substr(0U, hash));
        const std::string ch_token = trim(peer_and_channel.substr(hash + 1U));
        has_channel = true;
        if (ch_token.empty() || !parseI32Token(ch_token, parsed_channel)) {
          out_error = "node " + std::to_string(node_pos + 1U) + ": invalid channel";
          return false;
        }
      }

      if (!resolveTopologyPeerToken(peer_token, node.mac)) {
        out_error = "node " + std::to_string(node_pos + 1U) + ": peer is not a valid MAC or paired index";
        return false;
      }

      if (node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) {
        if (has_channel) {
          out_error = "node " + std::to_string(node_pos + 1U) + ": channel is only allowed for SM/RM";
          return false;
        }
        node.vi = -1;
      } else if (node.type == ChainNodeType::SemuChild) {
        if (!has_channel) {
          out_error = "node " + std::to_string(node_pos + 1U) + ": SM requires #<CH>";
          return false;
        }
        if (parsed_channel < 0 || parsed_channel > 7) {
          out_error = "node " + std::to_string(node_pos + 1U) + ": SM channel must be 0..7";
          return false;
        }
        node.vi = parsed_channel;
      } else {
        if (!has_channel) {
          out_error = "node " + std::to_string(node_pos + 1U) + ": RM requires #<CH>";
          return false;
        }
        if (parsed_channel < 0 || parsed_channel > 15) {
          out_error = "node " + std::to_string(node_pos + 1U) + ": RM channel must be 0..15";
          return false;
        }
        node.vi = parsed_channel;
      }

      out_chain.push_back(node);
      ++node_pos;
      if (sep == std::string::npos) {
        break;
      }
      cursor = sep + 1U;
    }
    return !out_chain.empty();
  };

  if (cmd_lower == "topology.chain.help" || startsWith(cmd_lower, "topology.chain.help ") ||
      cmd_lower == "topology.chain.set.help" || startsWith(cmd_lower, "topology.chain.set.help ")) {
    printTopologyChainHelp();
    return true;
  }

  if (cmd_lower == "topology.chain.show" || startsWith(cmd_lower, "topology.chain.show ")) {
    if (!requireNoChainArgs("topology.chain.show")) {
      return true;
    }
    std::string json_text;
    TopologyChainPlanDebug debug{};
    uint32_t topology_version = 1U;
    size_t target_count = 0U;
    std::string err;
    if (!loadFixedChainForRead(json_text, debug, topology_version, target_count, err)) {
      if (!err.empty()) {
        writef("[MASTER][TOPO][CHAIN] invalid path=%s reason=%s", kFixedChainPath, err.c_str());
      }
      return true;
    }

    writef("[MASTER][TOPO][CHAIN] path=%s topo_ver=%lu nodes=%u relay_groups=%u seeds=%u targets=%u",
           kFixedChainPath,
           static_cast<unsigned long>(topology_version),
           static_cast<unsigned int>(debug.nodes.size()),
           static_cast<unsigned int>(debug.groups.size()),
           static_cast<unsigned int>(debug.seed_u32.size()),
           static_cast<unsigned int>(target_count));

    if (!debug.seed_u32.empty()) {
      std::string seed_csv;
      for (size_t i = 0; i < debug.seed_u32.size(); ++i) {
        if (i > 0U) seed_csv += ",";
        seed_csv += std::to_string(debug.seed_u32[i]);
      }
      writef("[MASTER][TOPO][CHAIN] seed_csv=%s", seed_csv.c_str());
    }

    for (const auto& node : debug.nodes) {
      writef("  %s", formatChainNodeLong(node).c_str());
    }
    return true;
  }

  if (cmd_lower == "topology.chain.validate" || startsWith(cmd_lower, "topology.chain.validate ")) {
    if (!requireNoChainArgs("topology.chain.validate")) {
      return true;
    }
    std::string json_text;
    TopologyChainPlanDebug debug{};
    uint32_t topology_version = 1U;
    size_t target_count = 0U;
    std::string err;
    if (!loadFixedChainForRead(json_text, debug, topology_version, target_count, err)) {
      if (!err.empty()) {
        writef("[MASTER][TOPO][CHAIN] validate=FAIL path=%s reason=%s", kFixedChainPath, err.c_str());
      }
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] validate=OK path=%s topo_ver=%lu nodes=%u relay_groups=%u seeds=%u targets=%u",
           kFixedChainPath,
           static_cast<unsigned long>(topology_version),
           static_cast<unsigned int>(debug.nodes.size()),
           static_cast<unsigned int>(debug.groups.size()),
           static_cast<unsigned int>(debug.seed_u32.size()),
           static_cast<unsigned int>(target_count));
    return true;
  }

  if (cmd_lower == "topology.chain.graph" || startsWith(cmd_lower, "topology.chain.graph ")) {
    if (!requireNoChainArgs("topology.chain.graph")) {
      return true;
    }
    std::string json_text;
    TopologyChainPlanDebug debug{};
    uint32_t topology_version = 1U;
    size_t target_count = 0U;
    std::string err;
    if (!loadFixedChainForRead(json_text, debug, topology_version, target_count, err)) {
      (void)topology_version;
      (void)target_count;
      if (!err.empty()) {
        writef("[MASTER][TOPO][GRAPH] invalid path=%s reason=%s", kFixedChainPath, err.c_str());
      }
      return true;
    }
    (void)topology_version;
    (void)target_count;

    struct GraphSegment {
      size_t separator = 0U;
      std::vector<size_t> relay_nodes{};
    };
    std::vector<GraphSegment> segments{};
    for (size_t i = 0U; i < debug.nodes.size();) {
      GraphSegment seg{};
      seg.separator = i;
      ++i;
      while (i < debug.nodes.size() && isChainRelay(debug.nodes[i].type)) {
        seg.relay_nodes.push_back(i);
        ++i;
      }
      segments.push_back(seg);
    }

    auto boxTop = [](size_t inner_width) -> std::string {
      return "+" + std::string(inner_width, '-') + "+";
    };
    auto boxBody = [](const std::string& text, size_t inner_width) -> std::string {
      std::string clipped = text;
      if (clipped.size() > inner_width) {
        if (inner_width > 3U) {
          clipped = clipped.substr(0U, inner_width - 3U) + "...";
        } else {
          clipped = clipped.substr(0U, inner_width);
        }
      }
      const size_t total_pad = inner_width - clipped.size();
      const size_t left_pad = total_pad / 2U;
      const size_t right_pad = total_pad - left_pad;
      return "|" + std::string(left_pad, ' ') + clipped + std::string(right_pad, ' ') + "|";
    };

    constexpr size_t kSeparatorBoxInner = 56U;
    constexpr size_t kRelayBoxInner = 38U;
    writef("[MASTER][TOPO][GRAPH] view=vertical.blocks path=%s nodes=%u",
           kFixedChainPath,
           static_cast<unsigned int>(debug.nodes.size()));
    io_.writeln("");

    for (size_t s = 0U; s < segments.size(); ++s) {
      const bool is_last = (s + 1U == segments.size());
      const std::string branch = is_last ? "`-- " : "|-- ";
      const std::string child_prefix = is_last ? "    " : "|   ";

      const ChainNode& separator = debug.nodes[segments[s].separator];
      io_.writeln(branch + boxTop(kSeparatorBoxInner));
      io_.writeln(child_prefix + boxBody(formatChainNodeLong(separator), kSeparatorBoxInner));
      io_.writeln(child_prefix + boxTop(kSeparatorBoxInner));

      for (size_t relay_idx : segments[s].relay_nodes) {
        const ChainNode& relay = debug.nodes[relay_idx];
        io_.writeln(child_prefix + boxTop(kRelayBoxInner));
        io_.writeln(child_prefix + boxBody(formatChainNodeLong(relay), kRelayBoxInner));
        io_.writeln(child_prefix + boxTop(kRelayBoxInner));
      }

      if (!is_last) {
        io_.writeln("|");
      }
    }
    return true;
  }

  if (cmd_lower == "topology.chain.clear" || startsWith(cmd_lower, "topology.chain.clear ")) {
    if (!requireNoChainArgs("topology.chain.clear")) {
      return true;
    }
    std::string load_error;
    (void)loadEditorFromFixedFile(true, load_error);
    topology_edit.chain.clear();
    topology_edit.seeds.clear();
    size_t relay_groups = 0U;
    std::string write_error;
    if (!persistEditorRawToFixed(relay_groups, write_error)) {
      writef("[MASTER][TOPO][CHAIN] clear failed path=%s reason=%s",
             kFixedChainPath,
             write_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] cleared path=%s topo_ver=%lu",
           kFixedChainPath,
           static_cast<unsigned long>(topology_edit.topology_version));
    return true;
  }

  if (cmd_lower == "topology.chain.add") {
    io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.add <S|R|SM|RM> <paired_index|MAC> [vi]");
    return true;
  }
  if (startsWith(cmd_lower, "topology.chain.add ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() < 3U || tok.size() > 4U) {
      io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.add <S|R|SM|RM> <paired_index|MAC> [vi]");
      return true;
    }
    std::string load_error;
    if (!loadEditorFromFixedFile(true, load_error)) {
      writef("[MASTER][TOPO][CHAIN] add failed path=%s reason=%s", kFixedChainPath, load_error.c_str());
      return true;
    }

    TopologyEditNodeState node{};
    if (!parseChainNodeType(tok[1], node.type)) {
      io_.writeln("[MASTER][TOPO][CHAIN] invalid type (use S|R|SM|RM)");
      return true;
    }
    if (!resolveTopologyPeerToken(tok[2], node.mac)) {
      io_.writeln("[MASTER][TOPO][CHAIN] peer must be paired index or MAC");
      return true;
    }
    node.vi = (node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) ? -1 : 0;
    if (tok.size() == 4U) {
      int32_t vi = 0;
      if (!parseI32Token(tok[3], vi)) {
        io_.writeln("[MASTER][TOPO][CHAIN] invalid vi");
        return true;
      }
      node.vi = vi;
    }
    if ((node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) && node.vi != -1) {
      io_.writeln("[MASTER][TOPO][CHAIN] physical node vi must be -1");
      return true;
    }
    if (node.type == ChainNodeType::SemuChild && (node.vi < 0 || node.vi > 7)) {
      io_.writeln("[MASTER][TOPO][CHAIN] semu vi out of range (0..7)");
      return true;
    }
    if (node.type == ChainNodeType::RemuChild && (node.vi < 0 || node.vi > 15)) {
      io_.writeln("[MASTER][TOPO][CHAIN] remu vi out of range (0..15)");
      return true;
    }

    topology_edit.chain.push_back(node);
    size_t relay_groups = 0U;
    std::string write_error;
    if (!persistEditorRawToFixed(relay_groups, write_error)) {
      writef("[MASTER][TOPO][CHAIN] add failed path=%s reason=%s", kFixedChainPath, write_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] add ok index=%u type=%s mac=%s vi=%d nodes=%u relay_groups=%u",
           static_cast<unsigned int>(topology_edit.chain.size()),
           chainTypeToken(node.type),
           macToPrintable(node.mac).c_str(),
           static_cast<int>(node.vi),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups));
    return true;
  }

  if (cmd_lower == "topology.chain.edit") {
    io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.edit <index> <S|R|SM|RM> <paired_index|MAC> [vi]");
    return true;
  }
  if (startsWith(cmd_lower, "topology.chain.edit ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() < 4U || tok.size() > 5U) {
      io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.edit <index> <S|R|SM|RM> <paired_index|MAC> [vi]");
      return true;
    }
    std::string load_error;
    if (!loadEditorFromFixedFile(true, load_error)) {
      writef("[MASTER][TOPO][CHAIN] edit failed path=%s reason=%s", kFixedChainPath, load_error.c_str());
      return true;
    }
    if (topology_edit.chain.empty()) {
      io_.writeln("[MASTER][TOPO][CHAIN] edit failed: chain is empty");
      return true;
    }
    size_t pos = 0U;
    if (!parseChainIndexToken(tok[1], topology_edit.chain.size(), pos)) {
      writef("[MASTER][TOPO][CHAIN] invalid index (allowed 1..%u or 0..%u)",
             static_cast<unsigned int>(topology_edit.chain.size()),
             static_cast<unsigned int>(topology_edit.chain.size() - 1U));
      return true;
    }

    TopologyEditNodeState node{};
    if (!parseChainNodeType(tok[2], node.type)) {
      io_.writeln("[MASTER][TOPO][CHAIN] invalid type (use S|R|SM|RM)");
      return true;
    }
    if (!resolveTopologyPeerToken(tok[3], node.mac)) {
      io_.writeln("[MASTER][TOPO][CHAIN] peer must be paired index or MAC");
      return true;
    }
    node.vi = (node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) ? -1 : 0;
    if (tok.size() == 5U) {
      int32_t vi = 0;
      if (!parseI32Token(tok[4], vi)) {
        io_.writeln("[MASTER][TOPO][CHAIN] invalid vi");
        return true;
      }
      node.vi = vi;
    }
    if ((node.type == ChainNodeType::Sensor || node.type == ChainNodeType::Relay) && node.vi != -1) {
      io_.writeln("[MASTER][TOPO][CHAIN] physical node vi must be -1");
      return true;
    }
    if (node.type == ChainNodeType::SemuChild && (node.vi < 0 || node.vi > 7)) {
      io_.writeln("[MASTER][TOPO][CHAIN] semu vi out of range (0..7)");
      return true;
    }
    if (node.type == ChainNodeType::RemuChild && (node.vi < 0 || node.vi > 15)) {
      io_.writeln("[MASTER][TOPO][CHAIN] remu vi out of range (0..15)");
      return true;
    }

    topology_edit.chain[pos] = node;
    size_t relay_groups = 0U;
    std::string write_error;
    if (!persistEditorRawToFixed(relay_groups, write_error)) {
      writef("[MASTER][TOPO][CHAIN] edit failed path=%s reason=%s", kFixedChainPath, write_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] edit ok index=%u type=%s mac=%s vi=%d nodes=%u relay_groups=%u",
           static_cast<unsigned int>(pos + 1U),
           chainTypeToken(node.type),
           macToPrintable(node.mac).c_str(),
           static_cast<int>(node.vi),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups));
    return true;
  }

  if (cmd_lower == "topology.chain.del") {
    io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.del <index>");
    return true;
  }
  if (startsWith(cmd_lower, "topology.chain.del ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() != 2U) {
      io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.del <index>");
      return true;
    }
    std::string load_error;
    if (!loadEditorFromFixedFile(true, load_error)) {
      writef("[MASTER][TOPO][CHAIN] del failed path=%s reason=%s", kFixedChainPath, load_error.c_str());
      return true;
    }
    if (topology_edit.chain.empty()) {
      io_.writeln("[MASTER][TOPO][CHAIN] del failed: chain is empty");
      return true;
    }
    size_t pos = 0U;
    if (!parseChainIndexToken(tok[1], topology_edit.chain.size(), pos)) {
      writef("[MASTER][TOPO][CHAIN] invalid index (allowed 1..%u or 0..%u)",
             static_cast<unsigned int>(topology_edit.chain.size()),
             static_cast<unsigned int>(topology_edit.chain.size() - 1U));
      return true;
    }
    topology_edit.chain.erase(topology_edit.chain.begin() + pos);
    size_t relay_groups = 0U;
    std::string write_error;
    if (!persistEditorRawToFixed(relay_groups, write_error)) {
      writef("[MASTER][TOPO][CHAIN] del failed path=%s reason=%s", kFixedChainPath, write_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] del ok index=%u nodes=%u relay_groups=%u",
           static_cast<unsigned int>(pos + 1U),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups));
    return true;
  }

  if (cmd_lower == "topology.chain.move") {
    io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.move <from_index> <to_index>");
    return true;
  }
  if (startsWith(cmd_lower, "topology.chain.move ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() != 3U) {
      io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.move <from_index> <to_index>");
      return true;
    }
    std::string load_error;
    if (!loadEditorFromFixedFile(true, load_error)) {
      writef("[MASTER][TOPO][CHAIN] move failed path=%s reason=%s", kFixedChainPath, load_error.c_str());
      return true;
    }
    if (topology_edit.chain.empty()) {
      io_.writeln("[MASTER][TOPO][CHAIN] move failed: chain is empty");
      return true;
    }
    size_t from = 0U;
    size_t to = 0U;
    if (!parseChainIndexToken(tok[1], topology_edit.chain.size(), from) ||
        !parseChainIndexToken(tok[2], topology_edit.chain.size(), to)) {
      writef("[MASTER][TOPO][CHAIN] invalid index (allowed 1..%u or 0..%u)",
             static_cast<unsigned int>(topology_edit.chain.size()),
             static_cast<unsigned int>(topology_edit.chain.size() - 1U));
      return true;
    }
    if (from != to) {
      const TopologyEditNodeState moved = topology_edit.chain[from];
      topology_edit.chain.erase(topology_edit.chain.begin() + from);
      if (to > from) {
        --to;
      }
      topology_edit.chain.insert(topology_edit.chain.begin() + to, moved);
    }
    size_t relay_groups = 0U;
    std::string write_error;
    if (!persistEditorRawToFixed(relay_groups, write_error)) {
      writef("[MASTER][TOPO][CHAIN] move failed path=%s reason=%s", kFixedChainPath, write_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] move ok from=%u to=%u nodes=%u relay_groups=%u",
           static_cast<unsigned int>(from + 1U),
           static_cast<unsigned int>(to + 1U),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups));
    return true;
  }

  if (cmd_lower == "topology.chain.set") {
    io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.set <chain_spec>");
    return true;
  }
  if (startsWith(cmd_lower, "topology.chain.set ")) {
    std::string chain_spec = trim(cmd_line.substr(std::strlen("topology.chain.set ")));
    if (chain_spec.empty()) {
      io_.writeln("[MASTER][TOPO][CHAIN] usage: topology.chain.set <chain_spec>");
      return true;
    }

    std::vector<TopologyEditNodeState> parsed_chain{};
    std::string parse_error;
    if (!parseChainSpec(chain_spec, parsed_chain, parse_error)) {
      writef("[MASTER][TOPO][CHAIN] set failed: %s", parse_error.c_str());
      return true;
    }

    TopologyEditState old = topology_edit;
    std::string load_error;
    if (!loadEditorFromFixedFile(true, load_error)) {
      topology_edit = old;
      topology_edit.topology_version = static_cast<uint32_t>(std::time(nullptr));
      if (topology_edit.topology_version == 0U) topology_edit.topology_version = 1U;
      topology_edit.seeds.clear();
    }
    topology_edit.chain = parsed_chain;
    topology_edit.seeds.clear();

    size_t relay_groups = 0U;
    size_t target_count = 0U;
    std::string write_error;
    if (!persistEditorValidatedToFixed(relay_groups, target_count, write_error)) {
      topology_edit = old;
      writef("[MASTER][TOPO][CHAIN] set failed: %s", write_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] set ok path=%s nodes=%u relay_groups=%u seeds=%u targets=%u",
           kFixedChainPath,
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups),
           static_cast<unsigned int>(topology_edit.seeds.size()),
           static_cast<unsigned int>(target_count));
    return true;
  }

  if (cmd_lower == "topology.chain.fix" || startsWith(cmd_lower, "topology.chain.fix ")) {
    if (!requireNoChainArgs("topology.chain.fix")) {
      return true;
    }
    std::string load_error;
    if (!loadEditorFromFixedFile(true, load_error)) {
      writef("[MASTER][TOPO][CHAIN] fix failed path=%s reason=%s", kFixedChainPath, load_error.c_str());
      return true;
    }
    size_t relay_groups = 0U;
    size_t target_count = 0U;
    std::string write_error;
    if (!persistEditorValidatedToFixed(relay_groups, target_count, write_error)) {
      writef("[MASTER][TOPO][CHAIN] fix failed: %s", write_error.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] fix ok path=%s topo_ver=%lu nodes=%u relay_groups=%u seeds=%u targets=%u",
           kFixedChainPath,
           static_cast<unsigned long>(topology_edit.topology_version),
           static_cast<unsigned int>(topology_edit.chain.size()),
           static_cast<unsigned int>(relay_groups),
           static_cast<unsigned int>(topology_edit.seeds.size()),
           static_cast<unsigned int>(target_count));
    return true;
  }

  if (cmd_lower == "topology.chain.backup" || startsWith(cmd_lower, "topology.chain.backup ")) {
    if (!requireNoChainArgs("topology.chain.backup")) {
      return true;
    }
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO][CHAIN] backup failed: local storage backend unavailable");
      return true;
    }
    std::string src;
    std::string read_msg;
    if (!readTextFileLocal(*ota_push_storage_, kFixedChainPath, src, read_msg)) {
      writef("[MASTER][TOPO][CHAIN] backup failed path=%s reason=%s", kFixedChainPath, read_msg.c_str());
      return true;
    }
    std::string write_msg;
    const std::string backup_path = std::string(kFixedChainPath) + ".bak";
    if (!writeTextFileLocal(*ota_push_storage_, backup_path, src, write_msg)) {
      writef("[MASTER][TOPO][CHAIN] backup failed path=%s reason=%s", backup_path.c_str(), write_msg.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] backup ok src=%s dst=%s bytes=%u",
           kFixedChainPath,
           backup_path.c_str(),
           static_cast<unsigned int>(src.size()));
    return true;
  }

  if (cmd_lower == "topology.chain.restore" || startsWith(cmd_lower, "topology.chain.restore ")) {
    if (!requireNoChainArgs("topology.chain.restore")) {
      return true;
    }
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][TOPO][CHAIN] restore failed: local storage backend unavailable");
      return true;
    }
    const std::string backup_path = std::string(kFixedChainPath) + ".bak";
    std::string src;
    std::string read_msg;
    if (!readTextFileLocal(*ota_push_storage_, backup_path, src, read_msg)) {
      writef("[MASTER][TOPO][CHAIN] restore failed path=%s reason=%s", backup_path.c_str(), read_msg.c_str());
      return true;
    }
    std::string write_msg;
    if (!writeTextFileLocal(*ota_push_storage_, kFixedChainPath, src, write_msg)) {
      writef("[MASTER][TOPO][CHAIN] restore failed path=%s reason=%s", kFixedChainPath, write_msg.c_str());
      return true;
    }
    writef("[MASTER][TOPO][CHAIN] restore ok src=%s dst=%s bytes=%u",
           backup_path.c_str(),
           kFixedChainPath,
           static_cast<unsigned int>(src.size()));
    return true;
  }

  if (cmd_lower == "topology.chain.apply" || startsWith(cmd_lower, "topology.chain.apply ")) {
    if (!requireNoChainArgs("topology.chain.apply")) {
      return true;
    }
    std::string json_text;
    TopologyChainPlanDebug debug{};
    uint32_t topology_version = 1U;
    size_t target_count = 0U;
    std::string err;
    if (!loadFixedChainForRead(json_text, debug, topology_version, target_count, err)) {
      writef("[MASTER][TOPO][CHAIN] apply blocked: invalid path=%s reason=%s",
             kFixedChainPath,
             err.c_str());
      return true;
    }
    // Avoid recursive re-entry into this large handler; rewrite command and
    // continue in the same frame so it falls through to topology.deploy.file.
    cmd_line = "topology.deploy.file /o/s/tp.json";
    cmd_lower = "topology.deploy.file /o/s/tp.json";
  }

  if (cmd_lower == "topology.chain.verify" || startsWith(cmd_lower, "topology.chain.verify ")) {
    const std::vector<std::string> tok = splitTokens(cmd_line);
    if (tok.size() > 2U) {
      io_.writeln("[MASTER][TOPO][VERIFY] usage: topology.chain.verify [timeout_ms]");
      return true;
    }
    uint32_t timeout_ms = 6000U;
    if (tok.size() == 2U) {
      if (!parseU32Token(tok[1], timeout_ms) || timeout_ms < 1000U || timeout_ms > 120000U) {
        io_.writeln("[MASTER][TOPO][VERIFY] invalid timeout_ms (1000..120000)");
        return true;
      }
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][TOPO][VERIFY] management path unavailable");
      return true;
    }

    std::string json_text;
    TopologyChainPlanDebug debug{};
    uint32_t topology_version = 1U;
    size_t target_count = 0U;
    std::string read_error;
    if (!loadFixedChainForRead(json_text, debug, topology_version, target_count, read_error)) {
      writef("[MASTER][TOPO][VERIFY] blocked: invalid path=%s reason=%s",
             kFixedChainPath,
             read_error.c_str());
      return true;
    }

    std::vector<TopologyDeployTarget> targets{};
    std::string parse_error;
    if (!parseTopologyChainJson(json_text,
                                topology_version,
                                targets,
                                topology_version,
                                parse_error,
                                nullptr)) {
      writef("[MASTER][TOPO][VERIFY] blocked: parse failed path=%s reason=%s",
             kFixedChainPath,
             parse_error.c_str());
      return true;
    }
    if (targets.empty()) {
      io_.writeln("[MASTER][TOPO][VERIFY] no targets in topology chain");
      return true;
    }

    std::vector<MacAddress> paired_peers{};
    manager_.getPersistedPeers(paired_peers);
    if (paired_peers.empty()) {
      io_.writeln("[MASTER][TOPO][VERIFY] no paired peers available");
      return true;
    }

    clearTopologyVerifySession_();
    topology_verify_.active = true;
    topology_verify_.started_ms = nowMs();
    topology_verify_.deadline_ms = topology_verify_.started_ms + timeout_ms;
    topology_verify_.topology_version = topology_version;
    topology_verify_.source_path = kFixedChainPath;
    topology_verify_.targets.clear();
    topology_verify_.requests.clear();
    topology_verify_.targets.reserve(targets.size());
    topology_verify_.requests.reserve(targets.size());

    uint32_t queued_requests = 0U;
    uint32_t skipped_not_paired = 0U;
    uint32_t submit_failed = 0U;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    for (const auto& target : targets) {
      TopologyVerifyTargetState verify_target{};
      verify_target.target = target.target;
      verify_target.expected_snapshot = target.snapshot;
      verify_target.slots_done = true;
      verify_target.slots_ok = true;
      verify_target.slots_reason = "slots_check_not_requested";
      const size_t target_index = topology_verify_.targets.size();
      topology_verify_.targets.push_back(verify_target);
      TopologyVerifyTargetState& state = topology_verify_.targets.back();

      const bool is_paired = std::find(paired_peers.begin(),
                                       paired_peers.end(),
                                       target.target) != paired_peers.end();
      if (!is_paired) {
        state.status_done = true;
        state.status_ok = false;
        state.status_reason = "target_not_paired";
        ++skipped_not_paired;
        continue;
      }

      uint32_t req_id = 0U;
      const bool submit_ok =
          submitRuntimeTargeted_(mgmt,
                                 static_cast<uint16_t>(ManagementCommandId::TopologyStatusGet),
                                 {},
                                 &req_id,
                                 0U,
                                 false,
                                 &target.target);
      if (!submit_ok || req_id == 0U) {
        state.status_done = true;
        state.status_ok = false;
        state.status_reason = "status_submit_failed";
        ++submit_failed;
        continue;
      }

      state.status_req_id = req_id;
      TopologyVerifyRequestState req{};
      req.target_index = target_index;
      req.kind = TopologyVerifyReqKind::Status;
      topology_verify_.requests[req_id] = req;
      ++queued_requests;
    }
    correlation_id_ = mgmt.nextReqId();

    writef("[MASTER][TOPO][VERIFY] started path=%s topo_ver=%lu mode=status_checksum targets=%u queued=%u skipped_not_paired=%u submit_failed=%u timeout_ms=%lu",
           kFixedChainPath,
           static_cast<unsigned long>(topology_verify_.topology_version),
           static_cast<unsigned int>(topology_verify_.targets.size()),
           static_cast<unsigned int>(queued_requests),
           static_cast<unsigned int>(skipped_not_paired),
           static_cast<unsigned int>(submit_failed),
           static_cast<unsigned long>(timeout_ms));
    maybeFinalizeTopologyVerifySession_();
    return true;
  }

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
    io_.writeln("  topology.chain.help");
    io_.writeln("  topology.chain.set.help");
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
    uint32_t apply_req = 0U;
    const bool applied =
        submitTopo(mgmt, ManagementCommandId::TopologyStageSet, management_utils::buildTopologyStagePayload(snapshot), &apply_req);
    correlation_id_ = mgmt.nextReqId();
    if (applied) {
      writef("[MASTER][TOPO] apply requested req=%lu mode=direct_apply_overwrite version=%lu",
             static_cast<unsigned long>(apply_req),
             static_cast<unsigned long>(snapshot.topology_version));
    } else {
      io_.writeln("[MASTER][TOPO] apply failed: stage submit failed");
    }
    return applied;
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

          writef("[MASTER][TOPO][VER] file=%lu mode=direct_apply_overwrite",
                 static_cast<unsigned long>(topology_version));

          uint32_t queued = 0U;
          uint32_t failed = 0U;
          uint32_t skipped_not_paired = 0U;
          auto settleDeployRuntime = [&](uint32_t settle_ms) {
            if (settle_ms == 0U) return;
            const uint32_t start_ms = nowMs();
            while (static_cast<uint32_t>(nowMs() - start_ms) < settle_ms) {
              const uint32_t now_ms = nowMs();
              if (shouldTickManagementRuntimeFromCli_() && management_runtime_ != nullptr) {
                management_runtime_->tick(now_ms, 2U, 8U, 16U);
              }
              pumpManagementMailbox();
              pumpDescriptorQueue(now_ms);
              yieldCliBusyWaitLoop();
            }
          };
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
            uint32_t apply_req = 0U;
            bool applied = submitRuntimeTargeted_(mgmt,
                                                  static_cast<uint16_t>(ManagementCommandId::TopologyStageSet),
                                                  management_utils::buildTopologyStagePayload(target.snapshot),
                                                  &apply_req,
                                                  0U,
                                                  false,
                                                  &target.target);
            if (!applied) {
              // One bounded retry to absorb transient queue pressure.
              settleDeployRuntime(120U);
              applied = submitRuntimeTargeted_(mgmt,
                                               static_cast<uint16_t>(ManagementCommandId::TopologyStageSet),
                                               management_utils::buildTopologyStagePayload(target.snapshot),
                                               &apply_req,
                                               0U,
                                               false,
                                               &target.target);
            }
            settleDeployRuntime(80U);
            correlation_id_ = mgmt.nextReqId();
            if (applied) {
              ++queued;
              writef("[MASTER][TOPO] deploy queued peer=%s apply_req=%lu",
                     macToPrintable(target.target).c_str(),
                     static_cast<unsigned long>(apply_req));
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
          if (queued > 0U) {
            io_.writeln("[MASTER][TOPO] tip: run topology.chain.verify to confirm committed status on each target");
          }
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

}  // namespace espnow_link
