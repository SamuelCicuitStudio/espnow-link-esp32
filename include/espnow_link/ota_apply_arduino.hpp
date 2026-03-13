#pragma once

#include <cstddef>
#include <cstdint>

#include "espnow_link/ota_apply.hpp"
#include "espnow_link/ota_storage.hpp"

#if defined(ARDUINO)
#include <Arduino.h>
#include <Update.h>
#endif

namespace espnow_link {

#if defined(ARDUINO)

/**
 * @brief Arduino OTA apply executor that streams a staged image file into the OTA update partition.
 *
 * Storage access is delegated to `IOtaStorageBackend`, so the file can reside on SD or SPIFFS.
 */
class ArduinoOtaApplyExecutor final : public IOtaApplyExecutor {
 public:
  /** @brief Optional restart callback invoked on successful apply when reboot is enabled. */
  using RestartCallback = void (*)();

  explicit ArduinoOtaApplyExecutor(IOtaStorageBackend& storage,
                                   size_t chunk_bytes = 1024U,
                                   bool reboot_on_success = true)
      : storage_(storage), chunk_bytes_(chunk_bytes), reboot_on_success_(reboot_on_success) {}

  /** @brief Set read chunk size used while streaming image to Update API. */
  void setChunkBytes(size_t bytes);

  /** @brief Enable/disable immediate reboot after successful apply. */
  void setRebootOnSuccess(bool enabled) { reboot_on_success_ = enabled; }

  /** @brief Set optional reboot callback. Default path calls `ESP.restart()`. */
  void setRestartCallback(RestartCallback cb) { restart_cb_ = cb; }

  OtaApplyResult applyImage(const OtaApplyRequest& request) override;
  OtaApplyResult rollbackImage() override;

 private:
  static uint32_t crc32Update_(uint32_t running_crc, const uint8_t* data, size_t len);
  static uint32_t crc32Finalize_(uint32_t running_crc);
  static const char* updateErrorString_();

  IOtaStorageBackend& storage_;
  size_t chunk_bytes_ = 1024U;
  bool reboot_on_success_ = true;
  RestartCallback restart_cb_ = nullptr;
};

#endif  // defined(ARDUINO)

}  // namespace espnow_link
