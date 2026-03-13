#pragma once

#include <cstdint>

#if defined(ESP_PLATFORM)
#include <esp_sleep.h>
#endif

namespace espnow_link {

/**
 * @brief Reboot device by entering deep sleep for a short duration.
 * @param sleep_ms Sleep duration before wake/reboot.
 *
 * Useful when a full radio/peripheral reset is required.
 */
[[noreturn]] inline void deepSleepRebootMs(uint32_t sleep_ms = 1000) {
#if defined(ESP_PLATFORM)
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleep_ms) * 1000ULL);
  esp_deep_sleep_start();
#endif
  for (;;) {
  }
}

}  // namespace espnow_link
