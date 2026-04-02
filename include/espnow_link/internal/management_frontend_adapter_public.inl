
  /** @brief Origin tag for settings refresh/cache hydration actions. */
  enum class SettingsRefreshOrigin : uint8_t {
    Unknown = 0,
    PairBootstrap = 1,
    StartupReconcile = 2,
    ManualRefresh = 3,
    PostWriteSync = 4,
  };

  /** @brief Settings cache completeness state for one peer. */
  enum class SettingsCacheCompleteness : uint8_t {
    Empty = 0,
    Partial = 1,
    Full = 2,
  };

  /** @brief Metadata for cache-backed settings reads/refreshes. */
  struct SettingsCacheMeta {
    bool cache_hit = false;
    SettingsCacheCompleteness completeness = SettingsCacheCompleteness::Empty;
    bool ready_for_ui = false;
    bool refresh_inflight = false;
    SettingsRefreshOrigin last_refresh_origin = SettingsRefreshOrigin::Unknown;
    uint32_t settings_seq = 0U;
    uint64_t cache_updated_ms = 0U;
    uint64_t cache_age_ms = 0U;
    bool refresh_performed = false;
    ManagementStatus refresh_status = ManagementStatus::Ok;
    bool has_error = false;
    std::string error_message{};
  };

  /** @brief Queue depth snapshot. */
  struct QueueDepth {
    size_t requests = 0;
    size_t responses = 0;
    size_t events = 0;
  };

  /** @brief Aggregate frontend workload/busy snapshot for API backpressure gating. */
  struct BusySnapshot {
    bool busy = false;
    bool transport_backlog = false;
    bool command_inflight = false;
    bool settings_refresh_inflight = false;
    size_t inflight_commands = 0;
    size_t settings_refresh_nodes = 0;
    QueueDepth queue{};
    uint64_t updated_ms = 0U;
  };

  /** @brief Runtime pump diagnostics for single-owner hardening. */
  struct RuntimePumpStats {
    bool wait_loops_tick_runtime = true;
    uint32_t runtime_tick_calls = 0U;
    uint32_t wait_runtime_tick_calls = 0U;
    uint32_t wait_runtime_tick_skips = 0U;
  };

  /** @brief Last-known per-node cached view for frontend rendering. */
  struct CachedNodeSnapshot {
    MacAddress peer{};
    bool has_device = false;
    DeviceDescriptor device{};
    bool has_liveness = false;
    LivenessStatus liveness{};
    bool has_time = false;
    TimeStatus time{};
    bool has_settings = false;
    std::vector<SettingDescriptor> settings{};
    uint32_t settings_seq = 0U;
    uint64_t settings_updated_ms = 0U;
    SettingsCacheCompleteness settings_completeness = SettingsCacheCompleteness::Empty;
    ManagementStatus settings_last_status = ManagementStatus::Ok;
    std::string settings_last_error{};
    bool settings_refresh_inflight = false;
    uint32_t settings_refresh_req_id = 0U;
    uint64_t settings_refresh_started_ms = 0U;
    uint64_t settings_refresh_last_attempt_ms = 0U;
    SettingsRefreshOrigin settings_last_refresh_origin = SettingsRefreshOrigin::Unknown;
    bool has_telemetry = false;
    std::vector<TelemetrySample> telemetry_samples{};
    uint32_t update_seq = 0;
  };

  /** @brief Resolved cached setting view for frontend rendering. */
  struct CachedSettingView {
    uint16_t setting_id = 0U;
    std::string key{};
    SettingValueType value_type = SettingValueType::String;
    bool writable = false;
    std::string current_value{};
    std::string default_value{};
    std::string value{};
    bool has_current = false;
    bool has_default = false;
    bool from_default = false;
  };

  /** @brief Normalized command lifecycle state for frontend orchestration. */
  enum class CommandState : uint8_t {
    Queued = 0,
    Running = 1,
    Done = 2,
    Fail = 3,
    Timeout = 4,
  };

  /** @brief Canonical frontend error object. */
  struct CommandError {
    uint16_t code = 0U;
    const char* stage = "";  // submit|response|event|decode|wait
    std::string message{};
    bool retryable = false;
  };

  /** @brief Tracked command status snapshot by request id. */
  struct CommandStatusView {
    uint32_t req_id = 0U;
    uint16_t cmd_id = 0U;
    CommandState state = CommandState::Queued;
    ManagementStatus status = ManagementStatus::Ok;
    uint64_t updated_seq = 0U;
    bool has_response = false;
    ManagementResponse last_response{};
    bool has_event = false;
    ManagementEvent last_event{};
    bool terminal = false;
    bool has_error = false;
    CommandError error{};
  };

  /** @brief Blocking helper result for `commandRunAndWait`. */
  struct CommandRunResult {
    bool accepted = false;
    uint32_t req_id = 0U;
    uint16_t cmd_id = 0U;
    CommandState terminal_state = CommandState::Fail;
    ManagementStatus status = ManagementStatus::InternalError;
    bool has_response = false;
    ManagementResponse response{};
    bool has_event = false;
    ManagementEvent event{};
    bool has_error = false;
    CommandError error{};
  };

  /** @brief Options for lifecycle-aware `runAndTrack(...)` helper. */
  struct RunOptions {
    ManagementController::SubmitOptions submit{};
    bool wait_terminal = true;
  };

  /** @brief Unified lifecycle result for API/CLI orchestration parity paths. */
  struct CommandLifecycleResult {
    bool accepted = false;
    bool terminal = false;
    uint32_t req_id = 0U;
    uint16_t cmd_id = 0U;
    CommandState terminal_state = CommandState::Queued;
    ManagementStatus status = ManagementStatus::InternalError;
    bool terminal_from_event = false;
    bool has_error = false;
    CommandError error{};
  };

  /** @brief Read-only command metadata for CLI/API parity harnesses. */
  struct CommandTraits {
    ManagementAccessLevel min_access = ManagementAccessLevel::Owner;
    bool mutating = false;
    bool deferred_terminal = false;
    bool blocked_during_radio_transition = false;
    uint32_t default_timeout_ms = 0U;
    uint8_t default_priority = 0U;
  };

  /** @brief Frontend-oriented operation state (stable external contract). */
  enum class OperationState : uint8_t {
    Queued = 0,
    Running = 1,
    Succeeded = 2,
    Failed = 3,
    Timeout = 4,
    Canceled = 5,
  };

  /** @brief Operation handle returned by `operationSubmit(...)`. */
  struct OperationHandle {
    uint32_t operation_id = 0U;
    uint32_t req_id = 0U;
    uint16_t cmd_id = 0U;
    bool accepted = false;
  };

  /** @brief High-level operation status view for frontend polling. */
  struct OperationStatus {
    uint32_t operation_id = 0U;
    uint32_t req_id = 0U;
    uint16_t cmd_id = 0U;
    OperationState state = OperationState::Failed;
    bool terminal = false;
    ManagementStatus status_code = ManagementStatus::InternalError;
    const char* stage = "";   // submit|response|event|wait|unknown
    std::string message{};    // human-readable summary
    uint64_t updated_seq = 0; // monotonic adapter-local sequence
    bool has_response = false;
    ManagementResponse last_response{};
    bool has_event = false;
    ManagementEvent last_event{};
    bool has_error = false;
    CommandError error{};
    bool result_from_event = false;
    std::vector<uint8_t> result_payload{};
  };

  /** @brief One key/value item for batch settings mutation. */
  struct SettingsBatchItem {
    std::string key{};
    std::string value{};
  };

  /** @brief Batch mutation execution options. */
  struct SettingsBatchOptions {
    bool confirm = true;
    bool stop_on_error = false;
    bool refresh_cache = true;
    uint32_t timeout_ms = 0U;
  };

  /** @brief Per-key outcome for batch settings mutation. */
  struct SettingsBatchResultItem {
    std::string key{};
    std::string value{};
    uint32_t set_req_id = 0U;
    bool submitted = false;
    bool applied = false;
    bool confirmed = false;
    bool skipped_unchanged = false;
    ManagementStatus status = ManagementStatus::InternalError;
    bool has_error = false;
    CommandError error{};
  };

  /** @brief Aggregated one-call node snapshot for frontend screens. */
  struct NodeSnapshotView {
    MacAddress peer{};
    bool desc_ok = false;
    bool liveness_ok = false;
    bool time_ok = false;
    bool settings_ok = false;
    bool telemetry_ok = false;
    CommandRunResult desc_run{};
    CommandRunResult liveness_run{};
    CommandRunResult time_run{};
    CommandRunResult settings_run{};
    CommandRunResult telemetry_run{};
    bool has_cached_node = false;
    CachedNodeSnapshot node{};
    std::vector<CachedSettingView> resolved_settings{};
  };

  /** @brief Read model for one paired-peer inventory snapshot. */
  struct PairedPeersView {
    uint64_t generation = 0U;
    std::vector<MacAddress> peers{};
  };

  /** @brief Field selection mask for descriptor-bundle reads. */
  struct DescriptorBundleMask {
    bool device = true;
    bool capabilities = true;
    bool settings = true;
    bool telemetry_schema = true;
  };

  /** @brief Read model for one peer descriptor/capability/settings/schema bundle. */
  struct PeerDescriptorBundleView {
    MacAddress peer{};
    bool device_ok = false;
    bool capabilities_ok = false;
    bool settings_ok = false;
    bool telemetry_schema_ok = false;
    CommandRunResult device_run{};
    CommandRunResult capabilities_run{};
    CommandRunResult settings_run{};
    CommandRunResult telemetry_schema_run{};
    DeviceDescriptor device{};
    std::vector<CapabilityDescriptor> capabilities{};
    std::vector<SettingDescriptor> settings{};
    std::vector<TelemetryDescriptor> telemetry_schema{};
  };

  /** @brief Options for one-shot telemetry-now pull. */
  struct TelemetryNowOptions {
    uint32_t timeout_ms = 0U;
    bool fetch_all_pages = true;
    uint8_t page_size = 6U;
  };

  /** @brief Read model for one peer one-shot telemetry pull. */
  struct TelemetryNowView {
    MacAddress peer{};
    bool ok = false;
    CommandRunResult run{};
    std::vector<TelemetrySample> samples{};
  };

  /** @brief Read model for auto-pull/live-monitor runtime status. */
  struct AutoPullStatusView {
    bool ok = false;
    CommandRunResult run{};
    ManagementLiveMonitorStatusPayload status{};
  };

  /** @brief Normalized event record stored in adapter-side event ring. */
  struct NormalizedEventRecord {
    uint64_t seq = 0U;
    ManagementEvent event{};
    std::string event_name{};
    std::string status_name{};
    bool terminal = false;
    bool has_requested_peer = false;
    MacAddress requested_peer{};
    bool has_executed_peer = false;
    MacAddress executed_peer{};
  };

  /** @brief Snapshot output for event ring pagination. */
  struct EventSnapshotPage {
    std::vector<NormalizedEventRecord> events{};
    uint64_t next_seq = 0U;
  };

  /** @brief Adapter cache generation counters for deterministic frontend reconciliation. */
  struct StateGenerations {
    uint64_t paired_generation = 0U;
    uint64_t discovery_generation = 0U;
  };

  /** @brief Radio transition lifecycle state. */
  using RadioTransitionState = ManagementService::RadioTransitionState;

  /** @brief Begin options for frontend-owned radio transition flow. */
  struct RadioTransitionBeginOptions {
    uint32_t quiesce_timeout_ms = 3000U;
    bool stop_discovery = true;
    bool disable_live_monitor = true;
    bool clear_master_update_guard = true;
    bool cancel_deferred_operations = true;
    bool cancel_pending_mutating_requests = true;
  };

  /** @brief End options for frontend-owned radio transition flow. */
  struct RadioTransitionEndOptions {
    uint32_t resume_timeout_ms = 3000U;
    bool restore_live_monitor = true;
    bool sync_live_monitor_peers = true;
    bool resync_paired_snapshot = true;
    bool resync_discovery_snapshot = false;
    bool resync_event_ring = true;
  };

  /** @brief Current radio transition snapshot for frontend decisions. */
  struct RadioTransitionStatus {
    bool active = false;
    RadioTransitionState state = RadioTransitionState::Idle;
    uint32_t radio_epoch = 0U;
    uint16_t last_error_code = 0U;
    std::string last_error_stage{};
    std::string last_error_message{};
  };

  /** @brief Result returned by begin/end radio transition calls. */
  struct RadioTransitionResult {
    bool ok = false;
    uint32_t radio_epoch = 0U;
    std::string summary{};
  };

  /** @brief Options for one-shot hard radio deinit lifecycle. */
  struct RadioHardDeinitOptions {
    bool stop_discovery = true;
    bool disable_live_monitor = true;
    bool clear_master_update_guard = true;
    bool cancel_deferred_operations = true;
    bool cancel_pending_mutating_requests = true;
    bool clear_service_queues = true;
    bool clear_transport_queues = true;
    bool clear_adapter_cache = true;
  };

  /** @brief Result of hard radio deinit lifecycle. */
  struct RadioHardDeinitResult {
    bool ok = false;
    bool core_deinit = false;
    uint32_t radio_epoch = 0U;
    std::string summary{};
  };

  /** @brief Options for hard radio reinit lifecycle after hard deinit. */
  struct RadioHardReinitOptions {
    bool restore_link = true;
    bool reset_service_state = true;
    bool clear_adapter_cache = true;
    bool resync_paired_snapshot = true;
    bool resync_discovery_snapshot = false;
    bool resync_event_ring = true;
    uint32_t resume_timeout_ms = 3000U;
  };

  /** @brief Result of hard radio reinit lifecycle. */
  struct RadioHardReinitResult {
    bool ok = false;
    bool core_reinit = false;
    uint32_t radio_epoch = 0U;
    std::string summary{};
  };

  /** @brief Construct unbound adapter. */
  ManagementFrontendAdapter() = default;

  /**
   * @brief Construct adapter bound to queue/service/runtime backends.
   *
   * @param transport Preferred queue transport endpoint (nullable).
   * @param runtime Optional runtime used for stats/tick.
   * @param service Optional service handle used for radio lifecycle APIs.
   * @param source Request source metadata.
   */
  ManagementFrontendAdapter(ManagementQueueTransport* transport,
                            ManagementRuntime* runtime = nullptr,
                            ManagementService* service = nullptr,
                            ManagementSource source = ManagementSource::Unknown,
                            ManagementAccessLevel access_level = ManagementAccessLevel::Owner) {
    bind(transport, runtime, service, source, access_level);
  }

  /** @brief Rebind adapter to queue/service/runtime backends. */
  void bind(ManagementQueueTransport* transport,
            ManagementRuntime* runtime = nullptr,
            ManagementService* service = nullptr,
            ManagementSource source = ManagementSource::Unknown,
            ManagementAccessLevel access_level = ManagementAccessLevel::Owner) {
    if (!enforceOwnerContext_()) return;
    transport_ = transport;
    runtime_ = runtime;
    service_ = service;
    source_ = source;
    if (transport_ != nullptr) {
      // Canonical command path: queue transport only.
      transport_->setAccessLevel(access_level);
      controller_.bind(*transport_);
      if (source_ != ManagementSource::Unknown) {
        controller_.setSource(source_);
      } else {
        source_ = controller_.source();
      }
    } else {
      controller_.clearBindings();
      controller_.setSource(source_);
    }
    controller_.setAccessLevel(access_level);
    if (service_ != nullptr) {
      ManagementService::RadioTransitionStatus svc_status{};
      service_->radioTransitionStatusGet(svc_status);
      applyRadioTransitionStatusFromService_(svc_status);
    } else {
      radio_transition_active_ = false;
      radio_transition_state_ = RadioTransitionState::Idle;
      radio_transition_last_error_code_ = 0U;
      radio_transition_last_error_stage_.clear();
      radio_transition_last_error_message_.clear();
    }
  }

  /** @brief True when canonical queue-backed command path is available. */
  bool ready() const { return transport_ != nullptr; }

  /** @brief True when debug owner-check guardrails are compiled in. */
  static constexpr bool ownerCheckEnabled() {
#if ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_CHECK
    return true;
#else
    return false;
#endif
  }

  /** @brief Number of owner-check violations detected at runtime. */
  uint32_t ownerViolationCount() const { return owner_violation_count_; }

  /** @brief Reset owner-check capture/violation counters. */
  void ownerCheckReset() {
    owner_token_initialized_ = false;
    owner_token_ = 0U;
    owner_violation_count_ = 0U;
  }

  /**
   * @brief Control whether blocking wait loops are allowed to tick runtime directly.
   *
   * For strict single-owner runtime mode, keep this disabled and drive runtime
   * only via `tick(...)` from one canonical owner task.
   */
  void setWaitLoopsTickRuntime(bool enabled) {
    wait_loops_tick_runtime_ = enabled;
  }

  /** @brief Read current wait-loop runtime tick policy. */
  bool waitLoopsTickRuntime() const { return wait_loops_tick_runtime_; }

  /** @brief Snapshot runtime pump diagnostics counters. */
  void runtimePumpStatsGet(RuntimePumpStats& out_stats) const {
    out_stats.wait_loops_tick_runtime = wait_loops_tick_runtime_;
    out_stats.runtime_tick_calls = runtime_tick_calls_;
    out_stats.wait_runtime_tick_calls = wait_runtime_tick_calls_;
    out_stats.wait_runtime_tick_skips = wait_runtime_tick_skips_;
  }

  /** @brief Source identity used for submitted command metadata. */
  ManagementSource source() const { return controller_.source(); }
  /** @brief Set source identity for submitted command metadata. */
  void setSource(ManagementSource source) {
    if (!enforceOwnerContext_()) return;
    if (source == ManagementSource::Unknown && transport_ != nullptr) {
      source_ = transport_->source();
      controller_.setSource(source_);
      return;
    }
    source_ = source;
    controller_.setSource(source_);
  }

  /** @brief Access level used for submitted commands. */
  ManagementAccessLevel accessLevel() const { return controller_.accessLevel(); }
  /** @brief Set access level for submitted commands. */
  void setAccessLevel(ManagementAccessLevel access_level) {
    if (!enforceOwnerContext_()) return;
    controller_.setAccessLevel(access_level);
    if (transport_ != nullptr) {
      transport_->setAccessLevel(access_level);
    }
  }

  /** @brief Set next request id used for auto-id submissions. */
  void setNextReqId(uint32_t next_req_id) {
    if (!enforceOwnerContext_()) return;
    controller_.setNextReqId(next_req_id);
  }
  /** @brief Read next request id used for auto-id submissions. */
  uint32_t nextReqId() const { return controller_.nextReqId(); }

  /** @brief Set default timeout used when `timeout_ms==0` in `submit`. */
  void setDefaultTimeoutMs(uint32_t timeout_ms) {
    if (!enforceOwnerContext_()) return;
    controller_.setDefaultTimeoutMs(timeout_ms);
  }
  /** @brief Read default timeout used by `submit`. */
  uint32_t defaultTimeoutMs() const { return controller_.defaultTimeoutMs(); }

  /** @brief Set frontend orchestration wait timeout default used by helper APIs. */
  void setOrchestrationWaitDefaultMs(uint32_t timeout_ms) {
    if (!enforceOwnerContext_()) return;
    orchestration_wait_default_ms_ = (timeout_ms == 0U) ? 3000U : timeout_ms;
  }
  /** @brief Read frontend orchestration wait timeout default. */
  uint32_t orchestrationWaitDefaultMs() const { return orchestration_wait_default_ms_; }

  /** @brief Set default batch behavior for helper APIs. */
  void setBatchDefaults(bool confirm, bool refresh_cache) {
    if (!enforceOwnerContext_()) return;
    batch_confirm_default_ = confirm;
    batch_refresh_cache_default_ = refresh_cache;
  }
  /** @brief Read default batch confirm behavior. */
  bool batchConfirmDefault() const { return batch_confirm_default_; }
  /** @brief Read default batch refresh-cache behavior. */
  bool batchRefreshCacheDefault() const { return batch_refresh_cache_default_; }

  /** @brief Enable/disable one-shot settings cache bootstrap after pair success events. */
  void setAutoPairSettingsBootstrap(bool enabled, uint32_t timeout_ms = 4500U) {
    if (!enforceOwnerContext_()) return;
    auto_pair_settings_bootstrap_enabled_ = enabled;
    pair_bootstrap_settings_timeout_ms_ = (timeout_ms == 0U) ? 4500U : timeout_ms;
  }
  /** @brief Read pair-success settings bootstrap enable flag. */
  bool autoPairSettingsBootstrapEnabled() const { return auto_pair_settings_bootstrap_enabled_; }
  /** @brief Read pair-success settings bootstrap timeout. */
  uint32_t autoPairSettingsBootstrapTimeoutMs() const { return pair_bootstrap_settings_timeout_ms_; }

  /**
   * @brief Apply orchestration defaults from resolved settings key/value list.
   *
   * Recognized keys:
   * - `orch_wait_ms`
   * - `orch_event_ring`
   * - `orch_batch_confirm`
   * - `orch_refresh_cache`
   */
  void applyOrchestrationDefaultsFromResolvedSettings(const std::vector<CachedSettingView>& settings) {
    if (!enforceOwnerContext_()) return;
    for (const auto& s : settings) {
      if (s.key == "orch_wait_ms") {
        uint32_t v = 0U;
        if (parseU32Ascii_(s.value, v)) {
          setOrchestrationWaitDefaultMs(v);
        }
      } else if (s.key == "orch_event_ring") {
        uint32_t v = 0U;
        if (parseU32Ascii_(s.value, v)) {
          setEventRingCapacity(static_cast<size_t>(v));
        }
      } else if (s.key == "orch_batch_confirm") {
        bool b = false;
        if (parseBoolAscii_(s.value, b)) {
          batch_confirm_default_ = b;
        }
      } else if (s.key == "orch_refresh_cache") {
        bool b = false;
        if (parseBoolAscii_(s.value, b)) {
          batch_refresh_cache_default_ = b;
        }
      }
    }
  }

  /**
   * @brief Access typed management command surface.
   *
   * Frontends should use this accessor for CLI-equivalent control without
   * string parsing (descriptor, settings, telemetry, logger, storage, OTA...).
   */
  ManagementController& commands() { return controller_; }
  /** @brief Const access to typed management command surface. */
  const ManagementController& commands() const { return controller_; }

  /** @brief Read canonical traits for one management command id. */
  bool commandTraitsGet(ManagementCommandId cmd, CommandTraits& out_traits) const {
    out_traits = CommandTraits{};
    const uint16_t cmd_id = static_cast<uint16_t>(cmd);
    out_traits.min_access = requiredAccessForCmd_(cmd_id);
    out_traits.mutating = (mutationLaneDomainForCmd_(cmd_id) != MutationLaneDomain::None);
    out_traits.deferred_terminal = ManagementService::isAsyncTerminalCommand(cmd_id);
    out_traits.blocked_during_radio_transition = isRadioTransitionBlockedCommand_(cmd_id);
    out_traits.default_timeout_ms = ManagementService::commandTimeoutMs(cmd_id);
    out_traits.default_priority = ManagementService::commandPriority(cmd_id);
    return true;
  }

  /**
   * @brief Submit one management command with canonical submit options.
   *
   * @param cmd_id Management command id.
   * @param payload Command payload.
   * @param options Submit options (`req_id`, timeout, target behavior).
   * @return Rich submit result with acceptance/status/reject stage.
   */
  ManagementController::SubmitResult submit(uint16_t cmd_id,
                                            const std::vector<uint8_t>& payload = {},
                                            const ManagementController::SubmitOptions& options = {}) {
    return submitTracked_(cmd_id, payload, options);
  }

  /**
   * @brief Submit one command and wait for terminal state using adapter polling.
   *
   * This helper is additive and does not change service/CLI semantics.
   */
  bool commandRunAndWait(uint16_t cmd_id,
                         const std::vector<uint8_t>& payload,
                         CommandRunResult& out_result,
                         uint32_t timeout_ms = 0,
                         uint32_t req_id = 0) {
    ManagementController::SubmitOptions submit_options{};
    submit_options.req_id = req_id;
    submit_options.timeout_ms = timeout_ms;
    return commandRunAndWait(cmd_id, payload, out_result, submit_options);
  }

  /**
   * @brief Submit one command and wait for terminal state using explicit submit options.
   */
  bool commandRunAndWait(uint16_t cmd_id,
                         const std::vector<uint8_t>& payload,
                         CommandRunResult& out_result,
                         const ManagementController::SubmitOptions& submit_options) {
    out_result = CommandRunResult{};
    out_result.cmd_id = cmd_id;
    const ManagementController::SubmitResult submit_result = submit(cmd_id, payload, submit_options);
    out_result.accepted = submit_result.accepted;
    out_result.req_id = submit_result.req_id;
    out_result.status = submit_result.status;
    if (!submit_result.accepted) {
      out_result.terminal_state = CommandState::Fail;
      out_result.has_error = true;
      out_result.error.code = static_cast<uint16_t>(submit_result.status);
      out_result.error.stage = (submit_result.reject_stage != nullptr) ? submit_result.reject_stage : "submit";
      out_result.error.message = management_utils::managementStatusToString(submit_result.status);
      out_result.error.retryable = isRetryableStatus_(submit_result.status);
      return false;
    }

    uint32_t wait_ms = submit_options.timeout_ms;
    if (wait_ms == 0U) {
      wait_ms = controller_.defaultTimeoutMs();
    }
    if (wait_ms == 0U) {
      wait_ms = orchestration_wait_default_ms_;
    }
    const uint64_t start_ms = monotonicMs_();
    const uint64_t deadline_ms = start_ms + static_cast<uint64_t>(wait_ms == 0U ? 3000U : wait_ms);
    while (monotonicMs_() < deadline_ms) {
      tickRuntimeFromWait_(4U, 32U, 64U);
      (void)drainToCache(32U, 64U);
      CommandStatusView status{};
      if (commandStatusGet(submit_result.req_id, status) && status.terminal) {
        out_result.status = status.status;
        out_result.terminal_state = status.state;
        out_result.has_response = status.has_response;
        if (status.has_response) out_result.response = status.last_response;
        out_result.has_event = status.has_event;
        if (status.has_event) out_result.event = status.last_event;
        out_result.has_error = status.has_error;
        if (status.has_error) out_result.error = status.error;
        return status.state == CommandState::Done;
      }
    }

    CommandStatusView status{};
    if (commandStatusGet(submit_result.req_id, status)) {
      status.state = CommandState::Timeout;
      status.status = ManagementStatus::Timeout;
      status.terminal = true;
      status.has_error = true;
      status.error.code = static_cast<uint16_t>(ManagementStatus::Timeout);
      status.error.stage = "wait";
      status.error.message = "wait timeout";
      status.error.retryable = true;
      upsertReqStatus_(status);
      out_result.status = status.status;
      out_result.terminal_state = status.state;
      out_result.has_response = status.has_response;
      if (status.has_response) out_result.response = status.last_response;
      out_result.has_event = status.has_event;
      if (status.has_event) out_result.event = status.last_event;
      out_result.has_error = true;
      out_result.error = status.error;
      return false;
    }
    return false;
  }

  /**
   * @brief Submit one command and return normalized lifecycle result.
   *
   * This helper is additive and composes existing adapter behaviors only.
   */
  CommandLifecycleResult runAndTrack(uint16_t cmd_id,
                                     const std::vector<uint8_t>& payload,
                                     const RunOptions& options) {
    CommandLifecycleResult out{};
    out.cmd_id = cmd_id;
    out.terminal_state = CommandState::Queued;
    const ManagementController::SubmitResult submit_result = submit(cmd_id, payload, options.submit);
    out.accepted = submit_result.accepted;
    out.req_id = submit_result.req_id;
    out.status = submit_result.status;
    if (!submit_result.accepted) {
      out.terminal = true;
      out.terminal_state = CommandState::Fail;
      out.has_error = true;
      out.error.code = static_cast<uint16_t>(submit_result.status);
      out.error.stage = (submit_result.reject_stage != nullptr) ? submit_result.reject_stage : "submit";
      out.error.message = management_utils::managementStatusToString(submit_result.status);
      out.error.retryable = isRetryableStatus_(submit_result.status);
      return out;
    }

    if (!options.wait_terminal) {
      out.terminal = false;
      out.terminal_state = CommandState::Queued;
      return out;
    }

    OperationStatus op_status{};
    const bool ok = operationWait(submit_result.req_id, op_status, options.submit.timeout_ms);
    out.terminal = op_status.terminal;
    out.status = op_status.status_code;
    out.terminal_from_event = op_status.result_from_event;
    out.has_error = op_status.has_error;
    if (op_status.has_error) {
      out.error = op_status.error;
    }

    switch (op_status.state) {
      case OperationState::Succeeded:
        out.terminal_state = CommandState::Done;
        break;
      case OperationState::Timeout:
        out.terminal_state = CommandState::Timeout;
        break;
      case OperationState::Failed:
      case OperationState::Canceled:
        out.terminal_state = CommandState::Fail;
        break;
      case OperationState::Queued:
        out.terminal_state = CommandState::Queued;
        break;
      case OperationState::Running:
      default:
        out.terminal_state = CommandState::Running;
        break;
    }
    if (!ok && !out.has_error) {
      out.has_error = true;
      out.error.code = static_cast<uint16_t>(out.status);
      out.error.stage = "wait";
      out.error.message = "operation wait failed";
      out.error.retryable = (out.terminal_state == CommandState::Timeout);
    }
    return out;
  }

  CommandLifecycleResult runAndTrack(uint16_t cmd_id,
                                     const std::vector<uint8_t>& payload = {}) {
    RunOptions options{};
    return runAndTrack(cmd_id, payload, options);
  }

  /** @brief Read tracked command status by request id. */
  bool commandStatusGet(uint32_t req_id, CommandStatusView& out_status) const {
    if (!enforceOwnerContext_()) return false;
    const CommandStatusView* found = findReqStatus_(req_id);
    if (found == nullptr) return false;
    out_status = *found;
    return true;
  }

  /**
   * @brief Submit one operation and return a stable handle (`operation_id == req_id`).
   */
  bool operationSubmit(uint16_t cmd_id,
                       const std::vector<uint8_t>& payload,
                       OperationHandle& out_handle,
                       uint32_t timeout_ms = 0,
                       uint32_t req_id = 0) {
    ManagementController::SubmitOptions submit_options{};
    submit_options.req_id = req_id;
    submit_options.timeout_ms = timeout_ms;
    return operationSubmit(cmd_id, payload, out_handle, submit_options);
  }

  /**
   * @brief Submit one operation and return a stable handle (`operation_id == req_id`).
   */
  bool operationSubmit(uint16_t cmd_id,
                       const std::vector<uint8_t>& payload,
                       OperationHandle& out_handle,
                       const ManagementController::SubmitOptions& submit_options) {
    if (!enforceOwnerContext_()) {
      out_handle = OperationHandle{};
      return false;
    }
    out_handle = OperationHandle{};
    const ManagementController::SubmitResult submit_result = submit(cmd_id, payload, submit_options);
    out_handle.operation_id = submit_result.req_id;
    out_handle.req_id = submit_result.req_id;
    out_handle.cmd_id = cmd_id;
    out_handle.accepted = submit_result.accepted;
    return submit_result.accepted;
  }

  /** @brief Read normalized operation status by operation id (`req_id`). */
  bool operationStatus(uint32_t operation_id, OperationStatus& out_status) const {
    if (!enforceOwnerContext_()) return false;
    CommandStatusView tracked{};
    if (!commandStatusGet(operation_id, tracked)) {
      return false;
    }
    buildOperationStatus_(tracked, out_status);
    return true;
  }

  /** @brief Read normalized operation status from one operation handle. */
  bool operationStatus(const OperationHandle& handle, OperationStatus& out_status) const {
    const uint32_t operation_id = (handle.operation_id != 0U) ? handle.operation_id : handle.req_id;
    if (operation_id == 0U) return false;
    return operationStatus(operation_id, out_status);
  }

  /**
   * @brief Wait for terminal state of one already-submitted operation.
   *
   * @return true when terminal state is `Succeeded`.
   */
  bool operationWait(uint32_t operation_id, OperationStatus& out_status, uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) {
      out_status = OperationStatus{};
      return false;
    }
    out_status = OperationStatus{};
    if (operation_id == 0U) {
      return false;
    }

    uint32_t wait_ms = timeout_ms;
    if (wait_ms == 0U) {
      wait_ms = controller_.defaultTimeoutMs();
    }
    if (wait_ms == 0U) {
      wait_ms = orchestration_wait_default_ms_;
    }

    const uint64_t deadline_ms = monotonicMs_() + static_cast<uint64_t>((wait_ms == 0U) ? 3000U : wait_ms);
    while (monotonicMs_() < deadline_ms) {
      tickRuntimeFromWait_(4U, 32U, 64U);
      (void)drainToCache(32U, 64U);

      OperationStatus status{};
      if (operationStatus(operation_id, status) && status.terminal) {
        out_status = status;
        return status.state == OperationState::Succeeded;
      }
    }

    CommandStatusView timeout_status{};
    if (commandStatusGet(operation_id, timeout_status)) {
      timeout_status.state = CommandState::Timeout;
      timeout_status.status = ManagementStatus::Timeout;
      timeout_status.terminal = true;
      timeout_status.has_error = true;
      timeout_status.error.code = static_cast<uint16_t>(ManagementStatus::Timeout);
      timeout_status.error.stage = "wait";
      timeout_status.error.message = "wait timeout";
      timeout_status.error.retryable = true;
      timeout_status.updated_seq = ++status_update_seq_;
      upsertReqStatus_(timeout_status);
      (void)operationStatus(operation_id, out_status);
      return false;
    }

    out_status.operation_id = operation_id;
    out_status.req_id = operation_id;
    out_status.state = OperationState::Timeout;
    out_status.terminal = true;
    out_status.status_code = ManagementStatus::Timeout;
    out_status.stage = "wait";
    out_status.message = "wait timeout";
    out_status.has_error = true;
    out_status.error.code = static_cast<uint16_t>(ManagementStatus::Timeout);
    out_status.error.stage = "wait";
    out_status.error.message = "wait timeout";
    out_status.error.retryable = true;
    out_status.updated_seq = ++status_update_seq_;
    return false;
  }

  /** @brief Wait for terminal state using one operation handle. */
  bool operationWait(const OperationHandle& handle, OperationStatus& out_status, uint32_t timeout_ms = 0) {
    const uint32_t operation_id = (handle.operation_id != 0U) ? handle.operation_id : handle.req_id;
    return operationWait(operation_id, out_status, timeout_ms);
  }

  /** @brief Submit tracked pair request for one peer MAC. */
  bool pairPeerTracked(const MacAddress& peer, OperationHandle& out_handle, uint32_t timeout_ms = 0) {
    return operationSubmit(static_cast<uint16_t>(ManagementCommandId::PairRequest),
                           management_utils::buildMacPayload(peer),
                           out_handle,
                           timeout_ms);
  }

  /** @brief Submit tracked unpair request for one explicit peer. */
  bool unpairPeerTracked(const MacAddress& peer, OperationHandle& out_handle, uint32_t timeout_ms = 0) {
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    return operationSubmit(static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                           {},
                           out_handle,
                           submit_options);
  }

  /** @brief Submit tracked discovery start request with window in ms. */
  bool discoveryStartTracked(uint32_t window_ms, OperationHandle& out_handle, uint32_t timeout_ms = 0) {
    return operationSubmit(static_cast<uint16_t>(ManagementCommandId::DiscoveryStart),
                           management_utils::buildDiscoveryStartPayload(window_ms),
                           out_handle,
                           timeout_ms);
  }

  /** @brief Submit tracked discovery stop request. */
  bool discoveryStopTracked(OperationHandle& out_handle, uint32_t timeout_ms = 0) {
    return operationSubmit(static_cast<uint16_t>(ManagementCommandId::DiscoveryStop), {}, out_handle, timeout_ms);
  }

  /** @brief Set bounded adapter-side event ring capacity (minimum 16). */
  void setEventRingCapacity(size_t capacity) {
    event_ring_capacity_ = (capacity < 16U) ? 16U : capacity;
    trimEventRing_();
  }

  /** @brief Read current adapter-side event ring capacity. */
  size_t eventRingCapacity() const { return event_ring_capacity_; }

  /**
   * @brief Read normalized event records with seq pagination.
   * @param since_seq Returns records where `seq > since_seq`.
   * @param limit Max records to return (0 => default 64).
   */
  void eventsSnapshot(uint64_t since_seq, size_t limit, EventSnapshotPage& out_page) const {
    out_page = EventSnapshotPage{};
    const size_t cap = (limit == 0U) ? 64U : limit;
    out_page.events.reserve(std::min(cap, event_ring_.size()));
    for (const auto& rec : event_ring_) {
      if (rec.seq <= since_seq) continue;
      out_page.events.push_back(rec);
      if (out_page.events.size() >= cap) break;
    }
    if (!out_page.events.empty()) {
      out_page.next_seq = out_page.events.back().seq;
    } else {
      out_page.next_seq = since_seq;
    }
  }

  /**
   * @brief Enter app-owned radio transition mode (quiesce mutating flows).
   *
   * This is a local lifecycle API and does not send a remote management command.
   */
  bool beginRadioTransition(const RadioTransitionBeginOptions& options,
                            RadioTransitionResult* out_result = nullptr) {
    if (!enforceOwnerContext_()) {
      if (out_result != nullptr) {
        *out_result = RadioTransitionResult{};
        out_result->summary = "owner check violation";
      }
      return false;
    }
    if (out_result != nullptr) {
      *out_result = RadioTransitionResult{};
    }
    if (service_ == nullptr) {
      if (out_result != nullptr) {
        out_result->summary = "service not bound";
      }
      return false;
    }

    ManagementService::RadioTransitionBeginOptions svc_opts{};
    svc_opts.stop_discovery = options.stop_discovery;
    svc_opts.disable_live_monitor = options.disable_live_monitor;
    svc_opts.clear_master_update_guard = options.clear_master_update_guard;
    svc_opts.cancel_deferred_operations = options.cancel_deferred_operations;
    svc_opts.cancel_pending_mutating_requests = options.cancel_pending_mutating_requests;

    const bool ok = service_->beginRadioTransition(svc_opts);
    ManagementService::RadioTransitionStatus svc_status{};
    service_->radioTransitionStatusGet(svc_status);
    applyRadioTransitionStatusFromService_(svc_status);
    if (out_result != nullptr) {
      out_result->ok = ok;
      out_result->radio_epoch = radio_epoch_;
      out_result->summary = ok ? "radio transition paused" : "radio transition begin failed";
    }
    if (!ok) {
      return false;
    }

    uint32_t wait_ms = options.quiesce_timeout_ms;
    if (wait_ms == 0U) {
      wait_ms = 3000U;
    }
    const uint64_t deadline_ms = monotonicMs_() + static_cast<uint64_t>(wait_ms);
    while (monotonicMs_() < deadline_ms) {
      tickRuntimeFromWait_(4U, 32U, 64U);
      (void)drainToCache(64U, 128U);

      const QueueDepth depth = queueDepth();
      if (depth.requests == 0U && depth.responses == 0U && depth.events == 0U) {
        break;
      }
    }
    return true;
  }

  /** @brief Enter radio transition mode using default options. */
  bool beginRadioTransition(RadioTransitionResult* out_result = nullptr) {
    return beginRadioTransition(RadioTransitionBeginOptions{}, out_result);
  }

  /**
   * @brief Leave app-owned radio transition mode and optionally resync cache.
   */
  bool endRadioTransition(const RadioTransitionEndOptions& options,
                          RadioTransitionResult* out_result = nullptr) {
    if (!enforceOwnerContext_()) {
      if (out_result != nullptr) {
        *out_result = RadioTransitionResult{};
        out_result->summary = "owner check violation";
      }
      return false;
    }
    if (out_result != nullptr) {
      *out_result = RadioTransitionResult{};
    }
    if (service_ == nullptr) {
      if (out_result != nullptr) {
        out_result->summary = "service not bound";
      }
      return false;
    }

    ManagementService::RadioTransitionEndOptions svc_opts{};
    svc_opts.restore_live_monitor = options.restore_live_monitor;
    svc_opts.sync_live_monitor_peers = options.sync_live_monitor_peers;
    const bool ok = service_->endRadioTransition(svc_opts);

    ManagementService::RadioTransitionStatus svc_status{};
    service_->radioTransitionStatusGet(svc_status);
    applyRadioTransitionStatusFromService_(svc_status);
    if (out_result != nullptr) {
      out_result->ok = ok;
      out_result->radio_epoch = radio_epoch_;
      out_result->summary = ok ? "radio transition resumed" : "radio transition resume failed";
    }
    if (!ok) {
      return false;
    }

    CommandRunResult run{};
    if (options.resync_paired_snapshot) {
      (void)commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::PairedSnapshotGet), {}, run,
                              options.resume_timeout_ms);
    }
    if (options.resync_discovery_snapshot) {
      (void)commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::DiscoverySnapshotGet), {}, run,
                              options.resume_timeout_ms);
    }
    if (options.resync_event_ring) {
      (void)drainToCache(128U, 256U);
    }
    return true;
  }

  /** @brief Leave radio transition mode using default options. */
  bool endRadioTransition(RadioTransitionResult* out_result = nullptr) {
    return endRadioTransition(RadioTransitionEndOptions{}, out_result);
  }

  /** @brief Hard-stop radio lifecycle: quiesce + core deinit (library scope only). */
  bool hardDeinitRadio(const RadioHardDeinitOptions& options,
                       RadioHardDeinitResult* out_result = nullptr) {
    if (!enforceOwnerContext_()) {
      if (out_result != nullptr) {
        *out_result = RadioHardDeinitResult{};
        out_result->summary = "owner check violation";
      }
      return false;
    }
    if (out_result != nullptr) {
      *out_result = RadioHardDeinitResult{};
    }
    if (service_ == nullptr) {
      if (out_result != nullptr) {
        out_result->summary = "service not bound";
      }
      return false;
    }

    ManagementService::RadioHardDeinitOptions svc_opts{};
    svc_opts.enter_transition_if_needed = true;
    svc_opts.stop_discovery = options.stop_discovery;
    svc_opts.disable_live_monitor = options.disable_live_monitor;
    svc_opts.clear_master_update_guard = options.clear_master_update_guard;
    svc_opts.cancel_deferred_operations = options.cancel_deferred_operations;
    svc_opts.cancel_pending_mutating_requests = options.cancel_pending_mutating_requests;
    svc_opts.clear_queues = options.clear_service_queues;

    const bool core_ok = service_->hardDeinitRadio(svc_opts);
    ManagementService::RadioTransitionStatus svc_status{};
    service_->radioTransitionStatusGet(svc_status);
    applyRadioTransitionStatusFromService_(svc_status);

    if (options.clear_transport_queues && transport_ != nullptr) {
      transport_->clear();
    }
    if (options.clear_adapter_cache) {
      cacheClear();
    }

    const bool ok = core_ok;
    if (out_result != nullptr) {
      out_result->ok = ok;
      out_result->core_deinit = core_ok;
      out_result->radio_epoch = radio_epoch_;
      if (ok) {
        out_result->summary = "radio hard deinit complete";
      } else {
        out_result->summary = "radio hard deinit failed in core";
      }
    }
    return ok;
  }

  /** @brief Hard-stop radio lifecycle with default options. */
  bool hardDeinitRadio(RadioHardDeinitResult* out_result = nullptr) {
    return hardDeinitRadio(RadioHardDeinitOptions{}, out_result);
  }

  /** @brief Hard reinit lifecycle after hard deinit (library scope only). */
  bool hardReinitRadio(const RadioHardReinitOptions& options,
                       RadioHardReinitResult* out_result = nullptr) {
    if (!enforceOwnerContext_()) {
      if (out_result != nullptr) {
        *out_result = RadioHardReinitResult{};
        out_result->summary = "owner check violation";
      }
      return false;
    }
    if (out_result != nullptr) {
      *out_result = RadioHardReinitResult{};
    }
    if (service_ == nullptr) {
      if (out_result != nullptr) {
        out_result->summary = "service not bound";
      }
      return false;
    }

    ManagementService::RadioHardReinitOptions svc_opts{};
    svc_opts.restore_link = options.restore_link;
    svc_opts.reset_service_state = options.reset_service_state;

    const bool core_ok = service_->hardReinitRadio(svc_opts);
    ManagementService::RadioTransitionStatus svc_status{};
    service_->radioTransitionStatusGet(svc_status);
    applyRadioTransitionStatusFromService_(svc_status);
    if (!core_ok) {
      if (out_result != nullptr) {
        out_result->ok = false;
        out_result->core_reinit = false;
        out_result->radio_epoch = radio_epoch_;
        out_result->summary = "radio hard reinit failed in core";
      }
      return false;
    }

    if (options.clear_adapter_cache) {
      cacheClear();
    }
    CommandRunResult run{};
    if (options.resync_paired_snapshot) {
      (void)commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::PairedSnapshotGet), {}, run,
                              options.resume_timeout_ms);
    }
    if (options.resync_discovery_snapshot) {
      (void)commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::DiscoverySnapshotGet), {}, run,
                              options.resume_timeout_ms);
    }
    if (options.resync_event_ring) {
      (void)drainToCache(128U, 256U);
    }

    if (out_result != nullptr) {
      out_result->ok = true;
      out_result->core_reinit = true;
      out_result->radio_epoch = radio_epoch_;
      out_result->summary = "radio hard reinit complete";
    }
    return true;
  }

  /** @brief Hard reinit lifecycle with default options. */
  bool hardReinitRadio(RadioHardReinitResult* out_result = nullptr) {
    return hardReinitRadio(RadioHardReinitOptions{}, out_result);
  }

  /** @brief Read current radio transition status snapshot. */
  bool radioTransitionStatusGet(RadioTransitionStatus& out_status) const {
    if (!enforceOwnerContext_()) return false;
    if (service_ != nullptr) {
      ManagementService::RadioTransitionStatus svc_status{};
      service_->radioTransitionStatusGet(svc_status);
      out_status.active = svc_status.active;
      out_status.state = svc_status.state;
      out_status.radio_epoch = svc_status.radio_epoch;
      out_status.last_error_code = static_cast<uint16_t>(svc_status.last_error);
      out_status.last_error_stage = svc_status.last_error_stage;
      out_status.last_error_message = svc_status.last_error_message;
      return true;
    }
    out_status.active = radio_transition_active_;
    out_status.state = radio_transition_state_;
    out_status.radio_epoch = radio_epoch_;
    out_status.last_error_code = radio_transition_last_error_code_;
    out_status.last_error_stage = radio_transition_last_error_stage_;
    out_status.last_error_message = radio_transition_last_error_message_;
    return true;
  }

  /** @brief True when radio transition mode is active. */
  bool radioTransitionActive() const { return radio_transition_active_; }

  /** @brief Monotonic radio transition epoch. */
  uint32_t radioEpoch() const { return radio_epoch_; }

  /** @brief Request paired peer snapshot (`pair_seq` order, max 255 entries on wire). */
  bool pairedSnapshotGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.pairedSnapshotGet(out_req_id, timeout_ms);
  }

  /**
   * @brief Resolve paired snapshot in one call with optional role hints.
   *
   * Sends `PairedSnapshotGet`, waits for response, and decodes
   * `(peer, role_code)` entries for frontend list rendering.
   */
  bool pairedSnapshotGetResolved(std::vector<ManagementPairedPeerInfo>& out_peers,
                                 uint32_t timeout_ms = 0) {
    out_peers.clear();
    CommandRunResult run{};
    if (!commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::PairedSnapshotGet), {}, run, timeout_ms)) {
      return false;
    }
    if (!run.has_response) {
      return false;
    }
    return decodePairedSnapshotResponseDetailed(run.response, out_peers);
  }

  /**
   * @brief Read paired peers view in one call.
   *
   * When `refresh` is true, this submits `PairedSnapshotGet` and decodes the response.
   * When `refresh` is false, it returns adapter cache content.
   */
  bool pairedPeersGet(PairedPeersView& out_view, bool refresh = true, uint32_t timeout_ms = 0) {
    out_view = PairedPeersView{};
    if (!enforceOwnerContext_()) return false;

    bool ok = true;
    if (refresh) {
      CommandRunResult run{};
      ok = commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::PairedSnapshotGet), {}, run, timeout_ms);
      if (!ok || !run.has_response) {
        return false;
      }
      std::vector<MacAddress> peers{};
      if (!decodePairedSnapshotResponse(run.response, peers)) {
        return false;
      }
      cached_paired_peers_ = peers;
      ++paired_generation_;
    }

    out_view.generation = paired_generation_;
    out_view.peers = cached_paired_peers_;
    return ok;
  }

  /**
   * @brief Read one peer descriptor bundle (`desc/caps/settings/telem schema`).
   */
  bool descriptorBundleGet(const MacAddress& peer,
                           const DescriptorBundleMask& mask,
                           PeerDescriptorBundleView& out_view,
                           uint32_t timeout_ms = 0) {
    out_view = PeerDescriptorBundleView{};
    out_view.peer = peer;
    if (!enforceOwnerContext_()) return false;

    auto run_targeted = [&](ManagementCommandId cmd_id,
                            const std::vector<uint8_t>& payload,
                            CommandRunResult& out_run) -> bool {
      ManagementController::SubmitOptions opts{};
      opts.timeout_ms = timeout_ms;
      opts.has_target_peer = true;
      opts.target_peer = peer;
      return commandRunAndWait(static_cast<uint16_t>(cmd_id), payload, out_run, opts);
    };

    bool ok = true;

    if (mask.device) {
      out_view.device_ok = run_targeted(ManagementCommandId::DescGet, {}, out_view.device_run);
      ok = ok && out_view.device_ok && out_view.device_run.has_response;
      if (out_view.device_run.has_response) {
        DescriptorResponse desc{};
        if (decodeDescriptorResponse(out_view.device_run.response.payload.data(),
                                     out_view.device_run.response.payload.size(),
                                     desc) &&
            desc.type == DescriptorResponseType::Device) {
          out_view.device = desc.device;
        } else {
          out_view.device_ok = false;
          ok = false;
        }
      }
    }

    if (mask.capabilities) {
      out_view.capabilities_ok = run_targeted(ManagementCommandId::CapsGet, {}, out_view.capabilities_run);
      ok = ok && out_view.capabilities_ok && out_view.capabilities_run.has_response;
      if (out_view.capabilities_run.has_response) {
        DescriptorResponse desc{};
        if (decodeDescriptorResponse(out_view.capabilities_run.response.payload.data(),
                                     out_view.capabilities_run.response.payload.size(),
                                     desc) &&
            desc.type == DescriptorResponseType::Capabilities) {
          out_view.capabilities = desc.capabilities;
        } else {
          out_view.capabilities_ok = false;
          ok = false;
        }
      }
    }

    if (mask.settings) {
      out_view.settings_ok =
          run_targeted(ManagementCommandId::NodeBundleGet,
                       management_utils::buildNodeBundleGetPayload(kNodeBundleMaskSettings),
                       out_view.settings_run);
      if (out_view.settings_ok) {
        (void)drainToCache(32U, 64U);
        CachedNodeSnapshot node{};
        if (cachedNode(peer, node) && node.has_settings) {
          out_view.settings = node.settings;
        } else {
          out_view.settings_ok = false;
        }
      }
      ok = ok && out_view.settings_ok;
    }

    if (mask.telemetry_schema) {
      out_view.telemetry_schema_ok = run_targeted(ManagementCommandId::TelemSchemaGet, {}, out_view.telemetry_schema_run);
      ok = ok && out_view.telemetry_schema_ok && out_view.telemetry_schema_run.has_response;
      if (out_view.telemetry_schema_run.has_response) {
        DescriptorResponse desc{};
        if (decodeDescriptorResponse(out_view.telemetry_schema_run.response.payload.data(),
                                     out_view.telemetry_schema_run.response.payload.size(),
                                     desc) &&
            desc.type == DescriptorResponseType::Telemetry) {
          out_view.telemetry_schema = desc.telemetry;
        } else {
          out_view.telemetry_schema_ok = false;
          ok = false;
        }
      }
    }

    return ok;
  }

  /**
   * @brief Read one peer one-shot telemetry snapshot (`TelemPull`).
   */
  bool telemetryNowGet(const MacAddress& peer,
                       TelemetryNowView& out_view,
                       const TelemetryNowOptions& options) {
    out_view = TelemetryNowView{};
    out_view.peer = peer;
    if (!enforceOwnerContext_()) return false;

    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = options.timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    auto decodeSnapshot = [](const CommandRunResult& run,
                             DescriptorResponse& out_desc) -> bool {
      if (!run.has_response) {
        return false;
      }
      if (!decodeDescriptorResponse(run.response.payload.data(),
                                    run.response.payload.size(),
                                    out_desc)) {
        return false;
      }
      return out_desc.type == DescriptorResponseType::TelemetrySnapshot;
    };

    const uint8_t requested_page_size =
        static_cast<uint8_t>((options.page_size == 0U) ? 6U : std::min<uint8_t>(options.page_size, 16U));
    if (!options.fetch_all_pages) {
      out_view.ok = commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::TelemPull),
                                      {},
                                      out_view.run,
                                      submit_options);
      DescriptorResponse desc{};
      if (!decodeSnapshot(out_view.run, desc)) {
        out_view.ok = false;
        return false;
      }
      out_view.samples = desc.telemetry_samples;
      return out_view.ok;
    }

    auto appendU16Le = [](std::vector<uint8_t>& out, uint16_t value) {
      out.push_back(static_cast<uint8_t>(value & 0xFFU));
      out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    };

    uint16_t cursor = 0U;
    bool final_ok = true;
    std::vector<TelemetrySample> merged{};
    for (size_t page_guard = 0; page_guard < 512U; ++page_guard) {
      std::vector<uint8_t> payload{};
      appendU16Le(payload, cursor);
      payload.push_back(requested_page_size);

      CommandRunResult page_run{};
      const bool page_ok = commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::TelemPull),
                                             payload,
                                             page_run,
                                             submit_options);
      final_ok = final_ok && page_ok;
      if (page_guard == 0U) {
        out_view.run = page_run;
      }

      DescriptorResponse desc{};
      if (!decodeSnapshot(page_run, desc)) {
        out_view.ok = false;
        return false;
      }

      if (!desc.is_paged) {
        // Compatibility path for nodes that do not page telemetry snapshots yet.
        out_view.samples = desc.telemetry_samples;
        out_view.ok = final_ok;
        return out_view.ok;
      }

      // Live snapshots may legitimately change between pages (e.g. uptime ticks).
      // Merge by metric identity instead of enforcing a fixed snapshot id.

      for (const auto& sample : desc.telemetry_samples) {
        bool exists = false;
        if (sample.metric_id != 0U) {
          for (const auto& cur : merged) {
            if (cur.metric_id == sample.metric_id) {
              exists = true;
              break;
            }
          }
        } else {
          for (const auto& cur : merged) {
            if (cur.key == sample.key) {
              exists = true;
              break;
            }
          }
        }
        if (!exists) {
          merged.push_back(sample);
        }
      }

      if (desc.done) {
        out_view.samples = std::move(merged);
        out_view.ok = final_ok;
        return out_view.ok;
      }
      if (desc.next_cursor <= desc.cursor) {
        out_view.ok = false;
        return false;
      }
      cursor = desc.next_cursor;
    }

    out_view.ok = false;
    return false;
  }

  bool telemetryNowGet(const MacAddress& peer,
                       TelemetryNowView& out_view) {
    TelemetryNowOptions options{};
    return telemetryNowGet(peer, out_view, options);
  }

  /**
   * @brief Run one explicit-target push command with normalized lifecycle result.
   */
  CommandLifecycleResult pushControl(const MacAddress& peer,
                                     const TelemetryPushCommand& command,
                                     uint32_t timeout_ms = 0,
                                     bool wait_terminal = true) {
    CommandLifecycleResult out{};
    out.cmd_id = static_cast<uint16_t>(ManagementCommandId::PushGet);

    std::vector<uint8_t> payload{};
    if (!encodeTelemetryPushCommand(command, payload)) {
      out.accepted = false;
      out.terminal = true;
      out.terminal_state = CommandState::Fail;
      out.status = ManagementStatus::BadPayload;
      out.has_error = true;
      out.error.code = static_cast<uint16_t>(ManagementStatus::BadPayload);
      out.error.stage = "submit";
      out.error.message = "invalid push command payload";
      out.error.retryable = false;
      return out;
    }

    ManagementCommandId cmd_id = ManagementCommandId::PushGet;
    switch (command.action) {
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

    RunOptions run_options{};
    run_options.wait_terminal = wait_terminal;
    run_options.submit.timeout_ms = timeout_ms;
    run_options.submit.has_target_peer = true;
    run_options.submit.target_peer = peer;
    return runAndTrack(static_cast<uint16_t>(cmd_id), payload, run_options);
  }

  /**
   * @brief Enable/disable master auto-pull runtime (`LiveMonitorEnable/Disable`).
   */
  CommandLifecycleResult autoPullControl(bool enabled,
                                         uint32_t timeout_ms = 0,
                                         bool wait_terminal = true) {
    RunOptions run_options{};
    run_options.wait_terminal = wait_terminal;
    run_options.submit.timeout_ms = timeout_ms;
    const uint16_t cmd_id = static_cast<uint16_t>(enabled ? ManagementCommandId::LiveMonitorEnable
                                                           : ManagementCommandId::LiveMonitorDisable);
    return runAndTrack(cmd_id, {}, run_options);
  }

  /**
   * @brief Read auto-pull/live-monitor runtime status (`LiveMonitorStatusGet`).
   */
  bool autoPullStatusGet(AutoPullStatusView& out_view, uint32_t timeout_ms = 0) {
    out_view = AutoPullStatusView{};
    if (!enforceOwnerContext_()) return false;
    out_view.ok = commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::LiveMonitorStatusGet),
                                    {},
                                    out_view.run,
                                    timeout_ms);
    if (!out_view.ok || !out_view.run.has_response) {
      return false;
    }
    if (!decodeLiveMonitorStatusResponse(out_view.run.response, out_view.status)) {
      out_view.ok = false;
      return false;
    }
    return true;
  }

  /**
   * @brief Run one explicit-target topology command with normalized lifecycle result.
   */
  CommandLifecycleResult topologyControl(const MacAddress& peer,
                                         uint16_t cmd_id,
                                         const std::vector<uint8_t>& payload = {},
                                         uint32_t timeout_ms = 0,
                                         bool wait_terminal = true) {
    if (!isTopologyControlCommand_(cmd_id)) {
      CommandLifecycleResult out{};
      out.cmd_id = cmd_id;
      out.accepted = false;
      out.terminal = true;
      out.terminal_state = CommandState::Fail;
      out.status = ManagementStatus::DeniedByPolicy;
      out.has_error = true;
      out.error.code = static_cast<uint16_t>(ManagementStatus::DeniedByPolicy);
      out.error.stage = "validation";
      out.error.message = "topologyControl rejects non-topology command id";
      out.error.retryable = false;
      return out;
    }
    RunOptions run_options{};
    run_options.wait_terminal = wait_terminal;
    run_options.submit.timeout_ms = timeout_ms;
    run_options.submit.has_target_peer = true;
    run_options.submit.target_peer = peer;
    return runAndTrack(cmd_id, payload, run_options);
  }

  /**
   * @brief Run one explicit-target OTA command with normalized lifecycle result.
   */
  CommandLifecycleResult otaControl(const MacAddress& peer,
                                    uint16_t cmd_id,
                                    const std::vector<uint8_t>& payload = {},
                                    uint32_t timeout_ms = 0,
                                    bool wait_terminal = true) {
    if (!isOtaControlCommand_(cmd_id)) {
      CommandLifecycleResult out{};
      out.cmd_id = cmd_id;
      out.accepted = false;
      out.terminal = true;
      out.terminal_state = CommandState::Fail;
      out.status = ManagementStatus::DeniedByPolicy;
      out.has_error = true;
      out.error.code = static_cast<uint16_t>(ManagementStatus::DeniedByPolicy);
      out.error.stage = "validation";
      out.error.message = "otaControl rejects non-OTA command id";
      out.error.retryable = false;
      return out;
    }
    RunOptions run_options{};
    run_options.wait_terminal = wait_terminal;
    run_options.submit.timeout_ms = timeout_ms;
    run_options.submit.has_target_peer = true;
    run_options.submit.target_peer = peer;
    return runAndTrack(cmd_id, payload, run_options);
  }

  /** @brief Explicit-target audio ping command. */
  bool audioPingRequest(const MacAddress& peer, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return submitTargetedCommand_(
        peer, ManagementCommandId::AudioPingRequest, {}, out_req_id, timeout_ms);
  }

  /** @brief Explicit-target slave restart command. */
  bool restartTargetRequest(const MacAddress& peer,
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0) {
    return submitTargetedCommand_(
        peer, ManagementCommandId::RestartSlaveRequest, {}, out_req_id, timeout_ms);
  }

  /** @brief Explicit-target slave reset command. */
  bool resetTargetRequest(const MacAddress& peer,
                          uint32_t* out_req_id = nullptr,
                          uint32_t timeout_ms = 0) {
    return submitTargetedCommand_(
        peer, ManagementCommandId::ResetSlaveRequest, {}, out_req_id, timeout_ms);
  }

  /** @brief Explicit-target PMS 48V chain power control (`chain_48v_enable`). */
  bool pmsChain48vSet(const MacAddress& peer,
                      bool enabled,
                      uint32_t* out_req_id = nullptr,
                      uint32_t timeout_ms = 0) {
    return submitTargetedCommand_(
        peer,
        ManagementCommandId::SettingSet,
        management_utils::buildSettingSetByKeyPayload("chain_48v_enable", enabled ? "1" : "0"),
        out_req_id,
        timeout_ms);
  }

  /** @brief Explicit-target PMS charger control (`charger_enable`). */
  bool pmsChargerSet(const MacAddress& peer,
                     bool enabled,
                     uint32_t* out_req_id = nullptr,
                     uint32_t timeout_ms = 0) {
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::SettingSet,
                                  management_utils::buildSettingSetByKeyPayload(
                                      "charger_enable",
                                      enabled ? "1" : "0"),
                                  out_req_id,
                                  timeout_ms);
  }

  /**
   * @brief Explicit-target relay output control (`relay1_enable`/`relay2_enable`).
   *
   * @param relay_index Relay output index in [1..2].
   */
  bool relayOutputSet(const MacAddress& peer,
                      uint8_t relay_index,
                      bool enabled,
                      uint32_t* out_req_id = nullptr,
                      uint32_t timeout_ms = 0) {
    if (relay_index < 1U || relay_index > 2U) {
      return false;
    }
    const char* key = (relay_index == 1U) ? "relay1_enable" : "relay2_enable";
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::SettingSet,
                                  management_utils::buildSettingSetByKeyPayload(key, enabled ? "1" : "0"),
                                  out_req_id,
                                  timeout_ms);
  }

  /**
   * @brief Explicit-target REMU child output control (`v<idx>.output_enable`).
   *
   * @param child_index REMU child index in [0..15].
   */
  bool remuOutputSet(const MacAddress& peer,
                     uint8_t child_index,
                     bool enabled,
                     uint32_t* out_req_id = nullptr,
                     uint32_t timeout_ms = 0) {
    if (child_index > 15U) {
      return false;
    }
    const std::string key = std::string("v") + std::to_string(static_cast<unsigned int>(child_index)) + ".output_enable";
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::SettingSet,
                                  management_utils::buildSettingSetByKeyPayload(key, enabled ? "1" : "0"),
                                  out_req_id,
                                  timeout_ms);
  }

  /** @brief Enable local master CLI and persist state. */
  bool cliEnable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.cliEnable(out_req_id, timeout_ms);
  }

  /** @brief Disable local master CLI and persist state. */
  bool cliDisable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.cliDisable(out_req_id, timeout_ms);
  }

  /** @brief Query local master CLI status; response payload byte[0] = enabled(0/1). */
  bool cliStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.cliStatusGet(out_req_id, timeout_ms);
  }

  /** @brief Enable chain auto-loop for all supported chain slaves (RELAY/SENS/SEMU/REMU). */
  bool chainLoopEnable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.chainLoopEnable(out_req_id, timeout_ms);
  }

  /** @brief Disable chain auto-loop for all supported chain slaves (RELAY/SENS/SEMU/REMU). */
  bool chainLoopDisable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.chainLoopDisable(out_req_id, timeout_ms);
  }

  /** @brief Set chain auto-loop enable/disable. */
  bool chainLoopSet(bool enabled, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.chainLoopSetEnabled(enabled, out_req_id, timeout_ms);
  }

  /** @brief Query persisted chain auto-loop state (`byte[0]` => enabled 0/1). */
  bool chainLoopStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.chainLoopStatusGet(out_req_id, timeout_ms);
  }

  /**
   * @brief Run one targeted NodeBundle pull and merge response(s) into cache.
   *
   * This is the API-first bundled fetch path for one peer.
   * Returns true when command reaches terminal `Done`.
   */
  bool nodeBundleGet(const MacAddress& peer,
                     uint8_t bundle_mask,
                     CommandRunResult& out_run,
                     uint32_t timeout_ms = 0) {
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    const bool ok =
        commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::NodeBundleGet),
                          management_utils::buildNodeBundleGetPayload(bundle_mask),
                          out_run,
                          submit_options);
    if (ok) {
      (void)drainToCache(64U, 128U);
    }
    return ok;
  }

  /**
   * @brief Submit targeted settings bundle refresh without blocking.
   *
   * Replacement path: `NodeBundleGet(settings)` only.
   */
  bool settingsBundleRefresh(const MacAddress& peer,
                             uint32_t* out_req_id = nullptr,
                             uint32_t timeout_ms = 0) {
    return settingsBundleRefreshWithOrigin_(peer,
                                            SettingsRefreshOrigin::ManualRefresh,
                                            out_req_id,
                                            timeout_ms);
  }

  /**
   * @brief Fetch one peer settings using bundled API path and return resolved values.
   *
   * Explicit refresh path: forces one targeted settings bundle pull before returning.
   */
  bool settingsBundleGet(const MacAddress& peer,
                         std::vector<CachedSettingView>& out_settings,
                         uint32_t timeout_ms = 0,
                         CommandRunResult* out_run = nullptr) {
    return settingsRefresh(peer, out_settings, timeout_ms, out_run, nullptr);
  }

  /**
   * @brief Read cached resolved settings without triggering network pulls.
   */
  bool settingsReadCached(const MacAddress& peer,
                          std::vector<CachedSettingView>& out_settings,
                          SettingsCacheMeta* out_meta = nullptr) const {
    out_settings.clear();
    if (out_meta != nullptr) {
      *out_meta = SettingsCacheMeta{};
    }
    return fillSettingsFromCache_(peer, out_settings, out_meta);
  }

  /**
   * @brief Read cache only, but require UI-ready/full settings completeness.
   */
  bool settingsReadCachedForUi(const MacAddress& peer,
                               std::vector<CachedSettingView>& out_settings,
                               SettingsCacheMeta* out_meta = nullptr) const {
    out_settings.clear();
    if (out_meta != nullptr) {
      *out_meta = SettingsCacheMeta{};
    }
    const bool ok = fillSettingsFromCache_(peer, out_settings, out_meta, true);
    if (out_meta != nullptr) {
      out_meta->ready_for_ui = ok;
    }
    return ok;
  }

  /**
   * @brief Force one `NodeBundleGet(settings)` refresh then return cache view.
   *
   * If refresh fails but cache exists, returns cached settings and reports error in metadata.
   */
  bool settingsRefresh(const MacAddress& peer,
                       std::vector<CachedSettingView>& out_settings,
                       uint32_t timeout_ms = 0,
                       CommandRunResult* out_run = nullptr,
                       SettingsCacheMeta* out_meta = nullptr) {
    out_settings.clear();
    if (out_meta != nullptr) {
      *out_meta = SettingsCacheMeta{};
      out_meta->refresh_performed = true;
      out_meta->last_refresh_origin = SettingsRefreshOrigin::ManualRefresh;
    }

    CachedNodeSnapshot& refresh_node = ensureCachedNode_(peer);
    markSettingsRefreshInflight_(refresh_node, SettingsRefreshOrigin::ManualRefresh, 0U);

    const CachedNodeSnapshot* before_node = findCachedNode_(peer);
    const uint32_t baseline_settings_seq = (before_node != nullptr) ? before_node->settings_seq : 0U;

    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;

    CommandRunResult run{};
    const bool ok =
        commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::NodeBundleGet),
                          management_utils::buildNodeBundleGetPayload(kNodeBundleMaskSettings),
                          run,
                          submit_options);
    if (out_run != nullptr) *out_run = run;
    if (out_meta != nullptr) {
      out_meta->refresh_status = run.status;
    }

    if (!ok) {
      refresh_node.settings_last_status = run.status;
      refresh_node.settings_last_error = !run.error.message.empty() ? run.error.message : "settings_refresh_failed";
    }

    if (ok && hasFreshSettingsCache_(peer, baseline_settings_seq)) {
      SettingsCacheMeta tmp{};
      (void)fillSettingsFromCache_(peer, out_settings, &tmp);
      if (out_meta != nullptr) {
        *out_meta = tmp;
        out_meta->refresh_performed = true;
        out_meta->refresh_status = run.status;
        out_meta->last_refresh_origin = SettingsRefreshOrigin::ManualRefresh;
      }
      markSettingsRefreshDone_(refresh_node,
                               run.status,
                               run.error.message.empty() ? std::string{} : run.error.message);
      return true;
    }

    uint32_t settle_timeout_ms = timeout_ms;
    if (settle_timeout_ms == 0U) {
      settle_timeout_ms = controller_.defaultTimeoutMs();
    }
    if (settle_timeout_ms == 0U) {
      settle_timeout_ms = orchestration_wait_default_ms_;
    }
    if (settle_timeout_ms == 0U) {
      settle_timeout_ms = 1000U;
    }
    if (settle_timeout_ms > 1500U) {
      settle_timeout_ms = 1500U;
    }

    const uint64_t deadline_ms = monotonicMs_() + static_cast<uint64_t>(settle_timeout_ms);
    while (monotonicMs_() < deadline_ms) {
      tickRuntimeFromWait_(2U, 16U, 32U);
      const size_t consumed = drainToCache(16U, 32U);
      if (hasFreshSettingsCache_(peer, baseline_settings_seq)) {
        SettingsCacheMeta tmp{};
        (void)fillSettingsFromCache_(peer, out_settings, &tmp);
        if (out_meta != nullptr) {
          *out_meta = tmp;
          out_meta->refresh_performed = true;
          out_meta->refresh_status = run.status;
          out_meta->last_refresh_origin = SettingsRefreshOrigin::ManualRefresh;
        }
        markSettingsRefreshDone_(refresh_node,
                                 run.status,
                                 run.error.message.empty() ? std::string{} : run.error.message);
        return true;
      }
      if (consumed == 0U) {
        pauseShort_();
      }
    }

    // Refresh failed or did not produce a new cache generation.
    // Return existing cache when available to avoid blank frontend state.
    SettingsCacheMeta cached_meta{};
    const bool has_cached = fillSettingsFromCache_(peer, out_settings, &cached_meta, true);
    refresh_node.settings_last_status = run.status;
    refresh_node.settings_last_error = !ok ? (!run.error.message.empty() ? run.error.message : "settings_refresh_failed")
                                           : "settings_refresh_not_settled";
    markSettingsRefreshDone_(refresh_node, run.status, refresh_node.settings_last_error);
    if (out_meta != nullptr) {
      *out_meta = cached_meta;
      out_meta->refresh_performed = true;
      out_meta->refresh_status = run.status;
      out_meta->last_refresh_origin = SettingsRefreshOrigin::ManualRefresh;
      if (!ok) {
        out_meta->has_error = true;
        out_meta->error_message = !run.error.message.empty() ? run.error.message : "settings_refresh_failed";
      } else {
        out_meta->has_error = true;
        out_meta->error_message = "settings_refresh_not_settled";
      }
    }
    return has_cached;
  }

  /**
   * @brief Authoritative cache sync helper after setting writes.
   */
  bool settingsAfterWriteSync(const MacAddress& peer,
                              std::vector<CachedSettingView>& out_settings,
                              uint32_t timeout_ms = 0,
                              CommandRunResult* out_run = nullptr,
                              SettingsCacheMeta* out_meta = nullptr) {
    const bool ok = settingsRefresh(peer, out_settings, timeout_ms, out_run, out_meta);
    CachedNodeSnapshot& node = ensureCachedNode_(peer);
    node.settings_last_refresh_origin = SettingsRefreshOrigin::PostWriteSync;
    if (out_meta != nullptr) {
      out_meta->last_refresh_origin = SettingsRefreshOrigin::PostWriteSync;
    }
    return ok;
  }

  /**
   * @brief Refresh target slave settings cache by requesting full settings descriptor.
   *
   * Frontend flow:
   * 1) call this method
   * 2) drain feedback with `pollResponseCached/pollEventCached` or `drainToCache`
   * 3) read resolved values with `cachedSettingsResolved(...)`
   */
  bool settingsCacheRefresh(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.nodeBundleGet(kNodeBundleMaskSettings, out_req_id, timeout_ms);
  }

  /**
   * @brief Refresh one slave settings cache explicitly by peer.
   */
  bool settingsCacheRefreshPeer(const MacAddress& peer,
                                uint32_t* out_req_id = nullptr,
                                uint32_t timeout_ms = 0) {
    return settingsBundleRefreshWithOrigin_(peer,
                                            SettingsRefreshOrigin::ManualRefresh,
                                            out_req_id,
                                            timeout_ms);
  }

  /** @brief Refresh one target setting in cache by key. */
  bool settingsCacheRefreshKey(const std::string& key,
                               uint32_t* out_req_id = nullptr,
                               uint32_t timeout_ms = 0) {
    return controller_.settingGetByKey(key, out_req_id, timeout_ms);
  }

  /**
   * @brief Refresh one setting in one slave cache by key (peer-explicit).
   */
  bool settingsCacheRefreshPeerKey(const MacAddress& peer,
                                   const std::string& key,
                                   uint32_t* out_req_id = nullptr,
                                   uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    const ManagementController::SubmitResult submit_result =
        submit(static_cast<uint16_t>(ManagementCommandId::SettingGet),
               management_utils::buildSettingGetByKeyPayload(key),
               submit_options);
    if (out_req_id != nullptr) {
      *out_req_id = submit_result.req_id;
    }
    return submit_result.accepted;
  }

  /** @brief Refresh one target setting in cache by numeric id. */
  bool settingsCacheRefreshId(uint16_t setting_id,
                              uint32_t* out_req_id = nullptr,
                              uint32_t timeout_ms = 0) {
    return controller_.settingGetById(setting_id, out_req_id, timeout_ms);
  }

  /**
   * @brief Refresh one setting in one slave cache by id (peer-explicit).
   */
  bool settingsCacheRefreshPeerId(const MacAddress& peer,
                                  uint16_t setting_id,
                                  uint32_t* out_req_id = nullptr,
                                  uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    const ManagementController::SubmitResult submit_result =
        submit(static_cast<uint16_t>(ManagementCommandId::SettingGet),
               management_utils::buildSettingGetByIdPayload(setting_id),
               submit_options);
    if (out_req_id != nullptr) {
      *out_req_id = submit_result.req_id;
    }
    return submit_result.accepted;
  }

  /**
   * @brief Fetch one peer settings list and return resolved UI-ready values.
   *
   * Fast path: return full/ready cache immediately.
   * Fallback: run one targeted refresh then return resolved values
   * (`current` else `default`).
   */
  bool settingsGetResolved(const MacAddress& peer,
                           std::vector<CachedSettingView>& out_settings,
                           uint32_t timeout_ms = 0,
                           CommandRunResult* out_run = nullptr) {
    out_settings.clear();
    if (out_run != nullptr) {
      *out_run = CommandRunResult{};
    }
    if (settingsReadCachedForUi(peer, out_settings, nullptr)) {
      if (out_run != nullptr) {
        out_run->accepted = true;
        out_run->cmd_id = static_cast<uint16_t>(ManagementCommandId::NodeBundleGet);
        out_run->terminal_state = CommandState::Done;
        out_run->status = ManagementStatus::Ok;
      }
      return true;
    }
    return settingsRefresh(peer, out_settings, timeout_ms, out_run, nullptr);
  }

  /**
   * @brief Apply multiple settings on one peer with optional confirm/readback.
   */
  bool settingsSetBatch(const MacAddress& peer,
                        const std::vector<SettingsBatchItem>& items,
                        std::vector<SettingsBatchResultItem>& out_results,
                        const SettingsBatchOptions& options) {
    out_results.clear();
    out_results.reserve(items.size());
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = options.timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;

    bool all_ok = true;
    bool any_mutation_submitted = false;
    for (const auto& it : items) {
      SettingsBatchResultItem r{};
      r.key = it.key;
      r.value = it.value;
      if (it.key.empty()) {
        r.has_error = true;
        r.error.code = static_cast<uint16_t>(ManagementStatus::BadPayload);
        r.error.stage = "submit";
        r.error.message = "empty setting key";
        r.error.retryable = false;
        r.status = ManagementStatus::BadPayload;
        out_results.push_back(r);
        all_ok = false;
        if (options.stop_on_error) break;
        continue;
      }

      CachedSettingView cached{};
      if (cachedSettingResolved(peer, it.key, cached) && cached.value == it.value) {
        r.submitted = false;
        r.applied = true;
        r.confirmed = true;
        r.skipped_unchanged = true;
        r.status = ManagementStatus::Ok;
        out_results.push_back(r);
        continue;
      }

      CommandRunResult set_run{};
      const bool set_ok = commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::SettingSet),
                                            management_utils::buildSettingSetByKeyPayload(it.key, it.value),
                                            set_run,
                                            submit_options);
      r.set_req_id = set_run.req_id;
      r.submitted = set_run.accepted;
      if (r.submitted) {
        any_mutation_submitted = true;
      }
      r.applied = set_ok;
      r.status = set_run.status;
      r.has_error = set_run.has_error;
      if (set_run.has_error) r.error = set_run.error;

      if (set_ok && options.confirm) {
        CommandRunResult get_run{};
        const bool get_ok = commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::SettingGet),
                                              management_utils::buildSettingGetByKeyPayload(it.key),
                                              get_run,
                                              submit_options);
        if (get_ok) {
          std::string current{};
          if (tryDecodeSettingCurrentValue_(get_run, current)) {
            r.confirmed = (current == it.value);
          } else {
            r.confirmed = false;
          }
        }
        if (!r.confirmed) {
          all_ok = false;
          if (!r.has_error) {
            r.has_error = true;
            r.error.code = static_cast<uint16_t>(ManagementStatus::InternalError);
            r.error.stage = "decode";
            r.error.message = "confirm mismatch";
            r.error.retryable = false;
          }
          if (options.stop_on_error) {
            out_results.push_back(r);
            break;
          }
        }
      }

      if (!r.applied) {
        all_ok = false;
        if (options.stop_on_error) {
          out_results.push_back(r);
          break;
        }
      }
      out_results.push_back(r);
    }

    if (options.refresh_cache && any_mutation_submitted) {
      CommandRunResult refresh_run{};
      (void)commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::NodeBundleGet),
                              management_utils::buildNodeBundleGetPayload(kNodeBundleMaskSettings),
                              refresh_run,
                              submit_options);
    }
    return all_ok;
  }

  /** @brief Overload with default batch options. */
  bool settingsSetBatch(const MacAddress& peer,
                        const std::vector<SettingsBatchItem>& items,
                        std::vector<SettingsBatchResultItem>& out_results) {
    SettingsBatchOptions opts{};
    opts.confirm = batch_confirm_default_;
    opts.refresh_cache = batch_refresh_cache_default_;
    opts.timeout_ms = orchestration_wait_default_ms_;
    return settingsSetBatch(peer, items, out_results, opts);
  }

  /**
   * @brief Build one frontend-friendly snapshot in a single call.
   *
   * Uses one `NodeBundleGet` pull for target peer, then returns merged
   * cached view plus resolved settings (no legacy fallback fan-out).
   */
  bool nodeSnapshotGet(const MacAddress& peer,
                       NodeSnapshotView& out_snapshot,
                       uint32_t timeout_ms = 0,
                       bool include_liveness = true,
                       bool include_time = true,
                       bool include_telemetry = true) {
    out_snapshot = NodeSnapshotView{};
    out_snapshot.peer = peer;
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;

    uint8_t bundle_mask = static_cast<uint8_t>(kNodeBundleMaskDevice | kNodeBundleMaskSettings);
    if (include_liveness) {
      bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskLiveness);
    }
    if (include_time) {
      bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskTime);
    }
    if (include_telemetry) {
      bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskTelemetry);
    }

    CommandRunResult bundle_run{};
    const bool bundle_ok =
        commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::NodeBundleGet),
                          management_utils::buildNodeBundleGetPayload(bundle_mask),
                          bundle_run,
                          submit_options);
    if (bundle_ok) {
      out_snapshot.desc_run = bundle_run;
      out_snapshot.settings_run = bundle_run;
      if (include_liveness) {
        out_snapshot.liveness_run = bundle_run;
      }
      if (include_time) {
        out_snapshot.time_run = bundle_run;
      }
      if (include_telemetry) {
        out_snapshot.telemetry_run = bundle_run;
      }

      (void)drainToCache(64U, 128U);
      out_snapshot.has_cached_node = cachedNode(peer, out_snapshot.node);
      (void)cachedSettingsResolved(peer, out_snapshot.resolved_settings);
      if (out_snapshot.has_cached_node &&
          out_snapshot.node.settings_completeness != SettingsCacheCompleteness::Full) {
        out_snapshot.resolved_settings.clear();
      }

      out_snapshot.desc_ok = out_snapshot.has_cached_node && out_snapshot.node.has_device;
      out_snapshot.settings_ok = out_snapshot.has_cached_node &&
                                 out_snapshot.node.has_settings &&
                                 out_snapshot.node.settings_completeness == SettingsCacheCompleteness::Full;
      out_snapshot.liveness_ok = !include_liveness || (out_snapshot.has_cached_node && out_snapshot.node.has_liveness);
      out_snapshot.time_ok = !include_time || (out_snapshot.has_cached_node && out_snapshot.node.has_time);
      out_snapshot.telemetry_ok = !include_telemetry || (out_snapshot.has_cached_node && out_snapshot.node.has_telemetry);

      bool ok = out_snapshot.desc_ok && out_snapshot.settings_ok;
      if (include_liveness) ok = ok && out_snapshot.liveness_ok;
      if (include_time) ok = ok && out_snapshot.time_ok;
      if (include_telemetry) ok = ok && out_snapshot.telemetry_ok;
      return ok;
    }
    // No legacy fallback calls. Return current cache state when available.
    out_snapshot.desc_run = bundle_run;
    out_snapshot.settings_run = bundle_run;
    if (include_liveness) {
      out_snapshot.liveness_run = bundle_run;
    }
    if (include_time) {
      out_snapshot.time_run = bundle_run;
    }
    if (include_telemetry) {
      out_snapshot.telemetry_run = bundle_run;
    }
    out_snapshot.has_cached_node = cachedNode(peer, out_snapshot.node);
    (void)cachedSettingsResolved(peer, out_snapshot.resolved_settings);
    if (out_snapshot.has_cached_node &&
        out_snapshot.node.settings_completeness != SettingsCacheCompleteness::Full) {
      out_snapshot.resolved_settings.clear();
    }
    out_snapshot.desc_ok = out_snapshot.has_cached_node && out_snapshot.node.has_device;
    out_snapshot.settings_ok = out_snapshot.has_cached_node &&
                               out_snapshot.node.has_settings &&
                               out_snapshot.node.settings_completeness == SettingsCacheCompleteness::Full;
    out_snapshot.liveness_ok = !include_liveness || (out_snapshot.has_cached_node && out_snapshot.node.has_liveness);
    out_snapshot.time_ok = !include_time || (out_snapshot.has_cached_node && out_snapshot.node.has_time);
    out_snapshot.telemetry_ok = !include_telemetry || (out_snapshot.has_cached_node && out_snapshot.node.has_telemetry);

    bool ok = out_snapshot.desc_ok && out_snapshot.settings_ok;
    if (include_liveness) ok = ok && out_snapshot.liveness_ok;
    if (include_time) ok = ok && out_snapshot.time_ok;
    if (include_telemetry) ok = ok && out_snapshot.telemetry_ok;
    return ok;
  }

  /**
   * @brief Start/update SEMU child telemetry push for one explicit peer.
   *
   * Per-peer child state is tracked inside the adapter so different peers never
   * share one implicit child-mask cache.
   */
  bool semuChildPushStart(const MacAddress& peer,
                          uint8_t child_vid,
                          TelemetryPushMode mode = TelemetryPushMode::Hybrid,
                          uint32_t interval_ms = 2000U,
                          float delta_abs = 0.10f,
                          uint32_t gap_ms = 200U,
                          uint32_t* out_req_id = nullptr,
                          uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    if (child_vid > 7U) return false;
    ChildPushPeerState& state = ensureChildPushPeerState_(peer);
    const uint8_t old_mask = state.semu_mask;
    const TelemetryPushMode old_mode = state.semu_mode;
    const uint32_t old_interval = state.semu_interval_ms;
    const float old_delta = state.semu_delta_abs;
    const uint32_t old_gap = state.semu_gap_ms;

    state.semu_mode = mode;
    state.semu_interval_ms = interval_ms;
    state.semu_delta_abs = delta_abs;
    state.semu_gap_ms = gap_ms;
    state.semu_mask = static_cast<uint8_t>(state.semu_mask | static_cast<uint8_t>(1U << child_vid));

    TelemetryPushCommand cmd{};
    if (!buildSemuChildPushCommand_(state.semu_mask,
                                    (old_mask == 0U) ? TelemetryPushAction::Start : TelemetryPushAction::Update,
                                    state.semu_mode,
                                    state.semu_interval_ms,
                                    state.semu_delta_abs,
                                    state.semu_gap_ms,
                                    cmd)) {
      state.semu_mask = old_mask;
      state.semu_mode = old_mode;
      state.semu_interval_ms = old_interval;
      state.semu_delta_abs = old_delta;
      state.semu_gap_ms = old_gap;
      return false;
    }
    const CommandLifecycleResult run = pushControl(peer, cmd, timeout_ms, false);
    if (!run.accepted) {
      state.semu_mask = old_mask;
      state.semu_mode = old_mode;
      state.semu_interval_ms = old_interval;
      state.semu_delta_abs = old_delta;
      state.semu_gap_ms = old_gap;
      return false;
    }
    if (out_req_id != nullptr) {
      *out_req_id = run.req_id;
    }
    return true;
  }

  /**
   * @brief Start/update REMU child telemetry push for one explicit peer.
   *
   * Per-peer child state is tracked inside the adapter so different peers never
   * share one implicit child-mask cache.
   */
  bool remuChildPushStart(const MacAddress& peer,
                          uint8_t child_vid,
                          TelemetryPushMode mode = TelemetryPushMode::Hybrid,
                          uint32_t interval_ms = 2000U,
                          float delta_abs = 0.10f,
                          uint32_t gap_ms = 200U,
                          uint32_t* out_req_id = nullptr,
                          uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    if (child_vid > 15U) return false;
    ChildPushPeerState& state = ensureChildPushPeerState_(peer);
    const uint16_t old_mask = state.remu_mask;
    const TelemetryPushMode old_mode = state.remu_mode;
    const uint32_t old_interval = state.remu_interval_ms;
    const float old_delta = state.remu_delta_abs;
    const uint32_t old_gap = state.remu_gap_ms;

    state.remu_mode = mode;
    state.remu_interval_ms = interval_ms;
    state.remu_delta_abs = delta_abs;
    state.remu_gap_ms = gap_ms;
    state.remu_mask = static_cast<uint16_t>(state.remu_mask | static_cast<uint16_t>(1U << child_vid));

    TelemetryPushCommand cmd{};
    if (!buildRemuChildPushCommand_(state.remu_mask,
                                    (old_mask == 0U) ? TelemetryPushAction::Start : TelemetryPushAction::Update,
                                    state.remu_mode,
                                    state.remu_interval_ms,
                                    state.remu_delta_abs,
                                    state.remu_gap_ms,
                                    cmd)) {
      state.remu_mask = old_mask;
      state.remu_mode = old_mode;
      state.remu_interval_ms = old_interval;
      state.remu_delta_abs = old_delta;
      state.remu_gap_ms = old_gap;
      return false;
    }
    const CommandLifecycleResult run = pushControl(peer, cmd, timeout_ms, false);
    if (!run.accepted) {
      state.remu_mask = old_mask;
      state.remu_mode = old_mode;
      state.remu_interval_ms = old_interval;
      state.remu_delta_abs = old_delta;
      state.remu_gap_ms = old_gap;
      return false;
    }
    if (out_req_id != nullptr) {
      *out_req_id = run.req_id;
    }
    return true;
  }

  /**
   * @brief Stop one SEMU child from telemetry push for one explicit peer.
   *
   * Emits `PushUpdate` when at least one child remains active; otherwise emits `PushStop`.
   */
  bool semuChildPushStop(const MacAddress& peer,
                         uint8_t child_vid,
                         uint32_t* out_req_id = nullptr,
                         uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    if (child_vid > 7U) return false;
    ChildPushPeerState* state = findChildPushPeerState_(peer);
    if (state == nullptr) {
      return true;
    }
    const uint8_t old_mask = state->semu_mask;
    const uint8_t bit = static_cast<uint8_t>(1U << child_vid);
    if ((old_mask & bit) == 0U) {
      return true;  // already stopped
    }
    state->semu_mask = static_cast<uint8_t>(old_mask & static_cast<uint8_t>(~bit));

    TelemetryPushCommand cmd{};
    if (state->semu_mask == 0U) {
      cmd.action = TelemetryPushAction::Stop;
      cmd.config.stream_id = 1;
    } else if (!buildSemuChildPushCommand_(state->semu_mask,
                                           TelemetryPushAction::Update,
                                           state->semu_mode,
                                           state->semu_interval_ms,
                                           state->semu_delta_abs,
                                           state->semu_gap_ms,
                                           cmd)) {
      state->semu_mask = old_mask;
      return false;
    }

    const CommandLifecycleResult run = pushControl(peer, cmd, timeout_ms, false);
    if (!run.accepted) {
      state->semu_mask = old_mask;
      return false;
    }
    if (out_req_id != nullptr) {
      *out_req_id = run.req_id;
    }
    if (state->semu_mask == 0U && state->remu_mask == 0U) {
      removeChildPushPeerState_(peer);
    }
    return true;
  }

  /**
   * @brief Stop one REMU child from telemetry push for one explicit peer.
   *
   * Emits `PushUpdate` when at least one child remains active; otherwise emits `PushStop`.
   */
  bool remuChildPushStop(const MacAddress& peer,
                         uint8_t child_vid,
                         uint32_t* out_req_id = nullptr,
                         uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    if (child_vid > 15U) return false;
    ChildPushPeerState* state = findChildPushPeerState_(peer);
    if (state == nullptr) {
      return true;
    }
    const uint16_t old_mask = state->remu_mask;
    const uint16_t bit = static_cast<uint16_t>(1U << child_vid);
    if ((old_mask & bit) == 0U) {
      return true;  // already stopped
    }
    state->remu_mask = static_cast<uint16_t>(old_mask & static_cast<uint16_t>(~bit));

    TelemetryPushCommand cmd{};
    if (state->remu_mask == 0U) {
      cmd.action = TelemetryPushAction::Stop;
      cmd.config.stream_id = 1;
    } else if (!buildRemuChildPushCommand_(state->remu_mask,
                                           TelemetryPushAction::Update,
                                           state->remu_mode,
                                           state->remu_interval_ms,
                                           state->remu_delta_abs,
                                           state->remu_gap_ms,
                                           cmd)) {
      state->remu_mask = old_mask;
      return false;
    }

    const CommandLifecycleResult run = pushControl(peer, cmd, timeout_ms, false);
    if (!run.accepted) {
      state->remu_mask = old_mask;
      return false;
    }
    if (out_req_id != nullptr) {
      *out_req_id = run.req_id;
    }
    if (state->semu_mask == 0U && state->remu_mask == 0U) {
      removeChildPushPeerState_(peer);
    }
    return true;
  }

  /** @brief Clear adapter-side SEMU child push state cache for one explicit peer. */
  void semuChildPushResetState(const MacAddress& peer) {
    if (!enforceOwnerContext_()) return;
    ChildPushPeerState* state = findChildPushPeerState_(peer);
    if (state == nullptr) return;
    state->semu_mask = 0U;
    state->semu_mode = TelemetryPushMode::Hybrid;
    state->semu_interval_ms = 2000U;
    state->semu_delta_abs = 0.10f;
    state->semu_gap_ms = 200U;
    if (state->remu_mask == 0U) {
      removeChildPushPeerState_(peer);
    }
  }

  /** @brief Return currently active SEMU child push bitmask tracked for one explicit peer. */
  uint8_t semuChildPushMask(const MacAddress& peer) const {
    const ChildPushPeerState* state = findChildPushPeerState_(peer);
    return (state != nullptr) ? state->semu_mask : 0U;
  }

  /** @brief Clear adapter-side REMU child push state cache for one explicit peer. */
  void remuChildPushResetState(const MacAddress& peer) {
    if (!enforceOwnerContext_()) return;
    ChildPushPeerState* state = findChildPushPeerState_(peer);
    if (state == nullptr) return;
    state->remu_mask = 0U;
    state->remu_mode = TelemetryPushMode::Hybrid;
    state->remu_interval_ms = 2000U;
    state->remu_delta_abs = 0.10f;
    state->remu_gap_ms = 200U;
    if (state->semu_mask == 0U) {
      removeChildPushPeerState_(peer);
    }
  }

  /** @brief Return currently active REMU child push bitmask tracked for one explicit peer. */
  uint16_t remuChildPushMask(const MacAddress& peer) const {
    const ChildPushPeerState* state = findChildPushPeerState_(peer);
    return (state != nullptr) ? state->remu_mask : 0U;
  }

  /**
   * @brief Convenience pull wrapper for SEMU child view.
   *
   * Wire request remains a normal telemetry pull; caller can apply
   * `filterSemuChildTelemetry` on decoded samples.
   */
  bool semuChildTelemetryPull(const MacAddress& peer,
                              uint8_t child_vid,
                              uint32_t* out_req_id = nullptr,
                              uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    if (child_vid > 7U) return false;
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    const ManagementController::SubmitResult submit =
        controller_.submit(static_cast<uint16_t>(ManagementCommandId::TelemPull), {}, submit_options);
    if (out_req_id != nullptr) {
      *out_req_id = submit.req_id;
    }
    return submit.accepted;
  }

  /**
   * @brief Convenience pull wrapper for REMU child view.
   *
   * Wire request remains a normal telemetry pull; caller can apply
   * `filterRemuChildTelemetry` on decoded samples.
   */
  bool remuChildTelemetryPull(const MacAddress& peer,
                              uint8_t child_vid,
                              uint32_t* out_req_id = nullptr,
                              uint32_t timeout_ms = 0) {
    if (!enforceOwnerContext_()) return false;
    if (child_vid > 15U) return false;
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    submit_options.has_target_peer = true;
    submit_options.target_peer = peer;
    const ManagementController::SubmitResult submit =
        controller_.submit(static_cast<uint16_t>(ManagementCommandId::TelemPull), {}, submit_options);
    if (out_req_id != nullptr) {
      *out_req_id = submit.req_id;
    }
    return submit.accepted;
  }

  /**
   * @brief Filter telemetry samples to global metrics + one SEMU child (`v<vid>.*`).
   */
  static void filterSemuChildTelemetry(const std::vector<TelemetrySample>& input,
                                       uint8_t child_vid,
                                       std::vector<TelemetrySample>& output) {
    output.clear();
    if (child_vid > 7U) return;
    for (const auto& s : input) {
      if (s.key.size() < 4U || s.key[0] != 'v') {
        output.push_back(s);  // global metric
        continue;
      }
      const size_t dot = s.key.find('.');
      if (dot == std::string::npos || dot <= 1U) {
        output.push_back(s);  // treat malformed as global
        continue;
      }
      bool digits_only = true;
      for (size_t i = 1U; i < dot; ++i) {
        const char c = s.key[i];
        if (c < '0' || c > '9') {
          digits_only = false;
          break;
        }
      }
      if (!digits_only) {
        output.push_back(s);
        continue;
      }
      const unsigned long parsed = std::strtoul(s.key.substr(1U, dot - 1U).c_str(), nullptr, 10);
      if (parsed == static_cast<unsigned long>(child_vid)) {
        output.push_back(s);
      }
    }
  }

  /**
   * @brief Filter telemetry samples to global metrics + one REMU child (`v<vid>.*`).
   */
  static void filterRemuChildTelemetry(const std::vector<TelemetrySample>& input,
                                       uint8_t child_vid,
                                       std::vector<TelemetrySample>& output) {
    output.clear();
    if (child_vid > 15U) return;
    for (const auto& s : input) {
      if (s.key.size() < 4U || s.key[0] != 'v') {
        output.push_back(s);
        continue;
      }
      const size_t dot = s.key.find('.');
      if (dot == std::string::npos || dot <= 1U) {
        output.push_back(s);
        continue;
      }
      bool digits_only = true;
      for (size_t i = 1U; i < dot; ++i) {
        const char c = s.key[i];
        if (c < '0' || c > '9') {
          digits_only = false;
          break;
        }
      }
      if (!digits_only) {
        output.push_back(s);
        continue;
      }
      const unsigned long parsed = std::strtoul(s.key.substr(1U, dot - 1U).c_str(), nullptr, 10);
      if (parsed == static_cast<unsigned long>(child_vid)) {
        output.push_back(s);
      }
    }
  }

  /** @brief Convenience wrapper for remote/local archive list command. */
  bool otaArchiveList(char role = 'm',
                      uint32_t* out_req_id = nullptr,
                      uint32_t timeout_ms = 0,
                      bool remote = false) {
    return controller_.otaArchiveList(role, out_req_id, timeout_ms, remote);
  }

  /** @brief Convenience wrapper for archive save-running command. */
  bool otaArchiveSaveRunning(char role = 'm',
                             uint32_t* out_req_id = nullptr,
                             uint32_t timeout_ms = 0,
                             bool remote = false) {
    return controller_.otaArchiveSaveRunning(role, out_req_id, timeout_ms, remote);
  }

  /** @brief Convenience wrapper for archive save-staged command. */
  bool otaArchiveSaveStaged(char role = 'm',
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0,
                            bool remote = false) {
    return controller_.otaArchiveSaveStaged(role, out_req_id, timeout_ms, remote);
  }

  /** @brief Convenience wrapper for archive restore command. */
  bool otaArchiveRestore(const std::string& id,
                         char role = 'm',
                         uint32_t* out_req_id = nullptr,
                         uint32_t timeout_ms = 0,
                         bool remote = false) {
    return controller_.otaArchiveRestore(id, role, out_req_id, timeout_ms, remote);
  }

  /** @brief Convenience wrapper for archive delete command. */
  bool otaArchiveDelete(const std::string& id,
                        char role = 'm',
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0,
                        bool remote = false) {
    return controller_.otaArchiveDelete(id, role, out_req_id, timeout_ms, remote);
  }

  /** @brief Convenience wrapper for archive clear command. */
  bool otaArchiveClear(char role = 'm',
                       uint32_t* out_req_id = nullptr,
                       uint32_t timeout_ms = 0,
                       bool remote = false) {
    return controller_.otaArchiveClear(role, out_req_id, timeout_ms, remote);
  }

  /** @brief Convenience wrapper for archive verify command. */
  bool otaArchiveVerify(const std::string& id,
                        char role = 'm',
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0,
                        bool remote = false) {
    return controller_.otaArchiveVerify(id, role, out_req_id, timeout_ms, remote);
  }

  /** @brief Convenience wrapper for local master staged OTA apply command. */
  bool otaUpdateMasterStart(const std::string& local_path,
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0) {
    return controller_.otaUpdateMasterStart(local_path, out_req_id, timeout_ms);
  }

  /** @brief Convenience wrapper for storage info query (`StorageInfoGet`). */
  bool storageInfoGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.storageInfoGet(out_req_id, timeout_ms);
  }

  /** @brief Convenience wrapper for storage folder listing (`StorageList`). */
  bool storageList(const std::string& path, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.storageList(path, out_req_id, timeout_ms);
  }

  /** @brief Convenience wrapper for storage stat query (`StorageStat`). */
  bool storageStat(const std::string& path, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.storageStat(path, out_req_id, timeout_ms);
  }

  /** @brief Convenience wrapper for storage format command (`StorageFormat`). */
  bool storageFormat(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return controller_.storageFormat(out_req_id, timeout_ms);
  }

  /**
   * @brief Low-level raw response poll (queue mode only).
   *
   * This call does not update adapter status/cache.
   * Use `pollResponseCached(...)`, `drainToCache(...)`, or `ingestResponse(...)`
   * for frontend-safe state convergence.
   *
   * When `ESPNOW_LINK_FRONTEND_ADAPTER_STRICT_RAW_POLL=1`, this method returns
   * `false` to discourage raw polling paths.
   */
  bool pollResponse(ManagementResponse& out_response) {
    if (!enforceOwnerContext_()) return false;
#if ESPNOW_LINK_FRONTEND_ADAPTER_STRICT_RAW_POLL
    (void)out_response;
    return false;
#else
    return pollResponseRaw_(out_response);
#endif
  }

  /**
   * @brief Low-level raw event poll (queue mode only).
   *
   * This call does not update adapter status/cache.
   * Use `pollEventCached(...)`, `drainToCache(...)`, or `ingestEvent(...)`
   * for frontend-safe state convergence.
   *
   * When `ESPNOW_LINK_FRONTEND_ADAPTER_STRICT_RAW_POLL=1`, this method returns
   * `false` to discourage raw polling paths.
   */
  bool pollEvent(ManagementEvent& out_event) {
    if (!enforceOwnerContext_()) return false;
#if ESPNOW_LINK_FRONTEND_ADAPTER_STRICT_RAW_POLL
    (void)out_event;
    return false;
#else
    return pollEventRaw_(out_event);
#endif
  }

  /**
   * @brief Ingest one externally polled response into adapter status/cache.
   * @return true when cache accepted/updated payload.
   */
  bool ingestResponse(const ManagementResponse& response) {
    if (!enforceOwnerContext_()) return false;
    if (isStaleTrackedRequest_(response.req_id)) return false;
    trackResponse_(response);
    const bool ingested = cacheIngestResponse(response);
    return ingested;
  }

  /**
   * @brief Ingest one externally polled event into adapter status/cache.
   * @return true when cache accepted/updated payload.
   */
  bool ingestEvent(const ManagementEvent& event) {
    if (!enforceOwnerContext_()) return false;
    if (isStaleTrackedRequest_(event.req_id)) return false;
    trackEvent_(event);
    const bool ingested = cacheIngestEvent(event);
    return ingested;
  }

  /**
   * @brief Poll one response and automatically merge it into frontend cache.
   * @return true when a response was available.
   */
  bool pollResponseCached(ManagementResponse& out_response) {
    if (!enforceOwnerContext_()) return false;
    if (!pollResponseRaw_(out_response)) return false;
    (void)ingestResponse(out_response);
    return true;
  }

  /**
   * @brief Poll one event and automatically merge it into frontend cache.
   * @return true when an event was available.
   */
  bool pollEventCached(ManagementEvent& out_event) {
    if (!enforceOwnerContext_()) return false;
    if (!pollEventRaw_(out_event)) return false;
    (void)ingestEvent(out_event);
    return true;
  }

  /**
   * @brief Drain pending response/event queues into cache.
   * @param max_responses Upper bound of responses to consume this call.
   * @param max_events Upper bound of events to consume this call.
   * @return total consumed frames.
   */
  size_t drainToCache(size_t max_responses = 64U, size_t max_events = 128U) {
    if (!enforceOwnerContext_()) return 0U;
    size_t consumed = 0U;
    ManagementResponse resp{};
    while (consumed < max_responses && pollResponseRaw_(resp)) {
      (void)ingestResponse(resp);
      ++consumed;
    }
    size_t event_count = 0U;
    ManagementEvent evt{};
    while (event_count < max_events && pollEventRaw_(evt)) {
      (void)ingestEvent(evt);
      ++event_count;
      ++consumed;
    }
    return consumed;
  }

  /** @brief Clear frontend auto-cache state. */
  void cacheClear() {
    if (!enforceOwnerContext_()) return;
    cached_discovered_peers_.clear();
    cached_paired_peers_.clear();
    cached_nodes_.clear();
    req_status_.clear();
    req_status_index_.clear();
    mutation_lane_claims_.clear();
    event_ring_.clear();
    event_seq_ = 0U;
    status_update_seq_ = 0U;
    cache_update_seq_ = 0U;
    paired_generation_ = 0U;
    discovery_generation_ = 0U;
    req_epoch_map_.clear();
  }

  /** @brief Read adapter-side paired/discovery generation counters. */
  void stateGenerationsGet(StateGenerations& out_generations) const {
    out_generations.paired_generation = paired_generation_;
    out_generations.discovery_generation = discovery_generation_;
  }

  /** @brief Read paired-state generation counter. */
  uint64_t pairedGeneration() const { return paired_generation_; }

  /** @brief Read discovery-state generation counter. */
  uint64_t discoveryGeneration() const { return discovery_generation_; }

  /** @brief Read cached discovery peer list (hint stream view). */
  const std::vector<MacAddress>& cachedDiscoveredPeers() const { return cached_discovered_peers_; }

  /** @brief Read cached paired peer list. */
  const std::vector<MacAddress>& cachedPairedPeers() const { return cached_paired_peers_; }

  /** @brief Read one cached node snapshot by peer MAC. */
  bool cachedNode(const MacAddress& peer, CachedNodeSnapshot& out_node) const {
    const CachedNodeSnapshot* node = findCachedNode_(peer);
    if (node == nullptr) return false;
    out_node = *node;
    return true;
  }

  /** @brief Read all cached node snapshots. */
  void cachedNodes(std::vector<CachedNodeSnapshot>& out_nodes) const { out_nodes = cached_nodes_; }

  /**
   * @brief Read one resolved cached setting (`current` preferred, else `default`).
   *
   * Returns false when the peer or key is not present in cache.
   */
  bool cachedSettingResolved(const MacAddress& peer,
                             const std::string& key,
                             CachedSettingView& out_setting) const {
    const CachedNodeSnapshot* node = findCachedNode_(peer);
    if (node == nullptr || !node->has_settings) return false;
    for (const auto& s : node->settings) {
      if (!key.empty() && s.key != key) continue;
      out_setting.setting_id = s.setting_id;
      out_setting.key = s.key;
      out_setting.value_type = s.value_type;
      out_setting.writable = s.writable;
      out_setting.current_value = s.current_value;
      out_setting.default_value = s.default_value;
      out_setting.has_current = !s.current_value.empty();
      out_setting.has_default = !s.default_value.empty();
      out_setting.from_default = !out_setting.has_current && out_setting.has_default;
      out_setting.value = out_setting.from_default ? s.default_value : s.current_value;
      return true;
    }
    return false;
  }

  /**
   * @brief Read all resolved cached settings for one peer (`current` preferred, else `default`).
   */
  bool cachedSettingsResolved(const MacAddress& peer,
                              std::vector<CachedSettingView>& out_settings) const {
    out_settings.clear();
    const CachedNodeSnapshot* node = findCachedNode_(peer);
    if (node == nullptr || !node->has_settings) return false;
    out_settings.reserve(node->settings.size());
    for (const auto& s : node->settings) {
      CachedSettingView v{};
      v.setting_id = s.setting_id;
      v.key = s.key;
      v.value_type = s.value_type;
      v.writable = s.writable;
      v.current_value = s.current_value;
      v.default_value = s.default_value;
      v.has_current = !s.current_value.empty();
      v.has_default = !s.default_value.empty();
      v.from_default = !v.has_current && v.has_default;
      v.value = v.from_default ? s.default_value : s.current_value;
      out_settings.push_back(v);
    }
    return true;
  }

  /** @brief Merge one management response into frontend auto-cache. */
  bool cacheIngestResponse(const ManagementResponse& response) {
    if (response.cmd_id == static_cast<uint16_t>(ManagementCommandId::DiscoverySnapshotGet) &&
        response.status == ManagementStatus::Ok) {
      std::vector<MacAddress> peers{};
      if (!management_utils::parseDiscoverySnapshotPayload(response.payload, peers)) {
        return false;
      }
      if (cached_discovered_peers_ != peers) {
        cached_discovered_peers_ = peers;
        ++discovery_generation_;
      }
      return true;
    }

    if (response.cmd_id == static_cast<uint16_t>(ManagementCommandId::PairedSnapshotGet) &&
        response.status == ManagementStatus::Ok) {
      std::vector<MacAddress> peers{};
      if (!management_utils::parsePairedSnapshotPayload(response.payload, peers)) {
        return false;
      }
      const bool changed = (cached_paired_peers_ != peers);
      cached_paired_peers_ = peers;
      for (const auto& p : cached_paired_peers_) {
        (void)ensureCachedNode_(p);
      }
      // Paired snapshot is authoritative; drop cached nodes no longer paired.
      cached_nodes_.erase(std::remove_if(cached_nodes_.begin(),
                                         cached_nodes_.end(),
                                         [&](const CachedNodeSnapshot& n) {
                                           return std::find(cached_paired_peers_.begin(),
                                                            cached_paired_peers_.end(),
                                                            n.peer) == cached_paired_peers_.end();
                                         }),
                          cached_nodes_.end());
      if (changed) {
        ++paired_generation_;
      }
      return true;
    }

    if (response.cmd_id == static_cast<uint16_t>(ManagementCommandId::RemovePeerRequest) &&
        response.status == ManagementStatus::Ok &&
        response.has_executed_peer) {
      removeCachedPeer_(response.executed_peer);
      return true;
    }

    // Paged descriptor flows emit intermediate chunks as OkDeferred.
    // They must still be merged into cache, otherwise only the terminal page remains.
    const bool status_allows_descriptor_ingest =
        (response.status == ManagementStatus::Ok || response.status == ManagementStatus::OkDeferred);
    if (!status_allows_descriptor_ingest) {
      const bool settings_related_cmd =
          (response.cmd_id == static_cast<uint16_t>(ManagementCommandId::NodeBundleGet)) ||
          (response.cmd_id == static_cast<uint16_t>(ManagementCommandId::SettingGet));
      if (settings_related_cmd) {
        MacAddress failed_peer{};
        if (resolvePeerForCache_(response, failed_peer)) {
          CachedNodeSnapshot& node = ensureCachedNode_(failed_peer);
          if (node.settings_refresh_inflight &&
              (node.settings_refresh_req_id == 0U || node.settings_refresh_req_id == response.req_id)) {
            markSettingsRefreshDone_(node,
                                     response.status,
                                     management_utils::managementStatusToString(response.status));
          }
        }
      }
      return false;
    }

    if (response.payload.empty()) {
      return false;
    }

    DescriptorResponse desc{};
    if (!decodeDescriptorResponse(response.payload.data(), response.payload.size(), desc)) {
      return false;
    }

    MacAddress peer{};
    if (!resolvePeerForCache_(response, peer)) {
      return false;
    }
    CachedNodeSnapshot& node = ensureCachedNode_(peer);
    ++cache_update_seq_;
    node.update_seq = cache_update_seq_;
    switch (desc.type) {
      case DescriptorResponseType::Device:
        node.has_device = true;
        node.device = desc.device;
        return true;
      case DescriptorResponseType::Liveness:
        node.has_liveness = true;
        node.liveness = desc.liveness;
        return true;
      case DescriptorResponseType::Time:
        node.has_time = true;
        node.time = desc.time;
        return true;
      case DescriptorResponseType::Settings:
        if (!desc.is_paged) {
          node.settings = desc.settings;
        } else {
          if (desc.cursor == 0U) {
            node.settings.clear();
          }
          for (const auto& st : desc.settings) {
            upsertCachedSetting_(node.settings, st);
          }
          if (desc.done && desc.total_count == 0U) {
            node.settings.clear();
          }
        }
        node.has_settings = !node.settings.empty();
        node.settings_seq = node.update_seq;
        node.settings_completeness = settingsCompletenessFromPaging_(desc, node.settings);
        node.settings_updated_ms = monotonicMs_();
        node.settings_last_status = ManagementStatus::Ok;
        node.settings_last_error.clear();
        if (!desc.is_paged || desc.done) {
          markSettingsRefreshDone_(node, ManagementStatus::Ok);
        }
        return true;
      case DescriptorResponseType::Setting:
        node.has_settings = true;
        upsertCachedSetting_(node.settings, desc.setting);
        node.settings_seq = node.update_seq;
        if (node.settings_completeness == SettingsCacheCompleteness::Empty) {
          node.settings_completeness = SettingsCacheCompleteness::Partial;
        }
        node.settings_updated_ms = monotonicMs_();
        node.settings_last_status = ManagementStatus::Ok;
        node.settings_last_error.clear();
        markSettingsRefreshDone_(node, ManagementStatus::Ok);
        return true;
      case DescriptorResponseType::TelemetrySnapshot:
        node.has_telemetry = true;
        node.telemetry_samples = desc.telemetry_samples;
        return true;
      case DescriptorResponseType::NodeBundle: {
        uint8_t bundle_mask = desc.bundle_mask;
        if (bundle_mask == 0U) {
          if (!desc.device.device_type.empty() ||
              !desc.device.device_id.empty() ||
              !desc.device.device_name.empty() ||
              !desc.device.hw_version.empty() ||
              !desc.device.sw_version.empty() ||
              !desc.device.build_id.empty()) {
            bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskDevice);
          }
          if (!desc.liveness.state.empty() || desc.liveness.uptime_ms != 0U || desc.liveness.online) {
            bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskLiveness);
          }
          if (desc.time.epoch_s != 0ULL || desc.time.uptime_ms != 0U) {
            bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskTime);
          }
          if (!desc.settings.empty() || desc.is_paged) {
            bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskSettings);
          }
          if (!desc.telemetry_samples.empty()) {
            bundle_mask = static_cast<uint8_t>(bundle_mask | kNodeBundleMaskTelemetry);
          }
        }
        const bool include_device = ((bundle_mask & kNodeBundleMaskDevice) != 0U);
        const bool include_liveness = ((bundle_mask & kNodeBundleMaskLiveness) != 0U);
        const bool include_time = ((bundle_mask & kNodeBundleMaskTime) != 0U);
        const bool include_settings = ((bundle_mask & kNodeBundleMaskSettings) != 0U);
        const bool include_telemetry = ((bundle_mask & kNodeBundleMaskTelemetry) != 0U);

        if (include_device) {
          node.has_device = true;
          node.device = desc.device;
        }
        if (include_liveness) {
          node.has_liveness = true;
          node.liveness = desc.liveness;
        }
        if (include_time) {
          node.has_time = true;
          node.time = desc.time;
        }
        if (include_telemetry) {
          node.has_telemetry = true;
          node.telemetry_samples = desc.telemetry_samples;
        }
        if (include_settings) {
          if (!desc.is_paged) {
            node.settings = desc.settings;
          } else {
            if (desc.cursor == 0U) {
              node.settings.clear();
            }
            for (const auto& st : desc.settings) {
              upsertCachedSetting_(node.settings, st);
            }
            if (desc.done && desc.total_count == 0U) {
              node.settings.clear();
            }
          }
          node.has_settings = !node.settings.empty();
          node.settings_seq = node.update_seq;
          node.settings_completeness = settingsCompletenessFromPaging_(desc, node.settings);
          node.settings_updated_ms = monotonicMs_();
          node.settings_last_status = ManagementStatus::Ok;
          node.settings_last_error.clear();
          if (!desc.is_paged || desc.done) {
            markSettingsRefreshDone_(node, ManagementStatus::Ok);
          }
        }
        return true;
      }
      default:
        return false;
    }
  }

  /** @brief Merge one management event into frontend auto-cache. */
  bool cacheIngestEvent(const ManagementEvent& event) {
    if (event.event_id == ManagementEventId::DiscoveryUpdate) {
      ManagementDiscoveryUpdatePayload update{};
      if (!management_utils::parseDiscoveryUpdatePayload(event.payload, update)) {
        return false;
      }
      const bool expires_entry = event.payload.size() < 8U;
      auto it = std::find(cached_discovered_peers_.begin(), cached_discovered_peers_.end(), update.peer);
      bool changed = false;
      if (expires_entry) {
        if (it != cached_discovered_peers_.end()) {
          cached_discovered_peers_.erase(it);
          changed = true;
        }
      } else if (it == cached_discovered_peers_.end()) {
        cached_discovered_peers_.push_back(update.peer);
        changed = true;
      }
      if (changed) {
        ++discovery_generation_;
      }
      return true;
    }

    if (event.event_id == ManagementEventId::PairResult) {
      if (event.status == ManagementStatus::Ok) {
        ManagementPeerMessagePayload msg{};
        if (!management_utils::parsePeerMessagePayload(event.payload, msg)) {
          return false;
        }
        if (std::find(cached_paired_peers_.begin(), cached_paired_peers_.end(), msg.peer) == cached_paired_peers_.end()) {
          cached_paired_peers_.push_back(msg.peer);
        }
        CachedNodeSnapshot& node = ensureCachedNode_(msg.peer);
        if (auto_pair_settings_bootstrap_enabled_ &&
            (!node.has_settings || node.settings_completeness == SettingsCacheCompleteness::Empty)) {
          (void)settingsBundleRefreshWithOrigin_(msg.peer,
                                                 SettingsRefreshOrigin::PairBootstrap,
                                                 nullptr,
                                                 pair_bootstrap_settings_timeout_ms_);
        }
        return true;
      }
      return false;
    }

    if (event.event_id == ManagementEventId::UnpairResult) {
      if (event.status != ManagementStatus::Ok) return false;
      ManagementPeerMessagePayload msg{};
      if (!management_utils::parsePeerMessagePayload(event.payload, msg)) {
        return false;
      }
      removeCachedPeer_(msg.peer);
      return true;
    }

    if (event.event_id == ManagementEventId::PeerRemoved) {
      ManagementPeerRemovedPayload removed{};
      if (!management_utils::parsePeerRemovedPayload(event.payload, removed) || !removed.removed) {
        return false;
      }
      removeCachedPeer_(removed.peer);
      return true;
    }
    return false;
  }

  /** @brief Decode pair-progress / pair-result / unpair-result event payload. */
  static bool decodePeerMessageEvent(const ManagementEvent& event, ManagementPeerMessagePayload& out) {
    if (event.event_id != ManagementEventId::PairProgress &&
        event.event_id != ManagementEventId::PairResult &&
        event.event_id != ManagementEventId::UnpairResult) {
      return false;
    }
    return management_utils::parsePeerMessagePayload(event.payload, out);
  }

  /** @brief Decode peer-removed event payload. */
  static bool decodePeerRemovedEvent(const ManagementEvent& event, ManagementPeerRemovedPayload& out) {
    if (event.event_id != ManagementEventId::PeerRemoved) return false;
    return management_utils::parsePeerRemovedPayload(event.payload, out);
  }

  /** @brief Decode discovery-update event payload (`peer`, RSSI, name, optional role code). */
  static bool decodeDiscoveryUpdateEvent(const ManagementEvent& event,
                                         ManagementDiscoveryUpdatePayload& out) {
    if (event.event_id != ManagementEventId::DiscoveryUpdate) {
      return false;
    }
    return management_utils::parseDiscoveryUpdatePayload(event.payload, out);
  }

  /** @brief Decode mandatory OTA event payload (`OtaTransferReady` / `OtaBootComplete`). */
  static bool decodeMandatoryEvent(const ManagementEvent& event, ManagementMandatoryEventPayload& out) {
    if (event.event_id != ManagementEventId::OtaTransferReady &&
        event.event_id != ManagementEventId::OtaBootComplete) {
      return false;
    }
    return management_utils::parseMandatoryEventPayload(event.payload, out);
  }

  /** @brief Decode OTA transfer progress/status event payload. */
  static bool decodeOtaTransferStatusEvent(const ManagementEvent& event,
                                           ManagementOtaTransferStatusPayload& out) {
    if (event.event_id != ManagementEventId::OtaTransferStatus) return false;
    return management_utils::parseOtaTransferStatusEventPayload(event.payload, out);
  }

  /** @brief Decode OTA push lifecycle completion event payload (`CmdDone/CmdFail` for `OtaPushStart`). */
  static bool decodeOtaPushResultEvent(const ManagementEvent& event,
                                       ManagementOtaPushResultPayload& out) {
    if ((event.event_id != ManagementEventId::CmdDone && event.event_id != ManagementEventId::CmdFail) ||
        event.cmd_id != static_cast<uint16_t>(ManagementCommandId::OtaPushStart)) {
      return false;
    }
    return management_utils::parseOtaPushResultPayload(event.payload, out);
  }

  /** @brief Decode OTA update lifecycle completion event payload (`CmdDone/CmdFail` for `OtaUpdateStart`). */
  static bool decodeOtaUpdateResultEvent(const ManagementEvent& event,
                                         ManagementOtaUpdateResultPayload& out) {
    if ((event.event_id != ManagementEventId::CmdDone && event.event_id != ManagementEventId::CmdFail) ||
        event.cmd_id != static_cast<uint16_t>(ManagementCommandId::OtaUpdateStart)) {
      return false;
    }
    return management_utils::parseOtaUpdateResultPayload(event.payload, out);
  }

  /** @brief Decode deferred `OtaPushStart` / `OtaUpdateStart` response payload. */
  static bool decodeOtaPushStartResponse(const ManagementResponse& response,
                                         ManagementOtaPushStartPayload& out) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::OtaPushStart) &&
        response.cmd_id != static_cast<uint16_t>(ManagementCommandId::OtaUpdateStart)) {
      return false;
    }
    return management_utils::parseOtaPushStartResponsePayload(response.payload, out);
  }

  /** @brief Decode `OtaPushStatus` response payload. */
  static bool decodeOtaPushStatusResponse(const ManagementResponse& response,
                                          ManagementOtaPushStatusPayload& out) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::OtaPushStatus)) return false;
    return management_utils::parseOtaPushStatusPayload(response.payload, out);
  }

  /** @brief Decode OTA archive response payload text (`OtaArchive*`). */
  static bool decodeOtaArchiveResponse(const ManagementResponse& response, std::string& out_message) {
    const uint16_t cmd = response.cmd_id;
    if (cmd != static_cast<uint16_t>(ManagementCommandId::OtaArchiveList) &&
        cmd != static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveRunning) &&
        cmd != static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveStaged) &&
        cmd != static_cast<uint16_t>(ManagementCommandId::OtaArchiveRestore) &&
        cmd != static_cast<uint16_t>(ManagementCommandId::OtaArchiveDelete) &&
        cmd != static_cast<uint16_t>(ManagementCommandId::OtaArchiveClear) &&
        cmd != static_cast<uint16_t>(ManagementCommandId::OtaArchiveVerify)) {
      return false;
    }
    return management_utils::parseStringPayloadU16(response.payload, out_message);
  }

  /** @brief Decode `SettingSet` response string payload. */
  static bool decodeSettingSetTextResponse(const ManagementResponse& response, std::string& out_message) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::SettingSet)) {
      return false;
    }
    return management_utils::parseStringPayloadU16(response.payload, out_message);
  }

  /** @brief Decode `SettingSet` terminal event string payload (`CmdDone/CmdFail/Timeout`). */
  static bool decodeSettingSetTextEvent(const ManagementEvent& event, std::string& out_message) {
    if ((event.event_id != ManagementEventId::CmdDone &&
         event.event_id != ManagementEventId::CmdFail &&
         event.event_id != ManagementEventId::Timeout) ||
        event.cmd_id != static_cast<uint16_t>(ManagementCommandId::SettingSet)) {
      return false;
    }
    return management_utils::parseStringPayloadU16(event.payload, out_message);
  }

  /** @brief Decode discovery snapshot response payload (`DiscoverySnapshotGet`). */
  static bool decodeDiscoverySnapshotResponse(const ManagementResponse& response,
                                              std::vector<MacAddress>& out_peers) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::DiscoverySnapshotGet)) {
      return false;
    }
    return management_utils::parseDiscoverySnapshotPayload(response.payload, out_peers);
  }

  /** @brief Decode discovery snapshot payload with RSSI/age metadata (`DiscoverySnapshotGet`). */
  static bool decodeDiscoverySnapshotResponseDetailed(
      const ManagementResponse& response,
      std::vector<ManagementDiscoveredPeerInfo>& out_peers) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::DiscoverySnapshotGet)) {
      return false;
    }
    return management_utils::parseDiscoverySnapshotPayloadDetailed(response.payload, out_peers);
  }

  /** @brief Decode paired snapshot response payload (`PairedSnapshotGet`). */
  static bool decodePairedSnapshotResponse(const ManagementResponse& response,
                                           std::vector<MacAddress>& out_peers) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::PairedSnapshotGet)) {
      return false;
    }
    return management_utils::parsePairedSnapshotPayload(response.payload, out_peers);
  }

  /** @brief Decode paired snapshot response payload with role hints (`PairedSnapshotGet`). */
  static bool decodePairedSnapshotResponseDetailed(const ManagementResponse& response,
                                                   std::vector<ManagementPairedPeerInfo>& out_peers) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::PairedSnapshotGet)) {
      return false;
    }
    return management_utils::parsePairedSnapshotPayload(response.payload, out_peers);
  }

  /** @brief Decode topology status response payload (`TopologyStatusGet`). */
  static bool decodeTopologyStatusResponse(const ManagementResponse& response,
                                           ManagementTopologyStatusPayload& out_status) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::TopologyStatusGet)) {
      return false;
    }
    return management_utils::parseTopologyStatusPayload(response.payload, out_status);
  }

  /** @brief Decode topology slots response payload (`TopologySlotsGet`). */
  static bool decodeTopologySlotsResponse(const ManagementResponse& response,
                                          uint8_t& out_state,
                                          std::vector<ManagementTopologySlotPayload>& out_slots) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::TopologySlotsGet)) {
      return false;
    }
    return management_utils::parseTopologySlotsPayload(response.payload, out_state, out_slots);
  }

  /** @brief Decode topology trigger send response payload (`TopologyTriggerSend`). */
  static bool decodeTopologyTriggerSendResponse(
      const ManagementResponse& response,
      ManagementTopologyTriggerSendResponsePayload& out_trigger_response) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::TopologyTriggerSend)) {
      return false;
    }
    return management_utils::parseTopologyTriggerSendResponsePayload(response.payload, out_trigger_response);
  }

  /** @brief Decode live monitor status response payload (`LiveMonitorStatusGet`). */
  static bool decodeLiveMonitorStatusResponse(const ManagementResponse& response,
                                              ManagementLiveMonitorStatusPayload& out_status) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::LiveMonitorStatusGet)) {
      return false;
    }
    return management_utils::parseLiveMonitorStatusPayload(response.payload, out_status);
  }

  /** @brief Decode `CliControlSet` response payload (`byte[0]` => enabled 0/1). */
  static bool decodeCliControlResponse(const ManagementResponse& response, bool& out_enabled) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::CliControlSet)) {
      return false;
    }
    if (response.payload.size() != 1U || response.payload[0] > 1U) {
      return false;
    }
    out_enabled = (response.payload[0] != 0U);
    return true;
  }

  /** @brief Decode `ChainLoopControlSet` response payload (`byte[0]` => enabled 0/1). */
  static bool decodeChainLoopControlResponse(const ManagementResponse& response, bool& out_enabled) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::ChainLoopControlSet) ||
        response.status != ManagementStatus::Ok) {
      return false;
    }
    if (response.payload.size() != 1U || response.payload[0] > 1U) {
      return false;
    }
    out_enabled = (response.payload[0] != 0U);
    return true;
  }

  /** @brief Decode `CmdDone/CmdFail` payload emitted for `ChainLoopControlSet`. */
  static bool decodeChainLoopEvent(const ManagementEvent& event,
                                   ManagementChainLoopResultPayload& out_result) {
    if ((event.event_id != ManagementEventId::CmdDone && event.event_id != ManagementEventId::CmdFail) ||
        event.cmd_id != static_cast<uint16_t>(ManagementCommandId::ChainLoopControlSet)) {
      return false;
    }
    return management_utils::parseChainLoopResultPayload(event.payload, out_result);
  }

  /** @brief Decode liveness transition event payload (`PeerLivenessTransition`). */
  static bool decodePeerLivenessTransitionEvent(const ManagementEvent& event,
                                                ManagementPeerLivenessTransitionPayload& out_transition) {
    if (event.event_id != ManagementEventId::PeerLivenessTransition) {
      return false;
    }
    return management_utils::parsePeerLivenessTransitionPayload(event.payload, out_transition);
  }

  /** @brief Decode topology trigger event payload (`TopologyTrigger*`). */
  static bool decodeTopologyTriggerEvent(const ManagementEvent& event,
                                         ManagementTopologyTriggerEventPayload& out_trigger) {
    if (event.event_id != ManagementEventId::TopologyTriggerReceived &&
        event.event_id != ManagementEventId::TopologyTriggerRejected &&
        event.event_id != ManagementEventId::TopologyTriggerAck) {
      return false;
    }
    return management_utils::parseTopologyTriggerEventPayload(event.payload, out_trigger);
  }

  /** @brief Resolve index/MAC selector against one paired list. */
  static bool resolveTargetSelector(const std::string& selector,
                                    const std::vector<MacAddress>& paired_list,
                                    MacAddress& out_peer,
                                    size_t* out_index = nullptr) {
    return management_utils::resolveTargetPeerSelector(selector, paired_list, out_peer, out_index);
  }

  /**
   * @brief Resolve selector into one explicit peer without mutating controller target state.
   */
  bool selectTargetPeer(const std::string& selector,
                        const std::vector<MacAddress>& paired_list,
                        MacAddress* out_peer = nullptr,
                        size_t* out_index = nullptr) {
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) {
      return false;
    }
    if (out_peer != nullptr) {
      *out_peer = peer;
    }
    return true;
  }

  /**
   * @brief Resolve target selector and send audio ping to that slave.
   */
  bool targetAudioPing(const std::string& selector,
                       const std::vector<MacAddress>& paired_list,
                       uint32_t* out_req_id = nullptr,
                       uint32_t timeout_ms = 0,
                       MacAddress* out_peer = nullptr,
                       size_t* out_index = nullptr) {
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) return false;
    if (out_peer != nullptr) *out_peer = peer;
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::AudioPingRequest,
                                  {},
                                  out_req_id,
                                  timeout_ms);
  }

  /**
   * @brief Resolve target selector and send restart command to that slave.
   */
  bool targetRestart(const std::string& selector,
                     const std::vector<MacAddress>& paired_list,
                     uint32_t* out_req_id = nullptr,
                     uint32_t timeout_ms = 0,
                     MacAddress* out_peer = nullptr,
                     size_t* out_index = nullptr) {
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) return false;
    if (out_peer != nullptr) *out_peer = peer;
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::RestartSlaveRequest,
                                  {},
                                  out_req_id,
                                  timeout_ms);
  }

  /**
   * @brief Resolve target selector and send reset command to that slave.
   */
  bool targetReset(const std::string& selector,
                   const std::vector<MacAddress>& paired_list,
                   uint32_t* out_req_id = nullptr,
                   uint32_t timeout_ms = 0,
                   MacAddress* out_peer = nullptr,
                   size_t* out_index = nullptr) {
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) return false;
    if (out_peer != nullptr) *out_peer = peer;
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::ResetSlaveRequest,
                                  {},
                                  out_req_id,
                                  timeout_ms);
  }

  /**
   * @brief Resolve target selector and set PMS 48V chain power.
   */
  bool targetPmsChain48vSet(const std::string& selector,
                            const std::vector<MacAddress>& paired_list,
                            bool enabled,
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0,
                            MacAddress* out_peer = nullptr,
                            size_t* out_index = nullptr) {
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) return false;
    if (out_peer != nullptr) *out_peer = peer;
    return submitTargetedCommand_(
        peer,
        ManagementCommandId::SettingSet,
        management_utils::buildSettingSetByKeyPayload("chain_48v_enable", enabled ? "1" : "0"),
        out_req_id,
        timeout_ms);
  }

  /**
   * @brief Resolve target selector and set PMS charger state.
   */
  bool targetPmsChargerSet(const std::string& selector,
                           const std::vector<MacAddress>& paired_list,
                           bool enabled,
                           uint32_t* out_req_id = nullptr,
                           uint32_t timeout_ms = 0,
                           MacAddress* out_peer = nullptr,
                           size_t* out_index = nullptr) {
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) return false;
    if (out_peer != nullptr) *out_peer = peer;
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::SettingSet,
                                  management_utils::buildSettingSetByKeyPayload(
                                      "charger_enable",
                                      enabled ? "1" : "0"),
                                  out_req_id,
                                  timeout_ms);
  }

  /**
   * @brief Resolve target selector and set relay output state.
   */
  bool targetRelayOutputSet(const std::string& selector,
                            const std::vector<MacAddress>& paired_list,
                            uint8_t relay_index,
                            bool enabled,
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0,
                            MacAddress* out_peer = nullptr,
                            size_t* out_index = nullptr) {
    if (relay_index < 1U || relay_index > 2U) {
      return false;
    }
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) return false;
    if (out_peer != nullptr) *out_peer = peer;
    const char* key = (relay_index == 1U) ? "relay1_enable" : "relay2_enable";
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::SettingSet,
                                  management_utils::buildSettingSetByKeyPayload(key, enabled ? "1" : "0"),
                                  out_req_id,
                                  timeout_ms);
  }

  /**
   * @brief Resolve target selector and set REMU child output state.
   *
   * @param child_index REMU child index in [0..15].
   */
  bool targetRemuOutputSet(const std::string& selector,
                           const std::vector<MacAddress>& paired_list,
                           uint8_t child_index,
                           bool enabled,
                           uint32_t* out_req_id = nullptr,
                           uint32_t timeout_ms = 0,
                           MacAddress* out_peer = nullptr,
                           size_t* out_index = nullptr) {
    if (child_index > 15U) {
      return false;
    }
    MacAddress peer{};
    if (!resolveTargetSelector(selector, paired_list, peer, out_index)) return false;
    if (out_peer != nullptr) *out_peer = peer;
    const std::string key = std::string("v") + std::to_string(static_cast<unsigned int>(child_index)) + ".output_enable";
    return submitTargetedCommand_(peer,
                                  ManagementCommandId::SettingSet,
                                  management_utils::buildSettingSetByKeyPayload(key, enabled ? "1" : "0"),
                                  out_req_id,
                                  timeout_ms);
  }

  /** @brief Decode logger status response payload (local/remote). */
  static bool decodeLogStatusResponse(const ManagementResponse& response,
                                      bool& out_available,
                                      bool& out_enabled,
                                      uint8_t& out_min_level,
                                      LogStorageStats& out_stats) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::LogLocalStatusGet) &&
        response.cmd_id != static_cast<uint16_t>(ManagementCommandId::LogRemoteStatusGet)) {
      return false;
    }
    return management_utils::parseLogStatusPayload(response.payload,
                                                   out_available,
                                                   out_enabled,
                                                   out_min_level,
                                                   out_stats);
  }

  /** @brief Decode logger read response payload (local/remote). */
  static bool decodeLogReadResponse(const ManagementResponse& response,
                                    uint32_t& out_offset,
                                    uint32_t& out_total_size,
                                    std::vector<uint8_t>& out_chunk) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::LogLocalRead) &&
        response.cmd_id != static_cast<uint16_t>(ManagementCommandId::LogRemoteRead)) {
      return false;
    }
    return management_utils::parseLogReadResponsePayload(response.payload,
                                                         out_offset,
                                                         out_total_size,
                                                         out_chunk);
  }

  /** @brief Decode `ChannelRuntimeGet` response payload. */
  static bool decodeChannelRuntimeStatusResponse(const ManagementResponse& response,
                                                 ManagementRuntimeChannelStatusPayload& out_status) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::ChannelRuntimeGet) ||
        response.status != ManagementStatus::Ok) {
      return false;
    }
    return management_utils::parseRuntimeChannelStatusPayload(response.payload, out_status);
  }

  /** @brief Decode `CmdDone/CmdFail` payload emitted for `ChannelSyncAll`. */
  static bool decodeChannelSyncAllEvent(const ManagementEvent& event,
                                        ManagementChannelSyncAllResultPayload& out_result) {
    if ((event.event_id != ManagementEventId::CmdDone && event.event_id != ManagementEventId::CmdFail) ||
        event.cmd_id != static_cast<uint16_t>(ManagementCommandId::ChannelSyncAll)) {
      return false;
    }
    return management_utils::parseChannelSyncAllResultPayload(event.payload, out_result);
  }

  /** @brief Decode `StorageInfoGet` response payload into descriptor response. */
  static bool decodeStorageInfoResponse(const ManagementResponse& response, DescriptorResponse& out_descriptor) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::StorageInfoGet)) {
      return false;
    }
    if (!decodeDescriptorResponse(response.payload.data(), response.payload.size(), out_descriptor)) {
      return false;
    }
    return out_descriptor.type == DescriptorResponseType::StorageInfo;
  }

  /** @brief Decode `StorageList` response payload into descriptor response. */
  static bool decodeStorageListResponse(const ManagementResponse& response, DescriptorResponse& out_descriptor) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::StorageList)) {
      return false;
    }
    if (!decodeDescriptorResponse(response.payload.data(), response.payload.size(), out_descriptor)) {
      return false;
    }
    return out_descriptor.type == DescriptorResponseType::StorageList;
  }

  /** @brief Decode `StorageStat` response payload into descriptor response. */
  static bool decodeStorageStatResponse(const ManagementResponse& response, DescriptorResponse& out_descriptor) {
    if (response.cmd_id != static_cast<uint16_t>(ManagementCommandId::StorageStat)) {
      return false;
    }
    if (!decodeDescriptorResponse(response.payload.data(), response.payload.size(), out_descriptor)) {
      return false;
    }
    return out_descriptor.type == DescriptorResponseType::StorageStat;
  }

  /** @brief Read queue depth for queue mode endpoint. */
  QueueDepth queueDepth() const {
    QueueDepth d{};
    if (transport_ == nullptr) return d;
    d.requests = transport_->pendingRequestCount();
    d.responses = transport_->pendingResponseCount();
    d.events = transport_->pendingEventCount();
    return d;
  }

  /** @brief Read aggregate busy state across transport backlog + in-flight operations. */
  BusySnapshot busySnapshot() const {
    BusySnapshot out{};
    const uint64_t now_ms = monotonicMs_();
    out.queue = queueDepth();
    out.transport_backlog =
        (out.queue.requests > 0U) || (out.queue.responses > 0U) || (out.queue.events > 0U);

    for (const auto& s : req_status_) {
      if (!s.terminal && !isTerminalState_(s.state)) {
        ++out.inflight_commands;
      }
    }
    out.command_inflight = (out.inflight_commands > 0U);
    if (service_ != nullptr && service_->topologyBusy()) {
      out.command_inflight = true;
      if (out.inflight_commands == 0U) {
        out.inflight_commands = 1U;
      }
    }

    constexpr uint64_t kRefreshBusyWindowMs = 20000U;
    for (const auto& node : cached_nodes_) {
      if (!node.settings_refresh_inflight) continue;
      const bool hasAttemptTs = (node.settings_refresh_last_attempt_ms > 0U);
      const uint64_t age_ms =
          (hasAttemptTs && now_ms >= node.settings_refresh_last_attempt_ms)
              ? (now_ms - node.settings_refresh_last_attempt_ms)
              : 0U;
      if (!hasAttemptTs || age_ms <= kRefreshBusyWindowMs) {
        ++out.settings_refresh_nodes;
      }
    }
    out.settings_refresh_inflight = (out.settings_refresh_nodes > 0U);

    // Busy reflects active command execution, transport pressure, or fresh settings refresh work.
    out.busy = out.command_inflight || out.transport_backlog || out.settings_refresh_inflight;
    out.updated_ms = now_ms;
    return out;
  }

  /** @brief Read management runtime counters when runtime is bound. */
  bool runtimeStats(ManagementRuntime::Stats& out_stats) const {
    if (runtime_ == nullptr) return false;
    out_stats = runtime_->stats();
    return true;
  }

  /**
   * @brief Pump management processing.
   *
   * Canonical path uses runtime tick only.
   */
  void tick(uint32_t now_ms,
            size_t max_requests_per_transport = 4,
            size_t max_responses = 32,
            size_t max_events = 64) {
    if (!enforceOwnerContext_()) return;
    tickRuntimeOwned_(now_ms, max_requests_per_transport, max_responses, max_events);
  }


