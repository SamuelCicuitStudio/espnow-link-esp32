#include "espnow_link/management_runtime.hpp"

#include <algorithm>
#include <utility>

namespace espnow_link {

bool ManagementRuntime::addTransport(IManagementTransport* transport) {
  if (transport == nullptr) return false;
  if (std::find(transports_.begin(), transports_.end(), transport) != transports_.end()) {
    return false;
  }
  transports_.push_back(transport);
  return true;
}

bool ManagementRuntime::removeTransport(IManagementTransport* transport) {
  auto it = std::find(transports_.begin(), transports_.end(), transport);
  if (it == transports_.end()) return false;
  transports_.erase(it);
  return true;
}

void ManagementRuntime::clearTransports() { transports_.clear(); }

void ManagementRuntime::tick(uint32_t now_ms,
                             size_t max_requests_per_transport,
                             size_t max_responses,
                             size_t max_events) {
  const size_t transport_count = transports_.size();
  if (transport_count > 0U && max_requests_per_transport > 0U) {
    request_rr_cursor_ %= transport_count;
    for (size_t round = 0; round < max_requests_per_transport; ++round) {
      bool any_polled = false;
      for (size_t step = 0; step < transport_count; ++step) {
        const size_t idx = (request_rr_cursor_ + step) % transport_count;
        IManagementTransport* transport = transports_[idx];
        if (transport == nullptr) continue;

        ManagementRequest request{};
        if (!transport->pollRequest(request)) continue;
        any_polled = true;

        // Source is transport-owned to prevent client-side spoofing.
        request.source = transport->source();
        request.access_level = transport->accessLevel();

        if (service_.submit(std::move(request))) {
          ++stats_.submitted_requests;
        } else {
          ++stats_.dropped_requests;
          ++stats_.dropped_requests_service_rejected;
        }
      }
      if (!any_polled) {
        break;
      }
    }
    request_rr_cursor_ = (request_rr_cursor_ + 1U) % transport_count;
  }

  service_.tick(now_ms);

  for (size_t i = 0; i < max_responses; ++i) {
    ManagementResponse response{};
    if (!service_.pollResponse(response)) break;
    const DispatchResult dispatch = dispatchResponse_(response);
    if (dispatch.delivered == 0U) {
      ++stats_.dropped_responses;
    }
    if (dispatch.targeted == 0U) {
      ++stats_.dropped_responses_no_route;
    }
    stats_.dropped_responses_transport_rejected += static_cast<uint32_t>(dispatch.rejected);
    stats_.dispatched_responses += static_cast<uint32_t>(dispatch.delivered);
  }

  for (size_t i = 0; i < max_events; ++i) {
    ManagementEvent event{};
    if (!service_.pollEvent(event)) break;
    const DispatchResult dispatch = dispatchEvent_(event);
    if (dispatch.delivered == 0U) {
      ++stats_.dropped_events;
    }
    if (dispatch.targeted == 0U) {
      ++stats_.dropped_events_no_route;
    }
    stats_.dropped_events_transport_rejected += static_cast<uint32_t>(dispatch.rejected);
    stats_.dispatched_events += static_cast<uint32_t>(dispatch.delivered);
  }
}

ManagementRuntime::DispatchResult ManagementRuntime::dispatchResponse_(const ManagementResponse& response) {
  DispatchResult result{};
  for (IManagementTransport* transport : transports_) {
    if (transport == nullptr) continue;
    if (transport->source() != response.source) continue;
    ++result.targeted;
    if (transport->sendResponse(response)) {
      ++result.delivered;
    } else {
      ++result.rejected;
    }
  }
  return result;
}

ManagementRuntime::DispatchResult ManagementRuntime::dispatchEvent_(const ManagementEvent& event) {
  DispatchResult result{};
  const bool broadcast = (event.source == ManagementSource::Unknown);
  for (IManagementTransport* transport : transports_) {
    if (transport == nullptr) continue;
    if (!broadcast && transport->source() != event.source) continue;
    ++result.targeted;
    if (transport->sendEvent(event)) {
      ++result.delivered;
    } else {
      ++result.rejected;
    }
  }
  return result;
}

}  // namespace espnow_link
