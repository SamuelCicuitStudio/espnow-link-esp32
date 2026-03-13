#include "espnow_link/management_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "espnow_link/cli_master.hpp"
#include "espnow_link/management_utils.hpp"
#include "espnow_link/ota_paths.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/ota_types.hpp"

namespace espnow_link {

namespace {

constexpr uint32_t kDefaultDiscoveryWindowMs = 10000;
constexpr size_t kMaxPairedSlaves = 14;
constexpr uint8_t kLiveMonitorMetaSlot = 0x31;
constexpr uint8_t kLiveMonitorMetaVersion = 1;
constexpr uint8_t kChainLoopMetaSlot = 0x32;
constexpr uint8_t kChainLoopMetaVersion = 1;
constexpr uint32_t kLiveOfflineDetectMaxMs = 2500;
constexpr uint32_t kLiveProbeLeadMs = 1500;
constexpr uint32_t kLiveProbeUrgencyWindowMs = 600;
constexpr uint32_t kLiveProbeTimeoutFastMs = 500;
constexpr uint32_t kLiveProbeTimeoutNormalMs = 700;
constexpr uint8_t kLiveProbeBudgetFast = 2;
constexpr uint8_t kLiveProbeBudgetNormal = 1;
constexpr uint16_t kLiveIgnoreReasonOtaPush = 0x0001;
constexpr uint16_t kLiveIgnoreReasonOtaUpdate = 0x0002;
constexpr uint16_t kLiveIgnoreReasonCriticalInflight = 0x0004;
constexpr uint16_t kLiveIgnoreReasonMasterUpdateGuard = 0x0008;
constexpr uint32_t kMasterUpdateLiveGuardMs = 120000U;
constexpr uint8_t kLiveTransitionReasonPassiveRx = 1;
constexpr uint8_t kLiveTransitionReasonProbeSuccess = 2;
constexpr uint8_t kLiveTransitionReasonProbeTimeoutThreshold = 3;
constexpr uint8_t kLiveTransitionReasonResumeRecheckTimeout = 4;
constexpr uint8_t kSettingModeByKey = 0;
constexpr uint8_t kSettingModeById = 1;
constexpr uint16_t kControlCmdRestart = 0x0001;
constexpr uint16_t kControlCmdReset = 0x0002;
constexpr uint16_t kControlCmdAudioPing = 0x0004;
constexpr uint8_t kOtaStatusKindChunkAck = 0x01;
constexpr uint8_t kOtaStatusKindChunkNack = 0x02;
constexpr uint8_t kOtaStatusKindFinalizeOk = 0x03;
constexpr uint8_t kOtaStatusKindFinalizeFail = 0x04;
constexpr uint8_t kOtaStatusKindFinalizeAck = 0x05;
constexpr uint16_t kLogSourceManagement = 0x0301;
constexpr uint16_t kLogEvtCmdRx = 0x0001;
constexpr uint16_t kLogEvtCmdDone = 0x0002;
constexpr uint16_t kLogEvtCmdFail = 0x0003;
constexpr uint16_t kLogEvtTimeout = 0x0004;
constexpr uint16_t kLogEvtQueueFull = 0x0005;
constexpr uint16_t kOtaBootCompleteEventId = 0x7F10;
constexpr uint16_t kOtaTransferReadyEventId = 0x7F11;

bool isZeroMacAddress(const MacAddress& mac) {
  for (uint8_t b : mac) {
    if (b != 0U) {
      return false;
    }
  }
  return true;
}

bool isChainRoleCode(uint8_t role_code) {
  return role_code == static_cast<uint8_t>(kProfileSens & 0xFFU) ||
         role_code == static_cast<uint8_t>(kProfileSemu & 0xFFU) ||
         role_code == static_cast<uint8_t>(kProfileRelay & 0xFFU) ||
         role_code == static_cast<uint8_t>(kProfileRemu & 0xFFU);
}

const char* channelSettingKeyForRole(uint8_t role_code) {
  if (role_code == static_cast<uint8_t>(kProfilePms & 0xFFU) ||
      role_code == static_cast<uint8_t>(kProfileLockAlarm & 0xFFU)) {
    return "chan";
  }
  return "channel";
}

void upsertChannelSyncTarget(std::vector<MacAddress>& peers,
                             std::vector<uint8_t>& role_codes,
                             const MacAddress& peer,
                             uint8_t role_code) {
  for (size_t i = 0; i < peers.size(); ++i) {
    if (peers[i] == peer) {
      if (role_codes[i] == 0U && role_code != 0U) {
        role_codes[i] = role_code;
      }
      return;
    }
  }
  peers.push_back(peer);
  role_codes.push_back(role_code);
}

uint32_t liveProbeTimeoutMs(size_t paired_count) {
  return (paired_count <= 4U) ? kLiveProbeTimeoutFastMs : kLiveProbeTimeoutNormalMs;
}

uint8_t liveProbeBudgetPerPump(size_t paired_count) {
  return (paired_count <= 4U) ? kLiveProbeBudgetFast : kLiveProbeBudgetNormal;
}

uint32_t liveProbeTriggerAgeMs() {
  return (kLiveOfflineDetectMaxMs > kLiveProbeLeadMs) ? (kLiveOfflineDetectMaxMs - kLiveProbeLeadMs) : 0U;
}

uint32_t liveProbeUrgencyAgeMs() {
  return (kLiveOfflineDetectMaxMs > kLiveProbeUrgencyWindowMs) ? (kLiveOfflineDetectMaxMs - kLiveProbeUrgencyWindowMs) : 0U;
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

ManagementService::ManagementService(Role local_role,
                                     EspNowManager& manager,
                                     MasterPullClient* pull_client,
                                     IDeviceManagerPolicy* device_policy,
                                     IDeviceManagerActions* device_actions)
    : local_role_(local_role),
      manager_(manager),
      pull_(pull_client),
      device_policy_(device_policy),
      device_actions_(device_actions) {}

void ManagementService::begin(size_t max_queue_depth) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  max_queue_depth_ = (max_queue_depth == 0) ? 1 : max_queue_depth;
  now_ms_ = 0;
  discovery_active_ = false;
  manager_.setDiscoveryRxEnabled(false);
  discovery_deadline_ms_ = 0;
  discovery_window_ms_ = kDefaultDiscoveryWindowMs;
  live_monitor_ = LivenessMonitorState{};
  live_monitor_critical_inflight_ = false;
  live_monitor_master_update_guard_until_ms_ = 0U;
  channel_sync_all_ = ChannelSyncAllSession{};
  chain_loop_all_ = ChainLoopAllSession{};
  chain_loop_enabled_ = false;
  radio_transition_active_ = false;
  radio_transition_state_ = RadioTransitionState::Idle;
  radio_transition_epoch_ = 0U;
  radio_transition_restore_live_monitor_ = false;
  radio_transition_last_error_ = ManagementStatus::Ok;
  radio_transition_last_error_stage_.clear();
  radio_transition_last_error_message_.clear();
  loadLiveMonitorConfig();
  loadChainLoopConfig();
  syncLiveMonitorPeers();
  clearQueues();
}

bool ManagementService::submit(const ManagementRequest& request) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (request_queue_.size() >= max_queue_depth_) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::QueueFull);
    queueEvent({ManagementEventId::QueueFull, request.source, request.cmd_id, request.req_id, ManagementStatus::QueueFull, {}});
    emitManagementLog(manager_.logger(),
                      LibraryLogLevel::Warn,
                      kLogEvtQueueFull,
                      now_ms_,
                      request,
                      ManagementStatus::QueueFull);
    return false;
  }
  PendingRequest p{};
  p.request = request;
  p.priority = commandPriority(request.cmd_id);
  const uint32_t timeout_ms = (request.timeout_ms == 0) ? commandTimeoutMs(request.cmd_id) : request.timeout_ms;
  p.deadline_ms = now_ms_ + timeout_ms;
  auto insert_it = request_queue_.end();
  for (auto it = request_queue_.begin(); it != request_queue_.end(); ++it) {
    if (p.priority < it->priority) {
      insert_it = it;
      break;
    }
  }
  request_queue_.insert(insert_it, std::move(p));
  emitServiceEvent(ManagementEventId::CmdRx, request, ManagementStatus::Ok);
  emitManagementLog(manager_.logger(),
                    LibraryLogLevel::Info,
                    kLogEvtCmdRx,
                    now_ms_,
                    request,
                    ManagementStatus::Ok);
  return true;
}

void ManagementService::tick(uint32_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  now_ms_ = now_ms;
  if (discovery_active_ && static_cast<int32_t>(now_ms_ - discovery_deadline_ms_) >= 0) {
    discovery_active_ = false;
    manager_.setDiscoveryRxEnabled(false);
    queueEvent({ManagementEventId::DiscoveryFinished, ManagementSource::Unknown, 0, 0, ManagementStatus::Ok, {}});
  }

  if (request_queue_.empty()) {
    live_monitor_critical_inflight_ = false;
    pumpOtaPushLocal();
    pumpOtaUpdateLocal();
    pumpChannelSyncAll();
    pumpChainLoopAll();
    pumpLiveMonitor();
    return;
  }
  const PendingRequest p = request_queue_.front();
  request_queue_.pop_front();
  const bool critical_cmd = isCriticalLiveMonitorCommand(p.request.cmd_id);
  if (critical_cmd) {
    live_monitor_critical_inflight_ = true;
  }
  if (static_cast<int32_t>(now_ms_ - p.deadline_ms) >= 0) {
    queueResponse(p.request.source, p.request.cmd_id, p.request.req_id, ManagementStatus::Timeout);
    emitServiceEvent(ManagementEventId::Timeout, p.request, ManagementStatus::Timeout);
    emitManagementLog(manager_.logger(),
                      LibraryLogLevel::Warn,
                      kLogEvtTimeout,
                      now_ms_,
                      p.request,
                      ManagementStatus::Timeout);
    pumpOtaPushLocal();
    pumpOtaUpdateLocal();
    pumpChannelSyncAll();
    pumpChainLoopAll();
    pumpLiveMonitor();
    if (critical_cmd) {
      live_monitor_critical_inflight_ = false;
    }
    return;
  }

  if (!executeRequest(p.request)) {
    queueResponse(p.request.source, p.request.cmd_id, p.request.req_id, ManagementStatus::InternalError);
    emitServiceEvent(ManagementEventId::CmdFail, p.request, ManagementStatus::InternalError);
    emitManagementLog(manager_.logger(),
                      LibraryLogLevel::Error,
                      kLogEvtCmdFail,
                      now_ms_,
                      p.request,
                      ManagementStatus::InternalError);
    pumpOtaPushLocal();
    pumpOtaUpdateLocal();
    pumpChannelSyncAll();
    pumpChainLoopAll();
    pumpLiveMonitor();
    if (critical_cmd) {
      live_monitor_critical_inflight_ = false;
    }
    return;
  }
  ManagementStatus response_status = ManagementStatus::Ok;
  bool has_response = false;
  for (auto it = response_queue_.rbegin(); it != response_queue_.rend(); ++it) {
    if (it->source == p.request.source &&
        it->cmd_id == p.request.cmd_id &&
        it->req_id == p.request.req_id) {
      response_status = it->status;
      has_response = true;
      break;
    }
  }

  const bool async_terminal = isAsyncTerminalCommand(p.request.cmd_id);
  if (response_status == ManagementStatus::OkDeferred && async_terminal) {
    // Deferred commands publish terminal lifecycle later from async completion path.
  } else if (!has_response || response_status == ManagementStatus::Ok ||
             response_status == ManagementStatus::OkDeferred) {
    emitServiceEvent(ManagementEventId::CmdDone, p.request, ManagementStatus::Ok);
    emitManagementLog(manager_.logger(),
                      LibraryLogLevel::Info,
                      kLogEvtCmdDone,
                      now_ms_,
                      p.request,
                      ManagementStatus::Ok);
  } else if (response_status == ManagementStatus::Timeout) {
    emitServiceEvent(ManagementEventId::Timeout, p.request, ManagementStatus::Timeout);
    emitManagementLog(manager_.logger(),
                      LibraryLogLevel::Warn,
                      kLogEvtTimeout,
                      now_ms_,
                      p.request,
                      ManagementStatus::Timeout);
  } else {
    emitServiceEvent(ManagementEventId::CmdFail, p.request, response_status);
    emitManagementLog(manager_.logger(),
                      LibraryLogLevel::Error,
                      kLogEvtCmdFail,
                      now_ms_,
                      p.request,
                      response_status);
  }
  pumpOtaPushLocal();
  pumpOtaUpdateLocal();
  pumpChannelSyncAll();
  pumpChainLoopAll();
  pumpLiveMonitor();
  if (critical_cmd) {
    live_monitor_critical_inflight_ = false;
  }
}

bool ManagementService::pollResponse(ManagementResponse& out_response) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (response_queue_.empty()) return false;
  out_response = std::move(response_queue_.front());
  response_queue_.pop_front();
  return true;
}

bool ManagementService::pollEvent(ManagementEvent& out_event) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (event_queue_.empty()) return false;
  out_event = std::move(event_queue_.front());
  event_queue_.pop_front();
  return true;
}

void ManagementService::clearQueues() {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  request_queue_.clear();
  response_queue_.clear();
  event_queue_.clear();
  discovered_.clear();
  channel_sync_all_ = ChannelSyncAllSession{};
  chain_loop_all_ = ChainLoopAllSession{};
  deferred_lifecycle_commands_.clear();
}

size_t ManagementService::pendingRequestCount() const {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  return request_queue_.size();
}
size_t ManagementService::pendingResponseCount() const {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  return response_queue_.size();
}
size_t ManagementService::pendingEventCount() const {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  return event_queue_.size();
}

bool ManagementService::beginRadioTransition(const RadioTransitionBeginOptions& options) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (radio_transition_active_ &&
      (radio_transition_state_ == RadioTransitionState::Quiescing ||
       radio_transition_state_ == RadioTransitionState::Paused)) {
    return true;
  }

  radio_transition_active_ = true;
  radio_transition_state_ = RadioTransitionState::Quiescing;
  ++radio_transition_epoch_;
  if (radio_transition_epoch_ == 0U) {
    radio_transition_epoch_ = 1U;
  }
  radio_transition_last_error_ = ManagementStatus::Ok;
  radio_transition_last_error_stage_.clear();
  radio_transition_last_error_message_.clear();

  radio_transition_restore_live_monitor_ = options.disable_live_monitor && live_monitor_.enabled;

  if (options.stop_discovery) {
    discovery_active_ = false;
    manager_.setDiscoveryRxEnabled(false);
  }

  if (options.disable_live_monitor) {
    live_monitor_.enabled = false;
    live_monitor_.next_probe_due_ms = 0U;
    for (auto& peer : live_monitor_.peers) {
      peer.probe_pending = false;
      peer.probe_sent_ms = 0U;
      peer.probe_fail_count = 0U;
    }
  }

  if (options.clear_master_update_guard) {
    live_monitor_master_update_guard_until_ms_ = 0U;
  }

  if (options.cancel_deferred_operations) {
    if (channel_sync_all_.active || channel_sync_all_.req_id != 0U) {
      stopChannelSyncAll(false, ManagementStatus::BusyRadioTransition);
    }
    if (chain_loop_all_.active || chain_loop_all_.req_id != 0U) {
      stopChainLoopAll(false, ManagementStatus::BusyRadioTransition);
    }
    if (ota_push_local_.active) {
      stopOtaPushLocal(false,
                       ManagementStatus::BusyRadioTransition,
                       static_cast<uint16_t>(OtaStatusCode::InternalError),
                       "radio transition");
    }
    if (ota_update_local_.active) {
      stopOtaUpdateLocal(false,
                         ManagementStatus::BusyRadioTransition,
                         static_cast<uint16_t>(OtaStatusCode::InternalError),
                         "radio transition");
    }
    cancelDeferredLifecycleCommandsForTransition();
  }

  if (options.cancel_pending_mutating_requests) {
    cancelPendingMutatingRequestsForTransition();
  }

  radio_transition_state_ = RadioTransitionState::Paused;
  return true;
}

bool ManagementService::beginRadioTransition() {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  return beginRadioTransition(RadioTransitionBeginOptions{});
}

bool ManagementService::endRadioTransition(const RadioTransitionEndOptions& options) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (!radio_transition_active_) {
    radio_transition_state_ = RadioTransitionState::Idle;
    return true;
  }

  radio_transition_state_ = RadioTransitionState::Resuming;
  radio_transition_last_error_ = ManagementStatus::Ok;
  radio_transition_last_error_stage_.clear();
  radio_transition_last_error_message_.clear();

  if (options.restore_live_monitor && radio_transition_restore_live_monitor_) {
    live_monitor_.enabled = true;
    if (options.sync_live_monitor_peers) {
      syncLiveMonitorPeers();
    }
  } else if (options.sync_live_monitor_peers && live_monitor_.enabled) {
    syncLiveMonitorPeers();
  }
  radio_transition_restore_live_monitor_ = false;

  radio_transition_active_ = false;
  radio_transition_state_ = RadioTransitionState::Idle;
  return true;
}

bool ManagementService::endRadioTransition() {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  return endRadioTransition(RadioTransitionEndOptions{});
}

bool ManagementService::hardDeinitRadio(const RadioHardDeinitOptions& options) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (options.enter_transition_if_needed && !radio_transition_active_) {
    radio_transition_active_ = true;
    radio_transition_state_ = RadioTransitionState::Quiescing;
    ++radio_transition_epoch_;
    if (radio_transition_epoch_ == 0U) {
      radio_transition_epoch_ = 1U;
    }
  }

  radio_transition_last_error_ = ManagementStatus::Ok;
  radio_transition_last_error_stage_.clear();
  radio_transition_last_error_message_.clear();

  if (options.stop_discovery) {
    discovery_active_ = false;
    manager_.setDiscoveryRxEnabled(false);
  }

  if (options.disable_live_monitor) {
    live_monitor_.enabled = false;
    live_monitor_.next_probe_due_ms = 0U;
    for (auto& peer : live_monitor_.peers) {
      peer.probe_pending = false;
      peer.probe_sent_ms = 0U;
      peer.probe_fail_count = 0U;
    }
  }

  if (options.clear_master_update_guard) {
    live_monitor_master_update_guard_until_ms_ = 0U;
  }

  if (options.cancel_deferred_operations) {
    if (channel_sync_all_.active || channel_sync_all_.req_id != 0U) {
      stopChannelSyncAll(false, ManagementStatus::BusyRadioTransition);
    }
    if (chain_loop_all_.active || chain_loop_all_.req_id != 0U) {
      stopChainLoopAll(false, ManagementStatus::BusyRadioTransition);
    }
    if (ota_push_local_.active) {
      stopOtaPushLocal(false,
                       ManagementStatus::BusyRadioTransition,
                       static_cast<uint16_t>(OtaStatusCode::InternalError),
                       "radio hard deinit");
    }
    if (ota_update_local_.active) {
      stopOtaUpdateLocal(false,
                         ManagementStatus::BusyRadioTransition,
                         static_cast<uint16_t>(OtaStatusCode::InternalError),
                         "radio hard deinit");
    }
    cancelDeferredLifecycleCommandsForTransition();
  }

  if (options.cancel_pending_mutating_requests) {
    cancelPendingMutatingRequestsForTransition();
  }

  if (options.clear_queues) {
    clearQueues();
  }

  manager_.end();
  radio_transition_active_ = true;
  radio_transition_state_ = RadioTransitionState::Paused;
  return true;
}

bool ManagementService::hardDeinitRadio() {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  return hardDeinitRadio(RadioHardDeinitOptions{});
}

bool ManagementService::hardReinitRadio(const RadioHardReinitOptions& options) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  const MacAddress local_mac = manager_.localMac();
  if (isZeroMacAddress(local_mac)) {
    radio_transition_active_ = true;
    markRadioTransitionError(ManagementStatus::InternalError,
                             "radio_hard_reinit",
                             "local mac unavailable");
    return false;
  }

  manager_.end();
  if (!manager_.begin(local_mac)) {
    radio_transition_active_ = true;
    markRadioTransitionError(ManagementStatus::InternalError,
                             "radio_hard_reinit",
                             "manager begin failed");
    return false;
  }
  if (options.restore_link) {
    (void)manager_.restore();
  }

  if (options.reset_service_state) {
    begin(max_queue_depth_);
  } else {
    discovery_active_ = false;
    manager_.setDiscoveryRxEnabled(false);
    clearQueues();
    radio_transition_active_ = false;
    radio_transition_state_ = RadioTransitionState::Idle;
    radio_transition_restore_live_monitor_ = false;
    radio_transition_last_error_ = ManagementStatus::Ok;
    radio_transition_last_error_stage_.clear();
    radio_transition_last_error_message_.clear();
  }
  return true;
}

bool ManagementService::hardReinitRadio() {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  return hardReinitRadio(RadioHardReinitOptions{});
}

void ManagementService::radioTransitionStatusGet(RadioTransitionStatus& out_status) const {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  out_status.active = radio_transition_active_;
  out_status.state = radio_transition_state_;
  out_status.radio_epoch = radio_transition_epoch_;
  out_status.last_error = radio_transition_last_error_;
  out_status.last_error_stage = radio_transition_last_error_stage_;
  out_status.last_error_message = radio_transition_last_error_message_;
}

bool ManagementService::isRadioTransitionBlockedCommand(uint16_t cmd_id) const {
  const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
  switch (c) {
    case ManagementCommandId::DiscoveryStart:
    case ManagementCommandId::DiscoveryStop:
    case ManagementCommandId::PairRequest:
    case ManagementCommandId::UnpairRequest:
    case ManagementCommandId::RemovePeerRequest:
    case ManagementCommandId::SettingSet:
    case ManagementCommandId::TimeSet:
    case ManagementCommandId::PushStart:
    case ManagementCommandId::PushUpdate:
    case ManagementCommandId::PushPause:
    case ManagementCommandId::PushResume:
    case ManagementCommandId::PushStop:
    case ManagementCommandId::LiveMonitorEnable:
    case ManagementCommandId::LiveMonitorDisable:
    case ManagementCommandId::TopologyStageSet:
    case ManagementCommandId::TopologyCommit:
    case ManagementCommandId::TopologyTriggerSend:
    case ManagementCommandId::RestartSlaveRequest:
    case ManagementCommandId::ResetSlaveRequest:
    case ManagementCommandId::RestartMasterRequest:
    case ManagementCommandId::ResetMasterRequest:
    case ManagementCommandId::AudioPingRequest:
    case ManagementCommandId::LogLocalClear:
    case ManagementCommandId::LogLocalControlSet:
    case ManagementCommandId::LogRemoteClear:
    case ManagementCommandId::LogRemoteControlSet:
    case ManagementCommandId::ChannelSyncAll:
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
    case ManagementCommandId::CommTestRun:
    case ManagementCommandId::MetricsReset:
    case ManagementCommandId::CliControlSet:
    case ManagementCommandId::ChainLoopControlSet:
      return true;
    default:
      return false;
  }
}

void ManagementService::cancelDeferredLifecycleCommandsForTransition() {
  if (deferred_lifecycle_commands_.empty()) {
    return;
  }
  for (const auto& pending : deferred_lifecycle_commands_) {
    queueEvent({ManagementEventId::CmdFail,
                pending.source,
                pending.cmd_id,
                pending.req_id,
                ManagementStatus::BusyRadioTransition,
                {}});
  }
  deferred_lifecycle_commands_.clear();
}

void ManagementService::cancelPendingMutatingRequestsForTransition() {
  if (request_queue_.empty()) {
    return;
  }

  std::deque<PendingRequest> kept{};
  while (!request_queue_.empty()) {
    PendingRequest pending = request_queue_.front();
    request_queue_.pop_front();
    if (!isRadioTransitionBlockedCommand(pending.request.cmd_id)) {
      kept.push_back(std::move(pending));
      continue;
    }
    queueResponse(pending.request.source,
                  pending.request.cmd_id,
                  pending.request.req_id,
                  ManagementStatus::BusyRadioTransition);
    queueEvent({ManagementEventId::CmdFail,
                pending.request.source,
                pending.request.cmd_id,
                pending.request.req_id,
                ManagementStatus::BusyRadioTransition,
                {}});
  }
  request_queue_ = std::move(kept);
}

void ManagementService::markRadioTransitionError(ManagementStatus status,
                                                 const char* stage,
                                                 const char* message) {
  radio_transition_state_ = RadioTransitionState::Failed;
  radio_transition_last_error_ = status;
  radio_transition_last_error_stage_ = (stage != nullptr) ? stage : "";
  radio_transition_last_error_message_ = (message != nullptr) ? message : "";
}

bool ManagementService::executeRequest(const ManagementRequest& request) {
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(request.cmd_id);
  auto topologyErrorToStatus = [](const std::string& error) {
    if (error == "topology_not_staged") return ManagementStatus::TopologyNotStaged;
    if (error == "topology_version_stale") return ManagementStatus::TopologyVersionStale;
    if (error == "topology_apply_failed") return ManagementStatus::TopologyApplyFailed;
    if (management_utils::startsWith(error, "invalid_") ||
        error == "group_id_missing" ||
        error == "duplicate_logical_peer" ||
        error == "duplicate_relative_index") {
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

    if (manager_.persistedPairCount() >= 14U) {
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
    queueEvent(removed_event);
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

bool ManagementService::requirePairedPeer(const ManagementRequest& request,
                                          MacAddress& out_peer,
                                          PeerResolveContext* out_peer_ctx) {
  if (out_peer_ctx != nullptr) {
    *out_peer_ctx = PeerResolveContext{};
  }
  if (local_role_ == Role::Master && !request.has_target_peer) {
    queueResponse(request.source,
                  request.cmd_id,
                  request.req_id,
                  ManagementStatus::BadPayload,
                  {},
                  out_peer_ctx);
    return false;
  }
  if (request.has_target_peer) {
    if (out_peer_ctx != nullptr) {
      out_peer_ctx->has_requested_peer = true;
      out_peer_ctx->requested_peer = request.target_peer;
    }
    if (!manager_.hasPersistedPair(request.target_peer)) {
      queueResponse(request.source,
                    request.cmd_id,
                    request.req_id,
                    ManagementStatus::NotPaired,
                    {},
                    out_peer_ctx);
      return false;
    }
    out_peer = request.target_peer;
  } else {
    // Slave role keeps single-peer resolution behavior.
    MacAddress active{};
    const bool has_active = manager_.isPaired() && manager_.getPairedPeer(active);
    if (!has_active) {
      queueResponse(request.source,
                    request.cmd_id,
                    request.req_id,
                    ManagementStatus::NotPaired,
                    {},
                    out_peer_ctx);
      return false;
    }
    out_peer = active;
    if (out_peer_ctx != nullptr) {
      out_peer_ctx->has_requested_peer = true;
      out_peer_ctx->requested_peer = active;
    }
  }
  if (out_peer_ctx != nullptr) {
    out_peer_ctx->has_executed_peer = true;
    out_peer_ctx->executed_peer = out_peer;
  }
  return true;
}

void ManagementService::queueResponse(const ManagementResponse& response) {
  if (response_queue_.size() >= max_queue_depth_) response_queue_.pop_front();
  response_queue_.push_back(response);
}

void ManagementService::queueResponse(ManagementSource source,
                                      uint16_t cmd_id,
                                      uint32_t req_id,
                                      ManagementStatus status,
                                      const std::vector<uint8_t>& payload,
                                      const PeerResolveContext* peer_ctx) {
  ManagementResponse response{source, cmd_id, req_id, status, payload};
  if (peer_ctx != nullptr) {
    applyPeerContext(response, *peer_ctx);
  }
  queueResponse(response);
}

void ManagementService::queueEvent(const ManagementEvent& event) {
  if (event_queue_.size() >= (max_queue_depth_ * 2)) event_queue_.pop_front();
  event_queue_.push_back(event);
}

void ManagementService::emitServiceEvent(ManagementEventId event_id,
                                         const ManagementRequest& request,
                                         ManagementStatus status,
                                         const PeerResolveContext* peer_ctx) {
  ManagementEvent event{event_id, request.source, request.cmd_id, request.req_id, status, {}};
  if (peer_ctx != nullptr) {
    applyPeerContext(event, *peer_ctx);
  } else if (request.has_target_peer) {
    event.has_requested_peer = true;
    event.requested_peer = request.target_peer;
  }
  queueEvent(event);
}

void ManagementService::applyPeerContext(ManagementResponse& response,
                                         const PeerResolveContext& peer_ctx) const {
  response.has_requested_peer = peer_ctx.has_requested_peer;
  response.requested_peer = peer_ctx.requested_peer;
  response.has_executed_peer = peer_ctx.has_executed_peer;
  response.executed_peer = peer_ctx.executed_peer;
  response.activation_performed = peer_ctx.activation_performed;
  response.activation_latency_ms = peer_ctx.activation_latency_ms;
}

void ManagementService::applyPeerContext(ManagementEvent& event,
                                         const PeerResolveContext& peer_ctx) const {
  event.has_requested_peer = peer_ctx.has_requested_peer;
  event.requested_peer = peer_ctx.requested_peer;
  event.has_executed_peer = peer_ctx.has_executed_peer;
  event.executed_peer = peer_ctx.executed_peer;
  event.activation_performed = peer_ctx.activation_performed;
  event.activation_latency_ms = peer_ctx.activation_latency_ms;
}

bool ManagementService::buildDiscoverySnapshotPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  std::vector<MacAddress> peers;
  manager_.getDiscoveredPeers(peers);
  appendU8(out_payload, static_cast<uint8_t>(std::min<size_t>(peers.size(), 255)));
  for (size_t i = 0; i < peers.size() && i < 255; ++i) {
    out_payload.insert(out_payload.end(), peers[i].begin(), peers[i].end());
  }
  return true;
}

bool ManagementService::buildPairedSnapshotPayload(std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  std::vector<EspNowManager::PersistedPeerRoleEntry> peers{};
  manager_.getPersistedPeersWithRole(peers);
  appendU8(out_payload, static_cast<uint8_t>(std::min<size_t>(peers.size(), 255)));
  for (size_t i = 0; i < peers.size() && i < 255U; ++i) {
    out_payload.insert(out_payload.end(), peers[i].peer.begin(), peers[i].peer.end());
    appendU8(out_payload, peers[i].role_code);
  }
  return true;
}

bool ManagementService::buildStatusPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  MacAddress peer{};
  const bool has_peer = manager_.getPairedPeer(peer);
  appendU8(out_payload, manager_.isPaired() ? 1 : 0);
  appendU8(out_payload, has_peer ? 1 : 0);
  if (has_peer) out_payload.insert(out_payload.end(), peer.begin(), peer.end());
  appendU8(out_payload, discovery_active_ ? 1 : 0);
  appendU32(out_payload, now_ms_);
  appendU16(out_payload, static_cast<uint16_t>(request_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(response_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(event_queue_.size()));
  return true;
}

bool ManagementService::buildTopologyStatusPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  EspNowManager::TopologyStatus status{};
  (void)manager_.getTopologyStatus(status);

  appendU8(out_payload, EspNowManager::kTopologySchemaVersion);
  appendU8(out_payload, status.has_staged ? 1U : 0U);
  appendU8(out_payload, status.has_committed ? 1U : 0U);
  appendU8(out_payload, 0U);  // reserved
  appendU32(out_payload, status.has_staged ? status.staged.topology_version : 0U);
  appendU32(out_payload, status.has_committed ? status.committed.topology_version : 0U);
  appendU8(out_payload, static_cast<uint8_t>(status.has_staged ? status.staged.state
                                                               : EspNowManager::TopologyState::None));
  appendU8(out_payload, static_cast<uint8_t>(status.has_committed ? status.committed.state
                                                                  : EspNowManager::TopologyState::None));
  appendU8(out_payload, status.has_staged ? status.staged.enabled_slot_count : 0U);
  appendU8(out_payload, status.has_committed ? status.committed.enabled_slot_count : 0U);
  appendU8(out_payload, status.has_staged ? status.staged.enabled_group_count : 0U);
  appendU8(out_payload, status.has_committed ? status.committed.enabled_group_count : 0U);
  appendU8(out_payload, status.has_committed ? status.committed.index_neg
                                             : (status.has_staged ? status.staged.index_neg : 0U));
  appendU8(out_payload, status.has_committed ? status.committed.index_pos
                                             : (status.has_staged ? status.staged.index_pos : 0U));
  appendU32(out_payload, status.has_staged ? status.staged.checksum : 0U);
  appendU32(out_payload, status.has_committed ? status.committed.checksum : 0U);
  return true;
}

bool ManagementService::buildTopologySlotsPayload(bool committed, std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  EspNowManager::TopologySnapshot snapshot{};
  const EspNowManager::TopologyState state =
      committed ? EspNowManager::TopologyState::Committed : EspNowManager::TopologyState::Staged;
  if (!manager_.getTopologySnapshot(state, snapshot)) {
    return false;
  }

  appendU8(out_payload, snapshot.schema_version);
  appendU8(out_payload, static_cast<uint8_t>(snapshot.state));
  appendU8(out_payload, static_cast<uint8_t>(snapshot.slots.size()));
  for (size_t i = 0; i < snapshot.slots.size(); ++i) {
    const auto& slot = snapshot.slots[i];
    appendU8(out_payload, static_cast<uint8_t>(i));
    appendU8(out_payload, slot.enabled ? 1U : 0U);
    out_payload.insert(out_payload.end(), slot.peer.begin(), slot.peer.end());
    appendU8(out_payload, slot.peer_role);
    appendU8(out_payload, slot.group_id);
    appendU8(out_payload, static_cast<uint8_t>(slot.relative_index));
    appendU8(out_payload, slot.local_virtual_index);
    appendU8(out_payload, slot.peer_virtual_index);
    appendU8(out_payload, static_cast<uint8_t>(slot.axis_order));
    appendU16(out_payload, slot.delay_ms);
    appendU16(out_payload, slot.hold_ms);
  }
  return true;
}

bool ManagementService::buildTopologySnapshotPayload(bool committed,
                                                     ManagementTopologySnapshotPayload& out_snapshot) const {
  out_snapshot = ManagementTopologySnapshotPayload{};
  EspNowManager::TopologySnapshot snapshot{};
  const EspNowManager::TopologyState state =
      committed ? EspNowManager::TopologyState::Committed : EspNowManager::TopologyState::Staged;
  if (!manager_.getTopologySnapshot(state, snapshot)) {
    return false;
  }

  out_snapshot.schema_version = snapshot.schema_version;
  out_snapshot.topology_version = snapshot.topology_version;
  out_snapshot.index_neg = snapshot.index_neg;
  out_snapshot.index_pos = snapshot.index_pos;

  out_snapshot.groups.reserve(snapshot.groups.size());
  for (size_t i = 0; i < snapshot.groups.size(); ++i) {
    ManagementTopologyGroupSeedPayload g{};
    g.group_slot = static_cast<uint8_t>(i);
    g.enabled = snapshot.groups[i].enabled;
    g.group_id = snapshot.groups[i].group_id;
    g.seed = snapshot.groups[i].seed;
    out_snapshot.groups.push_back(g);
  }

  out_snapshot.slots.reserve(snapshot.slots.size());
  for (size_t i = 0; i < snapshot.slots.size(); ++i) {
    ManagementTopologySlotPayload s{};
    s.slot_index = static_cast<uint8_t>(i);
    s.enabled = snapshot.slots[i].enabled;
    s.peer = snapshot.slots[i].peer;
    s.peer_role = snapshot.slots[i].peer_role;
    s.group_id = snapshot.slots[i].group_id;
    s.relative_index = snapshot.slots[i].relative_index;
    s.local_virtual_index = snapshot.slots[i].local_virtual_index;
    s.peer_virtual_index = snapshot.slots[i].peer_virtual_index;
    s.axis_order = snapshot.slots[i].axis_order;
    s.delay_ms = snapshot.slots[i].delay_ms;
    s.hold_ms = snapshot.slots[i].hold_ms;
    out_snapshot.slots.push_back(s);
  }

  return true;
}

bool ManagementService::queueTopologyDeployToPeer(const MacAddress& peer,
                                                  const ManagementTopologySnapshotPayload& snapshot,
                                                  uint32_t corr_base) {
  if (pull_ == nullptr) {
    return false;
  }
  uint32_t corr = corr_base;
  bool sent = pull_->requestTopologyStageClear(peer, corr++);
  sent = sent && pull_->requestTopologyStageBegin(peer,
                                                  snapshot.schema_version,
                                                  snapshot.topology_version,
                                                  snapshot.index_neg,
                                                  snapshot.index_pos,
                                                  corr++);
  for (const auto& group : snapshot.groups) {
    sent = sent && pull_->requestTopologyStageGroupSet(peer, group, corr++);
  }
  for (const auto& slot : snapshot.slots) {
    sent = sent && pull_->requestTopologyStageSlotSet(peer, slot, corr++);
  }
  sent = sent && pull_->requestTopologyStageFinalize(peer, corr++);
  sent = sent && pull_->requestTopologyCommit(peer, corr++);
  return sent;
}

bool ManagementService::queueTopologyDeployForCommitted(uint32_t corr_seed,
                                                        uint32_t& out_queued_peers,
                                                        uint32_t& out_failed_peers) {
  out_queued_peers = 0U;
  out_failed_peers = 0U;
  if (local_role_ != Role::Master || pull_ == nullptr) {
    return false;
  }

  ManagementTopologySnapshotPayload snapshot{};
  if (!buildTopologySnapshotPayload(true, snapshot)) {
    return false;
  }

  std::vector<MacAddress> peers{};
  manager_.getPersistedPeers(peers);
  if (peers.empty()) {
    return true;
  }

  for (size_t i = 0; i < peers.size(); ++i) {
    const uint32_t base = corr_seed + 1U + static_cast<uint32_t>(i * 32U);
    if (queueTopologyDeployToPeer(peers[i], snapshot, base)) {
      ++out_queued_peers;
    } else {
      ++out_failed_peers;
    }
  }
  return true;
}

bool ManagementService::buildRuntimeChannelPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  out_payload.push_back(manager_.currentChannel());
  std::vector<EspNowManager::PersistedPeerChannelEntry> entries{};
  if (!manager_.getPersistedPeerChannels(entries)) {
    return false;
  }
  out_payload.push_back(static_cast<uint8_t>(std::min<size_t>(entries.size(), 255U)));
  const size_t n = std::min<size_t>(entries.size(), 255U);
  for (size_t i = 0; i < n; ++i) {
    const auto& e = entries[i];
    out_payload.insert(out_payload.end(), e.peer.begin(), e.peer.end());
    appendU8(out_payload, e.channel);
    const size_t key_n = std::min<size_t>(e.channel_key.size(), 255U);
    appendU8(out_payload, static_cast<uint8_t>(key_n));
    out_payload.insert(out_payload.end(),
                       e.channel_key.begin(),
                       e.channel_key.begin() + static_cast<std::string::difference_type>(key_n));
  }
  return true;
}

bool ManagementService::startChannelSyncAll(ManagementSource source, uint32_t req_id, uint8_t channel) {
  if (local_role_ != Role::Master || pull_ == nullptr) {
    return false;
  }
  channel_sync_all_ = ChannelSyncAllSession{};
  channel_sync_all_.active = true;
  channel_sync_all_.source = source;
  channel_sync_all_.owner_cmd_id = static_cast<uint16_t>(ManagementCommandId::ChannelSyncAll);
  channel_sync_all_.req_id = req_id;
  channel_sync_all_.channel = channel;
  channel_sync_all_.started_ms = now_ms_;
  channel_sync_all_.timeout_ms = 4000U;
  std::vector<EspNowManager::PersistedPeerRoleEntry> persisted_peers{};
  manager_.getPersistedPeersWithRole(persisted_peers);
  channel_sync_all_.peers.reserve(persisted_peers.size());
  channel_sync_all_.role_codes.reserve(persisted_peers.size());
  for (const auto& entry : persisted_peers) {
    upsertChannelSyncTarget(channel_sync_all_.peers, channel_sync_all_.role_codes, entry.peer, entry.role_code);
  }

  // Include topology-linked peers (chain roles) to keep topology bindings consistent.
  EspNowManager::TopologySnapshot snapshot{};
  if (manager_.getTopologySnapshot(EspNowManager::TopologyState::Committed, snapshot)) {
    for (const auto& slot : snapshot.slots) {
      if (!slot.enabled || !isChainRoleCode(slot.peer_role)) {
        continue;
      }
      upsertChannelSyncTarget(channel_sync_all_.peers, channel_sync_all_.role_codes, slot.peer, slot.peer_role);
    }
  }

  if (channel_sync_all_.peers.empty()) {
    const bool applied = manager_.applyRuntimeChannelToAllPeers(channel);
    stopChannelSyncAll(applied, applied ? ManagementStatus::Ok : ManagementStatus::InternalError);
    return true;
  }

  channel_sync_all_.corr_ids.reserve(channel_sync_all_.peers.size());
  channel_sync_all_.setting_keys.reserve(channel_sync_all_.peers.size());
  channel_sync_all_.acked.assign(channel_sync_all_.peers.size(), 0U);
  uint32_t corr = req_id + 1U;
  const std::string value = std::to_string(static_cast<unsigned int>(channel));
  for (size_t i = 0; i < channel_sync_all_.peers.size(); ++i) {
    const uint8_t role_code = (i < channel_sync_all_.role_codes.size()) ? channel_sync_all_.role_codes[i] : 0U;
    const char* key = channelSettingKeyForRole(role_code);
    channel_sync_all_.setting_keys.emplace_back(key);
    if (!pull_->requestSettingSet(channel_sync_all_.peers[i], key, value, corr)) {
      channel_sync_all_.active = false;
      return false;
    }
    channel_sync_all_.corr_ids.push_back(corr);
    ++corr;
  }
  return true;
}

void ManagementService::stopChannelSyncAll(bool success, ManagementStatus status) {
  if (!channel_sync_all_.active && channel_sync_all_.req_id == 0U) {
    return;
  }
  ManagementChannelSyncAllResultPayload result{};
  result.channel = channel_sync_all_.channel;
  result.total_peers = static_cast<uint8_t>(std::min<size_t>(channel_sync_all_.peers.size(), 255U));
  size_t acked = 0U;
  for (uint8_t v : channel_sync_all_.acked) {
    if (v != 0U) {
      ++acked;
    }
  }
  result.acked_peers = static_cast<uint8_t>(std::min<size_t>(acked, 255U));
  std::vector<uint8_t> payload{};
  appendU8(payload, result.channel);
  appendU8(payload, result.acked_peers);
  appendU8(payload, result.total_peers);

  queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
              channel_sync_all_.source,
              channel_sync_all_.owner_cmd_id,
              channel_sync_all_.req_id,
              status,
              payload});
  channel_sync_all_ = ChannelSyncAllSession{};
}

void ManagementService::pumpChannelSyncAll() {
  if (!channel_sync_all_.active) {
    return;
  }
  if (channel_sync_all_.timeout_ms != 0U &&
      static_cast<int32_t>(now_ms_ - (channel_sync_all_.started_ms + channel_sync_all_.timeout_ms)) >= 0) {
    stopChannelSyncAll(false, ManagementStatus::Timeout);
    return;
  }
  bool all_acked = !channel_sync_all_.acked.empty();
  for (uint8_t v : channel_sync_all_.acked) {
    if (v == 0U) {
      all_acked = false;
      break;
    }
  }
  if (!all_acked) {
    return;
  }
  const bool applied = manager_.applyRuntimeChannelToAllPeers(channel_sync_all_.channel);
  stopChannelSyncAll(applied, applied ? ManagementStatus::Ok : ManagementStatus::InternalError);
}

bool ManagementService::buildChainLoopTargetPeers(std::vector<MacAddress>& out_peers) const {
  out_peers.clear();
  EspNowManager::TopologySnapshot snapshot{};
  if (!manager_.getTopologySnapshot(EspNowManager::TopologyState::Committed, snapshot)) {
    return true;
  }

  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled || !isChainRoleCode(slot.peer_role)) {
      continue;
    }
    if (std::find(out_peers.begin(), out_peers.end(), slot.peer) == out_peers.end()) {
      out_peers.push_back(slot.peer);
    }
  }
  return true;
}

bool ManagementService::startChainLoopAll(ManagementSource source, uint32_t req_id, bool enabled) {
  if (local_role_ != Role::Master || pull_ == nullptr) {
    return false;
  }
  chain_loop_all_ = ChainLoopAllSession{};
  chain_loop_all_.active = true;
  chain_loop_all_.source = source;
  chain_loop_all_.owner_cmd_id = static_cast<uint16_t>(ManagementCommandId::ChainLoopControlSet);
  chain_loop_all_.req_id = req_id;
  chain_loop_all_.enabled = enabled;
  chain_loop_all_.started_ms = now_ms_;
  chain_loop_all_.timeout_ms = 4000U;
  if (!buildChainLoopTargetPeers(chain_loop_all_.peers)) {
    chain_loop_all_.active = false;
    return false;
  }
  if (chain_loop_all_.peers.empty()) {
    chain_loop_enabled_ = enabled;
    persistChainLoopConfig();
    stopChainLoopAll(true, ManagementStatus::Ok);
    return true;
  }

  chain_loop_all_.corr_ids.reserve(chain_loop_all_.peers.size());
  chain_loop_all_.acked.assign(chain_loop_all_.peers.size(), 0U);
  uint32_t corr = req_id + 1U;
  const std::string value = enabled ? "1" : "0";
  for (size_t i = 0; i < chain_loop_all_.peers.size(); ++i) {
    if (!pull_->requestSettingSet(chain_loop_all_.peers[i], "LoopAuto", value, corr)) {
      chain_loop_all_.active = false;
      return false;
    }
    chain_loop_all_.corr_ids.push_back(corr);
    ++corr;
  }
  return true;
}

void ManagementService::stopChainLoopAll(bool success, ManagementStatus status) {
  if (!chain_loop_all_.active && chain_loop_all_.req_id == 0U) {
    return;
  }
  if (success) {
    chain_loop_enabled_ = chain_loop_all_.enabled;
  } else {
    // Strict aggregate rule: true only when every targeted chain node acknowledged apply.
    chain_loop_enabled_ = false;
  }
  persistChainLoopConfig();

  ManagementChainLoopResultPayload result{};
  result.enabled = chain_loop_enabled_;
  result.total_peers = static_cast<uint8_t>(std::min<size_t>(chain_loop_all_.peers.size(), 255U));
  size_t acked = 0U;
  for (uint8_t v : chain_loop_all_.acked) {
    if (v != 0U) {
      ++acked;
    }
  }
  result.acked_peers = static_cast<uint8_t>(std::min<size_t>(acked, 255U));

  std::vector<uint8_t> payload{};
  appendU8(payload, result.enabled ? 1U : 0U);
  appendU8(payload, result.acked_peers);
  appendU8(payload, result.total_peers);

  queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
              chain_loop_all_.source,
              chain_loop_all_.owner_cmd_id,
              chain_loop_all_.req_id,
              status,
              payload});
  chain_loop_all_ = ChainLoopAllSession{};
}

void ManagementService::pumpChainLoopAll() {
  if (!chain_loop_all_.active) {
    return;
  }
  if (chain_loop_all_.timeout_ms != 0U &&
      static_cast<int32_t>(now_ms_ - (chain_loop_all_.started_ms + chain_loop_all_.timeout_ms)) >= 0) {
    stopChainLoopAll(false, ManagementStatus::Timeout);
    return;
  }
  bool all_acked = !chain_loop_all_.acked.empty();
  for (uint8_t v : chain_loop_all_.acked) {
    if (v == 0U) {
      all_acked = false;
      break;
    }
  }
  if (!all_acked) {
    return;
  }
  stopChainLoopAll(true, ManagementStatus::Ok);
}

bool ManagementService::buildLiveMonitorStatusPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  uint8_t online_count = 0U;
  uint8_t offline_count = 0U;
  for (const auto& peer : live_monitor_.peers) {
    if (peer.online) {
      if (online_count < 0xFFU) ++online_count;
    } else {
      if (offline_count < 0xFFU) ++offline_count;
    }
  }
  appendU8(out_payload, live_monitor_.enabled ? 1U : 0U);
  appendU8(out_payload, live_monitor_.ignore_active ? 1U : 0U);
  appendU16(out_payload, live_monitor_.ignore_reason_mask);
  appendU8(out_payload, static_cast<uint8_t>(std::min<size_t>(live_monitor_.peers.size(), 255U)));
  appendU8(out_payload, online_count);
  appendU8(out_payload, offline_count);
  appendU32(out_payload, live_monitor_.next_probe_due_ms);
  appendU32(out_payload, live_monitor_.last_transition_ms);
  return true;
}

bool ManagementService::buildQueuePayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  appendU16(out_payload, static_cast<uint16_t>(request_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(response_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(event_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(max_queue_depth_));
  return true;
}

bool ManagementService::buildMetricsPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  const ManagerRuntimeMetrics& m = manager_.runtimeMetrics();
  appendU64(out_payload, m.tick_count);
  appendU32(out_payload, m.tick_last_us);
  appendU32(out_payload, m.tick_max_us);
  appendU64(out_payload, m.tick_total_us);
  appendU64(out_payload, m.rx_frames);
  appendU64(out_payload, m.rx_bytes);
  appendU32(out_payload, m.rx_handler_last_us);
  appendU32(out_payload, m.rx_handler_max_us);
  appendU64(out_payload, m.rx_handler_total_us);
  appendU64(out_payload, m.tx_frames);
  appendU64(out_payload, m.tx_bytes);
  appendU64(out_payload, m.tx_failures);
  appendU32(out_payload, m.tx_send_last_us);
  appendU32(out_payload, m.tx_send_max_us);
  appendU64(out_payload, m.tx_send_total_us);
  return true;
}

void ManagementService::loadLiveMonitorConfig() {
  live_monitor_.enabled = false;
  std::vector<uint8_t> blob;
  if (!manager_.loadLocalMetaBlob(kLiveMonitorMetaSlot, blob)) {
    return;
  }
  if (blob.size() < 2U || blob[0U] != kLiveMonitorMetaVersion) {
    return;
  }
  live_monitor_.enabled = (blob[1U] != 0U);
}

void ManagementService::persistLiveMonitorConfig() const {
  uint8_t blob[2] = {kLiveMonitorMetaVersion, static_cast<uint8_t>(live_monitor_.enabled ? 1U : 0U)};
  (void)manager_.saveLocalMetaBlob(kLiveMonitorMetaSlot, blob, sizeof(blob));
}

void ManagementService::loadChainLoopConfig() {
  chain_loop_enabled_ = false;
  std::vector<uint8_t> blob;
  if (!manager_.loadLocalMetaBlob(kChainLoopMetaSlot, blob)) {
    return;
  }
  if (blob.size() < 2U || blob[0U] != kChainLoopMetaVersion || blob[1U] > 1U) {
    return;
  }
  chain_loop_enabled_ = (blob[1U] != 0U);
}

void ManagementService::persistChainLoopConfig() const {
  uint8_t blob[2] = {kChainLoopMetaVersion, static_cast<uint8_t>(chain_loop_enabled_ ? 1U : 0U)};
  (void)manager_.saveLocalMetaBlob(kChainLoopMetaSlot, blob, sizeof(blob));
}

ManagementService::LivenessPeerState* ManagementService::findLivePeerState(const MacAddress& peer) {
  auto it = std::find_if(live_monitor_.peers.begin(),
                         live_monitor_.peers.end(),
                         [&](const LivenessPeerState& s) { return s.peer == peer; });
  return (it == live_monitor_.peers.end()) ? nullptr : &(*it);
}

const ManagementService::LivenessPeerState* ManagementService::findLivePeerState(const MacAddress& peer) const {
  auto it = std::find_if(live_monitor_.peers.begin(),
                         live_monitor_.peers.end(),
                         [&](const LivenessPeerState& s) { return s.peer == peer; });
  return (it == live_monitor_.peers.end()) ? nullptr : &(*it);
}

void ManagementService::removeLivePeerState(const MacAddress& peer) {
  live_monitor_.peers.erase(std::remove_if(live_monitor_.peers.begin(),
                                           live_monitor_.peers.end(),
                                           [&](const LivenessPeerState& s) { return s.peer == peer; }),
                            live_monitor_.peers.end());
}

void ManagementService::syncLiveMonitorPeers() {
  std::vector<MacAddress> persisted{};
  manager_.getPersistedPeers(persisted);

  live_monitor_.peers.erase(
      std::remove_if(live_monitor_.peers.begin(),
                     live_monitor_.peers.end(),
                     [&](const LivenessPeerState& state) {
                       return std::find(persisted.begin(), persisted.end(), state.peer) == persisted.end();
                     }),
      live_monitor_.peers.end());

  for (const auto& peer : persisted) {
    LivenessPeerState* state = findLivePeerState(peer);
    if (state == nullptr) {
      LivenessPeerState created{};
      created.peer = peer;
      created.online = true;
      created.last_seen_ms = now_ms_;
      live_monitor_.peers.push_back(created);
      continue;
    }
    if (state->last_seen_ms == 0U) {
      state->last_seen_ms = now_ms_;
    }
  }
}

uint16_t ManagementService::liveMonitorIgnoreReasonMask() const {
  uint16_t mask = 0U;
  if (ota_push_local_.active) {
    mask |= kLiveIgnoreReasonOtaPush;
  }
  if (ota_update_local_.active) {
    mask |= kLiveIgnoreReasonOtaUpdate;
  }
  if (live_monitor_critical_inflight_) {
    mask |= kLiveIgnoreReasonCriticalInflight;
  }
  if (live_monitor_master_update_guard_until_ms_ != 0U &&
      static_cast<int32_t>(now_ms_ - live_monitor_master_update_guard_until_ms_) < 0) {
    mask |= kLiveIgnoreReasonMasterUpdateGuard;
  }
  return mask;
}

void ManagementService::pauseTelemetryPushForPeerBestEffort(const MacAddress& peer, uint32_t corr_id) {
  if (local_role_ != Role::Master) {
    return;
  }
  if (!manager_.hasPersistedPair(peer)) {
    return;
  }
  TelemetryPushCommand cmd{};
  cmd.action = TelemetryPushAction::Pause;
  const uint32_t use_corr = (corr_id == 0U) ? 1U : corr_id;
  (void)manager_.sendTelemetryPushCommand(peer, cmd, use_corr);
}

void ManagementService::pauseTelemetryPushForAllPeersBestEffort(uint32_t corr_seed) {
  if (local_role_ != Role::Master) {
    return;
  }
  std::vector<MacAddress> peers{};
  manager_.getPersistedPeers(peers);
  TelemetryPushCommand cmd{};
  cmd.action = TelemetryPushAction::Pause;
  uint32_t corr = (corr_seed == 0U) ? 1U : corr_seed;
  for (const auto& peer : peers) {
    (void)manager_.sendTelemetryPushCommand(peer, cmd, corr++);
    if (corr == 0U) {
      corr = 1U;
    }
  }
}

void ManagementService::emitLivePeerTransition(const MacAddress& peer, bool online, uint8_t reason_code) {
  if (!live_monitor_.enabled) {
    return;
  }
  live_monitor_.last_transition_ms = now_ms_;
  std::vector<uint8_t> payload;
  appendU8(payload, 1U);  // schema_version
  payload.insert(payload.end(), peer.begin(), peer.end());
  appendU8(payload, online ? 0U : 1U);
  appendU8(payload, reason_code);
  appendU32(payload, now_ms_);
  queueEvent({ManagementEventId::PeerLivenessTransition,
              ManagementSource::Unknown,
              0,
              0,
              ManagementStatus::Ok,
              payload});
}

void ManagementService::noteLivePeerSeen(const MacAddress& peer, uint8_t reason_code) {
  if (!manager_.hasPersistedPair(peer)) {
    return;
  }
  LivenessPeerState* state = findLivePeerState(peer);
  if (state == nullptr) {
    LivenessPeerState created{};
    created.peer = peer;
    created.online = true;
    created.last_seen_ms = now_ms_;
    live_monitor_.peers.push_back(created);
    return;
  }

  uint8_t transition_reason = reason_code;
  if (state->probe_pending) {
    transition_reason = kLiveTransitionReasonProbeSuccess;
  }
  state->last_seen_ms = now_ms_;
  state->probe_pending = false;
  state->probe_sent_ms = 0U;
  state->probe_fail_count = 0U;
  if (!state->online) {
    state->online = true;
    emitLivePeerTransition(peer, true, transition_reason);
  }
}

bool ManagementService::hasPendingTargetRequest(const MacAddress& peer) const {
  for (const auto& pending : request_queue_) {
    if (pending.request.has_target_peer && pending.request.target_peer == peer) {
      return true;
    }
  }
  return false;
}

bool ManagementService::isCriticalLiveMonitorCommand(uint16_t cmd_id) const {
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(cmd_id);
  switch (cmd) {
    case ManagementCommandId::RestartSlaveRequest:
    case ManagementCommandId::ResetSlaveRequest:
    case ManagementCommandId::RestartMasterRequest:
    case ManagementCommandId::ResetMasterRequest:
    case ManagementCommandId::StorageFormat:
    case ManagementCommandId::OtaApply:
    case ManagementCommandId::OtaRollback:
    case ManagementCommandId::OtaTransferBegin:
    case ManagementCommandId::OtaTransferChunk:
    case ManagementCommandId::OtaTransferEnd:
    case ManagementCommandId::OtaTransferAbort:
    case ManagementCommandId::OtaPushStart:
    case ManagementCommandId::OtaPushAbort:
    case ManagementCommandId::OtaUpdateStart:
    case ManagementCommandId::OtaMasterUpdateStart:
    case ManagementCommandId::TopologyStageSet:
    case ManagementCommandId::TopologyCommit:
    case ManagementCommandId::ChainLoopControlSet:
      return true;
    default:
      return false;
  }
}

void ManagementService::pumpLiveMonitor() {
  syncLiveMonitorPeers();
  const bool was_ignore = live_monitor_.ignore_active;
  live_monitor_.ignore_reason_mask = liveMonitorIgnoreReasonMask();
  live_monitor_.ignore_active = (live_monitor_.ignore_reason_mask != 0U);

  if (!live_monitor_.enabled) {
    live_monitor_.next_probe_due_ms = 0U;
    return;
  }

  if (live_monitor_.ignore_active) {
    live_monitor_.next_probe_due_ms = 0U;
    return;
  }

  const uint32_t probe_trigger_age_ms = liveProbeTriggerAgeMs();
  const uint32_t probe_urgency_age_ms = liveProbeUrgencyAgeMs();
  const uint32_t probe_timeout_ms = liveProbeTimeoutMs(live_monitor_.peers.size());
  uint8_t probe_budget = liveProbeBudgetPerPump(live_monitor_.peers.size());
  size_t offline_count = 0U;
  for (const auto& peer : live_monitor_.peers) {
    if (!peer.online) {
      ++offline_count;
    }
  }
  if (offline_count > 0U) {
    const uint8_t recovery_budget = (live_monitor_.peers.size() <= 4U) ? 3U : 2U;
    probe_budget = std::max<uint8_t>(probe_budget, recovery_budget);
  }
  auto trackNextDue = [](uint32_t& next_due_ms, uint32_t due_ms) {
    if (due_ms < next_due_ms) {
      next_due_ms = due_ms;
    }
  };
  uint32_t next_due_ms = 0xFFFFFFFFU;

  if (was_ignore) {
    for (auto& peer : live_monitor_.peers) {
      peer.probe_pending = false;
      peer.probe_sent_ms = 0U;
      peer.probe_fail_count = 0U;
      const uint32_t age_ms = now_ms_ - peer.last_seen_ms;
      if (age_ms >= kLiveOfflineDetectMaxMs && peer.online) {
        peer.online = false;
        emitLivePeerTransition(peer.peer, false, kLiveTransitionReasonResumeRecheckTimeout);
      } else if (age_ms < kLiveOfflineDetectMaxMs) {
        trackNextDue(next_due_ms, kLiveOfflineDetectMaxMs - age_ms);
      }
    }
  }

  for (auto& peer : live_monitor_.peers) {
    const uint32_t age_ms = now_ms_ - peer.last_seen_ms;
    if (age_ms >= kLiveOfflineDetectMaxMs) {
      peer.probe_pending = false;
      peer.probe_sent_ms = 0U;
      if (peer.online) {
        peer.online = false;
        emitLivePeerTransition(peer.peer, false, kLiveTransitionReasonProbeTimeoutThreshold);
      }
      continue;
    }

    if (!peer.probe_pending) {
      if (age_ms < probe_trigger_age_ms) {
        trackNextDue(next_due_ms, probe_trigger_age_ms - age_ms);
      } else {
        trackNextDue(next_due_ms, 1U);
      }
      continue;
    }

    const uint32_t probe_age_ms = now_ms_ - peer.probe_sent_ms;
    if (probe_age_ms < probe_timeout_ms) {
      trackNextDue(next_due_ms, probe_timeout_ms - probe_age_ms);
      continue;
    }

    peer.probe_pending = false;
    peer.probe_sent_ms = 0U;
    if (peer.probe_fail_count < 0xFFU) {
      ++peer.probe_fail_count;
    }
    if (age_ms >= probe_urgency_age_ms) {
      trackNextDue(next_due_ms, 20U);
    } else if (age_ms < probe_trigger_age_ms) {
      trackNextDue(next_due_ms, probe_trigger_age_ms - age_ms);
    } else {
      trackNextDue(next_due_ms, 80U);
    }
  }

  if (pull_ == nullptr || live_monitor_.peers.empty() || probe_budget == 0U) {
    live_monitor_.next_probe_due_ms = (next_due_ms == 0xFFFFFFFFU) ? 0U : next_due_ms;
    return;
  }

  const size_t peer_count = live_monitor_.peers.size();
  const size_t start_index = static_cast<size_t>(live_monitor_.probe_rr_cursor) % peer_count;
  uint8_t probes_sent = 0U;
  for (size_t offset = 0; offset < peer_count; ++offset) {
    const size_t idx = (start_index + offset) % peer_count;
    LivenessPeerState& peer = live_monitor_.peers[idx];
    if (peer.probe_pending) {
      continue;
    }

    const uint32_t age_ms = now_ms_ - peer.last_seen_ms;
    if (age_ms >= kLiveOfflineDetectMaxMs && peer.online) {
      peer.online = false;
      emitLivePeerTransition(peer.peer, false, kLiveTransitionReasonProbeTimeoutThreshold);
    }

    // Online peers are only probed near deadline; offline peers are actively probed for recovery.
    if (peer.online && age_ms < probe_trigger_age_ms) {
      trackNextDue(next_due_ms, probe_trigger_age_ms - age_ms);
      continue;
    }

    const bool near_deadline = peer.online && (age_ms >= probe_urgency_age_ms);
    if (hasPendingTargetRequest(peer.peer) && !near_deadline) {
      trackNextDue(next_due_ms, 50U);
      continue;
    }

    uint32_t corr_id = live_monitor_.next_probe_corr_id++;
    if (corr_id == 0U) {
      corr_id = 1U;
      live_monitor_.next_probe_corr_id = 2U;
    }

    if (pull_->requestLiveness(peer.peer, corr_id)) {
      peer.probe_pending = true;
      peer.probe_sent_ms = now_ms_;
      trackNextDue(next_due_ms, probe_timeout_ms);
      ++probes_sent;
      if (probes_sent >= probe_budget) {
        break;
      }
      continue;
    }

    trackNextDue(next_due_ms, near_deadline ? 20U : 50U);
  }

  if (peer_count > 0U) {
    live_monitor_.probe_rr_cursor = static_cast<uint8_t>((start_index + 1U) % peer_count);
  }

  live_monitor_.next_probe_due_ms = (next_due_ms == 0xFFFFFFFFU) ? 0U : next_due_ms;
}

bool ManagementService::runDescriptorPull(const ManagementRequest& request, uint16_t cmd_id) {
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

  bool sent = false;
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
    case ManagementCommandId::TelemPull:
      sent = pull_->requestTelemetryPull(peer, request.req_id);
      break;
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
      ManagementTopologySnapshotPayload stage_payload{};
      if (!management_utils::parseTopologyStagePayload(request.payload, stage_payload)) {
        peerResponse(ManagementStatus::BadPayload);
        return true;
      }
      uint32_t corr = request.req_id;
      sent = pull_->requestTopologyStageClear(peer, corr++);
      sent = sent && pull_->requestTopologyStageBegin(peer,
                                                      stage_payload.schema_version,
                                                      stage_payload.topology_version,
                                                      stage_payload.index_neg,
                                                      stage_payload.index_pos,
                                                      corr++);
      for (const auto& group : stage_payload.groups) {
        sent = sent && pull_->requestTopologyStageGroupSet(peer, group, corr++);
      }
      for (const auto& slot : stage_payload.slots) {
        sent = sent && pull_->requestTopologyStageSlotSet(peer, slot, corr++);
      }
      sent = sent && pull_->requestTopologyStageFinalize(peer, corr++);
      break;
    }
    case ManagementCommandId::TopologyCommit:
      sent = pull_->requestTopologyCommit(peer, request.req_id);
      break;
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
  const ManagementCommandId command = static_cast<ManagementCommandId>(cmd_id);
  const bool deferred = isDescriptorMutationCommand(command);
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

void ManagementService::pumpOtaPushLocal() {
  if (!ota_push_local_.active) {
    return;
  }
  if (pull_ == nullptr || ota_push_storage_ == nullptr) {
    stopOtaPushLocal(false, ManagementStatus::UnsupportedCommand, 0U, "local ota push unavailable");
    return;
  }

  if (!manager_.hasPersistedPair(ota_push_local_.peer)) {
    stopOtaPushLocal(false, ManagementStatus::NotPaired, 0U, "paired peer lost");
    return;
  }

  constexpr uint32_t kWaitBeginTimeoutMs = 12000U;
  constexpr uint32_t kWaitBeginOptimisticStartMs = 900U;
  constexpr uint8_t kChunkBudgetPerTick = 8U;
  constexpr uint32_t kChunkAckTimeoutMs = 1500U;
  constexpr uint32_t kChunkAckTimeoutRecoveryMs = 450U;
  constexpr uint8_t kChunkAckMaxRetry = 6U;
  constexpr uint8_t kChunkAckMaxRetryRecovery = 20U;
  constexpr uint8_t kWindowRecoveryChunks = 1U;
  constexpr uint32_t kWaitEndTimeoutMs = 60000U;
  constexpr uint32_t kStatusPollIntervalMs = 250U;

  if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitBegin) {
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.last_activity_ms) >=
        static_cast<int32_t>(kWaitBeginTimeoutMs)) {
      stopOtaPushLocal(false, ManagementStatus::Timeout, static_cast<uint16_t>(OtaStatusCode::Timeout), "begin status timeout");
      return;
    }
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.last_status_poll_ms) >=
        static_cast<int32_t>(kStatusPollIntervalMs)) {
      (void)pull_->requestOtaStatus(ota_push_local_.peer, ota_push_local_.req_id);
      ota_push_local_.last_status_poll_ms = now_ms_;
    }
    // Some slaves do not emit an explicit "begin ack" transfer-status event.
    // Avoid deadlock in WAIT_BEGIN by starting stream after a short grace period.
    if (!ota_push_local_.begin_acked &&
        static_cast<int32_t>(now_ms_ - ota_push_local_.started_ms) >=
        static_cast<int32_t>(kWaitBeginOptimisticStartMs)) {
      ota_push_local_.begin_acked = true;
      ota_push_local_.phase = OtaPushLocalSession::Phase::Streaming;
      ota_push_local_.waiting_chunk_ack = false;
      ota_push_local_.retry_count = 0U;
      ota_push_local_.next_send_ms = now_ms_;
    }
    return;
  }

  if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd) {
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.last_activity_ms) >=
        static_cast<int32_t>(kWaitEndTimeoutMs)) {
      stopOtaPushLocal(false, ManagementStatus::Timeout, static_cast<uint16_t>(OtaStatusCode::Timeout), "finalize timeout");
    }
    return;
  }

  if (ota_push_local_.phase != OtaPushLocalSession::Phase::Streaming) {
    return;
  }

  if (!ota_push_local_.begin_acked) {
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.started_ms) >=
        static_cast<int32_t>(kWaitBeginOptimisticStartMs)) {
      ota_push_local_.begin_acked = true;
      ota_push_local_.next_send_ms = now_ms_;
    } else {
      return;
    }
  }

  if (ota_push_local_.waiting_chunk_ack) {
    if (ota_push_local_.remote_acked_offset >= ota_push_local_.pending_end_offset) {
      ota_push_local_.waiting_chunk_ack = false;
      ota_push_local_.retry_count = 0U;
      ota_push_local_.pending_end_offset = 0U;
    } else if (static_cast<int32_t>(now_ms_ - ota_push_local_.wait_started_ms) >=
               static_cast<int32_t>((ota_push_local_.window_size_chunks <= kWindowRecoveryChunks)
                                        ? kChunkAckTimeoutRecoveryMs
                                        : kChunkAckTimeoutMs)) {
      const uint8_t retry_max =
          (ota_push_local_.window_size_chunks <= kWindowRecoveryChunks)
              ? kChunkAckMaxRetryRecovery
              : kChunkAckMaxRetry;
      if (ota_push_local_.retry_count >= retry_max) {
        stopOtaPushLocal(false,
                         ManagementStatus::Timeout,
                         static_cast<uint16_t>(OtaStatusCode::Timeout),
                         "window ack timeout");
        return;
      }
      ++ota_push_local_.retry_count;
      ota_push_local_.waiting_chunk_ack = false;
      ota_push_local_.pending_end_offset = 0U;
      ota_push_local_.next_offset =
          std::min<uint32_t>(ota_push_local_.remote_acked_offset, ota_push_local_.total_size);
      ota_push_local_.next_send_ms = now_ms_ + 12U;
    }
    return;
  }

  if (ota_push_local_.next_offset >= ota_push_local_.total_size) {
    if (!pull_->sendFirmwareEnd(ota_push_local_.peer,
                                ota_push_local_.total_size,
                                ota_push_local_.image_crc32,
                                ota_push_local_.req_id)) {
      stopOtaPushLocal(false, ManagementStatus::InternalError, static_cast<uint16_t>(OtaStatusCode::InternalError), "end send failed");
      return;
    }
    ota_push_local_.phase = OtaPushLocalSession::Phase::WaitEnd;
    ota_push_local_.last_activity_ms = now_ms_;
    return;
  }

  if (static_cast<int32_t>(now_ms_ - ota_push_local_.next_send_ms) < 0) {
    return;
  }

  if (ota_push_local_.pending_end_offset == 0U ||
      ota_push_local_.next_offset >= ota_push_local_.pending_end_offset) {
    const uint32_t window_bytes = static_cast<uint32_t>(ota_push_local_.chunk_bytes) *
                                  static_cast<uint32_t>(ota_push_local_.window_size_chunks);
    ota_push_local_.pending_end_offset =
        std::min<uint32_t>(ota_push_local_.next_offset + window_bytes, ota_push_local_.total_size);
  }

  uint8_t budget = kChunkBudgetPerTick;
  while (budget > 0U &&
         ota_push_local_.next_offset < ota_push_local_.total_size &&
         ota_push_local_.next_offset < ota_push_local_.pending_end_offset) {
    const size_t req = std::min<size_t>(ota_push_local_.chunk_bytes,
                                        static_cast<size_t>(ota_push_local_.total_size -
                                                            ota_push_local_.next_offset));
    if (ota_push_local_.chunk_buf.size() < req) {
      ota_push_local_.chunk_buf.resize(req);
    }
    size_t out_len = 0U;
    std::string msg;
    if (!ota_push_storage_->readAt(ota_push_local_.path,
                                   ota_push_local_.next_offset,
                                   ota_push_local_.chunk_buf.data(),
                                   req,
                                   out_len,
                                   msg) ||
        out_len == 0U) {
      stopOtaPushLocal(false,
                       ManagementStatus::InternalError,
                       static_cast<uint16_t>(OtaStatusCode::StorageNotReady),
                       "chunk read failed");
      return;
    }
    const uint32_t remaining = ota_push_local_.total_size - ota_push_local_.next_offset;
    if (out_len > remaining) {
      out_len = static_cast<size_t>(remaining);
      if (out_len == 0U) {
        break;
      }
    }

    if (!pull_->sendFirmwareChunk(ota_push_local_.peer,
                                  ota_push_local_.next_offset,
                                  ota_push_local_.chunk_buf.data(),
                                  out_len,
                                  ota_push_local_.req_id)) {
      ++ota_push_local_.send_fail_streak;
      if (ota_push_local_.send_fail_streak >= 24U) {
        stopOtaPushLocal(false,
                         ManagementStatus::InternalError,
                         static_cast<uint16_t>(OtaStatusCode::InternalError),
                         "chunk send failed");
        return;
      }
      const uint32_t backoff_ms = std::min<uint32_t>(
          40U, 2U + (static_cast<uint32_t>(ota_push_local_.send_fail_streak) * 2U));
      ota_push_local_.next_send_ms = now_ms_ + backoff_ms;
      return;
    }

    ota_push_local_.send_fail_streak = 0U;
    ota_push_local_.next_offset += static_cast<uint32_t>(out_len);
    ++ota_push_local_.chunks_sent;
    --budget;
  }

  if (ota_push_local_.next_offset >= ota_push_local_.pending_end_offset) {
    ota_push_local_.waiting_chunk_ack = true;
    ota_push_local_.wait_started_ms = now_ms_;
    ota_push_local_.retry_count = 0U;
  }
}

void ManagementService::stopOtaPushLocal(bool success,
                                         ManagementStatus status,
                                         uint16_t ota_status_code,
                                         const char* reason) {
  if (!ota_push_local_.active) {
    return;
  }
  const uint32_t req_id = ota_push_local_.req_id;
  const MacAddress peer = ota_push_local_.peer;
  const ManagementSource source = ota_push_local_.source;
  const uint16_t owner_cmd_id = ota_push_local_.owner_cmd_id;
  const bool emit_cmd_event = ota_push_local_.emit_cmd_event;

  // Best-effort remote RX cleanup: avoid leaving slave OTA sink stuck in "receiving"
  // after local pipeline failures.
  if (!success && pull_ != nullptr && manager_.hasPersistedPair(peer)) {
    (void)pull_->requestOtaClearScope(peer, "in", req_id);
  }

  if (ota_update_local_.active && ota_update_local_.req_id == req_id) {
    ota_update_local_.last_activity_ms = now_ms_;
    if (!success) {
      stopOtaUpdateLocal(false, status, ota_status_code, reason);
    } else if (!pull_->requestOtaApply(peer, ota_update_local_.image_name, ota_update_local_.req_id)) {
      stopOtaUpdateLocal(false,
                         ManagementStatus::InternalError,
                         static_cast<uint16_t>(OtaStatusCode::ApplyFailed),
                         "apply send failed");
    } else {
      ota_update_local_.phase = OtaUpdateLocalSession::Phase::WaitBoot;
      ota_update_local_.last_activity_ms = now_ms_;
    }
  }

  std::vector<uint8_t> payload;
  appendU32(payload, req_id);
  appendU32(payload, ota_push_local_.next_offset);
  appendU32(payload, ota_push_local_.total_size);
  appendU16(payload, ota_push_local_.chunks_sent);
  appendU16(payload, ota_status_code);
  appendStringU8(payload, (reason != nullptr) ? reason : "");
  if (emit_cmd_event) {
    queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
                source,
                owner_cmd_id,
                req_id,
                status,
                payload});
  }
  ota_push_local_ = OtaPushLocalSession{};
}

void ManagementService::pumpOtaUpdateLocal() {
  if (!ota_update_local_.active) {
    return;
  }

  if (!manager_.hasPersistedPair(ota_update_local_.peer)) {
    stopOtaUpdateLocal(false,
                       ManagementStatus::NotPaired,
                       static_cast<uint16_t>(OtaStatusCode::InvalidState),
                       "paired peer lost");
    return;
  }

  constexpr uint32_t kWaitBootTimeoutMs = 180000U;
  if (ota_update_local_.phase == OtaUpdateLocalSession::Phase::WaitBoot) {
    if (static_cast<int32_t>(now_ms_ - ota_update_local_.last_activity_ms) >=
        static_cast<int32_t>(kWaitBootTimeoutMs)) {
      stopOtaUpdateLocal(false,
                         ManagementStatus::Timeout,
                         static_cast<uint16_t>(OtaStatusCode::Timeout),
                         "boot-complete timeout");
    }
  }
}

void ManagementService::stopOtaUpdateLocal(bool success,
                                           ManagementStatus status,
                                           uint16_t ota_status_code,
                                           const char* reason) {
  if (!ota_update_local_.active) {
    return;
  }

  std::vector<uint8_t> payload;
  appendU32(payload, ota_update_local_.req_id);
  appendU8(payload, static_cast<uint8_t>(ota_update_local_.phase));
  appendU16(payload, ota_status_code);
  appendStringU8(payload, (reason != nullptr) ? reason : "");
  queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
              ota_update_local_.source,
              static_cast<uint16_t>(ManagementCommandId::OtaUpdateStart),
              ota_update_local_.req_id,
              status,
              payload});
  ota_update_local_ = OtaUpdateLocalSession{};
}

bool ManagementService::runMasterCritical(const ManagementRequest& request) {
  DeviceCommandContext ctx{};
  makeDeviceContext(request, ctx);
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(request.cmd_id);
  if (cmd == ManagementCommandId::OtaMasterUpdateStart) {
    if (!management_utils::parseOtaMasterUpdateStartPayload(request.payload, ctx.command_arg)) {
      queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::BadPayload);
      return true;
    }
  }
  if (device_policy_ != nullptr) {
    const DevicePolicyDecision d = device_policy_->authorizeCriticalCommand(ctx);
    if (d.code != DevicePolicyCode::AllowDeferred) {
      queueResponse(request.source, request.cmd_id, request.req_id, statusFromPolicy(d.code));
      return true;
    }
  }
  if (device_actions_ == nullptr || !device_actions_->queueCriticalCommand(ctx, nullptr)) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::DeniedByPolicy);
    return true;
  }
  if (cmd == ManagementCommandId::OtaMasterUpdateStart) {
    // OTA guard: suppress monitor probes while master update transaction is in progress.
    live_monitor_master_update_guard_until_ms_ = now_ms_ + kMasterUpdateLiveGuardMs;
    // OTA guard: pause telemetry push from all currently paired peers to minimize OTA contention.
    pauseTelemetryPushForAllPeersBestEffort(request.req_id);
  }
  queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::OkDeferred);
  return true;
}

bool ManagementService::runSlaveCritical(const ManagementRequest& request, uint16_t control_cmd_id) {
  if (pull_ == nullptr) {
    queueResponse(request.source, request.cmd_id, request.req_id, ManagementStatus::UnsupportedCommand);
    return true;
  }
  MacAddress peer{};
  PeerResolveContext peer_ctx{};
  if (!requirePairedPeer(request, peer, &peer_ctx)) return true;
  queueResponse(request.source, request.cmd_id, request.req_id,
                pull_->sendControlCommand(peer, control_cmd_id, request.req_id) ? ManagementStatus::OkDeferred
                                                                                 : ManagementStatus::InternalError,
                {},
                &peer_ctx);
  return true;
}

void ManagementService::registerDeferredLifecycleCommand(uint32_t req_id,
                                                         uint16_t cmd_id,
                                                         ManagementSource source) {
  if (req_id == 0U) {
    return;
  }
  for (auto& pending : deferred_lifecycle_commands_) {
    if (pending.req_id == req_id && pending.cmd_id == cmd_id) {
      pending.source = source;
      return;
    }
  }
  DeferredLifecycleCommand pending{};
  pending.req_id = req_id;
  pending.cmd_id = cmd_id;
  pending.source = source;
  deferred_lifecycle_commands_.push_back(pending);
}

bool ManagementService::consumeDeferredLifecycleCommand(uint32_t req_id,
                                                        uint16_t expected_cmd_id,
                                                        ManagementSource& out_source) {
  out_source = ManagementSource::Unknown;
  for (auto it = deferred_lifecycle_commands_.begin(); it != deferred_lifecycle_commands_.end(); ++it) {
    if (it->req_id != req_id || it->cmd_id != expected_cmd_id) {
      continue;
    }
    out_source = it->source;
    deferred_lifecycle_commands_.erase(it);
    return true;
  }
  return false;
}

uint8_t ManagementService::commandPriority(uint16_t cmd_id) {
  const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
  if (c == ManagementCommandId::PairRequest || c == ManagementCommandId::UnpairRequest ||
      c == ManagementCommandId::RemovePeerRequest ||
      c == ManagementCommandId::RestartMasterRequest || c == ManagementCommandId::ResetMasterRequest ||
      c == ManagementCommandId::RestartSlaveRequest || c == ManagementCommandId::ResetSlaveRequest ||
      c == ManagementCommandId::AudioPingRequest ||
      c == ManagementCommandId::LogLocalClear || c == ManagementCommandId::LogLocalControlSet ||
      c == ManagementCommandId::LogRemoteClear || c == ManagementCommandId::LogRemoteControlSet ||
      c == ManagementCommandId::StorageFormat || c == ManagementCommandId::OtaManifestRebuild ||
      c == ManagementCommandId::OtaClearScope || c == ManagementCommandId::OtaApply ||
      c == ManagementCommandId::OtaArchiveSaveRunning ||
      c == ManagementCommandId::OtaArchiveSaveStaged ||
      c == ManagementCommandId::OtaArchiveRestore ||
      c == ManagementCommandId::OtaArchiveDelete ||
      c == ManagementCommandId::OtaArchiveClear ||
      c == ManagementCommandId::OtaRollback ||
      c == ManagementCommandId::OtaTransferAbort ||
      c == ManagementCommandId::OtaPushAbort ||
      c == ManagementCommandId::OtaMasterUpdateStart ||
      c == ManagementCommandId::OtaUpdateStart ||
      c == ManagementCommandId::TopologyStageSet ||
      c == ManagementCommandId::TopologyCommit ||
      c == ManagementCommandId::TopologyTriggerSend ||
      c == ManagementCommandId::CommTestRun || c == ManagementCommandId::MetricsReset ||
      c == ManagementCommandId::CliControlSet ||
      c == ManagementCommandId::ChainLoopControlSet ||
      c == ManagementCommandId::ChannelSyncAll ||
      c == ManagementCommandId::LiveMonitorEnable || c == ManagementCommandId::LiveMonitorDisable) {
    return 0;
  }
  if (c == ManagementCommandId::StatusGet || c == ManagementCommandId::QueueGet ||
      c == ManagementCommandId::PairedSnapshotGet ||
      c == ManagementCommandId::MetricsGet ||
      c == ManagementCommandId::TopologyStatusGet ||
      c == ManagementCommandId::TopologySlotsGet ||
      c == ManagementCommandId::CommTestStatus || c == ManagementCommandId::CommTestReport ||
      c == ManagementCommandId::SettingGet ||
      c == ManagementCommandId::SettingSet || c == ManagementCommandId::LiveGet ||
      c == ManagementCommandId::LiveMonitorStatusGet ||
      c == ManagementCommandId::PingGet ||
      c == ManagementCommandId::TimeGet || c == ManagementCommandId::TimeSet ||
      c == ManagementCommandId::LogLocalStatusGet || c == ManagementCommandId::LogLocalRead ||
      c == ManagementCommandId::LogRemoteStatusGet || c == ManagementCommandId::LogRemoteRead ||
      c == ManagementCommandId::ChannelRuntimeGet ||
      c == ManagementCommandId::CapsPageGet || c == ManagementCommandId::TelemSchemaPageGet ||
      c == ManagementCommandId::SettingsPageGet ||
      c == ManagementCommandId::StorageInfoGet || c == ManagementCommandId::StorageList ||
      c == ManagementCommandId::StorageStat || c == ManagementCommandId::OtaStatusGet ||
      c == ManagementCommandId::OtaManifestGet || c == ManagementCommandId::OtaManifestPageGet ||
      c == ManagementCommandId::OtaArchiveVerify ||
      c == ManagementCommandId::OtaArchiveList ||
      c == ManagementCommandId::OtaCapacityGet || c == ManagementCommandId::OtaGateGet ||
      c == ManagementCommandId::OtaTransferBegin || c == ManagementCommandId::OtaTransferChunk ||
      c == ManagementCommandId::OtaTransferEnd ||
      c == ManagementCommandId::OtaPushStart || c == ManagementCommandId::OtaPushStatus ||
      c == ManagementCommandId::OtaUpdateStart) {
    return 1;
  }
  return 2;
}

ManagementAccessLevel ManagementService::commandRequiredAccessLevel(uint16_t cmd_id) {
  return requiredAccessLevel(static_cast<ManagementCommandId>(cmd_id));
}

bool ManagementService::isAsyncTerminalCommand(uint16_t cmd_id) {
  const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
  return c == ManagementCommandId::PairRequest ||
         c == ManagementCommandId::UnpairRequest ||
         c == ManagementCommandId::ChannelSyncAll ||
         c == ManagementCommandId::ChainLoopControlSet ||
         c == ManagementCommandId::OtaPushStart ||
         c == ManagementCommandId::OtaUpdateStart;
}

uint32_t ManagementService::commandTimeoutMs(uint16_t cmd_id) {
  const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
  if (c == ManagementCommandId::PairRequest || c == ManagementCommandId::UnpairRequest ||
      c == ManagementCommandId::CommTestRun) return 5000;
  if (c == ManagementCommandId::LogLocalRead || c == ManagementCommandId::LogRemoteRead) return 3000;
  if (c == ManagementCommandId::StorageFormat) return 30000;
  if (c == ManagementCommandId::OtaTransferBegin || c == ManagementCommandId::OtaTransferChunk ||
      c == ManagementCommandId::OtaTransferEnd || c == ManagementCommandId::OtaTransferAbort) {
    return 30000;
  }
  if (c == ManagementCommandId::RestartMasterRequest || c == ManagementCommandId::ResetMasterRequest ||
      c == ManagementCommandId::RestartSlaveRequest || c == ManagementCommandId::ResetSlaveRequest ||
      c == ManagementCommandId::AudioPingRequest ||
      c == ManagementCommandId::RemovePeerRequest ||
      c == ManagementCommandId::SettingSet || c == ManagementCommandId::LogLocalClear ||
      c == ManagementCommandId::LogLocalControlSet || c == ManagementCommandId::LogRemoteClear ||
      c == ManagementCommandId::LogRemoteControlSet ||
      c == ManagementCommandId::OtaManifestRebuild || c == ManagementCommandId::OtaClearScope ||
      c == ManagementCommandId::OtaArchiveSaveRunning ||
      c == ManagementCommandId::OtaArchiveSaveStaged ||
      c == ManagementCommandId::OtaArchiveRestore ||
      c == ManagementCommandId::OtaArchiveDelete ||
      c == ManagementCommandId::OtaArchiveClear ||
      c == ManagementCommandId::OtaApply || c == ManagementCommandId::OtaRollback ||
      c == ManagementCommandId::OtaPushStart || c == ManagementCommandId::OtaPushAbort ||
      c == ManagementCommandId::OtaMasterUpdateStart ||
      c == ManagementCommandId::OtaUpdateStart ||
      c == ManagementCommandId::TopologyStageSet ||
      c == ManagementCommandId::TopologyCommit ||
      c == ManagementCommandId::TopologyTriggerSend ||
      c == ManagementCommandId::MetricsReset ||
      c == ManagementCommandId::CliControlSet ||
      c == ManagementCommandId::ChainLoopControlSet ||
      c == ManagementCommandId::ChannelSyncAll ||
      isPushMutation(c)) {
    return 2500;
  }
  if (c == ManagementCommandId::OtaManifestGet ||
      c == ManagementCommandId::OtaManifestPageGet ||
      c == ManagementCommandId::OtaArchiveVerify ||
      c == ManagementCommandId::OtaArchiveList) {
    return 10000;
  }
  return 1500;
}

ManagementStatus ManagementService::statusFromPolicy(DevicePolicyCode code) {
  switch (code) {
    case DevicePolicyCode::AllowDeferred: return ManagementStatus::OkDeferred;
    case DevicePolicyCode::DenyNotPaired: return ManagementStatus::NotPaired;
    case DevicePolicyCode::DenySourceNotActiveMaster: return ManagementStatus::SourceNotActiveMaster;
    case DevicePolicyCode::DenyBusyPairing: return ManagementStatus::BusyPairing;
    case DevicePolicyCode::DenyUnpairInProgress: return ManagementStatus::UnpairInProgress;
    case DevicePolicyCode::DenyPolicy: return ManagementStatus::DeniedByPolicy;
    default: return ManagementStatus::InternalError;
  }
}

bool ManagementService::makeDeviceContext(const ManagementRequest& request, DeviceCommandContext& out_ctx) const {
  out_ctx = DeviceCommandContext{};
  out_ctx.local_role = local_role_;
  out_ctx.source = request.source;
  out_ctx.access_level = request.access_level;
  out_ctx.command_id = request.cmd_id;
  out_ctx.req_id = request.req_id;
  out_ctx.paired = manager_.isPaired();
  out_ctx.has_active_peer = manager_.getPairedPeer(out_ctx.active_peer);
  return true;
}

void ManagementService::onEvent(const Event& event) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (event.type == Event::Type::PullResponseSeen) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    if (channel_sync_all_.active) {
      for (size_t i = 0; i < channel_sync_all_.corr_ids.size(); ++i) {
        if (channel_sync_all_.corr_ids[i] == event.correlation_id &&
            channel_sync_all_.peers[i] == event.peer) {
          channel_sync_all_.acked[i] = 1U;
          break;
        }
      }
    }
    if (chain_loop_all_.active) {
      for (size_t i = 0; i < chain_loop_all_.corr_ids.size(); ++i) {
        if (chain_loop_all_.corr_ids[i] == event.correlation_id &&
            chain_loop_all_.peers[i] == event.peer) {
          chain_loop_all_.acked[i] = 1U;
          break;
        }
      }
    }
    return;
  }
  if (event.type == Event::Type::DiscoverySeen) {
    if (local_role_ == Role::Master && manager_.persistedPairCount() >= kMaxPairedSlaves) {
      if (discovery_active_) {
        discovery_active_ = false;
        manager_.setDiscoveryRxEnabled(false);
        queueEvent({ManagementEventId::DiscoveryStopped,
                    ManagementSource::Unknown,
                    0,
                    event.correlation_id,
                    ManagementStatus::Ok,
                    {}});
      }
      return;
    }
    if (std::find(discovered_.begin(), discovered_.end(), event.peer) == discovered_.end()) {
      discovered_.push_back(event.peer);
    }
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU16(payload, static_cast<uint16_t>(event.rssi));
    appendStringU8(payload, event.node_name);
    appendU8(payload, event.src_role);
    queueEvent({ManagementEventId::DiscoveryUpdate, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    return;
  }
  if (event.type == Event::Type::DiscoveryExpired) {
    discovered_.erase(std::remove(discovered_.begin(), discovered_.end(), event.peer), discovered_.end());
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    queueEvent({ManagementEventId::DiscoveryUpdate, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    return;
  }
  if (event.type == Event::Type::PairingStarted || event.type == Event::Type::PairingStep) {
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::PairProgress, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    if (event.message.find("unpair ack") != std::string::npos ||
        event.message.find("peer removed locally") != std::string::npos) {
      removeLivePeerState(event.peer);
      syncLiveMonitorPeers();
    }
    if (event.message.find("unpair") != std::string::npos) {
      const ManagementStatus unpair_status =
          (event.message.find("timeout") != std::string::npos) ? ManagementStatus::Timeout : ManagementStatus::Ok;
      queueEvent({ManagementEventId::UnpairResult,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  unpair_status,
                  payload});
      ManagementSource source = ManagementSource::Unknown;
      if (consumeDeferredLifecycleCommand(event.correlation_id,
                                          static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                                          source)) {
        if (unpair_status == ManagementStatus::Ok) {
          queueEvent({ManagementEventId::CmdDone,
                      source,
                      static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                      event.correlation_id,
                      ManagementStatus::Ok,
                      {}});
        } else if (unpair_status == ManagementStatus::Timeout) {
          queueEvent({ManagementEventId::Timeout,
                      source,
                      static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                      event.correlation_id,
                      ManagementStatus::Timeout,
                      {}});
        } else {
          queueEvent({ManagementEventId::CmdFail,
                      source,
                      static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                      event.correlation_id,
                      unpair_status,
                      {}});
        }
      }
    }
    return;
  }
  if (event.type == Event::Type::Paired) {
    syncLiveMonitorPeers();
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::PairResult, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    ManagementSource source = ManagementSource::Unknown;
    if (consumeDeferredLifecycleCommand(event.correlation_id,
                                        static_cast<uint16_t>(ManagementCommandId::PairRequest),
                                        source)) {
      queueEvent({ManagementEventId::CmdDone,
                  source,
                  static_cast<uint16_t>(ManagementCommandId::PairRequest),
                  event.correlation_id,
                  ManagementStatus::Ok,
                  {}});
    }
    if (local_role_ == Role::Master &&
        discovery_active_ &&
        manager_.persistedPairCount() >= kMaxPairedSlaves) {
      discovery_active_ = false;
      manager_.setDiscoveryRxEnabled(false);
      queueEvent({ManagementEventId::DiscoveryStopped,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  ManagementStatus::Ok,
                  {}});
    }
    return;
  }
  if (event.type == Event::Type::PairingFailed) {
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::PairResult, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::InternalError, payload});
    ManagementSource source = ManagementSource::Unknown;
    if (consumeDeferredLifecycleCommand(event.correlation_id,
                                        static_cast<uint16_t>(ManagementCommandId::PairRequest),
                                        source)) {
      const bool timeout = event.message.find("timeout") != std::string::npos;
      if (timeout) {
        queueEvent({ManagementEventId::Timeout,
                    source,
                    static_cast<uint16_t>(ManagementCommandId::PairRequest),
                    event.correlation_id,
                    ManagementStatus::Timeout,
                    {}});
      } else {
        queueEvent({ManagementEventId::CmdFail,
                    source,
                    static_cast<uint16_t>(ManagementCommandId::PairRequest),
                    event.correlation_id,
                    ManagementStatus::InternalError,
                    {}});
      }
    }
    return;
  }
  if (event.type == Event::Type::MandatoryEventReceived) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU16(payload, event.event_id);
    appendU8(payload, event.severity);
    appendU32(payload, static_cast<uint32_t>(event.event_value));
    appendU32(payload, event.event_ts_s);
    if (event.event_id == kOtaTransferReadyEventId) {
      if (ota_push_local_.active &&
          ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd &&
          event.peer == ota_push_local_.peer &&
          event.correlation_id == ota_push_local_.req_id) {
        stopOtaPushLocal(true,
                         ManagementStatus::Ok,
                         static_cast<uint16_t>(OtaStatusCode::Ok),
                         "local ota push complete");
      }
      queueEvent({ManagementEventId::OtaTransferReady,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  ManagementStatus::Ok,
                  payload});
      return;
    }
    if (event.event_id == kOtaBootCompleteEventId) {
      if (ota_update_local_.active &&
          ota_update_local_.phase == OtaUpdateLocalSession::Phase::WaitBoot &&
          event.peer == ota_update_local_.peer) {
        stopOtaUpdateLocal(true,
                           ManagementStatus::Ok,
                           static_cast<uint16_t>(OtaStatusCode::Ok),
                           "local ota update complete");
      }
      queueEvent({ManagementEventId::OtaBootComplete,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  ManagementStatus::Ok,
                  payload});
      return;
    }
  }
  if (event.type == Event::Type::OtaTransferStatus) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU8(payload, static_cast<uint8_t>(event.event_id & 0xFFU));
    appendU32(payload, static_cast<uint32_t>(event.event_value & 0x7FFFFFFFU));
    appendU16(payload, static_cast<uint16_t>(event.event_ts_s & 0xFFFFU));

    const uint8_t kind = static_cast<uint8_t>(event.event_id & 0xFFU);
    const uint32_t offset =
        static_cast<uint32_t>((event.event_value < 0) ? 0 : event.event_value);
    const uint16_t status_code = static_cast<uint16_t>(event.event_ts_s & 0xFFFFU);
    if (kind == kOtaStatusKindFinalizeOk || kind == kOtaStatusKindFinalizeFail) {
      (void)manager_.sendFirmwareFinalizeAck(event.peer,
                                             event.correlation_id,
                                             static_cast<uint32_t>(event.event_value & 0x7FFFFFFFU),
                                             status_code);
    }

    if (ota_push_local_.active && event.correlation_id == ota_push_local_.req_id) {
      ota_push_local_.last_activity_ms = now_ms_;
      if (kind == kOtaStatusKindChunkAck) {
        if (offset > ota_push_local_.remote_acked_offset) {
          ota_push_local_.remote_acked_offset = offset;
        }
        if (ota_push_local_.recovery_until_acked_offset != 0U &&
            ota_push_local_.remote_acked_offset >= ota_push_local_.recovery_until_acked_offset) {
          ota_push_local_.recovery_until_acked_offset = 0U;
          ota_push_local_.window_size_chunks = 16U;
        }
        if (!ota_push_local_.begin_acked && offset == 0U) {
          ota_push_local_.begin_acked = true;
          ota_push_local_.phase = OtaPushLocalSession::Phase::Streaming;
          ota_push_local_.waiting_chunk_ack = false;
          ota_push_local_.retry_count = 0U;
          ota_push_local_.next_send_ms = now_ms_;
        } else if (ota_push_local_.phase == OtaPushLocalSession::Phase::Streaming &&
                   ota_push_local_.waiting_chunk_ack &&
                   offset >= ota_push_local_.pending_end_offset) {
          ota_push_local_.waiting_chunk_ack = false;
          ota_push_local_.retry_count = 0U;
          ota_push_local_.pending_end_offset = 0U;
        }
      } else if (kind == kOtaStatusKindChunkNack) {
        if (ota_push_local_.phase == OtaPushLocalSession::Phase::Streaming) {
          uint32_t rewind_offset = std::min<uint32_t>(offset, ota_push_local_.total_size);
          if (rewind_offset > ota_push_local_.next_offset) {
            rewind_offset = ota_push_local_.next_offset;
          }
          if (rewind_offset < ota_push_local_.remote_acked_offset) {
            rewind_offset = ota_push_local_.remote_acked_offset;
          }
          ota_push_local_.remote_acked_offset = rewind_offset;
          ota_push_local_.waiting_chunk_ack = false;
          ota_push_local_.retry_count = 0U;
          ota_push_local_.next_offset = rewind_offset;
          ota_push_local_.pending_end_offset = 0U;
          ota_push_local_.window_size_chunks = 1U;
          ota_push_local_.recovery_until_acked_offset =
              std::min<uint32_t>(rewind_offset + static_cast<uint32_t>(ota_push_local_.chunk_bytes),
                                 ota_push_local_.total_size);
          ota_push_local_.next_send_ms = now_ms_ + 20U;
        }
      } else if (kind == kOtaStatusKindFinalizeOk) {
        if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd) {
          stopOtaPushLocal(true, ManagementStatus::Ok, status_code, "local ota push complete");
        }
      } else if (kind == kOtaStatusKindFinalizeFail) {
        if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd) {
          stopOtaPushLocal(false, ManagementStatus::InternalError, status_code, "local ota finalize failed");
        }
      }
    }

    const ManagementStatus status =
        (kind == kOtaStatusKindChunkNack || kind == kOtaStatusKindFinalizeFail)
            ? ManagementStatus::InternalError
            : ManagementStatus::Ok;
    queueEvent({ManagementEventId::OtaTransferStatus,
                ManagementSource::Unknown,
                0,
                event.correlation_id,
                status,
                payload});
    if (kind == kOtaStatusKindFinalizeAck) {
      return;
    }
    return;
  }
  if (event.type == Event::Type::TopologyTriggerReceived ||
      event.type == Event::Type::TopologyTriggerRejected ||
      event.type == Event::Type::TopologyTriggerDuplicate ||
      event.type == Event::Type::TopologyTriggerAck) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.reserve(23U);
    appendU8(payload, 1U);  // schema_version
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU16(payload, event.event_id);

    uint8_t state = 0U;
    ManagementEventId event_id = ManagementEventId::TopologyTriggerReceived;
    ManagementStatus status = ManagementStatus::Ok;
    if (event.type == Event::Type::TopologyTriggerRejected) {
      state = 1U;
      event_id = ManagementEventId::TopologyTriggerRejected;
      status = ManagementStatus::DeniedByRole;
    } else if (event.type == Event::Type::TopologyTriggerDuplicate) {
      state = 2U;
      event_id = ManagementEventId::TopologyTriggerReceived;
    } else if (event.type == Event::Type::TopologyTriggerAck) {
      state = 3U;
      event_id = ManagementEventId::TopologyTriggerAck;
      if (event.result != 0U) {
        status = ManagementStatus::DeniedByRole;
      }
    }

    appendU8(payload, state);
    appendU8(payload, event.reason);
    appendU8(payload, event.result);
    appendU8(payload, event.src_role);
    appendU8(payload, event.src_vid);
    appendU8(payload, event.dst_role);
    appendU8(payload, event.dst_vid);
    appendU8(payload, event.direction);
    appendU16(payload, event.delay_ms);
    appendU16(payload, event.hold_ms);
    appendU16(payload, event.ack_seq);
    queueEvent({event_id,
                ManagementSource::Unknown,
                0,
                event.correlation_id,
                status,
                payload});
    return;
  }
  if (event.type == Event::Type::LinkError || event.type == Event::Type::PacketDropped) {
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::CmdFail, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::InternalError, payload});
  }
}

}  // namespace espnow_link
