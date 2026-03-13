#include "espnow_link/cli_master.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "cli_helpers.hpp"

namespace espnow_link {

using namespace cli_helpers;

namespace {

const char* storageModeName(StorageBackendMode mode) {
  switch (mode) {
    case StorageBackendMode::Disabled:
      return "disabled";
    case StorageBackendMode::Sd:
      return "sd";
    case StorageBackendMode::Spiffs:
      return "spiffs";
    case StorageBackendMode::Unknown:
    default:
      return "unknown";
  }
}

double bytesToMb(uint32_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

const char* otaTransferStateName(uint8_t state) {
  switch (state) {
    case 0:
      return "idle";
    case 1:
      return "receiving";
    case 2:
      return "ready";
    case 3:
      return "applying";
    case 4:
      return "failed";
    default:
      return "unknown";
  }
}

const char* otaStatusCodeName(uint16_t code) {
  switch (code) {
    case 0x0000:
      return "ok";
    case 0x0001:
      return "storage_not_ready";
    case 0x0002:
      return "gate_denied";
    case 0x0003:
      return "gate_busy";
    case 0x0004:
      return "gate_prep_failed";
    case 0x0005:
      return "image_too_large";
    case 0x0006:
      return "invalid_state";
    case 0x0007:
      return "invalid_argument";
    case 0x0008:
      return "offset_mismatch";
    case 0x0009:
      return "size_mismatch";
    case 0x000A:
      return "crc_mismatch";
    case 0x000B:
      return "apply_rejected";
    case 0x000C:
      return "apply_failed";
    case 0x000D:
      return "timeout";
    case 0x00FF:
      return "internal_error";
    default:
      return "unknown";
  }
}

const char* otaGateDecisionName(uint8_t decision) {
  switch (decision) {
    case 0:
      return "ready";
    case 1:
      return "denied";
    case 2:
      return "busy";
    case 3:
      return "prep_failed";
    default:
      return "unknown";
  }
}

bool parseChildScopedTelemetryKey(const std::string& key, uint8_t& out_vid) {
  out_vid = 0U;
  if (key.size() < 4U || key[0] != 'v') {
    return false;
  }
  size_t dot = key.find('.');
  if (dot == std::string::npos || dot <= 1U) {
    return false;
  }
  for (size_t i = 1U; i < dot; ++i) {
    const unsigned char c = static_cast<unsigned char>(key[i]);
    if (std::isdigit(c) == 0) {
      return false;
    }
  }
  const unsigned long parsed = std::strtoul(key.substr(1U, dot - 1U).c_str(), nullptr, 10);
  if (parsed > 255UL) {
    return false;
  }
  out_vid = static_cast<uint8_t>(parsed);
  return true;
}

}  // namespace

void MasterCli::printMandatoryEvents() const {
  if (mandatory_events_.empty()) {
    io_.writeln("[MASTER][EVENT] no mandatory events");
    return;
  }
  io_.writeln("[MASTER][EVENT] mandatory events:");
  for (size_t i = 0; i < mandatory_events_.size(); ++i) {
    const auto& ev = mandatory_events_[i];
    writef("  %u. peer=%s corr=%lu id=%u sev=%u value=%ld ts=%lu rx_ms=%lu",
           static_cast<unsigned int>(i + 1),
           macToPrintable(ev.peer).c_str(),
           static_cast<unsigned long>(ev.corr_id),
           static_cast<unsigned int>(ev.event_id),
           static_cast<unsigned int>(ev.severity),
           static_cast<long>(ev.event_value),
           static_cast<unsigned long>(ev.event_ts_s),
           static_cast<unsigned long>(ev.rx_ms));
  }
}
void MasterCli::printDiscovered() const {
  if (discovered_.empty()) {
    io_.writeln("[MASTER] no discovered slave");
    return;
  }
  io_.writeln("[MASTER] discovered slaves:");
  for (size_t i = 0; i < discovered_.size(); ++i) {
    const char* name = discovered_[i].node_name.empty() ? "unknown" : discovered_[i].node_name.c_str();
    writef("  %u) %s  rssi=%d  name=%s",
           static_cast<unsigned int>(i),
           macToPrintable(discovered_[i].mac).c_str(),
           static_cast<int>(discovered_[i].rssi),
           name);
  }
}


void MasterCli::printSettingLine(const SettingDescriptor& s) {
  const char* type = "string";
  if (s.value_type == SettingValueType::Int) {
    type = "int";
  } else if (s.value_type == SettingValueType::Float) {
    type = "float";
  } else if (s.value_type == SettingValueType::Bool) {
    type = "bool";
  }

  writef("  - id=0x%04X %s (%s, rw=%s)",
         static_cast<unsigned int>(s.setting_id),
         s.key.c_str(),
         type,
         s.writable ? "yes" : "no");
  writef("    nvs=%s current=%s default=%s",
         s.nvs_key.c_str(),
         s.current_value.c_str(),
         s.default_value.c_str());
  if (!s.description.empty()) {
    writef("    desc=%s", s.description.c_str());
  }
}

const char* MasterCli::descriptorSourceLabel(const DescriptorResponse& d) const {
  if (d.message == "profile-schema") {
    return "profile-schema";
  }
  if (d.message == "profile-primary") {
    return "profile-primary";
  }
  if (d.message == "profile-primary+provider") {
    return "profile-primary+provider";
  }
  return "provider";
}

void MasterCli::printDeviceDescriptorResponse(const DescriptorResponse& d) {
  io_.writeln("[MASTER][DESC] device");
  writef("  type  : %s", d.device.device_type.c_str());
  writef("  id    : %s", d.device.device_id.c_str());
  writef("  name  : %s", d.device.device_name.c_str());
  writef("  hw    : %s", d.device.hw_version.c_str());
  writef("  sw    : %s", d.device.sw_version.c_str());
  writef("  build : %s", d.device.build_id.c_str());
}

void MasterCli::printCapabilitiesDescriptorResponse(const DescriptorResponse& d) {
  remote_profile_id_ = kProfileUnknown;
  remote_settings_count_ = 0;
  for (const auto& cap : d.capabilities) {
    if (cap.key == "profile_id") {
      const unsigned long profile_id = std::strtoul(cap.description.c_str(), nullptr, 10);
      if (profile_id > 0U && profile_id <= 0xFFFFUL) {
        remote_profile_id_ = static_cast<ProfileId>(profile_id);
      }
    } else if (cap.key == "settings_count") {
      remote_settings_count_ = static_cast<uint16_t>(std::strtoul(cap.description.c_str(), nullptr, 10));
    }
  }
  writef("[MASTER][DESC] capabilities (source=%s):", descriptorSourceLabel(d));
  if (d.capabilities.empty()) {
    io_.writeln("  (none)");
    return;
  }
  for (size_t i = 0; i < d.capabilities.size(); ++i) {
    const auto& cap = d.capabilities[i];
    writef("  %u. %s - %s",
           static_cast<unsigned int>(i + 1),
           cap.key.c_str(),
           cap.description.c_str());
  }
}

void MasterCli::printTelemetrySchemaDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][DESC] telemetry schema (source=%s):", descriptorSourceLabel(d));
  if (d.telemetry.empty()) {
    io_.writeln("  (none)");
    return;
  }
  for (size_t i = 0; i < d.telemetry.size(); ++i) {
    const auto& t = d.telemetry[i];
    writef("  %u. id=0x%04X %s [%s] range=%.2f..%.2f | %s",
           static_cast<unsigned int>(i + 1),
           static_cast<unsigned int>(t.metric_id),
           t.key.c_str(),
           t.unit.c_str(),
           static_cast<double>(t.min_value),
           static_cast<double>(t.max_value),
           t.description.c_str());
  }
}

void MasterCli::printTelemetrySnapshotDescriptorResponse(const DescriptorResponse& d) {
  io_.writeln("[MASTER][TELEM] live samples:");
  if (d.telemetry_samples.empty()) {
    io_.writeln("  (none)");
    semu_telem_child_filter_active_ = false;
    return;
  }
  const bool child_filter = semu_telem_child_filter_active_;
  const uint8_t child_vid = semu_telem_child_filter_vid_;
  const uint8_t child_max_vid = semu_telem_child_filter_max_vid_;
  if (child_filter) {
    writef("  filter: child=%u + global", static_cast<unsigned int>(child_vid));
  }
  uint32_t printed = 0U;
  for (size_t i = 0; i < d.telemetry_samples.size(); ++i) {
    const auto& t = d.telemetry_samples[i];
    if (child_filter) {
      uint8_t sample_vid = 0U;
      const bool is_child_metric = parseChildScopedTelemetryKey(t.key, sample_vid);
      if (is_child_metric && sample_vid > child_max_vid) {
        continue;
      }
      if (is_child_metric && sample_vid != child_vid) {
        continue;
      }
    }
    ++printed;
    writef("  %u. id=0x%04X %s=%s %s",
           static_cast<unsigned int>(printed),
           static_cast<unsigned int>(t.metric_id),
           t.key.c_str(),
           t.value.c_str(),
           t.unit.c_str());
  }
  if (printed == 0U) {
    io_.writeln("  (none)");
  }
  semu_telem_child_filter_active_ = false;
}

void MasterCli::printLivenessDescriptorResponse(const DescriptorResponse& d) {
  const bool from_ping = (probe_pending_kind_ == ProbePendingKind::Ping);
  const bool from_live_cmd = (probe_pending_kind_ == ProbePendingKind::Live);
  if (from_ping) {
    const uint32_t rtt_ms = (probe_sent_ms_ == 0) ? 0 : (nowMs() - probe_sent_ms_);
    MacAddress resolved_peer{};
    const bool has_peer = resolveRuntimePeer(resolved_peer);
    const std::string peer_label = has_peer ? macToPrintable(resolved_peer) : std::string("unknown");
    writef("[MASTER][PING] pong peer=%s rtt_ms=%lu online=%s",
           peer_label.c_str(),
           static_cast<unsigned long>(rtt_ms),
           d.liveness.online ? "yes" : "no");
  }
  probe_pending_kind_ = ProbePendingKind::None;
  probe_sent_ms_ = 0;
  bool recovered = false;
  auto_pull_.onLivenessResponse(d.liveness.online, nowMs(), recovered);
  if (from_live_cmd) {
    writef("[MASTER][LIVE] online=%s uptime_ms=%lu state=%s",
           d.liveness.online ? "yes" : "no",
           static_cast<unsigned long>(d.liveness.uptime_ms),
           d.liveness.state.c_str());
  }
  if (recovered && from_live_cmd) {
    io_.writeln("[MASTER][LIVE] slave recovered");
  }
}

void MasterCli::printTimeDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][TIME] epoch_s=%llu uptime_ms=%lu",
         static_cast<unsigned long long>(d.time.epoch_s),
         static_cast<unsigned long>(d.time.uptime_ms));
}

void MasterCli::printSettingsDescriptorResponse(const DescriptorResponse& d) {
  writef("[MASTER][DESC] settings (source=%s):", descriptorSourceLabel(d));
  if (d.message == "truncated") {
    writef("[MASTER][DESC] note: truncated by payload limit (received=%u, expected=%u). use settings.full",
           static_cast<unsigned int>(d.settings.size()),
           static_cast<unsigned int>(remote_settings_count_));
  }
  if (d.settings.empty()) {
    io_.writeln("  (none)");
    return;
  }
  for (size_t i = 0; i < d.settings.size(); ++i) {
    const auto& s = d.settings[i];
    const char* type = "string";
    if (s.value_type == SettingValueType::Int) {
      type = "int";
    } else if (s.value_type == SettingValueType::Float) {
      type = "float";
    } else if (s.value_type == SettingValueType::Bool) {
      type = "bool";
    }
    writef("  %u. id=0x%04X %s (%s, rw=%s) current=%s default=%s",
           static_cast<unsigned int>(i + 1),
           static_cast<unsigned int>(s.setting_id),
           s.key.c_str(),
           type,
           s.writable ? "yes" : "no",
           s.current_value.c_str(),
           s.default_value.c_str());
    if (!s.description.empty()) {
      writef("     %s", s.description.c_str());
    }
  }
}

void MasterCli::printSingleSettingDescriptorResponse(const DescriptorResponse& d) {
  writef("[MASTER][DESC] setting (source=%s):", descriptorSourceLabel(d));
  printSettingLine(d.setting);
}

void MasterCli::printLogStatusDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][LOGGER][REMOTE] available=%s enabled=%s level=%u size=%lu dropped=%lu records=%lu rotations=%lu",
         d.logger_available ? "yes" : "no",
         d.logger_enabled ? "yes" : "no",
         static_cast<unsigned int>(d.logger_min_level),
         static_cast<unsigned long>(d.log_bytes_used),
         static_cast<unsigned long>(d.log_bytes_dropped),
         static_cast<unsigned long>(d.log_records_appended),
         static_cast<unsigned long>(d.log_rotations));
}

void MasterCli::printLogChunkDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][LOGGER][REMOTE] chunk offset=%lu total=%lu bytes=%u",
         static_cast<unsigned long>(d.log_chunk_offset),
         static_cast<unsigned long>(d.log_total_size),
         static_cast<unsigned int>(d.log_chunk.size()));

  if (d.log_chunk.empty()) {
    io_.writeln("  (empty)");
    return;
  }

  for (size_t i = 0; i < d.log_chunk.size(); i += 16U) {
    char linebuf[96] = {0};
    int p = std::snprintf(linebuf, sizeof(linebuf), "  %08lX: ",
                          static_cast<unsigned long>(d.log_chunk_offset + static_cast<uint32_t>(i)));
    const size_t end = std::min<size_t>(i + 16U, d.log_chunk.size());
    for (size_t j = i; j < end && p > 0 && static_cast<size_t>(p) < sizeof(linebuf); ++j) {
      p += std::snprintf(linebuf + p, sizeof(linebuf) - static_cast<size_t>(p), "%02X ", d.log_chunk[j]);
    }
    io_.writeln(std::string(linebuf));
  }
}

void MasterCli::printStorageInfoDescriptorResponse(const DescriptorResponse& d) const {
  const double total_mb = bytesToMb(d.storage_info.total_bytes);
  const double used_mb = bytesToMb(d.storage_info.used_bytes);
  const double free_mb = bytesToMb(d.storage_info.free_bytes);
  const double used_pct = (d.storage_info.total_bytes == 0U)
                              ? 0.0
                              : (100.0 * static_cast<double>(d.storage_info.used_bytes) /
                                 static_cast<double>(d.storage_info.total_bytes));

  auto extractNoteField = [](const std::string& note, const char* field) -> std::string {
    const std::string key = std::string(field) + "=";
    const size_t begin = note.find(key);
    if (begin == std::string::npos) {
      return std::string();
    }
    const size_t value_begin = begin + key.size();
    const size_t value_end = note.find(' ', value_begin);
    if (value_end == std::string::npos) {
      return note.substr(value_begin);
    }
    return note.substr(value_begin, value_end - value_begin);
  };

  const char* state = (!d.storage_info.available) ? "unavailable" : (d.storage_info.mounted ? "ready" : "not-mounted");
  writef("[MASTER][STORAGE] %s %s | used %.2f/%.2f MB (%.2f%%) | free %.2f MB",
         storageModeName(d.storage_info.mode),
         state,
         used_mb,
         total_mb,
         used_pct,
         free_mb);
  writef("[MASTER][STORAGE] path root=%s cwd=%s", d.storage_info.root_path.c_str(), d.storage_info.cwd.c_str());
  if (!d.message.empty()) {
    const std::string card_type = extractNoteField(d.message, "card_type");
    const std::string card_mb = extractNoteField(d.message, "card_mb");
    if (!card_type.empty() || !card_mb.empty()) {
      writef("[MASTER][STORAGE] card type=%s size=%sMB",
             card_type.empty() ? "unknown" : card_type.c_str(),
             card_mb.empty() ? "?" : card_mb.c_str());
    } else {
      writef("[MASTER][STORAGE] note=%s", d.message.c_str());
    }
  }
}

void MasterCli::printStorageListDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][STORAGE] list path=%s parent=%s count=%u",
         d.storage_path.c_str(),
         d.storage_parent_path.c_str(),
         static_cast<unsigned int>(d.storage_entries.size()));
  if (d.storage_entries.empty()) {
    io_.writeln("  (empty)");
    return;
  }
  for (size_t i = 0; i < d.storage_entries.size(); ++i) {
    const auto& e = d.storage_entries[i];
    writef("  %u. %s %s size=%lu",
           static_cast<unsigned int>(i + 1),
           e.is_dir ? "[D]" : "[F]",
           e.name.c_str(),
           static_cast<unsigned long>(e.size_bytes));
  }
}

void MasterCli::printStorageStatDescriptorResponse(const DescriptorResponse& d) {
  const std::string resolved_path = d.storage_stat.path.empty() ? d.storage_path : d.storage_stat.path;
  writef("[MASTER][STORAGE] stat path=%s exists=%s type=%s size=%lu",
         resolved_path.c_str(),
         d.storage_stat.exists ? "yes" : "no",
         d.storage_stat.is_dir ? "dir" : "file",
         static_cast<unsigned long>(d.storage_stat.size_bytes));

  if (!remote_storage_cd_pending_.empty()) {
    const std::string pending = remote_storage_cd_pending_;
    remote_storage_cd_pending_.clear();
    if (d.storage_stat.exists && d.storage_stat.is_dir) {
      remote_storage_cwd_ = resolved_path.empty() ? pending : resolved_path;
      writef("[MASTER][SD][REMOTE] cwd=%s", remote_storage_cwd_.c_str());
    } else {
      writef("[MASTER][SD][REMOTE] cd failed path=%s", pending.c_str());
    }
  }
}

void MasterCli::printOtaStatusDescriptorResponse(const DescriptorResponse& d) const {
  const auto& s = d.ota_status;
  writef("[MASTER][OTA] state=%s(%u) code=%s(0x%04X) size=%lu/%lu crc=0x%08lX/0x%08lX",
         otaTransferStateName(s.transfer_state),
         static_cast<unsigned int>(s.transfer_state),
         otaStatusCodeName(s.status_code),
         static_cast<unsigned int>(s.status_code),
         static_cast<unsigned long>(s.received_size),
         static_cast<unsigned long>(s.expected_size),
         static_cast<unsigned long>(s.actual_crc32),
         static_cast<unsigned long>(s.expected_crc32));
  if (!s.temp_path.empty() || !s.image_path.empty()) {
    writef("[MASTER][OTA] temp=%s image=%s",
           s.temp_path.empty() ? "-" : s.temp_path.c_str(),
           s.image_path.empty() ? "-" : s.image_path.c_str());
  }
  if (!s.persistent_state.empty() && s.persistent_state != "none") {
    writef("[MASTER][OTA] persisted=%s epoch=%lu sw=%s build=%s",
           s.persistent_state.c_str(),
           static_cast<unsigned long>(s.persistent_epoch_s),
           s.confirmed_sw_version.empty() ? "-" : s.confirmed_sw_version.c_str(),
           s.confirmed_build_id.empty() ? "-" : s.confirmed_build_id.c_str());
  }
  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printOtaManifestDescriptorResponse(const DescriptorResponse& d) const {
  if (d.is_paged) {
    writef("[MASTER][OTA] manifest page snapshot=%lu total=%u cursor=%u returned=%u next=%u done=%s",
           static_cast<unsigned long>(d.snapshot_id),
           static_cast<unsigned int>(d.total_count),
           static_cast<unsigned int>(d.cursor),
           static_cast<unsigned int>(d.returned_count),
           static_cast<unsigned int>(d.next_cursor),
           d.done ? "yes" : "no");
  } else {
    writef("[MASTER][OTA] manifest entries=%u", static_cast<unsigned int>(d.ota_manifest.size()));
  }

  if (d.ota_manifest.empty()) {
    io_.writeln("  (empty)");
    if (!d.message.empty()) {
      writef("[MASTER][OTA] note=%s", d.message.c_str());
    }
    return;
  }

  for (size_t i = 0; i < d.ota_manifest.size(); ++i) {
    const auto& e = d.ota_manifest[i];
    writef("  %u. id=%lu name=%s size=%luB crc=0x%08lX state=%s",
           static_cast<unsigned int>(i + 1),
           static_cast<unsigned long>(e.file_id),
           e.file_name.c_str(),
           static_cast<unsigned long>(e.size_bytes),
           static_cast<unsigned long>(e.crc32),
           e.state.c_str());
    writef("     version=%s build=%s created=%lu app_required=%luB",
           e.version.c_str(),
           e.build_id.c_str(),
           static_cast<unsigned long>(e.created_epoch_s),
           static_cast<unsigned long>(e.required_app_bytes));
  }

  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printOtaCapacityDescriptorResponse(const DescriptorResponse& d) const {
  const auto& c = d.ota_capacity;
  writef("[MASTER][OTA] capacity max_fw=%.2fMB last_image=%.2fMB fit=%s",
         bytesToMb(c.max_fw_bytes),
         bytesToMb(c.last_checked_image_bytes),
         c.last_fit ? "yes" : "no");
  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printOtaGateDescriptorResponse(const DescriptorResponse& d) const {
  const auto& g = d.ota_gate;
  writef("[MASTER][OTA] gate=%s(%u) detail=%s",
         otaGateDecisionName(g.decision),
         static_cast<unsigned int>(g.decision),
         g.detail.empty() ? "-" : g.detail.c_str());
  if (!d.message.empty()) {
    writef("[MASTER][OTA] note=%s", d.message.c_str());
  }
}

void MasterCli::printAckDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][DESC] ok: %s", d.message.c_str());
}

void MasterCli::printErrorDescriptorResponse(const DescriptorResponse& d) const {
  writef("[MASTER][DESC] error: %s", d.message.c_str());
}

void MasterCli::printDescriptorResponse(const DescriptorResponse& d) {
  if (d.type == DescriptorResponseType::Device) {
    printDeviceDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Capabilities) {
    printCapabilitiesDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Telemetry) {
    printTelemetrySchemaDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::TelemetrySnapshot) {
    printTelemetrySnapshotDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Liveness) {
    printLivenessDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Time) {
    printTimeDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Settings) {
    printSettingsDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Setting) {
    printSingleSettingDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::LogStatus) {
    printLogStatusDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::LogChunk) {
    printLogChunkDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::StorageInfo) {
    printStorageInfoDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::StorageList) {
    printStorageListDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::StorageStat) {
    printStorageStatDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaStatus) {
    printOtaStatusDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaManifest) {
    printOtaManifestDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaCapacity) {
    printOtaCapacityDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::OtaGateInfo) {
    printOtaGateDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Ack) {
    printAckDescriptorResponse(d);
    return;
  }

  if (d.type == DescriptorResponseType::Error) {
    printErrorDescriptorResponse(d);
  }
}

}  // namespace espnow_link
