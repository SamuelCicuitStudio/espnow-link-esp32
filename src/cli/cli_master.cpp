#include "espnow_link/cli_master.hpp"

#include <algorithm>
#if defined(ARDUINO)
#include <Arduino.h>
#endif
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "espnow_link/address.hpp"
#include "espnow_link/management_controller.hpp"
#include "espnow_link/management_queue_transport.hpp"
#include "espnow_link/management_runtime.hpp"
#include "espnow_link/management_utils.hpp"
#include "espnow_link/ota_paths.hpp"
#include "espnow_link/power.hpp"
#include "espnow_link/profile.hpp"
#include "cli_helpers.hpp"

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
                     ManagementRuntime* management_runtime)
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
      ota_push_storage_(ota_push_storage) {
  loadCliEnabled();
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
  io_.writeln("  tip: run caps first, then settings.full / telem.now");
  io_.writeln("  tip: .remote. commands target slave; others are local/master-side control");
  io_.writeln("  tip: peer target syntax: <paired_index> <command>  or  <MAC> <command>");
  io_.writeln("  tip: peer-bound commands require explicit target prefix");
  io_.writeln("  quick help: help <topic>  or  <topic> help");
  io_.writeln("  topics: core paired pairing target topology desc settings push time control test log logger sd ota");
  io_.writeln("");

  io_.writeln("[CORE]");
  io_.writeln("  help                              Show this reference");
  io_.writeln("  list                              List discovered peers");
  io_.writeln("  paired                            List persisted paired peers (deterministic order)");
  io_.writeln("  status                            Show paired/runtime-target/discovery/queue status");
  io_.writeln("  live enable                       Enable automatic paired-peer liveness monitor");
  io_.writeln("  live disable                      Disable automatic paired-peer liveness monitor");
  io_.writeln("  live status                       Show liveness monitor runtime status");
  io_.writeln("  cli status                        Show CLI enabled/disabled state");
  io_.writeln("  cli on                            Enable CLI and persist");
  io_.writeln("  cli off                           Disable CLI and persist");
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
  io_.writeln("  rule: all peer-bound slave commands support target prefix");
  io_.writeln("  syntax: <index> <command>  or  <MAC> <command>");
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
  io_.writeln("  topology.local.status             Local-only status (force no target)");
  io_.writeln("  topology.local.slots [state]      Local-only slot dump");
  io_.writeln("  topology.local.stage.hex <hex>    Local-only stage");
  io_.writeln("  topology.local.stage.file <path>  Local-only stage");
  io_.writeln("  topology.local.commit             Local-only commit");
  io_.writeln("  note: stage payload must be full binary stream format (no partial diff)");
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
  io_.writeln("  note: SEMU child key syntax: v<0..7>.<sens_field>   (example: v2.tf_near_mm)");
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
  io_.writeln("  chain.loop.status                 Show persisted chain auto-loop state");
  io_.writeln("  chain.loop.on                     Enable LoopAuto for all chain slaves");
  io_.writeln("  chain.loop.off                    Disable LoopAuto for all chain slaves");
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
    io_.writeln("  live enable|disable|status   Global auto liveness monitor control/status");
    io_.writeln("  cli status|on|off            Persisted CLI enable control");
    io_.writeln("  target syntax                <index> <command> or <mac> <command>");
    return true;
  }

  if (t == "target" || t == "targeting" || t == "selector") {
    print_header("targeting", "How to route one command to one paired slave.");
    io_.writeln("  <index> <peer-command>       Preferred: paired list index target");
    io_.writeln("  <mac> <peer-command>         Preferred: explicit paired MAC target");
    io_.writeln("  target mode                  Prefix-only: <paired_index|paired_mac> <command>");
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
    io_.writeln("  topology.local.*                  Same commands but forced local-only (no target)");
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
    io_.writeln("  child keys: v<vid>.<field>   (SEMU vid=0..7, REMU vid=0..15)");
    io_.writeln("  examples: get v1.tf_far_mm   set v6.pulse_ms=750");
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
    io_.writeln("  chain.loop.status            Persisted chain LoopAuto aggregate state");
    io_.writeln("  chain.loop.on|off            Apply LoopAuto on all chain topology nodes");
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
}

bool MasterCli::resolveRuntimePeer(MacAddress& out_peer) const {
  if (command_target_override_active_) {
    out_peer = command_target_override_peer_;
    return true;
  }
  return false;
}

bool MasterCli::hasRuntimePeer() const {
  MacAddress peer{};
  return resolveRuntimePeer(peer);
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

bool MasterCli::startRemoteLogPull(uint16_t chunk_size) {
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    io_.writeln("[MASTER][LOGGER][REMOTE] target not selected");
    return false;
  }
  if (remote_log_pull_active_) {
    io_.writeln("[MASTER][LOGGER][REMOTE] pull already active");
    return false;
  }
  if (remote_log_store_ == nullptr) {
    io_.writeln("[MASTER][LOGGER][REMOTE] export store unavailable");
    return false;
  }
  if (chunk_size == 0U || chunk_size > 128U) {
    io_.writeln("[MASTER][LOGGER][REMOTE] invalid chunk_size (1..128)");
    return false;
  }
  if (!remote_log_store_->clear()) {
    io_.writeln("[MASTER][LOGGER][REMOTE] failed to clear export store");
    return false;
  }

  remote_log_pull_active_ = true;
  remote_log_pull_waiting_status_ = true;
  remote_log_pull_total_bytes_ = 0;
  remote_log_pull_next_offset_ = 0;
  remote_log_pull_chunk_size_ = chunk_size;
  remote_log_pull_chunks_ = 0;
  remote_log_pull_started_ms_ = nowMs();
  remote_log_pull_last_activity_ms_ = remote_log_pull_started_ms_;
  remote_log_pull_has_target_peer_ = true;
  remote_log_pull_target_peer_ = target_peer;

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    stopRemoteLogPull("management path unavailable", false);
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool sent = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LogRemoteStatusGet),
                                           {},
                                           nullptr,
                                           0U,
                                           false,
                                           &target_peer);
  correlation_id_ = mgmt.nextReqId();
  if (!sent) {
    stopRemoteLogPull("failed to request remote logger status", false);
    return false;
  }

  writef("[MASTER][LOGGER][REMOTE] pull started chunk=%u", static_cast<unsigned int>(chunk_size));
  return true;
}

bool MasterCli::requestNextRemoteLogChunk() {
  if (!remote_log_pull_active_ || !remote_log_pull_has_target_peer_) {
    return false;
  }
  if (management_transport_ == nullptr) {
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool sent = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::LogRemoteRead),
                                           management_utils::buildLogReadPayload(remote_log_pull_next_offset_,
                                                                                  remote_log_pull_chunk_size_),
                                           nullptr,
                                           0U,
                                           false,
                                           &remote_log_pull_target_peer_);
  correlation_id_ = mgmt.nextReqId();
  return sent;
}

void MasterCli::stopRemoteLogPull(const char* reason, bool success) {
  const uint32_t elapsed_ms = nowMs() - remote_log_pull_started_ms_;
  const uint32_t bytes = remote_log_pull_next_offset_;
  const uint16_t chunks = remote_log_pull_chunks_;
  remote_log_pull_active_ = false;
  remote_log_pull_waiting_status_ = false;
  remote_log_pull_has_target_peer_ = false;
  remote_log_pull_target_peer_ = {};

  if (success) {
    writef("[MASTER][LOGGER][REMOTE] %s bytes=%lu chunks=%u elapsed_ms=%lu file=%s",
           reason != nullptr ? reason : "done",
           static_cast<unsigned long>(bytes),
           static_cast<unsigned int>(chunks),
           static_cast<unsigned long>(elapsed_ms),
           (remote_log_store_ != nullptr) ? remote_log_store_->config().log_path.c_str() : "n/a");
    io_.writeln("[MASTER][LOGGER][REMOTE] decode with: python tools/log_decode.py <exported_file>");
    return;
  }

  writef("[MASTER][LOGGER][REMOTE] pull failed: %s bytes=%lu chunks=%u elapsed_ms=%lu",
         reason != nullptr ? reason : "unknown",
         static_cast<unsigned long>(bytes),
         static_cast<unsigned int>(chunks),
         static_cast<unsigned long>(elapsed_ms));
}

bool MasterCli::computeOtaPushCrc(const std::string& local_path,
                                  uint32_t size_bytes,
                                  uint32_t& out_crc,
                                  std::string* out_error) {
  if (ota_push_storage_ == nullptr) {
    if (out_error != nullptr) {
      *out_error = "storage backend is null";
    }
    return false;
  }
  if (size_bytes == 0U) {
    return false;
  }

  constexpr uint32_t kCrc32Poly = 0xEDB88320U;
  auto crcUpdate = [](uint32_t running_crc, const uint8_t* data, size_t len) -> uint32_t {
    if (data == nullptr || len == 0U) {
      return running_crc;
    }
    uint32_t crc = running_crc;
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint32_t>(data[i]);
      for (uint8_t b = 0; b < 8U; ++b) {
        const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
        crc = (crc >> 1U) ^ (kCrc32Poly & mask);
      }
    }
    return crc;
  };

  uint32_t offset = 0U;
  uint32_t running_crc = 0xFFFFFFFFU;
  std::string msg;
  // CRC scan uses backend readAt(), which opens/seeks per call.
  // Keep a larger block size here to reduce SD transaction churn.
  if (ota_push_buf_.size() < 4096U) {
    ota_push_buf_.resize(4096U);
  }
  while (offset < size_bytes) {
    const size_t req = std::min<size_t>(ota_push_buf_.size(), static_cast<size_t>(size_bytes - offset));
    size_t out_len = 0U;
    if (!ota_push_storage_->readAt(local_path, offset, ota_push_buf_.data(), req, out_len, msg)) {
      if (out_error != nullptr) {
        *out_error = msg.empty() ? "readAt failed" : msg;
      }
      return false;
    }
    if (out_len == 0U) {
      if (out_error != nullptr) {
        *out_error = "short read (0 bytes)";
      }
      return false;
    }
    running_crc = crcUpdate(running_crc, ota_push_buf_.data(), out_len);
    offset += static_cast<uint32_t>(out_len);
  }

  out_crc = ~running_crc;
  if (out_error != nullptr) {
    out_error->clear();
  }
  return true;
}

bool MasterCli::startOtaPush(const std::string& local_path, uint16_t chunk_bytes) {
  if (ota_push_storage_ == nullptr) {
    io_.writeln("[MASTER][OTA] ota.push unavailable (no local OTA storage backend bound)");
    return false;
  }
  MacAddress target_peer{};
  if (!resolveRuntimePeer(target_peer)) {
    io_.writeln("[MASTER][OTA] target not selected");
    return false;
  }
  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][OTA] ota.push unavailable (management path unavailable)");
    return false;
  }
  if (ota_push_active_) {
    io_.writeln("[MASTER][OTA] ota.push already active");
    return false;
  }
  if (chunk_bytes < 32U || chunk_bytes > 220U) {
    io_.writeln("[MASTER][OTA] invalid chunk_bytes (32..220)");
    return false;
  }

  OtaStorageStat st{};
  std::string msg;
  if (!ota_push_storage_->stat(local_path, st, msg)) {
    writef("[MASTER][OTA] ota.push stat failed: %s", msg.c_str());
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    io_.writeln("[MASTER][OTA] ota.push invalid file path");
    return false;
  }

  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  uint32_t corr = 0U;
  const bool queued = submitRuntimeTargeted_(mgmt,
                                             static_cast<uint16_t>(ManagementCommandId::OtaPushStart),
                                             management_utils::buildOtaPushStartPayload(local_path, chunk_bytes),
                                             &corr,
                                             0U,
                                             false,
                                             &target_peer);
  correlation_id_ = mgmt.nextReqId();
  if (!queued || corr == 0U) {
    io_.writeln("[MASTER][OTA] ota.push start request failed");
    return false;
  }

  ota_push_active_ = true;
  ota_push_has_target_peer_ = true;
  ota_push_target_peer_ = target_peer;
  ota_push_path_ = local_path;
  ota_push_chunk_bytes_ = chunk_bytes;
  ota_push_size_bytes_ = st.size_bytes;
  ota_push_crc32_ = 0U;
  ota_push_offset_ = 0U;
  ota_push_chunks_sent_ = 0U;
  ota_push_corr_id_ = corr;
  ota_push_phase_ = OtaPushPhase::WaitBeginStatus;
  ota_push_started_ms_ = nowMs();
  ota_push_last_activity_ms_ = ota_push_started_ms_;
  ota_push_last_status_req_ms_ = 0U;
  ota_push_send_fail_streak_ = 0U;
  ota_push_next_send_ms_ = 0U;
  ota_push_last_end_send_ms_ = 0U;
  ota_push_end_send_count_ = 0U;
  ota_push_remote_acked_offset_ = 0U;
  ota_push_last_nack_offset_ = 0U;
  ota_push_window_target_offset_ = 0U;
  ota_push_window_wait_started_ms_ = 0U;
  ota_push_window_retry_count_ = 0U;
  ota_push_window_size_chunks_ = ota_push_window_size_default_chunks_;
  ota_push_recovery_until_acked_offset_ = 0U;
  ota_push_last_nack_log_ms_ = 0U;
  ota_push_waiting_window_ack_ = false;
  ota_push_begin_ack_seen_ = false;
  ota_push_buf_.clear();
  ota_update_prepare_pending_ = false;
  ota_update_prepare_corr_id_ = 0U;
  ota_update_image_name_ = otaImageNameFromCorr(corr);

  writef("[MASTER][OTA] ota.push started path=%s size=%lu chunk=%u corr=%lu",
         ota_push_path_.c_str(),
         static_cast<unsigned long>(ota_push_size_bytes_),
         static_cast<unsigned int>(ota_push_chunk_bytes_),
         static_cast<unsigned long>(ota_push_corr_id_));
  io_.writeln("[MASTER][OTA] waiting for slave begin status (management scheduler)...");
  return true;
}

bool MasterCli::requestOtaPushStatus() {
  if (!ota_push_active_ || !ota_push_has_target_peer_) {
    return false;
  }
  if (management_transport_ == nullptr) {
    return false;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool sent = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::OtaStatusGet),
                                           {},
                                           nullptr,
                                           0U,
                                           false,
                                           &ota_push_target_peer_,
                                           ota_push_corr_id_);
  correlation_id_ = mgmt.nextReqId();
  if (!sent) {
    return false;
  }
  ota_push_last_status_req_ms_ = nowMs();
  return true;
}

void MasterCli::handleOtaPushStatusResponse(const DescriptorResponse& d) {
  if (!ota_push_active_) {
    return;
  }
  ota_push_last_activity_ms_ = nowMs();

  const uint8_t state = d.ota_status.transfer_state;
  const uint16_t code = d.ota_status.status_code;

  if (ota_push_phase_ == OtaPushPhase::WaitBeginStatus) {
    if (code != static_cast<uint16_t>(OtaStatusCode::Ok)) {
      stopOtaPush(d.message.empty() ? "slave rejected begin" : d.message.c_str(), false);
      return;
    }
    if (state == static_cast<uint8_t>(OtaTransferState::Failed)) {
      stopOtaPush(d.message.empty() ? "slave entered failed state" : d.message.c_str(), false);
      return;
    }
    if (state == static_cast<uint8_t>(OtaTransferState::Receiving)) {
      ota_push_begin_ack_seen_ = true;
      ota_push_phase_ = OtaPushPhase::Streaming;
      io_.writeln("[MASTER][OTA] begin acknowledged; streaming chunks...");
    }
    return;
  }

  if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
    if (code != static_cast<uint16_t>(OtaStatusCode::Ok) ||
        state == static_cast<uint8_t>(OtaTransferState::Failed)) {
      stopOtaPush(d.message.empty() ? "slave finalize failed" : d.message.c_str(), false);
      return;
    }

    if (state == static_cast<uint8_t>(OtaTransferState::Ready) &&
        d.ota_status.received_size == ota_push_size_bytes_ &&
        d.ota_status.expected_size == ota_push_size_bytes_) {
      const std::string ready_name = otaFileNameFromPath(d.ota_status.image_path);
      if (!ready_name.empty()) {
        ota_update_image_name_ = ready_name;
      }
      stopOtaPush("complete", true);
    }
  }
}

void MasterCli::stopOtaPush(const char* reason, bool success) {
  const uint32_t elapsed_ms = nowMs() - ota_push_started_ms_;
  const uint32_t sent_bytes = ota_push_offset_;
  const uint16_t chunks = ota_push_chunks_sent_;
  ota_push_active_ = false;

  if (success) {
    writef("[MASTER][OTA] ota.push done bytes=%lu/%lu chunks=%u elapsed_ms=%lu path=%s",
           static_cast<unsigned long>(sent_bytes),
           static_cast<unsigned long>(ota_push_size_bytes_),
           static_cast<unsigned int>(chunks),
           static_cast<unsigned long>(elapsed_ms),
           ota_push_path_.c_str());
  } else {
    writef("[MASTER][OTA] ota.push failed: %s bytes=%lu/%lu chunks=%u elapsed_ms=%lu",
           reason != nullptr ? reason : "unknown",
           static_cast<unsigned long>(sent_bytes),
           static_cast<unsigned long>(ota_push_size_bytes_),
           static_cast<unsigned int>(chunks),
           static_cast<unsigned long>(elapsed_ms));
  }

  if (ota_update_pipeline_active_) {
    if (!success) {
      io_.writeln("[MASTER][OTA] update pipeline failed during push");
      ota_update_prepare_pending_ = false;
      ota_update_prepare_corr_id_ = 0U;
      ota_update_staged_path_.clear();
      ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
      ota_update_pipeline_active_ = false;
      ota_update_wait_boot_notice_ = false;
      ota_update_image_name_.clear();
    } else if (ota_update_image_name_.empty()) {
      io_.writeln("[MASTER][OTA] update pipeline failed: image name missing after push");
      ota_update_prepare_pending_ = false;
      ota_update_prepare_corr_id_ = 0U;
      ota_update_staged_path_.clear();
      ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
      ota_update_pipeline_active_ = false;
      ota_update_wait_boot_notice_ = false;
    } else {
      bool apply_ok = false;
      if (management_transport_ != nullptr) {
        ManagementController mgmt(*management_transport_);
        mgmt.setNextReqId(correlation_id_);
        const MacAddress* target_peer = ota_push_has_target_peer_ ? &ota_push_target_peer_ : nullptr;
        apply_ok = submitRuntimeTargeted_(mgmt,
                                          static_cast<uint16_t>(ManagementCommandId::OtaApply),
                                          management_utils::buildStringPayloadU16(ota_update_image_name_),
                                          nullptr,
                                          0U,
                                          target_peer == nullptr,
                                          target_peer);
        correlation_id_ = mgmt.nextReqId();
      }
      if (apply_ok) {
        writef("[MASTER][OTA] update pipeline apply requested target=%s", ota_update_image_name_.c_str());
        ota_update_wait_boot_notice_ = true;
      } else {
        io_.writeln("[MASTER][OTA] update pipeline failed: apply request failed");
        ota_update_prepare_pending_ = false;
        ota_update_prepare_corr_id_ = 0U;
        ota_update_staged_path_.clear();
        ota_update_chunk_bytes_ = ota_push_chunk_bytes_;
        ota_update_pipeline_active_ = false;
        ota_update_wait_boot_notice_ = false;
        ota_update_image_name_.clear();
      }
    }
  }

  ota_push_path_.clear();
  ota_push_has_target_peer_ = false;
  ota_push_target_peer_ = {};
  ota_push_size_bytes_ = 0U;
  ota_push_crc32_ = 0U;
  ota_push_offset_ = 0U;
  ota_push_chunks_sent_ = 0U;
  ota_push_corr_id_ = 0U;
  ota_push_phase_ = OtaPushPhase::Idle;
  ota_push_started_ms_ = 0U;
  ota_push_last_activity_ms_ = 0U;
  ota_push_last_status_req_ms_ = 0U;
  ota_push_send_fail_streak_ = 0U;
  ota_push_next_send_ms_ = 0U;
  ota_push_last_end_send_ms_ = 0U;
  ota_push_end_send_count_ = 0U;
  ota_push_remote_acked_offset_ = 0U;
  ota_push_last_nack_offset_ = 0U;
  ota_push_window_target_offset_ = 0U;
  ota_push_window_wait_started_ms_ = 0U;
  ota_push_window_retry_count_ = 0U;
  ota_push_window_size_chunks_ = ota_push_window_size_default_chunks_;
  ota_push_recovery_until_acked_offset_ = 0U;
  ota_push_last_nack_log_ms_ = 0U;
  ota_push_waiting_window_ack_ = false;
  ota_push_begin_ack_seen_ = false;
}

void MasterCli::pumpOtaPush(uint32_t now_ms) {
  if (!ota_push_active_) {
    return;
  }
  if (!ota_push_has_target_peer_ || !manager_.hasPersistedPair(ota_push_target_peer_)) {
    stopOtaPush("link lost", false);
    return;
  }

  constexpr uint32_t kWaitBeginTimeoutMs = 12000U;
  constexpr uint32_t kWaitEndTimeoutMs = 60000U;
  constexpr uint32_t kStatusPollIntervalMs = 250U;

  if (ota_push_phase_ == OtaPushPhase::WaitBeginStatus) {
    if (static_cast<int32_t>(now_ms - ota_push_last_activity_ms_) >=
        static_cast<int32_t>(kWaitBeginTimeoutMs)) {
      stopOtaPush("begin status timeout", false);
      return;
    }
    if (ota_push_begin_ack_seen_) {
      ota_push_phase_ = OtaPushPhase::Streaming;
      io_.writeln("[MASTER][OTA] begin acknowledged by slave status; streaming chunks...");
      return;
    }
    if (static_cast<int32_t>(now_ms - ota_push_last_status_req_ms_) >= static_cast<int32_t>(kStatusPollIntervalMs)) {
      (void)requestOtaPushStatus();
    }
    return;
  }

  if (ota_push_phase_ == OtaPushPhase::WaitEndStatus) {
    if (static_cast<int32_t>(now_ms - ota_push_last_activity_ms_) >=
        static_cast<int32_t>(kWaitEndTimeoutMs)) {
      stopOtaPush("finalize status timeout", false);
    }
    return;
  }
}

}  // namespace espnow_link

