#pragma once

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "espnow_link/log_storage.hpp"
#include "espnow_link/firmware.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/telemetry_push.hpp"
#include "espnow_link/types.hpp"
#include "espnow_link/address.hpp"

namespace espnow_link {
namespace management_utils {

/** @brief Trim leading/trailing ASCII whitespace. */
inline std::string trim(const std::string& in) {
  size_t b = 0;
  while (b < in.size() && std::isspace(static_cast<unsigned char>(in[b])) != 0) {
    ++b;
  }
  size_t e = in.size();
  while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1])) != 0) {
    --e;
  }
  return in.substr(b, e - b);
}

/** @brief Lowercase ASCII string copy. */
inline std::string lowerAscii(std::string in) {
  for (char& c : in) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return in;
}

/** @brief Check whether string starts with prefix. */
inline bool startsWith(const std::string& s, const char* prefix) {
  if (prefix == nullptr) return false;
  const size_t n = std::strlen(prefix);
  return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

/** @brief Append little-endian uint16 to byte vector. */
inline void appendU16Le(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

/** @brief Append little-endian uint32 to byte vector. */
inline void appendU32Le(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

/** @brief Append little-endian uint64 to byte vector. */
inline void appendU64Le(std::vector<uint8_t>& out, uint64_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
}

/** @brief Read little-endian uint16 from byte buffer. */
inline bool readU16Le(const uint8_t* p, size_t len, uint16_t& out) {
  if (p == nullptr || len < 2) return false;
  out = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
  return true;
}

/** @brief Read little-endian uint32 from byte buffer. */
inline bool readU32Le(const uint8_t* p, size_t len, uint32_t& out) {
  if (p == nullptr || len < 4) return false;
  out = static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
  return true;
}

/** @brief Build payload for `DiscoveryStart` command. */
inline std::vector<uint8_t> buildDiscoveryStartPayload(uint32_t window_ms) {
  std::vector<uint8_t> p;
  appendU32Le(p, window_ms);
  return p;
}

/** @brief Build payload carrying one raw MAC address. */
inline std::vector<uint8_t> buildMacPayload(const MacAddress& mac) {
  return std::vector<uint8_t>(mac.begin(), mac.end());
}

/** @brief Build payload for key-based `SettingGet` command. */
inline std::vector<uint8_t> buildSettingGetByKeyPayload(const std::string& key) {
  std::vector<uint8_t> p;
  const size_t n = std::min<size_t>(key.size(), 255);
  p.push_back(0);  // mode: by key
  p.push_back(static_cast<uint8_t>(n));
  p.insert(p.end(), key.begin(), key.begin() + static_cast<std::string::difference_type>(n));
  return p;
}

/** @brief Build payload for id-based `SettingGet` command. */
inline std::vector<uint8_t> buildSettingGetByIdPayload(uint16_t setting_id) {
  std::vector<uint8_t> p;
  p.push_back(1);  // mode: by id
  appendU16Le(p, setting_id);
  return p;
}

/** @brief Build payload for key-based `SettingSet` command. */
inline std::vector<uint8_t> buildSettingSetByKeyPayload(const std::string& key, const std::string& value) {
  std::vector<uint8_t> p;
  const size_t key_n = std::min<size_t>(key.size(), 255);
  const size_t value_n = std::min<size_t>(value.size(), 0xFFFF);
  p.push_back(0);  // mode: by key
  p.push_back(static_cast<uint8_t>(key_n));
  p.insert(p.end(), key.begin(), key.begin() + static_cast<std::string::difference_type>(key_n));
  appendU16Le(p, static_cast<uint16_t>(value_n));
  p.insert(p.end(), value.begin(), value.begin() + static_cast<std::string::difference_type>(value_n));
  return p;
}

/** @brief Build payload for id-based `SettingSet` command. */
inline std::vector<uint8_t> buildSettingSetByIdPayload(uint16_t setting_id, const std::string& value) {
  std::vector<uint8_t> p;
  const size_t value_n = std::min<size_t>(value.size(), 0xFFFF);
  p.push_back(1);  // mode: by id
  appendU16Le(p, setting_id);
  appendU16Le(p, static_cast<uint16_t>(value_n));
  p.insert(p.end(), value.begin(), value.begin() + static_cast<std::string::difference_type>(value_n));
  return p;
}

/** @brief Build payload for `TimeSet` command. */
inline std::vector<uint8_t> buildTimeSetPayload(uint64_t epoch_s) {
  std::vector<uint8_t> p;
  appendU64Le(p, epoch_s);
  return p;
}

/** @brief Build payload carrying one UTF-8 string with uint16 length prefix. */
inline std::vector<uint8_t> buildStringPayloadU16(const std::string& value) {
  std::vector<uint8_t> p;
  const size_t n = std::min<size_t>(value.size(), 0xFFFFU);
  appendU16Le(p, static_cast<uint16_t>(n));
  p.insert(p.end(), value.begin(), value.begin() + static_cast<std::string::difference_type>(n));
  return p;
}

/** @brief Build payload for `LogLocalRead` / `LogRemoteRead`. */
inline std::vector<uint8_t> buildLogReadPayload(uint32_t offset, uint16_t max_bytes) {
  std::vector<uint8_t> p;
  appendU32Le(p, offset);
  appendU16Le(p, max_bytes);
  return p;
}

/** @brief Build payload for `LogLocalControlSet` / `LogRemoteControlSet`. */
inline std::vector<uint8_t> buildLogControlPayload(bool enabled) {
  return std::vector<uint8_t>{static_cast<uint8_t>(enabled ? 1U : 0U)};
}

/** @brief Build payload for `ChannelSyncAll` command. */
inline std::vector<uint8_t> buildChannelSyncAllPayload(uint8_t channel) {
  return std::vector<uint8_t>{channel};
}

/** @brief Build payload for `ChainLoopControlSet` command. */
inline std::vector<uint8_t> buildChainLoopControlPayload(bool enabled) {
  return std::vector<uint8_t>{static_cast<uint8_t>(enabled ? 1U : 0U)};
}

/** @brief Build payload for `OtaTransferBegin` command. */
inline std::vector<uint8_t> buildOtaTransferBeginPayload(uint32_t total_size,
                                                         uint32_t chunk_size,
                                                         uint32_t image_crc32,
                                                         const FirmwareImageMetadata* metadata = nullptr) {
  std::vector<uint8_t> p;
  p.reserve(12U + 3U + ((metadata != nullptr)
                            ? (metadata->sw_version.size() + metadata->build_id.size() + metadata->target_role.size())
                            : 0U));
  appendU32Le(p, total_size);
  appendU32Le(p, chunk_size);
  appendU32Le(p, image_crc32);
  if (metadata == nullptr) {
    return p;
  }
  const size_t sw_n = std::min<size_t>(metadata->sw_version.size(), 63U);
  const size_t build_n = std::min<size_t>(metadata->build_id.size(), 63U);
  const size_t role_n = std::min<size_t>(metadata->target_role.size(), 15U);
  p.push_back(static_cast<uint8_t>(sw_n));
  p.push_back(static_cast<uint8_t>(build_n));
  p.push_back(static_cast<uint8_t>(role_n));
  p.insert(p.end(), metadata->sw_version.begin(), metadata->sw_version.begin() + static_cast<std::string::difference_type>(sw_n));
  p.insert(p.end(), metadata->build_id.begin(), metadata->build_id.begin() + static_cast<std::string::difference_type>(build_n));
  if (role_n > 0U) {
    p.insert(p.end(),
             metadata->target_role.begin(),
             metadata->target_role.begin() + static_cast<std::string::difference_type>(role_n));
  }
  return p;
}

/** @brief Build payload for `OtaTransferChunk` command from raw bytes. */
inline std::vector<uint8_t> buildOtaTransferChunkPayload(uint32_t offset, const uint8_t* data, size_t len) {
  std::vector<uint8_t> p;
  if (data == nullptr || len == 0U) return p;
  p.reserve(4U + len);
  appendU32Le(p, offset);
  p.insert(p.end(), data, data + len);
  return p;
}

/** @brief Build payload for `OtaTransferChunk` command from vector bytes. */
inline std::vector<uint8_t> buildOtaTransferChunkPayload(uint32_t offset, const std::vector<uint8_t>& data) {
  if (data.empty()) return {};
  return buildOtaTransferChunkPayload(offset, data.data(), data.size());
}

/** @brief Build payload for `OtaTransferEnd` command. */
inline std::vector<uint8_t> buildOtaTransferEndPayload(uint32_t total_size, uint32_t image_crc32) {
  std::vector<uint8_t> p;
  appendU32Le(p, total_size);
  appendU32Le(p, image_crc32);
  return p;
}

/** @brief Build payload for `OtaPushStart` command. */
inline std::vector<uint8_t> buildOtaPushStartPayload(const std::string& local_path, uint16_t chunk_bytes) {
  std::vector<uint8_t> p;
  const size_t n = std::min<size_t>(local_path.size(), 0xFFFFU);
  p.reserve(4U + n);
  appendU16Le(p, static_cast<uint16_t>(n));
  p.insert(p.end(),
           local_path.begin(),
           local_path.begin() + static_cast<std::string::difference_type>(n));
  appendU16Le(p, chunk_bytes);
  return p;
}

/** @brief Build payload for OTA archive commands (`scope + role + optional id`). */
inline std::vector<uint8_t> buildOtaArchivePayload(char role,
                                                   const std::string& id = {},
                                                   bool remote = false) {
  const char normalized_role = (role == 's' || role == 'S') ? 's' : 'm';
  std::vector<uint8_t> p;
  const size_t id_n = std::min<size_t>(id.size(), 63U);
  p.reserve(3U + id_n);
  p.push_back(static_cast<uint8_t>(remote ? 1U : 0U));
  p.push_back(static_cast<uint8_t>(normalized_role));
  p.push_back(static_cast<uint8_t>(id_n));
  if (id_n > 0U) {
    p.insert(p.end(),
             id.begin(),
             id.begin() + static_cast<std::string::difference_type>(id_n));
  }
  return p;
}

/** @brief Build payload for local master staged OTA apply command. */
inline std::vector<uint8_t> buildOtaMasterUpdateStartPayload(const std::string& local_path) {
  return buildStringPayloadU16(local_path);
}

/** @brief Parse payload for `OtaPushStart` command. */
inline bool parseOtaPushStartPayload(const std::vector<uint8_t>& payload,
                                     std::string& out_local_path,
                                     uint16_t& out_chunk_bytes) {
  out_local_path.clear();
  out_chunk_bytes = 0U;
  if (payload.size() < 4U) return false;
  uint16_t n = 0U;
  if (!readU16Le(payload.data(), payload.size(), n)) return false;
  const size_t chunk_off = static_cast<size_t>(2U + n);
  if (payload.size() != chunk_off + 2U) return false;
  out_local_path.assign(reinterpret_cast<const char*>(payload.data() + 2U), n);
  return readU16Le(payload.data() + chunk_off, payload.size() - chunk_off, out_chunk_bytes);
}

/** @brief Parse payload for OTA archive commands (`role + optional id`). */
inline bool parseOtaArchivePayload(const std::vector<uint8_t>& payload,
                                   char& out_role,
                                   std::string& out_id,
                                   bool& out_remote,
                                   bool require_id = false) {
  out_role = 'm';
  out_id.clear();
  out_remote = false;
  if (payload.size() < 3U) return false;
  if (payload[0U] > 1U) return false;
  out_remote = (payload[0U] != 0U);
  const char role = static_cast<char>(payload[1U]);
  if (role != 'm' && role != 'M' && role != 's' && role != 'S') return false;
  out_role = (role == 's' || role == 'S') ? 's' : 'm';
  const uint8_t id_n = payload[2U];
  if (payload.size() != static_cast<size_t>(3U + id_n)) return false;
  if (id_n > 0U) {
    out_id.assign(reinterpret_cast<const char*>(payload.data() + 3U), id_n);
  }
  if (require_id && out_id.empty()) return false;
  return true;
}

/** @brief Parse payload for local master staged OTA apply command. */
inline bool parseOtaMasterUpdateStartPayload(const std::vector<uint8_t>& payload,
                                             std::string& out_local_path) {
  out_local_path.clear();
  uint16_t n = 0;
  if (!readU16Le(payload.data(), payload.size(), n)) return false;
  if (payload.size() != static_cast<size_t>(2U + n)) return false;
  out_local_path.assign(reinterpret_cast<const char*>(payload.data() + 2), n);
  return !out_local_path.empty();
}

/** @brief Parse payload for log read commands. */
inline bool parseLogReadPayload(const std::vector<uint8_t>& payload, uint32_t& out_offset, uint16_t& out_max_bytes) {
  out_offset = 0;
  out_max_bytes = 0;
  if (payload.size() != 4U && payload.size() != 6U) return false;
  if (!readU32Le(payload.data(), payload.size(), out_offset)) return false;
  if (payload.size() == 4U) {
    return true;
  }
  return readU16Le(payload.data() + 4, payload.size() - 4, out_max_bytes) && out_max_bytes > 0U;
}

/** @brief Parse payload for log control commands. */
inline bool parseLogControlPayload(const std::vector<uint8_t>& payload, bool& out_enabled) {
  out_enabled = false;
  if (payload.size() != 1U) return false;
  if (payload[0] > 1U) return false;
  out_enabled = (payload[0] != 0U);
  return true;
}

/** @brief Parse payload for `ChannelSyncAll` command. */
inline bool parseChannelSyncAllPayload(const std::vector<uint8_t>& payload, uint8_t& out_channel) {
  out_channel = 0U;
  if (payload.size() != 1U) return false;
  if (payload[0] < 1U || payload[0] > 14U) return false;
  out_channel = payload[0];
  return true;
}

/** @brief Parse payload for `ChainLoopControlSet` command. */
inline bool parseChainLoopControlPayload(const std::vector<uint8_t>& payload,
                                         bool& out_has_value,
                                         bool& out_enabled) {
  out_has_value = false;
  out_enabled = false;
  if (payload.empty()) {
    return true;
  }
  if (payload.size() != 1U || payload[0] > 1U) {
    return false;
  }
  out_has_value = true;
  out_enabled = (payload[0] != 0U);
  return true;
}

/** @brief Parse one UTF-8 string from uint16-length-prefixed payload. */
inline bool parseStringPayloadU16(const std::vector<uint8_t>& payload, std::string& out_value) {
  out_value.clear();
  uint16_t n = 0;
  if (!readU16Le(payload.data(), payload.size(), n)) return false;
  if (payload.size() != static_cast<size_t>(2U + n)) return false;
  out_value.assign(reinterpret_cast<const char*>(payload.data() + 2), n);
  return true;
}

/** @brief Parse logger status payload returned by management service. */
inline bool parseLogStatusPayload(const std::vector<uint8_t>& payload,
                                  bool& out_available,
                                  bool& out_enabled,
                                  uint8_t& out_min_level,
                                  LogStorageStats& out_stats) {
  out_available = false;
  out_enabled = false;
  out_min_level = 0;
  out_stats = LogStorageStats{};
  if (payload.size() < 20U) return false;

  out_available = (payload[0] != 0U);
  out_enabled = (payload[1] != 0U);
  out_min_level = payload[2];

  uint32_t used = 0;
  uint32_t dropped = 0;
  uint32_t records = 0;
  uint32_t rotations = 0;
  if (!readU32Le(payload.data() + 4, payload.size() - 4, used)) return false;
  if (!readU32Le(payload.data() + 8, payload.size() - 8, dropped)) return false;
  if (!readU32Le(payload.data() + 12, payload.size() - 12, records)) return false;
  if (!readU32Le(payload.data() + 16, payload.size() - 16, rotations)) return false;

  out_stats.available = out_available;
  out_stats.bytes_used = used;
  out_stats.bytes_dropped = dropped;
  out_stats.records_appended = records;
  out_stats.rotations = rotations;
  return true;
}

/** @brief Parse logger read payload returned by management service. */
inline bool parseLogReadResponsePayload(const std::vector<uint8_t>& payload,
                                        uint32_t& out_offset,
                                        uint32_t& out_total_size,
                                        std::vector<uint8_t>& out_chunk) {
  out_offset = 0;
  out_total_size = 0;
  out_chunk.clear();
  if (payload.size() < 10U) return false;

  uint16_t chunk_len = 0;
  if (!readU32Le(payload.data(), payload.size(), out_offset)) return false;
  if (!readU32Le(payload.data() + 4, payload.size() - 4, out_total_size)) return false;
  if (!readU16Le(payload.data() + 8, payload.size() - 8, chunk_len)) return false;
  if (payload.size() != static_cast<size_t>(10U + chunk_len)) return false;

  if (chunk_len > 0U) {
    out_chunk.assign(payload.begin() + 10, payload.end());
  }
  return true;
}

/** @brief Parse payload returned by `ChannelRuntimeGet`. */
inline bool parseRuntimeChannelStatusPayload(const std::vector<uint8_t>& payload,
                                             ManagementRuntimeChannelStatusPayload& out_status) {
  out_status = ManagementRuntimeChannelStatusPayload{};
  if (payload.size() < 2U) return false;
  out_status.current_channel = payload[0];
  const uint8_t count = payload[1];
  size_t off = 2U;
  out_status.entries.clear();
  out_status.entries.reserve(count);
  for (uint8_t i = 0; i < count; ++i) {
    if (payload.size() < off + 8U) return false;
    ManagementRuntimeChannelEntryPayload e{};
    std::memcpy(e.peer.data(), payload.data() + off, 6U);
    off += 6U;
    e.channel = payload[off++];
    const uint8_t key_len = payload[off++];
    if (payload.size() < off + key_len) return false;
    e.key.assign(reinterpret_cast<const char*>(payload.data() + off), key_len);
    off += key_len;
    out_status.entries.push_back(e);
  }
  return off == payload.size();
}

/** @brief Parse `CmdDone/CmdFail` payload emitted for `ChannelSyncAll`. */
inline bool parseChannelSyncAllResultPayload(const std::vector<uint8_t>& payload,
                                             ManagementChannelSyncAllResultPayload& out_result) {
  out_result = ManagementChannelSyncAllResultPayload{};
  if (payload.size() != 3U) return false;
  out_result.channel = payload[0];
  out_result.acked_peers = payload[1];
  out_result.total_peers = payload[2];
  return true;
}

/** @brief Parse `CmdDone/CmdFail` payload emitted for `ChainLoopControlSet`. */
inline bool parseChainLoopResultPayload(const std::vector<uint8_t>& payload,
                                        ManagementChainLoopResultPayload& out_result) {
  out_result = ManagementChainLoopResultPayload{};
  if (payload.size() != 3U || payload[0] > 1U) {
    return false;
  }
  out_result.enabled = (payload[0] != 0U);
  out_result.acked_peers = payload[1];
  out_result.total_peers = payload[2];
  return true;
}

/** @brief Parse one peer-list payload (`<count:u8><mac:6>*`). */
inline bool parsePeerListPayload(const std::vector<uint8_t>& payload, std::vector<MacAddress>& out) {
  out.clear();
  if (payload.empty()) return false;
  const size_t count = static_cast<size_t>(payload[0]);
  size_t off = 1U;
  for (size_t i = 0; i < count; ++i) {
    if (payload.size() < (off + 6U)) break;
    MacAddress mac{};
    std::memcpy(mac.data(), payload.data() + off, 6U);
    off += 6U;
    out.push_back(mac);
  }
  return true;
}

/** @brief Parse payload from `DiscoverySnapshotGet` response. */
inline bool parseDiscoverySnapshotPayload(const std::vector<uint8_t>& payload, std::vector<MacAddress>& out) {
  return parsePeerListPayload(payload, out);
}

/** @brief Parse payload from `PairedSnapshotGet` response (`pair_seq` ordered list with role hints). */
inline bool parsePairedSnapshotPayload(const std::vector<uint8_t>& payload,
                                       std::vector<ManagementPairedPeerInfo>& out) {
  out.clear();
  if (payload.empty()) return false;
  const size_t count = static_cast<size_t>(payload[0]);
  const size_t expected = 1U + (count * 7U);
  if (payload.size() != expected) return false;

  size_t off = 1U;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (payload.size() < (off + 7U)) return false;
    ManagementPairedPeerInfo entry{};
    std::memcpy(entry.peer.data(), payload.data() + off, 6U);
    off += 6U;
    entry.role_code = payload[off];
    off += 1U;
    out.push_back(entry);
  }
  return out.size() == count;
}

/** @brief Parse payload from `PairedSnapshotGet` response and return peer MAC list only. */
inline bool parsePairedSnapshotPayload(const std::vector<uint8_t>& payload, std::vector<MacAddress>& out) {
  out.clear();
  std::vector<ManagementPairedPeerInfo> detailed{};
  if (!parsePairedSnapshotPayload(payload, detailed)) {
    return false;
  }
  out.reserve(detailed.size());
  for (const auto& e : detailed) {
    out.push_back(e.peer);
  }
  return true;
}

/** @brief Build payload for `TopologySlotsGet` (empty => committed by default). */
inline std::vector<uint8_t> buildTopologySlotsGetPayload(bool committed = true) {
  return std::vector<uint8_t>{static_cast<uint8_t>(committed ? 2U : 1U)};
}

/** @brief Build payload for `TopologyTriggerSend`. */
inline std::vector<uint8_t> buildTopologyTriggerSendPayload(
    const ManagementTopologyTriggerSendPayload& trigger) {
  std::vector<uint8_t> payload;
  payload.reserve(7U);
  payload.push_back(static_cast<uint8_t>(trigger.target_index));
  payload.push_back(trigger.direction);
  appendU16Le(payload, trigger.delay_ms);
  appendU16Le(payload, trigger.hold_ms);
  payload.push_back(trigger.source_virtual_index);
  return payload;
}

/** @brief Parse payload for `TopologyTriggerSend` request. */
inline bool parseTopologyTriggerSendPayload(const std::vector<uint8_t>& payload,
                                            ManagementTopologyTriggerSendPayload& out_trigger) {
  out_trigger = ManagementTopologyTriggerSendPayload{};
  if (payload.size() != 7U) {
    return false;
  }
  out_trigger.target_index = static_cast<int8_t>(payload[0U]);
  out_trigger.direction = payload[1U];
  if (!readU16Le(payload.data() + 2U, payload.size() - 2U, out_trigger.delay_ms)) {
    return false;
  }
  if (!readU16Le(payload.data() + 4U, payload.size() - 4U, out_trigger.hold_ms)) {
    return false;
  }
  out_trigger.source_virtual_index = payload[6U];
  return true;
}

/** @brief Parse payload for `TopologyTriggerSend` response. */
inline bool parseTopologyTriggerSendResponsePayload(
    const std::vector<uint8_t>& payload,
    ManagementTopologyTriggerSendResponsePayload& out_response) {
  out_response = ManagementTopologyTriggerSendResponsePayload{};
  if (payload.size() != 2U) {
    return false;
  }
  return readU16Le(payload.data(), payload.size(), out_response.seq);
}

/** @brief Build payload for `TopologyStageSet`. */
inline std::vector<uint8_t> buildTopologyStagePayload(const ManagementTopologySnapshotPayload& snapshot) {
  std::vector<uint8_t> payload;
  const size_t slot_n = std::min<size_t>(snapshot.slots.size(), kManagementTopologyMaxSlots);
  const size_t group_n = std::min<size_t>(snapshot.groups.size(), kManagementTopologyMaxGroups);
  payload.reserve(12U + slot_n * 18U + group_n * 36U);
  payload.push_back(snapshot.schema_version);
  payload.push_back(0U);  // flags/reserved
  appendU32Le(payload, snapshot.topology_version);
  payload.push_back(snapshot.index_neg);
  payload.push_back(snapshot.index_pos);
  payload.push_back(static_cast<uint8_t>(slot_n));
  payload.push_back(static_cast<uint8_t>(group_n));

  for (size_t i = 0; i < slot_n; ++i) {
    const auto& slot = snapshot.slots[i];
    payload.push_back(slot.slot_index);
    payload.push_back(static_cast<uint8_t>(slot.enabled ? 1U : 0U));
    payload.insert(payload.end(), slot.peer.begin(), slot.peer.end());
    payload.push_back(slot.peer_role);
    payload.push_back(slot.group_id);
    payload.push_back(static_cast<uint8_t>(slot.relative_index));
    payload.push_back(slot.local_virtual_index);
    payload.push_back(slot.peer_virtual_index);
    payload.push_back(static_cast<uint8_t>(slot.axis_order));
    appendU16Le(payload, slot.delay_ms);
    appendU16Le(payload, slot.hold_ms);
  }

  for (size_t i = 0; i < group_n; ++i) {
    const auto& group = snapshot.groups[i];
    payload.push_back(group.group_slot);
    payload.push_back(static_cast<uint8_t>(group.enabled ? 1U : 0U));
    payload.push_back(group.group_id);
    const uint8_t seed_len = static_cast<uint8_t>(group.enabled ? group.seed.size() : 0U);
    payload.push_back(seed_len);
    if (seed_len > 0U) {
      payload.insert(payload.end(), group.seed.begin(), group.seed.end());
    }
  }
  return payload;
}

/** @brief Parse payload for `TopologySlotsGet` request. */
inline bool parseTopologySlotsGetPayload(const std::vector<uint8_t>& payload, bool& out_committed) {
  out_committed = true;
  if (payload.empty()) {
    return true;
  }
  if (payload.size() != 1U) {
    return false;
  }
  if (payload[0U] == 2U) {
    out_committed = true;
    return true;
  }
  if (payload[0U] == 1U) {
    out_committed = false;
    return true;
  }
  return false;
}

/** @brief Parse payload for `TopologyStageSet` request. */
inline bool parseTopologyStagePayload(const std::vector<uint8_t>& payload,
                                      ManagementTopologySnapshotPayload& out_snapshot) {
  out_snapshot = ManagementTopologySnapshotPayload{};
  if (payload.size() < 10U) {
    return false;
  }
  out_snapshot.schema_version = payload[0U];
  if (!readU32Le(payload.data() + 2U, payload.size() - 2U, out_snapshot.topology_version)) {
    return false;
  }
  out_snapshot.index_neg = payload[6U];
  out_snapshot.index_pos = payload[7U];
  const uint8_t slot_n = payload[8U];
  const uint8_t group_n = payload[9U];
  if (slot_n > kManagementTopologyMaxSlots || group_n > kManagementTopologyMaxGroups) {
    return false;
  }

  size_t off = 10U;
  out_snapshot.slots.reserve(slot_n);
  std::array<bool, kManagementTopologyMaxSlots> seen_slots{};
  for (uint8_t i = 0; i < slot_n; ++i) {
    if (payload.size() < off + 18U) {
      return false;
    }
    ManagementTopologySlotPayload slot{};
    slot.slot_index = payload[off + 0U];
    if (slot.slot_index >= kManagementTopologyMaxSlots || seen_slots[slot.slot_index]) {
      return false;
    }
    seen_slots[slot.slot_index] = true;
    slot.enabled = (payload[off + 1U] != 0U);
    std::memcpy(slot.peer.data(), payload.data() + off + 2U, 6U);
    slot.peer_role = payload[off + 8U];
    slot.group_id = payload[off + 9U];
    slot.relative_index = static_cast<int8_t>(payload[off + 10U]);
    slot.local_virtual_index = payload[off + 11U];
    slot.peer_virtual_index = payload[off + 12U];
    slot.axis_order = static_cast<int8_t>(payload[off + 13U]);
    if (!readU16Le(payload.data() + off + 14U, payload.size() - (off + 14U), slot.delay_ms)) {
      return false;
    }
    if (!readU16Le(payload.data() + off + 16U, payload.size() - (off + 16U), slot.hold_ms)) {
      return false;
    }
    out_snapshot.slots.push_back(slot);
    off += 18U;
  }

  out_snapshot.groups.reserve(group_n);
  std::array<bool, kManagementTopologyMaxGroups> seen_groups{};
  for (uint8_t i = 0; i < group_n; ++i) {
    if (payload.size() < off + 4U) {
      return false;
    }
    ManagementTopologyGroupSeedPayload group{};
    group.group_slot = payload[off + 0U];
    if (group.group_slot >= kManagementTopologyMaxGroups || seen_groups[group.group_slot]) {
      return false;
    }
    seen_groups[group.group_slot] = true;
    group.enabled = (payload[off + 1U] != 0U);
    group.group_id = payload[off + 2U];
    const uint8_t seed_len = payload[off + 3U];
    off += 4U;
    if (!group.enabled && seed_len != 0U) {
      return false;
    }
    if (group.enabled && seed_len != 32U) {
      return false;
    }
    if (payload.size() < off + seed_len) {
      return false;
    }
    if (seed_len == 32U) {
      std::memcpy(group.seed.data(), payload.data() + off, 32U);
    }
    off += seed_len;
    out_snapshot.groups.push_back(group);
  }
  return off == payload.size();
}

/** @brief Parse payload from `TopologyStatusGet` response. */
inline bool parseTopologyStatusPayload(const std::vector<uint8_t>& payload,
                                       ManagementTopologyStatusPayload& out_status) {
  out_status = ManagementTopologyStatusPayload{};
  if (payload.size() != 28U) {
    return false;
  }
  out_status.schema_version = payload[0U];
  out_status.has_staged = (payload[1U] != 0U);
  out_status.has_committed = (payload[2U] != 0U);
  if (!readU32Le(payload.data() + 4U, payload.size() - 4U, out_status.staged_version)) return false;
  if (!readU32Le(payload.data() + 8U, payload.size() - 8U, out_status.committed_version)) return false;
  out_status.staged_state = payload[12U];
  out_status.committed_state = payload[13U];
  out_status.staged_slot_count = payload[14U];
  out_status.committed_slot_count = payload[15U];
  out_status.staged_group_count = payload[16U];
  out_status.committed_group_count = payload[17U];
  out_status.index_neg = payload[18U];
  out_status.index_pos = payload[19U];
  if (!readU32Le(payload.data() + 20U, payload.size() - 20U, out_status.staged_checksum)) return false;
  if (!readU32Le(payload.data() + 24U, payload.size() - 24U, out_status.committed_checksum)) return false;
  return true;
}

/** @brief Parse payload from `TopologySlotsGet` response. */
inline bool parseTopologySlotsPayload(const std::vector<uint8_t>& payload,
                                      uint8_t& out_state,
                                      std::vector<ManagementTopologySlotPayload>& out_slots) {
  out_state = 0U;
  out_slots.clear();
  if (payload.size() < 3U) {
    return false;
  }
  const uint8_t schema_version = payload[0U];
  (void)schema_version;
  out_state = payload[1U];
  const uint8_t slot_n = payload[2U];
  if (slot_n > kManagementTopologyMaxSlots) {
    return false;
  }
  size_t off = 3U;
  out_slots.reserve(slot_n);
  for (uint8_t i = 0; i < slot_n; ++i) {
    if (payload.size() < off + 18U) {
      return false;
    }
    ManagementTopologySlotPayload slot{};
    slot.slot_index = payload[off + 0U];
    slot.enabled = (payload[off + 1U] != 0U);
    std::memcpy(slot.peer.data(), payload.data() + off + 2U, 6U);
    slot.peer_role = payload[off + 8U];
    slot.group_id = payload[off + 9U];
    slot.relative_index = static_cast<int8_t>(payload[off + 10U]);
    slot.local_virtual_index = payload[off + 11U];
    slot.peer_virtual_index = payload[off + 12U];
    slot.axis_order = static_cast<int8_t>(payload[off + 13U]);
    if (!readU16Le(payload.data() + off + 14U, payload.size() - (off + 14U), slot.delay_ms)) {
      return false;
    }
    if (!readU16Le(payload.data() + off + 16U, payload.size() - (off + 16U), slot.hold_ms)) {
      return false;
    }
    out_slots.push_back(slot);
    off += 18U;
  }
  return off == payload.size();
}

/**
 * @brief Resolve one frontend peer selector against paired list.
 *
 * Supported selector forms:
 * - bare decimal index (preferred)
 * - `AA:BB:CC:DD:EE:FF`
 */
inline bool resolveTargetPeerSelector(const std::string& selector,
                                      const std::vector<MacAddress>& paired_list,
                                      MacAddress& out_peer,
                                      size_t* out_index = nullptr) {
  if (out_index != nullptr) {
    *out_index = 0U;
  }
  const std::string raw = trim(selector);
  if (raw.empty()) {
    return false;
  }
  std::string index_token;
  bool looks_like_index = false;
  bool digits_only = true;
  for (char c : raw) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      digits_only = false;
      break;
    }
  }
  if (digits_only) {
    looks_like_index = true;
    index_token = raw;
  }

  if (looks_like_index) {
    if (index_token.empty()) {
      return false;
    }
    const unsigned long idx = std::strtoul(index_token.c_str(), nullptr, 10);
    if (idx >= paired_list.size()) {
      return false;
    }
    out_peer = paired_list[static_cast<size_t>(idx)];
    if (out_index != nullptr) {
      *out_index = static_cast<size_t>(idx);
    }
    return true;
  }

  return parseMac(raw, out_peer);
}

/** @brief Parse payload from discovery update event into typed fields. */
inline bool parseDiscoveryUpdatePayload(const std::vector<uint8_t>& payload,
                                        ManagementDiscoveryUpdatePayload& out) {
  out = ManagementDiscoveryUpdatePayload{};
  if (payload.size() < 6U) return false;
  std::memcpy(out.peer.data(), payload.data(), 6U);

  if (payload.size() < 8U) {
    return true;
  }
  const uint16_t raw = static_cast<uint16_t>(payload[6U]) |
                       (static_cast<uint16_t>(payload[7U]) << 8);
  out.rssi = static_cast<int16_t>(raw);

  if (payload.size() < 9U) {
    return true;
  }
  const uint8_t name_len = payload[8U];
  const size_t name_off = 9U;
  if (payload.size() < (name_off + static_cast<size_t>(name_len))) {
    return true;
  }
  out.name.assign(reinterpret_cast<const char*>(payload.data() + name_off), name_len);

  const size_t role_off = name_off + static_cast<size_t>(name_len);
  if (payload.size() > role_off) {
    out.role_code = payload[role_off];
  }
  return true;
}

/** @brief Parse peer+message payload (pair/unpair progress + pair result). */
inline bool parsePeerMessagePayload(const std::vector<uint8_t>& payload,
                                    ManagementPeerMessagePayload& out) {
  out = ManagementPeerMessagePayload{};
  if (payload.size() < 7U) return false;
  std::memcpy(out.peer.data(), payload.data(), 6U);
  const uint8_t msg_len = payload[6U];
  if (payload.size() != static_cast<size_t>(7U + msg_len)) return false;
  if (msg_len > 0U) {
    out.message.assign(reinterpret_cast<const char*>(payload.data() + 7U), msg_len);
  }
  return true;
}

/** @brief Parse peer removal payload (`<mac:6><removed:1>`). */
inline bool parsePeerRemovedPayload(const std::vector<uint8_t>& payload,
                                    ManagementPeerRemovedPayload& out) {
  out = ManagementPeerRemovedPayload{};
  if (payload.size() != 7U) return false;
  std::memcpy(out.peer.data(), payload.data(), 6U);
  out.removed = (payload[6U] != 0U);
  return true;
}

/** @brief Parse mandatory event payload (`<mac:6><evt:2><sev:1><value:4><ts:4>`). */
inline bool parseMandatoryEventPayload(const std::vector<uint8_t>& payload,
                                       ManagementMandatoryEventPayload& out) {
  out = ManagementMandatoryEventPayload{};
  if (payload.size() != 17U) return false;
  std::memcpy(out.peer.data(), payload.data(), 6U);
  if (!readU16Le(payload.data() + 6U, payload.size() - 6U, out.event_id)) return false;
  out.severity = payload[8U];
  uint32_t value_u32 = 0U;
  if (!readU32Le(payload.data() + 9U, payload.size() - 9U, value_u32)) return false;
  out.event_value = static_cast<int32_t>(value_u32);
  if (!readU32Le(payload.data() + 13U, payload.size() - 13U, out.event_ts_s)) return false;
  return true;
}

/** @brief Parse deferred OTA push start payload (`req,size,crc,chunk,path`). */
inline bool parseOtaPushStartResponsePayload(const std::vector<uint8_t>& payload,
                                             ManagementOtaPushStartPayload& out) {
  out = ManagementOtaPushStartPayload{};
  if (payload.size() < 15U) return false;
  if (!readU32Le(payload.data() + 0U, payload.size(), out.req_id)) return false;
  if (!readU32Le(payload.data() + 4U, payload.size() - 4U, out.total_size)) return false;
  if (!readU32Le(payload.data() + 8U, payload.size() - 8U, out.image_crc32)) return false;
  if (!readU16Le(payload.data() + 12U, payload.size() - 12U, out.chunk_bytes)) return false;
  const uint8_t n = payload[14U];
  if (payload.size() != static_cast<size_t>(15U + n)) return false;
  if (n > 0U) {
    out.local_path.assign(reinterpret_cast<const char*>(payload.data() + 15U), n);
  }
  return true;
}

/** @brief Parse `OtaPushStatus` response payload. */
inline bool parseOtaPushStatusPayload(const std::vector<uint8_t>& payload,
                                      ManagementOtaPushStatusPayload& out) {
  out = ManagementOtaPushStatusPayload{};
  if (payload.size() < 19U) return false;
  out.active = (payload[0U] != 0U);
  out.phase = payload[1U];
  if (!readU16Le(payload.data() + 2U, payload.size() - 2U, out.chunk_bytes)) return false;
  if (!readU32Le(payload.data() + 4U, payload.size() - 4U, out.req_id)) return false;
  if (!readU32Le(payload.data() + 8U, payload.size() - 8U, out.next_offset)) return false;
  if (!readU32Le(payload.data() + 12U, payload.size() - 12U, out.total_size)) return false;
  if (!readU16Le(payload.data() + 16U, payload.size() - 16U, out.chunks_sent)) return false;
  const uint8_t n = payload[18U];
  if (payload.size() != static_cast<size_t>(19U + n)) return false;
  if (n > 0U) {
    out.image_name.assign(reinterpret_cast<const char*>(payload.data() + 19U), n);
  }
  return true;
}

/** @brief Parse OTA push lifecycle payload from `CmdDone/CmdFail` events. */
inline bool parseOtaPushResultPayload(const std::vector<uint8_t>& payload,
                                      ManagementOtaPushResultPayload& out) {
  out = ManagementOtaPushResultPayload{};
  if (payload.size() < 17U) return false;
  if (!readU32Le(payload.data() + 0U, payload.size(), out.req_id)) return false;
  if (!readU32Le(payload.data() + 4U, payload.size() - 4U, out.next_offset)) return false;
  if (!readU32Le(payload.data() + 8U, payload.size() - 8U, out.total_size)) return false;
  if (!readU16Le(payload.data() + 12U, payload.size() - 12U, out.chunks_sent)) return false;
  if (!readU16Le(payload.data() + 14U, payload.size() - 14U, out.ota_status_code)) return false;
  const uint8_t n = payload[16U];
  if (payload.size() != static_cast<size_t>(17U + n)) return false;
  if (n > 0U) {
    out.reason.assign(reinterpret_cast<const char*>(payload.data() + 17U), n);
  }
  return true;
}

/** @brief Parse OTA update lifecycle payload from `CmdDone/CmdFail` events. */
inline bool parseOtaUpdateResultPayload(const std::vector<uint8_t>& payload,
                                        ManagementOtaUpdateResultPayload& out) {
  out = ManagementOtaUpdateResultPayload{};
  if (payload.size() < 8U) return false;
  if (!readU32Le(payload.data() + 0U, payload.size(), out.req_id)) return false;
  out.phase = payload[4U];
  if (!readU16Le(payload.data() + 5U, payload.size() - 5U, out.ota_status_code)) return false;
  const uint8_t n = payload[7U];
  if (payload.size() != static_cast<size_t>(8U + n)) return false;
  if (n > 0U) {
    out.reason.assign(reinterpret_cast<const char*>(payload.data() + 8U), n);
  }
  return true;
}

/** @brief Parse `LiveMonitorStatusGet` response payload. */
inline bool parseLiveMonitorStatusPayload(const std::vector<uint8_t>& payload,
                                          ManagementLiveMonitorStatusPayload& out) {
  out = ManagementLiveMonitorStatusPayload{};
  if (payload.size() != 15U) return false;
  out.enabled = (payload[0U] != 0U);
  out.ignore_active = (payload[1U] != 0U);
  if (!readU16Le(payload.data() + 2U, payload.size() - 2U, out.ignore_reason_mask)) return false;
  out.tracked_paired_count = payload[4U];
  out.online_count = payload[5U];
  out.offline_count = payload[6U];
  if (!readU32Le(payload.data() + 7U, payload.size() - 7U, out.next_probe_due_ms)) return false;
  if (!readU32Le(payload.data() + 11U, payload.size() - 11U, out.last_transition_ms)) return false;
  return true;
}

/** @brief Parse `PeerLivenessTransition` event payload. */
inline bool parsePeerLivenessTransitionPayload(const std::vector<uint8_t>& payload,
                                               ManagementPeerLivenessTransitionPayload& out) {
  out = ManagementPeerLivenessTransitionPayload{};
  if (payload.size() != 13U) return false;
  out.schema_version = payload[0U];
  std::memcpy(out.peer.data(), payload.data() + 1U, 6U);
  out.state = payload[7U];
  out.reason = payload[8U];
  if (!readU32Le(payload.data() + 9U, payload.size() - 9U, out.timestamp_ms)) return false;
  return true;
}

/** @brief Parse `TopologyTrigger*` management event payload. */
inline bool parseTopologyTriggerEventPayload(const std::vector<uint8_t>& payload,
                                             ManagementTopologyTriggerEventPayload& out) {
  out = ManagementTopologyTriggerEventPayload{};
  if (payload.size() != 23U) return false;
  out.schema_version = payload[0U];
  std::memcpy(out.peer.data(), payload.data() + 1U, 6U);
  if (!readU16Le(payload.data() + 7U, payload.size() - 7U, out.seq)) return false;
  out.state = payload[9U];
  out.reason = payload[10U];
  out.result = payload[11U];
  out.src_role = payload[12U];
  out.src_vid = payload[13U];
  out.dst_role = payload[14U];
  out.dst_vid = payload[15U];
  out.direction = payload[16U];
  if (!readU16Le(payload.data() + 17U, payload.size() - 17U, out.delay_ms)) return false;
  if (!readU16Le(payload.data() + 19U, payload.size() - 19U, out.hold_ms)) return false;
  if (!readU16Le(payload.data() + 21U, payload.size() - 21U, out.ack_seq)) return false;
  return true;
}

/** @brief Parse topology deploy summary payload from `CmdDone/CmdFail` for `TopologyCommit`. */
inline bool parseTopologyDeploySummaryPayload(const std::vector<uint8_t>& payload,
                                              ManagementTopologyDeploySummaryPayload& out) {
  out = ManagementTopologyDeploySummaryPayload{};
  if (payload.size() != 8U) {
    return false;
  }
  if (!readU32Le(payload.data(), payload.size(), out.queued_peers)) {
    return false;
  }
  if (!readU32Le(payload.data() + 4U, payload.size() - 4U, out.failed_peers)) {
    return false;
  }
  return true;
}

/** @brief Parse payload from `OtaTransferStatus` management event. */
inline bool parseOtaTransferStatusEventPayload(const std::vector<uint8_t>& payload,
                                               ManagementOtaTransferStatusPayload& out) {
  out = ManagementOtaTransferStatusPayload{};
  if (payload.size() != 13U) return false;
  std::memcpy(out.peer.data(), payload.data(), 6U);
  const uint8_t kind = payload[6U];
  out.kind = static_cast<ManagementOtaTransferStatusKind>(kind);
  if (!readU32Le(payload.data() + 7U, payload.size() - 7U, out.offset)) return false;
  if (!readU16Le(payload.data() + 11U, payload.size() - 11U, out.status_code)) return false;
  return true;
}

/** @brief Parse payload from `OtaTransferStatus` management event. */
inline bool parseOtaTransferStatusEventPayload(const std::vector<uint8_t>& payload,
                                               MacAddress& out_peer,
                                               uint8_t& out_kind,
                                               uint32_t& out_offset,
                                               uint16_t& out_status_code) {
  ManagementOtaTransferStatusPayload decoded{};
  if (!parseOtaTransferStatusEventPayload(payload, decoded)) return false;
  out_peer = decoded.peer;
  out_kind = static_cast<uint8_t>(decoded.kind);
  out_offset = decoded.offset;
  out_status_code = decoded.status_code;
  return true;
}

/** @brief Convert management status enum to readable token. */
inline const char* managementStatusToString(ManagementStatus status) {
  switch (status) {
    case ManagementStatus::Ok:
      return "ok";
    case ManagementStatus::OkDeferred:
      return "ok_deferred";
    case ManagementStatus::UnsupportedCommand:
      return "unsupported";
    case ManagementStatus::BadPayload:
      return "bad_payload";
    case ManagementStatus::NotPaired:
      return "not_paired";
    case ManagementStatus::QueueFull:
      return "queue_full";
    case ManagementStatus::BusyPairing:
      return "busy_pairing";
    case ManagementStatus::UnpairInProgress:
      return "unpair_in_progress";
    case ManagementStatus::SourceNotActiveMaster:
      return "source_not_active_master";
    case ManagementStatus::DeniedByPolicy:
      return "denied_by_policy";
    case ManagementStatus::Timeout:
      return "timeout";
    case ManagementStatus::DeniedByRole:
      return "denied_by_role";
    case ManagementStatus::CapacityLimitReached:
      return "capacity_limit_reached";
    case ManagementStatus::TopologyNotStaged:
      return "topology_not_staged";
    case ManagementStatus::TopologyVersionStale:
      return "topology_version_stale";
    case ManagementStatus::TopologyApplyFailed:
      return "topology_apply_failed";
    case ManagementStatus::BusyRadioTransition:
      return "busy_radio_transition";
    case ManagementStatus::InternalError:
      return "internal_error";
    default:
      return "unknown";
  }
}

/** @brief Convert management event enum to readable token. */
inline const char* managementEventToString(ManagementEventId event_id) {
  switch (event_id) {
    case ManagementEventId::CmdRx:
      return "cmd_rx";
    case ManagementEventId::CmdDone:
      return "cmd_done";
    case ManagementEventId::CmdFail:
      return "cmd_fail";
    case ManagementEventId::Timeout:
      return "timeout";
    case ManagementEventId::QueueFull:
      return "queue_full";
    case ManagementEventId::StateChange:
      return "state_change";
    case ManagementEventId::DiscoveryStarted:
      return "discovery_started";
    case ManagementEventId::DiscoveryUpdate:
      return "discovery_update";
    case ManagementEventId::DiscoveryStopped:
      return "discovery_stopped";
    case ManagementEventId::DiscoveryFinished:
      return "discovery_finished";
    case ManagementEventId::DiscoverySnapshot:
      return "discovery_snapshot";
    case ManagementEventId::PairProgress:
      return "pair_progress";
    case ManagementEventId::PairResult:
      return "pair_result";
    case ManagementEventId::UnpairResult:
      return "unpair_result";
    case ManagementEventId::PeerRemoved:
      return "peer_removed";
    case ManagementEventId::OtaTransferReady:
      return "ota_transfer_ready";
    case ManagementEventId::OtaBootComplete:
      return "ota_boot_complete";
    case ManagementEventId::OtaTransferStatus:
      return "ota_transfer_status";
    case ManagementEventId::PeerLivenessTransition:
      return "peer_liveness_transition";
    case ManagementEventId::TopologyStaged:
      return "topology_staged";
    case ManagementEventId::TopologyCommitted:
      return "topology_committed";
    case ManagementEventId::TopologyCommitFailed:
      return "topology_commit_failed";
    case ManagementEventId::TopologyTriggerReceived:
      return "topology_trigger_received";
    case ManagementEventId::TopologyTriggerRejected:
      return "topology_trigger_rejected";
    case ManagementEventId::TopologyTriggerAck:
      return "topology_trigger_ack";
    default:
      return "event_unknown";
  }
}

/** @brief Parse telemetry push mode token (`periodic`, `change`, `hybrid`). */
inline TelemetryPushMode parseTelemetryPushMode(const std::string& token) {
  const std::string t = lowerAscii(token);
  if (t == "periodic") return TelemetryPushMode::Periodic;
  if (t == "change" || t == "on_change") return TelemetryPushMode::OnChange;
  return TelemetryPushMode::Hybrid;
}

}  // namespace management_utils
}  // namespace espnow_link
