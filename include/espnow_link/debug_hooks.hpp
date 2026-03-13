#pragma once

#include <cstdint>

#include "espnow_link/hooks.hpp"

namespace espnow_link {

/**
 * @brief Default debug hook implementation for frame traces and optional RGB callback.
 */
class DebugHooks : public IPlatformHooks {
 public:
  using RgbBlinkFn = void (*)(uint8_t r, uint8_t g, uint8_t b, uint16_t ms);

  /**
   * @brief Construct debug hooks.
   * @param tag Log tag prefix.
   * @param rgb_fn Optional RGB callback.
   */
  explicit DebugHooks(const char* tag = "ESPNOW-LINK", RgbBlinkFn rgb_fn = nullptr);

  /** @brief Log decoded RX frame details. */
  void onRxFrame(const MacAddress& from,
                 MessageType type,
                 uint32_t corr_id,
                 size_t len,
                 int rssi) override;

  /** @brief Log TX attempt details. */
  void onTxFrame(const MacAddress& to,
                 MessageType type,
                 uint32_t corr_id,
                 size_t len,
                 bool ok) override;

  /** @brief Trigger optional RGB callback. */
  void onRgbBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) override;

 private:
  const char* tag_;
  RgbBlinkFn rgb_fn_;
};

}  // namespace espnow_link
