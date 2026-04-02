#include "espnow_link/management_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>

#include "espnow_link/cli_master.hpp"
#include "espnow_link/management_utils.hpp"
#include "espnow_link/ota_paths.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/ota_types.hpp"
#include "espnow_link/address.hpp"

namespace espnow_link {
namespace {

constexpr uint32_t kDefaultDiscoveryWindowMs = 10000;
constexpr size_t kMaxPairedSlaves = 15;
constexpr size_t kPendingDescriptorPullsMax = 32U;
constexpr uint8_t kSettingModeByKey = 0;
constexpr uint8_t kSettingModeById = 1;
constexpr uint16_t kControlCmdRestart = 0x0001;
constexpr uint16_t kControlCmdReset = 0x0002;
constexpr uint16_t kControlCmdAudioPing = 0x0004;
constexpr uint16_t kLogSourceManagement = 0x0301;
constexpr uint16_t kLogEvtCmdRx = 0x0001;
constexpr uint16_t kLogEvtCmdDone = 0x0002;
constexpr uint16_t kLogEvtCmdFail = 0x0003;
constexpr uint16_t kLogEvtTimeout = 0x0004;
constexpr uint16_t kLogEvtQueueFull = 0x0005;
constexpr uint8_t kTopologyStageStepNone = 0U;
constexpr uint8_t kTopologyStageStepWaitClearAck = 1U;
constexpr uint8_t kTopologyStageStepWaitBeginAck = 2U;
constexpr uint8_t kTopologyStageStepWaitGroupAck = 3U;
constexpr uint8_t kTopologyStageStepWaitSlotAck = 4U;
constexpr uint8_t kTopologyStageStepWaitFinalizeAck = 5U;

bool isZeroMacAddress(const MacAddress& mac) {
  for (uint8_t b : mac) {
    if (b != 0U) {
      return false;
    }
  }
  return true;
}

struct SettingGetArgs {
  bool by_id = false;
  uint16_t setting_id = 0;
  std::string key;
};

struct SettingSetArgs {
  bool by_id = false;
  uint16_t setting_id = 0;
  std::string key;
  std::string value;
};

struct LogReadArgs {
  uint32_t offset = 0;
  uint16_t max_bytes = 96;
};

struct LogControlArgs {
  bool enabled = true;
};

struct ChannelSyncAllArgs {
  uint8_t channel = 0;
};

struct ChainLoopControlArgs {
  bool has_value = false;
  bool enabled = false;
};

struct PageArgs {
  uint16_t cursor = 0;
  uint8_t page_size = 0;
};

struct RemovePeerArgs {
  bool has_peer = false;
  MacAddress peer{};
};

struct OtaTransferBeginArgs {
  uint32_t total_size = 0;
  uint32_t chunk_size = 0;
  uint32_t image_crc32 = 0;
  bool has_metadata = false;
  FirmwareImageMetadata metadata{};
};

struct OtaTransferChunkArgs {
  uint32_t offset = 0;
  const uint8_t* data = nullptr;
  size_t data_len = 0;
};

struct OtaTransferEndArgs {
  uint32_t total_size = 0;
  uint32_t image_crc32 = 0;
};

struct OtaPushStartArgs {
  std::string local_path{};
  uint16_t chunk_bytes = 0;
};

struct OtaArchiveArgs {
  char role = 'm';
  std::string id{};
  bool remote = false;
};

bool readU16Le(const uint8_t* p, size_t len, uint16_t& out) {
  if (p == nullptr || len < 2) return false;
  out = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
  return true;
}

bool readU32Le(const uint8_t* p, size_t len, uint32_t& out) {
  if (p == nullptr || len < 4) return false;
  out = static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
  return true;
}

bool readU64Le(const uint8_t* p, size_t len, uint64_t& out) {
  if (p == nullptr || len < 8) return false;
  out = static_cast<uint64_t>(p[0]) |
        (static_cast<uint64_t>(p[1]) << 8) |
        (static_cast<uint64_t>(p[2]) << 16) |
        (static_cast<uint64_t>(p[3]) << 24) |
        (static_cast<uint64_t>(p[4]) << 32) |
        (static_cast<uint64_t>(p[5]) << 40) |
        (static_cast<uint64_t>(p[6]) << 48) |
        (static_cast<uint64_t>(p[7]) << 56);
  return true;
}

bool parseSettingGetArgs(const std::vector<uint8_t>& payload, SettingGetArgs& out) {
  out = SettingGetArgs{};
  if (payload.empty()) return false;
  const uint8_t mode = payload[0];
  if (mode == kSettingModeById) {
    if (payload.size() < 3) return false;
    uint16_t id = 0;
    if (!readU16Le(payload.data() + 1, payload.size() - 1, id)) return false;
    out.by_id = true;
    out.setting_id = id;
    return true;
  }
  if (mode == kSettingModeByKey) {
    if (payload.size() < 2) return false;
    const uint8_t key_len = payload[1];
    if (payload.size() != static_cast<size_t>(2 + key_len)) return false;
    out.key.assign(reinterpret_cast<const char*>(payload.data() + 2), key_len);
    return !out.key.empty();
  }
  return false;
}

bool parseSettingSetArgs(const std::vector<uint8_t>& payload, SettingSetArgs& out) {
  out = SettingSetArgs{};
  if (payload.empty()) return false;
  const uint8_t mode = payload[0];
  if (mode == kSettingModeById) {
    if (payload.size() < 5) return false;
    uint16_t id = 0;
    uint16_t value_len = 0;
    if (!readU16Le(payload.data() + 1, payload.size() - 1, id)) return false;
    if (!readU16Le(payload.data() + 3, payload.size() - 3, value_len)) return false;
    if (payload.size() != static_cast<size_t>(5 + value_len)) return false;
    out.by_id = true;
    out.setting_id = id;
    out.value.assign(reinterpret_cast<const char*>(payload.data() + 5), value_len);
    return true;
  }
  if (mode == kSettingModeByKey) {
    if (payload.size() < 4) return false;
    const uint8_t key_len = payload[1];
    const size_t value_len_offset = static_cast<size_t>(2 + key_len);
    if (payload.size() < value_len_offset + 2) return false;
    uint16_t value_len = 0;
    if (!readU16Le(payload.data() + value_len_offset, payload.size() - value_len_offset, value_len)) return false;
    const size_t value_offset = value_len_offset + 2;
    if (payload.size() != value_offset + value_len) return false;
    out.key.assign(reinterpret_cast<const char*>(payload.data() + 2), key_len);
    if (out.key.empty()) return false;
    out.value.assign(reinterpret_cast<const char*>(payload.data() + value_offset), value_len);
    return true;
  }
  return false;
}

bool parseLogReadArgs(const std::vector<uint8_t>& payload, LogReadArgs& out) {
  out = LogReadArgs{};
  if (payload.size() != 4U && payload.size() != 6U) return false;
  if (!readU32Le(payload.data(), payload.size(), out.offset)) return false;
  if (payload.size() == 6U) {
    uint16_t max_bytes = 0;
    if (!readU16Le(payload.data() + 4, payload.size() - 4, max_bytes) || max_bytes == 0) {
      return false;
    }
    out.max_bytes = static_cast<uint16_t>(std::min<uint16_t>(max_bytes, 512));
  }
  return true;
}

bool parseLogControlArgs(const std::vector<uint8_t>& payload, LogControlArgs& out) {
  out = LogControlArgs{};
  if (payload.size() != 1) return false;
  if (payload[0] > 1U) return false;
  out.enabled = (payload[0] != 0U);
  return true;
}

bool parseChannelSyncAllArgs(const std::vector<uint8_t>& payload, ChannelSyncAllArgs& out) {
  out = ChannelSyncAllArgs{};
  if (payload.size() != 1U) return false;
  if (payload[0] < 1U || payload[0] > 14U) return false;
  out.channel = payload[0];
  return true;
}

bool parseChainLoopControlArgs(const std::vector<uint8_t>& payload, ChainLoopControlArgs& out) {
  out = ChainLoopControlArgs{};
  if (payload.empty()) {
    return true;
  }
  if (payload.size() != 1U || payload[0] > 1U) {
    return false;
  }
  out.has_value = true;
  out.enabled = (payload[0] != 0U);
  return true;
}

bool parseStringU16Payload(const std::vector<uint8_t>& payload, std::string& out_value) {
  out_value.clear();
  if (payload.size() < 2U) return false;
  uint16_t n = 0;
  if (!readU16Le(payload.data(), payload.size(), n)) return false;
  if (payload.size() != static_cast<size_t>(2U + n)) return false;
  out_value.assign(reinterpret_cast<const char*>(payload.data() + 2U), n);
  return !out_value.empty();
}

bool parsePageArgs(const std::vector<uint8_t>& payload, PageArgs& out) {
  out = PageArgs{};
  if (payload.size() != 3U) return false;
  uint16_t cursor = 0;
  if (!readU16Le(payload.data(), payload.size(), cursor)) return false;
  const uint8_t page_size = payload[2];
  if (page_size == 0U) return false;
  out.cursor = cursor;
  out.page_size = page_size;
  return true;
}

bool parseRemovePeerArgs(const std::vector<uint8_t>& payload, RemovePeerArgs& out) {
  out = RemovePeerArgs{};
  if (payload.empty()) {
    return true;
  }
  if (payload.size() != 6U) return false;
  out.has_peer = true;
  std::memcpy(out.peer.data(), payload.data(), 6U);
  return true;
}

bool parseOtaTransferBeginArgs(const std::vector<uint8_t>& payload, OtaTransferBeginArgs& out) {
  out = OtaTransferBeginArgs{};
  if (payload.size() < 12U) return false;
  if (!readU32Le(payload.data() + 0U, payload.size(), out.total_size) ||
      !readU32Le(payload.data() + 4U, payload.size(), out.chunk_size) ||
      !readU32Le(payload.data() + 8U, payload.size(), out.image_crc32)) {
    return false;
  }
  if (payload.size() == 12U) return true;
  if (payload.size() < 15U) return false;

  const uint8_t sw_len = payload[12U];
  const uint8_t build_len = payload[13U];
  const uint8_t role_len = payload[14U];
  if (sw_len > 63U || build_len > 63U) return false;
  if (role_len > 15U) return false;
  const size_t expected =
      15U + static_cast<size_t>(sw_len) + static_cast<size_t>(build_len) + static_cast<size_t>(role_len);
  if (payload.size() != expected) return false;
  size_t off = 15U;
  if (sw_len > 0U) {
    out.metadata.sw_version.assign(reinterpret_cast<const char*>(payload.data() + off), sw_len);
    off += static_cast<size_t>(sw_len);
  }
  if (build_len > 0U) {
    out.metadata.build_id.assign(reinterpret_cast<const char*>(payload.data() + off), build_len);
    off += static_cast<size_t>(build_len);
  }
  if (role_len > 0U) {
    out.metadata.target_role.assign(reinterpret_cast<const char*>(payload.data() + off), role_len);
  }
  out.has_metadata = true;
  return true;
}

bool parseOtaTransferChunkArgs(const std::vector<uint8_t>& payload, OtaTransferChunkArgs& out) {
  out = OtaTransferChunkArgs{};
  if (payload.size() <= 4U) return false;
  if (!readU32Le(payload.data(), payload.size(), out.offset)) return false;
  out.data = payload.data() + 4U;
  out.data_len = payload.size() - 4U;
  return out.data != nullptr && out.data_len > 0U;
}

bool parseOtaTransferEndArgs(const std::vector<uint8_t>& payload, OtaTransferEndArgs& out) {
  out = OtaTransferEndArgs{};
  if (payload.size() != 8U) return false;
  return readU32Le(payload.data() + 0U, payload.size(), out.total_size) &&
         readU32Le(payload.data() + 4U, payload.size(), out.image_crc32);
}

bool parseOtaPushStartArgs(const std::vector<uint8_t>& payload, OtaPushStartArgs& out) {
  out = OtaPushStartArgs{};
  return management_utils::parseOtaPushStartPayload(payload, out.local_path, out.chunk_bytes);
}

bool parseOtaArchiveArgs(const std::vector<uint8_t>& payload, OtaArchiveArgs& out, bool require_id) {
  out = OtaArchiveArgs{};
  return management_utils::parseOtaArchivePayload(payload, out.role, out.id, out.remote, require_id);
}

std::string otaArchiveTargetString(const char* action, const OtaArchiveArgs& args) {
  std::string target = "arc.";
  target += (action != nullptr) ? action : "";
  target.push_back('.');
  target.push_back(args.role);
  if (!args.id.empty()) {
    target.push_back('.');
    target += args.id;
  }
  return target;
}

std::string otaSidecarJsonPath(const std::string& bin_path) {
  const size_t slash = bin_path.find_last_of('/');
  const size_t dot = bin_path.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    return bin_path.substr(0U, dot) + ".json";
  }
  return bin_path + ".json";
}

std::string otaImageNameFromCorr(uint32_t corr_id) {
  (void)corr_id;
  return std::string("u.bin");
}

bool readTextFile(IOtaStorageBackend& storage,
                  const std::string& path,
                  std::string& out_text,
                  std::string& out_error) {
  out_text.clear();
  OtaStorageStat st{};
  std::string msg;
  if (!storage.stat(path, st, msg)) {
    out_error = msg.empty() ? "stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    out_error = "file missing";
    return false;
  }
  if (st.size_bytes > 4096U) {
    out_error = "file too large";
    return false;
  }
  std::vector<uint8_t> buf(st.size_bytes, 0U);
  size_t out_len = 0U;
  if (!storage.readAt(path, 0U, buf.data(), buf.size(), out_len, msg)) {
    out_error = msg.empty() ? "read failed" : msg;
    return false;
  }
  if (out_len == 0U) {
    out_error = "empty file";
    return false;
  }
  out_text.assign(reinterpret_cast<const char*>(buf.data()),
                  reinterpret_cast<const char*>(buf.data() + out_len));
  out_error.clear();
  return true;
}

bool extractJsonStringField(const std::string& json,
                            const char* key,
                            std::string& out_value) {
  out_value.clear();
  if (key == nullptr || key[0] == '\0') return false;
  const std::string pattern = std::string("\"") + key + "\"";
  const size_t key_pos = json.find(pattern);
  if (key_pos == std::string::npos) return false;
  size_t pos = json.find(':', key_pos + pattern.size());
  if (pos == std::string::npos) return false;
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
  if (pos >= json.size()) return false;
  if (json[pos] == '"') {
    ++pos;
    std::string value;
    value.reserve(32U);
    bool escaped = false;
    while (pos < json.size()) {
      const char c = json[pos++];
      if (escaped) {
        value.push_back(c);
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == '"') {
        out_value = management_utils::trim(value);
        return !out_value.empty();
      }
      value.push_back(c);
    }
    return false;
  }
  size_t end = pos;
  while (end < json.size() &&
         json[end] != ',' &&
         json[end] != '}' &&
         json[end] != '\n' &&
         json[end] != '\r') {
    ++end;
  }
  out_value = management_utils::trim(json.substr(pos, end - pos));
  return !out_value.empty();
}

bool loadFirmwareMetadataFromSidecar(IOtaStorageBackend& storage,
                                     const std::string& bin_path,
                                     FirmwareImageMetadata& out_meta,
                                     std::string& out_sidecar_path,
                                     std::string& out_error) {
  out_meta = FirmwareImageMetadata{};
  out_sidecar_path = otaSidecarJsonPath(bin_path);

  std::string json;
  if (!readTextFile(storage, out_sidecar_path, json, out_error)) {
    return false;
  }

  std::string version;
  (void)extractJsonStringField(json, "sw_version", version);
  if (version.empty()) {
    out_error = "missing sw_version";
    return false;
  }

  std::string build;
  (void)extractJsonStringField(json, "build_id", build);
  if (build.empty()) {
    out_error = "missing build_id";
    return false;
  }

  std::string target_role;
  (void)extractJsonStringField(json, "target_role", target_role);
  target_role = management_utils::lowerAscii(management_utils::trim(target_role));
  if (target_role != "master" && target_role != "slave") {
    out_error = "missing/invalid target_role (master|slave)";
    return false;
  }

  if (version.size() > 63U || build.size() > 63U || target_role.size() > 15U) {
    out_error = "metadata field too long";
    return false;
  }

  out_meta.sw_version = version;
  out_meta.build_id = build;
  out_meta.target_role = target_role;
  out_error.clear();
  return true;
}

bool computeOtaFileCrc(IOtaStorageBackend& storage,
                       const std::string& local_path,
                       uint32_t size_bytes,
                       uint32_t& out_crc,
                       std::string& out_error) {
  constexpr uint32_t kCrc32Poly = 0xEDB88320U;
  auto crcUpdate = [](uint32_t running_crc, const uint8_t* data, size_t len) -> uint32_t {
    if (data == nullptr || len == 0U) return running_crc;
    uint32_t crc = running_crc;
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint32_t>(data[i]);
      for (uint8_t b = 0; b < 8U; ++b) {
        const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
        crc = (crc >> 1U) ^ (kCrc32Poly & mask);
      }
    }
    return crc;
  };

  uint32_t offset = 0U;
  uint32_t running_crc = 0xFFFFFFFFU;
  std::vector<uint8_t> buf(4096U, 0U);
  std::string msg;
  while (offset < size_bytes) {
    const size_t req = std::min<size_t>(buf.size(), static_cast<size_t>(size_bytes - offset));
    size_t out_len = 0U;
    if (!storage.readAt(local_path, offset, buf.data(), req, out_len, msg)) {
      out_error = msg.empty() ? "readAt failed" : msg;
      return false;
    }
    if (out_len == 0U) {
      out_error = "short read (0 bytes)";
      return false;
    }
    running_crc = crcUpdate(running_crc, buf.data(), out_len);
    offset += static_cast<uint32_t>(out_len);
  }
  out_crc = ~running_crc;
  out_error.clear();
  return true;
}

struct OtaArchiveEntryLocal {
  std::string id{};
  std::string bin_name{};
  std::string meta_name{};
  uint32_t size_bytes = 0U;
  uint32_t crc32 = 0U;
  std::string sw_version{};
  std::string build_id{};
  std::string target_role{};
  std::string source{};
  uint32_t created_epoch_s = 0U;
};

std::string jsonEscape(const std::string& in);
bool writeTextFile(IOtaStorageBackend& storage,
                   const std::string& path,
                   const std::string& text,
                   std::string& out_error);

bool normalizeArchiveTargetRole(const std::string& raw_target, std::string& out_target) {
  out_target = management_utils::lowerAscii(management_utils::trim(raw_target));
  return out_target == "master" || out_target == "slave";
}

bool buildArchiveMetadataJson(const OtaArchiveEntryLocal& entry,
                              std::string& out_json,
                              std::string& out_error) {
  out_json.clear();
  out_error.clear();
  const std::string sw = entry.sw_version.empty() ? "running" : entry.sw_version;
  const std::string build = entry.build_id.empty() ? "na" : entry.build_id;
  std::string target;
  if (!normalizeArchiveTargetRole(entry.target_role, target)) {
    out_error = "metadata target_role missing/invalid";
    return false;
  }
  const std::string source = entry.source.empty() ? "archive" : entry.source;

  char crc_buf[16] = {0};
  std::snprintf(crc_buf, sizeof(crc_buf), "0x%08lX", static_cast<unsigned long>(entry.crc32));

  out_json = "{\"sw_version\":\"" + jsonEscape(sw) + "\"";
  out_json += ",\"build_id\":\"" + jsonEscape(build) + "\"";
  out_json += ",\"target_role\":\"" + jsonEscape(target) + "\"";
  out_json += ",\"source\":\"" + jsonEscape(source) + "\"";
  if (!entry.id.empty()) {
    out_json += ",\"archive_id\":\"" + jsonEscape(entry.id) + "\"";
  }
  out_json += ",\"archive_crc\":\"" + std::string(crc_buf) + "\"";
  out_json += ",\"archive_size\":" + std::to_string(entry.size_bytes);
  out_json += "}\n";
  return true;
}

bool writeArchiveMetadataSidecarToSd(IOtaStorageBackend& storage,
                                     const OtaArchiveEntryLocal& entry,
                                     const std::string& bucket,
                                     std::string& out_error) {
  const std::string tmp_meta = std::string(ota_paths::kState) + "/_a_" +
                               (entry.id.empty() ? std::string("meta") : entry.id) + ".jsn";
  std::string meta_json;
  if (!buildArchiveMetadataJson(entry, meta_json, out_error)) {
    return false;
  }

  if (!writeTextFile(storage, tmp_meta, meta_json, out_error)) {
    out_error = "temp metadata write failed: " + out_error;
    return false;
  }

  std::string msg;
  if (!storage.copySpiffsToSd(tmp_meta, bucket + "/" + entry.meta_name, msg)) {
    (void)storage.removePath(tmp_meta, msg);
    out_error = "metadata copy failed: " + (msg.empty() ? std::string("copy failed") : msg);
    return false;
  }

  (void)storage.removePath(tmp_meta, msg);
  out_error.clear();
  return true;
}

bool restoreArchiveMetadataToStage(IOtaStorageBackend& storage,
                                   const OtaArchiveEntryLocal& entry,
                                   const std::string& bucket,
                                   const std::string& stage_bin,
                                   const std::string& stage_meta,
                                   std::string& out_error) {
  out_error.clear();

  std::string copy_msg;
  if (!storage.copySdToSpiffs(bucket + "/" + entry.meta_name, stage_meta, copy_msg)) {
    out_error = "metadata copy failed: " + (copy_msg.empty() ? std::string("copy failed") : copy_msg);
    return false;
  }

  FirmwareImageMetadata parsed{};
  std::string parsed_sidecar;
  std::string parsed_error;
  if (!loadFirmwareMetadataFromSidecar(storage, stage_bin, parsed, parsed_sidecar, parsed_error)) {
    out_error = "copied metadata invalid: " + parsed_error;
    return false;
  }
  return true;
}

bool verifyArchiveEntryIntegrity(IOtaStorageBackend& storage,
                                 const OtaArchiveEntryLocal& entry,
                                 char role,
                                 const std::string& bucket,
                                 std::string& out_message) {
  const std::string bin_path = bucket + "/" + entry.bin_name;
  const std::string meta_path = bucket + "/" + entry.meta_name;

  OtaStorageStat bin_stat{};
  std::string msg;
  if (!storage.stat(bin_path, bin_stat, msg)) {
    out_message = "verify failed: bin stat failed (" + msg + ")";
    return false;
  }
  if (!bin_stat.exists || bin_stat.is_dir || bin_stat.size_bytes == 0U) {
    out_message = "verify failed: bin missing/invalid";
    return false;
  }
  if (entry.size_bytes > 0U && bin_stat.size_bytes != entry.size_bytes) {
    out_message = "verify failed: size mismatch expected=" + std::to_string(entry.size_bytes) +
                  " actual=" + std::to_string(bin_stat.size_bytes);
    return false;
  }

  uint32_t crc = 0U;
  if (!computeOtaFileCrc(storage, bin_path, bin_stat.size_bytes, crc, msg)) {
    out_message = "verify failed: crc read failed (" + msg + ")";
    return false;
  }
  if (entry.crc32 != 0U && crc != entry.crc32) {
    char got_crc[16] = {0};
    char exp_crc[16] = {0};
    std::snprintf(got_crc, sizeof(got_crc), "0x%08lX", static_cast<unsigned long>(crc));
    std::snprintf(exp_crc, sizeof(exp_crc), "0x%08lX", static_cast<unsigned long>(entry.crc32));
    out_message = std::string("verify failed: crc mismatch expected=") + exp_crc + " actual=" + got_crc;
    return false;
  }

  OtaStorageStat meta_stat{};
  if (!storage.stat(meta_path, meta_stat, msg)) {
    out_message = "verify failed: metadata stat failed (" + msg + ")";
    return false;
  }
  if (!meta_stat.exists || meta_stat.is_dir || meta_stat.size_bytes == 0U) {
    out_message = "verify failed: metadata missing/invalid";
    return false;
  }

  std::string meta_text;
  if (!readTextFile(storage, meta_path, meta_text, msg)) {
    out_message = "verify failed: metadata read failed (" + msg + ")";
    return false;
  }
  std::string sw;
  std::string build;
  std::string target;
  (void)extractJsonStringField(meta_text, "sw_version", sw);
  (void)extractJsonStringField(meta_text, "build_id", build);
  (void)extractJsonStringField(meta_text, "target_role", target);
  target = management_utils::lowerAscii(management_utils::trim(target));
  if (sw.empty()) {
    out_message = "verify failed: metadata missing sw_version";
    return false;
  }
  if (build.empty()) {
    out_message = "verify failed: metadata missing build_id";
    return false;
  }
  if (target != "master" && target != "slave") {
    out_message = "verify failed: metadata missing/invalid target_role";
    return false;
  }

  std::string expected_target;
  if (!normalizeArchiveTargetRole(entry.target_role, expected_target)) {
    out_message = "verify failed: manifest target_role invalid";
    return false;
  }
  if (target != expected_target) {
    out_message = "verify failed: metadata target mismatch expected=" + expected_target +
                  " actual=" + target;
    return false;
  }
  if (!entry.sw_version.empty() && sw != entry.sw_version) {
    out_message = "verify failed: metadata sw mismatch expected=" + entry.sw_version +
                  " actual=" + sw;
    return false;
  }
  if (!entry.build_id.empty() && build != entry.build_id) {
    out_message = "verify failed: metadata build mismatch expected=" + entry.build_id +
                  " actual=" + build;
    return false;
  }

  char crc_buf[16] = {0};
  std::snprintf(crc_buf, sizeof(crc_buf), "0x%08lX", static_cast<unsigned long>(crc));
  out_message = "verified id=" + entry.id +
                " role=" + std::string(1U, role) +
                " size=" + std::to_string(bin_stat.size_bytes) +
                " crc=" + std::string(crc_buf) +
                " target=" + target +
                " sw=" + sw +
                " build=" + build;
  return true;
}

char localRoleChar(Role r) {
  return (r == Role::Master) ? 'm' : 's';
}

std::string archiveManifestPath(char role) {
  std::string p = ota_paths::kState;
  p += "/a";
  p.push_back((role == 's') ? 's' : 'm');
  p += ".jsn";
  return p;
}

std::string archiveBucketPath(char role) {
  return (role == 's') ? std::string(ota_paths::kArchiveSlave) : std::string(ota_paths::kArchiveMaster);
}

std::string archiveFormatId(uint32_t seed) {
  char b[7] = {0};
  std::snprintf(b, sizeof(b), "%06lX", static_cast<unsigned long>(seed & 0xFFFFFFUL));
  return std::string(b);
}

std::string normalizeArchiveId(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isxdigit(uc) == 0) continue;
    out.push_back(static_cast<char>(std::toupper(uc)));
  }
  if (out.size() > 6U) {
    out = out.substr(out.size() - 6U);
  }
  return out;
}

std::string jsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8U);
  for (char c : in) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

bool extractJsonU32Field(const std::string& json, const char* key, uint32_t& out_value) {
  out_value = 0U;
  std::string tmp;
  if (!extractJsonStringField(json, key, tmp)) {
    const std::string pattern = std::string("\"") + key + "\"";
    const size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos) return false;
    size_t pos = json.find(':', key_pos + pattern.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
    if (pos >= json.size()) return false;
    size_t end = pos;
    while (end < json.size() &&
           json[end] != ',' &&
           json[end] != '}' &&
           std::isspace(static_cast<unsigned char>(json[end])) == 0) {
      ++end;
    }
    tmp = management_utils::trim(json.substr(pos, end - pos));
    if (tmp.empty()) return false;
  }
  char* endp = nullptr;
  const unsigned long v = std::strtoul(tmp.c_str(), &endp, 0);
  if (endp == nullptr || *endp != '\0') return false;
  out_value = static_cast<uint32_t>(v);
  return true;
}

bool writeTextFile(IOtaStorageBackend& storage,
                   const std::string& path,
                   const std::string& text,
                   std::string& out_error) {
  std::string msg;
  const size_t slash = path.find_last_of('/');
  const std::string parent = (slash == std::string::npos || slash == 0U) ? "/" : path.substr(0U, slash);
  if (!storage.ensureDir(parent, msg)) {
    out_error = msg.empty() ? "mkdir failed" : msg;
    return false;
  }
  if (!storage.truncateFile(path, msg)) {
    out_error = msg.empty() ? "truncate failed" : msg;
    return false;
  }
  if (!text.empty() &&
      !storage.writeAt(path, 0U, reinterpret_cast<const uint8_t*>(text.data()), text.size(), msg)) {
    out_error = msg.empty() ? "write failed" : msg;
    return false;
  }
  out_error.clear();
  return true;
}

bool loadArchiveManifest(IOtaStorageBackend& storage,
                         char role,
                         std::vector<OtaArchiveEntryLocal>& out_entries,
                         std::string& out_error) {
  out_entries.clear();
  const std::string manifest_path = archiveManifestPath(role);
  OtaStorageStat st{};
  std::string msg;
  if (!storage.stat(manifest_path, st, msg)) {
    out_error = msg.empty() ? "manifest stat failed" : msg;
    return false;
  }
  if (!st.exists || st.size_bytes == 0U) {
    out_error.clear();
    return true;
  }
  if (st.is_dir) {
    out_error = "manifest path is directory";
    return false;
  }
  std::string text;
  if (!readTextFile(storage, manifest_path, text, out_error)) {
    if (out_error == "file missing" || out_error == "empty file") {
      out_error.clear();
      return true;
    }
    return false;
  }

  size_t pos = 0U;
  while (true) {
    pos = text.find("{\"id\":\"", pos);
    if (pos == std::string::npos) break;
    const size_t end = text.find('}', pos);
    if (end == std::string::npos || end <= pos) break;
    const std::string obj = text.substr(pos, end - pos + 1U);
    OtaArchiveEntryLocal e{};
    if (!extractJsonStringField(obj, "id", e.id) ||
        !extractJsonStringField(obj, "bin", e.bin_name) ||
        !extractJsonStringField(obj, "meta", e.meta_name) ||
        !extractJsonStringField(obj, "sw", e.sw_version) ||
        !extractJsonStringField(obj, "build", e.build_id) ||
        !extractJsonStringField(obj, "target", e.target_role) ||
        !extractJsonStringField(obj, "source", e.source) ||
        !extractJsonU32Field(obj, "size", e.size_bytes) ||
        !extractJsonU32Field(obj, "crc", e.crc32) ||
        !extractJsonU32Field(obj, "ts", e.created_epoch_s)) {
      out_error = "manifest entry missing required fields";
      return false;
    }
    if (e.id.empty() ||
        e.bin_name.empty() ||
        e.meta_name.empty() ||
        e.sw_version.empty() ||
        e.build_id.empty() ||
        e.target_role.empty() ||
        e.source.empty()) {
      out_error = "manifest entry has empty required fields";
      return false;
    }
    if (e.size_bytes == 0U) {
      out_error = "manifest entry invalid size";
      return false;
    }
    e.target_role = management_utils::lowerAscii(management_utils::trim(e.target_role));
    if (e.target_role != "master" && e.target_role != "slave") {
      out_error = "manifest entry invalid target role";
      return false;
    }
    out_entries.push_back(e);
    pos = end + 1U;
  }
  out_error.clear();
  return true;
}

bool saveArchiveManifest(IOtaStorageBackend& storage,
                         char role,
                         const std::vector<OtaArchiveEntryLocal>& entries,
                         std::string& out_error) {
  std::string json = "{\"ver\":1,\"role\":\"";
  json.push_back((role == 's') ? 's' : 'm');
  json += "\",\"entries\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i > 0U) json += ",";
    const auto& e = entries[i];
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
  return writeTextFile(storage, archiveManifestPath(role), json, out_error);
}

void appendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
void appendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendU64(std::vector<uint8_t>& out, uint64_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
}

void appendStringU8(std::vector<uint8_t>& out, const std::string& s) {
  const size_t n = std::min<size_t>(s.size(), 255);
  out.push_back(static_cast<uint8_t>(n));
  out.insert(out.end(), s.begin(), s.begin() + static_cast<std::string::difference_type>(n));
}

bool isPushMutation(ManagementCommandId cmd) {
  return cmd == ManagementCommandId::PushStart ||
         cmd == ManagementCommandId::PushUpdate ||
         cmd == ManagementCommandId::PushPause ||
         cmd == ManagementCommandId::PushResume ||
         cmd == ManagementCommandId::PushStop;
}

bool isDescriptorMutationCommand(ManagementCommandId cmd) {
  return cmd == ManagementCommandId::SettingSet ||
         cmd == ManagementCommandId::LogRemoteClear ||
         cmd == ManagementCommandId::LogRemoteControlSet ||
         cmd == ManagementCommandId::StorageFormat ||
         cmd == ManagementCommandId::OtaManifestRebuild ||
         cmd == ManagementCommandId::OtaClearScope ||
         cmd == ManagementCommandId::OtaArchiveSaveRunning ||
         cmd == ManagementCommandId::OtaArchiveSaveStaged ||
         cmd == ManagementCommandId::OtaArchiveRestore ||
         cmd == ManagementCommandId::OtaArchiveDelete ||
         cmd == ManagementCommandId::OtaArchiveClear ||
         cmd == ManagementCommandId::OtaApply ||
         cmd == ManagementCommandId::OtaRollback ||
         cmd == ManagementCommandId::TopologyStageSet ||
         cmd == ManagementCommandId::TopologyCommit ||
         cmd == ManagementCommandId::CliControlSet ||
         cmd == ManagementCommandId::ChainLoopControlSet;
}

bool hasRequiredAccess(ManagementAccessLevel actual, ManagementAccessLevel required) {
  return static_cast<uint8_t>(actual) >= static_cast<uint8_t>(required);
}

ManagementAccessLevel requiredAccessLevel(ManagementCommandId cmd) {
  switch (cmd) {
    case ManagementCommandId::StatusGet:
    case ManagementCommandId::TopologyStatusGet:
    case ManagementCommandId::TopologySlotsGet:
    case ManagementCommandId::DiscoverySnapshotGet:
    case ManagementCommandId::PairedSnapshotGet:
    case ManagementCommandId::DescGet:
    case ManagementCommandId::CapsGet:
    case ManagementCommandId::CapsPageGet:
    case ManagementCommandId::NodeBundleGet:
    case ManagementCommandId::SettingsGet:
    case ManagementCommandId::SettingsPageGet:
    case ManagementCommandId::SettingGet:
    case ManagementCommandId::TelemSchemaGet:
    case ManagementCommandId::TelemSchemaPageGet:
    case ManagementCommandId::TelemPull:
    case ManagementCommandId::LiveGet:
    case ManagementCommandId::LiveMonitorStatusGet:
    case ManagementCommandId::PingGet:
    case ManagementCommandId::TimeGet:
    case ManagementCommandId::PushGet:
    case ManagementCommandId::LogLocalStatusGet:
    case ManagementCommandId::LogLocalRead:
    case ManagementCommandId::LogRemoteStatusGet:
    case ManagementCommandId::LogRemoteRead:
    case ManagementCommandId::ChannelRuntimeGet:
    case ManagementCommandId::StorageInfoGet:
    case ManagementCommandId::StorageList:
    case ManagementCommandId::StorageStat:
    case ManagementCommandId::OtaStatusGet:
    case ManagementCommandId::OtaManifestGet:
    case ManagementCommandId::OtaManifestPageGet:
    case ManagementCommandId::OtaCapacityGet:
    case ManagementCommandId::OtaGateGet:
    case ManagementCommandId::OtaPushStatus:
    case ManagementCommandId::OtaArchiveList:
    case ManagementCommandId::OtaArchiveVerify:
    case ManagementCommandId::CommTestStatus:
    case ManagementCommandId::CommTestReport:
    case ManagementCommandId::MetricsGet:
    case ManagementCommandId::QueueGet:
      return ManagementAccessLevel::Observer;

    case ManagementCommandId::DiscoveryStart:
    case ManagementCommandId::DiscoveryStop:
    case ManagementCommandId::PairRequest:
    case ManagementCommandId::UnpairRequest:
    case ManagementCommandId::TimeSet:
    case ManagementCommandId::SettingSet:
    case ManagementCommandId::PushStart:
    case ManagementCommandId::PushUpdate:
    case ManagementCommandId::PushPause:
    case ManagementCommandId::PushResume:
    case ManagementCommandId::PushStop:
    case ManagementCommandId::TopologyTriggerSend:
    case ManagementCommandId::AudioPingRequest:
    case ManagementCommandId::ChannelSyncAll:
    case ManagementCommandId::ChainLoopControlSet:
      return ManagementAccessLevel::Operator;

    case ManagementCommandId::LogLocalClear:
    case ManagementCommandId::LogLocalControlSet:
    case ManagementCommandId::LogRemoteClear:
    case ManagementCommandId::LogRemoteControlSet:
    case ManagementCommandId::StorageFormat:
    case ManagementCommandId::OtaManifestRebuild:
    case ManagementCommandId::OtaClearScope:
    case ManagementCommandId::OtaApply:
    case ManagementCommandId::OtaRollback:
    case ManagementCommandId::OtaTransferBegin:
    case ManagementCommandId::OtaTransferChunk:
    case ManagementCommandId::OtaTransferEnd:
    case ManagementCommandId::OtaTransferAbort:
    case ManagementCommandId::OtaPushStart:
    case ManagementCommandId::OtaPushAbort:
    case ManagementCommandId::OtaUpdateStart:
    case ManagementCommandId::OtaArchiveSaveRunning:
    case ManagementCommandId::OtaArchiveSaveStaged:
    case ManagementCommandId::OtaArchiveRestore:
    case ManagementCommandId::OtaArchiveDelete:
    case ManagementCommandId::OtaArchiveClear:
    case ManagementCommandId::OtaMasterUpdateStart:
    case ManagementCommandId::TopologyStageSet:
    case ManagementCommandId::TopologyCommit:
    case ManagementCommandId::CommTestRun:
    case ManagementCommandId::MetricsReset:
      return ManagementAccessLevel::Maintainer;

    case ManagementCommandId::RemovePeerRequest:
    case ManagementCommandId::RestartSlaveRequest:
    case ManagementCommandId::ResetSlaveRequest:
    case ManagementCommandId::RestartMasterRequest:
    case ManagementCommandId::ResetMasterRequest:
    case ManagementCommandId::CliControlSet:
    case ManagementCommandId::LiveMonitorEnable:
    case ManagementCommandId::LiveMonitorDisable:
      return ManagementAccessLevel::Owner;

    default:
      return ManagementAccessLevel::Owner;
  }
}

bool buildLoggerStatusPayload(LibraryLogger* logger, std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  LogStorageStats stats{};
  const bool stats_ok = (logger != nullptr) && logger->stats(stats);
  const bool available = stats_ok && stats.available;
  appendU8(out_payload, available ? 1U : 0U);
  appendU8(out_payload, (logger != nullptr && logger->enabled()) ? 1U : 0U);
  appendU8(out_payload, (logger != nullptr) ? static_cast<uint8_t>(logger->minLevel()) : 0U);
  appendU8(out_payload, 0U);  // reserved
  appendU32(out_payload, stats.bytes_used);
  appendU32(out_payload, stats.bytes_dropped);
  appendU32(out_payload, stats.records_appended);
  appendU32(out_payload, stats.rotations);
  return true;
}

void buildLoggerReadPayload(uint32_t offset,
                            uint32_t total_size,
                            const uint8_t* chunk,
                            uint16_t chunk_len,
                            std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  appendU32(out_payload, offset);
  appendU32(out_payload, total_size);
  appendU16(out_payload, chunk_len);
  if (chunk != nullptr && chunk_len > 0U) {
    out_payload.insert(out_payload.end(), chunk, chunk + static_cast<size_t>(chunk_len));
  }
}

void emitManagementLog(LibraryLogger* logger,
                       LibraryLogLevel level,
                       uint16_t event_id,
                       uint32_t now_ms,
                       const ManagementRequest& request,
                       ManagementStatus status) {
  if (logger == nullptr) return;
  LibraryLogRecord rec{};
  rec.level = level;
  rec.source_id = kLogSourceManagement;
  rec.event_id = event_id;
  rec.uptime_ms = now_ms;
  rec.p0 = static_cast<int32_t>(request.req_id);
  rec.p1 = static_cast<int32_t>(request.cmd_id);
  rec.p2 = static_cast<int32_t>(status);
  rec.ext.push_back(static_cast<uint8_t>(request.source));
  (void)logger->log(rec);
}

}  // namespace

bool ManagementService::executeRequest(const ManagementRequest& request) {
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(request.cmd_id);
  auto topologyErrorToStatus = [](const std::string& error) {
    const std::string e = management_utils::lowerAscii(management_utils::trim(error));
    if (e == "topology_not_staged") return ManagementStatus::TopologyNotStaged;
    if (e == "topology_version_stale") return ManagementStatus::TopologyVersionStale;
    if (e == "topology_apply_failed" ||
        e == "topology stage failed" ||
        e == "topology commit failed" ||
        e == "topology stage not active" ||
        e == "topology_stage_not_active") {
      return ManagementStatus::TopologyApplyFailed;
    }
    if (e == "topology source unauthorized" ||
        e == "topology_source_unauthorized" ||
        e == "source unauthorized") {
      return ManagementStatus::DeniedByRole;
    }
    if (management_utils::startsWith(e, "invalid_") ||
        management_utils::startsWith(e, "invalid ") ||
        e == "group_id_missing" ||
        e == "duplicate_logical_peer" ||
        e == "duplicate_relative_index" ||
        e == "out_of_window" ||
        e == "index_unmapped") {
      return ManagementStatus::BadPayload;
    }
    return ManagementStatus::InternalError;
  };
  if (!hasRequiredAccess(request.access_level, commandRequiredAccessLevel(request.cmd_id))) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::DeniedByRole);
    return true;
  }
  if (radio_transition_active_ && isRadioTransitionBlockedCommand(request.cmd_id)) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BusyRadioTransition);
    return true;
  }
  if (cmd == ManagementCommandId::DiscoveryStart) {
    if (local_role_ == Role::Master && manager_.persistedPairCount() >= kMaxPairedSlaves) {
      discovery_active_ = false;
      manager_.setDiscoveryRxEnabled(false);
      discovered_.clear();
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::CapacityLimitReached);
      return true;
    }
    uint32_t window_ms = kDefaultDiscoveryWindowMs;
    if (!request.payload.empty() && !readU32Le(request.payload.data(), request.payload.size(), window_ms)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    discovery_window_ms_ = window_ms;
    discovery_deadline_ms_ = now_ms_ + window_ms;
    discovery_active_ = true;
    manager_.setDiscoveryRxEnabled(true);
    discovered_.clear();
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok);
    queueEvent({ManagementEventId::DiscoveryStarted, request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, {}});
    return true;
  }
  if (cmd == ManagementCommandId::DiscoveryStop) {
    discovery_active_ = false;
    manager_.setDiscoveryRxEnabled(false);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok);
    queueEvent({ManagementEventId::DiscoveryStopped, request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, {}});
    return true;
  }
  if (cmd == ManagementCommandId::DiscoverySnapshotGet) {
    std::vector<uint8_t> payload;
    buildDiscoverySnapshotPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    queueEvent({ManagementEventId::DiscoverySnapshot, request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, {}});
    return true;
  }
  if (cmd == ManagementCommandId::PairedSnapshotGet) {
    std::vector<uint8_t> payload;
    buildPairedSnapshotPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::StatusGet) {
    std::vector<uint8_t> payload;
    buildStatusPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::TopologyStatusGet) {
    if (local_role_ == Role::Master && request.has_target_peer) {
      return runDescriptorPull(request, request.cmd_id);
    }
    std::vector<uint8_t> payload;
    buildTopologyStatusPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::TopologySlotsGet) {
    bool committed = true;
    if (!management_utils::parseTopologySlotsGetPayload(request.payload, committed)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    PeerResolveContext peer_ctx{};
    PeerResolveContext* peer_ctx_ptr = nullptr;
    if (local_role_ == Role::Master && request.has_target_peer) {
      MacAddress peer{};
      if (!requirePairedPeer(request, peer, &peer_ctx)) {
        return true;
      }
      peer_ctx_ptr = &peer_ctx;
    }
    std::vector<uint8_t> payload;
    if (!buildTopologySlotsPayload(committed, payload)) {
      queueResponse(request.source,
                    request.cmd_id,
                    request.req_id,
                    committed ? ManagementStatus::TopologyApplyFailed
                              : ManagementStatus::TopologyNotStaged,
                    {},
                    peer_ctx_ptr);
      return true;
    }
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload, peer_ctx_ptr);
    return true;
  }
  if (cmd == ManagementCommandId::TopologyStageSet) {
    if (local_role_ == Role::Master && request.has_target_peer) {
      return runDescriptorPull(request, request.cmd_id);
    }
    ManagementTopologySnapshotPayload stage_payload{};
    if (!management_utils::parseTopologyStagePayload(request.payload, stage_payload)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    EspNowManager::TopologySnapshot snapshot{};
    snapshot.schema_version = stage_payload.schema_version;
    snapshot.state = EspNowManager::TopologyState::Staged;
    snapshot.topology_version = stage_payload.topology_version;
    snapshot.index_neg = stage_payload.index_neg;
    snapshot.index_pos = stage_payload.index_pos;
    for (const auto& slot : stage_payload.slots) {
      if (slot.slot_index >= EspNowManager::kTopologyMaxSlots) {
        queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
        return true;
      }
      auto& s = snapshot.slots[slot.slot_index];
      s.enabled = slot.enabled;
      s.peer = slot.peer;
      s.peer_role = slot.peer_role;
      s.group_id = slot.group_id;
      s.relative_index = slot.relative_index;
      s.local_virtual_index = slot.local_virtual_index;
      s.peer_virtual_index = slot.peer_virtual_index;
      s.axis_order = slot.axis_order;
      s.delay_ms = slot.delay_ms;
      s.hold_ms = slot.hold_ms;
    }
    for (const auto& group : stage_payload.groups) {
      if (group.group_slot >= EspNowManager::kTopologyMaxGroups) {
        queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
        return true;
      }
      auto& g = snapshot.groups[group.group_slot];
      g.enabled = group.enabled;
      g.group_id = group.group_id;
      g.seed = group.seed;
    }

    std::string stage_error{};
    const bool staged = manager_.stageTopology(snapshot, &stage_error);
    if (!staged) {
      queueResponse(request.source,
                    request.cmd_id,
                    request.req_id,
                    topologyErrorToStatus(stage_error));
      return true;
    }
    std::vector<uint8_t> payload;
    buildTopologyStatusPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    queueEvent({ManagementEventId::TopologyStaged,
                request.source,
                request.cmd_id,
                request.req_id,
                ManagementStatus::Ok,
                payload});
    return true;
  }
  if (cmd == ManagementCommandId::TopologyCommit) {
    if (local_role_ == Role::Master && request.has_target_peer) {
      return runDescriptorPull(request, request.cmd_id);
    }
    std::string commit_error{};
    const bool committed = manager_.commitStagedTopology(&commit_error);
    if (!committed) {
      const ManagementStatus status = topologyErrorToStatus(commit_error);
      queueResponse(request.source, request.cmd_id, request.req_id, status);
      queueEvent({ManagementEventId::TopologyCommitFailed,
                  request.source,
                  request.cmd_id,
                  request.req_id,
                  status,
                  {}});
      return true;
    }
    std::vector<uint8_t> payload;
    buildTopologyStatusPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    queueEvent({ManagementEventId::TopologyCommitted,
                request.source,
                request.cmd_id,
                request.req_id,
                ManagementStatus::Ok,
                payload});
    if (local_role_ == Role::Master && !request.has_target_peer) {
      uint32_t queued_peers = 0U;
      uint32_t failed_peers = 0U;
      if (queueTopologyDeployForCommitted(request.req_id, queued_peers, failed_peers)) {
        std::vector<uint8_t> deploy_payload{};
        appendU32(deploy_payload, queued_peers);
        appendU32(deploy_payload, failed_peers);
        queueEvent({ManagementEventId::CmdDone,
                    request.source,
                    request.cmd_id,
                    request.req_id,
                    (failed_peers == 0U) ? ManagementStatus::Ok : ManagementStatus::InternalError,
                    deploy_payload});
      }
    }
    return true;
  }
  if (cmd == ManagementCommandId::TopologyTriggerSend) {
    if (local_role_ == Role::Master && request.has_target_peer) {
      return runDescriptorPull(request, request.cmd_id);
    }
    if (local_role_ != Role::Slave) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::DeniedByRole);
      return true;
    }
    ManagementTopologyTriggerSendPayload trigger_payload{};
    if (!management_utils::parseTopologyTriggerSendPayload(request.payload, trigger_payload)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    EspNowManager::TopologyTriggerRequest trigger{};
    trigger.target_index = trigger_payload.target_index;
    trigger.direction = trigger_payload.direction;
    trigger.delay_ms = trigger_payload.delay_ms;
    trigger.hold_ms = trigger_payload.hold_ms;
    trigger.source_virtual_index = trigger_payload.source_virtual_index;
    uint16_t seq = 0U;
    std::string trigger_error{};
    if (!manager_.sendTopologyTrigger(trigger, request.req_id, &seq, &trigger_error)) {
      ManagementStatus status = ManagementStatus::InternalError;
      if (trigger_error == "topology_missing") {
        status = ManagementStatus::TopologyApplyFailed;
      } else if (management_utils::startsWith(trigger_error, "invalid_") ||
                 trigger_error == "out_of_window" ||
                 trigger_error == "index_unmapped") {
        status = ManagementStatus::BadPayload;
      }
      queueResponse(request.source, request.cmd_id, request.req_id, status);
      return true;
    }
    std::vector<uint8_t> payload{};
    appendU16(payload, seq);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::QueueGet) {
    std::vector<uint8_t> payload;
    buildQueuePayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::MetricsGet) {
    std::vector<uint8_t> payload;
    buildMetricsPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::MetricsReset) {
    manager_.resetRuntimeMetrics();
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok);
    return true;
  }
  if (cmd == ManagementCommandId::CommTestStatus || cmd == ManagementCommandId::CommTestReport) {
    std::vector<uint8_t> payload;
    buildStatusPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::LogLocalStatusGet) {
    std::vector<uint8_t> payload;
    buildLoggerStatusPayload(manager_.logger(), payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::LogLocalRead) {
    LogReadArgs args{};
    if (!parseLogReadArgs(request.payload, args)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    LibraryLogger* logger = manager_.logger();
    if (logger == nullptr) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
      return true;
    }
    std::vector<uint8_t> chunk(args.max_bytes, 0);
    size_t out_len = 0;
    uint32_t total_size = 0;
    if (!logger->readChunk(args.offset, chunk.data(), chunk.size(), out_len, total_size)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    if (out_len > chunk.size()) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    std::vector<uint8_t> payload;
    buildLoggerReadPayload(args.offset,
                           total_size,
                           chunk.data(),
                           static_cast<uint16_t>(out_len),
                           payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::LogLocalClear) {
    if (device_policy_ != nullptr) {
      DeviceCommandContext ctx{};
      (void)makeDeviceContext(request, ctx);
      const DevicePolicyDecision decision = device_policy_->authorizeCriticalCommand(ctx);
      if (decision.code != DevicePolicyCode::AllowDeferred) {
        queueResponse(request.source, request.cmd_id, request.req_id, statusFromPolicy(decision.code));
        return true;
      }
    }
    LibraryLogger* logger = manager_.logger();
    if (logger == nullptr) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
      return true;
    }
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  logger->clear() ? ManagementStatus::Ok : ManagementStatus::InternalError);
    return true;
  }
  if (cmd == ManagementCommandId::LogLocalControlSet) {
    LogControlArgs args{};
    if (!parseLogControlArgs(request.payload, args)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    if (device_policy_ != nullptr) {
      DeviceCommandContext ctx{};
      (void)makeDeviceContext(request, ctx);
      const DevicePolicyDecision decision = device_policy_->authorizeCriticalCommand(ctx);
      if (decision.code != DevicePolicyCode::AllowDeferred) {
        queueResponse(request.source, request.cmd_id, request.req_id, statusFromPolicy(decision.code));
        return true;
      }
    }
    LibraryLogger* logger = manager_.logger();
    if (logger == nullptr) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
      return true;
    }
    logger->setEnabled(args.enabled);
    if (!manager_.persistLoggerConfig()) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok);
    return true;
  }
  if (cmd == ManagementCommandId::ChannelRuntimeGet) {
    std::vector<uint8_t> payload{};
    if (!buildRuntimeChannelPayload(payload)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }
  if (cmd == ManagementCommandId::ChannelSyncAll) {
    if (local_role_ != Role::Master || request.has_target_peer) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
      return true;
    }
    if (channel_sync_all_.active) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    ChannelSyncAllArgs args{};
    if (!parseChannelSyncAllArgs(request.payload, args)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    if (!startChannelSyncAll(request.source, request.req_id, args.channel)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    std::vector<uint8_t> payload{};
    appendU8(payload, args.channel);
    appendU8(payload, static_cast<uint8_t>(std::min<size_t>(channel_sync_all_.peers.size(), 255U)));
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::OkDeferred, payload);
    return true;
  }
  if (cmd == ManagementCommandId::ChainLoopControlSet) {
    if (local_role_ != Role::Master || request.has_target_peer) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
      return true;
    }
    ChainLoopControlArgs args{};
    if (!parseChainLoopControlArgs(request.payload, args)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    if (!args.has_value) {
      std::vector<uint8_t> payload{};
      appendU8(payload, chain_loop_enabled_ ? 1U : 0U);
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
      return true;
    }
    if (chain_loop_all_.active) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    if (!startChainLoopAll(request.source, request.req_id, args.enabled)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
      return true;
    }
    std::vector<uint8_t> payload{};
    appendU8(payload, args.enabled ? 1U : 0U);
    appendU8(payload, static_cast<uint8_t>(std::min<size_t>(chain_loop_all_.peers.size(), 255U)));
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::OkDeferred, payload);
    return true;
  }
  if (cmd == ManagementCommandId::CliControlSet) {
    if (local_role_ != Role::Master || master_cli_ == nullptr) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
      return true;
    }
    bool enabled = master_cli_->cliEnabled();
    if (!request.payload.empty()) {
      if (request.payload.size() != 1U || request.payload[0] > 1U) {
        queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
        return true;
      }
      enabled = (request.payload[0] != 0U);
      if (!master_cli_->setCliEnabled(enabled, true)) {
        queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
        return true;
      }
    }
    std::vector<uint8_t> payload{};
    payload.push_back(enabled ? 1U : 0U);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }

  if (local_role_ != Role::Master) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }

  if (cmd == ManagementCommandId::LiveMonitorEnable) {
    live_monitor_.enabled = true;
    syncLiveMonitorPeers();
    persistLiveMonitorConfig();
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok);
    return true;
  }
  if (cmd == ManagementCommandId::LiveMonitorDisable) {
    live_monitor_.enabled = false;
    live_monitor_.next_probe_due_ms = 0U;
    for (auto& peer : live_monitor_.peers) {
      peer.probe_pending = false;
      peer.probe_sent_ms = 0U;
      peer.probe_fail_count = 0U;
    }
    persistLiveMonitorConfig();
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok);
    return true;
  }
  if (cmd == ManagementCommandId::LiveMonitorStatusGet) {
    std::vector<uint8_t> payload;
    buildLiveMonitorStatusPayload(payload);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }

  if (cmd == ManagementCommandId::PairRequest) {
    if (request.payload.size() != 6) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    MacAddress peer{};
    std::memcpy(peer.data(), request.payload.data(), 6);

    PeerResolveContext peer_ctx{};
    peer_ctx.has_requested_peer = true;
    peer_ctx.requested_peer = peer;

    if (manager_.hasPersistedPair(peer)) {
      peer_ctx.has_executed_peer = true;
      peer_ctx.executed_peer = peer;
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, {}, &peer_ctx);
      return true;
    }

    if (manager_.persistedPairCount() >= kMaxPairedSlaves) {
      queueResponse(request.source,
                    request.cmd_id,
                    request.req_id,
                    ManagementStatus::CapacityLimitReached,
                    {},
                    &peer_ctx);
      return true;
    }

    const bool started = manager_.requestPair(peer, request.req_id);
    if (!started) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError, {}, &peer_ctx);
      return true;
    }

    peer_ctx.has_executed_peer = true;
    peer_ctx.executed_peer = peer;
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::OkDeferred, {}, &peer_ctx);
    registerDeferredLifecycleCommand(request.req_id, request.cmd_id, request.source);
    return true;
  }
  if (cmd == ManagementCommandId::UnpairRequest) {
    MacAddress peer{};
    PeerResolveContext peer_ctx{};
    if (!requirePairedPeer(request, peer, &peer_ctx)) return true;
    const bool started = manager_.requestUnpair(peer, request.req_id);
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  started ? ManagementStatus::OkDeferred : ManagementStatus::InternalError,
                  {},
                  &peer_ctx);
    if (started) {
      registerDeferredLifecycleCommand(request.req_id, request.cmd_id, request.source);
    }
    return true;
  }
  if (cmd == ManagementCommandId::RemovePeerRequest) {
    RemovePeerArgs args{};
    if (!parseRemovePeerArgs(request.payload, args)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
    ManagementRequest target_request = request;
    if (args.has_peer) {
      if (target_request.has_target_peer && target_request.target_peer != args.peer) {
        queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
        return true;
      }
      target_request.has_target_peer = true;
      target_request.target_peer = args.peer;
    }
    MacAddress target{};
    PeerResolveContext peer_ctx{};
    if (!requirePairedPeer(target_request, target, &peer_ctx)) {
      return true;
    }
    if (device_policy_ != nullptr) {
      DeviceCommandContext ctx{};
      (void)makeDeviceContext(request, ctx);
      const DevicePolicyDecision decision = device_policy_->authorizeCriticalCommand(ctx);
      if (decision.code != DevicePolicyCode::AllowDeferred) {
        queueResponse(request.source, request.cmd_id, request.req_id, statusFromPolicy(decision.code));
        return true;
      }
    }
    const bool removed = manager_.removePeer(target, true);
    if (removed) {
      removeLivePeerState(target);
    }
    std::vector<uint8_t> payload(target.begin(), target.end());
    payload.push_back(removed ? 1U : 0U);
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  removed ? ManagementStatus::Ok : ManagementStatus::InternalError,
                  payload,
                  &peer_ctx);
    ManagementEvent removed_event{ManagementEventId::PeerRemoved,
                                  request.source,
                                  request.cmd_id,
                                  request.req_id,
                                  removed ? ManagementStatus::Ok : ManagementStatus::InternalError,
                                  payload};
    applyPeerContext(removed_event, peer_ctx);
    queueEvent(std::move(removed_event));
    return true;
  }
  if (cmd == ManagementCommandId::CommTestRun) {
    if (pull_ == nullptr) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
      return true;
    }
    MacAddress peer{};
    PeerResolveContext peer_ctx{};
    if (!requirePairedPeer(request, peer, &peer_ctx)) return true;
    uint32_t corr = request.req_id;
    bool sent = true;
    sent = sent && pull_->requestDevice(peer, corr++);
    sent = sent && pull_->requestCapabilities(peer, corr++);
    sent = sent && pull_->requestSettings(peer, corr++);
    sent = sent && pull_->requestTelemetrySchema(peer, corr++);
    sent = sent && pull_->requestTelemetryPull(peer, corr++);
    sent = sent && pull_->requestLiveness(peer, corr++);
    sent = sent && pull_->requestTimeGet(peer, corr++);
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  sent ? ManagementStatus::OkDeferred : ManagementStatus::InternalError,
                  {},
                  &peer_ctx);
    return true;
  }
  if (cmd == ManagementCommandId::RestartMasterRequest || cmd == ManagementCommandId::ResetMasterRequest) {
    return runMasterCritical(request);
  }
  if (cmd == ManagementCommandId::RestartSlaveRequest) return runSlaveCritical(request, kControlCmdRestart);
  if (cmd == ManagementCommandId::ResetSlaveRequest) return runSlaveCritical(request, kControlCmdReset);
  if (cmd == ManagementCommandId::AudioPingRequest) return runSlaveCritical(request, kControlCmdAudioPing);
  if (cmd == ManagementCommandId::PushStart || cmd == ManagementCommandId::PushUpdate ||
      cmd == ManagementCommandId::PushPause || cmd == ManagementCommandId::PushResume ||
      cmd == ManagementCommandId::PushStop || cmd == ManagementCommandId::PushGet) {
    return runPushCommand(request, request.cmd_id);
  }
  if (cmd == ManagementCommandId::OtaTransferBegin ||
      cmd == ManagementCommandId::OtaTransferChunk ||
      cmd == ManagementCommandId::OtaTransferEnd ||
      cmd == ManagementCommandId::OtaTransferAbort) {
    return runOtaTransferCommand(request, request.cmd_id);
  }
  if (cmd == ManagementCommandId::OtaPushStart ||
      cmd == ManagementCommandId::OtaPushAbort ||
      cmd == ManagementCommandId::OtaPushStatus) {
    return runOtaPushLocalCommand(request, request.cmd_id);
  }
  if (cmd == ManagementCommandId::OtaArchiveList ||
      cmd == ManagementCommandId::OtaArchiveSaveRunning ||
      cmd == ManagementCommandId::OtaArchiveSaveStaged ||
      cmd == ManagementCommandId::OtaArchiveRestore ||
      cmd == ManagementCommandId::OtaArchiveDelete ||
      cmd == ManagementCommandId::OtaArchiveClear ||
      cmd == ManagementCommandId::OtaArchiveVerify) {
    return runOtaArchiveCommand(request, request.cmd_id);
  }
  if (cmd == ManagementCommandId::OtaUpdateStart) {
    return runOtaUpdateLocalCommand(request, request.cmd_id);
  }
  if (cmd == ManagementCommandId::OtaMasterUpdateStart) {
    return runMasterCritical(request);
  }
  return runDescriptorPull(request, request.cmd_id);
}


bool ManagementService::runDescriptorPull(const ManagementRequest& request, uint16_t cmd_id) {
  if (pull_ == nullptr) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }
  const ManagementCommandId command = static_cast<ManagementCommandId>(cmd_id);
  const bool should_track_pull_candidate =
      (command == ManagementCommandId::NodeBundleGet ||
       command == ManagementCommandId::TopologyStageSet ||
       command == ManagementCommandId::TopologyStatusGet ||
       command == ManagementCommandId::TopologyCommit ||
       request.source != ManagementSource::Cli);
  MacAddress peer{};
  PeerResolveContext peer_ctx{};
  if (!requirePairedPeer(request, peer, &peer_ctx)) return true;
  auto peerResponse = [&](ManagementStatus status) {
    queueResponse(request.source, request.cmd_id, request.req_id, status, {}, &peer_ctx);
  };
  if (should_track_pull_candidate && request.req_id != 0U) {
    const PendingDescriptorPull* existing =
        findPendingDescriptorPullByCorrelation_(peer, request.req_id);
    if (existing != nullptr) {
      if (command == ManagementCommandId::TopologyCommit &&
          existing->cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyStageSet)) {
        // Allow commit enqueue while stage session is in-flight; commit will
        // be deferred until stage reaches terminal state for this peer.
      } else if (existing->source == request.source && existing->cmd_id == request.cmd_id) {
        // Idempotent retry: keep original in-flight pull and return deferred ack.
        queueResponse(request.source,
                      request.cmd_id,
                      request.req_id,
                      ManagementStatus::OkDeferred,
                      {},
                      &peer_ctx);
      } else {
        // Prevent ambiguous correlation ownership across source/command collisions.
        peerResponse(ManagementStatus::QueueFull);
      }
      return true;
    }
    if (pending_descriptor_pulls_.size() >= kPendingDescriptorPullsMax) {
      peerResponse(ManagementStatus::QueueFull);
      return true;
    }
  }

  bool sent = false;
  bool stage_flow = false;
  uint32_t stage_corr_first = 0U;
  uint32_t stage_corr_last = 0U;
  uint32_t stage_finalize_corr = 0U;
  ManagementTopologySnapshotPayload topology_stage_snapshot{};
  bool has_topology_stage_snapshot = false;
  switch (static_cast<ManagementCommandId>(cmd_id)) {
    case ManagementCommandId::DescGet:
      sent = pull_->requestDevice(peer, request.req_id);
      break;
    case ManagementCommandId::CapsGet:
      sent = pull_->requestCapabilities(peer, request.req_id);
      break;
    case ManagementCommandId::CapsPageGet: {
      PageArgs args{};
      if (!parsePageArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestCapabilitiesPage(peer, args.cursor, args.page_size, request.req_id);
      break;
    }
    case ManagementCommandId::NodeBundleGet: {
      uint8_t bundle_mask = 0x1FU;
      if (!management_utils::parseNodeBundleGetPayload(request.payload, bundle_mask)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestNodeBundle(peer, bundle_mask, request.req_id);
      break;
    }
    case ManagementCommandId::SettingsGet:
      sent = pull_->requestSettings(peer, request.req_id);
      break;
    case ManagementCommandId::SettingsPageGet: {
      PageArgs args{};
      if (!parsePageArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestSettingsPage(peer, args.cursor, args.page_size, request.req_id);
      break;
    }
    case ManagementCommandId::SettingGet: {
      SettingGetArgs args{};
      if (!parseSettingGetArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = args.by_id ? pull_->requestSettingGetById(peer, args.setting_id, request.req_id)
                        : pull_->requestSettingGet(peer, args.key, request.req_id);
      break;
    }
    case ManagementCommandId::SettingSet: {
      SettingSetArgs args{};
      if (!parseSettingSetArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = args.by_id ? pull_->requestSettingSetById(peer, args.setting_id, args.value, request.req_id)
                        : pull_->requestSettingSet(peer, args.key, args.value, request.req_id);
      break;
    }
    case ManagementCommandId::TelemSchemaGet:
      sent = pull_->requestTelemetrySchema(peer, request.req_id);
      break;
    case ManagementCommandId::TelemSchemaPageGet: {
      PageArgs args{};
      if (!parsePageArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestTelemetrySchemaPage(peer, args.cursor, args.page_size, request.req_id);
      break;
    }
    case ManagementCommandId::TelemPull: {
      if (!request.payload.empty()) {
        PageArgs args{};
        if (!parsePageArgs(request.payload, args)) {
          peerResponse(ManagementStatus::BadPayload);
          return true;
        }
        sent = pull_->requestTelemetryPullPage(peer, args.cursor, args.page_size, request.req_id);
      } else {
        sent = pull_->requestTelemetryPull(peer, request.req_id);
      }
      break;
    }
    case ManagementCommandId::LiveGet:
      sent = pull_->requestLiveness(peer, request.req_id);
      break;
    case ManagementCommandId::PingGet:
      sent = pull_->requestLiveness(peer, request.req_id);
      break;
    case ManagementCommandId::TimeGet:
      sent = pull_->requestTimeGet(peer, request.req_id);
      break;
    case ManagementCommandId::TimeSet: {
      uint64_t epoch_s = 0;
      if (!readU64Le(request.payload.data(), request.payload.size(), epoch_s)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestTimeSet(peer, epoch_s, request.req_id);
      break;
    }
    case ManagementCommandId::LogRemoteStatusGet:
      sent = pull_->requestLogStatus(peer, request.req_id);
      break;
    case ManagementCommandId::LogRemoteRead: {
      LogReadArgs args{};
      if (!parseLogReadArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestLogReadChunk(peer, args.offset, args.max_bytes, request.req_id);
      break;
    }
    case ManagementCommandId::LogRemoteClear:
      if (device_policy_ != nullptr) {
        DeviceCommandContext ctx{};
        (void)makeDeviceContext(request, ctx);
        const DevicePolicyDecision decision = device_policy_->authorizeCriticalCommand(ctx);
        if (decision.code != DevicePolicyCode::AllowDeferred) {
          peerResponse(statusFromPolicy(decision.code));
          return true;
        }
      }
      sent = pull_->requestLogClear(peer, request.req_id);
      break;
    case ManagementCommandId::LogRemoteControlSet: {
      LogControlArgs args{};
      if (!parseLogControlArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (device_policy_ != nullptr) {
        DeviceCommandContext ctx{};
        (void)makeDeviceContext(request, ctx);
        const DevicePolicyDecision decision = device_policy_->authorizeCriticalCommand(ctx);
        if (decision.code != DevicePolicyCode::AllowDeferred) {
          peerResponse(statusFromPolicy(decision.code));
          return true;
        }
      }
      sent = pull_->requestLogSetEnabled(peer, args.enabled, request.req_id);
      break;
    }
    case ManagementCommandId::StorageInfoGet:
      sent = pull_->requestStorageInfo(peer, request.req_id);
      break;
    case ManagementCommandId::StorageList: {
      std::string path;
      if (!parseStringU16Payload(request.payload, path)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestStorageList(peer, path, request.req_id);
      break;
    }
    case ManagementCommandId::StorageStat: {
      std::string path;
      if (!parseStringU16Payload(request.payload, path)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestStorageStat(peer, path, request.req_id);
      break;
    }
    case ManagementCommandId::StorageFormat:
      sent = pull_->requestStorageFormat(peer, request.req_id);
      break;
    case ManagementCommandId::OtaStatusGet:
      sent = pull_->requestOtaStatus(peer, request.req_id);
      break;
    case ManagementCommandId::OtaManifestGet:
      sent = pull_->requestOtaManifest(peer, request.req_id);
      break;
    case ManagementCommandId::OtaManifestPageGet: {
      PageArgs args{};
      if (!parsePageArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaManifestPage(peer, args.cursor, args.page_size, request.req_id);
      break;
    }
    case ManagementCommandId::OtaManifestRebuild:
      sent = pull_->requestOtaManifestRebuild(peer, request.req_id);
      break;
    case ManagementCommandId::OtaClearScope: {
      std::string scope;
      if (!parseStringU16Payload(request.payload, scope)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaClearScope(peer, scope, request.req_id);
      break;
    }
    case ManagementCommandId::OtaArchiveList: {
      OtaArchiveArgs args{};
      if (!parseOtaArchiveArgs(request.payload, args, false)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (!args.remote) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, otaArchiveTargetString("list", args), request.req_id);
      break;
    }
    case ManagementCommandId::OtaArchiveSaveRunning: {
      OtaArchiveArgs args{};
      if (!parseOtaArchiveArgs(request.payload, args, false)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (!args.remote) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, otaArchiveTargetString("save", args), request.req_id);
      break;
    }
    case ManagementCommandId::OtaArchiveSaveStaged: {
      OtaArchiveArgs args{};
      if (!parseOtaArchiveArgs(request.payload, args, false)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (!args.remote) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, otaArchiveTargetString("save_staged", args), request.req_id);
      break;
    }
    case ManagementCommandId::OtaArchiveRestore: {
      OtaArchiveArgs args{};
      if (!parseOtaArchiveArgs(request.payload, args, true)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (!args.remote) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, otaArchiveTargetString("restore", args), request.req_id);
      break;
    }
    case ManagementCommandId::OtaArchiveDelete: {
      OtaArchiveArgs args{};
      if (!parseOtaArchiveArgs(request.payload, args, true)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (!args.remote) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, otaArchiveTargetString("delete", args), request.req_id);
      break;
    }
    case ManagementCommandId::OtaArchiveClear: {
      OtaArchiveArgs args{};
      if (!parseOtaArchiveArgs(request.payload, args, false)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (!args.remote) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, otaArchiveTargetString("clear", args), request.req_id);
      break;
    }
    case ManagementCommandId::OtaArchiveVerify: {
      OtaArchiveArgs args{};
      if (!parseOtaArchiveArgs(request.payload, args, true)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      if (!args.remote) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, otaArchiveTargetString("verify", args), request.req_id);
      break;
    }
    case ManagementCommandId::OtaCapacityGet:
      sent = pull_->requestOtaCapacity(peer, request.req_id);
      break;
    case ManagementCommandId::OtaGateGet:
      sent = pull_->requestOtaGateInfo(peer, request.req_id);
      break;
    case ManagementCommandId::OtaApply: {
      std::string target;
      if (!parseStringU16Payload(request.payload, target)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestOtaApply(peer, target, request.req_id);
      break;
    }
    case ManagementCommandId::OtaRollback:
      sent = pull_->requestOtaApply(peer, "rollback", request.req_id);
      break;
    case ManagementCommandId::TopologyStatusGet:
      sent = pull_->requestTopologyStatus(peer, request.req_id);
      break;
    case ManagementCommandId::TopologyStageSet: {
      if (hasPendingTopologyStageForPeer_(peer)) {
        peerResponse(ManagementStatus::QueueFull);
        return true;
      }
      ManagementTopologySnapshotPayload stage_payload{};
      if (!management_utils::parseTopologyStagePayload(request.payload, stage_payload)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      topology_stage_snapshot = stage_payload;
      has_topology_stage_snapshot = true;
      uint32_t corr = allocateInternalCorrelation_();
      if (corr == 0U) {
        corr = (request.req_id == 0U) ? 1U : request.req_id;
      }
      stage_flow = true;
      stage_corr_first = corr;
      // Ack-paced stage flow:
      // send clear first, then advance one command per descriptor ack in onPullResponse().
      sent = pull_->requestTopologyStageClear(peer, corr);
      stage_corr_last = corr;
      stage_finalize_corr = 0U;
      break;
    }
    case ManagementCommandId::TopologyCommit: {
      if (hasPendingTopologyStageForPeer_(peer)) {
        bool duplicate = false;
        for (const auto& pending_commit : deferred_topology_commits_) {
          if (pending_commit.req_id == request.req_id &&
              pending_commit.source == request.source &&
              pending_commit.peer == peer) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          PendingDeferredTopologyCommit deferred{};
          deferred.req_id = request.req_id;
          deferred.cmd_id = request.cmd_id;
          deferred.source = request.source;
          deferred.peer = peer;
          deferred.peer_ctx = peer_ctx;
          uint32_t timeout_ms = request.timeout_ms;
          if (timeout_ms == 0U) {
            timeout_ms = commandTimeoutMs(request.cmd_id);
          }
          if (timeout_ms == 0U) {
            timeout_ms = 1500U;
          }
          deferred.deadline_ms = now_ms_ + timeout_ms;
          deferred_topology_commits_.push_back(std::move(deferred));
        }
        queueResponse(request.source,
                      request.cmd_id,
                      request.req_id,
                      ManagementStatus::OkDeferred,
                      {},
                      &peer_ctx);
        return true;
      }
      sent = pull_->requestTopologyCommit(peer, request.req_id);
      break;
    }
    case ManagementCommandId::TopologyTriggerSend: {
      ManagementTopologyTriggerSendPayload trigger_payload{};
      if (!management_utils::parseTopologyTriggerSendPayload(request.payload, trigger_payload)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->requestTopologyTriggerSend(peer,
                                               trigger_payload.target_index,
                                               trigger_payload.direction,
                                               trigger_payload.delay_ms,
                                               trigger_payload.hold_ms,
                                               trigger_payload.source_virtual_index,
                                               request.req_id);
      break;
    }
    default:
      peerResponse(ManagementStatus::UnsupportedCommand);
      return true;
  }
  const bool should_track_pull = sent && should_track_pull_candidate;
  if (should_track_pull) {
    const uint32_t corr_first = stage_flow ? stage_corr_first : request.req_id;
    const uint32_t corr_last = stage_flow ? stage_corr_last : request.req_id;
    const uint32_t finalize_corr = stage_flow ? stage_finalize_corr : 0U;
    trackPendingDescriptorPull_(request,
                                peer_ctx,
                                peer,
                                corr_first,
                                corr_last,
                                stage_flow,
                                finalize_corr,
                                has_topology_stage_snapshot ? &topology_stage_snapshot : nullptr);
  }
  const bool deferred = isDescriptorMutationCommand(command) || should_track_pull;
  queueResponse(request.source,
                request.cmd_id,
                request.req_id,
                sent ? (deferred ? ManagementStatus::OkDeferred : ManagementStatus::Ok)
                     : ManagementStatus::InternalError,
                {},
                &peer_ctx);
  return true;
}

bool ManagementService::runPushCommand(const ManagementRequest& request, uint16_t cmd_id) {
  MacAddress peer{};
  PeerResolveContext peer_ctx{};
  if (!requirePairedPeer(request, peer, &peer_ctx)) return true;
  TelemetryPushCommand cmd{};
  if (!request.payload.empty()) {
    if (!parseTelemetryPushCommand(request.payload.data(), request.payload.size(), cmd)) {
      queueResponse(request.source,
                    request.cmd_id,
                    request.req_id,
                    ManagementStatus::BadPayload,
                    {},
                    &peer_ctx);
      return true;
    }
  } else {
    const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
    cmd.action = (c == ManagementCommandId::PushStart) ? TelemetryPushAction::Start :
                 (c == ManagementCommandId::PushUpdate) ? TelemetryPushAction::Update :
                 (c == ManagementCommandId::PushPause) ? TelemetryPushAction::Pause :
                 (c == ManagementCommandId::PushResume) ? TelemetryPushAction::Resume :
                 (c == ManagementCommandId::PushStop) ? TelemetryPushAction::Stop : TelemetryPushAction::Get;
  }
  const bool sent = manager_.sendTelemetryPushCommand(peer, cmd, request.req_id);
  const ManagementCommandId command = static_cast<ManagementCommandId>(cmd_id);
  queueResponse(request.source,
                request.cmd_id,
                request.req_id,
                sent ? (isPushMutation(command) ? ManagementStatus::OkDeferred : ManagementStatus::Ok)
                     : ManagementStatus::InternalError,
                {},
                &peer_ctx);
  return true;
}

bool ManagementService::runOtaTransferCommand(const ManagementRequest& request, uint16_t cmd_id) {
  if (pull_ == nullptr) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }
  MacAddress peer{};
  PeerResolveContext peer_ctx{};
  if (!requirePairedPeer(request, peer, &peer_ctx)) return true;
  auto peerResponse = [&](ManagementStatus status) {
    queueResponse(request.source, request.cmd_id, request.req_id, status, {}, &peer_ctx);
  };

  const ManagementCommandId command = static_cast<ManagementCommandId>(cmd_id);
  bool sent = false;
  switch (command) {
    case ManagementCommandId::OtaTransferBegin: {
      OtaTransferBeginArgs args{};
      if (!parseOtaTransferBeginArgs(request.payload, args) ||
          args.total_size == 0U || args.chunk_size == 0U) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->sendFirmwareBegin(peer,
                                      args.total_size,
                                      args.chunk_size,
                                      args.image_crc32,
                                      request.req_id,
                                      args.has_metadata ? &args.metadata : nullptr);
      break;
    }
    case ManagementCommandId::OtaTransferChunk: {
      OtaTransferChunkArgs args{};
      if (!parseOtaTransferChunkArgs(request.payload, args)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->sendFirmwareChunk(peer, args.offset, args.data, args.data_len, request.req_id);
      break;
    }
    case ManagementCommandId::OtaTransferEnd: {
      OtaTransferEndArgs args{};
      if (!parseOtaTransferEndArgs(request.payload, args) || args.total_size == 0U) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      sent = pull_->sendFirmwareEnd(peer, args.total_size, args.image_crc32, request.req_id);
      break;
    }
    case ManagementCommandId::OtaTransferAbort:
      sent = pull_->requestOtaClearScope(peer, "in", request.req_id);
      break;
    default:
      peerResponse(ManagementStatus::UnsupportedCommand);
      return true;
  }
  queueResponse(request.source,
                request.cmd_id,
                request.req_id,
                sent ? ManagementStatus::OkDeferred : ManagementStatus::InternalError,
                {},
                &peer_ctx);
  return true;
}

bool ManagementService::startOtaPushLocalSession(ManagementSource source,
                                                 uint16_t owner_cmd_id,
                                                 uint32_t req_id,
                                                 const MacAddress& peer,
                                                 const std::string& local_path,
                                                 uint16_t chunk_bytes,
                                                 bool emit_cmd_event,
                                                 std::vector<uint8_t>* out_start_payload,
                                                 ManagementStatus* out_status) {
  if (out_status != nullptr) {
    *out_status = ManagementStatus::InternalError;
  }
  if (ota_push_local_.active) {
    return false;
  }
  if (pull_ == nullptr || ota_push_storage_ == nullptr) {
    if (out_status != nullptr) {
      *out_status = ManagementStatus::UnsupportedCommand;
    }
    return false;
  }
  if (local_path.empty() || chunk_bytes < 32U || chunk_bytes > 220U) {
    if (out_status != nullptr) {
      *out_status = ManagementStatus::BadPayload;
    }
    return false;
  }

  OtaStorageStat st{};
  std::string msg;
  if (!ota_push_storage_->stat(local_path, st, msg) || !st.exists || st.is_dir || st.size_bytes == 0U) {
    if (out_status != nullptr) {
      *out_status = ManagementStatus::InternalError;
    }
    return false;
  }

  FirmwareImageMetadata metadata{};
  std::string sidecar_path;
  if (!loadFirmwareMetadataFromSidecar(*ota_push_storage_, local_path, metadata, sidecar_path, msg)) {
    if (out_status != nullptr) {
      *out_status = ManagementStatus::BadPayload;
    }
    return false;
  }
  if (management_utils::lowerAscii(management_utils::trim(metadata.target_role)) != "slave") {
    if (out_status != nullptr) {
      *out_status = ManagementStatus::BadPayload;
    }
    return false;
  }

  uint32_t crc = 0U;
  if (!computeOtaFileCrc(*ota_push_storage_, local_path, st.size_bytes, crc, msg)) {
    if (out_status != nullptr) {
      *out_status = ManagementStatus::InternalError;
    }
    return false;
  }

  if (!pull_->sendFirmwareBegin(peer,
                                st.size_bytes,
                                chunk_bytes,
                                crc,
                                req_id,
                                &metadata)) {
    if (out_status != nullptr) {
      *out_status = ManagementStatus::InternalError;
    }
    return false;
  }

  ota_push_local_ = OtaPushLocalSession{};
  ota_push_local_.active = true;
  ota_push_local_.source = source;
  ota_push_local_.owner_cmd_id = owner_cmd_id;
  ota_push_local_.emit_cmd_event = emit_cmd_event;
  ota_push_local_.peer = peer;
  ota_push_local_.req_id = req_id;
  ota_push_local_.path = local_path;
  ota_push_local_.image_name = otaImageNameFromCorr(req_id);
  ota_push_local_.chunk_bytes = chunk_bytes;
  ota_push_local_.total_size = st.size_bytes;
  ota_push_local_.image_crc32 = crc;
  ota_push_local_.phase = OtaPushLocalSession::Phase::WaitBegin;
  ota_push_local_.started_ms = now_ms_;
  ota_push_local_.last_activity_ms = now_ms_;
  ota_push_local_.last_status_poll_ms = 0U;
  ota_push_local_.next_send_ms = now_ms_;
  ota_push_local_.remote_acked_offset = 0U;
  ota_push_local_.recovery_until_acked_offset = 0U;
  ota_push_local_.window_size_chunks = 16U;
  ota_push_local_.chunk_buf.resize(chunk_bytes);

  if (out_start_payload != nullptr) {
    out_start_payload->clear();
    appendU32(*out_start_payload, req_id);
    appendU32(*out_start_payload, st.size_bytes);
    appendU32(*out_start_payload, crc);
    appendU16(*out_start_payload, chunk_bytes);
    appendStringU8(*out_start_payload, local_path);
  }

  if (out_status != nullptr) {
    *out_status = ManagementStatus::OkDeferred;
  }
  return true;
}

bool ManagementService::runOtaPushLocalCommand(const ManagementRequest& request, uint16_t cmd_id) {
  const ManagementCommandId command = static_cast<ManagementCommandId>(cmd_id);
  if (local_role_ != Role::Master) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }
  if (pull_ == nullptr || ota_push_storage_ == nullptr) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }

  if (command == ManagementCommandId::OtaPushStatus) {
    std::vector<uint8_t> payload;
    appendU8(payload, ota_push_local_.active ? 1U : 0U);
    appendU8(payload, static_cast<uint8_t>(ota_push_local_.phase));
    appendU16(payload, ota_push_local_.chunk_bytes);
    appendU32(payload, ota_push_local_.req_id);
    appendU32(payload, ota_push_local_.next_offset);
    appendU32(payload, ota_push_local_.total_size);
    appendU16(payload, ota_push_local_.chunks_sent);
    appendStringU8(payload, ota_push_local_.image_name);
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok, payload);
    return true;
  }

  if (command == ManagementCommandId::OtaPushAbort) {
    if (ota_update_local_.active) {
      stopOtaUpdateLocal(false,
                         ManagementStatus::InternalError,
                         static_cast<uint16_t>(OtaStatusCode::ApplyRejected),
                         "local ota update aborted");
    }
    if (!ota_push_local_.active) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::Ok);
      return true;
    }
    const bool sent = pull_->requestOtaClearScope(ota_push_local_.peer, "in", request.req_id);
    stopOtaPushLocal(false,
                     sent ? ManagementStatus::Ok : ManagementStatus::InternalError,
                     0U,
                     "local ota push aborted");
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  sent ? ManagementStatus::OkDeferred : ManagementStatus::InternalError);
    return true;
  }

  if (command != ManagementCommandId::OtaPushStart) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }

  if (ota_update_local_.active) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
    return true;
  }

  OtaPushStartArgs args{};
  if (!parseOtaPushStartArgs(request.payload, args)) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
    return true;
  }

  MacAddress peer{};
  PeerResolveContext peer_ctx{};
  if (!requirePairedPeer(request, peer, &peer_ctx)) return true;

  std::vector<uint8_t> payload;
  ManagementStatus start_status = ManagementStatus::InternalError;
  if (!startOtaPushLocalSession(request.source,
                                request.cmd_id,
                                request.req_id,
                                peer,
                                args.local_path,
                                args.chunk_bytes,
                                true,
                                &payload,
                                &start_status)) {
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  start_status,
                  {},
                  &peer_ctx);
    return true;
  }

  queueResponse(request.source,
                request.cmd_id,
                request.req_id,
                ManagementStatus::OkDeferred,
                payload,
                &peer_ctx);
  return true;
}

bool ManagementService::runOtaArchiveCommand(const ManagementRequest& request, uint16_t cmd_id) {
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(cmd_id);
  OtaArchiveArgs args{};
  const bool require_id = (cmd == ManagementCommandId::OtaArchiveRestore ||
                           cmd == ManagementCommandId::OtaArchiveDelete ||
                           cmd == ManagementCommandId::OtaArchiveVerify);
  if (!parseOtaArchiveArgs(request.payload, args, require_id)) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
    return true;
  }

  if (args.remote) {
    return runDescriptorPull(request, cmd_id);
  }

  auto respond = [&](ManagementStatus status, const std::string& message) {
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  status,
                  management_utils::buildStringPayloadU16(message));
  };

  if (ota_push_storage_ == nullptr) {
    respond(ManagementStatus::UnsupportedCommand, "archive unavailable: OTA storage backend not bound");
    return true;
  }
  std::string msg;
  if (!ota_push_storage_->begin(msg)) {
    respond(ManagementStatus::InternalError, std::string("archive storage not ready: ") + msg);
    return true;
  }

  const char requested_role = (args.role == 's') ? 's' : 'm';
  const std::string bucket = archiveBucketPath(requested_role);
  if (!ota_push_storage_->ensureDir(bucket, msg)) {
    respond(ManagementStatus::InternalError, std::string("archive bucket ensure failed: ") + msg);
    return true;
  }

  auto buildListMessage = [&](char role,
                              const std::vector<OtaArchiveEntryLocal>& entries,
                              std::string& out_message) {
    out_message.clear();
    if (entries.empty()) {
      out_message = std::string("role=") + role + " entries=0";
      return;
    }
    out_message = std::string("role=") + role + " entries=" + std::to_string(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      char crc_buf[16] = {0};
      std::snprintf(crc_buf, sizeof(crc_buf), "0x%08lX", static_cast<unsigned long>(e.crc32));
      out_message += "\n";
      out_message += std::to_string(i + 1U);
      out_message += ". id=" + e.id;
      out_message += " size=" + std::to_string(e.size_bytes);
      out_message += " crc=" + std::string(crc_buf);
      out_message += " sw=" + e.sw_version;
      out_message += " build=" + e.build_id;
      out_message += " target=" + e.target_role;
      out_message += " source=" + e.source;
    }
  };

  auto assignUniqueId = [&](char role, std::vector<OtaArchiveEntryLocal>& entries, std::string& out_id) -> bool {
    std::string load_error;
    if (!loadArchiveManifest(*ota_push_storage_, role, entries, load_error)) {
      msg = "manifest load failed: " + load_error;
      return false;
    }
    auto idExists = [&](const std::string& test_id) {
      return std::any_of(entries.begin(), entries.end(), [&](const OtaArchiveEntryLocal& e) {
        return e.id == test_id;
      });
    };
    uint32_t seed = static_cast<uint32_t>(std::time(nullptr)) ^
                    static_cast<uint32_t>(entries.size() * 2654435761UL);
    out_id = archiveFormatId(seed);
    for (uint16_t i = 0; i < 1024U && idExists(out_id); ++i) {
      ++seed;
      out_id = archiveFormatId(seed);
    }
    return true;
  };

  if (cmd == ManagementCommandId::OtaArchiveList) {
    std::vector<OtaArchiveEntryLocal> entries;
    std::string load_error;
    if (!loadArchiveManifest(*ota_push_storage_, requested_role, entries, load_error)) {
      respond(ManagementStatus::InternalError, "manifest load failed: " + load_error);
      return true;
    }
    std::string list_message;
    buildListMessage(requested_role, entries, list_message);
    respond(ManagementStatus::Ok, list_message);
    return true;
  }

  if (cmd == ManagementCommandId::OtaArchiveSaveRunning) {
    const char expected_role = localRoleChar(local_role_);
    if (requested_role != expected_role) {
      respond(ManagementStatus::DeniedByRole, "save running denied: role must match local running firmware");
      return true;
    }

    std::vector<OtaArchiveEntryLocal> entries;
    std::string generated_id;
    if (!assignUniqueId(requested_role, entries, generated_id)) {
      respond(ManagementStatus::InternalError, msg);
      return true;
    }

    OtaArchiveEntryLocal entry{};
    entry.id = generated_id;
    entry.bin_name = generated_id + ".bin";
    entry.meta_name = generated_id + ".jsn";
    entry.target_role = (requested_role == 's') ? "slave" : "master";
    entry.source = "running";
    entry.sw_version = "running";
    entry.build_id = "na";
    entry.created_epoch_s = static_cast<uint32_t>(std::time(nullptr));

    if (!ota_push_storage_->dumpRunningFirmwareToSd(bucket + "/" + entry.bin_name,
                                                    entry.size_bytes,
                                                    entry.crc32,
                                                    msg)) {
      respond(ManagementStatus::InternalError, "running dump failed: " + msg);
      return true;
    }

    if (!writeArchiveMetadataSidecarToSd(*ota_push_storage_, entry, bucket, msg)) {
      respond(ManagementStatus::InternalError, "running metadata write failed: " + msg);
      return true;
    }

    entries.push_back(entry);
    if (!saveArchiveManifest(*ota_push_storage_, requested_role, entries, msg)) {
      respond(ManagementStatus::InternalError, "manifest save failed: " + msg);
      return true;
    }
    respond(ManagementStatus::Ok,
            "saved id=" + entry.id + " role=" + std::string(1U, requested_role) +
                " source=running size=" + std::to_string(entry.size_bytes) +
                " path=" + bucket + "/" + entry.bin_name);
    return true;
  }

  if (cmd == ManagementCommandId::OtaArchiveSaveStaged) {
    std::string stage_bin = std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedBinName;
    OtaStorageStat st{};
    if (!ota_push_storage_->stat(stage_bin, st, msg) || !st.exists || st.is_dir || st.size_bytes == 0U) {
      std::vector<std::string> names;
      if (!ota_push_storage_->listDir(ota_paths::kStaging, names, msg)) {
        respond(ManagementStatus::InternalError, "staging dir unavailable: " + msg);
        return true;
      }
      const auto it = std::find_if(names.begin(), names.end(), [&](const std::string& n) {
        if (n.size() < 5U) return false;
        const std::string ext = management_utils::lowerAscii(n.substr(n.size() - 4U));
        return ext == ".bin";
      });
      if (it == names.end()) {
        respond(ManagementStatus::InternalError, "no staged .bin found in /o/s");
        return true;
      }
      stage_bin = std::string(ota_paths::kStaging) + "/" + *it;
      if (!ota_push_storage_->stat(stage_bin, st, msg) || !st.exists || st.is_dir || st.size_bytes == 0U) {
        respond(ManagementStatus::InternalError, "staged bin missing: " + stage_bin);
        return true;
      }
    }

    FirmwareImageMetadata meta{};
    std::string sidecar_path;
    if (!loadFirmwareMetadataFromSidecar(*ota_push_storage_, stage_bin, meta, sidecar_path, msg)) {
      respond(ManagementStatus::InternalError, "staged metadata invalid: " + msg);
      return true;
    }
    (void)sidecar_path;
    const char meta_role = (meta.target_role == "slave") ? 's' : 'm';

    std::vector<OtaArchiveEntryLocal> entries;
    std::string generated_id;
    if (!assignUniqueId(meta_role, entries, generated_id)) {
      respond(ManagementStatus::InternalError, msg);
      return true;
    }

    OtaArchiveEntryLocal entry{};
    entry.id = generated_id;
    entry.bin_name = generated_id + ".bin";
    entry.meta_name = generated_id + ".jsn";
    entry.target_role = meta.target_role;
    entry.source = "staged";
    entry.sw_version = meta.sw_version;
    entry.build_id = meta.build_id;
    entry.size_bytes = st.size_bytes;
    entry.created_epoch_s = static_cast<uint32_t>(std::time(nullptr));

    if (!computeOtaFileCrc(*ota_push_storage_, stage_bin, st.size_bytes, entry.crc32, msg)) {
      respond(ManagementStatus::InternalError, "staged crc failed: " + msg);
      return true;
    }

    const std::string meta_bucket = archiveBucketPath(meta_role);
    if (!ota_push_storage_->ensureDir(meta_bucket, msg)) {
      respond(ManagementStatus::InternalError, "archive bucket ensure failed: " + msg);
      return true;
    }
    if (!ota_push_storage_->copySpiffsToSd(stage_bin, meta_bucket + "/" + entry.bin_name, msg)) {
      respond(ManagementStatus::InternalError, "staged bin copy failed: " + msg);
      return true;
    }
    if (!writeArchiveMetadataSidecarToSd(*ota_push_storage_, entry, meta_bucket, msg)) {
      respond(ManagementStatus::InternalError, "staged metadata write failed: " + msg);
      return true;
    }
    entries.push_back(entry);
    if (!saveArchiveManifest(*ota_push_storage_, meta_role, entries, msg)) {
      respond(ManagementStatus::InternalError, "manifest save failed: " + msg);
      return true;
    }
    respond(ManagementStatus::Ok,
            "saved id=" + entry.id + " role=" + std::string(1U, meta_role) +
                " source=staged size=" + std::to_string(entry.size_bytes) +
                " path=" + meta_bucket + "/" + entry.bin_name);
    return true;
  }

  const std::string normalized_id = normalizeArchiveId(args.id);
  if (require_id && normalized_id.empty()) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
    return true;
  }

  std::vector<OtaArchiveEntryLocal> entries;
  std::string load_error;
  if (!loadArchiveManifest(*ota_push_storage_, requested_role, entries, load_error)) {
    respond(ManagementStatus::InternalError, "manifest load failed: " + load_error);
    return true;
  }
  auto find_entry = [&](const std::string& id) {
    return std::find_if(entries.begin(), entries.end(), [&](const OtaArchiveEntryLocal& e) {
      return e.id == id;
    });
  };

  if (cmd == ManagementCommandId::OtaArchiveRestore) {
    const auto it = find_entry(normalized_id);
    if (it == entries.end()) {
      respond(ManagementStatus::InternalError, "restore failed: id not found (" + normalized_id + ")");
      return true;
    }
    const std::string stage_bin = std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedBinName;
    const std::string stage_meta = std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedMetaName;
    if (!ota_push_storage_->copySdToSpiffs(bucket + "/" + it->bin_name, stage_bin, msg)) {
      respond(ManagementStatus::InternalError, "restore failed: bin copy failed (" + msg + ")");
      return true;
    }
    if (!restoreArchiveMetadataToStage(*ota_push_storage_,
                                       *it,
                                       bucket,
                                       stage_bin,
                                       stage_meta,
                                       msg)) {
      respond(ManagementStatus::InternalError, "restore failed: metadata ensure failed (" + msg + ")");
      return true;
    }
    respond(ManagementStatus::Ok, "restored id=" + normalized_id + " to /o/s/fw.bin");
    return true;
  }

  if (cmd == ManagementCommandId::OtaArchiveVerify) {
    const auto it = find_entry(normalized_id);
    if (it == entries.end()) {
      respond(ManagementStatus::InternalError, "verify failed: id not found (" + normalized_id + ")");
      return true;
    }
    std::string verify_message;
    if (!verifyArchiveEntryIntegrity(*ota_push_storage_, *it, requested_role, bucket, verify_message)) {
      respond(ManagementStatus::InternalError, verify_message);
      return true;
    }
    respond(ManagementStatus::Ok, verify_message);
    return true;
  }

  if (cmd == ManagementCommandId::OtaArchiveDelete) {
    const auto it = find_entry(normalized_id);
    if (it == entries.end()) {
      respond(ManagementStatus::InternalError, "delete failed: id not found (" + normalized_id + ")");
      return true;
    }
    if (!ota_push_storage_->removePathOnSd(bucket + "/" + it->bin_name, msg)) {
      respond(ManagementStatus::InternalError, "delete failed: bin remove failed (" + msg + ")");
      return true;
    }
    if (!ota_push_storage_->removePathOnSd(bucket + "/" + it->meta_name, msg)) {
      respond(ManagementStatus::InternalError, "delete failed: metadata remove failed (" + msg + ")");
      return true;
    }
    entries.erase(it);
    if (!saveArchiveManifest(*ota_push_storage_, requested_role, entries, msg)) {
      respond(ManagementStatus::InternalError, "manifest save failed: " + msg);
      return true;
    }
    respond(ManagementStatus::Ok, "deleted id=" + normalized_id + " role=" + std::string(1U, requested_role));
    return true;
  }

  if (cmd == ManagementCommandId::OtaArchiveClear) {
    for (const auto& e : entries) {
      if (!ota_push_storage_->removePathOnSd(bucket + "/" + e.bin_name, msg)) {
        respond(ManagementStatus::InternalError, "clear failed: bin remove failed (" + msg + ")");
        return true;
      }
      if (!ota_push_storage_->removePathOnSd(bucket + "/" + e.meta_name, msg)) {
        respond(ManagementStatus::InternalError, "clear failed: metadata remove failed (" + msg + ")");
        return true;
      }
    }
    entries.clear();
    if (!saveArchiveManifest(*ota_push_storage_, requested_role, entries, msg)) {
      respond(ManagementStatus::InternalError, "manifest save failed: " + msg);
      return true;
    }
    respond(ManagementStatus::Ok, "cleared role=" + std::string(1U, requested_role));
    return true;
  }

  queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
  return true;
}

bool ManagementService::runOtaUpdateLocalCommand(const ManagementRequest& request, uint16_t cmd_id) {
  if (static_cast<ManagementCommandId>(cmd_id) != ManagementCommandId::OtaUpdateStart) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }
  if (local_role_ != Role::Master) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }
  if (pull_ == nullptr || ota_push_storage_ == nullptr) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }
  if (ota_update_local_.active || ota_push_local_.active) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::InternalError);
    return true;
  }

  OtaPushStartArgs args{};
  if (!parseOtaPushStartArgs(request.payload, args)) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
    return true;
  }

  MacAddress peer{};
  PeerResolveContext peer_ctx{};
  if (!requirePairedPeer(request, peer, &peer_ctx)) return true;

  // OTA guard: stop unsolicited telemetry stream pressure from target before transfer.
  pauseTelemetryPushForPeerBestEffort(peer, request.req_id);

  if (!pull_->requestOtaClearScope(peer, "in", request.req_id)) {
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  ManagementStatus::InternalError,
                  {},
                  &peer_ctx);
    return true;
  }

  std::vector<uint8_t> payload;
  ManagementStatus start_status = ManagementStatus::InternalError;
  if (!startOtaPushLocalSession(request.source,
                                static_cast<uint16_t>(ManagementCommandId::OtaPushStart),
                                request.req_id,
                                peer,
                                args.local_path,
                                args.chunk_bytes,
                                false,
                                &payload,
                                &start_status)) {
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  start_status,
                  {},
                  &peer_ctx);
    return true;
  }

  ota_update_local_ = OtaUpdateLocalSession{};
  ota_update_local_.active = true;
  ota_update_local_.source = request.source;
  ota_update_local_.peer = peer;
  ota_update_local_.req_id = request.req_id;
  ota_update_local_.local_path = args.local_path;
  ota_update_local_.chunk_bytes = args.chunk_bytes;
  ota_update_local_.image_name = otaImageNameFromCorr(request.req_id);
  ota_update_local_.phase = OtaUpdateLocalSession::Phase::Push;
  ota_update_local_.started_ms = now_ms_;
  ota_update_local_.last_activity_ms = now_ms_;

  queueResponse(request.source,
                request.cmd_id,
                request.req_id,
                ManagementStatus::OkDeferred,
                payload,
                &peer_ctx);
  return true;
}


}  // namespace espnow_link


