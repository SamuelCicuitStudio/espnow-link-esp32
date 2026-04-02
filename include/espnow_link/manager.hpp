#pragma once

#include <array>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "espnow_link/codec.hpp"
#include "espnow_link/config.hpp"
#include "espnow_link/control.hpp"
#include "espnow_link/events.hpp"
#include "espnow_link/firmware.hpp"
#include "espnow_link/hooks.hpp"
#include "espnow_link/library_logger.hpp"
#include "espnow_link/pairing.hpp"
#include "espnow_link/peer_window.hpp"
#include "espnow_link/peer_registry.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/persistence.hpp"
#include "espnow_link/protocol.hpp"
#include "espnow_link/telemetry_push.hpp"
#include "espnow_link/time.hpp"
#include "espnow_link/transport.hpp"

namespace espnow_link {

#ifndef ESPNOW_LINK_ENABLE_RUNTIME_METRICS
#define ESPNOW_LINK_ENABLE_RUNTIME_METRICS 1
#endif

struct ManagerRuntimeMetrics {
  uint64_t tick_count = 0;
  uint32_t tick_last_us = 0;
  uint32_t tick_max_us = 0;
  uint64_t tick_total_us = 0;

  uint64_t rx_frames = 0;
  uint64_t rx_bytes = 0;
  uint32_t rx_handler_last_us = 0;
  uint32_t rx_handler_max_us = 0;
  uint64_t rx_handler_total_us = 0;

  uint64_t tx_frames = 0;
  uint64_t tx_bytes = 0;
  uint64_t tx_failures = 0;
  uint32_t tx_send_last_us = 0;
  uint32_t tx_send_max_us = 0;
  uint64_t tx_send_total_us = 0;
};

/**
 * @brief Core runtime for ESP-NOW link lifecycle, pairing, pull/control messaging, and telemetry push.
 *
 * Use one instance per device role (master or slave). Call `begin()`, then `restore()`, and drive it with `tick()`.
 */
class EspNowManager {
 public:
  /**
   * @brief Construct the manager with transport, persistence, and optional behavior adapters.
   * @param config Static runtime configuration (role, channel, timing, security settings).
   * @param transport ESP-NOW transport implementation used for radio I/O.
   * @param store Pairing persistence backend used to restore/save links.
   * @param events Optional event sink for high-level state/report callbacks.
   * @param hooks Optional platform hooks for debug, RGB, and policy checks.
   * @param firmware_sink Optional sink used during firmware transfer frames.
   * @param control_plane Optional pull-plane handler for custom request/response behavior.
   * @param time_source Optional system time provider for outbound timestamps.
   * @param time_sink Optional time sink to apply inbound time synchronization.
   * @param telemetry_push_provider Optional telemetry provider used by slave push mode.
   */
  EspNowManager(const ManagerConfig& config,
                ITransport& transport,
                PairingStore* store,
                IEventSink* events = nullptr,
                IPlatformHooks* hooks = nullptr,
                IFirmwareStreamSink* firmware_sink = nullptr,
                IControlPlane* control_plane = nullptr,
                ITimeSource* time_source = nullptr,
                ITimeSink* time_sink = nullptr,
                ITelemetryPushProvider* telemetry_push_provider = nullptr,
                LibraryLogger* logger = nullptr);

  /**
   * @brief Initialize transport, pairing engine, and runtime state.
   * @param local_mac Local STA MAC used as node identity.
   * @return true if initialization succeeded.
   */
  bool begin(const MacAddress& local_mac);
  /** @brief Read local device MAC bound at `begin()`. */
  const MacAddress& localMac() const { return local_mac_; }

  /**
   * @brief Restore persisted link state (paired peer, channel, session metadata).
   * @return true if a valid persisted state was restored.
   */
  bool restore();

  /** @brief Stop runtime and release transport resources. */
  void end();

  /**
   * @brief Process one raw inbound ESP-NOW frame.
   * @param from Source MAC.
   * @param data Raw frame bytes.
   * @param len Frame length in bytes.
   * @param rssi Optional RSSI when available from the platform transport.
   * @return true if frame was parsed and accepted by current state.
   */
  bool onRx(const MacAddress& from, const uint8_t* data, size_t len, int rssi = 0);

  /**
   * @brief Send a pull request frame to a peer.
   * @param to Destination MAC.
   * @param payload Encoded pull payload bytes.
   * @param len Payload length in bytes.
   * @param corr_id Correlation ID used for request/response tracking.
   * @return true if queued to transport.
   */
  bool sendPullRequest(const MacAddress& to, const uint8_t* payload, size_t len, uint32_t corr_id);

  /**
   * @brief Send a pull response frame to a peer.
   * @param to Destination MAC.
   * @param payload Encoded response payload bytes.
   * @param len Payload length in bytes.
   * @param corr_id Correlation ID from the original request.
   * @return true if queued to transport.
   */
  bool sendPullResponse(const MacAddress& to, const uint8_t* payload, size_t len, uint32_t corr_id);

  /**
   * @brief Send firmware transfer begin frame.
   * @param to Destination MAC.
   * @param total_size Declared image size in bytes.
   * @param chunk_size Planned chunk size in bytes.
   * @param image_crc32 Declared image CRC32.
   * @param corr_id Correlation ID for stream session.
   * @param metadata Optional firmware metadata (version/build/target_role) sent with begin frame.
   * @return true if frame was queued to transport.
   */
  bool sendFirmwareBegin(const MacAddress& to,
                         uint32_t total_size,
                         uint32_t chunk_size,
                         uint32_t image_crc32,
                         uint32_t corr_id,
                         const FirmwareImageMetadata* metadata = nullptr);

  /**
   * @brief Send one firmware transfer chunk frame.
   * @param to Destination MAC.
   * @param offset Byte offset of chunk in firmware image.
   * @param data Chunk bytes.
   * @param len Chunk length in bytes.
   * @param corr_id Correlation ID for stream session.
   * @return true if frame was queued to transport.
   */
  bool sendFirmwareChunk(const MacAddress& to,
                         uint32_t offset,
                         const uint8_t* data,
                         size_t len,
                         uint32_t corr_id);

  /**
   * @brief Send firmware transfer end frame.
   * @param to Destination MAC.
   * @param total_size Final image size in bytes.
   * @param image_crc32 Final image CRC32.
   * @param corr_id Correlation ID for stream session.
   * @return true if frame was queued to transport.
   */
  bool sendFirmwareEnd(const MacAddress& to,
                       uint32_t total_size,
                       uint32_t image_crc32,
                       uint32_t corr_id);

  /**
   * @brief Acknowledge a slave finalize status frame for one OTA transfer.
   * @param to Destination slave MAC.
   * @param corr_id OTA transfer correlation ID.
   * @param finalized_offset Bytes finalized by slave (for traceability).
   * @param status_code Finalize status code being acknowledged.
   * @return true if ack frame was queued to transport.
   */
  bool sendFirmwareFinalizeAck(const MacAddress& to,
                               uint32_t corr_id,
                               uint32_t finalized_offset,
                               uint16_t status_code);

  /**
   * @brief Send a telemetry push control command to a paired slave.
   * @param to Destination MAC.
   * @param cmd Push command/action configuration.
   * @param corr_id Correlation ID for tracking acknowledgments.
   * @return true if command frame was sent.
   */
  bool sendTelemetryPushCommand(const MacAddress& to, const TelemetryPushCommand& cmd, uint32_t corr_id);

  /**
   * @brief Request coordinated channel switch with an already linked peer.
   * @param peer Peer MAC to switch with.
   * @param new_channel Target Wi-Fi channel.
   * @param now_ms Current monotonic tick time.
   * @param corr_id Correlation ID for switch handshake.
   * @return true if switch sequence started.
   */
  bool requestChannelChange(const MacAddress& peer, uint8_t new_channel, uint32_t now_ms, uint32_t corr_id);

  /**
   * @brief Start pair flow with a discovered peer.
   * @param peer Target peer MAC.
   * @param corr_id Correlation ID for pair handshake.
   * @return true if pair flow started.
   */
  bool requestPair(const MacAddress& peer, uint32_t corr_id);

  /**
   * @brief Re-install one already persisted secure link.
   * @param record Persisted pair record to restore.
   * @return true if peer was restored into active runtime state.
   */
  bool restorePairedLink(const PairRecord& record);

  /**
   * @brief Activate a known persisted paired peer as the current runtime target.
   *
   * For multi-slave scaling, this performs a deterministic context switch to an already
   * discovered paired peer record stored in persistence, keeping only one active secured peer
   * in the runtime pairing engine.
   *
   * @param peer Target peer MAC to activate.
   * @return true when active context is the requested peer after return.
   */
  bool activatePairedPeer(const MacAddress& peer);

  /**
   * @brief Start unpair flow with current peer.
   * @param peer Peer MAC to unpair.
   * @param corr_id Correlation ID for unpair handshake.
   * @return true if unpair flow started.
   */
  bool requestUnpair(const MacAddress& peer, uint32_t corr_id);

  /**
   * @brief Remove a peer from runtime and optionally from persistence.
   * @param peer Peer MAC to remove.
   * @param erase_persisted True to erase stored link metadata as well.
   * @return true if runtime peer was removed.
   */
  bool removePeer(const MacAddress& peer, bool erase_persisted);

  /**
   * @brief Check whether a secure paired link is currently active.
   *
   * Single-slave invariant: this reports state for the single active link only.
   */
  bool isPaired() const;

  /**
   * @brief Get currently paired peer MAC.
   * @param out_peer Filled with active peer when paired.
   * @return true when the single active paired peer is available.
   */
  bool getPairedPeer(MacAddress& out_peer) const;

  /** @brief Read current active ESP-NOW channel from transport. */
  uint8_t currentChannel() const { return transport_.getChannel(); }

  /**
   * @brief Check whether one peer exists in persisted paired records.
   * @param peer Target peer MAC.
   * @return true when a valid persisted pair record exists for this manager local MAC.
   */
  bool hasPersistedPair(const MacAddress& peer);

  /**
   * @brief Count persisted paired peers for this manager local MAC.
   *
   * Uses paired index metadata only.
   *
   * @return Number of valid persisted peers.
   */
  size_t persistedPairCount();

  /**
   * @brief Snapshot persisted paired peers for this manager local MAC.
   *
   * Order is deterministic and follows `(pair_seq ASC, mac ASC)` from pair index metadata.
   *
   * @param out Filled with persisted peer MACs.
   */
  void getPersistedPeers(std::vector<MacAddress>& out);

  /** @brief One persisted peer entry with optional role hint. */
  struct PersistedPeerRoleEntry {
    MacAddress peer{};
    /** Profile/role code (`kProfile* & 0xFF`) or `0` when unknown. */
    uint8_t role_code = 0;
  };

  /**
   * @brief Snapshot persisted peers with role hints when available.
   * @param out Filled with `(peer, role_code)` entries in persisted order.
   */
  void getPersistedPeersWithRole(std::vector<PersistedPeerRoleEntry>& out);

  /**
   * @brief Persist one peer role hint (`kProfile* & 0xFF`) for frontend pairing lists.
   * @return true when cached (and persisted when backend available).
   */
  bool savePeerRoleHint(const MacAddress& peer, uint8_t role_code);

  /**
   * @brief Load one peer role hint (`kProfile* & 0xFF`) for paired snapshot enrichment.
   * @return true when a non-zero role code is available.
   */
  bool loadPeerRoleHint(const MacAddress& peer, uint8_t& out_role_code);

  /** @brief One runtime-persisted channel binding entry for one peer. */
  struct PersistedPeerChannelEntry {
    MacAddress peer{};
    uint8_t channel = 0;
    std::string channel_key{};
  };

  /**
   * @brief Snapshot persisted per-peer channel bindings and derived runtime key names.
   * @param out Filled with `(peer, channel, channel_key)` entries.
   * @return true when query completed (including empty result when persistence is unavailable).
   */
  bool getPersistedPeerChannels(std::vector<PersistedPeerChannelEntry>& out) const;

  /**
   * @brief Apply one runtime channel locally and persist it for all known peers.
   * @param channel Target channel (`1..14`).
   * @return true when local apply (and persistence updates when enabled) succeeds.
   */
  bool applyRuntimeChannelToAllPeers(uint8_t channel);

  /** @brief Maximum secure links supported by one physical topology device (includes ICM link). */
  static constexpr size_t kTopologyMaxSecureLinksPhysical = 14U;
  /** @brief Maximum lateral topology slots per physical device (secure links minus ICM link). */
  static constexpr size_t kTopologyMaxSlots = kTopologyMaxSecureLinksPhysical - 1U;
  /** @brief Maximum number of topology group-seed records cached per device. */
  static constexpr size_t kTopologyMaxGroups = 12U;
  /** @brief Topology blob schema version used by manager runtime persistence. */
  static constexpr uint8_t kTopologySchemaVersion = 1U;

  /** @brief Topology lifecycle state in runtime. */
  enum class TopologyState : uint8_t {
    None = 0,
    Staged = 1,
    Committed = 2,
  };

  /** @brief One topology slot resolved to one lateral peer route. */
  struct TopologySlot {
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

  /** @brief One deterministic group-seed cache entry used for lateral LMK derivation. */
  struct TopologyGroupSeed {
    bool enabled = false;
    uint8_t group_id = 0;
    std::array<uint8_t, 32> seed{};
  };

  /** @brief Full manager-owned topology snapshot (staged or committed). */
  struct TopologySnapshot {
    uint8_t schema_version = kTopologySchemaVersion;
    TopologyState state = TopologyState::None;
    uint32_t topology_version = 0;
    uint8_t index_neg = 0;
    uint8_t index_pos = 0;
    uint8_t enabled_slot_count = 0;
    uint8_t enabled_group_count = 0;
    uint32_t checksum = 0;
    std::array<TopologySlot, kTopologyMaxSlots> slots{};
    std::array<TopologyGroupSeed, kTopologyMaxGroups> groups{};
  };

  /** @brief Combined staged+committed topology runtime status. */
  struct TopologyStatus {
    bool has_staged = false;
    bool has_committed = false;
    TopologySnapshot staged{};
    TopologySnapshot committed{};
  };

  /** @brief Index-based lateral trigger request resolved via committed topology slots. */
  struct TopologyTriggerRequest {
    int8_t target_index = 0;          // -13..+13 (non-zero)
    uint8_t direction = 0;            // 1=forward, 2=reverse
    uint16_t delay_ms = 0;
    uint16_t hold_ms = 0;
    uint8_t source_virtual_index = 0xFF;  // 255 physical child
  };

  /** @brief One index-based lateral trigger entry used by batch send API. */
  struct TopologyTriggerBatchEntry {
    int8_t target_index = 0;          // -13..+13 (non-zero)
    uint16_t delay_ms = 0;
    uint16_t hold_ms = 0;
  };

  /** @brief Batch topology trigger request resolved and sent to one physical peer. */
  struct TopologyTriggerBatchRequest {
    uint8_t direction = 0;                 // 1=forward, 2=reverse
    uint8_t source_virtual_index = 0xFF;   // 255 physical child
    std::vector<TopologyTriggerBatchEntry> entries{};
  };

  /**
   * @brief Stage one candidate topology snapshot in runtime (no lateral apply yet).
   *
   * Validation and checksum normalization are applied before acceptance.
   *
   * @param snapshot Candidate topology snapshot.
   * @param out_error Optional error token when staging fails.
   * @return true when snapshot is accepted into staged runtime state.
   */
  bool stageTopology(const TopologySnapshot& snapshot, std::string* out_error = nullptr);

  /**
   * @brief Commit currently staged topology snapshot into active committed runtime state.
   * @param out_error Optional error token when commit fails.
   * @return true when committed snapshot is updated.
   */
  bool commitStagedTopology(std::string* out_error = nullptr);

  /**
   * @brief Clear currently staged topology snapshot.
   * @return true when staged snapshot is cleared (or already empty).
   */
  bool clearStagedTopology();

  /**
   * @brief Read staged/committed topology runtime status.
   * @param out_status Filled status output.
   * @return true when status could be produced.
   */
  bool getTopologyStatus(TopologyStatus& out_status) const;

  /**
   * @brief Read one topology snapshot by state selector.
   * @param state Requested snapshot state (`Staged` or `Committed`).
   * @param out_snapshot Filled snapshot output.
   * @return true when selected snapshot exists.
   */
  bool getTopologySnapshot(TopologyState state, TopologySnapshot& out_snapshot) const;

  /**
   * @brief Resolve one relative topology index to an enabled slot route.
   *
   * @param target_idx Relative target index (`-16..+16`, non-zero).
   * @param out_slot Filled with resolved slot route.
   * @param out_error Optional error token (`invalid_index`, `out_of_window`,
   *                  `index_unmapped`, `topology_missing`).
   * @param committed True to resolve against committed snapshot, false for staged.
   * @return true when one enabled slot is resolved.
   */
  bool resolveTopologyTargetIndex(int8_t target_idx,
                                  TopologySlot& out_slot,
                                  std::string* out_error = nullptr,
                                  bool committed = true) const;

  /**
   * @brief Send one topology lateral trigger command using index-based routing.
   *
   * This resolves `target_index` against committed topology, validates direction/source
   * virtual index, builds `L2P v1` trigger payload, and sends to resolved peer route.
   *
   * @param request Trigger request fields.
   * @param corr_id Optional transport correlation ID (0 auto-generates).
   * @param out_seq Optional returned trigger sequence used in payload.
   * @param out_error Optional error token (`topology_missing`, `invalid_direction`,
   *                  `invalid_source_virtual_index`, `encode_failed`,
   *                  `transport_send_failed`, plus resolver errors).
   * @return true when trigger was encoded and queued to transport.
   */
  bool sendTopologyTrigger(const TopologyTriggerRequest& request,
                           uint32_t corr_id = 0,
                           uint16_t* out_seq = nullptr,
                           std::string* out_error = nullptr);

  /**
   * @brief Send one topology lateral trigger batch to one resolved physical peer.
   *
   * All entries must resolve to the same target peer MAC and target role.
   *
   * @param request Batch request fields.
   * @param corr_id Optional transport correlation ID (0 auto-generates).
   * @param out_seq Optional returned batch sequence used in payload.
   * @param out_error Optional error token.
   * @return true when batch payload was encoded and queued to transport.
   */
  bool sendTopologyTriggerBatch(const TopologyTriggerBatchRequest& request,
                                uint32_t corr_id = 0,
                                uint16_t* out_seq = nullptr,
                                std::string* out_error = nullptr);

  /**
   * @brief Snapshot logical peer registry state.
   * @param out Filled with current registry records.
   */
  void getPeerRegistrySnapshot(std::vector<PeerRecord>& out) const;

  /**
   * @brief Advance internal state machines and timed jobs.
   * @param now_ms Monotonic time in milliseconds.
   */
  void tick(uint32_t now_ms);

  /**
   * @brief Return currently discovered peers cache.
   * @param out Filled with peer MACs still inside discovery TTL.
   */
  void getDiscoveredPeers(std::vector<MacAddress>& out) const;

  /**
   * @brief Queue a mandatory slave-originated event report.
   * @param event_id Profile event identifier.
   * @param severity Event severity level.
   * @param value Event integer value payload.
   * @param event_ts_s Optional event timestamp in epoch seconds.
   * @return true if queued for delivery.
   */
  bool publishMandatoryEvent(uint16_t event_id, uint8_t severity, int32_t value, uint32_t event_ts_s = 0);

  /**
   * @brief Set active wire codec instance.
   * @param codec Codec implementation; null resets to default codec.
   */
  void setCodec(IProfileCodec* codec);

  /**
   * @brief Set active wire codec by registered codec ID.
   * @param codec_id Registered codec identifier.
   * @return true if codec exists and is allowed by current profile.
   */
  bool setCodecById(CodecId codec_id);

  /** @brief Get currently active wire codec. */
  const IProfileCodec& codec() const;

  /**
   * @brief Encode descriptor query using active codec.
   * @param query Descriptor query structure.
   * @param out_payload Encoded payload bytes.
   * @return true on successful encoding.
   */
  bool encodeDescriptorQueryPayload(const DescriptorQuery& query, std::vector<uint8_t>& out_payload) const;

  /**
   * @brief Decode descriptor query using active codec.
   * @param payload Encoded payload bytes.
   * @param len Payload length in bytes.
   * @param out_query Decoded query output.
   * @return true on successful decoding.
   */
  bool decodeDescriptorQueryPayload(const uint8_t* payload, size_t len, DescriptorQuery& out_query) const;

  /**
   * @brief Encode descriptor response using active codec.
   * @param response Response object to encode.
   * @param out_payload Encoded payload bytes.
   * @return true on successful encoding.
   */
  bool encodeDescriptorResponsePayload(const DescriptorResponse& response, std::vector<uint8_t>& out_payload) const;

  /**
   * @brief Decode descriptor response using active codec.
   * @param payload Encoded payload bytes.
   * @param len Payload length in bytes.
   * @param out_response Decoded response output.
   * @return true on successful decoding.
   */
  bool decodeDescriptorResponsePayload(const uint8_t* payload, size_t len, DescriptorResponse& out_response) const;

  /**
   * @brief Encode control command payload with active codec.
   * @param cmd_id Control command ID.
   * @param out_payload Encoded payload bytes.
   * @return true on successful encoding.
   */
  bool encodeControlCommandPayload(uint16_t cmd_id, std::vector<uint8_t>& out_payload) const;

  /**
   * @brief Decode control command payload with active codec.
   * @param payload Encoded payload bytes.
   * @param len Payload length in bytes.
   * @param out_cmd_id Decoded command ID.
   * @return true on successful decoding.
   */
  bool decodeControlCommandPayload(const uint8_t* payload, size_t len, uint16_t& out_cmd_id) const;

  /**
   * @brief Encode control result payload with active codec.
   * @param cmd_id Original command ID.
   * @param result_code Command result code.
   * @param out_payload Encoded payload bytes.
   * @return true on successful encoding.
   */
  bool encodeControlResultPayload(uint16_t cmd_id, uint16_t result_code, std::vector<uint8_t>& out_payload) const;

  /**
   * @brief Decode control result payload with active codec.
   * @param payload Encoded payload bytes.
   * @param len Payload length in bytes.
   * @param out_cmd_id Decoded command ID.
   * @param out_result_code Decoded result code.
   * @return true on successful decoding.
   */
  bool decodeControlResultPayload(const uint8_t* payload,
                                  size_t len,
                                  uint16_t& out_cmd_id,
                                  uint16_t& out_result_code) const;

  /**
   * @brief Execute one descriptor query using active profile/logger context.
   * @param provider Descriptor provider implementation.
   * @param query Decoded query object.
   * @param out_response Filled descriptor response.
   * @return true if query handler executed.
   */
  bool executeDescriptorQuery(IDescriptorProvider& provider,
                              const DescriptorQuery& query,
                              DescriptorResponse& out_response);

  /**
   * @brief Select local device profile.
   * @param profile_id Profile identifier to activate.
   * @return true if profile exists and activation succeeded.
   */
  bool setLocalProfile(ProfileId profile_id);

  /** @brief Get active local profile ID. */
  ProfileId localProfileId() const;

  /** @brief Get active local profile display name. */
  const char* localProfileName() const;

  /** @brief Get active local profile definition pointer. */
  const IProfileDefinition* localProfile() const;

  /** @brief Access accumulated runtime counters/timings. */
  const ManagerRuntimeMetrics& runtimeMetrics() const;

  /** @brief Clear runtime counters/timings back to zero. */
  void resetRuntimeMetrics();

  /**
   * @brief Attach or replace runtime logger.
   * @param logger Logger instance or null to disable runtime logging.
   */
  void setLogger(LibraryLogger* logger);

  /** @brief Get attached runtime logger pointer (nullable). */
  LibraryLogger* logger() const;

  /**
   * @brief Enable/disable master-side processing of incoming discovery frames.
   *
   * When disabled on master, discovery packets are dropped early and do not
   * trigger discovery cache updates, events, or pairing hooks.
   */
  void setDiscoveryRxEnabled(bool enabled);

  /** @brief Read current master-side discovery RX gate state. */
  bool discoveryRxEnabled() const;

  /**
   * @brief Replace hardware peer-window policy used by pairing attach/evict operations.
   *
   * Pass `nullptr` to restore default direct transport behavior.
   */
  void setHardwarePeerWindow(IHardwarePeerWindow* peer_window);

  /**
   * @brief Append one externally prepared logger record.
   * @param record Complete record payload to encode/store.
   * @return true when logger accepted the record.
   */
  bool logExternalRecord(const LibraryLogRecord& record);

  /**
   * @brief Append one external event without manually building `LibraryLogRecord`.
   *
   * Typical usage is from device/application classes that own product logic and
   * want to write compact numeric diagnostics into the shared library log stream.
   *
   * @param level Severity level (`Error..Debug`).
   * @param source_id Product-defined source identifier.
   * @param event_id Product-defined event identifier.
   * @param p0 Optional integer parameter 0.
   * @param p1 Optional integer parameter 1.
   * @param p2 Optional integer parameter 2.
   * @param ext Optional compact binary extension bytes.
   * @param ext_len Extension length in bytes.
   * @return true when logger accepted the record.
   */
  bool logExternalEvent(LibraryLogLevel level,
                        uint16_t source_id,
                        uint16_t event_id,
                        int32_t p0 = 0,
                        int32_t p1 = 0,
                        int32_t p2 = 0,
                        const uint8_t* ext = nullptr,
                        size_t ext_len = 0);

  /**
   * @brief Persist logger runtime config (enabled/min-level) when persistence is available.
   * @return true when persistence succeeded or is not configured.
   */
  bool persistLoggerConfig();

  /** @brief Save local manager-scoped metadata blob in persistence. */
  bool saveLocalMetaBlob(uint8_t slot, const uint8_t* data, size_t len);
  /** @brief Load local manager-scoped metadata blob from persistence. */
  bool loadLocalMetaBlob(uint8_t slot, std::vector<uint8_t>& out) const;
  /** @brief Erase local manager-scoped metadata blob from persistence. */
  bool eraseLocalMetaBlob(uint8_t slot);

 private:
  struct DiscoveryCacheEntry {
    MacAddress mac{};
    uint32_t ttl_ms = 0;
    uint32_t last_tick_ms = 0;
  };

  struct PendingPullContext {
    bool valid = false;
    MacAddress peer{};
    uint32_t corr_id = 0;
    uint8_t service = 0;
    uint8_t op = 0;
  };
  struct QueuedPullRequest {
    MacAddress from{};
    uint32_t corr_id = 0;
    uint8_t wire_service = 0;
    uint8_t wire_op = 0;
    std::vector<uint8_t> payload{};
    uint32_t queued_ms = 0;
  };
  struct QueuedPullResponse {
    MacAddress from{};
    uint32_t corr_id = 0;
    std::vector<uint8_t> payload{};
    uint32_t queued_ms = 0;
  };
  struct QueuedFirmwareRx {
    MessageType type = MessageType::Error;
    MacAddress from{};
    uint32_t corr_id = 0;
    std::vector<uint8_t> payload{};
    uint32_t queued_ms = 0;
    int rssi = 0;
  };
  struct QueuedControlRx {
    MessageType type = MessageType::Error;
    MacAddress from{};
    uint32_t corr_id = 0;
    std::vector<uint8_t> payload{};
    uint32_t queued_ms = 0;
    int rssi = 0;
  };

  struct PendingMandatoryEvent {
    uint16_t event_id = 0;
    uint8_t severity = 0;
    int32_t value = 0;
    uint32_t event_ts_s = 0;
    uint32_t corr_id = 0;
    uint8_t retry_count = 0;
  };

  struct PushMetricState {
    std::string key;
    bool enabled = true;
    TelemetryPushMode mode = TelemetryPushMode::Hybrid;
    uint32_t interval_ms = 1000;
    uint32_t min_gap_ms = 200;
    uint32_t next_periodic_ms = 0;
    bool use_threshold = false;
    float delta_abs = 0.0f;
    bool has_last = false;
    float last_value = 0.0f;
    uint32_t last_report_ms = 0;
  };

  struct TelemetryPushSession {
    uint16_t stream_id = 1;
    bool enabled = false;
    bool paused = false;
    TelemetryPushMode mode = TelemetryPushMode::Hybrid;
    uint32_t interval_ms = 1000;
    uint32_t min_gap_ms = 200;
    MacAddress master{};
    bool has_master = false;
    std::vector<PushMetricState> metrics;
    uint8_t consecutive_tx_fail = 0;
    uint32_t backoff_until_ms = 0;
  };

  bool sendTyped(const MacAddress& to,
                 MessageType type,
                 const uint8_t* payload,
                 size_t len,
                 uint32_t corr_id,
                 uint8_t wire_service = 0,
                 uint8_t wire_op = 0,
                 uint8_t wire_msg_type = 0,
                 std::string* out_error = nullptr);
  bool shouldAttachTimeSync(uint32_t now_ms) const;
  bool appendTimeSyncMetadata(const uint8_t* payload,
                              size_t len,
                              uint8_t& inout_flags,
                              std::vector<uint8_t>& out_payload);
  bool applyTimeSyncFromFrame(const FrameHeader& header, const uint8_t* payload, size_t len);
  bool dispatchRxByType(const MacAddress& from,
                        const FrameHeader& header,
                        const uint8_t* payload,
                        size_t payload_len,
                        int rssi);
  bool onRxDiscovery(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len, int rssi);
  bool onRxPairInit(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len);
  bool onRxPairInitAck(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len);
  bool onRxPairConfirm(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len);
  bool onRxPairConfirmAck(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len);
  bool onRxUnpairRequest(const MacAddress& from, const FrameHeader& header);
  bool onRxPullRequest(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len);
  bool onRxPullResponse(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len);
  bool onRxEventReport(const MacAddress& from, const FrameHeader& header, const uint8_t* payload, size_t payload_len);
  bool onRxTopologyTrigger(const MacAddress& from,
                           const FrameHeader& header,
                           const uint8_t* payload,
                           size_t payload_len);
  bool onRxTopologyTriggerBatch(const MacAddress& from,
                                const FrameHeader& header,
                                const uint8_t* payload,
                                size_t payload_len);
  bool onRxTopologyTriggerAck(const MacAddress& from,
                              const FrameHeader& header,
                              const uint8_t* payload,
                              size_t payload_len);
  bool onRxChannelSwitchPrepare(const MacAddress& from,
                                const FrameHeader& header,
                                const uint8_t* payload,
                                size_t payload_len);

  bool handleTelemetryPushControl(const MacAddress& from,
                                  uint32_t corr_id,
                                  const uint8_t* payload,
                                  size_t len);
  void tickTelemetryPush(uint32_t now_ms);
  void tickPullRequestQueue(uint32_t now_ms);
  bool processQueuedPullRequest_(const QueuedPullRequest& request);
  void tickPullResponseQueue(uint32_t now_ms);
  bool processQueuedPullResponse_(const QueuedPullResponse& response);
  void tickFirmwareRxQueue(uint32_t now_ms);
  bool processQueuedFirmwareRx_(const QueuedFirmwareRx& frame);
  void tickControlRxQueue(uint32_t now_ms);
  bool processQueuedControlRx_(const QueuedControlRx& frame);
  void tickOtaBootCompletionNotice(uint32_t now_ms);
  void tickOtaFinalizeStatusRetry(uint32_t now_ms);
  void tickMandatoryEventQueue(uint32_t now_ms);
  bool sendMandatoryEventReport(const MacAddress& to, const PendingMandatoryEvent& ev);
  bool sendTelemetryPushReport(const MacAddress& to,
                               const std::vector<TelemetrySample>& samples,
                               uint32_t corr_id,
                               const char* cause);
  bool buildDescriptorReply(bool ok, const std::string& message, std::vector<uint8_t>& out_payload);
  bool isKnownTelemetryKey(const std::string& key,
                           const std::vector<TelemetryDescriptor>* schema_cache = nullptr,
                           const std::vector<TelemetrySample>* snapshot_cache = nullptr) const;
  bool telemetryKeyFromIndex(uint16_t metric_index,
                             std::string& out_key,
                             const std::vector<TelemetryDescriptor>* schema_cache = nullptr) const;
  bool resolveTelemetryMetricIdentity(const TelemetryPushMetricConfig& metric,
                                      std::string& out_key,
                                      const std::vector<TelemetryDescriptor>* schema_cache = nullptr,
                                      const std::vector<TelemetrySample>* snapshot_cache = nullptr) const;
  bool persistTelemetryPushConfig();
  bool restoreTelemetryPushConfig(const MacAddress& master);
  bool clearTelemetryPushConfig(const MacAddress& master);
  bool restoreLoggerConfig();
  bool validateTopologySnapshot_(TopologySnapshot& inout_snapshot, std::string* out_error) const;
  uint32_t computeTopologyChecksum_(const TopologySnapshot& snapshot) const;
  bool saveTopologySnapshotMeta_(uint8_t slot, const TopologySnapshot& snapshot);
  bool loadTopologySnapshotMeta_(uint8_t slot, TopologySnapshot& out_snapshot) const;
  bool eraseTopologySnapshotMeta_(uint8_t slot);
  bool resolveTopologyMasterMac_(MacAddress& out_master) const;
  bool findTopologyGroupSeed_(const TopologySnapshot& snapshot,
                              uint8_t group_id,
                              std::array<uint8_t, 32>& out_seed) const;
  bool deriveTopologySlotLmk_(const TopologySnapshot& snapshot,
                              const TopologySlot& slot,
                              LmkKey& out_lmk,
                              std::string* out_error) const;
  struct TopologyPersistedLmkEntry_ {
    MacAddress peer{};
    LmkKey lmk{};
    uint8_t group_id = 0;
  };
  struct TopologyPersistedLmkMeta_ {
    uint32_t topology_version = 0;
    uint32_t topology_checksum = 0;
    MacAddress master_mac{};
    MacAddress local_mac{};
    uint8_t local_profile = 0;
    std::vector<TopologyPersistedLmkEntry_> entries{};
  };
  bool loadTopologyLmkMeta_(TopologyPersistedLmkMeta_& out_meta, std::string* out_error);
  bool validateTopologyLmkMeta_(const TopologyPersistedLmkMeta_& meta,
                                const TopologySnapshot& snapshot,
                                const MacAddress& paired_master,
                                std::string* out_error) const;
  bool findSlotLmkFromNvs_(const TopologySlot& slot,
                           const TopologyPersistedLmkMeta_& meta,
                           LmkKey& out_lmk) const;
  bool buildDesiredTopologyPeersFromNvs_(const TopologySnapshot& snapshot,
                                         const TopologyPersistedLmkMeta_& meta,
                                         std::map<MacAddress, LmkKey>& out_desired,
                                         std::string* out_error) const;
  bool applyTopologyDesiredPeers_(const std::map<MacAddress, LmkKey>& desired,
                                  std::string* out_error);
  bool ensureTopologyPeerReadyForSlot_(const TopologySlot& slot, std::string* out_error);
  bool materializeTopologyPeers_(const TopologySnapshot& snapshot, std::string* out_error);
  bool saveTopologyLmkMeta_(const TopologySnapshot& snapshot);
  void restoreTopologySnapshots_();
  bool findTopologyAuthorizedSourceSlot_(const MacAddress& from,
                                         uint8_t src_role,
                                         uint8_t src_vid,
                                         uint8_t dst_vid,
                                         TopologySlot& out_slot) const;
  bool isTopologyTriggerDuplicate_(const MacAddress& from, uint8_t src_role, uint8_t src_vid, uint16_t seq);
  void pruneTopologyTriggerDedup_(uint32_t now_ms);

  void blinkForMessage(MessageType type);
  void touchDiscovery(const MacAddress& from);
  void tickDiscoveryCache(uint32_t now_ms);
  void syncPairedCacheState_();
  bool handleFirmwareBegin(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len);
  bool handleFirmwareChunk(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len);
  bool handleFirmwareEnd(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len);
  bool handleFirmwareStatus(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len);
  bool sendFirmwareStatus(const MacAddress& to,
                          uint32_t corr_id,
                          uint8_t kind,
                          uint32_t offset_bytes,
                          uint16_t status_code);
  void upsertPeerRoleHintCache_(const MacAddress& peer, uint8_t role_code);
  bool findPeerRoleHintCache_(const MacAddress& peer, uint8_t& out_role_code) const;
  void emitRuntimeLog(LibraryLogLevel level,
                      uint16_t event_id,
                      uint32_t corr_id,
                      int32_t p1,
                      int32_t p2,
                      const uint8_t* ext = nullptr,
                      size_t ext_len = 0);
  void emitTopologyRuntimeLog_(LibraryLogLevel level,
                               uint16_t event_id,
                               int32_t p1,
                               int32_t p2,
                               const char* reason);
  void setTopologyRestoreStatus_(uint8_t mode, const char* reason);
  void noteTopologyPeerReadyFailure_(const char* reason);

  ManagerConfig config_;
  ITransport& transport_;
  PairingStore* store_;
  IEventSink* events_;
  IPlatformHooks* hooks_;
  IFirmwareStreamSink* firmware_sink_;
  IControlPlane* control_plane_;
  ITelemetryPushProvider* telemetry_push_provider_;
  DefaultProfileCodec default_codec_{};
  IProfileCodec* codec_ = nullptr;
  const IProfileDefinition* local_profile_ = nullptr;
  ProfileId local_profile_id_ = kProfileUnknown;
  SystemTimeSource default_time_source_{};
  SystemTimeSink default_time_sink_{};
  ITimeSource* time_source_;
  ITimeSink* time_sink_;
  DirectHardwarePeerWindow default_peer_window_;
  IHardwarePeerWindow* peer_window_ = nullptr;
  PairingEngine pairing_;
  struct PeerRoleHintEntry {
    MacAddress peer{};
    uint8_t role_code = 0;
  };
  std::vector<PeerRoleHintEntry> peer_role_hints_{};
  PeerRegistry peer_registry_{};
  std::vector<DiscoveryCacheEntry> discovery_cache_;
  bool discovery_rx_enabled_ = true;
  uint32_t current_now_ms_ = 0;
  uint32_t last_time_sync_tx_ms_ = 0;
  bool has_time_sync_tx_ = false;
  uint64_t last_time_sync_rx_epoch_s_ = 0;
  PendingPullContext pending_pull_;
  std::deque<QueuedPullRequest> pull_request_queue_{};
  std::deque<QueuedPullResponse> pull_response_queue_{};
  std::deque<QueuedFirmwareRx> firmware_rx_queue_{};
  std::deque<QueuedControlRx> control_rx_queue_{};
  QueuedPullRequest pull_request_recycle_{};
  QueuedPullResponse pull_response_recycle_{};
  QueuedFirmwareRx firmware_rx_recycle_{};
  QueuedControlRx control_rx_recycle_{};
  bool has_pull_request_recycle_ = false;
  bool has_pull_response_recycle_ = false;
  bool has_firmware_rx_recycle_ = false;
  bool has_control_rx_recycle_ = false;
  std::vector<uint8_t> tx_wrapped_payload_scratch_{};
  std::vector<uint8_t> tx_encoded_frame_scratch_{};
  TelemetryPushSession push_session_{};
  std::vector<TelemetrySample> telemetry_snapshot_cache_{};
  std::vector<TelemetrySample> telemetry_aligned_snapshot_cache_{};
  std::unordered_map<std::string, const TelemetrySample*> telemetry_snapshot_index_cache_{};
  std::vector<const TelemetrySample*> telemetry_metric_samples_cache_{};
  std::vector<float> telemetry_metric_values_cache_{};
  std::vector<uint8_t> telemetry_metric_flags_cache_{};
  std::vector<TelemetrySample> telemetry_send_samples_cache_{};
  std::deque<PendingMandatoryEvent> mandatory_event_queue_{};
  uint32_t mandatory_event_corr_seq_ = 1;
  uint32_t mandatory_event_last_tx_ms_ = 0;
  uint32_t mandatory_event_backoff_until_ms_ = 0;
  uint8_t mandatory_event_fail_streak_ = 0;
  MacAddress local_mac_{};
  uint32_t telemetry_push_seq_ = 1;
  uint32_t ota_boot_notice_last_try_ms_ = 0;
  uint32_t ota_transfer_corr_id_ = 0;
  uint32_t ota_transfer_last_received_offset_ = 0;
  uint32_t ota_transfer_expected_size_ = 0;
  uint32_t ota_transfer_last_acked_offset_ = 0;
  uint32_t ota_transfer_ack_step_bytes_ = 0;
  uint32_t ota_transfer_next_ack_offset_ = 0;
  uint32_t ota_transfer_last_nack_offset_ = 0;
  uint32_t ota_transfer_last_nack_ms_ = 0;
  bool ota_finalize_pending_ = false;
  MacAddress ota_finalize_peer_{};
  uint32_t ota_finalize_corr_id_ = 0;
  uint32_t ota_finalize_offset_ = 0;
  uint16_t ota_finalize_status_code_ = 0;
  uint8_t ota_finalize_kind_ = 0;
  uint32_t ota_finalize_started_ms_ = 0;
  uint32_t ota_finalize_last_tx_ms_ = 0;
  bool paired_cache_valid_ = false;
  bool paired_cache_state_ = false;
  MacAddress paired_cache_peer_{};
  TopologySnapshot topology_staged_{};
  TopologySnapshot topology_committed_{};
  bool has_topology_staged_ = false;
  bool has_topology_committed_ = false;
  uint8_t topology_restore_mode_ = 0;
  std::array<char, 40> topology_restore_reason_{};
  uint32_t topology_peer_ready_fail_count_ = 0;
  std::array<char, 40> topology_peer_ready_last_reason_{};
  std::map<std::string, uint32_t> topology_peer_ready_fail_by_reason_{};
  std::vector<MacAddress> topology_attached_peers_{};
  struct TopologyTriggerDedupEntry {
    MacAddress source{};
    uint8_t src_role = 0;
    uint8_t src_vid = 0xFF;
    uint16_t seq = 0;
    uint32_t seen_ms = 0;
  };
  std::deque<TopologyTriggerDedupEntry> topology_trigger_dedup_{};
  uint16_t topology_trigger_seq_ = 1;
  ManagerRuntimeMetrics metrics_{};
  LibraryLogger* logger_ = nullptr;
};

}  // namespace espnow_link








