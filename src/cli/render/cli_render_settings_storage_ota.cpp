/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Settings/log/storage/OTA/ack/error descriptor render paths.
 **************************************************************/
#include "../internal/cli_render_internal.hpp"
#include "../internal/cli_render_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

void MasterCli::printSettingsDescriptorResponse(const DescriptorResponse& d) {
  const unsigned int total_settings =
      (d.total_count != 0U) ? static_cast<unsigned int>(d.total_count)
                            : static_cast<unsigned int>(d.settings.size());
  writef("[MASTER][SET] Settings snapshot=%lu source=%s total=%u",
         static_cast<unsigned long>(d.snapshot_id),
         descriptorSourceLabel(d),
         total_settings);
  if (d.message == "truncated") {
    writef("[MASTER][SET] note: truncated by payload limit (received=%u, expected=%u). use settings.full",
           static_cast<unsigned int>(d.settings.size()),
           static_cast<unsigned int>(remote_settings_count_));
  }
  if (d.settings.empty()) {
    io_.writeln("  (none)");
    return;
  }

  struct DisplayRow {
    std::string id;
    std::string key;
    std::string value;
    std::string default_value;
    std::string type;
    std::string rw;
    std::string notes;
  };

  const char* headers[7] = {"ID", "Key", "Value", "Default", "Type", "RW", "Range / Notes"};
  const size_t max_widths[7] = {6U, 22U, 20U, 20U, 6U, 2U, 32U};
  const SettingsSection order[] = {
      SettingsSection::General,
      SettingsSection::UiFeedback,
      SettingsSection::Protection,
      SettingsSection::PushRuntime,
      SettingsSection::Topology,
      SettingsSection::Provisioning,
      SettingsSection::Other,
  };

  auto printTable = [&](const char* title, const std::vector<DisplayRow>& rows) {
    io_.writeln("");
    writef("[%s]", title);

    std::vector<size_t> widths = {2U, 3U, 5U, 7U, 4U, 2U, 13U};
    for (size_t i = 0; i < 7U; ++i) {
      const size_t header_len = std::strlen(headers[i]);
      if (header_len > widths[i]) {
        widths[i] = header_len;
      }
    }
    for (const auto& row : rows) {
      const std::string cells[7] = {
          row.id, row.key, row.value, row.default_value, row.type, row.rw, row.notes};
      for (size_t i = 0; i < 7U; ++i) {
        size_t candidate = cells[i].size();
        if (candidate > max_widths[i]) {
          candidate = max_widths[i];
        }
        if (candidate > widths[i]) {
          widths[i] = candidate;
        }
      }
    }

    const std::string border = tableBorder(widths);
    io_.writeln(border);

    auto emitRow = [&](const std::string (&cells)[7]) {
      std::string line = "|";
      for (size_t i = 0; i < 7U; ++i) {
        line += " ";
        line += fitCell(cells[i], widths[i]);
        line += " |";
      }
      io_.writeln(line);
    };

    const std::string header_cells[7] = {headers[0],
                                         headers[1],
                                         headers[2],
                                         headers[3],
                                         headers[4],
                                         headers[5],
                                         headers[6]};
    emitRow(header_cells);
    io_.writeln(border);

    for (const auto& row : rows) {
      const std::string cells[7] = {row.id,
                                    row.key,
                                    row.value,
                                    row.default_value,
                                    row.type,
                                    row.rw,
                                    row.notes};
      emitRow(cells);
    }
    io_.writeln(border);
  };

  for (SettingsSection section : order) {
    std::vector<DisplayRow> rows{};
    rows.reserve(d.settings.size());
    for (const auto& s : d.settings) {
      if (classifySettingSection(s) != section) {
        continue;
      }
      char id_buf[16] = {0};
      std::snprintf(id_buf, sizeof(id_buf), "0x%04X", static_cast<unsigned int>(s.setting_id));
      std::string type = renderSettingType(s);
      std::string enum_text{};
      if (parseDescField(s.description, "enum", enum_text) && !enum_text.empty() && type != "bool") {
        type = "enum";
      }
      rows.push_back(DisplayRow{
          id_buf,
          s.key,
          renderSettingValue(s, s.current_value),
          renderSettingValue(s, s.default_value),
          type,
          s.writable ? "Y" : "N",
          renderSettingNotes(s),
      });
    }
    if (!rows.empty()) {
      printTable(sectionTitle(section), rows);
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

  auto toUpperAscii = [](std::string value) -> std::string {
    for (char& c : value) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
  };

  const char* backend = "UNKNOWN";
  switch (d.storage_info.mode) {
    case StorageBackendMode::Sd:
      backend = "SD CARD";
      break;
    case StorageBackendMode::Spiffs:
      backend = "SPIFFS";
      break;
    case StorageBackendMode::Disabled:
      backend = "DISABLED";
      break;
    case StorageBackendMode::Unknown:
    default:
      backend = "UNKNOWN";
      break;
  }
  const char* state = "UNAVAILABLE";
  if (d.storage_info.available) {
    state = d.storage_info.mounted ? "READY" : "NOT-MOUNTED";
  }
  std::string card_type = toUpperAscii(extractNoteField(d.message, "card_type"));
  if (card_type.empty()) {
    card_type = "UNKNOWN";
  }
  const std::string root = d.storage_info.root_path.empty() ? "/" : d.storage_info.root_path;
  const std::string cwd = d.storage_info.cwd.empty() ? "/" : d.storage_info.cwd;

  writef("[MASTER][STORAGE] %s | %s | %s | ROOT:%s | CWD:%s",
         backend,
         state,
         card_type.c_str(),
         root.c_str(),
         cwd.c_str());
  writef("[MASTER][STORAGE] FREE: %.2f MB | USED: %.2f/%.2f MB",
         free_mb,
         used_mb,
         total_mb);

  constexpr size_t kBarWidth = 20U;
  size_t used_slots = static_cast<size_t>((used_pct * static_cast<double>(kBarWidth) / 100.0) + 0.5);
  if (used_pct > 0.0 && used_slots == 0U) {
    used_slots = 1U;
  }
  if (used_slots > kBarWidth) {
    used_slots = kBarWidth;
  }
  const std::string bar = std::string(used_slots, '#') + std::string(kBarWidth - used_slots, '-');
  writef("[MASTER][STORAGE] USAGE [%s] %.2f%%", bar.c_str(), used_pct);
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
