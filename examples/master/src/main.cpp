#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <WiFi.h>

#include "profile_catalog/masters/icm/icm_profile.hpp"
#include "profile_catalog/registry/slave_schema_registry.hpp"
#include "espnow_link/espnow_link.hpp"
#include "wifi_frontend_ui.hpp"

using namespace espnow_link;

namespace {

constexpr int kSdSckPin = 42;
constexpr int kSdMisoPin = 39;
constexpr int kSdMosiPin = 38;
constexpr int kSdCsPin = 41;
constexpr uint32_t kSdSpiHz = 10000000UL;  // ICM (stability-first for sustained OTA reads)
constexpr uint32_t kLogRotateSpiffsBytes = 1572864U;   // 1.5 MiB
constexpr uint32_t kLogRotateSdBytes = 52428800U;      // 50 MiB
constexpr const char* kUiApSsid = "ENL-MASTER-UI";
constexpr const char* kUiApPassword = "12345678";
constexpr const char* kRouterSsid = "geyehub";
constexpr const char* kRouterPassword = "123456789";

EspNowArduinoTransport g_transport;
PreferencesStore g_nvs("enl_master");
PersistenceAdapter<PreferencesStore> g_persist(g_nvs);
ArduinoSelectableLogStorageBackend g_log_backend(false, ArduinoSelectableLogStorageBackend::SpiffsOwnershipMode::ExternalMounted);
EspLogStore g_log_store(g_log_backend);
EspLogStore g_remote_log_export_store(g_log_backend, []() {
  LogStoreConfig c{};
  c.log_path = "/rlg.bin";
  c.index_path = "/rlg.idx";
  c.max_bytes = 1572864U;
  return c;
}());
LibraryLogger g_logger(&g_log_store);
DebugHooks g_hooks("MASTER", nullptr);

ManagementService* g_mgmt = nullptr;
MasterCli* g_cli = nullptr;
ArduinoStreamCliIo g_cli_io;
MasterNodeBootstrap g_master_bootstrap;
ManagementFrontendAdapter g_wifi_frontend{};
MasterWifiFrontendUi g_wifi_ui{};
SPIClass g_sd_spi(HSPI);
bool g_sd_ready = false;
bool g_spiffs_ready = false;
ArduinoStorageExplorer g_storage_explorer;
ArduinoSelectableOtaStorageBackend g_ota_backend;
MasterStorageBootstrap g_storage_bootstrap;

const char* wifiModeToString(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_MODE_STA:
      return "sta";
    case WIFI_MODE_AP:
      return "ap";
    case WIFI_MODE_APSTA:
      return "apsta";
    case WIFI_MODE_NULL:
      return "off";
    default:
      return "unknown";
  }
}

const char* wifiStatusToString(wl_status_t st) {
  switch (st) {
    case WL_CONNECTED:
      return "connected";
    case WL_DISCONNECTED:
      return "disconnected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_NO_SSID_AVAIL:
      return "ssid_unavailable";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_IDLE_STATUS:
      return "idle";
    default:
      return "unknown";
  }
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8U);
  for (size_t i = 0U; i < in.length(); ++i) {
    const char c = in.charAt(i);
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

class AppRadioTransitionTestRunner {
 public:
  struct Config {
    const char* ap_ssid = kUiApSsid;
    const char* ap_password = kUiApPassword;
    const char* router_ssid = kRouterSsid;
    const char* router_password = kRouterPassword;
    uint8_t fallback_slave_channel = 1U;
    uint32_t router_connect_timeout_ms = 15000U;
  };

  void bind(ManagementFrontendAdapter* adapter, EspNowArduinoTransport* transport, const Config& cfg) {
    adapter_ = adapter;
    transport_ = transport;
    cfg_ = cfg;
  }

  bool start() {
    if (running_) {
      Serial.println("[MASTER][APP-RADIO-TEST] already running");
      return false;
    }
    if (adapter_ == nullptr || transport_ == nullptr) {
      Serial.println("[MASTER][APP-RADIO-TEST] not ready (adapter/transport missing)");
      return false;
    }

    running_ = true;
    finished_ = false;
    passed_ = false;
    aborted_ = false;
    radio_hard_down_ = false;
    has_target_peer_ = false;
    phase_started_ms_ = millis();
    phase_deadline_ms_ = 0U;
    phase_message_ = "starting";
    last_error_ = "";
    saved_slave_channel_ = cfg_.fallback_slave_channel;
    step_ = Step::PrecheckPaired;

    Serial.println("[MASTER][APP-RADIO-TEST] start requested from web UI");
    Serial.println("[MASTER][APP-RADIO-TEST] expected baseline: WIFI_AP_STA, user connected to ESP hotspot");
    Serial.println("[MASTER][APP-RADIO-TEST] process: baseline ping -> hardDeinit -> STA-only -> hardReinit -> "
                   "ping -> hardDeinit -> router connect 3s -> disconnect + channel restore -> hardReinit -> "
                   "ping -> AP+STA restore -> ping");
    return true;
  }

  bool abort() {
    if (!running_) {
      Serial.println("[MASTER][APP-RADIO-TEST] abort ignored (not running)");
      return false;
    }
    Serial.println("[MASTER][APP-RADIO-TEST] abort requested from web UI");
    running_ = false;
    finished_ = true;
    passed_ = false;
    aborted_ = true;
    phase_message_ = "ABORTED";
    last_error_ = "aborted_by_user";
    const bool cleanup_ok = safeCleanup_();
    Serial.printf("[MASTER][APP-RADIO-TEST] ABORTED cleanup=%s\n", cleanup_ok ? "ok" : "warn");
    return cleanup_ok;
  }

  void tick() {
    if (!running_) {
      return;
    }

    switch (step_) {
      case Step::PrecheckPaired: {
        phaseMessage_("precheck paired slave and target selection");
        if (!refreshTargetPeer_()) {
          fail_("no paired slave available; pair one slave first");
          return;
        }
        if (!refreshSavedSlaveChannel_()) {
          Serial.printf("[MASTER][APP-RADIO-TEST] warning: channel.runtime.get failed, fallback channel=%u\n",
                        static_cast<unsigned int>(saved_slave_channel_));
        }
        step_ = Step::BaselinePing;
        return;
      }

      case Step::BaselinePing: {
        phaseMessage_("step1 baseline ping in WIFI_AP_STA");
        if (!pingSlave_("baseline_apsta")) {
          fail_("baseline slave ping failed");
          return;
        }
        step_ = Step::BeginTransition;
        return;
      }

      case Step::BeginTransition: {
        phaseMessage_("step2 hard deinit radio before STA-only transition");
        if (!hardDeinit_()) {
          fail_("hardDeinitRadio failed before STA-only");
          return;
        }
        if (!verifyBusyGate_()) {
          fail_("busy gate validation failed");
          return;
        }
        step_ = Step::SwitchStaOnly;
        return;
      }

      case Step::SwitchStaOnly: {
        phaseMessage_("step2 switch to WIFI_STA only (not connected)");
        Serial.println("[MASTER][APP-RADIO-TEST] prompt: switching to STA-only now");
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_STA);
        phase_deadline_ms_ = millis() + 1200U;
        step_ = Step::WaitStaOnlySettle;
        return;
      }

      case Step::WaitStaOnlySettle: {
        if (static_cast<int32_t>(millis() - phase_deadline_ms_) < 0) {
          return;
        }
        Serial.printf("[MASTER][APP-RADIO-TEST] wifi mode=%s status=%s ch=%u\n",
                      wifiModeToString(WiFi.getMode()),
                      wifiStatusToString(WiFi.status()),
                      static_cast<unsigned int>(WiFi.channel()));
        if (!applySlaveChannel_()) {
          fail_("failed to restore slave channel in STA-only step");
          return;
        }
        step_ = Step::PingStaOnly;
        return;
      }

      case Step::PingStaOnly: {
        if (!hardReinit_()) {
          fail_("hardReinitRadio failed for STA-only ping");
          return;
        }
        phaseMessage_("step2 ping slave in STA-only not connected");
        if (!pingSlave_("sta_only_no_router")) {
          fail_("slave ping failed in STA-only mode");
          return;
        }
        step_ = Step::ConnectRouter;
        return;
      }

      case Step::ConnectRouter: {
        if (!hardDeinit_()) {
          fail_("hardDeinitRadio failed before router connect");
          return;
        }
        phaseMessage_("step3 connect STA to router geyehub");
        Serial.printf("[MASTER][APP-RADIO-TEST] prompt: connecting to router ssid=%s\n", cfg_.router_ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(cfg_.router_ssid, cfg_.router_password);
        phase_deadline_ms_ = millis() + cfg_.router_connect_timeout_ms;
        step_ = Step::WaitRouterConnected;
        return;
      }

      case Step::WaitRouterConnected: {
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("[MASTER][APP-RADIO-TEST] router connected ip=%s ch=%u\n",
                        WiFi.localIP().toString().c_str(),
                        static_cast<unsigned int>(WiFi.channel()));
          phase_deadline_ms_ = millis() + 3000U;
          step_ = Step::HoldRouter3s;
          return;
        }
        if (static_cast<int32_t>(millis() - phase_deadline_ms_) >= 0) {
          fail_("router connection timeout");
        }
        return;
      }

      case Step::HoldRouter3s: {
        if (static_cast<int32_t>(millis() - phase_deadline_ms_) < 0) {
          return;
        }
        Serial.printf("[MASTER][APP-RADIO-TEST] held 3s on router; router channel=%u\n",
                      static_cast<unsigned int>(WiFi.channel()));
        step_ = Step::DisconnectRouter;
        return;
      }

      case Step::DisconnectRouter: {
        phaseMessage_("step4 disconnect from router and restore slave channel");
        Serial.println("[MASTER][APP-RADIO-TEST] prompt: disconnecting from router now");
        WiFi.disconnect(false, false);
        phase_deadline_ms_ = millis() + 1200U;
        step_ = Step::WaitRouterDisconnect;
        return;
      }

      case Step::WaitRouterDisconnect: {
        if (static_cast<int32_t>(millis() - phase_deadline_ms_) < 0) {
          return;
        }
        if (!applySlaveChannel_()) {
          fail_("failed to restore slave channel after router disconnect");
          return;
        }
        step_ = Step::PingAfterRouterDisconnect;
        return;
      }

      case Step::PingAfterRouterDisconnect: {
        if (!hardReinit_()) {
          fail_("hardReinitRadio failed after router disconnect");
          return;
        }
        phaseMessage_("step4 ping slave after router disconnect");
        if (!pingSlave_("post_router_disconnect")) {
          fail_("slave ping failed after router disconnect");
          return;
        }
        step_ = Step::RestoreApSta;
        return;
      }

      case Step::RestoreApSta: {
        phaseMessage_("step5 restore WIFI_AP_STA so user can reconnect to ESP hotspot");
        WiFi.mode(WIFI_AP_STA);
        const bool ap_ok = WiFi.softAP(cfg_.ap_ssid, cfg_.ap_password, saved_slave_channel_);
        Serial.printf("[MASTER][APP-RADIO-TEST] AP restore %s ssid=%s ap_ip=%s ch=%u\n",
                      ap_ok ? "ok" : "failed",
                      cfg_.ap_ssid,
                      WiFi.softAPIP().toString().c_str(),
                      static_cast<unsigned int>(saved_slave_channel_));
        if (!ap_ok) {
          fail_("failed to restore AP+STA hotspot");
          return;
        }
        phase_deadline_ms_ = millis() + 1800U;
        step_ = Step::WaitApStaSettle;
        return;
      }

      case Step::WaitApStaSettle: {
        if (static_cast<int32_t>(millis() - phase_deadline_ms_) < 0) {
          return;
        }
        if (!applySlaveChannel_()) {
          fail_("failed to re-apply slave channel in AP+STA");
          return;
        }
        step_ = Step::PingFinal;
        return;
      }

      case Step::PingFinal: {
        phaseMessage_("final ping in restored WIFI_AP_STA");
        if (!pingSlave_("final_apsta")) {
          fail_("final slave ping failed in AP+STA");
          return;
        }
        running_ = false;
        finished_ = true;
        passed_ = true;
        aborted_ = false;
        phase_message_ = "PASS";
        Serial.println("[MASTER][APP-RADIO-TEST] PASS");
        return;
      }

      default:
        return;
    }
  }

  String statusJson() const {
    String json = "{";
    json += "\"ok\":true,";
    json += "\"running\":" + String(running_ ? "true" : "false") + ",";
    json += "\"finished\":" + String(finished_ ? "true" : "false") + ",";
    json += "\"passed\":" + String(passed_ ? "true" : "false") + ",";
    json += "\"aborted\":" + String(aborted_ ? "true" : "false") + ",";
    json += "\"radio_hard_down\":" + String(radio_hard_down_ ? "true" : "false") + ",";
    json += "\"step\":\"" + String(stepName_(step_)) + "\",";
    json += "\"message\":\"" + jsonEscape(phase_message_) + "\",";
    json += "\"error\":\"" + jsonEscape(last_error_) + "\",";
    json += "\"saved_slave_channel\":" + String(static_cast<unsigned int>(saved_slave_channel_)) + ",";
    json += "\"wifi_mode\":\"" + String(wifiModeToString(WiFi.getMode())) + "\",";
    json += "\"wifi_status\":\"" + String(wifiStatusToString(WiFi.status())) + "\",";
    json += "\"wifi_channel\":" + String(static_cast<unsigned int>(WiFi.channel()));
    json += "}";
    return json;
  }

 private:
  enum class Step : uint8_t {
    Idle = 0,
    PrecheckPaired,
    BaselinePing,
    BeginTransition,
    SwitchStaOnly,
    WaitStaOnlySettle,
    PingStaOnly,
    ConnectRouter,
    WaitRouterConnected,
    HoldRouter3s,
    DisconnectRouter,
    WaitRouterDisconnect,
    PingAfterRouterDisconnect,
    RestoreApSta,
    WaitApStaSettle,
    PingFinal,
  };

  static const char* stepName_(Step step) {
    switch (step) {
      case Step::Idle: return "idle";
      case Step::PrecheckPaired: return "precheck_paired";
      case Step::BaselinePing: return "baseline_ping";
      case Step::BeginTransition: return "begin_transition";
      case Step::SwitchStaOnly: return "switch_sta_only";
      case Step::WaitStaOnlySettle: return "wait_sta_only_settle";
      case Step::PingStaOnly: return "ping_sta_only";
      case Step::ConnectRouter: return "connect_router";
      case Step::WaitRouterConnected: return "wait_router_connected";
      case Step::HoldRouter3s: return "hold_router_3s";
      case Step::DisconnectRouter: return "disconnect_router";
      case Step::WaitRouterDisconnect: return "wait_router_disconnect";
      case Step::PingAfterRouterDisconnect: return "ping_after_router_disconnect";
      case Step::RestoreApSta: return "restore_apsta";
      case Step::WaitApStaSettle: return "wait_apsta_settle";
      case Step::PingFinal: return "ping_final";
      default: return "unknown";
    }
  }

  void phaseMessage_(const char* msg) {
    if (msg == nullptr) {
      return;
    }
    if (phase_message_ == msg) {
      return;
    }
    phase_message_ = msg;
    Serial.printf("[MASTER][APP-RADIO-TEST] step=%s msg=%s\n", stepName_(step_), msg);
  }

  void fail_(const char* err) {
    running_ = false;
    finished_ = true;
    passed_ = false;
    aborted_ = false;
    phase_message_ = "FAIL";
    last_error_ = (err != nullptr) ? err : "unknown error";
    Serial.printf("[MASTER][APP-RADIO-TEST] FAIL: %s\n", last_error_.c_str());
    (void)safeCleanup_();
  }

  bool safeCleanup_() {
    bool ok = true;
    if (radio_hard_down_) {
      if (!hardReinit_()) {
        ok = false;
      }
    }
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(cfg_.ap_ssid, cfg_.ap_password, saved_slave_channel_)) {
      Serial.println("[MASTER][APP-RADIO-TEST] cleanup warning: failed to restore AP");
      ok = false;
    }
    if (!applySlaveChannel_()) {
      Serial.println("[MASTER][APP-RADIO-TEST] cleanup warning: failed to restore slave channel");
      ok = false;
    }
    return ok;
  }

  bool hardDeinit_() {
    if (radio_hard_down_) {
      return true;
    }
    ManagementFrontendAdapter::RadioHardDeinitResult deinit_result{};
    ManagementFrontendAdapter::RadioHardDeinitOptions opts{};
    opts.stop_discovery = true;
    opts.disable_live_monitor = true;
    opts.clear_master_update_guard = true;
    opts.cancel_deferred_operations = true;
    opts.cancel_pending_mutating_requests = true;
    opts.clear_service_queues = true;
    opts.clear_transport_queues = true;
    opts.clear_adapter_cache = false;
    const bool ok = adapter_->hardDeinitRadio(opts, &deinit_result);
    Serial.printf("[MASTER][APP-RADIO-TEST] hard deinit %s epoch=%lu summary=%s\n",
                  ok ? "ok" : "fail",
                  static_cast<unsigned long>(deinit_result.radio_epoch),
                  deinit_result.summary.c_str());
    radio_hard_down_ = ok;
    return ok;
  }

  bool hardReinit_() {
    if (!radio_hard_down_) {
      return true;
    }
    ManagementFrontendAdapter::RadioHardReinitResult reinit_result{};
    ManagementFrontendAdapter::RadioHardReinitOptions opts{};
    opts.restore_link = true;
    opts.reset_service_state = true;
    opts.clear_adapter_cache = false;
    opts.resync_paired_snapshot = true;
    opts.resync_discovery_snapshot = false;
    opts.resync_event_ring = true;
    opts.resume_timeout_ms = 3000U;
    const bool ok = adapter_->hardReinitRadio(opts, &reinit_result);
    Serial.printf("[MASTER][APP-RADIO-TEST] hard reinit %s epoch=%lu summary=%s\n",
                  ok ? "ok" : "fail",
                  static_cast<unsigned long>(reinit_result.radio_epoch),
                  reinit_result.summary.c_str());
    radio_hard_down_ = !ok;
    return ok;
  }

  bool verifyBusyGate_() {
    ManagementFrontendAdapter::CommandRunResult run{};
    const bool ok = adapter_->commandRunAndWait(
        static_cast<uint16_t>(ManagementCommandId::DiscoveryStart),
        management_utils::buildDiscoveryStartPayload(1000U),
        run,
        1500U);
    const bool gate_ok = (!ok && run.status == ManagementStatus::BusyRadioTransition);
    Serial.printf("[MASTER][APP-RADIO-TEST] busy gate check=%s status=%s(0x%04X)\n",
                  gate_ok ? "ok" : "fail",
                  management_utils::managementStatusToString(run.status),
                  static_cast<unsigned int>(run.status));
    return gate_ok;
  }

  bool refreshTargetPeer_() {
    std::vector<ManagementPairedPeerInfo> peers{};
    if (!adapter_->pairedSnapshotGetResolved(peers, 3000U)) {
      return false;
    }
    if (peers.empty()) {
      return false;
    }
    target_peer_ = peers.front().peer;
    has_target_peer_ = true;
    Serial.printf("[MASTER][APP-RADIO-TEST] target peer=%s role=%u\n",
                  macToString(target_peer_).c_str(),
                  static_cast<unsigned int>(peers.front().role_code));
    return true;
  }

  bool refreshSavedSlaveChannel_() {
    ManagementFrontendAdapter::CommandRunResult run{};
    if (!adapter_->commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::ChannelRuntimeGet), {}, run, 2500U)) {
      return false;
    }
    if (!run.has_response) {
      return false;
    }
    ManagementRuntimeChannelStatusPayload status{};
    if (!ManagementFrontendAdapter::decodeChannelRuntimeStatusResponse(run.response, status)) {
      return false;
    }
    if (status.current_channel >= 1U && status.current_channel <= 14U) {
      saved_slave_channel_ = status.current_channel;
    }
    Serial.printf("[MASTER][APP-RADIO-TEST] saved slave channel=%u\n",
                  static_cast<unsigned int>(saved_slave_channel_));
    return true;
  }

  bool applySlaveChannel_() {
    if (saved_slave_channel_ < 1U || saved_slave_channel_ > 14U) {
      return false;
    }
    const bool ok = transport_->setChannel(saved_slave_channel_);
    Serial.printf("[MASTER][APP-RADIO-TEST] apply slave channel=%u => %s\n",
                  static_cast<unsigned int>(saved_slave_channel_),
                  ok ? "ok" : "fail");
    return ok;
  }

  bool pingSlave_(const char* stage) {
    if (!has_target_peer_) {
      return false;
    }
    ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = 3000U;
    submit_options.has_target_peer = true;
    submit_options.target_peer = target_peer_;
    ManagementFrontendAdapter::CommandRunResult run{};
    const bool ok =
        adapter_->commandRunAndWait(static_cast<uint16_t>(ManagementCommandId::PingGet), {}, run, submit_options);
    Serial.printf("[MASTER][APP-RADIO-TEST] ping stage=%s peer=%s result=%s status=%s(0x%04X)\n",
                  (stage != nullptr) ? stage : "-",
                  macToString(target_peer_).c_str(),
                  ok ? "ok" : "fail",
                  management_utils::managementStatusToString(run.status),
                  static_cast<unsigned int>(run.status));
    return ok;
  }

  ManagementFrontendAdapter* adapter_ = nullptr;
  EspNowArduinoTransport* transport_ = nullptr;
  Config cfg_{};
  bool running_ = false;
  bool finished_ = false;
  bool passed_ = false;
  bool aborted_ = false;
  bool radio_hard_down_ = false;
  bool has_target_peer_ = false;
  Step step_ = Step::Idle;
  uint32_t phase_started_ms_ = 0U;
  uint32_t phase_deadline_ms_ = 0U;
  uint8_t saved_slave_channel_ = 1U;
  MacAddress target_peer_{};
  String phase_message_{};
  String last_error_{};
};

AppRadioTransitionTestRunner g_radio_test;

void applyWifiFrontendOrchestrationDefaultsFromNvs() {
  std::vector<ManagementFrontendAdapter::CachedSettingView> settings{};

  uint32_t wait_ms = 3000U;
  (void)g_nvs.getU32(PCAT_ICM_KEY_OWAIT, wait_ms);
  ManagementFrontendAdapter::CachedSettingView s_wait{};
  s_wait.key = PCAT_ICM_SET_OWAIT;
  s_wait.value = std::to_string(static_cast<unsigned long>(wait_ms));
  settings.push_back(s_wait);

  uint32_t ring_cap = 256U;
  (void)g_nvs.getU32(PCAT_ICM_KEY_OEVRG, ring_cap);
  ManagementFrontendAdapter::CachedSettingView s_ring{};
  s_ring.key = PCAT_ICM_SET_OEVRG;
  s_ring.value = std::to_string(static_cast<unsigned long>(ring_cap));
  settings.push_back(s_ring);

  bool batch_confirm = true;
  (void)g_nvs.getBool(PCAT_ICM_KEY_OBCFM, batch_confirm);
  ManagementFrontendAdapter::CachedSettingView s_confirm{};
  s_confirm.key = PCAT_ICM_SET_OBCFM;
  s_confirm.value = batch_confirm ? "1" : "0";
  settings.push_back(s_confirm);

  bool refresh_cache = true;
  (void)g_nvs.getBool(PCAT_ICM_KEY_ORFRC, refresh_cache);
  ManagementFrontendAdapter::CachedSettingView s_refresh{};
  s_refresh.key = PCAT_ICM_SET_ORFRC;
  s_refresh.value = refresh_cache ? "1" : "0";
  settings.push_back(s_refresh);

  g_wifi_frontend.applyOrchestrationDefaultsFromResolvedSettings(settings);
  Serial.printf("[MASTER][WIFI-FE] orchestration defaults wait_ms=%lu ring=%lu confirm=%s refresh=%s\n",
                static_cast<unsigned long>(g_wifi_frontend.orchestrationWaitDefaultMs()),
                static_cast<unsigned long>(g_wifi_frontend.eventRingCapacity()),
                g_wifi_frontend.batchConfirmDefault() ? "1" : "0",
                g_wifi_frontend.batchRefreshCacheDefault() ? "1" : "0");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("[MASTER] boot");
  // Keep STA interface up for ESP-NOW TX compatibility, but do not connect to any router.
  WiFi.mode(WIFI_AP_STA);
  Serial.println("[MASTER] wifi initialized by app (AP+STA, no STA connect)");

  if (!g_nvs.begin()) {
    Serial.println("[MASTER] preferences begin failed");
  }
  g_spiffs_ready = SPIFFS.begin(false);
  if (!g_spiffs_ready) {
    Serial.println("[MASTER] SPIFFS begin failed");
  }
  g_sd_spi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  g_sd_ready = SD.begin(kSdCsPin, g_sd_spi, kSdSpiHz, "/sd", 5U, false);
  Serial.printf("[MASTER] SD init %s (sck=%d miso=%d mosi=%d cs=%d hz=%lu)\n",
                g_sd_ready ? "ok" : "failed",
                kSdSckPin,
                kSdMisoPin,
                kSdMosiPin,
                kSdCsPin,
                static_cast<unsigned long>(kSdSpiHz));
  MasterStorageBootstrap::Config storage_cfg{};
  storage_cfg.sd_ready = g_sd_ready;
  storage_cfg.spiffs_ready = g_spiffs_ready;
  storage_cfg.sd_fs = &SD;
  storage_cfg.spiffs_fs = &SPIFFS;
  storage_cfg.log_backend = &g_log_backend;
  storage_cfg.storage_explorer = &g_storage_explorer;
  storage_cfg.ota_backend = &g_ota_backend;
  storage_cfg.log_store = &g_log_store;
  storage_cfg.remote_log_store = &g_remote_log_export_store;
  storage_cfg.logger = &g_logger;
  storage_cfg.prefer_sd_for_logs = true;
  storage_cfg.prefer_sd_for_explorer = true;
  storage_cfg.log_rotate_spiffs_bytes = kLogRotateSpiffsBytes;
  storage_cfg.log_rotate_sd_bytes = kLogRotateSdBytes;
  ArduinoNodeStorageBootstrap::Report storage_report{};
  if (!g_storage_bootstrap.begin(storage_cfg, &storage_report)) {
    Serial.println("[MASTER] storage bootstrap failed");
  }

  // Register app-owned schemas before bootstrap constructs EspNowManager,
  // so app-owned profile IDs are the canonical entries in ProfileRegistry.
  const app_owned::MasterSchemaPackage icm_pkg = app_owned::makeIcmMasterSchemaPackage();
  if (!icm_pkg.valid() || !icm_pkg.registerProfile()) {
    Serial.println("[MASTER][PROFILE] ICM app-owned profile registration failed");
    return;
  }
  if (!app_owned::registerSupportedSlaveMasterSchemas(kCodecIdCompactIndexed)) {
    Serial.println("[MASTER][PROFILE] supported slave schema registration failed");
    return;
  }

  MasterNodeBootstrap::Config boot_cfg{};
  boot_cfg.manager_config = MasterNodeBootstrap::defaultManagerConfig(1, false, true, 5000);
  boot_cfg.transport = &g_transport;
  boot_cfg.persistence = &g_persist;
  boot_cfg.hooks = &g_hooks;
  boot_cfg.logger = &g_logger;
  boot_cfg.cli_io = &g_cli_io;
  boot_cfg.local_storage = &g_storage_explorer;
  boot_cfg.ota_push_storage = &g_ota_backend;
  boot_cfg.remote_log_store = &g_remote_log_export_store;
  boot_cfg.cli_enable_key = "clienbl";
  boot_cfg.local_profile_id = icm_pkg.profile_id;
  boot_cfg.local_codec_id = icm_pkg.codec_id;
  boot_cfg.runtime_idle_delay_ms = 2U;
  boot_cfg.actions_config.log_prefix = "MASTER";
  boot_cfg.actions_config.pre_reboot_hold_ms = 80U;
  boot_cfg.actions_config.reboot_delay_ms = 1000U;
  if (!g_master_bootstrap.begin(boot_cfg)) {
    Serial.println("[MASTER] runtime bootstrap begin failed");
  }

  g_mgmt = g_master_bootstrap.management();
  g_cli = g_master_bootstrap.cliInstance();
  if (g_mgmt == nullptr || g_cli == nullptr) {
    Serial.println("[MASTER] runtime bootstrap invalid handles");
    return;
  }
  g_wifi_frontend = g_master_bootstrap.makeWifiFrontendAdapter(true);
  applyWifiFrontendOrchestrationDefaultsFromNvs();

  if (!g_master_bootstrap.bringUp()) {
    Serial.println("[MASTER] manager begin failed");
  }

  MasterWifiFrontendUi::Config ui_cfg{};
  // AP-only mode: do not attempt STA at all.
  ui_cfg.ssid = "";
  ui_cfg.password = "";
  ui_cfg.ap_ssid = kUiApSsid;
  ui_cfg.ap_password = kUiApPassword;
  ui_cfg.espnow_channel = 1;
  ui_cfg.prefer_runtime_channel = false;
  ui_cfg.enforce_channel_match = false;
  ui_cfg.fallback_to_ap = true;
  ui_cfg.port = 80;
  AppRadioTransitionTestRunner::Config radio_test_cfg{};
  radio_test_cfg.ap_ssid = ui_cfg.ap_ssid;
  radio_test_cfg.ap_password = ui_cfg.ap_password;
  radio_test_cfg.router_ssid = kRouterSsid;
  radio_test_cfg.router_password = kRouterPassword;
  radio_test_cfg.fallback_slave_channel = ui_cfg.espnow_channel;
  g_radio_test.bind(&g_wifi_frontend, &g_transport, radio_test_cfg);
  g_wifi_ui.setRadioTransitionTestHooks(
      []() -> bool { return g_radio_test.start(); },
      []() -> String { return g_radio_test.statusJson(); },
      []() -> bool { return g_radio_test.abort(); });
  g_wifi_ui.begin(&g_wifi_frontend, ui_cfg);
  WiFi.mode(WIFI_AP_STA);
  Serial.println("[MASTER] wifi forced by main to AP+STA (AP ch=1, STA not connected)");
  Serial.println("[MASTER] radio transition app test ready: start from web UI button 'Start Radio Transition Test'");
}

void loop() {
  g_master_bootstrap.loop();
  g_radio_test.tick();
  g_wifi_ui.loop();
}
