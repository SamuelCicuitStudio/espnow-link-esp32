#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/control_codec.hpp"
#include "espnow_link/descriptor.hpp"
#include "espnow_link/manager.hpp"
#include "espnow_link/management_types.hpp"

namespace espnow_link {

/** @brief Decoded pull response payload category. */
enum class PullResponseKind : uint8_t {
  Unknown = 0,
  Descriptor,
  ControlResult,
};

/** @brief Decoded control command result payload. */
struct ControlResultPayload {
  uint16_t command_id = 0;
  uint16_t result_code = 0;
};

/** @brief Unified decoded pull response object. */
struct PullResponseDecoded {
  PullResponseKind kind = PullResponseKind::Unknown;
  DescriptorResponse descriptor;
  ControlResultPayload control;
};

/**
 * @brief Helper client for master-side pull RPC calls.
 *
 * Wraps `EspNowManager` send/decode calls with typed descriptor/control helpers.
 */
class MasterPullClient {
 public:
  explicit MasterPullClient(EspNowManager& manager) : manager_(manager) {}

  /**
   * @brief Send a raw descriptor query.
   * @param to Target peer MAC.
   * @param query Query object to encode and send.
   * @param corr_id Correlation ID for response matching.
   * @return true if frame was sent.
   */
  bool sendDescriptorQuery(const MacAddress& to, const DescriptorQuery& query, uint32_t corr_id);

  /** @brief Request device descriptor from peer. */
  bool requestDevice(const MacAddress& to, uint32_t corr_id);
  /** @brief Request capabilities list from peer. */
  bool requestCapabilities(const MacAddress& to, uint32_t corr_id);
  /** @brief Request one capabilities page from peer. */
  bool requestCapabilitiesPage(const MacAddress& to, uint16_t cursor, uint8_t page_size, uint32_t corr_id);
  /** @brief Request telemetry schema from peer. */
  bool requestTelemetrySchema(const MacAddress& to, uint32_t corr_id);
  /** @brief Request one telemetry schema page from peer. */
  bool requestTelemetrySchemaPage(const MacAddress& to, uint16_t cursor, uint8_t page_size, uint32_t corr_id);
  /** @brief Request live telemetry snapshot from peer. */
  bool requestTelemetryPull(const MacAddress& to, uint32_t corr_id);
  /** @brief Request liveness status from peer. */
  bool requestLiveness(const MacAddress& to, uint32_t corr_id);
  /** @brief Request time status from peer. */
  bool requestTimeGet(const MacAddress& to, uint32_t corr_id);
  /** @brief Set peer epoch time. */
  bool requestTimeSet(const MacAddress& to, uint64_t epoch_s, uint32_t corr_id);
  /** @brief Request settings list from peer. */
  bool requestSettings(const MacAddress& to, uint32_t corr_id);
  /** @brief Request one settings page from peer. */
  bool requestSettingsPage(const MacAddress& to, uint16_t cursor, uint8_t page_size, uint32_t corr_id);
  /** @brief Request one setting by key from peer. */
  bool requestSettingGet(const MacAddress& to, const std::string& key, uint32_t corr_id);
  /** @brief Request one setting by numeric ID from peer. */
  bool requestSettingGetById(const MacAddress& to, uint16_t setting_id, uint32_t corr_id);
  /** @brief Set one peer setting by key. */
  bool requestSettingSet(const MacAddress& to,
                         const std::string& key,
                         const std::string& value,
                         uint32_t corr_id);
  /** @brief Set one peer setting by numeric ID. */
  bool requestSettingSetById(const MacAddress& to,
                             uint16_t setting_id,
                             const std::string& value,
                             uint32_t corr_id);
  /** @brief Request remote logger status snapshot. */
  bool requestLogStatus(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote logger binary chunk. */
  bool requestLogReadChunk(const MacAddress& to, uint32_t offset, uint16_t max_bytes, uint32_t corr_id);
  /** @brief Request remote logger clear operation. */
  bool requestLogClear(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote logger enable/disable. */
  bool requestLogSetEnabled(const MacAddress& to, bool enabled, uint32_t corr_id);
  /** @brief Request remote storage backend info snapshot. */
  bool requestStorageInfo(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote directory listing for path. */
  bool requestStorageList(const MacAddress& to, const std::string& path, uint32_t corr_id);
  /** @brief Request remote stat for one path. */
  bool requestStorageStat(const MacAddress& to, const std::string& path, uint32_t corr_id);
  /** @brief Request remote active storage format + library tree recreate. */
  bool requestStorageFormat(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote OTA status snapshot. */
  bool requestOtaStatus(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote OTA manifest list (non-paged). */
  bool requestOtaManifest(const MacAddress& to, uint32_t corr_id);
  /** @brief Request one OTA manifest page. */
  bool requestOtaManifestPage(const MacAddress& to, uint16_t cursor, uint8_t page_size, uint32_t corr_id);
  /** @brief Request remote OTA manifest rebuild operation. */
  bool requestOtaManifestRebuild(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote OTA clear scope operation (`in|img|man|all`). */
  bool requestOtaClearScope(const MacAddress& to, const std::string& scope, uint32_t corr_id);
  /** @brief Request remote OTA capacity info. */
  bool requestOtaCapacity(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote OTA gate info. */
  bool requestOtaGateInfo(const MacAddress& to, uint32_t corr_id);
  /** @brief Request remote OTA apply operation for target image id/name. */
  bool requestOtaApply(const MacAddress& to, const std::string& target, uint32_t corr_id);
  /** @brief Clear slave-side staged topology buffer before new stage session. */
  bool requestTopologyStageClear(const MacAddress& to, uint32_t corr_id);
  /** @brief Begin slave-side staged topology session header. */
  bool requestTopologyStageBegin(const MacAddress& to,
                                 uint8_t schema_version,
                                 uint32_t topology_version,
                                 uint8_t index_neg,
                                 uint8_t index_pos,
                                 uint32_t corr_id);
  /** @brief Set one slave-side staged topology slot record. */
  bool requestTopologyStageSlotSet(const MacAddress& to,
                                   const ManagementTopologySlotPayload& slot,
                                   uint32_t corr_id);
  /** @brief Set one slave-side staged topology group seed record. */
  bool requestTopologyStageGroupSet(const MacAddress& to,
                                    const ManagementTopologyGroupSeedPayload& group,
                                    uint32_t corr_id);
  /** @brief Finalize slave-side staged topology snapshot (run stage validation/persist). */
  bool requestTopologyStageFinalize(const MacAddress& to, uint32_t corr_id);
  /** @brief Commit slave-side staged topology into active runtime state. */
  bool requestTopologyCommit(const MacAddress& to, uint32_t corr_id);
  /** @brief Request compact slave topology status summary string. */
  bool requestTopologyStatus(const MacAddress& to, uint32_t corr_id);
  /** @brief Ask one slave to send one topology trigger by relative index. */
  bool requestTopologyTriggerSend(const MacAddress& to,
                                  int8_t target_index,
                                  uint8_t direction,
                                  uint16_t delay_ms,
                                  uint16_t hold_ms,
                                  uint8_t source_virtual_index,
                                  uint32_t corr_id);

  /** @brief Send firmware stream begin frame. */
  bool sendFirmwareBegin(const MacAddress& to,
                         uint32_t total_size,
                         uint32_t chunk_size,
                         uint32_t image_crc32,
                         uint32_t corr_id,
                         const FirmwareImageMetadata* metadata = nullptr);
  /** @brief Send one firmware stream chunk frame. */
  bool sendFirmwareChunk(const MacAddress& to,
                         uint32_t offset,
                         const uint8_t* data,
                         size_t len,
                         uint32_t corr_id);
  /** @brief Send firmware stream end frame. */
  bool sendFirmwareEnd(const MacAddress& to,
                       uint32_t total_size,
                       uint32_t image_crc32,
                       uint32_t corr_id);

  /**
   * @brief Send compact control command payload to peer.
   * @param to Target peer MAC.
   * @param cmd_id Control command ID.
   * @param corr_id Correlation ID for result tracking.
   * @return true if frame was sent.
   */
  bool sendControlCommand(const MacAddress& to, uint16_t cmd_id, uint32_t corr_id);

  /**
   * @brief Decode pull response using manager's current active codec.
   * @param payload Response payload bytes.
   * @param len Payload length in bytes.
   * @param out Decoded response object.
   * @return true on successful decode.
   */
  bool decodePullResponseWithActiveCodec(const uint8_t* payload, size_t len, PullResponseDecoded& out) const;

 private:
  bool requestSimple(const MacAddress& to,
                     DescriptorQueryType type,
                     uint32_t corr_id);

  EspNowManager& manager_;
};

}  // namespace espnow_link
