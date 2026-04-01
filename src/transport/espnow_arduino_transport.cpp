#include "espnow_link/espnow_arduino_transport.hpp"

#if defined(ARDUINO)

#include "espnow_link/manager.hpp"

#include <algorithm>
#include <array>

namespace espnow_link {

EspNowArduinoTransport* EspNowArduinoTransport::instance_ = nullptr;

#if !ESPNOW_LINK_USE_RECV_INFO_CB
namespace {
struct RssiCacheEntry {
  MacAddress mac{};
  int16_t rssi = 0;
  uint32_t seen_ms = 0U;
  bool valid = false;
};

constexpr size_t kRssiCacheSize = 48U;
constexpr uint32_t kRssiMatchWindowMs = 1200U;
constexpr uint32_t kChannelVerifyIntervalMs = 1500U;
constexpr uint32_t kRecoverCooldownMs = 4000U;
constexpr uint32_t kRecoverFailStreakThreshold = 8U;
constexpr uint32_t kNoMemRecoverCooldownMs = 1000U;
constexpr uint32_t kNoMemRecoverFailStreakThreshold = 2U;
constexpr uint32_t kNoMemStuckWindowMs = 12000U;
constexpr uint32_t kNoMemStuckProbeIntervalMs = 1800U;
constexpr uint32_t kNoMemHardReinitFailStreakThreshold = 6U;
constexpr uint32_t kNoMemHardReinitStuckMs = 18000U;
std::array<RssiCacheEntry, kRssiCacheSize> g_rssi_cache{};
size_t g_rssi_cache_head = 0U;
portMUX_TYPE g_rssi_cache_mux = portMUX_INITIALIZER_UNLOCKED;

bool macAllEqual(const uint8_t* mac, uint8_t value) {
  if (mac == nullptr) return true;
  for (size_t i = 0; i < 6U; ++i) {
    if (mac[i] != value) return false;
  }
  return true;
}

void cacheRssiSample(const uint8_t* src, int16_t rssi, uint32_t now_ms) {
  if (src == nullptr || rssi == 0) return;
  if (macAllEqual(src, 0x00U) || macAllEqual(src, 0xFFU)) return;

  portENTER_CRITICAL(&g_rssi_cache_mux);
  for (auto& entry : g_rssi_cache) {
    if (!entry.valid) continue;
    if (std::memcmp(entry.mac.data(), src, 6U) != 0) continue;
    entry.rssi = rssi;
    entry.seen_ms = now_ms;
    portEXIT_CRITICAL(&g_rssi_cache_mux);
    return;
  }

  RssiCacheEntry& slot = g_rssi_cache[g_rssi_cache_head % kRssiCacheSize];
  std::memcpy(slot.mac.data(), src, 6U);
  slot.rssi = rssi;
  slot.seen_ms = now_ms;
  slot.valid = true;
  g_rssi_cache_head = (g_rssi_cache_head + 1U) % kRssiCacheSize;
  portEXIT_CRITICAL(&g_rssi_cache_mux);
}
}  // namespace
#endif

MacAddress EspNowArduinoTransport::toMac(const uint8_t* m) {
  MacAddress out{};
  if (m == nullptr) {
    return out;
  }
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = m[i];
  }
  return out;
}

const EspNowArduinoTransport::PeerCacheEntry* EspNowArduinoTransport::findPeerCacheEntry_(
    const MacAddress& mac) const {
  for (const auto& entry : peer_cache_) {
    if (entry.mac == mac) {
      return &entry;
    }
  }
  return nullptr;
}

bool EspNowArduinoTransport::upsertPeerCacheEntry_(const MacAddress& mac,
                                                   bool encrypted,
                                                   const LmkKey* lmk) {
  for (auto& entry : peer_cache_) {
    if (entry.mac == mac) {
      entry.encrypted = encrypted;
      if (!encrypted) {
        entry.has_lmk = false;
        entry.lmk = LmkKey{};
        return true;
      }
      if (lmk != nullptr) {
        entry.has_lmk = true;
        entry.lmk = *lmk;
        return true;
      }
      // Preserve a previously learned LMK when callers only provide encryption intent.
      if (!entry.has_lmk) {
        entry.lmk = LmkKey{};
      }
      return true;
    }
  }

  PeerCacheEntry entry{};
  entry.mac = mac;
  entry.encrypted = encrypted;
  entry.has_lmk = (encrypted && lmk != nullptr);
  entry.lmk = entry.has_lmk ? *lmk : LmkKey{};
  peer_cache_.push_back(entry);
  return true;
}

void EspNowArduinoTransport::erasePeerCacheEntry_(const MacAddress& mac) {
  peer_cache_.erase(
      std::remove_if(peer_cache_.begin(), peer_cache_.end(), [&](const PeerCacheEntry& entry) {
        return entry.mac == mac;
      }),
      peer_cache_.end());
}

bool EspNowArduinoTransport::begin(uint8_t channel, bool encrypted) {
  peer_cache_.clear();
  encrypted_default_ = encrypted;
  has_last_send_attempt_ = false;
  last_send_peer_ = MacAddress{};
  last_send_len_ = 0U;
  last_send_rc_ = ESP_OK;
  last_add_peer_rc_ = ESP_OK;
#if !ESPNOW_LINK_USE_RECV_INFO_CB
  promisc_owned_ = false;
#endif

  if (esp_now_init() != ESP_OK) {
    return false;
  }

  if (esp_now_set_pmk(getPmkBytes()) != ESP_OK) {
    esp_now_deinit();
    return false;
  }

#if ESPNOW_LINK_USE_RECV_INFO_CB
  esp_now_register_recv_cb(&EspNowArduinoTransport::onRecvV3);
#else
  esp_now_register_recv_cb(&EspNowArduinoTransport::onRecvV2);
  wifi_promiscuous_filter_t filter{};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  (void)esp_wifi_set_promiscuous_filter(&filter);
  (void)esp_wifi_set_promiscuous_rx_cb(&EspNowArduinoTransport::onPromiscRx_);
  promisc_owned_ = (esp_wifi_set_promiscuous(true) == ESP_OK);
#endif

  if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
#if !ESPNOW_LINK_USE_RECV_INFO_CB
    if (promisc_owned_) {
      (void)esp_wifi_set_promiscuous(false);
      promisc_owned_ = false;
    }
#endif
    esp_now_deinit();
    return false;
  }

  channel_ = channel;
  instance_ = this;
  last_send_ok_ms_ = static_cast<uint32_t>(millis());
  last_no_mem_probe_ms_ = 0U;
  send_fail_streak_ = 0U;
  no_mem_streak_ = 0U;
  no_mem_backoff_until_ms_ = 0U;
  last_channel_check_ms_ = 0U;
  last_recover_ms_ = 0U;

  return addPeer(kBroadcastMac, false, nullptr);
}

void EspNowArduinoTransport::end() {
#if !ESPNOW_LINK_USE_RECV_INFO_CB
  if (promisc_owned_) {
    (void)esp_wifi_set_promiscuous(false);
    promisc_owned_ = false;
  }
#endif
  esp_now_deinit();
  instance_ = nullptr;
  peer_cache_.clear();
}

bool EspNowArduinoTransport::addPeer(const MacAddress& mac, bool encrypted, const LmkKey* lmk) {
  last_add_peer_rc_ = ESP_OK;
  if (esp_now_is_peer_exist(mac.data())) {
    esp_now_peer_info_t oldp{};
    if (esp_now_get_peer(mac.data(), &oldp) == ESP_OK) {
      bool keep_existing = (oldp.encrypt == encrypted) && (oldp.channel == channel_);
      if (keep_existing && encrypted && lmk != nullptr) {
        keep_existing = (std::memcmp(oldp.lmk, lmk->data(), 16) == 0);
      }
      if (keep_existing) {
        (void)upsertPeerCacheEntry_(mac, encrypted, lmk);
        return true;
      }
    }
    const esp_err_t del_rc = esp_now_del_peer(mac.data());
    if (del_rc != ESP_OK) {
      last_add_peer_rc_ = del_rc;
    }
  }

  esp_now_peer_info_t p{};
  std::memcpy(p.peer_addr, mac.data(), 6);
  p.channel = channel_;
  p.encrypt = encrypted;
  if (encrypted && lmk != nullptr) {
    std::memcpy(p.lmk, lmk->data(), 16);
  }
  const esp_err_t add_rc = esp_now_add_peer(&p);
  if (add_rc != ESP_OK) {
    last_add_peer_rc_ = add_rc;
    return false;
  }
  last_add_peer_rc_ = ESP_OK;
  return upsertPeerCacheEntry_(mac, encrypted, lmk);
}

bool EspNowArduinoTransport::removePeer(const MacAddress& mac) {
  erasePeerCacheEntry_(mac);
  return esp_now_del_peer(mac.data()) == ESP_OK;
}

bool EspNowArduinoTransport::send(const MacAddress& to, const uint8_t* data, size_t len) {
  has_last_send_attempt_ = true;
  last_send_peer_ = to;
  last_send_len_ = len;
  last_send_rc_ = ESP_OK;

  const uint32_t now_ms = static_cast<uint32_t>(millis());
  if ((now_ms - last_channel_check_ms_) >= kChannelVerifyIntervalMs) {
    last_channel_check_ms_ = now_ms;
    (void)ensureRuntimeChannel_(false);
  }
  if (no_mem_backoff_until_ms_ != 0U &&
      static_cast<int32_t>(now_ms - no_mem_backoff_until_ms_) < 0) {
    const bool stuck_no_mem =
        (last_send_ok_ms_ != 0U) && ((now_ms - last_send_ok_ms_) >= kNoMemStuckWindowMs);
    if (stuck_no_mem && (now_ms - last_no_mem_probe_ms_) >= kNoMemStuckProbeIntervalMs) {
      last_no_mem_probe_ms_ = now_ms;
      (void)recoverAfterSendFailures_(ESP_ERR_ESPNOW_NO_MEM);
    }
    last_send_rc_ = ESP_ERR_ESPNOW_NO_MEM;
    return false;
  }

  auto resolvePeerConfig = [&](bool& out_encrypted, const LmkKey*& out_lmk) {
    out_encrypted = encrypted_default_;
    out_lmk = nullptr;
    const PeerCacheEntry* cached = findPeerCacheEntry_(to);
    if (cached == nullptr) {
      return;
    }
    out_encrypted = cached->encrypted;
    if (cached->encrypted && cached->has_lmk) {
      out_lmk = &cached->lmk;
    }
  };

  if (!esp_now_is_peer_exist(to.data())) {
    bool encrypted = encrypted_default_;
    const LmkKey* lmk = nullptr;
    resolvePeerConfig(encrypted, lmk);
    if (!addPeer(to, encrypted, lmk)) {
      last_send_rc_ = last_add_peer_rc_;
      return false;
    }
  }

  esp_err_t rc = esp_now_send(to.data(), data, len);
  if (rc == ESP_OK) {
    send_fail_streak_ = 0U;
    no_mem_streak_ = 0U;
    no_mem_backoff_until_ms_ = 0U;
    last_send_ok_ms_ = now_ms;
    last_no_mem_probe_ms_ = 0U;
    last_send_rc_ = ESP_OK;
    return true;
  }

  // Retry once on missing peer races by re-attaching using cached security context.
  if (rc == ESP_ERR_ESPNOW_NOT_FOUND || !esp_now_is_peer_exist(to.data())) {
    bool encrypted = encrypted_default_;
    const LmkKey* lmk = nullptr;
    resolvePeerConfig(encrypted, lmk);
    if (addPeer(to, encrypted, lmk)) {
      rc = esp_now_send(to.data(), data, len);
    }
  }
  if (rc == ESP_OK) {
    send_fail_streak_ = 0U;
    no_mem_streak_ = 0U;
    no_mem_backoff_until_ms_ = 0U;
    last_send_ok_ms_ = now_ms;
    last_no_mem_probe_ms_ = 0U;
    last_send_rc_ = ESP_OK;
    return true;
  }

  if (rc == ESP_ERR_ESPNOW_NO_MEM) {
    if (no_mem_streak_ < 32U) {
      ++no_mem_streak_;
    }
    const uint32_t backoff_ms =
        std::min<uint32_t>(400U + static_cast<uint32_t>(no_mem_streak_) * 500U, 5000U);
    no_mem_backoff_until_ms_ = now_ms + backoff_ms;
  } else {
    no_mem_streak_ = 0U;
    no_mem_backoff_until_ms_ = 0U;
  }

  if (send_fail_streak_ < 0xFFFFFFFFU) {
    ++send_fail_streak_;
  }

  const bool no_mem_rc = (rc == ESP_ERR_ESPNOW_NO_MEM);
  const uint32_t recover_threshold =
      no_mem_rc ? kNoMemRecoverFailStreakThreshold : kRecoverFailStreakThreshold;
  const uint32_t recover_cooldown =
      no_mem_rc ? kNoMemRecoverCooldownMs : kRecoverCooldownMs;

  if (send_fail_streak_ >= recover_threshold &&
      (now_ms - last_recover_ms_) >= recover_cooldown) {
    if (recoverAfterSendFailures_(rc)) {
      // Retry once after recovery path.
      bool encrypted = encrypted_default_;
      const LmkKey* lmk = nullptr;
      resolvePeerConfig(encrypted, lmk);
      if (!esp_now_is_peer_exist(to.data())) {
        (void)addPeer(to, encrypted, lmk);
      }
      rc = esp_now_send(to.data(), data, len);
      if (rc == ESP_OK) {
        send_fail_streak_ = 0U;
        no_mem_streak_ = 0U;
        no_mem_backoff_until_ms_ = 0U;
        last_send_ok_ms_ = now_ms;
        last_no_mem_probe_ms_ = 0U;
        last_send_rc_ = ESP_OK;
        return true;
      }
    }
  }
  last_send_rc_ = rc;
#if defined(ARDUINO)
  if (rc != ESP_OK) {
    static uint32_t s_last_log_ms = 0U;
    static esp_err_t s_last_rc = ESP_OK;
    const uint32_t log_now_ms = static_cast<uint32_t>(millis());
    if (rc != s_last_rc || (log_now_ms - s_last_log_ms) >= 2000U) {
      s_last_rc = rc;
      s_last_log_ms = log_now_ms;
      Serial.printf(
          "[ESPNOW][TX] send fail rc=0x%04X streak=%lu nomem_streak=%u backoff_ms=%lu peer=%02X:%02X:%02X:%02X:%02X:%02X peer_exists=%u len=%u\n",
          static_cast<unsigned>(rc),
          static_cast<unsigned long>(send_fail_streak_),
          static_cast<unsigned>(no_mem_streak_),
          static_cast<unsigned long>(
              (no_mem_backoff_until_ms_ != 0U &&
               static_cast<int32_t>(no_mem_backoff_until_ms_ - log_now_ms) > 0)
                  ? (no_mem_backoff_until_ms_ - log_now_ms)
                  : 0U),
          to[0],
          to[1],
          to[2],
          to[3],
          to[4],
          to[5],
          esp_now_is_peer_exist(to.data()) ? 1U : 0U,
          static_cast<unsigned>(len));
    }
  }
#endif
  return rc == ESP_OK;
}

void EspNowArduinoTransport::getLastTxDiagnostics(TxDiagnostics& out) const {
  out = TxDiagnostics{};
  out.has_attempt = has_last_send_attempt_;
  out.peer = last_send_peer_;
  out.len = last_send_len_;
  out.send_rc = static_cast<int32_t>(last_send_rc_);
  out.add_peer_rc = static_cast<int32_t>(last_add_peer_rc_);
  out.expected_channel = channel_;
  out.encrypted_default = encrypted_default_;
  out.fail_streak = send_fail_streak_;
  out.no_mem_streak = no_mem_streak_;

  const uint32_t now_ms = static_cast<uint32_t>(millis());
  if (no_mem_backoff_until_ms_ != 0U &&
      static_cast<int32_t>(no_mem_backoff_until_ms_ - now_ms) > 0) {
    out.no_mem_backoff_ms = no_mem_backoff_until_ms_ - now_ms;
  } else {
    out.no_mem_backoff_ms = 0U;
  }

  uint8_t runtime_channel = 0U;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&runtime_channel, &second) == ESP_OK) {
    out.runtime_channel = runtime_channel;
  }
  if (has_last_send_attempt_) {
    out.peer_exists = esp_now_is_peer_exist(last_send_peer_.data()) ? 1U : 0U;
  }
}

bool EspNowArduinoTransport::setChannel(uint8_t ch) {
  if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    return false;
  }
  channel_ = ch;
  bool ok = true;
  for (const auto& entry : peer_cache_) {
    if (esp_now_is_peer_exist(entry.mac.data())) {
      (void)esp_now_del_peer(entry.mac.data());
    }
    esp_now_peer_info_t p{};
    std::memcpy(p.peer_addr, entry.mac.data(), 6);
    p.channel = channel_;
    p.encrypt = entry.encrypted;
    if (entry.encrypted && entry.has_lmk) {
      std::memcpy(p.lmk, entry.lmk.data(), 16);
    }
    if (esp_now_add_peer(&p) != ESP_OK) {
      ok = false;
    }
  }
  return ok;
}

bool EspNowArduinoTransport::ensureRuntimeChannel_(bool force_reapply) {
  uint8_t wifi_channel = 0U;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&wifi_channel, &second) != ESP_OK) {
    return false;
  }
  if (!force_reapply && wifi_channel == channel_) {
    return true;
  }
  const bool ok = setChannel(channel_);
#if defined(ARDUINO)
  if (wifi_channel != channel_ || force_reapply) {
    Serial.printf(
        "[ESPNOW][CHAN] runtime=%u expected=%u reapply=%u result=%s\n",
        static_cast<unsigned>(wifi_channel),
        static_cast<unsigned>(channel_),
        force_reapply ? 1U : 0U,
        ok ? "ok" : "fail");
  }
#endif
  return ok;
}

bool EspNowArduinoTransport::reinitEspNowStack_() {
#if !ESPNOW_LINK_USE_RECV_INFO_CB
  if (promisc_owned_) {
    (void)esp_wifi_set_promiscuous(false);
    promisc_owned_ = false;
  }
#endif

  (void)esp_now_deinit();

  if (esp_now_init() != ESP_OK) {
    return false;
  }
  if (esp_now_set_pmk(getPmkBytes()) != ESP_OK) {
    (void)esp_now_deinit();
    return false;
  }

#if ESPNOW_LINK_USE_RECV_INFO_CB
  esp_now_register_recv_cb(&EspNowArduinoTransport::onRecvV3);
#else
  esp_now_register_recv_cb(&EspNowArduinoTransport::onRecvV2);
  wifi_promiscuous_filter_t filter{};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  (void)esp_wifi_set_promiscuous_filter(&filter);
  (void)esp_wifi_set_promiscuous_rx_cb(&EspNowArduinoTransport::onPromiscRx_);
  promisc_owned_ = (esp_wifi_set_promiscuous(true) == ESP_OK);
#endif

  if (esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
#if !ESPNOW_LINK_USE_RECV_INFO_CB
    if (promisc_owned_) {
      (void)esp_wifi_set_promiscuous(false);
      promisc_owned_ = false;
    }
#endif
    (void)esp_now_deinit();
    return false;
  }

  instance_ = this;
  bool ok = true;
  if (!esp_now_is_peer_exist(kBroadcastMac.data())) {
    ok = addPeer(kBroadcastMac, false, nullptr) && ok;
  }
  for (const auto& entry : peer_cache_) {
    if (entry.mac == kBroadcastMac) continue;
    const LmkKey* lmk = (entry.encrypted && entry.has_lmk) ? &entry.lmk : nullptr;
    ok = addPeer(entry.mac, entry.encrypted, lmk) && ok;
  }
  return ok;
}

bool EspNowArduinoTransport::recoverAfterSendFailures_(esp_err_t last_rc) {
  last_recover_ms_ = static_cast<uint32_t>(millis());
  bool ok = ensureRuntimeChannel_(true);
  if (!esp_now_is_peer_exist(kBroadcastMac.data())) {
    ok = addPeer(kBroadcastMac, false, nullptr) && ok;
  }
  for (const auto& entry : peer_cache_) {
    if (esp_now_is_peer_exist(entry.mac.data())) {
      continue;
    }
    const LmkKey* lmk = (entry.encrypted && entry.has_lmk) ? &entry.lmk : nullptr;
    ok = addPeer(entry.mac, entry.encrypted, lmk) && ok;
  }

  const uint32_t now_ms = static_cast<uint32_t>(millis());
  const bool no_mem_rc = (last_rc == ESP_ERR_ESPNOW_NO_MEM);
  const bool no_mem_stuck =
      (last_send_ok_ms_ != 0U) && ((now_ms - last_send_ok_ms_) >= kNoMemHardReinitStuckMs);
  const bool should_hard_reinit =
      no_mem_rc && (!ok || send_fail_streak_ >= kNoMemHardReinitFailStreakThreshold || no_mem_stuck);
  if (should_hard_reinit) {
    const bool hard_ok = reinitEspNowStack_();
    ok = hard_ok;
#if defined(ARDUINO)
    Serial.printf("[ESPNOW][RECOVER][HARD] rc=0x%04X streak=%lu nomem_streak=%u result=%s\n",
                  static_cast<unsigned>(last_rc),
                  static_cast<unsigned long>(send_fail_streak_),
                  static_cast<unsigned>(no_mem_streak_),
                  hard_ok ? "ok" : "fail");
#endif
    if (hard_ok) {
      // After successful reinit we can clear transient backoff pressure.
      no_mem_backoff_until_ms_ = 0U;
      no_mem_streak_ = 0U;
    }
  }
#if defined(ARDUINO)
  Serial.printf("[ESPNOW][RECOVER] rc=0x%04X streak=%lu result=%s\n",
                static_cast<unsigned>(last_rc),
                static_cast<unsigned long>(send_fail_streak_),
                ok ? "ok" : "fail");
#endif
  return ok;
}

#if ESPNOW_LINK_USE_RECV_INFO_CB
void EspNowArduinoTransport::onRecvV3(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (instance_ == nullptr || instance_->mgr_ == nullptr || info == nullptr || data == nullptr || len <= 0) {
    return;
  }

  int rssi = 0;
  if (info->rx_ctrl != nullptr) {
    rssi = static_cast<int>(info->rx_ctrl->rssi);
  }

  instance_->mgr_->onRx(toMac(info->src_addr), data, static_cast<size_t>(len), rssi);
}
#else
void EspNowArduinoTransport::onPromiscRx_(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (instance_ == nullptr || buf == nullptr) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

  const wifi_promiscuous_pkt_t* pkt = reinterpret_cast<const wifi_promiscuous_pkt_t*>(buf);
  if (pkt == nullptr) return;
  if (pkt->rx_ctrl.sig_len < 16U) return;

  const int rssi_i = static_cast<int>(pkt->rx_ctrl.rssi);
  if (rssi_i == 0) return;
  const int clamped = std::max(-127, std::min(127, rssi_i));
  const uint8_t* src = pkt->payload + 10U;
  cacheRssiSample(src, static_cast<int16_t>(clamped), static_cast<uint32_t>(millis()));
}

bool EspNowArduinoTransport::lookupRecentRssi_(const MacAddress& mac, int16_t& out_rssi) {
  out_rssi = 0;
  const uint32_t now_ms = static_cast<uint32_t>(millis());
  bool found = false;
  uint32_t best_seen_ms = 0U;

  portENTER_CRITICAL(&g_rssi_cache_mux);
  for (const auto& entry : g_rssi_cache) {
    if (!entry.valid) continue;
    if (entry.rssi == 0) continue;
    if (entry.mac != mac) continue;
    const uint32_t age_ms = (now_ms >= entry.seen_ms) ? (now_ms - entry.seen_ms) : 0U;
    if (age_ms > kRssiMatchWindowMs) continue;
    if (!found || entry.seen_ms >= best_seen_ms) {
      found = true;
      best_seen_ms = entry.seen_ms;
      out_rssi = entry.rssi;
    }
  }
  portEXIT_CRITICAL(&g_rssi_cache_mux);
  return found;
}

void EspNowArduinoTransport::onRecvV2(const uint8_t* mac, const uint8_t* data, int len) {
  if (instance_ == nullptr || instance_->mgr_ == nullptr || mac == nullptr || data == nullptr || len <= 0) {
    return;
  }

  const MacAddress from = toMac(mac);
  int16_t rssi = 0;
  (void)lookupRecentRssi_(from, rssi);
  instance_->mgr_->onRx(from, data, static_cast<size_t>(len), static_cast<int>(rssi));
}
#endif

}  // namespace espnow_link

#endif  // ARDUINO


