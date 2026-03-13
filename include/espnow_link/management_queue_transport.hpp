#pragma once

#include <deque>

#include "espnow_link/management_transport.hpp"

namespace espnow_link {

/**
 * @brief In-memory queue transport for management request/response/event bridging.
 *
 * This concrete `IManagementTransport` is frontend-agnostic and can be used by
 * CLI/WiFi/BLE/custom adapters as a typed mailbox:
 * - frontend pushes requests with `enqueueRequest`
 * - `ManagementRuntime` pulls via `pollRequest`
 * - runtime pushes responses/events via `sendResponse`/`sendEvent`
 * - frontend consumes them via `pollResponse`/`pollEvent`
 */
class ManagementQueueTransport final : public IManagementTransport {
 public:
  /** @brief Queue overflow handling policy per channel. */
  enum class OverflowPolicy : uint8_t {
    RejectNew = 0,
    DropOldest = 1,
  };

  /** @brief Per-queue admission counters for diagnostics. */
  struct QueueStats {
    uint32_t enqueued = 0;
    uint32_t rejected_new = 0;
    uint32_t dropped_oldest = 0;
  };

  /** @brief Queue sizing and source identity config. */
  struct Config {
    ManagementSource source = ManagementSource::Unknown;
    ManagementAccessLevel access_level = ManagementAccessLevel::Owner;
    size_t max_requests = 32;
    size_t max_responses = 64;
    size_t max_events = 128;
    OverflowPolicy request_overflow_policy = OverflowPolicy::RejectNew;
    OverflowPolicy response_overflow_policy = OverflowPolicy::RejectNew;
    OverflowPolicy event_overflow_policy = OverflowPolicy::RejectNew;
  };

  /** @brief Construct transport with default queue config. */
  ManagementQueueTransport() = default;
  /** @brief Construct transport with config override. */
  explicit ManagementQueueTransport(const Config& cfg) : cfg_(cfg) {}

  /** @brief Update queue config for future enqueue operations. */
  void configure(const Config& cfg) { cfg_ = cfg; }
  /** @brief Update transport access level used by runtime request stamping. */
  void setAccessLevel(ManagementAccessLevel access_level) { cfg_.access_level = access_level; }

  ManagementSource source() const override { return cfg_.source; }
  ManagementAccessLevel accessLevel() const override { return cfg_.access_level; }

  /** @brief Frontend-side: enqueue one outbound request for runtime processing. */
  bool enqueueRequest(const ManagementRequest& request) {
    const QueueAdmissionResult admission =
        makeRoom_(requests_, cfg_.max_requests, cfg_.request_overflow_policy);
    if (admission == QueueAdmissionResult::Rejected) {
      ++request_stats_.rejected_new;
      return false;
    }
    if (admission == QueueAdmissionResult::DroppedOldest) {
      ++request_stats_.dropped_oldest;
    }
    requests_.push_back(request);
    ++request_stats_.enqueued;
    return true;
  }

  /** @brief Frontend-side: poll one response emitted by runtime. */
  bool pollResponse(ManagementResponse& out_response) {
    if (responses_.empty()) return false;
    out_response = responses_.front();
    responses_.pop_front();
    return true;
  }

  /** @brief Frontend-side: poll one event emitted by runtime. */
  bool pollEvent(ManagementEvent& out_event) {
    if (events_.empty()) return false;
    out_event = events_.front();
    events_.pop_front();
    return true;
  }

  /** @brief Clear all request/response/event queues. */
  void clear() {
    requests_.clear();
    responses_.clear();
    events_.clear();
  }

  /** @brief Number of queued frontend requests not yet consumed by runtime. */
  size_t pendingRequestCount() const { return requests_.size(); }
  /** @brief Number of queued responses awaiting frontend consumption. */
  size_t pendingResponseCount() const { return responses_.size(); }
  /** @brief Number of queued events awaiting frontend consumption. */
  size_t pendingEventCount() const { return events_.size(); }

  /** @brief Runtime-side: pop one request from frontend queue. */
  bool pollRequest(ManagementRequest& out_request) override {
    if (requests_.empty()) return false;
    out_request = requests_.front();
    requests_.pop_front();
    return true;
  }

  /** @brief Runtime-side: push one response to frontend queue. */
  bool sendResponse(const ManagementResponse& response) override {
    const QueueAdmissionResult admission =
        makeRoom_(responses_, cfg_.max_responses, cfg_.response_overflow_policy);
    if (admission == QueueAdmissionResult::Rejected) {
      ++response_stats_.rejected_new;
      return false;
    }
    if (admission == QueueAdmissionResult::DroppedOldest) {
      ++response_stats_.dropped_oldest;
    }
    responses_.push_back(response);
    ++response_stats_.enqueued;
    return true;
  }

  /** @brief Runtime-side: push one event to frontend queue. */
  bool sendEvent(const ManagementEvent& event) override {
    const QueueAdmissionResult admission =
        makeRoom_(events_, cfg_.max_events, cfg_.event_overflow_policy);
    if (admission == QueueAdmissionResult::Rejected) {
      ++event_stats_.rejected_new;
      return false;
    }
    if (admission == QueueAdmissionResult::DroppedOldest) {
      ++event_stats_.dropped_oldest;
    }
    events_.push_back(event);
    ++event_stats_.enqueued;
    return true;
  }

  /** @brief Read request queue admission counters. */
  const QueueStats& requestStats() const { return request_stats_; }
  /** @brief Read response queue admission counters. */
  const QueueStats& responseStats() const { return response_stats_; }
  /** @brief Read event queue admission counters. */
  const QueueStats& eventStats() const { return event_stats_; }
  /** @brief Reset all queue admission counters. */
  void resetStats() {
    request_stats_ = QueueStats{};
    response_stats_ = QueueStats{};
    event_stats_ = QueueStats{};
  }

 private:
  enum class QueueAdmissionResult : uint8_t {
    Admitted = 0,
    Rejected = 1,
    DroppedOldest = 2,
  };

  template <typename T>
  QueueAdmissionResult makeRoom_(std::deque<T>& q, size_t max_size, OverflowPolicy policy) {
    if (max_size == 0) return QueueAdmissionResult::Rejected;
    if (q.size() < max_size) return QueueAdmissionResult::Admitted;
    if (policy != OverflowPolicy::DropOldest) {
      return QueueAdmissionResult::Rejected;
    }
    q.pop_front();
    return QueueAdmissionResult::DroppedOldest;
  }

  Config cfg_{};
  std::deque<ManagementRequest> requests_{};
  std::deque<ManagementResponse> responses_{};
  std::deque<ManagementEvent> events_{};
  QueueStats request_stats_{};
  QueueStats response_stats_{};
  QueueStats event_stats_{};
};

}  // namespace espnow_link
