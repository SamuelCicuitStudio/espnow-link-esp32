/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Remote log pull lifecycle, chunk requests, and termination behavior.
 **************************************************************/
#include "../internal/cli_master_internal.hpp"
#include "../internal/cli_master_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

bool MasterCli::startRemoteLogPull(uint16_t chunk_size) {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    io_.writeln("[MASTER][LOGGER][REMOTE] target not selected");
    return false;
  }
  if (remote_log_pull_active_) {
    io_.writeln("[MASTER][LOGGER][REMOTE] pull already active");
    return false;
  }
  if (remote_log_store_ == nullptr) {
    io_.writeln("[MASTER][LOGGER][REMOTE] export store unavailable");
    return false;
  }
  if (chunk_size == 0U || chunk_size > 128U) {
    io_.writeln("[MASTER][LOGGER][REMOTE] invalid chunk_size (1..128)");
    return false;
  }
  if (!remote_log_store_->clear()) {
    io_.writeln("[MASTER][LOGGER][REMOTE] failed to clear export store");
    return false;
  }

  remote_log_pull_active_ = true;
  remote_log_pull_waiting_status_ = true;
  remote_log_pull_total_bytes_ = 0;
  remote_log_pull_next_offset_ = 0;
  remote_log_pull_chunk_size_ = chunk_size;
  remote_log_pull_chunks_ = 0;
  remote_log_pull_started_ms_ = nowMs();
  remote_log_pull_last_activity_ms_ = remote_log_pull_started_ms_;
  remote_log_pull_has_target_peer_ = true;
  remote_log_pull_target_peer_ = target_peer;

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    stopRemoteLogPull("management path unavailable", false);
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool sent = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LogRemoteStatusGet),
                                           {},
                                           nullptr,
                                           0U,
                                           false,
                                           &target_peer);
  correlation_id_ = mgmt.nextReqId();
  if (!sent) {
    stopRemoteLogPull("failed to request remote logger status", false);
    return false;
  }

  writef("[MASTER][LOGGER][REMOTE] pull started chunk=%u", static_cast<unsigned int>(chunk_size));
  return true;
}

bool MasterCli::requestNextRemoteLogChunk() {
  if (!remote_log_pull_active_ || !remote_log_pull_has_target_peer_) {
    return false;
  }
  if (management_transport_ == nullptr) {
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool sent = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LogRemoteRead),
                                           management_utils::buildLogReadPayload(remote_log_pull_next_offset_,
                                                                                  remote_log_pull_chunk_size_),
                                           nullptr,
                                           0U,
                                           false,
                                           &remote_log_pull_target_peer_);
  correlation_id_ = mgmt.nextReqId();
  return sent;
}

void MasterCli::stopRemoteLogPull(const char* reason, bool success) {
  const uint32_t elapsed_ms = nowMs() - remote_log_pull_started_ms_;
  const uint32_t bytes = remote_log_pull_next_offset_;
  const uint16_t chunks = remote_log_pull_chunks_;
  remote_log_pull_active_ = false;
  remote_log_pull_waiting_status_ = false;
  remote_log_pull_has_target_peer_ = false;
  remote_log_pull_target_peer_ = {};

  if (success) {
    writef("[MASTER][LOGGER][REMOTE] %s bytes=%lu chunks=%u elapsed_ms=%lu file=%s",
           reason != nullptr ? reason : "done",
           static_cast<unsigned long>(bytes),
           static_cast<unsigned int>(chunks),
           static_cast<unsigned long>(elapsed_ms),
           (remote_log_store_ != nullptr) ? remote_log_store_->config().log_path.c_str() : "n/a");
    io_.writeln("[MASTER][LOGGER][REMOTE] decode with: python tools/log_decode.py <exported_file>");
    return;
  }

  writef("[MASTER][LOGGER][REMOTE] pull failed: %s bytes=%lu chunks=%u elapsed_ms=%lu",
         reason != nullptr ? reason : "unknown",
         static_cast<unsigned long>(bytes),
         static_cast<unsigned int>(chunks),
         static_cast<unsigned long>(elapsed_ms));
}

}  // namespace espnow_link
