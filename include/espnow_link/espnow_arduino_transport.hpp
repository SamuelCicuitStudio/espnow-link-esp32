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
#if defined(__has_include)
#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif
#endif
#endif

namespace espnow_link {

#if defined(ARDUINO)

#if (defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)) || \
    (defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5))
#define ESPNOW_LINK_USE_RECV_INFO_CB 1
#else
#define ESPNOW_LINK_USE_RECV_INFO_CB 0
#endif

class EspNowManager;

/**
 * @brief Arduino/ESP-IDF transport adapter that implements `ITransport` on top of ESP-NOW APIs.
 */
class EspNowArduinoTransport : public ITransport {
 public:
  EspNowArduinoTransport() = default;

  struct TxDiagnostics {
    bool has_attempt = false;
    MacAddress peer{};
    size_t len = 0U;
    int32_t send_rc = 0;
    int32_t add_peer_rc = 0;
    uint8_t peer_exists = 0U;
    uint8_t expected_channel = 0U;
    uint8_t runtime_channel = 0U;
    bool encrypted_default = false;
    uint32_t fail_streak = 0U;
    uint8_t no_mem_streak = 0U;
    uint32_t no_mem_backoff_ms = 0U;
  };

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
  /** @brief Return last TX diagnostics (best-effort snapshot). */
  void getLastTxDiagnostics(TxDiagnostics& out) const;

 private:
  struct PeerCacheEntry {
    MacAddress mac{};
    bool encrypted = false;
    bool has_lmk = false;
    LmkKey lmk{};
  };

  static MacAddress toMac(const uint8_t* m);
  const PeerCacheEntry* findPeerCacheEntry_(const MacAddress& mac) const;
  bool upsertPeerCacheEntry_(const MacAddress& mac, bool encrypted, const LmkKey* lmk);
  void erasePeerCacheEntry_(const MacAddress& mac);
  bool ensureRuntimeChannel_(bool force_reapply);
  bool reinitEspNowStack_();
  bool recoverAfterSendFailures_(esp_err_t last_rc);

#if ESPNOW_LINK_USE_RECV_INFO_CB
  static void onRecvV3(const esp_now_recv_info_t* info, const uint8_t* data, int len);
#else
  static void onRecvV2(const uint8_t* mac, const uint8_t* data, int len);
  static void onPromiscRx_(void* buf, wifi_promiscuous_pkt_type_t type);
  static bool lookupRecentRssi_(const MacAddress& mac, int16_t& out_rssi);
#endif

  static EspNowArduinoTransport* instance_;
  EspNowManager* mgr_ = nullptr;
  uint8_t channel_ = 1;
  bool encrypted_default_ = false;
  std::vector<PeerCacheEntry> peer_cache_{};
  uint32_t send_fail_streak_ = 0U;
  uint8_t no_mem_streak_ = 0U;
  uint32_t no_mem_backoff_until_ms_ = 0U;
  uint32_t last_send_ok_ms_ = 0U;
  uint32_t last_no_mem_probe_ms_ = 0U;
  uint32_t last_channel_check_ms_ = 0U;
  uint32_t last_recover_ms_ = 0U;
  MacAddress last_send_peer_{};
  size_t last_send_len_ = 0U;
  bool has_last_send_attempt_ = false;
  esp_err_t last_send_rc_ = ESP_OK;
  esp_err_t last_add_peer_rc_ = ESP_OK;
#if !ESPNOW_LINK_USE_RECV_INFO_CB
  bool promisc_owned_ = false;
#endif
};

#endif  // ARDUINO

}  // namespace espnow_link
