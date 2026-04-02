#include "espnow_link/management_service.hpp"

#include "espnow_link/management_utils.hpp"

namespace espnow_link {

namespace {

constexpr uint32_t kMasterUpdateLiveGuardMs = 120000U;

bool isPushMutation(ManagementCommandId cmd) {
  return cmd == ManagementCommandId::PushStart ||
         cmd == ManagementCommandId::PushUpdate ||
         cmd == ManagementCommandId::PushPause ||
         cmd == ManagementCommandId::PushResume ||
         cmd == ManagementCommandId::PushStop;
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
}  // namespace
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
      c == ManagementCommandId::NodeBundleGet ||
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
  if (c == ManagementCommandId::NodeBundleGet ||
      c == ManagementCommandId::SettingsGet ||
      c == ManagementCommandId::SettingsPageGet) return 5000;
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



ManagementAccessLevel ManagementService::commandRequiredAccessLevel(uint16_t cmd_id) {
  return requiredAccessLevel(static_cast<ManagementCommandId>(cmd_id));
}


}  // namespace espnow_link




