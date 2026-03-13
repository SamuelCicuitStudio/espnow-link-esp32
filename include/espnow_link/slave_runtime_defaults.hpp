#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "espnow_link/adapters.hpp"
#include "espnow_link/control.hpp"
#include "espnow_link/descriptor.hpp"
#include "espnow_link/events.hpp"
#include "espnow_link/manager.hpp"
#include "espnow_link/address.hpp"
#include "espnow_link/multi_event_sink.hpp"
#include "espnow_link/node_runtime.hpp"
#include "espnow_link/ota_descriptor_adapter.hpp"
#include "espnow_link/ota_firmware_sink.hpp"
#include "espnow_link/ota_manager.hpp"
#include "espnow_link/ota_update_gate.hpp"
#include "espnow_link/persistence.hpp"
#include "espnow_link/pms_descriptor_provider.hpp"
#include "espnow_link/preferences_store.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/slave_node_runtime.hpp"
#include "espnow_link/storage_bootstrap_arduino.hpp"
#include "espnow_link/time.hpp"

namespace espnow_link {

/**
 * @brief Default serial event logger for slave pairing/runtime events.
 *
 * This library-owned sink replaces example-local `EventsImpl` and preserves
 * the standard one-slave serial traces for pair/drop/failure lifecycle events.
 */
class SlaveEventLogger {
 public:
  /** @brief Runtime behavior/config for event logging. */
  struct Config {
    const char* log_prefix = "SLAVE";
    bool log_paired = true;
    bool log_pairing_step = true;
    bool log_packet_drop = true;
    bool log_pairing_fail = true;
  };

  /** @brief Update event logger configuration. */
  void configure(const Config& cfg) { cfg_ = cfg; }

  /** @brief EventSinkAdapter entrypoint. */
  void onEvent(const Event& e);

  /** @brief Clear cached paired peer state. */
  void clearPeer();

 private:
  void logf_(const char* fmt, ...) const;
  std::string macText_(const MacAddress& mac) const;

  Config cfg_{};
  MacAddress peer_{};
  bool has_peer_ = false;
};

/**
 * @brief Default slave pull-plane handler for descriptor and control commands.
 *
 * This library-owned implementation replaces example-local `SlaveControl`
 * boilerplate and provides:
 * - descriptor query decode/execute/encode path
 * - canonical parsing/encoding path
 * - restart/reset control command handling via NVS flags
 */
class SlaveControlPlane : public IControlPlane {
 public:
  /** @brief Optional callback invoked when audio ping command is received. */
  using AudioPingCallback = void (*)(bool buzzer_enabled, bool led_feedback_enabled, void* user);

  /** @brief Runtime behavior/config for request handling. */
  struct Config {
    bool verbose_pull_trace = false;
    const char* log_prefix = "SLAVE";
    const char* reset_key = "reset_key";
    const char* restart_request_key = "restart_req";
    const char* audio_ping_buzzer_enable_key = "buzen";
    const char* audio_ping_led_feedback_key = "ledfb";
  };

  /** @brief Update control-plane runtime configuration. */
  void configure(const Config& cfg) { cfg_ = cfg; }

  /** @brief Bind manager used for codec/decode/response routing. */
  void bindManager(EspNowManager* manager) { manager_ = manager; }
  /** @brief Bind NVS backend used for restart/reset request flags. */
  void bindNvs(PreferencesStore* nvs) { nvs_ = nvs; }
  /** @brief Bind descriptor provider used to execute descriptor queries. */
  void bindDescriptor(IDescriptorProvider* descriptor) { descriptor_ = descriptor; }
  /** @brief Bind optional callback used to perform audio ping feedback. */
  void setAudioPingCallback(AudioPingCallback cb, void* user = nullptr) {
    audio_ping_cb_ = cb;
    audio_ping_user_ = user;
  }

  bool onPullRequest(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len) override;
  bool onPullResponse(const MacAddress&, uint32_t, const uint8_t*, size_t) override { return true; }

 private:
  bool sendDescriptorResponse_(const MacAddress& to, uint32_t corr_id, const DescriptorResponse& response);
  bool sendDescriptorError_(const MacAddress& to, uint32_t corr_id, const std::string& message);
  bool sendDescriptorAck_(const MacAddress& to, uint32_t corr_id, const std::string& message);
  bool handleTopologyQuery_(const MacAddress& from,
                            uint32_t corr_id,
                            const DescriptorQuery& query,
                            bool& out_handled);
  void resetTopologyStageSession_();
  bool ensureTopologySourceAuthorized_(const MacAddress& from) const;
  bool readBoolSetting_(const char* key, bool fallback) const;

  void setFlag_(const char* key);
  void logf_(const char* fmt, ...) const;
  std::string macText_(const MacAddress& mac) const;

  Config cfg_{};
  EspNowManager* manager_ = nullptr;
  PreferencesStore* nvs_ = nullptr;
  IDescriptorProvider* descriptor_ = nullptr;
  AudioPingCallback audio_ping_cb_ = nullptr;
  void* audio_ping_user_ = nullptr;
  bool topology_stage_active_ = false;
  MacAddress topology_stage_owner_{};
  EspNowManager::TopologySnapshot topology_stage_snapshot_{};
  std::array<bool, EspNowManager::kTopologyMaxSlots> topology_stage_slot_seen_{};
  std::array<bool, EspNowManager::kTopologyMaxGroups> topology_stage_group_seen_{};
};

/**
 * @brief Runtime pre-tick hook that processes slave reset/restart request flags.
 *
 * `applyResetIfRequested()` should be called once after manager begin and before
 * restore. `onRuntimeTick()` processes restart flags continuously.
 */
class SlaveRestartResetHook : public INodeRuntimeHook {
 public:
  /** @brief Runtime behavior/config for reset/restart processing. */
  struct Config {
    const char* log_prefix = "SLAVE";
    const char* reset_key = "reset_key";
    const char* restart_request_key = "restart_req";
    uint16_t pre_reboot_hold_ms = 80;
    uint32_t reboot_delay_ms = 1000;
  };

  /** @brief Callback fired after reset processing clears local pairing state. */
  using ResetAppliedCallback = void (*)(void* user);

  /** @brief Update hook configuration. */
  void configure(const Config& cfg) { cfg_ = cfg; }

  /** @brief Bind dependencies used during reset/restart processing. */
  void bind(PreferencesStore* nvs, PairingStore* store, EspNowManager* manager, const MacAddress* local_mac);

  /** @brief Set optional callback invoked after reset cleanup. */
  void setResetAppliedCallback(ResetAppliedCallback cb, void* user);

  /** @brief Process one pending reset flag request immediately. */
  bool applyResetIfRequested();

  /** @brief Runtime pre-tick: process pending restart flag. */
  void onRuntimeTick(uint32_t now_ms) override;

 private:
  void logf_(const char* fmt, ...) const;
  bool readAndClearFlag_(const char* key);

  Config cfg_{};
  PreferencesStore* nvs_ = nullptr;
  PairingStore* store_ = nullptr;
  EspNowManager* manager_ = nullptr;
  const MacAddress* local_mac_ = nullptr;
  ResetAppliedCallback reset_cb_ = nullptr;
  void* reset_cb_user_ = nullptr;
};

/**
 * @brief Runtime post-tick hook that prints OTA transfer trace progress.
 */
class SlaveOtaTraceHook : public INodeRuntimeHook {
 public:
  /** @brief Runtime behavior/config for OTA trace emission. */
  struct Config {
    bool enabled = true;
    const char* log_prefix = "SLAVE";
    uint32_t poll_interval_ms = 50;
    uint32_t progress_step_bytes = 16384;
  };

  /** @brief Update trace hook configuration. */
  void configure(const Config& cfg) { cfg_ = cfg; }
  /** @brief Bind OTA manager source for status polling. */
  void bind(OtaManager* ota_manager) { ota_manager_ = ota_manager; }

  /** @brief Runtime post-tick: emit OTA status/progress lines. */
  void onRuntimeTick(uint32_t now_ms) override;

 private:
  static const char* stateName_(OtaTransferState state);
  static const char* codeName_(OtaStatusCode code);
  void logf_(const char* fmt, ...) const;

  Config cfg_{};
  OtaManager* ota_manager_ = nullptr;
  OtaTransferState last_state_ = OtaTransferState::Idle;
  OtaStatusCode last_code_ = OtaStatusCode::Ok;
  uint32_t last_expected_size_ = 0U;
  uint32_t last_received_size_ = 0U;
  uint32_t next_progress_bytes_ = 0U;
  uint32_t last_poll_ms_ = 0U;
};

/**
 * @brief System time sink with optional serial logging for slave time sync.
 */
class SlaveLoggedTimeSink : public ITimeSink {
 public:
  /** @brief Runtime behavior/config for time updates. */
  struct Config {
    const char* log_prefix = "SLAVE";
    bool log_updates = true;
  };

  /** @brief Update sink configuration. */
  void configure(const Config& cfg) { cfg_ = cfg; }

  bool setEpochSec(uint64_t epoch_s) override;

  /** @brief Last successfully applied epoch time. */
  uint64_t lastEpochSec() const { return last_epoch_s_; }

 private:
  Config cfg_{};
  uint64_t last_epoch_s_ = 0U;
};

/**
 * @brief Default slave OTA gate enforcing non-empty/newer image metadata.
 */
class SlaveOtaVersionGate : public IOtaUpdateGate {
 public:
  /** @brief Runtime behavior/config for OTA gate checks. */
  struct Config {
    std::string current_sw_version;
    bool require_newer_version = true;
    bool strict_role_check = true;
  };

  /** @brief Update gate configuration. */
  void configure(const Config& cfg) { cfg_ = cfg; }

  OtaGateResult prepareForTransfer(uint32_t image_size, uint32_t image_crc32) override;
  OtaGateResult prepareForTransferWithMetadata(uint32_t image_size,
                                               uint32_t image_crc32,
                                               const FirmwareImageMetadata* metadata) override;
  OtaGateResult prepareForApply(const std::string& image_path,
                                uint32_t image_size,
                                uint32_t image_crc32) override;

 private:
  static int compareVersion_(const std::string& lhs, const std::string& rhs);

  Config cfg_{};
};

/**
 * @brief PMS descriptor/provider bootstrap helper for slave examples.
 *
 * Owns one `PmsDescriptorProvider` and captures local MAC used by descriptor
 * identity fields.
 */
class SlavePmsProviderBootstrap {
 public:
  /** @brief Input config for PMS provider bootstrap. */
  struct Config {
    PreferencesStore* nvs = nullptr;
    ITimeSink* time_sink = nullptr;
    IStorageExplorerProvider* storage = nullptr;
    OtaDescriptorAdapter* ota = nullptr;
    PmsDescriptorProviderConfig provider_config{};
    bool print_report = true;
    const char* log_prefix = "SLAVE";
  };

  /**
   * @brief Initialize provider with explicit local MAC.
   * @param local_mac Local node MAC.
   * @param cfg Provider bootstrap config.
   * @return true when provider is ready.
   */
  bool begin(const MacAddress& local_mac, const Config& cfg);

  /**
   * @brief Initialize provider using local STA MAC on ESP targets.
   * @param cfg Provider bootstrap config.
   * @param out_local_mac Optional local MAC output.
   * @return true when provider is ready.
   */
  bool begin(const Config& cfg, MacAddress* out_local_mac = nullptr);

  /** @brief Whether provider is initialized and ready. */
  bool ready() const { return provider_ != nullptr; }

  /** @brief Access owned PMS provider (nullable before `begin`). */
  PmsDescriptorProvider* provider() { return provider_.get(); }
  /** @brief Access owned PMS provider as descriptor interface. */
  IDescriptorProvider* descriptor() { return provider_.get(); }
  /** @brief Access owned PMS provider as telemetry push provider. */
  ITelemetryPushProvider* telemetryPushProvider() { return provider_.get(); }
  /** @brief Local MAC used to initialize provider identity. */
  const MacAddress& localMac() const { return local_mac_; }

 private:
  Config cfg_{};
  MacAddress local_mac_{};
  std::unique_ptr<PmsDescriptorProvider> provider_{};
};

#if defined(ARDUINO)
/**
 * @brief Slave-oriented OTA manager/bootstrap helper.
 *
 * Owns default OTA apply executor (unless externally provided) plus:
 * - `OtaManager`
 * - `OtaFirmwareSink`
 * - `OtaDescriptorAdapter`
 */
class SlaveOtaBootstrap {
 public:
  /** @brief Input config for slave OTA bootstrap. */
  struct Config {
    IOtaStorageBackend* storage = nullptr;
    IOtaUpdateGate* gate = nullptr;
    IOtaApplyExecutor* apply_executor = nullptr;
    size_t apply_chunk_bytes = 1024U;
    bool apply_reboot_on_success = true;
    OtaManagerConfig manager_config{};
    uint32_t max_firmware_bytes = 0U;
    const char* running_sw_version = nullptr;
    const char* running_build_id = nullptr;
    bool mark_boot_success = true;
    bool print_report = true;
    const char* log_prefix = "SLAVE";
  };

  /**
   * @brief Initialize and bind OTA runtime stack.
   *
   * Initializes `OtaManager`, `OtaFirmwareSink`, and `OtaDescriptorAdapter`.
   * Optionally marks boot success once after startup.
   */
  bool begin(const Config& cfg);

  /** @brief Access owned OTA manager (nullable before `begin`). */
  OtaManager* manager() { return manager_.get(); }
  /** @brief Access owned OTA firmware sink (nullable before `begin`). */
  OtaFirmwareSink* firmwareSink() { return sink_.get(); }
  /** @brief Access owned OTA descriptor adapter (nullable before `begin`). */
  OtaDescriptorAdapter* descriptorAdapter() { return descriptor_.get(); }

 private:
  Config cfg_{};
  std::unique_ptr<IOtaApplyExecutor> owned_apply_{};
  std::unique_ptr<OtaManager> manager_{};
  std::unique_ptr<OtaFirmwareSink> sink_{};
  std::unique_ptr<OtaDescriptorAdapter> descriptor_{};
};

/**
 * @brief Slave-oriented wrapper over `ArduinoNodeStorageBootstrap`.
 *
 * Keeps `examples/slave_pms/main.cpp` concise by centralizing storage bootstrap
 * input mapping and standard serial report printing.
 */
class SlaveStorageBootstrap {
 public:
  /** @brief Input config for slave storage bootstrap. */
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
    const char* log_prefix = "SLAVE";
    bool print_report = true;
  };

  /**
   * @brief Run storage bootstrap with slave defaults and optional report out.
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
 * @brief Library-owned slave bootstrap that wires manager/control/runtime graph.
 *
 * This consolidates common slave setup logic:
 * `PairingStore -> EspNowManager -> SlaveControlPlane -> SlaveRestartResetHook -> SlaveNodeRuntime`.
 */
class SlaveNodeBootstrap {
 public:
  /**
   * @brief Build a sane default manager config for slave nodes.
   *
   * Slave defaults advertise discovery beacons (`discovery_name`) and keep
   * time sync enabled.
   */
  static ManagerConfig defaultManagerConfig(uint8_t channel = 1,
                                            const char* discovery_name = "PMS",
                                            bool auto_pair_on_discovery = true,
                                            bool time_sync_enabled = true,
                                            uint32_t time_sync_interval_ms = 5000);

  /** @brief Dependency/configuration set required to build slave runtime graph. */
  struct Config {
    ManagerConfig manager_config{};
    ITransport* transport = nullptr;
    IPersistenceBackend* persistence = nullptr;
    PreferencesStore* nvs = nullptr;
    IPlatformHooks* hooks = nullptr;
    IDescriptorProvider* descriptor = nullptr;
    ITelemetryPushProvider* telemetry_push = nullptr;
    IFirmwareStreamSink* firmware_sink = nullptr;
    ITimeSink* time_sink = nullptr;
    OtaManager* ota_manager_for_trace = nullptr;
    LibraryLogger* logger = nullptr;
    IEventSink* extra_event_sink = nullptr;
    // Required: runtime begin fails when local profile is not explicitly provided.
    ProfileId local_profile_id = kProfileUnknown;
    CodecId local_codec_id = 0;
    SlaveEventLogger::Config event_config{};
    SlaveControlPlane::Config control_config{};
    SlaveRestartResetHook::Config restart_config{};
    SlaveOtaTraceHook::Config ota_trace_config{};
    uint16_t runtime_idle_delay_ms = 2;
    bool print_startup_summary = true;
    const char* log_prefix = "SLAVE";
  };

  /**
   * @brief Build and bind slave runtime object graph.
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
   * @brief Start manager runtime and optionally apply reset/restore persisted link.
   *
   * @param local_mac Local node MAC for manager initialization.
   * @param restore_link True to run persisted-link restore flow after begin.
   * @param apply_reset_flag True to apply pending reset flag before restore.
   * @param out_restored_peer Optional restored peer output (zero MAC when none).
   * @return true when manager begin succeeded.
   */
  bool bringUp(const MacAddress& local_mac,
               bool restore_link = true,
               bool apply_reset_flag = true,
               MacAddress* out_restored_peer = nullptr);

  /**
   * @brief Arduino helper bring-up using local STA MAC as manager identity.
   *
   * @param restore_link True to run persisted-link restore flow after begin.
   * @param apply_reset_flag True to apply pending reset flag before restore.
   * @param out_local_mac Optional local MAC output.
   * @param out_restored_peer Optional restored peer output.
   * @return true when begin succeeded.
   */
  bool bringUp(bool restore_link = true,
               bool apply_reset_flag = true,
               MacAddress* out_local_mac = nullptr,
               MacAddress* out_restored_peer = nullptr);

  /** @brief Access owned manager instance (nullable before `begin`). */
  EspNowManager* manager() { return manager_.get(); }
  /** @brief Access owned pairing store instance (nullable before `begin`). */
  PairingStore* store() { return store_.get(); }
  /** @brief Access internal control-plane handler. */
  SlaveControlPlane* control() { return &control_; }
  /** @brief Access internal event logger. */
  SlaveEventLogger* events() { return &events_; }
  /** @brief Access internal restart/reset hook. */
  SlaveRestartResetHook* restartHook() { return &restart_hook_; }
  /** @brief Access internal OTA trace hook. */
  SlaveOtaTraceHook* otaTraceHook() { return &ota_trace_hook_; }

 private:
  static void onResetAppliedStatic_(void* user);
  void onResetApplied_();

  Config cfg_{};
  bool ready_ = false;
  MacAddress local_mac_{};
  bool has_local_mac_ = false;

  SlaveEventLogger events_{};
  SlaveControlPlane control_{};
  SlaveRestartResetHook restart_hook_{};
  SlaveOtaTraceHook ota_trace_hook_{};
  MultiEventSink event_mux_{};
  std::unique_ptr<EventSinkAdapter<SlaveEventLogger>> event_adapter_{};
  SlaveNodeRuntime runtime_{};

  std::unique_ptr<PairingStore> store_{};
  std::unique_ptr<EspNowManager> manager_{};
};

}  // namespace espnow_link
