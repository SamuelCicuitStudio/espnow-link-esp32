#include "espnow_link/master_runtime_defaults.hpp"

#include <cctype>
#include <cstdio>

#include "espnow_link/address.hpp"
#include "espnow_link/ota_paths.hpp"

#if defined(ARDUINO)
#include "espnow_link/cli_io_arduino.hpp"
#include "espnow_link/ota_apply_arduino.hpp"
#endif

#if defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#include <esp_system.h>
#endif

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace espnow_link {

namespace {

std::string normalizeMasterStagedPath(const std::string& raw_path) {
  std::string out;
  out.reserve(raw_path.size());
  for (char c : raw_path) {
    if (c == '\\') {
      out.push_back('/');
    } else {
      out.push_back(c);
    }
  }
  size_t begin = 0U;
  while (begin < out.size() && std::isspace(static_cast<unsigned char>(out[begin])) != 0) {
    ++begin;
  }
  size_t end = out.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(out[end - 1U])) != 0) {
    --end;
  }
  out = out.substr(begin, end - begin);
  if (out.empty()) {
    return std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedBinName;
  }
  if (out.front() == '/') {
    return out;
  }
  return std::string(ota_paths::kStaging) + "/" + out;
}

void configureQueueOverflowPolicies(ManagementQueueTransport::Config& cfg, bool drop_oldest_when_full) {
  // Keep request path deterministic: never drop oldest mutating command implicitly.
  cfg.request_overflow_policy = ManagementQueueTransport::OverflowPolicy::RejectNew;

  // Response/event channels can optionally prefer newest data under pressure.
  const auto passive_policy = drop_oldest_when_full
                                  ? ManagementQueueTransport::OverflowPolicy::DropOldest
                                  : ManagementQueueTransport::OverflowPolicy::RejectNew;
  cfg.response_overflow_policy = passive_policy;
  cfg.event_overflow_policy = passive_policy;
}

}  // namespace

#if defined(ARDUINO)
bool MasterStorageBootstrap::begin(const Config& cfg, ArduinoNodeStorageBootstrap::Report* out_report) {
  ArduinoNodeStorageBootstrap::Config boot_cfg{};
  boot_cfg.sd_ready = cfg.sd_ready;
  boot_cfg.spiffs_ready = cfg.spiffs_ready;
  boot_cfg.sd_fs = cfg.sd_fs;
  boot_cfg.sd_fs_typed = cfg.sd_fs;
  boot_cfg.spiffs_fs = cfg.spiffs_fs;
  boot_cfg.spiffs_fs_typed = cfg.spiffs_fs;
  boot_cfg.log_backend = cfg.log_backend;
  boot_cfg.storage_explorer = cfg.storage_explorer;
  boot_cfg.ota_backend = cfg.ota_backend;
  boot_cfg.log_store = cfg.log_store;
  boot_cfg.remote_log_store = cfg.remote_log_store;
  boot_cfg.logger = cfg.logger;
  boot_cfg.prefer_sd_for_logs = cfg.prefer_sd_for_logs;
  boot_cfg.prefer_sd_for_explorer = cfg.prefer_sd_for_explorer;
  boot_cfg.log_rotate_spiffs_bytes = cfg.log_rotate_spiffs_bytes;
  boot_cfg.log_rotate_sd_bytes = cfg.log_rotate_sd_bytes;

  ArduinoNodeStorageBootstrap::Report report{};
  const bool ok = bootstrap_.begin(boot_cfg, &report);
  if (cfg.print_report) {
    printReport_(cfg, report);
  }
  if (out_report != nullptr) {
    *out_report = report;
  }
  return ok;
}

void MasterStorageBootstrap::printReport_(const Config& cfg,
                                          const ArduinoNodeStorageBootstrap::Report& report) const {
  const char* prefix = (cfg.log_prefix != nullptr) ? cfg.log_prefix : "MASTER";
  if (report.ota_layout_ready) {
    Serial.printf("[%s][OTA] spiffs ota layout ready\n", prefix);
  }
  if (!report.ota_backend_ready) {
    Serial.printf("[%s][OTA] storage not ready: %s\n", prefix, report.ota_message.c_str());
  } else if (cfg.ota_backend != nullptr) {
    Serial.printf("[%s][OTA] storage backend=%s\n", prefix, cfg.ota_backend->activeBackendName());
  }
  if (!report.log_ready) {
    Serial.printf("[%s] log store begin failed (logger disabled until storage is ready)\n", prefix);
  } else {
    Serial.printf("[%s] logger backend=%s rotate_max=%lu bytes\n",
                  prefix,
                  report.log_backend_name,
                  static_cast<unsigned long>(report.log_rotate_max_bytes));
  }
  if (!report.remote_log_ready) {
    Serial.printf("[%s] remote export log store begin failed\n", prefix);
  }
}
#endif  // defined(ARDUINO)

void MasterCriticalActions::requestMasterRestart(bool reset_semantic) {
  pending_ = true;
  reset_ = reset_semantic;
}

bool MasterCriticalActions::requestMasterRollback(std::string* out_message) {
#if defined(ESP_PLATFORM)
  const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
  if (err != ESP_OK) {
    if (out_message != nullptr) {
      char buf[48] = {0};
      std::snprintf(buf, sizeof(buf), "err=0x%lX", static_cast<unsigned long>(err));
      *out_message = buf;
    }
    return false;
  }
  if (out_message != nullptr) {
    *out_message = "rollback requested; rebooting";
  }
  return true;
#else
  if (out_message != nullptr) {
    *out_message = "rollback unsupported";
  }
  return false;
#endif
}

bool MasterCriticalActions::requestMasterUpdateFromStaged(const std::string& staged_path,
                                                          std::string* out_message) {
#if defined(ARDUINO)
  if (cfg_.ota_storage == nullptr) {
    if (out_message != nullptr) {
      *out_message = "ota storage backend unavailable";
    }
    return false;
  }
  ota_apply_path_ = normalizeMasterStagedPath(staged_path);
  ota_apply_pending_ = true;
  if (out_message != nullptr) {
    *out_message = std::string("master staged update queued path=") + ota_apply_path_;
  }
  return true;
#else
  (void)staged_path;
  if (out_message != nullptr) {
    *out_message = "master staged update unsupported";
  }
  return false;
#endif
}

bool MasterCriticalActions::queueCriticalCommand(const DeviceCommandContext& ctx, std::string* out_message) {
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(ctx.command_id);
  if (cmd == ManagementCommandId::RestartMasterRequest) {
    requestMasterRestart(false);
    if (out_message != nullptr) {
      *out_message = "restart queued";
    }
    return true;
  }
  if (cmd == ManagementCommandId::ResetMasterRequest) {
    requestMasterRestart(true);
    if (out_message != nullptr) {
      *out_message = "reset queued";
    }
    return true;
  }
  if (cmd == ManagementCommandId::OtaMasterUpdateStart) {
    return requestMasterUpdateFromStaged(ctx.command_arg, out_message);
  }
  if (out_message != nullptr) {
    *out_message = "unsupported critical command in master actions";
  }
  return false;
}

void MasterCriticalActions::tick() {
  if (ota_apply_pending_) {
    ota_apply_pending_ = false;
#if defined(ARDUINO)
    const char* prefix = (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "MASTER";
    std::string msg;
    if (!cfg_.ota_storage->begin(msg)) {
      Serial.printf("[%s][OTA] master staged update failed: storage not ready (%s)\n",
                    prefix,
                    msg.c_str());
      ota_apply_path_.clear();
      return;
    }
    OtaStorageStat st{};
    if (!cfg_.ota_storage->stat(ota_apply_path_, st, msg) || !st.exists || st.is_dir || st.size_bytes == 0U) {
      Serial.printf("[%s][OTA] master staged update failed: staged image missing (%s)\n",
                    prefix,
                    msg.empty() ? ota_apply_path_.c_str() : msg.c_str());
      ota_apply_path_.clear();
      return;
    }

    Serial.printf("[%s][OTA] master staged update apply started path=%s size=%lu\n",
                  prefix,
                  ota_apply_path_.c_str(),
                  static_cast<unsigned long>(st.size_bytes));

    ArduinoOtaApplyExecutor apply(*cfg_.ota_storage, 1024U, cfg_.ota_reboot_on_success);
    OtaApplyRequest req{};
    req.image_path = ota_apply_path_;
    req.image_size = st.size_bytes;
    req.image_crc32 = 0U;
    const OtaApplyResult result = apply.applyImage(req);
    Serial.printf("[%s][OTA] master staged update %s%s%s\n",
                  prefix,
                  result.ok ? "ok" : "failed",
                  result.message.empty() ? "" : ": ",
                  result.message.empty() ? "" : result.message.c_str());
#endif
    ota_apply_path_.clear();
  }

  if (!pending_) {
    return;
  }

#if defined(ARDUINO)
  Serial.printf("[%s] %s requested: reboot in %lums\n",
                (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "MASTER",
                reset_ ? "reset" : "restart",
                static_cast<unsigned long>(cfg_.reboot_delay_ms));
  if (cfg_.pre_reboot_hold_ms > 0U) {
    delay(cfg_.pre_reboot_hold_ms);
  }
#endif

  pending_ = false;
  deepSleepRebootMs(cfg_.reboot_delay_ms);
}

DevicePolicyDecision MasterLoggerMutationPolicy::authorizeCriticalCommand(const DeviceCommandContext& ctx) {
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(ctx.command_id);
  if (cmd == ManagementCommandId::LogLocalClear ||
      cmd == ManagementCommandId::LogLocalControlSet ||
      cmd == ManagementCommandId::LogRemoteClear ||
      cmd == ManagementCommandId::LogRemoteControlSet) {
    if (!allow_logger_mutation_) {
      DevicePolicyDecision d{};
      d.code = DevicePolicyCode::DenyPolicy;
      d.message = "logger mutation denied by master policy";
      return d;
    }
  }
  return {};
}

void MasterActionsTickHook::onRuntimeTick(uint32_t now_ms) {
  if (runtime_ != nullptr) {
    runtime_->tick(now_ms);
  }
  if (actions_ != nullptr) {
    actions_->tick();
  }
}

ManagerConfig MasterNodeBootstrap::defaultManagerConfig(uint8_t channel,
                                                        bool auto_pair_on_discovery,
                                                        bool time_sync_enabled,
                                                        uint32_t time_sync_interval_ms) {
  ManagerConfig cfg{};
  cfg.local_role = Role::Master;
  cfg.channel = channel;
  cfg.discovery_interval_ms = 700;
  cfg.discovery_name = nullptr;  // master does not advertise discovery beacons
  cfg.auto_pair_on_discovery = auto_pair_on_discovery;
  cfg.time_sync_enabled = time_sync_enabled;
  cfg.time_sync_interval_ms = time_sync_interval_ms;
  cfg.time_sync_min_update_delta_s = 1;
  return cfg;
}

bool MasterNodeBootstrap::begin(const Config& cfg) {
  cfg_ = cfg;
  ready_ = false;
  input_ = nullptr;
  control_mux_.clear();
  event_mux_.clearSinks();
  owned_input_.reset();
  owned_io_.reset();
  cli_.reset();
  owned_queue_transports_.clear();
  management_runtime_.reset();
  management_.reset();
  pull_.reset();
  manager_.reset();
  store_.reset();

  if (cfg_.transport == nullptr || cfg_.persistence == nullptr) {
    return false;
  }

  MasterCriticalActions::Config actions_cfg = cfg_.actions_config;
  if (actions_cfg.ota_storage == nullptr) {
    actions_cfg.ota_storage = cfg_.ota_push_storage;
  }
  actions_.configure(actions_cfg);
  policy_.setAllowLoggerMutation(cfg_.allow_logger_mutation);
  actions_hook_.bind(&actions_);

  ManagerConfig manager_cfg = cfg_.manager_config;
  manager_cfg.local_role = Role::Master;  // hard guard: this bootstrap is master-only
  manager_cfg.discovery_name = nullptr;   // hard guard: master never advertises

  store_ = std::make_unique<PairingStore>(manager_cfg.persistence, cfg_.persistence);
  manager_ = std::make_unique<EspNowManager>(manager_cfg,
                                             *cfg_.transport,
                                             store_.get(),
                                             &event_mux_,
                                             cfg_.hooks,
                                             nullptr,
                                             &control_mux_,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             cfg_.logger);
  cfg_.transport->bindManager(manager_.get());
  pull_ = std::make_unique<MasterPullClient>(*manager_);
  management_ = std::make_unique<ManagementService>(Role::Master, *manager_, pull_.get(), &policy_, &actions_);
  management_->bindOtaPushStorage(cfg_.ota_push_storage);
  management_runtime_ = std::make_unique<ManagementRuntime>(*management_);
  if (management_runtime_ == nullptr) {
    return false;
  }
  ManagementQueueTransport* cli_management_transport = nullptr;
  {
    ManagementQueueTransport::Config cli_transport_cfg{};
    cli_transport_cfg.source = ManagementSource::Cli;
    cli_transport_cfg.access_level = ManagementAccessLevel::Owner;
    cli_transport_cfg.max_requests = 64U;
    cli_transport_cfg.max_responses = 128U;
    cli_transport_cfg.max_events = 256U;
    configureQueueOverflowPolicies(cli_transport_cfg, true);
    cli_management_transport = addOwnedQueueTransport(cli_transport_cfg);
  }
  for (IManagementTransport* transport : cfg_.management_transports) {
    if (transport != nullptr) {
      (void)management_runtime_->addTransport(transport);
    }
  }
  actions_hook_.bindManagementRuntime(management_runtime_.get());

  IMasterCliIo* io = cfg_.cli_io;
#if defined(ARDUINO)
  if (io == nullptr) {
    owned_io_ = std::make_unique<ArduinoStreamCliIo>();
    io = owned_io_.get();
  }
#endif
  if (io == nullptr) {
    return false;
  }

  cli_ = std::make_unique<MasterCli>(*manager_,
                                     *pull_,
                                     *io,
                                     &actions_,
                                     cfg_.persistence,
                                     (cfg_.cli_enable_key != nullptr) ? cfg_.cli_enable_key : "clienbl",
                                     cfg_.logger,
                                     management_.get(),
                                     cfg_.remote_log_store,
                                     cfg_.local_storage,
                                     cfg_.ota_push_storage,
                                     cli_management_transport,
                                     management_runtime_.get(),
                                     cfg_.cli_traffic_policy);
  management_->bindMasterCli(cli_.get());

  control_mux_.clear();
  control_mux_.add(management_.get());
  control_mux_.add(cli_.get());
  event_mux_.addSink(cli_.get());
  event_mux_.addSink(management_.get());
  if (cfg_.extra_event_sink != nullptr) {
    event_mux_.addSink(cfg_.extra_event_sink);
  }

  input_ = cfg_.input;
#if defined(ARDUINO)
  if (input_ == nullptr) {
    owned_input_ = std::make_unique<ArduinoStreamCliInput>(*cli_);
    input_ = owned_input_.get();
  }
#endif

  MasterNodeRuntime::Config node_runtime_cfg{};
  node_runtime_cfg.manager = manager_.get();
  node_runtime_cfg.management = nullptr;
  node_runtime_cfg.cli = cli_.get();
  node_runtime_cfg.input = input_;
  node_runtime_cfg.post_tick = &actions_hook_;
  node_runtime_cfg.idle_delay_ms = cfg_.runtime_idle_delay_ms;
  if (!runtime_.begin(node_runtime_cfg)) {
    return false;
  }

  if (cfg_.local_profile_id == kProfileUnknown) {
    return false;
  }
  if (!manager_->setLocalProfile(cfg_.local_profile_id)) {
    return false;
  }
  if (cfg_.local_codec_id != 0U) {
    if (!manager_->setCodecById(cfg_.local_codec_id)) {
      return false;
    }
  }

  ready_ = true;
  return true;
}

void MasterNodeBootstrap::tick(uint32_t now_ms) {
  if (!ready_) {
    return;
  }
  runtime_.tick(now_ms);
}

void MasterNodeBootstrap::loop() {
  if (!ready_) {
    return;
  }
  runtime_.loop();
}

bool MasterNodeBootstrap::bringUp(const MacAddress& local_mac,
                                  bool restore_link,
                                  MacAddress* out_restored_peer) {
  if (!ready_ || manager_ == nullptr || cli_ == nullptr) {
    return false;
  }

  if (out_restored_peer != nullptr) {
    *out_restored_peer = MacAddress{};
  }

  if (!manager_->begin(local_mac)) {
    return false;
  }

  if (cfg_.print_startup_summary) {
    const char* prefix = (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "MASTER";
    Serial.printf("[%s] profile=%s (%u) codec=%s (%u)\n",
                  prefix,
                  manager_->localProfileName(),
                  static_cast<unsigned int>(manager_->localProfileId()),
                  manager_->codec().codecName(),
                  static_cast<unsigned int>(manager_->codec().codecId()));
    Serial.printf("[%s] local mac=%s\n", prefix, macToString(local_mac).c_str());
  }

  if (!restore_link) {
    return true;
  }

  if (!manager_->restore()) {
    return true;
  }

  MacAddress peer{};
  if (manager_->getPairedPeer(peer)) {
    if (cfg_.print_startup_summary) {
      const char* prefix = (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "MASTER";
      Serial.printf("[%s] restored paired link with %s\n", prefix, macToString(peer).c_str());
    }
    if (out_restored_peer != nullptr) {
      *out_restored_peer = peer;
    }
  }
  return true;
}

bool MasterNodeBootstrap::bringUp(bool restore_link,
                                  MacAddress* out_local_mac,
                                  MacAddress* out_restored_peer) {
  MacAddress local_mac{};
#if defined(ESP_PLATFORM)
  esp_read_mac(local_mac.data(), ESP_MAC_WIFI_STA);
#else
  return false;
#endif
  if (out_local_mac != nullptr) {
    *out_local_mac = local_mac;
  }
  if (!bringUp(local_mac, restore_link, out_restored_peer)) {
    return false;
  }
  if (cfg_.print_help_on_bringup && cli_ != nullptr) {
    cli_->printHelp();
  }
  return true;
}

bool MasterNodeBootstrap::addManagementTransport(IManagementTransport* transport) {
  if (management_runtime_ == nullptr || transport == nullptr) {
    return false;
  }
  return management_runtime_->addTransport(transport);
}

bool MasterNodeBootstrap::removeManagementTransport(IManagementTransport* transport) {
  if (management_runtime_ == nullptr || transport == nullptr) {
    return false;
  }
  return management_runtime_->removeTransport(transport);
}

size_t MasterNodeBootstrap::managementTransportCount() const {
  return (management_runtime_ != nullptr) ? management_runtime_->transportCount() : 0U;
}

bool MasterNodeBootstrap::managementRuntimeStats(ManagementRuntime::Stats& out_stats) const {
  if (management_runtime_ == nullptr) {
    return false;
  }
  out_stats = management_runtime_->stats();
  return true;
}

bool MasterNodeBootstrap::managementQueueDepth(ManagementSource source,
                                               size_t& out_req,
                                               size_t& out_resp,
                                               size_t& out_evt) const {
  out_req = 0U;
  out_resp = 0U;
  out_evt = 0U;
  const ManagementQueueTransport* q = ownedQueueTransport(source);
  if (q == nullptr) {
    return false;
  }
  out_req = q->pendingRequestCount();
  out_resp = q->pendingResponseCount();
  out_evt = q->pendingEventCount();
  return true;
}

ManagementQueueTransport* MasterNodeBootstrap::ownedQueueTransport(ManagementSource source) {
  for (const auto& t : owned_queue_transports_) {
    if (t != nullptr && t->source() == source) {
      return t.get();
    }
  }
  return nullptr;
}

const ManagementQueueTransport* MasterNodeBootstrap::ownedQueueTransport(ManagementSource source) const {
  for (const auto& t : owned_queue_transports_) {
    if (t != nullptr && t->source() == source) {
      return t.get();
    }
  }
  return nullptr;
}

ManagementQueueTransport* MasterNodeBootstrap::addOwnedQueueTransport(
    ManagementQueueTransport::Config cfg) {
  if (management_runtime_ == nullptr) {
    return nullptr;
  }
  auto owned = std::make_unique<ManagementQueueTransport>(cfg);
  if (owned == nullptr) {
    return nullptr;
  }
  ManagementQueueTransport* raw = owned.get();
  if (!management_runtime_->addTransport(raw)) {
    return nullptr;
  }
  owned_queue_transports_.push_back(std::move(owned));
  return raw;
}

ManagementQueueTransport* MasterNodeBootstrap::ensureOwnedQueueTransport(ManagementSource source,
                                                                         size_t max_requests,
                                                                         size_t max_responses,
                                                                         size_t max_events,
                                                                         ManagementAccessLevel access_level,
                                                                         bool drop_oldest_when_full) {
  if (source == ManagementSource::Unknown) {
    return nullptr;
  }
  if (ManagementQueueTransport* existing = ownedQueueTransport(source); existing != nullptr) {
    return existing;
  }
  ManagementQueueTransport::Config cfg{};
  cfg.source = source;
  cfg.access_level = access_level;
  cfg.max_requests = max_requests;
  cfg.max_responses = max_responses;
  cfg.max_events = max_events;
  configureQueueOverflowPolicies(cfg, drop_oldest_when_full);
  return addOwnedQueueTransport(cfg);
}

ManagementFrontendAdapter MasterNodeBootstrap::makeFrontendAdapter(ManagementSource source,
                                                                   bool create_transport,
                                                                   size_t max_requests,
                                                                   size_t max_responses,
                                                                   size_t max_events,
                                                                   ManagementAccessLevel access_level,
                                                                   bool drop_oldest_when_full) {
  ManagementQueueTransport* transport = ownedQueueTransport(source);
  if (transport == nullptr && create_transport) {
    transport = ensureOwnedQueueTransport(source,
                                          max_requests,
                                          max_responses,
                                          max_events,
                                          access_level,
                                          drop_oldest_when_full);
  }
  return ManagementFrontendAdapter(transport, management_runtime_.get(), management_.get(), source, access_level);
}

ManagementFrontendAdapter MasterNodeBootstrap::makeWifiFrontendAdapter(bool create_transport,
                                                                       size_t max_requests,
                                                                       size_t max_responses,
                                                                       size_t max_events,
                                                                       ManagementAccessLevel access_level,
                                                                       bool drop_oldest_when_full) {
  return makeFrontendAdapter(ManagementSource::Wifi,
                             create_transport,
                             max_requests,
                             max_responses,
                             max_events,
                             access_level,
                             drop_oldest_when_full);
}

ManagementFrontendAdapter MasterNodeBootstrap::makeBleFrontendAdapter(bool create_transport,
                                                                      size_t max_requests,
                                                                      size_t max_responses,
                                                                      size_t max_events,
                                                                      ManagementAccessLevel access_level,
                                                                      bool drop_oldest_when_full) {
  return makeFrontendAdapter(ManagementSource::Ble,
                             create_transport,
                             max_requests,
                             max_responses,
                             max_events,
                             access_level,
                             drop_oldest_when_full);
}

ManagementFrontendAdapter MasterNodeBootstrap::makeCustomFrontendAdapter(bool create_transport,
                                                                         size_t max_requests,
                                                                         size_t max_responses,
                                                                         size_t max_events,
                                                                         ManagementAccessLevel access_level,
                                                                         bool drop_oldest_when_full) {
  return makeFrontendAdapter(ManagementSource::Custom,
                             create_transport,
                             max_requests,
                             max_responses,
                             max_events,
                             access_level,
                             drop_oldest_when_full);
}

void MasterNodeBootstrap::clearOwnedQueueTransports() {
  if (management_runtime_ != nullptr) {
    for (const auto& t : owned_queue_transports_) {
      if (t != nullptr) {
        (void)management_runtime_->removeTransport(t.get());
      }
    }
  }
  owned_queue_transports_.clear();
}

}  // namespace espnow_link
