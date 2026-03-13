#pragma once

#include <cstdint>
#include <string>

#include "espnow_link/firmware.hpp"

namespace espnow_link {

/** @brief Update-gate decision returned before OTA transfer/apply. */
enum class OtaGateDecision : uint8_t {
  Ready = 0,
  Denied = 1,
  Busy = 2,
  PrepFailed = 3,
};

/** @brief Gate result payload with optional context message. */
struct OtaGateResult {
  OtaGateDecision decision = OtaGateDecision::Ready;
  std::string message;
};

/** @brief Device-owned gate used to authorize/prep OTA transitions. */
class IOtaUpdateGate {
 public:
  virtual ~IOtaUpdateGate() = default;

  /** @brief Called before accepting inbound transfer begin. */
  virtual OtaGateResult prepareForTransfer(uint32_t image_size, uint32_t image_crc32) = 0;

  /**
   * @brief Called before accepting inbound transfer begin with metadata context.
   *
   * Default implementation delegates to `prepareForTransfer(...)`.
   */
  virtual OtaGateResult prepareForTransferWithMetadata(uint32_t image_size,
                                                       uint32_t image_crc32,
                                                       const FirmwareImageMetadata* metadata) {
    (void)metadata;
    return prepareForTransfer(image_size, image_crc32);
  }

  /** @brief Called before applying an image already present on storage. */
  virtual OtaGateResult prepareForApply(const std::string& image_path,
                                        uint32_t image_size,
                                        uint32_t image_crc32) = 0;

  /**
   * @brief Optional boot-time confirmation gate called before marking OTA as fully confirmed.
   *
   * Device code can run schema migration / integrity checks here and deny confirmation if needed.
   * Default implementation returns `Ready`.
   */
  virtual OtaGateResult confirmBootedImage(const std::string& image_path,
                                           uint32_t image_size,
                                           uint32_t image_crc32,
                                           const std::string& sw_version,
                                           const std::string& build_id) {
    (void)image_path;
    (void)image_size;
    (void)image_crc32;
    (void)sw_version;
    (void)build_id;
    return OtaGateResult{};
  }
};

}  // namespace espnow_link
