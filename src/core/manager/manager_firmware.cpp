/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Firmware RX begin/chunk/end/status handling behavior.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
bool EspNowManager::handleFirmwareBegin(const MacAddress& from,
                                        uint32_t corr_id,
                                        const uint8_t* payload,
                                        size_t len) {
  if (firmware_sink_ == nullptr || payload == nullptr || len < 12U) {
    return false;
  }

  const uint32_t total_size = readLe32(payload + 0);
  const uint32_t chunk_size = readLe32(payload + 4);
  const uint32_t crc = readLe32(payload + 8);
  FirmwareImageMetadata meta{};
  if (len > 12U) {
    if (len < (12U + kFirmwareMetaHeaderSizeV1)) {
      return false;
    }
    if (payload[12] != kFirmwareMetaMagic) {
      return false;
    }
    const uint8_t meta_version = payload[13];
    const uint8_t sw_len = payload[14];
    const uint8_t build_len = payload[15];
    if (sw_len > 63U || build_len > 63U) {
      return false;
    }
    if (meta_version == kFirmwareMetaVersionV1) {
      const size_t expected_len =
          12U + kFirmwareMetaHeaderSizeV1 + static_cast<size_t>(sw_len) + static_cast<size_t>(build_len);
      if (expected_len != len) {
        return false;
      }
      if (sw_len > 0U) {
        meta.sw_version.assign(reinterpret_cast<const char*>(payload + 16U),
                               reinterpret_cast<const char*>(payload + 16U + sw_len));
      }
      if (build_len > 0U) {
        const size_t off = 16U + static_cast<size_t>(sw_len);
        meta.build_id.assign(reinterpret_cast<const char*>(payload + off),
                             reinterpret_cast<const char*>(payload + off + build_len));
      }
    } else if (meta_version == kFirmwareMetaVersionV2) {
      if (len < (12U + kFirmwareMetaHeaderSizeV2)) {
        return false;
      }
      const uint8_t role_len = payload[16];
      if (role_len > 15U) {
        return false;
      }
      const size_t expected_len = 12U + kFirmwareMetaHeaderSizeV2 + static_cast<size_t>(sw_len) +
                                  static_cast<size_t>(build_len) + static_cast<size_t>(role_len);
      if (expected_len != len) {
        return false;
      }
      size_t off = 17U;
      if (sw_len > 0U) {
        meta.sw_version.assign(reinterpret_cast<const char*>(payload + off),
                               reinterpret_cast<const char*>(payload + off + sw_len));
        off += static_cast<size_t>(sw_len);
      }
      if (build_len > 0U) {
        meta.build_id.assign(reinterpret_cast<const char*>(payload + off),
                             reinterpret_cast<const char*>(payload + off + build_len));
        off += static_cast<size_t>(build_len);
      }
      if (role_len > 0U) {
        meta.target_role.assign(reinterpret_cast<const char*>(payload + off),
                                reinterpret_cast<const char*>(payload + off + role_len));
      }
    } else {
      return false;
    }
  }
  const bool ok = firmware_sink_->begin(from, corr_id, total_size, chunk_size, crc, meta);
  if (config_.local_role == Role::Slave) {
    ota_finalize_pending_ = false;
    ota_finalize_corr_id_ = 0U;
    ota_finalize_offset_ = 0U;
    ota_finalize_status_code_ = 0U;
    ota_finalize_kind_ = 0U;
    ota_finalize_started_ms_ = 0U;
    ota_finalize_last_tx_ms_ = 0U;
    if (ok) {
      ota_transfer_corr_id_ = corr_id;
      ota_transfer_expected_size_ = total_size;
      ota_transfer_last_received_offset_ = 0U;
      ota_transfer_last_acked_offset_ = 0U;
      ota_transfer_ack_step_bytes_ = std::max<uint32_t>(1024U, chunk_size * 16U);
      ota_transfer_next_ack_offset_ = ota_transfer_ack_step_bytes_;
      ota_transfer_last_nack_offset_ = 0U;
      ota_transfer_last_nack_ms_ = 0U;
      (void)sendFirmwareStatus(from,
                               corr_id,
                               kFirmwareStatusKindChunkAck,
                               0U,
                               static_cast<uint16_t>(OtaStatusCode::Ok));
    } else {
      ota_transfer_expected_size_ = 0U;
      ota_transfer_last_acked_offset_ = 0U;
      (void)sendFirmwareStatus(from,
                               corr_id,
                               kFirmwareStatusKindChunkNack,
                               0U,
                               static_cast<uint16_t>(OtaStatusCode::InvalidState));
    }
  }
  return ok;
}

bool EspNowManager::handleFirmwareChunk(const MacAddress& from,
                                        uint32_t corr_id,
                                        const uint8_t* payload,
                                        size_t len) {
  if (firmware_sink_ == nullptr || payload == nullptr || len < 4) {
    return false;
  }

  const uint32_t offset = readLe32(payload);
  const uint8_t* chunk = payload + 4;
  const size_t chunk_len = len - 4;
  const bool track_ota = (config_.local_role == Role::Slave &&
                          ota_transfer_corr_id_ == corr_id);

  const bool ok = firmware_sink_->writeChunk(from, corr_id, offset, chunk, chunk_len);
  if (track_ota) {
    uint32_t expected_offset = ota_transfer_last_received_offset_;
    const uint32_t sink_contiguous = firmware_sink_->contiguousReceiveSize();
    if (sink_contiguous >= expected_offset) {
      expected_offset = sink_contiguous;
    }

    if (ok) {
      ota_transfer_last_received_offset_ = expected_offset;
      const bool tail_complete = (ota_transfer_expected_size_ != 0U &&
                                  expected_offset == ota_transfer_expected_size_);
      const bool threshold_reached = (expected_offset >= ota_transfer_next_ack_offset_);
      const bool should_ack = (threshold_reached || tail_complete || expected_offset == 0U);
      if (should_ack && expected_offset != ota_transfer_last_acked_offset_) {
        (void)sendFirmwareStatus(from,
                                 corr_id,
                                 kFirmwareStatusKindChunkAck,
                                 expected_offset,
                                 static_cast<uint16_t>(OtaStatusCode::Ok));
        ota_transfer_last_acked_offset_ = expected_offset;
        while (ota_transfer_ack_step_bytes_ != 0U &&
               ota_transfer_next_ack_offset_ <= expected_offset) {
          ota_transfer_next_ack_offset_ += ota_transfer_ack_step_bytes_;
        }
      }
    } else {
      const uint32_t now_ms = current_now_ms_;
      ota_transfer_last_received_offset_ = expected_offset;
      const bool nack_same_offset = (expected_offset == ota_transfer_last_nack_offset_);
      const bool nack_recent = nack_same_offset &&
                               static_cast<int32_t>(now_ms - ota_transfer_last_nack_ms_) < 60;
      if (!nack_recent) {
        (void)sendFirmwareStatus(from,
                                 corr_id,
                                 kFirmwareStatusKindChunkNack,
                                 expected_offset,
                                 static_cast<uint16_t>(OtaStatusCode::OffsetMismatch));
        ota_transfer_last_nack_offset_ = expected_offset;
        ota_transfer_last_nack_ms_ = now_ms;
      }
    }
  }
  return ok;
}

bool EspNowManager::handleFirmwareEnd(const MacAddress& from,
                                      uint32_t corr_id,
                                      const uint8_t* payload,
                                      size_t len) {
  if (firmware_sink_ == nullptr || payload == nullptr || len != 8) {
    return false;
  }

  const uint32_t total_size = readLe32(payload + 0);
  const uint32_t crc = readLe32(payload + 4);
  const bool ok = firmware_sink_->end(from, corr_id, total_size, crc);
  if (config_.local_role == Role::Slave) {
    const uint8_t finalize_kind = ok ? kFirmwareStatusKindFinalizeOk : kFirmwareStatusKindFinalizeFail;
    const uint32_t finalize_offset = ok ? total_size : ota_transfer_last_received_offset_;
    uint16_t finalize_code = static_cast<uint16_t>(OtaStatusCode::Ok);
    if (!ok) {
      finalize_code = firmware_sink_->lastOtaStatusCode();
      if (finalize_code == 0U) {
        finalize_code = static_cast<uint16_t>(OtaStatusCode::InvalidState);
      }
    }

    ota_finalize_pending_ = true;
    ota_finalize_peer_ = from;
    ota_finalize_corr_id_ = corr_id;
    ota_finalize_offset_ = finalize_offset;
    ota_finalize_status_code_ = finalize_code;
    ota_finalize_kind_ = finalize_kind;
    ota_finalize_started_ms_ = current_now_ms_;
    ota_finalize_last_tx_ms_ = 0U;

    if (sendFirmwareStatus(from, corr_id, finalize_kind, finalize_offset, finalize_code)) {
      ota_finalize_last_tx_ms_ = current_now_ms_;
    }

    if (ok) {
      uint32_t epoch_s = 0U;
      uint64_t now_epoch = 0U;
      if (time_source_ != nullptr && time_source_->nowEpochSec(now_epoch)) {
        epoch_s = static_cast<uint32_t>(now_epoch & 0xFFFFFFFFULL);
      }
      (void)publishMandatoryEvent(kOtaTransferReadyEventId,
                                  1U,
                                  static_cast<int32_t>(corr_id & 0x7FFFFFFFU),
                                  epoch_s);
    }
    if (ota_transfer_corr_id_ == corr_id) {
      ota_transfer_corr_id_ = 0U;
      ota_transfer_last_received_offset_ = 0U;
      ota_transfer_expected_size_ = 0U;
      ota_transfer_last_acked_offset_ = 0U;
      ota_transfer_ack_step_bytes_ = 0U;
      ota_transfer_next_ack_offset_ = 0U;
      ota_transfer_last_nack_offset_ = 0U;
      ota_transfer_last_nack_ms_ = 0U;
    }
  }
  return ok;
}

bool EspNowManager::handleFirmwareStatus(const MacAddress& from,
                                         uint32_t corr_id,
                                         const uint8_t* payload,
                                         size_t len) {
  (void)corr_id;
  if (payload == nullptr || len < 11U) {
    return false;
  }

  uint8_t kind = 0U;
  uint32_t transfer_corr_id = 0U;
  uint32_t offset_bytes = 0U;
  uint16_t status_code = 0U;
  if (!parseFirmwareStatusPayload(payload, len, kind, transfer_corr_id, offset_bytes, status_code)) {
    return false;
  }

  if (config_.local_role == Role::Slave) {
    if (kind == kFirmwareStatusKindFinalizeAck &&
        ota_finalize_pending_ &&
        ota_finalize_corr_id_ == transfer_corr_id &&
        ota_finalize_peer_ == from) {
      ota_finalize_pending_ = false;
      ota_finalize_corr_id_ = 0U;
      ota_finalize_offset_ = 0U;
      ota_finalize_status_code_ = 0U;
      ota_finalize_kind_ = 0U;
      ota_finalize_started_ms_ = 0U;
      ota_finalize_last_tx_ms_ = 0U;
    }
    return true;
  }

  if (config_.local_role != Role::Master) {
    return true;
  }

  if (events_ != nullptr) {
    Event e{};
    e.type = Event::Type::OtaTransferStatus;
    e.peer = from;
    e.correlation_id = transfer_corr_id;
    e.event_id = kind;
    e.event_value = static_cast<int32_t>(offset_bytes & 0x7FFFFFFFU);
    e.event_ts_s = static_cast<uint32_t>(status_code);
    e.message = "ota transfer status";
    events_->onEvent(e);
  }
  return true;
}

}  // namespace espnow_link

