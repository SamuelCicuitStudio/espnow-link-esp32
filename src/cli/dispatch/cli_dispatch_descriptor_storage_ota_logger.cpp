/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Descriptor, storage, OTA, and logger command-family dispatch paths.
 **************************************************************/
#include "../internal/cli_dispatch_internal.hpp"
#include "../internal/cli_dispatch_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

bool MasterCli::handleDescriptorShortCommands(const std::string& lower) {
  if (lower == "live enable") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LiveMonitorEnable),
                                           {},
                                           nullptr,
                                           0U,
                                           false);
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][LIVE] monitor enable requested" : "[MASTER][LIVE] monitor enable request failed");
    return true;
  }
  if (lower == "live disable") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LiveMonitorDisable),
                                           {},
                                           nullptr,
                                           0U,
                                           false);
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][LIVE] monitor disable requested" : "[MASTER][LIVE] monitor disable request failed");
    return true;
  }
  if (lower == "live status") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    live_monitor_status_pending_ = false;
    live_monitor_status_req_id_ = 0U;
    uint32_t req_id = 0;
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LiveMonitorStatusGet),
                                           {},
                                           &req_id,
                                           0U,
                                           false);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      live_monitor_status_pending_ = true;
      live_monitor_status_req_id_ = req_id;
    }
    io_.writeln(ok ? "[MASTER][LIVE] monitor status requested" : "[MASTER][LIVE] monitor status request failed");
    return true;
  }
  if (lower == "desc") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::DescGet));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][CLI] desc requested");
    } else {
      io_.writeln("[MASTER][CLI] descriptor request failed");
    }
    return true;
  }
  if (lower == "caps") {
    (void)startPagedFetch(PagedFetchKind::Capabilities, 6, "[MASTER][CLI] caps paged fetch started");
    return true;
  }
  if (lower == "telem") {
    (void)startPagedFetch(PagedFetchKind::Telemetry, 6, "[MASTER][CLI] telemetry paged fetch started");
    return true;
  }
  if (startsWith(lower, "telem.now.child ")) {
    ProfileId profile_id = kProfileUnknown;
    (void)ensureRuntimeProfileKnown_(profile_id, true);
    uint8_t max_vid = 15U;
    const char* profile_label = "TARGET";
    if (profile_id == kProfileSemu) {
      max_vid = 7U;
      profile_label = "SEMU";
    } else if (profile_id == kProfileRemu) {
      max_vid = 15U;
      profile_label = "REMU";
    } else {
      io_.writeln("[MASTER][CLI] profile unresolved; using vid range 0..15 until probe resolves");
    }
    const std::vector<std::string> tokens = splitTokens(lower);
    if (tokens.size() != 2U && tokens.size() != 3U) {
      writef("[MASTER][CLI] usage: telem.now.child <vid:0..%u>", static_cast<unsigned int>(max_vid));
      return true;
    }
    uint32_t vid = 0U;
    const std::string& vid_token = (tokens.size() == 2U) ? tokens[1] : tokens[2];
    if (!parseU32Token(vid_token, vid) || vid > max_vid) {
      writef("[MASTER][CLI] invalid child vid (0..%u)", static_cast<unsigned int>(max_vid));
      return true;
    }
    semu_telem_child_filter_active_ = true;
    semu_telem_child_filter_vid_ = static_cast<uint8_t>(vid);
    semu_telem_child_filter_max_vid_ = max_vid;
    const std::string queued_msg =
        std::string("[MASTER][CLI] requested ") + profile_label + " child telemetry vid=" +
        std::to_string(static_cast<unsigned int>(semu_telem_child_filter_vid_)) + " (+global)";
    const bool ok = startPagedFetch(PagedFetchKind::TelemetrySnapshot, 6, queued_msg.c_str());
    if (ok) {
      // queued message emitted by startPagedFetch
    } else {
      semu_telem_child_filter_active_ = false;
      io_.writeln("[MASTER][CLI] child telemetry request failed");
    }
    return true;
  }
  if (lower == "telem.now") {
    semu_telem_child_filter_active_ = false;
    semu_telem_child_filter_max_vid_ = 7U;
    const bool ok = startPagedFetch(PagedFetchKind::TelemetrySnapshot, 6, "[MASTER][CLI] requested live telemetry");
    if (ok) {
      // queued message emitted by startPagedFetch
    } else {
      io_.writeln("[MASTER][CLI] live telemetry request failed");
    }
    return true;
  }
  if (lower == "live") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::LiveGet));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][CLI] requested liveness");
    } else {
      io_.writeln("[MASTER][CLI] liveness request failed");
    }
    return true;
  }
  if (lower == "ping") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::PingGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][PING] request queued" : "[MASTER][PING] request failed");
    return true;
  }
  if (lower == "audio ping") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::AudioPingRequest));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][AUDIO] ping request queued" : "[MASTER][AUDIO] ping request failed");
    return true;
  }
  return false;
}

bool MasterCli::handleEventCommands(const std::string& lower) {
  if (lower == "event.list") {
    printMandatoryEvents();
    return true;
  }

  if (lower == "event.clear") {
    mandatory_events_.clear();
    io_.writeln("[MASTER][EVENT] cache cleared");
    return true;
  }

  return false;
}

bool MasterCli::handleSettingsCommands(const std::string& lower) {
  if (lower == "settings") {
    (void)startPagedFetch(PagedFetchKind::Settings, 4, "[MASTER][CLI] settings paged fetch started");
    return true;
  }

  if (lower == "settings.full") {
    (void)requestFullSettingsByProfile();
    return true;
  }

  if (lower == "settings.raw") {
    (void)startPagedFetch(PagedFetchKind::Settings, 4, "[MASTER][CLI] settings.raw paged fetch started");
    return true;
  }

  return false;
}

bool MasterCli::handleStorageCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "sd.")) {
    return false;
  }

  auto printStorageError = [&](const std::string& fallback, const std::string& msg) {
    DescriptorResponse d{};
    d.type = DescriptorResponseType::Error;
    d.message = msg.empty() ? fallback : msg;
    printDescriptorResponse(d);
  };

  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.empty()) {
    io_.writeln("[MASTER][SD] invalid command");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "parse");
    return true;
  }

  if (lower == "sd.remote.pwd") {
    writef("[MASTER][SD][REMOTE] cwd=%s", remote_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (lower == "sd.remote.info") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::StorageInfoGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][SD][REMOTE] info requested" : "[MASTER][SD][REMOTE] info request failed");
    return true;
  }
  if (startsWith(lower, "sd.remote.ls")) {
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][SD][REMOTE] usage: sd.remote.ls [path]");
      return true;
    }
    const std::string path = (tokens.size() == 2U) ? tokens[1] : remote_storage_cwd_;
    const std::string resolved = resolveFsPath(remote_storage_cwd_, path);
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageList),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] ls requested path=%s", resolved.c_str());
    } else {
      io_.writeln("[MASTER][SD][REMOTE] ls request failed");
    }
    return true;
  }
  if (startsWith(lower, "sd.remote.stat ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][REMOTE] usage: sd.remote.stat <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(remote_storage_cwd_, tokens[1]);
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageStat),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] stat requested path=%s", resolved.c_str());
    } else {
      io_.writeln("[MASTER][SD][REMOTE] stat request failed");
    }
    return true;
  }
  if (startsWith(lower, "sd.remote.cd ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][REMOTE] usage: sd.remote.cd <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(remote_storage_cwd_, tokens[1]);
    remote_storage_cd_pending_ = resolved;
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageStat),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] cd requested path=%s", resolved.c_str());
    } else {
      remote_storage_cd_pending_.clear();
      io_.writeln("[MASTER][SD][REMOTE] cd request failed");
    }
    return true;
  }
  if (lower == "sd.remote.up") {
    const std::string resolved = parentFsPath(remote_storage_cwd_);
    remote_storage_cd_pending_ = resolved;
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::StorageStat),
                                           management_utils::buildStringPayloadU16(resolved));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][SD][REMOTE] up requested path=%s", resolved.c_str());
    } else {
      remote_storage_cd_pending_.clear();
      io_.writeln("[MASTER][SD][REMOTE] up request failed");
    }
    return true;
  }
  if (lower == "sd.remote.format") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::StorageFormat));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][SD][REMOTE] format requested");
    } else {
      io_.writeln("[MASTER][SD][REMOTE] format request failed");
    }
    return true;
  }

  if (lower == "sd.pwd") {
    writef("[MASTER][SD][LOCAL] cwd=%s", local_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (local_storage_ == nullptr) {
    io_.writeln("[MASTER][SD][LOCAL] storage explorer unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  if (lower == "sd.info") {
    StorageInfo info{};
    std::string msg;
    if (!local_storage_->getStorageInfo(info, msg)) {
      printStorageError("local storage info unavailable", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!info.cwd.empty()) {
      local_storage_cwd_ = normalizeFsPath(info.cwd);
    }
    DescriptorResponse d{};
    d.type = DescriptorResponseType::StorageInfo;
    d.storage_info = info;
    d.message = msg;
    printDescriptorResponse(d);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (startsWith(lower, "sd.ls")) {
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][SD][LOCAL] usage: sd.ls [path]");
      return true;
    }
    const std::string path = (tokens.size() == 2U) ? tokens[1] : local_storage_cwd_;
    const std::string resolved = resolveFsPath(local_storage_cwd_, path);
    std::vector<StorageEntry> entries;
    std::string canonical;
    std::string parent;
    std::string msg;
    if (!local_storage_->listStoragePath(resolved, canonical, parent, entries, msg)) {
      printStorageError("local storage list failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    DescriptorResponse d{};
    d.type = DescriptorResponseType::StorageList;
    d.storage_path = canonical.empty() ? resolved : canonical;
    d.storage_parent_path = parent;
    d.storage_entries = std::move(entries);
    d.message = msg;
    printDescriptorResponse(d);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (startsWith(lower, "sd.stat ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][LOCAL] usage: sd.stat <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(local_storage_cwd_, tokens[1]);
    StorageStat st{};
    std::string msg;
    if (!local_storage_->statStoragePath(resolved, st, msg)) {
      printStorageError("local storage stat failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    DescriptorResponse d{};
    d.type = DescriptorResponseType::StorageStat;
    d.storage_path = resolved;
    d.storage_stat = st;
    d.message = msg;
    printDescriptorResponse(d);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (startsWith(lower, "sd.cd ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][SD][LOCAL] usage: sd.cd <path>");
      return true;
    }
    const std::string resolved = resolveFsPath(local_storage_cwd_, tokens[1]);
    StorageStat st{};
    std::string msg;
    if (!local_storage_->statStoragePath(resolved, st, msg)) {
      printStorageError("local storage cd failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!st.exists || !st.is_dir) {
      io_.writeln("[MASTER][SD][LOCAL] cd failed: target is not directory");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
      return true;
    }
    local_storage_cwd_ = normalizeFsPath(st.path.empty() ? resolved : st.path);
    writef("[MASTER][SD][LOCAL] cwd=%s", local_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (lower == "sd.up") {
    const std::string resolved = parentFsPath(local_storage_cwd_);
    StorageStat st{};
    std::string msg;
    if (!local_storage_->statStoragePath(resolved, st, msg)) {
      printStorageError("local storage up failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!st.exists || !st.is_dir) {
      io_.writeln("[MASTER][SD][LOCAL] up failed: parent is not directory");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
      return true;
    }
    local_storage_cwd_ = normalizeFsPath(st.path.empty() ? resolved : st.path);
    writef("[MASTER][SD][LOCAL] cwd=%s", local_storage_cwd_.c_str());
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }
  if (lower == "sd.format") {
    io_.writeln("[MASTER][SD][LOCAL] format started (erasing and rebuilding layout)...");
    bool restore_logger = false;
    bool logger_enabled_before = false;
    if (logger_ != nullptr) {
      logger_enabled_before = logger_->enabled();
      if (logger_enabled_before) {
        logger_->setEnabled(false);
        restore_logger = true;
      }
    }

    std::string msg;
    const bool ok = local_storage_->formatStorage(msg);

    if (restore_logger && logger_ != nullptr) {
      logger_->setEnabled(logger_enabled_before);
    }

    if (!ok) {
      printStorageError("local storage format failed", msg);
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    io_.writeln(msg.empty() ? "[MASTER][SD][LOCAL] format done" : msg);
    local_storage_cwd_ = "/";
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  io_.writeln("[MASTER][SD] unknown storage command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

bool MasterCli::handleOtaCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "ota.")) {
    return false;
  }

  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.empty()) {
    io_.writeln("[MASTER][OTA] invalid command");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "parse");
    return true;
  }

  const std::string first_token = lowerCopy(tokens[0]);
  std::string cmd = first_token;
  std::string cmd_line = lower;

  const bool is_archive_cmd =
      startsWith(cmd, "ota.archive.") || startsWith(cmd, "ota.arc.");
  const bool is_prepare_cmd = startsWith(cmd, "ota.prepare");

  if (cmd == "ota.archive" || cmd == "ota.arc") {
    io_.writeln("[MASTER][OTA] archive commands:");
    io_.writeln("  ota.arc.save [master|slave]          (save running firmware)");
    io_.writeln("  ota.arc.save.staged [master|slave]   (save /o/s/fw.bin + fw.json)");
    io_.writeln("  ota.arc.list [master|slave]");
    io_.writeln("  ota.arc.verify <id6hex> [master|slave]");
    io_.writeln("  ota.arc.restore <id6hex> [master|slave]");
    io_.writeln("  ota.arc.delete <id6hex> [master|slave]");
    io_.writeln("  ota.arc.clear [master|slave]");
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (is_archive_cmd) {
    const std::string prefix = startsWith(cmd, "ota.archive.") ? "ota.archive." : "ota.arc.";
    std::string action = cmd.substr(prefix.size());
    if (action == "save_staged") {
      action = "save.staged";
    }

    std::vector<std::string> args;
    args.reserve(tokens.size());
    for (size_t i = 1U; i < tokens.size(); ++i) {
      args.push_back(tokens[i]);
    }

    char role = 'm';
    std::string id;
    const auto parseRoleArg = [&](size_t idx, bool& ok) {
      ok = true;
      if (idx >= args.size()) {
        return;
      }
      if (!otaArchiveNormalizeRole(args[idx], role)) {
        ok = false;
      }
    };

    bool parse_ok = true;
    if (action == "save" || action == "save.staged" || action == "list" || action == "clear") {
      if (args.size() > 1U) {
        parse_ok = false;
      } else if (!args.empty()) {
        parseRoleArg(0U, parse_ok);
      }
    } else if (action == "verify" || action == "restore" || action == "delete") {
      if (args.empty() || args.size() > 2U) {
        parse_ok = false;
      } else {
        id = otaArchiveNormalizeId(args[0]);
        if (id.empty()) {
          parse_ok = false;
        }
        if (args.size() == 2U) {
          parseRoleArg(1U, parse_ok);
        }
      }
    } else {
      parse_ok = false;
    }

    if (!parse_ok) {
      io_.writeln("[MASTER][OTA] usage:");
      io_.writeln("  ota.arc.save [master|slave]");
      io_.writeln("  ota.arc.save.staged [master|slave]");
      io_.writeln("  ota.arc.list [master|slave]");
      io_.writeln("  ota.arc.verify <id6hex> [master|slave]");
      io_.writeln("  ota.arc.restore <id6hex> [master|slave]");
      io_.writeln("  ota.arc.delete <id6hex> [master|slave]");
      io_.writeln("  ota.arc.clear [master|slave]");
      return true;
    }

    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    bool ok = false;
    if (action == "list") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveList),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    } else if (action == "save") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveRunning),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    } else if (action == "save.staged") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveSaveStaged),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    } else if (action == "restore") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveRestore),
                                  management_utils::buildOtaArchivePayload(role, id, false));
    } else if (action == "verify") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveVerify),
                                  management_utils::buildOtaArchivePayload(role, id, false));
    } else if (action == "delete") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveDelete),
                                  management_utils::buildOtaArchivePayload(role, id, false));
    } else if (action == "clear") {
      ok = submitRuntimeTargeted_(mgmt,
                                  static_cast<uint16_t>(ManagementCommandId::OtaArchiveClear),
                                  management_utils::buildOtaArchivePayload(role, {}, false));
    }
    correlation_id_ = mgmt.nextReqId();
    if (!ok) {
      io_.writeln("[MASTER][OTA] archive request failed");
      return true;
    }
    writef("[MASTER][OTA] archive requested action=%s role=%c%s%s",
           action.c_str(),
           role,
           id.empty() ? "" : " id=",
           id.empty() ? "" : id.c_str());
    return true;
  }

  if (is_prepare_cmd) {
    if (tokens.size() != 1U) {
      io_.writeln("[MASTER][OTA] usage: ota.prepare");
      return true;
    }
    if (otaPrepareRemote()) {
      io_.writeln("[MASTER][OTA] remote prepare requested (OTA.CLEAR in)");
    } else {
      io_.writeln("[MASTER][OTA] remote prepare request failed");
    }
    return true;
  }

  if (startsWith(cmd, "ota.stage")) {
    io_.writeln("[MASTER][OTA] ota.stage removed");
    io_.writeln("[MASTER][OTA] place firmware directly in /o/s and use ota.push <name>");
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (cmd == "ota.local.clear.images") {
    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][OTA] local clear unavailable (no local OTA storage backend bound)");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    std::string msg;
    if (!ota_push_storage_->begin(msg)) {
      writef("[MASTER][OTA] local clear failed: storage not ready (%s)", msg.c_str());
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!ota_push_storage_->removePath(ota_paths::kImage, msg)) {
      writef("[MASTER][OTA] local clear failed: %s", msg.empty() ? "remove failed" : msg.c_str());
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    if (!ota_push_storage_->ensureDir(ota_paths::kImage, msg)) {
      writef("[MASTER][OTA] local clear failed: %s", msg.empty() ? "mkdir failed" : msg.c_str());
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "execution");
      return true;
    }
    writef("[MASTER][OTA] local images cleared: %s", ota_paths::kImage);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (cmd == "ota.clear.images") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaClearScope),
                                           management_utils::buildStringPayloadU16("img"));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][OTA] clear requested scope=img");
    } else {
      io_.writeln("[MASTER][OTA] clear request failed");
    }
    return true;
  }

  if (cmd == "ota.push.abort") {
    const bool had_active = ota_push_active_;
    ota_update_pipeline_active_ = false;
    ota_update_prepare_pending_ = false;
    ota_update_prepare_corr_id_ = 0U;
    ota_update_wait_boot_notice_ = false;
    ota_update_image_name_.clear();
    ota_update_req_id_ = 0U;
    ota_update_has_target_peer_ = false;
    ota_update_target_peer_ = {};
    ota_update_staged_path_.clear();
    ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
    if (had_active) {
      stopOtaPush("aborted by operator", false);
    } else {
      io_.writeln("[MASTER][OTA] ota.push is not active (sending remote abort anyway)");
    }
    if (hasRuntimePeer()) {
      if (management_transport_ == nullptr) {
        io_.writeln("[MASTER][CLI] management path unavailable");
        captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
        return true;
      }
      ManagementController mgmt(*management_transport_);
      mgmt.setNextReqId(correlation_id_);
      const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaPushAbort));
      correlation_id_ = mgmt.nextReqId();
      if (ok) {
        io_.writeln("[MASTER][OTA] management abort requested");
      } else {
        io_.writeln("[MASTER][OTA] management abort request failed");
      }
    } else if (!had_active) {
      io_.writeln("[MASTER][OTA] target not selected");
    }
    if (!dispatch_snapshot_.seen) {
      captureDispatchSnapshot_(had_active,
                               0U,
                               0U,
                               had_active ? ManagementStatus::Ok : ManagementStatus::BadPayload,
                               had_active ? "" : "target");
    }
    return true;
  }
  if (startsWith(cmd_line, "ota.update.from.arc ")) {
    if (tokens.size() < 3U || tokens.size() > 4U) {
      io_.writeln("[MASTER][OTA] usage: ota.update.from.arc <id6hex> [chunk_bytes<=220] [master|slave]");
      return true;
    }

    const std::string id = otaArchiveNormalizeId(tokens[1]);
    if (id.empty()) {
      io_.writeln("[MASTER][OTA] invalid archive id");
      return true;
    }

    constexpr uint16_t kOtaUpdateDefaultChunk = 180U;
    uint16_t chunk = kOtaUpdateDefaultChunk;
    char role = 's';
    bool chunk_set = false;
    bool role_set = false;
    for (size_t i = 2U; i < tokens.size(); ++i) {
      const std::string tok = trim(tokens[i]);
      if (tok.empty()) {
        continue;
      }

      char parsed_role = 0;
      if (otaArchiveNormalizeRole(tok, parsed_role)) {
        if (role_set) {
          io_.writeln("[MASTER][OTA] duplicate role argument");
          return true;
        }
        role = parsed_role;
        role_set = true;
        continue;
      }

      char* endp = nullptr;
      const unsigned long parsed = std::strtoul(tok.c_str(), &endp, 10);
      if (endp != nullptr && *endp == '\0') {
        if (chunk_set) {
          io_.writeln("[MASTER][OTA] duplicate chunk argument");
          return true;
        }
        if (parsed < 32UL || parsed > 220UL) {
          io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
          return true;
        }
        chunk = static_cast<uint16_t>(parsed);
        chunk_set = true;
        continue;
      }

      io_.writeln("[MASTER][OTA] usage: ota.update.from.arc <id6hex> [chunk_bytes<=220] [master|slave]");
      return true;
    }

    if (ota_push_storage_ == nullptr) {
      io_.writeln("[MASTER][OTA] archive update unavailable (no local OTA storage backend bound)");
      return true;
    }
    std::string msg;
    if (!ota_push_storage_->begin(msg)) {
      writef("[MASTER][OTA] archive update failed: storage not ready (%s)", msg.c_str());
      return true;
    }

    std::vector<OtaArchiveEntryLocal> entries;
    if (!otaArchiveLoadManifest(*ota_push_storage_, role, entries, msg)) {
      writef("[MASTER][OTA] archive update failed: manifest load failed (%s)", msg.c_str());
      return true;
    }
    auto it = std::find_if(entries.begin(), entries.end(), [&](const OtaArchiveEntryLocal& e) {
      return e.id == id;
    });
    if (it == entries.end()) {
      writef("[MASTER][OTA] archive update failed: id not found (%s)", id.c_str());
      return true;
    }
    if (!it->target_role.empty()) {
      const char entry_role = (it->target_role == "slave") ? 's' : 'm';
      if (entry_role != role) {
        writef("[MASTER][OTA] archive update failed: role mismatch id=%s target=%s arg_role=%c",
               id.c_str(),
               it->target_role.c_str(),
               role);
        return true;
      }
    }

    const std::string bucket = otaArchiveBucketPath(role);
    const std::string stage_bin = std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedBinName;
    const std::string stage_meta = std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedMetaName;

    if (!ota_push_storage_->copySdToSpiffs(bucket + "/" + it->bin_name, stage_bin, msg)) {
      writef("[MASTER][OTA] archive update failed: restore bin failed (%s)", msg.c_str());
      return true;
    }
    if (!ota_push_storage_->copySdToSpiffs(bucket + "/" + it->meta_name, stage_meta, msg)) {
      writef("[MASTER][OTA] archive update failed: restore metadata failed (%s)", msg.c_str());
      return true;
    }
    writef("[MASTER][OTA] archive restore complete id=%s role=%c -> %s",
           id.c_str(),
           role,
           stage_bin.c_str());

    if (role == 'm') {
      std::string out_message;
      if (!otaUpdateMaster(stage_bin, &out_message)) {
        writef("[MASTER][OTA] archive update failed: %s",
               out_message.empty() ? "master update start failed" : out_message.c_str());
        return true;
      }
      writef("[MASTER][OTA] master update started from archive id=%s path=%s",
             id.c_str(),
             stage_bin.c_str());
      return true;
    }

    if (!hasRuntimePeer()) {
      io_.writeln("[MASTER][OTA] target not selected");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }

    std::string out_message;
    if (!otaUpdateRemote(stage_bin, chunk, &out_message)) {
      writef("[MASTER][OTA] archive update pipeline start failed: %s",
             out_message.empty() ? "failed" : out_message.c_str());
      return true;
    }

    writef("[MASTER][OTA] update pipeline started from archive id=%s path=%s chunk=%u",
           id.c_str(),
           stage_bin.c_str(),
           static_cast<unsigned int>(chunk));
    io_.writeln("[MASTER][OTA] management-owned steps: prepare -> push -> apply -> wait boot-complete");
    return true;
  }
  if (startsWith(cmd_line, "ota.update.master")) {
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.update.master [local_path]");
      return true;
    }
    std::string path_arg;
    if (tokens.size() == 2U) {
      path_arg = trim(tokens[1]);
      if (path_arg.empty()) {
        io_.writeln("[MASTER][OTA] usage: ota.update.master [local_path]");
        return true;
      }
    }
    std::string out_message;
    if (!otaUpdateMaster(path_arg, &out_message)) {
      writef("[MASTER][OTA] master update failed: %s",
             out_message.empty() ? "request failed" : out_message.c_str());
      return true;
    }
    writef("[MASTER][OTA] master update requested%s%s",
           out_message.empty() ? "" : ": ",
           out_message.empty() ? "" : out_message.c_str());
    return true;
  }
  if (startsWith(cmd_line, "ota.update ")) {
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][OTA] usage: ota.update <local_path> [chunk_bytes<=220]");
      return true;
    }
    constexpr uint16_t kOtaUpdateDefaultChunk = 180U;
    uint16_t chunk = kOtaUpdateDefaultChunk;
    if (tokens.size() == 3U) {
      const unsigned long parsed = std::strtoul(tokens[2].c_str(), nullptr, 10);
      if (parsed < 32UL || parsed > 220UL) {
        io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
        return true;
      }
      chunk = static_cast<uint16_t>(parsed);
    }
    const std::string local_arg = trim(tokens[1]);
    if (local_arg.empty()) {
      io_.writeln("[MASTER][OTA] usage: ota.update <local_path> [chunk_bytes<=220]");
      return true;
    }
    std::string path;
    const bool has_sep = (local_arg.find('/') != std::string::npos) || (local_arg.find('\\') != std::string::npos);
    if (!has_sep) {
      path = std::string(ota_paths::kStaging) + "/" + shortOtaName(local_arg);
    } else {
      path = resolveFsPath(local_storage_cwd_, local_arg);
    }

    if (!hasRuntimePeer()) {
      io_.writeln("[MASTER][OTA] target not selected");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }

    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    std::string out_message;
    if (!otaUpdateRemote(path, chunk, &out_message)) {
      writef("[MASTER][OTA] update pipeline start failed: %s",
             out_message.empty() ? "failed" : out_message.c_str());
      return true;
    }

    writef("[MASTER][OTA] update pipeline started path=%s chunk=%u",
           path.c_str(),
           static_cast<unsigned int>(chunk));
    io_.writeln("[MASTER][OTA] management-owned steps: prepare -> push -> apply -> wait boot-complete");
    return true;
  }
  if (startsWith(cmd_line, "ota.push ")) {
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][OTA] usage: ota.push <local_path> [chunk_bytes<=220]");
      return true;
    }
    uint16_t chunk = ota_push_chunk_bytes_;
    if (tokens.size() == 3U) {
      const unsigned long parsed = std::strtoul(tokens[2].c_str(), nullptr, 10);
      if (parsed < 32UL || parsed > 220UL) {
        io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
        return true;
      }
      chunk = static_cast<uint16_t>(parsed);
    }
    const std::string local_arg = trim(tokens[1]);
    if (local_arg.empty()) {
      io_.writeln("[MASTER][OTA] usage: ota.push <local_path> [chunk_bytes<=220]");
      return true;
    }
    std::string path;
    const bool has_sep = (local_arg.find('/') != std::string::npos) || (local_arg.find('\\') != std::string::npos);
    if (!has_sep) {
      path = std::string(ota_paths::kStaging) + "/" + shortOtaName(local_arg);
    } else {
      path = resolveFsPath(local_storage_cwd_, local_arg);
    }
    ota_update_pipeline_active_ = false;
    ota_update_prepare_pending_ = false;
    ota_update_prepare_corr_id_ = 0U;
    ota_update_wait_boot_notice_ = false;
    ota_update_image_name_.clear();
    ota_update_req_id_ = 0U;
    ota_update_has_target_peer_ = false;
    ota_update_target_peer_ = {};
    ota_update_staged_path_.clear();
    ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
    (void)otaPushStaged(path, chunk);
    return true;
  }

  if (cmd == "ota.info") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaStatusGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] status requested" : "[MASTER][OTA] status request failed");
    return true;
  }
  if (cmd == "ota.manifest") {
    (void)startPagedFetch(PagedFetchKind::OtaManifest, 8, "[MASTER][OTA] manifest paged fetch queued");
    return true;
  }
  if (cmd == "ota.manifest.rebuild") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaManifestRebuild));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] manifest rebuild requested" : "[MASTER][OTA] manifest rebuild request failed");
    return true;
  }
  if (cmd == "ota.capacity") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaCapacityGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] capacity requested" : "[MASTER][OTA] capacity request failed");
    return true;
  }
  if (cmd == "ota.gate") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaGateGet));
    correlation_id_ = mgmt.nextReqId();
    io_.writeln(ok ? "[MASTER][OTA] gate status requested" : "[MASTER][OTA] gate request failed");
    return true;
  }
  if (startsWith(cmd_line, "ota.clear ")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.clear <in|img|man|all>");
      return true;
    }
    std::string scope = lowerCopy(trim(tokens[1]));
    if (scope != "in" && scope != "img" && scope != "man" && scope != "all") {
      io_.writeln("[MASTER][OTA] invalid clear scope (use in|img|man|all)");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaClearScope),
                                           management_utils::buildStringPayloadU16(scope));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][OTA] clear requested scope=%s", scope.c_str());
    } else {
      io_.writeln("[MASTER][OTA] clear request failed");
    }
    return true;
  }
  if (startsWith(cmd_line, "ota.apply ")) {
    if (tokens.size() < 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.apply <image_id|image_name>");
      return true;
    }
    const std::string target = trim(line.substr(line.find(' ') + 1));
    if (target.empty()) {
      io_.writeln("[MASTER][OTA] usage: ota.apply <image_id|image_name>");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaApply),
                                           management_utils::buildStringPayloadU16(target));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][OTA] apply requested target=%s", target.c_str());
    } else {
      io_.writeln("[MASTER][OTA] apply request failed");
    }
    return true;
  }
  if (startsWith(cmd, "ota.rollback")) {
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][OTA] usage: ota.rollback <master|slave>");
      return true;
    }
    const std::string target = lowerCopy(trim(tokens[1]));
    if (target == "slave") {
      if (management_transport_ == nullptr) {
        io_.writeln("[MASTER][CLI] management path unavailable");
        captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
        return true;
      }
      ManagementController mgmt(*management_transport_);
      mgmt.setNextReqId(correlation_id_);
      const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::OtaRollback));
      correlation_id_ = mgmt.nextReqId();
      if (ok) {
        io_.writeln("[MASTER][OTA] slave rollback requested");
      } else {
        io_.writeln("[MASTER][OTA] slave rollback request failed");
      }
      return true;
    }
    if (target == "master") {
      if (actions_ == nullptr) {
        io_.writeln("[MASTER][OTA] master rollback unavailable (no actions hook)");
        captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
        return true;
      }
      std::string msg;
      const bool ok = actions_->requestMasterRollback(&msg);
      if (ok) {
        writef("[MASTER][OTA] master rollback requested%s%s",
               msg.empty() ? "" : ": ",
               msg.empty() ? "" : msg.c_str());
      } else {
        writef("[MASTER][OTA] master rollback failed%s%s",
               msg.empty() ? "" : ": ",
               msg.empty() ? "" : msg.c_str());
      }
      captureDispatchSnapshot_(ok,
                               0U,
                               0U,
                               ok ? ManagementStatus::Ok : ManagementStatus::InternalError,
                               ok ? "" : "execution");
      return true;
    }
    io_.writeln("[MASTER][OTA] usage: ota.rollback <master|slave>");
    return true;
  }

  io_.writeln("[MASTER][OTA] unknown ota command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

bool MasterCli::handleLoggerCommands(const std::string& line, const std::string& lower) {
  (void)line;
  if (!startsWith(lower, "logger.") &&
      !startsWith(lower, "channel.") &&
      !startsWith(lower, "chain.loop.")) {
    return false;
  }

  std::string cmd = lower;

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }

  auto runMgmt = [&](ManagementCommandId cmd,
                     const std::vector<uint8_t>& payload,
                     ManagementResponse& out_resp,
                     bool& out_has_response) -> bool {
    out_has_response = false;
    ManagementController mgmt_controller(*management_transport_);
    mgmt_controller.setNextReqId(correlation_id_);

    const uint16_t cmd_id = static_cast<uint16_t>(cmd);
    const bool requires_runtime_target =
        (cmd == ManagementCommandId::LogRemoteStatusGet ||
         cmd == ManagementCommandId::LogRemoteRead ||
         cmd == ManagementCommandId::LogRemoteClear ||
         cmd == ManagementCommandId::LogRemoteControlSet);

    uint32_t req_id = 0U;
    bool accepted = false;
    if (requires_runtime_target) {
      if (!hasRuntimePeer()) {
        io_.writeln("[MASTER][LOGGER] target not selected");
        captureDispatchSnapshot_(false, cmd_id, 0U, ManagementStatus::BadPayload, "target");
        return true;
      }
      accepted = submitRuntimeTargeted_(mgmt_controller, cmd_id, payload, &req_id);
    } else {
      accepted = submitRuntimeTargeted_(mgmt_controller,
                                        cmd_id,
                                        payload,
                                        &req_id,
                                        0U,
                                        false);
    }

    if (!accepted) {
      io_.writeln("[MASTER][LOGGER] submit rejected");
      return true;
    }
    correlation_id_ = mgmt_controller.nextReqId();

    const uint32_t now_ms = nowMs();
    if (shouldTickManagementRuntimeFromCli_()) {
      management_runtime_->tick(now_ms);
    }

    ManagementResponse resp{};
    while (management_transport_->pollResponse(resp)) {
      if (resp.source == ManagementSource::Cli &&
          resp.cmd_id == cmd_id &&
          resp.req_id == req_id) {
        out_resp = std::move(resp);
        out_has_response = true;
        break;
      }
    }
    if (!out_has_response) {
      io_.writeln("[MASTER][LOGGER] management response pending");
    }
    return true;
  };

  if (cmd == "logger.remote.status") {
    if (remote_log_pull_active_) {
      io_.writeln("[MASTER][LOGGER][REMOTE] pull is active; stop it first or wait for completion");
      return true;
    }
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteStatusGet, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote status requested");
        } else {
          writef("[MASTER][LOGGER] remote status request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (cmd == "logger.remote.enable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteControlSet,
                management_utils::buildLogControlPayload(true),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote enable requested");
        } else {
          writef("[MASTER][LOGGER] remote enable request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (cmd == "logger.remote.disable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteControlSet,
                management_utils::buildLogControlPayload(false),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote disable requested");
        } else {
          writef("[MASTER][LOGGER] remote disable request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (cmd == "logger.remote.clear") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteClear, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          io_.writeln("[MASTER][LOGGER] remote clear requested");
        } else {
          writef("[MASTER][LOGGER] remote clear request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }
  if (startsWith(cmd, "logger.remote.read ")) {
    if (remote_log_pull_active_) {
      io_.writeln("[MASTER][LOGGER][REMOTE] pull is active; stop it first or wait for completion");
      return true;
    }
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][LOGGER] usage: logger.remote.read <offset> [max_bytes<=128]");
      return true;
    }
    uint32_t offset = 0;
    uint32_t max_bytes = 96;
    if (!parseU32Token(tokens[1], offset)) {
      io_.writeln("[MASTER][LOGGER] invalid offset");
      return true;
    }
    if (tokens.size() >= 3U) {
      if (!parseU32Token(tokens[2], max_bytes) || max_bytes == 0U) {
        io_.writeln("[MASTER][LOGGER] invalid max_bytes");
        return true;
      }
    }
    if (max_bytes > 128U) {
      max_bytes = 128U;
    }

    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogRemoteRead,
                management_utils::buildLogReadPayload(offset, static_cast<uint16_t>(max_bytes)),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok || resp.status == ManagementStatus::OkDeferred) {
          writef("[MASTER][LOGGER] remote read requested offset=%lu max_bytes=%lu",
                 static_cast<unsigned long>(offset),
                 static_cast<unsigned long>(max_bytes));
        } else {
          writef("[MASTER][LOGGER] remote read request failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (startsWith(cmd, "logger.remote.pull")) {
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() > 2U) {
      io_.writeln("[MASTER][LOGGER][REMOTE] usage: logger.remote.pull [chunk_bytes<=128]");
      return true;
    }
    uint32_t chunk_size = 128U;
    if (tokens.size() == 2U) {
      if (!parseU32Token(tokens[1], chunk_size) || chunk_size == 0U || chunk_size > 128U) {
        io_.writeln("[MASTER][LOGGER][REMOTE] invalid chunk_bytes (1..128)");
        return true;
      }
    }
    const bool started = startRemoteLogPull(static_cast<uint16_t>(chunk_size));
    if (!dispatch_snapshot_.seen) {
      captureDispatchSnapshot_(started,
                               0U,
                               0U,
                               started ? ManagementStatus::Ok : ManagementStatus::BadPayload,
                               started ? "" : "validation");
    }
    return true;
  }

  if (cmd == "logger.remote.stop") {
    if (!remote_log_pull_active_) {
      io_.writeln("[MASTER][LOGGER][REMOTE] no active pull");
      // Treat stop-without-active as idempotent no-op for automation/test flows.
      captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
      return true;
    }
    stopRemoteLogPull("stopped by user", false);
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (cmd == "channel.runtime.status") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChannelRuntimeGet, {}, resp, has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status != ManagementStatus::Ok) {
        writef("[MASTER][CHANNEL] runtime status failed: %s",
               management_utils::managementStatusToString(resp.status));
        return true;
      }
      ManagementRuntimeChannelStatusPayload status{};
      if (!management_utils::parseRuntimeChannelStatusPayload(resp.payload, status)) {
        io_.writeln("[MASTER][CHANNEL] runtime status parse failed");
        return true;
      }
      writef("[MASTER][CHANNEL] current=%u peers=%u",
             static_cast<unsigned int>(status.current_channel),
             static_cast<unsigned int>(status.entries.size()));
      for (const auto& e : status.entries) {
        writef("  peer=%s channel=%u key=%s",
               macToPrintable(e.peer).c_str(),
               static_cast<unsigned int>(e.channel),
               e.key.c_str());
      }
      return true;
    }
    return true;
  }

  if (startsWith(cmd, "channel.sync ")) {
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() != 2U) {
      io_.writeln("[MASTER][CHANNEL] usage: channel.sync <1..14>");
      return true;
    }
    uint32_t channel_u32 = 0U;
    if (!parseU32Token(tokens[1], channel_u32) || channel_u32 < 1U || channel_u32 > 14U) {
      io_.writeln("[MASTER][CHANNEL] invalid channel (1..14)");
      return true;
    }
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChannelSyncAll,
                management_utils::buildChannelSyncAllPayload(static_cast<uint8_t>(channel_u32)),
                resp,
                has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status == ManagementStatus::OkDeferred || resp.status == ManagementStatus::Ok) {
        writef("[MASTER][CHANNEL] sync started target=%u",
               static_cast<unsigned int>(channel_u32));
      } else {
        writef("[MASTER][CHANNEL] sync rejected: %s",
               management_utils::managementStatusToString(resp.status));
      }
      return true;
    }
    return true;
  }

  if (cmd == "chain.loop.status") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChainLoopControlSet, {}, resp, has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status != ManagementStatus::Ok) {
        writef("[MASTER][CHAIN] status failed: %s",
               management_utils::managementStatusToString(resp.status));
        return true;
      }
      bool enabled = false;
      bool has_value = false;
      if (!management_utils::parseChainLoopControlPayload(resp.payload, has_value, enabled) || !has_value) {
        io_.writeln("[MASTER][CHAIN] status parse failed");
        return true;
      }
      writef("[MASTER][CHAIN] loop_auto=%s", enabled ? "on" : "off");
      return true;
    }
    return true;
  }

  if (cmd == "chain.loop.on" || cmd == "chain.loop.off") {
    const bool enabled = (cmd == "chain.loop.on");
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::ChainLoopControlSet,
                management_utils::buildChainLoopControlPayload(enabled),
                resp,
                has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status == ManagementStatus::OkDeferred || resp.status == ManagementStatus::Ok) {
        writef("[MASTER][CHAIN] loop_auto apply started target=%s", enabled ? "on" : "off");
      } else {
        writef("[MASTER][CHAIN] loop_auto apply rejected: %s",
               management_utils::managementStatusToString(resp.status));
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.status") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalStatusGet, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status != ManagementStatus::Ok) {
          writef("[MASTER][LOGGER] status failed: %s",
                 management_utils::managementStatusToString(resp.status));
          return true;
        }
        bool available = false;
        bool enabled = false;
        uint8_t min_level = 0;
        LogStorageStats stats{};
        if (!management_utils::parseLogStatusPayload(resp.payload, available, enabled, min_level, stats)) {
          io_.writeln("[MASTER][LOGGER] status parse failed");
          return true;
        }
        writef("[MASTER][LOGGER] enabled=%s min_level=%u store=%s used=%lu dropped=%lu records=%lu rotations=%lu",
               enabled ? "yes" : "no",
               static_cast<unsigned int>(min_level),
               available ? "ready" : "unavailable",
               static_cast<unsigned long>(stats.bytes_used),
               static_cast<unsigned long>(stats.bytes_dropped),
               static_cast<unsigned long>(stats.records_appended),
               static_cast<unsigned long>(stats.rotations));
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.enable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalControlSet,
                management_utils::buildLogControlPayload(true),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok) {
          io_.writeln("[MASTER][LOGGER] enabled");
        } else {
          writef("[MASTER][LOGGER] enable failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.disable") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalControlSet,
                management_utils::buildLogControlPayload(false),
                resp,
                has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok) {
          io_.writeln("[MASTER][LOGGER] disabled");
        } else {
          writef("[MASTER][LOGGER] disable failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (cmd == "logger.clear") {
    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalClear, {}, resp, has_response)) {
      if (has_response) {
        if (resp.status == ManagementStatus::Ok) {
          io_.writeln("[MASTER][LOGGER] cleared");
        } else {
          writef("[MASTER][LOGGER] clear failed: %s",
                 management_utils::managementStatusToString(resp.status));
        }
      }
      return true;
    }
    return true;
  }

  if (startsWith(cmd, "logger.read ")) {
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() < 2U || tokens.size() > 3U) {
      io_.writeln("[MASTER][LOGGER] usage: logger.read <offset> [max_bytes]");
      return true;
    }
    uint32_t offset = 0;
    uint32_t max_bytes = 128;
    if (!parseU32Token(tokens[1], offset)) {
      io_.writeln("[MASTER][LOGGER] invalid offset");
      return true;
    }
    if (tokens.size() >= 3U) {
      if (!parseU32Token(tokens[2], max_bytes) || max_bytes == 0U) {
        io_.writeln("[MASTER][LOGGER] invalid max_bytes");
        return true;
      }
    }
    if (max_bytes > 512U) {
      max_bytes = 512U;
    }

    ManagementResponse resp{};
    bool has_response = false;
    if (runMgmt(ManagementCommandId::LogLocalRead,
                management_utils::buildLogReadPayload(offset, static_cast<uint16_t>(max_bytes)),
                resp,
                has_response)) {
      if (!has_response) {
        return true;
      }
      if (resp.status != ManagementStatus::Ok) {
        writef("[MASTER][LOGGER] read failed: %s",
               management_utils::managementStatusToString(resp.status));
        return true;
      }
      uint32_t offset_resp = 0;
      uint32_t total_size = 0;
      std::vector<uint8_t> chunk{};
      if (!management_utils::parseLogReadResponsePayload(resp.payload, offset_resp, total_size, chunk)) {
        io_.writeln("[MASTER][LOGGER] read parse failed");
        return true;
      }

      writef("[MASTER][LOGGER] chunk offset=%lu total=%lu bytes=%u",
             static_cast<unsigned long>(offset_resp),
             static_cast<unsigned long>(total_size),
             static_cast<unsigned int>(chunk.size()));
      if (chunk.empty()) {
        io_.writeln("  (empty)");
        return true;
      }
      for (size_t i = 0; i < chunk.size(); i += 16U) {
        char linebuf[96] = {0};
        int p = std::snprintf(linebuf, sizeof(linebuf), "  %08lX: ",
                              static_cast<unsigned long>(offset_resp + static_cast<uint32_t>(i)));
        const size_t end = std::min<size_t>(i + 16U, chunk.size());
        for (size_t j = i; j < end && p > 0 && static_cast<size_t>(p) < sizeof(linebuf); ++j) {
          p += std::snprintf(linebuf + p, sizeof(linebuf) - static_cast<size_t>(p), "%02X ", chunk[j]);
        }
        io_.writeln(std::string(linebuf));
      }
      return true;
    }
    return true;
  }

  io_.writeln("[MASTER][LOGGER] unknown logger command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

}  // namespace espnow_link
