#include "espnow_link/storage_bootstrap_arduino.hpp"
#include "espnow_link/ota_paths.hpp"

#if defined(ARDUINO)

namespace espnow_link {

bool ArduinoNodeStorageBootstrap::ensureOtaLayout_(ArduinoSelectableOtaStorageBackend& ota_backend,
                                                   std::string& out_message) {
  static constexpr const char* kOtaDirs[] = {
      ota_paths::kRoot,
      ota_paths::kIn,
      ota_paths::kStaging,
      ota_paths::kImage,
      ota_paths::kManifest,
      ota_paths::kState,
      ota_paths::kArchiveRoot,
      ota_paths::kArchiveMaster,
      ota_paths::kArchiveSlave,
  };
  for (const char* dir : kOtaDirs) {
    if (!ota_backend.ensureDir(dir, out_message)) {
      return false;
    }
  }
  out_message = "ok";
  return true;
}

bool ArduinoNodeStorageBootstrap::begin(const Config& cfg, Report* out_report) const {
  Report local{};

  if (cfg.log_backend == nullptr || cfg.storage_explorer == nullptr ||
      cfg.ota_backend == nullptr || cfg.log_store == nullptr) {
    if (out_report != nullptr) {
      out_report->log_message = "bootstrap config missing required pointers";
    }
    return false;
  }

  fs::FS* sd_fs = (cfg.sd_fs != nullptr) ? cfg.sd_fs : static_cast<fs::FS*>(cfg.sd_fs_typed);
  fs::FS* spiffs_fs = (cfg.spiffs_fs != nullptr) ? cfg.spiffs_fs : static_cast<fs::FS*>(cfg.spiffs_fs_typed);

  cfg.log_backend->bindSd(sd_fs);
  cfg.log_backend->bindSpiffs(spiffs_fs);
  const bool prefer_sd_logs = cfg.prefer_sd_for_logs && cfg.sd_ready;
  cfg.log_backend->setPreferredBackend(prefer_sd_logs
                                           ? ArduinoSelectableLogStorageBackend::PreferredBackend::Sd
                                           : ArduinoSelectableLogStorageBackend::PreferredBackend::Spiffs);

  if (cfg.sd_fs_typed != nullptr) {
    cfg.storage_explorer->bindSd(cfg.sd_fs_typed, cfg.sd_root_path != nullptr ? cfg.sd_root_path : "/");
  } else {
    cfg.storage_explorer->bindSd(sd_fs, cfg.sd_root_path != nullptr ? cfg.sd_root_path : "/");
  }
  if (cfg.spiffs_fs_typed != nullptr) {
    cfg.storage_explorer->bindSpiffs(cfg.spiffs_fs_typed,
                                     cfg.spiffs_root_path != nullptr ? cfg.spiffs_root_path : "/");
  } else {
    cfg.storage_explorer->bindSpiffs(spiffs_fs, cfg.spiffs_root_path != nullptr ? cfg.spiffs_root_path : "/");
  }

  if (cfg.prefer_sd_for_explorer && cfg.sd_ready) {
    cfg.storage_explorer->setMode(StorageBackendMode::Sd);
  } else if (cfg.spiffs_ready) {
    cfg.storage_explorer->setMode(StorageBackendMode::Spiffs);
  } else if (cfg.sd_ready) {
    cfg.storage_explorer->setMode(StorageBackendMode::Sd);
  } else {
    cfg.storage_explorer->setMode(StorageBackendMode::Disabled);
  }
  local.explorer_mode = cfg.storage_explorer->mode();

  cfg.ota_backend->bindSd(sd_fs);
  cfg.ota_backend->bindSpiffs(spiffs_fs);
  cfg.ota_backend->setPreferredBackend(ArduinoSelectableOtaStorageBackend::PreferredBackend::Spiffs);

  std::string msg;
  std::string layout_msg;
  if (cfg.spiffs_ready && cfg.ota_backend->begin(msg)) {
    local.ota_layout_ready = ensureOtaLayout_(*cfg.ota_backend, layout_msg);
    if (!local.ota_layout_ready && local.ota_message.empty()) {
      local.ota_message = layout_msg;
    }
  } else if (cfg.spiffs_ready) {
    local.ota_message = msg;
  }

  msg.clear();
  local.ota_backend_ready = cfg.ota_backend->begin(msg);
  if (!local.ota_layout_ready && !layout_msg.empty()) {
    local.ota_message = layout_msg;
  } else {
    local.ota_message = msg;
  }

  local.log_ready = cfg.log_store->begin();
  if (local.log_ready) {
    const uint32_t max_log_bytes =
        (cfg.log_backend->activeBackend() == ArduinoSelectableLogStorageBackend::PreferredBackend::Sd)
            ? cfg.log_rotate_sd_bytes
            : cfg.log_rotate_spiffs_bytes;
    cfg.log_store->setMaxBytes(max_log_bytes);
    local.log_rotate_max_bytes = max_log_bytes;
    local.log_backend_name = cfg.log_backend->activeBackendName();
    local.log_message = "ok";

    if (cfg.remote_log_store != nullptr) {
      cfg.remote_log_store->setMaxBytes(max_log_bytes);
      local.remote_log_ready = cfg.remote_log_store->begin();
      local.remote_log_message = local.remote_log_ready ? "ok" : "remote log store begin failed";
    }
  } else {
    if (cfg.logger != nullptr) {
      cfg.logger->setEnabled(false);
    }
    local.log_message = "log store begin failed";
  }

  local.storage_bound = true;
  if (out_report != nullptr) {
    *out_report = local;
  }
  return true;
}

}  // namespace espnow_link

#endif  // defined(ARDUINO)
