/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Runtime target/profile state, discovery/event handling, and OTA frontend command entry points.
 **************************************************************/
#include "../internal/cli_master_internal.hpp"
#include "../internal/cli_master_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

void MasterCli::clearPeerSessionState_() {
  auto_pull_.resetState();
  auto_pull_has_target_peer_ = false;
  auto_pull_target_peer_ = {};
  descriptor_request_queue_.clear();
  clearPagedFetchState();
  remote_profile_id_ = kProfileUnknown;
  remote_settings_count_ = 0;
  child_push_peer_states_.clear();
  remote_log_pull_active_ = false;
  remote_log_pull_waiting_status_ = false;
  remote_log_pull_has_target_peer_ = false;
  remote_log_pull_target_peer_ = {};
  remote_storage_cwd_ = "/";
  remote_storage_cd_pending_.clear();
  ota_push_active_ = false;
  ota_push_has_target_peer_ = false;
  ota_push_target_peer_ = {};
  ota_push_path_.clear();
  ota_push_size_bytes_ = 0;
  ota_push_crc32_ = 0;
  ota_push_offset_ = 0;
  ota_push_chunks_sent_ = 0;
  ota_push_corr_id_ = 0;
  ota_push_send_fail_streak_ = 0;
  ota_push_next_send_ms_ = 0;
  ota_push_phase_ = OtaPushPhase::Idle;
  ota_push_started_ms_ = 0;
  ota_push_last_activity_ms_ = 0;
  ota_push_last_status_req_ms_ = 0;
  ota_update_req_id_ = 0U;
  ota_update_has_target_peer_ = false;
  ota_update_target_peer_ = {};
  probe_pending_kind_ = ProbePendingKind::None;
  probe_sent_ms_ = 0;
  clearTopologyVerifySession_();
  clearActiveTarget_();
  peer_profile_cache_.clear();
}

void MasterCli::clearActiveTarget_() {
  sticky_target_active_ = false;
  sticky_target_peer_ = {};
  remote_profile_id_ = kProfileUnknown;
  remote_settings_count_ = 0U;
}

bool MasterCli::setActiveTargetBySelector_(const std::string& selector, MacAddress* out_peer) {
  const std::string arg = trim(selector);
  if (arg.empty()) {
    return false;
  }
  std::vector<EspNowManager::PersistedPeerRoleEntry> persisted{};
  manager_.getPersistedPeersWithRole(persisted);
  if (persisted.empty()) {
    return false;
  }

  MacAddress selected{};
  ProfileId hinted_profile = kProfileUnknown;
  bool ok = false;
  bool all_digits = true;
  for (char c : arg) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      all_digits = false;
      break;
    }
  }
  if (all_digits) {
    const unsigned long idx = std::strtoul(arg.c_str(), nullptr, 10);
    if (idx < persisted.size()) {
      const auto& entry = persisted[static_cast<size_t>(idx)];
      selected = entry.peer;
      hinted_profile = profileIdFromRoleCode(entry.role_code);
      ok = true;
    }
  } else {
    MacAddress mac{};
    if (parseMac(arg, mac) && manager_.hasPersistedPair(mac)) {
      selected = mac;
      for (const auto& entry : persisted) {
        if (entry.peer == mac) {
          hinted_profile = profileIdFromRoleCode(entry.role_code);
          break;
        }
      }
      ok = true;
    }
  }
  if (!ok) {
    return false;
  }

  sticky_target_active_ = true;
  sticky_target_peer_ = selected;
  if (hinted_profile != kProfileUnknown) {
    upsertCachedRemoteProfile_(selected, hinted_profile);
  }
  refreshRuntimeProfileHint_();
  if (out_peer != nullptr) {
    *out_peer = selected;
  }
  return true;
}

bool MasterCli::resolveRuntimePeer(MacAddress& out_peer) const {
  if (command_target_override_active_) {
    out_peer = command_target_override_peer_;
    return true;
  }
  if (sticky_target_active_ && manager_.hasPersistedPair(sticky_target_peer_)) {
    out_peer = sticky_target_peer_;
    return true;
  }
  return false;
}

bool MasterCli::hasRuntimePeer() const {
  MacAddress peer{};
  return resolveRuntimePeer(peer);
}

bool MasterCli::getCachedRemoteProfile_(const MacAddress& peer, ProfileId& out_profile) const {
  out_profile = kProfileUnknown;
  for (const auto& entry : peer_profile_cache_) {
    if (entry.peer == peer) {
      out_profile = entry.profile_id;
      return out_profile != kProfileUnknown;
    }
  }
  return false;
}

void MasterCli::upsertCachedRemoteProfile_(const MacAddress& peer, ProfileId profile_id) {
  if (profile_id == kProfileUnknown) {
    return;
  }
  for (auto& entry : peer_profile_cache_) {
    if (entry.peer == peer) {
      entry.profile_id = profile_id;
      return;
    }
  }
  PeerProfileCacheEntry add{};
  add.peer = peer;
  add.profile_id = profile_id;
  peer_profile_cache_.push_back(add);
}

void MasterCli::eraseCachedRemoteProfile_(const MacAddress& peer) {
  peer_profile_cache_.erase(
      std::remove_if(peer_profile_cache_.begin(),
                     peer_profile_cache_.end(),
                     [&](const PeerProfileCacheEntry& e) { return e.peer == peer; }),
      peer_profile_cache_.end());
}

void MasterCli::refreshRuntimeProfileHint_() {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    remote_profile_id_ = kProfileUnknown;
    remote_settings_count_ = 0U;
    return;
  }
  ProfileId cached = kProfileUnknown;
  if (getCachedRemoteProfile_(target_peer, cached)) {
    remote_profile_id_ = cached;
  } else {
    uint8_t role_code = 0U;
    const ProfileId hinted = (manager_.loadPeerRoleHint(target_peer, role_code)
                                  ? profileIdFromRoleCode(role_code)
                                  : kProfileUnknown);
    if (hinted != kProfileUnknown) {
      upsertCachedRemoteProfile_(target_peer, hinted);
      remote_profile_id_ = hinted;
    } else {
      remote_profile_id_ = kProfileUnknown;
    }
  }
}

bool MasterCli::ensureRuntimeProfileKnown_(ProfileId& out_profile, bool trigger_probe_on_miss) {
  out_profile = kProfileUnknown;
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    remote_profile_id_ = kProfileUnknown;
    return false;
  }
  ProfileId cached = kProfileUnknown;
  if (getCachedRemoteProfile_(target_peer, cached)) {
    remote_profile_id_ = cached;
    out_profile = cached;
    return true;
  }
  uint8_t role_code = 0U;
  const ProfileId hinted = (manager_.loadPeerRoleHint(target_peer, role_code)
                                ? profileIdFromRoleCode(role_code)
                                : kProfileUnknown);
  if (hinted != kProfileUnknown) {
    upsertCachedRemoteProfile_(target_peer, hinted);
    remote_profile_id_ = hinted;
    out_profile = hinted;
    return true;
  }
  remote_profile_id_ = kProfileUnknown;
  if (trigger_probe_on_miss) {
    (void)executeDescriptorQueryNow("CAPS.GET", &target_peer);
  }
  return false;
}

MasterCli::ChildPushPeerState* MasterCli::findChildPushState_(const MacAddress& peer) {
  for (auto& state : child_push_peer_states_) {
    if (state.peer == peer) {
      return &state;
    }
  }
  return nullptr;
}

const MasterCli::ChildPushPeerState* MasterCli::findChildPushState_(const MacAddress& peer) const {
  for (const auto& state : child_push_peer_states_) {
    if (state.peer == peer) {
      return &state;
    }
  }
  return nullptr;
}

MasterCli::ChildPushPeerState& MasterCli::ensureChildPushState_(const MacAddress& peer) {
  ChildPushPeerState* state = findChildPushState_(peer);
  if (state != nullptr) {
    return *state;
  }
  ChildPushPeerState add{};
  add.peer = peer;
  child_push_peer_states_.push_back(add);
  return child_push_peer_states_.back();
}

void MasterCli::loadCliEnabled() {
  cli_enabled_ = true;
  if (persistence_ == nullptr) {
    return;
  }
  std::vector<uint8_t> blob;
  if (!persistence_->getBlob(enable_key_, blob) || blob.empty()) {
    return;
  }
  cli_enabled_ = (blob[0] != 0);
}

bool MasterCli::persistCliEnabled() {
  if (persistence_ == nullptr) {
    return true;
  }
  const uint8_t v = cli_enabled_ ? 1U : 0U;
  return persistence_->putBlob(enable_key_, &v, 1);
}

bool MasterCli::setCliEnabled(bool enabled, bool persist_state) {
  cli_enabled_ = enabled;
  if (!enabled) {
    setAutoPull(false, auto_pull_interval_ms_);
  }
  if (!persist_state) {
    return true;
  }
  return persistCliEnabled();
}

void MasterCli::upsertDiscovery(const MacAddress& peer, const std::string& node_name, int16_t rssi) {
  const uint32_t now = nowMs();
  for (auto& item : discovered_) {
    if (item.mac == peer) {
      item.last_seen_ms = now;
      item.rssi = rssi;
      item.node_name = node_name;
      return;
    }
  }
  DiscoveryItem d{};
  d.mac = peer;
  d.last_seen_ms = now;
  d.rssi = rssi;
  d.node_name = node_name;
  discovered_.push_back(d);
  if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
    writef("[MASTER] discovered[%u] %s",
           static_cast<unsigned int>(discovered_.size() - 1),
           macToPrintable(peer).c_str());
  }
}

bool MasterCli::peerByIndex(size_t index, MacAddress& out) const {
  if (index >= discovered_.size()) {
    return false;
  }
  out = discovered_[index].mac;
  return true;
}


void MasterCli::onEvent(const Event& e) {
  constexpr uint16_t kOtaStatusKindChunkAck = 0x01;
  constexpr uint16_t kOtaStatusKindChunkNack = 0x02;
  constexpr uint16_t kOtaStatusKindFinalizeOk = 0x03;
  constexpr uint16_t kOtaStatusKindFinalizeFail = 0x04;
  constexpr uint16_t kOtaStatusKindFinalizeAck = 0x05;
  if (e.type == Event::Type::DiscoverySeen) {
    if (collect_discovery_) {
      upsertDiscovery(e.peer, e.node_name, e.rssi);
    }
    return;
  }

  if (usesManagementOnlyTraffic_()) {
    ++observer_ignored_events_;
    return;
  }

  if (e.type == Event::Type::Paired) {
    auto_pull_.resetState();
    if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
      writef("[MASTER] paired with %s", macToPrintable(e.peer).c_str());
    }
    return;
  }

  if (e.type == Event::Type::PairingStep) {
    if (e.message == "discovery seen (manual pair mode)") {
      return;
    }
    if (e.message == "unpair ack received" ||
        e.message == "unpair timeout; forced local clear" ||
        e.message == "unpaired by request") {
      clearPeerSessionState_();
    }
    if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
      writef("[MASTER][PAIR] corr=%lu peer=%s step=%s",
             static_cast<unsigned long>(e.correlation_id),
             macToPrintable(e.peer).c_str(),
             e.message.c_str());
    }
    return;
  }

  if (e.type == Event::Type::MandatoryEventReceived) {
    if (management_transport_ != nullptr) {
      // When management transport is active, consume mandatory OTA lifecycle via
      // management events in pumpManagementMailbox() to keep one frontend path.
      return;
    }
    constexpr uint16_t kOtaBootCompleteEventId = 0x7F10;
    constexpr uint16_t kOtaTransferReadyEventId = 0x7F11;
    MandatoryEventItem item{};
    item.peer = e.peer;
    item.corr_id = e.correlation_id;
    item.event_id = e.event_id;
    item.severity = e.severity;
    item.event_value = e.event_value;
    item.event_ts_s = e.event_ts_s;
    item.rx_ms = nowMs();
    mandatory_events_.push_back(item);
    if (mandatory_events_.size() > 32U) {
      mandatory_events_.erase(mandatory_events_.begin());
    }
    if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
      writef("[MASTER][EVENT] peer=%s corr=%lu id=%u sev=%u value=%ld ts=%lu",
             macToPrintable(e.peer).c_str(),
             static_cast<unsigned long>(e.correlation_id),
             static_cast<unsigned int>(e.event_id),
             static_cast<unsigned int>(e.severity),
             static_cast<long>(e.event_value),
             static_cast<unsigned long>(e.event_ts_s));
    }
    if (cli_enabled_ && e.event_id == kOtaBootCompleteEventId) {
      io_.writeln("[MASTER][OTA] slave reports update completed after reboot");
      if (enqueueDescriptorQuery("DESC.GET")) {
        io_.writeln("[MASTER][OTA] auto verify queued: desc");
      }
      if (ota_update_wait_boot_notice_) {
        ota_update_prepare_pending_ = false;
        ota_update_prepare_corr_id_ = 0U;
        ota_update_staged_path_.clear();
        ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
        ota_update_wait_boot_notice_ = false;
        ota_update_pipeline_active_ = false;
        ota_update_image_name_.clear();
        io_.writeln("[MASTER][OTA] update pipeline complete");
      }
    }
    if (cli_enabled_ && e.event_id == kOtaTransferReadyEventId) {
      writef("[MASTER][OTA] slave finalize event received corr=%ld",
             static_cast<long>(e.event_value));
      if (ota_push_active_ &&
          ota_push_phase_ == OtaPushPhase::WaitEndStatus &&
          static_cast<uint32_t>(e.event_value) == ota_push_corr_id_) {
        if (ota_update_image_name_.empty()) {
          ota_update_image_name_ = otaImageNameFromCorr(ota_push_corr_id_);
        }
        stopOtaPush("complete", true);
      }
    }
    return;
  }

  if (e.type == Event::Type::OtaTransferStatus) {
    if (management_transport_ != nullptr) {
      // When management transport is active, consume OTA transfer status via
      // management events in pumpManagementMailbox().
      return;
    }
    const uint32_t transfer_corr = e.correlation_id;
    const uint16_t kind = e.event_id;
    const uint32_t offset = (e.event_value < 0) ? 0U : static_cast<uint32_t>(e.event_value);
    const uint16_t status_code = static_cast<uint16_t>(e.event_ts_s & 0xFFFFU);
    if (kind == kOtaStatusKindFinalizeOk || kind == kOtaStatusKindFinalizeFail) {
      (void)manager_.sendFirmwareFinalizeAck(e.peer, transfer_corr, offset, status_code);
    }
    if (ota_push_active_ && transfer_corr == ota_push_corr_id_) {
      ota_push_last_activity_ms_ = nowMs();
      if (kind == kOtaStatusKindChunkAck) {
        ota_push_offset_ = std::min<uint32_t>(offset, ota_push_size_bytes_);
        if (!ota_push_begin_ack_seen_ && offset == 0U) {
          ota_push_begin_ack_seen_ = true;
          if (ota_push_phase_ == OtaPushPhase::WaitBeginStatus) {
            ota_push_phase_ = OtaPushPhase::Streaming;
            io_.writeln("[MASTER][OTA] begin acknowledged by slave status; streaming chunks...");
          }
        }
        if (ota_push_size_bytes_ > 0U && offset >= ota_push_size_bytes_) {
          ota_push_phase_ = OtaPushPhase::WaitEndStatus;
        }
      } else if (kind == kOtaStatusKindChunkNack) {
        ota_push_offset_ = std::min<uint32_t>(offset, ota_push_size_bytes_);
        if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
          writef("[MASTER][OTA] nack received offset=%lu code=%s(0x%04X)",
                 static_cast<unsigned long>(ota_push_offset_),
                 otaStatusCodeName(status_code),
                 static_cast<unsigned int>(status_code));
        }
      } else if (kind == kOtaStatusKindFinalizeOk) {
        if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
          stopOtaPush("complete", true);
        }
      } else if (kind == kOtaStatusKindFinalizeFail) {
        if (cli_enabled_ && logEnabled(CliLogLevel::Error)) {
          writef("[MASTER][OTA] finalize fail code=%s(0x%04X) offset=%lu",
                 otaStatusCodeName(status_code),
                 static_cast<unsigned int>(status_code),
                 static_cast<unsigned long>(offset));
        }
        if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
          stopOtaPush("slave finalize failed", false);
        }
      } else if (kind == kOtaStatusKindFinalizeAck) {
        // Slave never sends finalize-ack to master; ignore gracefully.
      }
    }
    return;
  }

  if (e.type == Event::Type::PacketDropped) {
    if (cli_enabled_ && logEnabled(CliLogLevel::Error)) {
      writef("[MASTER][DROP] corr=%lu peer=%s reason=%s",
             static_cast<unsigned long>(e.correlation_id),
             macToPrintable(e.peer).c_str(),
             e.message.c_str());
    }
    return;
  }

  if (e.type == Event::Type::PairingFailed) {
    if (cli_enabled_ && logEnabled(CliLogLevel::Error)) {
      writef("[MASTER][PAIR][FAIL] corr=%lu peer=%s reason=%s",
             static_cast<unsigned long>(e.correlation_id),
             macToPrintable(e.peer).c_str(),
             e.message.c_str());
    }
  }
}

bool MasterCli::onPullRequest(const MacAddress&, uint32_t, const uint8_t*, size_t) {
  return true;
}

bool MasterCli::onPullResponse(const MacAddress& from,
                               uint32_t corr_id,
                               const uint8_t* payload,
                               size_t len) {
  if (auto_pull_enabled_ && auto_pull_has_target_peer_ && from == auto_pull_target_peer_) {
    bool recovered = false;
    auto_pull_.onPeerActivity(nowMs(), recovered);
    (void)recovered;
  }

  if (!shouldProcessObserverPullResponse_(corr_id)) {
    ++observer_ignored_pull_responses_;
    return true;
  }
  if (!cli_enabled_) {
    return true;
  }
  PullResponseDecoded decoded{};
  if (!pull_.decodePullResponseWithActiveCodec(payload, len, decoded)) {
    if (logEnabled(CliLogLevel::Error)) {
      writef("[MASTER] response corr=%lu from=%s len=%u (unparsed)",
             static_cast<unsigned long>(corr_id),
             macToPrintable(from).c_str(),
             static_cast<unsigned int>(len));
    }
    if (remote_log_pull_active_) {
      stopRemoteLogPull("remote response decode failed", false);
    }
    return true;
  }

  if (decoded.kind == PullResponseKind::Descriptor) {
    if (decoded.descriptor.type == DescriptorResponseType::Capabilities) {
      ProfileId profile_id = kProfileUnknown;
      if (extractProfileIdFromCapabilities(decoded.descriptor, profile_id) &&
          profile_id != kProfileUnknown) {
        upsertCachedRemoteProfile_(from, profile_id);
        MacAddress active_peer{};
        if (resolveRuntimePeer(active_peer) && active_peer == from) {
          remote_profile_id_ = profile_id;
        }
      }
    }

    if (ota_push_active_ && decoded.descriptor.type == DescriptorResponseType::OtaStatus) {
      handleOtaPushStatusResponse(decoded.descriptor);
      return true;
    }

    if (ota_update_prepare_pending_ && corr_id == ota_update_prepare_corr_id_) {
      if (decoded.descriptor.type == DescriptorResponseType::Ack) {
        ota_update_prepare_pending_ = false;
        ota_update_prepare_corr_id_ = 0U;
        if (!startOtaPush(ota_update_staged_path_, ota_update_chunk_bytes_)) {
          io_.writeln("[MASTER][OTA] update pipeline failed: push start failed after prepare");
          ota_update_pipeline_active_ = false;
          ota_update_wait_boot_notice_ = false;
          ota_update_staged_path_.clear();
          ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
        } else {
          io_.writeln("[MASTER][OTA] update pipeline: prepare acknowledged");
        }
      } else {
        io_.writeln("[MASTER][OTA] update pipeline failed: prepare rejected");
        if (!decoded.descriptor.message.empty()) {
          writef("[MASTER][OTA] prepare note=%s", decoded.descriptor.message.c_str());
        }
        ota_update_prepare_pending_ = false;
        ota_update_prepare_corr_id_ = 0U;
        ota_update_pipeline_active_ = false;
        ota_update_wait_boot_notice_ = false;
        ota_update_staged_path_.clear();
        ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
      }
      return true;
    }

    if (remote_log_pull_active_) {
      if (decoded.descriptor.type == DescriptorResponseType::Error) {
        stopRemoteLogPull(decoded.descriptor.message.c_str(), false);
        return true;
      }
      if (decoded.descriptor.type == DescriptorResponseType::LogStatus) {
        remote_log_pull_last_activity_ms_ = nowMs();
        if (remote_log_pull_waiting_status_) {
          remote_log_pull_waiting_status_ = false;
          remote_log_pull_total_bytes_ = decoded.descriptor.log_total_size;
          remote_log_pull_next_offset_ = 0;
          remote_log_pull_chunks_ = 0;

          if (!decoded.descriptor.logger_available) {
            stopRemoteLogPull("remote logger unavailable", false);
            return true;
          }
          if (remote_log_pull_total_bytes_ == 0U) {
            stopRemoteLogPull("remote logger empty", true);
            return true;
          }
          if (!requestNextRemoteLogChunk()) {
            stopRemoteLogPull("failed to request first chunk", false);
          }
        }
        return true;
      }
      if (decoded.descriptor.type == DescriptorResponseType::LogChunk) {
        remote_log_pull_last_activity_ms_ = nowMs();
        const uint32_t chunk_offset = decoded.descriptor.log_chunk_offset;
        const uint32_t total = decoded.descriptor.log_total_size;
        const size_t chunk_len = decoded.descriptor.log_chunk.size();

        if (chunk_offset != remote_log_pull_next_offset_) {
          stopRemoteLogPull("chunk offset mismatch", false);
          return true;
        }
        if (remote_log_pull_total_bytes_ == 0U) {
          remote_log_pull_total_bytes_ = total;
        }

        if (chunk_len > 0U && remote_log_store_ != nullptr) {
          if (!remote_log_store_->append(decoded.descriptor.log_chunk.data(), chunk_len)) {
            stopRemoteLogPull("append to local export store failed", false);
            return true;
          }
        }

        remote_log_pull_next_offset_ += static_cast<uint32_t>(chunk_len);
        ++remote_log_pull_chunks_;
        if (remote_log_pull_chunks_ % 8U == 0U || remote_log_pull_next_offset_ >= remote_log_pull_total_bytes_) {
          writef("[MASTER][LOGGER][REMOTE] pull progress %lu/%lu bytes chunks=%u",
                 static_cast<unsigned long>(remote_log_pull_next_offset_),
                 static_cast<unsigned long>(remote_log_pull_total_bytes_),
                 static_cast<unsigned int>(remote_log_pull_chunks_));
        }

        if (chunk_len == 0U || remote_log_pull_next_offset_ >= remote_log_pull_total_bytes_) {
          stopRemoteLogPull("remote log pull complete", true);
          return true;
        }
        if (!requestNextRemoteLogChunk()) {
          stopRemoteLogPull("failed to request next chunk", false);
        }
        return true;
      }
    }

    if (handlePagedDescriptorResponse(decoded.descriptor)) {
      return true;
    }
    if (decoded.descriptor.type == DescriptorResponseType::Error && !remote_storage_cd_pending_.empty()) {
      remote_storage_cd_pending_.clear();
    }
    const bool should_print = (decoded.descriptor.type == DescriptorResponseType::Error)
                                  ? logEnabled(CliLogLevel::Error)
                                  : logEnabled(CliLogLevel::Info);
    if (should_print) {
      printDescriptorResponse(decoded.descriptor);
    }
    return true;
  }

  if (decoded.kind == PullResponseKind::ControlResult) {
    const bool is_error = (decoded.control.result_code != 0);
    if ((is_error && logEnabled(CliLogLevel::Error)) ||
        (!is_error && logEnabled(CliLogLevel::Info))) {
      writef("[MASTER][CTRL] corr=%lu from=%s cmd=0x%04X result=0x%04X",
             static_cast<unsigned long>(corr_id),
             macToPrintable(from).c_str(),
             static_cast<unsigned int>(decoded.control.command_id),
             static_cast<unsigned int>(decoded.control.result_code));
    }
    return true;
  }

  return true;
}

bool MasterCli::otaPrepareRemote(std::string* out_message) {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    if (out_message != nullptr) {
      *out_message = "target not selected";
    }
    return false;
  }
  if (management_transport_ == nullptr) {
    if (out_message != nullptr) {
      *out_message = "management path unavailable";
    }
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::OtaClearScope),
                                         management_utils::buildStringPayloadU16("in"),
                                         nullptr,
                                         0U,
                                         false,
                                         &target_peer);
  correlation_id_ = mgmt.nextReqId();
  if (out_message != nullptr) {
    *out_message = ok ? "remote prepare requested" : "remote prepare request failed";
  }
  return ok;
}

bool MasterCli::otaPushStaged(const std::string& staged_name,
                              uint16_t chunk_bytes,
                              std::string* out_message) {
  if (staged_name.empty()) {
    if (out_message != nullptr) {
      *out_message = "empty staged name";
    }
    return false;
  }
  std::string path = resolveStagedPathInput(staged_name, local_storage_cwd_);

  const bool ok = startOtaPush(path, chunk_bytes);
  if (out_message != nullptr) {
    if (ok) {
      *out_message = "ota push started";
    } else if (ota_push_active_) {
      *out_message = "ota push already active";
    } else {
      *out_message = "ota push start failed";
    }
  }
  return ok;
}

bool MasterCli::otaAbortPush(std::string* out_message) {
  const bool had_active = ota_push_active_;
  const bool had_active_target = ota_push_has_target_peer_;
  const MacAddress active_target_peer = ota_push_target_peer_;
  if (had_active) {
    stopOtaPush("aborted by hook", false);
  }

  bool sent = false;
  MacAddress target_peer{};
  bool has_target_peer = false;
  if (had_active_target) {
    target_peer = active_target_peer;
    has_target_peer = true;
  } else if (resolveRuntimePeer(target_peer)) {
    has_target_peer = true;
  }
  if (has_target_peer && management_transport_ != nullptr) {
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    sent = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaPushAbort),
                                  {},
                                  nullptr,
                                  0U,
                                  false,
                                  &target_peer);
    correlation_id_ = mgmt.nextReqId();
  }

  if (out_message != nullptr) {
    if (!had_active && !sent) {
      *out_message = "no active push and target not selected";
    } else if (sent) {
      *out_message = had_active ? "push aborted and remote clear requested" : "remote clear requested";
    } else {
      *out_message = "push aborted";
    }
  }
  return had_active || sent;
}

bool MasterCli::otaRequestRemoteStatus(std::string* out_message) {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    if (out_message != nullptr) {
      *out_message = "target not selected";
    }
    return false;
  }
  if (management_transport_ == nullptr) {
    if (out_message != nullptr) {
      *out_message = "management path unavailable";
    }
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::OtaStatusGet),
                                         {},
                                         nullptr,
                                         0U,
                                         false,
                                         &target_peer);
  correlation_id_ = mgmt.nextReqId();
  if (out_message != nullptr) {
    *out_message = ok ? "ota status requested" : "ota status request failed";
  }
  return ok;
}

bool MasterCli::otaRequestRemoteManifest(std::string* out_message) {
  if (!hasRuntimePeer()) {
    if (out_message != nullptr) {
      *out_message = "target not selected";
    }
    return false;
  }
  const bool ok = startPagedFetch(PagedFetchKind::OtaManifest, 8, "[MASTER][OTA] manifest paged fetch queued");
  if (out_message != nullptr) {
    *out_message = ok ? "ota manifest paged fetch queued" : "ota manifest request failed";
  }
  return ok;
}

bool MasterCli::otaApplyRemote(const std::string& target, std::string* out_message) {
  const std::string trimmed = trim(target);
  if (trimmed.empty()) {
    if (out_message != nullptr) {
      *out_message = "empty apply target";
    }
    return false;
  }
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    if (out_message != nullptr) {
      *out_message = "target not selected";
    }
    return false;
  }
  if (management_transport_ == nullptr) {
    if (out_message != nullptr) {
      *out_message = "management path unavailable";
    }
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::OtaApply),
                                         management_utils::buildStringPayloadU16(trimmed),
                                         nullptr,
                                         0U,
                                         false,
                                         &target_peer);
  correlation_id_ = mgmt.nextReqId();
  if (out_message != nullptr) {
    *out_message = ok ? "ota apply requested" : "ota apply request failed";
  }
  return ok;
}

bool MasterCli::otaUpdateRemote(const std::string& staged_name,
                                uint16_t chunk_bytes,
                                std::string* out_message) {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    if (out_message != nullptr) {
      *out_message = "target not selected";
    }
    return false;
  }
  if (chunk_bytes < 32U || chunk_bytes > 220U) {
    if (out_message != nullptr) {
      *out_message = "invalid chunk_bytes (32..220)";
    }
    return false;
  }
  if (management_transport_ == nullptr) {
    if (out_message != nullptr) {
      *out_message = "management path unavailable";
    }
    return false;
  }

  const std::string path = resolveStagedPathInput(staged_name, local_storage_cwd_);
  if (path.empty()) {
    if (out_message != nullptr) {
      *out_message = "empty staged path";
    }
    return false;
  }

  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  uint32_t req_id = 0U;
  const bool update_sent = submitRuntimeTargeted_(mgmt,
                                                  static_cast<uint16_t>(ManagementCommandId::OtaUpdateStart),
                                                  management_utils::buildOtaPushStartPayload(path, chunk_bytes),
                                                  &req_id,
                                                  0U,
                                                  false,
                                                  &target_peer);
  correlation_id_ = mgmt.nextReqId();
  ota_update_req_id_ = (update_sent && req_id != 0U) ? req_id : 0U;
  ota_update_has_target_peer_ = (update_sent && req_id != 0U);
  ota_update_target_peer_ = ota_update_has_target_peer_ ? target_peer : MacAddress{};
  if (out_message != nullptr) {
    if (!update_sent || req_id == 0U) {
      *out_message = "remote update pipeline start failed";
    } else {
      *out_message = "remote update pipeline started";
    }
  }
  return update_sent && req_id != 0U;
}

bool MasterCli::otaUpdateMaster(const std::string& staged_name, std::string* out_message) {
  if (management_transport_ == nullptr) {
    if (out_message != nullptr) {
      *out_message = "management path unavailable";
    }
    return false;
  }
  if (ota_push_storage_ == nullptr) {
    if (out_message != nullptr) {
      *out_message = "master update unavailable (no local OTA storage backend bound)";
    }
    return false;
  }

  const std::string path = resolveStagedPathInput(staged_name, local_storage_cwd_);
  std::string sidecar_path;
  std::string meta_err;
  FirmwareImageMetadata meta{};
  if (!loadFirmwareMetadataFromSidecar(*ota_push_storage_, path, meta, sidecar_path, meta_err)) {
    if (out_message != nullptr) {
      *out_message = "metadata invalid (" + sidecar_path + "): " + meta_err;
    }
    return false;
  }
  if (meta.target_role != "master") {
    if (out_message != nullptr) {
      *out_message = "metadata target_role mismatch (expected master)";
    }
    return false;
  }

  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  uint32_t req_id = 0U;
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::OtaMasterUpdateStart),
                                         management_utils::buildOtaMasterUpdateStartPayload(path),
                                         &req_id,
                                         0U,
                                         false);
  correlation_id_ = mgmt.nextReqId();
  if (out_message != nullptr) {
    *out_message = (ok && req_id != 0U)
                       ? std::string("master update queued path=") + path
                       : "master update request failed";
  }
  return ok && req_id != 0U;
}

}  // namespace espnow_link
