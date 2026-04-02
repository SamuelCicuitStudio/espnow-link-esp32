
  static bool isTerminalState_(CommandState state) {
    return state == CommandState::Done || state == CommandState::Fail || state == CommandState::Timeout;
  }

  static bool isRetryableStatus_(ManagementStatus status) {
    return status == ManagementStatus::Timeout ||
           status == ManagementStatus::QueueFull ||
           status == ManagementStatus::BusyPairing ||
           status == ManagementStatus::BusyRadioTransition;
  }

  static ManagementAccessLevel requiredAccessForCmd_(uint16_t cmd_id) {
    return ManagementService::commandRequiredAccessLevel(cmd_id);
  }

  static bool isTopologyControlCommand_(uint16_t cmd_id) {
    const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
    switch (c) {
      case ManagementCommandId::TopologyStageSet:
      case ManagementCommandId::TopologyCommit:
      case ManagementCommandId::TopologyStatusGet:
      case ManagementCommandId::TopologySlotsGet:
      case ManagementCommandId::TopologyTriggerSend:
      case ManagementCommandId::ChannelRuntimeGet:
      case ManagementCommandId::ChannelSyncAll:
      case ManagementCommandId::ChainLoopControlSet:
        return true;
      default:
        return false;
    }
  }

  static bool isOtaControlCommand_(uint16_t cmd_id) {
    const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
    switch (c) {
      case ManagementCommandId::OtaStatusGet:
      case ManagementCommandId::OtaManifestGet:
      case ManagementCommandId::OtaManifestPageGet:
      case ManagementCommandId::OtaManifestRebuild:
      case ManagementCommandId::OtaClearScope:
      case ManagementCommandId::OtaCapacityGet:
      case ManagementCommandId::OtaGateGet:
      case ManagementCommandId::OtaApply:
      case ManagementCommandId::OtaRollback:
      case ManagementCommandId::OtaTransferBegin:
      case ManagementCommandId::OtaTransferChunk:
      case ManagementCommandId::OtaTransferEnd:
      case ManagementCommandId::OtaTransferAbort:
      case ManagementCommandId::OtaPushStart:
      case ManagementCommandId::OtaPushAbort:
      case ManagementCommandId::OtaPushStatus:
      case ManagementCommandId::OtaUpdateStart:
      case ManagementCommandId::OtaArchiveList:
      case ManagementCommandId::OtaArchiveSaveRunning:
      case ManagementCommandId::OtaArchiveSaveStaged:
      case ManagementCommandId::OtaArchiveRestore:
      case ManagementCommandId::OtaArchiveDelete:
      case ManagementCommandId::OtaArchiveClear:
      case ManagementCommandId::OtaMasterUpdateStart:
      case ManagementCommandId::OtaArchiveVerify:
        return true;
      default:
        return false;
    }
  }

  enum class MutationLaneDomain : uint8_t {
    None = 0,
    Pairing = 1,
    SettingsTime = 2,
    Push = 3,
    Topology = 4,
    Ota = 5,
    LoggerStorage = 6,
    RestartReset = 7,
  };

  struct MutationLaneClaim {
    uint32_t req_id = 0U;
    MutationLaneDomain domain = MutationLaneDomain::None;
    bool has_peer = false;
    MacAddress peer{};
  };

  struct ChildPushPeerState {
    MacAddress peer{};
    uint8_t semu_mask = 0U;
    TelemetryPushMode semu_mode = TelemetryPushMode::Hybrid;
    uint32_t semu_interval_ms = 2000U;
    float semu_delta_abs = 0.10f;
    uint32_t semu_gap_ms = 200U;
    uint16_t remu_mask = 0U;
    TelemetryPushMode remu_mode = TelemetryPushMode::Hybrid;
    uint32_t remu_interval_ms = 2000U;
    float remu_delta_abs = 0.10f;
    uint32_t remu_gap_ms = 200U;
  };

  static MutationLaneDomain mutationLaneDomainForCmd_(uint16_t cmd_id) {
    const ManagementCommandId c = static_cast<ManagementCommandId>(cmd_id);
    switch (c) {
      case ManagementCommandId::PairRequest:
      case ManagementCommandId::UnpairRequest:
      case ManagementCommandId::RemovePeerRequest:
        return MutationLaneDomain::Pairing;
      case ManagementCommandId::SettingSet:
      case ManagementCommandId::TimeSet:
        return MutationLaneDomain::SettingsTime;
      case ManagementCommandId::PushStart:
      case ManagementCommandId::PushUpdate:
      case ManagementCommandId::PushPause:
      case ManagementCommandId::PushResume:
      case ManagementCommandId::PushStop:
      case ManagementCommandId::LiveMonitorEnable:
      case ManagementCommandId::LiveMonitorDisable:
        return MutationLaneDomain::Push;
      case ManagementCommandId::TopologyStageSet:
      case ManagementCommandId::TopologyCommit:
      case ManagementCommandId::TopologyTriggerSend:
      case ManagementCommandId::ChannelSyncAll:
      case ManagementCommandId::ChainLoopControlSet:
        return MutationLaneDomain::Topology;
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
        return MutationLaneDomain::Ota;
      case ManagementCommandId::LogLocalClear:
      case ManagementCommandId::LogLocalControlSet:
      case ManagementCommandId::LogRemoteClear:
      case ManagementCommandId::LogRemoteControlSet:
      case ManagementCommandId::StorageFormat:
        return MutationLaneDomain::LoggerStorage;
      case ManagementCommandId::RestartSlaveRequest:
      case ManagementCommandId::ResetSlaveRequest:
      case ManagementCommandId::RestartMasterRequest:
      case ManagementCommandId::ResetMasterRequest:
        return MutationLaneDomain::RestartReset;
      default:
        return MutationLaneDomain::None;
    }
  }

  void resolveSubmitTarget_(const ManagementController::SubmitOptions& options,
                            bool& out_has_peer,
                            MacAddress& out_peer) const {
    out_has_peer = false;
    out_peer = {};
    if (options.has_target_peer) {
      out_has_peer = true;
      out_peer = options.target_peer;
    }
  }

  void pruneMutationLaneClaims_() {
    mutation_lane_claims_.erase(std::remove_if(mutation_lane_claims_.begin(),
                                               mutation_lane_claims_.end(),
                                               [&](const MutationLaneClaim& claim) {
                                                 const CommandStatusView* status = findReqStatus_(claim.req_id);
                                                 return status != nullptr && status->terminal;
                                               }),
                                mutation_lane_claims_.end());
  }

  bool hasMutationLaneConflict_(MutationLaneDomain domain,
                                bool has_peer,
                                const MacAddress& peer,
                                uint32_t* out_blocking_req_id = nullptr) const {
    if (out_blocking_req_id != nullptr) *out_blocking_req_id = 0U;
    for (const auto& claim : mutation_lane_claims_) {
      if (claim.domain != domain) continue;
      if (claim.has_peer != has_peer) continue;
      if (has_peer && claim.peer != peer) continue;
      if (out_blocking_req_id != nullptr) {
        *out_blocking_req_id = claim.req_id;
      }
      return true;
    }
    return false;
  }

  void claimMutationLane_(uint32_t req_id,
                          MutationLaneDomain domain,
                          bool has_peer,
                          const MacAddress& peer) {
    if (req_id == 0U || domain == MutationLaneDomain::None) return;
    releaseMutationLaneByReq_(req_id);
    MutationLaneClaim claim{};
    claim.req_id = req_id;
    claim.domain = domain;
    claim.has_peer = has_peer;
    claim.peer = peer;
    mutation_lane_claims_.push_back(claim);
  }

  void releaseMutationLaneByReq_(uint32_t req_id) {
    if (req_id == 0U) return;
    mutation_lane_claims_.erase(std::remove_if(mutation_lane_claims_.begin(),
                                               mutation_lane_claims_.end(),
                                               [&](const MutationLaneClaim& claim) {
                                                 return claim.req_id == req_id;
                                               }),
                                mutation_lane_claims_.end());
  }

  ManagementController::SubmitResult submitTracked_(uint16_t cmd_id,
                                                    const std::vector<uint8_t>& payload,
                                                    const ManagementController::SubmitOptions& options) {
    ManagementController::SubmitResult result{};
    result.cmd_id = cmd_id;
    result.req_id = 0U;
    result.status = ManagementStatus::DeniedByPolicy;
    result.reject_stage = "submit";

    if (!enforceOwnerContext_()) {
      result.reject_stage = "owner";
      return result;
    }
    if (transport_ == nullptr) {
      result.reject_stage = "availability";
      return result;
    }
    if (radio_transition_active_ && isRadioTransitionBlockedCommand_(cmd_id)) {
      result.status = ManagementStatus::BusyRadioTransition;
      result.reject_stage = "submit";
      return result;
    }

    const MutationLaneDomain lane_domain = mutationLaneDomainForCmd_(cmd_id);
    bool lane_has_peer = false;
    MacAddress lane_peer{};
    if (lane_domain != MutationLaneDomain::None) {
      resolveSubmitTarget_(options, lane_has_peer, lane_peer);
      pruneMutationLaneClaims_();
      if (hasMutationLaneConflict_(lane_domain, lane_has_peer, lane_peer)) {
        result.status = ManagementStatus::DeniedByPolicy;
        result.reject_stage = "lane";
        return result;
      }
    }

    result = controller_.submit(cmd_id, payload, options);
    if (!result.accepted) {
      return result;
    }
    trackSubmittedRequest_(result.req_id, cmd_id);
    if (lane_domain != MutationLaneDomain::None) {
      claimMutationLane_(result.req_id, lane_domain, lane_has_peer, lane_peer);
    }
    return result;
  }

  bool submitTargetedCommand_(const MacAddress& peer,
                              ManagementCommandId cmd,
                              const std::vector<uint8_t>& payload,
                              uint32_t* out_req_id,
                              uint32_t timeout_ms) {
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    const ManagementController::SubmitResult submit_result =
        submit(static_cast<uint16_t>(cmd), payload, submit_options);
    if (out_req_id != nullptr) {
      *out_req_id = submit_result.req_id;
    }
    return submit_result.accepted;
  }

  static bool parseU32Ascii_(const std::string& text, uint32_t& out) {
    if (text.empty()) return false;
    char* endp = nullptr;
    const unsigned long v = std::strtoul(text.c_str(), &endp, 10);
    if (endp == nullptr || *endp != '\0') return false;
    out = static_cast<uint32_t>(v);
    return true;
  }

  static bool parseBoolAscii_(const std::string& text, bool& out) {
    if (text == "1" || text == "true" || text == "on" || text == "yes") {
      out = true;
      return true;
    }
    if (text == "0" || text == "false" || text == "off" || text == "no") {
      out = false;
      return true;
    }
    return false;
  }

  static CommandState toCommandState_(OperationState state) {
    switch (state) {
      case OperationState::Queued:
        return CommandState::Queued;
      case OperationState::Running:
        return CommandState::Running;
      case OperationState::Succeeded:
        return CommandState::Done;
      case OperationState::Timeout:
        return CommandState::Timeout;
      case OperationState::Canceled:
      case OperationState::Failed:
      default:
        return CommandState::Fail;
    }
  }

  static const char* operationStateToString_(OperationState state) {
    switch (state) {
      case OperationState::Queued:
        return "queued";
      case OperationState::Running:
        return "running";
      case OperationState::Succeeded:
        return "succeeded";
      case OperationState::Failed:
        return "failed";
      case OperationState::Timeout:
        return "timeout";
      case OperationState::Canceled:
        return "canceled";
      default:
        return "failed";
    }
  }

  static bool isRadioTransitionBlockedCommand_(uint16_t cmd_id) {
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

  struct RequestEpochEntry {
    uint32_t req_id = 0U;
    uint32_t epoch = 0U;
  };

  void upsertReqEpoch_(uint32_t req_id, uint32_t epoch) {
    if (req_id == 0U) return;
    for (auto& e : req_epoch_map_) {
      if (e.req_id != req_id) continue;
      e.epoch = epoch;
      return;
    }
    RequestEpochEntry add{};
    add.req_id = req_id;
    add.epoch = epoch;
    req_epoch_map_.push_back(add);
    if (req_epoch_map_.size() > 4096U) {
      req_epoch_map_.erase(req_epoch_map_.begin(),
                           req_epoch_map_.begin() +
                               static_cast<std::vector<RequestEpochEntry>::difference_type>(
                                   req_epoch_map_.size() - 2048U));
    }
  }

  bool isStaleTrackedRequest_(uint32_t req_id) const {
    if (req_id == 0U) return false;
    for (const auto& e : req_epoch_map_) {
      if (e.req_id != req_id) continue;
      return e.epoch < radio_epoch_;
    }
    return false;
  }

  void applyRadioTransitionStatusFromService_(const ManagementService::RadioTransitionStatus& svc_status) {
    radio_transition_active_ = svc_status.active;
    radio_transition_state_ = svc_status.state;
    radio_epoch_ = svc_status.radio_epoch;
    radio_transition_last_error_code_ = static_cast<uint16_t>(svc_status.last_error);
    radio_transition_last_error_stage_ = svc_status.last_error_stage;
    radio_transition_last_error_message_ = svc_status.last_error_message;
  }

  static OperationState toOperationState_(CommandState state) {
    switch (state) {
      case CommandState::Queued:
        return OperationState::Queued;
      case CommandState::Running:
        return OperationState::Running;
      case CommandState::Done:
        return OperationState::Succeeded;
      case CommandState::Fail:
        return OperationState::Failed;
      case CommandState::Timeout:
        return OperationState::Timeout;
      default:
        return OperationState::Failed;
    }
  }

  void buildOperationStatus_(const CommandStatusView& in_status, OperationStatus& out_status) const {
    out_status = OperationStatus{};
    out_status.operation_id = in_status.req_id;
    out_status.req_id = in_status.req_id;
    out_status.cmd_id = in_status.cmd_id;
    out_status.state = toOperationState_(in_status.state);
    out_status.terminal = in_status.terminal;
    out_status.status_code = in_status.status;
    out_status.updated_seq = in_status.updated_seq;
    out_status.has_response = in_status.has_response;
    out_status.last_response = in_status.last_response;
    out_status.has_event = in_status.has_event;
    out_status.last_event = in_status.last_event;
    out_status.has_error = in_status.has_error;
    out_status.error = in_status.error;

    if (in_status.has_error) {
      out_status.stage = (in_status.error.stage != nullptr && in_status.error.stage[0] != '\0')
                             ? in_status.error.stage
                             : "unknown";
      out_status.message = in_status.error.message;
    } else if (in_status.has_event) {
      out_status.stage = "event";
      out_status.message = management_utils::managementEventToString(in_status.last_event.event_id);
    } else if (in_status.has_response) {
      out_status.stage = "response";
      out_status.message = management_utils::managementStatusToString(in_status.status);
    } else if (in_status.state == CommandState::Queued) {
      out_status.stage = "submit";
      out_status.message = "queued";
    } else if (in_status.state == CommandState::Running) {
      out_status.stage = "event";
      out_status.message = "running";
    } else {
      out_status.stage = "unknown";
      out_status.message = "";
    }

    if (in_status.has_event) {
      out_status.result_from_event = true;
      out_status.result_payload = in_status.last_event.payload;
    } else if (in_status.has_response) {
      out_status.result_from_event = false;
      out_status.result_payload = in_status.last_response.payload;
    }
  }

  static bool tryDecodeSettingCurrentValue_(const CommandRunResult& run, std::string& out_value) {
    out_value.clear();
    const ManagementResponse* resp = nullptr;
    if (run.has_response) {
      resp = &run.response;
    } else if (run.has_event) {
      // Events for SettingGet are not expected to carry descriptor payload.
      return false;
    } else {
      return false;
    }
    DescriptorResponse d{};
    if (resp->payload.empty() || !decodeDescriptorResponse(resp->payload.data(), resp->payload.size(), d)) {
      return false;
    }
    if (d.type != DescriptorResponseType::Setting) {
      return false;
    }
    out_value = d.setting.current_value;
    return true;
  }

  void trackSubmittedRequest_(uint32_t req_id, uint16_t cmd_id) {
    if (req_id == 0U) return;
    upsertReqEpoch_(req_id, radio_epoch_);
    CommandStatusView* s = findReqStatusMutable_(req_id);
    if (s == nullptr) {
      CommandStatusView add{};
      add.req_id = req_id;
      add.cmd_id = cmd_id;
      add.state = CommandState::Queued;
      add.status = ManagementStatus::Ok;
      add.terminal = false;
      add.updated_seq = ++status_update_seq_;
      req_status_.push_back(add);
      req_status_index_[req_id] = req_status_.size() - 1U;
      trimReqStatus_();
      return;
    }
    s->cmd_id = cmd_id;
    s->state = CommandState::Queued;
    s->status = ManagementStatus::Ok;
    s->terminal = false;
    s->has_error = false;
    s->error = CommandError{};
    s->updated_seq = ++status_update_seq_;
  }

  void trackResponse_(const ManagementResponse& response) {
    if (response.req_id == 0U) return;
    CommandStatusView* s = findReqStatusMutable_(response.req_id);
    if (s == nullptr) {
      CommandStatusView add{};
      add.req_id = response.req_id;
      add.cmd_id = response.cmd_id;
      add.status = response.status;
      add.has_response = true;
      add.last_response = response;
      if (response.status == ManagementStatus::OkDeferred) {
        add.state = CommandState::Running;
        add.terminal = false;
      } else if (response.status == ManagementStatus::Ok) {
        add.state = CommandState::Done;
        add.terminal = true;
      } else if (response.status == ManagementStatus::Timeout) {
        add.state = CommandState::Timeout;
        add.terminal = true;
      } else {
        add.state = CommandState::Fail;
        add.terminal = true;
      }
      if (add.terminal && add.state != CommandState::Done) {
        add.has_error = true;
        add.error.code = static_cast<uint16_t>(response.status);
        add.error.stage = "response";
        add.error.message = management_utils::managementStatusToString(response.status);
        add.error.retryable = isRetryableStatus_(response.status);
      }
      if (add.terminal) {
        releaseMutationLaneByReq_(response.req_id);
      }
      add.updated_seq = ++status_update_seq_;
      req_status_.push_back(add);
      req_status_index_[response.req_id] = req_status_.size() - 1U;
      trimReqStatus_();
      return;
    }

    s->cmd_id = response.cmd_id;
    s->status = response.status;
    s->has_response = true;
    s->last_response = response;
    if (response.status == ManagementStatus::OkDeferred) {
      if (!isTerminalState_(s->state)) s->state = CommandState::Running;
      s->updated_seq = ++status_update_seq_;
      return;
    }
    if (response.status == ManagementStatus::Ok) {
      // Keep running until event terminal when deferred flow is active.
      if (!s->has_event || !isTerminalState_(s->state)) {
        s->state = CommandState::Done;
        s->terminal = true;
      }
      if (s->terminal) {
        releaseMutationLaneByReq_(response.req_id);
      }
      s->updated_seq = ++status_update_seq_;
      return;
    }
    if (response.status == ManagementStatus::Timeout) {
      s->state = CommandState::Timeout;
    } else {
      s->state = CommandState::Fail;
    }
    s->terminal = true;
    s->has_error = true;
    s->error.code = static_cast<uint16_t>(response.status);
    s->error.stage = "response";
    s->error.message = management_utils::managementStatusToString(response.status);
    s->error.retryable = isRetryableStatus_(response.status);
    releaseMutationLaneByReq_(response.req_id);
    s->updated_seq = ++status_update_seq_;
  }

  void trackEvent_(const ManagementEvent& event) {
    pushEventRecord_(event);
    if (event.req_id == 0U) return;
    CommandStatusView* s = findReqStatusMutable_(event.req_id);
    if (s == nullptr) {
      CommandStatusView add{};
      add.req_id = event.req_id;
      add.cmd_id = event.cmd_id;
      add.status = event.status;
      add.has_event = true;
      add.last_event = event;
      if (event.event_id == ManagementEventId::CmdDone) {
        add.state = CommandState::Done;
        add.terminal = true;
      } else if (event.event_id == ManagementEventId::Timeout) {
        add.state = CommandState::Timeout;
        add.terminal = true;
      } else if (event.event_id == ManagementEventId::CmdFail) {
        add.state = CommandState::Fail;
        add.terminal = true;
      } else {
        add.state = CommandState::Running;
        add.terminal = false;
      }
      if (add.terminal && add.state != CommandState::Done) {
        add.has_error = true;
        add.error.code = static_cast<uint16_t>(event.status);
        add.error.stage = "event";
        add.error.message = management_utils::managementEventToString(event.event_id);
        add.error.retryable = (add.state == CommandState::Timeout);
      }
      if (add.terminal) {
        releaseMutationLaneByReq_(event.req_id);
      }
      add.updated_seq = ++status_update_seq_;
      req_status_.push_back(add);
      req_status_index_[event.req_id] = req_status_.size() - 1U;
      trimReqStatus_();
      return;
    }

    s->cmd_id = event.cmd_id;
    s->status = event.status;
    s->has_event = true;
    s->last_event = event;
    if (event.event_id == ManagementEventId::CmdDone) {
      s->state = CommandState::Done;
      s->terminal = true;
      s->has_error = false;
      s->error = CommandError{};
      releaseMutationLaneByReq_(event.req_id);
      s->updated_seq = ++status_update_seq_;
      return;
    }
    if (event.event_id == ManagementEventId::Timeout) {
      s->state = CommandState::Timeout;
      s->terminal = true;
    } else if (event.event_id == ManagementEventId::CmdFail) {
      s->state = CommandState::Fail;
      s->terminal = true;
    } else if (!isTerminalState_(s->state)) {
      s->state = CommandState::Running;
      s->terminal = false;
      s->updated_seq = ++status_update_seq_;
      return;
    } else {
      return;
    }
    s->has_error = true;
    s->error.code = static_cast<uint16_t>(event.status);
    s->error.stage = "event";
    s->error.message = management_utils::managementEventToString(event.event_id);
    s->error.retryable = (s->state == CommandState::Timeout);
    releaseMutationLaneByReq_(event.req_id);
    s->updated_seq = ++status_update_seq_;
  }

  void pushEventRecord_(const ManagementEvent& event) {
    NormalizedEventRecord rec{};
    rec.seq = ++event_seq_;
    rec.event = event;
    rec.event_name = management_utils::managementEventToString(event.event_id);
    rec.status_name = management_utils::managementStatusToString(event.status);
    rec.terminal = (event.event_id == ManagementEventId::CmdDone ||
                    event.event_id == ManagementEventId::CmdFail ||
                    event.event_id == ManagementEventId::Timeout);
    rec.has_requested_peer = event.has_requested_peer;
    rec.requested_peer = event.requested_peer;
    rec.has_executed_peer = event.has_executed_peer;
    rec.executed_peer = event.executed_peer;
    event_ring_.push_back(rec);
    trimEventRing_();
  }

  void trimEventRing_() {
    while (event_ring_.size() > event_ring_capacity_) {
      event_ring_.pop_front();
    }
  }

  void trimReqStatus_() {
    bool trimmed = false;
    while (req_status_.size() > req_status_capacity_) {
      size_t drop_idx = req_status_.size();
      bool found = false;
      uint64_t oldest_seq = 0U;

      for (size_t i = 0U; i < req_status_.size(); ++i) {
        if (!req_status_[i].terminal) continue;
        if (!found || req_status_[i].updated_seq < oldest_seq) {
          found = true;
          oldest_seq = req_status_[i].updated_seq;
          drop_idx = i;
        }
      }

      if (!found) {
        for (size_t i = 0U; i < req_status_.size(); ++i) {
          if (!found || req_status_[i].updated_seq < oldest_seq) {
            found = true;
            oldest_seq = req_status_[i].updated_seq;
            drop_idx = i;
          }
        }
      }

      if (!found || drop_idx >= req_status_.size()) {
        break;
      }
      releaseMutationLaneByReq_(req_status_[drop_idx].req_id);
      req_status_.erase(req_status_.begin() +
                        static_cast<std::vector<CommandStatusView>::difference_type>(drop_idx));
      trimmed = true;
    }
    if (trimmed) {
      rebuildReqStatusIndex_();
    }
  }

  void upsertReqStatus_(const CommandStatusView& in_status) {
    CommandStatusView applied = in_status;
    if (applied.updated_seq == 0U) {
      applied.updated_seq = ++status_update_seq_;
    }
    if (applied.terminal) {
      releaseMutationLaneByReq_(applied.req_id);
    }
    CommandStatusView* s = findReqStatusMutable_(in_status.req_id);
    if (s != nullptr) {
      *s = applied;
      return;
    }
    req_status_.push_back(applied);
    if (applied.req_id != 0U) {
      req_status_index_[applied.req_id] = req_status_.size() - 1U;
    }
    trimReqStatus_();
  }

  CommandStatusView* findReqStatusMutable_(uint32_t req_id) {
    if (req_id == 0U) {
      return nullptr;
    }
    auto it = req_status_index_.find(req_id);
    if (it != req_status_index_.end()) {
      const size_t idx = it->second;
      if (idx < req_status_.size() && req_status_[idx].req_id == req_id) {
        return &req_status_[idx];
      }
      req_status_index_.erase(it);
    }
    for (size_t i = 0U; i < req_status_.size(); ++i) {
      if (req_status_[i].req_id == req_id) {
        req_status_index_[req_id] = i;
        return &req_status_[i];
      }
    }
    return nullptr;
  }

  const CommandStatusView* findReqStatus_(uint32_t req_id) const {
    if (req_id == 0U) {
      return nullptr;
    }
    auto it = req_status_index_.find(req_id);
    if (it != req_status_index_.end()) {
      const size_t idx = it->second;
      if (idx < req_status_.size() && req_status_[idx].req_id == req_id) {
        return &req_status_[idx];
      }
    }
    for (const auto& s : req_status_) {
      if (s.req_id == req_id) {
        return &s;
      }
    }
    return nullptr;
  }

  void rebuildReqStatusIndex_() {
    req_status_index_.clear();
    req_status_index_.reserve(req_status_.size());
    for (size_t i = 0U; i < req_status_.size(); ++i) {
      const uint32_t req_id = req_status_[i].req_id;
      if (req_id != 0U) {
        req_status_index_[req_id] = i;
      }
    }
  }

  static void upsertCachedSetting_(std::vector<SettingDescriptor>& settings, const SettingDescriptor& in_setting) {
    for (auto& s : settings) {
      if (s.setting_id != 0U && in_setting.setting_id != 0U && s.setting_id == in_setting.setting_id) {
        s = in_setting;
        return;
      }
      if (!s.key.empty() && !in_setting.key.empty() && s.key == in_setting.key) {
        s = in_setting;
        return;
      }
    }
    settings.push_back(in_setting);
  }

  static SettingsCacheCompleteness settingsCompletenessFromPaging_(
      const DescriptorResponse& desc,
      const std::vector<SettingDescriptor>& merged_settings) {
    if (!desc.is_paged) {
      return merged_settings.empty() ? SettingsCacheCompleteness::Empty : SettingsCacheCompleteness::Full;
    }
    if (desc.done) {
      if (desc.total_count == 0U || merged_settings.empty()) {
        return SettingsCacheCompleteness::Empty;
      }
      if (merged_settings.size() < static_cast<size_t>(desc.total_count)) {
        return SettingsCacheCompleteness::Partial;
      }
      return SettingsCacheCompleteness::Full;
    }
    return merged_settings.empty() ? SettingsCacheCompleteness::Empty : SettingsCacheCompleteness::Partial;
  }

  bool fillSettingsFromCache_(const MacAddress& peer,
                              std::vector<CachedSettingView>& out_settings,
                              SettingsCacheMeta* out_meta,
                              bool require_full = false) const {
    out_settings.clear();
    const CachedNodeSnapshot* node = findCachedNode_(peer);
    if (node == nullptr || !node->has_settings) {
      if (out_meta != nullptr) {
        out_meta->cache_hit = false;
        out_meta->completeness = SettingsCacheCompleteness::Empty;
        out_meta->ready_for_ui = false;
        out_meta->refresh_inflight = (node != nullptr) ? node->settings_refresh_inflight : false;
        out_meta->last_refresh_origin =
            (node != nullptr) ? node->settings_last_refresh_origin : SettingsRefreshOrigin::Unknown;
        out_meta->settings_seq = (node != nullptr) ? node->settings_seq : 0U;
      }
      return false;
    }

    if (out_meta != nullptr) {
      out_meta->cache_hit = true;
      out_meta->completeness = node->settings_completeness;
      out_meta->ready_for_ui = (node->settings_completeness == SettingsCacheCompleteness::Full);
      out_meta->refresh_inflight = node->settings_refresh_inflight;
      out_meta->last_refresh_origin = node->settings_last_refresh_origin;
      out_meta->settings_seq = node->settings_seq;
      out_meta->cache_updated_ms = node->settings_updated_ms;
      const uint64_t now_ms = monotonicMs_();
      out_meta->cache_age_ms = (node->settings_updated_ms > 0U && now_ms >= node->settings_updated_ms)
                                   ? (now_ms - node->settings_updated_ms)
                                   : 0U;
      out_meta->refresh_status = node->settings_last_status;
      if (!node->settings_last_error.empty()) {
        out_meta->has_error = true;
        out_meta->error_message = node->settings_last_error;
      }
    }
    if (require_full && node->settings_completeness != SettingsCacheCompleteness::Full) {
      if (out_meta != nullptr) {
        out_meta->ready_for_ui = false;
      }
      return false;
    }
    if (!cachedSettingsResolved(peer, out_settings)) {
      if (out_meta != nullptr) {
        out_meta->cache_hit = false;
        out_meta->completeness = SettingsCacheCompleteness::Empty;
        out_meta->ready_for_ui = false;
        out_meta->refresh_inflight = node->settings_refresh_inflight;
        out_meta->last_refresh_origin = node->settings_last_refresh_origin;
        out_meta->settings_seq = node->settings_seq;
      }
      return false;
    }
    if (out_meta != nullptr) {
      out_meta->ready_for_ui = (node->settings_completeness == SettingsCacheCompleteness::Full);
    }
    return true;
  }

  void markSettingsRefreshInflight_(CachedNodeSnapshot& node,
                                    SettingsRefreshOrigin origin,
                                    uint32_t req_id) {
    node.settings_refresh_inflight = true;
    node.settings_refresh_req_id = req_id;
    node.settings_refresh_started_ms = monotonicMs_();
    node.settings_refresh_last_attempt_ms = node.settings_refresh_started_ms;
    node.settings_last_refresh_origin = origin;
  }

  void markSettingsRefreshDone_(CachedNodeSnapshot& node,
                                ManagementStatus status,
                                const std::string& error_message = std::string()) {
    node.settings_refresh_inflight = false;
    node.settings_refresh_req_id = 0U;
    node.settings_last_status = status;
    if (!error_message.empty()) {
      node.settings_last_error = error_message;
    } else {
      node.settings_last_error.clear();
    }
  }

  bool settingsBundleRefreshWithOrigin_(const MacAddress& peer,
                                        SettingsRefreshOrigin origin,
                                        uint32_t* out_req_id = nullptr,
                                        uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;

    const ManagementController::SubmitResult bundle_submit =
        submit(static_cast<uint16_t>(ManagementCommandId::NodeBundleGet),
               management_utils::buildNodeBundleGetPayload(kNodeBundleMaskSettings),
               submit_options);
    if (out_req_id != nullptr) {
      *out_req_id = bundle_submit.req_id;
    }
    if (!bundle_submit.accepted) {
      CachedNodeSnapshot& node = ensureCachedNode_(peer);
      node.settings_last_refresh_origin = origin;
      node.settings_last_status = bundle_submit.status;
      node.settings_last_error = "settings_refresh_submit_failed";
      node.settings_refresh_inflight = false;
      node.settings_refresh_req_id = 0U;
      return false;
    }
    CachedNodeSnapshot& node = ensureCachedNode_(peer);
    markSettingsRefreshInflight_(node, origin, bundle_submit.req_id);
    return true;
  }

  bool hasFreshSettingsCache_(const MacAddress& peer, uint32_t baseline_settings_seq) const {
    const CachedNodeSnapshot* node = findCachedNode_(peer);
    if (node == nullptr || !node->has_settings) {
      return false;
    }
    if (node->settings_completeness != SettingsCacheCompleteness::Full) {
      return false;
    }
    if (node->settings_seq <= baseline_settings_seq) {
      return false;
    }
    return true;
  }

  bool resolvePeerForCache_(const ManagementResponse& response, MacAddress& out_peer) const {
    if (response.has_executed_peer) {
      out_peer = response.executed_peer;
      return true;
    }
    if (response.has_requested_peer) {
      out_peer = response.requested_peer;
      return true;
    }
    return false;
  }

  CachedNodeSnapshot* findCachedNode_(const MacAddress& peer) {
    for (auto& node : cached_nodes_) {
      if (node.peer == peer) {
        return &node;
      }
    }
    return nullptr;
  }

  const CachedNodeSnapshot* findCachedNode_(const MacAddress& peer) const {
    for (const auto& node : cached_nodes_) {
      if (node.peer == peer) {
        return &node;
      }
    }
    return nullptr;
  }

  bool isCachedPairedPeer_(const MacAddress& peer) const {
    for (const auto& p : cached_paired_peers_) {
      if (p == peer) {
        return true;
      }
    }
    return false;
  }

  size_t pickCachedNodeEvictIndex_(bool require_unpaired, bool require_not_refresh) const {
    size_t pick = cached_nodes_.size();
    bool found = false;
    uint32_t best_seq = 0U;
    for (size_t i = 0U; i < cached_nodes_.size(); ++i) {
      const CachedNodeSnapshot& node = cached_nodes_[i];
      if (require_unpaired && isCachedPairedPeer_(node.peer)) {
        continue;
      }
      if (require_not_refresh && node.settings_refresh_inflight) {
        continue;
      }
      if (!found || node.update_seq < best_seq) {
        found = true;
        best_seq = node.update_seq;
        pick = i;
      }
    }
    return found ? pick : cached_nodes_.size();
  }

  void evictCachedNodeForCapacity_() {
    if (cached_nodes_.empty()) {
      return;
    }
    size_t pick = pickCachedNodeEvictIndex_(true, true);
    if (pick >= cached_nodes_.size()) {
      pick = pickCachedNodeEvictIndex_(true, false);
    }
    if (pick >= cached_nodes_.size()) {
      pick = pickCachedNodeEvictIndex_(false, true);
    }
    if (pick >= cached_nodes_.size()) {
      pick = pickCachedNodeEvictIndex_(false, false);
    }
    if (pick >= cached_nodes_.size()) {
      return;
    }
    const MacAddress evicted_peer = cached_nodes_[pick].peer;
    cached_nodes_.erase(cached_nodes_.begin() +
                        static_cast<std::vector<CachedNodeSnapshot>::difference_type>(pick));
    removeChildPushPeerState_(evicted_peer);
  }

  CachedNodeSnapshot& ensureCachedNode_(const MacAddress& peer) {
    CachedNodeSnapshot* node = findCachedNode_(peer);
    if (node != nullptr) {
      return *node;
    }
    if (cached_nodes_.size() >= cached_nodes_capacity_) {
      evictCachedNodeForCapacity_();
    }
    CachedNodeSnapshot add{};
    add.peer = peer;
    cached_nodes_.push_back(add);
    return cached_nodes_.back();
  }

  void removeCachedPeer_(const MacAddress& peer) {
    cached_paired_peers_.erase(std::remove(cached_paired_peers_.begin(), cached_paired_peers_.end(), peer),
                               cached_paired_peers_.end());
    cached_nodes_.erase(std::remove_if(cached_nodes_.begin(),
                                       cached_nodes_.end(),
                                       [&](const CachedNodeSnapshot& n) { return n.peer == peer; }),
                        cached_nodes_.end());
    removeChildPushPeerState_(peer);
  }

  ChildPushPeerState* findChildPushPeerState_(const MacAddress& peer) {
    for (auto& state : child_push_peer_states_) {
      if (state.peer == peer) {
        return &state;
      }
    }
    return nullptr;
  }

  const ChildPushPeerState* findChildPushPeerState_(const MacAddress& peer) const {
    for (const auto& state : child_push_peer_states_) {
      if (state.peer == peer) {
        return &state;
      }
    }
    return nullptr;
  }

  ChildPushPeerState& ensureChildPushPeerState_(const MacAddress& peer) {
    ChildPushPeerState* state = findChildPushPeerState_(peer);
    if (state != nullptr) {
      return *state;
    }
    ChildPushPeerState add{};
    add.peer = peer;
    child_push_peer_states_.push_back(add);
    return child_push_peer_states_.back();
  }

  void removeChildPushPeerState_(const MacAddress& peer) {
    child_push_peer_states_.erase(
        std::remove_if(child_push_peer_states_.begin(),
                       child_push_peer_states_.end(),
                       [&](const ChildPushPeerState& s) { return s.peer == peer; }),
        child_push_peer_states_.end());
  }

  static bool buildSemuChildPushCommand_(uint8_t child_mask,
                                         TelemetryPushAction action,
                                         TelemetryPushMode mode,
                                         uint32_t interval_ms,
                                         float delta_abs,
                                         uint32_t gap_ms,
                                         TelemetryPushCommand& out_cmd) {
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
      if ((child_mask & static_cast<uint8_t>(1U << vid)) == 0U) continue;
      TelemetryPushMetricConfig a{};
      a.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".tfl_a_mm";
      a.enabled = true;
      a.mode = mode;
      a.interval_ms = interval_ms;
      a.min_report_gap_ms = gap_ms;
      a.use_threshold = (mode != TelemetryPushMode::Periodic);
      a.delta_abs = (mode == TelemetryPushMode::Periodic) ? 0.0f : delta_abs;
      out_cmd.config.metrics.push_back(a);

      TelemetryPushMetricConfig b{};
      b.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".tfl_b_mm";
      b.enabled = true;
      b.mode = mode;
      b.interval_ms = interval_ms;
      b.min_report_gap_ms = gap_ms;
      b.use_threshold = (mode != TelemetryPushMode::Periodic);
      b.delta_abs = (mode == TelemetryPushMode::Periodic) ? 0.0f : delta_abs;
      out_cmd.config.metrics.push_back(b);
    }
    return !out_cmd.config.metrics.empty();
  }

  static bool buildRemuChildPushCommand_(uint16_t child_mask,
                                         TelemetryPushAction action,
                                         TelemetryPushMode mode,
                                         uint32_t interval_ms,
                                         float delta_abs,
                                         uint32_t gap_ms,
                                         TelemetryPushCommand& out_cmd) {
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
      if ((child_mask & static_cast<uint16_t>(1U << vid)) == 0U) continue;
      TelemetryPushMetricConfig relay_state{};
      relay_state.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".relay_bitmap";
      relay_state.enabled = true;
      relay_state.mode = mode;
      relay_state.interval_ms = interval_ms;
      relay_state.min_report_gap_ms = gap_ms;
      relay_state.use_threshold = (mode != TelemetryPushMode::Periodic);
      relay_state.delta_abs = (mode == TelemetryPushMode::Periodic) ? 0.0f : delta_abs;
      out_cmd.config.metrics.push_back(relay_state);
    }
    return !out_cmd.config.metrics.empty();
  }

  void tickRuntimeOwned_(uint32_t now_ms,
                         size_t max_requests_per_transport,
                         size_t max_responses,
                         size_t max_events) {
    if (runtime_ == nullptr) return;
    runtime_->tick(now_ms, max_requests_per_transport, max_responses, max_events);
    if (runtime_tick_calls_ < UINT32_MAX) ++runtime_tick_calls_;
  }

  void tickRuntimeFromWait_(size_t max_requests_per_transport,
                            size_t max_responses,
                            size_t max_events) {
    if (runtime_ == nullptr) return;
    if (!wait_loops_tick_runtime_) {
      if (wait_runtime_tick_skips_ < UINT32_MAX) ++wait_runtime_tick_skips_;
      return;
    }
    const uint32_t now_ms = static_cast<uint32_t>(monotonicMs_());
    runtime_->tick(now_ms, max_requests_per_transport, max_responses, max_events);
    if (runtime_tick_calls_ < UINT32_MAX) ++runtime_tick_calls_;
    if (wait_runtime_tick_calls_ < UINT32_MAX) ++wait_runtime_tick_calls_;
  }

  static uint64_t monotonicMs_() {
#if defined(ARDUINO)
    return static_cast<uint64_t>(millis());
#else
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
#endif
  }

  static uint64_t currentExecutionContextToken_() {
#if defined(ARDUINO) && ESPNOW_LINK_FRONTEND_ADAPTER_HAS_RTOS_TASK_ID
    const TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
#elif defined(ARDUINO)
    // Single-loop token when task handle is unavailable in this build.
    return 1U;
#else
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
  }

  static void pauseShort_() {
#if defined(ARDUINO)
    delay(1);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  }

  bool enforceOwnerContext_() const {
#if ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_CHECK
    const uint64_t token = currentExecutionContextToken_();
    if (!owner_token_initialized_) {
      owner_token_initialized_ = true;
      owner_token_ = token;
      return true;
    }
    if (owner_token_ == token) {
      return true;
    }
    ++owner_violation_count_;
#if ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_ASSERT
    assert(false && "ManagementFrontendAdapter owner context violation");
#endif
    return false;
#else
    return true;
#endif
  }

  bool pollResponseRaw_(ManagementResponse& out_response) {
    if (transport_ == nullptr) return false;
    return transport_->pollResponse(out_response);
  }

  bool pollEventRaw_(ManagementEvent& out_event) {
    if (transport_ == nullptr) return false;
    return transport_->pollEvent(out_event);
  }

  ManagementQueueTransport* transport_ = nullptr;
  ManagementRuntime* runtime_ = nullptr;
  ManagementService* service_ = nullptr;
  ManagementSource source_ = ManagementSource::Unknown;
  ManagementController controller_{};
  std::vector<ChildPushPeerState> child_push_peer_states_{};
  std::vector<MacAddress> cached_discovered_peers_{};
  std::vector<MacAddress> cached_paired_peers_{};
  std::vector<CachedNodeSnapshot> cached_nodes_{};
  size_t cached_nodes_capacity_ = 18U;
  std::vector<CommandStatusView> req_status_{};
  std::unordered_map<uint32_t, size_t> req_status_index_{};
  size_t req_status_capacity_ = 256U;
  std::vector<MutationLaneClaim> mutation_lane_claims_{};
  std::deque<NormalizedEventRecord> event_ring_{};
  size_t event_ring_capacity_ = 256U;
  uint64_t event_seq_ = 0U;
  uint64_t status_update_seq_ = 0U;
  uint32_t orchestration_wait_default_ms_ = 3000U;
  bool batch_confirm_default_ = true;
  bool batch_refresh_cache_default_ = true;
  bool auto_pair_settings_bootstrap_enabled_ = true;
  uint32_t pair_bootstrap_settings_timeout_ms_ = 4500U;
  uint64_t paired_generation_ = 0U;
  uint64_t discovery_generation_ = 0U;
  uint32_t cache_update_seq_ = 0U;
  bool radio_transition_active_ = false;
  RadioTransitionState radio_transition_state_ = RadioTransitionState::Idle;
  uint32_t radio_epoch_ = 0U;
  uint16_t radio_transition_last_error_code_ = 0U;
  std::string radio_transition_last_error_stage_{};
  std::string radio_transition_last_error_message_{};
  std::vector<RequestEpochEntry> req_epoch_map_{};
  mutable bool owner_token_initialized_ = false;
  mutable uint64_t owner_token_ = 0U;
  mutable uint32_t owner_violation_count_ = 0U;
  bool wait_loops_tick_runtime_ = true;
  uint32_t runtime_tick_calls_ = 0U;
  uint32_t wait_runtime_tick_calls_ = 0U;
  uint32_t wait_runtime_tick_skips_ = 0U;

