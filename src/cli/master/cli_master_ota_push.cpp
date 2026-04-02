/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: OTA push/update state machine internals and pacing/retry behavior.
 **************************************************************/
#include "../internal/cli_master_internal.hpp"
#include "../internal/cli_master_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

bool MasterCli::computeOtaPushCrc(const std::string& local_path,
                                  uint32_t size_bytes,
                                  uint32_t& out_crc,
                                  std::string* out_error) {
  if (ota_push_storage_ == nullptr) {
    if (out_error != nullptr) {
      *out_error = "storage backend is null";
    }
    return false;
  }
  if (size_bytes == 0U) {
    return false;
  }

  constexpr uint32_t kCrc32Poly = 0xEDB88320U;
  auto crcUpdate = [](uint32_t running_crc, const uint8_t* data, size_t len) -> uint32_t {
    if (data == nullptr || len == 0U) {
      return running_crc;
    }
    uint32_t crc = running_crc;
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint32_t>(data[i]);
      for (uint8_t b = 0; b < 8U; ++b) {
        const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
        crc = (crc >> 1U) ^ (kCrc32Poly & mask);
      }
    }
    return crc;
  };

  uint32_t offset = 0U;
  uint32_t running_crc = 0xFFFFFFFFU;
  std::string msg;
  // CRC scan uses backend readAt(), which opens/seeks per call.
  // Keep a larger block size here to reduce SD transaction churn.
  if (ota_push_buf_.size() < 4096U) {
    ota_push_buf_.resize(4096U);
  }
  while (offset < size_bytes) {
    const size_t req = std::min<size_t>(ota_push_buf_.size(), static_cast<size_t>(size_bytes - offset));
    size_t out_len = 0U;
    if (!ota_push_storage_->readAt(local_path, offset, ota_push_buf_.data(), req, out_len, msg)) {
      if (out_error != nullptr) {
        *out_error = msg.empty() ? "readAt failed" : msg;
      }
      return false;
    }
    if (out_len == 0U) {
      if (out_error != nullptr) {
        *out_error = "short read (0 bytes)";
      }
      return false;
    }
    running_crc = crcUpdate(running_crc, ota_push_buf_.data(), out_len);
    offset += static_cast<uint32_t>(out_len);
  }

  out_crc = ~running_crc;
  if (out_error != nullptr) {
    out_error->clear();
  }
  return true;
}

bool MasterCli::startOtaPush(const std::string& local_path, uint16_t chunk_bytes) {
  if (ota_push_storage_ == nullptr) {
    io_.writeln("[MASTER][OTA] ota.push unavailable (no local OTA storage backend bound)");
    return false;
  }
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    io_.writeln("[MASTER][OTA] target not selected");
    return false;
  }
  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][OTA] ota.push unavailable (management path unavailable)");
    return false;
  }
  if (ota_push_active_) {
    io_.writeln("[MASTER][OTA] ota.push already active");
    return false;
  }
  if (chunk_bytes < 32U || chunk_bytes > 220U) {
    io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
    return false;
  }

  OtaStorageStat st{};
  std::string msg;
  if (!ota_push_storage_->stat(local_path, st, msg)) {
    writef("[MASTER][OTA] ota.push stat failed: %s", msg.c_str());
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    io_.writeln("[MASTER][OTA] ota.push invalid file path");
    return false;
  }

  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  uint32_t corr = 0U;
  const bool queued = submitRuntimeTargeted_(mgmt,
                                             static_cast<uint16_t>(ManagementCommandId::OtaPushStart),
                                             management_utils::buildOtaPushStartPayload(local_path, chunk_bytes),
                                             &corr,
                                             0U,
                                             false,
                                             &target_peer);
  correlation_id_ = mgmt.nextReqId();
  if (!queued || corr == 0U) {
    io_.writeln("[MASTER][OTA] ota.push start request failed");
    return false;
  }

  ota_push_active_ = true;
  ota_push_has_target_peer_ = true;
  ota_push_target_peer_ = target_peer;
  ota_push_path_ = local_path;
  ota_push_chunk_bytes_ = chunk_bytes;
  ota_push_size_bytes_ = st.size_bytes;
  ota_push_crc32_ = 0U;
  ota_push_offset_ = 0U;
  ota_push_chunks_sent_ = 0U;
  ota_push_corr_id_ = corr;
  ota_push_phase_ = OtaPushPhase::WaitBeginStatus;
  ota_push_started_ms_ = nowMs();
  ota_push_last_activity_ms_ = ota_push_started_ms_;
  ota_push_last_status_req_ms_ = 0U;
  ota_push_send_fail_streak_ = 0U;
  ota_push_next_send_ms_ = 0U;
  ota_push_last_end_send_ms_ = 0U;
  ota_push_end_send_count_ = 0U;
  ota_push_remote_acked_offset_ = 0U;
  ota_push_last_nack_offset_ = 0U;
  ota_push_window_target_offset_ = 0U;
  ota_push_window_wait_started_ms_ = 0U;
  ota_push_window_retry_count_ = 0U;
  ota_push_window_size_chunks_ = ota_push_window_size_default_chunks_;
  ota_push_recovery_until_acked_offset_ = 0U;
  ota_push_last_nack_log_ms_ = 0U;
  ota_push_waiting_window_ack_ = false;
  ota_push_begin_ack_seen_ = false;
  ota_push_buf_.clear();
  ota_update_prepare_pending_ = false;
  ota_update_prepare_corr_id_ = 0U;
  ota_update_image_name_ = otaImageNameFromCorr(corr);

  writef("[MASTER][OTA] ota.push started path=%s size=%lu chunk=%u corr=%lu",
         ota_push_path_.c_str(),
         static_cast<unsigned long>(ota_push_size_bytes_),
         static_cast<unsigned int>(ota_push_chunk_bytes_),
         static_cast<unsigned long>(ota_push_corr_id_));
  io_.writeln("[MASTER][OTA] waiting for slave begin status (management scheduler)...");
  return true;
}

bool MasterCli::requestOtaPushStatus() {
  if (!ota_push_active_ || !ota_push_has_target_peer_) {
    return false;
  }
  if (management_transport_ == nullptr) {
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool sent = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaStatusGet),
                                           {},
                                           nullptr,
                                           0U,
                                           false,
                                           &ota_push_target_peer_,
                                           ota_push_corr_id_);
  correlation_id_ = mgmt.nextReqId();
  if (!sent) {
    return false;
  }
  ota_push_last_status_req_ms_ = nowMs();
  return true;
}

void MasterCli::handleOtaPushStatusResponse(const DescriptorResponse& d) {
  if (!ota_push_active_) {
    return;
  }
  ota_push_last_activity_ms_ = nowMs();

  const uint8_t state = d.ota_status.transfer_state;
  const uint16_t code = d.ota_status.status_code;

  if (ota_push_phase_ == OtaPushPhase::WaitBeginStatus) {
    if (code != static_cast<uint16_t>(OtaStatusCode::Ok)) {
      stopOtaPush(d.message.empty() ? "slave rejected begin" : d.message.c_str(), false);
      return;
    }
    if (state == static_cast<uint8_t>(OtaTransferState::Failed)) {
      stopOtaPush(d.message.empty() ? "slave entered failed state" : d.message.c_str(), false);
      return;
    }
    if (state == static_cast<uint8_t>(OtaTransferState::Receiving)) {
      ota_push_begin_ack_seen_ = true;
      ota_push_phase_ = OtaPushPhase::Streaming;
      io_.writeln("[MASTER][OTA] begin acknowledged; streaming chunks...");
    }
    return;
  }

  if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
    if (code != static_cast<uint16_t>(OtaStatusCode::Ok) ||
        state == static_cast<uint8_t>(OtaTransferState::Failed)) {
      stopOtaPush(d.message.empty() ? "slave finalize failed" : d.message.c_str(), false);
      return;
    }

    if (state == static_cast<uint8_t>(OtaTransferState::Ready) &&
        d.ota_status.received_size == ota_push_size_bytes_ &&
        d.ota_status.expected_size == ota_push_size_bytes_) {
      const std::string ready_name = otaFileNameFromPath(d.ota_status.image_path);
      if (!ready_name.empty()) {
        ota_update_image_name_ = ready_name;
      }
      stopOtaPush("complete", true);
    }
  }
}

void MasterCli::stopOtaPush(const char* reason, bool success) {
  const uint32_t elapsed_ms = nowMs() - ota_push_started_ms_;
  const uint32_t sent_bytes = ota_push_offset_;
  const uint16_t chunks = ota_push_chunks_sent_;
  ota_push_active_ = false;

  if (success) {
    writef("[MASTER][OTA] ota.push done bytes=%lu/%lu chunks=%u elapsed_ms=%lu path=%s",
           static_cast<unsigned long>(sent_bytes),
           static_cast<unsigned long>(ota_push_size_bytes_),
           static_cast<unsigned int>(chunks),
           static_cast<unsigned long>(elapsed_ms),
           ota_push_path_.c_str());
  } else {
    writef("[MASTER][OTA] ota.push failed: %s bytes=%lu/%lu chunks=%u elapsed_ms=%lu",
           reason != nullptr ? reason : "unknown",
           static_cast<unsigned long>(sent_bytes),
           static_cast<unsigned long>(ota_push_size_bytes_),
           static_cast<unsigned int>(chunks),
           static_cast<unsigned long>(elapsed_ms));
  }

  if (ota_update_pipeline_active_) {
    if (!success) {
      io_.writeln("[MASTER][OTA] update pipeline failed during push");
      ota_update_prepare_pending_ = false;
      ota_update_prepare_corr_id_ = 0U;
      ota_update_staged_path_.clear();
      ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
      ota_update_pipeline_active_ = false;
      ota_update_wait_boot_notice_ = false;
      ota_update_image_name_.clear();
    } else if (ota_update_image_name_.empty()) {
      io_.writeln("[MASTER][OTA] update pipeline failed: image name missing after push");
      ota_update_prepare_pending_ = false;
      ota_update_prepare_corr_id_ = 0U;
      ota_update_staged_path_.clear();
      ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
      ota_update_pipeline_active_ = false;
      ota_update_wait_boot_notice_ = false;
    } else {
      bool apply_ok = false;
      if (management_transport_ != nullptr) {
        ManagementController mgmt(*management_transport_);
        mgmt.setNextReqId(correlation_id_);
        const MacAddress* target_peer = ota_push_has_target_peer_ ? &ota_push_target_peer_ : nullptr;
        apply_ok = submitRuntimeTargeted_(mgmt,
                                          static_cast<uint16_t>(ManagementCommandId::OtaApply),
                                          management_utils::buildStringPayloadU16(ota_update_image_name_),
                                          nullptr,
                                          0U,
                                          target_peer == nullptr,
                                          target_peer);
        correlation_id_ = mgmt.nextReqId();
      }
      if (apply_ok) {
        writef("[MASTER][OTA] update pipeline apply requested target=%s", ota_update_image_name_.c_str());
        ota_update_wait_boot_notice_ = true;
      } else {
        io_.writeln("[MASTER][OTA] update pipeline failed: apply request failed");
        ota_update_prepare_pending_ = false;
        ota_update_prepare_corr_id_ = 0U;
        ota_update_staged_path_.clear();
        ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
        ota_update_pipeline_active_ = false;
        ota_update_wait_boot_notice_ = false;
        ota_update_image_name_.clear();
      }
    }
  }

  ota_push_path_.clear();
  ota_push_has_target_peer_ = false;
  ota_push_target_peer_ = {};
  ota_push_size_bytes_ = 0U;
  ota_push_crc32_ = 0U;
  ota_push_offset_ = 0U;
  ota_push_chunks_sent_ = 0U;
  ota_push_corr_id_ = 0U;
  ota_push_phase_ = OtaPushPhase::Idle;
  ota_push_started_ms_ = 0U;
  ota_push_last_activity_ms_ = 0U;
  ota_push_last_status_req_ms_ = 0U;
  ota_push_send_fail_streak_ = 0U;
  ota_push_next_send_ms_ = 0U;
  ota_push_last_end_send_ms_ = 0U;
  ota_push_end_send_count_ = 0U;
  ota_push_remote_acked_offset_ = 0U;
  ota_push_last_nack_offset_ = 0U;
  ota_push_window_target_offset_ = 0U;
  ota_push_window_wait_started_ms_ = 0U;
  ota_push_window_retry_count_ = 0U;
  ota_push_window_size_chunks_ = ota_push_window_size_default_chunks_;
  ota_push_recovery_until_acked_offset_ = 0U;
  ota_push_last_nack_log_ms_ = 0U;
  ota_push_waiting_window_ack_ = false;
  ota_push_begin_ack_seen_ = false;
}

void MasterCli::pumpOtaPush(uint32_t now_ms) {
  if (!ota_push_active_) {
    return;
  }
  if (!ota_push_has_target_peer_ || !manager_.hasPersistedPair(ota_push_target_peer_)) {
    stopOtaPush("link lost", false);
    return;
  }

  constexpr uint32_t kWaitBeginTimeoutMs = 12000U;
  constexpr uint32_t kWaitEndTimeoutMs = 60000U;
  constexpr uint32_t kStatusPollIntervalMs = 250U;

  if (ota_push_phase_ == OtaPushPhase::WaitBeginStatus) {
    if (static_cast<int32_t>(now_ms - ota_push_last_activity_ms_) >=
        static_cast<int32_t>(kWaitBeginTimeoutMs)) {
      stopOtaPush("begin status timeout", false);
      return;
    }
    if (ota_push_begin_ack_seen_) {
      ota_push_phase_ = OtaPushPhase::Streaming;
      io_.writeln("[MASTER][OTA] begin acknowledged by slave status; streaming chunks...");
      return;
    }
    if (static_cast<int32_t>(now_ms - ota_push_last_status_req_ms_) >= static_cast<int32_t>(kStatusPollIntervalMs)) {
      (void)requestOtaPushStatus();
    }
    return;
  }

  if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
    if (static_cast<int32_t>(now_ms - ota_push_last_activity_ms_) >=
        static_cast<int32_t>(kWaitEndTimeoutMs)) {
      stopOtaPush("finalize status timeout", false);
    }
    return;
  }
}

}  // namespace espnow_link
