#include "espnow_link/espnow_arduino_transport.hpp"

#if defined(ARDUINO)

#include "espnow_link/manager.hpp"

namespace espnow_link {

EspNowArduinoTransport* EspNowArduinoTransport::instance_ = nullptr;

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

bool EspNowArduinoTransport::begin(uint8_t channel, bool encrypted) {
  encrypted_default_ = encrypted;

  if (esp_now_init() != ESP_OK) {
    return false;
  }

  if (esp_now_set_pmk(getPmkBytes()) != ESP_OK) {
    esp_now_deinit();
    return false;
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  esp_now_register_recv_cb(&EspNowArduinoTransport::onRecvV3);
#else
  esp_now_register_recv_cb(&EspNowArduinoTransport::onRecvV2);
#endif

  if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    esp_now_deinit();
    return false;
  }

  channel_ = channel;
  instance_ = this;

  return addPeer(kBroadcastMac, false, nullptr);
}

void EspNowArduinoTransport::end() {
  esp_now_deinit();
  instance_ = nullptr;
}

bool EspNowArduinoTransport::addPeer(const MacAddress& mac, bool encrypted, const LmkKey* lmk) {
  if (esp_now_is_peer_exist(mac.data())) {
    esp_now_peer_info_t oldp{};
    if (esp_now_get_peer(mac.data(), &oldp) == ESP_OK && oldp.encrypt == encrypted) {
      return true;
    }
    esp_now_del_peer(mac.data());
  }

  esp_now_peer_info_t p{};
  std::memcpy(p.peer_addr, mac.data(), 6);
  p.channel = channel_;
  p.encrypt = encrypted;
  if (encrypted && lmk != nullptr) {
    std::memcpy(p.lmk, lmk->data(), 16);
  }
  return esp_now_add_peer(&p) == ESP_OK;
}

bool EspNowArduinoTransport::removePeer(const MacAddress& mac) {
  return esp_now_del_peer(mac.data()) == ESP_OK;
}

bool EspNowArduinoTransport::send(const MacAddress& to, const uint8_t* data, size_t len) {
  if (!esp_now_is_peer_exist(to.data())) {
    if (!addPeer(to, encrypted_default_, nullptr)) {
      return false;
    }
  }
  return esp_now_send(to.data(), data, len) == ESP_OK;
}

bool EspNowArduinoTransport::setChannel(uint8_t ch) {
  if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    return false;
  }
  channel_ = ch;
  return true;
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
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
void EspNowArduinoTransport::onRecvV2(const uint8_t* mac, const uint8_t* data, int len) {
  if (instance_ == nullptr || instance_->mgr_ == nullptr || mac == nullptr || data == nullptr || len <= 0) {
    return;
  }

  instance_->mgr_->onRx(toMac(mac), data, static_cast<size_t>(len), 0);
}
#endif

}  // namespace espnow_link

#endif  // ARDUINO


