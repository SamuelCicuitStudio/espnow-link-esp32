#pragma once

#include <string>

#include "espnow_link/control.hpp"
#include "espnow_link/master_pull_client.hpp"

namespace espnow_link {

/** @brief Text sink used by pull response logger output. */
class IPullResponseLogSink {
 public:
  virtual ~IPullResponseLogSink() = default;
  /** @brief Write one formatted log line. */
  virtual void logLine(const std::string& line) = 0;
};

/**
 * @brief Control-plane implementation that decodes pull responses and logs them.
 */
class PullResponseLogger : public IControlPlane {
 public:
  /**
   * @brief Construct logger with output sink.
   * @param sink Log sink implementation.
   */
  explicit PullResponseLogger(IPullResponseLogSink& sink)
      : sink_(sink) {}

  /**
   * @brief Attach pull client used to decode payloads with active codec.
   * @param pull Pull client pointer.
   */
  void bindPullClient(MasterPullClient* pull) { pull_ = pull; }

  /** @brief Ignore pull requests (logger handles responses only). */
  bool onPullRequest(const MacAddress&, uint32_t, const uint8_t*, size_t) override { return true; }
  /** @brief Decode and print pull response payload. */
  bool onPullResponse(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len) override;

 private:
  MasterPullClient* pull_ = nullptr;
  IPullResponseLogSink& sink_;
};

}  // namespace espnow_link
