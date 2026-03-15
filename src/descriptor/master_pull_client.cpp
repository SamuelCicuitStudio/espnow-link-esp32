#include "espnow_link/master_pull_client.hpp"

namespace espnow_link {

bool MasterPullClient::sendDescriptorQuery(const MacAddress& to,
                                           const DescriptorQuery& query,
                                           uint32_t corr_id) {
  std::vector<uint8_t> payload;
  if (!manager_.encodeDescriptorQueryPayload(query, payload)) {
    return false;
  }
  return manager_.sendPullRequest(to, payload.data(), payload.size(), corr_id);
}

bool MasterPullClient::requestSimple(const MacAddress& to,
                                     DescriptorQueryType type,
                                     uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = type;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestDevice(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetDevice, corr_id);
}

bool MasterPullClient::requestCapabilities(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetCapabilities, corr_id);
}

bool MasterPullClient::requestCapabilitiesPage(const MacAddress& to,
                                               uint16_t cursor,
                                               uint8_t page_size,
                                               uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::GetCapabilities;
  q.paged = true;
  q.cursor = cursor;
  q.page_size = page_size;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestTelemetrySchema(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetTelemetry, corr_id);
}

bool MasterPullClient::requestTelemetrySchemaPage(const MacAddress& to,
                                                  uint16_t cursor,
                                                  uint8_t page_size,
                                                  uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::GetTelemetry;
  q.paged = true;
  q.cursor = cursor;
  q.page_size = page_size;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestTelemetryPull(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::PullTelemetry, corr_id);
}

bool MasterPullClient::requestTelemetryPullPage(const MacAddress& to,
                                                uint16_t cursor,
                                                uint8_t page_size,
                                                uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::PullTelemetry;
  q.paged = true;
  q.cursor = cursor;
  q.page_size = page_size;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestLiveness(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetLiveness, corr_id);
}

bool MasterPullClient::requestTimeGet(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetTime, corr_id);
}

bool MasterPullClient::requestTimeSet(const MacAddress& to,
                                      uint64_t epoch_s,
                                      uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::SetTime;
  q.time_epoch_s = epoch_s;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestSettings(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetSettings, corr_id);
}

bool MasterPullClient::requestSettingsPage(const MacAddress& to,
                                           uint16_t cursor,
                                           uint8_t page_size,
                                           uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::GetSettings;
  q.paged = true;
  q.cursor = cursor;
  q.page_size = page_size;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestSettingGet(const MacAddress& to,
                                         const std::string& key,
                                         uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::GetSetting;
  q.key = key;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestSettingGetById(const MacAddress& to,
                                             uint16_t setting_id,
                                             uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::GetSetting;
  q.has_setting_id = true;
  q.setting_id = setting_id;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestSettingSet(const MacAddress& to,
                                         const std::string& key,
                                         const std::string& value,
                                         uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::SetSetting;
  q.key = key;
  q.value = value;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestSettingSetById(const MacAddress& to,
                                             uint16_t setting_id,
                                             const std::string& value,
                                             uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::SetSetting;
  q.has_setting_id = true;
  q.setting_id = setting_id;
  q.value = value;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestLogStatus(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetLogStatus, corr_id);
}

bool MasterPullClient::requestLogReadChunk(const MacAddress& to,
                                           uint32_t offset,
                                           uint16_t max_bytes,
                                           uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::ReadLogChunk;
  q.log_offset = offset;
  q.log_max_bytes = (max_bytes == 0) ? 96U : max_bytes;
  if (q.log_max_bytes > 128U) {
    q.log_max_bytes = 128U;
  }
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestLogClear(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::ClearLog, corr_id);
}

bool MasterPullClient::requestLogSetEnabled(const MacAddress& to, bool enabled, uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::SetLogControl;
  q.has_log_enable = true;
  q.log_enable = enabled;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestStorageInfo(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetStorageInfo, corr_id);
}

bool MasterPullClient::requestStorageList(const MacAddress& to,
                                          const std::string& path,
                                          uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::ListStoragePath;
  q.storage_path = path;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestStorageStat(const MacAddress& to,
                                          const std::string& path,
                                          uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::StatStoragePath;
  q.storage_path = path;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestStorageFormat(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::FormatStorage, corr_id);
}

bool MasterPullClient::requestOtaStatus(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetOtaStatus, corr_id);
}

bool MasterPullClient::requestOtaManifest(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetOtaManifest, corr_id);
}

bool MasterPullClient::requestOtaManifestPage(const MacAddress& to,
                                              uint16_t cursor,
                                              uint8_t page_size,
                                              uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::GetOtaManifest;
  q.paged = true;
  q.cursor = cursor;
  q.page_size = page_size;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestOtaManifestRebuild(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::RebuildOtaManifest, corr_id);
}

bool MasterPullClient::requestOtaClearScope(const MacAddress& to,
                                            const std::string& scope,
                                            uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::ClearOtaScope;
  q.ota_scope = scope;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestOtaCapacity(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetOtaCapacity, corr_id);
}

bool MasterPullClient::requestOtaGateInfo(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::GetOtaGateInfo, corr_id);
}

bool MasterPullClient::requestOtaApply(const MacAddress& to,
                                       const std::string& target,
                                       uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::ApplyOtaImage;
  q.ota_target = target;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestTopologyStageClear(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::TopologyStageClear, corr_id);
}

bool MasterPullClient::requestTopologyStageBegin(const MacAddress& to,
                                                 uint8_t schema_version,
                                                 uint32_t topology_version,
                                                 uint8_t index_neg,
                                                 uint8_t index_pos,
                                                 uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::TopologyStageBegin;
  q.topology_schema_version = schema_version;
  q.topology_version = topology_version;
  q.topology_index_neg = index_neg;
  q.topology_index_pos = index_pos;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestTopologyStageSlotSet(const MacAddress& to,
                                                   const ManagementTopologySlotPayload& slot,
                                                   uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::TopologyStageSlotSet;
  q.topology_slot_index = slot.slot_index;
  q.topology_slot_enabled = slot.enabled;
  q.topology_peer = slot.peer;
  q.topology_peer_role = slot.peer_role;
  q.topology_group_id = slot.group_id;
  q.topology_relative_index = slot.relative_index;
  q.topology_local_virtual_index = slot.local_virtual_index;
  q.topology_peer_virtual_index = slot.peer_virtual_index;
  q.topology_axis_order = slot.axis_order;
  q.topology_delay_ms = slot.delay_ms;
  q.topology_hold_ms = slot.hold_ms;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestTopologyStageGroupSet(const MacAddress& to,
                                                    const ManagementTopologyGroupSeedPayload& group,
                                                    uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::TopologyStageGroupSet;
  q.topology_group_slot = group.group_slot;
  q.topology_group_enabled = group.enabled;
  q.topology_group_id = group.group_id;
  q.topology_group_seed = group.seed;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::requestTopologyStageFinalize(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::TopologyStageFinalize, corr_id);
}

bool MasterPullClient::requestTopologyCommit(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::TopologyCommit, corr_id);
}

bool MasterPullClient::requestTopologyStatus(const MacAddress& to, uint32_t corr_id) {
  return requestSimple(to, DescriptorQueryType::TopologyStatus, corr_id);
}

bool MasterPullClient::requestTopologyTriggerSend(const MacAddress& to,
                                                  int8_t target_index,
                                                  uint8_t direction,
                                                  uint16_t delay_ms,
                                                  uint16_t hold_ms,
                                                  uint8_t source_virtual_index,
                                                  uint32_t corr_id) {
  DescriptorQuery q{};
  q.type = DescriptorQueryType::TopologyTriggerSend;
  q.topology_target_index = target_index;
  q.topology_trigger_direction = direction;
  q.topology_delay_ms = delay_ms;
  q.topology_hold_ms = hold_ms;
  q.topology_source_virtual_index = source_virtual_index;
  return sendDescriptorQuery(to, q, corr_id);
}

bool MasterPullClient::sendFirmwareBegin(const MacAddress& to,
                                         uint32_t total_size,
                                         uint32_t chunk_size,
                                         uint32_t image_crc32,
                                         uint32_t corr_id,
                                         const FirmwareImageMetadata* metadata) {
  return manager_.sendFirmwareBegin(to, total_size, chunk_size, image_crc32, corr_id, metadata);
}

bool MasterPullClient::sendFirmwareChunk(const MacAddress& to,
                                         uint32_t offset,
                                         const uint8_t* data,
                                         size_t len,
                                         uint32_t corr_id) {
  return manager_.sendFirmwareChunk(to, offset, data, len, corr_id);
}

bool MasterPullClient::sendFirmwareEnd(const MacAddress& to,
                                       uint32_t total_size,
                                       uint32_t image_crc32,
                                       uint32_t corr_id) {
  return manager_.sendFirmwareEnd(to, total_size, image_crc32, corr_id);
}

bool MasterPullClient::sendControlCommand(const MacAddress& to,
                                          uint16_t cmd_id,
                                          uint32_t corr_id) {
  std::vector<uint8_t> payload;
  if (!manager_.encodeControlCommandPayload(cmd_id, payload)) {
    return false;
  }
  return manager_.sendPullRequest(to, payload.data(), payload.size(), corr_id);
}

bool MasterPullClient::decodePullResponseWithActiveCodec(const uint8_t* payload,
                                                         size_t len,
                                                         PullResponseDecoded& out) const {
  out = PullResponseDecoded{};

  DescriptorResponse d{};
  if (manager_.decodeDescriptorResponsePayload(payload, len, d)) {
    out.kind = PullResponseKind::Descriptor;
    out.descriptor = d;
    return true;
  }

  uint16_t cmd = 0;
  uint16_t result = 0;
  if (manager_.decodeControlResultPayload(payload, len, cmd, result)) {
    out.kind = PullResponseKind::ControlResult;
    out.control.command_id = cmd;
    out.control.result_code = result;
    return true;
  }

  out.kind = PullResponseKind::Unknown;
  return false;
}

}  // namespace espnow_link
