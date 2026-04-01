#include "espnow_link/master_autopull.hpp"

namespace espnow_link {

void MasterAutoPull::resetOfflineMissStreaks_() {
  offline_timeout_miss_streak_ = 0;
  offline_stale_miss_streak_ = 0;
}

void MasterAutoPull::resetState() {
  slave_online_ = false;
  liveness_waiting_ = false;
  last_liveness_rx_ms_ = 0;
  last_liveness_req_ms_ = 0;
  last_peer_activity_ms_ = 0;
  tx_fail_streak_ = 0;
  tx_backoff_until_ms_ = 0;
  resetOfflineMissStreaks_();
}

void MasterAutoPull::setEnabled(bool enabled, uint32_t now_ms, uint32_t interval_ms) {
  enabled_ = enabled;
  if (interval_ms >= 300U) {
    interval_ms_ = interval_ms;
  }
  next_live_ms_ = now_ms;
  next_telem_ms_ = now_ms + (interval_ms_ / 2U);
  liveness_waiting_ = false;
  last_peer_activity_ms_ = 0;
  resetOfflineMissStreaks_();
  tx_fail_streak_ = 0;
  tx_backoff_until_ms_ = 0;
}

uint32_t MasterAutoPull::livenessTimeoutMs() const {
  return (interval_ms_ * 2U) + 500U;
}

void MasterAutoPull::markLivenessRequested(uint32_t now_ms) {
  liveness_waiting_ = true;
  last_liveness_req_ms_ = now_ms;
}

void MasterAutoPull::onLivenessResponse(bool online, uint32_t now_ms, bool& out_recovered) {
  const bool was_online = slave_online_;
  slave_online_ = online;
  last_liveness_rx_ms_ = now_ms;
  last_peer_activity_ms_ = now_ms;
  liveness_waiting_ = false;
  resetOfflineMissStreaks_();
  noteTxSuccess_();
  out_recovered = (!was_online && slave_online_);
}

void MasterAutoPull::onPeerActivity(uint32_t now_ms, bool& out_recovered) {
  const bool was_online = slave_online_;
  slave_online_ = true;
  last_liveness_rx_ms_ = now_ms;
  last_peer_activity_ms_ = now_ms;
  liveness_waiting_ = false;
  resetOfflineMissStreaks_();
  noteTxSuccess_();
  out_recovered = !was_online;
}

void MasterAutoPull::noteTxSuccess_() {
  tx_fail_streak_ = 0;
  tx_backoff_until_ms_ = 0;
}

uint32_t MasterAutoPull::txBackoffMs_() const {
  const uint8_t n = (tx_fail_streak_ > 6U) ? 6U : tx_fail_streak_;
  const uint32_t base = 200U;
  const uint32_t backoff = (base << n);  // 200,400,800,...,12800
  return (backoff > 8000U) ? 8000U : backoff;
}

void MasterAutoPull::noteTxFailure_(uint32_t now_ms) {
  if (tx_fail_streak_ < 255U) {
    ++tx_fail_streak_;
  }
  tx_backoff_until_ms_ = now_ms + txBackoffMs_();
}

MasterAutoPullTickResult MasterAutoPull::tick(MasterPullClient* pull,
                                              const MacAddress& peer,
                                              bool can_poll,
                                              uint32_t now_ms,
                                              uint32_t& inout_corr_id) {
  MasterAutoPullTickResult out{};
  if (!enabled_ || !can_poll || pull == nullptr) {
    return out;
  }

  const bool backoff_active =
      (tx_backoff_until_ms_ != 0U) &&
      (static_cast<int32_t>(now_ms - tx_backoff_until_ms_) < 0);
  bool poll_attempted = false;

  if (!backoff_active && static_cast<int32_t>(now_ms - next_live_ms_) >= 0) {
    poll_attempted = true;
    if (pull->requestLiveness(peer, inout_corr_id++)) {
      markLivenessRequested(now_ms);
      next_live_ms_ = now_ms + interval_ms_;
      noteTxSuccess_();
      out.sent_liveness = true;
    } else {
      noteTxFailure_(now_ms);
      next_live_ms_ = now_ms + txBackoffMs_();
    }
  }

  if (!backoff_active && !poll_attempted &&
      static_cast<int32_t>(now_ms - next_telem_ms_) >= 0) {
    // When link is currently offline, skip telemetry pressure and let liveness
    // probes drive recovery. Telemetry resumes automatically once peer activity
    // flips slave_online_ back to true.
    if (!slave_online_) {
      next_telem_ms_ = now_ms + interval_ms_;
    } else if (pull->requestTelemetryPull(peer, inout_corr_id++)) {
      noteTxSuccess_();
      next_telem_ms_ = now_ms + interval_ms_;
      out.sent_telemetry = true;
    } else {
      noteTxFailure_(now_ms);
      next_telem_ms_ = now_ms + txBackoffMs_();
    }
  }

  if (liveness_waiting_ && (now_ms - last_liveness_req_ms_) > livenessTimeoutMs()) {
    liveness_waiting_ = false;
    if (offline_timeout_miss_streak_ < 255U) {
      ++offline_timeout_miss_streak_;
    }
    if (offline_timeout_miss_streak_ >= kOfflineTimeoutMissThreshold && slave_online_) {
      slave_online_ = false;
      out.offline_timeout = true;
    }
  }

  const uint32_t freshness_ms =
      (last_peer_activity_ms_ != 0U) ? last_peer_activity_ms_ : last_liveness_rx_ms_;
  if (freshness_ms != 0U &&
      (now_ms - freshness_ms) > (livenessTimeoutMs() + interval_ms_)) {
    if (offline_stale_miss_streak_ < 255U) {
      ++offline_stale_miss_streak_;
    }
    if (offline_stale_miss_streak_ >= kOfflineStaleMissThreshold && slave_online_) {
      slave_online_ = false;
      out.offline_stale = true;
    }
  } else {
    offline_stale_miss_streak_ = 0;
  }

  return out;
}

}  // namespace espnow_link
