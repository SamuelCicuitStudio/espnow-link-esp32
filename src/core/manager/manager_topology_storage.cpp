/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Topology persistence, LMK metadata handling, and peer materialization.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
bool EspNowManager::saveTopologySnapshotMeta_(uint8_t slot, const TopologySnapshot& snapshot) {
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }
  std::vector<uint8_t> blob;
  if (!serializeTopologySnapshotBlob(snapshot, blob)) {
    return false;
  }
  return saveLocalMetaBlob(slot, blob.data(), blob.size());
}

bool EspNowManager::loadTopologySnapshotMeta_(uint8_t slot, TopologySnapshot& out_snapshot) const {
  out_snapshot = TopologySnapshot{};
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }
  std::vector<uint8_t> blob;
  if (!loadLocalMetaBlob(slot, blob)) {
    return false;
  }
  return deserializeTopologySnapshotBlob(blob, out_snapshot);
}

bool EspNowManager::eraseTopologySnapshotMeta_(uint8_t slot) {
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }
  return eraseLocalMetaBlob(slot);
}

void EspNowManager::restoreTopologySnapshots_() {
  topology_staged_ = TopologySnapshot{};
  topology_committed_ = TopologySnapshot{};
  has_topology_staged_ = false;
  has_topology_committed_ = false;
  topology_attached_peers_.clear();

  TopologySnapshot committed{};
  if (loadTopologySnapshotMeta_(kTopologyMetaSlotCommitted, committed)) {
    committed.state = TopologyState::Committed;
    std::string err{};
    if (validateTopologySnapshot_(committed, &err)) {
      topology_committed_ = committed;
      has_topology_committed_ = true;
    } else {
      (void)eraseTopologySnapshotMeta_(kTopologyMetaSlotCommitted);
    }
  }

  TopologySnapshot staged{};
  if (loadTopologySnapshotMeta_(kTopologyMetaSlotStaged, staged)) {
    staged.state = TopologyState::Staged;
    std::string err{};
    if (validateTopologySnapshot_(staged, &err)) {
      topology_staged_ = staged;
      has_topology_staged_ = true;
    } else {
      (void)eraseTopologySnapshotMeta_(kTopologyMetaSlotStaged);
    }
  }
}

bool EspNowManager::loadTopologyLmkMeta_(TopologyPersistedLmkMeta_& out_meta,
                                         std::string* out_error) {
  out_meta = TopologyPersistedLmkMeta_{};
  if (store_ == nullptr || !store_->enabled()) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_disabled";
    }
    return false;
  }

  std::vector<uint8_t> blob{};
  if (!loadLocalMetaBlob(kTopologyMetaSlotLmk, blob)) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_miss";
    }
    return false;
  }
  if (blob.size() < 2U) {
    const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
    emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                            kLogEvtTopologyLmkNvsLifecycle,
                            static_cast<int32_t>(kTopologyMetaSlotLmk),
                            0,
                            erased ? "topology_lmk_nvs_invalid_purged"
                                   : "topology_lmk_nvs_invalid_purge_failed");
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_invalid";
    }
    return false;
  }

  const uint8_t version = blob[0U];
  const uint8_t count = blob[1U];

  size_t off = 0U;
  if (version == kTopologyLmkBlobVersionV2) {
    const size_t expected = kTopologyLmkBlobHeaderSizeV2 + (static_cast<size_t>(count) * kTopologyLmkBlobEntrySize);
    if (blob.size() != expected || blob.size() < kTopologyLmkBlobHeaderSizeV2) {
      const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
      emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                              kLogEvtTopologyLmkNvsLifecycle,
                              static_cast<int32_t>(kTopologyMetaSlotLmk),
                              0,
                              erased ? "topology_lmk_nvs_invalid_purged"
                                     : "topology_lmk_nvs_invalid_purge_failed");
      if (out_error != nullptr) {
        *out_error = "topology_lmk_nvs_invalid";
      }
      return false;
    }
    if (!readU32Le(blob.data() + 2U, blob.size() - 2U, out_meta.topology_version) ||
        !readU32Le(blob.data() + 6U, blob.size() - 6U, out_meta.topology_checksum)) {
      const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
      emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                              kLogEvtTopologyLmkNvsLifecycle,
                              static_cast<int32_t>(kTopologyMetaSlotLmk),
                              0,
                              erased ? "topology_lmk_nvs_invalid_purged"
                                     : "topology_lmk_nvs_invalid_purge_failed");
      if (out_error != nullptr) {
        *out_error = "topology_lmk_nvs_invalid";
      }
      return false;
    }
    std::memcpy(out_meta.master_mac.data(), blob.data() + 10U, out_meta.master_mac.size());
    std::memcpy(out_meta.local_mac.data(), blob.data() + 16U, out_meta.local_mac.size());
    out_meta.local_profile = blob[22U];
    off = kTopologyLmkBlobHeaderSizeV2;
  } else {
    const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
    emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                            kLogEvtTopologyLmkNvsLifecycle,
                            static_cast<int32_t>(kTopologyMetaSlotLmk),
                            0,
                            erased ? "topology_lmk_nvs_invalid_version_purged"
                                   : "topology_lmk_nvs_invalid_version_purge_failed");
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_version_invalid";
    }
    return false;
  }

  out_meta.entries.clear();
  out_meta.entries.reserve(count);
  for (uint8_t i = 0U; i < count; ++i) {
    if ((off + kTopologyLmkBlobEntrySize) > blob.size()) {
      const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
      emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                              kLogEvtTopologyLmkNvsLifecycle,
                              static_cast<int32_t>(kTopologyMetaSlotLmk),
                              0,
                              erased ? "topology_lmk_nvs_invalid_purged"
                                     : "topology_lmk_nvs_invalid_purge_failed");
      if (out_error != nullptr) {
        *out_error = "topology_lmk_nvs_invalid";
      }
      return false;
    }
    TopologyPersistedLmkEntry_ entry{};
    std::memcpy(entry.peer.data(), blob.data() + off, entry.peer.size());
    off += entry.peer.size();
    std::memcpy(entry.lmk.data(), blob.data() + off, entry.lmk.size());
    off += entry.lmk.size();
    entry.group_id = blob[off++];
    out_meta.entries.push_back(entry);
  }
  if (off != blob.size()) {
    const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
    emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                            kLogEvtTopologyLmkNvsLifecycle,
                            static_cast<int32_t>(kTopologyMetaSlotLmk),
                            0,
                            erased ? "topology_lmk_nvs_invalid_purged"
                                   : "topology_lmk_nvs_invalid_purge_failed");
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_invalid";
    }
    return false;
  }
  return true;
}

bool EspNowManager::validateTopologyLmkMeta_(const TopologyPersistedLmkMeta_& meta,
                                             const TopologySnapshot& snapshot,
                                             const MacAddress& paired_master,
                                             std::string* out_error) const {
  if (meta.entries.empty()) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_empty";
    }
    return false;
  }
  if (meta.topology_version != snapshot.topology_version) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_version_mismatch";
    }
    return false;
  }
  if (meta.topology_checksum != snapshot.checksum) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_checksum_mismatch";
    }
    return false;
  }
  if (meta.master_mac != paired_master) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_master_mismatch";
    }
    return false;
  }
  if (meta.local_mac != local_mac_) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_local_mismatch";
    }
    return false;
  }
  const uint8_t local_profile_code = static_cast<uint8_t>(local_profile_id_ & 0xFFU);
  if (meta.local_profile == 0U || meta.local_profile != local_profile_code) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_nvs_profile_mismatch";
    }
    return false;
  }
  return true;
}

bool EspNowManager::findSlotLmkFromNvs_(const TopologySlot& slot,
                                        const TopologyPersistedLmkMeta_& meta,
                                        LmkKey& out_lmk) const {
  out_lmk = LmkKey{};
  for (const auto& entry : meta.entries) {
    if (entry.peer == slot.peer) {
      out_lmk = entry.lmk;
      return true;
    }
  }
  return false;
}

bool EspNowManager::buildDesiredTopologyPeersFromNvs_(const TopologySnapshot& snapshot,
                                                      const TopologyPersistedLmkMeta_& meta,
                                                      std::map<MacAddress, LmkKey>& out_desired,
                                                      std::string* out_error) const {
  out_desired.clear();
  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled) {
      continue;
    }
    LmkKey slot_lmk{};
    if (!findSlotLmkFromNvs_(slot, meta, slot_lmk)) {
      if (out_error != nullptr) {
        *out_error = "topology_lmk_missing";
      }
      return false;
    }
    auto it = out_desired.find(slot.peer);
    if (it == out_desired.end()) {
      out_desired[slot.peer] = slot_lmk;
    } else if (it->second != slot_lmk) {
      if (out_error != nullptr) {
        *out_error = "topology_lmk_conflict";
      }
      return false;
    }
  }
  return true;
}

bool EspNowManager::applyTopologyDesiredPeers_(const std::map<MacAddress, LmkKey>& desired,
                                               std::string* out_error) {
  if (config_.local_role != Role::Slave) {
    return true;
  }
  if (peer_window_ == nullptr) {
    if (out_error != nullptr) {
      *out_error = "topology_peer_window_missing";
    }
    return false;
  }

  MacAddress active_peer{};
  const bool has_active_peer = pairing_.getPairedPeer(active_peer);

  for (const auto& kv : desired) {
    if (has_active_peer && kv.first == active_peer) {
      continue;
    }
    if (!peer_window_->requestAttach(kv.first, true, &kv.second)) {
      if (out_error != nullptr) {
        *out_error = "topology_attach_failed";
      }
      return false;
    }
  }

  for (const auto& old_peer : topology_attached_peers_) {
    if (has_active_peer && old_peer == active_peer) {
      continue;
    }
    if (desired.find(old_peer) != desired.end()) {
      continue;
    }
    if (!peer_window_->requestEvict(old_peer)) {
      if (out_error != nullptr) {
        *out_error = "topology_evict_failed";
      }
      return false;
    }
  }

  std::vector<MacAddress> next_attached{};
  next_attached.reserve(desired.size());
  for (const auto& kv : desired) {
    if (has_active_peer && kv.first == active_peer) {
      continue;
    }
    next_attached.push_back(kv.first);
  }
  topology_attached_peers_ = std::move(next_attached);
  return true;
}

bool EspNowManager::ensureTopologyPeerReadyForSlot_(const TopologySlot& slot, std::string* out_error) {
  auto fail_peer_ready = [&](const char* reason) {
    noteTopologyPeerReadyFailure_(reason);
    emitTopologyRuntimeLog_(LibraryLogLevel::Warn,
                            kLogEvtTopologyPeerReady,
                            static_cast<int32_t>(slot.relative_index),
                            static_cast<int32_t>(topology_peer_ready_fail_count_),
                            reason);
    if (out_error != nullptr) {
      *out_error = (reason != nullptr && reason[0] != '\0') ? reason : "peer_not_ready";
    }
    return false;
  };

  if (config_.local_role != Role::Slave) {
    return true;
  }
  if (!slot.enabled) {
    return fail_peer_ready("index_unmapped");
  }
  if (peer_window_ == nullptr) {
    return fail_peer_ready("peer_not_ready");
  }

  MacAddress active_peer{};
  if (pairing_.getPairedPeer(active_peer) && active_peer == slot.peer) {
    if (out_error != nullptr) {
      *out_error = "ok";
    }
    return true;
  }

  MacAddress paired_master{};
  if (!resolveTopologyMasterMac_(paired_master)) {
    return fail_peer_ready("pair_missing");
  }
  if (!has_topology_committed_) {
    return fail_peer_ready("topology_missing");
  }

  LmkKey resolved_lmk{};
  bool have_lmk = false;
  bool used_fallback_derive = false;

  TopologyPersistedLmkMeta_ meta{};
  std::string meta_error{};
  if (loadTopologyLmkMeta_(meta, &meta_error) &&
      validateTopologyLmkMeta_(meta, topology_committed_, paired_master, &meta_error) &&
      findSlotLmkFromNvs_(slot, meta, resolved_lmk)) {
    have_lmk = true;
    emitTopologyRuntimeLog_(LibraryLogLevel::Info,
                            kLogEvtTopologyLmkNvsLifecycle,
                            static_cast<int32_t>(slot.relative_index),
                            static_cast<int32_t>(topology_committed_.topology_version),
                            "topology_lmk_nvs_hit");
  } else {
    emitTopologyRuntimeLog_(LibraryLogLevel::Warn,
                            kLogEvtTopologyLmkNvsLifecycle,
                            static_cast<int32_t>(slot.relative_index),
                            static_cast<int32_t>(topology_committed_.topology_version),
                            meta_error.empty() ? "topology_lmk_nvs_miss" : meta_error.c_str());
  }

  if (!have_lmk) {
    std::string derive_error{};
    if (!deriveTopologySlotLmk_(topology_committed_, slot, resolved_lmk, &derive_error)) {
      return fail_peer_ready(derive_error.empty() ? "topology_lmk_missing" : derive_error.c_str());
    }
    have_lmk = true;
    used_fallback_derive = true;
    emitTopologyRuntimeLog_(LibraryLogLevel::Info,
                            kLogEvtTopologyLmkNvsLifecycle,
                            static_cast<int32_t>(slot.relative_index),
                            static_cast<int32_t>(topology_committed_.topology_version),
                            "topology_lmk_nvs_fallback_derive");
  }

  if (!have_lmk || !peer_window_->requestAttach(slot.peer, true, &resolved_lmk)) {
    return fail_peer_ready("peer_attach_failed");
  }

  if (used_fallback_derive) {
    (void)saveTopologyLmkMeta_(topology_committed_);
  }

  emitTopologyRuntimeLog_(LibraryLogLevel::Info,
                          kLogEvtTopologyPeerReady,
                          static_cast<int32_t>(slot.relative_index),
                          static_cast<int32_t>(used_fallback_derive ? 1 : 0),
                          used_fallback_derive ? "topology_trigger_peer_ready_ok_fallback" : "topology_trigger_peer_ready_ok");
  if (out_error != nullptr) {
    *out_error = "ok";
  }
  return true;
}

bool EspNowManager::materializeTopologyPeers_(const TopologySnapshot& snapshot, std::string* out_error) {
  if (config_.local_role != Role::Slave) {
    return true;
  }
  std::map<MacAddress, std::pair<LmkKey, uint8_t>> peer_choice{};
  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled) {
      continue;
    }
    LmkKey slot_lmk{};
    std::string derive_error{};
    if (!deriveTopologySlotLmk_(snapshot, slot, slot_lmk, &derive_error)) {
      if (out_error != nullptr) {
        *out_error = derive_error;
      }
      return false;
    }

    auto it = peer_choice.find(slot.peer);
    if (it == peer_choice.end() || slot.group_id < it->second.second) {
      peer_choice[slot.peer] = std::make_pair(slot_lmk, slot.group_id);
    } else if (slot.group_id == it->second.second && it->second.first != slot_lmk) {
      if (out_error != nullptr) {
        *out_error = "topology_lmk_conflict";
      }
      return false;
    }
  }
  std::map<MacAddress, LmkKey> desired{};
  for (const auto& kv : peer_choice) {
    desired[kv.first] = kv.second.first;
  }
  return applyTopologyDesiredPeers_(desired, out_error);
}

bool EspNowManager::saveTopologyLmkMeta_(const TopologySnapshot& snapshot) {
  if (store_ == nullptr || !store_->enabled()) {
    return true;
  }
  auto log_lmk_meta = [&](LibraryLogLevel level, const char* reason, int32_t entry_count) {
    emitTopologyRuntimeLog_(level,
                            kLogEvtTopologyLmkNvsLifecycle,
                            entry_count,
                            static_cast<int32_t>(snapshot.topology_version),
                            reason);
  };
  MacAddress paired_master{};
  if (!resolveTopologyMasterMac_(paired_master)) {
    log_lmk_meta(LibraryLogLevel::Warn, "topology_lmk_master_missing", 0);
    return false;
  }
  const uint8_t local_profile_code = static_cast<uint8_t>(local_profile_id_ & 0xFFU);
  if (local_profile_code == 0U) {
    log_lmk_meta(LibraryLogLevel::Warn, "topology_lmk_local_profile_missing", 0);
    return false;
  }

  struct PersistedLmk {
    MacAddress peer{};
    LmkKey lmk{};
    uint8_t group_id = 0;
  };

  std::map<MacAddress, PersistedLmk> peer_choice{};
  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled) {
      continue;
    }
    LmkKey slot_lmk{};
    std::string derive_error{};
    if (!deriveTopologySlotLmk_(snapshot, slot, slot_lmk, &derive_error)) {
      log_lmk_meta(LibraryLogLevel::Warn,
                   derive_error.empty() ? "topology_lmk_derive_failed" : derive_error.c_str(),
                   0);
      return false;
    }
    auto it = peer_choice.find(slot.peer);
    if (it == peer_choice.end() || slot.group_id < it->second.group_id) {
      PersistedLmk entry{};
      entry.peer = slot.peer;
      entry.group_id = slot.group_id;
      entry.lmk = slot_lmk;
      peer_choice[slot.peer] = entry;
    } else if (slot.group_id == it->second.group_id && it->second.lmk != slot_lmk) {
      log_lmk_meta(LibraryLogLevel::Warn, "topology_lmk_conflict", 0);
      return false;
    }
  }

  std::vector<PersistedLmk> persisted{};
  persisted.reserve(peer_choice.size());
  for (const auto& kv : peer_choice) {
    persisted.push_back(kv.second);
  }

  if (persisted.empty()) {
    const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
    log_lmk_meta(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                 erased ? "topology_lmk_nvs_purge_old" : "topology_lmk_nvs_purge_old_failed",
                 0);
    return erased;
  }
  if (persisted.size() > 255U) {
    log_lmk_meta(LibraryLogLevel::Warn, "topology_lmk_nvs_too_many_entries", 0);
    return false;
  }

  std::vector<uint8_t> blob{};
  blob.reserve(kTopologyLmkBlobHeaderSizeV2 + persisted.size() * kTopologyLmkBlobEntrySize);
  blob.push_back(kTopologyLmkBlobVersionV2);
  blob.push_back(static_cast<uint8_t>(persisted.size()));
  appendU32Le(blob, snapshot.topology_version);
  appendU32Le(blob, snapshot.checksum);
  blob.insert(blob.end(), paired_master.begin(), paired_master.end());
  blob.insert(blob.end(), local_mac_.begin(), local_mac_.end());
  blob.push_back(local_profile_code);
  for (const auto& entry : persisted) {
    blob.insert(blob.end(), entry.peer.begin(), entry.peer.end());
    blob.insert(blob.end(), entry.lmk.begin(), entry.lmk.end());
    blob.push_back(entry.group_id);
  }
  const bool saved = saveLocalMetaBlob(kTopologyMetaSlotLmk, blob.data(), blob.size());
  log_lmk_meta(saved ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
               saved ? "topology_lmk_nvs_write_ok" : "topology_lmk_nvs_write_fail",
               static_cast<int32_t>(persisted.size()));
  return saved;
}

}  // namespace espnow_link

