#include "espnow_link/management_service.hpp"

#include <utility>

#include "espnow_link/ota_types.hpp"

namespace espnow_link {

namespace {

constexpr uint32_t kDefaultDiscoveryWindowMs = 10000;
constexpr uint16_t kLogSourceManagement = 0x0301;
constexpr uint16_t kLogEvtCmdRx = 0x0001;
constexpr uint16_t kLogEvtCmdDone = 0x0002;
constexpr uint16_t kLogEvtCmdFail = 0x0003;
constexpr uint16_t kLogEvtTimeout = 0x0004;
constexpr uint16_t kLogEvtQueueFull = 0x0005;

bool isZeroMacAddress(const MacAddress& mac) {
  for (uint8_t b : mac) {
    if (b != 0U) {
      return false;
    }
  }
  return true;
}

void emitManagementLog(LibraryLogger* logger,
                       LibraryLogLevel level,
                       uint16_t event_id,
                       uint32_t now_ms,
                       const ManagementRequest& request,
                       ManagementStatus status) {
  if (logger == nullptr) {
    return;
  }
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

bool ManagementService::submit(ManagementRequest request) {
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
  p.request = std::move(request);
  p.priority = commandPriority(p.request.cmd_id);
  const uint32_t timeout_ms =
      (p.request.timeout_ms == 0) ? commandTimeoutMs(p.request.cmd_id) : p.request.timeout_ms;
  p.deadline_ms = now_ms_ + timeout_ms;
  auto insert_it = request_queue_.end();
  for (auto it = request_queue_.begin(); it != request_queue_.end(); ++it) {
    if (p.priority < it->priority) {
      insert_it = it;
      break;
    }
  }
  const auto queued_it = request_queue_.insert(insert_it, std::move(p));
  emitServiceEvent(ManagementEventId::CmdRx, queued_it->request, ManagementStatus::Ok);
  emitManagementLog(manager_.logger(),
                    LibraryLogLevel::Info,
                    kLogEvtCmdRx,
                    now_ms_,
                    queued_it->request,
                    ManagementStatus::Ok);
  return true;
}

void ManagementService::tick(uint32_t now_ms) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  now_ms_ = now_ms;
  prunePendingDescriptorPulls_();
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
  PendingRequest p = std::move(request_queue_.front());
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

  const bool async_terminal =
      isAsyncTerminalCommand(p.request.cmd_id) ||
      hasPendingDescriptorPullRequest_(p.request.source, p.request.req_id);
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
  pending_descriptor_pulls_.clear();
  deferred_topology_commits_.clear();
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

bool ManagementService::topologyBusy() const {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  const auto is_topology_cmd = [](uint16_t cmd_id) {
    const ManagementCommandId cmd = static_cast<ManagementCommandId>(cmd_id);
    switch (cmd) {
      case ManagementCommandId::TopologyStageSet:
      case ManagementCommandId::TopologyCommit:
      case ManagementCommandId::TopologyStatusGet:
      case ManagementCommandId::TopologySlotsGet:
      case ManagementCommandId::TopologyTriggerSend:
        return true;
      default:
        return false;
    }
  };

  for (const auto& pending : request_queue_) {
    if (is_topology_cmd(pending.request.cmd_id)) {
      return true;
    }
  }
  for (const auto& pending : pending_descriptor_pulls_) {
    if (is_topology_cmd(pending.cmd_id)) {
      return true;
    }
  }
  if (!deferred_topology_commits_.empty()) {
    return true;
  }
  return false;
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
    PendingRequest pending = std::move(request_queue_.front());
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


}  // namespace espnow_link

