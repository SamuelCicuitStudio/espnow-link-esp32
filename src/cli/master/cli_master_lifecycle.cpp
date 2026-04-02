/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: MasterCli lifecycle wiring, policy helpers, and help output flow.
 **************************************************************/
#include "../internal/cli_master_internal.hpp"

namespace espnow_link {

using namespace cli_helpers;

namespace {

std::string otaSidecarJsonPath(const std::string& bin_path) {
  const size_t slash = bin_path.find_last_of('/');
  const size_t dot = bin_path.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    return bin_path.substr(0U, dot) + ".json";
  }
  return bin_path + ".json";
}

std::string otaFileNameFromPath(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::string p = path;
  for (char& c : p) {
    if (c == '\\') {
      c = '/';
    }
  }
  const size_t sep = p.find_last_of('/');
  if (sep == std::string::npos) {
    return p;
  }
  if (sep + 1U >= p.size()) {
    return {};
  }
  return p.substr(sep + 1U);
}

std::string otaImageNameFromCorr(uint32_t corr_id) {
  (void)corr_id;
  return std::string("u.bin");
}

std::string resolveStagedPathInput(const std::string& staged_name, const std::string& cwd) {
  std::string path;
  const bool has_sep =
      (staged_name.find('/') != std::string::npos) || (staged_name.find('\\') != std::string::npos);
  if (has_sep) {
    path = staged_name;
    for (char& c : path) {
      if (c == '\\') {
        c = '/';
      }
    }
    if (!path.empty() && path.front() != '/') {
      std::string normalized_cwd = cwd;
      for (char& c : normalized_cwd) {
        if (c == '\\') {
          c = '/';
        }
      }
      if (normalized_cwd.empty() || normalized_cwd.back() != '/') {
        normalized_cwd.push_back('/');
      }
      path = normalized_cwd + path;
    }
    return path;
  }
  if (staged_name.empty()) {
    return std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedBinName;
  }
  return std::string(ota_paths::kStaging) + "/" + MasterCli::shortOtaName(staged_name);
}

const char* otaStatusCodeName(uint16_t code) {
  switch (static_cast<OtaStatusCode>(code)) {
    case OtaStatusCode::Ok:
      return "ok";
    case OtaStatusCode::StorageNotReady:
      return "storage_not_ready";
    case OtaStatusCode::GateDenied:
      return "gate_denied";
    case OtaStatusCode::GateBusy:
      return "gate_busy";
    case OtaStatusCode::GatePrepFailed:
      return "gate_prep_failed";
    case OtaStatusCode::ImageTooLarge:
      return "image_too_large";
    case OtaStatusCode::InvalidState:
      return "invalid_state";
    case OtaStatusCode::InvalidArgument:
      return "invalid_argument";
    case OtaStatusCode::OffsetMismatch:
      return "offset_mismatch";
    case OtaStatusCode::SizeMismatch:
      return "size_mismatch";
    case OtaStatusCode::CrcMismatch:
      return "crc_mismatch";
    case OtaStatusCode::ApplyRejected:
      return "apply_rejected";
    case OtaStatusCode::ApplyFailed:
      return "apply_failed";
    case OtaStatusCode::Timeout:
      return "timeout";
    case OtaStatusCode::InternalError:
      return "internal_error";
    default:
      return "?";
  }
}

bool readTextFile(IOtaStorageBackend& storage,
                  const std::string& path,
                  std::string& out_text,
                  std::string& out_error) {
  out_text.clear();
  OtaStorageStat st{};
  std::string msg;
  if (!storage.stat(path, st, msg)) {
    out_error = msg.empty() ? "stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    out_error = "file missing";
    return false;
  }
  if (st.size_bytes > 4096U) {
    out_error = "file too large";
    return false;
  }

  std::vector<uint8_t> buf(st.size_bytes, 0U);
  size_t out_len = 0U;
  if (!storage.readAt(path, 0U, buf.data(), buf.size(), out_len, msg)) {
    out_error = msg.empty() ? "read failed" : msg;
    return false;
  }
  if (out_len == 0U) {
    out_error = "empty file";
    return false;
  }

  out_text.assign(reinterpret_cast<const char*>(buf.data()),
                  reinterpret_cast<const char*>(buf.data() + out_len));
  out_error.clear();
  return true;
}

bool extractJsonStringField(const std::string& json,
                            const char* key,
                            std::string& out_value) {
  out_value.clear();
  if (key == nullptr || key[0] == '\0') {
    return false;
  }
  const std::string pattern = std::string("\"") + key + "\"";
  const size_t key_pos = json.find(pattern);
  if (key_pos == std::string::npos) {
    return false;
  }
  size_t pos = json.find(':', key_pos + pattern.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos >= json.size()) {
    return false;
  }
  if (json[pos] == '"') {
    ++pos;
    std::string value;
    value.reserve(32U);
    bool escaped = false;
    while (pos < json.size()) {
      const char c = json[pos++];
      if (escaped) {
        value.push_back(c);
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == '"') {
        out_value = trim(value);
        return !out_value.empty();
      }
      value.push_back(c);
    }
    return false;
  }

  size_t end = pos;
  while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n' && json[end] != '\r') {
    ++end;
  }
  out_value = trim(json.substr(pos, end - pos));
  return !out_value.empty();
}

bool loadFirmwareMetadataFromSidecar(IOtaStorageBackend& storage,
                                     const std::string& bin_path,
                                     FirmwareImageMetadata& out_meta,
                                     std::string& out_sidecar_path,
                                     std::string& out_error) {
  out_meta = FirmwareImageMetadata{};
  out_sidecar_path = otaSidecarJsonPath(bin_path);

  std::string json;
  if (!readTextFile(storage, out_sidecar_path, json, out_error)) {
    return false;
  }

  std::string version;
  (void)extractJsonStringField(json, "sw_version", version);
  if (version.empty()) {
    out_error = "missing sw_version";
    return false;
  }

  std::string build;
  (void)extractJsonStringField(json, "build_id", build);
  if (build.empty()) {
    out_error = "missing build_id";
    return false;
  }

  std::string target_role;
  (void)extractJsonStringField(json, "target_role", target_role);
  target_role = lowerCopy(trim(target_role));
  if (target_role != "master" && target_role != "slave") {
    out_error = "missing/invalid target_role (master|slave)";
    return false;
  }

  if (version.size() > 63U || build.size() > 63U || target_role.size() > 15U) {
    out_error = "metadata field too long";
    return false;
  }

  out_meta.sw_version = version;
  out_meta.build_id = build;
  out_meta.target_role = target_role;
  out_error.clear();
  return true;
}

bool extractProfileIdFromCapabilities(const DescriptorResponse& d, ProfileId& out_profile_id) {
  out_profile_id = kProfileUnknown;
  if (d.type != DescriptorResponseType::Capabilities) {
    return false;
  }
  for (const auto& cap : d.capabilities) {
    if (cap.key != "profile_id") {
      continue;
    }
    const unsigned long parsed = std::strtoul(cap.description.c_str(), nullptr, 10);
    if (parsed > 0U && parsed <= 0xFFFFUL) {
      out_profile_id = static_cast<ProfileId>(parsed);
      return true;
    }
  }
  return false;
}

ProfileId profileIdFromRoleCode(uint8_t role_code) {
  if (role_code == static_cast<uint8_t>(kProfilePms & 0xFFU)) {
    return kProfilePms;
  }
  if (role_code == static_cast<uint8_t>(kProfileRelay & 0xFFU)) {
    return kProfileRelay;
  }
  if (role_code == static_cast<uint8_t>(kProfileSens & 0xFFU)) {
    return kProfileSens;
  }
  if (role_code == static_cast<uint8_t>(kProfileSemu & 0xFFU)) {
    return kProfileSemu;
  }
  if (role_code == static_cast<uint8_t>(kProfileRemu & 0xFFU)) {
    return kProfileRemu;
  }
  if (role_code == static_cast<uint8_t>(kProfileLockAlarm & 0xFFU)) {
    return kProfileLockAlarm;
  }
  return kProfileUnknown;
}

}  // namespace


MasterCli::MasterCli(EspNowManager& manager,
                     MasterPullClient& pull,
                     IMasterCliIo& io,
                     IMasterCliActions* actions,
                     IPersistenceBackend* persistence,
                     const std::string& enable_key,
                     LibraryLogger* logger,
                     ManagementService* management,
                     EspLogStore* remote_log_store,
                     IStorageExplorerProvider* local_storage,
                     IOtaStorageBackend* ota_push_storage,
                     ManagementQueueTransport* management_transport,
                     ManagementRuntime* management_runtime,
                     CliTrafficPolicy traffic_policy)
    : manager_(manager),
      pull_(pull),
      io_(io),
      actions_(actions),
      persistence_(persistence),
      enable_key_((enable_key.empty() || enable_key.size() > 7U) ? "clienbl" : enable_key),
      logger_(logger),
      management_(management),
      management_transport_(management_transport),
      management_runtime_(management_runtime),
      remote_log_store_(remote_log_store),
      local_storage_(local_storage),
      ota_push_storage_(ota_push_storage),
      traffic_policy_(traffic_policy) {
  loadCliEnabled();
}

MasterCli::CliTrafficPolicy MasterCli::effectiveTrafficPolicy() const {
  if (traffic_policy_ != CliTrafficPolicy::Auto) {
    return traffic_policy_;
  }
  return (management_transport_ != nullptr) ? CliTrafficPolicy::ManagementOnly
                                            : CliTrafficPolicy::LegacyObserver;
}

bool MasterCli::usesManagementOnlyTraffic_() const {
  return effectiveTrafficPolicy() == CliTrafficPolicy::ManagementOnly;
}

bool MasterCli::shouldTickManagementRuntimeFromCli_() const {
  return (management_runtime_ != nullptr) && !usesManagementOnlyTraffic_();
}

void MasterCli::noteCliOwnedReqId_(uint32_t req_id) const {
  if (req_id == 0U) {
    return;
  }
  for (const uint32_t existing : cli_owned_req_ids_) {
    if (existing == req_id) {
      return;
    }
  }
  cli_owned_req_ids_.push_back(req_id);
  while (cli_owned_req_ids_.size() > cli_owned_req_ids_max_) {
    cli_owned_req_ids_.pop_front();
  }
}

bool MasterCli::isCliOwnedReqId_(uint32_t req_id) const {
  if (req_id == 0U) {
    return false;
  }
  for (const uint32_t owned : cli_owned_req_ids_) {
    if (owned == req_id) {
      return true;
    }
  }
  return false;
}

bool MasterCli::shouldProcessObserverPullResponse_(uint32_t corr_id) const {
  if (!usesManagementOnlyTraffic_()) {
    return true;
  }
  return isCliOwnedReqId_(corr_id);
}

bool MasterCli::shouldProcessManagementMailboxResponse_(const ManagementResponse& resp) const {
  if (!usesManagementOnlyTraffic_()) {
    return true;
  }
  if (resp.source == ManagementSource::Cli) {
    return true;
  }
  if (resp.source == ManagementSource::Unknown) {
    if (isCliOwnedReqId_(resp.req_id)) {
      return true;
    }
    if (live_monitor_status_pending_ && resp.req_id == live_monitor_status_req_id_) {
      return true;
    }
  }
  return false;
}

bool MasterCli::shouldProcessManagementMailboxEvent_(const ManagementEvent& evt) const {
  if (!usesManagementOnlyTraffic_()) {
    return true;
  }
  if (evt.source == ManagementSource::Cli) {
    return true;
  }
  if (evt.source != ManagementSource::Unknown) {
    return false;
  }
  if (isCliOwnedReqId_(evt.req_id)) {
    return true;
  }
  if (ota_push_active_ || ota_update_pipeline_active_) {
    if (evt.event_id == ManagementEventId::OtaTransferReady ||
        evt.event_id == ManagementEventId::OtaTransferStatus ||
        evt.event_id == ManagementEventId::OtaBootComplete) {
      return true;
    }
  }
  return false;
}

std::string MasterCli::shortOtaName(const std::string& input_name) {
  std::string name = input_name;
  for (char& c : name) {
    if (c == '\\') {
      c = '/';
    }
  }

  const size_t sep = name.find_last_of('/');
  if (sep != std::string::npos) {
    name = name.substr(sep + 1U);
  }
  if (name.empty()) {
    return "f.bin";
  }

  std::string ext;
  std::string base = name;
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0U && dot + 1U < name.size()) {
    base = name.substr(0U, dot);
    ext = name.substr(dot + 1U);
  }

  auto sanitize = [](const std::string& in, bool allow_dot) -> std::string {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
      const unsigned char uc = static_cast<unsigned char>(c);
      if (std::isalnum(uc) != 0) {
        out.push_back(static_cast<char>(std::tolower(uc)));
        continue;
      }
      if (c == '_' || c == '-') {
        out.push_back(c);
        continue;
      }
      if (allow_dot && c == '.') {
        out.push_back(c);
      }
    }
    return out;
  };

  base = sanitize(base, false);
  ext = sanitize(ext, false);
  if (base.empty()) {
    base = "f";
  }
  if (ext.empty()) {
    ext = "bin";
  }
  if (ext.size() > 3U) {
    ext = ext.substr(0U, 3U);
  }

  if (base.size() > 8U) {
    uint32_t h = 2166136261U;
    for (char c : base) {
      h ^= static_cast<uint8_t>(c);
      h *= 16777619U;
    }
    char hbuf[5] = {0};
    std::snprintf(hbuf, sizeof(hbuf), "%03X", static_cast<unsigned int>(h & 0xFFFU));
    base = base.substr(0U, 5U) + hbuf;
  }

  std::string out = base;
  if (!ext.empty()) {
    out.push_back('.');
    out += ext;
  }
  return out;
}

void MasterCli::writef(const char* fmt, ...) const {
  char buf[320] = {0};
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  io_.writeln(std::string(buf));
}

void MasterCli::printHelp() {
  io_.writeln("[MASTER][CLI] ESP-NOW Link Command Reference");
  io_.writeln("  tip: set sticky target once with 'active <paired_index|mac>'");
  io_.writeln("  tip: .remote. commands target slave; others are local/master-side control");
  io_.writeln("  tip: peer target syntax: active <index|mac> OR <index|mac> <command>");
  io_.writeln("  tip: prefix target overrides active target for one command");
  io_.writeln("  quick help: help <topic>  or  <topic> help");
  io_.writeln("  topics: core paired pairing target topology desc settings push time control test log logger sd ota");
  io_.writeln("");

  io_.writeln("[CORE]");
  io_.writeln("  help                              Show this reference");
  io_.writeln("  list                              List discovered peers");
  io_.writeln("  paired                            List persisted paired peers (deterministic order)");
  io_.writeln("  status                            Show paired/runtime-target/discovery/queue status");
  io_.writeln("  active                            Show sticky active target");
  io_.writeln("  active <index|MAC>                Set sticky active target");
  io_.writeln("  active clear                      Clear sticky active target");
  io_.writeln("  live enable                       Enable automatic paired-peer liveness monitor");
  io_.writeln("  live disable                      Disable automatic paired-peer liveness monitor");
  io_.writeln("  live status                       Show liveness monitor runtime status");
  io_.writeln("  cli status                        Show CLI enabled/disabled state");
  io_.writeln("  cli on                            Enable CLI and persist");
  io_.writeln("  cli off                           Disable CLI and persist");
  io_.writeln("  cli.baud                          Show persisted CLI serial baud (ICM)");
  io_.writeln("  cli.baud set <baud>               Persist baud and restart master (ICM)");
  io_.writeln("  note: slave baud = target peer, set cli_baud=<baud>, then restart slave");
  io_.writeln("");

  io_.writeln("[PAIRING]");
  io_.writeln("  pair <index|MAC>                  Pair target (idempotent if already paired)");
  io_.writeln("  unpair                            Graceful unpair with selected target peer");
  io_.writeln("  remove [index|MAC|slave]          Remove one paired peer (index: discovered first, then persisted)");
  io_.writeln("  <index> <peer-command>            Route peer-bound command to paired list index");
  io_.writeln("  <MAC> <peer-command>              Route peer-bound command to one paired MAC");
  io_.writeln("  note: hard paired capacity is 14; no auto-eviction");
  io_.writeln("  note: at 14, pair and discovery start are rejected until one peer is removed");
  io_.writeln("");

  io_.writeln("[TARGETING]");
  io_.writeln("  active <index|MAC>                Sticky target for subsequent peer-bound commands");
  io_.writeln("  active clear                      Clear sticky target");
  io_.writeln("  rule: all peer-bound commands support both active target and prefix override");
  io_.writeln("  syntax: <index> <command>  or  <MAC> <command> (one-shot override)");
  io_.writeln("");

  io_.writeln("[TOPOLOGY]");
  io_.writeln("  topology.status                   Topology runtime status (targeted or local)");
  io_.writeln("  topology.slots [committed|staged] Topology slot dump (targeted or local)");
  io_.writeln("  topology.trigger <idx> <dir> [d] [h] [vid]  Send index-based topology trigger");
  io_.writeln("  topology.stage.hex <hex_payload>  Stage full topology payload from hex stream");
  io_.writeln("  topology.stage.file <path>        Stage full topology payload from local file");
  io_.writeln("  topology.commit                   Commit staged topology");
  io_.writeln("  topology.apply.hex <hex_payload>  Stage then commit from hex payload");
  io_.writeln("  topology.apply.file <path>        Stage then commit from local file");
  io_.writeln("  topology.plan.file <path>         Dry topology processing report (no RF send)");
  io_.writeln("  topology.deploy.file <path>       Fan-out stage+commit to all persisted paired peers");
  io_.writeln("  topology.edit.new [topo_ver] [seed_csv]  Start/reset CLI chain editor");
  io_.writeln("  topology.edit.add <S|R|SM|RM> <paired_index|MAC> [vi]  Append one chain node");
  io_.writeln("  topology.edit.del <chain_pos>     Delete one node at zero-based position");
  io_.writeln("  topology.edit.clear               Clear in-memory chain nodes");
  io_.writeln("  topology.edit.show                Show in-memory chain and seeds");
  io_.writeln("  topology.edit.validate            Validate editor chain with parser rules");
  io_.writeln("  topology.edit.save [path]         Save JSON chain file (default /o/s/topology_chain.json)");
  io_.writeln("  topology.edit.load [path]         Load JSON chain file into editor");
  io_.writeln("  topology.file.show [path]         Print raw topology JSON file");
  io_.writeln("  topology.chain.help               Show fixed-file chain command list");
  io_.writeln("  topology.chain.set.help           Show one-line chain_spec syntax");
  io_.writeln("  topology.chain.show               Show fixed /o/s/tp.json chain");
  io_.writeln("  topology.chain.graph              Show fixed /o/s/tp.json chain graph");
  io_.writeln("  topology.chain.clear              Clear fixed /o/s/tp.json chain");
  io_.writeln("  topology.chain.add <S|R|SM|RM> <paired_index|MAC> [vi]");
  io_.writeln("  topology.chain.edit <index> <S|R|SM|RM> <paired_index|MAC> [vi]");
  io_.writeln("  topology.chain.del <index>        Delete one chain node from fixed file");
  io_.writeln("  topology.chain.move <from> <to>   Move one chain node in fixed file");
  io_.writeln("  topology.chain.validate           Validate fixed /o/s/tp.json");
  io_.writeln("  topology.chain.fix                Auto-fix fixed /o/s/tp.json");
  io_.writeln("  topology.chain.apply              Apply fixed /o/s/tp.json");
  io_.writeln("  topology.chain.verify [timeout_ms] Verify each target committed fixed /o/s/tp.json");
  io_.writeln("  topology.chain.backup             Backup fixed /o/s/tp.json");
  io_.writeln("  topology.chain.restore            Restore fixed /o/s/tp.json backup");
  io_.writeln("  topology.chain.set <chain_spec>   Replace full chain in one command");
  io_.writeln("  topology.local.status             Local-only status (force no target)");
  io_.writeln("  topology.local.slots [state]      Local-only slot dump");
  io_.writeln("  topology.local.stage.hex <hex>    Local-only stage");
  io_.writeln("  topology.local.stage.file <path>  Local-only stage");
  io_.writeln("  topology.local.commit             Local-only commit");
  io_.writeln("  chain.loop.status                 Show persisted chain auto-loop state");
  io_.writeln("  chain.loop.on                     Enable LoopAuto for all chain slaves");
  io_.writeln("  chain.loop.off                    Disable LoopAuto for all chain slaves");
  io_.writeln("  note: stage payload must be full binary stream format (no partial diff)");
  io_.writeln("  rules: chain starts/ends with S or SM; no adjacent S/SM; R and RM adjacency allowed");
  io_.writeln("");

  io_.writeln("[DESCRIPTOR / PROFILE]");
  io_.writeln("  desc                              Show descriptor (type/id/name/hw/sw/build)");
  io_.writeln("  caps                              Show capabilities schema");
  io_.writeln("  telem                             Show telemetry schema");
  io_.writeln("  telem.now                         Pull live telemetry snapshot");
  io_.writeln("  telem.now.child <vid>             Pull SEMU/REMU child telemetry (+global metrics)");
  io_.writeln("  live                              One-shot liveness pull on selected peer");
  io_.writeln("  ping                              Lightweight slave presence check");
  io_.writeln("  audio ping                        Trigger targeted slave audio/LED feedback ping");
  io_.writeln("");

  io_.writeln("[SETTINGS]");
  io_.writeln("  settings                          Pull settings summary");
  io_.writeln("  settings.full                     Pull all settings via paged fetch");
  io_.writeln("  settings.raw                      Pull raw settings descriptor view (paged)");
  io_.writeln("  get <setting_key>                 Read setting by key");
  io_.writeln("  get.id <setting_id>               Read setting by numeric ID");
  io_.writeln("  set <setting_key>=<value>         Write setting by key");
  io_.writeln("  set.id <setting_id>=<value>       Write setting by numeric ID");
  io_.writeln("  get cli_baud                      Read selected slave persisted CLI baud");
  io_.writeln("  set cli_baud=<baud>               Persist selected slave CLI baud (restart required)");
  io_.writeln("  note: supported baud = 9600,19200,38400,57600,74880,115200,230400,250000,460800,921600");
  io_.writeln("  note: SEMU child key syntax: v<0..7>.<sens_field>   (example: v2.detect_fall_delta_cm)");
  io_.writeln("  note: REMU child key syntax: v<0..15>.<relay_field> (example: v9.pulse_ms)");
  io_.writeln("");

  io_.writeln("[TELEMETRY PUSH]");
  io_.writeln("  push.start [mode] [int] [d] [g]   Start slave push (hybrid|periodic|change)");
  io_.writeln("  push.update [mode] [int] [d] [g]  Update push parameters");
  io_.writeln("  push.pause                        Pause push stream");
  io_.writeln("  push.resume                       Resume push stream");
  io_.writeln("  push.stop                         Stop push stream");
  io_.writeln("  push.get                          Query current push config");
  io_.writeln("  push.one <key> <mode> <int> <d> <g>  Configure one metric by key");
  io_.writeln("  push.id <id> <mode> <int> <d> <g>    Configure one metric by ID");
  io_.writeln("  push.child.start <vid> [mode] [int] [d] [g]  SEMU/REMU child stream add/update");
  io_.writeln("  push.child.stop <vid>             SEMU/REMU child stream remove");
  io_.writeln("  note: child metric key example: v2.tfl_a_mm  /  v9.relay_bitmap");
  io_.writeln("  autopull on [ms]                  Auto-poll common descriptors on interval");
  io_.writeln("  autopull off                      Stop auto-poll");
  io_.writeln("");

  io_.writeln("[TIME]");
  io_.writeln("  time.get                          Read slave epoch/uptime");
  io_.writeln("  time.set <epoch_s>                Set slave epoch seconds");
  io_.writeln("  time.set.now                      Set slave time from master now");
  io_.writeln("  time.local                        Show master local/system time");
  io_.writeln("");

  io_.writeln("[LIFECYCLE / CONTROL]");
  io_.writeln("  restart master                    Queue master restart");
  io_.writeln("  reset master                      Reset flow on master");
  io_.writeln("  restart slave                     Request slave restart");
  io_.writeln("  reset slave                       Request slave reset");
  io_.writeln("  tip: after set cli_baud=<baud> on a slave, run restart slave to apply");
  io_.writeln("  audio ping                        Request targeted slave audio/LED ping");
  io_.writeln("");

  io_.writeln("[TEST / DIAGNOSTICS]");
  io_.writeln("  test.all                          Run full non-destructive dispatch sweep");
  io_.writeln("  selftest                          Local self-check entry");
  io_.writeln("  comm.test                         Start communication test sequence");
  io_.writeln("  comm.test.status                  Read communication test status");
  io_.writeln("  comm.test.report                  Read communication test report");
  io_.writeln("  radio.drytest                     Validate radio-transition busy gate in one command");
  io_.writeln("  metrics                           Show runtime counters");
  io_.writeln("  metrics.reset                     Reset runtime counters");
  io_.writeln("  queue                             Show management queue depth");
  io_.writeln("");

  io_.writeln("[CLI LOG VERBOSITY]");
  io_.writeln("  log                               Show current CLI verbosity");
  io_.writeln("  log error                         Set CLI verbosity to errors only");
  io_.writeln("  log info                          Set CLI verbosity to info");
  io_.writeln("  log debug                         Set CLI verbosity to debug");
  io_.writeln("");

  io_.writeln("[LIBRARY LOGGER - LOCAL]");
  io_.writeln("  logger.status                     Show local binary logger status");
  io_.writeln("  logger.enable                     Enable local binary logger");
  io_.writeln("  logger.disable                    Disable local binary logger");
  io_.writeln("  logger.clear                      Clear local log storage");
  io_.writeln("  logger.read <offset> [max_bytes]  Read local raw log chunk");
  io_.writeln("");

  io_.writeln("[LIBRARY LOGGER - REMOTE]");
  io_.writeln("  logger.remote.status              Query slave logger status");
  io_.writeln("  logger.remote.enable              Enable slave logger");
  io_.writeln("  logger.remote.disable             Disable slave logger");
  io_.writeln("  logger.remote.clear               Clear slave logger storage");
  io_.writeln("  logger.remote.read <off> [<=128]  Read one remote log chunk");
  io_.writeln("  logger.remote.pull [<=128]        Pull full remote log export");
  io_.writeln("  logger.remote.stop                Stop active remote pull");
  io_.writeln("  channel.runtime.status             Show runtime channel keys/state");
  io_.writeln("  channel.sync <1..14>              Stage+ack all slaves then apply channel");
  io_.writeln("");

  io_.writeln("[STORAGE EXPLORER - LOCAL]");
  io_.writeln("  sd.info                           Show local storage backend/capacity");
  io_.writeln("  sd.pwd                            Show current local directory");
  io_.writeln("  sd.ls [path]                      List local directory");
  io_.writeln("  sd.cd <path>                      Change local directory");
  io_.writeln("  sd.up                             Move to parent local directory");
  io_.writeln("  sd.stat <path>                    Stat local file/dir");
  io_.writeln("  sd.format                         Format active local backend and rebuild tree");
  io_.writeln("");

  io_.writeln("[STORAGE EXPLORER - REMOTE]");
  io_.writeln("  sd.remote.info                    Show slave storage backend/capacity");
  io_.writeln("  sd.remote.pwd                     Show slave current directory");
  io_.writeln("  sd.remote.ls [path]               List slave directory");
  io_.writeln("  sd.remote.cd <path>               Change slave directory");
  io_.writeln("  sd.remote.up                      Move to parent slave directory");
  io_.writeln("  sd.remote.stat <path>             Stat slave file/dir");
  io_.writeln("  sd.remote.format                  Format slave backend and rebuild tree");
  io_.writeln("");

  io_.writeln("[OTA - STATUS / MANIFEST]");
  io_.writeln("  ota.info                          Read slave OTA transfer status");
  io_.writeln("  ota.manifest                      Pull slave OTA manifest");
  io_.writeln("  ota.manifest.rebuild              Rebuild slave OTA manifest");
  io_.writeln("  ota.clear <in|img|man|all>        Clear slave OTA scope");
  io_.writeln("  ota.clear.images                  Clear slave OTA image store");
  io_.writeln("  ota.local.clear.images            Clear local staged image store");
  io_.writeln("  ota.capacity                      Query slave OTA capacity limits");
  io_.writeln("  ota.gate                          Query slave OTA gate policy");
  io_.writeln("  ota.apply <image_id|name>         Ask slave to apply image from manifest");
  io_.writeln("  ota.rollback <master|slave>       Trigger rollback action on target");
  io_.writeln("");

  io_.writeln("[OTA - TRANSFER PIPELINE]");
  io_.writeln("  ota.prepare                       Clear slave receive folder (/o/in)");
  io_.writeln("  ota.push <local_path> [<=220]     Stream firmware to slave");
  io_.writeln("    note: bare filename resolves to /o/s/<name>");
  io_.writeln("    note: sidecar required /o/s/<name>.json with sw_version/build_id/target_role");
  io_.writeln("  ota.update <path> [<=220]         Slave pipeline: prepare -> push -> apply");
  io_.writeln("  ota.update.master [path]          Master local apply from stage (target_role=master)");
  io_.writeln("  ota.update.from.arc <id> [<=220] [master|slave]");
  io_.writeln("                                   Restore archive id to /o/s then run target update");
  io_.writeln("  ota.push.abort                    Abort active push and clear remote RX scope");
  io_.writeln("");

  io_.writeln("[OTA - ARCHIVE]");
  io_.writeln("  ota.arc.save [master|slave]       Save running firmware into SD archive (copy)");
  io_.writeln("  ota.arc.save.staged [master|slave] Copy staged /o/s/fw.bin + fw.json into SD archive");
  io_.writeln("  ota.arc.list [master|slave]       List archived firmware ids from local manifest");
  io_.writeln("  ota.arc.verify <id> [role]        Verify archive pair integrity (size/crc/metadata)");
  io_.writeln("  ota.arc.restore <id> [role]       Restore archived pair to /o/s/fw.bin + fw.json");
  io_.writeln("  ota.arc.delete <id> [role]        Remove one archive entry (SD + manifest)");
  io_.writeln("  ota.arc.clear [master|slave]      Remove all archive entries for role");
  io_.writeln("");

}

bool MasterCli::printTopicHelp(const std::string& topic) {
  const std::string t = lowerCopy(trim(topic));
  if (t.empty() || t == "all") {
    printHelp();
    return true;
  }

  auto print_header = [&](const char* name, const char* purpose) {
    writef("[MASTER][CLI][HELP] %s", name);
    writef("  purpose: %s", purpose);
  };

  if (t == "core" || t == "cli" || t == "status" || t == "list" || t == "paired" || t == "paired.list" ||
      t == "live") {
    print_header("core", "CLI runtime state, discovery listing, and operator shell control.");
    io_.writeln("  help                         Full command reference");
    io_.writeln("  help <topic>                 Detailed topic help");
    io_.writeln("  list                         Discovery window + peer list");
    io_.writeln("  paired                       Persisted paired peers list (pair_seq order)");
    io_.writeln("  status                       Paired/runtime-target/discovery/queue summary");
    io_.writeln("  active [<index|mac>|clear]   Sticky runtime target show/set/clear");
    io_.writeln("  live enable|disable|status   Global auto liveness monitor control/status");
    io_.writeln("  cli status|on|off            Persisted CLI enable control");
    io_.writeln("  cli.baud                     Show persisted CLI serial baud");
    io_.writeln("  cli.baud set <baud>          Save baud and restart master");
    io_.writeln("  target syntax                <index> <command> or <mac> <command>");
    return true;
  }

  if (t == "target" || t == "targeting" || t == "selector") {
    print_header("targeting", "How to route one command to one paired slave.");
    io_.writeln("  active <index|mac>           Set sticky target peer");
    io_.writeln("  active                       Show sticky target peer");
    io_.writeln("  active clear                 Clear sticky target peer");
    io_.writeln("  <index> <peer-command>       Preferred: paired list index target");
    io_.writeln("  <mac> <peer-command>         Preferred: explicit paired MAC target");
    io_.writeln("  target mode                  Active target + optional prefix override");
    io_.writeln("  tip: run 'paired' to see stable paired indexes");
    io_.writeln("  note: applies to peer-bound slave commands");
    return true;
  }

  if (t == "topology" || t == "topo") {
    print_header("topology", "Stage/commit/status control for topology runtime and slave deployment.");
    io_.writeln("  topology.status                   Get topology runtime status");
    io_.writeln("  topology.slots [committed|staged] Dump topology slots");
    io_.writeln("  topology.trigger <idx> <dir> [delay] [hold] [src_vid]");
    io_.writeln("                                   Trigger by relative index (dir: forward|reverse|1|2)");
    io_.writeln("  topology.stage.hex <hex>          Stage topology from raw hex payload");
    io_.writeln("  topology.stage.file <path>        Stage topology from local binary file");
    io_.writeln("  topology.commit                   Commit staged topology");
    io_.writeln("  topology.apply.hex <hex>          Stage then commit");
    io_.writeln("  topology.apply.file <path>        Stage then commit");
    io_.writeln("  topology.plan.file <path>         Dry processing report (no RF send)");
    io_.writeln("  topology.deploy.file <path>       Stage+commit fan-out to all paired peers");
    io_.writeln("  topology.edit.new [topo_ver] [seed_csv]");
    io_.writeln("  topology.edit.add <S|R|SM|RM> <paired_index|mac> [vi]");
    io_.writeln("  topology.edit.del <chain_pos>     Delete one chain node");
    io_.writeln("  topology.edit.clear               Clear in-memory chain");
    io_.writeln("  topology.edit.show                Show in-memory chain");
    io_.writeln("  topology.edit.validate            Validate with chain parser rules");
    io_.writeln("  topology.edit.save [path]         Save editor chain JSON to storage");
    io_.writeln("  topology.edit.load [path]         Load chain JSON into editor");
    io_.writeln("  topology.file.show [path]         Print raw topology JSON file");
    io_.writeln("  topology.chain.help               Show fixed-file chain command list");
    io_.writeln("  topology.chain.set.help           Show one-line chain_spec syntax");
    io_.writeln("  topology.chain.show");
    io_.writeln("  topology.chain.graph");
    io_.writeln("  topology.chain.clear");
    io_.writeln("  topology.chain.add <S|R|SM|RM> <paired_index|mac> [vi]");
    io_.writeln("  topology.chain.edit <index> <S|R|SM|RM> <paired_index|mac> [vi]");
    io_.writeln("  topology.chain.del <index>");
    io_.writeln("  topology.chain.move <from_index> <to_index>");
    io_.writeln("  topology.chain.validate");
    io_.writeln("  topology.chain.fix");
    io_.writeln("  topology.chain.apply");
    io_.writeln("  topology.chain.verify [timeout_ms]");
    io_.writeln("  topology.chain.backup");
    io_.writeln("  topology.chain.restore");
    io_.writeln("  topology.chain.set <chain_spec>");
    io_.writeln("  topology.local.*                  Same commands but forced local-only (no target)");
    io_.writeln("  chain.loop.status                 Persisted chain LoopAuto aggregate state");
    io_.writeln("  chain.loop.on|off                 Apply LoopAuto on all chain topology nodes");
    io_.writeln("  chain rules                       start/end with S|SM; no adjacent S/SM; R/RM adjacency allowed");
    io_.writeln("  target syntax                     <index> <command> or <mac> <command> works on topology.*");
    return true;
  }

  if (t == "pair" || t == "pairing" || t == "unpair" || t == "remove") {
    print_header("pairing", "Pair/unpair/remove peers under hard max-14 persisted capacity.");
    io_.writeln("  pair <index|mac>             Pair target (idempotent if already paired)");
    io_.writeln("  unpair                       Graceful unpair with selected target peer");
    io_.writeln("  remove [index|mac|slave]     Remove one paired peer (index: discovered first, then persisted)");
    io_.writeln("  <index> <peer-command>       Route one peer-bound command to paired index");
    io_.writeln("  <mac> <peer-command>         Route one peer-bound command to paired MAC");
    io_.writeln("  note: hard limit=14, no automatic eviction");
    io_.writeln("  note: at 14, pair and discovery start return capacity_limit_reached");
    return true;
  }

  if (t == "desc" || t == "descriptor" || t == "profile" || t == "caps" || t == "telem" ||
      t == "live" || t == "ping" || t == "audio" || t == "audio ping") {
    print_header("descriptor/profile", "Read remote identity, schema and online state.");
    io_.writeln("  desc                         Device identity (type/id/name/hw/sw/build)");
    io_.writeln("  caps                         Capability schema");
    io_.writeln("  telem                        Telemetry schema");
    io_.writeln("  telem.now                    Live telemetry values");
    io_.writeln("  telem.now.child <vid>        SEMU/REMU child-only view (+global metrics)");
    io_.writeln("  live                         One-shot liveness heartbeat/status on selected peer");
    io_.writeln("  ping                         Lightweight slave reachability check");
    io_.writeln("  audio ping                   Trigger targeted slave audio/LED feedback ping");
    io_.writeln("  live enable|disable|status   Global monitor control (not peer-targeted)");
    return true;
  }

  if (t == "settings" || t == "setting" || t == "get" || t == "set") {
    print_header("settings", "Read/write runtime configuration on slave by key or numeric id.");
    io_.writeln("  settings                     Summary settings pull");
    io_.writeln("  settings.full                Full paged settings fetch");
    io_.writeln("  settings.raw                 Raw descriptor settings view (paged)");
    io_.writeln("  get <key> | get.id <id>      Read one setting");
    io_.writeln("  set <key>=<v> | set.id ...   Write one setting");
    io_.writeln("  get cli_baud                 Read selected slave persisted CLI baud");
    io_.writeln("  set cli_baud=<baud>          Persist selected slave CLI baud");
    io_.writeln("  restart slave                Apply new slave CLI baud after set");
    io_.writeln("  baud list                    9600,19200,38400,57600,74880,115200,230400,250000,460800,921600");
    io_.writeln("  child keys: v<vid>.<field>   (SEMU vid=0..7, REMU vid=0..15)");
    io_.writeln("  examples: get v1.detect_release_delta_cm   set v6.pulse_ms=750");
    io_.writeln("  baud example: active 0  -> set cli_baud=250000  -> restart slave");
    io_.writeln("  note: values are validated by descriptor provider rules");
    return true;
  }

  if (t == "push" || t == "autopull" || t == "telemetry") {
    print_header("telemetry push", "Control slave-to-master telemetry streaming behavior.");
    io_.writeln("  push.start/update            Stream mode: hybrid|periodic|change");
    io_.writeln("  push.pause/resume/stop/get   Runtime stream control/status");
    io_.writeln("  push.one / push.id           Per-metric override by key/id");
    io_.writeln("  push.child.start <vid> ...   Add/update one SEMU/REMU child in active stream set");
    io_.writeln("  push.child.stop <vid>        Remove one SEMU/REMU child from active stream set");
    io_.writeln("  child metric keys            v<vid>.<metric> (SEMU/SENS/RELAY/REMU profile dependent)");
    io_.writeln("  autopull on [ms] / off       Master-side polling helper");
    io_.writeln("  units: int=interval_ms d=delta_abs g=min_gap_ms");
    return true;
  }

  if (t == "time" || t == "clock") {
    print_header("time", "Synchronize and inspect slave epoch/uptime.");
    io_.writeln("  time.get                     Read slave epoch and uptime");
    io_.writeln("  time.set <epoch_s>           Set slave UNIX epoch seconds");
    io_.writeln("  time.set.now                 Push master current time to slave");
    io_.writeln("  time.local                   Show master local/system epoch");
    return true;
  }

  if (t == "control" || t == "lifecycle" || t == "restart" || t == "reset") {
    print_header("lifecycle/control", "Controlled restart/reset operations through management path.");
    io_.writeln("  restart master | reset master");
    io_.writeln("  restart slave  | reset slave");
    io_.writeln("  audio ping");
    io_.writeln("  note: CLI no longer mutates peer runtime state directly");
    io_.writeln("  note: all operations go through management command path");
    return true;
  }

  if (t == "test" || t == "diagnostics" || t == "diag" || t == "metrics" || t == "queue" ||
      t == "selftest" || t == "comm.test") {
    print_header("test/diagnostics", "Health checks, communication probes and runtime counters.");
    io_.writeln("  test.all                     Full non-destructive dispatch sweep");
    io_.writeln("  selftest                     Local self-check");
    io_.writeln("  comm.test / status / report  Communication test lifecycle");
    io_.writeln("  radio.drytest                begin->blocked command->end transition dry run");
    io_.writeln("  metrics / metrics.reset      Runtime + management counters");
    io_.writeln("  queue                        Management queue depth");
    return true;
  }

  if (t == "log" || t == "verbosity") {
    print_header("cli verbosity", "Set text verbosity of CLI output.");
    io_.writeln("  log                          Show current level");
    io_.writeln("  log error|info|debug         Set level");
    io_.writeln("  scope: CLI text only (does not change binary logger)");
    return true;
  }

  if (t == "logger" || t == "logs") {
    print_header("library logger", "Binary logger control and extraction (local + remote).");
    io_.writeln("  logger.status/read/clear/enable/disable");
    io_.writeln("  logger.remote.status/read/clear/enable/disable");
    io_.writeln("  logger.remote.pull [<=128]   Full remote export pull");
    io_.writeln("  logger.remote.stop           Stop active export pull");
    io_.writeln("  channel.runtime.status       Runtime channel keys/status");
    io_.writeln("  channel.sync <1..14>         Stage all slaves then apply channel");
    return true;
  }

  if (t == "sd" || t == "storage" || t == "files" || t == "fs") {
    print_header("storage explorer", "Navigate/stat/format local and remote storage backends.");
    io_.writeln("  sd.info/pwd/ls/cd/up/stat/format");
    io_.writeln("  sd.remote.info/pwd/ls/cd/up/stat/format");
    io_.writeln("  note: format rebuilds library-required folder tree");
    return true;
  }

  if (t == "ota" || t == "update" || t == "firmware" || t == "archive" || t == "ota.arc") {
    print_header("ota", "Firmware status/transfer/apply/rollback/archive workflows.");
    io_.writeln("  status/manifest: ota.info ota.manifest ota.capacity ota.gate");
    io_.writeln("  remote update: ota.update <path> [chunk<=220]");
    io_.writeln("  local master update: ota.update.master [path]");
    io_.writeln("  push-only flow: ota.prepare -> ota.push -> ota.apply <image_id|name>");
    io_.writeln("  archive: ota.arc.save(.staged)/list/verify/restore/delete/clear");
    io_.writeln("  from archive: ota.update.from.arc <id> [chunk] [master|slave]");
    io_.writeln("  metadata sidecar: <bin>.json with sw_version/build_id/target_role");
    return true;
  }

  return false;
}

}  // namespace espnow_link
