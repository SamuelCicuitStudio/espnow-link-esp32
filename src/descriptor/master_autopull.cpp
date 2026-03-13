#include "espnow_link/master_autopull.hpp"

namespace espnow_link {

void MasterAutoPull::resetState() {
  slave_online_ = false;
  liveness_waiting_ = false;
  last_liveness_rx_ms_ = 0;
  last_liveness_req_ms_ = 0;
}

void MasterAutoPull::setEnabled(bool enabled, uint32_t now_ms, uint32_t interval_ms) {
  enabled_ = enabled;
  if (interval_ms >= 300U) {
    interval_ms_ = interval_ms;
  }
  next_live_ms_ = now_ms;
  next_telem_ms_ = now_ms + (interval_ms_ / 2U);
  liveness_waiting_ = false;
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
  liveness_waiting_ = false;
  out_recovered = (!was_online && slave_online_);
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

  if (static_cast<int32_t>(now_ms - next_live_ms_) >= 0) {
    if (pull->requestLiveness(peer, inout_corr_id++)) {
      markLivenessRequested(now_ms);
      next_live_ms_ = now_ms + interval_ms_;
      out.sent_liveness = true;
    } else {
      next_live_ms_ = now_ms + 500U;
    }
  }

  if (static_cast<int32_t>(now_ms - next_telem_ms_) >= 0) {
    (void)pull->requestTelemetryPull(peer, inout_corr_id++);
    next_telem_ms_ = now_ms + interval_ms_;
    out.sent_telemetry = true;
  }

  if (liveness_waiting_ && (now_ms - last_liveness_req_ms_) > livenessTimeoutMs()) {
    liveness_waiting_ = false;
    if (slave_online_) {
      slave_online_ = false;
      out.offline_timeout = true;
    }
  }

  if (last_liveness_rx_ms_ != 0 &&
      (now_ms - last_liveness_rx_ms_) > (livenessTimeoutMs() + interval_ms_)) {
    if (slave_online_) {
      slave_online_ = false;
      out.offline_stale = true;
    }
  }

  return out;
}

}  // namespace espnow_link
