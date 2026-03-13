#include "espnow_link/management_controller.hpp"

#include "espnow_link/management_utils.hpp"

namespace espnow_link {

ManagementController::SubmitResult ManagementController::submit(
    uint16_t cmd_id,
    const std::vector<uint8_t>& payload,
    const SubmitOptions& options) {
  SubmitResult result{};
  result.cmd_id = cmd_id;
  result.reject_stage = "bind";

  if (queue_transport_ == nullptr) {
    result.status = ManagementStatus::DeniedByPolicy;
    return result;
  }

  const uint32_t used_req_id = (options.req_id == 0U) ? next_req_id_++ : options.req_id;
  result.req_id = used_req_id;

  ManagementRequest request{};
  request.source = source_;
  request.access_level = access_level_;
  request.cmd_id = cmd_id;
  request.req_id = used_req_id;
  request.timeout_ms = (options.timeout_ms == 0U) ? default_timeout_ms_ : options.timeout_ms;

  // Priority is reserved for future request-contract expansion.
  (void)options.priority;

  if (options.has_target_peer) {
    request.has_target_peer = true;
    request.target_peer = options.target_peer;
  }

  request.payload = payload;
  result.reject_stage = "queue";
  result.accepted = queue_transport_->enqueueRequest(request);
  result.status = result.accepted ? ManagementStatus::Ok : ManagementStatus::QueueFull;
  return result;
}

bool ManagementController::submitCommand_(ManagementCommandId cmd,
                                          const std::vector<uint8_t>& payload,
                                          uint32_t* out_req_id,
                                          uint32_t timeout_ms) {
  SubmitOptions options{};
  options.timeout_ms = timeout_ms;
  const SubmitResult result = submit(static_cast<uint16_t>(cmd), payload, options);
  if (out_req_id != nullptr) {
    *out_req_id = result.req_id;
  }
  return result.accepted;
}

bool ManagementController::discoveryStart(uint32_t window_ms, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::DiscoveryStart,
                        management_utils::buildDiscoveryStartPayload(window_ms),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::discoveryStop(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::DiscoveryStop, {}, out_req_id, timeout_ms);
}

bool ManagementController::discoverySnapshotGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::DiscoverySnapshotGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::pairedSnapshotGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PairedSnapshotGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::statusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::StatusGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::topologyStageSet(const ManagementTopologySnapshotPayload& snapshot,
                                            uint32_t* out_req_id,
                                            uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TopologyStageSet,
                        management_utils::buildTopologyStagePayload(snapshot),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::topologyCommit(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TopologyCommit, {}, out_req_id, timeout_ms);
}

bool ManagementController::topologyStatusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TopologyStatusGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::topologySlotsGet(bool committed, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TopologySlotsGet,
                        management_utils::buildTopologySlotsGetPayload(committed),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::topologyTriggerSend(const ManagementTopologyTriggerSendPayload& trigger,
                                               uint32_t* out_req_id,
                                               uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TopologyTriggerSend,
                        management_utils::buildTopologyTriggerSendPayload(trigger),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::pairRequest(const MacAddress& peer, uint32_t* out_req_id, uint32_t timeout_ms) {
  std::vector<uint8_t> payload(peer.begin(), peer.end());
  return submitCommand_(ManagementCommandId::PairRequest, payload, out_req_id, timeout_ms);
}

bool ManagementController::unpairRequest(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::UnpairRequest, {}, out_req_id, timeout_ms);
}

bool ManagementController::removePeerRequest(const MacAddress& peer, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::RemovePeerRequest,
                        management_utils::buildMacPayload(peer),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::descGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::DescGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::capsGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::CapsGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::capsPageGet(uint16_t cursor,
                                       uint8_t page_size,
                                       uint32_t* out_req_id,
                                       uint32_t timeout_ms) {
  std::vector<uint8_t> payload;
  management_utils::appendU16Le(payload, cursor);
  payload.push_back(page_size);
  return submitCommand_(ManagementCommandId::CapsPageGet, payload, out_req_id, timeout_ms);
}

bool ManagementController::settingsGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::SettingsGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::settingsPageGet(uint16_t cursor,
                                           uint8_t page_size,
                                           uint32_t* out_req_id,
                                           uint32_t timeout_ms) {
  std::vector<uint8_t> payload;
  management_utils::appendU16Le(payload, cursor);
  payload.push_back(page_size);
  return submitCommand_(ManagementCommandId::SettingsPageGet, payload, out_req_id, timeout_ms);
}

bool ManagementController::settingGetByKey(const std::string& key, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::SettingGet,
                        management_utils::buildSettingGetByKeyPayload(key),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::settingGetById(uint16_t setting_id, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::SettingGet,
                        management_utils::buildSettingGetByIdPayload(setting_id),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::settingSetByKey(const std::string& key,
                                           const std::string& value,
                                           uint32_t* out_req_id,
                                           uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::SettingSet,
                        management_utils::buildSettingSetByKeyPayload(key, value),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::settingSetById(uint16_t setting_id,
                                          const std::string& value,
                                          uint32_t* out_req_id,
                                          uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::SettingSet,
                        management_utils::buildSettingSetByIdPayload(setting_id, value),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::telemetrySchemaGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TelemSchemaGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::telemetrySchemaPageGet(uint16_t cursor,
                                                  uint8_t page_size,
                                                  uint32_t* out_req_id,
                                                  uint32_t timeout_ms) {
  std::vector<uint8_t> payload;
  management_utils::appendU16Le(payload, cursor);
  payload.push_back(page_size);
  return submitCommand_(ManagementCommandId::TelemSchemaPageGet, payload, out_req_id, timeout_ms);
}

bool ManagementController::telemetryPull(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TelemPull, {}, out_req_id, timeout_ms);
}

bool ManagementController::livenessGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LiveGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::liveMonitorEnable(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LiveMonitorEnable, {}, out_req_id, timeout_ms);
}

bool ManagementController::liveMonitorDisable(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LiveMonitorDisable, {}, out_req_id, timeout_ms);
}

bool ManagementController::liveMonitorStatusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LiveMonitorStatusGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::pingGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PingGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::timeGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TimeGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::timeSet(uint64_t epoch_s, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::TimeSet,
                        management_utils::buildTimeSetPayload(epoch_s),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::pushCommand(const TelemetryPushCommand& cmd, uint32_t* out_req_id, uint32_t timeout_ms) {
  std::vector<uint8_t> payload;
  if (!encodeTelemetryPushCommand(cmd, payload)) return false;

  ManagementCommandId cmd_id = ManagementCommandId::PushGet;
  switch (cmd.action) {
    case TelemetryPushAction::Start:
      cmd_id = ManagementCommandId::PushStart;
      break;
    case TelemetryPushAction::Update:
      cmd_id = ManagementCommandId::PushUpdate;
      break;
    case TelemetryPushAction::Pause:
      cmd_id = ManagementCommandId::PushPause;
      break;
    case TelemetryPushAction::Resume:
      cmd_id = ManagementCommandId::PushResume;
      break;
    case TelemetryPushAction::Stop:
      cmd_id = ManagementCommandId::PushStop;
      break;
    case TelemetryPushAction::Get:
    default:
      cmd_id = ManagementCommandId::PushGet;
      break;
  }
  return submitCommand_(cmd_id, payload, out_req_id, timeout_ms);
}

bool ManagementController::pushStart(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PushStart, {}, out_req_id, timeout_ms);
}

bool ManagementController::pushUpdate(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PushUpdate, {}, out_req_id, timeout_ms);
}

bool ManagementController::pushPause(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PushPause, {}, out_req_id, timeout_ms);
}

bool ManagementController::pushResume(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PushResume, {}, out_req_id, timeout_ms);
}

bool ManagementController::pushStop(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PushStop, {}, out_req_id, timeout_ms);
}

bool ManagementController::pushGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::PushGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::restartSlaveRequest(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::RestartSlaveRequest, {}, out_req_id, timeout_ms);
}

bool ManagementController::resetSlaveRequest(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::ResetSlaveRequest, {}, out_req_id, timeout_ms);
}

bool ManagementController::audioPingRequest(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::AudioPingRequest, {}, out_req_id, timeout_ms);
}

bool ManagementController::restartMasterRequest(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::RestartMasterRequest, {}, out_req_id, timeout_ms);
}

bool ManagementController::resetMasterRequest(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::ResetMasterRequest, {}, out_req_id, timeout_ms);
}

bool ManagementController::cliSetEnabled(bool enabled, uint32_t* out_req_id, uint32_t timeout_ms) {
  std::vector<uint8_t> payload;
  payload.push_back(enabled ? 1U : 0U);
  return submitCommand_(ManagementCommandId::CliControlSet, payload, out_req_id, timeout_ms);
}

bool ManagementController::cliStatusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::CliControlSet, {}, out_req_id, timeout_ms);
}

bool ManagementController::chainLoopSetEnabled(bool enabled, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::ChainLoopControlSet,
                        management_utils::buildChainLoopControlPayload(enabled),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::chainLoopStatusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::ChainLoopControlSet, {}, out_req_id, timeout_ms);
}

bool ManagementController::logLocalStatusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogLocalStatusGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::logLocalRead(uint32_t offset,
                                        uint16_t max_bytes,
                                        uint32_t* out_req_id,
                                        uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogLocalRead,
                        management_utils::buildLogReadPayload(offset, max_bytes),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::logLocalClear(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogLocalClear, {}, out_req_id, timeout_ms);
}

bool ManagementController::logLocalSetEnabled(bool enabled, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogLocalControlSet,
                        management_utils::buildLogControlPayload(enabled),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::logRemoteStatusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogRemoteStatusGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::logRemoteRead(uint32_t offset,
                                         uint16_t max_bytes,
                                         uint32_t* out_req_id,
                                         uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogRemoteRead,
                        management_utils::buildLogReadPayload(offset, max_bytes),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::logRemoteClear(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogRemoteClear, {}, out_req_id, timeout_ms);
}

bool ManagementController::logRemoteSetEnabled(bool enabled, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::LogRemoteControlSet,
                        management_utils::buildLogControlPayload(enabled),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::channelRuntimeGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::ChannelRuntimeGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::channelSyncAll(uint8_t channel, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::ChannelSyncAll,
                        management_utils::buildChannelSyncAllPayload(channel),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::storageInfoGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::StorageInfoGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::storageList(const std::string& path, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::StorageList,
                        management_utils::buildStringPayloadU16(path),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::storageStat(const std::string& path, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::StorageStat,
                        management_utils::buildStringPayloadU16(path),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::storageFormat(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::StorageFormat, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaStatusGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaStatusGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaManifestGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaManifestGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaManifestPageGet(uint16_t cursor,
                                              uint8_t page_size,
                                              uint32_t* out_req_id,
                                              uint32_t timeout_ms) {
  std::vector<uint8_t> payload;
  management_utils::appendU16Le(payload, cursor);
  payload.push_back(page_size);
  return submitCommand_(ManagementCommandId::OtaManifestPageGet, payload, out_req_id, timeout_ms);
}

bool ManagementController::otaManifestRebuild(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaManifestRebuild, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaClearScope(const std::string& scope, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaClearScope,
                        management_utils::buildStringPayloadU16(scope),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaCapacityGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaCapacityGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaGateGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaGateGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaApply(const std::string& target, uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaApply,
                        management_utils::buildStringPayloadU16(target),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaRollback(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaRollback, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaTransferBegin(uint32_t total_size,
                                            uint32_t chunk_size,
                                            uint32_t image_crc32,
                                            const FirmwareImageMetadata* metadata,
                                            uint32_t* out_req_id,
                                            uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaTransferBegin,
                        management_utils::buildOtaTransferBeginPayload(total_size,
                                                                       chunk_size,
                                                                       image_crc32,
                                                                       metadata),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaTransferChunk(uint32_t offset,
                                            const uint8_t* data,
                                            size_t len,
                                            uint32_t* out_req_id,
                                            uint32_t timeout_ms) {
  const std::vector<uint8_t> payload = management_utils::buildOtaTransferChunkPayload(offset, data, len);
  if (payload.empty()) return false;
  return submitCommand_(ManagementCommandId::OtaTransferChunk, payload, out_req_id, timeout_ms);
}

bool ManagementController::otaTransferChunk(uint32_t offset,
                                            const std::vector<uint8_t>& data,
                                            uint32_t* out_req_id,
                                            uint32_t timeout_ms) {
  const std::vector<uint8_t> payload = management_utils::buildOtaTransferChunkPayload(offset, data);
  if (payload.empty()) return false;
  return submitCommand_(ManagementCommandId::OtaTransferChunk, payload, out_req_id, timeout_ms);
}

bool ManagementController::otaTransferEnd(uint32_t total_size,
                                          uint32_t image_crc32,
                                          uint32_t* out_req_id,
                                          uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaTransferEnd,
                        management_utils::buildOtaTransferEndPayload(total_size, image_crc32),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaTransferAbort(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaTransferAbort, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaPushStart(const std::string& local_path,
                                        uint16_t chunk_bytes,
                                        uint32_t* out_req_id,
                                        uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaPushStart,
                        management_utils::buildOtaPushStartPayload(local_path, chunk_bytes),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaPushAbort(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaPushAbort, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaPushStatus(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaPushStatus, {}, out_req_id, timeout_ms);
}

bool ManagementController::otaUpdateStart(const std::string& local_path,
                                          uint16_t chunk_bytes,
                                          uint32_t* out_req_id,
                                          uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaUpdateStart,
                        management_utils::buildOtaPushStartPayload(local_path, chunk_bytes),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaArchiveList(char role,
                                          uint32_t* out_req_id,
                                          uint32_t timeout_ms,
                                          bool remote) {
  return submitCommand_(ManagementCommandId::OtaArchiveList,
                        management_utils::buildOtaArchivePayload(role, {}, remote),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaArchiveSaveRunning(char role,
                                                 uint32_t* out_req_id,
                                                 uint32_t timeout_ms,
                                                 bool remote) {
  return submitCommand_(ManagementCommandId::OtaArchiveSaveRunning,
                        management_utils::buildOtaArchivePayload(role, {}, remote),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaArchiveSaveStaged(char role,
                                                uint32_t* out_req_id,
                                                uint32_t timeout_ms,
                                                bool remote) {
  return submitCommand_(ManagementCommandId::OtaArchiveSaveStaged,
                        management_utils::buildOtaArchivePayload(role, {}, remote),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaArchiveRestore(const std::string& id,
                                             char role,
                                             uint32_t* out_req_id,
                                             uint32_t timeout_ms,
                                             bool remote) {
  return submitCommand_(ManagementCommandId::OtaArchiveRestore,
                        management_utils::buildOtaArchivePayload(role, id, remote),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaArchiveDelete(const std::string& id,
                                            char role,
                                            uint32_t* out_req_id,
                                            uint32_t timeout_ms,
                                            bool remote) {
  return submitCommand_(ManagementCommandId::OtaArchiveDelete,
                        management_utils::buildOtaArchivePayload(role, id, remote),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaArchiveClear(char role,
                                           uint32_t* out_req_id,
                                           uint32_t timeout_ms,
                                           bool remote) {
  return submitCommand_(ManagementCommandId::OtaArchiveClear,
                        management_utils::buildOtaArchivePayload(role, {}, remote),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaArchiveVerify(const std::string& id,
                                            char role,
                                            uint32_t* out_req_id,
                                            uint32_t timeout_ms,
                                            bool remote) {
  return submitCommand_(ManagementCommandId::OtaArchiveVerify,
                        management_utils::buildOtaArchivePayload(role, id, remote),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::otaUpdateMasterStart(const std::string& local_path,
                                                uint32_t* out_req_id,
                                                uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::OtaMasterUpdateStart,
                        management_utils::buildOtaMasterUpdateStartPayload(local_path),
                        out_req_id,
                        timeout_ms);
}

bool ManagementController::commTestRun(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::CommTestRun, {}, out_req_id, timeout_ms);
}

bool ManagementController::commTestStatus(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::CommTestStatus, {}, out_req_id, timeout_ms);
}

bool ManagementController::commTestReport(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::CommTestReport, {}, out_req_id, timeout_ms);
}

bool ManagementController::metricsGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::MetricsGet, {}, out_req_id, timeout_ms);
}

bool ManagementController::metricsReset(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::MetricsReset, {}, out_req_id, timeout_ms);
}

bool ManagementController::queueGet(uint32_t* out_req_id, uint32_t timeout_ms) {
  return submitCommand_(ManagementCommandId::QueueGet, {}, out_req_id, timeout_ms);
}

}  // namespace espnow_link
