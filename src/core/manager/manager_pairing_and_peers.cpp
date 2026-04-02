/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Pairing lifecycle and persisted peer/channel management logic.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
bool EspNowManager::requestPair(const MacAddress& peer, uint32_t corr_id) {
  const bool ok = pairing_.requestPair(peer, corr_id);
  emitRuntimeLog(ok ? LibraryLogLevel::Info : LibraryLogLevel::Error,
                 kLogEvtPairReq,
                 corr_id,
                 ok ? 1 : 0,
                 0,
                 peer.data(),
                 peer.size());
  if (ok) {
    (void)peer_registry_.markPairing(peer, current_now_ms_);
  }
  return ok;
}

bool EspNowManager::activatePairedPeer(const MacAddress& peer) {
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }

  PairRecord target_record{};
  if (!store_->loadPair(local_mac_, peer, target_record) || !target_record.valid) {
    return false;
  }

  if (target_record.local_role != config_.local_role || target_record.local_mac != local_mac_) {
    return false;
  }

  MacAddress active_peer{};
  const bool had_active = getPairedPeer(active_peer);
  if (had_active && active_peer == peer) {
    return true;
  }

  if (config_.local_role == Role::Slave && had_active && active_peer != peer) {
    (void)clearTelemetryPushConfig(active_peer);
  }

  if (restorePairedLink(target_record)) {
    return true;
  }

  if (config_.local_role == Role::Slave && had_active && active_peer != peer) {
    (void)restoreTelemetryPushConfig(active_peer);
  }
  return false;
}

bool EspNowManager::restorePairedLink(const PairRecord& record) {
  if (!pairing_.restorePairedLink(record)) {
    return false;
  }
  paired_cache_valid_ = true;
  paired_cache_state_ = true;
  paired_cache_peer_ = record.peer_mac;
  if (config_.local_role == Role::Slave) {
    (void)restoreTelemetryPushConfig(record.peer_mac);
    setTopologyRestoreStatus_(kTopologyRestoreModeNone, "topology_not_committed");
    if (has_topology_committed_) {
      std::string topology_error{};
      bool topology_ok = false;
      bool used_nvs_restore = false;
      bool used_derive_restore = false;

      TopologyPersistedLmkMeta_ meta{};
      std::string load_error{};
      if (loadTopologyLmkMeta_(meta, &load_error)) {
        std::string validate_error{};
        if (validateTopologyLmkMeta_(meta, topology_committed_, record.peer_mac, &validate_error)) {
          std::map<MacAddress, LmkKey> desired{};
          std::string desired_error{};
          if (buildDesiredTopologyPeersFromNvs_(topology_committed_, meta, desired, &desired_error)) {
            topology_ok = applyTopologyDesiredPeers_(desired, &topology_error);
            used_nvs_restore = topology_ok;
            if (topology_ok) {
              setTopologyRestoreStatus_(kTopologyRestoreModeNvs, "topology_lmk_nvs_hit");
              emitTopologyRuntimeLog_(LibraryLogLevel::Info,
                                      kLogEvtTopologyLmkRestore,
                                      static_cast<int32_t>(topology_committed_.enabled_slot_count),
                                      static_cast<int32_t>(topology_committed_.topology_version),
                                      "topology_lmk_nvs_hit");
            }
          } else {
            topology_error = desired_error;
          }
        } else {
          topology_error = validate_error;
        }
      } else {
        topology_error = load_error;
      }

      if (!topology_ok) {
        emitTopologyRuntimeLog_(LibraryLogLevel::Warn,
                                kLogEvtTopologyLmkNvsLifecycle,
                                static_cast<int32_t>(topology_committed_.enabled_slot_count),
                                static_cast<int32_t>(topology_committed_.topology_version),
                                topology_error.empty() ? "topology_lmk_nvs_miss" : topology_error.c_str());
        std::string derive_error{};
        topology_ok = materializeTopologyPeers_(topology_committed_, &derive_error);
        if (topology_ok) {
          used_derive_restore = true;
          setTopologyRestoreStatus_(kTopologyRestoreModeDerived, "topology_lmk_nvs_fallback_derive");
          emitTopologyRuntimeLog_(LibraryLogLevel::Info,
                                  kLogEvtTopologyLmkRestore,
                                  static_cast<int32_t>(topology_committed_.enabled_slot_count),
                                  static_cast<int32_t>(topology_committed_.topology_version),
                                  "topology_lmk_nvs_fallback_derive");
          const bool nvs_saved = saveTopologyLmkMeta_(topology_committed_);
          if (!nvs_saved) {
            emitTopologyRuntimeLog_(LibraryLogLevel::Warn,
                                    kLogEvtTopologyLmkNvsLifecycle,
                                    static_cast<int32_t>(topology_committed_.enabled_slot_count),
                                    static_cast<int32_t>(topology_committed_.topology_version),
                                    "topology_lmk_nvs_write_fail");
          }
        } else if (!derive_error.empty()) {
          topology_error = derive_error;
        }
      }

      if (!topology_ok) {
        setTopologyRestoreStatus_(kTopologyRestoreModeFailed,
                                  topology_error.empty() ? "topology_lmk_restore_fail" : topology_error.c_str());
        emitTopologyRuntimeLog_(LibraryLogLevel::Warn,
                                kLogEvtTopologyLmkRestore,
                                static_cast<int32_t>(topology_committed_.enabled_slot_count),
                                static_cast<int32_t>(topology_committed_.topology_version),
                                topology_error.empty() ? "topology_lmk_restore_fail" : topology_error.c_str());
      } else if (!used_nvs_restore && !used_derive_restore) {
        setTopologyRestoreStatus_(kTopologyRestoreModeNone, "topology_not_applied");
      }
    }
  }
  (void)peer_registry_.markPaired(record.peer_mac, current_now_ms_);
  return true;
}

bool EspNowManager::requestUnpair(const MacAddress& peer, uint32_t corr_id) {
  if (config_.local_role == Role::Master) {
    (void)clearTelemetryPushConfig(peer);
  } else {
    mandatory_event_queue_.clear();
  }
  const bool ok = pairing_.requestUnpair(peer, corr_id);
  emitRuntimeLog(ok ? LibraryLogLevel::Info : LibraryLogLevel::Error,
                 kLogEvtUnpairReq,
                 corr_id,
                 ok ? 1 : 0,
                 0,
                 peer.data(),
                 peer.size());
  if (ok && config_.local_role == Role::Slave && store_ != nullptr && store_->enabled()) {
    const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
    emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                            kLogEvtTopologyLmkNvsLifecycle,
                            static_cast<int32_t>(kTopologyMetaSlotLmk),
                            0,
                            erased ? "topology_lmk_nvs_purge_old" : "topology_lmk_nvs_purge_old_failed");
  }
  return ok;
}

bool EspNowManager::removePeer(const MacAddress& peer, bool erase_persisted) {
  if (erase_persisted) {
    (void)clearTelemetryPushConfig(peer);
    if (store_ != nullptr && store_->enabled()) {
      (void)store_->eraseMetaBlob(local_mac_, peer, kPeerRoleMetaSlot);
    }
  }
  mandatory_event_queue_.clear();
  const bool removed = pairing_.removePeer(peer, erase_persisted);
  peer_role_hints_.erase(std::remove_if(peer_role_hints_.begin(),
                                        peer_role_hints_.end(),
                                        [&](const PeerRoleHintEntry& e) {
                                          return e.peer == peer;
                                        }),
                         peer_role_hints_.end());
  if (removed) {
    (void)peer_registry_.removePeer(peer);
    topology_attached_peers_.erase(
        std::remove(topology_attached_peers_.begin(), topology_attached_peers_.end(), peer),
        topology_attached_peers_.end());
    if (erase_persisted && config_.local_role == Role::Slave && store_ != nullptr && store_->enabled()) {
      const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
      emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                              kLogEvtTopologyLmkNvsLifecycle,
                              static_cast<int32_t>(kTopologyMetaSlotLmk),
                              0,
                              erased ? "topology_lmk_nvs_purge_old" : "topology_lmk_nvs_purge_old_failed");
    }
  }
  return removed;
}

bool EspNowManager::isPaired() const { return pairing_.isPaired(); }

bool EspNowManager::getPairedPeer(MacAddress& out_peer) const {
  return pairing_.getPairedPeer(out_peer);
}

bool EspNowManager::hasPersistedPair(const MacAddress& peer) {
  if (store_ == nullptr || !store_->enabled()) {
    MacAddress active{};
    return getPairedPeer(active) && active == peer;
  }

  PairRecord record{};
  if (!store_->loadPair(local_mac_, peer, record) || !record.valid) {
    return false;
  }
  return record.local_role == config_.local_role && record.local_mac == local_mac_;
}

size_t EspNowManager::persistedPairCount() {
  std::vector<MacAddress> peers;
  getPersistedPeers(peers);
  return peers.size();
}

void EspNowManager::getPersistedPeers(std::vector<MacAddress>& out) {
  out.clear();
  if (store_ == nullptr || !store_->enabled()) {
    MacAddress active{};
    if (getPairedPeer(active)) {
      out.push_back(active);
    }
    return;
  }

  std::vector<PairIndexEntry> entries;
  if (store_->loadPairIndex(local_mac_, entries)) {
    struct IndexedPeer {
      MacAddress peer{};
      uint32_t pair_seq = 0;
    };
    std::vector<IndexedPeer> ordered{};
    ordered.reserve(entries.size());

    for (const auto& entry : entries) {
      if (!entry.valid || entry.pair_seq == 0U) {
        continue;
      }
      PairRecord record{};
      if (!store_->loadPair(local_mac_, entry.peer_mac, record) || !record.valid) {
        continue;
      }
      if (record.local_role != config_.local_role || record.local_mac != local_mac_) {
        continue;
      }

      const auto seen = std::find_if(ordered.begin(),
                                     ordered.end(),
                                     [&](const IndexedPeer& existing) {
                                       return existing.peer == entry.peer_mac;
                                     });
      if (seen != ordered.end()) {
        continue;
      }

      IndexedPeer peer_entry{};
      peer_entry.peer = entry.peer_mac;
      peer_entry.pair_seq = entry.pair_seq;
      ordered.push_back(peer_entry);
    }

    std::sort(ordered.begin(), ordered.end(), [](const IndexedPeer& lhs, const IndexedPeer& rhs) {
      if (lhs.pair_seq != rhs.pair_seq) {
        return lhs.pair_seq < rhs.pair_seq;
      }
      return compareMac(lhs.peer, rhs.peer) < 0;
    });

    out.reserve(ordered.size());
    for (const auto& item : ordered) {
      out.push_back(item.peer);
    }
    return;
  }

}

void EspNowManager::getPersistedPeersWithRole(std::vector<PersistedPeerRoleEntry>& out) {
  out.clear();
  std::vector<MacAddress> peers{};
  getPersistedPeers(peers);
  out.reserve(peers.size());
  for (const auto& peer : peers) {
    PersistedPeerRoleEntry entry{};
    entry.peer = peer;
    uint8_t role_code = 0U;
    if (loadPeerRoleHint(peer, role_code)) {
      entry.role_code = role_code;
    }
    out.push_back(entry);
  }
}

void EspNowManager::upsertPeerRoleHintCache_(const MacAddress& peer, uint8_t role_code) {
  if (role_code == 0U) {
    return;
  }
  for (auto& e : peer_role_hints_) {
    if (e.peer == peer) {
      e.role_code = role_code;
      return;
    }
  }
  PeerRoleHintEntry add{};
  add.peer = peer;
  add.role_code = role_code;
  peer_role_hints_.push_back(add);
}

bool EspNowManager::findPeerRoleHintCache_(const MacAddress& peer, uint8_t& out_role_code) const {
  out_role_code = 0U;
  for (const auto& e : peer_role_hints_) {
    if (e.peer == peer) {
      out_role_code = e.role_code;
      return true;
    }
  }
  return false;
}

bool EspNowManager::getPersistedPeerChannels(std::vector<PersistedPeerChannelEntry>& out) const {
  out.clear();
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }

  std::vector<MacAddress> peers{};
  const_cast<EspNowManager*>(this)->getPersistedPeers(peers);
  out.reserve(peers.size());
  for (const auto& peer : peers) {
    uint8_t channel = 0U;
    if (!store_->loadChannel(local_mac_, peer, channel)) {
      continue;
    }
    PersistedPeerChannelEntry entry{};
    entry.peer = peer;
    entry.channel = channel;
    entry.channel_key = store_->makeKey(RecordKind::Channel, local_mac_, peer);
    out.push_back(entry);
  }
  return true;
}

bool EspNowManager::applyRuntimeChannelToAllPeers(uint8_t channel) {
  if (channel < 1U || channel > 14U) {
    return false;
  }
  if (!transport_.setChannel(channel)) {
    return false;
  }
  if (hooks_ != nullptr) {
    hooks_->onChannelCommitted(channel);
  }
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }

  std::vector<MacAddress> peers{};
  getPersistedPeers(peers);
  TopologySnapshot committed{};
  if (getTopologySnapshot(TopologyState::Committed, committed)) {
    for (const auto& slot : committed.slots) {
      if (!slot.enabled) {
        continue;
      }
      if (std::find(peers.begin(), peers.end(), slot.peer) == peers.end()) {
        peers.push_back(slot.peer);
      }
    }
  }
  bool ok = true;
  for (const auto& peer : peers) {
    ok = store_->saveChannel(local_mac_, peer, channel) && ok;
  }
  return ok;
}

void EspNowManager::getPeerRegistrySnapshot(std::vector<PeerRecord>& out) const {
  peer_registry_.snapshot(out);
}

void EspNowManager::syncPairedCacheState_() {
  MacAddress peer{};
  const bool paired = pairing_.isPaired() && pairing_.getPairedPeer(peer);
  paired_cache_valid_ = true;
  paired_cache_state_ = paired;
  paired_cache_peer_ = paired ? peer : MacAddress{};
}

}  // namespace espnow_link

