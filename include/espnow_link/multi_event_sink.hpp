#pragma once

#include <algorithm>
#include <vector>

#include "espnow_link/events.hpp"

namespace espnow_link {

/**
 * @brief Fan-out event sink that forwards each event to multiple sinks.
 */
class MultiEventSink : public IEventSink {
 public:
  /**
   * @brief Add sink if not already present.
   * @param sink Sink pointer to add.
   */
  void addSink(IEventSink* sink) {
    if (sink == nullptr) return;
    if (std::find(sinks_.begin(), sinks_.end(), sink) != sinks_.end()) return;
    sinks_.push_back(sink);
  }

  /**
   * @brief Remove sink if present.
   * @param sink Sink pointer to remove.
   */
  void removeSink(IEventSink* sink) {
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
  }

  /** @brief Remove all registered sinks. */
  void clearSinks() { sinks_.clear(); }

  /** @brief Forward event to all registered sinks. */
  void onEvent(const Event& event) override {
    for (IEventSink* sink : sinks_) {
      if (sink != nullptr) {
        sink->onEvent(event);
      }
    }
  }

 private:
  std::vector<IEventSink*> sinks_{};
};

}  // namespace espnow_link
