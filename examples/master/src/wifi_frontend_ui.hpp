#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <functional>

#include "espnow_link/address.hpp"
#include "espnow_link/management_frontend_adapter.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/management_utils.hpp"

class MasterWifiFrontendUi {
 public:
  struct Config {
    const char* ssid = "geyehub";
    const char* password = "1234567890";
    const char* ap_ssid = "ENL-MASTER-UI";
    const char* ap_password = "12345678";
    uint8_t espnow_channel = 1;
    bool prefer_runtime_channel = true;
    bool enforce_channel_match = true;
    bool fallback_to_ap = true;
    uint32_t sta_connect_timeout_ms = 8000U;
    uint16_t port = 80;
    uint32_t reconnect_interval_ms = 10000U;
  };

  MasterWifiFrontendUi() : server_(80) {}

  void begin(espnow_link::ManagementFrontendAdapter* adapter);
  void begin(espnow_link::ManagementFrontendAdapter* adapter, const Config& cfg);
  void loop();
  void setRadioTransitionTestHooks(const std::function<bool(void)>& start_cb,
                                   const std::function<String(void)>& status_cb,
                                   const std::function<bool(void)>& abort_cb);

 private:
  void setupRoutes_();
  void handleRoot_();
  void handlePing_();
  void handleDiscoveryStart_();
  void handleDiscoveryStop_();
  void handleDiscoverySnapshot_();
  void handlePair_();
  void handleUnpair_();
  void handlePairedSnapshot_();
  void handleOperationStatus_();
  void handleOperationWait_();
  void handleEvents_();
  void handleState_();
  void handleCommand_();
  void handleRadioTestStart_();
  void handleRadioTestStatus_();
  void handleRadioTestAbort_();

  bool parseMacArg_(const char* arg_name, espnow_link::MacAddress& out_mac);
  int32_t parseI32Arg_(const char* arg_name, int32_t default_value);
  uint32_t parseU32Arg_(const char* arg_name, uint32_t default_value);
  size_t parseSizeArg_(const char* arg_name, size_t default_value);
  static String macToString_(const espnow_link::MacAddress& mac);
  static String jsonEscape_(const String& in);
  static const char* opStateToString_(espnow_link::ManagementFrontendAdapter::OperationState st);
  static const char* statusToString_(espnow_link::ManagementStatus st);
  bool submitSimple_(uint16_t cmd_id,
                     const std::vector<uint8_t>& payload,
                     espnow_link::ManagementFrontendAdapter::OperationHandle& out_op,
                     uint32_t timeout_ms = 0U);
  bool startApFallback_(const char* reason);
  uint8_t resolveEspNowChannel_();
  void sendJson_(int code, const String& json);
  void tryReconnectWifi_();

  Config cfg_{};
  WebServer server_;
  espnow_link::ManagementFrontendAdapter* adapter_ = nullptr;
  uint32_t last_reconnect_attempt_ms_ = 0U;
  uint8_t espnow_channel_ = 0U;
  bool sta_reconnect_enabled_ = false;
  bool ap_mode_active_ = false;
  std::function<bool(void)> radio_test_start_cb_{};
  std::function<String(void)> radio_test_status_cb_{};
  std::function<bool(void)> radio_test_abort_cb_{};
};
