#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/types.hpp"

namespace espnow_link {

/** @brief Management-plane wire version for request/response/event contracts. */
constexpr uint8_t kManagementProtocolVersion = 1;
/** @brief Topology slot count hard cap carried by management payload contracts (lateral links only). */
constexpr uint8_t kManagementTopologyMaxSlots = 13U;
/** @brief Topology group-seed count hard cap carried by management payload contracts. */
constexpr uint8_t kManagementTopologyMaxGroups = 12U;

/** @brief High-level management frame category. */
enum class ManagementMessageType : uint8_t {
  Request = 1,
  Response = 2,
  Event = 3,
};

/** @brief External origin of a management request. */
enum class ManagementSource : uint8_t {
  Unknown = 0,
  Cli = 1,
  Wifi = 2,
  Ble = 3,
  Custom = 4,
};

/** @brief Authorization level attached to each management request. */
enum class ManagementAccessLevel : uint8_t {
  Observer = 0,
  Operator = 1,
  Maintainer = 2,
  Owner = 3,
};

/** @brief Supported management commands handled by `ManagementService`. */
enum class ManagementCommandId : uint16_t {
  DiscoveryStart = 0x0001,
  StatusGet = 0x0002,
  PairRequest = 0x0003,
  UnpairRequest = 0x0004,
  DiscoveryStop = 0x0005,
  DiscoverySnapshotGet = 0x0006,
  RemovePeerRequest = 0x0007,
  PairedSnapshotGet = 0x0008,

  DescGet = 0x0010,
  CapsGet = 0x0011,
  SettingsGet = 0x0012,
  SettingGet = 0x0013,
  SettingSet = 0x0014,
  TelemSchemaGet = 0x0015,
  TelemPull = 0x0016,
  LiveGet = 0x0017,
  TimeGet = 0x0018,
  TimeSet = 0x0019,
  CapsPageGet = 0x001A,
  TelemSchemaPageGet = 0x001B,
  SettingsPageGet = 0x001C,
  PingGet = 0x001D,
  LiveMonitorEnable = 0x001E,
  LiveMonitorDisable = 0x001F,

  PushStart = 0x0020,
  PushUpdate = 0x0021,
  PushPause = 0x0022,
  PushResume = 0x0023,
  PushStop = 0x0024,
  PushGet = 0x0025,
  LiveMonitorStatusGet = 0x0026,
  TopologyStageSet = 0x0027,
  TopologyCommit = 0x0028,
  TopologyStatusGet = 0x0029,
  TopologySlotsGet = 0x002A,
  TopologyTriggerSend = 0x002B,

  RestartSlaveRequest = 0x0030,
  ResetSlaveRequest = 0x0031,
  RestartMasterRequest = 0x0032,
  ResetMasterRequest = 0x0033,
  AudioPingRequest = 0x0034,

  LogLocalStatusGet = 0x0040,
  LogLocalRead = 0x0041,
  LogLocalClear = 0x0042,
  LogLocalControlSet = 0x0043,
  LogRemoteStatusGet = 0x0044,
  LogRemoteRead = 0x0045,
  LogRemoteClear = 0x0046,
  LogRemoteControlSet = 0x0047,
  ChannelRuntimeGet = 0x0048,
  ChannelSyncAll = 0x0049,

  StorageInfoGet = 0x0050,
  StorageList = 0x0051,
  StorageStat = 0x0052,
  StorageFormat = 0x0053,

  OtaStatusGet = 0x0060,
  OtaManifestGet = 0x0061,
  OtaManifestPageGet = 0x0062,
  OtaManifestRebuild = 0x0063,
  OtaClearScope = 0x0064,
  OtaCapacityGet = 0x0065,
  OtaGateGet = 0x0066,
  OtaApply = 0x0067,
  OtaRollback = 0x0068,
  OtaTransferBegin = 0x0069,
  OtaTransferChunk = 0x006A,
  OtaTransferEnd = 0x006B,
  OtaTransferAbort = 0x006C,
  OtaPushStart = 0x006D,
  OtaPushAbort = 0x006E,
  OtaPushStatus = 0x006F,

  CommTestRun = 0x0070,
  CommTestStatus = 0x0071,
  CommTestReport = 0x0072,
  MetricsGet = 0x0073,
  MetricsReset = 0x0074,
  QueueGet = 0x0075,
  OtaUpdateStart = 0x0076,
  OtaArchiveList = 0x0077,
  OtaArchiveSaveRunning = 0x0078,
  OtaArchiveSaveStaged = 0x0079,
  OtaArchiveRestore = 0x007A,
  OtaArchiveDelete = 0x007B,
  OtaArchiveClear = 0x007C,
  OtaMasterUpdateStart = 0x007D,
  OtaArchiveVerify = 0x007E,
  CliControlSet = 0x007F,
  ChainLoopControlSet = 0x0080,
  NodeBundleGet = 0x0081,
};

/** @brief Execution status returned by management command handlers. */
enum class ManagementStatus : uint16_t {
  Ok = 0x0000,
  OkDeferred = 0x0001,

  UnsupportedCommand = 0x1000,
  BadPayload = 0x1001,
  NotPaired = 0x1002,
  QueueFull = 0x1003,
  BusyPairing = 0x1004,
  UnpairInProgress = 0x1005,
  SourceNotActiveMaster = 0x1006,
  DeniedByPolicy = 0x1007,
  Timeout = 0x1008,
  DeniedByRole = 0x1009,
  CapacityLimitReached = 0x100A,
  TopologyNotStaged = 0x100B,
  TopologyVersionStale = 0x100C,
  TopologyApplyFailed = 0x100D,
  BusyRadioTransition = 0x100E,
  InternalError = 0x10FF,
};

/** @brief Asynchronous event IDs emitted by `ManagementService`. */
enum class ManagementEventId : uint16_t {
  CmdRx = 0x0001,
  CmdDone = 0x0002,
  CmdFail = 0x0003,
  Timeout = 0x0004,
  QueueFull = 0x0005,
  StateChange = 0x0006,
  DiscoveryStarted = 0x0007,
  DiscoveryUpdate = 0x0008,
  DiscoveryStopped = 0x0009,
  DiscoveryFinished = 0x000A,
  DiscoverySnapshot = 0x000B,
  PairProgress = 0x000C,
  PairResult = 0x000D,
  UnpairResult = 0x000E,
  PeerRemoved = 0x000F,
  OtaTransferReady = 0x0010,
  OtaBootComplete = 0x0011,
  OtaTransferStatus = 0x0012,
  PeerLivenessTransition = 0x0013,
  TopologyStaged = 0x0014,
  TopologyCommitted = 0x0015,
  TopologyCommitFailed = 0x0016,
  TopologyTriggerReceived = 0x0017,
  TopologyTriggerRejected = 0x0018,
  TopologyTriggerAck = 0x0019,
};

/** @brief OTA transfer status kind encoded in `ManagementEventId::OtaTransferStatus` payload. */
enum class ManagementOtaTransferStatusKind : uint8_t {
  Unknown = 0x00,
  ChunkAck = 0x01,
  ChunkNack = 0x02,
  FinalizeOk = 0x03,
  FinalizeFail = 0x04,
  FinalizeAck = 0x05,
};

/**
 * @brief Inbound management command envelope.
 *
 * `cmd_id` uses `ManagementCommandId` values and payload format is command-specific.
 */
struct ManagementRequest {
  ManagementSource source = ManagementSource::Unknown;
  ManagementAccessLevel access_level = ManagementAccessLevel::Owner;
  uint16_t cmd_id = 0;
  uint32_t req_id = 0;
  uint32_t timeout_ms = 0;
  /**
   * Explicit peer target for peer-bound commands.
   * Master-side peer-bound commands require this field to be set.
   */
  bool has_target_peer = false;
  MacAddress target_peer{};
  std::vector<uint8_t> payload;
};

/**
 * @brief Outbound management response envelope.
 *
 * `req_id` mirrors the originating request for correlation.
 */
struct ManagementResponse {
  ManagementSource source = ManagementSource::Unknown;
  uint16_t cmd_id = 0;
  uint32_t req_id = 0;
  ManagementStatus status = ManagementStatus::Ok;
  std::vector<uint8_t> payload;
  /** True when request resolution selected a target peer for this response. */
  bool has_requested_peer = false;
  /** Peer requested by caller for peer-bound command. */
  MacAddress requested_peer{};
  /** True when command execution peer is known for this response. */
  bool has_executed_peer = false;
  /** Peer used for execution after optional activation/switch. */
  MacAddress executed_peer{};
  /** True when runtime peer activation was performed before command dispatch. */
  bool activation_performed = false;
  /** Activation latency in milliseconds (0 when unavailable/not measured). */
  uint16_t activation_latency_ms = 0;
};

/**
 * @brief Asynchronous management event envelope.
 *
 * Used for discovery updates, queue signals, and command lifecycle events.
 */
struct ManagementEvent {
  ManagementEventId event_id = ManagementEventId::StateChange;
  ManagementSource source = ManagementSource::Unknown;
  uint16_t cmd_id = 0;
  uint32_t req_id = 0;
  ManagementStatus status = ManagementStatus::Ok;
  std::vector<uint8_t> payload;
  /** True when request resolution selected a target peer for this event. */
  bool has_requested_peer = false;
  /** Peer requested by caller for peer-bound command. */
  MacAddress requested_peer{};
  /** True when command execution peer is known for this event. */
  bool has_executed_peer = false;
  /** Peer used for execution after optional activation/switch. */
  MacAddress executed_peer{};
  /** True when runtime peer activation was performed before command dispatch. */
  bool activation_performed = false;
  /** Activation latency in milliseconds (0 when unavailable/not measured). */
  uint16_t activation_latency_ms = 0;
};

/** @brief Standard peer + message payload used by pair/unpair/discovery status events. */
struct ManagementPeerMessagePayload {
  MacAddress peer{};
  std::string message{};
};

/** @brief Discovery update payload (`peer`, RSSI, optional display name, optional role code). */
struct ManagementDiscoveryUpdatePayload {
  MacAddress peer{};
  bool has_rssi = false;
  int16_t rssi = 0;
  bool has_last_seen_age_ms = false;
  uint32_t last_seen_age_ms = 0;
  std::string name{};
  /** Profile/role code (`kProfile* & 0xFF`) when present; 0 means unknown/unspecified sender. */
  uint8_t role_code = 0;
};

/** @brief One discovery snapshot entry with optional freshness metadata. */
struct ManagementDiscoveredPeerInfo {
  MacAddress peer{};
  bool has_rssi = false;
  int16_t rssi = 0;
  bool has_last_seen_age_ms = false;
  uint32_t last_seen_age_ms = 0;
};

/** @brief One paired-peer entry (`PairedSnapshotGet`) with optional role hint. */
struct ManagementPairedPeerInfo {
  MacAddress peer{};
  /** Profile/role code (`kProfile* & 0xFF`) when available, otherwise `0`. */
  uint8_t role_code = 0;
};

/** @brief Peer removal payload emitted by `RemovePeerRequest` response/event. */
struct ManagementPeerRemovedPayload {
  MacAddress peer{};
  bool removed = false;
};

/** @brief Mandatory event payload used by OTA-ready / OTA-boot-complete management events. */
struct ManagementMandatoryEventPayload {
  MacAddress peer{};
  uint16_t event_id = 0;
  uint8_t severity = 0;
  int32_t event_value = 0;
  uint32_t event_ts_s = 0;
};

/** @brief Decoded OTA transfer status payload from management events. */
struct ManagementOtaTransferStatusPayload {
  MacAddress peer{};
  ManagementOtaTransferStatusKind kind = ManagementOtaTransferStatusKind::Unknown;
  uint32_t offset = 0;
  uint16_t status_code = 0;
};

/** @brief Deferred `OtaPushStart` / `OtaUpdateStart` response payload. */
struct ManagementOtaPushStartPayload {
  uint32_t req_id = 0;
  uint32_t total_size = 0;
  uint32_t image_crc32 = 0;
  uint16_t chunk_bytes = 0;
  std::string local_path{};
};

/** @brief `OtaPushStatus` response payload (local management-owned push session). */
struct ManagementOtaPushStatusPayload {
  bool active = false;
  uint8_t phase = 0;
  uint16_t chunk_bytes = 0;
  uint32_t req_id = 0;
  uint32_t next_offset = 0;
  uint32_t total_size = 0;
  uint16_t chunks_sent = 0;
  std::string image_name{};
};

/** @brief `CmdDone/CmdFail` payload emitted for `OtaPushStart` lifecycle completion. */
struct ManagementOtaPushResultPayload {
  uint32_t req_id = 0;
  uint32_t next_offset = 0;
  uint32_t total_size = 0;
  uint16_t chunks_sent = 0;
  uint16_t ota_status_code = 0;
  std::string reason{};
};

/** @brief `CmdDone/CmdFail` payload emitted for `OtaUpdateStart` lifecycle completion. */
struct ManagementOtaUpdateResultPayload {
  uint32_t req_id = 0;
  uint8_t phase = 0;
  uint16_t ota_status_code = 0;
  std::string reason{};
};

/** @brief `LiveMonitorStatusGet` response payload. */
struct ManagementLiveMonitorStatusPayload {
  bool enabled = false;
  bool ignore_active = false;
  uint16_t ignore_reason_mask = 0;
  uint8_t tracked_paired_count = 0;
  uint8_t online_count = 0;
  uint8_t offline_count = 0;
  uint32_t next_probe_due_ms = 0;
  uint32_t last_transition_ms = 0;
};

/** @brief `PeerLivenessTransition` event payload. */
struct ManagementPeerLivenessTransitionPayload {
  uint8_t schema_version = 1;
  MacAddress peer{};
  uint8_t state = 0;   // 0=online, 1=offline
  uint8_t reason = 0;  // 1=passive_rx, 2=probe_success, 3=probe_timeout_threshold, 4=resume_recheck_timeout
  uint32_t timestamp_ms = 0;
};

/** @brief `TopologyTrigger*` management event payload. */
struct ManagementTopologyTriggerEventPayload {
  uint8_t schema_version = 1;
  MacAddress peer{};
  uint16_t seq = 0;
  uint8_t state = 0;  // 0=received,1=rejected,2=duplicate,3=ack
  uint8_t reason = 0;
  uint8_t result = 0;  // ack result (0 accepted,1 rejected)
  uint8_t src_role = 0;
  uint8_t src_vid = 0xFF;
  uint8_t dst_role = 0;
  uint8_t dst_vid = 0xFF;
  uint8_t direction = 0;
  uint16_t delay_ms = 0;
  uint16_t hold_ms = 0;
  uint16_t ack_seq = 0;
};

/** @brief One topology slot entry for management stage/inspect payloads. */
struct ManagementTopologySlotPayload {
  uint8_t slot_index = 0;  // 0..12
  bool enabled = false;
  MacAddress peer{};
  uint8_t peer_role = 0;
  uint8_t group_id = 0;
  int8_t relative_index = 0;  // -13..+13, non-zero when enabled
  uint8_t local_virtual_index = 0xFF;
  uint8_t peer_virtual_index = 0xFF;
  int8_t axis_order = 0;
  uint16_t delay_ms = 0;
  uint16_t hold_ms = 0;
};

/** @brief One topology group seed entry for deterministic lateral key derivation. */
struct ManagementTopologyGroupSeedPayload {
  uint8_t group_slot = 0;  // 0..11
  bool enabled = false;
  uint8_t group_id = 0;
  std::array<uint8_t, 32> seed{};
};

/** @brief Full topology snapshot payload used by `TopologyStageSet`. */
struct ManagementTopologySnapshotPayload {
  uint8_t schema_version = 1;
  uint32_t topology_version = 0;
  uint8_t index_neg = 0;
  uint8_t index_pos = 0;
  std::vector<ManagementTopologySlotPayload> slots{};
  std::vector<ManagementTopologyGroupSeedPayload> groups{};
};

/** @brief Runtime status payload returned by `TopologyStatusGet`. */
struct ManagementTopologyStatusPayload {
  uint8_t schema_version = 1;
  bool has_staged = false;
  bool has_committed = false;
  uint8_t staged_state = 0;     // 0=none,1=staged,2=committed
  uint8_t committed_state = 0;  // 0=none,1=staged,2=committed
  uint32_t staged_version = 0;
  uint32_t committed_version = 0;
  uint8_t staged_slot_count = 0;
  uint8_t committed_slot_count = 0;
  uint8_t staged_group_count = 0;
  uint8_t committed_group_count = 0;
  uint8_t index_neg = 0;
  uint8_t index_pos = 0;
  uint32_t staged_checksum = 0;
  uint32_t committed_checksum = 0;
};

/** @brief Payload for `TopologyTriggerSend` request. */
struct ManagementTopologyTriggerSendPayload {
  int8_t target_index = 0;         // -16..+16, non-zero
  uint8_t direction = 0;           // 1=forward, 2=reverse
  uint16_t delay_ms = 0;
  uint16_t hold_ms = 0;
  uint8_t source_virtual_index = 0xFF;
};

/** @brief Payload for `TopologyTriggerSend` response. */
struct ManagementTopologyTriggerSendResponsePayload {
  uint16_t seq = 0;
};

/** @brief `CmdDone/CmdFail` payload emitted for local `TopologyCommit` auto-deploy fan-out. */
struct ManagementTopologyDeploySummaryPayload {
  uint32_t queued_peers = 0;
  uint32_t failed_peers = 0;
};

/** @brief One runtime-persisted channel binding entry (peer + channel key + value). */
struct ManagementRuntimeChannelEntryPayload {
  MacAddress peer{};
  uint8_t channel = 0;
  std::string key{};
};

/** @brief `ChannelRuntimeGet` response payload. */
struct ManagementRuntimeChannelStatusPayload {
  uint8_t current_channel = 0;
  std::vector<ManagementRuntimeChannelEntryPayload> entries{};
};

/** @brief `ChannelSyncAll` deferred result payload (`CmdDone/CmdFail`). */
struct ManagementChannelSyncAllResultPayload {
  uint8_t channel = 0;
  uint8_t acked_peers = 0;
  uint8_t total_peers = 0;
};

/** @brief `ChainLoopControlSet` deferred result payload (`CmdDone/CmdFail`). */
struct ManagementChainLoopResultPayload {
  bool enabled = false;
  uint8_t acked_peers = 0;
  uint8_t total_peers = 0;
};

}  // namespace espnow_link
