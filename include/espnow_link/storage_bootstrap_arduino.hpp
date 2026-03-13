#pragma once

#include <cstdint>
#include <string>

#include "espnow_link/library_logger.hpp"
#include "espnow_link/log_storage_arduino.hpp"
#include "espnow_link/ota_storage_arduino.hpp"
#include "espnow_link/storage_explorer_arduino.hpp"

#if defined(ARDUINO)
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#endif

namespace espnow_link {

#if defined(ARDUINO)

/**
 * @brief Centralized Arduino storage/bootstrap helper for node startup.
 *
 * This helper keeps storage setup logic out of application `main.cpp` by
 * handling the common startup flow:
 * - bind SD/SPIFFS to logger, storage explorer, and OTA backends
 * - select active storage mode for explorer
 * - ensure OTA directory tree in SPIFFS
 * - initialize log stores and apply rotation policy by active backend
 */
class ArduinoNodeStorageBootstrap {
 public:
  /** @brief Input configuration for one bootstrap run. */
  struct Config {
    bool sd_ready = false;
    bool spiffs_ready = false;

    fs::FS* sd_fs = nullptr;
    fs::SDFS* sd_fs_typed = nullptr;
    fs::FS* spiffs_fs = nullptr;
    fs::SPIFFSFS* spiffs_fs_typed = nullptr;

    const char* sd_root_path = "/";
    const char* spiffs_root_path = "/";

    bool prefer_sd_for_logs = true;
    bool prefer_sd_for_explorer = true;

    uint32_t log_rotate_spiffs_bytes = 1572864U;   // 1.5 MiB
    uint32_t log_rotate_sd_bytes = 52428800U;      // 50 MiB

    ArduinoSelectableLogStorageBackend* log_backend = nullptr;
    ArduinoStorageExplorer* storage_explorer = nullptr;
    ArduinoSelectableOtaStorageBackend* ota_backend = nullptr;
    EspLogStore* log_store = nullptr;
    EspLogStore* remote_log_store = nullptr;
    LibraryLogger* logger = nullptr;
  };

  /** @brief Output report for diagnostics/UI prints. */
  struct Report {
    bool storage_bound = false;
    StorageBackendMode explorer_mode = StorageBackendMode::Disabled;

    bool ota_layout_ready = false;
    bool ota_backend_ready = false;
    std::string ota_message;

    bool log_ready = false;
    uint32_t log_rotate_max_bytes = 0U;
    const char* log_backend_name = "none";
    std::string log_message;

    bool remote_log_ready = false;
    std::string remote_log_message;
  };

  /**
   * @brief Run storage/bootstrap initialization with externally mounted FS.
   *
   * @param cfg Startup configuration and object bindings.
   * @param out_report Optional output with status for user-facing logs.
   * @return true when required objects were provided and bootstrap executed.
   */
  bool begin(const Config& cfg, Report* out_report = nullptr) const;

 private:
  static bool ensureOtaLayout_(ArduinoSelectableOtaStorageBackend& ota_backend, std::string& out_message);
};

#endif  // defined(ARDUINO)

}  // namespace espnow_link

