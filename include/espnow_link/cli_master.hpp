#pragma once

#include <cstdint>
#include <string>
#include <deque>
#include <vector>
#include <unordered_set>

#include "espnow_link/control.hpp"
#include "espnow_link/events.hpp"
#include "espnow_link/library_logger.hpp"
#include "espnow_link/log_storage.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/master_autopull.hpp"
#include "espnow_link/master_pull_client.hpp"
#include "espnow_link/manager.hpp"
#include "espnow_link/ota_storage.hpp"
#include "espnow_link/ota_types.hpp"
#include "espnow_link/persistence.hpp"

namespace espnow_link {

class ManagementService;
class ManagementQueueTransport;
class ManagementRuntime;
class ManagementController;
struct TelemetryPushCommand;

/** @brief Output interface used by `MasterCli` to print text. */
class IMasterCliIo {
 public:
  virtual ~IMasterCliIo() = default;
  /** @brief Write text without newline. */
  virtual void write(const std::string& text) = 0;
  /** @brief Write text with newline. */
  virtual void writeln(const std::string& text) = 0;
};

/** @brief Optional action hook for local master restart/reset requests. */
class IMasterCliActions {
 public:
  virtual ~IMasterCliActions() = default;
  /**
   * @brief Request local master restart/reset action.
   * @param reset_semantic True for reset, false for restart.
   */
  virtual void requestMasterRestart(bool reset_semantic) = 0;

  /**
   * @brief Request local master rollback to previous image.
   * @param out_message Optional response detail.
   * @return true when rollback was accepted.
   */
  virtual bool requestMasterRollback(std::string* out_message = nullptr) {
    if (out_message != nullptr) {
      *out_message = "rollback unsupported";
    }
    return false;
  }

  /**
   * @brief Request local master OTA apply from staged SPIFFS image path.
   * @param staged_path Staged image path (typically `/o/s/fw.bin`).
   * @param out_message Optional response detail.
   * @return true when update request was accepted.
   */
  virtual bool requestMasterUpdateFromStaged(const std::string& staged_path,
                                             std::string* out_message = nullptr) {
    (void)staged_path;
    if (out_message != nullptr) {
      *out_message = "master staged update unsupported";
    }
    return false;
  }
};

/**
 * @brief Frontend-agnostic OTA control hook for master-side orchestration.
 *
 * Wi-Fi/BLE/custom UI layers can use this typed API instead of text commands.
 */
class IMasterOtaFrontendHook {
 public:
  virtual ~IMasterOtaFrontendHook() = default;
  virtual bool otaPrepareRemote(std::string* out_message = nullptr) = 0;
  virtual bool otaPushStaged(const std::string& staged_name,
                             uint16_t chunk_bytes = 220,
                             std::string* out_message = nullptr) = 0;
  virtual bool otaAbortPush(std::string* out_message = nullptr) = 0;
  virtual bool otaRequestRemoteStatus(std::string* out_message = nullptr) = 0;
  virtual bool otaRequestRemoteManifest(std::string* out_message = nullptr) = 0;
  virtual bool otaApplyRemote(const std::string& target, std::string* out_message = nullptr) = 0;
  virtual bool otaUpdateRemote(const std::string& staged_name,
                               uint16_t chunk_bytes = 180,
                               std::string* out_message = nullptr) = 0;
  virtual bool otaUpdateMaster(const std::string& staged_name = "",
                               std::string* out_message = nullptr) = 0;
};

/**
 * @brief Built-in serial-friendly CLI frontend for master testing and diagnostics.
 */
class MasterCli : public IEventSink, public IControlPlane, public IMasterOtaFrontendHook {
 public:
  /**
   * @brief Construct master CLI adapter.
   * @param manager Manager instance.
   * @param pull Pull client instance.
   * @param io Text I/O backend.
   * @param actions Optional local restart/reset actions.
   * @param persistence Optional backend used for CLI enabled flag persistence.
   * @param enable_key Persistence key for CLI enabled flag.
   * @param logger Optional local binary logger for direct logger commands.
   * @param management Optional management service used for logger command routing.
   * @param local_storage Optional local storage explorer provider used by `sd.*` commands.
   */
  MasterCli(EspNowManager& manager,
            MasterPullClient& pull,
            IMasterCliIo& io,
            IMasterCliActions* actions = nullptr,
            IPersistenceBackend* persistence = nullptr,
            const std::string& enable_key = "clienbl",
            LibraryLogger* logger = nullptr,
            ManagementService* management = nullptr,
            EspLogStore* remote_log_store = nullptr,
            IStorageExplorerProvider* local_storage = nullptr,
            IOtaStorageBackend* ota_push_storage = nullptr,
            ManagementQueueTransport* management_transport = nullptr,
            ManagementRuntime* management_runtime = nullptr);

  /** @brief Current master CLI enabled state. */
  bool cliEnabled() const { return cli_enabled_; }
  /**
   * @brief Enable/disable CLI at runtime and optionally persist state.
   * @param enabled Desired enabled state.
   * @param persist_state Persist state to configured backend.
   * @return true on success (or no-op when persistence is unavailable).
   */
  bool setCliEnabled(bool enabled, bool persist_state = true);

  /** @brief Print supported command list. */
  void printHelp();

  /** @brief Machine-readable dispatch outcome for one CLI command line. */
  struct CommandDispatchResult {
    bool parsed = false;
    bool handled = false;
    bool submitted = false;
    bool accepted = false;
    uint16_t cmd_id = 0U;
    uint32_t req_id = 0U;
    ManagementStatus status = ManagementStatus::InternalError;
    std::string reject_stage{};
  };
  /**
   * @brief Handle one command line.
   * @param line Raw command text.
   */
  void handleLine(const std::string& line);
  /** @brief Handle one line and return machine-readable dispatch result. */
  CommandDispatchResult handleLineEx(const std::string& line);
  /**
   * @brief Advance CLI periodic tasks and queues.
   * @param now_ms Current tick time in ms.
   */
  void tick(uint32_t now_ms);

  /** @brief Consume runtime events from manager. */
  void onEvent(const Event& event) override;
  /** @brief Ignore inbound pull requests (CLI does not serve requests). */
  bool onPullRequest(const MacAddress&, uint32_t, const uint8_t*, size_t) override;
  /** @brief Decode and print inbound pull responses. */
  bool onPullResponse(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len) override;

  /** @brief Request remote OTA receiver preparation (`OTA.CLEAR in`). */
  bool otaPrepareRemote(std::string* out_message = nullptr) override;
  /** @brief Push one staged SPIFFS image from `/o/s/<name>` (unless absolute path given). */
  bool otaPushStaged(const std::string& staged_name,
                     uint16_t chunk_bytes = 220,
                     std::string* out_message = nullptr) override;
  /** @brief Abort current OTA push and request remote RX clear. */
  bool otaAbortPush(std::string* out_message = nullptr) override;
  /** @brief Request remote OTA runtime status. */
  bool otaRequestRemoteStatus(std::string* out_message = nullptr) override;
  /** @brief Start paged remote OTA manifest fetch. */
  bool otaRequestRemoteManifest(std::string* out_message = nullptr) override;
  /** @brief Request remote OTA apply for image id/name. */
  bool otaApplyRemote(const std::string& target, std::string* out_message = nullptr) override;
  /** @brief Start remote OTA update pipeline from staged SPIFFS file (`prepare->push->apply`). */
  bool otaUpdateRemote(const std::string& staged_name,
                       uint16_t chunk_bytes = 180,
                       std::string* out_message = nullptr) override;
  /** @brief Request local master OTA apply from staged SPIFFS file (default `/o/s/fw.bin`). */
  bool otaUpdateMaster(const std::string& staged_name = "",
                       std::string* out_message = nullptr) override;

  /** @brief Compact/sanitize OTA filename to short 8.3-style form. */
  static std::string shortOtaName(const std::string& input_name);

 private:
  enum class CliLogLevel : uint8_t {
    Error = 0,
    Info = 1,
    Debug = 2,
  };

  struct DescriptorRequestQueueItem {
    std::string cmd;
    uint32_t enqueued_ms = 0;
    bool has_target_peer = false;
    MacAddress target_peer{};
  };

  struct DiscoveryItem {
    MacAddress mac{};
    int16_t rssi = 0;
    std::string node_name;
    uint32_t last_seen_ms = 0;
  };

  struct MandatoryEventItem {
    MacAddress peer{};
    uint32_t corr_id = 0;
    uint16_t event_id = 0;
    uint8_t severity = 0;
    int32_t event_value = 0;
    uint32_t event_ts_s = 0;
    uint32_t rx_ms = 0;
  };

  struct ChildPushPeerState {
    MacAddress peer{};
    uint8_t semu_mask = 0U;
    uint16_t remu_mask = 0U;
    TelemetryPushMode semu_mode = TelemetryPushMode::Hybrid;
    TelemetryPushMode remu_mode = TelemetryPushMode::Hybrid;
    uint32_t semu_interval_ms = 2000U;
    uint32_t remu_interval_ms = 2000U;
    float semu_delta_abs = 0.10f;
    float remu_delta_abs = 0.10f;
    uint32_t semu_gap_ms = 200U;
    uint32_t remu_gap_ms = 200U;
  };

  struct PeerProfileCacheEntry {
    MacAddress peer{};
    ProfileId profile_id = kProfileUnknown;
  };

  enum class PagedFetchKind : uint8_t {
    None = 0,
    Capabilities,
    Telemetry,
    TelemetrySnapshot,
    Settings,
    OtaManifest,
  };
  enum class ProbePendingKind : uint8_t {
    None = 0,
    Live,
    Ping,
  };
  enum class OtaPushPhase : uint8_t {
    Idle = 0,
    WaitBeginStatus,
    Streaming,
    WaitEndStatus,
  };

  struct SubmitDispatchSnapshot {
    bool seen = false;
    bool accepted = false;
    uint16_t cmd_id = 0U;
    uint32_t req_id = 0U;
    ManagementStatus status = ManagementStatus::InternalError;
    const char* reject_stage = "";
  };

  bool sendDescriptorQuery(const std::string& cmd);
  void setAutoPull(bool enabled, uint32_t interval_ms);
  void clearPeerSessionState_();
  void clearActiveTarget_();
  bool setActiveTargetBySelector_(const std::string& selector, MacAddress* out_peer = nullptr);
  bool resolveRuntimePeer(MacAddress& out_peer) const;
  bool hasRuntimePeer() const;
  bool getCachedRemoteProfile_(const MacAddress& peer, ProfileId& out_profile) const;
  void upsertCachedRemoteProfile_(const MacAddress& peer, ProfileId profile_id);
  void eraseCachedRemoteProfile_(const MacAddress& peer);
  void refreshRuntimeProfileHint_();
  bool ensureRuntimeProfileKnown_(ProfileId& out_profile, bool trigger_probe_on_miss = true);
  bool submitRuntimeTargeted_(ManagementController& controller,
                              uint16_t cmd_id,
                              const std::vector<uint8_t>& payload = {},
                              uint32_t* out_req_id = nullptr,
                              uint32_t timeout_ms = 0U,
                              bool apply_runtime_target = true,
                              const MacAddress* explicit_target_peer = nullptr,
                              uint32_t req_id_override = 0U) const;
  bool submitRuntimePushCommand_(ManagementController& controller,
                                 const TelemetryPushCommand& command,
                                 uint32_t* out_req_id = nullptr,
                                 uint32_t timeout_ms = 0U,
                                 bool apply_runtime_target = true) const;
  ChildPushPeerState* findChildPushState_(const MacAddress& peer);
  const ChildPushPeerState* findChildPushState_(const MacAddress& peer) const;
  ChildPushPeerState& ensureChildPushState_(const MacAddress& peer);
  void loadCliEnabled();
  bool persistCliEnabled();
  bool peerByIndex(size_t index, MacAddress& out) const;
  void printDiscovered() const;
  void printMandatoryEvents() const;
  void upsertDiscovery(const MacAddress& peer, const std::string& node_name, int16_t rssi);
  void writef(const char* fmt, ...) const;
  bool printTopicHelp(const std::string& topic);
  bool requestFullSettingsByProfile();
  bool handleGetIdCommand(const std::string& line, const std::string& lower);
  bool handleSetIdCommand(const std::string& line, const std::string& lower);
  bool handleGetCommand(const std::string& line, const std::string& lower);
  bool handleSetCommand(const std::string& line, const std::string& lower);
  bool handleTimeCommand(const std::string& line, const std::string& lower);
  bool handleAutopullCommand(const std::string& lower);
  bool handlePushCommands(const std::string& line, const std::string& lower);
  bool handleTestAndLocalCommands(const std::string& lower);
  bool handleRestartResetCommands(const std::string& lower);
  bool handleCliMetaCommands(const std::string& lower);
  bool handleListAndStatusCommands(const std::string& lower);
  bool handlePairingCommands(const std::string& line, const std::string& lower);
  bool handleTopologyCommands(const std::string& line, const std::string& lower);
  bool handleDescriptorShortCommands(const std::string& lower);
  bool handleEventCommands(const std::string& lower);
  bool handleSettingsCommands(const std::string& lower);
  bool handleOtaCommands(const std::string& line, const std::string& lower);
  bool handleStorageCommands(const std::string& line, const std::string& lower);
  bool handleLoggerCommands(const std::string& line, const std::string& lower);
  bool handleLogLevelCommand(const std::string& lower);
  bool logEnabled(CliLogLevel level) const;
  bool enqueueDescriptorQuery(const std::string& cmd, const MacAddress* target_peer = nullptr);
  bool executeDescriptorQueryNow(const std::string& cmd, const MacAddress* target_peer = nullptr);
  void pumpDescriptorQueue(uint32_t now_ms);
  void pumpManagementMailbox();
  void printQueueStatus() const;
  bool startPagedFetch(PagedFetchKind kind, uint8_t page_size, const char* queued_msg);
  bool enqueuePagedFetchPage(uint16_t cursor);
  bool handlePagedDescriptorResponse(const DescriptorResponse& d);
  void clearPagedFetchState();
  void printPagedFetchSummary() const;
  const char* descriptorSourceLabel(const DescriptorResponse& d) const;
  void printDeviceDescriptorResponse(const DescriptorResponse& d);
  void printCapabilitiesDescriptorResponse(const DescriptorResponse& d);
  void printTelemetrySchemaDescriptorResponse(const DescriptorResponse& d) const;
  void printTelemetrySnapshotDescriptorResponse(const DescriptorResponse& d);
  void printLivenessDescriptorResponse(const DescriptorResponse& d);
  void printTimeDescriptorResponse(const DescriptorResponse& d) const;
  void printSettingsDescriptorResponse(const DescriptorResponse& d);
  void printSingleSettingDescriptorResponse(const DescriptorResponse& d);
  void printLogStatusDescriptorResponse(const DescriptorResponse& d) const;
  void printLogChunkDescriptorResponse(const DescriptorResponse& d) const;
  void printStorageInfoDescriptorResponse(const DescriptorResponse& d) const;
  void printStorageListDescriptorResponse(const DescriptorResponse& d) const;
  void printStorageStatDescriptorResponse(const DescriptorResponse& d);
  void printOtaStatusDescriptorResponse(const DescriptorResponse& d) const;
  void printOtaManifestDescriptorResponse(const DescriptorResponse& d) const;
  void printOtaCapacityDescriptorResponse(const DescriptorResponse& d) const;
  void printOtaGateDescriptorResponse(const DescriptorResponse& d) const;
  void printAckDescriptorResponse(const DescriptorResponse& d) const;
  void printErrorDescriptorResponse(const DescriptorResponse& d) const;
  void printSettingLine(const SettingDescriptor& s);
  void printDescriptorResponse(const DescriptorResponse& d);
  bool startRemoteLogPull(uint16_t chunk_size);
  bool requestNextRemoteLogChunk();
  void stopRemoteLogPull(const char* reason, bool success);
  void resetDispatchSnapshot_() const;
  void captureDispatchSnapshot_(bool accepted,
                                uint16_t cmd_id,
                                uint32_t req_id,
                                ManagementStatus status,
                                const char* reject_stage) const;
  bool startOtaPush(const std::string& local_path, uint16_t chunk_bytes);
  bool computeOtaPushCrc(const std::string& local_path,
                         uint32_t size_bytes,
                         uint32_t& out_crc,
                         std::string* out_error = nullptr);
  bool requestOtaPushStatus();
  void handleOtaPushStatusResponse(const DescriptorResponse& d);
  void stopOtaPush(const char* reason, bool success);
  void pumpOtaPush(uint32_t now_ms);

  EspNowManager& manager_;
  MasterPullClient& pull_;
  IMasterCliIo& io_;
  IMasterCliActions* actions_ = nullptr;

  std::vector<DiscoveryItem> discovered_;
  std::vector<MandatoryEventItem> mandatory_events_;
  bool collect_discovery_ = false;
  bool list_window_active_ = false;
  uint32_t list_window_deadline_ms_ = 0;
  uint32_t correlation_id_ = 100;
  bool command_target_override_active_ = false;
  MacAddress command_target_override_peer_{};
  bool sticky_target_active_ = false;
  MacAddress sticky_target_peer_{};
  std::vector<PeerProfileCacheEntry> peer_profile_cache_{};

  MasterAutoPull auto_pull_{};
  bool auto_pull_enabled_ = false;
  uint32_t auto_pull_interval_ms_ = 2000;
  IPersistenceBackend* persistence_ = nullptr;
  std::string enable_key_;
  bool cli_enabled_ = true;
  ProfileId remote_profile_id_ = kProfileUnknown;
  uint16_t remote_settings_count_ = 0;
  std::deque<DescriptorRequestQueueItem> descriptor_request_queue_{};
  size_t descriptor_queue_max_ = 48;
  uint8_t descriptor_send_budget_per_tick_ = 4;
  uint32_t descriptor_queue_drop_count_ = 0;
  uint32_t descriptor_queue_sent_count_ = 0;
  uint32_t descriptor_queue_fail_count_ = 0;
  CliLogLevel log_level_ = CliLogLevel::Info;
  PagedFetchKind paged_fetch_kind_ = PagedFetchKind::None;
  uint8_t paged_fetch_page_size_ = 6;
  uint16_t paged_fetch_next_cursor_ = 0;
  uint16_t paged_fetch_expected_cursor_ = 0;
  bool paged_fetch_snapshot_locked_ = false;
  uint32_t paged_fetch_snapshot_id_ = 0;
  uint16_t paged_fetch_total_count_ = 0;
  bool paged_fetch_has_target_peer_ = false;
  MacAddress paged_fetch_target_peer_{};
  uint8_t paged_fetch_restart_count_ = 0;
  std::vector<CapabilityDescriptor> paged_caps_cache_{};
  std::vector<TelemetryDescriptor> paged_telem_cache_{};
  std::vector<TelemetrySample> paged_telem_samples_cache_{};
  std::vector<SettingDescriptor> paged_settings_cache_{};
  std::vector<OtaManifestEntry> paged_ota_manifest_cache_{};
  std::unordered_set<std::string> paged_caps_seen_keys_{};
  std::unordered_set<uint16_t> paged_telem_seen_ids_{};
  std::unordered_set<std::string> paged_telem_seen_keys_{};
  std::unordered_set<uint16_t> paged_telem_samples_seen_ids_{};
  std::unordered_set<std::string> paged_telem_samples_seen_keys_{};
  std::unordered_set<uint16_t> paged_settings_seen_ids_{};
  std::unordered_set<std::string> paged_settings_seen_keys_{};
  std::unordered_set<uint32_t> paged_ota_seen_ids_{};
  std::unordered_set<std::string> paged_ota_seen_names_{};
  LibraryLogger* logger_ = nullptr;
  ManagementService* management_ = nullptr;
  ManagementQueueTransport* management_transport_ = nullptr;
  ManagementRuntime* management_runtime_ = nullptr;
  EspLogStore* remote_log_store_ = nullptr;
  IStorageExplorerProvider* local_storage_ = nullptr;
  std::string local_storage_cwd_ = "/";
  std::string remote_storage_cwd_ = "/";
  std::string remote_storage_cd_pending_{};
  bool remote_log_pull_active_ = false;
  bool remote_log_pull_waiting_status_ = false;
  bool remote_log_pull_has_target_peer_ = false;
  MacAddress remote_log_pull_target_peer_{};
  uint32_t remote_log_pull_total_bytes_ = 0;
  uint32_t remote_log_pull_next_offset_ = 0;
  uint16_t remote_log_pull_chunk_size_ = 128;
  uint16_t remote_log_pull_chunks_ = 0;
  uint32_t remote_log_pull_started_ms_ = 0;
  uint32_t remote_log_pull_last_activity_ms_ = 0;
  IOtaStorageBackend* ota_push_storage_ = nullptr;
  bool ota_push_active_ = false;
  bool ota_push_has_target_peer_ = false;
  MacAddress ota_push_target_peer_{};
  std::string ota_push_path_{};
  uint16_t ota_push_chunk_bytes_ = 220;
  uint32_t ota_push_size_bytes_ = 0;
  uint32_t ota_push_crc32_ = 0;
  uint32_t ota_push_offset_ = 0;
  uint16_t ota_push_chunks_sent_ = 0;
  uint32_t ota_push_corr_id_ = 0;
  OtaPushPhase ota_push_phase_ = OtaPushPhase::Idle;
  uint32_t ota_push_started_ms_ = 0;
  uint32_t ota_push_last_activity_ms_ = 0;
  uint32_t ota_push_last_status_req_ms_ = 0;
  uint8_t ota_push_send_fail_streak_ = 0;
  uint32_t ota_push_next_send_ms_ = 0;
  uint32_t ota_push_last_end_send_ms_ = 0;
  uint8_t ota_push_end_send_count_ = 0;
  uint32_t ota_push_remote_acked_offset_ = 0;
  uint32_t ota_push_last_nack_offset_ = 0;
  uint32_t ota_push_window_target_offset_ = 0;
  uint32_t ota_push_window_wait_started_ms_ = 0;
  uint8_t ota_push_window_retry_count_ = 0;
  uint8_t ota_push_window_size_chunks_ = 16;
  uint8_t ota_push_window_size_default_chunks_ = 16;
  uint8_t ota_push_window_size_recovery_chunks_ = 1;
  uint32_t ota_push_recovery_until_acked_offset_ = 0;
  uint32_t ota_push_last_nack_log_ms_ = 0;
  bool ota_push_waiting_window_ack_ = false;
  bool ota_push_begin_ack_seen_ = false;
  std::vector<uint8_t> ota_push_buf_{};
  bool ota_update_pipeline_active_ = false;
  bool ota_update_prepare_pending_ = false;
  uint32_t ota_update_prepare_corr_id_ = 0;
  std::string ota_update_staged_path_{};
  uint16_t ota_update_chunk_bytes_ = 220;
  bool ota_update_wait_boot_notice_ = false;
  std::string ota_update_image_name_{};
  uint32_t ota_update_req_id_ = 0;
  bool ota_update_has_target_peer_ = false;
  MacAddress ota_update_target_peer_{};
  bool live_monitor_status_pending_ = false;
  uint32_t live_monitor_status_req_id_ = 0;
  bool auto_pull_has_target_peer_ = false;
  MacAddress auto_pull_target_peer_{};
  std::vector<ChildPushPeerState> child_push_peer_states_{};
  bool semu_telem_child_filter_active_ = false;
  uint8_t semu_telem_child_filter_vid_ = 0U;
  uint8_t semu_telem_child_filter_max_vid_ = 7U;
  ProbePendingKind probe_pending_kind_ = ProbePendingKind::None;
  uint32_t probe_sent_ms_ = 0;
  mutable SubmitDispatchSnapshot dispatch_snapshot_{};
};

}  // namespace espnow_link








