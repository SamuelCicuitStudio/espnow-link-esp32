#include "espnow_link/manager.hpp"
#include "manager_helpers.hpp"

namespace espnow_link {

using namespace manager_helpers;

bool EspNowManager::dispatchRxByType(const MacAddress& from,
                                     const FrameHeader& header,
                                     const uint8_t* payload,
                                     size_t payload_len,
                                     int rssi) {
  auto enqueue_control_rx = [&](MessageType type) {
    QueuedControlRx frame{};
    frame.type = type;
    frame.from = from;
    frame.corr_id = header.correlation_id;
    frame.queued_ms = current_now_ms_;
    frame.rssi = rssi;
    if (payload != nullptr && payload_len > 0U) {
      frame.payload.assign(payload, payload + payload_len);
    }

    constexpr size_t kQueueMax = 32U;
    if (control_rx_queue_.size() >= kQueueMax) {
      control_rx_queue_.pop_front();
    }
    control_rx_queue_.push_back(std::move(frame));
    return true;
  };

  auto enqueue_firmware_rx = [&](MessageType type) {
    QueuedFirmwareRx frame{};
    frame.type = type;
    frame.from = from;
    frame.corr_id = header.correlation_id;
    frame.queued_ms = current_now_ms_;
    frame.rssi = rssi;
    if (payload != nullptr && payload_len > 0U) {
      frame.payload.assign(payload, payload + payload_len);
    }

    constexpr size_t kQueueMax = 32U;
    if (firmware_rx_queue_.size() >= kQueueMax) {
      firmware_rx_queue_.pop_front();
    }
    firmware_rx_queue_.push_back(std::move(frame));
    return true;
  };

  switch (header.type) {
    case MessageType::Discovery:
      return enqueue_control_rx(MessageType::Discovery);

    case MessageType::PairInit:
      return enqueue_control_rx(MessageType::PairInit);

    case MessageType::PairInitAck:
      return enqueue_control_rx(MessageType::PairInitAck);

    case MessageType::PairConfirm:
      return enqueue_control_rx(MessageType::PairConfirm);

    case MessageType::PairConfirmAck:
      return enqueue_control_rx(MessageType::PairConfirmAck);

    case MessageType::PairBusy:
      return enqueue_control_rx(MessageType::PairBusy);

    case MessageType::UnpairRequest:
      return enqueue_control_rx(MessageType::UnpairRequest);

    case MessageType::UnpairAck:
      return enqueue_control_rx(MessageType::UnpairAck);

    case MessageType::PullRequest:
      return onRxPullRequest(from, header, payload, payload_len);

    case MessageType::PullResponse:
      return onRxPullResponse(from, header, payload, payload_len);

    case MessageType::EventReport:
      return enqueue_control_rx(MessageType::EventReport);

    case MessageType::TopologyTrigger:
      return enqueue_control_rx(MessageType::TopologyTrigger);

    case MessageType::TopologyTriggerAck:
      return enqueue_control_rx(MessageType::TopologyTriggerAck);

    case MessageType::ChannelSwitchPrepare:
      return enqueue_control_rx(MessageType::ChannelSwitchPrepare);

    case MessageType::ChannelSwitchAck:
      return enqueue_control_rx(MessageType::ChannelSwitchAck);

    case MessageType::ChannelSwitchCommitAck:
      return enqueue_control_rx(MessageType::ChannelSwitchCommitAck);

    case MessageType::FirmwareBegin:
      if (config_.local_role == Role::Slave) {
        return enqueue_firmware_rx(MessageType::FirmwareBegin);
      }
      blinkForMessage(header.type);
      return handleFirmwareBegin(from, header.correlation_id, payload, payload_len);

    case MessageType::FirmwareChunk:
      if (config_.local_role == Role::Slave) {
        return enqueue_firmware_rx(MessageType::FirmwareChunk);
      }
      blinkForMessage(header.type);
      return handleFirmwareChunk(from, header.correlation_id, payload, payload_len);

    case MessageType::FirmwareEnd:
      if (config_.local_role == Role::Slave) {
        return enqueue_firmware_rx(MessageType::FirmwareEnd);
      }
      blinkForMessage(header.type);
      return handleFirmwareEnd(from, header.correlation_id, payload, payload_len);

    case MessageType::FirmwareStatus:
      return enqueue_control_rx(MessageType::FirmwareStatus);

    default:
      return false;
  }
}

bool EspNowManager::onRxDiscovery(const MacAddress& from,
                                  const FrameHeader& header,
                                  const uint8_t* payload,
                                  size_t payload_len,
                                  int rssi) {
  if (config_.local_role == Role::Master && !discoveryRxEnabled()) {
    // Explicitly drop discovery frames unless the management discovery window is active.
    return true;
  }
  if (config_.local_role == Role::Master && persistedPairCount() >= 14U) {
    return true;
  }
  touchDiscovery(from);
  (void)peer_registry_.upsertDiscovery(from, static_cast<int16_t>(rssi), current_now_ms_);
  const std::string node_name = decodeDiscoveryName(payload, payload_len);
  const uint8_t role_code = decodeDiscoveryRoleCode(payload, payload_len);
  if (role_code != 0U) {
    upsertPeerRoleHintCache_(from, role_code);
    if (hasPersistedPair(from)) {
      (void)savePeerRoleHint(from, role_code);
    }
  }
  if (events_ != nullptr) {
    Event e{};
    e.type = Event::Type::DiscoverySeen;
    e.peer = from;
    e.correlation_id = header.correlation_id;
    e.message = "discovery rx";
    e.rssi = static_cast<int16_t>(rssi);
    e.node_name = node_name;
    e.src_role = role_code;
    events_->onEvent(e);
  }
  blinkForMessage(header.type);
  return pairing_.onDiscovery(from, header.correlation_id);
}

bool EspNowManager::onRxPairInit(const MacAddress& from,
                                 const FrameHeader& header,
                                 const uint8_t* payload,
                                 size_t payload_len) {
  PairSeed seed{};
  uint32_t pairing_nonce = 0;
  uint8_t profile = 0;
  if (!parsePairInitPayload(payload, payload_len, seed, pairing_nonce, profile)) {
    return false;
  }
  // Profile value 0 is treated as wildcard target profile.
  if (local_profile_id_ != kProfileUnknown &&
      profile != 0U &&
      profile != static_cast<uint8_t>(local_profile_id_)) {
    return false;
  }
  blinkForMessage(header.type);
  return pairing_.onPairInit(from, header.correlation_id, seed, pairing_nonce);
}

bool EspNowManager::onRxPairInitAck(const MacAddress& from,
                                    const FrameHeader& header,
                                    const uint8_t* payload,
                                    size_t payload_len) {
  uint32_t nonce_echo = 0;
  if (!parseNonceEchoPayload(payload, payload_len, 0x20, nonce_echo)) {
    return false;
  }
  blinkForMessage(header.type);
  return pairing_.onPairInitAck(from, header.correlation_id, nonce_echo);
}

bool EspNowManager::onRxPairConfirm(const MacAddress& from,
                                    const FrameHeader& header,
                                    const uint8_t* payload,
                                    size_t payload_len) {
  uint32_t nonce_echo = 0;
  if (!parseNonceEchoPayload(payload, payload_len, 0x30, nonce_echo)) {
    return false;
  }
  blinkForMessage(header.type);
  return pairing_.onPairConfirm(from, header.correlation_id, nonce_echo);
}

bool EspNowManager::onRxPairConfirmAck(const MacAddress& from,
                                       const FrameHeader& header,
                                       const uint8_t* payload,
                                       size_t payload_len) {
  bool paired_flag = false;
  if (!parsePairConfirmAckPayload(payload, payload_len, paired_flag)) {
    return false;
  }
  blinkForMessage(header.type);
  const bool ok = pairing_.onPairConfirmAck(from, header.correlation_id, paired_flag);
  if (ok && paired_flag) {
    (void)peer_registry_.markPaired(from, current_now_ms_);
    uint8_t role_hint = 0U;
    if (findPeerRoleHintCache_(from, role_hint) && role_hint != 0U) {
      (void)savePeerRoleHint(from, role_hint);
    }
    if (config_.local_role == Role::Slave && has_topology_committed_) {
      std::string topology_error{};
      (void)materializeTopologyPeers_(topology_committed_, &topology_error);
    }
  }
  return ok;
}

bool EspNowManager::onRxUnpairRequest(const MacAddress& from, const FrameHeader& header) {
  blinkForMessage(header.type);
  if (config_.local_role == Role::Slave) {
    push_session_ = TelemetryPushSession{};
    mandatory_event_queue_.clear();
    (void)clearTelemetryPushConfig(from);
  }
  return pairing_.onUnpairRequest(from, header.correlation_id);
}

bool EspNowManager::onRxPullRequest(const MacAddress& from,
                                    const FrameHeader& header,
                                    const uint8_t* payload,
                                    size_t payload_len) {
  if (config_.local_role != Role::Slave) {
    return false;
  }

  QueuedPullRequest request{};
  request.from = from;
  request.corr_id = header.correlation_id;
  request.wire_service = header.wire_service;
  request.wire_op = header.wire_op_code;
  request.queued_ms = current_now_ms_;
  if (payload != nullptr && payload_len > 0U) {
    request.payload.assign(payload, payload + payload_len);
  }

  constexpr size_t kQueueMax = 8U;
  if (pull_request_queue_.size() >= kQueueMax) {
    pull_request_queue_.pop_front();
  }
  pull_request_queue_.push_back(std::move(request));
  return true;
}

bool EspNowManager::onRxPullResponse(const MacAddress& from,
                                     const FrameHeader& header,
                                     const uint8_t* payload,
                                     size_t payload_len) {
  if (config_.local_role == Role::Master) {
    QueuedPullResponse response{};
    response.from = from;
    response.corr_id = header.correlation_id;
    response.queued_ms = current_now_ms_;
    if (payload != nullptr && payload_len > 0U) {
      response.payload.assign(payload, payload + payload_len);
    }

    constexpr size_t kQueueMax = 32U;
    if (pull_response_queue_.size() >= kQueueMax) {
      pull_response_queue_.pop_front();
    }
    pull_response_queue_.push_back(std::move(response));
    return true;
  }

  if (events_ != nullptr) {
    events_->onEvent({Event::Type::PullResponseSeen, from, header.correlation_id, "pull response rx"});
  }
  blinkForMessage(header.type);
  (void)peer_registry_.updateLiveness(from, true, current_now_ms_);
  return false;
}

bool EspNowManager::onRxEventReport(const MacAddress& from,
                                    const FrameHeader& header,
                                    const uint8_t* payload,
                                    size_t payload_len) {
  if (config_.local_role != Role::Master) {
    return false;
  }

  uint16_t event_id = 0;
  uint8_t severity = 0;
  int32_t event_value = 0;
  uint32_t event_ts_s = 0;
  if (!parseMandatoryEventPayload(payload, payload_len, event_id, severity, event_value, event_ts_s)) {
    return false;
  }

  if (events_ != nullptr) {
    Event e{};
    e.type = Event::Type::MandatoryEventReceived;
    e.peer = from;
    e.correlation_id = header.correlation_id;
    e.event_id = event_id;
    e.severity = severity;
    e.event_value = event_value;
    e.event_ts_s = event_ts_s;
    e.message = "mandatory event";
    events_->onEvent(e);
  }
  blinkForMessage(header.type);
  return true;
}

bool EspNowManager::onRxChannelSwitchPrepare(const MacAddress& from,
                                             const FrameHeader& header,
                                             const uint8_t* payload,
                                             size_t payload_len) {
  ChannelSwitchPayload cs{};
  if (!parseChannelSwitchPayload(payload, payload_len, cs)) {
    return false;
  }
  blinkForMessage(header.type);
  return pairing_.onChannelSwitchPrepare(from, header.correlation_id, cs);
}

}  // namespace espnow_link
