#pragma once

namespace {

constexpr uint16_t kLogSourceManager = 0x0101;
constexpr uint16_t kLogEvtBeginOk = 0x0001;
constexpr uint16_t kLogEvtBeginFail = 0x0002;
constexpr uint16_t kLogEvtRestoreOk = 0x0003;
constexpr uint16_t kLogEvtRestoreFail = 0x0004;
constexpr uint16_t kLogEvtRxDecodeFail = 0x0005;
constexpr uint16_t kLogEvtRxVersionDrop = 0x0006;
constexpr uint16_t kLogEvtTxEncodeFail = 0x0007;
constexpr uint16_t kLogEvtTxSendFail = 0x0008;
constexpr uint16_t kLogEvtPairReq = 0x0009;
constexpr uint16_t kLogEvtUnpairReq = 0x000A;
constexpr uint16_t kLogEvtRestoreTrimDrop = 0x000B;
constexpr uint16_t kLogEvtRestoreTrimSummary = 0x000C;
constexpr uint16_t kLogEvtTopologyLmkRestore = 0x000D;
constexpr uint16_t kLogEvtTopologyPeerReady = 0x000E;
constexpr uint16_t kLogEvtTopologyLmkNvsLifecycle = 0x000F;
constexpr uint16_t kOtaBootCompleteEventId = 0x7F10;
constexpr uint16_t kOtaTransferReadyEventId = 0x7F11;
constexpr uint8_t kFirmwareStatusKindChunkAck = 0x01;
constexpr uint8_t kFirmwareStatusKindChunkNack = 0x02;
constexpr uint8_t kFirmwareStatusKindFinalizeOk = 0x03;
constexpr uint8_t kFirmwareStatusKindFinalizeFail = 0x04;
constexpr uint8_t kFirmwareStatusKindFinalizeAck = 0x05;
constexpr uint32_t kFinalizeStatusRetryIntervalMs = 400U;
constexpr uint32_t kFinalizeStatusRetryWindowMs = 12000U;
constexpr uint8_t kLoggerMetaSlot = 2;
constexpr uint8_t kLoggerMetaVersion = 1;
constexpr size_t kLoggerMetaSize = 3;
constexpr uint8_t kPeerRoleMetaSlot = 3;
constexpr uint8_t kPeerRoleMetaVersion = 1;
constexpr size_t kPeerRoleMetaSize = 2;
constexpr uint8_t kFirmwareMetaMagic = 0xA5;
constexpr uint8_t kFirmwareMetaVersionV1 = 1;
constexpr uint8_t kFirmwareMetaVersionV2 = 2;
constexpr size_t kFirmwareMetaHeaderSizeV1 = 4;
constexpr size_t kFirmwareMetaHeaderSizeV2 = 5;
constexpr size_t kRestorePairLimit = 14;
constexpr uint8_t kRestoreTrimSchemaVersion = 1;
constexpr uint8_t kRestoreTrimActionDrop = 1;
constexpr uint8_t kRestoreTrimActionSummary = 2;
constexpr char kRestoreTrimReason[] = "restore_over_capacity";
constexpr uint8_t kTopologyMetaSlotStaged = 0x32;
constexpr uint8_t kTopologyMetaSlotCommitted = 0x33;
constexpr uint8_t kTopologyMetaSlotLmk = 0x34;
constexpr uint8_t kTopologyBlobVersion = 1;
constexpr size_t kTopologySeedSize = 32U;
constexpr size_t kTopologySlotBlobSize = 17U;
constexpr size_t kTopologyGroupBlobSize = 34U;
constexpr size_t kTopologyHeaderBlobSize = 16U;
constexpr size_t kTopologyBlobSize =
    kTopologyHeaderBlobSize +
    (espnow_link::EspNowManager::kTopologyMaxSlots * kTopologySlotBlobSize) +
    (espnow_link::EspNowManager::kTopologyMaxGroups * kTopologyGroupBlobSize);
constexpr uint8_t kTopologyLmkBlobVersionV1 = 1U;
constexpr uint8_t kTopologyLmkBlobVersionV2 = 2U;
constexpr size_t kTopologyLmkBlobEntrySize = 23U;  // mac(6)+lmk(16)+group(1)
constexpr size_t kTopologyLmkBlobHeaderSizeV2 = 23U;
constexpr uint32_t kTopologyTriggerDedupWindowMs = 15000U;
constexpr size_t kTopologyTriggerDedupMaxEntries = 32U;
constexpr uint8_t kTopologyTriggerResultAccepted = 0U;
constexpr uint8_t kTopologyTriggerResultRejected = 1U;
constexpr uint8_t kTopologyTriggerReasonOk = 0x00;
constexpr uint8_t kTopologyTriggerReasonUnauthorizedPeer = 0x01;
constexpr uint8_t kTopologyTriggerReasonStaleTopology = 0x02;
constexpr uint8_t kTopologyTriggerReasonBadIndex = 0x03;
constexpr uint8_t kTopologyTriggerReasonRangeRejected = 0x04;
constexpr uint8_t kTopologyTriggerReasonBusy = 0x05;
constexpr size_t kPullRequestDispatchBudgetPerTick = 1U;
constexpr size_t kPullResponseDispatchBudgetPerTick = 4U;
constexpr size_t kFirmwareRxDispatchBudgetPerTick = 4U;
constexpr size_t kControlRxDispatchBudgetPerTick = 4U;
constexpr uint8_t kTopologyRestoreModeNone = 0U;
constexpr uint8_t kTopologyRestoreModeNvs = 1U;
constexpr uint8_t kTopologyRestoreModeDerived = 2U;
constexpr uint8_t kTopologyRestoreModeFailed = 3U;

int compareMac(const espnow_link::MacAddress& lhs, const espnow_link::MacAddress& rhs) {
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] < rhs[i]) {
      return -1;
    }
    if (lhs[i] > rhs[i]) {
      return 1;
    }
  }
  return 0;
}

bool isOtaWireMessage(espnow_link::MessageType type) {
  return type == espnow_link::MessageType::FirmwareBegin ||
         type == espnow_link::MessageType::FirmwareChunk ||
         type == espnow_link::MessageType::FirmwareEnd ||
         type == espnow_link::MessageType::FirmwareStatus;
}

void appendU16Le(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
}

void appendU32Le(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
}

bool readU16Le(const uint8_t* in, size_t len, uint16_t& out) {
  if (in == nullptr || len < 2U) {
    return false;
  }
  out = static_cast<uint16_t>(in[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8);
  return true;
}

bool readU32Le(const uint8_t* in, size_t len, uint32_t& out) {
  if (in == nullptr || len < 4U) {
    return false;
  }
  out = static_cast<uint32_t>(in[0]) |
        (static_cast<uint32_t>(in[1]) << 8) |
        (static_cast<uint32_t>(in[2]) << 16) |
        (static_cast<uint32_t>(in[3]) << 24);
  return true;
}

bool isAllZeroMac(const espnow_link::MacAddress& mac) {
  for (uint8_t b : mac) {
    if (b != 0U) {
      return false;
    }
  }
  return true;
}

bool isVirtualIndexAllowedForRole(uint8_t role_code, uint8_t vi) {
  if (vi == 0xFFU) {
    return true;
  }
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileSemu & 0xFFU)) {
    return vi <= 7U;
  }
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileRemu & 0xFFU)) {
    return vi <= 15U;
  }
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileSens & 0xFFU) ||
      role_code == static_cast<uint8_t>(espnow_link::kProfileRelay & 0xFFU) ||
      role_code == static_cast<uint8_t>(espnow_link::kProfilePms & 0xFFU)) {
    return false;
  }
  return vi <= 15U;
}

bool isBroadcastMac(const espnow_link::MacAddress& mac) {
  for (uint8_t b : mac) {
    if (b != 0xFFU) {
      return false;
    }
  }
  return true;
}

bool isAllZeroSeed(const std::array<uint8_t, 32>& seed) {
  for (uint8_t b : seed) {
    if (b != 0U) {
      return false;
    }
  }
  return true;
}

bool isSensorProfileCode(uint8_t code) {
  return code == static_cast<uint8_t>(espnow_link::kProfileSens & 0xFFU) ||
         code == static_cast<uint8_t>(espnow_link::kProfileSemu & 0xFFU);
}

bool isRelayProfileCode(uint8_t code) {
  return code == static_cast<uint8_t>(espnow_link::kProfileRelay & 0xFFU) ||
         code == static_cast<uint8_t>(espnow_link::kProfileRemu & 0xFFU);
}

uint8_t topologySlotCapForLocalRole(uint8_t /*local_code*/) {
  return static_cast<uint8_t>(espnow_link::EspNowManager::kTopologyMaxSlots);
}

bool isTopologyRolePairAllowed(uint8_t local_code, uint8_t peer_code) {
  if (isSensorProfileCode(local_code)) {
    return isRelayProfileCode(peer_code);
  }
  if (isRelayProfileCode(local_code)) {
    return isSensorProfileCode(peer_code);
  }
  return true;
}

void appendTopologySlotBlob(std::vector<uint8_t>& out,
                            const espnow_link::EspNowManager::TopologySlot& slot) {
  out.push_back(static_cast<uint8_t>(slot.enabled ? 1U : 0U));
  out.insert(out.end(), slot.peer.begin(), slot.peer.end());
  out.push_back(slot.peer_role);
  out.push_back(slot.group_id);
  out.push_back(static_cast<uint8_t>(slot.relative_index));
  out.push_back(slot.local_virtual_index);
  out.push_back(slot.peer_virtual_index);
  out.push_back(static_cast<uint8_t>(slot.axis_order));
  appendU16Le(out, slot.delay_ms);
  appendU16Le(out, slot.hold_ms);
}

bool readTopologySlotBlob(const uint8_t* in,
                          size_t len,
                          espnow_link::EspNowManager::TopologySlot& out_slot) {
  if (in == nullptr || len < kTopologySlotBlobSize) {
    return false;
  }
  out_slot = espnow_link::EspNowManager::TopologySlot{};
  out_slot.enabled = (in[0] != 0U);
  std::memcpy(out_slot.peer.data(), in + 1U, out_slot.peer.size());
  out_slot.peer_role = in[7U];
  out_slot.group_id = in[8U];
  out_slot.relative_index = static_cast<int8_t>(in[9U]);
  out_slot.local_virtual_index = in[10U];
  out_slot.peer_virtual_index = in[11U];
  out_slot.axis_order = static_cast<int8_t>(in[12U]);
  if (!readU16Le(in + 13U, len - 13U, out_slot.delay_ms)) {
    return false;
  }
  if (!readU16Le(in + 15U, len - 15U, out_slot.hold_ms)) {
    return false;
  }
  return true;
}

void appendTopologyGroupBlob(std::vector<uint8_t>& out,
                             const espnow_link::EspNowManager::TopologyGroupSeed& group) {
  out.push_back(static_cast<uint8_t>(group.enabled ? 1U : 0U));
  out.push_back(group.group_id);
  out.insert(out.end(), group.seed.begin(), group.seed.end());
}

bool readTopologyGroupBlob(const uint8_t* in,
                           size_t len,
                           espnow_link::EspNowManager::TopologyGroupSeed& out_group) {
  if (in == nullptr || len < kTopologyGroupBlobSize) {
    return false;
  }
  out_group = espnow_link::EspNowManager::TopologyGroupSeed{};
  out_group.enabled = (in[0] != 0U);
  out_group.group_id = in[1U];
  std::memcpy(out_group.seed.data(), in + 2U, out_group.seed.size());
  return true;
}

bool serializeTopologySnapshotBlob(const espnow_link::EspNowManager::TopologySnapshot& snapshot,
                                   std::vector<uint8_t>& out_blob) {
  out_blob.clear();
  out_blob.reserve(kTopologyBlobSize);
  out_blob.push_back(kTopologyBlobVersion);
  out_blob.push_back(snapshot.schema_version);
  out_blob.push_back(static_cast<uint8_t>(snapshot.state));
  out_blob.push_back(0U);  // reserved
  appendU32Le(out_blob, snapshot.topology_version);
  out_blob.push_back(snapshot.index_neg);
  out_blob.push_back(snapshot.index_pos);
  out_blob.push_back(snapshot.enabled_slot_count);
  out_blob.push_back(snapshot.enabled_group_count);
  appendU32Le(out_blob, snapshot.checksum);

  for (const auto& slot : snapshot.slots) {
    appendTopologySlotBlob(out_blob, slot);
  }
  for (const auto& group : snapshot.groups) {
    appendTopologyGroupBlob(out_blob, group);
  }
  return out_blob.size() == kTopologyBlobSize;
}

bool deserializeTopologySnapshotBlob(const std::vector<uint8_t>& blob,
                                     espnow_link::EspNowManager::TopologySnapshot& out_snapshot) {
  out_snapshot = espnow_link::EspNowManager::TopologySnapshot{};
  if (blob.size() != kTopologyBlobSize) {
    return false;
  }
  if (blob[0U] != kTopologyBlobVersion) {
    return false;
  }
  out_snapshot.schema_version = blob[1U];
  out_snapshot.state = static_cast<espnow_link::EspNowManager::TopologyState>(blob[2U]);
  if (!readU32Le(blob.data() + 4U, blob.size() - 4U, out_snapshot.topology_version)) {
    return false;
  }
  out_snapshot.index_neg = blob[8U];
  out_snapshot.index_pos = blob[9U];
  out_snapshot.enabled_slot_count = blob[10U];
  out_snapshot.enabled_group_count = blob[11U];
  if (!readU32Le(blob.data() + 12U, blob.size() - 12U, out_snapshot.checksum)) {
    return false;
  }

  size_t off = kTopologyHeaderBlobSize;
  for (auto& slot : out_snapshot.slots) {
    if (!readTopologySlotBlob(blob.data() + off, blob.size() - off, slot)) {
      return false;
    }
    off += kTopologySlotBlobSize;
  }
  for (auto& group : out_snapshot.groups) {
    if (!readTopologyGroupBlob(blob.data() + off, blob.size() - off, group)) {
      return false;
    }
    off += kTopologyGroupBlobSize;
  }
  return off == blob.size();
}

std::vector<uint8_t> makeRestoreTrimDropExt(const espnow_link::MacAddress& peer,
                                            uint16_t kept_count,
                                            uint16_t drop_index) {
  std::vector<uint8_t> ext;
  const size_t reason_len = std::strlen(kRestoreTrimReason);
  ext.reserve(2 + peer.size() + 2 + 2 + reason_len);
  ext.push_back(kRestoreTrimSchemaVersion);
  ext.push_back(kRestoreTrimActionDrop);
  ext.insert(ext.end(), peer.begin(), peer.end());
  appendU16Le(ext, kept_count);
  appendU16Le(ext, drop_index);
  ext.insert(ext.end(), kRestoreTrimReason, kRestoreTrimReason + reason_len);
  return ext;
}

std::vector<uint8_t> makeRestoreTrimSummaryExt(uint16_t kept_count, uint16_t dropped_count) {
  std::vector<uint8_t> ext;
  const size_t reason_len = std::strlen(kRestoreTrimReason);
  ext.reserve(2 + 2 + 2 + reason_len);
  ext.push_back(kRestoreTrimSchemaVersion);
  ext.push_back(kRestoreTrimActionSummary);
  appendU16Le(ext, kept_count);
  appendU16Le(ext, dropped_count);
  ext.insert(ext.end(), kRestoreTrimReason, kRestoreTrimReason + reason_len);
  return ext;
}

}  // namespace
