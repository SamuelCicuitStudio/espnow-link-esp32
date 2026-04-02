#include "espnow_link/management_service.hpp"

#include <algorithm>
#include <utility>

#include "espnow_link/profile.hpp"

namespace espnow_link {

namespace {

constexpr uint8_t kLiveMonitorMetaSlot = 0x31;
constexpr uint8_t kLiveMonitorMetaVersion = 1;
constexpr uint8_t kChainLoopMetaSlot = 0x32;
constexpr uint8_t kChainLoopMetaVersion = 1;
constexpr uint32_t kLiveOfflineDetectMaxMs = 2500;
constexpr uint32_t kLiveProbeLeadMs = 1500;
constexpr uint32_t kLiveProbeUrgencyWindowMs = 600;
constexpr uint32_t kLiveProbeTimeoutFastMs = 500;
constexpr uint32_t kLiveProbeTimeoutNormalMs = 700;
constexpr uint8_t kLiveProbeBudgetFast = 2;
constexpr uint8_t kLiveProbeBudgetNormal = 1;
constexpr uint16_t kLiveIgnoreReasonOtaPush = 0x0001;
constexpr uint16_t kLiveIgnoreReasonOtaUpdate = 0x0002;
constexpr uint16_t kLiveIgnoreReasonCriticalInflight = 0x0004;
constexpr uint16_t kLiveIgnoreReasonMasterUpdateGuard = 0x0008;
constexpr uint8_t kLiveTransitionReasonProbeSuccess = 2;
constexpr uint8_t kLiveTransitionReasonProbeTimeoutThreshold = 3;
constexpr uint8_t kLiveTransitionReasonResumeRecheckTimeout = 4;

bool isChainRoleCode(uint8_t role_code) {
  return role_code == static_cast<uint8_t>(kProfileSens & 0xFFU) ||
         role_code == static_cast<uint8_t>(kProfileSemu & 0xFFU) ||
         role_code == static_cast<uint8_t>(kProfileRelay & 0xFFU) ||
         role_code == static_cast<uint8_t>(kProfileRemu & 0xFFU);
}

const char* channelSettingKeyForRole(uint8_t role_code) {
  if (role_code == static_cast<uint8_t>(kProfilePms & 0xFFU) ||
      role_code == static_cast<uint8_t>(kProfileLockAlarm & 0xFFU)) {
    return "chan";
  }
  return "channel";
}

void upsertChannelSyncTarget(std::vector<MacAddress>& peers,
                             std::vector<uint8_t>& role_codes,
                             const MacAddress& peer,
                             uint8_t role_code) {
  for (size_t i = 0; i < peers.size(); ++i) {
    if (peers[i] == peer) {
      if (role_codes[i] == 0U && role_code != 0U) {
        role_codes[i] = role_code;
      }
      return;
    }
  }
  peers.push_back(peer);
  role_codes.push_back(role_code);
}

uint32_t liveProbeTimeoutMs(size_t paired_count) {
  return (paired_count <= 4U) ? kLiveProbeTimeoutFastMs : kLiveProbeTimeoutNormalMs;
}

uint8_t liveProbeBudgetPerPump(size_t paired_count) {
  return (paired_count <= 4U) ? kLiveProbeBudgetFast : kLiveProbeBudgetNormal;
}

uint32_t liveProbeTriggerAgeMs() {
  return (kLiveOfflineDetectMaxMs > kLiveProbeLeadMs) ? (kLiveOfflineDetectMaxMs - kLiveProbeLeadMs) : 0U;
}

uint32_t liveProbeUrgencyAgeMs() {
  return (kLiveOfflineDetectMaxMs > kLiveProbeUrgencyWindowMs) ? (kLiveOfflineDetectMaxMs - kLiveProbeUrgencyWindowMs) : 0U;
}

void appendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
void appendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendU64(std::vector<uint8_t>& out, uint64_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
}

}  // namespace
void ManagementService::queueResponse(ManagementResponse response) {
  if (response_queue_.size() >= max_queue_depth_) response_queue_.pop_front();
  response_queue_.push_back(std::move(response));
}

void ManagementService::queueResponse(ManagementSource source,
                                      uint16_t cmd_id,
                                      uint32_t req_id,
                                      ManagementStatus status,
                                      std::vector<uint8_t> payload,
                                      const PeerResolveContext* peer_ctx) {
  ManagementResponse response{source, cmd_id, req_id, status, std::move(payload)};
  if (peer_ctx != nullptr) {
    applyPeerContext(response, *peer_ctx);
  }
  queueResponse(std::move(response));
}

void ManagementService::queueEvent(ManagementEvent event) {
  if (event_queue_.size() >= (max_queue_depth_ * 2)) event_queue_.pop_front();
  event_queue_.push_back(std::move(event));
}

void ManagementService::emitServiceEvent(ManagementEventId event_id,
                                         const ManagementRequest& request,
                                         ManagementStatus status,
                                         const PeerResolveContext* peer_ctx) {
  ManagementEvent event{event_id, request.source, request.cmd_id, request.req_id, status, {}};
  if (peer_ctx != nullptr) {
    applyPeerContext(event, *peer_ctx);
  } else if (request.has_target_peer) {
    event.has_requested_peer = true;
    event.requested_peer = request.target_peer;
  }
  queueEvent(std::move(event));
}

void ManagementService::applyPeerContext(ManagementResponse& response,
                                         const PeerResolveContext& peer_ctx) const {
  response.has_requested_peer = peer_ctx.has_requested_peer;
  response.requested_peer = peer_ctx.requested_peer;
  response.has_executed_peer = peer_ctx.has_executed_peer;
  response.executed_peer = peer_ctx.executed_peer;
  response.activation_performed = peer_ctx.activation_performed;
  response.activation_latency_ms = peer_ctx.activation_latency_ms;
}

void ManagementService::applyPeerContext(ManagementEvent& event,
                                         const PeerResolveContext& peer_ctx) const {
  event.has_requested_peer = peer_ctx.has_requested_peer;
  event.requested_peer = peer_ctx.requested_peer;
  event.has_executed_peer = peer_ctx.has_executed_peer;
  event.executed_peer = peer_ctx.executed_peer;
  event.activation_performed = peer_ctx.activation_performed;
  event.activation_latency_ms = peer_ctx.activation_latency_ms;
}

bool ManagementService::buildDiscoverySnapshotPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  std::vector<MacAddress> peers;
  manager_.getDiscoveredPeers(peers);
  const size_t count = std::min<size_t>(peers.size(), 255U);
  appendU8(out_payload, static_cast<uint8_t>(count));
  for (size_t i = 0; i < count; ++i) {
    out_payload.insert(out_payload.end(), peers[i].begin(), peers[i].end());
  }
  if (count == 0U) {
    return true;
  }

  std::vector<PeerRecord> registry{};
  manager_.getPeerRegistrySnapshot(registry);

  constexpr uint8_t kSnapshotMetaTag = 0xD5U;
  constexpr uint8_t kSnapshotMetaVersion = 1U;
  constexpr uint8_t kSnapshotMetaEntrySize = 4U;  // flags:u8 + rssi_s8:u8 + age_s:u16
  appendU8(out_payload, kSnapshotMetaTag);
  appendU8(out_payload, kSnapshotMetaVersion);
  appendU8(out_payload, static_cast<uint8_t>(count));
  appendU8(out_payload, kSnapshotMetaEntrySize);

  for (size_t i = 0; i < count; ++i) {
    uint8_t flags = 0U;
    int8_t rssi_s8 = 0;
    uint16_t age_s = 0U;

    const auto it = std::find_if(registry.begin(),
                                 registry.end(),
                                 [&](const PeerRecord& rec) { return rec.mac == peers[i]; });
    if (it != registry.end()) {
      if (it->last_rssi != 0) {
        const int rssi_i = static_cast<int>(it->last_rssi);
        const int clamped = std::max(-127, std::min(127, rssi_i));
        rssi_s8 = static_cast<int8_t>(clamped);
        flags |= 0x01U;
      }
      if (it->last_seen_ms != 0U) {
        const uint32_t age_ms = (now_ms_ >= it->last_seen_ms) ? (now_ms_ - it->last_seen_ms) : 0U;
        const uint32_t age_s_u32 = age_ms / 1000U;
        age_s = static_cast<uint16_t>(std::min<uint32_t>(age_s_u32, 0xFFFFU));
        flags |= 0x02U;
      }
    }

    appendU8(out_payload, flags);
    appendU8(out_payload, static_cast<uint8_t>(rssi_s8));
    appendU16(out_payload, age_s);
  }
  return true;
}

bool ManagementService::buildPairedSnapshotPayload(std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  std::vector<EspNowManager::PersistedPeerRoleEntry> peers{};
  manager_.getPersistedPeersWithRole(peers);
  appendU8(out_payload, static_cast<uint8_t>(std::min<size_t>(peers.size(), 255)));
  for (size_t i = 0; i < peers.size() && i < 255U; ++i) {
    out_payload.insert(out_payload.end(), peers[i].peer.begin(), peers[i].peer.end());
    appendU8(out_payload, peers[i].role_code);
  }
  return true;
}

bool ManagementService::buildStatusPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  MacAddress peer{};
  const bool has_peer = manager_.getPairedPeer(peer);
  appendU8(out_payload, manager_.isPaired() ? 1 : 0);
  appendU8(out_payload, has_peer ? 1 : 0);
  if (has_peer) out_payload.insert(out_payload.end(), peer.begin(), peer.end());
  appendU8(out_payload, discovery_active_ ? 1 : 0);
  appendU32(out_payload, now_ms_);
  appendU16(out_payload, static_cast<uint16_t>(request_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(response_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(event_queue_.size()));
  return true;
}

bool ManagementService::buildTopologyStatusPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  EspNowManager::TopologyStatus status{};
  (void)manager_.getTopologyStatus(status);

  appendU8(out_payload, EspNowManager::kTopologySchemaVersion);
  appendU8(out_payload, status.has_staged ? 1U : 0U);
  appendU8(out_payload, status.has_committed ? 1U : 0U);
  appendU8(out_payload, 0U);  // reserved
  appendU32(out_payload, status.has_staged ? status.staged.topology_version : 0U);
  appendU32(out_payload, status.has_committed ? status.committed.topology_version : 0U);
  appendU8(out_payload, static_cast<uint8_t>(status.has_staged ? status.staged.state
                                                               : EspNowManager::TopologyState::None));
  appendU8(out_payload, static_cast<uint8_t>(status.has_committed ? status.committed.state
                                                                  : EspNowManager::TopologyState::None));
  appendU8(out_payload, status.has_staged ? status.staged.enabled_slot_count : 0U);
  appendU8(out_payload, status.has_committed ? status.committed.enabled_slot_count : 0U);
  appendU8(out_payload, status.has_staged ? status.staged.enabled_group_count : 0U);
  appendU8(out_payload, status.has_committed ? status.committed.enabled_group_count : 0U);
  appendU8(out_payload, status.has_committed ? status.committed.index_neg
                                             : (status.has_staged ? status.staged.index_neg : 0U));
  appendU8(out_payload, status.has_committed ? status.committed.index_pos
                                             : (status.has_staged ? status.staged.index_pos : 0U));
  appendU32(out_payload, status.has_staged ? status.staged.checksum : 0U);
  appendU32(out_payload, status.has_committed ? status.committed.checksum : 0U);
  return true;
}

bool ManagementService::buildTopologySlotsPayload(bool committed, std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  EspNowManager::TopologySnapshot snapshot{};
  const EspNowManager::TopologyState state =
      committed ? EspNowManager::TopologyState::Committed : EspNowManager::TopologyState::Staged;
  if (!manager_.getTopologySnapshot(state, snapshot)) {
    return false;
  }

  appendU8(out_payload, snapshot.schema_version);
  appendU8(out_payload, static_cast<uint8_t>(snapshot.state));
  appendU8(out_payload, static_cast<uint8_t>(snapshot.slots.size()));
  for (size_t i = 0; i < snapshot.slots.size(); ++i) {
    const auto& slot = snapshot.slots[i];
    appendU8(out_payload, static_cast<uint8_t>(i));
    appendU8(out_payload, slot.enabled ? 1U : 0U);
    out_payload.insert(out_payload.end(), slot.peer.begin(), slot.peer.end());
    appendU8(out_payload, slot.peer_role);
    appendU8(out_payload, slot.group_id);
    appendU8(out_payload, static_cast<uint8_t>(slot.relative_index));
    appendU8(out_payload, slot.local_virtual_index);
    appendU8(out_payload, slot.peer_virtual_index);
    appendU8(out_payload, static_cast<uint8_t>(slot.axis_order));
    appendU16(out_payload, slot.delay_ms);
    appendU16(out_payload, slot.hold_ms);
  }
  return true;
}

bool ManagementService::buildTopologySnapshotPayload(bool committed,
                                                     ManagementTopologySnapshotPayload& out_snapshot) const {
  out_snapshot = ManagementTopologySnapshotPayload{};
  EspNowManager::TopologySnapshot snapshot{};
  const EspNowManager::TopologyState state =
      committed ? EspNowManager::TopologyState::Committed : EspNowManager::TopologyState::Staged;
  if (!manager_.getTopologySnapshot(state, snapshot)) {
    return false;
  }

  out_snapshot.schema_version = snapshot.schema_version;
  out_snapshot.topology_version = snapshot.topology_version;
  out_snapshot.index_neg = snapshot.index_neg;
  out_snapshot.index_pos = snapshot.index_pos;

  out_snapshot.groups.reserve(snapshot.groups.size());
  for (size_t i = 0; i < snapshot.groups.size(); ++i) {
    ManagementTopologyGroupSeedPayload g{};
    g.group_slot = static_cast<uint8_t>(i);
    g.enabled = snapshot.groups[i].enabled;
    g.group_id = snapshot.groups[i].group_id;
    g.seed = snapshot.groups[i].seed;
    out_snapshot.groups.push_back(g);
  }

  out_snapshot.slots.reserve(snapshot.slots.size());
  for (size_t i = 0; i < snapshot.slots.size(); ++i) {
    ManagementTopologySlotPayload s{};
    s.slot_index = static_cast<uint8_t>(i);
    s.enabled = snapshot.slots[i].enabled;
    s.peer = snapshot.slots[i].peer;
    s.peer_role = snapshot.slots[i].peer_role;
    s.group_id = snapshot.slots[i].group_id;
    s.relative_index = snapshot.slots[i].relative_index;
    s.local_virtual_index = snapshot.slots[i].local_virtual_index;
    s.peer_virtual_index = snapshot.slots[i].peer_virtual_index;
    s.axis_order = snapshot.slots[i].axis_order;
    s.delay_ms = snapshot.slots[i].delay_ms;
    s.hold_ms = snapshot.slots[i].hold_ms;
    out_snapshot.slots.push_back(s);
  }

  return true;
}

bool ManagementService::queueTopologyDeployToPeer(const MacAddress& peer,
                                                  const ManagementTopologySnapshotPayload& snapshot,
                                                  uint32_t corr_base) {
  if (pull_ == nullptr) {
    return false;
  }
  uint32_t corr = corr_base;
  bool sent = pull_->requestTopologyStageClear(peer, corr++);
  sent = sent && pull_->requestTopologyStageBegin(peer,
                                                  snapshot.schema_version,
                                                  snapshot.topology_version,
                                                  snapshot.index_neg,
                                                  snapshot.index_pos,
                                                  corr++);
  for (const auto& group : snapshot.groups) {
    sent = sent && pull_->requestTopologyStageGroupSet(peer, group, corr++);
  }
  for (const auto& slot : snapshot.slots) {
    sent = sent && pull_->requestTopologyStageSlotSet(peer, slot, corr++);
  }
  sent = sent && pull_->requestTopologyStageFinalize(peer, corr++);
  sent = sent && pull_->requestTopologyCommit(peer, corr++);
  return sent;
}

bool ManagementService::queueTopologyDeployForCommitted(uint32_t corr_seed,
                                                        uint32_t& out_queued_peers,
                                                        uint32_t& out_failed_peers) {
  out_queued_peers = 0U;
  out_failed_peers = 0U;
  if (local_role_ != Role::Master || pull_ == nullptr) {
    return false;
  }

  ManagementTopologySnapshotPayload snapshot{};
  if (!buildTopologySnapshotPayload(true, snapshot)) {
    return false;
  }

  std::vector<MacAddress> peers{};
  manager_.getPersistedPeers(peers);
  if (peers.empty()) {
    return true;
  }

  for (size_t i = 0; i < peers.size(); ++i) {
    const uint32_t base = corr_seed + 1U + static_cast<uint32_t>(i * 32U);
    if (queueTopologyDeployToPeer(peers[i], snapshot, base)) {
      ++out_queued_peers;
    } else {
      ++out_failed_peers;
    }
  }
  return true;
}

bool ManagementService::buildRuntimeChannelPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  out_payload.push_back(manager_.currentChannel());
  std::vector<EspNowManager::PersistedPeerChannelEntry> entries{};
  if (!manager_.getPersistedPeerChannels(entries)) {
    return false;
  }
  out_payload.push_back(static_cast<uint8_t>(std::min<size_t>(entries.size(), 255U)));
  const size_t n = std::min<size_t>(entries.size(), 255U);
  for (size_t i = 0; i < n; ++i) {
    const auto& e = entries[i];
    out_payload.insert(out_payload.end(), e.peer.begin(), e.peer.end());
    appendU8(out_payload, e.channel);
    const size_t key_n = std::min<size_t>(e.channel_key.size(), 255U);
    appendU8(out_payload, static_cast<uint8_t>(key_n));
    out_payload.insert(out_payload.end(),
                       e.channel_key.begin(),
                       e.channel_key.begin() + static_cast<std::string::difference_type>(key_n));
  }
  return true;
}

bool ManagementService::startChannelSyncAll(ManagementSource source, uint32_t req_id, uint8_t channel) {
  if (local_role_ != Role::Master || pull_ == nullptr) {
    return false;
  }
  channel_sync_all_ = ChannelSyncAllSession{};
  channel_sync_all_.active = true;
  channel_sync_all_.source = source;
  channel_sync_all_.owner_cmd_id = static_cast<uint16_t>(ManagementCommandId::ChannelSyncAll);
  channel_sync_all_.req_id = req_id;
  channel_sync_all_.channel = channel;
  channel_sync_all_.started_ms = now_ms_;
  channel_sync_all_.timeout_ms = 4000U;
  std::vector<EspNowManager::PersistedPeerRoleEntry> persisted_peers{};
  manager_.getPersistedPeersWithRole(persisted_peers);
  channel_sync_all_.peers.reserve(persisted_peers.size());
  channel_sync_all_.role_codes.reserve(persisted_peers.size());
  for (const auto& entry : persisted_peers) {
    upsertChannelSyncTarget(channel_sync_all_.peers, channel_sync_all_.role_codes, entry.peer, entry.role_code);
  }

  // Include topology-linked peers (chain roles) to keep topology bindings consistent.
  EspNowManager::TopologySnapshot snapshot{};
  if (manager_.getTopologySnapshot(EspNowManager::TopologyState::Committed, snapshot)) {
    for (const auto& slot : snapshot.slots) {
      if (!slot.enabled || !isChainRoleCode(slot.peer_role)) {
        continue;
      }
      upsertChannelSyncTarget(channel_sync_all_.peers, channel_sync_all_.role_codes, slot.peer, slot.peer_role);
    }
  }

  if (channel_sync_all_.peers.empty()) {
    const bool applied = manager_.applyRuntimeChannelToAllPeers(channel);
    stopChannelSyncAll(applied, applied ? ManagementStatus::Ok : ManagementStatus::InternalError);
    return true;
  }

  channel_sync_all_.corr_ids.reserve(channel_sync_all_.peers.size());
  channel_sync_all_.setting_keys.reserve(channel_sync_all_.peers.size());
  channel_sync_all_.acked.assign(channel_sync_all_.peers.size(), 0U);
  uint32_t corr = req_id + 1U;
  const std::string value = std::to_string(static_cast<unsigned int>(channel));
  for (size_t i = 0; i < channel_sync_all_.peers.size(); ++i) {
    const uint8_t role_code = (i < channel_sync_all_.role_codes.size()) ? channel_sync_all_.role_codes[i] : 0U;
    const char* key = channelSettingKeyForRole(role_code);
    channel_sync_all_.setting_keys.emplace_back(key);
    if (!pull_->requestSettingSet(channel_sync_all_.peers[i], key, value, corr)) {
      channel_sync_all_.active = false;
      return false;
    }
    channel_sync_all_.corr_ids.push_back(corr);
    ++corr;
  }
  return true;
}

void ManagementService::stopChannelSyncAll(bool success, ManagementStatus status) {
  if (!channel_sync_all_.active && channel_sync_all_.req_id == 0U) {
    return;
  }
  ManagementChannelSyncAllResultPayload result{};
  result.channel = channel_sync_all_.channel;
  result.total_peers = static_cast<uint8_t>(std::min<size_t>(channel_sync_all_.peers.size(), 255U));
  size_t acked = 0U;
  for (uint8_t v : channel_sync_all_.acked) {
    if (v != 0U) {
      ++acked;
    }
  }
  result.acked_peers = static_cast<uint8_t>(std::min<size_t>(acked, 255U));
  std::vector<uint8_t> payload{};
  appendU8(payload, result.channel);
  appendU8(payload, result.acked_peers);
  appendU8(payload, result.total_peers);

  queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
              channel_sync_all_.source,
              channel_sync_all_.owner_cmd_id,
              channel_sync_all_.req_id,
              status,
              payload});
  channel_sync_all_ = ChannelSyncAllSession{};
}

void ManagementService::pumpChannelSyncAll() {
  if (!channel_sync_all_.active) {
    return;
  }
  if (channel_sync_all_.timeout_ms != 0U &&
      static_cast<int32_t>(now_ms_ - (channel_sync_all_.started_ms + channel_sync_all_.timeout_ms)) >= 0) {
    stopChannelSyncAll(false, ManagementStatus::Timeout);
    return;
  }
  bool all_acked = !channel_sync_all_.acked.empty();
  for (uint8_t v : channel_sync_all_.acked) {
    if (v == 0U) {
      all_acked = false;
      break;
    }
  }
  if (!all_acked) {
    return;
  }
  const bool applied = manager_.applyRuntimeChannelToAllPeers(channel_sync_all_.channel);
  stopChannelSyncAll(applied, applied ? ManagementStatus::Ok : ManagementStatus::InternalError);
}

bool ManagementService::buildChainLoopTargetPeers(std::vector<MacAddress>& out_peers) {
  out_peers.clear();
  EspNowManager::TopologySnapshot snapshot{};
  if (manager_.getTopologySnapshot(EspNowManager::TopologyState::Committed, snapshot)) {
    for (const auto& slot : snapshot.slots) {
      if (!slot.enabled || !isChainRoleCode(slot.peer_role)) {
        continue;
      }
      if (std::find(out_peers.begin(), out_peers.end(), slot.peer) == out_peers.end()) {
        out_peers.push_back(slot.peer);
      }
    }
  }

  if (!out_peers.empty()) {
    return true;
  }

  // Fallback for chain JSON deploy flow: ICM may not have a local committed topology
  // snapshot, so derive targets from persisted paired peers.
  std::vector<EspNowManager::PersistedPeerRoleEntry> persisted_peers{};
  manager_.getPersistedPeersWithRole(persisted_peers);
  for (const auto& entry : persisted_peers) {
    if (entry.role_code != 0U && !isChainRoleCode(entry.role_code)) {
      continue;
    }
    if (std::find(out_peers.begin(), out_peers.end(), entry.peer) == out_peers.end()) {
      out_peers.push_back(entry.peer);
    }
  }
  return true;
}

bool ManagementService::startChainLoopAll(ManagementSource source, uint32_t req_id, bool enabled) {
  if (local_role_ != Role::Master || pull_ == nullptr) {
    return false;
  }
  chain_loop_all_ = ChainLoopAllSession{};
  chain_loop_all_.active = true;
  chain_loop_all_.source = source;
  chain_loop_all_.owner_cmd_id = static_cast<uint16_t>(ManagementCommandId::ChainLoopControlSet);
  chain_loop_all_.req_id = req_id;
  chain_loop_all_.enabled = enabled;
  chain_loop_all_.started_ms = now_ms_;
  chain_loop_all_.timeout_ms = 4000U;
  if (!buildChainLoopTargetPeers(chain_loop_all_.peers)) {
    chain_loop_all_.active = false;
    return false;
  }
  if (chain_loop_all_.peers.empty()) {
    chain_loop_enabled_ = enabled;
    persistChainLoopConfig();
    stopChainLoopAll(true, ManagementStatus::Ok);
    return true;
  }

  chain_loop_all_.corr_ids.reserve(chain_loop_all_.peers.size());
  chain_loop_all_.acked.assign(chain_loop_all_.peers.size(), 0U);
  uint32_t corr = req_id + 1U;
  const std::string value = enabled ? "1" : "0";
  for (size_t i = 0; i < chain_loop_all_.peers.size(); ++i) {
    if (!pull_->requestSettingSet(chain_loop_all_.peers[i], "LoopAuto", value, corr)) {
      chain_loop_all_.active = false;
      return false;
    }
    chain_loop_all_.corr_ids.push_back(corr);
    ++corr;
  }
  return true;
}

void ManagementService::stopChainLoopAll(bool success, ManagementStatus status) {
  if (!chain_loop_all_.active && chain_loop_all_.req_id == 0U) {
    return;
  }
  if (success) {
    chain_loop_enabled_ = chain_loop_all_.enabled;
  } else {
    // Strict aggregate rule: true only when every targeted chain node acknowledged apply.
    chain_loop_enabled_ = false;
  }
  persistChainLoopConfig();

  ManagementChainLoopResultPayload result{};
  result.enabled = chain_loop_enabled_;
  result.total_peers = static_cast<uint8_t>(std::min<size_t>(chain_loop_all_.peers.size(), 255U));
  size_t acked = 0U;
  for (uint8_t v : chain_loop_all_.acked) {
    if (v != 0U) {
      ++acked;
    }
  }
  result.acked_peers = static_cast<uint8_t>(std::min<size_t>(acked, 255U));

  std::vector<uint8_t> payload{};
  appendU8(payload, result.enabled ? 1U : 0U);
  appendU8(payload, result.acked_peers);
  appendU8(payload, result.total_peers);

  queueEvent({success ? ManagementEventId::CmdDone : ManagementEventId::CmdFail,
              chain_loop_all_.source,
              chain_loop_all_.owner_cmd_id,
              chain_loop_all_.req_id,
              status,
              payload});
  chain_loop_all_ = ChainLoopAllSession{};
}

void ManagementService::pumpChainLoopAll() {
  if (!chain_loop_all_.active) {
    return;
  }
  if (chain_loop_all_.timeout_ms != 0U &&
      static_cast<int32_t>(now_ms_ - (chain_loop_all_.started_ms + chain_loop_all_.timeout_ms)) >= 0) {
    stopChainLoopAll(false, ManagementStatus::Timeout);
    return;
  }
  bool all_acked = !chain_loop_all_.acked.empty();
  for (uint8_t v : chain_loop_all_.acked) {
    if (v == 0U) {
      all_acked = false;
      break;
    }
  }
  if (!all_acked) {
    return;
  }
  stopChainLoopAll(true, ManagementStatus::Ok);
}

bool ManagementService::buildLiveMonitorStatusPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  uint8_t online_count = 0U;
  uint8_t offline_count = 0U;
  for (const auto& peer : live_monitor_.peers) {
    if (peer.online) {
      if (online_count < 0xFFU) ++online_count;
    } else {
      if (offline_count < 0xFFU) ++offline_count;
    }
  }
  appendU8(out_payload, live_monitor_.enabled ? 1U : 0U);
  appendU8(out_payload, live_monitor_.ignore_active ? 1U : 0U);
  appendU16(out_payload, live_monitor_.ignore_reason_mask);
  appendU8(out_payload, static_cast<uint8_t>(std::min<size_t>(live_monitor_.peers.size(), 255U)));
  appendU8(out_payload, online_count);
  appendU8(out_payload, offline_count);
  appendU32(out_payload, live_monitor_.next_probe_due_ms);
  appendU32(out_payload, live_monitor_.last_transition_ms);
  return true;
}

bool ManagementService::buildQueuePayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  appendU16(out_payload, static_cast<uint16_t>(request_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(response_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(event_queue_.size()));
  appendU16(out_payload, static_cast<uint16_t>(max_queue_depth_));
  return true;
}

bool ManagementService::buildMetricsPayload(std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  const ManagerRuntimeMetrics& m = manager_.runtimeMetrics();
  appendU64(out_payload, m.tick_count);
  appendU32(out_payload, m.tick_last_us);
  appendU32(out_payload, m.tick_max_us);
  appendU64(out_payload, m.tick_total_us);
  appendU64(out_payload, m.rx_frames);
  appendU64(out_payload, m.rx_bytes);
  appendU32(out_payload, m.rx_handler_last_us);
  appendU32(out_payload, m.rx_handler_max_us);
  appendU64(out_payload, m.rx_handler_total_us);
  appendU64(out_payload, m.tx_frames);
  appendU64(out_payload, m.tx_bytes);
  appendU64(out_payload, m.tx_failures);
  appendU32(out_payload, m.tx_send_last_us);
  appendU32(out_payload, m.tx_send_max_us);
  appendU64(out_payload, m.tx_send_total_us);
  return true;
}

void ManagementService::loadLiveMonitorConfig() {
  live_monitor_.enabled = false;
  std::vector<uint8_t> blob;
  if (!manager_.loadLocalMetaBlob(kLiveMonitorMetaSlot, blob)) {
    return;
  }
  if (blob.size() < 2U || blob[0U] != kLiveMonitorMetaVersion) {
    return;
  }
  live_monitor_.enabled = (blob[1U] != 0U);
}

void ManagementService::persistLiveMonitorConfig() const {
  uint8_t blob[2] = {kLiveMonitorMetaVersion, static_cast<uint8_t>(live_monitor_.enabled ? 1U : 0U)};
  (void)manager_.saveLocalMetaBlob(kLiveMonitorMetaSlot, blob, sizeof(blob));
}

void ManagementService::loadChainLoopConfig() {
  chain_loop_enabled_ = false;
  std::vector<uint8_t> blob;
  if (!manager_.loadLocalMetaBlob(kChainLoopMetaSlot, blob)) {
    return;
  }
  if (blob.size() < 2U || blob[0U] != kChainLoopMetaVersion || blob[1U] > 1U) {
    return;
  }
  chain_loop_enabled_ = (blob[1U] != 0U);
}

void ManagementService::persistChainLoopConfig() const {
  uint8_t blob[2] = {kChainLoopMetaVersion, static_cast<uint8_t>(chain_loop_enabled_ ? 1U : 0U)};
  (void)manager_.saveLocalMetaBlob(kChainLoopMetaSlot, blob, sizeof(blob));
}

ManagementService::LivenessPeerState* ManagementService::findLivePeerState(const MacAddress& peer) {
  auto it = std::find_if(live_monitor_.peers.begin(),
                         live_monitor_.peers.end(),
                         [&](const LivenessPeerState& s) { return s.peer == peer; });
  return (it == live_monitor_.peers.end()) ? nullptr : &(*it);
}

const ManagementService::LivenessPeerState* ManagementService::findLivePeerState(const MacAddress& peer) const {
  auto it = std::find_if(live_monitor_.peers.begin(),
                         live_monitor_.peers.end(),
                         [&](const LivenessPeerState& s) { return s.peer == peer; });
  return (it == live_monitor_.peers.end()) ? nullptr : &(*it);
}

void ManagementService::removeLivePeerState(const MacAddress& peer) {
  live_monitor_.peers.erase(std::remove_if(live_monitor_.peers.begin(),
                                           live_monitor_.peers.end(),
                                           [&](const LivenessPeerState& s) { return s.peer == peer; }),
                            live_monitor_.peers.end());
}

void ManagementService::syncLiveMonitorPeers() {
  std::vector<MacAddress> persisted{};
  manager_.getPersistedPeers(persisted);

  live_monitor_.peers.erase(
      std::remove_if(live_monitor_.peers.begin(),
                     live_monitor_.peers.end(),
                     [&](const LivenessPeerState& state) {
                       return std::find(persisted.begin(), persisted.end(), state.peer) == persisted.end();
                     }),
      live_monitor_.peers.end());

  for (const auto& peer : persisted) {
    LivenessPeerState* state = findLivePeerState(peer);
    if (state == nullptr) {
      LivenessPeerState created{};
      created.peer = peer;
      created.online = true;
      created.last_seen_ms = now_ms_;
      live_monitor_.peers.push_back(created);
      continue;
    }
    if (state->last_seen_ms == 0U) {
      state->last_seen_ms = now_ms_;
    }
  }
}

uint16_t ManagementService::liveMonitorIgnoreReasonMask() const {
  uint16_t mask = 0U;
  if (ota_push_local_.active) {
    mask |= kLiveIgnoreReasonOtaPush;
  }
  if (ota_update_local_.active) {
    mask |= kLiveIgnoreReasonOtaUpdate;
  }
  if (live_monitor_critical_inflight_) {
    mask |= kLiveIgnoreReasonCriticalInflight;
  }
  if (live_monitor_master_update_guard_until_ms_ != 0U &&
      static_cast<int32_t>(now_ms_ - live_monitor_master_update_guard_until_ms_) < 0) {
    mask |= kLiveIgnoreReasonMasterUpdateGuard;
  }
  return mask;
}

void ManagementService::pauseTelemetryPushForPeerBestEffort(const MacAddress& peer, uint32_t corr_id) {
  if (local_role_ != Role::Master) {
    return;
  }
  if (!manager_.hasPersistedPair(peer)) {
    return;
  }
  TelemetryPushCommand cmd{};
  cmd.action = TelemetryPushAction::Pause;
  const uint32_t use_corr = (corr_id == 0U) ? 1U : corr_id;
  (void)manager_.sendTelemetryPushCommand(peer, cmd, use_corr);
}

void ManagementService::pauseTelemetryPushForAllPeersBestEffort(uint32_t corr_seed) {
  if (local_role_ != Role::Master) {
    return;
  }
  std::vector<MacAddress> peers{};
  manager_.getPersistedPeers(peers);
  TelemetryPushCommand cmd{};
  cmd.action = TelemetryPushAction::Pause;
  uint32_t corr = (corr_seed == 0U) ? 1U : corr_seed;
  for (const auto& peer : peers) {
    (void)manager_.sendTelemetryPushCommand(peer, cmd, corr++);
    if (corr == 0U) {
      corr = 1U;
    }
  }
}

void ManagementService::emitLivePeerTransition(const MacAddress& peer, bool online, uint8_t reason_code) {
  if (!live_monitor_.enabled) {
    return;
  }
  live_monitor_.last_transition_ms = now_ms_;
  std::vector<uint8_t> payload;
  appendU8(payload, 1U);  // schema_version
  payload.insert(payload.end(), peer.begin(), peer.end());
  appendU8(payload, online ? 0U : 1U);
  appendU8(payload, reason_code);
  appendU32(payload, now_ms_);
  queueEvent({ManagementEventId::PeerLivenessTransition,
              ManagementSource::Unknown,
              0,
              0,
              ManagementStatus::Ok,
              payload});
}

void ManagementService::noteLivePeerSeen(const MacAddress& peer, uint8_t reason_code) {
  if (!manager_.hasPersistedPair(peer)) {
    return;
  }
  LivenessPeerState* state = findLivePeerState(peer);
  if (state == nullptr) {
    LivenessPeerState created{};
    created.peer = peer;
    created.online = true;
    created.last_seen_ms = now_ms_;
    live_monitor_.peers.push_back(created);
    return;
  }

  uint8_t transition_reason = reason_code;
  if (state->probe_pending) {
    transition_reason = kLiveTransitionReasonProbeSuccess;
  }
  state->last_seen_ms = now_ms_;
  state->probe_pending = false;
  state->probe_sent_ms = 0U;
  state->probe_fail_count = 0U;
  if (!state->online) {
    state->online = true;
    emitLivePeerTransition(peer, true, transition_reason);
  }
}

bool ManagementService::hasPendingTargetRequest(const MacAddress& peer) const {
  for (const auto& pending : request_queue_) {
    if (pending.request.has_target_peer && pending.request.target_peer == peer) {
      return true;
    }
  }
  return false;
}

uint32_t ManagementService::allocateInternalCorrelation_() {
  uint32_t corr = next_internal_corr_id_;
  if (corr == 0U || corr < 0x80000000U) {
    corr = 0x80000000U;
  }
  next_internal_corr_id_ = corr + 1U;
  if (next_internal_corr_id_ == 0U || next_internal_corr_id_ < 0x80000000U) {
    next_internal_corr_id_ = 0x80000000U;
  }
  return corr;
}

bool ManagementService::hasPendingDescriptorPullRequest_(ManagementSource source, uint32_t req_id) const {
  if (req_id == 0U) {
    return false;
  }
  for (const auto& pending : pending_descriptor_pulls_) {
    if (source != ManagementSource::Unknown && pending.source != source) {
      continue;
    }
    if (pending.req_id == req_id) {
      return true;
    }
    if (pending.wait_topology_stage_done) {
      if (pending.corr_last == req_id) {
        return true;
      }
      continue;
    }
    const uint32_t corr_first = (pending.corr_first == 0U) ? pending.req_id : pending.corr_first;
    const uint32_t corr_last = (pending.corr_last == 0U) ? pending.req_id : pending.corr_last;
    if (req_id >= corr_first && req_id <= corr_last) {
      return true;
    }
  }
  for (const auto& deferred : deferred_topology_commits_) {
    if (deferred.req_id == req_id &&
        (source == ManagementSource::Unknown || deferred.source == source)) {
      return true;
    }
  }
  return false;
}

const ManagementService::PendingDescriptorPull* ManagementService::findPendingDescriptorPullByCorrelation_(
    const MacAddress& peer,
    uint32_t req_id) const {
  if (req_id == 0U) {
    return nullptr;
  }
  for (const auto& pending : pending_descriptor_pulls_) {
    if (pending.peer != peer) {
      continue;
    }
    if (pending.wait_topology_stage_done) {
      if (pending.corr_last == req_id) {
        return &pending;
      }
      continue;
    }
    const uint32_t corr_first = (pending.corr_first == 0U) ? pending.req_id : pending.corr_first;
    const uint32_t corr_last = (pending.corr_last == 0U) ? pending.req_id : pending.corr_last;
    if (req_id >= corr_first && req_id <= corr_last) {
      return &pending;
    }
  }
  return nullptr;
}

bool ManagementService::hasPendingTopologyStageForPeer_(const MacAddress& peer) const {
  for (const auto& pending : pending_descriptor_pulls_) {
    if (pending.peer == peer &&
        pending.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyStageSet)) {
      return true;
    }
  }
  return false;
}

bool ManagementService::isCriticalLiveMonitorCommand(uint16_t cmd_id) const {
  const ManagementCommandId cmd = static_cast<ManagementCommandId>(cmd_id);
  switch (cmd) {
    case ManagementCommandId::RestartSlaveRequest:
    case ManagementCommandId::ResetSlaveRequest:
    case ManagementCommandId::RestartMasterRequest:
    case ManagementCommandId::ResetMasterRequest:
    case ManagementCommandId::StorageFormat:
    case ManagementCommandId::OtaApply:
    case ManagementCommandId::OtaRollback:
    case ManagementCommandId::OtaTransferBegin:
    case ManagementCommandId::OtaTransferChunk:
    case ManagementCommandId::OtaTransferEnd:
    case ManagementCommandId::OtaTransferAbort:
    case ManagementCommandId::OtaPushStart:
    case ManagementCommandId::OtaPushAbort:
    case ManagementCommandId::OtaUpdateStart:
    case ManagementCommandId::OtaMasterUpdateStart:
    case ManagementCommandId::TopologyStageSet:
    case ManagementCommandId::TopologyCommit:
    case ManagementCommandId::ChainLoopControlSet:
      return true;
    default:
      return false;
  }
}

void ManagementService::pumpLiveMonitor() {
  syncLiveMonitorPeers();
  const bool was_ignore = live_monitor_.ignore_active;
  live_monitor_.ignore_reason_mask = liveMonitorIgnoreReasonMask();
  live_monitor_.ignore_active = (live_monitor_.ignore_reason_mask != 0U);

  if (!live_monitor_.enabled) {
    live_monitor_.next_probe_due_ms = 0U;
    return;
  }

  if (live_monitor_.ignore_active) {
    live_monitor_.next_probe_due_ms = 0U;
    return;
  }

  const uint32_t probe_trigger_age_ms = liveProbeTriggerAgeMs();
  const uint32_t probe_urgency_age_ms = liveProbeUrgencyAgeMs();
  const uint32_t probe_timeout_ms = liveProbeTimeoutMs(live_monitor_.peers.size());
  uint8_t probe_budget = liveProbeBudgetPerPump(live_monitor_.peers.size());
  size_t offline_count = 0U;
  for (const auto& peer : live_monitor_.peers) {
    if (!peer.online) {
      ++offline_count;
    }
  }
  if (offline_count > 0U) {
    const uint8_t recovery_budget = (live_monitor_.peers.size() <= 4U) ? 3U : 2U;
    probe_budget = std::max<uint8_t>(probe_budget, recovery_budget);
  }
  auto trackNextDue = [](uint32_t& next_due_ms, uint32_t due_ms) {
    if (due_ms < next_due_ms) {
      next_due_ms = due_ms;
    }
  };
  uint32_t next_due_ms = 0xFFFFFFFFU;

  if (was_ignore) {
    for (auto& peer : live_monitor_.peers) {
      peer.probe_pending = false;
      peer.probe_sent_ms = 0U;
      peer.probe_fail_count = 0U;
      const uint32_t age_ms = now_ms_ - peer.last_seen_ms;
      if (age_ms >= kLiveOfflineDetectMaxMs && peer.online) {
        peer.online = false;
        emitLivePeerTransition(peer.peer, false, kLiveTransitionReasonResumeRecheckTimeout);
      } else if (age_ms < kLiveOfflineDetectMaxMs) {
        trackNextDue(next_due_ms, kLiveOfflineDetectMaxMs - age_ms);
      }
    }
  }

  for (auto& peer : live_monitor_.peers) {
    const uint32_t age_ms = now_ms_ - peer.last_seen_ms;
    if (age_ms >= kLiveOfflineDetectMaxMs) {
      peer.probe_pending = false;
      peer.probe_sent_ms = 0U;
      if (peer.online) {
        peer.online = false;
        emitLivePeerTransition(peer.peer, false, kLiveTransitionReasonProbeTimeoutThreshold);
      }
      continue;
    }

    if (!peer.probe_pending) {
      if (age_ms < probe_trigger_age_ms) {
        trackNextDue(next_due_ms, probe_trigger_age_ms - age_ms);
      } else {
        trackNextDue(next_due_ms, 1U);
      }
      continue;
    }

    const uint32_t probe_age_ms = now_ms_ - peer.probe_sent_ms;
    if (probe_age_ms < probe_timeout_ms) {
      trackNextDue(next_due_ms, probe_timeout_ms - probe_age_ms);
      continue;
    }

    peer.probe_pending = false;
    peer.probe_sent_ms = 0U;
    if (peer.probe_fail_count < 0xFFU) {
      ++peer.probe_fail_count;
    }
    if (age_ms >= probe_urgency_age_ms) {
      trackNextDue(next_due_ms, 20U);
    } else if (age_ms < probe_trigger_age_ms) {
      trackNextDue(next_due_ms, probe_trigger_age_ms - age_ms);
    } else {
      trackNextDue(next_due_ms, 80U);
    }
  }

  if (pull_ == nullptr || live_monitor_.peers.empty() || probe_budget == 0U) {
    live_monitor_.next_probe_due_ms = (next_due_ms == 0xFFFFFFFFU) ? 0U : next_due_ms;
    return;
  }

  const size_t peer_count = live_monitor_.peers.size();
  const size_t start_index = static_cast<size_t>(live_monitor_.probe_rr_cursor) % peer_count;
  uint8_t probes_sent = 0U;
  for (size_t offset = 0; offset < peer_count; ++offset) {
    const size_t idx = (start_index + offset) % peer_count;
    LivenessPeerState& peer = live_monitor_.peers[idx];
    if (peer.probe_pending) {
      continue;
    }

    const uint32_t age_ms = now_ms_ - peer.last_seen_ms;
    if (age_ms >= kLiveOfflineDetectMaxMs && peer.online) {
      peer.online = false;
      emitLivePeerTransition(peer.peer, false, kLiveTransitionReasonProbeTimeoutThreshold);
    }

    // Online peers are only probed near deadline; offline peers are actively probed for recovery.
    if (peer.online && age_ms < probe_trigger_age_ms) {
      trackNextDue(next_due_ms, probe_trigger_age_ms - age_ms);
      continue;
    }

    const bool near_deadline = peer.online && (age_ms >= probe_urgency_age_ms);
    if (hasPendingTargetRequest(peer.peer) && !near_deadline) {
      trackNextDue(next_due_ms, 50U);
      continue;
    }

    uint32_t corr_id = live_monitor_.next_probe_corr_id++;
    if (corr_id == 0U) {
      corr_id = 1U;
      live_monitor_.next_probe_corr_id = 2U;
    }

    if (pull_->requestLiveness(peer.peer, corr_id)) {
      peer.probe_pending = true;
      peer.probe_sent_ms = now_ms_;
      trackNextDue(next_due_ms, probe_timeout_ms);
      ++probes_sent;
      if (probes_sent >= probe_budget) {
        break;
      }
      continue;
    }

    trackNextDue(next_due_ms, near_deadline ? 20U : 50U);
  }

  if (peer_count > 0U) {
    live_monitor_.probe_rr_cursor = static_cast<uint8_t>((start_index + 1U) % peer_count);
  }

  live_monitor_.next_probe_due_ms = (next_due_ms == 0xFFFFFFFFU) ? 0U : next_due_ms;
}


}  // namespace espnow_link

