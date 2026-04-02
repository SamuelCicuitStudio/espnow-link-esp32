#pragma once

#include <vector>

#include "espnow_link/config.hpp"
#include "espnow_link/events.hpp"
#include "espnow_link/hooks.hpp"
#include "espnow_link/library_logger.hpp"
#include "espnow_link/persistence.hpp"
#include "espnow_link/peer_window.hpp"
#include "espnow_link/protocol.hpp"
#include "espnow_link/security.hpp"
#include "espnow_link/transport.hpp"
#include "espnow_link/types.hpp"
#include "espnow_link/time.hpp"

namespace espnow_link {

/**
 * @brief Pairing state machine implementation for master/slave secure link lifecycle.
 */
class PairingEngine {
 public:
  /**
   * @brief Construct pairing engine.
   * @param config Manager configuration.
   * @param transport Radio transport implementation.
   * @param store Optional persistence store.
   * @param events Optional event sink.
   * @param hooks Optional platform hooks.
   * @param time_source Optional wall-clock provider for metadata.
   * @param logger Optional runtime logger for compact pairing traces.
   */
  PairingEngine(const ManagerConfig& config,
                ITransport& transport,
                PairingStore* store,
                IEventSink* events,
                IPlatformHooks* hooks,
                IHardwarePeerWindow* peer_window = nullptr,
                ITimeSource* time_source = nullptr,
                LibraryLogger* logger = nullptr);

  /** @brief Replace runtime logger sink used by pairing engine. */
  void setLogger(LibraryLogger* logger);
  /** @brief Replace hardware peer-window policy used for attach/evict operations. */
  void setPeerWindow(IHardwarePeerWindow* peer_window);

  /** @brief Set local MAC identity used for pairing packets. */
  void setLocalMac(const MacAddress& local);
  /** @brief Set local profile ID announced during pair init. */
  void setLocalProfileId(uint8_t profile_id);
  /** @brief Force internal paired flag (used during restore/recovery paths). */
  void setPaired(bool paired);
  /** @brief Check whether a secure pair link is currently active. */
  bool isPaired() const;
  /** @brief Get active paired peer MAC. */
  bool getPairedPeer(MacAddress& out_peer) const;
  /** @brief Get local MAC configured in pairing engine. */
  const MacAddress& localMac() const;

  /** @brief Advance pairing timers and periodic discovery/unpair tasks. */
  bool tick(uint32_t now_ms);

  /** @brief Handle inbound discovery frame from peer. */
  bool onDiscovery(const MacAddress& from, uint32_t corr_id);
  /** @brief Handle inbound pair init request. */
  bool onPairInit(const MacAddress& from, uint32_t corr_id, const PairSeed& seed, uint32_t pairing_nonce);
  /** @brief Handle inbound pair init ack. */
  bool onPairInitAck(const MacAddress& from, uint32_t corr_id, uint32_t pairing_nonce_echo);
  /** @brief Handle inbound pair confirm request. */
  bool onPairConfirm(const MacAddress& from, uint32_t corr_id, uint32_t pairing_nonce_echo);
  /** @brief Handle inbound pair confirm ack. */
  bool onPairConfirmAck(const MacAddress& from, uint32_t corr_id, bool paired_flag);
  /** @brief Handle inbound pair busy signal. */
  bool onPairBusy(const MacAddress& from, uint32_t corr_id);
  /** @brief Restore active paired link from persisted pair record. */
  bool restorePairedLink(const PairRecord& record);
  /** @brief Start outbound pair flow to target peer. */
  bool requestPair(const MacAddress& peer, uint32_t corr_id);
  /** @brief Start outbound unpair flow to target peer. */
  bool requestUnpair(const MacAddress& peer, uint32_t corr_id);
  /** @brief Handle inbound unpair request. */
  bool onUnpairRequest(const MacAddress& from, uint32_t corr_id);
  /** @brief Handle inbound unpair ack. */
  bool onUnpairAck(const MacAddress& from, uint32_t corr_id);
  /** @brief Remove peer from runtime and optionally erase stored link. */
  bool removePeer(const MacAddress& peer, bool erase_persisted);

  /** @brief Start coordinated channel switch with paired peer. */
  bool requestChannelSwitch(const MacAddress& peer, uint8_t new_channel, uint32_t now_ms, uint32_t corr_id);
  /** @brief Handle inbound channel-switch prepare payload. */
  bool onChannelSwitchPrepare(const MacAddress& from, uint32_t corr_id, const ChannelSwitchPayload& payload);
  /** @brief Handle inbound channel-switch ack. */
  bool onChannelSwitchAck(const MacAddress& from, uint32_t corr_id);
  /** @brief Handle inbound channel-switch commit ack. */
  bool onChannelSwitchCommitAck(const MacAddress& from, uint32_t corr_id);

 private:
  bool sendControl(const MacAddress& to,
                   MessageType type,
                   uint32_t corr_id,
                   const uint8_t* payload,
                   size_t payload_len);
  bool attachPeer(const MacAddress& mac, bool encrypted, const LmkKey* lmk);
  bool evictPeer(const MacAddress& mac);
  bool shouldAttachTimeSync(uint32_t now_ms) const;

  void resetPairing();
  void failPairing(const MacAddress& peer, uint32_t corr_id, const char* msg);
  void emit(Event::Type type, const MacAddress& peer, uint32_t corr_id, const char* msg);
  void emitPairingLog(LibraryLogLevel level,
                      uint16_t event_id,
                      const MacAddress& peer,
                      uint32_t corr_id,
                      int32_t p1,
                      int32_t p2) const;
  bool persistPair();

  enum class PairPhase : uint8_t {
    Idle = 0,
    MasterWaitInitAck,
    MasterSecureGuard,
    MasterWaitConfirmAck,
    SlaveWaitConfirm,
  };

  enum class ChannelSwitchState : uint8_t {
    Idle = 0,
    MasterWaitAck,
    MasterWaitCommit,
    SlavePrepared,
  };

  ManagerConfig config_;
  ITransport& transport_;

  PairingStore* store_;
  IEventSink* events_;
  IPlatformHooks* hooks_;
  IHardwarePeerWindow* peer_window_;
  ITimeSource* time_source_;
  LibraryLogger* logger_;

  MacAddress local_mac_{};
  uint8_t local_profile_id_ = 1;
  // Single-slave runtime invariant: one active peer context per node.
  // Multi-peer scheduling/window management is intentionally out of scope here.
  MacAddress peer_mac_{};
  PairSeed current_seed_{};
  LmkKey current_lmk_{};
  uint32_t pairing_nonce_ = 0;

  LinkState state_ = LinkState::Unpaired;
  PairPhase pair_phase_ = PairPhase::Idle;
  uint32_t corr_id_ = 0;
  uint32_t pair_deadline_ms_ = 0;
  uint32_t pair_total_deadline_ms_ = 0;
  uint32_t pair_last_tick_ms_ = 0;

  uint32_t current_now_ms_ = 0;
  uint32_t discovery_last_sent_ms_ = 0;
  uint32_t discovery_counter_ = 1;
  uint32_t last_time_sync_tx_ms_ = 0;
  bool has_time_sync_tx_ = false;

  bool unpair_pending_ = false;
  uint32_t unpair_corr_id_ = 0;
  uint32_t unpair_deadline_ms_ = 0;
  uint32_t unpair_last_tick_ms_ = 0;
  bool deferred_unpair_cleanup_ = false;
  MacAddress deferred_unpair_peer_{};
  uint32_t deferred_unpair_corr_id_ = 0;
  uint32_t deferred_unpair_deadline_ms_ = 0;
  uint32_t deferred_unpair_last_tick_ms_ = 0;

  ChannelSwitchState channel_state_ = ChannelSwitchState::Idle;
  uint8_t pending_channel_ = 0;
  uint32_t channel_corr_id_ = 0;
  uint32_t channel_effective_at_ms_ = 0;
  uint32_t channel_deadline_ms_ = 0;
  std::vector<uint8_t> tx_wrapped_payload_scratch_{};
  std::vector<uint8_t> tx_encoded_frame_scratch_{};
};

}  // namespace espnow_link




