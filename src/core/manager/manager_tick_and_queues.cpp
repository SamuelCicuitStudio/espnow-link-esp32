/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Main tick loop and deferred RX/queue processing paths.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
void EspNowManager::tick(uint32_t now_ms) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ScopedMetricsDuration tick_duration(&metrics_.tick_total_us,
                                      &metrics_.tick_last_us,
                                      &metrics_.tick_max_us);
  ++metrics_.tick_count;
#endif
  current_now_ms_ = now_ms;
  pairing_.tick(now_ms);
  syncPairedCacheState_();
  tickControlRxQueue(now_ms);
  tickPullRequestQueue(now_ms);
  tickPullResponseQueue(now_ms);
  tickFirmwareRxQueue(now_ms);
  tickDiscoveryCache(now_ms);
  tickOtaBootCompletionNotice(now_ms);
  tickOtaFinalizeStatusRetry(now_ms);
  tickMandatoryEventQueue(now_ms);
  tickTelemetryPush(now_ms);
}

void EspNowManager::tickPullRequestQueue(uint32_t now_ms) {
  (void)now_ms;
  if (config_.local_role != Role::Slave || pull_request_queue_.empty()) {
    return;
  }

  size_t budget = kPullRequestDispatchBudgetPerTick;
  while (budget-- > 0U && !pull_request_queue_.empty()) {
    QueuedPullRequest request = std::move(pull_request_queue_.front());
    pull_request_queue_.pop_front();
    (void)processQueuedPullRequest_(request);
    pull_request_recycle_ = std::move(request);
    has_pull_request_recycle_ = true;
  }
}

bool EspNowManager::processQueuedPullRequest_(const QueuedPullRequest& request) {
  if (events_ != nullptr) {
    events_->onEvent({Event::Type::PullRequestSeen, request.from, request.corr_id, "pull request rx"});
  }
  blinkForMessage(MessageType::PullRequest);

  pending_pull_.valid = true;
  pending_pull_.peer = request.from;
  pending_pull_.corr_id = request.corr_id;
  pending_pull_.service = request.wire_service;
  pending_pull_.op = request.wire_op;

  const uint8_t* payload = request.payload.empty() ? nullptr : request.payload.data();
  const size_t payload_len = request.payload.size();

  if (handleTelemetryPushControl(request.from, request.corr_id, payload, payload_len)) {
    return true;
  }

  if (control_plane_ != nullptr) {
    const bool handled = control_plane_->onPullRequest(request.from, request.corr_id, payload, payload_len);
    if (handled) {
      return true;
    }
  }

  // Never silently drop undecoded/unsupported pull payloads.
  // Return an explicit error response so master-side flows do not stall on timeout.
  std::vector<uint8_t> reply;
  if (buildDescriptorReply(false, "pull request unsupported", reply)) {
    (void)sendPullResponse(request.from, reply.data(), reply.size(), request.corr_id);
  }
  return false;
}

void EspNowManager::tickPullResponseQueue(uint32_t now_ms) {
  (void)now_ms;
  if (config_.local_role != Role::Master || pull_response_queue_.empty()) {
    return;
  }

  size_t budget = kPullResponseDispatchBudgetPerTick;
  while (budget-- > 0U && !pull_response_queue_.empty()) {
    QueuedPullResponse response = std::move(pull_response_queue_.front());
    pull_response_queue_.pop_front();
    (void)processQueuedPullResponse_(response);
    pull_response_recycle_ = std::move(response);
    has_pull_response_recycle_ = true;
  }
}

bool EspNowManager::processQueuedPullResponse_(const QueuedPullResponse& response) {
  if (events_ != nullptr) {
    events_->onEvent({Event::Type::PullResponseSeen, response.from, response.corr_id, "pull response rx"});
  }
  blinkForMessage(MessageType::PullResponse);
  (void)peer_registry_.updateLiveness(response.from, true, current_now_ms_);

  if (control_plane_ == nullptr) {
    return true;
  }
  const uint8_t* payload = response.payload.empty() ? nullptr : response.payload.data();
  return control_plane_->onPullResponse(response.from, response.corr_id, payload, response.payload.size());
}

void EspNowManager::tickFirmwareRxQueue(uint32_t now_ms) {
  (void)now_ms;
  if (config_.local_role != Role::Slave || firmware_rx_queue_.empty()) {
    return;
  }

  size_t budget = kFirmwareRxDispatchBudgetPerTick;
  while (budget-- > 0U && !firmware_rx_queue_.empty()) {
    QueuedFirmwareRx frame = std::move(firmware_rx_queue_.front());
    firmware_rx_queue_.pop_front();
    (void)processQueuedFirmwareRx_(frame);
    firmware_rx_recycle_ = std::move(frame);
    has_firmware_rx_recycle_ = true;
  }
}

bool EspNowManager::processQueuedFirmwareRx_(const QueuedFirmwareRx& frame) {
  const uint8_t* payload = frame.payload.empty() ? nullptr : frame.payload.data();
  const size_t payload_len = frame.payload.size();

  switch (frame.type) {
    case MessageType::FirmwareBegin:
      blinkForMessage(MessageType::FirmwareBegin);
      return handleFirmwareBegin(frame.from, frame.corr_id, payload, payload_len);
    case MessageType::FirmwareChunk:
      blinkForMessage(MessageType::FirmwareChunk);
      return handleFirmwareChunk(frame.from, frame.corr_id, payload, payload_len);
    case MessageType::FirmwareEnd:
      blinkForMessage(MessageType::FirmwareEnd);
      return handleFirmwareEnd(frame.from, frame.corr_id, payload, payload_len);
    default:
      return false;
  }
}

void EspNowManager::tickControlRxQueue(uint32_t now_ms) {
  (void)now_ms;
  if (control_rx_queue_.empty()) {
    return;
  }

  size_t budget = kControlRxDispatchBudgetPerTick;
  while (budget-- > 0U && !control_rx_queue_.empty()) {
    QueuedControlRx frame = std::move(control_rx_queue_.front());
    control_rx_queue_.pop_front();
    (void)processQueuedControlRx_(frame);
    control_rx_recycle_ = std::move(frame);
    has_control_rx_recycle_ = true;
  }
}

bool EspNowManager::processQueuedControlRx_(const QueuedControlRx& frame) {
  FrameHeader header{};
  header.type = frame.type;
  header.correlation_id = frame.corr_id;
  const uint8_t* payload = frame.payload.empty() ? nullptr : frame.payload.data();
  const size_t payload_len = frame.payload.size();

  switch (frame.type) {
    case MessageType::Discovery:
      return onRxDiscovery(frame.from, header, payload, payload_len, frame.rssi);
    case MessageType::PairInit:
      return onRxPairInit(frame.from, header, payload, payload_len);
    case MessageType::PairInitAck:
      return onRxPairInitAck(frame.from, header, payload, payload_len);
    case MessageType::PairConfirm:
      return onRxPairConfirm(frame.from, header, payload, payload_len);
    case MessageType::PairConfirmAck:
      return onRxPairConfirmAck(frame.from, header, payload, payload_len);
    case MessageType::PairBusy:
      blinkForMessage(frame.type);
      return pairing_.onPairBusy(frame.from, frame.corr_id);
    case MessageType::UnpairRequest:
      return onRxUnpairRequest(frame.from, header);
    case MessageType::UnpairAck:
      blinkForMessage(frame.type);
      return pairing_.onUnpairAck(frame.from, frame.corr_id);
    case MessageType::EventReport:
      return onRxEventReport(frame.from, header, payload, payload_len);
    case MessageType::TopologyTrigger:
      return onRxTopologyTrigger(frame.from, header, payload, payload_len);
    case MessageType::TopologyTriggerBatch:
      return onRxTopologyTriggerBatch(frame.from, header, payload, payload_len);
    case MessageType::TopologyTriggerAck:
      return onRxTopologyTriggerAck(frame.from, header, payload, payload_len);
    case MessageType::ChannelSwitchPrepare:
      return onRxChannelSwitchPrepare(frame.from, header, payload, payload_len);
    case MessageType::ChannelSwitchAck:
      blinkForMessage(frame.type);
      return pairing_.onChannelSwitchAck(frame.from, frame.corr_id);
    case MessageType::ChannelSwitchCommitAck:
      blinkForMessage(frame.type);
      return pairing_.onChannelSwitchCommitAck(frame.from, frame.corr_id);
    case MessageType::FirmwareStatus:
      blinkForMessage(frame.type);
      return handleFirmwareStatus(frame.from, frame.corr_id, payload, payload_len);
    default:
      return false;
  }
}

void EspNowManager::getDiscoveredPeers(std::vector<MacAddress>& out) const {
  out.clear();
  out.reserve(discovery_cache_.size());
  for (const auto& e : discovery_cache_) {
    out.push_back(e.mac);
  }
}

void EspNowManager::touchDiscovery(const MacAddress& from) {
  for (auto& e : discovery_cache_) {
    if (e.mac == from) {
      e.ttl_ms = config_.discovery_peer_ttl_ms;
      e.last_tick_ms = 0;
      return;
    }
  }

  DiscoveryCacheEntry e{};
  e.mac = from;
  e.ttl_ms = config_.discovery_peer_ttl_ms;
  e.last_tick_ms = 0;
  discovery_cache_.push_back(e);
}

void EspNowManager::tickDiscoveryCache(uint32_t now_ms) {
  for (size_t i = 0; i < discovery_cache_.size();) {
    auto& e = discovery_cache_[i];
    if (e.ttl_ms == 0) {
      if (events_ != nullptr) {
        events_->onEvent({Event::Type::DiscoveryExpired, e.mac, 0, "discovery ttl expired"});
      }
      discovery_cache_.erase(discovery_cache_.begin() + static_cast<long>(i));
      continue;
    }

    if (e.last_tick_ms == 0) {
      e.last_tick_ms = now_ms;
      ++i;
      continue;
    }

    const uint32_t elapsed = now_ms - e.last_tick_ms;
    e.last_tick_ms = now_ms;
    if (elapsed >= e.ttl_ms) {
      e.ttl_ms = 0;
    } else {
      e.ttl_ms -= elapsed;
    }
    ++i;
  }
}

}  // namespace espnow_link

