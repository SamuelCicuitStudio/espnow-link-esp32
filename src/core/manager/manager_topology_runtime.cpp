/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Topology runtime state, routing, trigger, and validation behavior.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
bool EspNowManager::stageTopology(const TopologySnapshot& snapshot, std::string* out_error) {
  TopologySnapshot normalized = snapshot;
  normalized.state = TopologyState::Staged;
  if (!validateTopologySnapshot_(normalized, out_error)) {
    return false;
  }

  if (config_.local_role == Role::Slave && store_ != nullptr && store_->enabled()) {
    const bool differs_from_staged =
        (!has_topology_staged_) ||
        (topology_staged_.topology_version != normalized.topology_version) ||
        (topology_staged_.checksum != normalized.checksum);
    const bool differs_from_committed =
        (!has_topology_committed_) ||
        (topology_committed_.topology_version != normalized.topology_version) ||
        (topology_committed_.checksum != normalized.checksum);
    if (differs_from_staged || differs_from_committed) {
      const bool erased = eraseLocalMetaBlob(kTopologyMetaSlotLmk);
      emitTopologyRuntimeLog_(erased ? LibraryLogLevel::Info : LibraryLogLevel::Warn,
                              kLogEvtTopologyLmkNvsLifecycle,
                              static_cast<int32_t>(normalized.topology_version),
                              static_cast<int32_t>(normalized.enabled_slot_count),
                              erased ? "topology_lmk_nvs_purge_old" : "topology_lmk_nvs_purge_old_failed");
    }
  }

  const TopologySnapshot old_staged = topology_staged_;
  const bool had_staged = has_topology_staged_;
  topology_staged_ = normalized;
  has_topology_staged_ = true;
  if (!saveTopologySnapshotMeta_(kTopologyMetaSlotStaged, topology_staged_)) {
    topology_staged_ = old_staged;
    has_topology_staged_ = had_staged;
    if (out_error != nullptr) {
      *out_error = "topology_persist_failed";
    }
    return false;
  }
  return true;
}

bool EspNowManager::commitStagedTopology(std::string* out_error) {
  if (!has_topology_staged_) {
    if (has_topology_committed_) {
      if (out_error != nullptr) {
        out_error->clear();
      }
      return true;
    }
    if (out_error != nullptr) {
      *out_error = "topology_not_staged";
    }
    return false;
  }

  TopologySnapshot normalized = topology_staged_;
  normalized.state = TopologyState::Committed;
  // Commit changes topology state from staged->committed.
  // Recompute checksum in committed context during validation.
  normalized.checksum = 0U;
  if (!validateTopologySnapshot_(normalized, out_error)) {
    return false;
  }

  const TopologySnapshot old_committed = topology_committed_;
  const bool had_committed = has_topology_committed_;
  const std::vector<MacAddress> old_attached = topology_attached_peers_;
  std::vector<uint8_t> old_lmk_blob{};
  const bool had_old_lmk_blob =
      (config_.local_role == Role::Slave &&
       store_ != nullptr &&
       store_->enabled() &&
       loadLocalMetaBlob(kTopologyMetaSlotLmk, old_lmk_blob));
  auto restore_old_lmk_meta = [&]() {
    if (config_.local_role != Role::Slave || store_ == nullptr || !store_->enabled()) {
      return;
    }
    if (had_old_lmk_blob && !old_lmk_blob.empty()) {
      (void)saveLocalMetaBlob(kTopologyMetaSlotLmk, old_lmk_blob.data(), old_lmk_blob.size());
      return;
    }
    (void)eraseLocalMetaBlob(kTopologyMetaSlotLmk);
  };

  if (config_.local_role == Role::Slave) {
    std::string apply_error{};
    if (!materializeTopologyPeers_(normalized, &apply_error)) {
      topology_attached_peers_ = old_attached;
      if (out_error != nullptr) {
        *out_error = "topology_apply_failed";
      }
      return false;
    }
    if (!saveTopologyLmkMeta_(normalized)) {
      // Best-effort rollback to previously active committed topology peers.
      topology_attached_peers_ = old_attached;
      if (had_committed) {
        std::string rollback_error{};
        (void)materializeTopologyPeers_(old_committed, &rollback_error);
      }
      restore_old_lmk_meta();
      if (out_error != nullptr) {
        *out_error = "topology_persist_failed";
      }
      return false;
    }
  }

  topology_committed_ = normalized;
  has_topology_committed_ = true;
  if (!saveTopologySnapshotMeta_(kTopologyMetaSlotCommitted, topology_committed_)) {
    topology_committed_ = old_committed;
    has_topology_committed_ = had_committed;
    if (config_.local_role == Role::Slave) {
      topology_attached_peers_ = old_attached;
      if (had_committed) {
        std::string rollback_error{};
        (void)materializeTopologyPeers_(old_committed, &rollback_error);
      }
      restore_old_lmk_meta();
    }
    if (out_error != nullptr) {
      *out_error = "topology_persist_failed";
    }
    return false;
  }

  topology_staged_ = TopologySnapshot{};
  has_topology_staged_ = false;
  (void)eraseTopologySnapshotMeta_(kTopologyMetaSlotStaged);
  return true;
}

bool EspNowManager::clearStagedTopology() {
  topology_staged_ = TopologySnapshot{};
  has_topology_staged_ = false;
  return eraseTopologySnapshotMeta_(kTopologyMetaSlotStaged);
}

bool EspNowManager::getTopologyStatus(TopologyStatus& out_status) const {
  out_status = TopologyStatus{};
  out_status.has_staged = has_topology_staged_;
  out_status.has_committed = has_topology_committed_;
  if (has_topology_staged_) {
    out_status.staged = topology_staged_;
  }
  if (has_topology_committed_) {
    out_status.committed = topology_committed_;
  }
  return true;
}

bool EspNowManager::getTopologySnapshot(TopologyState state, TopologySnapshot& out_snapshot) const {
  out_snapshot = TopologySnapshot{};
  if (state == TopologyState::Staged) {
    if (!has_topology_staged_) {
      return false;
    }
    out_snapshot = topology_staged_;
    return true;
  }
  if (state == TopologyState::Committed) {
    if (!has_topology_committed_) {
      return false;
    }
    out_snapshot = topology_committed_;
    return true;
  }
  return false;
}

bool EspNowManager::resolveTopologyTargetIndex(int8_t target_idx,
                                               TopologySlot& out_slot,
                                               std::string* out_error,
                                               bool committed) const {
  out_slot = TopologySlot{};
  const TopologyState state = committed ? TopologyState::Committed : TopologyState::Staged;
  TopologySnapshot snapshot{};
  if (!getTopologySnapshot(state, snapshot)) {
    if (out_error != nullptr) {
      *out_error = "topology_missing";
    }
    return false;
  }

  if (target_idx == 0) {
    if (out_error != nullptr) {
      *out_error = "invalid_index";
    }
    return false;
  }
  if ((target_idx < 0 && static_cast<uint8_t>(-target_idx) > snapshot.index_neg) ||
      (target_idx > 0 && static_cast<uint8_t>(target_idx) > snapshot.index_pos)) {
    if (out_error != nullptr) {
      *out_error = "out_of_window";
    }
    return false;
  }

  for (const auto& slot : snapshot.slots) {
    if (!slot.enabled) {
      continue;
    }
    if (slot.relative_index == target_idx) {
      out_slot = slot;
      return true;
    }
  }
  if (out_error != nullptr) {
    *out_error = "index_unmapped";
  }
  return false;
}

bool EspNowManager::sendTopologyTrigger(const TopologyTriggerRequest& request,
                                        uint32_t corr_id,
                                        uint16_t* out_seq,
                                        std::string* out_error) {
  if (!has_topology_committed_) {
    if (out_error != nullptr) {
      *out_error = "topology_missing";
    }
    return false;
  }
  if (request.direction != 1U && request.direction != 2U) {
    if (out_error != nullptr) {
      *out_error = "invalid_direction";
    }
    return false;
  }
  if (request.source_virtual_index != 0xFFU && request.source_virtual_index > 0x0FU) {
    if (out_error != nullptr) {
      *out_error = "invalid_source_virtual_index";
    }
    return false;
  }
  if (local_profile_id_ == kProfileUnknown) {
    if (out_error != nullptr) {
      *out_error = "local_profile_unknown";
    }
    return false;
  }

  TopologySlot slot{};
  if (!resolveTopologyTargetIndex(request.target_index, slot, out_error, true)) {
    return false;
  }

  manager_helpers::TopologyTriggerPayload trigger{};
  trigger.topology_version = topology_committed_.topology_version;
  trigger.seq = topology_trigger_seq_++;
  if (topology_trigger_seq_ == 0U) {
    topology_trigger_seq_ = 1U;
  }
  trigger.src_role = static_cast<uint8_t>(local_profile_id_ & 0xFFU);
  trigger.src_vid = request.source_virtual_index;
  trigger.dst_role = slot.peer_role;
  trigger.dst_vid = slot.peer_virtual_index;
  if (!isTopologyRolePairAllowed(trigger.src_role, trigger.dst_role)) {
    if (out_error != nullptr) {
      *out_error = "invalid_role_pair";
    }
    return false;
  }
  std::string peer_ready_error{};
  if (!ensureTopologyPeerReadyForSlot_(slot, &peer_ready_error)) {
    if (out_error != nullptr) {
      *out_error = peer_ready_error.empty() ? "peer_not_ready" : peer_ready_error;
    }
    return false;
  }
  trigger.direction = request.direction;
  trigger.delay_ms = request.delay_ms;
  trigger.hold_ms = request.hold_ms;

  std::vector<uint8_t> payload{};
  if (!manager_helpers::buildTopologyTriggerPayload(trigger, payload)) {
    if (out_error != nullptr) {
      *out_error = "trigger_encode_failed";
    }
    return false;
  }

  uint32_t send_corr = corr_id;
  if (send_corr == 0U) {
    send_corr = (0x54000000U |
                 (static_cast<uint32_t>(static_cast<uint8_t>(request.target_index)) << 16) |
                 trigger.seq);
  }
  std::string send_error{};
  if (!sendTyped(slot.peer,
                 MessageType::TopologyTrigger,
                 payload.data(),
                 payload.size(),
                 send_corr,
                 0,
                 0,
                 0,
                 &send_error)) {
    if (out_error != nullptr) {
      *out_error = send_error.empty() ? "send_failed" : send_error;
    }
    return false;
  }

  if (out_seq != nullptr) {
    *out_seq = trigger.seq;
  }
  return true;
}

bool EspNowManager::sendTopologyTriggerBatch(const TopologyTriggerBatchRequest& request,
                                             uint32_t corr_id,
                                             uint16_t* out_seq,
                                             std::string* out_error) {
  if (!has_topology_committed_) {
    if (out_error != nullptr) {
      *out_error = "topology_missing";
    }
    return false;
  }
  if (request.entries.empty()) {
    if (out_error != nullptr) {
      *out_error = "batch_empty";
    }
    return false;
  }
  if (request.direction != 1U && request.direction != 2U) {
    if (out_error != nullptr) {
      *out_error = "invalid_direction";
    }
    return false;
  }
  if (request.source_virtual_index != 0xFFU && request.source_virtual_index > 0x0FU) {
    if (out_error != nullptr) {
      *out_error = "invalid_source_virtual_index";
    }
    return false;
  }
  if (local_profile_id_ == kProfileUnknown) {
    if (out_error != nullptr) {
      *out_error = "local_profile_unknown";
    }
    return false;
  }

  TopologySlot first_slot{};
  bool have_first_slot = false;
  std::vector<manager_helpers::TopologyTriggerBatchItem> items{};
  items.reserve(request.entries.size());

  for (const auto& entry : request.entries) {
    if (entry.target_index == 0) {
      if (out_error != nullptr) {
        *out_error = "invalid_target_index";
      }
      return false;
    }

    TopologySlot slot{};
    if (!resolveTopologyTargetIndex(entry.target_index, slot, out_error, true)) {
      return false;
    }

    if (!have_first_slot) {
      first_slot = slot;
      have_first_slot = true;
    } else if (slot.peer != first_slot.peer || slot.peer_role != first_slot.peer_role) {
      if (out_error != nullptr) {
        *out_error = "batch_multi_peer";
      }
      return false;
    }

    manager_helpers::TopologyTriggerBatchItem item{};
    item.dst_vid = slot.peer_virtual_index;
    item.target_index = entry.target_index;
    item.delay_ms = entry.delay_ms;
    item.hold_ms = entry.hold_ms;
    items.push_back(item);
  }

  if (!have_first_slot || items.empty()) {
    if (out_error != nullptr) {
      *out_error = "batch_empty";
    }
    return false;
  }

  const uint8_t src_role = static_cast<uint8_t>(local_profile_id_ & 0xFFU);
  if (!isTopologyRolePairAllowed(src_role, first_slot.peer_role)) {
    if (out_error != nullptr) {
      *out_error = "invalid_role_pair";
    }
    return false;
  }

  std::string peer_ready_error{};
  if (!ensureTopologyPeerReadyForSlot_(first_slot, &peer_ready_error)) {
    if (out_error != nullptr) {
      *out_error = peer_ready_error.empty() ? "peer_not_ready" : peer_ready_error;
    }
    return false;
  }

  manager_helpers::TopologyTriggerBatchPayload trigger{};
  trigger.topology_version = topology_committed_.topology_version;
  trigger.seq = topology_trigger_seq_++;
  if (topology_trigger_seq_ == 0U) {
    topology_trigger_seq_ = 1U;
  }
  trigger.src_role = src_role;
  trigger.src_vid = request.source_virtual_index;
  trigger.dst_role = first_slot.peer_role;
  trigger.direction = request.direction;
  trigger.items = std::move(items);

  std::vector<uint8_t> payload{};
  if (!manager_helpers::buildTopologyTriggerBatchPayload(trigger, payload)) {
    if (out_error != nullptr) {
      *out_error = "trigger_encode_failed";
    }
    return false;
  }

  uint32_t send_corr = corr_id;
  if (send_corr == 0U) {
    send_corr = (0x5A000000U | trigger.seq);
  }
  std::string send_error{};
  if (!sendTyped(first_slot.peer,
                 MessageType::TopologyTriggerBatch,
                 payload.data(),
                 payload.size(),
                 send_corr,
                 0,
                 0,
                 0,
                 &send_error)) {
    if (out_error != nullptr) {
      *out_error = send_error.empty() ? "send_failed" : send_error;
    }
    return false;
  }

  if (out_seq != nullptr) {
    *out_seq = trigger.seq;
  }
  return true;
}

bool EspNowManager::findTopologyAuthorizedSourceSlot_(const MacAddress& from,
                                                      uint8_t src_role,
                                                      uint8_t src_vid,
                                                      uint8_t dst_vid,
                                                      TopologySlot& out_slot) const {
  out_slot = TopologySlot{};
  if (!has_topology_committed_) {
    return false;
  }

  for (const auto& slot : topology_committed_.slots) {
    if (!slot.enabled) {
      continue;
    }
    if (slot.peer != from) {
      continue;
    }
    if (slot.peer_role != src_role) {
      continue;
    }
    if (slot.peer_virtual_index != src_vid) {
      continue;
    }
    if (slot.local_virtual_index != dst_vid) {
      continue;
    }
    out_slot = slot;
    return true;
  }
  return false;
}

void EspNowManager::pruneTopologyTriggerDedup_(uint32_t now_ms) {
  while (!topology_trigger_dedup_.empty()) {
    const uint32_t age = static_cast<uint32_t>(now_ms - topology_trigger_dedup_.front().seen_ms);
    if (age <= kTopologyTriggerDedupWindowMs &&
        topology_trigger_dedup_.size() <= kTopologyTriggerDedupMaxEntries) {
      break;
    }
    topology_trigger_dedup_.pop_front();
  }
}

bool EspNowManager::isTopologyTriggerDuplicate_(const MacAddress& from,
                                                uint8_t src_role,
                                                uint8_t src_vid,
                                                uint16_t seq) {
  pruneTopologyTriggerDedup_(current_now_ms_);
  for (const auto& entry : topology_trigger_dedup_) {
    if (entry.source == from &&
        entry.src_role == src_role &&
        entry.src_vid == src_vid &&
        entry.seq == seq) {
      return true;
    }
  }
  TopologyTriggerDedupEntry entry{};
  entry.source = from;
  entry.src_role = src_role;
  entry.src_vid = src_vid;
  entry.seq = seq;
  entry.seen_ms = current_now_ms_;
  topology_trigger_dedup_.push_back(entry);
  pruneTopologyTriggerDedup_(current_now_ms_);
  return false;
}

bool EspNowManager::onRxTopologyTrigger(const MacAddress& from,
                                        const FrameHeader& header,
                                        const uint8_t* payload,
                                        size_t payload_len) {
  if (config_.local_role != Role::Slave) {
    return false;
  }

  manager_helpers::TopologyTriggerPayload trigger{};
  if (!manager_helpers::parseTopologyTriggerPayload(payload, payload_len, trigger)) {
    return false;
  }
  (void)peer_registry_.updateLiveness(from, true, current_now_ms_);

  uint8_t reason = kTopologyTriggerReasonOk;
  bool accepted = false;
  bool duplicate = false;
  TopologySlot authorized_slot{};
  const uint8_t local_role = static_cast<uint8_t>(local_profile_id_ & 0xFFU);

  if (!has_topology_committed_) {
    reason = kTopologyTriggerReasonStaleTopology;
  } else if (local_profile_id_ == kProfileUnknown || trigger.dst_role != local_role) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if (!isTopologyRolePairAllowed(local_role, trigger.src_role)) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if ((trigger.src_vid != 0xFFU && trigger.src_vid > 0x0FU) ||
             (trigger.dst_vid != 0xFFU && trigger.dst_vid > 0x0FU)) {
    reason = kTopologyTriggerReasonBadIndex;
  } else if (trigger.direction != 1U && trigger.direction != 2U) {
    reason = kTopologyTriggerReasonRangeRejected;
  } else if (!findTopologyAuthorizedSourceSlot_(from,
                                                trigger.src_role,
                                                trigger.src_vid,
                                                trigger.dst_vid,
                                                authorized_slot)) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if (isTopologyTriggerDuplicate_(from, trigger.src_role, trigger.src_vid, trigger.seq)) {
    accepted = true;
    duplicate = true;
  } else {
    accepted = true;
  }

  if (events_ != nullptr) {
    Event e{};
    e.peer = from;
    e.correlation_id = header.correlation_id;
    e.event_id = trigger.seq;
    e.severity = trigger.direction;
    e.event_value = static_cast<int32_t>(trigger.hold_ms);
    e.src_role = trigger.src_role;
    e.src_vid = trigger.src_vid;
    e.dst_role = trigger.dst_role;
    e.dst_vid = trigger.dst_vid;
    e.direction = trigger.direction;
    e.delay_ms = trigger.delay_ms;
    e.hold_ms = trigger.hold_ms;
    e.reason = reason;
    e.result = accepted ? kTopologyTriggerResultAccepted : kTopologyTriggerResultRejected;
    if (accepted && duplicate) {
      e.type = Event::Type::TopologyTriggerDuplicate;
      e.message = "topology trigger duplicate";
    } else if (accepted) {
      e.type = Event::Type::TopologyTriggerReceived;
      e.message = "topology trigger accepted";
    } else {
      e.type = Event::Type::TopologyTriggerRejected;
      e.message = "topology trigger rejected";
      e.severity = reason;
    }
    events_->onEvent(e);
  }

  if (accepted && !duplicate && hooks_ != nullptr) {
    IPlatformHooks::TopologyTriggerNotification note{};
    note.source = from;
    note.seq = trigger.seq;
    note.src_role = trigger.src_role;
    note.src_vid = trigger.src_vid;
    note.dst_role = trigger.dst_role;
    note.dst_vid = trigger.dst_vid;
    note.direction = trigger.direction;
    note.delay_ms = trigger.delay_ms;
    note.hold_ms = trigger.hold_ms;
    hooks_->onTopologyTrigger(note);
  }

  manager_helpers::TopologyTriggerAckPayload ack{};
  ack.topology_version = has_topology_committed_ ? topology_committed_.topology_version : trigger.topology_version;
  ack.seq = trigger.seq;
  ack.src_role = local_role;
  ack.src_vid = trigger.dst_vid;
  ack.dst_role = trigger.src_role;
  ack.dst_vid = trigger.src_vid;
  ack.ack_seq = trigger.seq;
  ack.result = accepted ? kTopologyTriggerResultAccepted : kTopologyTriggerResultRejected;
  ack.reason = accepted ? kTopologyTriggerReasonOk : reason;

  std::vector<uint8_t> ack_payload{};
  if (manager_helpers::buildTopologyTriggerAckPayload(ack, ack_payload)) {
    (void)sendTyped(from,
                    MessageType::TopologyTriggerAck,
                    ack_payload.data(),
                    ack_payload.size(),
                    header.correlation_id);
  }
  blinkForMessage(header.type);
  return true;
}

bool EspNowManager::onRxTopologyTriggerBatch(const MacAddress& from,
                                             const FrameHeader& header,
                                             const uint8_t* payload,
                                             size_t payload_len) {
  if (config_.local_role != Role::Slave) {
    return false;
  }

  manager_helpers::TopologyTriggerBatchPayload trigger{};
  if (!manager_helpers::parseTopologyTriggerBatchPayload(payload, payload_len, trigger)) {
    return false;
  }
  (void)peer_registry_.updateLiveness(from, true, current_now_ms_);

  uint8_t reason = kTopologyTriggerReasonOk;
  bool accepted = false;
  bool duplicate = false;
  std::vector<TopologySlot> authorized_slots{};
  const uint8_t local_role = static_cast<uint8_t>(local_profile_id_ & 0xFFU);

  if (!has_topology_committed_) {
    reason = kTopologyTriggerReasonStaleTopology;
  } else if (local_profile_id_ == kProfileUnknown || trigger.dst_role != local_role) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if (!isTopologyRolePairAllowed(local_role, trigger.src_role)) {
    reason = kTopologyTriggerReasonUnauthorizedPeer;
  } else if ((trigger.src_vid != 0xFFU && trigger.src_vid > 0x0FU) ||
             trigger.items.empty()) {
    reason = kTopologyTriggerReasonBadIndex;
  } else if (trigger.direction != 1U && trigger.direction != 2U) {
    reason = kTopologyTriggerReasonRangeRejected;
  } else if (isTopologyTriggerDuplicate_(from, trigger.src_role, trigger.src_vid, trigger.seq)) {
    accepted = true;
    duplicate = true;
  } else {
    authorized_slots.reserve(trigger.items.size());
    for (const auto& item : trigger.items) {
      if ((item.dst_vid != 0xFFU && item.dst_vid > 0x0FU) || item.target_index == 0) {
        reason = kTopologyTriggerReasonBadIndex;
        break;
      }
      TopologySlot slot{};
      if (!findTopologyAuthorizedSourceSlot_(from,
                                             trigger.src_role,
                                             trigger.src_vid,
                                             item.dst_vid,
                                             slot)) {
        reason = kTopologyTriggerReasonUnauthorizedPeer;
        break;
      }
      authorized_slots.push_back(slot);
    }
    if (reason == kTopologyTriggerReasonOk) {
      accepted = true;
    }
  }

  if (events_ != nullptr) {
    Event e{};
    e.peer = from;
    e.correlation_id = header.correlation_id;
    e.event_id = trigger.seq;
    e.severity = trigger.direction;
    e.event_value = static_cast<int32_t>(trigger.items.size());
    e.src_role = trigger.src_role;
    e.src_vid = trigger.src_vid;
    e.dst_role = trigger.dst_role;
    e.dst_vid = 0xFFU;
    e.direction = trigger.direction;
    e.reason = reason;
    e.result = accepted ? kTopologyTriggerResultAccepted : kTopologyTriggerResultRejected;
    if (accepted && duplicate) {
      e.type = Event::Type::TopologyTriggerDuplicate;
      e.message = "topology trigger batch duplicate";
    } else if (accepted) {
      e.type = Event::Type::TopologyTriggerReceived;
      e.message = "topology trigger batch accepted";
    } else {
      e.type = Event::Type::TopologyTriggerRejected;
      e.message = "topology trigger batch rejected";
      e.severity = reason;
    }
    events_->onEvent(e);
  }

  if (accepted && !duplicate && hooks_ != nullptr) {
    const size_t item_count = std::min(trigger.items.size(), authorized_slots.size());
    for (size_t i = 0U; i < item_count; ++i) {
      const auto& item = trigger.items[i];
      IPlatformHooks::TopologyTriggerNotification note{};
      note.source = from;
      note.seq = trigger.seq;
      note.src_role = trigger.src_role;
      note.src_vid = trigger.src_vid;
      note.dst_role = trigger.dst_role;
      note.dst_vid = item.dst_vid;
      note.direction = trigger.direction;
      note.delay_ms = item.delay_ms;
      note.hold_ms = item.hold_ms;
      hooks_->onTopologyTrigger(note);
    }
  }

  manager_helpers::TopologyTriggerAckPayload ack{};
  ack.topology_version = has_topology_committed_ ? topology_committed_.topology_version : trigger.topology_version;
  ack.seq = trigger.seq;
  ack.src_role = local_role;
  ack.src_vid = 0xFFU;
  ack.dst_role = trigger.src_role;
  ack.dst_vid = trigger.src_vid;
  ack.ack_seq = trigger.seq;
  ack.result = accepted ? kTopologyTriggerResultAccepted : kTopologyTriggerResultRejected;
  ack.reason = accepted ? kTopologyTriggerReasonOk : reason;

  std::vector<uint8_t> ack_payload{};
  if (manager_helpers::buildTopologyTriggerAckPayload(ack, ack_payload)) {
    (void)sendTyped(from,
                    MessageType::TopologyTriggerAck,
                    ack_payload.data(),
                    ack_payload.size(),
                    header.correlation_id);
  }
  blinkForMessage(header.type);
  return true;
}

bool EspNowManager::onRxTopologyTriggerAck(const MacAddress& from,
                                           const FrameHeader& header,
                                           const uint8_t* payload,
                                           size_t payload_len) {
  manager_helpers::TopologyTriggerAckPayload ack{};
  if (!manager_helpers::parseTopologyTriggerAckPayload(payload, payload_len, ack)) {
    return false;
  }
  (void)peer_registry_.updateLiveness(from, true, current_now_ms_);
  if (events_ != nullptr) {
    Event e{};
    e.type = Event::Type::TopologyTriggerAck;
    e.peer = from;
    e.correlation_id = header.correlation_id;
    e.event_id = ack.ack_seq;
    e.severity = ack.result;
    e.event_value = static_cast<int32_t>(ack.reason);
    e.src_role = ack.src_role;
    e.src_vid = ack.src_vid;
    e.dst_role = ack.dst_role;
    e.dst_vid = ack.dst_vid;
    e.result = ack.result;
    e.reason = ack.reason;
    e.ack_seq = ack.ack_seq;
    e.message = (ack.result == kTopologyTriggerResultAccepted)
                    ? "topology trigger ack accepted"
                    : "topology trigger ack rejected";
    events_->onEvent(e);
  }
  blinkForMessage(header.type);
  return true;
}

bool EspNowManager::validateTopologySnapshot_(TopologySnapshot& inout_snapshot, std::string* out_error) const {
  const uint8_t local_profile_code = static_cast<uint8_t>(local_profile_id_ & 0xFFU);
  const uint8_t local_slot_cap = topologySlotCapForLocalRole(local_profile_code);

  if (inout_snapshot.schema_version == 0U) {
    inout_snapshot.schema_version = kTopologySchemaVersion;
  }
  if (inout_snapshot.schema_version != kTopologySchemaVersion) {
    if (out_error != nullptr) {
      *out_error = "invalid_schema_version";
    }
    return false;
  }
  if (inout_snapshot.topology_version == 0U) {
    if (out_error != nullptr) {
      *out_error = "invalid_topology_version";
    }
    return false;
  }
  if (inout_snapshot.index_neg > local_slot_cap ||
      inout_snapshot.index_pos > local_slot_cap) {
    if (out_error != nullptr) {
      *out_error = "invalid_index_window";
    }
    return false;
  }

  uint8_t enabled_slots = 0U;
  uint8_t enabled_groups = 0U;
  for (const auto& group : inout_snapshot.groups) {
    if (!group.enabled) {
      continue;
    }
    if (group.group_id == 0U || isAllZeroSeed(group.seed)) {
      if (out_error != nullptr) {
        *out_error = "invalid_group_seed";
      }
      return false;
    }
    ++enabled_groups;
  }

  for (size_t i = 0; i < inout_snapshot.slots.size(); ++i) {
    const TopologySlot& slot = inout_snapshot.slots[i];
    if (!slot.enabled) {
      continue;
    }
    if (slot.peer_role == 0U) {
      if (out_error != nullptr) {
        *out_error = "invalid_peer_role";
      }
      return false;
    }
    if (!isTopologyRolePairAllowed(local_profile_code, slot.peer_role)) {
      if (out_error != nullptr) {
        *out_error = "invalid_role_pair";
      }
      return false;
    }
    if (isAllZeroMac(slot.peer) || isBroadcastMac(slot.peer)) {
      if (out_error != nullptr) {
        *out_error = "invalid_peer_mac";
      }
      return false;
    }
    if (slot.relative_index == 0 ||
        slot.relative_index < -static_cast<int8_t>(local_slot_cap) ||
        slot.relative_index > static_cast<int8_t>(local_slot_cap)) {
      if (out_error != nullptr) {
        *out_error = "invalid_relative_index";
      }
      return false;
    }
    if (!isVirtualIndexAllowedForRole(local_profile_code, slot.local_virtual_index)) {
      if (out_error != nullptr) {
        *out_error = "invalid_local_virtual_index";
      }
      return false;
    }
    if (!isVirtualIndexAllowedForRole(slot.peer_role, slot.peer_virtual_index)) {
      if (out_error != nullptr) {
        *out_error = "invalid_peer_virtual_index";
      }
      return false;
    }

    bool group_found = false;
    for (const auto& group : inout_snapshot.groups) {
      if (!group.enabled) {
        continue;
      }
      if (group.group_id == slot.group_id) {
        group_found = true;
        break;
      }
    }
    if (!group_found) {
      if (out_error != nullptr) {
        *out_error = "group_id_missing";
      }
      return false;
    }

    for (size_t j = i + 1; j < inout_snapshot.slots.size(); ++j) {
      const TopologySlot& other = inout_snapshot.slots[j];
      if (!other.enabled) {
        continue;
      }
      if (slot.peer == other.peer &&
          slot.peer_role == other.peer_role &&
          slot.local_virtual_index == other.local_virtual_index &&
          slot.peer_virtual_index == other.peer_virtual_index) {
        if (out_error != nullptr) {
          *out_error = "duplicate_logical_peer";
        }
        return false;
      }
      if (slot.relative_index == other.relative_index) {
        if (out_error != nullptr) {
          *out_error = "duplicate_relative_index";
        }
        return false;
      }
    }
    ++enabled_slots;
  }

  if (enabled_slots > local_slot_cap ||
      enabled_groups > static_cast<uint8_t>(kTopologyMaxGroups)) {
    if (out_error != nullptr) {
      *out_error = "capacity_exceeded";
    }
    return false;
  }
  inout_snapshot.enabled_slot_count = enabled_slots;
  inout_snapshot.enabled_group_count = enabled_groups;
  const uint32_t computed_checksum = computeTopologyChecksum_(inout_snapshot);
  if (inout_snapshot.checksum != 0U && inout_snapshot.checksum != computed_checksum) {
    if (out_error != nullptr) {
      *out_error = "invalid_checksum";
    }
    return false;
  }
  inout_snapshot.checksum = computed_checksum;
  return true;
}

uint32_t EspNowManager::computeTopologyChecksum_(const TopologySnapshot& snapshot) const {
  uint32_t hash = 2166136261UL;
  auto mix8 = [&](uint8_t v) {
    hash ^= static_cast<uint32_t>(v);
    hash *= 16777619UL;
  };
  auto mix32 = [&](uint32_t v) {
    mix8(static_cast<uint8_t>(v & 0xFFU));
    mix8(static_cast<uint8_t>((v >> 8) & 0xFFU));
    mix8(static_cast<uint8_t>((v >> 16) & 0xFFU));
    mix8(static_cast<uint8_t>((v >> 24) & 0xFFU));
  };

  mix8(snapshot.schema_version);
  mix8(static_cast<uint8_t>(snapshot.state));
  mix32(snapshot.topology_version);
  mix8(snapshot.index_neg);
  mix8(snapshot.index_pos);
  mix8(snapshot.enabled_slot_count);
  mix8(snapshot.enabled_group_count);
  for (const auto& slot : snapshot.slots) {
    mix8(static_cast<uint8_t>(slot.enabled ? 1U : 0U));
    for (uint8_t b : slot.peer) {
      mix8(b);
    }
    mix8(slot.peer_role);
    mix8(slot.group_id);
    mix8(static_cast<uint8_t>(slot.relative_index));
    mix8(slot.local_virtual_index);
    mix8(slot.peer_virtual_index);
    mix8(static_cast<uint8_t>(slot.axis_order));
    mix8(static_cast<uint8_t>(slot.delay_ms & 0xFFU));
    mix8(static_cast<uint8_t>((slot.delay_ms >> 8) & 0xFFU));
    mix8(static_cast<uint8_t>(slot.hold_ms & 0xFFU));
    mix8(static_cast<uint8_t>((slot.hold_ms >> 8) & 0xFFU));
  }
  for (const auto& group : snapshot.groups) {
    mix8(static_cast<uint8_t>(group.enabled ? 1U : 0U));
    mix8(group.group_id);
    for (uint8_t b : group.seed) {
      mix8(b);
    }
  }
  return hash;
}

bool EspNowManager::resolveTopologyMasterMac_(MacAddress& out_master) const {
  out_master = MacAddress{};
  if (pairing_.getPairedPeer(out_master)) {
    return true;
  }
  return false;
}

bool EspNowManager::findTopologyGroupSeed_(const TopologySnapshot& snapshot,
                                           uint8_t group_id,
                                           std::array<uint8_t, 32>& out_seed) const {
  out_seed = {};
  if (group_id == 0U) {
    return false;
  }
  for (const auto& group : snapshot.groups) {
    if (!group.enabled) {
      continue;
    }
    if (group.group_id == group_id) {
      out_seed = group.seed;
      return true;
    }
  }
  return false;
}

bool EspNowManager::deriveTopologySlotLmk_(const TopologySnapshot& snapshot,
                                           const TopologySlot& slot,
                                           LmkKey& out_lmk,
                                           std::string* out_error) const {
  out_lmk = {};
  if (!slot.enabled) {
    if (out_error != nullptr) {
      *out_error = "topology_slot_disabled";
    }
    return false;
  }
  if (slot.peer == local_mac_) {
    if (out_error != nullptr) {
      *out_error = "topology_slot_self_peer";
    }
    return false;
  }

  std::array<uint8_t, 32> group_seed{};
  if (!findTopologyGroupSeed_(snapshot, slot.group_id, group_seed)) {
    if (out_error != nullptr) {
      *out_error = "topology_group_seed_missing";
    }
    return false;
  }

  MacAddress master_mac{};
  if (!resolveTopologyMasterMac_(master_mac)) {
    if (out_error != nullptr) {
      *out_error = "topology_master_missing";
    }
    return false;
  }

  if (!deriveLmkFromTopologySeed(group_seed, master_mac, local_mac_, slot.peer, slot.group_id, out_lmk)) {
    if (out_error != nullptr) {
      *out_error = "topology_lmk_derive_failed";
    }
    return false;
  }
  return true;
}

}  // namespace espnow_link

