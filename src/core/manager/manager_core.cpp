/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Core manager construction, codec/profile setup, and runtime/logger primitives.
 **************************************************************/
#include "../internal/manager_internal.hpp"

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
  tx_wrapped_payload_scratch_.reserve(ProtocolCodec::kMaxPayload);
  tx_encoded_frame_scratch_.reserve(ProtocolCodec::kMaxFrameBytes);
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

void EspNowManager::emitTopologyRuntimeLog_(LibraryLogLevel level,
                                            uint16_t event_id,
                                            int32_t p1,
                                            int32_t p2,
                                            const char* reason) {
  const uint8_t* ext = nullptr;
  size_t ext_len = 0U;
  if (reason != nullptr && reason[0] != '\0') {
    ext = reinterpret_cast<const uint8_t*>(reason);
    ext_len = std::strlen(reason);
  }
  emitRuntimeLog(level, event_id, 0U, p1, p2, ext, ext_len);
}

void EspNowManager::setTopologyRestoreStatus_(uint8_t mode, const char* reason) {
  topology_restore_mode_ = mode;
  topology_restore_reason_.fill('\0');
  if (reason != nullptr && reason[0] != '\0') {
    std::snprintf(topology_restore_reason_.data(), topology_restore_reason_.size(), "%s", reason);
  }
}

void EspNowManager::noteTopologyPeerReadyFailure_(const char* reason) {
  ++topology_peer_ready_fail_count_;
  const char* safe_reason = (reason != nullptr && reason[0] != '\0') ? reason : "peer_not_ready";
  ++topology_peer_ready_fail_by_reason_[safe_reason];
  topology_peer_ready_last_reason_.fill('\0');
  std::snprintf(topology_peer_ready_last_reason_.data(), topology_peer_ready_last_reason_.size(), "%s", safe_reason);
}

}  // namespace espnow_link

