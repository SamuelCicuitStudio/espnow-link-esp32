/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Paged fetch flow, descriptor queue/mailbox pumping, and main handle/tick loop.
 **************************************************************/
#include "../internal/cli_dispatch_internal.hpp"
#include "../internal/cli_dispatch_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

void MasterCli::clearPagedFetchState() {
  paged_fetch_kind_ = PagedFetchKind::None;
  paged_fetch_page_size_ = 6;
  paged_fetch_next_cursor_ = 0;
  paged_fetch_expected_cursor_ = 0;
  paged_fetch_snapshot_locked_ = false;
  paged_fetch_snapshot_id_ = 0;
  paged_fetch_total_count_ = 0;
  paged_fetch_has_target_peer_ = false;
  paged_fetch_target_peer_ = {};
  paged_fetch_restart_count_ = 0;
  paged_caps_cache_.clear();
  paged_telem_cache_.clear();
  paged_telem_samples_cache_.clear();
  paged_settings_cache_.clear();
  paged_ota_manifest_cache_.clear();
  paged_caps_seen_keys_.clear();
  paged_telem_seen_ids_.clear();
  paged_telem_seen_keys_.clear();
  paged_telem_samples_seen_ids_.clear();
  paged_telem_samples_seen_keys_.clear();
  paged_settings_seen_ids_.clear();
  paged_settings_seen_keys_.clear();
  paged_ota_seen_ids_.clear();
  paged_ota_seen_names_.clear();
}

bool MasterCli::enqueuePagedFetchPage(uint16_t cursor) {
  if (!paged_fetch_has_target_peer_) {
    return false;
  }
  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    return enqueueDescriptorQuery("CAPS.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    return enqueueDescriptorQuery("TELEM.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    return enqueueDescriptorQuery("TELEM.PULL.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    return enqueueDescriptorQuery("SETTINGS.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
                                  &paged_fetch_target_peer_);
  }
  if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    return enqueueDescriptorQuery(
        "OTA.MANIFEST.PAGE " + std::to_string(cursor) + " " + std::to_string(paged_fetch_page_size_),
        &paged_fetch_target_peer_);
  }
  return false;
}

bool MasterCli::startPagedFetch(PagedFetchKind kind, uint8_t page_size, const char* queued_msg) {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return false;
  }

  clearPagedFetchState();
  paged_fetch_kind_ = kind;
  paged_fetch_page_size_ = (page_size == 0) ? 6 : page_size;
  if (paged_fetch_page_size_ > 16U) {
    paged_fetch_page_size_ = 16U;
  }
  paged_fetch_next_cursor_ = 0;
  paged_fetch_expected_cursor_ = 0;
  paged_fetch_has_target_peer_ = true;
  paged_fetch_target_peer_ = target_peer;

  if (!enqueuePagedFetchPage(0)) {
    io_.writeln("[MASTER][CLI] paged fetch request failed");
    clearPagedFetchState();
    return false;
  }

  io_.writeln(queued_msg);
  return true;
}

void MasterCli::printPagedFetchSummary() const {
  const char* kind = "unknown";
  size_t received = 0;
  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    kind = "caps";
    received = paged_caps_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    kind = "telem";
    received = paged_telem_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    kind = "telem.now";
    received = paged_telem_samples_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    kind = "settings";
    received = paged_settings_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    kind = "ota.manifest";
    received = paged_ota_manifest_cache_.size();
  }
  const size_t expected = static_cast<size_t>(paged_fetch_total_count_);
  const size_t missing = (received < expected) ? (expected - received) : 0U;
  writef("[MASTER][CLI] paged fetch %s complete snapshot=%lu total=%u received=%u missing=%u page_size=%u",
         kind,
         static_cast<unsigned long>(paged_fetch_snapshot_id_),
         static_cast<unsigned int>(paged_fetch_total_count_),
         static_cast<unsigned int>(received),
         static_cast<unsigned int>(missing),
         static_cast<unsigned int>(paged_fetch_page_size_));
}

bool MasterCli::handlePagedDescriptorResponse(const DescriptorResponse& d) {
  if (paged_fetch_kind_ == PagedFetchKind::None) {
    return false;
  }

  const DescriptorResponseType expected =
      (paged_fetch_kind_ == PagedFetchKind::Capabilities)
          ? DescriptorResponseType::Capabilities
          : (paged_fetch_kind_ == PagedFetchKind::Telemetry
                 ? DescriptorResponseType::Telemetry
                 : (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot
                        ? DescriptorResponseType::TelemetrySnapshot
                        : (paged_fetch_kind_ == PagedFetchKind::Settings ? DescriptorResponseType::Settings
                                                                         : DescriptorResponseType::OtaManifest)));

  if (d.type != expected) {
    return false;
  }

  if (!d.is_paged) {
    if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
      // Compatibility path: older nodes may return non-paged live telemetry.
      printDescriptorResponse(d);
      clearPagedFetchState();
      return true;
    }
    io_.writeln("[MASTER][CLI] paged fetch failed: non-paged response");
    clearPagedFetchState();
    return true;
  }

  if (d.cursor != paged_fetch_expected_cursor_) {
    if (paged_fetch_restart_count_ >= 1U) {
      io_.writeln("[MASTER][CLI] paged fetch failed: cursor mismatch");
      clearPagedFetchState();
      return true;
    }
    ++paged_fetch_restart_count_;
    paged_caps_cache_.clear();
    paged_telem_cache_.clear();
    paged_telem_samples_cache_.clear();
    paged_settings_cache_.clear();
    paged_ota_manifest_cache_.clear();
    paged_caps_seen_keys_.clear();
    paged_telem_seen_ids_.clear();
    paged_telem_seen_keys_.clear();
    paged_telem_samples_seen_ids_.clear();
    paged_telem_samples_seen_keys_.clear();
    paged_settings_seen_ids_.clear();
    paged_settings_seen_keys_.clear();
    paged_ota_seen_ids_.clear();
    paged_ota_seen_names_.clear();
    paged_fetch_snapshot_locked_ = false;
    paged_fetch_expected_cursor_ = 0;
    paged_fetch_next_cursor_ = 0;
    if (paged_fetch_page_size_ > 1U) {
      paged_fetch_page_size_ = static_cast<uint8_t>(paged_fetch_page_size_ / 2U);
      if (paged_fetch_page_size_ == 0U) {
        paged_fetch_page_size_ = 1U;
      }
    }
    if (!enqueuePagedFetchPage(0)) {
      io_.writeln("[MASTER][CLI] paged fetch restart failed");
      clearPagedFetchState();
    } else {
      io_.writeln("[MASTER][CLI] paged fetch restarted (cursor mismatch)");
    }
    return true;
  }

  if (!paged_fetch_snapshot_locked_) {
    paged_fetch_snapshot_locked_ = true;
    paged_fetch_snapshot_id_ = d.snapshot_id;
    paged_fetch_total_count_ = d.total_count;
  } else if (paged_fetch_kind_ != PagedFetchKind::TelemetrySnapshot &&
             paged_fetch_snapshot_id_ != d.snapshot_id) {
    if (paged_fetch_restart_count_ >= 1U) {
      io_.writeln("[MASTER][CLI] paged fetch failed: snapshot changed repeatedly");
      clearPagedFetchState();
      return true;
    }
    ++paged_fetch_restart_count_;
    paged_caps_cache_.clear();
    paged_telem_cache_.clear();
    paged_telem_samples_cache_.clear();
    paged_settings_cache_.clear();
    paged_ota_manifest_cache_.clear();
    paged_caps_seen_keys_.clear();
    paged_telem_seen_ids_.clear();
    paged_telem_seen_keys_.clear();
    paged_telem_samples_seen_ids_.clear();
    paged_telem_samples_seen_keys_.clear();
    paged_settings_seen_ids_.clear();
    paged_settings_seen_keys_.clear();
    paged_ota_seen_ids_.clear();
    paged_ota_seen_names_.clear();
    paged_fetch_snapshot_locked_ = false;
    paged_fetch_expected_cursor_ = 0;
    paged_fetch_next_cursor_ = 0;
    if (!enqueuePagedFetchPage(0)) {
      io_.writeln("[MASTER][CLI] paged fetch restart failed");
      clearPagedFetchState();
    } else {
      io_.writeln("[MASTER][CLI] paged fetch restarted (snapshot changed)");
    }
    return true;
  }

  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    for (const auto& cap : d.capabilities) {
      const std::string dedupe_key = cap.key + '\x1F' + cap.description;
      if (paged_caps_seen_keys_.insert(dedupe_key).second) {
        paged_caps_cache_.push_back(cap);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    for (const auto& t : d.telemetry) {
      bool inserted = false;
      if (t.metric_id != 0U) {
        inserted = paged_telem_seen_ids_.insert(t.metric_id).second;
      } else {
        inserted = paged_telem_seen_keys_.insert(t.key).second;
      }
      if (inserted) {
        paged_telem_cache_.push_back(t);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    for (const auto& s : d.telemetry_samples) {
      bool inserted = false;
      if (s.metric_id != 0U) {
        inserted = paged_telem_samples_seen_ids_.insert(s.metric_id).second;
      } else {
        inserted = paged_telem_samples_seen_keys_.insert(s.key).second;
      }
      if (inserted) {
        paged_telem_samples_cache_.push_back(s);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    for (const auto& s : d.settings) {
      bool inserted = false;
      if (s.setting_id != 0U) {
        inserted = paged_settings_seen_ids_.insert(s.setting_id).second;
      } else {
        inserted = paged_settings_seen_keys_.insert(s.key).second;
      }
      if (inserted) {
        paged_settings_cache_.push_back(s);
      }
    }
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    for (const auto& e : d.ota_manifest) {
      bool inserted = false;
      if (e.file_id != 0U) {
        inserted = paged_ota_seen_ids_.insert(e.file_id).second;
      } else {
        inserted = paged_ota_seen_names_.insert(e.file_name).second;
      }
      if (inserted) {
        paged_ota_manifest_cache_.push_back(e);
      }
    }
  }

  if (!d.done) {
    if (d.next_cursor <= d.cursor) {
      if (paged_fetch_restart_count_ >= 1U) {
        io_.writeln("[MASTER][CLI] paged fetch failed: invalid next cursor");
        clearPagedFetchState();
        return true;
      }
      ++paged_fetch_restart_count_;
      paged_caps_cache_.clear();
      paged_telem_cache_.clear();
      paged_telem_samples_cache_.clear();
      paged_settings_cache_.clear();
      paged_ota_manifest_cache_.clear();
      paged_caps_seen_keys_.clear();
      paged_telem_seen_ids_.clear();
      paged_telem_seen_keys_.clear();
      paged_telem_samples_seen_ids_.clear();
      paged_telem_samples_seen_keys_.clear();
      paged_settings_seen_ids_.clear();
      paged_settings_seen_keys_.clear();
      paged_ota_seen_ids_.clear();
      paged_ota_seen_names_.clear();
      paged_fetch_snapshot_locked_ = false;
      paged_fetch_expected_cursor_ = 0;
      paged_fetch_next_cursor_ = 0;
      if (paged_fetch_page_size_ > 1U) {
        paged_fetch_page_size_ = static_cast<uint8_t>(paged_fetch_page_size_ / 2U);
        if (paged_fetch_page_size_ == 0U) {
          paged_fetch_page_size_ = 1U;
        }
      }
      if (!enqueuePagedFetchPage(0)) {
        io_.writeln("[MASTER][CLI] paged fetch restart failed");
        clearPagedFetchState();
      } else {
        io_.writeln("[MASTER][CLI] paged fetch restarted (invalid next cursor)");
      }
      return true;
    }
    paged_fetch_next_cursor_ = d.next_cursor;
    paged_fetch_expected_cursor_ = d.next_cursor;
    if (!enqueuePagedFetchPage(paged_fetch_next_cursor_)) {
      io_.writeln("[MASTER][CLI] paged fetch next page request failed");
      clearPagedFetchState();
    }
    return true;
  }

  size_t received = 0U;
  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    received = paged_caps_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    received = paged_telem_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    received = paged_telem_samples_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    received = paged_settings_cache_.size();
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    received = paged_ota_manifest_cache_.size();
  }
  const size_t expected_total = static_cast<size_t>(paged_fetch_total_count_);
  if (received != expected_total) {
    if (paged_fetch_restart_count_ >= 1U || paged_fetch_page_size_ == 1U) {
      writef("[MASTER][CLI] paged fetch failed: incomplete dataset total=%u received=%u",
             static_cast<unsigned int>(expected_total),
             static_cast<unsigned int>(received));
      clearPagedFetchState();
      return true;
    }
    ++paged_fetch_restart_count_;
    paged_caps_cache_.clear();
    paged_telem_cache_.clear();
    paged_telem_samples_cache_.clear();
    paged_settings_cache_.clear();
    paged_ota_manifest_cache_.clear();
    paged_caps_seen_keys_.clear();
    paged_telem_seen_ids_.clear();
    paged_telem_seen_keys_.clear();
    paged_telem_samples_seen_ids_.clear();
    paged_telem_samples_seen_keys_.clear();
    paged_settings_seen_ids_.clear();
    paged_settings_seen_keys_.clear();
    paged_ota_seen_ids_.clear();
    paged_ota_seen_names_.clear();
    paged_fetch_snapshot_locked_ = false;
    paged_fetch_expected_cursor_ = 0;
    paged_fetch_next_cursor_ = 0;
    if (paged_fetch_page_size_ > 1U) {
      paged_fetch_page_size_ = static_cast<uint8_t>(paged_fetch_page_size_ / 2U);
      if (paged_fetch_page_size_ == 0U) {
        paged_fetch_page_size_ = 1U;
      }
    }
    if (!enqueuePagedFetchPage(0)) {
      io_.writeln("[MASTER][CLI] paged fetch restart failed");
      clearPagedFetchState();
    } else {
      writef("[MASTER][CLI] paged fetch restarted (incomplete dataset; new page_size=%u)",
             static_cast<unsigned int>(paged_fetch_page_size_));
    }
    return true;
  }

  DescriptorResponse merged = d;
  merged.is_paged = false;
  merged.done = true;
  merged.returned_count = 0;
  merged.cursor = 0;
  merged.next_cursor = 0;
  merged.total_count = 0;
  merged.snapshot_id = 0;

  if (paged_fetch_kind_ == PagedFetchKind::Capabilities) {
    merged.capabilities = paged_caps_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::Telemetry) {
    merged.telemetry = paged_telem_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::TelemetrySnapshot) {
    merged.telemetry_samples = paged_telem_samples_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::Settings) {
    merged.settings = paged_settings_cache_;
  } else if (paged_fetch_kind_ == PagedFetchKind::OtaManifest) {
    merged.ota_manifest = paged_ota_manifest_cache_;
  }

  if (paged_fetch_kind_ == PagedFetchKind::Capabilities && paged_fetch_has_target_peer_) {
    ProfileId resolved = kProfileUnknown;
    for (const auto& cap : merged.capabilities) {
      if (cap.key != "profile_id") {
        continue;
      }
      const unsigned long parsed = std::strtoul(cap.description.c_str(), nullptr, 10);
      if (parsed > 0U && parsed <= 0xFFFFUL) {
        resolved = static_cast<ProfileId>(parsed);
      }
      break;
    }
    if (resolved != kProfileUnknown) {
      upsertCachedRemoteProfile_(paged_fetch_target_peer_, resolved);
      MacAddress active_peer{};
      if (resolveRuntimePeer(active_peer) && active_peer == paged_fetch_target_peer_) {
        remote_profile_id_ = resolved;
      }
    }
  }

  printDescriptorResponse(merged);
  printPagedFetchSummary();
  clearPagedFetchState();
  return true;
}


bool MasterCli::logEnabled(CliLogLevel level) const {
  return static_cast<uint8_t>(log_level_) >= static_cast<uint8_t>(level);
}

bool MasterCli::enqueueDescriptorQuery(const std::string& cmd, const MacAddress* target_peer) {
  MacAddress target{};
  if (target_peer != nullptr) {
    target = *target_peer;
  } else if (!resolveRuntimePeer(target)) {
    io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return false;
  }
  if (cmd.empty()) {
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "parse");
    return false;
  }
  if (descriptor_request_queue_.size() >= descriptor_queue_max_) {
    ++descriptor_queue_drop_count_;
    if (logEnabled(CliLogLevel::Error)) {
      writef("[MASTER][CLI][QUEUE] drop full depth=%u max=%u cmd=%s",
             static_cast<unsigned int>(descriptor_request_queue_.size()),
             static_cast<unsigned int>(descriptor_queue_max_),
             cmd.c_str());
    }
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::QueueFull, "queue");
    return false;
  }
  DescriptorRequestQueueItem item{};
  item.cmd = cmd;
  item.enqueued_ms = nowMs();
  item.has_target_peer = true;
  item.target_peer = target;
  descriptor_request_queue_.push_back(std::move(item));
  captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
  return true;
}

bool MasterCli::executeDescriptorQueryNow(const std::string& cmd, const MacAddress* target_peer) {
  MacAddress resolved_target{};
  if (target_peer != nullptr) {
    resolved_target = *target_peer;
  } else if (!resolveRuntimePeer(resolved_target)) {
    return false;
  }

  auto parsePageCmd = [](const std::string& raw, const char* prefix, uint16_t& out_cursor, uint8_t& out_size) -> bool {
    if (!startsWith(raw, prefix)) {
      return false;
    }
    const std::string body = trim(raw.substr(std::strlen(prefix)));
    const size_t sp = body.find(' ');
    if (sp == std::string::npos) {
      return false;
    }
    const std::string c = trim(body.substr(0, sp));
    const std::string s = trim(body.substr(sp + 1));
    if (c.empty() || s.empty()) {
      return false;
    }
    const unsigned long cursor = std::strtoul(c.c_str(), nullptr, 10);
    const unsigned long size = std::strtoul(s.c_str(), nullptr, 10);
    if (cursor > 0xFFFFUL || size > 0xFFUL) {
      return false;
    }
    out_cursor = static_cast<uint16_t>(cursor);
    out_size = static_cast<uint8_t>(size);
    return true;
  };

  auto parseLogReadCmd = [](const std::string& raw, uint32_t& out_offset, uint16_t& out_max_bytes) -> bool {
    if (!startsWith(raw, "LOGGER.READ ")) {
      return false;
    }
    const std::string body = trim(raw.substr(12));
    if (body.empty()) {
      return false;
    }
    const size_t sp = body.find(' ');
    std::string o = body;
    std::string m;
    if (sp != std::string::npos) {
      o = trim(body.substr(0, sp));
      m = trim(body.substr(sp + 1));
    }
    if (o.empty()) {
      return false;
    }
    const unsigned long offset = std::strtoul(o.c_str(), nullptr, 10);
    out_offset = static_cast<uint32_t>(offset);
    out_max_bytes = 96;
    if (!m.empty()) {
      const unsigned long mb = std::strtoul(m.c_str(), nullptr, 10);
      if (mb == 0U || mb > 0xFFFFU) {
        return false;
      }
      out_max_bytes = static_cast<uint16_t>(mb);
    }
    if (out_max_bytes > 128U) {
      out_max_bytes = 128U;
    }
    return true;
  };

  auto parseStoragePathCmd = [](const std::string& raw,
                                const char* prefix,
                                std::string& out_path) -> bool {
    if (!startsWith(raw, prefix)) {
      return false;
    }
    out_path = trim(raw.substr(std::strlen(prefix)));
    return !out_path.empty();
  };

  auto parseOtaArgCmd = [](const std::string& raw, const char* prefix, std::string& out_arg) -> bool {
    if (!startsWith(raw, prefix)) {
      return false;
    }
    out_arg = trim(raw.substr(std::strlen(prefix)));
    return !out_arg.empty();
  };

  bool sent = false;
  const bool mgmt_available = (management_transport_ != nullptr);
  if (!mgmt_available) {
    if (logEnabled(CliLogLevel::Error)) {
      io_.writeln("[MASTER][CLI] management path unavailable");
    }
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return false;
  }
  uint16_t page_cursor = 0;
  uint8_t page_size = 0;
  uint32_t log_offset = 0;
  uint16_t log_max_bytes = 0;
  std::string storage_path;
  std::string ota_arg;
  auto buildPagePayload = [](uint16_t cursor, uint8_t size) {
    std::vector<uint8_t> payload{};
    management_utils::appendU16Le(payload, cursor);
    payload.push_back(size);
    return payload;
  };
  auto sendMgmtRaw = [&](ManagementCommandId cmd_id, const std::vector<uint8_t>& payload = {}) -> bool {
    if (!mgmt_available) return false;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool accepted = submitRuntimeTargeted_(mgmt,
                                                 static_cast<uint16_t>(cmd_id),
                                                 payload,
                                                 nullptr,
                                                 0U,
                                                 false,
                                                 &resolved_target);
    correlation_id_ = mgmt.nextReqId();
    return accepted;
  };
  if (parsePageCmd(cmd, "CAPS.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::CapsPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "TELEM.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::TelemSchemaPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "TELEM.PULL.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::TelemPull,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "SETTINGS.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::SettingsPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (parsePageCmd(cmd, "OTA.MANIFEST.PAGE ", page_cursor, page_size)) {
    sent = sendMgmtRaw(ManagementCommandId::OtaManifestPageGet,
                       buildPagePayload(page_cursor, page_size));
  } else if (cmd == "DESC.GET") {
    sent = sendMgmtRaw(ManagementCommandId::DescGet);
  } else if (cmd == "CAPS.GET") {
    sent = sendMgmtRaw(ManagementCommandId::CapsGet);
  } else if (cmd == "TELEM.GET") {
    sent = sendMgmtRaw(ManagementCommandId::TelemSchemaGet);
  } else if (cmd == "TELEM.PULL") {
    sent = sendMgmtRaw(ManagementCommandId::TelemPull);
  } else if (cmd == "LIVE.GET") {
    sent = sendMgmtRaw(ManagementCommandId::LiveGet);
    if (sent) {
      auto_pull_.markLivenessRequested(nowMs());
      probe_pending_kind_ = ProbePendingKind::Live;
      probe_sent_ms_ = nowMs();
    }
  } else if (cmd == "PING.GET") {
    sent = sendMgmtRaw(ManagementCommandId::PingGet);
    if (sent) {
      auto_pull_.markLivenessRequested(nowMs());
      probe_pending_kind_ = ProbePendingKind::Ping;
      probe_sent_ms_ = nowMs();
    }
  } else if (cmd == "AUDIO.PING") {
    sent = sendMgmtRaw(ManagementCommandId::AudioPingRequest);
  } else if (cmd == "TIME.GET") {
    sent = sendMgmtRaw(ManagementCommandId::TimeGet);
  } else if (startsWith(cmd, "TIME.SET ")) {
    const std::string arg = cmd.substr(9);
    const uint64_t epoch_s = static_cast<uint64_t>(std::strtoull(arg.c_str(), nullptr, 10));
    sent = sendMgmtRaw(ManagementCommandId::TimeSet, management_utils::buildTimeSetPayload(epoch_s));
  } else if (cmd == "SETTINGS.GET") {
    sent = sendMgmtRaw(ManagementCommandId::SettingsGet);
  } else if (startsWith(cmd, "SETTING.GET ")) {
    const std::string key = cmd.substr(12);
    sent = sendMgmtRaw(ManagementCommandId::SettingGet, management_utils::buildSettingGetByKeyPayload(key));
  } else if (startsWith(cmd, "SETTING.SET ")) {
    const std::string body = cmd.substr(12);
    const size_t eq = body.find('=');
    if (eq == std::string::npos || eq == 0) {
      return false;
    }
    const std::string key = body.substr(0, eq);
    const std::string value = trim(body.substr(eq + 1));
    sent = sendMgmtRaw(ManagementCommandId::SettingSet,
                       management_utils::buildSettingSetByKeyPayload(key, value));
  } else if (cmd == "LOGGER.STATUS") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteStatusGet);
  } else if (cmd == "LOGGER.CLEAR") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteClear);
  } else if (cmd == "LOGGER.ENABLE") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteControlSet,
                       management_utils::buildLogControlPayload(true));
  } else if (cmd == "LOGGER.DISABLE") {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteControlSet,
                       management_utils::buildLogControlPayload(false));
  } else if (parseLogReadCmd(cmd, log_offset, log_max_bytes)) {
    sent = sendMgmtRaw(ManagementCommandId::LogRemoteRead,
                       management_utils::buildLogReadPayload(log_offset, log_max_bytes));
  } else if (cmd == "STORAGE.INFO") {
    sent = sendMgmtRaw(ManagementCommandId::StorageInfoGet);
  } else if (parseStoragePathCmd(cmd, "STORAGE.LIST ", storage_path)) {
    sent = sendMgmtRaw(ManagementCommandId::StorageList,
                       management_utils::buildStringPayloadU16(storage_path));
  } else if (parseStoragePathCmd(cmd, "STORAGE.STAT ", storage_path)) {
    sent = sendMgmtRaw(ManagementCommandId::StorageStat,
                       management_utils::buildStringPayloadU16(storage_path));
  } else if (cmd == "STORAGE.FORMAT") {
    sent = sendMgmtRaw(ManagementCommandId::StorageFormat);
  } else if (cmd == "OTA.STATUS") {
    sent = sendMgmtRaw(ManagementCommandId::OtaStatusGet);
  } else if (cmd == "OTA.MANIFEST.GET") {
    sent = sendMgmtRaw(ManagementCommandId::OtaManifestGet);
  } else if (cmd == "OTA.MANIFEST.REBUILD") {
    sent = sendMgmtRaw(ManagementCommandId::OtaManifestRebuild);
  } else if (parseOtaArgCmd(cmd, "OTA.CLEAR ", ota_arg)) {
    sent = sendMgmtRaw(ManagementCommandId::OtaClearScope,
                       management_utils::buildStringPayloadU16(ota_arg));
  } else if (cmd == "OTA.CAPACITY") {
    sent = sendMgmtRaw(ManagementCommandId::OtaCapacityGet);
  } else if (cmd == "OTA.GATE") {
    sent = sendMgmtRaw(ManagementCommandId::OtaGateGet);
  } else if (parseOtaArgCmd(cmd, "OTA.APPLY ", ota_arg)) {
    sent = sendMgmtRaw(ManagementCommandId::OtaApply,
                       management_utils::buildStringPayloadU16(ota_arg));
  }
  return sent;
}

bool MasterCli::sendDescriptorQuery(const std::string& cmd) {
  return enqueueDescriptorQuery(cmd);
}

void MasterCli::pumpDescriptorQueue(uint32_t now_ms) {
  if (descriptor_request_queue_.empty()) {
    return;
  }

  uint8_t budget = descriptor_send_budget_per_tick_;
  while (budget > 0 && !descriptor_request_queue_.empty()) {
    DescriptorRequestQueueItem item = std::move(descriptor_request_queue_.front());
    descriptor_request_queue_.pop_front();
    const bool sent =
        executeDescriptorQueryNow(item.cmd, item.has_target_peer ? &item.target_peer : nullptr);
    if (sent) {
      ++descriptor_queue_sent_count_;
      if (logEnabled(CliLogLevel::Debug)) {
        writef("[MASTER][CLI][QUEUE] sent age_ms=%lu cmd=%s",
               static_cast<unsigned long>(now_ms - item.enqueued_ms),
               item.cmd.c_str());
      }
    } else {
      ++descriptor_queue_fail_count_;
      if (logEnabled(CliLogLevel::Error)) {
        writef("[MASTER][CLI][QUEUE] send_failed age_ms=%lu cmd=%s",
               static_cast<unsigned long>(now_ms - item.enqueued_ms),
               item.cmd.c_str());
      }
    }
    --budget;
  }
}

void MasterCli::pumpManagementMailbox() {
  if (management_transport_ == nullptr) {
    return;
  }
  if (!cli_enabled_) {
    ManagementResponse drop_resp{};
    while (management_transport_->pollResponse(drop_resp)) {}
    ManagementEvent drop_evt{};
    while (management_transport_->pollEvent(drop_evt)) {}
    return;
  }
  auto readU16Le = [](const uint8_t* p, size_t n, uint16_t& out) -> bool {
    if (p == nullptr || n < 2U) return false;
    out = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8U);
    return true;
  };
  auto readU32Le = [](const uint8_t* p, size_t n, uint32_t& out) -> bool {
    if (p == nullptr || n < 4U) return false;
    out = static_cast<uint32_t>(p[0]) |
          (static_cast<uint32_t>(p[1]) << 8U) |
          (static_cast<uint32_t>(p[2]) << 16U) |
          (static_cast<uint32_t>(p[3]) << 24U);
    return true;
  };
  auto responsePeerContext = [&](const ManagementResponse& resp) -> std::string {
    if (!resp.has_requested_peer && !resp.has_executed_peer && !resp.activation_performed) {
      return std::string();
    }
    const std::string requested = resp.has_requested_peer ? macToPrintable(resp.requested_peer) : "-";
    const std::string executed = resp.has_executed_peer ? macToPrintable(resp.executed_peer) : "-";
    std::string out = " req_peer=" + requested +
                      " exec_peer=" + executed +
                      " activated=" + (resp.activation_performed ? std::string("yes") : std::string("no"));
    if (resp.activation_performed) {
      out += " act_ms=" + std::to_string(static_cast<unsigned int>(resp.activation_latency_ms));
    }
    return out;
  };
  auto eventPeerContext = [&](const ManagementEvent& evt) -> std::string {
    if (!evt.has_requested_peer && !evt.has_executed_peer && !evt.activation_performed) {
      return std::string();
    }
    const std::string requested = evt.has_requested_peer ? macToPrintable(evt.requested_peer) : "-";
    const std::string executed = evt.has_executed_peer ? macToPrintable(evt.executed_peer) : "-";
    std::string out = " req_peer=" + requested +
                      " exec_peer=" + executed +
                      " activated=" + (evt.activation_performed ? std::string("yes") : std::string("no"));
    if (evt.activation_performed) {
      out += " act_ms=" + std::to_string(static_cast<unsigned int>(evt.activation_latency_ms));
    }
    return out;
  };
  const bool management_only = usesManagementOnlyTraffic_();
  ManagementResponse resp{};
  while (management_transport_->pollResponse(resp)) {
    if (management_only && !shouldProcessManagementMailboxResponse_(resp)) {
      ++mailbox_ignored_responses_;
      continue;
    }
    ++mailbox_processed_responses_;
    handleTopologyVerifyResponse_(resp);
    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::LiveMonitorStatusGet)) {
      const bool show_live_status =
          live_monitor_status_pending_ &&
          resp.source == ManagementSource::Cli &&
          resp.req_id == live_monitor_status_req_id_;
      if (show_live_status) {
        live_monitor_status_pending_ = false;
        live_monitor_status_req_id_ = 0U;
        if (resp.status != ManagementStatus::Ok && resp.status != ManagementStatus::OkDeferred) {
          writef("[MASTER][LIVE] monitor status failed: %s",
                 management_utils::managementStatusToString(resp.status));
        } else {
          ManagementLiveMonitorStatusPayload live_status{};
          if (management_utils::parseLiveMonitorStatusPayload(resp.payload, live_status)) {
            auto ignoreLabel = [&](uint16_t mask) -> const char* {
              if (mask == 0U) return "none";
              if ((mask & 0x0001U) != 0U && (mask & ~0x0001U) == 0U) return "ota_push";
              if ((mask & 0x0002U) != 0U && (mask & ~0x0002U) == 0U) return "ota_update";
              if ((mask & 0x0004U) != 0U && (mask & ~0x0004U) == 0U) return "critical_cmd";
              if ((mask & 0x0008U) != 0U && (mask & ~0x0008U) == 0U) return "master_update_guard";
              return "mixed";
            };
            io_.writeln("[MASTER][LIVE] status");
            writef("  monitor : %s", live_status.enabled ? "ON" : "OFF");
            writef("  tracked : %u", static_cast<unsigned int>(live_status.tracked_paired_count));
            writef("  online  : %u", static_cast<unsigned int>(live_status.online_count));
            writef("  offline : %u", static_cast<unsigned int>(live_status.offline_count));
            writef("  ignore  : %s", ignoreLabel(live_status.ignore_reason_mask));
          } else {
            io_.writeln("[MASTER][LIVE] monitor status payload decode failed");
          }
        }
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyStatusGet) &&
        (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred)) {
      auto topoStateLabel = [](uint8_t state) -> const char* {
        switch (state) {
          case 1U: return "staged";
          case 2U: return "committed";
          default: return "none";
        }
      };
      ManagementTopologyStatusPayload topo{};
      bool committed_state_known = false;
      bool committed_groups_known = false;
      bool index_window_known = false;
      bool committed_checksum_known = false;
      bool parsed_status = false;
      if (management_utils::parseTopologyStatusPayload(resp.payload, topo)) {
        parsed_status = true;
        committed_state_known = true;
        committed_groups_known = true;
        index_window_known = true;
        committed_checksum_known = true;
      } else if (!resp.payload.empty()) {
        DescriptorResponse desc{};
        if (manager_.decodeDescriptorResponsePayload(resp.payload.data(), resp.payload.size(), desc) &&
            desc.type == DescriptorResponseType::Ack) {
          ParsedTopologyStatusForVerify parsed{};
          if (parseTopologyStatusAckMessageForVerify(desc.message, parsed)) {
            topo = parsed.status;
            parsed_status = true;
            committed_state_known = parsed.committed_state_known;
            committed_groups_known = parsed.committed_groups_known;
            index_window_known = parsed.index_window_known;
            committed_checksum_known = parsed.committed_checksum_known;
          }
        }
      }

      if (parsed_status) {
        io_.writeln("[MASTER][TOPO] status");
        writef("  schema         : v%u", static_cast<unsigned int>(topo.schema_version));
        writef("  staged         : %s", topo.has_staged ? "yes" : "no");
        writef("  committed      : %s", topo.has_committed ? "yes" : "no");
        writef("  staged_ver     : %lu", static_cast<unsigned long>(topo.staged_version));
        writef("  committed_ver  : %lu", static_cast<unsigned long>(topo.committed_version));
        writef("  staged_state   : %s", topoStateLabel(topo.staged_state));
        if (committed_state_known) {
          writef("  committed_state: %s", topoStateLabel(topo.committed_state));
        } else {
          io_.writeln("  committed_state: n/a");
        }
        writef("  staged_slots   : %u", static_cast<unsigned int>(topo.staged_slot_count));
        writef("  committed_slots: %u", static_cast<unsigned int>(topo.committed_slot_count));
        if (committed_groups_known) {
          writef("  staged_groups  : %u", static_cast<unsigned int>(topo.staged_group_count));
          writef("  committed_groups: %u", static_cast<unsigned int>(topo.committed_group_count));
        }
        if (index_window_known) {
          writef("  index_neg      : %u", static_cast<unsigned int>(topo.index_neg));
          writef("  index_pos      : %u", static_cast<unsigned int>(topo.index_pos));
        }
        if (committed_checksum_known) {
          writef("  committed_checksum: 0x%08lX",
                 static_cast<unsigned long>(topo.committed_checksum));
        }
      } else if (resp.status == ManagementStatus::OkDeferred || resp.payload.empty()) {
        io_.writeln("[MASTER][TOPO] status request accepted (remote/async)");
      } else {
        io_.writeln("[MASTER][TOPO] status payload decode failed");
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologySlotsGet) &&
        (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred)) {
      uint8_t state = 0U;
      std::vector<ManagementTopologySlotPayload> slots{};
      if (management_utils::parseTopologySlotsPayload(resp.payload, state, slots)) {
        const char* state_label = (state == 1U) ? "staged" : (state == 2U) ? "committed" : "none";
        uint32_t enabled = 0U;
        for (const auto& s : slots) {
          if (s.enabled) ++enabled;
        }
        writef("[MASTER][TOPO] slots state=%s total=%u enabled=%u",
               state_label,
               static_cast<unsigned int>(slots.size()),
               static_cast<unsigned int>(enabled));
        for (const auto& s : slots) {
          if (!s.enabled) {
            continue;
          }
          writef("  slot=%u peer=%s role=%u gid=%u rid=%d lvid=%u pvid=%u axis=%d dly=%u hold=%u",
                 static_cast<unsigned int>(s.slot_index),
                 macToPrintable(s.peer).c_str(),
                 static_cast<unsigned int>(s.peer_role),
                 static_cast<unsigned int>(s.group_id),
                 static_cast<int>(s.relative_index),
                 static_cast<unsigned int>(s.local_virtual_index),
                 static_cast<unsigned int>(s.peer_virtual_index),
                 static_cast<int>(s.axis_order),
                 static_cast<unsigned int>(s.delay_ms),
                 static_cast<unsigned int>(s.hold_ms));
        }
      } else if (resp.status == ManagementStatus::OkDeferred || resp.payload.empty()) {
        io_.writeln("[MASTER][TOPO] slots request accepted (remote/async)");
      } else {
        io_.writeln("[MASTER][TOPO] slots payload decode failed");
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyTriggerSend) &&
        (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred)) {
      ManagementTopologyTriggerSendResponsePayload trigger_resp{};
      if (management_utils::parseTopologyTriggerSendResponsePayload(resp.payload, trigger_resp)) {
        writef("[MASTER][TOPO] trigger queued seq=%u",
               static_cast<unsigned int>(trigger_resp.seq));
      } else if (resp.status == ManagementStatus::OkDeferred || resp.payload.empty()) {
        io_.writeln("[MASTER][TOPO] trigger request accepted (async)");
      }
    }

    const bool is_archive_cmd =
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveList) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveRunning) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveStaged) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveVerify) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveRestore) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveDelete) ||
        resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaArchiveClear);
    if (is_archive_cmd) {
      std::string arc_message;
      (void)management_utils::parseStringPayloadU16(resp.payload, arc_message);
      if (!arc_message.empty()) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          writef("[MASTER][OTA][ARC] %s", arc_message.c_str());
        } else {
          writef("[MASTER][OTA][ARC] failed: %s", arc_message.c_str());
        }
      }
    }

    if (resp.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaPushStart) &&
        ota_push_active_ &&
        resp.req_id == ota_push_corr_id_) {
      if (resp.status != ManagementStatus::Ok &&
          resp.status != ManagementStatus::OkDeferred) {
        stopOtaPush("management ota.push start rejected", false);
      }
    }

    const std::string peer_ctx = responsePeerContext(resp);
    if (resp.status == ManagementStatus::InternalError ||
        resp.status == ManagementStatus::Timeout ||
        resp.status == ManagementStatus::DeniedByPolicy ||
        resp.status == ManagementStatus::NotPaired ||
        resp.status == ManagementStatus::CapacityLimitReached ||
        resp.status == ManagementStatus::TopologyNotStaged ||
        resp.status == ManagementStatus::TopologyVersionStale ||
        resp.status == ManagementStatus::TopologyApplyFailed ||
        resp.status == ManagementStatus::SourceNotActiveMaster ||
        resp.status == ManagementStatus::UnsupportedCommand ||
        resp.status == ManagementStatus::BadPayload) {
      if (logEnabled(CliLogLevel::Error)) {
        writef("[MASTER][MGMT][RESP] req=%lu cmd=0x%04X status=0x%04X%s",
               static_cast<unsigned long>(resp.req_id),
               static_cast<unsigned int>(resp.cmd_id),
               static_cast<unsigned int>(resp.status),
               peer_ctx.c_str());
      }
    } else if (logEnabled(CliLogLevel::Debug)) {
      writef("[MASTER][MGMT][RESP] req=%lu cmd=0x%04X status=0x%04X%s",
             static_cast<unsigned long>(resp.req_id),
             static_cast<unsigned int>(resp.cmd_id),
             static_cast<unsigned int>(resp.status),
             peer_ctx.c_str());
    }
  }

  ManagementEvent evt{};
  while (management_transport_->pollEvent(evt)) {
    if (management_only && !shouldProcessManagementMailboxEvent_(evt)) {
      ++mailbox_ignored_events_;
      continue;
    }
    ++mailbox_processed_events_;
    if (evt.event_id == ManagementEventId::PeerLivenessTransition) {
      ManagementPeerLivenessTransitionPayload transition{};
      if (management_utils::parsePeerLivenessTransitionPayload(evt.payload, transition)) {
        const char* state_label = (transition.state == 0U) ? "ONLINE" : "OFFLINE";
        writef("[MASTER][LIVE] peer=%s %s",
               macToPrintable(transition.peer).c_str(),
               state_label);
      }
      continue;
    }

    if (evt.event_id == ManagementEventId::TopologyTriggerReceived ||
        evt.event_id == ManagementEventId::TopologyTriggerRejected ||
        evt.event_id == ManagementEventId::TopologyTriggerAck) {
      ManagementTopologyTriggerEventPayload trigger_evt{};
      if (management_utils::parseTopologyTriggerEventPayload(evt.payload, trigger_evt)) {
        if (evt.event_id == ManagementEventId::TopologyTriggerAck) {
          writef("[MASTER][TOPO][ACK] peer=%s ack_seq=%u result=%u reason=%u",
                 macToPrintable(trigger_evt.peer).c_str(),
                 static_cast<unsigned int>(trigger_evt.ack_seq),
                 static_cast<unsigned int>(trigger_evt.result),
                 static_cast<unsigned int>(trigger_evt.reason));
        } else if (evt.event_id == ManagementEventId::TopologyTriggerRejected) {
          writef("[MASTER][TOPO][RX] peer=%s seq=%u REJECT reason=%u",
                 macToPrintable(trigger_evt.peer).c_str(),
                 static_cast<unsigned int>(trigger_evt.seq),
                 static_cast<unsigned int>(trigger_evt.reason));
        } else {
          const char* state = (trigger_evt.state == 2U) ? "DUP" : "OK";
          writef("[MASTER][TOPO][RX] peer=%s seq=%u %s dir=%u delay=%u hold=%u",
                 macToPrintable(trigger_evt.peer).c_str(),
                 static_cast<unsigned int>(trigger_evt.seq),
                 state,
                 static_cast<unsigned int>(trigger_evt.direction),
                 static_cast<unsigned int>(trigger_evt.delay_ms),
                 static_cast<unsigned int>(trigger_evt.hold_ms));
        }
      }
      continue;
    }

    if (logEnabled(CliLogLevel::Debug)) {
      const std::string peer_ctx = eventPeerContext(evt);
      writef("[MASTER][MGMT][EVT] id=0x%04X cmd=0x%04X req=%lu status=0x%04X%s",
             static_cast<unsigned int>(evt.event_id),
             static_cast<unsigned int>(evt.cmd_id),
             static_cast<unsigned long>(evt.req_id),
             static_cast<unsigned int>(evt.status),
             peer_ctx.c_str());
    }

    if (evt.event_id == ManagementEventId::CmdFail &&
        evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaPushStart) &&
        ota_push_active_ &&
        evt.req_id == ota_push_corr_id_) {
      stopOtaPush("management ota.push failed", false);
      continue;
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::OtaUpdateStart)) {
      if (ota_update_req_id_ == 0U || evt.req_id != ota_update_req_id_) {
        continue;
      }
      if (evt.event_id == ManagementEventId::CmdDone) {
        io_.writeln("[MASTER][OTA] update pipeline complete");
        if (ota_update_has_target_peer_ && enqueueDescriptorQuery("DESC.GET", &ota_update_target_peer_)) {
          io_.writeln("[MASTER][OTA] auto verify queued: desc");
        }
        ota_update_req_id_ = 0U;
        ota_update_has_target_peer_ = false;
        ota_update_target_peer_ = {};
      } else if (evt.event_id == ManagementEventId::CmdFail) {
        writef("[MASTER][OTA] update pipeline failed (status=%s)",
               management_utils::managementStatusToString(evt.status));
        ota_update_req_id_ = 0U;
        ota_update_has_target_peer_ = false;
        ota_update_target_peer_ = {};
      }
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyCommit) &&
        (evt.event_id == ManagementEventId::CmdDone || evt.event_id == ManagementEventId::CmdFail)) {
      ManagementTopologyDeploySummaryPayload deploy{};
      if (management_utils::parseTopologyDeploySummaryPayload(evt.payload, deploy)) {
        writef("[MASTER][TOPO] deploy summary queued=%lu failed=%lu",
               static_cast<unsigned long>(deploy.queued_peers),
               static_cast<unsigned long>(deploy.failed_peers));
      }
      continue;
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::ChannelSyncAll) &&
        (evt.event_id == ManagementEventId::CmdDone || evt.event_id == ManagementEventId::CmdFail)) {
      ManagementChannelSyncAllResultPayload result{};
      if (management_utils::parseChannelSyncAllResultPayload(evt.payload, result)) {
        writef("[MASTER][CHANNEL] sync %s target=%u acked=%u/%u",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed",
               static_cast<unsigned int>(result.channel),
               static_cast<unsigned int>(result.acked_peers),
               static_cast<unsigned int>(result.total_peers));
      } else {
        writef("[MASTER][CHANNEL] sync %s",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed");
      }
      continue;
    }

    if (evt.cmd_id == static_cast<uint16_t>(ManagementCommandId::ChainLoopControlSet) &&
        (evt.event_id == ManagementEventId::CmdDone || evt.event_id == ManagementEventId::CmdFail)) {
      ManagementChainLoopResultPayload result{};
      if (management_utils::parseChainLoopResultPayload(evt.payload, result)) {
        writef("[MASTER][CHAIN] loop_auto %s state=%s acked=%u/%u",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed",
               result.enabled ? "on" : "off",
               static_cast<unsigned int>(result.acked_peers),
               static_cast<unsigned int>(result.total_peers));
      } else {
        writef("[MASTER][CHAIN] loop_auto %s",
               (evt.event_id == ManagementEventId::CmdDone) ? "done" : "failed");
      }
      continue;
    }

    if (evt.event_id == ManagementEventId::OtaTransferReady ||
        evt.event_id == ManagementEventId::OtaBootComplete) {
      if (evt.payload.size() >= 17U) {
        MandatoryEventItem item{};
        std::memcpy(item.peer.data(), evt.payload.data(), 6U);
        (void)readU16Le(evt.payload.data() + 6U, evt.payload.size() - 6U, item.event_id);
        item.severity = evt.payload[8U];
        uint32_t event_value_u32 = 0U;
        uint32_t event_ts_u32 = 0U;
        (void)readU32Le(evt.payload.data() + 9U, evt.payload.size() - 9U, event_value_u32);
        (void)readU32Le(evt.payload.data() + 13U, evt.payload.size() - 13U, event_ts_u32);
        item.event_value = static_cast<int32_t>(event_value_u32);
        item.event_ts_s = event_ts_u32;
        item.corr_id = evt.req_id;
        item.rx_ms = nowMs();
        mandatory_events_.push_back(item);
        if (mandatory_events_.size() > 32U) {
          mandatory_events_.erase(mandatory_events_.begin());
        }
      }

      if (evt.event_id == ManagementEventId::OtaBootComplete) {
        io_.writeln("[MASTER][OTA] slave reports update completed after reboot");
        const MacAddress* verify_peer = ota_push_has_target_peer_
                                            ? &ota_push_target_peer_
                                            : (ota_update_has_target_peer_ ? &ota_update_target_peer_ : nullptr);
        if (verify_peer != nullptr && enqueueDescriptorQuery("DESC.GET", verify_peer)) {
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
          ota_update_req_id_ = 0U;
          ota_update_has_target_peer_ = false;
          ota_update_target_peer_ = {};
          io_.writeln("[MASTER][OTA] update pipeline complete");
        }
      } else if (evt.event_id == ManagementEventId::OtaTransferReady) {
        uint32_t transfer_corr = 0U;
        if (evt.payload.size() >= 13U) {
          (void)readU32Le(evt.payload.data() + 9U, evt.payload.size() - 9U, transfer_corr);
        }
        if (cli_enabled_ && logEnabled(CliLogLevel::Info)) {
          writef("[MASTER][OTA] slave finalize event received corr=%lu",
                 static_cast<unsigned long>(transfer_corr));
        }
        if (ota_push_active_ &&
            transfer_corr == ota_push_corr_id_ &&
            ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
          if (ota_update_image_name_.empty()) {
            ota_update_image_name_ = "u.bin";
          }
          stopOtaPush("complete", true);
        }
      }
      continue;
    }

    if (evt.event_id == ManagementEventId::OtaTransferStatus) {
      constexpr uint8_t kOtaStatusKindChunkAck = 0x01;
      constexpr uint8_t kOtaStatusKindChunkNack = 0x02;
      constexpr uint8_t kOtaStatusKindFinalizeOk = 0x03;
      constexpr uint8_t kOtaStatusKindFinalizeFail = 0x04;
      if (evt.payload.size() < 13U) {
        continue;
      }
      const uint32_t transfer_corr = evt.req_id;
      const uint8_t kind = evt.payload[6U];
      uint32_t offset = 0U;
      uint16_t status_code = 0U;
      (void)readU32Le(evt.payload.data() + 7U, evt.payload.size() - 7U, offset);
      (void)readU16Le(evt.payload.data() + 11U, evt.payload.size() - 11U, status_code);
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
            writef("[MASTER][OTA] nack received offset=%lu code=0x%04X",
                   static_cast<unsigned long>(ota_push_offset_),
                   static_cast<unsigned int>(status_code));
          }
        } else if (kind == kOtaStatusKindFinalizeOk) {
          if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
            stopOtaPush("complete", true);
          }
        } else if (kind == kOtaStatusKindFinalizeFail) {
          if (cli_enabled_ && logEnabled(CliLogLevel::Error)) {
            writef("[MASTER][OTA] finalize fail code=0x%04X offset=%lu",
                   static_cast<unsigned int>(status_code),
                   static_cast<unsigned long>(offset));
          }
          if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
            stopOtaPush("slave finalize failed", false);
          }
        }
      }
      continue;
    }
  }
}

bool MasterCli::compareTopologySlotsForVerify_(const ManagementTopologySnapshotPayload& expected,
                                               const std::vector<ManagementTopologySlotPayload>& actual_slots,
                                               std::string& out_reason) const {
  out_reason.clear();
  std::map<uint8_t, ManagementTopologySlotPayload> expected_enabled{};
  for (const auto& slot : expected.slots) {
    if (!slot.enabled) {
      continue;
    }
    expected_enabled[slot.slot_index] = slot;
  }

  std::map<uint8_t, ManagementTopologySlotPayload> actual_enabled{};
  for (const auto& slot : actual_slots) {
    if (!slot.enabled) {
      continue;
    }
    const auto it = actual_enabled.find(slot.slot_index);
    if (it != actual_enabled.end()) {
      out_reason = "duplicate_actual_slot_index_" + std::to_string(static_cast<unsigned int>(slot.slot_index));
      return false;
    }
    actual_enabled[slot.slot_index] = slot;
  }

  if (expected_enabled.size() != actual_enabled.size()) {
    out_reason = "enabled_slot_count_mismatch expected=" +
                 std::to_string(static_cast<unsigned int>(expected_enabled.size())) +
                 " actual=" + std::to_string(static_cast<unsigned int>(actual_enabled.size()));
    return false;
  }

  for (const auto& kv : expected_enabled) {
    const uint8_t idx = kv.first;
    const auto actual_it = actual_enabled.find(idx);
    if (actual_it == actual_enabled.end()) {
      out_reason = "missing_slot_index_" + std::to_string(static_cast<unsigned int>(idx));
      return false;
    }
    const ManagementTopologySlotPayload& exp = kv.second;
    const ManagementTopologySlotPayload& act = actual_it->second;
    if (exp.peer != act.peer ||
        exp.peer_role != act.peer_role ||
        exp.group_id != act.group_id ||
        exp.relative_index != act.relative_index ||
        exp.local_virtual_index != act.local_virtual_index ||
        exp.peer_virtual_index != act.peer_virtual_index ||
        exp.axis_order != act.axis_order ||
        exp.delay_ms != act.delay_ms ||
        exp.hold_ms != act.hold_ms) {
      out_reason = "slot_mismatch_index_" + std::to_string(static_cast<unsigned int>(idx));
      return false;
    }
  }
  return true;
}

void MasterCli::clearTopologyVerifySession_(const char* reason) {
  if (topology_verify_.active && reason != nullptr && reason[0] != '\0') {
    writef("[MASTER][TOPO][VERIFY] aborted reason=%s", reason);
  }
  topology_verify_ = TopologyVerifySessionState{};
}

void MasterCli::maybeFinalizeTopologyVerifySession_() {
  if (!topology_verify_.active) {
    return;
  }
  uint32_t passed = 0U;
  uint32_t failed = 0U;
  uint32_t pending = 0U;
  for (auto& target : topology_verify_.targets) {
    const bool done = target.status_done && target.slots_done;
    if (!done) {
      ++pending;
      continue;
    }
    const bool ok = target.status_ok && target.slots_ok;
    if (!target.reported) {
      const std::string reason = !target.status_ok ? target.status_reason : target.slots_reason;
      if (ok) {
        const uint8_t expected_slots = countEnabledSnapshotSlotsForVerify(target.expected_snapshot);
        const uint8_t expected_groups = countEnabledSnapshotGroupsForVerify(target.expected_snapshot);
        writef("[MASTER][TOPO][VERIFY] PASS target=%s ver=%lu slots=%u groups=%u",
               macToPrintable(target.target).c_str(),
               static_cast<unsigned long>(target.expected_snapshot.topology_version),
               static_cast<unsigned int>(expected_slots),
               static_cast<unsigned int>(expected_groups));
      } else {
        writef("[MASTER][TOPO][VERIFY] FAIL target=%s reason=%s",
               macToPrintable(target.target).c_str(),
               reason.empty() ? "unknown" : reason.c_str());
      }
      target.reported = true;
    }
    if (ok) {
      ++passed;
    } else {
      ++failed;
    }
  }

  if (pending > 0U) {
    return;
  }
  const uint32_t elapsed_ms = nowMs() - topology_verify_.started_ms;
  writef("[MASTER][TOPO][VERIFY] summary path=%s topo_ver=%lu targets=%u pass=%u fail=%u elapsed_ms=%lu",
         topology_verify_.source_path.empty() ? "/o/s/tp.json" : topology_verify_.source_path.c_str(),
         static_cast<unsigned long>(topology_verify_.topology_version),
         static_cast<unsigned int>(topology_verify_.targets.size()),
         static_cast<unsigned int>(passed),
         static_cast<unsigned int>(failed),
         static_cast<unsigned long>(elapsed_ms));
  clearTopologyVerifySession_();
}

void MasterCli::checkTopologyVerifyTimeout_(uint32_t now_ms) {
  if (!topology_verify_.active) {
    return;
  }
  if (static_cast<int32_t>(now_ms - topology_verify_.deadline_ms) < 0) {
    return;
  }
  topology_verify_.requests.clear();
  for (auto& target : topology_verify_.targets) {
    if (!target.status_done) {
      target.status_done = true;
      target.status_ok = false;
      target.status_reason = "status_timeout";
    }
    if (!target.slots_done) {
      target.slots_done = true;
      target.slots_ok = false;
      target.slots_reason = "slots_timeout";
    }
  }
  maybeFinalizeTopologyVerifySession_();
}

void MasterCli::handleTopologyVerifyResponse_(const ManagementResponse& resp) {
  if (!topology_verify_.active) {
    return;
  }
  const auto req_it = topology_verify_.requests.find(resp.req_id);
  if (req_it == topology_verify_.requests.end()) {
    return;
  }
  if ((resp.status == ManagementStatus::OkDeferred || resp.status == ManagementStatus::Ok) &&
      resp.payload.empty()) {
    return;
  }
  const TopologyVerifyRequestState req_state = req_it->second;
  if (req_state.target_index >= topology_verify_.targets.size()) {
    topology_verify_.requests.erase(req_it);
    maybeFinalizeTopologyVerifySession_();
    return;
  }
  TopologyVerifyTargetState& target = topology_verify_.targets[req_state.target_index];

  auto finalize_status_fail = [&](const std::string& reason) {
    target.status_done = true;
    target.status_ok = false;
    target.status_reason = reason;
    topology_verify_.requests.erase(resp.req_id);
    maybeFinalizeTopologyVerifySession_();
  };
  auto finalize_slots_fail = [&](const std::string& reason) {
    target.slots_done = true;
    target.slots_ok = false;
    target.slots_reason = reason;
    topology_verify_.requests.erase(resp.req_id);
    maybeFinalizeTopologyVerifySession_();
  };

  if (req_state.kind == TopologyVerifyReqKind::Status) {
    if (resp.status != ManagementStatus::Ok && resp.status != ManagementStatus::OkDeferred) {
      finalize_status_fail(std::string("status_") + management_utils::managementStatusToString(resp.status));
      return;
    }

    ManagementTopologyStatusPayload topo{};
    bool parsed = false;
    bool committed_state_known = false;
    bool committed_groups_known = false;
    bool index_window_known = false;
    bool committed_checksum_known = false;

    if (management_utils::parseTopologyStatusPayload(resp.payload, topo)) {
      parsed = true;
      committed_state_known = true;
      committed_groups_known = true;
      index_window_known = true;
      committed_checksum_known = true;
    } else if (!resp.payload.empty()) {
      DescriptorResponse desc{};
      if (manager_.decodeDescriptorResponsePayload(resp.payload.data(), resp.payload.size(), desc)) {
        if (desc.type == DescriptorResponseType::Error) {
          finalize_status_fail(desc.message.empty() ? "descriptor_error" : desc.message);
          return;
        }
        if (desc.type == DescriptorResponseType::Ack) {
          ParsedTopologyStatusForVerify parsed_status{};
          if (parseTopologyStatusAckMessageForVerify(desc.message, parsed_status)) {
            topo = parsed_status.status;
            parsed = true;
            committed_state_known = parsed_status.committed_state_known;
            committed_groups_known = parsed_status.committed_groups_known;
            index_window_known = parsed_status.index_window_known;
            committed_checksum_known = parsed_status.committed_checksum_known;
          }
        }
      }
    }
    if (!parsed) {
      finalize_status_fail("status_payload_parse_failed");
      return;
    }

    const uint8_t expected_slots = countEnabledSnapshotSlotsForVerify(target.expected_snapshot);
    const uint8_t expected_groups = countEnabledSnapshotGroupsForVerify(target.expected_snapshot);
    const uint32_t expected_checksum = computeSnapshotChecksumForVerify(target.expected_snapshot);
    std::string mismatch_reason{};
    if (!topo.has_committed) {
      mismatch_reason = "committed_missing";
    } else if (committed_state_known && topo.committed_state != 2U) {
      mismatch_reason = "committed_state_not_committed";
    } else if (topo.committed_slot_count != expected_slots) {
      mismatch_reason = "committed_slots_mismatch expected=" +
                        std::to_string(static_cast<unsigned int>(expected_slots)) +
                        " actual=" + std::to_string(static_cast<unsigned int>(topo.committed_slot_count));
    } else if (committed_groups_known && topo.committed_group_count != expected_groups) {
      mismatch_reason = "committed_groups_mismatch expected=" +
                        std::to_string(static_cast<unsigned int>(expected_groups)) +
                        " actual=" + std::to_string(static_cast<unsigned int>(topo.committed_group_count));
    } else if (index_window_known &&
               (topo.index_neg != target.expected_snapshot.index_neg ||
                topo.index_pos != target.expected_snapshot.index_pos)) {
      mismatch_reason = "index_window_mismatch expected=(" +
                        std::to_string(static_cast<unsigned int>(target.expected_snapshot.index_neg)) +
                        "," + std::to_string(static_cast<unsigned int>(target.expected_snapshot.index_pos)) +
                        ") actual=(" + std::to_string(static_cast<unsigned int>(topo.index_neg)) +
                        "," + std::to_string(static_cast<unsigned int>(topo.index_pos)) + ")";
    } else if (committed_checksum_known && topo.committed_checksum != expected_checksum) {
      mismatch_reason = "committed_checksum_mismatch expected=0x" +
                        formatHex32ForVerify(expected_checksum) +
                        " actual=0x" + formatHex32ForVerify(topo.committed_checksum);
    }

    target.status_done = true;
    target.status_ok = mismatch_reason.empty();
    target.status_reason = mismatch_reason;
    topology_verify_.requests.erase(resp.req_id);
    maybeFinalizeTopologyVerifySession_();
    return;
  }

  if (req_state.kind == TopologyVerifyReqKind::SlotsCommitted) {
    if (resp.status != ManagementStatus::Ok && resp.status != ManagementStatus::OkDeferred) {
      finalize_slots_fail(std::string("slots_") + management_utils::managementStatusToString(resp.status));
      return;
    }
    uint8_t state = 0U;
    std::vector<ManagementTopologySlotPayload> slots{};
    if (!management_utils::parseTopologySlotsPayload(resp.payload, state, slots)) {
      finalize_slots_fail("slots_payload_parse_failed");
      return;
    }
    if (state != 2U) {
      finalize_slots_fail("slots_state_not_committed");
      return;
    }
    std::string compare_reason{};
    if (!compareTopologySlotsForVerify_(target.expected_snapshot, slots, compare_reason)) {
      finalize_slots_fail(compare_reason);
      return;
    }
    target.slots_done = true;
    target.slots_ok = true;
    target.slots_reason.clear();
    topology_verify_.requests.erase(resp.req_id);
    maybeFinalizeTopologyVerifySession_();
  }
}

void MasterCli::printQueueStatus() const {
  const char* policy = "legacy";
  const CliTrafficPolicy effective = effectiveTrafficPolicy();
  if (effective == CliTrafficPolicy::ManagementOnly) {
    policy = "mgmt_only";
  } else if (effective == CliTrafficPolicy::Auto) {
    policy = "auto";
  }
  writef("[MASTER][CLI] queue depth=%u max=%u sent=%lu fail=%lu drop=%lu",
         static_cast<unsigned int>(descriptor_request_queue_.size()),
         static_cast<unsigned int>(descriptor_queue_max_),
         static_cast<unsigned long>(descriptor_queue_sent_count_),
         static_cast<unsigned long>(descriptor_queue_fail_count_),
         static_cast<unsigned long>(descriptor_queue_drop_count_));
  writef("[MASTER][CLI] traffic policy=%s mailbox(resp=%lu ignored=%lu evt=%lu ignored=%lu) observer(ignored_resp=%lu ignored_evt=%lu)",
         policy,
         static_cast<unsigned long>(mailbox_processed_responses_),
         static_cast<unsigned long>(mailbox_ignored_responses_),
         static_cast<unsigned long>(mailbox_processed_events_),
         static_cast<unsigned long>(mailbox_ignored_events_),
         static_cast<unsigned long>(observer_ignored_pull_responses_),
         static_cast<unsigned long>(observer_ignored_events_));
}

void MasterCli::setAutoPull(bool enabled, uint32_t interval_ms) {
  auto_pull_enabled_ = enabled;
  if (!enabled) {
    auto_pull_has_target_peer_ = false;
    auto_pull_target_peer_ = {};
  }
  if (interval_ms >= 300U) {
    auto_pull_interval_ms_ = interval_ms;
  }
  auto_pull_.setEnabled(enabled, nowMs(), auto_pull_interval_ms_);
}

void MasterCli::handleLine(const std::string& raw) {
  (void)handleLineEx(raw);
}

MasterCli::CommandDispatchResult MasterCli::handleLineEx(const std::string& raw) {
  CommandDispatchResult result{};
  resetDispatchSnapshot_();
  command_target_override_active_ = false;
  command_target_override_peer_ = {};
  const std::string line = trim(raw);
  if (line.empty()) {
    return result;
  }
  result.parsed = true;

  auto clearTargetOverride = [&]() {
    command_target_override_active_ = false;
    command_target_override_peer_ = {};
  };

  std::string command_line = line;
  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.size() >= 2U) {
    const std::string selector = trim(tokens[0]);
    bool selector_used = false;
    bool selector_valid = false;
    MacAddress selected_peer{};

    bool digits_only = !selector.empty();
    for (char c : selector) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        digits_only = false;
        break;
      }
    }
    if (digits_only) {
      selector_used = true;
      const unsigned long idx = std::strtoul(selector.c_str(), nullptr, 10);
      std::vector<MacAddress> persisted{};
      manager_.getPersistedPeers(persisted);
      if (idx < persisted.size()) {
        selected_peer = persisted[static_cast<size_t>(idx)];
        selector_valid = true;
      }
    } else {
      MacAddress mac{};
      if (parseMac(selector, mac)) {
        selector_used = true;
        if (manager_.hasPersistedPair(mac)) {
          selected_peer = mac;
          selector_valid = true;
        }
      }
    }

    if (selector_used) {
      if (!selector_valid) {
        io_.writeln("[MASTER][CLI] invalid target selector (use <paired_index> or paired MAC)");
        result.handled = true;
        result.accepted = false;
        result.status = ManagementStatus::BadPayload;
        result.reject_stage = "target";
        clearTargetOverride();
        return result;
      }
      const size_t first_sep = line.find_first_of(" \t");
      if (first_sep == std::string::npos) {
        io_.writeln("[MASTER][CLI] missing command after target selector");
        result.handled = true;
        result.accepted = false;
        result.status = ManagementStatus::BadPayload;
        result.reject_stage = "target";
        clearTargetOverride();
        return result;
      }
      command_line = trim(line.substr(first_sep + 1U));
      if (command_line.empty()) {
        io_.writeln("[MASTER][CLI] missing command after target selector");
        result.handled = true;
        result.accepted = false;
        result.status = ManagementStatus::BadPayload;
        result.reject_stage = "target";
        clearTargetOverride();
        return result;
      }
      command_target_override_active_ = true;
      command_target_override_peer_ = selected_peer;
    }
  }

  const std::string lower = lowerCopy(command_line);
  refreshRuntimeProfileHint_();

  auto isLocalOnlyHandledCommand = [&](const std::string& lower_cmd) -> bool {
    if (lower_cmd == "help" ||
        startsWith(lower_cmd, "help ") ||
        (lower_cmd.size() > 5U && lower_cmd.compare(lower_cmd.size() - 5U, 5U, " help") == 0) ||
        lower_cmd == "log" ||
        lower_cmd == "log error" ||
        lower_cmd == "log info" ||
        lower_cmd == "log debug" ||
        lower_cmd == "queue" ||
        lower_cmd == "cli status" ||
        lower_cmd == "cli on" ||
        lower_cmd == "cli off" ||
        lower_cmd == "cli.baud" ||
        lower_cmd == "cli.baud.get" ||
        startsWith(lower_cmd, "cli.baud set ") ||
        lower_cmd == "cli.speed" ||
        lower_cmd == "cli.speed.get" ||
        startsWith(lower_cmd, "cli.speed set ") ||
        lower_cmd == "metrics" ||
        lower_cmd == "metrics.reset" ||
        lower_cmd == "active" ||
        lower_cmd == "active clear" ||
        startsWith(lower_cmd, "active ") ||
        lower_cmd == "paired" ||
        lower_cmd == "paired.list" ||
        lower_cmd == "status" ||
        lower_cmd == "time.local" ||
        startsWith(lower_cmd, "autopull ") ||
        lower_cmd == "event.list" ||
        lower_cmd == "event.clear" ||
        lower_cmd == "radio.drytest" ||
        lower_cmd == "comm.test.status" ||
        lower_cmd == "comm.test.report") {
      return true;
    }
    return false;
  };

  auto applyDispatchSnapshot = [&](const std::string& lower_cmd) {
    if (dispatch_snapshot_.seen) {
      result.submitted = true;
      result.accepted = dispatch_snapshot_.accepted;
      result.cmd_id = dispatch_snapshot_.cmd_id;
      result.req_id = dispatch_snapshot_.req_id;
      result.status = dispatch_snapshot_.status;
      if (dispatch_snapshot_.reject_stage != nullptr && dispatch_snapshot_.reject_stage[0] != '\0') {
        result.reject_stage = dispatch_snapshot_.reject_stage;
      } else {
        result.reject_stage.clear();
      }
      return;
    }
    result.submitted = false;
    if (!result.handled || result.status != ManagementStatus::InternalError) {
      return;
    }
    if (isLocalOnlyHandledCommand(lower_cmd)) {
      result.accepted = true;
      result.status = ManagementStatus::Ok;
      result.reject_stage.clear();
    } else {
      result.accepted = false;
      result.status = ManagementStatus::BadPayload;
      result.reject_stage = "validation";
    }
  };

  if (handleCliMetaCommands(lower)) {
    result.handled = true;
    applyDispatchSnapshot(lower);
    clearTargetOverride();
    return result;
  }

  if (!cli_enabled_) {
    result.handled = true;
    result.accepted = false;
    result.status = ManagementStatus::DeniedByPolicy;
    result.reject_stage = "cli_disabled";
    clearTargetOverride();
    return result;
  }

  using Handler = bool (*)(MasterCli*, const std::string&, const std::string&);
  static constexpr Handler kHandlers[] = {
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleListAndStatusCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handlePairingCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleTopologyCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleDescriptorShortCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleTimeCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleAutopullCommand(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handlePushCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleEventCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleSettingsCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleStorageCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleOtaCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleLoggerCommands(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleGetIdCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleSetIdCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleGetCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string& line_cmd, const std::string& lower_cmd) {
        return self->handleSetCommand(line_cmd, lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleTestAndLocalCommands(lower_cmd);
      },
      [](MasterCli* self, const std::string&, const std::string& lower_cmd) {
        return self->handleRestartResetCommands(lower_cmd);
      },
  };

  for (auto fn : kHandlers) {
    if (fn(this, command_line, lower)) {
      result.handled = true;
      applyDispatchSnapshot(lower);
      clearTargetOverride();
      return result;
    }
  }

  io_.writeln("[MASTER][CLI] unknown command (type: help)");
  result.handled = false;
  result.accepted = false;
  result.status = ManagementStatus::UnsupportedCommand;
  result.reject_stage = "parse";
  clearTargetOverride();
  return result;
}

void MasterCli::tick(uint32_t now_ms) {
  pumpManagementMailbox();
  checkTopologyVerifyTimeout_(now_ms);
  if (!cli_enabled_) {
    return;
  }
  pumpDescriptorQueue(now_ms);

  if (probe_pending_kind_ != ProbePendingKind::None) {
    constexpr uint32_t kProbeTimeoutMs = 3500U;
    if (static_cast<int32_t>(now_ms - probe_sent_ms_) >=
        static_cast<int32_t>(kProbeTimeoutMs)) {
      const ProbePendingKind pending = probe_pending_kind_;
      probe_pending_kind_ = ProbePendingKind::None;
      probe_sent_ms_ = 0;
      if (pending == ProbePendingKind::Ping) {
        io_.writeln("[MASTER][PING] timeout; no response from slave");
      } else {
        io_.writeln("[MASTER][LIVE] request timeout; no response from slave");
      }
    }
  }

  if (remote_log_pull_active_ &&
      (!remote_log_pull_has_target_peer_ || !manager_.hasPersistedPair(remote_log_pull_target_peer_))) {
    stopRemoteLogPull("target unavailable during pull", false);
  } else if (remote_log_pull_active_) {
    constexpr uint32_t kRemotePullIdleTimeoutMs = 5000U;
    if (static_cast<int32_t>(now_ms - remote_log_pull_last_activity_ms_) >=
        static_cast<int32_t>(kRemotePullIdleTimeoutMs)) {
      stopRemoteLogPull(remote_log_pull_waiting_status_
                            ? "timeout waiting remote logger status"
                            : "timeout waiting remote logger chunk",
                        false);
    }
  }

  pumpOtaPush(now_ms);

  const MacAddress poll_peer = auto_pull_target_peer_;
  const bool can_poll = auto_pull_has_target_peer_;
  const uint32_t corr_before_autopull = correlation_id_;
  MasterAutoPullTickResult r = auto_pull_.tick(&pull_,
                                                poll_peer,
                                                auto_pull_enabled_ && can_poll,
                                                now_ms,
                                                correlation_id_);
  uint32_t corr_cursor = corr_before_autopull;
  while (corr_cursor != correlation_id_) {
    noteCliOwnedReqId_(corr_cursor);
    ++corr_cursor;
  }
  if (r.offline_timeout) {
    io_.writeln("[MASTER][LIVE] timeout waiting for liveness response; slave offline");
  }
  if (r.offline_stale) {
    io_.writeln("[MASTER][LIVE] stale liveness; slave offline");
  }

  if (list_window_active_ && static_cast<int32_t>(now_ms - list_window_deadline_ms_) >= 0) {
    list_window_active_ = false;
    collect_discovery_ = false;
    io_.writeln("[MASTER][CLI] discovery window finished");
    printDiscovered();
  }
}


}  // namespace espnow_link
