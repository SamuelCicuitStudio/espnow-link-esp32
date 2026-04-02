/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Lifecycle start/restore flow, time sync metadata, and RX entry handling.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
bool EspNowManager::begin(const MacAddress& local_mac) {
  if (local_profile_ == nullptr || local_profile_id_ == kProfileUnknown) {
    return false;
  }

  uint8_t start_channel = config_.channel;
  if (hooks_ != nullptr) {
    uint8_t hook_channel = 0;
    if (hooks_->getBootChannel(hook_channel) && hook_channel >= 1 && hook_channel <= 14) {
      start_channel = hook_channel;
    }
  }

  if (!transport_.begin(start_channel, config_.enable_encryption)) {
    emitRuntimeLog(LibraryLogLevel::Error, kLogEvtBeginFail, 0, static_cast<int32_t>(start_channel), 0);
    return false;
  }

  if (hooks_ != nullptr) {
    uint8_t committed_channel = transport_.getChannel();
    if (committed_channel == 0) {
      committed_channel = start_channel;
    }
    hooks_->onChannelCommitted(committed_channel);
  }

  local_mac_ = local_mac;
  pairing_.setLocalMac(local_mac);
  (void)restoreLoggerConfig();
  restoreTopologySnapshots_();
  peer_registry_.clear();
  paired_cache_valid_ = false;
  paired_cache_state_ = false;
  paired_cache_peer_ = MacAddress{};
  setTopologyRestoreStatus_(kTopologyRestoreModeNone, "unset");
  topology_peer_ready_fail_count_ = 0U;
  topology_peer_ready_last_reason_.fill('\0');
  topology_peer_ready_fail_by_reason_.clear();
  emitRuntimeLog(LibraryLogLevel::Info, kLogEvtBeginOk, 0, static_cast<int32_t>(start_channel), 0);
  return true;
}

bool EspNowManager::restore() {
  if (store_ == nullptr || !store_->enabled()) {
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 1, 0);
    return false;
  }

  struct RestoreCandidate {
    PairRecord record{};
    uint32_t pair_seq = 0;
  };

  std::vector<PairIndexEntry> index_entries;
  if (!store_->loadPairIndex(local_mac_, index_entries) || index_entries.empty()) {
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 2, 0);
    return false;
  }

  bool index_mutated = false;
  std::vector<RestoreCandidate> candidates;
  candidates.reserve(index_entries.size());
  for (const auto& entry : index_entries) {
    if (!entry.valid || entry.pair_seq == 0U) {
      index_mutated = true;
      continue;
    }

    PairRecord record{};
    if (!store_->loadPair(local_mac_, entry.peer_mac, record) || !record.valid) {
      (void)store_->erasePair(local_mac_, entry.peer_mac);
      (void)store_->eraseChannel(local_mac_, entry.peer_mac);
      index_mutated = true;
      continue;
    }

    RestoreCandidate candidate{};
    candidate.record = record;
    candidate.pair_seq = entry.pair_seq;
    candidates.push_back(candidate);
  }

  std::sort(candidates.begin(), candidates.end(), [](const RestoreCandidate& lhs, const RestoreCandidate& rhs) {
    if (lhs.pair_seq != rhs.pair_seq) {
      return lhs.pair_seq < rhs.pair_seq;
    }
    return compareMac(lhs.record.peer_mac, rhs.record.peer_mac) < 0;
  });

  std::vector<RestoreCandidate> ordered_unique;
  ordered_unique.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    const auto duplicate =
        std::find_if(ordered_unique.begin(),
                     ordered_unique.end(),
                     [&](const RestoreCandidate& existing) {
                       return existing.record.peer_mac == candidate.record.peer_mac;
                     });
    if (duplicate != ordered_unique.end()) {
      index_mutated = true;
      continue;
    }
    ordered_unique.push_back(candidate);
  }

  const size_t keep_count = std::min(ordered_unique.size(), kRestorePairLimit);
  const size_t dropped_count = ordered_unique.size() - keep_count;
  if (dropped_count > 0U) {
    for (size_t i = 0; i < dropped_count; ++i) {
      const auto& dropped = ordered_unique[keep_count + i];
      (void)store_->erasePair(local_mac_, dropped.record.peer_mac);
      (void)store_->eraseChannel(local_mac_, dropped.record.peer_mac);

      const std::vector<uint8_t> ext =
          makeRestoreTrimDropExt(dropped.record.peer_mac,
                                 static_cast<uint16_t>(kRestorePairLimit),
                                 static_cast<uint16_t>(i + 1U));
      emitRuntimeLog(LibraryLogLevel::Warn,
                     kLogEvtRestoreTrimDrop,
                     0,
                     static_cast<int32_t>(kRestorePairLimit),
                     static_cast<int32_t>(i + 1U),
                     ext.data(),
                     ext.size());
    }

    const std::vector<uint8_t> summary_ext =
        makeRestoreTrimSummaryExt(static_cast<uint16_t>(kRestorePairLimit),
                                  static_cast<uint16_t>(dropped_count));
    emitRuntimeLog(LibraryLogLevel::Warn,
                   kLogEvtRestoreTrimSummary,
                   0,
                   static_cast<int32_t>(kRestorePairLimit),
                   static_cast<int32_t>(dropped_count),
                   summary_ext.data(),
                   summary_ext.size());
    index_mutated = true;
  }

  if (keep_count == 0U) {
    if (index_mutated) {
      const std::vector<PairIndexEntry> empty_index{};
      (void)store_->savePairIndex(local_mac_, empty_index);
    }
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 3, 0);
    return false;
  }

  std::vector<PairIndexEntry> canonical_index;
  canonical_index.reserve(keep_count);
  for (size_t i = 0; i < keep_count; ++i) {
    PairIndexEntry normalized{};
    normalized.peer_mac = ordered_unique[i].record.peer_mac;
    normalized.pair_seq = ordered_unique[i].pair_seq;
    normalized.valid = true;
    canonical_index.push_back(normalized);
  }

  std::vector<PairIndexEntry> restored_index;
  restored_index.reserve(canonical_index.size());
  size_t restored_count = 0U;
  for (size_t i = 0; i < keep_count; ++i) {
    const auto& candidate = ordered_unique[i];
    if (restorePairedLink(candidate.record)) {
      restored_index.push_back(canonical_index[i]);
      ++restored_count;
      continue;
    }
    (void)store_->erasePair(local_mac_, candidate.record.peer_mac);
    (void)store_->eraseChannel(local_mac_, candidate.record.peer_mac);
    index_mutated = true;
  }

  if (restored_count == 0U) {
    if (index_mutated || !canonical_index.empty()) {
      const std::vector<PairIndexEntry> empty_index{};
      (void)store_->savePairIndex(local_mac_, empty_index);
    }
    emitRuntimeLog(LibraryLogLevel::Warn, kLogEvtRestoreFail, 0, 4, 0);
    return false;
  }

  if (index_mutated || restored_index.size() != canonical_index.size()) {
    (void)store_->savePairIndex(local_mac_, restored_index);
  }

  emitRuntimeLog(LibraryLogLevel::Info,
                 kLogEvtRestoreOk,
                 0,
                 static_cast<int32_t>(restored_count),
                 0);
  return true;
}

void EspNowManager::end() { transport_.end(); }

bool EspNowManager::shouldAttachTimeSync(uint32_t now_ms) const {
  if (!config_.time_sync_enabled || config_.local_role != Role::Master || time_source_ == nullptr) {
    return false;
  }

  if (!has_time_sync_tx_) {
    return true;
  }

  if (config_.time_sync_interval_ms == 0) {
    return true;
  }

  return (now_ms - last_time_sync_tx_ms_) >= config_.time_sync_interval_ms;
}

bool EspNowManager::appendTimeSyncMetadata(const uint8_t* payload,
                                           size_t len,
                                           uint8_t& inout_flags,
                                           std::vector<uint8_t>& out_payload) {
  out_payload.clear();

  if (!shouldAttachTimeSync(current_now_ms_)) {
    return true;
  }

  if (len > (ProtocolCodec::kMaxPayload - kTimeSyncMetadataSize)) {
    return true;
  }

  uint64_t epoch_s = 0;
  if (!time_source_->nowEpochSec(epoch_s)) {
    return true;
  }

  out_payload.resize(kTimeSyncMetadataSize + len);
  const uint32_t epoch_s32 = static_cast<uint32_t>(epoch_s & 0xFFFFFFFFULL);
  out_payload[0] = static_cast<uint8_t>(epoch_s32 & 0xFFU);
  out_payload[1] = static_cast<uint8_t>((epoch_s32 >> 8) & 0xFFU);
  out_payload[2] = static_cast<uint8_t>((epoch_s32 >> 16) & 0xFFU);
  out_payload[3] = static_cast<uint8_t>((epoch_s32 >> 24) & 0xFFU);
  if (len > 0 && payload != nullptr) {
    for (size_t i = 0; i < len; ++i) {
      out_payload[kTimeSyncMetadataSize + i] = payload[i];
    }
  }

  inout_flags |= kFrameFlagTimeSyncEpoch;
  has_time_sync_tx_ = true;
  last_time_sync_tx_ms_ = current_now_ms_;
  return true;
}

bool EspNowManager::applyTimeSyncFromFrame(const FrameHeader& header, const uint8_t* payload, size_t len) {
  if ((header.flags & kFrameFlagTimeSyncEpoch) == 0) {
    return true;
  }

  if (payload == nullptr || len < kTimeSyncMetadataSize) {
    return false;
  }

  if (!config_.time_sync_enabled || !config_.time_sync_apply_on_slave || config_.local_role != Role::Slave ||
      header.role != Role::Master || time_sink_ == nullptr) {
    return true;
  }

  const uint64_t epoch_s = static_cast<uint64_t>(readLe32(payload));
  if (epoch_s < config_.time_sync_min_valid_epoch_s) {
    return true;
  }

  if (last_time_sync_rx_epoch_s_ != 0) {
    const uint64_t delta = (epoch_s > last_time_sync_rx_epoch_s_) ? (epoch_s - last_time_sync_rx_epoch_s_)
                                                                   : (last_time_sync_rx_epoch_s_ - epoch_s);
    if (delta < static_cast<uint64_t>(config_.time_sync_min_update_delta_s)) {
      return true;
    }
  }

  if (time_sink_->setEpochSec(epoch_s)) {
    last_time_sync_rx_epoch_s_ = epoch_s;
  }

  return true;
}

bool EspNowManager::onRx(const MacAddress& from, const uint8_t* data, size_t len, int rssi) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ScopedMetricsDuration rx_duration(&metrics_.rx_handler_total_us,
                                    &metrics_.rx_handler_last_us,
                                    &metrics_.rx_handler_max_us);
  ++metrics_.rx_frames;
  metrics_.rx_bytes += static_cast<uint64_t>(len);
#endif
  FrameHeader h;
  const uint8_t* payload = nullptr;
  size_t payload_len = 0;
  if (!ProtocolCodec::decode(data, len, h, payload, payload_len)) {
    if (events_ != nullptr) {
      events_->onEvent({Event::Type::PacketDropped, from, 0, "decode failed"});
    }
    if (hooks_ != nullptr) {
      hooks_->onRxFrame(from, MessageType::Error, 0, len, rssi);
    }
    emitRuntimeLog(LibraryLogLevel::Warn,
                   kLogEvtRxDecodeFail,
                   0,
                   static_cast<int32_t>(len),
                   rssi,
                   from.data(),
                   from.size());
    return false;
  }

  if (h.type == MessageType::Discovery &&
      config_.local_role == Role::Master &&
      !discoveryRxEnabled()) {
    // Hard drop discovery packets when discovery window is inactive.
    return true;
  }

  const bool suppress_rx_hook = isOtaWireMessage(h.type);
  if (hooks_ != nullptr && !suppress_rx_hook) {
    hooks_->onRxFrame(from, h.type, h.correlation_id, len, rssi);
  }

  if (h.version != kProtocolVersion) {
    if (events_ != nullptr) {
      events_->onEvent({Event::Type::PacketDropped, from, h.correlation_id, "version mismatch"});
    }
    emitRuntimeLog(LibraryLogLevel::Warn,
                   kLogEvtRxVersionDrop,
                   h.correlation_id,
                   static_cast<int32_t>(h.version),
                   static_cast<int32_t>(kProtocolVersion),
                   from.data(),
                   from.size());
    return false;
  }

  if (!applyTimeSyncFromFrame(h, payload, payload_len)) {
    return false;
  }

  const uint8_t* effective_payload = payload;
  size_t effective_payload_len = payload_len;
  if ((h.flags & kFrameFlagTimeSyncEpoch) != 0) {
    effective_payload += kTimeSyncMetadataSize;
    effective_payload_len -= kTimeSyncMetadataSize;
  }

  return dispatchRxByType(from, h, effective_payload, effective_payload_len, rssi);
}

}  // namespace espnow_link

