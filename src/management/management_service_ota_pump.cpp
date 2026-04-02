#include "espnow_link/management_service.hpp"

#include <algorithm>

#include "espnow_link/ota_types.hpp"

namespace espnow_link {

namespace {

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
void ManagementService::pumpOtaPushLocal() {
  if (!ota_push_local_.active) {
    return;
  }
  if (pull_ == nullptr || ota_push_storage_ == nullptr) {
    stopOtaPushLocal(false, ManagementStatus::UnsupportedCommand, 0U, "local ota push unavailable");
    return;
  }

  if (!manager_.hasPersistedPair(ota_push_local_.peer)) {
    stopOtaPushLocal(false, ManagementStatus::NotPaired, 0U, "paired peer lost");
    return;
  }

  constexpr uint32_t kWaitBeginTimeoutMs = 12000U;
  constexpr uint32_t kWaitBeginOptimisticStartMs = 900U;
  constexpr uint8_t kChunkBudgetPerTick = 8U;
  constexpr uint32_t kChunkAckTimeoutMs = 1500U;
  constexpr uint32_t kChunkAckTimeoutRecoveryMs = 450U;
  constexpr uint8_t kChunkAckMaxRetry = 6U;
  constexpr uint8_t kChunkAckMaxRetryRecovery = 20U;
  constexpr uint8_t kWindowRecoveryChunks = 1U;
  constexpr uint32_t kWaitEndTimeoutMs = 60000U;
  constexpr uint32_t kStatusPollIntervalMs = 250U;

  if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitBegin) {
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.last_activity_ms) >=
        static_cast<int32_t>(kWaitBeginTimeoutMs)) {
      stopOtaPushLocal(false, ManagementStatus::Timeout, static_cast<uint16_t>(OtaStatusCode::Timeout), "begin status timeout");
      return;
    }
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.last_status_poll_ms) >=
        static_cast<int32_t>(kStatusPollIntervalMs)) {
      (void)pull_->requestOtaStatus(ota_push_local_.peer, ota_push_local_.req_id);
      ota_push_local_.last_status_poll_ms = now_ms_;
    }
    // Some slaves do not emit an explicit "begin ack" transfer-status event.
    // Avoid deadlock in WAIT_BEGIN by starting stream after a short grace period.
    if (!ota_push_local_.begin_acked &&
        static_cast<int32_t>(now_ms_ - ota_push_local_.started_ms) >=
        static_cast<int32_t>(kWaitBeginOptimisticStartMs)) {
      ota_push_local_.begin_acked = true;
      ota_push_local_.phase = OtaPushLocalSession::Phase::Streaming;
      ota_push_local_.waiting_chunk_ack = false;
      ota_push_local_.retry_count = 0U;
      ota_push_local_.next_send_ms = now_ms_;
    }
    return;
  }

  if (ota_push_local_.phase == OtaPushLocalSession::Phase::WaitEnd) {
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.last_activity_ms) >=
        static_cast<int32_t>(kWaitEndTimeoutMs)) {
      stopOtaPushLocal(false, ManagementStatus::Timeout, static_cast<uint16_t>(OtaStatusCode::Timeout), "finalize timeout");
    }
    return;
  }

  if (ota_push_local_.phase != OtaPushLocalSession::Phase::Streaming) {
    return;
  }

  if (!ota_push_local_.begin_acked) {
    if (static_cast<int32_t>(now_ms_ - ota_push_local_.started_ms) >=
        static_cast<int32_t>(kWaitBeginOptimisticStartMs)) {
      ota_push_local_.begin_acked = true;
      ota_push_local_.next_send_ms = now_ms_;
    } else {
      return;
    }
  }

  if (ota_push_local_.waiting_chunk_ack) {
    if (ota_push_local_.remote_acked_offset >= ota_push_local_.pending_end_offset) {
      ota_push_local_.waiting_chunk_ack = false;
      ota_push_local_.retry_count = 0U;
      ota_push_local_.pending_end_offset = 0U;
    } else if (static_cast<int32_t>(now_ms_ - ota_push_local_.wait_started_ms) >=
               static_cast<int32_t>((ota_push_local_.window_size_chunks <= kWindowRecoveryChunks)
                                        ? kChunkAckTimeoutRecoveryMs
                                        : kChunkAckTimeoutMs)) {
      const uint8_t retry_max =
          (ota_push_local_.window_size_chunks <= kWindowRecoveryChunks)
              ? kChunkAckMaxRetryRecovery
              : kChunkAckMaxRetry;
      if (ota_push_local_.retry_count >= retry_max) {
        stopOtaPushLocal(false,
                         ManagementStatus::Timeout,
                         static_cast<uint16_t>(OtaStatusCode::Timeout),
                         "window ack timeout");
        return;
      }
      ++ota_push_local_.retry_count;
      ota_push_local_.waiting_chunk_ack = false;
      ota_push_local_.pending_end_offset = 0U;
      ota_push_local_.next_offset =
          std::min<uint32_t>(ota_push_local_.remote_acked_offset, ota_push_local_.total_size);
      ota_push_local_.next_send_ms = now_ms_ + 12U;
    }
    return;
  }

  if (ota_push_local_.next_offset >= ota_push_local_.total_size) {
    if (!pull_->sendFirmwareEnd(ota_push_local_.peer,
                                ota_push_local_.total_size,
                                ota_push_local_.image_crc32,
                                ota_push_local_.req_id)) {
      stopOtaPushLocal(false, ManagementStatus::InternalError, static_cast<uint16_t>(OtaStatusCode::InternalError), "end send failed");
      return;
    }
    ota_push_local_.phase = OtaPushLocalSession::Phase::WaitEnd;
    ota_push_local_.last_activity_ms = now_ms_;
    return;
  }

  if (static_cast<int32_t>(now_ms_ - ota_push_local_.next_send_ms) < 0) {
    return;
  }

  if (ota_push_local_.pending_end_offset == 0U ||
      ota_push_local_.next_offset >= ota_push_local_.pending_end_offset) {
    const uint32_t window_bytes = static_cast<uint32_t>(ota_push_local_.chunk_bytes) *
                                  static_cast<uint32_t>(ota_push_local_.window_size_chunks);
    ota_push_local_.pending_end_offset =
        std::min<uint32_t>(ota_push_local_.next_offset + window_bytes, ota_push_local_.total_size);
  }

  uint8_t budget = kChunkBudgetPerTick;
  while (budget > 0U &&
         ota_push_local_.next_offset < ota_push_local_.total_size &&
         ota_push_local_.next_offset < ota_push_local_.pending_end_offset) {
    const size_t req = std::min<size_t>(ota_push_local_.chunk_bytes,
                                        static_cast<size_t>(ota_push_local_.total_size -
                                                            ota_push_local_.next_offset));
    if (ota_push_local_.chunk_buf.size() < req) {
      ota_push_local_.chunk_buf.resize(req);
    }
    size_t out_len = 0U;
    std::string msg;
    if (!ota_push_storage_->readAt(ota_push_local_.path,
                                   ota_push_local_.next_offset,
                                   ota_push_local_.chunk_buf.data(),
                                   req,
                                   out_len,
                                   msg) ||
        out_len == 0U) {
      stopOtaPushLocal(false,
                       ManagementStatus::InternalError,
                       static_cast<uint16_t>(OtaStatusCode::StorageNotReady),
                       "chunk read failed");
      return;
    }
    const uint32_t remaining = ota_push_local_.total_size - ota_push_local_.next_offset;
    if (out_len > remaining) {
      out_len = static_cast<size_t>(remaining);
      if (out_len == 0U) {
        break;
      }
    }

    if (!pull_->sendFirmwareChunk(ota_push_local_.peer,
                                  ota_push_local_.next_offset,
                                  ota_push_local_.chunk_buf.data(),
                                  out_len,
                                  ota_push_local_.req_id)) {
      ++ota_push_local_.send_fail_streak;
      if (ota_push_local_.send_fail_streak >= 24U) {
        stopOtaPushLocal(false,
                         ManagementStatus::InternalError,
                         static_cast<uint16_t>(OtaStatusCode::InternalError),
                         "chunk send failed");
        return;
      }
      const uint32_t backoff_ms = std::min<uint32_t>(
          40U, 2U + (static_cast<uint32_t>(ota_push_local_.send_fail_streak) * 2U));
      ota_push_local_.next_send_ms = now_ms_ + backoff_ms;
      return;
    }

    ota_push_local_.send_fail_streak = 0U;
    ota_push_local_.next_offset += static_cast<uint32_t>(out_len);
    ++ota_push_local_.chunks_sent;
    --budget;
  }

  if (ota_push_local_.next_offset >= ota_push_local_.pending_end_offset) {
    ota_push_local_.waiting_chunk_ack = true;
    ota_push_local_.wait_started_ms = now_ms_;
    ota_push_local_.retry_count = 0U;
  }
}

void ManagementService::stopOtaPushLocal(bool success,
                                         ManagementStatus status,
                                         uint16_t ota_status_code,
                                         const char* reason) {
  if (!ota_push_local_.active) {
    return;
  }
  const uint32_t req_id = ota_push_local_.req_id;
  const MacAddress peer = ota_push_local_.peer;
  const ManagementSource source = ota_push_local_.source;
  const uint16_t owner_cmd_id = ota_push_local_.owner_cmd_id;
  const bool emit_cmd_event = ota_push_local_.emit_cmd_event;

  // Best-effort remote RX cleanup: avoid leaving slave OTA sink stuck in "receiving"
  // after local pipeline failures.
  if (!success && pull_ != nullptr && manager_.hasPersistedPair(peer)) {
    (void)pull_->requestOtaClearScope(peer, "in", req_id);
  }

  if (ota_update_local_.active && ota_update_local_.req_id == req_id) {
    ota_update_local_.last_activity_ms = now_ms_;
    if (!success) {
      stopOtaUpdateLocal(false, status, ota_status_code, reason);
    } else if (!pull_->requestOtaApply(peer, ota_update_local_.image_name, ota_update_local_.req_id)) {
      stopOtaUpdateLocal(false,
                         ManagementStatus::InternalError,
                         static_cast<uint16_t>(OtaStatusCode::ApplyFailed),
                         "apply send failed");
    } else {
      ota_update_local_.phase = OtaUpdateLocalSession::Phase::WaitBoot;
      ota_update_local_.last_activity_ms = now_ms_;
    }
  }

  std::vector<uint8_t> payload;
  appendU32(payload, req_id);
  appendU32(payload, ota_push_local_.next_offset);
  appendU32(payload, ota_push_local_.total_size);
  appendU16(payload, ota_push_local_.chunks_sent);
  appendU16(payload, ota_status_code);
  appendStringU8(payload, (reason != nullptr) ? reason : "");
  if (emit_cmd_event) {
    queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
                source,
                owner_cmd_id,
                req_id,
                status,
                payload});
  }
  ota_push_local_ = OtaPushLocalSession{};
}

void ManagementService::pumpOtaUpdateLocal() {
  if (!ota_update_local_.active) {
    return;
  }

  if (!manager_.hasPersistedPair(ota_update_local_.peer)) {
    stopOtaUpdateLocal(false,
                       ManagementStatus::NotPaired,
                       static_cast<uint16_t>(OtaStatusCode::InvalidState),
                       "paired peer lost");
    return;
  }

  constexpr uint32_t kWaitBootTimeoutMs = 180000U;
  if (ota_update_local_.phase == OtaUpdateLocalSession::Phase::WaitBoot) {
    if (static_cast<int32_t>(now_ms_ - ota_update_local_.last_activity_ms) >=
        static_cast<int32_t>(kWaitBootTimeoutMs)) {
      stopOtaUpdateLocal(false,
                         ManagementStatus::Timeout,
                         static_cast<uint16_t>(OtaStatusCode::Timeout),
                         "boot-complete timeout");
    }
  }
}

void ManagementService::stopOtaUpdateLocal(bool success,
                                           ManagementStatus status,
                                           uint16_t ota_status_code,
                                           const char* reason) {
  if (!ota_update_local_.active) {
    return;
  }

  std::vector<uint8_t> payload;
  appendU32(payload, ota_update_local_.req_id);
  appendU8(payload, static_cast<uint8_t>(ota_update_local_.phase));
  appendU16(payload, ota_status_code);
  appendStringU8(payload, (reason != nullptr) ? reason : "");
  queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
              ota_update_local_.source,
              static_cast<uint16_t>(ManagementCommandId::OtaUpdateStart),
              ota_update_local_.req_id,
              status,
              payload});
  ota_update_local_ = OtaUpdateLocalSession{};
}


}  // namespace espnow_link

