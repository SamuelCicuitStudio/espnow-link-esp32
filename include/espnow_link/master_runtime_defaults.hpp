#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "espnow_link/cli_master.hpp"
#include "espnow_link/codec.hpp"
#include "espnow_link/control.hpp"
#include "espnow_link/device_manager.hpp"
#include "espnow_link/management_frontend_adapter.hpp"
#include "espnow_link/management_service.hpp"
#include "espnow_link/management_queue_transport.hpp"
#include "espnow_link/management_runtime.hpp"
#include "espnow_link/management_transport.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/manager.hpp"
#include "espnow_link/master_node_runtime.hpp"
#include "espnow_link/master_pull_client.hpp"
#include "espnow_link/multi_event_sink.hpp"
#include "espnow_link/node_runtime.hpp"
#include "espnow_link/storage_bootstrap_arduino.hpp"
#include "espnow_link/pairing.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/power.hpp"

namespace espnow_link {

/**
 * @brief Simple control-plane forwarder used to route manager pull traffic.
 *
 * This is the library-owned replacement for ad-hoc example mux classes.
 */
class ControlPlaneForwarder : public IControlPlane {
 public:
  /** @brief Bind one target and clear any previous target list. */
  void bind(IControlPlane* target) {
    targets_.clear();
    if (target != nullptr) {
      targets_.push_back(target);
    }
  }
  /** @brief Add one additional target control-plane endpoint (nullable ignored). */
  void add(IControlPlane* target) {
    if (target == nullptr) {
      return;
    }
    for (IControlPlane* existing : targets_) {
      if (existing == target) {
        return;
      }
    }
    targets_.push_back(target);
  }
  /** @brief Clear target control-plane endpoint list. */
  void clear() { targets_.clear(); }

  bool onPullRequest(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len) override {
    bool ok = true;
    for (IControlPlane* target : targets_) {
      if (target != nullptr) {
        ok = target->onPullRequest(from, corr_id, payload, len) && ok;
      }
    }
    return ok;
  }

  bool onPullResponse(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len) override {
    bool ok = true;
    for (IControlPlane* target : targets_) {
      if (target != nullptr) {
        ok = target->onPullResponse(from, corr_id, payload, len) && ok;
      }
    }
    return ok;
  }

 private:
  std::vector<IControlPlane*> targets_{};
};

/**
 * @brief Default master-side critical actions for restart/reset and rollback.
 *
 * Implements both `IMasterCliActions` and `IDeviceManagerActions` so CLI and
 * management service can share one execution path.
 */
class MasterCriticalActions : public IMasterCliActions, public IDeviceManagerActions {
 public:
  /** @brief Runtime behavior/config for deferred restart execution. */
  struct Config {
    const char* log_prefix = "MASTER";
    uint16_t pre_reboot_hold_ms = 80;
    uint32_t reboot_delay_ms = 1000;
    IOtaStorageBackend* ota_storage = nullptr;
    bool ota_reboot_on_success = true;
  };

  /** @brief Update action configuration. */
  void configure(const Config& cfg) { cfg_ = cfg; }

  void requestMasterRestart(bool reset_semantic) override;
  bool requestMasterRollback(std::string* out_message = nullptr) override;
  bool requestMasterUpdateFromStaged(const std::string& staged_path,
                                     std::string* out_message = nullptr) override;
  bool queueCriticalCommand(const DeviceCommandContext& ctx, std::string* out_message) override;

  /** @brief Execute pending deferred restart/reset in runtime loop context. */
  void tick();

 private:
  Config cfg_{};
  bool pending_ = false;
  bool reset_ = false;
  bool ota_apply_pending_ = false;
  std::string ota_apply_path_{};
};

/**
 * @brief Default policy for logger mutation commands.
 *
 * All commands are allowed by default. When logger mutation is disabled, logger
 * clear/control commands are rejected with `DenyPolicy`.
 */
class MasterLoggerMutationPolicy : public IDeviceManagerPolicy {
 public:
  /** @brief Allow/deny logger mutation commands (`clear`, `set enabled`). */
  void setAllowLoggerMutation(bool allow) { allow_logger_mutation_ = allow; }

  DevicePolicyDecision authorizeCriticalCommand(const DeviceCommandContext& ctx) override;

 private:
  bool allow_logger_mutation_ = true;
};

/**
 * @brief Runtime post-tick hook that executes `MasterCriticalActions::tick()`.
 */
class MasterActionsTickHook : public INodeRuntimeHook {
 public:
  /** @brief Bind actions implementation (nullable). */
  void bind(MasterCriticalActions* actions) { actions_ = actions; }
  /** @brief Bind management runtime implementation (nullable). */
  void bindManagementRuntime(ManagementRuntime* runtime) { runtime_ = runtime; }

  void onRuntimeTick(uint32_t now_ms) override;

 private:
  ManagementRuntime* runtime_ = nullptr;
  MasterCriticalActions* actions_ = nullptr;
};

#if defined(ARDUINO)
/**
 * @brief Master-oriented wrapper over `ArduinoNodeStorageBootstrap`.
 *
 * Keeps `examples/master/main.cpp` concise by centralizing storage bootstrap
 * input mapping and standard serial report printing.
 */
class MasterStorageBootstrap {
 public:
  /** @brief Input config for master storage bootstrap. */
  struct Config {
    bool sd_ready = false;
    bool spiffs_ready = false;
    fs::SDFS* sd_fs = nullptr;
    fs::SPIFFSFS* spiffs_fs = nullptr;
    ArduinoSelectableLogStorageBackend* log_backend = nullptr;
    ArduinoStorageExplorer* storage_explorer = nullptr;
    ArduinoSelectableOtaStorageBackend* ota_backend = nullptr;
    EspLogStore* log_store = nullptr;
    EspLogStore* remote_log_store = nullptr;
    LibraryLogger* logger = nullptr;
    bool prefer_sd_for_logs = true;
    bool prefer_sd_for_explorer = true;
    uint32_t log_rotate_spiffs_bytes = 1572864U;
    uint32_t log_rotate_sd_bytes = 52428800U;
    const char* log_prefix = "MASTER";
    bool print_report = true;
  };

  /**
   * @brief Run storage bootstrap with master defaults and optional report out.
   * @param cfg Bootstrap config.
   * @param out_report Optional output report.
   * @return true when bootstrap executed.
   */
  bool begin(const Config& cfg, ArduinoNodeStorageBootstrap::Report* out_report = nullptr);

 private:
  void printReport_(const Config& cfg, const ArduinoNodeStorageBootstrap::Report& report) const;
  ArduinoNodeStorageBootstrap bootstrap_{};
};
#endif  // defined(ARDUINO)

/**
 * @brief Library-owned bootstrap that wires master core services and runtime.
 *
 * This reduces example/app boilerplate by owning and binding:
 * `PairingStore -> EspNowManager -> MasterPullClient -> ManagementService -> MasterCli -> MasterNodeRuntime`.
 */
class MasterNodeBootstrap {
 public:
  /**
   * @brief Build a sane default manager config for master nodes.
   *
   * Master defaults intentionally do not advertise discovery beacons
   * (advertising is slave-only).
   */
  static ManagerConfig defaultManagerConfig(uint8_t channel = 1,
                                            bool auto_pair_on_discovery = false,
                                            bool time_sync_enabled = true,
                                            uint32_t time_sync_interval_ms = 5000);

  /** @brief Dependency/configuration set required to build master runtime graph. */
  struct Config {
    ManagerConfig manager_config{};
    ITransport* transport = nullptr;
    IPersistenceBackend* persistence = nullptr;
    IPlatformHooks* hooks = nullptr;
    LibraryLogger* logger = nullptr;
    IMasterCliIo* cli_io = nullptr;
    INodeRuntimePoller* input = nullptr;
    IStorageExplorerProvider* local_storage = nullptr;
    IOtaStorageBackend* ota_push_storage = nullptr;
    EspLogStore* remote_log_store = nullptr;
    const char* cli_enable_key = "clienbl";
    IEventSink* extra_event_sink = nullptr;
    // Required: runtime begin fails when local profile is not explicitly provided.
    ProfileId local_profile_id = kProfileUnknown;
    CodecId local_codec_id = 0;
    MasterCriticalActions::Config actions_config{};
    bool allow_logger_mutation = true;
    uint16_t runtime_idle_delay_ms = 2;
    bool print_help_on_bringup = true;
    bool print_startup_summary = true;
    const char* log_prefix = "MASTER";
    MasterCli::CliTrafficPolicy cli_traffic_policy = MasterCli::CliTrafficPolicy::Auto;
    std::vector<IManagementTransport*> management_transports{};
  };

  /**
   * @brief Build and bind master runtime object graph.
   * @param cfg Bootstrap config/dependencies.
   * @return true when all required components are initialized.
   */
  bool begin(const Config& cfg);

  /** @brief Whether bootstrap graph is ready for runtime use. */
  bool ready() const { return ready_; }

  /** @brief Execute one runtime tick. */
  void tick(uint32_t now_ms);
  /** @brief Arduino-friendly loop helper. */
  void loop();

  /**
   * @brief Start manager runtime and optionally restore persisted peer link.
   *
   * Calls `EspNowManager::begin(local_mac)` and, when `restore_link` is true,
   * calls `restore()` and reports restored paired peer.
   *
   * @param local_mac Local node MAC for manager initialization.
   * @param restore_link True to run persisted-link restore flow after begin.
   * @param out_restored_peer Optional restored peer output (zero MAC when none).
   * @return true when manager begin succeeded.
   */
  bool bringUp(const MacAddress& local_mac,
               bool restore_link = true,
               MacAddress* out_restored_peer = nullptr);

  /**
   * @brief Arduino helper bring-up using local STA MAC as manager identity.
   *
   * Reads local STA MAC, then runs `bringUp(local_mac, restore_link, ...)`.
   * When enabled in config, prints CLI help after successful bring-up.
   *
   * @param restore_link True to run persisted-link restore flow after begin.
   * @param out_local_mac Optional local MAC output.
   * @param out_restored_peer Optional restored peer output.
   * @return true when begin succeeded.
   */
  bool bringUp(bool restore_link = true,
               MacAddress* out_local_mac = nullptr,
               MacAddress* out_restored_peer = nullptr);

  /** @brief Access owned manager instance (nullable before `begin`). */
  EspNowManager* manager() { return manager_.get(); }
  /** @brief Access owned pull client instance (nullable before `begin`). */
  MasterPullClient* pull() { return pull_.get(); }
  /** @brief Access owned management service instance (nullable before `begin`). */
  ManagementService* management() { return management_.get(); }
  /** @brief Access owned management runtime instance (nullable before `begin`). */
  ManagementRuntime* managementRuntime() { return management_runtime_.get(); }
  /** @brief Register one management transport endpoint on runtime. */
  bool addManagementTransport(IManagementTransport* transport);
  /** @brief Unregister one management transport endpoint from runtime. */
  bool removeManagementTransport(IManagementTransport* transport);
  /** @brief Count currently registered management transport endpoints. */
  size_t managementTransportCount() const;
  /** @brief Read current management runtime counters. */
  bool managementRuntimeStats(ManagementRuntime::Stats& out_stats) const;
  /**
   * @brief Read queue depth for one owned queue transport source.
   * @param source Logical transport source (CLI/WiFi/BLE/...).
   * @param out_req Pending inbound requests.
   * @param out_resp Pending outbound responses.
   * @param out_evt Pending outbound events.
   * @return true when owned queue transport for source exists.
   */
  bool managementQueueDepth(ManagementSource source,
                            size_t& out_req,
                            size_t& out_resp,
                            size_t& out_evt) const;
  /** @brief Find one owned queue transport by source. */
  ManagementQueueTransport* ownedQueueTransport(ManagementSource source);
  /** @brief Find one owned queue transport by source (const). */
  const ManagementQueueTransport* ownedQueueTransport(ManagementSource source) const;
  /**
   * @brief Create one owned queue transport and register it on runtime.
   *
   * This is intended for frontend adapters (WiFi/BLE/custom) that need
   * managed lifetime tied to bootstrap object.
   *
   * @param cfg Queue transport config.
   * @return Pointer to owned transport when created/registered, else null.
   */
  ManagementQueueTransport* addOwnedQueueTransport(
      ManagementQueueTransport::Config cfg = ManagementQueueTransport::Config{});
  /**
   * @brief Get or create one owned queue transport for a frontend source.
   *
   * Returns an existing owned transport when present. Otherwise creates one
   * with the requested queue sizing and registers it on runtime.
   * Request overflow policy is always `RejectNew`; `drop_oldest_when_full`
   * applies to response/event channels only.
   */
  ManagementQueueTransport* ensureOwnedQueueTransport(ManagementSource source,
                                                      size_t max_requests = 64U,
                                                      size_t max_responses = 128U,
                                                      size_t max_events = 256U,
                                                      ManagementAccessLevel access_level = ManagementAccessLevel::Owner,
                                                      bool drop_oldest_when_full = true);
  /**
   * @brief Build a typed frontend adapter bound to runtime + service + source.
   *
   * When `create_transport` is true, queue transport is created on-demand for
   * the requested source.
   * Request overflow policy is always `RejectNew`; `drop_oldest_when_full`
   * applies to response/event channels only.
   */
  ManagementFrontendAdapter makeFrontendAdapter(ManagementSource source,
                                                bool create_transport = true,
                                                size_t max_requests = 64U,
                                                size_t max_responses = 128U,
                                                size_t max_events = 256U,
                                                ManagementAccessLevel access_level = ManagementAccessLevel::Owner,
                                                bool drop_oldest_when_full = true);
  /** @brief Convenience builder for WiFi frontend adapter. */
  ManagementFrontendAdapter makeWifiFrontendAdapter(bool create_transport = true,
                                                    size_t max_requests = 64U,
                                                    size_t max_responses = 128U,
                                                    size_t max_events = 256U,
                                                    ManagementAccessLevel access_level = ManagementAccessLevel::Owner,
                                                    bool drop_oldest_when_full = true);
  /** @brief Convenience builder for BLE frontend adapter. */
  ManagementFrontendAdapter makeBleFrontendAdapter(bool create_transport = true,
                                                   size_t max_requests = 64U,
                                                   size_t max_responses = 128U,
                                                   size_t max_events = 256U,
                                                   ManagementAccessLevel access_level = ManagementAccessLevel::Owner,
                                                   bool drop_oldest_when_full = true);
  /** @brief Convenience builder for custom frontend adapter. */
  ManagementFrontendAdapter makeCustomFrontendAdapter(bool create_transport = true,
                                                      size_t max_requests = 64U,
                                                      size_t max_responses = 128U,
                                                      size_t max_events = 256U,
                                                      ManagementAccessLevel access_level = ManagementAccessLevel::Owner,
                                                      bool drop_oldest_when_full = true);
  /** @brief Remove and destroy all owned queue transports from runtime. */
  void clearOwnedQueueTransports();
  /** @brief Access owned CLI instance (nullable before `begin`). */
  MasterCli* cliInstance() { return cli_.get(); }
  /** @brief Access active runtime input poller (owned or external). */
  INodeRuntimePoller* input() const { return input_; }
  /** @brief Access default master action handler. */
  MasterCriticalActions* actions() { return &actions_; }
  /** @brief Access default device policy handler. */
  MasterLoggerMutationPolicy* policy() { return &policy_; }
  /** @brief Access internal control-plane forwarder. */
  IControlPlane* controlPlane() { return &control_mux_; }
  /** @brief Access internal event multiplexer. */
  IEventSink* eventSink() { return &event_mux_; }

 private:
  Config cfg_{};
  bool ready_ = false;
  INodeRuntimePoller* input_ = nullptr;

  ControlPlaneForwarder control_mux_{};
  MultiEventSink event_mux_{};
  MasterCriticalActions actions_{};
  MasterLoggerMutationPolicy policy_{};
  MasterActionsTickHook actions_hook_{};
  MasterNodeRuntime runtime_{};

  std::unique_ptr<PairingStore> store_{};
  std::unique_ptr<EspNowManager> manager_{};
  std::unique_ptr<MasterPullClient> pull_{};
  std::unique_ptr<ManagementService> management_{};
  std::unique_ptr<ManagementRuntime> management_runtime_{};
  std::vector<std::unique_ptr<ManagementQueueTransport>> owned_queue_transports_{};
  std::unique_ptr<MasterCli> cli_{};
  std::unique_ptr<IMasterCliIo> owned_io_{};
  std::unique_ptr<INodeRuntimePoller> owned_input_{};
};

}  // namespace espnow_link
