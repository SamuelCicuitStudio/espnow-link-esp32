#pragma once

#include <cstdint>

#if defined(ESP_PLATFORM)
#include <esp_system.h>
#endif

namespace espnow_link {

/**
 * @brief Reboot device (policy: sleep transitions disabled).
 * @param sleep_ms Ignored in production no-sleep policy.
 *
 * Kept under legacy function name to avoid broad call-site churn.
 */
[[noreturn]] inline void deepSleepRebootMs(uint32_t sleep_ms = 1000) {
  (void)sleep_ms;
#if defined(ESP_PLATFORM)
  esp_restart();
#endif
  for (;;) {
  }
}

}  // namespace espnow_link
