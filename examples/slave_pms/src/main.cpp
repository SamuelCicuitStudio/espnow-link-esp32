#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>

#include <cstdlib>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <sys/time.h>
#include <time.h>

#include "profile_catalog/slaves/pms/pms_profile.hpp"
#include "espnow_link/espnow_link.hpp"

using namespace espnow_link;

namespace {

constexpr int kSdSckPin = 42;
constexpr int kSdMisoPin = 39;
constexpr int kSdMosiPin = 38;
constexpr int kSdCsPin = 41;
constexpr uint32_t kSdSpiHz = 10000000UL;  // PMS (stability-first for sustained OTA/log reads)
constexpr uint32_t kLogRotateSpiffsBytes = 1572864U;   // 1.5 MiB
constexpr uint32_t kLogRotateSdBytes = 52428800U;      // 50 MiB
constexpr const char* kSlaveSwVersion = "3.1.1";
constexpr bool kVerbosePullRequestTrace = false;
const std::string kSlaveBuildId = app_owned::buildid::defaultBuildId(BUILD_ID, PCAT_PMS_BUILD_TAG, kSlaveSwVersion);

constexpr const char* kResetKey = "rstkey";
constexpr const char* kRestartRequestKey = "rstrq";

EspNowArduinoTransport g_transport;
PreferencesStore g_nvs{};
ArduinoSelectableLogStorageBackend g_log_backend(false, ArduinoSelectableLogStorageBackend::SpiffsOwnershipMode::ExternalMounted);
EspLogStore g_log_store(g_log_backend);
LibraryLogger g_logger(&g_log_store);

PersistenceAdapter<PreferencesStore> g_persist_adapter(g_nvs);
DebugHooks g_hooks("SLAVE", nullptr);

SlaveLoggedTimeSink g_time_sink;
SPIClass g_sd_spi(HSPI);
bool g_sd_ready = false;
bool g_spiffs_ready = false;
ArduinoStorageExplorer g_storage_explorer;
ArduinoSelectableOtaStorageBackend g_ota_backend;
SlaveStorageBootstrap g_storage_bootstrap;
SlaveOtaVersionGate g_ota_gate;
MacAddress g_local_mac{};
std::unique_ptr<app_owned::PmsAppDescriptorProvider> g_pms_provider;
app_owned::SlaveSchemaPackage g_pms_schema{};
SlaveOtaBootstrap g_ota_bootstrap;
SlaveNodeBootstrap g_slave_bootstrap;

struct PmsRuntimeState {
  uint32_t seed = 0U;
};

PmsRuntimeState g_pms_runtime{};

bool readPmsTelemetry(void* user, app_owned::PmsRuntimeTelemetrySnapshot& out) {
  (void)user;
#if defined(ARDUINO)
  const uint32_t now = millis();
#else
  const uint32_t now = 0U;
#endif
  const float phase = static_cast<float>(now % 12000U) / 12000.0f;
  const float wall_v = 12.0f + (0.6f * phase);
  const float batt_v = 11.7f + (0.4f * phase);
  const float wall_i = -0.9f + (2.3f * phase);
  const float batt_i = -0.7f + (1.9f * phase);

  out.wall_v = wall_v;
  out.batt_v = batt_v;
  out.wall_i = wall_i;
  out.batt_i = batt_i;
  out.trip = ((now / 30000U) % 2U) != 0U;
  out.relay_cut = out.trip;
  out.power_source = (wall_v >= batt_v) ? "wall" : "battery";
  out.valid = true;
  return true;
}

const char* storageModeName(StorageBackendMode mode) {
  switch (mode) {
    case StorageBackendMode::Sd:
      return "sd";
    case StorageBackendMode::Spiffs:
      return "spiffs";
    case StorageBackendMode::Disabled:
    default:
      return "disabled";
  }
}

bool loadLocalMac(MacAddress& out_mac) {
  return esp_read_mac(out_mac.data(), ESP_MAC_WIFI_STA) == ESP_OK;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("[SLAVE] boot");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  Serial.println("[SLAVE] wifi initialized by app (STA, disconnected)");

  if (!g_nvs.begin()) {
    Serial.println("[SLAVE] preferences begin failed");
  }
  g_spiffs_ready = SPIFFS.begin(false);
  if (!g_spiffs_ready) {
    Serial.println("[SLAVE] SPIFFS begin failed");
  }
  g_sd_spi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  g_sd_ready = SD.begin(kSdCsPin, g_sd_spi, kSdSpiHz, "/sd", 5U, false);
  Serial.printf("[SLAVE] SD init %s (sck=%d miso=%d mosi=%d cs=%d hz=%lu)\n",
                g_sd_ready ? "ok" : "failed",
                kSdSckPin,
                kSdMisoPin,
                kSdMosiPin,
                kSdCsPin,
                static_cast<unsigned long>(kSdSpiHz));
  SlaveStorageBootstrap::Config storage_cfg{};
  storage_cfg.sd_ready = g_sd_ready;
  storage_cfg.spiffs_ready = g_spiffs_ready;
  storage_cfg.sd_fs = &SD;
  storage_cfg.spiffs_fs = &SPIFFS;
  storage_cfg.log_backend = &g_log_backend;
  storage_cfg.storage_explorer = &g_storage_explorer;
  storage_cfg.ota_backend = &g_ota_backend;
  storage_cfg.log_store = &g_log_store;
  storage_cfg.logger = &g_logger;
  storage_cfg.prefer_sd_for_logs = true;
  storage_cfg.prefer_sd_for_explorer = true;
  storage_cfg.log_rotate_spiffs_bytes = kLogRotateSpiffsBytes;
  storage_cfg.log_rotate_sd_bytes = kLogRotateSdBytes;
  ArduinoNodeStorageBootstrap::Report storage_report{};
  if (!g_storage_bootstrap.begin(storage_cfg, &storage_report)) {
    Serial.println("[SLAVE] storage bootstrap failed");
    return;
  }
  Serial.printf("[SLAVE][STORAGE] explorer=%s ota_backend_ready=%s ota_layout_ready=%s\n",
                storageModeName(storage_report.explorer_mode),
                storage_report.ota_backend_ready ? "yes" : "no",
                storage_report.ota_layout_ready ? "yes" : "no");
  if (!storage_report.ota_message.empty()) {
    Serial.printf("[SLAVE][STORAGE] ota note=%s\n", storage_report.ota_message.c_str());
  }
  if (!storage_report.ota_backend_ready || !storage_report.ota_layout_ready) {
    Serial.println("[SLAVE][STORAGE] OTA path bootstrap failed; stopping startup");
    return;
  }
  Serial.printf("[SLAVE][STORAGE] logger backend=%s rotate_max=%lu bytes\n",
                storage_report.log_backend_name,
                static_cast<unsigned long>(storage_report.log_rotate_max_bytes));
  if (!storage_report.log_message.empty()) {
    Serial.printf("[SLAVE][STORAGE] log note=%s\n", storage_report.log_message.c_str());
  }

  SlaveLoggedTimeSink::Config time_cfg{};
  time_cfg.log_prefix = "SLAVE";
  time_cfg.log_updates = true;
  g_time_sink.configure(time_cfg);

  SlaveOtaVersionGate::Config gate_cfg{};
  gate_cfg.current_sw_version = kSlaveSwVersion;
  gate_cfg.require_newer_version = true;
  g_ota_gate.configure(gate_cfg);

  SlaveOtaBootstrap::Config ota_cfg{};
  ota_cfg.storage = &g_ota_backend;
  ota_cfg.gate = &g_ota_gate;
  ota_cfg.running_sw_version = kSlaveSwVersion;
  ota_cfg.running_build_id = kSlaveBuildId.c_str();
  ota_cfg.print_report = true;
  ota_cfg.log_prefix = "SLAVE";
  if (!g_ota_bootstrap.begin(ota_cfg)) {
    Serial.println("[SLAVE][OTA] bootstrap failed");
    return;
  }
  OtaManager* ota_mgr = g_ota_bootstrap.manager();
  OtaFirmwareSink* ota_sink = g_ota_bootstrap.firmwareSink();
  OtaDescriptorAdapter* ota_adapter = g_ota_bootstrap.descriptorAdapter();
  if (ota_mgr == nullptr || ota_sink == nullptr || ota_adapter == nullptr) {
    Serial.println("[SLAVE][OTA] bootstrap invalid handles");
    return;
  }

  if (!loadLocalMac(g_local_mac)) {
    Serial.println("[SLAVE][DESC] local mac read failed");
    return;
  }
  app_owned::PmsDescriptorAppConfig app_cfg{};
  app_cfg.device_type = "PMS";
  app_cfg.hw_version = "PMS-HW1";
  app_cfg.sw_version = kSlaveSwVersion;
  app_cfg.build_id = kSlaveBuildId;
  app_cfg.default_device_name = "PMS-Node";
  app_cfg.runtime_user = &g_pms_runtime;
  app_cfg.read_telemetry = &readPmsTelemetry;
  g_pms_provider.reset(new app_owned::PmsAppDescriptorProvider(
      g_nvs, g_local_mac, &g_time_sink, &g_storage_explorer, ota_adapter, app_cfg));
  if (!g_pms_provider) {
    Serial.println("[SLAVE][DESC] app-owned provider allocation failed");
    return;
  }

  g_pms_schema = app_owned::makePmsSlaveSchemaPackage(*g_pms_provider, kCodecIdCompactIndexed);
  if (!g_pms_schema.valid() || !g_pms_schema.registerProfile()) {
    Serial.println("[SLAVE][PROFILE] app-owned PMS schema registration failed");
    return;
  }

  SlaveNodeBootstrap::Config boot_cfg{};
  boot_cfg.manager_config = SlaveNodeBootstrap::defaultManagerConfig(1, "PMS", true, true, 5000);
  boot_cfg.transport = &g_transport;
  boot_cfg.persistence = &g_persist_adapter;
  boot_cfg.nvs = &g_nvs;
  boot_cfg.hooks = &g_hooks;
  boot_cfg.descriptor = g_pms_schema.descriptor;
  boot_cfg.telemetry_push = g_pms_schema.telemetry_push;
  boot_cfg.firmware_sink = ota_sink;
  boot_cfg.time_sink = &g_time_sink;
  boot_cfg.ota_manager_for_trace = ota_mgr;
  boot_cfg.logger = &g_logger;
  boot_cfg.local_profile_id = g_pms_schema.profile_id;
  boot_cfg.local_codec_id = g_pms_schema.codec_id;
  boot_cfg.runtime_idle_delay_ms = 2U;
  boot_cfg.print_startup_summary = true;
  boot_cfg.log_prefix = "SLAVE";

  boot_cfg.event_config.log_prefix = "SLAVE";
  boot_cfg.control_config.verbose_pull_trace = kVerbosePullRequestTrace;
  boot_cfg.control_config.log_prefix = "SLAVE";
  boot_cfg.control_config.reset_key = kResetKey;
  boot_cfg.control_config.restart_request_key = kRestartRequestKey;

  boot_cfg.restart_config.log_prefix = "SLAVE";
  boot_cfg.restart_config.reset_key = kResetKey;
  boot_cfg.restart_config.restart_request_key = kRestartRequestKey;
  boot_cfg.restart_config.pre_reboot_hold_ms = 80U;
  boot_cfg.restart_config.reboot_delay_ms = 1000U;

  boot_cfg.ota_trace_config.enabled = true;
  boot_cfg.ota_trace_config.log_prefix = "SLAVE";
  boot_cfg.ota_trace_config.poll_interval_ms = 50U;
  boot_cfg.ota_trace_config.progress_step_bytes = 16384U;

  if (!g_slave_bootstrap.begin(boot_cfg)) {
    Serial.println("[SLAVE] runtime bootstrap begin failed");
    return;
  }

  Serial.printf("[SLAVE] time sync enabled interval_ms=%lu sink=logged-system\n",
                static_cast<unsigned long>(boot_cfg.manager_config.time_sync_interval_ms));
  if (!g_slave_bootstrap.bringUp(true, true)) {
    Serial.println("[SLAVE] manager begin failed");
    return;
  }
}

void loop() {
  g_slave_bootstrap.loop();
}





























