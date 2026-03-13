#include "espnow_link/manager.hpp"
#include "espnow_link/descriptor.hpp"
#include "espnow_link/control_codec.hpp"
#include "espnow_link/ota_types.hpp"
#include "espnow_link/security.hpp"
#include "espnow_link/telemetry_push.hpp"
#include "manager_helpers.hpp"
#include "telemetry_alignment.hpp"
#include "../descriptor/descriptor_reply.hpp"

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace {

constexpr uint16_t kLogSourceManager = 0x0101;
constexpr uint16_t kLogEvtBeginOk = 0x0001;
constexpr uint16_t kLogEvtBeginFail = 0x0002;
constexpr uint16_t kLogEvtRestoreOk = 0x0003;
constexpr uint16_t kLogEvtRestoreFail = 0x0004;
constexpr uint16_t kLogEvtRxDecodeFail = 0x0005;
constexpr uint16_t kLogEvtRxVersionDrop = 0x0006;
constexpr uint16_t kLogEvtTxEncodeFail = 0x0007;
constexpr uint16_t kLogEvtTxSendFail = 0x0008;
constexpr uint16_t kLogEvtPairReq = 0x0009;
constexpr uint16_t kLogEvtUnpairReq = 0x000A;
constexpr uint16_t kLogEvtRestoreTrimDrop = 0x000B;
constexpr uint16_t kLogEvtRestoreTrimSummary = 0x000C;
constexpr uint16_t kOtaBootCompleteEventId = 0x7F10;
constexpr uint16_t kOtaTransferReadyEventId = 0x7F11;
constexpr uint8_t kFirmwareStatusKindChunkAck = 0x01;
constexpr uint8_t kFirmwareStatusKindChunkNack = 0x02;
constexpr uint8_t kFirmwareStatusKindFinalizeOk = 0x03;
constexpr uint8_t kFirmwareStatusKindFinalizeFail = 0x04;
constexpr uint8_t kFirmwareStatusKindFinalizeAck = 0x05;
constexpr uint32_t kFinalizeStatusRetryIntervalMs = 400U;
constexpr uint32_t kFinalizeStatusRetryWindowMs = 12000U;
constexpr uint8_t kLoggerMetaSlot = 2;
constexpr uint8_t kLoggerMetaVersion = 1;
constexpr size_t kLoggerMetaSize = 3;
constexpr uint8_t kPeerRoleMetaSlot = 3;
constexpr uint8_t kPeerRoleMetaVersion = 1;
constexpr size_t kPeerRoleMetaSize = 2;
constexpr uint8_t kFirmwareMetaMagic = 0xA5;
constexpr uint8_t kFirmwareMetaVersionV1 = 1;
constexpr uint8_t kFirmwareMetaVersionV2 = 2;
constexpr size_t kFirmwareMetaHeaderSizeV1 = 4;
constexpr size_t kFirmwareMetaHeaderSizeV2 = 5;
constexpr size_t kRestorePairLimit = 14;
constexpr uint8_t kRestoreTrimSchemaVersion = 1;
constexpr uint8_t kRestoreTrimActionDrop = 1;
constexpr uint8_t kRestoreTrimActionSummary = 2;
constexpr char kRestoreTrimReason[] = "restore_over_capacity";
constexpr uint8_t kTopologyMetaSlotStaged = 0x32;
constexpr uint8_t kTopologyMetaSlotCommitted = 0x33;
constexpr uint8_t kTopologyMetaSlotLmk = 0x34;
constexpr uint8_t kTopologyBlobVersion = 1;
constexpr size_t kTopologySeedSize = 32U;
constexpr size_t kTopologySlotBlobSize = 17U;
constexpr size_t kTopologyGroupBlobSize = 34U;
constexpr size_t kTopologyHeaderBlobSize = 16U;
constexpr size_t kTopologyBlobSize =
    kTopologyHeaderBlobSize +
    (espnow_link::EspNowManager::kTopologyMaxSlots * kTopologySlotBlobSize) +
    (espnow_link::EspNowManager::kTopologyMaxGroups * kTopologyGroupBlobSize);
constexpr uint8_t kTopologyLmkBlobVersion = 1U;
constexpr size_t kTopologyLmkBlobEntrySize = 23U;  // mac(6)+lmk(16)+group(1)
constexpr uint32_t kTopologyTriggerDedupWindowMs = 15000U;
constexpr size_t kTopologyTriggerDedupMaxEntries = 32U;
constexpr uint8_t kTopologyTriggerResultAccepted = 0U;
constexpr uint8_t kTopologyTriggerResultRejected = 1U;
constexpr uint8_t kTopologyTriggerReasonOk = 0x00;
constexpr uint8_t kTopologyTriggerReasonUnauthorizedPeer = 0x01;
constexpr uint8_t kTopologyTriggerReasonStaleTopology = 0x02;
constexpr uint8_t kTopologyTriggerReasonBadIndex = 0x03;
constexpr uint8_t kTopologyTriggerReasonRangeRejected = 0x04;
constexpr uint8_t kTopologyTriggerReasonBusy = 0x05;
constexpr size_t kPullRequestDispatchBudgetPerTick = 1U;
constexpr size_t kPullResponseDispatchBudgetPerTick = 4U;
constexpr size_t kFirmwareRxDispatchBudgetPerTick = 4U;
constexpr size_t kControlRxDispatchBudgetPerTick = 4U;

int compareMac(const espnow_link::MacAddress& lhs, const espnow_link::MacAddress& rhs) {
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] < rhs[i]) {
      return -1;
    }
    if (lhs[i] > rhs[i]) {
      return 1;
    }
  }
  return 0;
}

bool isOtaWireMessage(espnow_link::MessageType type) {
  return type == espnow_link::MessageType::FirmwareBegin ||
         type == espnow_link::MessageType::FirmwareChunk ||
         type == espnow_link::MessageType::FirmwareEnd ||
         type == espnow_link::MessageType::FirmwareStatus;
}

void appendU16Le(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
}

void appendU32Le(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
}

bool readU16Le(const uint8_t* in, size_t len, uint16_t& out) {
  if (in == nullptr || len < 2U) {
    return false;
  }
  out = static_cast<uint16_t>(in[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8);
  return true;
}

bool readU32Le(const uint8_t* in, size_t len, uint32_t& out) {
  if (in == nullptr || len < 4U) {
    return false;
  }
  out = static_cast<uint32_t>(in[0]) |
        (static_cast<uint32_t>(in[1]) << 8) |
        (static_cast<uint32_t>(in[2]) << 16) |
        (static_cast<uint32_t>(in[3]) << 24);
  return true;
}

bool isAllZeroMac(const espnow_link::MacAddress& mac) {
  for (uint8_t b : mac) {
    if (b != 0U) {
      return false;
    }
  }
  return true;
}

bool isVirtualIndexAllowedForRole(uint8_t role_code, uint8_t vi) {
  if (vi == 0xFFU) {
    return true;
  }
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileSemu & 0xFFU)) {
    return vi <= 7U;
  }
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileRemu & 0xFFU)) {
    return vi <= 15U;
  }
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileSens & 0xFFU) ||
      role_code == static_cast<uint8_t>(espnow_link::kProfileRelay & 0xFFU) ||
      role_code == static_cast<uint8_t>(espnow_link::kProfilePms & 0xFFU)) {
    return false;
  }
  return vi <= 15U;
}

bool isBroadcastMac(const espnow_link::MacAddress& mac) {
  for (uint8_t b : mac) {
    if (b != 0xFFU) {
      return false;
    }
  }
  return true;
}

bool isAllZeroSeed(const std::array<uint8_t, 32>& seed) {
  for (uint8_t b : seed) {
    if (b != 0U) {
      return false;
    }
  }
  return true;
}

bool isSensorProfileCode(uint8_t code) {
  return code == static_cast<uint8_t>(espnow_link::kProfileSens & 0xFFU) ||
         code == static_cast<uint8_t>(espnow_link::kProfileSemu & 0xFFU);
}

bool isRelayProfileCode(uint8_t code) {
  return code == static_cast<uint8_t>(espnow_link::kProfileRelay & 0xFFU) ||
         code == static_cast<uint8_t>(espnow_link::kProfileRemu & 0xFFU);
}

uint8_t topologySlotCapForLocalRole(uint8_t /*local_code*/) {
  return static_cast<uint8_t>(espnow_link::EspNowManager::kTopologyMaxSlots);
}

bool isTopologyRolePairAllowed(uint8_t local_code, uint8_t peer_code) {
  if (isSensorProfileCode(local_code)) {
    return isRelayProfileCode(peer_code);
  }
  if (isRelayProfileCode(local_code)) {
    return isSensorProfileCode(peer_code);
  }
  return true;
}

void appendTopologySlotBlob(std::vector<uint8_t>& out,
                            const espnow_link::EspNowManager::TopologySlot& slot) {
  out.push_back(static_cast<uint8_t>(slot.enabled ? 1U : 0U));
  out.insert(out.end(), slot.peer.begin(), slot.peer.end());
  out.push_back(slot.peer_role);
  out.push_back(slot.group_id);
  out.push_back(static_cast<uint8_t>(slot.relative_index));
  out.push_back(slot.local_virtual_index);
  out.push_back(slot.peer_virtual_index);
  out.push_back(static_cast<uint8_t>(slot.axis_order));
  appendU16Le(out, slot.delay_ms);
  appendU16Le(out, slot.hold_ms);
}

bool readTopologySlotBlob(const uint8_t* in,
                          size_t len,
                          espnow_link::EspNowManager::TopologySlot& out_slot) {
  if (in == nullptr || len < kTopologySlotBlobSize) {
    return false;
  }
  out_slot = espnow_link::EspNowManager::TopologySlot{};
  out_slot.enabled = (in[0] != 0U);
  std::memcpy(out_slot.peer.data(), in + 1U, out_slot.peer.size());
  out_slot.peer_role = in[7U];
  out_slot.group_id = in[8U];
  out_slot.relative_index = static_cast<int8_t>(in[9U]);
  out_slot.local_virtual_index = in[10U];
  out_slot.peer_virtual_index = in[11U];
  out_slot.axis_order = static_cast<int8_t>(in[12U]);
  if (!readU16Le(in + 13U, len - 13U, out_slot.delay_ms)) {
    return false;
  }
  if (!readU16Le(in + 15U, len - 15U, out_slot.hold_ms)) {
    return false;
  }
  return true;
}

void appendTopologyGroupBlob(std::vector<uint8_t>& out,
                             const espnow_link::EspNowManager::TopologyGroupSeed& group) {
  out.push_back(static_cast<uint8_t>(group.enabled ? 1U : 0U));
  out.push_back(group.group_id);
  out.insert(out.end(), group.seed.begin(), group.seed.end());
}

bool readTopologyGroupBlob(const uint8_t* in,
                           size_t len,
                           espnow_link::EspNowManager::TopologyGroupSeed& out_group) {
  if (in == nullptr || len < kTopologyGroupBlobSize) {
    return false;
  }
  out_group = espnow_link::EspNowManager::TopologyGroupSeed{};
  out_group.enabled = (in[0] != 0U);
  out_group.group_id = in[1U];
  std::memcpy(out_group.seed.data(), in + 2U, out_group.seed.size());
  return true;
}

bool serializeTopologySnapshotBlob(const espnow_link::EspNowManager::TopologySnapshot& snapshot,
                                   std::vector<uint8_t>& out_blob) {
  out_blob.clear();
  out_blob.reserve(kTopologyBlobSize);
  out_blob.push_back(kTopologyBlobVersion);
  out_blob.push_back(snapshot.schema_version);
  out_blob.push_back(static_cast<uint8_t>(snapshot.state));
  out_blob.push_back(0U);  // reserved
  appendU32Le(out_blob, snapshot.topology_version);
  out_blob.push_back(snapshot.index_neg);
  out_blob.push_back(snapshot.index_pos);
  out_blob.push_back(snapshot.enabled_slot_count);
  out_blob.push_back(snapshot.enabled_group_count);
  appendU32Le(out_blob, snapshot.checksum);

  for (const auto& slot : snapshot.slots) {
    appendTopologySlotBlob(out_blob, slot);
  }
  for (const auto& group : snapshot.groups) {
    appendTopologyGroupBlob(out_blob, group);
  }
  return out_blob.size() == kTopologyBlobSize;
}

bool deserializeTopologySnapshotBlob(const std::vector<uint8_t>& blob,
                                     espnow_link::EspNowManager::TopologySnapshot& out_snapshot) {
  out_snapshot = espnow_link::EspNowManager::TopologySnapshot{};
  if (blob.size() != kTopologyBlobSize) {
    return false;
  }
  if (blob[0U] != kTopologyBlobVersion) {
    return false;
  }
  out_snapshot.schema_version = blob[1U];
  out_snapshot.state = static_cast<espnow_link::EspNowManager::TopologyState>(blob[2U]);
  if (!readU32Le(blob.data() + 4U, blob.size() - 4U, out_snapshot.topology_version)) {
    return false;
  }
  out_snapshot.index_neg = blob[8U];
  out_snapshot.index_pos = blob[9U];
  out_snapshot.enabled_slot_count = blob[10U];
  out_snapshot.enabled_group_count = blob[11U];
  if (!readU32Le(blob.data() + 12U, blob.size() - 12U, out_snapshot.checksum)) {
    return false;
  }

  size_t off = kTopologyHeaderBlobSize;
  for (auto& slot : out_snapshot.slots) {
    if (!readTopologySlotBlob(blob.data() + off, blob.size() - off, slot)) {
      return false;
    }
    off += kTopologySlotBlobSize;
  }
  for (auto& group : out_snapshot.groups) {
    if (!readTopologyGroupBlob(blob.data() + off, blob.size() - off, group)) {
      return false;
    }
    off += kTopologyGroupBlobSize;
  }
  return off == blob.size();
}

std::vector<uint8_t> makeRestoreTrimDropExt(const espnow_link::MacAddress& peer,
                                            uint16_t kept_count,
                                            uint16_t drop_index) {
  std::vector<uint8_t> ext;
  const size_t reason_len = std::strlen(kRestoreTrimReason);
  ext.reserve(2 + peer.size() + 2 + 2 + reason_len);
  ext.push_back(kRestoreTrimSchemaVersion);
  ext.push_back(kRestoreTrimActionDrop);
  ext.insert(ext.end(), peer.begin(), peer.end());
  appendU16Le(ext, kept_count);
  appendU16Le(ext, drop_index);
  ext.insert(ext.end(), kRestoreTrimReason, kRestoreTrimReason + reason_len);
  return ext;
}

std::vector<uint8_t> makeRestoreTrimSummaryExt(uint16_t kept_count, uint16_t dropped_count) {
  std::vector<uint8_t> ext;
  const size_t reason_len = std::strlen(kRestoreTrimReason);
  ext.reserve(2 + 2 + 2 + reason_len);
  ext.push_back(kRestoreTrimSchemaVersion);
  ext.push_back(kRestoreTrimActionSummary);
  appendU16Le(ext, kept_count);
  appendU16Le(ext, dropped_count);
  ext.insert(ext.end(), kRestoreTrimReason, kRestoreTrimReason + reason_len);
  return ext;
}

}  // namespace

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;

EspNowManager::EspNowManager(const ManagerConfig& config,
                             ITransport& transport,
                             PairingStore* store,
                             IEventSink* events,
                             IPlatformHooks* hooks,
                             IFirmwareStreamSink* firmware_sink,
                             IControlPlane* control_plane,
                             ITimeSource* time_source,
                             ITimeSink* time_sink,
                             ITelemetryPushProvider* telemetry_push_provider,
                             LibraryLogger* logger)
    : config_(config),
      transport_(transport),
      store_(store),
      events_(events),
      hooks_(hooks),
      firmware_sink_(firmware_sink),
      control_plane_(control_plane),
      telemetry_push_provider_(telemetry_push_provider),
      time_source_(time_source != nullptr ? time_source : &default_time_source_),
      time_sink_(time_sink != nullptr ? time_sink : &default_time_sink_),
      default_peer_window_(transport),
      peer_window_(&default_peer_window_),
      pairing_(config, transport, store, events, hooks, peer_window_, time_source_, logger),
      logger_(logger) {
  discovery_rx_enabled_ = (config_.local_role != Role::Master);
  pairing_.setLocalProfileId(static_cast<uint8_t>(kProfilePms));
  registerBuiltInCodecs(CodecRegistry::instance());
  codec_ = &default_codec_;
  local_profile_ = nullptr;
  local_profile_id_ = kProfileUnknown;
}

void EspNowManager::setCodec(IProfileCodec* codec) {
  if (codec == nullptr) {
    codec_ = &default_codec_;
    return;
  }
  if (local_profile_ != nullptr && !local_profile_->supportsCodec(codec->codecId())) {
    return;
  }
  codec_ = codec;
}

bool EspNowManager::setCodecById(CodecId codec_id) {
  IProfileCodec* candidate = CodecRegistry::instance().find(codec_id);
  if (candidate == nullptr) {
    return false;
  }
  if (local_profile_ != nullptr && !local_profile_->supportsCodec(codec_id)) {
    return false;
  }
  codec_ = candidate;
  return true;
}

const IProfileCodec& EspNowManager::codec() const {
  if (codec_ != nullptr) {
    return *codec_;
  }
  return default_codec_;
}

bool EspNowManager::encodeDescriptorQueryPayload(const DescriptorQuery& query,
                                                 std::vector<uint8_t>& out_payload) const {
  return codec().encodeDescriptorQuery(query, out_payload);
}

bool EspNowManager::decodeDescriptorQueryPayload(const uint8_t* payload,
                                                 size_t len,
                                                 DescriptorQuery& out_query) const {
  return codec().decodeDescriptorQuery(payload, len, out_query);
}

bool EspNowManager::encodeDescriptorResponsePayload(const DescriptorResponse& response,
                                                    std::vector<uint8_t>& out_payload) const {
  return codec().encodeDescriptorResponse(response, out_payload);
}

bool EspNowManager::decodeDescriptorResponsePayload(const uint8_t* payload,
                                                    size_t len,
                                                    DescriptorResponse& out_response) const {
  return codec().decodeDescriptorResponse(payload, len, out_response);
}

bool EspNowManager::encodeControlCommandPayload(uint16_t cmd_id,
                                                std::vector<uint8_t>& out_payload) const {
  return codec().encodeControlCommand(cmd_id, out_payload);
}

bool EspNowManager::decodeControlCommandPayload(const uint8_t* payload,
                                                size_t len,
                                                uint16_t& out_cmd_id) const {
  return codec().decodeControlCommand(payload, len, out_cmd_id);
}

bool EspNowManager::encodeControlResultPayload(uint16_t cmd_id,
                                               uint16_t result_code,
                                               std::vector<uint8_t>& out_payload) const {
  return codec().encodeControlResult(cmd_id, result_code, out_payload);
}

bool EspNowManager::decodeControlResultPayload(const uint8_t* payload,
                                               size_t len,
                                               uint16_t& out_cmd_id,
                                               uint16_t& out_result_code) const {
  return codec().decodeControlResult(payload, len, out_cmd_id, out_result_code);
}

bool EspNowManager::executeDescriptorQuery(IDescriptorProvider& provider,
                                           const DescriptorQuery& query,
                                           DescriptorResponse& out_response) {
  if (!handleDescriptorQuery(provider, query, out_response, local_profile_, logger_)) {
    return false;
  }

  if (query.type == DescriptorQueryType::SetLogControl &&
      out_response.type == DescriptorResponseType::Ack) {
    if (!persistLoggerConfig()) {
      out_response.type = DescriptorResponseType::Error;
      out_response.message = "logger config persist failed";
    }
  }

  return true;
}

bool EspNowManager::setLocalProfile(ProfileId profile_id) {
  if (profile_id == kProfileUnknown) {
    local_profile_id_ = kProfileUnknown;
    local_profile_ = nullptr;
    pairing_.setLocalProfileId(static_cast<uint8_t>(kProfilePms));
    setCodec(&default_codec_);
    return true;
  }

  const IProfileDefinition* p = ProfileRegistry::instance().find(profile_id);
  if (p == nullptr) {
    return false;
  }

  local_profile_id_ = profile_id;
  local_profile_ = p;
  pairing_.setLocalProfileId(static_cast<uint8_t>(profile_id & 0xFF));

  const CodecId profile_codec = p->defaultCodecId();
  if (!setCodecById(profile_codec)) {
    if (p->supportsCodec(kCodecIdDefault)) {
      setCodec(&default_codec_);
    } else {
      return false;
    }
  }

  return true;
}

ProfileId EspNowManager::localProfileId() const {
  return local_profile_id_;
}

const char* EspNowManager::localProfileName() const {
  return (local_profile_ != nullptr && local_profile_->profileName() != nullptr)
             ? local_profile_->profileName()
             : "UNKNOWN";
}

const IProfileDefinition* EspNowManager::localProfile() const {
  return local_profile_;
}

const ManagerRuntimeMetrics& EspNowManager::runtimeMetrics() const {
  return metrics_;
}

void EspNowManager::resetRuntimeMetrics() {
  metrics_ = ManagerRuntimeMetrics{};
}

void EspNowManager::setLogger(LibraryLogger* logger) {
  logger_ = logger;
  pairing_.setLogger(logger);
}

void EspNowManager::setHardwarePeerWindow(IHardwarePeerWindow* peer_window) {
  peer_window_ = (peer_window != nullptr) ? peer_window : &default_peer_window_;
  pairing_.setPeerWindow(peer_window_);
}

LibraryLogger* EspNowManager::logger() const {
  return logger_;
}

void EspNowManager::setDiscoveryRxEnabled(bool enabled) {
  // This gate is master-specific. Slave discovery broadcast behavior is unchanged.
  if (config_.local_role != Role::Master) {
    discovery_rx_enabled_ = true;
    return;
  }
  discovery_rx_enabled_ = enabled;
}

bool EspNowManager::discoveryRxEnabled() const {
  return (config_.local_role != Role::Master) ? true : discovery_rx_enabled_;
}

bool EspNowManager::logExternalRecord(const LibraryLogRecord& record) {
  if (logger_ == nullptr) {
    return false;
  }
  return logger_->log(record);
}

bool EspNowManager::logExternalEvent(LibraryLogLevel level,
                                     uint16_t source_id,
                                     uint16_t event_id,
                                     int32_t p0,
                                     int32_t p1,
                                     int32_t p2,
                                     const uint8_t* ext,
                                     size_t ext_len) {
  if (logger_ == nullptr) {
    return false;
  }

  LibraryLogRecord rec{};
  rec.level = level;
  rec.source_id = source_id;
  rec.event_id = event_id;
  rec.epoch_s = 0;
  if (time_source_ != nullptr) {
    uint64_t epoch_s = 0;
    if (time_source_->nowEpochSec(epoch_s)) {
      rec.epoch_s = static_cast<uint32_t>(epoch_s & 0xFFFFFFFFULL);
    }
  }
  rec.uptime_ms = current_now_ms_;
  rec.p0 = p0;
  rec.p1 = p1;
  rec.p2 = p2;
  if (ext != nullptr && ext_len > 0U) {
    rec.ext.assign(ext, ext + static_cast<std::ptrdiff_t>(ext_len));
  }
  return logger_->log(rec);
}

bool EspNowManager::persistLoggerConfig() {
  if (logger_ == nullptr) {
    return false;
  }
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }

  const MacAddress scoped_peer{};
  uint8_t blob[kLoggerMetaSize] = {0};
  blob[0] = kLoggerMetaVersion;
  blob[1] = logger_->enabled() ? 1U : 0U;
  blob[2] = static_cast<uint8_t>(logger_->minLevel());
  return store_->saveMetaBlob(local_mac_, scoped_peer, kLoggerMetaSlot, blob, sizeof(blob));
}

bool EspNowManager::saveLocalMetaBlob(uint8_t slot, const uint8_t* data, size_t len) {
  if (store_ == nullptr || !store_->enabled() || data == nullptr || len == 0U) {
    return false;
  }
  const MacAddress scoped_peer{};
  return store_->saveMetaBlob(local_mac_, scoped_peer, slot, data, len);
}

bool EspNowManager::loadLocalMetaBlob(uint8_t slot, std::vector<uint8_t>& out) const {
  out.clear();
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }
  const MacAddress scoped_peer{};
  return store_->loadMetaBlob(local_mac_, scoped_peer, slot, out);
}

bool EspNowManager::eraseLocalMetaBlob(uint8_t slot) {
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }
  const MacAddress scoped_peer{};
  return store_->eraseMetaBlob(local_mac_, scoped_peer, slot);
}

bool EspNowManager::savePeerRoleHint(const MacAddress& peer, uint8_t role_code) {
  if (role_code == 0U) {
    return false;
  }
  upsertPeerRoleHintCache_(peer, role_code);
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }
  uint8_t blob[kPeerRoleMetaSize] = {0U};
  blob[0] = kPeerRoleMetaVersion;
  blob[1] = role_code;
  return store_->saveMetaBlob(local_mac_, peer, kPeerRoleMetaSlot, blob, sizeof(blob));
}

bool EspNowManager::loadPeerRoleHint(const MacAddress& peer, uint8_t& out_role_code) {
  out_role_code = 0U;
  if (findPeerRoleHintCache_(peer, out_role_code) && out_role_code != 0U) {
    return true;
  }
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }
  std::vector<uint8_t> blob{};
  if (!store_->loadMetaBlob(local_mac_, peer, kPeerRoleMetaSlot, blob)) {
    return false;
  }
  if (blob.empty()) {
    return false;
  }

  uint8_t parsed_role = 0U;
  if (blob.size() >= kPeerRoleMetaSize && blob[0] == kPeerRoleMetaVersion) {
    parsed_role = blob[1];
  }
  if (parsed_role == 0U) {
    return false;
  }
  out_role_code = parsed_role;
  upsertPeerRoleHintCache_(peer, parsed_role);
  return true;
}

bool EspNowManager::restoreLoggerConfig() {
  if (logger_ == nullptr || store_ == nullptr || !store_->enabled()) {
    return false;
  }

  const MacAddress scoped_peer{};
  std::vector<uint8_t> blob;
  if (!store_->loadMetaBlob(local_mac_, scoped_peer, kLoggerMetaSlot, blob)) {
    return false;
  }
  if (blob.size() < kLoggerMetaSize || blob[0] != kLoggerMetaVersion) {
    (void)store_->eraseMetaBlob(local_mac_, scoped_peer, kLoggerMetaSlot);
    return false;
  }

  logger_->setEnabled(blob[1] != 0U);
  uint8_t raw_level = blob[2];
  if (raw_level > static_cast<uint8_t>(LibraryLogLevel::Debug)) {
    raw_level = static_cast<uint8_t>(LibraryLogLevel::Info);
  }
  logger_->setMinLevel(static_cast<LibraryLogLevel>(raw_level));
  return true;
}

void EspNowManager::emitRuntimeLog(LibraryLogLevel level,
                                   uint16_t event_id,
                                   uint32_t corr_id,
                                   int32_t p1,
                                   int32_t p2,
                                   const uint8_t* ext,
                                   size_t ext_len) {
  const size_t clipped_ext_len = std::min<size_t>(ext_len, 64U);
  (void)logExternalEvent(level,
                         kLogSourceManager,
                         event_id,
                         static_cast<int32_t>(corr_id),
                         p1,
                         p2,
                         ext,
                         clipped_ext_len);
}

bool EspNowManager::begin(const MacAddress& local_mac) {
  if (local_profile_ == nullptr || local_profile_id_ == kProfileUnknown) {
    return false;
  }

  uint8_t start_channel = config_.channel;
  if (hooks_ != nullptr) {
    uint8_t hook_channel = 0;
    if (hooks_->getBootChannel(hook_channel) && hook_channel >= 1 && hook_channel <= 14) {
      start_channel = hook_channel;
    }
  }

  if (!transport_.begin(start_channel, config_.enable_encryption)) {
    emitRuntimeLog(LibraryLogLevel::Error, kLogEvtBeginFail, 0, static_cast<int32_t>(start_channel), 0);
    return false;
  }

  if (hooks_ != nullptr) {
    uint8_t committed_channel = transport_.getChannel();
    if (committed_channel == 0) {
      committed_channel = start_channel;
    }
    hooks_->onChannelCommitted(committed_channel);
  }

  local_mac_ = local_mac;
  pairing_.setLocalMac(local_mac);
  (void)restoreLoggerConfig();
  restoreTopologySnapshots_();
  peer_registry_.clear();
  paired_cache_valid_ = false;
  paired_cache_state_ = false;
  paired_cache_peer_ = MacAddress{};
  emitRuntimeLog(LibraryLogLevel::Info, kLogEvtBeginOk, 0, static_cast<int32_t>(start_channel), 0);
  return true;
}

bool EspNowManager::restore() {
  if (store_ == nullptr || !store_->enabled()) {
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 1, 0);
    return false;
  }

  struct RestoreCandidate {
    PairRecord record{};
    uint32_t pair_seq = 0;
  };

  std::vector<PairIndexEntry> index_entries;
  if (!store_->loadPairIndex(local_mac_, index_entries) || index_entries.empty()) {
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 2, 0);
    return false;
  }

  bool index_mutated = false;
  std::vector<RestoreCandidate> candidates;
  candidates.reserve(index_entries.size());
  for (const auto& entry : index_entries) {
    if (!entry.valid || entry.pair_seq == 0U) {
      index_mutated = true;
      continue;
    }

    PairRecord record{};
    if (!store_->loadPair(local_mac_, entry.peer_mac, record) || !record.valid) {
      (void)store_->erasePair(local_mac_, entry.peer_mac);
      (void)store_->eraseChannel(local_mac_, entry.peer_mac);
      index_mutated = true;
      continue;
    }

    RestoreCandidate candidate{};
    candidate.record = record;
    candidate.pair_seq = entry.pair_seq;
    candidates.push_back(candidate);
  }

  std::sort(candidates.begin(), candidates.end(), [](const RestoreCandidate& lhs, const RestoreCandidate& rhs) {
    if (lhs.pair_seq != rhs.pair_seq) {
      return lhs.pair_seq < rhs.pair_seq;
    }
    return compareMac(lhs.record.peer_mac, rhs.record.peer_mac) < 0;
  });

  std::vector<RestoreCandidate> ordered_unique;
  ordered_unique.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    const auto duplicate =
        std::find_if(ordered_unique.begin(),
                     ordered_unique.end(),
                     [&](const RestoreCandidate& existing) {
                       return existing.record.peer_mac == candidate.record.peer_mac;
                     });
    if (duplicate != ordered_unique.end()) {
      index_mutated = true;
      continue;
    }
    ordered_unique.push_back(candidate);
  }

  const size_t keep_count = std::min(ordered_unique.size(), kRestorePairLimit);
  const size_t dropped_count = ordered_unique.size() - keep_count;
  if (dropped_count > 0U) {
    for (size_t i = 0; i < dropped_count; ++i) {
      const auto& dropped = ordered_unique[keep_count + i];
      (void)store_->erasePair(local_mac_, dropped.record.peer_mac);
      (void)store_->eraseChannel(local_mac_, dropped.record.peer_mac);

      const std::vector<uint8_t> ext =
          makeRestoreTrimDropExt(dropped.record.peer_mac,
                                 static_cast<uint16_t>(kRestorePairLimit),
                                 static_cast<uint16_t>(i + 1U));
      emitRuntimeLog(LibraryLogLevel::Warn,
                     kLogEvtRestoreTrimDrop,
                     0,
                     static_cast<int32_t>(kRestorePairLimit),
                     static_cast<int32_t>(i + 1U),
                     ext.data(),
                     ext.size());
    }

    const std::vector<uint8_t> summary_ext =
        makeRestoreTrimSummaryExt(static_cast<uint16_t>(kRestorePairLimit),
                                  static_cast<uint16_t>(dropped_count));
    emitRuntimeLog(LibraryLogLevel::Warn,
                   kLogEvtRestoreTrimSummary,
                   0,
                   static_cast<int32_t>(kRestorePairLimit),
                   static_cast<int32_t>(dropped_count),
                   summary_ext.data(),
                   summary_ext.size());
    index_mutated = true;
  }

  if (keep_count == 0U) {
    if (index_mutated) {
      const std::vector<PairIndexEntry> empty_index{};
      (void)store_->savePairIndex(local_mac_, empty_index);
    }
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 3, 0);
    return false;
  }

  std::vector<PairIndexEntry> canonical_index;
  canonical_index.reserve(keep_count);
  for (size_t i = 0; i < keep_count; ++i) {
    PairIndexEntry normalized{};
    normalized.peer_mac = ordered_unique[i].record.peer_mac;
    normalized.pair_seq = ordered_unique[i].pair_seq;
    normalized.valid = true;
    canonical_index.push_back(normalized);
  }

  std::vector<PairIndexEntry> restored_index;
  restored_index.reserve(canonical_index.size());
  size_t restored_count = 0U;
  for (size_t i = 0; i < keep_count; ++i) {
    const auto& candidate = ordered_unique[i];
    if (restorePairedLink(candidate.record)) {
      restored_index.push_back(canonical_index[i]);
      ++restored_count;
      continue;
    }
    (void)store_->erasePair(local_mac_, candidate.record.peer_mac);
    (void)store_->eraseChannel(local_mac_, candidate.record.peer_mac);
    index_mutated = true;
  }

  if (restored_count == 0U) {
    if (index_mutated || !canonical_index.empty()) {
      const std::vector<PairIndexEntry> empty_index{};
      (void)store_->savePairIndex(local_mac_, empty_index);
    }
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 4, 0);
    return false;
  }

  if (index_mutated || restored_index.size() != canonical_index.size()) {
    (void)store_->savePairIndex(local_mac_, restored_index);
  }

  emitRuntimeLog(LibraryLogLevel::Info,
                 kLogEvtRestoreOk,
                 0,
                 static_cast<int32_t>(restored_count),
                 0);
  return true;
}

void EspNowManager::end() { transport_.end(); }
bool EspNowManager::shouldAttachTimeSync(uint32_t now_ms) const {
  if (!config_.time_sync_enabled || config_.local_role != Role::Master || time_source_ == nullptr) {
    return false;
  }

  if (!has_time_sync_tx_) {
    return true;
  }

  if (config_.time_sync_interval_ms == 0) {
    return true;
  }

  return (now_ms - last_time_sync_tx_ms_) >= config_.time_sync_interval_ms;
}

bool EspNowManager::appendTimeSyncMetadata(const uint8_t* payload,
                                           size_t len,
                                           uint8_t& inout_flags,
                                           std::vector<uint8_t>& out_payload) {
  out_payload.clear();

  if (!shouldAttachTimeSync(current_now_ms_)) {
    return true;
  }

  if (len > (ProtocolCodec::kMaxPayload - kTimeSyncMetadataSize)) {
    return true;
  }

  uint64_t epoch_s = 0;
  if (!time_source_->nowEpochSec(epoch_s)) {
    return true;
  }

  out_payload.resize(kTimeSyncMetadataSize + len);
  const uint32_t epoch_s32 = static_cast<uint32_t>(epoch_s & 0xFFFFFFFFULL);
  out_payload[0] = static_cast<uint8_t>(epoch_s32 & 0xFFU);
  out_payload[1] = static_cast<uint8_t>((epoch_s32 >> 8) & 0xFFU);
  out_payload[2] = static_cast<uint8_t>((epoch_s32 >> 16) & 0xFFU);
  out_payload[3] = static_cast<uint8_t>((epoch_s32 >> 24) & 0xFFU);
  if (len > 0 && payload != nullptr) {
    for (size_t i = 0; i < len; ++i) {
      out_payload[kTimeSyncMetadataSize + i] = payload[i];
    }
  }

  inout_flags |= kFrameFlagTimeSyncEpoch;
  has_time_sync_tx_ = true;
  last_time_sync_tx_ms_ = current_now_ms_;
  return true;
}

bool EspNowManager::applyTimeSyncFromFrame(const FrameHeader& header, const uint8_t* payload, size_t len) {
  if ((header.flags & kFrameFlagTimeSyncEpoch) == 0) {
    return true;
  }

  if (payload == nullptr || len < kTimeSyncMetadataSize) {
    return false;
  }

  if (!config_.time_sync_enabled || !config_.time_sync_apply_on_slave || config_.local_role != Role::Slave ||
      header.role != Role::Master || time_sink_ == nullptr) {
    return true;
  }

  const uint64_t epoch_s = static_cast<uint64_t>(readLe32(payload));
  if (epoch_s < config_.time_sync_min_valid_epoch_s) {
    return true;
  }

  if (last_time_sync_rx_epoch_s_ != 0) {
    const uint64_t delta = (epoch_s > last_time_sync_rx_epoch_s_) ? (epoch_s - last_time_sync_rx_epoch_s_)
                                                                   : (last_time_sync_rx_epoch_s_ - epoch_s);
    if (delta < static_cast<uint64_t>(config_.time_sync_min_update_delta_s)) {
      return true;
    }
  }

  if (time_sink_->setEpochSec(epoch_s)) {
    last_time_sync_rx_epoch_s_ = epoch_s;
  }

  return true;
}

bool EspNowManager::onRx(const MacAddress& from, const uint8_t* data, size_t len, int rssi) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ScopedMetricsDuration rx_duration(&metrics_.rx_handler_total_us,
                                    &metrics_.rx_handler_last_us,
                                    &metrics_.rx_handler_max_us);
  ++metrics_.rx_frames;
  metrics_.rx_bytes += static_cast<uint64_t>(len);
#endif
  FrameHeader h;
  const uint8_t* payload = nullptr;
  size_t payload_len = 0;
  if (!ProtocolCodec::decode(data, len, h, payload, payload_len)) {
    if (events_ != nullptr) {
      events_->onEvent({Event::Type::PacketDropped, from, 0, "decode failed"});
    }
    if (hooks_ != nullptr) {
      hooks_->onRxFrame(from, MessageType::Error, 0, len, rssi);
    }
    emitRuntimeLog(LibraryLogLevel::Warn,
                   kLogEvtRxDecodeFail,
                   0,
                   static_cast<int32_t>(len),
                   rssi,
                   from.data(),
                   from.size());
    return false;
  }

  if (h.type == MessageType::Discovery &&
      config_.local_role == Role::Master &&
      !discoveryRxEnabled()) {
    // Hard drop discovery packets when discovery window is inactive.
    return true;
  }

  const bool suppress_rx_hook = isOtaWireMessage(h.type);
  if (hooks_ != nullptr && !suppress_rx_hook) {
    hooks_->onRxFrame(from, h.type, h.correlation_id, len, rssi);
  }

  if (h.version != kProtocolVersion) {
    if (events_ != nullptr) {
      events_->onEvent({Event::Type::PacketDropped, from, h.correlation_id, "version mismatch"});
    }
    emitRuntimeLog(LibraryLogLevel::Warn,
                   kLogEvtRxVersionDrop,
                   h.correlation_id,
                   static_cast<int32_t>(h.version),
                   static_cast<int32_t>(kProtocolVersion),
                   from.data(),
                   from.size());
    return false;
  }

  if (!applyTimeSyncFromFrame(h, payload, payload_len)) {
    return false;
  }

  const uint8_t* effective_payload = payload;
  size_t effective_payload_len = payload_len;
  if ((h.flags & kFrameFlagTimeSyncEpoch) != 0) {
    effective_payload += kTimeSyncMetadataSize;
    effective_payload_len -= kTimeSyncMetadataSize;
  }

  return dispatchRxByType(from, h, effective_payload, effective_payload_len, rssi);
}
bool EspNowManager::sendPullRequest(const MacAddress& to,
                                    const uint8_t* payload,
                                    size_t len,
                                    uint32_t corr_id) {
  if (config_.local_role != Role::Master) {
    return false;
  }

  uint8_t wire_service = 0x06;
  uint8_t wire_op = 0x01;
  (void)classifyPullRequest(payload, len, wire_service, wire_op);
  if (!isValidPullRequestWire(wire_service, wire_op)) {
    wire_service = 0x06;
    wire_op = 0x01;
  }
  return sendTyped(to, MessageType::PullRequest, payload, len, corr_id, wire_service, wire_op, 0x01);
}

bool EspNowManager::sendPullResponse(const MacAddress& to,
                                     const uint8_t* payload,
                                     size_t len,
                                     uint32_t corr_id) {
  if (config_.master_initiated_only && config_.local_role != Role::Slave) {
    return false;
  }

  uint8_t wire_service = 0x06;
  uint8_t wire_op = 0x02;
  if (pending_pull_.valid && pending_pull_.peer == to && pending_pull_.corr_id == corr_id) {
    wire_service = pending_pull_.service;
    wire_op = responseOpForRequest(pending_pull_.service, pending_pull_.op);
    pending_pull_.valid = false;
  }
  if (!isValidPullResponseWire(wire_service, wire_op)) {
    wire_service = 0x06;
    wire_op = 0x02;
  }

  return sendTyped(to, MessageType::PullResponse, payload, len, corr_id, wire_service, wire_op, 0x02);
}

bool EspNowManager::sendFirmwareBegin(const MacAddress& to,
                                      uint32_t total_size,
                                      uint32_t chunk_size,
                                      uint32_t image_crc32,
                                      uint32_t corr_id,
                                      const FirmwareImageMetadata* metadata) {
  if (config_.local_role != Role::Master || total_size == 0 || chunk_size == 0) {
    return false;
  }

  FirmwareImageMetadata meta{};
  if (metadata != nullptr) {
    meta = *metadata;
  }
  if (meta.sw_version.size() > 63U || meta.build_id.size() > 63U || meta.target_role.size() > 15U) {
    return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(12U + kFirmwareMetaHeaderSizeV2 + meta.sw_version.size() + meta.build_id.size() +
                  meta.target_role.size());
  appendLe32(payload, total_size);
  appendLe32(payload, chunk_size);
  appendLe32(payload, image_crc32);
  if (!meta.empty()) {
    const bool use_v2 = !meta.target_role.empty();
    payload.push_back(kFirmwareMetaMagic);
    payload.push_back(use_v2 ? kFirmwareMetaVersionV2 : kFirmwareMetaVersionV1);
    payload.push_back(static_cast<uint8_t>(meta.sw_version.size() & 0xFFU));
    payload.push_back(static_cast<uint8_t>(meta.build_id.size() & 0xFFU));
    if (use_v2) {
      payload.push_back(static_cast<uint8_t>(meta.target_role.size() & 0xFFU));
    }
    payload.insert(payload.end(), meta.sw_version.begin(), meta.sw_version.end());
    payload.insert(payload.end(), meta.build_id.begin(), meta.build_id.end());
    if (use_v2) {
      payload.insert(payload.end(), meta.target_role.begin(), meta.target_role.end());
    }
  }

  return sendTyped(to, MessageType::FirmwareBegin, payload.data(), payload.size(), corr_id, 0x07, 0x01, 0x01);
}

bool EspNowManager::sendFirmwareChunk(const MacAddress& to,
                                      uint32_t offset,
                                      const uint8_t* data,
                                      size_t len,
                                      uint32_t corr_id) {
  if (config_.local_role != Role::Master || data == nullptr || len == 0) {
    return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(4 + len);
  appendLe32(payload, offset);
  payload.insert(payload.end(), data, data + len);

  return sendTyped(to, MessageType::FirmwareChunk, payload.data(), payload.size(), corr_id, 0x07, 0x02, 0x01);
}

bool EspNowManager::sendFirmwareEnd(const MacAddress& to,
                                    uint32_t total_size,
                                    uint32_t image_crc32,
                                    uint32_t corr_id) {
  if (config_.local_role != Role::Master || total_size == 0) {
    return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(8);
  appendLe32(payload, total_size);
  appendLe32(payload, image_crc32);

  return sendTyped(to, MessageType::FirmwareEnd, payload.data(), payload.size(), corr_id, 0x07, 0x03, 0x01);
}

bool EspNowManager::sendFirmwareFinalizeAck(const MacAddress& to,
                                            uint32_t corr_id,
                                            uint32_t finalized_offset,
                                            uint16_t status_code) {
  return sendFirmwareStatus(to,
                            corr_id,
                            kFirmwareStatusKindFinalizeAck,
                            finalized_offset,
                            status_code);
}

bool EspNowManager::sendFirmwareStatus(const MacAddress& to,
                                       uint32_t corr_id,
                                       uint8_t kind,
                                       uint32_t offset_bytes,
                                       uint16_t status_code) {
  std::vector<uint8_t> payload;
  if (!buildFirmwareStatusPayload(kind, corr_id, offset_bytes, status_code, payload)) {
    return false;
  }
  return sendTyped(to, MessageType::FirmwareStatus, payload.data(), payload.size(), corr_id, 0x07, 0x04, 0x02);
}

bool EspNowManager::sendTelemetryPushCommand(const MacAddress& to,
                                             const TelemetryPushCommand& cmd,
                                             uint32_t corr_id) {
  if (config_.local_role != Role::Master) {
    return false;
  }

  std::vector<uint8_t> payload;
  if (!encodeTelemetryPushCommand(cmd, payload)) {
    return false;
  }

  return sendTyped(to,
                   MessageType::PullRequest,
                   payload.data(),
                   payload.size(),
                   corr_id,
                   0x04,
                   0x01,
                   0x01);
}

bool EspNowManager::requestChannelChange(const MacAddress& peer,
                                         uint8_t new_channel,
                                         uint32_t now_ms,
                                         uint32_t corr_id) {
  return pairing_.requestChannelSwitch(peer, new_channel, now_ms, corr_id);
}

bool EspNowManager::requestPair(const MacAddress& peer, uint32_t corr_id) {
  const bool ok = pairing_.requestPair(peer, corr_id);
  emitRuntimeLog(ok ? LibraryLogLevel::Info : LibraryLogLevel::Error,
                 kLogEvtPairReq,
                 corr_id,
                 ok ? 1 : 0,
                 0,
                 peer.data(),
                 peer.size());
  if (ok) {
    (void)peer_registry_.markPairing(peer, current_now_ms_);
  }
  return ok;
}

bool EspNowManager::activatePairedPeer(const MacAddress& peer) {
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }

  PairRecord target_record{};
  if (!store_->loadPair(local_mac_, peer, target_record) || !target_record.valid) {
    return false;
  }

  if (target_record.local_role != config_.local_role || target_record.local_mac != local_mac_) {
    return false;
  }

  MacAddress active_peer{};
  const bool had_active = getPairedPeer(active_peer);
  if (had_active && active_peer == peer) {
    return true;
  }

  if (config_.local_role == Role::Slave && had_active && active_peer != peer) {
    (void)clearTelemetryPushConfig(active_peer);
  }

  if (restorePairedLink(target_record)) {
    return true;
  }

  if (config_.local_role == Role::Slave && had_active && active_peer != peer) {
    (void)restoreTelemetryPushConfig(active_peer);
  }
  return false;
}

bool EspNowManager::restorePairedLink(const PairRecord& record) {
  if (!pairing_.restorePairedLink(record)) {
    return false;
  }
  paired_cache_valid_ = true;
  paired_cache_state_ = true;
  paired_cache_peer_ = record.peer_mac;
  if (config_.local_role == Role::Slave) {
    (void)restoreTelemetryPushConfig(record.peer_mac);
    if (has_topology_committed_) {
      std::string topology_error{};
      (void)materializeTopologyPeers_(topology_committed_, &topology_error);
    }
  }
  (void)peer_registry_.markPaired(record.peer_mac, current_now_ms_);
  return true;
}

bool EspNowManager::requestUnpair(const MacAddress& peer, uint32_t corr_id) {
  if (config_.local_role == Role::Master) {
    (void)clearTelemetryPushConfig(peer);
  } else {
    mandatory_event_queue_.clear();
  }
  const bool ok = pairing_.requestUnpair(peer, corr_id);
  emitRuntimeLog(ok ? LibraryLogLevel::Info : LibraryLogLevel::Error,
                 kLogEvtUnpairReq,
                 corr_id,
                 ok ? 1 : 0,
                 0,
                 peer.data(),
                 peer.size());
  return ok;
}

bool EspNowManager::removePeer(const MacAddress& peer, bool erase_persisted) {
  if (erase_persisted) {
    (void)clearTelemetryPushConfig(peer);
    if (store_ != nullptr && store_->enabled()) {
      (void)store_->eraseMetaBlob(local_mac_, peer, kPeerRoleMetaSlot);
    }
  }
  mandatory_event_queue_.clear();
  const bool removed = pairing_.removePeer(peer, erase_persisted);
  peer_role_hints_.erase(std::remove_if(peer_role_hints_.begin(),
                                        peer_role_hints_.end(),
                                        [&](const PeerRoleHintEntry& e) {
                                          return e.peer == peer;
                                        }),
                         peer_role_hints_.end());
  if (removed) {
    (void)peer_registry_.removePeer(peer);
    topology_attached_peers_.erase(
        std::remove(topology_attached_peers_.begin(), topology_attached_peers_.end(), peer),
        topology_attached_peers_.end());
  }
  return removed;
}

bool EspNowManager::isPaired() const { return pairing_.isPaired(); }

bool EspNowManager::getPairedPeer(MacAddress& out_peer) const {
  return pairing_.getPairedPeer(out_peer);
}

bool EspNowManager::hasPersistedPair(const MacAddress& peer) {
  if (store_ == nullptr || !store_->enabled()) {
    MacAddress active{};
    return getPairedPeer(active) && active == peer;
  }

  PairRecord record{};
  if (!store_->loadPair(local_mac_, peer, record) || !record.valid) {
    return false;
  }
  return record.local_role == config_.local_role && record.local_mac == local_mac_;
}

size_t EspNowManager::persistedPairCount() {
  std::vector<MacAddress> peers;
  getPersistedPeers(peers);
  return peers.size();
}

void EspNowManager::getPersistedPeers(std::vector<MacAddress>& out) {
  out.clear();
  if (store_ == nullptr || !store_->enabled()) {
    MacAddress active{};
    if (getPairedPeer(active)) {
      out.push_back(active);
    }
    return;
  }

  std::vector<PairIndexEntry> entries;
  if (store_->loadPairIndex(local_mac_, entries)) {
    struct IndexedPeer {
      MacAddress peer{};
      uint32_t pair_seq = 0;
    };
    std::vector<IndexedPeer> ordered{};
    ordered.reserve(entries.size());

    for (const auto& entry : entries) {
      if (!entry.valid || entry.pair_seq == 0U) {
        continue;
      }
      PairRecord record{};
      if (!store_->loadPair(local_mac_, entry.peer_mac, record) || !record.valid) {
        continue;
      }
      if (record.local_role != config_.local_role || record.local_mac != local_mac_) {
        continue;
      }

      const auto seen = std::find_if(ordered.begin(),
                                     ordered.end(),
                                     [&](const IndexedPeer& existing) {
                                       return existing.peer == entry.peer_mac;
                                     });
      if (seen != ordered.end()) {
        continue;
      }

      IndexedPeer peer_entry{};
      peer_entry.peer = entry.peer_mac;
      peer_entry.pair_seq = entry.pair_seq;
      ordered.push_back(peer_entry);
    }

    std::sort(ordered.begin(), ordered.end(), [](const IndexedPeer& lhs, const IndexedPeer& rhs) {
      if (lhs.pair_seq != rhs.pair_seq) {
        return lhs.pair_seq < rhs.pair_seq;
      }
      return compareMac(lhs.peer, rhs.peer) < 0;
    });

    out.reserve(ordered.size());
    for (const auto& item : ordered) {
      out.push_back(item.peer);
    }
    return;
  }

}

void EspNowManager::getPersistedPeersWithRole(std::vector<PersistedPeerRoleEntry>& out) {
  out.clear();
  std::vector<MacAddress> peers{};
  getPersistedPeers(peers);
  out.reserve(peers.size());
  for (const auto& peer : peers) {
    PersistedPeerRoleEntry entry{};
    entry.peer = peer;
    uint8_t role_code = 0U;
    if (loadPeerRoleHint(peer, role_code)) {
      entry.role_code = role_code;
    }
    out.push_back(entry);
  }
}

void EspNowManager::upsertPeerRoleHintCache_(const MacAddress& peer, uint8_t role_code) {
  if (role_code == 0U) {
    return;
  }
  for (auto& e : peer_role_hints_) {
    if (e.peer == peer) {
      e.role_code = role_code;
      return;
    }
  }
  PeerRoleHintEntry add{};
  add.peer = peer;
  add.role_code = role_code;
  peer_role_hints_.push_back(add);
}

bool EspNowManager::findPeerRoleHintCache_(const MacAddress& peer, uint8_t& out_role_code) const {
  out_role_code = 0U;
  for (const auto& e : peer_role_hints_) {
    if (e.peer == peer) {
      out_role_code = e.role_code;
      return true;
    }
  }
  return false;
}

bool EspNowManager::getPersistedPeerChannels(std::vector<PersistedPeerChannelEntry>& out) const {
  out.clear();
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }

  std::vector<MacAddress> peers{};
  const_cast<EspNowManager*>(this)->getPersistedPeers(peers);
  out.reserve(peers.size());
  for (const auto& peer : peers) {
    uint8_t channel = 0U;
    if (!store_->loadChannel(local_mac_, peer, channel)) {
      continue;
    }
    PersistedPeerChannelEntry entry{};
    entry.peer = peer;
    entry.channel = channel;
    entry.channel_key = store_->makeKey(RecordKind::Channel, local_mac_, peer);
    out.push_back(entry);
  }
  return true;
}

bool EspNowManager::applyRuntimeChannelToAllPeers(uint8_t channel) {
  if (channel < 1U || channel > 14U) {
    return false;
  }
  if (!transport_.setChannel(channel)) {
    return false;
  }
  if (hooks_ != nullptr) {
    hooks_->onChannelCommitted(channel);
  }
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }

  std::vector<MacAddress> peers{};
  getPersistedPeers(peers);
  TopologySnapshot committed{};
  if (getTopologySnapshot(TopologyState::Committed, committed)) {
    for (const auto& slot : committed.slots) {
      if (!slot.enabled) {
        continue;
      }
      if (std::find(peers.begin(), peers.end(), slot.peer) == peers.end()) {
        peers.push_back(slot.peer);
      }
    }
  }
  bool ok = true;
  for (const auto& peer : peers) {
    ok = store_->saveChannel(local_mac_, peer, channel) && ok;
  }
  return ok;
}

void EspNowManager::getPeerRegistrySnapshot(std::vector<PeerRecord>& out) const {
  peer_registry_.snapshot(out);
}

bool EspNowManager::stageTopology(const TopologySnapshot& snapshot, std::string* out_error) {
  TopologySnapshot normalized = snapshot;
  normalized.state = TopologyState::Staged;
  if (!validateTopologySnapshot_(normalized, out_error)) {
    return false;
  }
  if (has_topology_committed_ &&
      normalized.topology_version < topology_committed_.topology_version) {
    if (out_error != nullptr) {
      *out_error = "topology_version_stale";
    }
    return false;
  }

  const TopologySnapshot old_staged = topology_staged_;
  const bool had_staged = has_topology_staged_;
  topology_staged_ = normalized;
  has_topology_staged_ = true;
  if (!saveTopologySnapshotMeta_(kTopologyMetaSlotStaged, topology_staged_)) {
    topology_staged_ = old_staged;
    has_topology_staged_ = had_staged;
    if (out_error != nullptr) {
      *out_error = "topology_persist_failed";
    }
    return false;
  }
  return true;
}

bool EspNowManager::commitStagedTopology(std::string* out_error) {
  if (!has_topology_staged_) {
    if (out_error != nullptr) {
      *out_error = "topology_not_staged";
    }
    return false;
  }

  TopologySnapshot normalized = topology_staged_;
  normalized.state = TopologyState::Committed;
  if (!validateTopologySnapshot_(normalized, out_error)) {
    return false;
  }
  if (has_topology_committed_ &&
      normalized.topology_version < topology_committed_.topology_version) {
    if (out_error != nullptr) {
      *out_error = "topology_version_stale";
    }
    return false;
  }

  const TopologySnapshot old_committed = topology_committed_;
  const bool had_committed = has_topology_committed_;
  const std::vector<MacAddress> old_attached = topology_attached_peers_;

  if (config_.local_role == Role::Slave) {
    std::string apply_error{};
    if (!materializeTopologyPeers_(normalized, &apply_error)) {
      topology_attached_peers_ = old_attached;
      if (out_error != nullptr) {
        *out_error = "topology_apply_failed";
      }
      return false;
    }
    if (!saveTopologyLmkMeta_(normalized)) {
      // Best-effort rollback to previously active committed topology peers.
      topology_attached_peers_ = old_attached;
      if (had_committed) {
        std::string rollback_error{};
        (void)materializeTopologyPeers_(old_committed, &rollback_error);
      }
      if (out_error != nullptr) {
        *out_error = "topology_persist_failed";
      }
      return false;
    }
  }

  topology_committed_ = normalized;
  has_topology_committed_ = true;
  if (!saveTopologySnapshotMeta_(kTopologyMetaSlotCommitted, topology_committed_)) {
    topology_committed_ = old_committed;
    has_topology_committed_ = had_committed;
    if (config_.local_role == Role::Slave) {
      topology_attached_peers_ = old_attached;
      if (had_committed) {
        std::string rollback_error{};
        (void)materializeTopologyPeers_(old_committed, &rollback_error);
      }
    }
    if (out_error != nullptr) {
      *out_error = "topology_persist_failed";
    }
    return false;
  }

  topology_staged_ = TopologySnapshot{};
  has_topology_staged_ = false;
  (void)eraseTopologySnapshotMeta_(kTopologyMetaSlotStaged);
  return true;
}

bool EspNowManager::clearStagedTopology() {
  topology_staged_ = TopologySnapshot{};
  has_topology_staged_ = false;
  return eraseTopologySnapshotMeta_(kTopologyMetaSlotStaged);
}

bool EspNowManager::getTopologyStatus(TopologyStatus& out_status) const {
  out_status = TopologyStatus{};
  out_status.has_staged = has_topology_staged_;
  out_status.has_committed = has_topology_committed_;
  if (has_topology_staged_) {
    out_status.staged = topology_staged_;
  }
  if (has_topology_committed_) {
    out_status.committed = topology_committed_;
  }
  return true;
}

bool EspNowManager::getTopologySnapshot(TopologyState state, TopologySnapshot& out_snapshot) const {
  out_snapshot = TopologySnapshot{};
  if (state == TopologyState::Staged) {
    if (!has_topology_staged_) {
      return false;
    }
    out_snapshot = topology_staged_;
    return true;
  }
  if (state == TopologyState::Committed) {
    if (!has_topology_committed_) {
      return false;
    }
    out_snapshot = topology_committed_;
    return true;
  }
  return false;
}

bool EspNowManager::resolveTopologyTargetIndex(int8_t target_idx,
                                               TopologySlot& out_slot,
                                               std::string* out_error,
                                               bool committed) const {
  out_slot = TopologySlot{};
  const TopologyState state = committed ? TopologyState::Committed : TopologyState::Staged;
  TopologySnapshot snapshot{};
  if (!getTopologySnapshot(state, snapshot)) {
    if (out_error != nullptr) {
      *out_error = "topology_missing";
    }
    return false;
  }

  if (target_idx == 0) {
    if (out_error != nullptr) {
      *out_error = "invalid_index";
    }
    return false;
  }
  if ((target_idx < 0 && static_cast<uint8_t>(-target_idx) > snapshot.index_neg) ||
      (target_idx > 0 && static_cast<uint8_t>(target_idx) > snapshot.index_pos)) {
    if (out_error != nullptr) {
      *out_error = "out_of_window";
    }
    return false;
  }

  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled) {
      continue;
    }
    if (slot.relative_index == target_idx) {
      out_slot = slot;
      return true;
    }
  }
  if (out_error != nullptr) {
    *out_error = "index_unmapped";
  }
  return false;
}

bool EspNowManager::sendTopologyTrigger(const TopologyTriggerRequest& request,
                                        uint32_t corr_id,
                                        uint16_t* out_seq,
                                        std::string* out_error) {
  if (!has_topology_committed_) {
    if (out_error != nullptr) {
      *out_error = "topology_missing";
    }
    return false;
  }
  if (request.direction != 1U && request.direction != 2U) {
    if (out_error != nullptr) {
      *out_error = "invalid_direction";
    }
    return false;
  }
  if (request.source_virtual_index != 0xFFU && request.source_virtual_index > 0x0FU) {
    if (out_error != nullptr) {
      *out_error = "invalid_source_virtual_index";
    }
    return false;
  }
  if (local_profile_id_ == kProfileUnknown) {
    if (out_error != nullptr) {
      *out_error = "local_profile_unknown";
    }
    return false;
  }

  TopologySlot slot{};
  if (!resolveTopologyTargetIndex(request.target_index, slot, out_error, true)) {
    return false;
  }

  manager_helpers::TopologyTriggerPayload trigger{};
  trigger.topology_version = topology_committed_.topology_version;
  trigger.seq = topology_trigger_seq_++;
  if (topology_trigger_seq_ == 0U) {
    topology_trigger_seq_ = 1U;
  }
  trigger.src_role = static_cast<uint8_t>(local_profile_id_ & 0xFFU);
  trigger.src_vid = request.source_virtual_index;
  trigger.dst_role = slot.peer_role;
  trigger.dst_vid = slot.peer_virtual_index;
  if (!isTopologyRolePairAllowed(trigger.src_role, trigger.dst_role)) {
    if (out_error != nullptr) {
      *out_error = "invalid_role_pair";
    }
    return false;
  }
  trigger.direction = request.direction;
  trigger.delay_ms = request.delay_ms;
  trigger.hold_ms = request.hold_ms;

  std::vector<uint8_t> payload{};
  if (!manager_helpers::buildTopologyTriggerPayload(trigger, payload)) {
    if (out_error != nullptr) {
      *out_error = "trigger_encode_failed";
    }
    return false;
  }

  uint32_t send_corr = corr_id;
  if (send_corr == 0U) {
    send_corr = (0x54000000U |
                 (static_cast<uint32_t>(static_cast<uint8_t>(request.target_index)) << 16) |
                 trigger.seq);
  }
  if (!sendTyped(slot.peer, MessageType::TopologyTrigger, payload.data(), payload.size(), send_corr)) {
    if (out_error != nullptr) {
      *out_error = "send_failed";
    }
    return false;
  }

  if (out_seq != nullptr) {
    *out_seq = trigger.seq;
  }
  return true;
}

bool EspNowManager::findTopologyAuthorizedSourceSlot_(const MacAddress& from,
                                                      uint8_t src_role,
                                                      uint8_t src_vid,
                                                      uint8_t dst_vid,
                                                      TopologySlot& out_slot) const {
  out_slot = TopologySlot{};
  if (!has_topology_committed_) {
    return false;
  }

  for (const auto& slot : topology_committed_.slots) {
    if (!slot.enabled) {
      continue;
    }
    if (slot.peer != from) {
      continue;
    }
    if (slot.peer_role != src_role) {
      continue;
    }
    if (slot.peer_virtual_index != src_vid) {
      continue;
    }
    if (slot.local_virtual_index != dst_vid) {
      continue;
    }
    out_slot = slot;
    return true;
  }
  return false;
}

void EspNowManager::pruneTopologyTriggerDedup_(uint32_t now_ms) {
  while (!topology_trigger_dedup_.empty()) {
    const uint32_t age = static_cast<uint32_t>(now_ms - topology_trigger_dedup_.front().seen_ms);
    if (age <= kTopologyTriggerDedupWindowMs &&
        topology_trigger_dedup_.size() <= kTopologyTriggerDedupMaxEntries) {
      break;
    }
    topology_trigger_dedup_.pop_front();
  }
}

bool EspNowManager::isTopologyTriggerDuplicate_(const MacAddress& from,
                                                uint8_t src_role,
                                                uint8_t src_vid,
                                                uint16_t seq) {
  pruneTopologyTriggerDedup_(current_now_ms_);
  for (const auto& entry : topology_trigger_dedup_) {
    if (entry.source == from &&
        entry.src_role == src_role &&
        entry.src_vid == src_vid &&
        entry.seq == seq) {
      return true;
    }
  }
  TopologyTriggerDedupEntry entry{};
  entry.source = from;
  entry.src_role = src_role;
  entry.src_vid = src_vid;
  entry.seq = seq;
  entry.seen_ms = current_now_ms_;
  topology_trigger_dedup_.push_back(entry);
  pruneTopologyTriggerDedup_(current_now_ms_);
  return false;
}

bool EspNowManager::onRxTopologyTrigger(const MacAddress& from,
                                        const FrameHeader& header,
                                        const uint8_t* payload,
                                        size_t payload_len) {
  if (config_.local_role != Role::Slave) {
    return false;
  }

  manager_helpers::TopologyTriggerPayload trigger{};
  if (!manager_helpers::parseTopologyTriggerPayload(payload, payload_len, trigger)) {
    return false;
  }
  (void)peer_registry_.updateLiveness(from, true, current_now_ms_);

  uint8_t reason = kTopologyTriggerReasonOk;
  bool accepted = false;
  bool duplicate = false;
  TopologySlot authorized_slot{};
  const uint8_t local_role = static_cast<uint8_t>(local_profile_id_ & 0xFFU);

  if (!has_topology_committed_ || trigger.topology_version != topology_committed_.topology_version) {
    reason = kTopologyTriggerReasonStaleTopology;
  } else if (local_profile_id_ == kProfileUnknown || trigger.dst_role != local_role) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if (!isTopologyRolePairAllowed(local_role, trigger.src_role)) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if ((trigger.src_vid != 0xFFU && trigger.src_vid > 0x0FU) ||
             (trigger.dst_vid != 0xFFU && trigger.dst_vid > 0x0FU)) {
    reason = kTopologyTriggerReasonBadIndex;
  } else if (trigger.direction != 1U && trigger.direction != 2U) {
    reason = kTopologyTriggerReasonRangeRejected;
  } else if (!findTopologyAuthorizedSourceSlot_(from,
                                                trigger.src_role,
                                                trigger.src_vid,
                                                trigger.dst_vid,
                                                authorized_slot)) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if (isTopologyTriggerDuplicate_(from, trigger.src_role, trigger.src_vid, trigger.seq)) {
    accepted = true;
    duplicate = true;
  } else {
    accepted = true;
  }

  if (events_ != nullptr) {
    Event e{};
    e.peer = from;
    e.correlation_id = header.correlation_id;
    e.event_id = trigger.seq;
    e.severity = trigger.direction;
    e.event_value = static_cast<int32_t>(trigger.hold_ms);
    e.src_role = trigger.src_role;
    e.src_vid = trigger.src_vid;
    e.dst_role = trigger.dst_role;
    e.dst_vid = trigger.dst_vid;
    e.direction = trigger.direction;
    e.delay_ms = trigger.delay_ms;
    e.hold_ms = trigger.hold_ms;
    e.reason = reason;
    e.result = accepted ? kTopologyTriggerResultAccepted : kTopologyTriggerResultRejected;
    if (accepted && duplicate) {
      e.type = Event::Type::TopologyTriggerDuplicate;
      e.message = "topology trigger duplicate";
    } else if (accepted) {
      e.type = Event::Type::TopologyTriggerReceived;
      e.message = "topology trigger accepted";
    } else {
      e.type = Event::Type::TopologyTriggerRejected;
      e.message = "topology trigger rejected";
      e.severity = reason;
    }
    events_->onEvent(e);
  }

  if (accepted && !duplicate && hooks_ != nullptr) {
    IPlatformHooks::TopologyTriggerNotification note{};
    note.source = from;
    note.seq = trigger.seq;
    note.src_role = trigger.src_role;
    note.src_vid = trigger.src_vid;
    note.dst_role = trigger.dst_role;
    note.dst_vid = trigger.dst_vid;
    note.direction = trigger.direction;
    note.delay_ms = trigger.delay_ms;
    note.hold_ms = trigger.hold_ms;
    hooks_->onTopologyTrigger(note);
  }

  manager_helpers::TopologyTriggerAckPayload ack{};
  ack.topology_version = has_topology_committed_ ? topology_committed_.topology_version : trigger.topology_version;
  ack.seq = trigger.seq;
  ack.src_role = local_role;
  ack.src_vid = trigger.dst_vid;
  ack.dst_role = trigger.src_role;
  ack.dst_vid = trigger.src_vid;
  ack.ack_seq = trigger.seq;
  ack.result = accepted ? kTopologyTriggerResultAccepted : kTopologyTriggerResultRejected;
  ack.reason = accepted ? kTopologyTriggerReasonOk : reason;

  std::vector<uint8_t> ack_payload{};
  if (manager_helpers::buildTopologyTriggerAckPayload(ack, ack_payload)) {
    (void)sendTyped(from,
                    MessageType::TopologyTriggerAck,
                    ack_payload.data(),
                    ack_payload.size(),
                    header.correlation_id);
  }
  blinkForMessage(header.type);
  return true;
}

bool EspNowManager::onRxTopologyTriggerAck(const MacAddress& from,
                                           const FrameHeader& header,
                                           const uint8_t* payload,
                                           size_t payload_len) {
  manager_helpers::TopologyTriggerAckPayload ack{};
  if (!manager_helpers::parseTopologyTriggerAckPayload(payload, payload_len, ack)) {
    return false;
  }
  (void)peer_registry_.updateLiveness(from, true, current_now_ms_);
  if (events_ != nullptr) {
    Event e{};
    e.type = Event::Type::TopologyTriggerAck;
    e.peer = from;
    e.correlation_id = header.correlation_id;
    e.event_id = ack.ack_seq;
    e.severity = ack.result;
    e.event_value = static_cast<int32_t>(ack.reason);
    e.src_role = ack.src_role;
    e.src_vid = ack.src_vid;
    e.dst_role = ack.dst_role;
    e.dst_vid = ack.dst_vid;
    e.result = ack.result;
    e.reason = ack.reason;
    e.ack_seq = ack.ack_seq;
    e.message = (ack.result == kTopologyTriggerResultAccepted)
                    ? "topology trigger ack accepted"
                    : "topology trigger ack rejected";
    events_->onEvent(e);
  }
  blinkForMessage(header.type);
  return true;
}

bool EspNowManager::validateTopologySnapshot_(TopologySnapshot& inout_snapshot, std::string* out_error) const {
  const uint8_t local_profile_code = static_cast<uint8_t>(local_profile_id_ & 0xFFU);
  const uint8_t local_slot_cap = topologySlotCapForLocalRole(local_profile_code);

  if (inout_snapshot.schema_version == 0U) {
    inout_snapshot.schema_version = kTopologySchemaVersion;
  }
  if (inout_snapshot.schema_version != kTopologySchemaVersion) {
    if (out_error != nullptr) {
      *out_error = "invalid_schema_version";
    }
    return false;
  }
  if (inout_snapshot.topology_version == 0U) {
    if (out_error != nullptr) {
      *out_error = "invalid_topology_version";
    }
    return false;
  }
  if (inout_snapshot.index_neg > local_slot_cap ||
      inout_snapshot.index_pos > local_slot_cap) {
    if (out_error != nullptr) {
      *out_error = "invalid_index_window";
    }
    return false;
  }

  uint8_t enabled_slots = 0U;
  uint8_t enabled_groups = 0U;
  for (const auto& group : inout_snapshot.groups) {
    if (!group.enabled) {
      continue;
    }
    if (group.group_id == 0U || isAllZeroSeed(group.seed)) {
      if (out_error != nullptr) {
        *out_error = "invalid_group_seed";
      }
      return false;
    }
    ++enabled_groups;
  }

  for (size_t i = 0; i < inout_snapshot.slots.size(); ++i) {
    const TopologySlot& slot = inout_snapshot.slots[i];
    if (!slot.enabled) {
      continue;
    }
    if (slot.peer_role == 0U) {
      if (out_error != nullptr) {
        *out_error = "invalid_peer_role";
      }
      return false;
    }
    if (!isTopologyRolePairAllowed(local_profile_code, slot.peer_role)) {
      if (out_error != nullptr) {
        *out_error = "invalid_role_pair";
      }
      return false;
    }
    if (isAllZeroMac(slot.peer) || isBroadcastMac(slot.peer)) {
      if (out_error != nullptr) {
        *out_error = "invalid_peer_mac";
      }
      return false;
    }
    if (slot.relative_index == 0 ||
        slot.relative_index < -static_cast<int8_t>(local_slot_cap) ||
        slot.relative_index > static_cast<int8_t>(local_slot_cap)) {
      if (out_error != nullptr) {
        *out_error = "invalid_relative_index";
      }
      return false;
    }
    if (!isVirtualIndexAllowedForRole(local_profile_code, slot.local_virtual_index)) {
      if (out_error != nullptr) {
        *out_error = "invalid_local_virtual_index";
      }
      return false;
    }
    if (!isVirtualIndexAllowedForRole(slot.peer_role, slot.peer_virtual_index)) {
      if (out_error != nullptr) {
        *out_error = "invalid_peer_virtual_index";
      }
      return false;
    }

    bool group_found = false;
    for (const auto& group : inout_snapshot.groups) {
      if (!group.enabled) {
        continue;
      }
      if (group.group_id == slot.group_id) {
        group_found = true;
        break;
      }
    }
    if (!group_found) {
      if (out_error != nullptr) {
        *out_error = "group_id_missing";
      }
      return false;
    }

    for (size_t j = i + 1; j < inout_snapshot.slots.size(); ++j) {
      const TopologySlot& other = inout_snapshot.slots[j];
      if (!other.enabled) {
        continue;
      }
      if (slot.peer == other.peer &&
          slot.peer_role == other.peer_role &&
          slot.peer_virtual_index == other.peer_virtual_index) {
        if (out_error != nullptr) {
          *out_error = "duplicate_logical_peer";
        }
        return false;
      }
      if (slot.relative_index == other.relative_index) {
        if (out_error != nullptr) {
          *out_error = "duplicate_relative_index";
        }
        return false;
      }
    }
    ++enabled_slots;
  }

  if (enabled_slots > local_slot_cap ||
      enabled_groups > static_cast<uint8_t>(kTopologyMaxGroups)) {
    if (out_error != nullptr) {
      *out_error = "capacity_exceeded";
    }
    return false;
  }
  inout_snapshot.enabled_slot_count = enabled_slots;
  inout_snapshot.enabled_group_count = enabled_groups;
  const uint32_t computed_checksum = computeTopologyChecksum_(inout_snapshot);
  if (inout_snapshot.checksum != 0U && inout_snapshot.checksum != computed_checksum) {
    if (out_error != nullptr) {
      *out_error = "invalid_checksum";
    }
    return false;
  }
  inout_snapshot.checksum = computed_checksum;
  return true;
}

uint32_t EspNowManager::computeTopologyChecksum_(const TopologySnapshot& snapshot) const {
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
  mix8(static_cast<uint8_t>(snapshot.state));
  mix32(snapshot.topology_version);
  mix8(snapshot.index_neg);
  mix8(snapshot.index_pos);
  mix8(snapshot.enabled_slot_count);
  mix8(snapshot.enabled_group_count);
  for (const auto& slot : snapshot.slots) {
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
  for (const auto& group : snapshot.groups) {
    mix8(static_cast<uint8_t>(group.enabled ? 1U : 0U));
    mix8(group.group_id);
    for (uint8_t b : group.seed) {
      mix8(b);
    }
  }
  return hash;
}

bool EspNowManager::saveTopologySnapshotMeta_(uint8_t slot, const TopologySnapshot& snapshot) {
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }
  std::vector<uint8_t> blob;
  if (!serializeTopologySnapshotBlob(snapshot, blob)) {
    return false;
  }
  return saveLocalMetaBlob(slot, blob.data(), blob.size());
}

bool EspNowManager::loadTopologySnapshotMeta_(uint8_t slot, TopologySnapshot& out_snapshot) const {
  out_snapshot = TopologySnapshot{};
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }
  std::vector<uint8_t> blob;
  if (!loadLocalMetaBlob(slot, blob)) {
    return false;
  }
  return deserializeTopologySnapshotBlob(blob, out_snapshot);
}

bool EspNowManager::eraseTopologySnapshotMeta_(uint8_t slot) {
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }
  return eraseLocalMetaBlob(slot);
}

void EspNowManager::restoreTopologySnapshots_() {
  topology_staged_ = TopologySnapshot{};
  topology_committed_ = TopologySnapshot{};
  has_topology_staged_ = false;
  has_topology_committed_ = false;
  topology_attached_peers_.clear();

  TopologySnapshot committed{};
  if (loadTopologySnapshotMeta_(kTopologyMetaSlotCommitted, committed)) {
    committed.state = TopologyState::Committed;
    std::string err{};
    if (validateTopologySnapshot_(committed, &err)) {
      topology_committed_ = committed;
      has_topology_committed_ = true;
    } else {
      (void)eraseTopologySnapshotMeta_(kTopologyMetaSlotCommitted);
    }
  }

  TopologySnapshot staged{};
  if (loadTopologySnapshotMeta_(kTopologyMetaSlotStaged, staged)) {
    staged.state = TopologyState::Staged;
    std::string err{};
    if (validateTopologySnapshot_(staged, &err) &&
        (!has_topology_committed_ || staged.topology_version >= topology_committed_.topology_version)) {
      topology_staged_ = staged;
      has_topology_staged_ = true;
    } else {
      (void)eraseTopologySnapshotMeta_(kTopologyMetaSlotStaged);
    }
  }
}

bool EspNowManager::resolveTopologyMasterMac_(MacAddress& out_master) const {
  out_master = MacAddress{};
  if (pairing_.getPairedPeer(out_master)) {
    return true;
  }
  return false;
}

bool EspNowManager::findTopologyGroupSeed_(const TopologySnapshot& snapshot,
                                           uint8_t group_id,
                                           std::array<uint8_t, 32>& out_seed) const {
  out_seed = {};
  if (group_id == 0U) {
    return false;
  }
  for (const auto& group : snapshot.groups) {
    if (!group.enabled) {
      continue;
    }
    if (group.group_id == group_id) {
      out_seed = group.seed;
      return true;
    }
  }
  return false;
}

bool EspNowManager::deriveTopologySlotLmk_(const TopologySnapshot& snapshot,
                                           const TopologySlot& slot,
                                           LmkKey& out_lmk,
                                           std::string* out_error) const {
  out_lmk = {};
  if (!slot.enabled) {
    if (out_error != nullptr) {
      *out_error = "topology_slot_disabled";
    }
    return false;
  }
  if (slot.peer == local_mac_) {
    if (out_error != nullptr) {
      *out_error = "topology_slot_self_peer";
    }
    return false;
  }

  std::array<uint8_t, 32> group_seed{};
  if (!findTopologyGroupSeed_(snapshot, slot.group_id, group_seed)) {
    if (out_error != nullptr) {
      *out_error = "topology_group_seed_missing";
    }
    return false;
  }

  MacAddress master_mac{};
  if (!resolveTopologyMasterMac_(master_mac)) {
    if (out_error != nullptr) {
      *out_error = "topology_master_missing";
    }
    return false;
  }

  if (!deriveLmkFromTopologySeed(group_seed, master_mac, local_mac_, slot.peer, slot.group_id, out_lmk)) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_derive_failed";
    }
    return false;
  }
  return true;
}

bool EspNowManager::materializeTopologyPeers_(const TopologySnapshot& snapshot, std::string* out_error) {
  if (config_.local_role != Role::Slave) {
    return true;
  }
  if (peer_window_ == nullptr) {
    if (out_error != nullptr) {
      *out_error = "topology_peer_window_missing";
    }
    return false;
  }

  struct DesiredPeer {
    MacAddress mac{};
    LmkKey lmk{};
    uint8_t group_id = 0;
  };

  std::map<MacAddress, DesiredPeer> peer_choice{};
  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled) {
      continue;
    }
    LmkKey slot_lmk{};
    std::string derive_error{};
    if (!deriveTopologySlotLmk_(snapshot, slot, slot_lmk, &derive_error)) {
      if (out_error != nullptr) {
        *out_error = derive_error;
      }
      return false;
    }

    auto it = peer_choice.find(slot.peer);
    if (it == peer_choice.end() || slot.group_id < it->second.group_id) {
      DesiredPeer chosen{};
      chosen.mac = slot.peer;
      chosen.group_id = slot.group_id;
      chosen.lmk = slot_lmk;
      peer_choice[slot.peer] = chosen;
    } else if (slot.group_id == it->second.group_id && it->second.lmk != slot_lmk) {
      if (out_error != nullptr) {
        *out_error = "topology_lmk_conflict";
      }
      return false;
    }
  }

  std::vector<DesiredPeer> desired{};
  desired.reserve(peer_choice.size());
  for (const auto& kv : peer_choice) {
    desired.push_back(kv.second);
  }

  MacAddress active_peer{};
  const bool has_active_peer = pairing_.getPairedPeer(active_peer);

  for (const auto& peer : desired) {
    if (has_active_peer && peer.mac == active_peer) {
      continue;
    }
    if (!peer_window_->requestAttach(peer.mac, true, &peer.lmk)) {
      if (out_error != nullptr) {
        *out_error = "topology_attach_failed";
      }
      return false;
    }
  }

  for (const auto& old_peer : topology_attached_peers_) {
    if (has_active_peer && old_peer == active_peer) {
      continue;
    }
    const bool still_needed =
        std::any_of(desired.begin(), desired.end(), [&](const DesiredPeer& p) { return p.mac == old_peer; });
    if (!still_needed) {
      if (!peer_window_->requestEvict(old_peer)) {
        if (out_error != nullptr) {
          *out_error = "topology_evict_failed";
        }
        return false;
      }
    }
  }

  std::vector<MacAddress> next_attached{};
  next_attached.reserve(desired.size());
  for (const auto& peer : desired) {
    if (has_active_peer && peer.mac == active_peer) {
      continue;
    }
    next_attached.push_back(peer.mac);
  }
  topology_attached_peers_ = std::move(next_attached);
  return true;
}

bool EspNowManager::saveTopologyLmkMeta_(const TopologySnapshot& snapshot) {
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }

  struct PersistedLmk {
    MacAddress peer{};
    LmkKey lmk{};
    uint8_t group_id = 0;
  };

  std::map<MacAddress, PersistedLmk> peer_choice{};
  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled) {
      continue;
    }
    LmkKey slot_lmk{};
    std::string derive_error{};
    if (!deriveTopologySlotLmk_(snapshot, slot, slot_lmk, &derive_error)) {
      return false;
    }
    auto it = peer_choice.find(slot.peer);
    if (it == peer_choice.end() || slot.group_id < it->second.group_id) {
      PersistedLmk entry{};
      entry.peer = slot.peer;
      entry.group_id = slot.group_id;
      entry.lmk = slot_lmk;
      peer_choice[slot.peer] = entry;
    } else if (slot.group_id == it->second.group_id && it->second.lmk != slot_lmk) {
      return false;
    }
  }

  std::vector<PersistedLmk> persisted{};
  persisted.reserve(peer_choice.size());
  for (const auto& kv : peer_choice) {
    persisted.push_back(kv.second);
  }

  if (persisted.empty()) {
    return eraseLocalMetaBlob(kTopologyMetaSlotLmk);
  }
  if (persisted.size() > 255U) {
    return false;
  }

  std::vector<uint8_t> blob{};
  blob.reserve(2U + persisted.size() * kTopologyLmkBlobEntrySize);
  blob.push_back(kTopologyLmkBlobVersion);
  blob.push_back(static_cast<uint8_t>(persisted.size()));
  for (const auto& entry : persisted) {
    blob.insert(blob.end(), entry.peer.begin(), entry.peer.end());
    blob.insert(blob.end(), entry.lmk.begin(), entry.lmk.end());
    blob.push_back(entry.group_id);
  }
  return saveLocalMetaBlob(kTopologyMetaSlotLmk, blob.data(), blob.size());
}

void EspNowManager::tick(uint32_t now_ms) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ScopedMetricsDuration tick_duration(&metrics_.tick_total_us,
                                      &metrics_.tick_last_us,
                                      &metrics_.tick_max_us);
  ++metrics_.tick_count;
#endif
  current_now_ms_ = now_ms;
  pairing_.tick(now_ms);
  syncPairedCacheState_();
  tickControlRxQueue(now_ms);
  tickPullRequestQueue(now_ms);
  tickPullResponseQueue(now_ms);
  tickFirmwareRxQueue(now_ms);
  tickDiscoveryCache(now_ms);
  tickOtaBootCompletionNotice(now_ms);
  tickOtaFinalizeStatusRetry(now_ms);
  tickMandatoryEventQueue(now_ms);
  tickTelemetryPush(now_ms);
}

void EspNowManager::tickPullRequestQueue(uint32_t now_ms) {
  (void)now_ms;
  if (config_.local_role != Role::Slave || pull_request_queue_.empty()) {
    return;
  }

  size_t budget = kPullRequestDispatchBudgetPerTick;
  while (budget-- > 0U && !pull_request_queue_.empty()) {
    QueuedPullRequest request = std::move(pull_request_queue_.front());
    pull_request_queue_.pop_front();
    (void)processQueuedPullRequest_(request);
  }
}

bool EspNowManager::processQueuedPullRequest_(const QueuedPullRequest& request) {
  if (events_ != nullptr) {
    events_->onEvent({Event::Type::PullRequestSeen, request.from, request.corr_id, "pull request rx"});
  }
  blinkForMessage(MessageType::PullRequest);

  pending_pull_.valid = true;
  pending_pull_.peer = request.from;
  pending_pull_.corr_id = request.corr_id;
  pending_pull_.service = request.wire_service;
  pending_pull_.op = request.wire_op;

  const uint8_t* payload = request.payload.empty() ? nullptr : request.payload.data();
  const size_t payload_len = request.payload.size();

  if (handleTelemetryPushControl(request.from, request.corr_id, payload, payload_len)) {
    return true;
  }

  if (control_plane_ != nullptr) {
    const bool handled = control_plane_->onPullRequest(request.from, request.corr_id, payload, payload_len);
    if (handled) {
      return true;
    }
  }

  // Never silently drop undecoded/unsupported pull payloads.
  // Return an explicit error response so master-side flows do not stall on timeout.
  std::vector<uint8_t> reply;
  if (buildDescriptorReply(false, "pull request unsupported", reply)) {
    (void)sendPullResponse(request.from, reply.data(), reply.size(), request.corr_id);
  }
  return false;
}

void EspNowManager::tickPullResponseQueue(uint32_t now_ms) {
  (void)now_ms;
  if (config_.local_role != Role::Master || pull_response_queue_.empty()) {
    return;
  }

  size_t budget = kPullResponseDispatchBudgetPerTick;
  while (budget-- > 0U && !pull_response_queue_.empty()) {
    QueuedPullResponse response = std::move(pull_response_queue_.front());
    pull_response_queue_.pop_front();
    (void)processQueuedPullResponse_(response);
  }
}

bool EspNowManager::processQueuedPullResponse_(const QueuedPullResponse& response) {
  if (events_ != nullptr) {
    events_->onEvent({Event::Type::PullResponseSeen, response.from, response.corr_id, "pull response rx"});
  }
  blinkForMessage(MessageType::PullResponse);
  (void)peer_registry_.updateLiveness(response.from, true, current_now_ms_);

  if (control_plane_ == nullptr) {
    return true;
  }
  const uint8_t* payload = response.payload.empty() ? nullptr : response.payload.data();
  return control_plane_->onPullResponse(response.from, response.corr_id, payload, response.payload.size());
}

void EspNowManager::tickFirmwareRxQueue(uint32_t now_ms) {
  (void)now_ms;
  if (config_.local_role != Role::Slave || firmware_rx_queue_.empty()) {
    return;
  }

  size_t budget = kFirmwareRxDispatchBudgetPerTick;
  while (budget-- > 0U && !firmware_rx_queue_.empty()) {
    QueuedFirmwareRx frame = std::move(firmware_rx_queue_.front());
    firmware_rx_queue_.pop_front();
    (void)processQueuedFirmwareRx_(frame);
  }
}

bool EspNowManager::processQueuedFirmwareRx_(const QueuedFirmwareRx& frame) {
  const uint8_t* payload = frame.payload.empty() ? nullptr : frame.payload.data();
  const size_t payload_len = frame.payload.size();

  switch (frame.type) {
    case MessageType::FirmwareBegin:
      blinkForMessage(MessageType::FirmwareBegin);
      return handleFirmwareBegin(frame.from, frame.corr_id, payload, payload_len);
    case MessageType::FirmwareChunk:
      blinkForMessage(MessageType::FirmwareChunk);
      return handleFirmwareChunk(frame.from, frame.corr_id, payload, payload_len);
    case MessageType::FirmwareEnd:
      blinkForMessage(MessageType::FirmwareEnd);
      return handleFirmwareEnd(frame.from, frame.corr_id, payload, payload_len);
    default:
      return false;
  }
}

void EspNowManager::tickControlRxQueue(uint32_t now_ms) {
  (void)now_ms;
  if (control_rx_queue_.empty()) {
    return;
  }

  size_t budget = kControlRxDispatchBudgetPerTick;
  while (budget-- > 0U && !control_rx_queue_.empty()) {
    QueuedControlRx frame = std::move(control_rx_queue_.front());
    control_rx_queue_.pop_front();
    (void)processQueuedControlRx_(frame);
  }
}

bool EspNowManager::processQueuedControlRx_(const QueuedControlRx& frame) {
  FrameHeader header{};
  header.type = frame.type;
  header.correlation_id = frame.corr_id;
  const uint8_t* payload = frame.payload.empty() ? nullptr : frame.payload.data();
  const size_t payload_len = frame.payload.size();

  switch (frame.type) {
    case MessageType::Discovery:
      return onRxDiscovery(frame.from, header, payload, payload_len, frame.rssi);
    case MessageType::PairInit:
      return onRxPairInit(frame.from, header, payload, payload_len);
    case MessageType::PairInitAck:
      return onRxPairInitAck(frame.from, header, payload, payload_len);
    case MessageType::PairConfirm:
      return onRxPairConfirm(frame.from, header, payload, payload_len);
    case MessageType::PairConfirmAck:
      return onRxPairConfirmAck(frame.from, header, payload, payload_len);
    case MessageType::PairBusy:
      blinkForMessage(frame.type);
      return pairing_.onPairBusy(frame.from, frame.corr_id);
    case MessageType::UnpairRequest:
      return onRxUnpairRequest(frame.from, header);
    case MessageType::UnpairAck:
      blinkForMessage(frame.type);
      return pairing_.onUnpairAck(frame.from, frame.corr_id);
    case MessageType::EventReport:
      return onRxEventReport(frame.from, header, payload, payload_len);
    case MessageType::TopologyTrigger:
      return onRxTopologyTrigger(frame.from, header, payload, payload_len);
    case MessageType::TopologyTriggerAck:
      return onRxTopologyTriggerAck(frame.from, header, payload, payload_len);
    case MessageType::ChannelSwitchPrepare:
      return onRxChannelSwitchPrepare(frame.from, header, payload, payload_len);
    case MessageType::ChannelSwitchAck:
      blinkForMessage(frame.type);
      return pairing_.onChannelSwitchAck(frame.from, frame.corr_id);
    case MessageType::ChannelSwitchCommitAck:
      blinkForMessage(frame.type);
      return pairing_.onChannelSwitchCommitAck(frame.from, frame.corr_id);
    case MessageType::FirmwareStatus:
      blinkForMessage(frame.type);
      return handleFirmwareStatus(frame.from, frame.corr_id, payload, payload_len);
    default:
      return false;
  }
}

void EspNowManager::getDiscoveredPeers(std::vector<MacAddress>& out) const {
  out.clear();
  out.reserve(discovery_cache_.size());
  for (const auto& e : discovery_cache_) {
    out.push_back(e.mac);
  }
}

bool EspNowManager::publishMandatoryEvent(uint16_t event_id,
                                          uint8_t severity,
                                          int32_t value,
                                          uint32_t event_ts_s) {
  if (config_.local_role != Role::Slave || event_id == 0) {
    return false;
  }

  PendingMandatoryEvent ev{};
  ev.event_id = event_id;
  ev.severity = severity;
  ev.value = value;
  ev.event_ts_s = event_ts_s;
  ev.corr_id = mandatory_event_corr_seq_++;

  for (auto& queued : mandatory_event_queue_) {
    if (queued.event_id == ev.event_id) {
      queued = ev;
      return true;
    }
  }

  if (mandatory_event_queue_.size() >= config_.mandatory_event_max_queue) {
    if (events_ != nullptr) {
      Event dropped{};
      dropped.type = Event::Type::MandatoryEventDropped;
      dropped.event_id = mandatory_event_queue_.front().event_id;
      dropped.severity = mandatory_event_queue_.front().severity;
      dropped.event_value = mandatory_event_queue_.front().value;
      dropped.event_ts_s = mandatory_event_queue_.front().event_ts_s;
      dropped.message = "event queue full";
      events_->onEvent(dropped);
    }
    mandatory_event_queue_.pop_front();
  }

  mandatory_event_queue_.push_back(ev);
  return true;
}

void EspNowManager::touchDiscovery(const MacAddress& from) {
  for (auto& e : discovery_cache_) {
    if (e.mac == from) {
      e.ttl_ms = config_.discovery_peer_ttl_ms;
      e.last_tick_ms = 0;
      return;
    }
  }

  DiscoveryCacheEntry e{};
  e.mac = from;
  e.ttl_ms = config_.discovery_peer_ttl_ms;
  e.last_tick_ms = 0;
  discovery_cache_.push_back(e);
}

void EspNowManager::tickDiscoveryCache(uint32_t now_ms) {
  for (size_t i = 0; i < discovery_cache_.size();) {
    auto& e = discovery_cache_[i];
    if (e.ttl_ms == 0) {
      if (events_ != nullptr) {
        events_->onEvent({Event::Type::DiscoveryExpired, e.mac, 0, "discovery ttl expired"});
      }
      discovery_cache_.erase(discovery_cache_.begin() + static_cast<long>(i));
      continue;
    }

    if (e.last_tick_ms == 0) {
      e.last_tick_ms = now_ms;
      ++i;
      continue;
    }

    const uint32_t elapsed = now_ms - e.last_tick_ms;
    e.last_tick_ms = now_ms;
    if (elapsed >= e.ttl_ms) {
      e.ttl_ms = 0;
    } else {
      e.ttl_ms -= elapsed;
    }
    ++i;
  }
}

void EspNowManager::syncPairedCacheState_() {
  MacAddress peer{};
  const bool paired = pairing_.isPaired() && pairing_.getPairedPeer(peer);
  paired_cache_valid_ = true;
  paired_cache_state_ = paired;
  paired_cache_peer_ = paired ? peer : MacAddress{};
}

bool EspNowManager::buildDescriptorReply(bool ok,
                                         const std::string& message,
                                         std::vector<uint8_t>& out_payload) {
  return descriptor_reply::buildDescriptorAckReply(ok, message, out_payload);
}
bool EspNowManager::handleTelemetryPushControl(const MacAddress& from,
                                               uint32_t corr_id,
                                               const uint8_t* payload,
                                               size_t len) {
  if (config_.local_role != Role::Slave || telemetry_push_provider_ == nullptr) {
    return false;
  }

  TelemetryPushCommand cmd{};
  if (!parseTelemetryPushCommand(payload, len, cmd)) {
    return false;
  }

  bool ok = true;
  std::string msg = "stream ok";

  auto reject = [&](const char* why) {
    ok = false;
    msg = why;
  };

  MacAddress paired_peer{};
  if (!pairing_.getPairedPeer(paired_peer) || paired_peer != from) {
    reject("stream rejected: peer not active master");
  }

  if (ok && push_session_.has_master && push_session_.master != from) {
    reject("stream rejected: owner mismatch");
  }

  auto is_interval_valid = [](uint32_t v) {
    return v >= 200 && v <= 60000;
  };
  auto is_gap_valid = [](uint32_t v) {
    return v >= 50 && v <= 60000;
  };

  if (ok && (cmd.action == TelemetryPushAction::Start || cmd.action == TelemetryPushAction::Update)) {
    if (cmd.config.metrics.empty()) {
      reject("stream invalid: no metrics");
    }
    if (!is_interval_valid(cmd.config.interval_ms)) {
      reject("stream invalid: stream interval out of bounds");
    }
    if (!is_gap_valid(cmd.config.min_report_gap_ms)) {
      reject("stream invalid: stream min_report_gap out of bounds");
    }
    if (cmd.config.metrics.size() > 16U) {
      reject("stream invalid: too many metrics");
    }
  }

  if (ok) {
    switch (cmd.action) {
      case TelemetryPushAction::Start:
      case TelemetryPushAction::Update: {
        TelemetryPushSession next{};
        next.enabled = true;
        next.paused = false;
        next.stream_id = cmd.config.stream_id;
        next.mode = cmd.config.mode;
        next.interval_ms = cmd.config.interval_ms;
        next.min_gap_ms = cmd.config.min_report_gap_ms;
        next.master = from;
        next.has_master = true;
        next.metrics.reserve(cmd.config.metrics.size());

        for (const auto& m : cmd.config.metrics) {
          std::string resolved_key;
          if (!resolveTelemetryMetricIdentity(m, resolved_key)) {
            reject("stream invalid: unknown metric key");
            break;
          }

          bool dup = false;
          for (const auto& ex : next.metrics) {
            if (ex.key == resolved_key) {
              dup = true;
              break;
            }
          }
          if (dup) {
            reject("stream invalid: duplicate metric key");
            break;
          }

          PushMetricState ps{};
          ps.key = resolved_key;
          ps.enabled = m.enabled;
          ps.mode = m.mode;
          ps.interval_ms = (m.interval_ms == 0) ? next.interval_ms : m.interval_ms;
          ps.min_gap_ms = (m.min_report_gap_ms == 0) ? next.min_gap_ms : m.min_report_gap_ms;
          ps.next_periodic_ms = current_now_ms_ + ps.interval_ms;
          ps.use_threshold = m.use_threshold;
          ps.delta_abs = m.delta_abs;

          if ((ps.mode == TelemetryPushMode::Periodic || ps.mode == TelemetryPushMode::Hybrid) &&
              !is_interval_valid(ps.interval_ms)) {
            reject("stream invalid: metric interval out of bounds");
            break;
          }
          if ((ps.mode == TelemetryPushMode::OnChange || ps.mode == TelemetryPushMode::Hybrid) &&
              !is_gap_valid(ps.min_gap_ms)) {
            reject("stream invalid: metric min_report_gap out of bounds");
            break;
          }

          next.metrics.push_back(ps);
        }
        if (ok) {
          bool any_enabled = false;
          for (const auto& m : next.metrics) {
            if (m.enabled) {
              any_enabled = true;
              break;
            }
          }
          if (!any_enabled) {
            reject("stream invalid: all metrics disabled");
          }
        }

        if (ok) {
          push_session_ = next;
          if (!persistTelemetryPushConfig()) {
            reject("persist_failed");
            push_session_ = TelemetryPushSession{};
          } else {
            msg = (cmd.action == TelemetryPushAction::Start) ? "stream started" : "stream updated";
          }
        }
      } break;

      case TelemetryPushAction::Pause:
        if (!push_session_.enabled) {
          reject("stream not active");
        } else {
          push_session_.paused = true;
          if (!persistTelemetryPushConfig()) {
            reject("persist_failed");
          } else {
            msg = "stream paused";
          }
        }
        break;

      case TelemetryPushAction::Resume:
        if (!push_session_.enabled) {
          reject("stream not active");
        } else {
          push_session_.paused = false;
          if (!persistTelemetryPushConfig()) {
            reject("persist_failed");
          } else {
            msg = "stream resumed";
          }
        }
        break;

      case TelemetryPushAction::Stop:
        push_session_ = TelemetryPushSession{};
        if (!clearTelemetryPushConfig(from)) {
          reject("persist_failed");
        } else {
          msg = "stream stopped";
        }
        break;

      case TelemetryPushAction::Get:
        msg = push_session_.enabled ? (push_session_.paused ? "stream active paused" : "stream active")
                                    : "stream inactive";
        break;

      default:
        reject("stream invalid action");
        break;
    }
  }

  std::vector<uint8_t> reply;
  if (!buildDescriptorReply(ok, msg, reply)) {
    return false;
  }
  return sendPullResponse(from, reply.data(), reply.size(), corr_id);
}


bool EspNowManager::sendTelemetryPushReport(const MacAddress& to,
                                            const std::vector<TelemetrySample>& samples,
                                            uint32_t corr_id,
                                            const char* cause) {
  std::vector<uint8_t> payload;
  if (!descriptor_reply::buildTelemetrySnapshotReply(samples, cause, payload)) {
    return false;
  }

  return sendTyped(to,
                   MessageType::PullResponse,
                   payload.data(),
                   payload.size(),
                   corr_id,
                   0x04,
                   0x04,
                   0x02);
}
bool EspNowManager::sendMandatoryEventReport(const MacAddress& to, const PendingMandatoryEvent& ev) {
  std::vector<uint8_t> payload;
  if (!buildMandatoryEventPayload(ev.event_id, ev.severity, ev.value, ev.event_ts_s, payload)) {
    return false;
  }

  return sendTyped(to,
                   MessageType::EventReport,
                   payload.data(),
                   payload.size(),
                   ev.corr_id,
                   0x06,
                   0x10,
                   0x04);
}

void EspNowManager::tickOtaBootCompletionNotice(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || firmware_sink_ == nullptr || !pairing_.isPaired()) {
    return;
  }
  if (ota_boot_notice_last_try_ms_ != 0U &&
      (now_ms - ota_boot_notice_last_try_ms_) < 1000U) {
    return;
  }

  FirmwareImageMetadata meta{};
  uint32_t epoch_s = 0U;
  if (!firmware_sink_->peekBootCompletionNotice(meta, epoch_s)) {
    return;
  }

  ota_boot_notice_last_try_ms_ = now_ms;
  const int32_t packed = static_cast<int32_t>(meta.sw_version.empty() ? 1U : (static_cast<uint32_t>(meta.sw_version.size()) & 0x7FFFU));
  (void)publishMandatoryEvent(kOtaBootCompleteEventId, 1U, packed, epoch_s);
}

void EspNowManager::tickOtaFinalizeStatusRetry(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || !ota_finalize_pending_) {
    return;
  }
  if (!pairing_.isPaired()) {
    return;
  }
  if (static_cast<int32_t>(now_ms - ota_finalize_started_ms_) >= static_cast<int32_t>(kFinalizeStatusRetryWindowMs)) {
    ota_finalize_pending_ = false;
    ota_finalize_corr_id_ = 0U;
    ota_finalize_offset_ = 0U;
    ota_finalize_status_code_ = 0U;
    ota_finalize_kind_ = 0U;
    ota_finalize_started_ms_ = 0U;
    ota_finalize_last_tx_ms_ = 0U;
    return;
  }
  if (static_cast<int32_t>(now_ms - ota_finalize_last_tx_ms_) < static_cast<int32_t>(kFinalizeStatusRetryIntervalMs)) {
    return;
  }
  if (sendFirmwareStatus(ota_finalize_peer_,
                         ota_finalize_corr_id_,
                         ota_finalize_kind_,
                         ota_finalize_offset_,
                         ota_finalize_status_code_)) {
    ota_finalize_last_tx_ms_ = now_ms;
  }
}

void EspNowManager::tickMandatoryEventQueue(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || mandatory_event_queue_.empty()) {
    return;
  }

  MacAddress master{};
  if (!pairing_.getPairedPeer(master)) {
    return;
  }

  if (mandatory_event_backoff_until_ms_ != 0 &&
      static_cast<int32_t>(now_ms - mandatory_event_backoff_until_ms_) < 0) {
    return;
  }

  if (mandatory_event_last_tx_ms_ != 0 &&
      (now_ms - mandatory_event_last_tx_ms_) < config_.mandatory_event_min_gap_ms) {
    return;
  }

  PendingMandatoryEvent ev = mandatory_event_queue_.front();
  if (!sendMandatoryEventReport(master, ev)) {
    mandatory_event_last_tx_ms_ = now_ms;
    mandatory_event_queue_.front().retry_count++;
    mandatory_event_fail_streak_++;

    if (mandatory_event_queue_.front().retry_count > config_.mandatory_event_retry_max) {
      if (events_ != nullptr) {
        Event dropped{};
        dropped.type = Event::Type::MandatoryEventDropped;
        dropped.peer = master;
        dropped.correlation_id = ev.corr_id;
        dropped.event_id = ev.event_id;
        dropped.severity = ev.severity;
        dropped.event_value = ev.value;
        dropped.event_ts_s = ev.event_ts_s;
        dropped.message = "event retry exceeded";
        events_->onEvent(dropped);
      }
      mandatory_event_queue_.pop_front();
    }

    if (mandatory_event_fail_streak_ >= config_.mandatory_event_fail_backoff_threshold) {
      mandatory_event_backoff_until_ms_ = now_ms + config_.mandatory_event_backoff_ms;
      mandatory_event_fail_streak_ = 0;
    }
    return;
  }

  mandatory_event_last_tx_ms_ = now_ms;
  mandatory_event_backoff_until_ms_ = 0;
  mandatory_event_fail_streak_ = 0;
  if (ev.event_id == kOtaBootCompleteEventId && firmware_sink_ != nullptr) {
    std::string msg;
    (void)firmware_sink_->clearBootCompletionNotice(&msg);
  }
  mandatory_event_queue_.pop_front();

  if (events_ != nullptr) {
    Event sent{};
    sent.type = Event::Type::MandatoryEventSent;
    sent.peer = master;
    sent.correlation_id = ev.corr_id;
    sent.event_id = ev.event_id;
    sent.severity = ev.severity;
    sent.event_value = ev.value;
    sent.event_ts_s = ev.event_ts_s;
    sent.message = "mandatory event tx";
    events_->onEvent(sent);
  }
}

void EspNowManager::tickTelemetryPush(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || !pairing_.isPaired()) {
    return;
  }
  if (!push_session_.enabled || push_session_.paused || !push_session_.has_master || telemetry_push_provider_ == nullptr) {
    return;
  }
  if (push_session_.backoff_until_ms != 0 && static_cast<int32_t>(now_ms - push_session_.backoff_until_ms) < 0) {
    return;
  }

  telemetry_snapshot_cache_.clear();
  if (!telemetry_push_provider_->getTelemetrySnapshot(telemetry_snapshot_cache_)) {
    return;
  }

  alignSnapshotToProfileInPlace(local_profile_, telemetry_snapshot_cache_, telemetry_aligned_snapshot_cache_);
  if (telemetry_aligned_snapshot_cache_.empty()) {
    return;
  }

  buildSnapshotIndex(telemetry_aligned_snapshot_cache_, telemetry_snapshot_index_cache_);

  const size_t metric_count = push_session_.metrics.size();
  telemetry_metric_samples_cache_.assign(metric_count, nullptr);
  telemetry_metric_values_cache_.assign(metric_count, 0.0f);
  telemetry_metric_flags_cache_.assign(metric_count, 0U);
  telemetry_send_samples_cache_.clear();
  telemetry_send_samples_cache_.reserve(metric_count);

  static constexpr uint8_t kMetricValueOk = 0x01;
  static constexpr uint8_t kMetricSent = 0x02;

  bool periodic_cause = false;
  bool threshold_cause = false;

  // Phase 1: evaluate each metric once and decide whether it should be sent now.
  for (size_t i = 0; i < metric_count; ++i) {
    auto& metric = push_session_.metrics[i];
    if (!metric.enabled) {
      continue;
    }

    auto it = telemetry_snapshot_index_cache_.find(metric.key);
    if (it == telemetry_snapshot_index_cache_.end()) {
      continue;
    }

    const TelemetrySample* sample = it->second;
    telemetry_metric_samples_cache_[i] = sample;
    if (sample == nullptr) {
      continue;
    }

    float parsed_value = 0.0f;
    if (parseSampleFloat(sample->value, parsed_value)) {
      telemetry_metric_values_cache_[i] = parsed_value;
      telemetry_metric_flags_cache_[i] |= kMetricValueOk;
    }

    const bool periodic_mode =
        (metric.mode == TelemetryPushMode::Periodic || metric.mode == TelemetryPushMode::Hybrid);
    const bool change_mode =
        (metric.mode == TelemetryPushMode::OnChange || metric.mode == TelemetryPushMode::Hybrid);

    bool should_send = false;
    if (periodic_mode && (metric.next_periodic_ms == 0 || static_cast<int32_t>(now_ms - metric.next_periodic_ms) >= 0)) {
      should_send = true;
      periodic_cause = true;
    }

    if (change_mode &&
        (telemetry_metric_flags_cache_[i] & kMetricValueOk) != 0 &&
        metric.has_last &&
        (now_ms - metric.last_report_ms) >= metric.min_gap_ms) {
      const float delta = std::fabs(telemetry_metric_values_cache_[i] - metric.last_value);
      const float threshold = metric.use_threshold ? metric.delta_abs : 0.0f;
      if (delta >= threshold) {
        should_send = true;
        threshold_cause = true;
      }
    }

    if (should_send) {
      bool already_added = false;
      for (const auto& ex : telemetry_send_samples_cache_) {
        if (ex.key == sample->key) {
          already_added = true;
          break;
        }
      }
      if (!already_added) {
        telemetry_send_samples_cache_.push_back(*sample);
      }
      telemetry_metric_flags_cache_[i] |= kMetricSent;
    }
  }

  if (telemetry_send_samples_cache_.empty()) {
    return;
  }

  const char* cause = periodic_cause && threshold_cause
                          ? "periodic+threshold"
                          : (threshold_cause ? "threshold_cross" : "periodic_tick");

  const bool sent =
      sendTelemetryPushReport(push_session_.master, telemetry_send_samples_cache_, telemetry_push_seq_++, cause);
  if (!sent) {
    if (++push_session_.consecutive_tx_fail >= 5) {
      push_session_.backoff_until_ms = now_ms + 2000;
      push_session_.consecutive_tx_fail = 0;
    }
    return;
  }

  push_session_.consecutive_tx_fail = 0;
  push_session_.backoff_until_ms = 0;

  // Phase 2: update metric state only after successful transmit.
  for (size_t i = 0; i < metric_count; ++i) {
    auto& metric = push_session_.metrics[i];
    if (!metric.enabled) {
      continue;
    }

    if (metric.mode == TelemetryPushMode::Periodic || metric.mode == TelemetryPushMode::Hybrid) {
      metric.next_periodic_ms = now_ms + metric.interval_ms;
    }

    if ((telemetry_metric_flags_cache_[i] & kMetricValueOk) == 0U) {
      continue;
    }

    metric.has_last = true;
    metric.last_value = telemetry_metric_values_cache_[i];
    if ((telemetry_metric_flags_cache_[i] & kMetricSent) != 0U) {
      metric.last_report_ms = now_ms;
    }
  }
}
bool EspNowManager::isKnownTelemetryKey(const std::string& key) const {
  if (key.empty()) {
    return false;
  }

  if (local_profile_ != nullptr) {
    return findProfileTelemetryByKey(local_profile_, key) != nullptr;
  }

  if (telemetry_push_provider_ == nullptr) {
    return false;
  }

  std::vector<TelemetryDescriptor> schema;
  if (telemetry_push_provider_->getTelemetrySchema(schema) && !schema.empty()) {
    for (const auto& t : schema) {
      if (t.key == key) {
        return true;
      }
    }
  }

  std::vector<TelemetrySample> snap;
  if (telemetry_push_provider_->getTelemetrySnapshot(snap)) {
    for (const auto& s : snap) {
      if (s.key == key) {
        return true;
      }
    }
  }
  return false;
}

bool EspNowManager::telemetryKeyFromIndex(uint16_t metric_index, std::string& out_key) const {
  out_key.clear();

  if (local_profile_ != nullptr) {
    const ProfileTelemetryMetricSpec* spec = findProfileTelemetryById(local_profile_, metric_index);
    if (spec != nullptr && spec->key != nullptr && spec->key[0] != '\0') {
      out_key = spec->key;
      return true;
    }
    return false;
  }

  if (telemetry_push_provider_ == nullptr) {
    return false;
  }

  std::vector<TelemetryDescriptor> schema;
  if (!telemetry_push_provider_->getTelemetrySchema(schema) || schema.empty()) {
    return false;
  }

  if (static_cast<size_t>(metric_index) >= schema.size()) {
    return false;
  }

  out_key = schema[metric_index].key;
  return !out_key.empty();
}

bool EspNowManager::resolveTelemetryMetricIdentity(const TelemetryPushMetricConfig& metric,
                                                    std::string& out_key) const {
  out_key.clear();

  const bool has_key = !metric.key.empty();
  const bool has_index = metric.has_metric_index;

  // Exactly one metric identity form is allowed.
  if (has_key == has_index) {
    return false;
  }

  if (has_key) {
    out_key = metric.key;
    return isKnownTelemetryKey(out_key);
  }

  return telemetryKeyFromIndex(metric.metric_index, out_key);
}

bool EspNowManager::persistTelemetryPushConfig() {
  if (store_ == nullptr || !store_->enabled() || !push_session_.has_master) {
    return false;
  }

  static constexpr uint8_t kTelemetryMetaSlot = 1;
  std::vector<uint8_t> blob;
  blob.reserve(128);

  auto putU8 = [&](uint8_t v) { blob.push_back(v); };
  auto putU16 = [&](uint16_t v) {
    blob.push_back(static_cast<uint8_t>(v & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  };
  auto putU32 = [&](uint32_t v) {
    blob.push_back(static_cast<uint8_t>(v & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto putF32 = [&](float f) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&f);
    blob.push_back(p[0]);
    blob.push_back(p[1]);
    blob.push_back(p[2]);
    blob.push_back(p[3]);
  };

  putU8(1);
  putU8(push_session_.enabled ? 1 : 0);
  putU8(push_session_.paused ? 1 : 0);
  putU8(static_cast<uint8_t>(push_session_.mode));
  putU16(push_session_.stream_id);
  putU32(push_session_.interval_ms);
  putU32(push_session_.min_gap_ms);

  if (push_session_.metrics.size() > 255U) {
    return false;
  }
  putU8(static_cast<uint8_t>(push_session_.metrics.size()));

  for (const auto& m : push_session_.metrics) {
    if (m.key.empty() || m.key.size() > 63U) {
      return false;
    }
    putU8(static_cast<uint8_t>(m.key.size()));
    blob.insert(blob.end(), m.key.begin(), m.key.end());
    putU8(m.enabled ? 1 : 0);
    putU8(static_cast<uint8_t>(m.mode));
    putU32(m.interval_ms);
    putU32(m.min_gap_ms);
    putU8(m.use_threshold ? 1 : 0);
    putF32(m.delta_abs);
  }

  return store_->saveMetaBlob(local_mac_, push_session_.master, kTelemetryMetaSlot, blob.data(), blob.size());
}

bool EspNowManager::restoreTelemetryPushConfig(const MacAddress& master) {
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }

  static constexpr uint8_t kTelemetryMetaSlot = 1;
  std::vector<uint8_t> blob;
  if (!store_->loadMetaBlob(local_mac_, master, kTelemetryMetaSlot, blob)) {
    return false;
  }
  if (blob.size() < 15) {
    (void)store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
    return false;
  }

  size_t off = 0;
  auto getU8 = [&](uint8_t& out) {
    if (off + 1 > blob.size()) return false;
    out = blob[off++];
    return true;
  };
  auto getU16 = [&](uint16_t& out) {
    if (off + 2 > blob.size()) return false;
    out = static_cast<uint16_t>(blob[off]) | (static_cast<uint16_t>(blob[off + 1]) << 8);
    off += 2;
    return true;
  };
  auto getU32 = [&](uint32_t& out) {
    if (off + 4 > blob.size()) return false;
    out = static_cast<uint32_t>(blob[off]) |
          (static_cast<uint32_t>(blob[off + 1]) << 8) |
          (static_cast<uint32_t>(blob[off + 2]) << 16) |
          (static_cast<uint32_t>(blob[off + 3]) << 24);
    off += 4;
    return true;
  };
  auto getF32 = [&](float& out) {
    if (off + 4 > blob.size()) return false;
    uint8_t* p = reinterpret_cast<uint8_t*>(&out);
    p[0] = blob[off + 0];
    p[1] = blob[off + 1];
    p[2] = blob[off + 2];
    p[3] = blob[off + 3];
    off += 4;
    return true;
  };

  uint8_t version = 0;
  if (!getU8(version) || version != 1) {
    (void)store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
    return false;
  }

  TelemetryPushSession restored{};
  restored.master = master;
  restored.has_master = true;

  uint8_t tmp = 0;
  if (!getU8(tmp)) return false;
  restored.enabled = (tmp != 0);
  if (!getU8(tmp)) return false;
  restored.paused = (tmp != 0);
  if (!getU8(tmp)) return false;
  restored.mode = static_cast<TelemetryPushMode>(tmp);
  if (!getU16(restored.stream_id)) return false;
  if (!getU32(restored.interval_ms)) return false;
  if (!getU32(restored.min_gap_ms)) return false;

  uint8_t metric_count = 0;
  if (!getU8(metric_count)) return false;
  restored.metrics.reserve(metric_count);

  for (uint8_t i = 0; i < metric_count; ++i) {
    uint8_t key_len = 0;
    if (!getU8(key_len) || key_len == 0 || (off + key_len) > blob.size()) {
      return false;
    }

    PushMetricState s{};
    s.key.assign(reinterpret_cast<const char*>(blob.data() + off),
                 reinterpret_cast<const char*>(blob.data() + off + key_len));
    off += key_len;

    if (!getU8(tmp)) return false;
    s.enabled = (tmp != 0);
    if (!getU8(tmp)) return false;
    s.mode = static_cast<TelemetryPushMode>(tmp);
    if (!getU32(s.interval_ms)) return false;
    if (!getU32(s.min_gap_ms)) return false;
    if (!getU8(tmp)) return false;
    s.use_threshold = (tmp != 0);
    if (!getF32(s.delta_abs)) return false;

    if (!isKnownTelemetryKey(s.key)) {
      continue;
    }
    s.next_periodic_ms = current_now_ms_ + s.interval_ms;
    restored.metrics.push_back(s);
  }

  if (restored.enabled && restored.metrics.empty()) {
    (void)store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
    return false;
  }

  push_session_ = restored;
  return true;
}

bool EspNowManager::clearTelemetryPushConfig(const MacAddress& master) {
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }

  static constexpr uint8_t kTelemetryMetaSlot = 1;

  // Treat missing key as success (idempotent stop/reset/unpair behavior).
  std::vector<uint8_t> existing;
  const bool has_existing = store_->loadMetaBlob(local_mac_, master, kTelemetryMetaSlot, existing);
  bool erased = true;
  if (has_existing) {
    erased = store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
  }

  if (push_session_.has_master && push_session_.master == master) {
    push_session_ = TelemetryPushSession{};
  }
  return erased;
}

bool EspNowManager::sendTyped(const MacAddress& to,
                              MessageType type,
                              const uint8_t* payload,
                              size_t len,
                              uint32_t corr_id,
                              uint8_t wire_service,
                              uint8_t wire_op,
                              uint8_t wire_msg_type) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ScopedMetricsDuration tx_duration(&metrics_.tx_send_total_us,
                                    &metrics_.tx_send_last_us,
                                    &metrics_.tx_send_max_us);
#endif
  FrameHeader h;
  h.version = kProtocolVersion;
  h.type = type;
  h.flags = 0;
  h.correlation_id = corr_id;
  h.role = config_.local_role;
  h.wire_service = wire_service;
  h.wire_op_code = wire_op;
  h.wire_msg_type = wire_msg_type;

  std::vector<uint8_t> wrapped_payload;
  (void)appendTimeSyncMetadata(payload, len, h.flags, wrapped_payload);

  const uint8_t* wire_payload = payload;
  size_t wire_len = len;
  if (!wrapped_payload.empty()) {
    wire_payload = wrapped_payload.data();
    wire_len = wrapped_payload.size();
  }

  h.payload_length = static_cast<uint16_t>(wire_len);

  std::vector<uint8_t> bytes;
  if (!ProtocolCodec::encode(h, wire_payload, wire_len, bytes)) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
    ++metrics_.tx_failures;
#endif
    if (hooks_ != nullptr) {
      hooks_->onTxFrame(to, type, corr_id, wire_len, false);
    }
    emitRuntimeLog(LibraryLogLevel::Error,
                   kLogEvtTxEncodeFail,
                   corr_id,
                   static_cast<int32_t>(type),
                   static_cast<int32_t>(wire_len),
                   to.data(),
                   to.size());
    return false;
  }

#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ++metrics_.tx_frames;
  metrics_.tx_bytes += static_cast<uint64_t>(bytes.size());
#endif

  const bool ok = transport_.send(to, bytes.data(), bytes.size());
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  if (!ok) {
    ++metrics_.tx_failures;
  }
#endif
  const bool suppress_tx_hook = isOtaWireMessage(type);
  if (hooks_ != nullptr && !suppress_tx_hook) {
    hooks_->onTxFrame(to, type, corr_id, wire_len, ok);
  }
  if (!ok) {
    emitRuntimeLog(LibraryLogLevel::Error,
                   kLogEvtTxSendFail,
                   corr_id,
                   static_cast<int32_t>(type),
                   static_cast<int32_t>(wire_len),
                   to.data(),
                   to.size());
  }
  return ok;
}

void EspNowManager::blinkForMessage(MessageType type) {
  if (hooks_ == nullptr) {
    return;
  }

  switch (type) {
    case MessageType::Discovery:
      hooks_->onRgbBlink(0, 0, 255, 50);
      break;
    case MessageType::PairInit:
    case MessageType::PairInitAck:
    case MessageType::PairConfirm:
    case MessageType::PairConfirmAck:
      hooks_->onRgbBlink(255, 165, 0, 80);
      break;
    case MessageType::UnpairRequest:
    case MessageType::PairBusy:
    case MessageType::UnpairAck:
      hooks_->onRgbBlink(255, 0, 0, 120);
      break;
    case MessageType::ChannelSwitchPrepare:
    case MessageType::ChannelSwitchAck:
    case MessageType::ChannelSwitchCommitAck:
      hooks_->onRgbBlink(255, 0, 255, 100);
      break;
    case MessageType::PullRequest:
    case MessageType::PullResponse:
      hooks_->onRgbBlink(0, 255, 0, 40);
      break;
    case MessageType::EventReport:
      hooks_->onRgbBlink(255, 220, 0, 60);
      break;
    case MessageType::TopologyTrigger:
      hooks_->onRgbBlink(0, 180, 255, 45);
      break;
    case MessageType::TopologyTriggerAck:
      hooks_->onRgbBlink(0, 120, 220, 35);
      break;
    case MessageType::FirmwareBegin:
      hooks_->onRgbBlink(0, 255, 255, 120);
      break;
    case MessageType::FirmwareChunk:
      // Suppress per-chunk blink to keep WiFi callback path lightweight during OTA bursts.
      break;
    case MessageType::FirmwareEnd:
      hooks_->onRgbBlink(255, 255, 255, 180);
      break;
    case MessageType::FirmwareStatus:
      // Keep OTA data/control path as lightweight as possible in WiFi callback context.
      break;
    default:
      hooks_->onRgbBlink(128, 128, 128, 20);
      break;
  }
}

bool EspNowManager::handleFirmwareBegin(const MacAddress& from,
                                        uint32_t corr_id,
                                        const uint8_t* payload,
                                        size_t len) {
  if (firmware_sink_ == nullptr || payload == nullptr || len < 12U) {
    return false;
  }

  const uint32_t total_size = readLe32(payload + 0);
  const uint32_t chunk_size = readLe32(payload + 4);
  const uint32_t crc = readLe32(payload + 8);
  FirmwareImageMetadata meta{};
  if (len > 12U) {
    if (len < (12U + kFirmwareMetaHeaderSizeV1)) {
      return false;
    }
    if (payload[12] != kFirmwareMetaMagic) {
      return false;
    }
    const uint8_t meta_version = payload[13];
    const uint8_t sw_len = payload[14];
    const uint8_t build_len = payload[15];
    if (sw_len > 63U || build_len > 63U) {
      return false;
    }
    if (meta_version == kFirmwareMetaVersionV1) {
      const size_t expected_len =
          12U + kFirmwareMetaHeaderSizeV1 + static_cast<size_t>(sw_len) + static_cast<size_t>(build_len);
      if (expected_len != len) {
        return false;
      }
      if (sw_len > 0U) {
        meta.sw_version.assign(reinterpret_cast<const char*>(payload + 16U),
                               reinterpret_cast<const char*>(payload + 16U + sw_len));
      }
      if (build_len > 0U) {
        const size_t off = 16U + static_cast<size_t>(sw_len);
        meta.build_id.assign(reinterpret_cast<const char*>(payload + off),
                             reinterpret_cast<const char*>(payload + off + build_len));
      }
    } else if (meta_version == kFirmwareMetaVersionV2) {
      if (len < (12U + kFirmwareMetaHeaderSizeV2)) {
        return false;
      }
      const uint8_t role_len = payload[16];
      if (role_len > 15U) {
        return false;
      }
      const size_t expected_len = 12U + kFirmwareMetaHeaderSizeV2 + static_cast<size_t>(sw_len) +
                                  static_cast<size_t>(build_len) + static_cast<size_t>(role_len);
      if (expected_len != len) {
        return false;
      }
      size_t off = 17U;
      if (sw_len > 0U) {
        meta.sw_version.assign(reinterpret_cast<const char*>(payload + off),
                               reinterpret_cast<const char*>(payload + off + sw_len));
        off += static_cast<size_t>(sw_len);
      }
      if (build_len > 0U) {
        meta.build_id.assign(reinterpret_cast<const char*>(payload + off),
                             reinterpret_cast<const char*>(payload + off + build_len));
        off += static_cast<size_t>(build_len);
      }
      if (role_len > 0U) {
        meta.target_role.assign(reinterpret_cast<const char*>(payload + off),
                                reinterpret_cast<const char*>(payload + off + role_len));
      }
    } else {
      return false;
    }
  }
  const bool ok = firmware_sink_->begin(from, corr_id, total_size, chunk_size, crc, meta);
  if (config_.local_role == Role::Slave) {
    ota_finalize_pending_ = false;
    ota_finalize_corr_id_ = 0U;
    ota_finalize_offset_ = 0U;
    ota_finalize_status_code_ = 0U;
    ota_finalize_kind_ = 0U;
    ota_finalize_started_ms_ = 0U;
    ota_finalize_last_tx_ms_ = 0U;
    if (ok) {
      ota_transfer_corr_id_ = corr_id;
      ota_transfer_expected_size_ = total_size;
      ota_transfer_last_received_offset_ = 0U;
      ota_transfer_last_acked_offset_ = 0U;
      ota_transfer_ack_step_bytes_ = std::max<uint32_t>(1024U, chunk_size * 16U);
      ota_transfer_next_ack_offset_ = ota_transfer_ack_step_bytes_;
      ota_transfer_last_nack_offset_ = 0U;
      ota_transfer_last_nack_ms_ = 0U;
      (void)sendFirmwareStatus(from,
                               corr_id,
                               kFirmwareStatusKindChunkAck,
                               0U,
                               static_cast<uint16_t>(OtaStatusCode::Ok));
    } else {
      ota_transfer_expected_size_ = 0U;
      ota_transfer_last_acked_offset_ = 0U;
      (void)sendFirmwareStatus(from,
                               corr_id,
                               kFirmwareStatusKindChunkNack,
                               0U,
                               static_cast<uint16_t>(OtaStatusCode::InvalidState));
    }
  }
  return ok;
}

bool EspNowManager::handleFirmwareChunk(const MacAddress& from,
                                        uint32_t corr_id,
                                        const uint8_t* payload,
                                        size_t len) {
  if (firmware_sink_ == nullptr || payload == nullptr || len < 4) {
    return false;
  }

  const uint32_t offset = readLe32(payload);
  const uint8_t* chunk = payload + 4;
  const size_t chunk_len = len - 4;
  const bool track_ota = (config_.local_role == Role::Slave &&
                          ota_transfer_corr_id_ == corr_id);

  const bool ok = firmware_sink_->writeChunk(from, corr_id, offset, chunk, chunk_len);
  if (track_ota) {
    uint32_t expected_offset = ota_transfer_last_received_offset_;
    const uint32_t sink_contiguous = firmware_sink_->contiguousReceiveSize();
    if (sink_contiguous >= expected_offset) {
      expected_offset = sink_contiguous;
    }

    if (ok) {
      ota_transfer_last_received_offset_ = expected_offset;
      const bool tail_complete = (ota_transfer_expected_size_ != 0U &&
                                  expected_offset == ota_transfer_expected_size_);
      const bool threshold_reached = (expected_offset >= ota_transfer_next_ack_offset_);
      const bool should_ack = (threshold_reached || tail_complete || expected_offset == 0U);
      if (should_ack && expected_offset != ota_transfer_last_acked_offset_) {
        (void)sendFirmwareStatus(from,
                                 corr_id,
                                 kFirmwareStatusKindChunkAck,
                                 expected_offset,
                                 static_cast<uint16_t>(OtaStatusCode::Ok));
        ota_transfer_last_acked_offset_ = expected_offset;
        while (ota_transfer_ack_step_bytes_ != 0U &&
               ota_transfer_next_ack_offset_ <= expected_offset) {
          ota_transfer_next_ack_offset_ += ota_transfer_ack_step_bytes_;
        }
      }
    } else {
      const uint32_t now_ms = current_now_ms_;
      ota_transfer_last_received_offset_ = expected_offset;
      const bool nack_same_offset = (expected_offset == ota_transfer_last_nack_offset_);
      const bool nack_recent = nack_same_offset &&
                               static_cast<int32_t>(now_ms - ota_transfer_last_nack_ms_) < 60;
      if (!nack_recent) {
        (void)sendFirmwareStatus(from,
                                 corr_id,
                                 kFirmwareStatusKindChunkNack,
                                 expected_offset,
                                 static_cast<uint16_t>(OtaStatusCode::OffsetMismatch));
        ota_transfer_last_nack_offset_ = expected_offset;
        ota_transfer_last_nack_ms_ = now_ms;
      }
    }
  }
  return ok;
}

bool EspNowManager::handleFirmwareEnd(const MacAddress& from,
                                      uint32_t corr_id,
                                      const uint8_t* payload,
                                      size_t len) {
  if (firmware_sink_ == nullptr || payload == nullptr || len != 8) {
    return false;
  }

  const uint32_t total_size = readLe32(payload + 0);
  const uint32_t crc = readLe32(payload + 4);
  const bool ok = firmware_sink_->end(from, corr_id, total_size, crc);
  if (config_.local_role == Role::Slave) {
    const uint8_t finalize_kind = ok ? kFirmwareStatusKindFinalizeOk : kFirmwareStatusKindFinalizeFail;
    const uint32_t finalize_offset = ok ? total_size : ota_transfer_last_received_offset_;
    uint16_t finalize_code = static_cast<uint16_t>(OtaStatusCode::Ok);
    if (!ok) {
      finalize_code = firmware_sink_->lastOtaStatusCode();
      if (finalize_code == 0U) {
        finalize_code = static_cast<uint16_t>(OtaStatusCode::InvalidState);
      }
    }

    ota_finalize_pending_ = true;
    ota_finalize_peer_ = from;
    ota_finalize_corr_id_ = corr_id;
    ota_finalize_offset_ = finalize_offset;
    ota_finalize_status_code_ = finalize_code;
    ota_finalize_kind_ = finalize_kind;
    ota_finalize_started_ms_ = current_now_ms_;
    ota_finalize_last_tx_ms_ = 0U;

    if (sendFirmwareStatus(from, corr_id, finalize_kind, finalize_offset, finalize_code)) {
      ota_finalize_last_tx_ms_ = current_now_ms_;
    }

    if (ok) {
      uint32_t epoch_s = 0U;
      uint64_t now_epoch = 0U;
      if (time_source_ != nullptr && time_source_->nowEpochSec(now_epoch)) {
        epoch_s = static_cast<uint32_t>(now_epoch & 0xFFFFFFFFULL);
      }
      (void)publishMandatoryEvent(kOtaTransferReadyEventId,
                                  1U,
                                  static_cast<int32_t>(corr_id & 0x7FFFFFFFU),
                                  epoch_s);
    }
    if (ota_transfer_corr_id_ == corr_id) {
      ota_transfer_corr_id_ = 0U;
      ota_transfer_last_received_offset_ = 0U;
      ota_transfer_expected_size_ = 0U;
      ota_transfer_last_acked_offset_ = 0U;
      ota_transfer_ack_step_bytes_ = 0U;
      ota_transfer_next_ack_offset_ = 0U;
      ota_transfer_last_nack_offset_ = 0U;
      ota_transfer_last_nack_ms_ = 0U;
    }
  }
  return ok;
}

bool EspNowManager::handleFirmwareStatus(const MacAddress& from,
                                         uint32_t corr_id,
                                         const uint8_t* payload,
                                         size_t len) {
  (void)corr_id;
  if (payload == nullptr || len < 11U) {
    return false;
  }

  uint8_t kind = 0U;
  uint32_t transfer_corr_id = 0U;
  uint32_t offset_bytes = 0U;
  uint16_t status_code = 0U;
  if (!parseFirmwareStatusPayload(payload, len, kind, transfer_corr_id, offset_bytes, status_code)) {
    return false;
  }

  if (config_.local_role == Role::Slave) {
    if (kind == kFirmwareStatusKindFinalizeAck &&
        ota_finalize_pending_ &&
        ota_finalize_corr_id_ == transfer_corr_id &&
        ota_finalize_peer_ == from) {
      ota_finalize_pending_ = false;
      ota_finalize_corr_id_ = 0U;
      ota_finalize_offset_ = 0U;
      ota_finalize_status_code_ = 0U;
      ota_finalize_kind_ = 0U;
      ota_finalize_started_ms_ = 0U;
      ota_finalize_last_tx_ms_ = 0U;
    }
    return true;
  }

  if (config_.local_role != Role::Master) {
    return true;
  }

  if (events_ != nullptr) {
    Event e{};
    e.type = Event::Type::OtaTransferStatus;
    e.peer = from;
    e.correlation_id = transfer_corr_id;
    e.event_id = kind;
    e.event_value = static_cast<int32_t>(offset_bytes & 0x7FFFFFFFU);
    e.event_ts_s = static_cast<uint32_t>(status_code);
    e.message = "ota transfer status";
    events_->onEvent(e);
  }
  return true;
}

}  // namespace espnow_link













































































