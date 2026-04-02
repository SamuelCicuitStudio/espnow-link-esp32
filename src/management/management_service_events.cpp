#include "espnow_link/management_service.hpp"

#include <algorithm>

#include "espnow_link/ota_types.hpp"

namespace espnow_link {

namespace {

constexpr size_t kMaxPairedSlaves = 15;
constexpr uint8_t kLiveTransitionReasonPassiveRx = 1;
constexpr uint8_t kOtaStatusKindChunkAck = 0x01;
constexpr uint8_t kOtaStatusKindChunkNack = 0x02;
constexpr uint8_t kOtaStatusKindFinalizeOk = 0x03;
constexpr uint8_t kOtaStatusKindFinalizeFail = 0x04;
constexpr uint8_t kOtaStatusKindFinalizeAck = 0x05;
constexpr uint16_t kOtaBootCompleteEventId = 0x7F10;
constexpr uint16_t kOtaTransferReadyEventId = 0x7F11;

void appendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
void appendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendStringU8(std::vector<uint8_t>& out, const std::string& s) {
  const size_t n = std::min<size_t>(s.size(), 255);
  out.push_back(static_cast<uint8_t>(n));
  out.insert(out.end(), s.begin(), s.begin() + static_cast<std::string::difference_type>(n));
}

}  // namespace

void ManagementService::onEvent(const Event& event) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (event.type == Event::Type::PullResponseSeen) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    if (channel_sync_all_.active) {
      for (size_t i = 0; i < channel_sync_all_.corr_ids.size(); ++i) {
        if (channel_sync_all_.corr_ids[i] == event.correlation_id &&
            channel_sync_all_.peers[i] == event.peer) {
          channel_sync_all_.acked[i] = 1U;
          break;
        }
      }
    }
    if (chain_loop_all_.active) {
      for (size_t i = 0; i < chain_loop_all_.corr_ids.size(); ++i) {
        if (chain_loop_all_.corr_ids[i] == event.correlation_id &&
            chain_loop_all_.peers[i] == event.peer) {
          chain_loop_all_.acked[i] = 1U;
          break;
        }
      }
    }
    return;
  }
  if (event.type == Event::Type::DiscoverySeen) {
    if (local_role_ == Role::Master && manager_.persistedPairCount() >= kMaxPairedSlaves) {
      if (discovery_active_) {
        discovery_active_ = false;
        manager_.setDiscoveryRxEnabled(false);
        queueEvent({ManagementEventId::DiscoveryStopped,
                    ManagementSource::Unknown,
                    0,
                    event.correlation_id,
                    ManagementStatus::Ok,
                    {}});
      }
      return;
    }
    if (std::find(discovered_.begin(), discovered_.end(), event.peer) == discovered_.end()) {
      discovered_.push_back(event.peer);
    }
    std::vector<uint8_t> payload;
    payload.reserve(12U + std::min<size_t>(event.node_name.size(), 255U));
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU16(payload, static_cast<uint16_t>(event.rssi));
    appendStringU8(payload, event.node_name);
    appendU8(payload, event.src_role);
    appendU16(payload, 0U);  // just-seen age in seconds
    queueEvent({ManagementEventId::DiscoveryUpdate, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    return;
  }
  if (event.type == Event::Type::DiscoveryExpired) {
    discovered_.erase(std::remove(discovered_.begin(), discovered_.end(), event.peer), discovered_.end());
    std::vector<uint8_t> payload;
    payload.reserve(6U);
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    queueEvent({ManagementEventId::DiscoveryUpdate, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    return;
  }
  if (event.type == Event::Type::PairingStarted || event.type == Event::Type::PairingStep) {
    std::vector<uint8_t> payload;
    payload.reserve(8U + std::min<size_t>(event.message.size(), 255U));
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::PairProgress, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    if (event.message.find("unpair ack") != std::string::npos ||
        event.message.find("peer removed locally") != std::string::npos) {
      removeLivePeerState(event.peer);
      syncLiveMonitorPeers();
    }
    if (event.message.find("unpair") != std::string::npos) {
      const ManagementStatus unpair_status =
          (event.message.find("timeout") != std::string::npos) ? ManagementStatus::Timeout : ManagementStatus::Ok;
      queueEvent({ManagementEventId::UnpairResult,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  unpair_status,
                  payload});
      ManagementSource source = ManagementSource::Unknown;
      if (consumeDeferredLifecycleCommand(event.correlation_id,
                                          static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                                          source)) {
        if (unpair_status == ManagementStatus::Ok) {
          queueEvent({ManagementEventId::CmdDone,
                      source,
                      static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                      event.correlation_id,
                      ManagementStatus::Ok,
                      {}});
        } else if (unpair_status == ManagementStatus::Timeout) {
          queueEvent({ManagementEventId::Timeout,
                      source,
                      static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                      event.correlation_id,
                      ManagementStatus::Timeout,
                      {}});
        } else {
          queueEvent({ManagementEventId::CmdFail,
                      source,
                      static_cast<uint16_t>(ManagementCommandId::UnpairRequest),
                      event.correlation_id,
                      unpair_status,
                      {}});
        }
      }
    }
    return;
  }
  if (event.type == Event::Type::Paired) {
    syncLiveMonitorPeers();
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.reserve(8U + std::min<size_t>(event.message.size(), 255U));
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::PairResult, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::Ok, payload});
    ManagementSource source = ManagementSource::Unknown;
    if (consumeDeferredLifecycleCommand(event.correlation_id,
                                        static_cast<uint16_t>(ManagementCommandId::PairRequest),
                                        source)) {
      queueEvent({ManagementEventId::CmdDone,
                  source,
                  static_cast<uint16_t>(ManagementCommandId::PairRequest),
                  event.correlation_id,
                  ManagementStatus::Ok,
                  {}});
    }
    if (local_role_ == Role::Master &&
        discovery_active_ &&
        manager_.persistedPairCount() >= kMaxPairedSlaves) {
      discovery_active_ = false;
      manager_.setDiscoveryRxEnabled(false);
      queueEvent({ManagementEventId::DiscoveryStopped,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  ManagementStatus::Ok,
                  {}});
    }
    return;
  }
  if (event.type == Event::Type::PairingFailed) {
    std::vector<uint8_t> payload;
    payload.reserve(8U + std::min<size_t>(event.message.size(), 255U));
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::PairResult, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::InternalError, payload});
    ManagementSource source = ManagementSource::Unknown;
    if (consumeDeferredLifecycleCommand(event.correlation_id,
                                        static_cast<uint16_t>(ManagementCommandId::PairRequest),
                                        source)) {
      const bool timeout = event.message.find("timeout") != std::string::npos;
      if (timeout) {
        queueEvent({ManagementEventId::Timeout,
                    source,
                    static_cast<uint16_t>(ManagementCommandId::PairRequest),
                    event.correlation_id,
                    ManagementStatus::Timeout,
                    {}});
      } else {
        queueEvent({ManagementEventId::CmdFail,
                    source,
                    static_cast<uint16_t>(ManagementCommandId::PairRequest),
                    event.correlation_id,
                    ManagementStatus::InternalError,
                    {}});
      }
    }
    return;
  }
  if (event.type == Event::Type::MandatoryEventReceived) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.reserve(17U);
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU16(payload, event.event_id);
    appendU8(payload, event.severity);
    appendU32(payload, static_cast<uint32_t>(event.event_value));
    appendU32(payload, event.event_ts_s);
    if (event.event_id == kOtaTransferReadyEventId) {
      if (ota_push_local_.active &&
          ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd &&
          event.peer == ota_push_local_.peer &&
          event.correlation_id == ota_push_local_.req_id) {
        stopOtaPushLocal(true,
                         ManagementStatus::Ok,
                         static_cast<uint16_t>(OtaStatusCode::Ok),
                         "local ota push complete");
      }
      queueEvent({ManagementEventId::OtaTransferReady,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  ManagementStatus::Ok,
                  payload});
      return;
    }
    if (event.event_id == kOtaBootCompleteEventId) {
      if (ota_update_local_.active &&
          ota_update_local_.phase == OtaUpdateLocalSession::Phase::WaitBoot &&
          event.peer == ota_update_local_.peer) {
        stopOtaUpdateLocal(true,
                           ManagementStatus::Ok,
                           static_cast<uint16_t>(OtaStatusCode::Ok),
                           "local ota update complete");
      }
      queueEvent({ManagementEventId::OtaBootComplete,
                  ManagementSource::Unknown,
                  0,
                  event.correlation_id,
                  ManagementStatus::Ok,
                  payload});
      return;
    }
  }
  if (event.type == Event::Type::OtaTransferStatus) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.reserve(13U);
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU8(payload, static_cast<uint8_t>(event.event_id & 0xFFU));
    appendU32(payload, static_cast<uint32_t>(event.event_value & 0x7FFFFFFFU));
    appendU16(payload, static_cast<uint16_t>(event.event_ts_s & 0xFFFFU));

    const uint8_t kind = static_cast<uint8_t>(event.event_id & 0xFFU);
    const uint32_t offset =
        static_cast<uint32_t>((event.event_value < 0) ? 0 : event.event_value);
    const uint16_t status_code = static_cast<uint16_t>(event.event_ts_s & 0xFFFFU);
    if (kind == kOtaStatusKindFinalizeOk || kind == kOtaStatusKindFinalizeFail) {
      (void)manager_.sendFirmwareFinalizeAck(event.peer,
                                             event.correlation_id,
                                             static_cast<uint32_t>(event.event_value & 0x7FFFFFFFU),
                                             status_code);
    }

    if (ota_push_local_.active && event.correlation_id == ota_push_local_.req_id) {
      ota_push_local_.last_activity_ms = now_ms_;
      if (kind == kOtaStatusKindChunkAck) {
        if (offset > ota_push_local_.remote_acked_offset) {
          ota_push_local_.remote_acked_offset = offset;
        }
        if (ota_push_local_.recovery_until_acked_offset != 0U &&
            ota_push_local_.remote_acked_offset >= ota_push_local_.recovery_until_acked_offset) {
          ota_push_local_.recovery_until_acked_offset = 0U;
          ota_push_local_.window_size_chunks = 16U;
        }
        if (!ota_push_local_.begin_acked && offset == 0U) {
          ota_push_local_.begin_acked = true;
          ota_push_local_.phase = OtaPushLocalSession::Phase::Streaming;
          ota_push_local_.waiting_chunk_ack = false;
          ota_push_local_.retry_count = 0U;
          ota_push_local_.next_send_ms = now_ms_;
        } else if (ota_push_local_.phase == OtaPushLocalSession::Phase::Streaming &&
                   ota_push_local_.waiting_chunk_ack &&
                   offset >= ota_push_local_.pending_end_offset) {
          ota_push_local_.waiting_chunk_ack = false;
          ota_push_local_.retry_count = 0U;
          ota_push_local_.pending_end_offset = 0U;
        }
      } else if (kind == kOtaStatusKindChunkNack) {
        if (ota_push_local_.phase == OtaPushLocalSession::Phase::Streaming) {
          uint32_t rewind_offset = std::min<uint32_t>(offset, ota_push_local_.total_size);
          if (rewind_offset > ota_push_local_.next_offset) {
            rewind_offset = ota_push_local_.next_offset;
          }
          if (rewind_offset < ota_push_local_.remote_acked_offset) {
            rewind_offset = ota_push_local_.remote_acked_offset;
          }
          ota_push_local_.remote_acked_offset = rewind_offset;
          ota_push_local_.waiting_chunk_ack = false;
          ota_push_local_.retry_count = 0U;
          ota_push_local_.next_offset = rewind_offset;
          ota_push_local_.pending_end_offset = 0U;
          ota_push_local_.window_size_chunks = 1U;
          ota_push_local_.recovery_until_acked_offset =
              std::min<uint32_t>(rewind_offset + static_cast<uint32_t>(ota_push_local_.chunk_bytes),
                                 ota_push_local_.total_size);
          ota_push_local_.next_send_ms = now_ms_ + 20U;
        }
      } else if (kind == kOtaStatusKindFinalizeOk) {
        if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd) {
          stopOtaPushLocal(true, ManagementStatus::Ok, status_code, "local ota push complete");
        }
      } else if (kind == kOtaStatusKindFinalizeFail) {
        if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd) {
          stopOtaPushLocal(false, ManagementStatus::InternalError, status_code, "local ota finalize failed");
        }
      }
    }

    const ManagementStatus status =
        (kind == kOtaStatusKindChunkNack || kind == kOtaStatusKindFinalizeFail)
            ? ManagementStatus::InternalError
            : ManagementStatus::Ok;
    queueEvent({ManagementEventId::OtaTransferStatus,
                ManagementSource::Unknown,
                0,
                event.correlation_id,
                status,
                payload});
    if (kind == kOtaStatusKindFinalizeAck) {
      return;
    }
    return;
  }
  if (event.type == Event::Type::TopologyTriggerReceived ||
      event.type == Event::Type::TopologyTriggerRejected ||
      event.type == Event::Type::TopologyTriggerDuplicate ||
      event.type == Event::Type::TopologyTriggerAck) {
    noteLivePeerSeen(event.peer, kLiveTransitionReasonPassiveRx);
    std::vector<uint8_t> payload;
    payload.reserve(23U);
    appendU8(payload, 1U);  // schema_version
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendU16(payload, event.event_id);

    uint8_t state = 0U;
    ManagementEventId event_id = ManagementEventId::TopologyTriggerReceived;
    ManagementStatus status = ManagementStatus::Ok;
    if (event.type == Event::Type::TopologyTriggerRejected) {
      state = 1U;
      event_id = ManagementEventId::TopologyTriggerRejected;
      status = ManagementStatus::DeniedByRole;
    } else if (event.type == Event::Type::TopologyTriggerDuplicate) {
      state = 2U;
      event_id = ManagementEventId::TopologyTriggerReceived;
    } else if (event.type == Event::Type::TopologyTriggerAck) {
      state = 3U;
      event_id = ManagementEventId::TopologyTriggerAck;
      if (event.result != 0U) {
        status = ManagementStatus::DeniedByRole;
      }
    }

    appendU8(payload, state);
    appendU8(payload, event.reason);
    appendU8(payload, event.result);
    appendU8(payload, event.src_role);
    appendU8(payload, event.src_vid);
    appendU8(payload, event.dst_role);
    appendU8(payload, event.dst_vid);
    appendU8(payload, event.direction);
    appendU16(payload, event.delay_ms);
    appendU16(payload, event.hold_ms);
    appendU16(payload, event.ack_seq);
    queueEvent({event_id,
                ManagementSource::Unknown,
                0,
                event.correlation_id,
                status,
                payload});
    return;
  }
  if (event.type == Event::Type::LinkError || event.type == Event::Type::PacketDropped) {
    std::vector<uint8_t> payload;
    payload.reserve(8U + std::min<size_t>(event.message.size(), 255U));
    payload.insert(payload.end(), event.peer.begin(), event.peer.end());
    appendStringU8(payload, event.message);
    queueEvent({ManagementEventId::CmdFail, ManagementSource::Unknown, 0, event.correlation_id, ManagementStatus::InternalError, payload});
  }
}

bool ManagementService::onPullRequest(const MacAddress& from,
                                      uint32_t corr_id,
                                      const uint8_t* payload,
                                      size_t len) {
  (void)from;
  (void)corr_id;
  (void)payload;
  (void)len;
  return true;
}

}  // namespace espnow_link
