#include "espnow_link/debug_hooks.hpp"

#include <cstdio>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

namespace espnow_link {

namespace {

#ifndef ESPNOW_LINK_LOG_FRAMES
#define ESPNOW_LINK_LOG_FRAMES 0
#endif

const char* toTypeName(MessageType t) {
  switch (t) {
    case MessageType::Discovery:
      return "DISC";
    case MessageType::PairInit:
      return "PAIR_INIT";
    case MessageType::PairInitAck:
      return "PAIR_INIT_ACK";
    case MessageType::PairConfirm:
      return "PAIR_CONFIRM";
    case MessageType::PairConfirmAck:
      return "PAIR_CONFIRM_ACK";
    case MessageType::PairBusy:
      return "PAIR_BUSY";
    case MessageType::PullRequest:
      return "PULL_REQ";
    case MessageType::PullResponse:
      return "PULL_RESP";
    case MessageType::ChannelSwitchPrepare:
      return "CH_SW_PREP";
    case MessageType::ChannelSwitchAck:
      return "CH_SW_ACK";
    case MessageType::ChannelSwitchCommitAck:
      return "CH_SW_COMMIT";
    case MessageType::FirmwareBegin:
      return "FW_BEGIN";
    case MessageType::FirmwareChunk:
      return "FW_CHUNK";
    case MessageType::FirmwareEnd:
      return "FW_END";
    case MessageType::UnpairRequest:
      return "UNPAIR_REQ";
    case MessageType::UnpairAck:
      return "UNPAIR_ACK";
    case MessageType::EventReport:
      return "EVENT";
    case MessageType::FirmwareStatus:
      return "FW_STATUS";
    case MessageType::TopologyTrigger:
      return "TOPO_TRIG";
    case MessageType::TopologyTriggerAck:
      return "TOPO_ACK";
    case MessageType::TopologyTriggerBatch:
      return "TOPO_BATCH";
    default:
      return "ERR";
  }
}

}  // namespace

DebugHooks::DebugHooks(const char* tag, RgbBlinkFn rgb_fn)
    : tag_(tag == nullptr ? "ESPNOW-LINK" : tag), rgb_fn_(rgb_fn) {}

void DebugHooks::onRxFrame(const MacAddress& from,
                           MessageType type,
                           uint32_t corr_id,
                           size_t len,
                           int rssi) {
#if ESPNOW_LINK_LOG_FRAMES
#if defined(ESP_PLATFORM)
  ESP_LOGI(tag_, "[RX] from=%02X:%02X:%02X:%02X:%02X:%02X type=%s corr=%lu len=%u rssi=%d",
           from[0],
           from[1],
           from[2],
           from[3],
           from[4],
           from[5],
           toTypeName(type),
           static_cast<unsigned long>(corr_id),
           static_cast<unsigned int>(len),
           rssi);
#else
  std::printf("[%s][RX] from=%02X:%02X:%02X:%02X:%02X:%02X type=%s corr=%lu len=%u rssi=%d\n",
              tag_,
              from[0],
              from[1],
              from[2],
              from[3],
              from[4],
              from[5],
              toTypeName(type),
              static_cast<unsigned long>(corr_id),
              static_cast<unsigned int>(len),
              rssi);
#endif
#else
  (void)from;
  (void)type;
  (void)corr_id;
  (void)len;
  (void)rssi;
#endif
}

void DebugHooks::onTxFrame(const MacAddress& to,
                           MessageType type,
                           uint32_t corr_id,
                           size_t len,
                           bool ok) {
#if ESPNOW_LINK_LOG_FRAMES
#if defined(ESP_PLATFORM)
  ESP_LOGI(tag_, "[TX] to=%02X:%02X:%02X:%02X:%02X:%02X type=%s corr=%lu len=%u status=%s",
           to[0],
           to[1],
           to[2],
           to[3],
           to[4],
           to[5],
           toTypeName(type),
           static_cast<unsigned long>(corr_id),
           static_cast<unsigned int>(len),
           ok ? "OK" : "FAIL");
#else
  std::printf("[%s][TX] to=%02X:%02X:%02X:%02X:%02X:%02X type=%s corr=%lu len=%u status=%s\n",
              tag_,
              to[0],
              to[1],
              to[2],
              to[3],
              to[4],
              to[5],
              toTypeName(type),
              static_cast<unsigned long>(corr_id),
              static_cast<unsigned int>(len),
              ok ? "OK" : "FAIL");
#endif
#else
  (void)to;
  (void)type;
  (void)corr_id;
  (void)len;
  (void)ok;
#endif
}

void DebugHooks::onRgbBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  if (rgb_fn_ != nullptr) {
    rgb_fn_(r, g, b, ms);
  }
}

}  // namespace espnow_link


