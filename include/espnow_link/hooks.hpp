#pragma once

#include <cstddef>
#include <cstdint>

#include "espnow_link/types.hpp"

namespace espnow_link {

/**
 * @brief Platform callback interface for diagnostics and policy checks.
 */
class IPlatformHooks {
 public:
  /** @brief Validated topology trigger notification delivered to slave application hooks. */
  struct TopologyTriggerNotification {
    MacAddress source{};
    uint16_t seq = 0;
    uint8_t src_role = 0;
    uint8_t src_vid = 0xFF;
    uint8_t dst_role = 0;
    uint8_t dst_vid = 0xFF;
    uint8_t direction = 0;  // 1=forward, 2=reverse
    uint16_t delay_ms = 0;
    uint16_t hold_ms = 0;
  };

  virtual ~IPlatformHooks() = default;

  /**
   * @brief Called after an RX frame is decoded.
   * @param from Source MAC.
   * @param type Message type.
   * @param corr_id Correlation ID.
   * @param len Payload length in bytes.
   * @param rssi RX RSSI value when available.
   */
  virtual void onRxFrame(const MacAddress& from,
                         MessageType type,
                         uint32_t corr_id,
                         size_t len,
                         int rssi) = 0;

  /**
   * @brief Called after a TX attempt.
   * @param to Destination MAC.
   * @param type Message type.
   * @param corr_id Correlation ID.
   * @param len Payload length in bytes.
   * @param ok True when transport reported success.
   */
  virtual void onTxFrame(const MacAddress& to,
                         MessageType type,
                         uint32_t corr_id,
                         size_t len,
                         bool ok) = 0;

  /**
   * @brief Optional RGB blink indicator callback.
   * @param r Red intensity.
   * @param g Green intensity.
   * @param b Blue intensity.
   * @param ms Duration in milliseconds.
   */
  virtual void onRgbBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) = 0;

  /**
   * @brief Optional boot-channel authority hook.
   * @param out_channel Channel chosen by platform authority.
   * @return true to override `ManagerConfig.channel`.
   */
  virtual bool getBootChannel(uint8_t& out_channel) {
    (void)out_channel;
    return false;
  }

  /**
   * @brief Optional policy hook for runtime channel switch requests.
   * @param current_channel Current channel.
   * @param requested_channel Requested target channel.
   * @return true to allow switch, false to deny.
   */
  virtual bool allowChannelSwitch(uint8_t current_channel, uint8_t requested_channel) {
    (void)current_channel;
    (void)requested_channel;
    return true;
  }

  /**
   * @brief Optional notification after channel switch commit.
   * @param channel Committed channel.
   */
  virtual void onChannelCommitted(uint8_t channel) { (void)channel; }

  /**
   * @brief Called on slave when one topology trigger is accepted and not duplicate.
   *
   * This callback is intended for app/device-class actuation handling (e.g. relay timing).
   */
  virtual void onTopologyTrigger(const TopologyTriggerNotification& note) { (void)note; }
};

/** @brief No-op hook implementation for integrations that do not need callbacks. */
class NullPlatformHooks : public IPlatformHooks {
 public:
  void onRxFrame(const MacAddress&, MessageType, uint32_t, size_t, int) override {}
  void onTxFrame(const MacAddress&, MessageType, uint32_t, size_t, bool) override {}
  void onRgbBlink(uint8_t, uint8_t, uint8_t, uint16_t) override {}
};

}  // namespace espnow_link
