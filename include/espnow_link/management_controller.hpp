#pragma once

#include <cstddef>
#include <vector>

#include "espnow_link/management_queue_transport.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/telemetry_push.hpp"
#include "espnow_link/types.hpp"
#include "espnow_link/firmware.hpp"

namespace espnow_link {

/**
 * @brief Typed request submitter for management commands.
 *
 * This class wraps queue transport request submission with stable typed helpers so
 * frontends (Wi-Fi, BLE, custom, CLI) can issue commands without text parsing.
 */
class ManagementController {
 public:
  /** @brief Construct unbound controller. */
  ManagementController() = default;

  /**
   * @brief Construct controller bound to queue transport endpoint.
   *
   * Use this for frontend adapters (WiFi/BLE/custom/CLI) that enqueue into
   * `ManagementRuntime` instead of calling `ManagementService` directly.
   *
   * @param transport Queue transport endpoint owned by frontend/runtime.
   */
  explicit ManagementController(ManagementQueueTransport& transport)
      : queue_transport_(&transport),
        source_(transport.source()),
        access_level_(transport.accessLevel()) {}

  /**
   * @brief Bind controller to queue transport backend.
   * @param transport Queue transport endpoint.
   */
  void bind(ManagementQueueTransport& transport) {
    queue_transport_ = &transport;
    source_ = transport.source();
    access_level_ = transport.accessLevel();
  }

  /** @brief Clear all backends (controller becomes unbound). */
  void clearBindings() {
    queue_transport_ = nullptr;
  }

  /** @brief True when queue transport backend is bound. */
  bool ready() const { return queue_transport_ != nullptr; }

  /** @brief Set controller source identity written into submitted requests. */
  void setSource(ManagementSource source) { source_ = source; }

  /** @brief Get current controller source identity. */
  ManagementSource source() const { return source_; }

  /** @brief Set controller access level written into submitted requests. */
  void setAccessLevel(ManagementAccessLevel access_level) { access_level_ = access_level; }

  /** @brief Get current controller access level. */
  ManagementAccessLevel accessLevel() const { return access_level_; }

  /**
   * @brief Set default request timeout (ms) used when per-call timeout is zero.
   * @param timeout_ms Timeout in milliseconds.
   */
  void setDefaultTimeoutMs(uint32_t timeout_ms) { default_timeout_ms_ = timeout_ms; }

  /** @brief Get default request timeout in milliseconds. */
  uint32_t defaultTimeoutMs() const { return default_timeout_ms_; }

  /**
   * @brief Set next auto-generated request id.
   * @param next_id Next id returned for req_id=0 submissions.
   */
  void setNextReqId(uint32_t next_id) { next_req_id_ = next_id; }

  /** @brief Read next auto-generated request id. */
  uint32_t nextReqId() const { return next_req_id_; }

  /**
   * @brief Per-call submit options for deterministic request construction.
   *
   * This is additive and does not change management command semantics.
   */
  struct SubmitOptions {
    /** Explicit request id (`0` means auto-generated). */
    uint32_t req_id = 0;
    /** Per-call timeout override (`0` means controller default). */
    uint32_t timeout_ms = 0;
    /**
     * Optional future-facing priority field.
     *
     * Current management request contract has no priority wire field, so this is
     * currently ignored by `ManagementController`.
     */
    uint8_t priority = 0;
    /** True to set per-call target peer override. */
    bool has_target_peer = false;
    /** Per-call target peer used when `has_target_peer == true`. */
    MacAddress target_peer{};
  };

  /** @brief Rich submit result for frontend/CLI orchestration paths. */
  struct SubmitResult {
    /** True when request was accepted by queue backend. */
    bool accepted = false;
    /** Submitted command id. */
    uint16_t cmd_id = 0;
    /** Request id used for submission (non-zero when accepted). */
    uint32_t req_id = 0;
    /** Submission-stage status projection (`Ok` when accepted). */
    ManagementStatus status = ManagementStatus::InternalError;
    /** Reject stage hint (`bind|queue|validation`). */
    const char* reject_stage = "bind";
  };

  /**
   * @brief Submit a raw management request.
   *
   * This is the canonical submission API. It uses per-call options and returns
   * a rich acceptance/reject result.
   */
  SubmitResult submit(uint16_t cmd_id,
                      std::vector<uint8_t> payload,
                      const SubmitOptions& options);
  SubmitResult submit(uint16_t cmd_id,
                      const std::vector<uint8_t>& payload = {}) {
    SubmitOptions options{};
    return submit(cmd_id, payload, options);
  }

  bool discoveryStart(uint32_t window_ms, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool discoveryStop(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool discoverySnapshotGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pairedSnapshotGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool statusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool topologyStageSet(const ManagementTopologySnapshotPayload& snapshot,
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0);
  bool topologyCommit(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool topologyStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool topologySlotsGet(bool committed = true,
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0);
  bool topologyTriggerSend(const ManagementTopologyTriggerSendPayload& trigger,
                           uint32_t* out_req_id = nullptr,
                           uint32_t timeout_ms = 0);

  bool pairRequest(const MacAddress& peer, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool unpairRequest(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool removePeerRequest(const MacAddress& peer, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);

  bool descGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool capsGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool capsPageGet(uint16_t cursor,
                   uint8_t page_size,
                   uint32_t* out_req_id = nullptr,
                   uint32_t timeout_ms = 0);
  bool nodeBundleGet(uint8_t bundle_mask = 0x1FU,
                     uint32_t* out_req_id = nullptr,
                     uint32_t timeout_ms = 0);
  bool settingsGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool settingsPageGet(uint16_t cursor,
                       uint8_t page_size,
                       uint32_t* out_req_id = nullptr,
                       uint32_t timeout_ms = 0);
  bool settingGetByKey(const std::string& key, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool settingGetById(uint16_t setting_id, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool settingSetByKey(const std::string& key,
                       const std::string& value,
                       uint32_t* out_req_id = nullptr,
                       uint32_t timeout_ms = 0);
  bool settingSetById(uint16_t setting_id,
                      const std::string& value,
                      uint32_t* out_req_id = nullptr,
                      uint32_t timeout_ms = 0);
  bool telemetrySchemaGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool telemetrySchemaPageGet(uint16_t cursor,
                              uint8_t page_size,
                              uint32_t* out_req_id = nullptr,
                              uint32_t timeout_ms = 0);
  bool telemetryPull(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool telemetryPullPageGet(uint16_t cursor,
                            uint8_t page_size,
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0);
  bool livenessGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool liveMonitorEnable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool liveMonitorDisable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool liveMonitorStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pingGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool timeGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool timeSet(uint64_t epoch_s, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);

  bool pushCommand(const TelemetryPushCommand& cmd, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pushStart(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pushUpdate(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pushPause(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pushResume(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pushStop(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool pushGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);

  bool restartSlaveRequest(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool resetSlaveRequest(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool audioPingRequest(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool restartMasterRequest(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool resetMasterRequest(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool cliSetEnabled(bool enabled, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool cliEnable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return cliSetEnabled(true, out_req_id, timeout_ms);
  }
  bool cliDisable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return cliSetEnabled(false, out_req_id, timeout_ms);
  }
  bool cliStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool chainLoopSetEnabled(bool enabled, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool chainLoopEnable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return chainLoopSetEnabled(true, out_req_id, timeout_ms);
  }
  bool chainLoopDisable(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0) {
    return chainLoopSetEnabled(false, out_req_id, timeout_ms);
  }
  bool chainLoopStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);

  bool logLocalStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool logLocalRead(uint32_t offset,
                    uint16_t max_bytes = 96,
                    uint32_t* out_req_id = nullptr,
                    uint32_t timeout_ms = 0);
  bool logLocalClear(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool logLocalSetEnabled(bool enabled, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool logRemoteStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool logRemoteRead(uint32_t offset,
                     uint16_t max_bytes = 96,
                     uint32_t* out_req_id = nullptr,
                     uint32_t timeout_ms = 0);
  bool logRemoteClear(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool logRemoteSetEnabled(bool enabled, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool channelRuntimeGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool channelSyncAll(uint8_t channel, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);

  bool storageInfoGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool storageList(const std::string& path, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool storageStat(const std::string& path, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool storageFormat(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);

  bool otaStatusGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaManifestGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaManifestPageGet(uint16_t cursor,
                          uint8_t page_size,
                          uint32_t* out_req_id = nullptr,
                          uint32_t timeout_ms = 0);
  bool otaManifestRebuild(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaClearScope(const std::string& scope, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaCapacityGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaGateGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaApply(const std::string& target, uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaRollback(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaTransferBegin(uint32_t total_size,
                        uint32_t chunk_size,
                        uint32_t image_crc32,
                        const FirmwareImageMetadata* metadata = nullptr,
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0);
  bool otaTransferChunk(uint32_t offset,
                        const uint8_t* data,
                        size_t len,
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0);
  bool otaTransferChunk(uint32_t offset,
                        const std::vector<uint8_t>& data,
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0);
  bool otaTransferEnd(uint32_t total_size,
                      uint32_t image_crc32,
                      uint32_t* out_req_id = nullptr,
                      uint32_t timeout_ms = 0);
  bool otaTransferAbort(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaPushStart(const std::string& local_path,
                    uint16_t chunk_bytes = 220,
                    uint32_t* out_req_id = nullptr,
                    uint32_t timeout_ms = 0);
  bool otaPushAbort(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaPushStatus(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool otaUpdateStart(const std::string& local_path,
                      uint16_t chunk_bytes = 180,
                      uint32_t* out_req_id = nullptr,
                      uint32_t timeout_ms = 0);
  bool otaArchiveList(char role = 'm',
                      uint32_t* out_req_id = nullptr,
                      uint32_t timeout_ms = 0,
                      bool remote = false);
  bool otaArchiveSaveRunning(char role = 'm',
                             uint32_t* out_req_id = nullptr,
                             uint32_t timeout_ms = 0,
                             bool remote = false);
  bool otaArchiveSaveStaged(char role = 'm',
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0,
                            bool remote = false);
  bool otaArchiveRestore(const std::string& id,
                         char role = 'm',
                         uint32_t* out_req_id = nullptr,
                         uint32_t timeout_ms = 0,
                         bool remote = false);
  bool otaArchiveDelete(const std::string& id,
                        char role = 'm',
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0,
                        bool remote = false);
  bool otaArchiveClear(char role = 'm',
                       uint32_t* out_req_id = nullptr,
                       uint32_t timeout_ms = 0,
                       bool remote = false);
  bool otaArchiveVerify(const std::string& id,
                        char role = 'm',
                        uint32_t* out_req_id = nullptr,
                        uint32_t timeout_ms = 0,
                        bool remote = false);
  bool otaUpdateMasterStart(const std::string& local_path,
                            uint32_t* out_req_id = nullptr,
                            uint32_t timeout_ms = 0);

  bool commTestRun(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool commTestStatus(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool commTestReport(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool metricsGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool metricsReset(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);
  bool queueGet(uint32_t* out_req_id = nullptr, uint32_t timeout_ms = 0);

 private:
  bool submitCommand_(ManagementCommandId cmd,
                      std::vector<uint8_t> payload,
                      uint32_t* out_req_id,
                      uint32_t timeout_ms);

  ManagementQueueTransport* queue_transport_ = nullptr;
  ManagementSource source_ = ManagementSource::Unknown;
  ManagementAccessLevel access_level_ = ManagementAccessLevel::Owner;
  uint32_t default_timeout_ms_ = 0;
  uint32_t next_req_id_ = 1;
};

}  // namespace espnow_link
