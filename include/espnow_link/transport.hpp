#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "espnow_link/types.hpp"

namespace espnow_link {

class EspNowManager;

/**
 * @brief Low-level radio transport abstraction for ESP-NOW operations.
 */
class ITransport {
 public:
  virtual ~ITransport() = default;

  /**
   * @brief Initialize radio/ESP-NOW runtime.
   * @param channel Initial Wi-Fi channel.
   * @param encrypted True when secure peer mode is enabled.
   * @return true on success.
   */
  virtual bool begin(uint8_t channel, bool encrypted) = 0;

  /** @brief Shutdown radio/ESP-NOW runtime. */
  virtual void end() = 0;

  /**
   * @brief Add peer entry to radio backend.
   * @param mac Peer MAC.
   * @param encrypted True for encrypted peer.
   * @param lmk Optional LMK for encrypted peer.
   * @return true on success.
   */
  virtual bool addPeer(const MacAddress& mac, bool encrypted, const LmkKey* lmk) = 0;

  /**
   * @brief Remove peer entry from radio backend.
   * @param mac Peer MAC.
   * @return true on success.
   */
  virtual bool removePeer(const MacAddress& mac) = 0;

  /**
   * @brief Send one payload to peer.
   * @param to Destination MAC.
   * @param data Payload bytes.
   * @param len Payload length in bytes.
   * @return true when accepted by transport layer.
   */
  virtual bool send(const MacAddress& to, const uint8_t* data, size_t len) = 0;

  /**
   * @brief Bind manager callback target for transport RX forwarding.
   *
   * Default implementation is a no-op so non-ESP-NOW transports do not need
   * to implement manager binding.
   *
   * @param mgr Manager pointer (may be null).
   */
  virtual void bindManager(EspNowManager* mgr) { (void)mgr; }

  /**
   * @brief Change current Wi-Fi channel.
   * @param channel Target channel.
   * @return true on success.
   */
  virtual bool setChannel(uint8_t channel) = 0;

  /**
   * @brief Get current active Wi-Fi channel.
   * @return Current channel value.
   */
  virtual uint8_t getChannel() const = 0;
};

}  // namespace espnow_link
