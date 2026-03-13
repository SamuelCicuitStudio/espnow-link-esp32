#pragma once

#include <cstddef>
#include <vector>

#include "espnow_link/management_service.hpp"
#include "espnow_link/management_transport.hpp"

namespace espnow_link {

/**
 * @brief Runtime router that bridges one `ManagementService` with one or more transports.
 *
 * The runtime polls inbound requests from registered transports, submits them to the
 * shared service, then dispatches service responses/events back to transport endpoints.
 */
class ManagementRuntime {
 public:
  /**
   * @brief Construct runtime bound to a management service.
   * @param service Service instance used for command execution.
   */
  explicit ManagementRuntime(ManagementService& service) : service_(service) {}

  /**
   * @brief Add a transport endpoint to the runtime.
   * @param transport Non-null transport instance.
   * @return true when transport was added, false when null or already present.
   */
  bool addTransport(IManagementTransport* transport);

  /**
   * @brief Remove one transport endpoint.
   * @param transport Transport instance to remove.
   * @return true when transport was removed.
   */
  bool removeTransport(IManagementTransport* transport);

  /** @brief Remove all registered transports. */
  void clearTransports();

  /** @brief Number of currently registered transports. */
  size_t transportCount() const { return transports_.size(); }

  /**
   * @brief Pump request intake, service execution, and response/event dispatch.
   *
   * @param now_ms Current monotonic milliseconds used for service tick.
   * @param max_requests_per_transport Maximum requests pulled from each transport in one tick.
   * @param max_responses Maximum responses drained from service in one tick.
   * @param max_events Maximum events drained from service in one tick.
   */
  void tick(uint32_t now_ms,
            size_t max_requests_per_transport = 4,
            size_t max_responses = 32,
            size_t max_events = 64);

  /** @brief Runtime delivery counters for diagnostics. */
  struct Stats {
    uint32_t submitted_requests = 0;
    uint32_t dropped_requests = 0;
    uint32_t dropped_requests_service_rejected = 0;
    uint32_t dispatched_responses = 0;
    uint32_t dropped_responses = 0;
    uint32_t dropped_responses_no_route = 0;
    uint32_t dropped_responses_transport_rejected = 0;
    uint32_t dispatched_events = 0;
    uint32_t dropped_events = 0;
    uint32_t dropped_events_no_route = 0;
    uint32_t dropped_events_transport_rejected = 0;
  };

  /** @brief Read current runtime counters. */
  const Stats& stats() const { return stats_; }

  /** @brief Reset runtime counters to zero. */
  void resetStats() { stats_ = Stats{}; }

 private:
  struct DispatchResult {
    size_t targeted = 0;
    size_t delivered = 0;
    size_t rejected = 0;
  };

  DispatchResult dispatchResponse_(const ManagementResponse& response);
  DispatchResult dispatchEvent_(const ManagementEvent& event);

  ManagementService& service_;
  std::vector<IManagementTransport*> transports_;
  Stats stats_{};
  size_t request_rr_cursor_ = 0;
};

}  // namespace espnow_link
