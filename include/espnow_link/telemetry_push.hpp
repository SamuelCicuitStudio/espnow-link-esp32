#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/descriptor.hpp"

namespace espnow_link {

/** @brief Per-metric reporting strategy for telemetry push. */
enum class TelemetryPushMode : uint8_t {
  Periodic = 1,
  OnChange = 2,
  Hybrid = 3,
};

/** @brief Stream control operation requested by master. */
enum class TelemetryPushAction : uint8_t {
  Start = 1,
  Update = 2,
  Pause = 3,
  Resume = 4,
  Stop = 5,
  Get = 6,
};

/** @brief Per-metric stream configuration inside a push command. */
struct TelemetryPushMetricConfig {
  // Exactly one identity field must be set:
  // - key (readable path)
  // - metric_index (compact indexed path)
  std::string key;
  bool has_metric_index = false;
  uint16_t metric_index = 0;

  bool enabled = true;
  TelemetryPushMode mode = TelemetryPushMode::Hybrid;
  uint32_t interval_ms = 0;
  uint32_t min_report_gap_ms = 0;
  bool use_threshold = false;
  float delta_abs = 0.0f;
};

/** @brief Global stream configuration containing selected metric rules. */
struct TelemetryPushConfig {
  uint16_t stream_id = 1;
  bool enabled = true;
  TelemetryPushMode mode = TelemetryPushMode::Hybrid;
  uint32_t interval_ms = 1000;
  uint32_t min_report_gap_ms = 200;
  std::vector<TelemetryPushMetricConfig> metrics;
};

/** @brief Full telemetry push command envelope. */
struct TelemetryPushCommand {
  TelemetryPushAction action = TelemetryPushAction::Get;
  TelemetryPushConfig config;
};

/**
 * @brief Provider interface used by slave runtime for telemetry push.
 */
class ITelemetryPushProvider {
 public:
  virtual ~ITelemetryPushProvider() = default;

  /**
   * @brief Return latest telemetry sample set for push evaluation/reporting.
   * @param out Output sample list.
   * @return true when snapshot is available.
   */
  virtual bool getTelemetrySnapshot(std::vector<TelemetrySample>& out) = 0;

  /**
   * @brief Optional schema provider for key/index validation.
   * @param out Output schema list.
   * @return true when schema is available.
   */
  virtual bool getTelemetrySchema(std::vector<TelemetryDescriptor>& out) {
    (void)out;
    return false;
  }
};

/**
 * @brief Encode telemetry push command into wire payload.
 * @param cmd Input command.
 * @param out_payload Encoded payload bytes.
 * @return true on success.
 */
bool encodeTelemetryPushCommand(const TelemetryPushCommand& cmd, std::vector<uint8_t>& out_payload);

/**
 * @brief Decode telemetry push command from wire payload.
 * @param payload Input bytes.
 * @param len Input length in bytes.
 * @param out_cmd Output decoded command.
 * @return true on success.
 */
bool parseTelemetryPushCommand(const uint8_t* payload, size_t len, TelemetryPushCommand& out_cmd);

}  // namespace espnow_link


