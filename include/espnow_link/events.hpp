#pragma once

#include <cstdint>
#include <string>

#include "espnow_link/types.hpp"

namespace espnow_link {

/** @brief Runtime event payload emitted by manager and propagated to sinks. */
struct Event {
  /** @brief Event category. */
  enum class Type : uint8_t {
    DiscoverySeen,
    DiscoveryExpired,
    PairingStarted,
    PairingStep,
    Paired,
    PairingFailed,
    PullRequestSeen,
    PullResponseSeen,
    ChannelSwitchStarted,
    ChannelSwitchCommitted,
    ChannelSwitchFailed,
    LinkError,
    MandatoryEventReceived,
    MandatoryEventSent,
    MandatoryEventDropped,
    TopologyTriggerReceived,
    TopologyTriggerRejected,
    TopologyTriggerDuplicate,
    TopologyTriggerAck,
    OtaTransferStatus,
    PacketDropped,
  };

  Type type = Type::PacketDropped;
  MacAddress peer{};
  uint32_t correlation_id = 0;
  std::string message;
  int16_t rssi = 0;
  std::string node_name;
  uint16_t event_id = 0;
  uint8_t severity = 0;
  int32_t event_value = 0;
  uint32_t event_ts_s = 0;
  uint8_t src_role = 0;
  uint8_t src_vid = 0xFF;
  uint8_t dst_role = 0;
  uint8_t dst_vid = 0xFF;
  uint8_t direction = 0;
  uint8_t reason = 0;
  uint8_t result = 0;
  uint16_t delay_ms = 0;
  uint16_t hold_ms = 0;
  uint16_t ack_seq = 0;
};

/**
 * @brief Event sink interface for observing manager runtime behavior.
 */
class IEventSink {
 public:
  virtual ~IEventSink() = default;

  /**
   * @brief Receive one runtime event.
   * @param event Event payload.
   */
  virtual void onEvent(const Event& event) = 0;
};

}  // namespace espnow_link
