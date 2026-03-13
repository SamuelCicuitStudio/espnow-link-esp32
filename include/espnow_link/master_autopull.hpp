#pragma once

#include <cstdint>

#include "espnow_link/master_pull_client.hpp"

namespace espnow_link {

/** @brief Result flags returned by one auto-pull scheduler tick. */
struct MasterAutoPullTickResult {
  bool sent_liveness = false;
  bool sent_telemetry = false;
  bool offline_timeout = false;
  bool offline_stale = false;
};

/**
 * @brief Lightweight scheduler for periodic master liveness/telemetry pull.
 */
class MasterAutoPull {
 public:
  /** @brief Reset internal scheduler and liveness state. */
  void resetState();

  /**
   * @brief Enable/disable scheduler.
   * @param enabled True to enable periodic pull.
   * @param now_ms Current time in milliseconds.
   * @param interval_ms Optional pull interval override.
   */
  void setEnabled(bool enabled, uint32_t now_ms, uint32_t interval_ms = 0);
  /** @brief Check if scheduler is enabled. */
  bool isEnabled() const { return enabled_; }

  /** @brief Current pull interval in milliseconds. */
  uint32_t intervalMs() const { return interval_ms_; }
  /** @brief Last known slave online state. */
  bool slaveOnline() const { return slave_online_; }

  /** @brief True after at least one liveness response was received. */
  bool hasLivenessRx() const { return last_liveness_rx_ms_ != 0; }
  /**
   * @brief Age of last liveness response.
   * @param now_ms Current time.
   * @return Milliseconds since last liveness response.
   */
  uint32_t livenessAgeMs(uint32_t now_ms) const {
    return (last_liveness_rx_ms_ == 0) ? 0 : (now_ms - last_liveness_rx_ms_);
  }

  /** @brief Mark that a liveness request has just been sent. */
  void markLivenessRequested(uint32_t now_ms);
  /**
   * @brief Update scheduler state from liveness response.
   * @param online Reported online flag.
   * @param now_ms Current time.
   * @param out_recovered Set true when transitioning offline->online.
   */
  void onLivenessResponse(bool online, uint32_t now_ms, bool& out_recovered);

  /**
   * @brief Run one scheduler cycle and optionally send pull requests.
   * @param pull Pull client used for requests.
   * @param peer Target peer MAC.
   * @param can_poll True when caller allows outbound polling this cycle.
   * @param now_ms Current time.
   * @param inout_corr_id Correlation ID counter (incremented when used).
   * @return Tick result flags.
   */
  MasterAutoPullTickResult tick(MasterPullClient* pull,
                                const MacAddress& peer,
                                bool can_poll,
                                uint32_t now_ms,
                                uint32_t& inout_corr_id);

 private:
  uint32_t livenessTimeoutMs() const;

  bool enabled_ = false;
  bool slave_online_ = false;
  bool liveness_waiting_ = false;

  uint32_t interval_ms_ = 2000;
  uint32_t last_liveness_rx_ms_ = 0;
  uint32_t last_liveness_req_ms_ = 0;
  uint32_t next_live_ms_ = 0;
  uint32_t next_telem_ms_ = 0;
};

}  // namespace espnow_link
