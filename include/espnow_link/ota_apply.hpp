#pragma once

#include <cstdint>
#include <string>

namespace espnow_link {

/** @brief Apply request passed to app-owned OTA executor. */
struct OtaApplyRequest {
  std::string image_path;
  uint32_t image_size = 0;
  uint32_t image_crc32 = 0;
};

/** @brief Apply execution result. */
struct OtaApplyResult {
  bool ok = false;
  std::string message;
};

/** @brief App-owned executor that performs platform-specific OTA apply/reboot flow. */
class IOtaApplyExecutor {
 public:
  virtual ~IOtaApplyExecutor() = default;

  /** @brief Apply image file and trigger required reboot behavior on success path. */
  virtual OtaApplyResult applyImage(const OtaApplyRequest& request) = 0;

  /**
   * @brief Request rollback to the previous valid image and reboot when supported.
   *
   * Default implementation reports unsupported rollback.
   */
  virtual OtaApplyResult rollbackImage() {
    OtaApplyResult out{};
    out.ok = false;
    out.message = "rollback not supported";
    return out;
  }
};

}  // namespace espnow_link
