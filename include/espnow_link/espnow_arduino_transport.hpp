#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "espnow_link/transport.hpp"

#if defined(ARDUINO)
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif

namespace espnow_link {

#if defined(ARDUINO)

class EspNowManager;

/**
 * @brief Arduino/ESP-IDF transport adapter that implements `ITransport` on top of ESP-NOW APIs.
 */
class EspNowArduinoTransport : public ITransport {
 public:
  EspNowArduinoTransport() = default;

  /**
   * @brief Bind manager instance for RX callback forwarding.
   * @param mgr Manager pointer (must outlive transport).
   */
  void bindManager(EspNowManager* mgr) override { mgr_ = mgr; }

  /** @brief Initialize ESP-NOW stack (Wi-Fi mode/lifecycle is app-managed). */
  bool begin(uint8_t channel, bool encrypted) override;
  /** @brief Deinitialize ESP-NOW stack. */
  void end() override;
  /** @brief Add peer to ESP-NOW peer table. */
  bool addPeer(const MacAddress& mac, bool encrypted, const LmkKey* lmk) override;
  /** @brief Remove peer from ESP-NOW peer table. */
  bool removePeer(const MacAddress& mac) override;
  /** @brief Send raw data frame to peer MAC. */
  bool send(const MacAddress& to, const uint8_t* data, size_t len) override;
  /** @brief Set radio channel. */
  bool setChannel(uint8_t ch) override;
  /** @brief Get current radio channel. */
  uint8_t getChannel() const override { return channel_; }

 private:
  struct PeerCacheEntry {
    MacAddress mac{};
    bool encrypted = false;
    bool has_lmk = false;
    LmkKey lmk{};
  };

  static MacAddress toMac(const uint8_t* m);
  bool upsertPeerCacheEntry_(const MacAddress& mac, bool encrypted, const LmkKey* lmk);
  void erasePeerCacheEntry_(const MacAddress& mac);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  static void onRecvV3(const esp_now_recv_info_t* info, const uint8_t* data, int len);
#else
  static void onRecvV2(const uint8_t* mac, const uint8_t* data, int len);
#endif

  static EspNowArduinoTransport* instance_;
  EspNowManager* mgr_ = nullptr;
  uint8_t channel_ = 1;
  bool encrypted_default_ = false;
  std::vector<PeerCacheEntry> peer_cache_{};
};

#endif  // ARDUINO

}  // namespace espnow_link
