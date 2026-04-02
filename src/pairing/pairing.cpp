#include "espnow_link/pairing.hpp"

#include <vector>
#include <cstring>
#include <esp_system.h>

namespace espnow_link {

namespace {

constexpr uint16_t kLogSourcePairing = 0x0102;
constexpr uint16_t kLogEvtPairReqTx = 0x0001;
constexpr uint16_t kLogEvtPairInitRx = 0x0002;
constexpr uint16_t kLogEvtPairAckTx = 0x0003;
constexpr uint16_t kLogEvtSecurePeerInstall = 0x0004;
constexpr uint16_t kLogEvtPairConfirmTx = 0x0005;
constexpr uint16_t kLogEvtPairComplete = 0x0006;
constexpr uint16_t kLogEvtPairFail = 0x0007;
constexpr uint16_t kLogEvtUnpairReq = 0x0008;
constexpr uint16_t kLogEvtUnpairAck = 0x0009;
constexpr uint16_t kLogEvtUnpairTimeout = 0x000A;
constexpr uint16_t kLogEvtRestore = 0x000B;
constexpr uint16_t kLogEvtBusyReject = 0x000C;

void encodeChannelPayload(const ChannelSwitchPayload& in, uint8_t out[5]) {
  out[0] = in.new_channel;
  out[1] = static_cast<uint8_t>(in.effective_at_ms & 0xFF);
  out[2] = static_cast<uint8_t>((in.effective_at_ms >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>((in.effective_at_ms >> 16) & 0xFF);
  out[4] = static_cast<uint8_t>((in.effective_at_ms >> 24) & 0xFF);
}



bool appendTlv(std::vector<uint8_t>& out, uint8_t tag, uint8_t type, const uint8_t* value, uint16_t len) {
  out.push_back(tag);
  out.push_back(type);
  out.push_back(static_cast<uint8_t>(len & 0xFF));
  out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  if (len > 0) {
    if (value == nullptr) {
      return false;
    }
    out.insert(out.end(), value, value + len);
  }
  return true;
}

bool appendTlvU8(std::vector<uint8_t>& out, uint8_t tag, uint8_t v) {
  return appendTlv(out, tag, 0x01, &v, 1);
}

bool appendTlvU32(std::vector<uint8_t>& out, uint8_t tag, uint32_t v) {
  uint8_t b[4] = {
      static_cast<uint8_t>(v & 0xFF),
      static_cast<uint8_t>((v >> 8) & 0xFF),
      static_cast<uint8_t>((v >> 16) & 0xFF),
      static_cast<uint8_t>((v >> 24) & 0xFF),
  };
  return appendTlv(out, tag, 0x03, b, 4);
}

bool parsePairInitPayload(const uint8_t* payload,
                          size_t len,
                          PairSeed& out_seed,
                          uint32_t& out_nonce,
                          uint8_t& out_profile) {
  bool have_seed = false;
  bool have_nonce = false;
  bool have_profile = false;
  size_t off = 0;
  while (off + 4 <= len) {
    const uint8_t tag = payload[off + 0];
    const uint8_t type = payload[off + 1];
    const uint16_t tlv_len = static_cast<uint16_t>(payload[off + 2]) |
                             (static_cast<uint16_t>(payload[off + 3]) << 8);
    off += 4;
    if ((off + tlv_len) > len) {
      return false;
    }

    const uint8_t* v = payload + off;
    if (tag == 0x10 && type == 0x08 && tlv_len == out_seed.bytes.size()) {
      std::memcpy(out_seed.bytes.data(), v, out_seed.bytes.size());
      have_seed = true;
    } else if (tag == 0x11 && type == 0x03 && tlv_len == 4) {
      out_nonce = static_cast<uint32_t>(v[0]) |
                  (static_cast<uint32_t>(v[1]) << 8) |
                  (static_cast<uint32_t>(v[2]) << 16) |
                  (static_cast<uint32_t>(v[3]) << 24);
      have_nonce = true;
    } else if (tag == 0x12 && type == 0x01 && tlv_len == 1) {
      out_profile = v[0];
      have_profile = true;
    }
    off += tlv_len;
  }

  return have_seed && have_nonce && have_profile;
}

bool parseNonceEchoPayload(const uint8_t* payload, size_t len, uint8_t expected_tag, uint32_t& out_nonce) {
  size_t off = 0;
  while (off + 4 <= len) {
    const uint8_t tag = payload[off + 0];
    const uint8_t type = payload[off + 1];
    const uint16_t tlv_len = static_cast<uint16_t>(payload[off + 2]) |
                             (static_cast<uint16_t>(payload[off + 3]) << 8);
    off += 4;
    if ((off + tlv_len) > len) {
      return false;
    }

    const uint8_t* v = payload + off;
    if (tag == expected_tag && type == 0x03 && tlv_len == 4) {
      out_nonce = static_cast<uint32_t>(v[0]) |
                  (static_cast<uint32_t>(v[1]) << 8) |
                  (static_cast<uint32_t>(v[2]) << 16) |
                  (static_cast<uint32_t>(v[3]) << 24);
      return true;
    }
    off += tlv_len;
  }
  return false;
}

bool parsePairedFlagPayload(const uint8_t* payload, size_t len, bool& out_paired) {
  size_t off = 0;
  while (off + 4 <= len) {
    const uint8_t tag = payload[off + 0];
    const uint8_t type = payload[off + 1];
    const uint16_t tlv_len = static_cast<uint16_t>(payload[off + 2]) |
                             (static_cast<uint16_t>(payload[off + 3]) << 8);
    off += 4;
    if ((off + tlv_len) > len) {
      return false;
    }
    if (tag == 0x40 && type == 0x01 && tlv_len == 1) {
      out_paired = (payload[off] == 1);
      return true;
    }
    off += tlv_len;
  }
  return false;
}

bool encodePairInitPayload(const PairSeed& seed, uint32_t nonce, uint8_t profile_id, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(32);
  uint8_t schema = 1;
  if (!appendTlv(out, 0x01, 0x01, &schema, 1)) {
    return false;
  }
  if (!appendTlv(out, 0x10, 0x08, seed.bytes.data(), static_cast<uint16_t>(seed.bytes.size()))) {
    return false;
  }
  if (!appendTlvU32(out, 0x11, nonce)) {
    return false;
  }
  if (!appendTlvU8(out, 0x12, profile_id)) {
    return false;
  }
  return true;
}

bool encodePairInitAckPayload(uint32_t nonce, std::vector<uint8_t>& out) {
  out.clear();
  return appendTlvU32(out, 0x20, nonce);
}

bool encodePairConfirmPayload(uint32_t nonce, std::vector<uint8_t>& out) {
  out.clear();
  return appendTlvU32(out, 0x30, nonce);
}

bool encodePairConfirmAckPayload(std::vector<uint8_t>& out) {
  out.clear();
  return appendTlvU8(out, 0x40, 1);
}

bool encodeChannelPreparePayload(const ChannelSwitchPayload& in, std::vector<uint8_t>& out) {
  out.clear();
  if (!appendTlvU8(out, 0x10, in.new_channel)) {
    return false;
  }
  return appendTlvU32(out, 0x11, in.effective_at_ms);
}
}  // namespace

constexpr uint32_t kPairInitAckTimeoutMs = 1200;
constexpr uint32_t kSecureGuardDelayMs = 150;
constexpr uint32_t kPairConfirmAckTimeoutMs = 1500;
constexpr uint32_t kPairTotalTimeoutMs = 4000;
constexpr uint32_t kSlavePairHoldTimeoutMs = 2500;

PairingEngine::PairingEngine(const ManagerConfig& config,
                             ITransport& transport,
                             PairingStore* store,
                             IEventSink* events,
                             IPlatformHooks* hooks,
                             IHardwarePeerWindow* peer_window,
                             ITimeSource* time_source,
                             LibraryLogger* logger)
    : config_(config),
      transport_(transport),
      store_(store),
      events_(events),
      hooks_(hooks),
      peer_window_(peer_window),
      time_source_(time_source),
      logger_(logger) {
  tx_wrapped_payload_scratch_.reserve(ProtocolCodec::kMaxPayload);
  tx_encoded_frame_scratch_.reserve(ProtocolCodec::kMaxFrameBytes);
}

void PairingEngine::setLogger(LibraryLogger* logger) {
  logger_ = logger;
}

void PairingEngine::setPeerWindow(IHardwarePeerWindow* peer_window) {
  peer_window_ = peer_window;
}

void PairingEngine::setLocalMac(const MacAddress& local) { local_mac_ = local; }

void PairingEngine::setLocalProfileId(uint8_t profile_id) {
  local_profile_id_ = (profile_id == 0) ? 1 : profile_id;
}

void PairingEngine::setPaired(bool paired) {
  state_ = paired ? LinkState::Secured : LinkState::Unpaired;
  if (paired) {
    pair_phase_ = PairPhase::Idle;
    pair_deadline_ms_ = 0;
    pair_total_deadline_ms_ = 0;
  }
}

bool PairingEngine::isPaired() const { return state_ == LinkState::Secured; }

bool PairingEngine::getPairedPeer(MacAddress& out_peer) const {
  if (state_ != LinkState::Secured) {
    return false;
  }
  out_peer = peer_mac_;
  return true;
}

const MacAddress& PairingEngine::localMac() const { return local_mac_; }

bool PairingEngine::attachPeer(const MacAddress& mac, bool encrypted, const LmkKey* lmk) {
  if (peer_window_ != nullptr) {
    return peer_window_->requestAttach(mac, encrypted, lmk);
  }
  return transport_.addPeer(mac, encrypted, lmk);
}

bool PairingEngine::evictPeer(const MacAddress& mac) {
  if (peer_window_ != nullptr) {
    return peer_window_->requestEvict(mac);
  }
  return transport_.removePeer(mac);
}

bool PairingEngine::tick(uint32_t now_ms) {
  current_now_ms_ = now_ms;
  // Unpaired slave periodically advertises discovery.
  if (config_.local_role == Role::Slave && state_ == LinkState::Unpaired) {
    if (discovery_last_sent_ms_ == 0 || (now_ms - discovery_last_sent_ms_) >= config_.discovery_interval_ms) {
      discovery_last_sent_ms_ = now_ms;
      uint8_t disc[14] = {0};
      disc[0] = kProtocolVersion;
      disc[1] = 0;
      disc[2] = 2;
      disc[3] = 2;
      disc[4] = 1;
      const char* name = config_.discovery_name;
      uint8_t name_id = 0;
      if (name != nullptr) {
        for (size_t i = 0; name[i] != '\0'; ++i) {
          name_id ^= static_cast<uint8_t>(name[i]);
        }
      }
      disc[5] = name_id;
      // Expose slave profile role code in discovery beacons for frontend/UI typing.
      disc[6] = local_profile_id_;
      sendControl(kBroadcastMac, MessageType::Discovery, discovery_counter_++, disc, sizeof(disc));
    }
  }

  if (state_ == LinkState::Pairing) {
    if (pair_last_tick_ms_ == 0) {
      pair_last_tick_ms_ = now_ms;
    } else {
      const uint32_t elapsed = now_ms - pair_last_tick_ms_;
      pair_last_tick_ms_ = now_ms;

      if (pair_total_deadline_ms_ != 0) {
        if (elapsed >= pair_total_deadline_ms_) {
          failPairing(peer_mac_, corr_id_, "pair total timeout");
          return false;
        }
        pair_total_deadline_ms_ -= elapsed;
      }

      if (pair_deadline_ms_ != 0) {
        if (elapsed >= pair_deadline_ms_) {
          if (pair_phase_ == PairPhase::MasterSecureGuard) {
            std::vector<uint8_t> pair_confirm;
            if (!encodePairConfirmPayload(pairing_nonce_, pair_confirm)) {
              failPairing(peer_mac_, corr_id_, "pair confirm tlv encode failed on master");
              return false;
            }
            if (!sendControl(peer_mac_,
                             MessageType::PairConfirm,
                             corr_id_,
                             pair_confirm.data(),
                             pair_confirm.size())) {
              failPairing(peer_mac_, corr_id_, "pair confirm send failed on master");
              return false;
            }
            pair_phase_ = PairPhase::MasterWaitConfirmAck;
            pair_deadline_ms_ = kPairConfirmAckTimeoutMs;
            pair_last_tick_ms_ = 0;
            emit(Event::Type::PairingStep, peer_mac_, corr_id_, "pair_confirm_tx_secure");
            emitPairingLog(LibraryLogLevel::Info,
                           kLogEvtPairConfirmTx,
                           peer_mac_,
                           corr_id_,
                           static_cast<int32_t>(pairing_nonce_),
                           0);
          } else {
            const char* reason = "pair timeout";
            if (pair_phase_ == PairPhase::MasterWaitInitAck) {
              reason = "pair init ack timeout";
            } else if (pair_phase_ == PairPhase::MasterWaitConfirmAck) {
              reason = "pair confirm ack timeout";
            } else if (pair_phase_ == PairPhase::SlaveWaitConfirm) {
              reason = "pair confirm wait timeout on slave";
            }
            failPairing(peer_mac_, corr_id_, reason);
            return false;
          }
        } else {
          pair_deadline_ms_ -= elapsed;
        }
      }
    }
  }

  if (channel_state_ != ChannelSwitchState::Idle && channel_deadline_ms_ != 0 && now_ms > channel_deadline_ms_) {
    emit(Event::Type::ChannelSwitchFailed, peer_mac_, channel_corr_id_, "channel switch timeout");
    channel_state_ = ChannelSwitchState::Idle;
    return false;
  }

  if (channel_state_ == ChannelSwitchState::SlavePrepared && now_ms >= channel_effective_at_ms_) {
    if (!transport_.setChannel(pending_channel_)) {
      emit(Event::Type::ChannelSwitchFailed, peer_mac_, channel_corr_id_, "slave channel apply failed");
      channel_state_ = ChannelSwitchState::Idle;
      return false;
    }

    if (store_ != nullptr) {
      store_->saveChannel(local_mac_, peer_mac_, pending_channel_);
    }

    if (hooks_ != nullptr) {
      hooks_->onChannelCommitted(pending_channel_);
    }

    const bool sent = sendControl(peer_mac_, MessageType::ChannelSwitchCommitAck, channel_corr_id_, nullptr, 0);
    channel_state_ = ChannelSwitchState::Idle;
    if (sent) {
      emit(Event::Type::ChannelSwitchCommitted, peer_mac_, channel_corr_id_, "slave channel committed");
    }
    return sent;
  }

  if (channel_state_ == ChannelSwitchState::MasterWaitCommit && now_ms >= channel_effective_at_ms_) {
    if (!transport_.setChannel(pending_channel_)) {
      emit(Event::Type::ChannelSwitchFailed, peer_mac_, channel_corr_id_, "master channel apply failed");
      channel_state_ = ChannelSwitchState::Idle;
      return false;
    }
  }

  if (unpair_pending_ && unpair_deadline_ms_ != 0) {
    if (unpair_last_tick_ms_ == 0) {
      unpair_last_tick_ms_ = now_ms;
    } else {
      const uint32_t elapsed = now_ms - unpair_last_tick_ms_;
      unpair_last_tick_ms_ = now_ms;
      if (elapsed >= unpair_deadline_ms_) {
        unpair_pending_ = false;
        unpair_deadline_ms_ = 0;
        unpair_last_tick_ms_ = 0;
        emit(Event::Type::PairingStep, peer_mac_, unpair_corr_id_, "unpair timeout; still paired");
        emitPairingLog(LibraryLogLevel::Warn,
                       kLogEvtUnpairTimeout,
                       peer_mac_,
                       unpair_corr_id_,
                       0,
                       0);
      } else {
        unpair_deadline_ms_ -= elapsed;
      }
    }
  }

  if (deferred_unpair_cleanup_ && deferred_unpair_deadline_ms_ != 0) {
    if (deferred_unpair_last_tick_ms_ == 0) {
      deferred_unpair_last_tick_ms_ = now_ms;
    } else {
      const uint32_t elapsed = now_ms - deferred_unpair_last_tick_ms_;
      deferred_unpair_last_tick_ms_ = now_ms;
      if (elapsed >= deferred_unpair_deadline_ms_) {
        if (store_ != nullptr) {
          store_->erasePair(local_mac_, deferred_unpair_peer_);
          store_->eraseChannel(local_mac_, deferred_unpair_peer_);
        }
        evictPeer(deferred_unpair_peer_);
        setPaired(false);
        resetPairing();
        discovery_last_sent_ms_ = 0;
        emit(Event::Type::PairingStep,
             deferred_unpair_peer_,
             deferred_unpair_corr_id_,
             "unpaired by request");
        deferred_unpair_cleanup_ = false;
        deferred_unpair_deadline_ms_ = 0;
        deferred_unpair_last_tick_ms_ = 0;
        deferred_unpair_corr_id_ = 0;
      } else {
        deferred_unpair_deadline_ms_ -= elapsed;
      }
    }
  }

  return true;
}
bool PairingEngine::onDiscovery(const MacAddress& from, uint32_t corr_id) {
  if (config_.local_role != Role::Master || state_ != LinkState::Unpaired) {
    return false;
  }

  // Discovery is always accepted as visibility signal. Pairing can be manual.
  if (!config_.auto_pair_on_discovery) {
    emit(Event::Type::PairingStep, from, corr_id, "discovery seen (manual pair mode)");
    return true;
  }

  return requestPair(from, corr_id);
}

bool PairingEngine::requestPair(const MacAddress& peer, uint32_t corr_id) {
  if (config_.local_role != Role::Master) {
    return false;
  }
  if (state_ == LinkState::Pairing || unpair_pending_) {
    return false;
  }

  peer_mac_ = peer;
  corr_id_ = corr_id;
  state_ = LinkState::Pairing;
  pair_phase_ = PairPhase::MasterWaitInitAck;
  pair_deadline_ms_ = kPairInitAckTimeoutMs;
  pair_total_deadline_ms_ = kPairTotalTimeoutMs;
  pair_last_tick_ms_ = 0;
  pairing_nonce_ = 0;

  if (!attachPeer(peer, false, nullptr)) {
    failPairing(peer, corr_id, "unsecure peer add failed on master");
    return false;
  }

  makePairSeed(corr_id, local_mac_, peer, current_seed_);
  pairing_nonce_ = static_cast<uint32_t>(esp_random());
  // Pair-init profile is a target-profile hint for the slave.
  // Master uses 0 as wildcard because it may pair with multiple slave profiles.
  const uint8_t pair_init_profile_id =
      (config_.local_role == Role::Master) ? static_cast<uint8_t>(0U) : local_profile_id_;
  std::vector<uint8_t> pair_init_payload;
  if (!encodePairInitPayload(current_seed_, pairing_nonce_, pair_init_profile_id, pair_init_payload)) {
    failPairing(peer, corr_id, "pair init tlv encode failed");
    return false;
  }

  emit(Event::Type::PairingStarted, peer, corr_id, "pair_init_tx_unsecure");
  emitPairingLog(LibraryLogLevel::Info,
                 kLogEvtPairReqTx,
                 peer,
                 corr_id,
                 static_cast<int32_t>(pairing_nonce_),
                 static_cast<int32_t>(pair_init_profile_id));
  if (!sendControl(peer,
                   MessageType::PairInit,
                   corr_id,
                   pair_init_payload.data(),
                   pair_init_payload.size())) {
    failPairing(peer, corr_id, "pair init send failed");
    return false;
  }

  return true;
}
bool PairingEngine::onPairInit(const MacAddress& from, uint32_t corr_id, const PairSeed& seed, uint32_t pairing_nonce) {
  if (config_.local_role != Role::Slave || state_ == LinkState::Secured) {
    return false;
  }

  if (state_ == LinkState::Pairing && (from != peer_mac_ || corr_id != corr_id_)) {
    (void)attachPeer(from, false, nullptr);
    const uint8_t busy_reason = 1;
    (void)sendControl(from, MessageType::PairBusy, corr_id, &busy_reason, 1);
    emit(Event::Type::PairingStep, from, corr_id, "busy_pairing_reject");
    emitPairingLog(LibraryLogLevel::Warn, kLogEvtBusyReject, from, corr_id, 1, 0);
    return true;
  }

  peer_mac_ = from;
  corr_id_ = corr_id;
  current_seed_ = seed;
  pairing_nonce_ = pairing_nonce;
  state_ = LinkState::Pairing;
  pair_phase_ = PairPhase::SlaveWaitConfirm;
  pair_deadline_ms_ = kSlavePairHoldTimeoutMs;
  pair_total_deadline_ms_ = kPairTotalTimeoutMs;
  pair_last_tick_ms_ = 0;

  emit(Event::Type::PairingStep, from, corr_id, "pair_init_rx_unsecure");
  emitPairingLog(LibraryLogLevel::Info, kLogEvtPairInitRx, from, corr_id, static_cast<int32_t>(pairing_nonce), 0);

  if (!attachPeer(from, false, nullptr)) {
    failPairing(from, corr_id, "unsecure peer add failed on slave");
    return false;
  }

  std::vector<uint8_t> pair_init_ack;
  if (!encodePairInitAckPayload(pairing_nonce_, pair_init_ack)) {
    failPairing(from, corr_id, "pair init ack tlv encode failed on slave");
    return false;
  }
  if (!sendControl(from,
                   MessageType::PairInitAck,
                   corr_id,
                   pair_init_ack.data(),
                   pair_init_ack.size())) {
    failPairing(from, corr_id, "pair init ack send failed on slave");
    return false;
  }
  emit(Event::Type::PairingStep, from, corr_id, "pair_init_ack_tx_unsecure");
  emitPairingLog(LibraryLogLevel::Info, kLogEvtPairAckTx, from, corr_id, static_cast<int32_t>(pairing_nonce), 0);

  if (!deriveLmkFromSeed(current_seed_, from, local_mac_, pairing_nonce_, current_lmk_)) {
    failPairing(from, corr_id, "lmk derive failed on slave (prepare)");
    return false;
  }

  (void)evictPeer(from);
  if (!attachPeer(from, true, &current_lmk_)) {
    failPairing(from, corr_id, "secure peer add failed on slave (prepare)");
    return false;
  }

  emit(Event::Type::PairingStep, from, corr_id, "secure_peer_install_done");
  emitPairingLog(LibraryLogLevel::Info, kLogEvtSecurePeerInstall, from, corr_id, 0, 0);
  return true;
}
bool PairingEngine::onPairInitAck(const MacAddress& from, uint32_t corr_id, uint32_t pairing_nonce_echo) {
  if (config_.local_role != Role::Master || state_ != LinkState::Pairing ||
      pair_phase_ != PairPhase::MasterWaitInitAck || corr_id != corr_id_) {
    return false;
  }
  if (pairing_nonce_echo != pairing_nonce_) {
    failPairing(from, corr_id, "pair init ack nonce mismatch");
    return false;
  }

  (void)evictPeer(from);

  if (!deriveLmkFromSeed(current_seed_, local_mac_, from, pairing_nonce_, current_lmk_)) {
    failPairing(from, corr_id, "lmk derive failed on master");
    return false;
  }

  if (!attachPeer(from, true, &current_lmk_)) {
    failPairing(from, corr_id, "secure peer add failed on master");
    return false;
  }

  pair_phase_ = PairPhase::MasterSecureGuard;
  pair_deadline_ms_ = kSecureGuardDelayMs;
  pair_last_tick_ms_ = 0;
  emit(Event::Type::PairingStep, from, corr_id, "secure_guard_wait_150ms");
  emitPairingLog(LibraryLogLevel::Info, kLogEvtSecurePeerInstall, from, corr_id, 1, 0);
  return true;
}
bool PairingEngine::onPairConfirm(const MacAddress& from, uint32_t corr_id, uint32_t pairing_nonce_echo) {
  if (config_.local_role != Role::Slave || state_ != LinkState::Pairing ||
      pair_phase_ != PairPhase::SlaveWaitConfirm || corr_id != corr_id_) {
    return false;
  }
  if (pairing_nonce_echo != pairing_nonce_) {
    failPairing(from, corr_id, "pair confirm nonce mismatch on slave");
    return false;
  }

  if (!deriveLmkFromSeed(current_seed_, from, local_mac_, pairing_nonce_, current_lmk_)) {
    failPairing(from, corr_id, "lmk derive failed on slave");
    return false;
  }

  (void)evictPeer(from);
  if (!attachPeer(from, true, &current_lmk_)) {
    failPairing(from, corr_id, "secure peer add failed on slave");
    return false;
  }

  state_ = LinkState::Secured;
  pair_phase_ = PairPhase::Idle;
  pair_deadline_ms_ = 0;
  pair_total_deadline_ms_ = 0;
  pair_last_tick_ms_ = 0;
  if (!persistPair()) {
    (void)evictPeer(from);
    failPairing(from, corr_id, "persist failed on slave");
    return false;
  }
  emit(Event::Type::Paired, from, corr_id, "pair_complete_persisted");
  emitPairingLog(LibraryLogLevel::Info, kLogEvtPairComplete, from, corr_id, 1, 0);

  std::vector<uint8_t> pair_confirm_ack;
  if (!encodePairConfirmAckPayload(pair_confirm_ack)) {
    failPairing(from, corr_id, "pair confirm ack tlv encode failed on slave");
    return false;
  }
  return sendControl(from,
                     MessageType::PairConfirmAck,
                     corr_id,
                     pair_confirm_ack.data(),
                     pair_confirm_ack.size());
}

bool PairingEngine::onPairConfirmAck(const MacAddress& from, uint32_t corr_id, bool paired_flag) {
  if (config_.local_role != Role::Master || state_ != LinkState::Pairing ||
      pair_phase_ != PairPhase::MasterWaitConfirmAck || corr_id != corr_id_) {
    return false;
  }
  if (!paired_flag) {
    failPairing(from, corr_id, "pair confirm ack missing paired flag");
    return false;
  }

  state_ = LinkState::Secured;
  pair_phase_ = PairPhase::Idle;
  pair_deadline_ms_ = 0;
  pair_total_deadline_ms_ = 0;
  pair_last_tick_ms_ = 0;
  pairing_nonce_ = 0;
  if (!persistPair()) {
    (void)evictPeer(from);
    failPairing(from, corr_id, "persist failed on master");
    return false;
  }
  emit(Event::Type::Paired, from, corr_id, "pair_complete_persisted");
  emitPairingLog(LibraryLogLevel::Info, kLogEvtPairComplete, from, corr_id, 2, 0);
  return true;
}

bool PairingEngine::onPairBusy(const MacAddress& from, uint32_t corr_id) {
  if (config_.local_role != Role::Master || state_ != LinkState::Pairing || corr_id != corr_id_) {
    return false;
  }

  emit(Event::Type::PairingFailed, from, corr_id, "busy_pairing");
  emitPairingLog(LibraryLogLevel::Warn, kLogEvtBusyReject, from, corr_id, 2, 0);
  (void)evictPeer(from);
  resetPairing();
  return false;
}
bool PairingEngine::restorePairedLink(const PairRecord& record) {
  if (!record.valid || record.local_role != config_.local_role || record.local_mac != local_mac_) {
    return false;
  }

  if (!transport_.setChannel(record.channel)) {
    return false;
  }

  if (hooks_ != nullptr) {
    hooks_->onChannelCommitted(record.channel);
  }

  if (!attachPeer(record.peer_mac, true, &record.lmk)) {
    return false;
  }

  peer_mac_ = record.peer_mac;
  current_lmk_ = record.lmk;
  state_ = LinkState::Secured;
  pair_phase_ = PairPhase::Idle;
  corr_id_ = 0;
  pair_deadline_ms_ = 0;
  pair_total_deadline_ms_ = 0;
  pairing_nonce_ = 0;
  discovery_last_sent_ms_ = 0;
  emit(Event::Type::PairingStep, peer_mac_, 0, "paired link restored from persistence");
  emitPairingLog(LibraryLogLevel::Info,
                 kLogEvtRestore,
                 peer_mac_,
                 0,
                 static_cast<int32_t>(record.channel),
                 static_cast<int32_t>(config_.local_role));
  return true;
}

bool PairingEngine::requestUnpair(const MacAddress& peer, uint32_t corr_id) {
  if (config_.local_role != Role::Master || state_ != LinkState::Secured || unpair_pending_) {
    return false;
  }

  peer_mac_ = peer;
  unpair_pending_ = true;
  unpair_corr_id_ = corr_id;
  unpair_deadline_ms_ = config_.unpair_ack_timeout_ms;
  unpair_last_tick_ms_ = 0;

  if (!sendControl(peer, MessageType::UnpairRequest, corr_id, nullptr, 0)) {
    unpair_pending_ = false;
    unpair_corr_id_ = 0;
    unpair_deadline_ms_ = 0;
    unpair_last_tick_ms_ = 0;
    return false;
  }
  emitPairingLog(LibraryLogLevel::Info, kLogEvtUnpairReq, peer, corr_id, 0, 0);

  return true;
}

bool PairingEngine::onUnpairRequest(const MacAddress& from, uint32_t corr_id) {
  if (state_ != LinkState::Secured) {
    return false;
  }

  if (!sendControl(from, MessageType::UnpairAck, corr_id, nullptr, 0)) {
    return false;
  }

  // Defer local teardown to give UnpairAck time to leave radio queue.
  deferred_unpair_cleanup_ = true;
  deferred_unpair_peer_ = from;
  deferred_unpair_corr_id_ = corr_id;
  deferred_unpair_deadline_ms_ = config_.unpair_ack_grace_ms;
  deferred_unpair_last_tick_ms_ = 0;
  return true;
}

bool PairingEngine::onUnpairAck(const MacAddress& from, uint32_t corr_id) {
  if (!unpair_pending_ || corr_id != unpair_corr_id_) {
    return false;
  }

  if (store_ != nullptr) {
    store_->erasePair(local_mac_, from);
    store_->eraseChannel(local_mac_, from);
  }
  evictPeer(from);
  setPaired(false);
  resetPairing();
  unpair_pending_ = false;
  unpair_corr_id_ = 0;
  unpair_deadline_ms_ = 0;
  unpair_last_tick_ms_ = 0;
  emit(Event::Type::PairingStep, from, corr_id, "unpair ack received");
  emitPairingLog(LibraryLogLevel::Info, kLogEvtUnpairAck, from, corr_id, 0, 0);
  return true;
}

bool PairingEngine::removePeer(const MacAddress& peer, bool erase_persisted) {
  const bool removed = evictPeer(peer);

  if (erase_persisted && store_ != nullptr) {
    store_->erasePair(local_mac_, peer);
    store_->eraseChannel(local_mac_, peer);
  }

  if (state_ == LinkState::Secured && peer == peer_mac_) {
    setPaired(false);
    resetPairing();
    discovery_last_sent_ms_ = 0;
  }

  emit(Event::Type::PairingStep, peer, 0, "peer removed locally");
  return removed;
}

bool PairingEngine::requestChannelSwitch(const MacAddress& peer,
                                         uint8_t new_channel,
                                         uint32_t now_ms,
                                         uint32_t corr_id) {
  if (config_.local_role != Role::Master || state_ != LinkState::Secured || channel_state_ != ChannelSwitchState::Idle) {
    return false;
  }

  if (new_channel < 1 || new_channel > 14) {
    return false;
  }

  if (hooks_ != nullptr) {
    const uint8_t current_channel = transport_.getChannel();
    if (!hooks_->allowChannelSwitch(current_channel, new_channel)) {
      emit(Event::Type::ChannelSwitchFailed, peer, corr_id, "channel switch denied by hook");
      return false;
    }
  }

  ChannelSwitchPayload payload;
  payload.new_channel = new_channel;
  payload.effective_at_ms = now_ms + config_.channel_switch_grace_ms;

  std::vector<uint8_t> channel_payload;
  if (!encodeChannelPreparePayload(payload, channel_payload)) {
    return false;
  }

  if (!sendControl(peer,
                   MessageType::ChannelSwitchPrepare,
                   corr_id,
                   channel_payload.data(),
                   channel_payload.size())) {
    return false;
  }

  peer_mac_ = peer;
  pending_channel_ = new_channel;
  channel_corr_id_ = corr_id;
  channel_effective_at_ms_ = payload.effective_at_ms;
  channel_deadline_ms_ = now_ms + config_.channel_switch_timeout_ms;
  channel_state_ = ChannelSwitchState::MasterWaitAck;
  emit(Event::Type::ChannelSwitchStarted, peer, corr_id, "prepare sent");
  return true;
}

bool PairingEngine::onChannelSwitchPrepare(const MacAddress& from,
                                           uint32_t corr_id,
                                           const ChannelSwitchPayload& payload) {
  if (config_.local_role != Role::Slave || state_ != LinkState::Secured) {
    return false;
  }

  if (payload.new_channel < 1 || payload.new_channel > 14) {
    emit(Event::Type::ChannelSwitchFailed, from, corr_id, "invalid requested channel");
    return false;
  }

  if (hooks_ != nullptr) {
    const uint8_t current_channel = transport_.getChannel();
    if (!hooks_->allowChannelSwitch(current_channel, payload.new_channel)) {
      emit(Event::Type::ChannelSwitchFailed, from, corr_id, "channel switch denied by hook");
      return false;
    }
  }

  peer_mac_ = from;
  pending_channel_ = payload.new_channel;
  channel_corr_id_ = corr_id;
  channel_effective_at_ms_ = payload.effective_at_ms;
  channel_deadline_ms_ = payload.effective_at_ms + config_.channel_switch_timeout_ms;
  channel_state_ = ChannelSwitchState::SlavePrepared;

  emit(Event::Type::ChannelSwitchStarted, from, corr_id, "slave prepared");
  return sendControl(from, MessageType::ChannelSwitchAck, corr_id, nullptr, 0);
}

bool PairingEngine::onChannelSwitchAck(const MacAddress& from, uint32_t corr_id) {
  if (config_.local_role != Role::Master || channel_state_ != ChannelSwitchState::MasterWaitAck || corr_id != channel_corr_id_) {
    return false;
  }

  emit(Event::Type::PairingStep, from, corr_id, "channel switch ack received");
  channel_state_ = ChannelSwitchState::MasterWaitCommit;
  return true;
}

bool PairingEngine::onChannelSwitchCommitAck(const MacAddress& from, uint32_t corr_id) {
  if (config_.local_role != Role::Master || channel_state_ != ChannelSwitchState::MasterWaitCommit || corr_id != channel_corr_id_) {
    return false;
  }

  if (store_ != nullptr) {
    store_->saveChannel(local_mac_, from, pending_channel_);
  }

  if (hooks_ != nullptr) {
    hooks_->onChannelCommitted(pending_channel_);
  }

  emit(Event::Type::ChannelSwitchCommitted, from, corr_id, "master switch confirmed");
  channel_state_ = ChannelSwitchState::Idle;
  return true;
}

bool PairingEngine::shouldAttachTimeSync(uint32_t now_ms) const {
  if (!config_.time_sync_enabled || config_.local_role != Role::Master || time_source_ == nullptr) {
    return false;
  }

  if (!has_time_sync_tx_) {
    return true;
  }

  if (config_.time_sync_interval_ms == 0) {
    return true;
  }

  return (now_ms - last_time_sync_tx_ms_) >= config_.time_sync_interval_ms;
}
bool PairingEngine::sendControl(const MacAddress& to,
                                MessageType type,
                                uint32_t corr_id,
                                const uint8_t* payload,
                                size_t payload_len) {
  FrameHeader h;
  h.version = kProtocolVersion;
  h.type = type;
  h.flags = 0;
  h.correlation_id = corr_id;
  h.role = config_.local_role;

  tx_wrapped_payload_scratch_.clear();
  if (shouldAttachTimeSync(current_now_ms_) && payload_len <= (ProtocolCodec::kMaxPayload - kTimeSyncMetadataSize)) {
    uint64_t epoch_s = 0;
    if (time_source_->nowEpochSec(epoch_s)) {
      tx_wrapped_payload_scratch_.resize(kTimeSyncMetadataSize + payload_len);
      const uint32_t epoch_s32 = static_cast<uint32_t>(epoch_s & 0xFFFFFFFFULL);
      tx_wrapped_payload_scratch_[0] = static_cast<uint8_t>(epoch_s32 & 0xFFU);
      tx_wrapped_payload_scratch_[1] = static_cast<uint8_t>((epoch_s32 >> 8) & 0xFFU);
      tx_wrapped_payload_scratch_[2] = static_cast<uint8_t>((epoch_s32 >> 16) & 0xFFU);
      tx_wrapped_payload_scratch_[3] = static_cast<uint8_t>((epoch_s32 >> 24) & 0xFFU);
      if (payload_len > 0 && payload != nullptr) {
        for (size_t i = 0; i < payload_len; ++i) {
          tx_wrapped_payload_scratch_[kTimeSyncMetadataSize + i] = payload[i];
        }
      }
      h.flags |= kFrameFlagTimeSyncEpoch;
      has_time_sync_tx_ = true;
      last_time_sync_tx_ms_ = current_now_ms_;
    }
  }

  const uint8_t* wire_payload = payload;
  size_t wire_len = payload_len;
  if (!tx_wrapped_payload_scratch_.empty()) {
    wire_payload = tx_wrapped_payload_scratch_.data();
    wire_len = tx_wrapped_payload_scratch_.size();
  }

  h.payload_length = static_cast<uint16_t>(wire_len);

  tx_encoded_frame_scratch_.clear();
  if (!ProtocolCodec::encode(h, wire_payload, wire_len, tx_encoded_frame_scratch_)) {
    if (hooks_ != nullptr) {
      hooks_->onTxFrame(to, type, corr_id, wire_len, false);
    }
    return false;
  }

  const bool ok = transport_.send(to,
                                  tx_encoded_frame_scratch_.data(),
                                  tx_encoded_frame_scratch_.size());
  if (hooks_ != nullptr) {
    hooks_->onTxFrame(to, type, corr_id, wire_len, ok);
  }
  return ok;
}

void PairingEngine::resetPairing() {
  state_ = LinkState::Unpaired;
  pair_phase_ = PairPhase::Idle;
  corr_id_ = 0;
  pair_deadline_ms_ = 0;
  pair_total_deadline_ms_ = 0;
  pair_last_tick_ms_ = 0;
  pairing_nonce_ = 0;
  has_time_sync_tx_ = false;
  last_time_sync_tx_ms_ = 0;
  unpair_pending_ = false;
  unpair_corr_id_ = 0;
  unpair_deadline_ms_ = 0;
  unpair_last_tick_ms_ = 0;
  deferred_unpair_cleanup_ = false;
  deferred_unpair_peer_ = {};
  deferred_unpair_corr_id_ = 0;
  deferred_unpair_deadline_ms_ = 0;
  deferred_unpair_last_tick_ms_ = 0;
}

void PairingEngine::failPairing(const MacAddress& peer, uint32_t corr_id, const char* msg) {
  const int32_t phase = static_cast<int32_t>(pair_phase_);
  const int32_t state = static_cast<int32_t>(state_);
  emitPairingLog(LibraryLogLevel::Error, kLogEvtPairFail, peer, corr_id, phase, state);
  emit(Event::Type::PairingFailed, peer, corr_id, msg);
  if (!(peer == kBroadcastMac)) {
    (void)evictPeer(peer);
  }
  resetPairing();
  discovery_last_sent_ms_ = 0;
}

void PairingEngine::emit(Event::Type type, const MacAddress& peer, uint32_t corr_id, const char* msg) {
  if (events_ != nullptr) {
    events_->onEvent({type, peer, corr_id, msg});
  }

  if (hooks_ == nullptr) {
    return;
  }

  switch (type) {
    case Event::Type::PairingStarted:
    case Event::Type::PairingStep:
      hooks_->onRgbBlink(255, 165, 0, 80);
      break;
    case Event::Type::Paired:
      hooks_->onRgbBlink(0, 255, 0, 150);
      break;
    case Event::Type::PairingFailed:
    case Event::Type::ChannelSwitchFailed:
      hooks_->onRgbBlink(255, 0, 0, 150);
      break;
    case Event::Type::ChannelSwitchStarted:
    case Event::Type::ChannelSwitchCommitted:
      hooks_->onRgbBlink(255, 0, 255, 100);
      break;
    default:
      break;
  }
}

bool PairingEngine::persistPair() {
  if (store_ == nullptr || !store_->enabled() || state_ != LinkState::Secured) {
    return false;
  }

  PairRecord record;
  record.valid = true;
  record.local_role = config_.local_role;
  record.local_mac = local_mac_;
  record.peer_mac = peer_mac_;
  record.lmk = current_lmk_;
  record.channel = transport_.getChannel();

  const bool saved_pair = store_->savePair(record);
  const bool saved_chan = store_->saveChannel(local_mac_, peer_mac_, record.channel);
  return saved_pair && saved_chan;
}

void PairingEngine::emitPairingLog(LibraryLogLevel level,
                                   uint16_t event_id,
                                   const MacAddress& peer,
                                   uint32_t corr_id,
                                   int32_t p1,
                                   int32_t p2) const {
  if (logger_ == nullptr) {
    return;
  }

  LibraryLogRecord rec{};
  rec.level = level;
  rec.source_id = kLogSourcePairing;
  rec.event_id = event_id;
  rec.uptime_ms = current_now_ms_;
  rec.p0 = static_cast<int32_t>(corr_id);
  rec.p1 = p1;
  rec.p2 = p2;
  if (time_source_ != nullptr) {
    uint64_t epoch_s = 0;
    if (time_source_->nowEpochSec(epoch_s)) {
      rec.epoch_s = static_cast<uint32_t>(epoch_s & 0xFFFFFFFFULL);
    }
  }
  rec.ext.assign(peer.begin(), peer.end());
  (void)logger_->log(rec);
}

}  // namespace espnow_link





































