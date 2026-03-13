#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "espnow_link/device_manager.hpp"
#include "espnow_link/events.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/manager.hpp"
#include "espnow_link/master_pull_client.hpp"
#include "espnow_link/ota_storage.hpp"
#include "espnow_link/telemetry_push.hpp"

namespace espnow_link {

class MasterCli;

/**
 * @brief Management command executor and event hub above `EspNowManager`.
 *
 * This service receives normalized management requests (CLI/Wi-Fi/BLE/custom), executes
 * command handlers, and exposes queued responses/events for transport adapters.
 */
class ManagementService : public IEventSink {
 public:
  /**
   * @brief Construct management service with role-aware dependencies.
   * @param local_role Local node role (master/slave).
   * @param manager Core ESP-NOW manager instance.
   * @param pull_client Optional master pull helper used for descriptor/control RPC.
   * @param device_policy Optional critical-command authorization policy.
   * @param device_actions Optional deferred execution backend for critical commands.
   */
  ManagementService(Role local_role,
                    EspNowManager& manager,
                    MasterPullClient* pull_client = nullptr,
                    IDeviceManagerPolicy* device_policy = nullptr,
                    IDeviceManagerActions* device_actions = nullptr);

  /**
   * @brief Initialize internal queues/state.
   * @param max_queue_depth Maximum request/response queue length.
   */
  void begin(size_t max_queue_depth = 32);

  /**
   * @brief Submit one management request for execution.
   * @param request Request envelope.
   * @return true if request was accepted into queue.
   */
  bool submit(const ManagementRequest& request);

  /**
   * @brief Advance request processing and time-based transitions.
   * @param now_ms Monotonic time in milliseconds.
   */
  void tick(uint32_t now_ms);

  /**
   * @brief Pop one completed response from queue.
   * @param out_response Filled response output.
   * @return true when a response was available.
   */
  bool pollResponse(ManagementResponse& out_response);

  /**
   * @brief Pop one asynchronous event from queue.
   * @param out_event Filled event output.
   * @return true when an event was available.
   */
  bool pollEvent(ManagementEvent& out_event);

  /** @brief Clear request, response, and event queues. */
  void clearQueues();

  /** @brief Get number of queued requests. */
  size_t pendingRequestCount() const;

  /** @brief Get number of queued responses. */
  size_t pendingResponseCount() const;

  /** @brief Get number of queued events. */
  size_t pendingEventCount() const;

  /**
   * @brief Receive manager runtime events and translate to management events.
   * @param event Runtime event from `EspNowManager`.
   */
  void onEvent(const Event& event) override;

  /** @brief Bind local OTA staging storage backend for management-owned push orchestration. */
  void bindOtaPushStorage(IOtaStorageBackend* storage) { ota_push_storage_ = storage; }
  /** @brief Bind local master CLI control endpoint for management CLI on/off/status commands. */
  void bindMasterCli(MasterCli* cli) { master_cli_ = cli; }

  /** @brief Service-local radio transition lifecycle state. */
  enum class RadioTransitionState : uint8_t {
    Idle = 0,
    Quiescing = 1,
    Paused = 2,
    Resuming = 3,
    Failed = 4,
  };

  /** @brief Options for entering radio transition mode. */
  struct RadioTransitionBeginOptions {
    bool stop_discovery = true;
    bool disable_live_monitor = true;
    bool clear_master_update_guard = true;
    bool cancel_deferred_operations = true;
    bool cancel_pending_mutating_requests = true;
  };

  /** @brief Options for leaving radio transition mode. */
  struct RadioTransitionEndOptions {
    bool restore_live_monitor = true;
    bool sync_live_monitor_peers = true;
  };

  /** @brief Snapshot status of radio transition lifecycle. */
  struct RadioTransitionStatus {
    bool active = false;
    RadioTransitionState state = RadioTransitionState::Idle;
    uint32_t radio_epoch = 0;
    ManagementStatus last_error = ManagementStatus::Ok;
    std::string last_error_stage{};
    std::string last_error_message{};
  };

  /** @brief Options for hard radio deinit after transition preparation. */
  struct RadioHardDeinitOptions {
    bool enter_transition_if_needed = true;
    bool stop_discovery = true;
    bool disable_live_monitor = true;
    bool clear_master_update_guard = true;
    bool cancel_deferred_operations = true;
    bool cancel_pending_mutating_requests = true;
    bool clear_queues = true;
  };

  /** @brief Options for hard radio reinit after hard deinit. */
  struct RadioHardReinitOptions {
    bool restore_link = true;
    bool reset_service_state = true;
  };

  /** @brief Enter transition mode: quiesce mutating management workflows. */
  bool beginRadioTransition(const RadioTransitionBeginOptions& options);
  bool beginRadioTransition();
  /** @brief Exit transition mode: resume normal management processing. */
  bool endRadioTransition(const RadioTransitionEndOptions& options);
  bool endRadioTransition();
  /** @brief Force hard radio deinit (manager transport end) after best-effort quiesce. */
  bool hardDeinitRadio(const RadioHardDeinitOptions& options);
  bool hardDeinitRadio();
  /** @brief Bring manager transport/runtime back up after hard deinit. */
  bool hardReinitRadio(const RadioHardReinitOptions& options);
  bool hardReinitRadio();
  /** @brief Read current radio transition status. */
  void radioTransitionStatusGet(RadioTransitionStatus& out_status) const;
  /** @brief True when transition mode is active. */
  bool radioTransitionActive() const { return radio_transition_active_; }
  /** @brief Monotonic epoch incremented on each transition begin. */
  uint32_t radioTransitionEpoch() const { return radio_transition_epoch_; }

  /** @brief Canonical command priority metadata for orchestration surfaces. */
  static uint8_t commandPriority(uint16_t cmd_id);
  /** @brief Canonical command access metadata for orchestration surfaces. */
  static ManagementAccessLevel commandRequiredAccessLevel(uint16_t cmd_id);
  /** @brief True when command completion is deferred to async lifecycle events. */
  static bool isAsyncTerminalCommand(uint16_t cmd_id);
  /** @brief Canonical command default timeout metadata for orchestration surfaces. */
  static uint32_t commandTimeoutMs(uint16_t cmd_id);

 private:
  struct PendingRequest {
    ManagementRequest request{};
    uint32_t deadline_ms = 0;
    uint8_t priority = 0;
  };

  struct PeerResolveContext {
    bool has_requested_peer = false;
    MacAddress requested_peer{};
    bool has_executed_peer = false;
    MacAddress executed_peer{};
    bool activation_performed = false;
    uint16_t activation_latency_ms = 0;
  };

  struct LivenessPeerState {
    MacAddress peer{};
    bool online = true;
    uint32_t last_seen_ms = 0;
    bool probe_pending = false;
    uint32_t probe_sent_ms = 0;
    uint8_t probe_fail_count = 0;
  };

  struct LivenessMonitorState {
    bool enabled = false;
    bool ignore_active = false;
    uint16_t ignore_reason_mask = 0;
    uint32_t next_probe_due_ms = 0;
    uint32_t last_transition_ms = 0;
    uint32_t next_probe_corr_id = 1;
    uint8_t probe_rr_cursor = 0;
    std::vector<LivenessPeerState> peers{};
  };

  bool executeRequest(const ManagementRequest& request);
  bool requirePairedPeer(const ManagementRequest& request,
                         MacAddress& out_peer,
                         PeerResolveContext* out_peer_ctx = nullptr);

  void queueResponse(const ManagementResponse& response);
  void queueResponse(ManagementSource source,
                     uint16_t cmd_id,
                     uint32_t req_id,
                     ManagementStatus status,
                     const std::vector<uint8_t>& payload = {},
                     const PeerResolveContext* peer_ctx = nullptr);
  void queueEvent(const ManagementEvent& event);
  void emitServiceEvent(ManagementEventId event_id,
                        const ManagementRequest& request,
                        ManagementStatus status,
                        const PeerResolveContext* peer_ctx = nullptr);
  bool buildDiscoverySnapshotPayload(std::vector<uint8_t>& out_payload) const;
  bool buildPairedSnapshotPayload(std::vector<uint8_t>& out_payload);
  bool buildStatusPayload(std::vector<uint8_t>& out_payload) const;
  bool buildTopologyStatusPayload(std::vector<uint8_t>& out_payload) const;
  bool buildTopologySlotsPayload(bool committed, std::vector<uint8_t>& out_payload) const;
  bool buildTopologySnapshotPayload(bool committed, ManagementTopologySnapshotPayload& out_snapshot) const;
  bool queueTopologyDeployToPeer(const MacAddress& peer,
                                 const ManagementTopologySnapshotPayload& snapshot,
                                 uint32_t corr_base);
  bool queueTopologyDeployForCommitted(uint32_t corr_seed,
                                       uint32_t& out_queued_peers,
                                       uint32_t& out_failed_peers);
  bool buildQueuePayload(std::vector<uint8_t>& out_payload) const;
  bool buildMetricsPayload(std::vector<uint8_t>& out_payload) const;
  void applyPeerContext(ManagementResponse& response, const PeerResolveContext& peer_ctx) const;
  void applyPeerContext(ManagementEvent& event, const PeerResolveContext& peer_ctx) const;

  bool runDescriptorPull(const ManagementRequest& request, uint16_t cmd_id);
  bool runOtaArchiveCommand(const ManagementRequest& request, uint16_t cmd_id);
  bool runPushCommand(const ManagementRequest& request, uint16_t cmd_id);
  bool runOtaTransferCommand(const ManagementRequest& request, uint16_t cmd_id);
  bool runOtaPushLocalCommand(const ManagementRequest& request, uint16_t cmd_id);
  bool runOtaUpdateLocalCommand(const ManagementRequest& request, uint16_t cmd_id);
  bool startChannelSyncAll(ManagementSource source, uint32_t req_id, uint8_t channel);
  void pumpChannelSyncAll();
  void stopChannelSyncAll(bool success, ManagementStatus status);
  bool startChainLoopAll(ManagementSource source, uint32_t req_id, bool enabled);
  void pumpChainLoopAll();
  void stopChainLoopAll(bool success, ManagementStatus status);
  bool buildChainLoopTargetPeers(std::vector<MacAddress>& out_peers) const;
  bool buildRuntimeChannelPayload(std::vector<uint8_t>& out_payload) const;
  bool buildLiveMonitorStatusPayload(std::vector<uint8_t>& out_payload) const;
  void loadLiveMonitorConfig();
  void persistLiveMonitorConfig() const;
  void loadChainLoopConfig();
  void persistChainLoopConfig() const;
  void syncLiveMonitorPeers();
  uint16_t liveMonitorIgnoreReasonMask() const;
  void pumpLiveMonitor();
  void noteLivePeerSeen(const MacAddress& peer, uint8_t reason_code);
  void emitLivePeerTransition(const MacAddress& peer, bool online, uint8_t reason_code);
  bool hasPendingTargetRequest(const MacAddress& peer) const;
  bool isCriticalLiveMonitorCommand(uint16_t cmd_id) const;
  LivenessPeerState* findLivePeerState(const MacAddress& peer);
  const LivenessPeerState* findLivePeerState(const MacAddress& peer) const;
  void removeLivePeerState(const MacAddress& peer);
  bool runMasterCritical(const ManagementRequest& request);
  bool runSlaveCritical(const ManagementRequest& request, uint16_t control_cmd_id);
  bool isRadioTransitionBlockedCommand(uint16_t cmd_id) const;
  void cancelDeferredLifecycleCommandsForTransition();
  void cancelPendingMutatingRequestsForTransition();
  void markRadioTransitionError(ManagementStatus status, const char* stage, const char* message);
  bool startOtaPushLocalSession(ManagementSource source,
                                uint16_t owner_cmd_id,
                                uint32_t req_id,
                                const MacAddress& peer,
                                const std::string& local_path,
                                uint16_t chunk_bytes,
                                bool emit_cmd_event,
                                std::vector<uint8_t>* out_start_payload,
                                ManagementStatus* out_status);
  void pumpOtaPushLocal();
  void pumpOtaUpdateLocal();
  void stopOtaPushLocal(bool success, ManagementStatus status, uint16_t ota_status_code, const char* reason);
  void stopOtaUpdateLocal(bool success, ManagementStatus status, uint16_t ota_status_code, const char* reason);
  void pauseTelemetryPushForPeerBestEffort(const MacAddress& peer, uint32_t corr_id);
  void pauseTelemetryPushForAllPeersBestEffort(uint32_t corr_seed);

  static ManagementStatus statusFromPolicy(DevicePolicyCode code);
  bool makeDeviceContext(const ManagementRequest& request, DeviceCommandContext& out_ctx) const;
  void registerDeferredLifecycleCommand(uint32_t req_id, uint16_t cmd_id, ManagementSource source);
  bool consumeDeferredLifecycleCommand(uint32_t req_id,
                                       uint16_t expected_cmd_id,
                                       ManagementSource& out_source);

  struct OtaPushLocalSession {
    enum class Phase : uint8_t {
      Idle = 0,
      WaitBegin,
      Streaming,
      WaitEnd,
    };
    bool active = false;
    ManagementSource source = ManagementSource::Unknown;
    MacAddress peer{};
    uint32_t req_id = 0;
    std::string path{};
    std::string image_name{};
    uint16_t chunk_bytes = 0;
    uint32_t total_size = 0;
    uint32_t image_crc32 = 0;
    Phase phase = Phase::Idle;
    uint32_t next_offset = 0;
    uint32_t pending_end_offset = 0;
    uint16_t chunks_sent = 0;
    bool waiting_chunk_ack = false;
    bool begin_acked = false;
    uint8_t retry_count = 0;
    uint8_t send_fail_streak = 0;
    uint8_t window_size_chunks = 16;
    uint32_t started_ms = 0;
    uint32_t last_activity_ms = 0;
    uint32_t wait_started_ms = 0;
    uint32_t last_status_poll_ms = 0;
    uint32_t next_send_ms = 0;
    uint32_t remote_acked_offset = 0;
    uint32_t recovery_until_acked_offset = 0;
    uint16_t owner_cmd_id = 0;
    bool emit_cmd_event = true;
    std::vector<uint8_t> chunk_buf{};
  };

  struct OtaUpdateLocalSession {
    enum class Phase : uint8_t {
      Idle = 0,
      Push,
      WaitBoot,
    };
    bool active = false;
    ManagementSource source = ManagementSource::Unknown;
    MacAddress peer{};
    uint32_t req_id = 0;
    std::string local_path{};
    std::string image_name{};
    uint16_t chunk_bytes = 0;
    Phase phase = Phase::Idle;
    uint32_t started_ms = 0;
    uint32_t last_activity_ms = 0;
  };

  struct ChannelSyncAllSession {
    bool active = false;
    ManagementSource source = ManagementSource::Unknown;
    uint16_t owner_cmd_id = 0;
    uint32_t req_id = 0;
    uint8_t channel = 0;
    uint32_t started_ms = 0;
    uint32_t timeout_ms = 0;
    std::vector<MacAddress> peers{};
    std::vector<uint8_t> role_codes{};
    std::vector<std::string> setting_keys{};
    std::vector<uint32_t> corr_ids{};
    std::vector<uint8_t> acked{};
  };

  struct ChainLoopAllSession {
    bool active = false;
    ManagementSource source = ManagementSource::Unknown;
    uint16_t owner_cmd_id = 0;
    uint32_t req_id = 0;
    bool enabled = false;
    uint32_t started_ms = 0;
    uint32_t timeout_ms = 0;
    std::vector<MacAddress> peers{};
    std::vector<uint32_t> corr_ids{};
    std::vector<uint8_t> acked{};
  };

  struct DeferredLifecycleCommand {
    uint32_t req_id = 0;
    uint16_t cmd_id = 0;
    ManagementSource source = ManagementSource::Unknown;
  };

  Role local_role_ = Role::Slave;
  EspNowManager& manager_;
  MasterPullClient* pull_ = nullptr;
  IDeviceManagerPolicy* device_policy_ = nullptr;
  IDeviceManagerActions* device_actions_ = nullptr;
  MasterCli* master_cli_ = nullptr;
  IOtaStorageBackend* ota_push_storage_ = nullptr;
  OtaPushLocalSession ota_push_local_{};
  OtaUpdateLocalSession ota_update_local_{};
  ChannelSyncAllSession channel_sync_all_{};
  ChainLoopAllSession chain_loop_all_{};
  LivenessMonitorState live_monitor_{};
  bool chain_loop_enabled_ = false;
  bool live_monitor_critical_inflight_ = false;
  uint32_t live_monitor_master_update_guard_until_ms_ = 0;
  bool radio_transition_active_ = false;
  RadioTransitionState radio_transition_state_ = RadioTransitionState::Idle;
  uint32_t radio_transition_epoch_ = 0;
  bool radio_transition_restore_live_monitor_ = false;
  ManagementStatus radio_transition_last_error_ = ManagementStatus::Ok;
  std::string radio_transition_last_error_stage_{};
  std::string radio_transition_last_error_message_{};

  uint32_t now_ms_ = 0;
  bool discovery_active_ = false;
  uint32_t discovery_deadline_ms_ = 0;
  uint32_t discovery_window_ms_ = 10000;
  std::vector<MacAddress> discovered_;

  size_t max_queue_depth_ = 32;
  std::deque<PendingRequest> request_queue_;
  std::deque<ManagementResponse> response_queue_;
  std::deque<ManagementEvent> event_queue_;
  std::vector<DeferredLifecycleCommand> deferred_lifecycle_commands_{};
  mutable std::recursive_mutex state_mx_{};
};

}  // namespace espnow_link
