#pragma once

#include <cstdint>

namespace espnow_link {

/**
 * @brief Generic poller interface used by node runtime loop wrappers.
 *
 * Typical implementation: CLI line input poller.
 */
class INodeRuntimePoller {
 public:
  virtual ~INodeRuntimePoller() = default;
  /** @brief Poll one runtime source (serial input, queue, etc.). */
  virtual void poll() = 0;
};

/**
 * @brief Generic per-tick hook interface for node runtime wrappers.
 *
 * Typical implementations:
 * - pre-tick restart flag checks
 * - post-tick trace polling
 * - deferred local action execution
 */
class INodeRuntimeHook {
 public:
  virtual ~INodeRuntimeHook() = default;
  /** @brief Called once per runtime tick. */
  virtual void onRuntimeTick(uint32_t now_ms) = 0;
};

}  // namespace espnow_link

