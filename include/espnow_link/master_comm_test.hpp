#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "espnow_link/management_service.hpp"
#include "espnow_link/master_pull_client.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/telemetry_push.hpp"

namespace espnow_link {

/** @brief One executed communication test step result. */
struct MasterCommTestStepResult {
  std::string name;
  uint16_t cmd_id = 0;
  uint32_t req_id = 0;
  bool passed = false;
  ManagementStatus status = ManagementStatus::InternalError;
  std::string detail;
};

/** @brief Aggregate summary of a communication test run. */
struct MasterCommTestSummary {
  bool running = false;
  bool completed = false;
  bool passed = false;
  uint32_t started_ms = 0;
  uint32_t ended_ms = 0;
  uint16_t total = 0;
  uint16_t passed_count = 0;
  uint16_t failed_count = 0;
  std::string profile_name;
  uint32_t expected_settings_count = 0;
  uint32_t seen_settings_count = 0;
  uint32_t expected_telemetry_count = 0;
  uint32_t seen_telemetry_count = 0;
};

/**
 * @brief Master-side communication test runner for one-slave validation.
 *
 * The suite is management-driven and validates request/response communication paths.
 * It does not directly perform transport IO; it submits requests through `ManagementService`.
 *
 * Usage:
 * 1. call `start(now_ms)`
 * 2. call `tick(now_ms)` in loop
 * 3. feed polled management responses/events through `onResponse/onEvent`
 * 4. read `summary()` + `results()`
 */
class MasterCommTest {
 public:
  /** @brief Runtime options for one communication test execution. */
  struct Options {
    uint32_t step_timeout_ms = 5000;
    ManagementSource source = ManagementSource::Custom;
    bool include_settings_full = true;
    bool include_push_get = true;
  };

  /**
   * @brief Construct communication test runner.
   * @param mgmt Management service used for request submission.
   * @param pull Pull client used for descriptor decode validation.
   */
  MasterCommTest(ManagementService& mgmt, MasterPullClient& pull);

  /**
   * @brief Start a new test run.
   * @param now_ms Current monotonic time in milliseconds.
   * @param options Test run options.
   * @return true when run is accepted.
   */
  bool start(uint32_t now_ms);
  bool start(uint32_t now_ms, const Options& options);

  /** @brief Advance timeout/finalization logic. */
  void tick(uint32_t now_ms);

  /**
   * @brief Feed one management response to active test step.
   * @param response Polled management response.
   * @return true if response matched current active test step.
   */
  bool onResponse(const ManagementResponse& response);

  /**
   * @brief Feed one management event for diagnostics.
   * @param event Polled management event.
   */
  void onEvent(const ManagementEvent& event);

  /** @brief Reset runner state and drop queued test plan/results. */
  void reset();

  /** @brief Get latest run summary snapshot. */
  MasterCommTestSummary summary() const;

  /** @brief Get immutable list of step-level results. */
  const std::vector<MasterCommTestStepResult>& results() const { return results_; }

 private:
  struct Step {
    std::string name;
    ManagementCommandId cmd = ManagementCommandId::StatusGet;
    std::vector<uint8_t> payload;
    DescriptorResponseType expected_desc = DescriptorResponseType::Unknown;
    bool require_desc = false;
    bool parse_caps = false;
    bool parse_setting = false;
    bool parse_telem_schema = false;
    bool require_telem_snapshot_non_empty = false;
  };

  struct ActiveStep {
    Step step;
    uint32_t req_id = 0;
    uint32_t sent_ms = 0;
  };

  void buildBasePlan();
  void enqueueStep(const Step& step);
  bool dispatchNext(uint32_t now_ms);
  void finalizeIfDone(uint32_t now_ms);
  void markStepResult(const ActiveStep& active,
                      bool passed,
                      ManagementStatus status,
                      const std::string& detail);
  bool handleDescriptorValidation(const ActiveStep& active,
                                  const ManagementResponse& response,
                                  std::string& out_detail,
                                  bool& out_passed);
  void deriveProfileFromCapabilities(const DescriptorResponse& d);
  void enqueueSettingsSweepFromProfile();
  uint32_t nextReqId();

  ManagementService& mgmt_;
  MasterPullClient& pull_;

  Options options_{};
  bool running_ = false;
  bool completed_ = false;
  bool passed_ = true;
  uint32_t started_ms_ = 0;
  uint32_t ended_ms_ = 0;

  std::deque<Step> plan_{};
  ActiveStep active_{};
  bool active_valid_ = false;
  std::vector<MasterCommTestStepResult> results_{};
  std::vector<std::string> event_trace_{};
  uint32_t next_req_id_ = 30000;

  ProfileId remote_profile_id_ = kProfileUnknown;
  const IProfileDefinition* remote_profile_ = nullptr;
  bool settings_sweep_enqueued_ = false;
  uint32_t expected_settings_count_ = 0;
  uint32_t expected_telemetry_count_ = 0;
  uint32_t seen_telemetry_count_ = 0;
  std::unordered_set<uint16_t> seen_settings_{};
};

}  // namespace espnow_link
