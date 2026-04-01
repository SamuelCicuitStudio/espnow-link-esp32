#include "espnow_link/slave_runtime_defaults.hpp"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "espnow_link/power.hpp"
#if defined(ARDUINO)
#include "espnow_link/ota_apply_arduino.hpp"
#endif

#if defined(ARDUINO)
#include <sys/time.h>
#endif
#if defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#include <esp_system.h>
#endif

namespace {
constexpr uint16_t kControlCmdRestart = 0x0001;
constexpr uint16_t kControlCmdReset = 0x0002;
constexpr uint16_t kControlCmdAudioPing = 0x0004;
constexpr uint16_t kControlResultUnsupported = 0x0004;
constexpr uint16_t kPresencePingEventId = 0x7F12;

const char* topologyTriggerReasonText_(uint8_t reason) {
  switch (reason) {
    case 0x00:
      return "ok";
    case 0x01:
      return "unauthorized_peer";
    case 0x02:
      return "stale_topology";
    case 0x03:
      return "bad_index";
    case 0x04:
      return "range_rejected";
    case 0x05:
      return "busy";
    default:
      return "unknown";
  }
}
}  // namespace

namespace espnow_link {

#if defined(ARDUINO)
bool SlaveOtaBootstrap::begin(const Config& cfg) {
  cfg_ = cfg;
  manager_.reset();
  sink_.reset();
  descriptor_.reset();
  owned_apply_.reset();

  if (cfg_.storage == nullptr) {
    return false;
  }

  IOtaApplyExecutor* apply = cfg_.apply_executor;
  if (apply == nullptr) {
    auto owned = std::make_unique<ArduinoOtaApplyExecutor>(
        *cfg_.storage, cfg_.apply_chunk_bytes, cfg_.apply_reboot_on_success);
    if (owned == nullptr) {
      return false;
    }
    apply = owned.get();
    owned_apply_ = std::move(owned);
  }

  OtaManagerConfig manager_cfg = cfg_.manager_config;
  if (manager_cfg.max_firmware_bytes == 0U && cfg_.max_firmware_bytes > 0U) {
    manager_cfg.max_firmware_bytes = cfg_.max_firmware_bytes;
  }
#if defined(ESP_PLATFORM)
  if (manager_cfg.max_firmware_bytes == 0U) {
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (update_partition != nullptr) {
      manager_cfg.max_firmware_bytes = static_cast<uint32_t>(update_partition->size);
    }
  }
#endif

  manager_ = std::make_unique<OtaManager>(*cfg_.storage, manager_cfg, cfg_.gate, apply);
  if (manager_ == nullptr) {
    return false;
  }
  sink_ = std::make_unique<OtaFirmwareSink>(*manager_);
  descriptor_ = std::make_unique<OtaDescriptorAdapter>(*manager_, *cfg_.storage, manager_cfg);
  if (sink_ == nullptr || descriptor_ == nullptr) {
    return false;
  }

  std::string msg;
  if (!manager_->begin(msg)) {
    if (cfg_.print_report) {
      Serial.printf("[%s][OTA] manager begin failed: %s\n",
                    (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
                    msg.c_str());
    }
    return false;
  }

  if (cfg_.print_report) {
    Serial.printf("[%s][OTA] manager ready max_fw_bytes=%lu\n",
                  (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
                  static_cast<unsigned long>(manager_->maxFirmwareBytes()));
  }

  if (cfg_.mark_boot_success && cfg_.running_sw_version != nullptr && cfg_.running_sw_version[0] != '\0') {
    std::string boot_msg;
    const std::string build =
        (cfg_.running_build_id != nullptr) ? cfg_.running_build_id : (std::string(__DATE__) + " " + std::string(__TIME__));
    if (manager_->markBootSuccessful(cfg_.running_sw_version, build, boot_msg)) {
      if (cfg_.print_report && boot_msg != "no pending ota apply") {
        Serial.printf("[%s][OTA] %s\n",
                      (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
                      boot_msg.c_str());
      }
    } else if (cfg_.print_report) {
      Serial.printf("[%s][OTA] boot confirm failed: %s\n",
                    (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
                    boot_msg.c_str());
    }
  }

  return true;
}

bool SlaveStorageBootstrap::begin(const Config& cfg, ArduinoNodeStorageBootstrap::Report* out_report) {
  ArduinoNodeStorageBootstrap::Config boot_cfg{};
  boot_cfg.sd_ready = cfg.sd_ready;
  boot_cfg.spiffs_ready = cfg.spiffs_ready;
  boot_cfg.sd_fs = cfg.sd_fs;
  boot_cfg.sd_fs_typed = cfg.sd_fs;
  boot_cfg.spiffs_fs = cfg.spiffs_fs;
  boot_cfg.spiffs_fs_typed = cfg.spiffs_fs;
  boot_cfg.log_backend = cfg.log_backend;
  boot_cfg.storage_explorer = cfg.storage_explorer;
  boot_cfg.ota_backend = cfg.ota_backend;
  boot_cfg.log_store = cfg.log_store;
  boot_cfg.remote_log_store = cfg.remote_log_store;
  boot_cfg.logger = cfg.logger;
  boot_cfg.prefer_sd_for_logs = cfg.prefer_sd_for_logs;
  boot_cfg.prefer_sd_for_explorer = cfg.prefer_sd_for_explorer;
  boot_cfg.log_rotate_spiffs_bytes = cfg.log_rotate_spiffs_bytes;
  boot_cfg.log_rotate_sd_bytes = cfg.log_rotate_sd_bytes;

  ArduinoNodeStorageBootstrap::Report report{};
  const bool ok = bootstrap_.begin(boot_cfg, &report);
  if (cfg.print_report) {
    printReport_(cfg, report);
  }
  if (out_report != nullptr) {
    *out_report = report;
  }
  return ok;
}

void SlaveStorageBootstrap::printReport_(const Config& cfg,
                                         const ArduinoNodeStorageBootstrap::Report& report) const {
  const char* prefix = (cfg.log_prefix != nullptr) ? cfg.log_prefix : "SLAVE";
  if (report.ota_layout_ready) {
    Serial.printf("[%s][OTA] spiffs ota layout ready\n", prefix);
  }
  if (!report.ota_backend_ready) {
    Serial.printf("[%s][OTA] storage not ready: %s\n", prefix, report.ota_message.c_str());
  } else if (cfg.ota_backend != nullptr) {
    Serial.printf("[%s][OTA] storage backend=%s\n", prefix, cfg.ota_backend->activeBackendName());
  }
  if (!report.log_ready) {
    Serial.printf("[%s] log store begin failed (logger disabled until storage is ready)\n", prefix);
  } else {
    Serial.printf("[%s] logger backend=%s rotate_max=%lu bytes\n",
                  prefix,
                  report.log_backend_name,
                  static_cast<unsigned long>(report.log_rotate_max_bytes));
  }
}
#endif  // defined(ARDUINO)

void SlaveEventLogger::logf_(const char* fmt, ...) const {
#if defined(ARDUINO)
  va_list args;
  va_start(args, fmt);
  char buf[240] = {0};
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
#else
  (void)fmt;
#endif
}

std::string SlaveEventLogger::macText_(const MacAddress& mac) const { return macToString(mac); }

void SlaveEventLogger::clearPeer() {
  has_peer_ = false;
  peer_ = {};
}

void SlaveEventLogger::onEvent(const Event& e) {
  const char* prefix = (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE";
  if (e.type == Event::Type::TopologyTriggerReceived ||
      e.type == Event::Type::TopologyTriggerRejected ||
      e.type == Event::Type::TopologyTriggerDuplicate) {
    const char* decision = "accepted";
    if (e.type == Event::Type::TopologyTriggerRejected) {
      decision = "rejected";
    } else if (e.type == Event::Type::TopologyTriggerDuplicate) {
      decision = "duplicate";
    }
    logf_("[%s][TOPO][RX] seq=%u decision=%s reason=%s(%u) src=%u:%u dst=%u:%u peer=%s corr=%lu\n",
          prefix,
          static_cast<unsigned>(e.event_id),
          decision,
          topologyTriggerReasonText_(e.reason),
          static_cast<unsigned>(e.reason),
          static_cast<unsigned>(e.src_role),
          static_cast<unsigned>(e.src_vid),
          static_cast<unsigned>(e.dst_role),
          static_cast<unsigned>(e.dst_vid),
          macText_(e.peer).c_str(),
          static_cast<unsigned long>(e.correlation_id));
    return;
  }

  if (e.type == Event::Type::Paired) {
    peer_ = e.peer;
    has_peer_ = true;
    if (cfg_.log_paired) {
      logf_("[%s] paired with %s\n", prefix, macText_(peer_).c_str());
    }
    return;
  }

  if (e.type == Event::Type::PairingStep) {
    if (cfg_.log_pairing_step) {
      logf_("[%s][PAIR] corr=%lu peer=%s step=%s\n",
            prefix,
            static_cast<unsigned long>(e.correlation_id),
            macText_(e.peer).c_str(),
            e.message.c_str());
    }
    if (e.message.find("unpair") != std::string::npos) {
      clearPeer();
    }
    return;
  }

  if (e.type == Event::Type::PacketDropped) {
    if (cfg_.log_packet_drop) {
      logf_("[%s][DROP] corr=%lu peer=%s reason=%s\n",
            prefix,
            static_cast<unsigned long>(e.correlation_id),
            macText_(e.peer).c_str(),
            e.message.c_str());
    }
    return;
  }

  if (e.type == Event::Type::PairingFailed && cfg_.log_pairing_fail) {
    logf_("[%s][PAIR][FAIL] corr=%lu peer=%s reason=%s\n",
          prefix,
          static_cast<unsigned long>(e.correlation_id),
          macText_(e.peer).c_str(),
          e.message.c_str());
  }
}

void SlaveControlPlane::setFlag_(const char* key) {
  if (nvs_ == nullptr || key == nullptr || key[0] == '\0') {
    return;
  }
  const uint8_t flag = 1U;
  (void)nvs_->putFixed(key, &flag, sizeof(flag));
}

void SlaveControlPlane::logf_(const char* fmt, ...) const {
#if defined(ARDUINO)
  va_list args;
  va_start(args, fmt);
  char buf[240] = {0};
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
#else
  (void)fmt;
#endif
}

std::string SlaveControlPlane::macText_(const MacAddress& mac) const { return macToString(mac); }

bool SlaveControlPlane::readBoolSetting_(const char* key, bool fallback) const {
  if (nvs_ == nullptr || key == nullptr || key[0] == '\0') {
    return fallback;
  }
  bool value = fallback;
  (void)nvs_->getBool(key, value);
  return value;
}

bool SlaveControlPlane::sendDescriptorResponse_(const MacAddress& to,
                                                uint32_t corr_id,
                                                const DescriptorResponse& response) {
  if (manager_ == nullptr) {
    return false;
  }
  std::vector<uint8_t> encoded;
  if (!manager_->encodeDescriptorResponsePayload(response, encoded)) {
    return false;
  }
  return manager_->sendPullResponse(to, encoded.data(), encoded.size(), corr_id);
}

bool SlaveControlPlane::sendDescriptorError_(const MacAddress& to,
                                             uint32_t corr_id,
                                             const std::string& message) {
  DescriptorResponse response{};
  response.type = DescriptorResponseType::Error;
  response.message = message;
  return sendDescriptorResponse_(to, corr_id, response);
}

bool SlaveControlPlane::sendDescriptorAck_(const MacAddress& to,
                                           uint32_t corr_id,
                                           const std::string& message) {
  DescriptorResponse response{};
  response.type = DescriptorResponseType::Ack;
  response.message = message;
  return sendDescriptorResponse_(to, corr_id, response);
}

void SlaveControlPlane::resetTopologyStageSession_() {
  topology_stage_active_ = false;
  topology_stage_owner_ = {};
  topology_stage_snapshot_ = EspNowManager::TopologySnapshot{};
  topology_stage_snapshot_.state = EspNowManager::TopologyState::Staged;
  topology_stage_slot_seen_.fill(false);
  topology_stage_group_seen_.fill(false);
}

bool SlaveControlPlane::ensureTopologySourceAuthorized_(const MacAddress& from) const {
  if (manager_ == nullptr) {
    return false;
  }
  MacAddress active_peer{};
  return manager_->getPairedPeer(active_peer) && active_peer == from;
}

bool SlaveControlPlane::handleTopologyQuery_(const MacAddress& from,
                                             uint32_t corr_id,
                                             const DescriptorQuery& query,
                                             bool& out_handled) {
  out_handled = false;
  if (manager_ == nullptr) {
    return false;
  }

  const bool is_topology_query =
      query.type == DescriptorQueryType::TopologyStageClear ||
      query.type == DescriptorQueryType::TopologyStageBegin ||
      query.type == DescriptorQueryType::TopologyStageSlotSet ||
      query.type == DescriptorQueryType::TopologyStageGroupSet ||
      query.type == DescriptorQueryType::TopologyStageFinalize ||
      query.type == DescriptorQueryType::TopologyCommit ||
      query.type == DescriptorQueryType::TopologyStatus ||
      query.type == DescriptorQueryType::TopologyTriggerSend;
  if (!is_topology_query) {
    return false;
  }
  out_handled = true;

  if (!ensureTopologySourceAuthorized_(from)) {
    return sendDescriptorError_(from, corr_id, "topology source unauthorized");
  }

  if (query.type == DescriptorQueryType::TopologyStageClear) {
    resetTopologyStageSession_();
    if (!manager_->clearStagedTopology()) {
      logf_("[%s][TOPO] stage_clear status=fail\n",
            (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE");
      return sendDescriptorError_(from, corr_id, "topology stage clear failed");
    }
    logf_("[%s][TOPO] stage_clear status=ok\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE");
    return sendDescriptorAck_(from, corr_id, "topology stage cleared");
  }

  if (query.type == DescriptorQueryType::TopologyStageBegin) {
    resetTopologyStageSession_();
    topology_stage_active_ = true;
    topology_stage_owner_ = from;
    topology_stage_snapshot_.schema_version = query.topology_schema_version;
    topology_stage_snapshot_.state = EspNowManager::TopologyState::Staged;
    topology_stage_snapshot_.topology_version = query.topology_version;
    topology_stage_snapshot_.index_neg = query.topology_index_neg;
    topology_stage_snapshot_.index_pos = query.topology_index_pos;
    logf_("[%s][TOPO] stage_begin ver=%lu neg=%u pos=%u schema=%u owner=%s\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          static_cast<unsigned long>(query.topology_version),
          static_cast<unsigned int>(query.topology_index_neg),
          static_cast<unsigned int>(query.topology_index_pos),
          static_cast<unsigned int>(query.topology_schema_version),
          macText_(from).c_str());
    return sendDescriptorAck_(from, corr_id, "topology stage begin ok");
  }

  if (query.type == DescriptorQueryType::TopologyStageSlotSet) {
    if (!topology_stage_active_ || topology_stage_owner_ != from) {
      return sendDescriptorError_(from, corr_id, "topology stage not active");
    }
    if (query.topology_slot_index >= EspNowManager::kTopologyMaxSlots) {
      return sendDescriptorError_(from, corr_id, "topology slot index invalid");
    }
    auto& slot = topology_stage_snapshot_.slots[query.topology_slot_index];
    slot.enabled = query.topology_slot_enabled;
    slot.peer = query.topology_peer;
    slot.peer_role = query.topology_peer_role;
    slot.group_id = query.topology_group_id;
    slot.relative_index = query.topology_relative_index;
    slot.local_virtual_index = query.topology_local_virtual_index;
    slot.peer_virtual_index = query.topology_peer_virtual_index;
    slot.axis_order = query.topology_axis_order;
    slot.delay_ms = query.topology_delay_ms;
    slot.hold_ms = query.topology_hold_ms;
    topology_stage_slot_seen_[query.topology_slot_index] = true;
    logf_("[%s][TOPO] stage_slot idx=%u enabled=%u rel=%d group=%u peer=%s\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          static_cast<unsigned int>(query.topology_slot_index),
          static_cast<unsigned int>(slot.enabled ? 1U : 0U),
          static_cast<int>(slot.relative_index),
          static_cast<unsigned int>(slot.group_id),
          macText_(slot.peer).c_str());
    return sendDescriptorAck_(from, corr_id, "topology slot staged");
  }

  if (query.type == DescriptorQueryType::TopologyStageGroupSet) {
    if (!topology_stage_active_ || topology_stage_owner_ != from) {
      return sendDescriptorError_(from, corr_id, "topology stage not active");
    }
    if (query.topology_group_slot >= EspNowManager::kTopologyMaxGroups) {
      return sendDescriptorError_(from, corr_id, "topology group index invalid");
    }
    auto& group = topology_stage_snapshot_.groups[query.topology_group_slot];
    group.enabled = query.topology_group_enabled;
    group.group_id = query.topology_group_id;
    group.seed = query.topology_group_seed;
    topology_stage_group_seen_[query.topology_group_slot] = true;
    logf_("[%s][TOPO] stage_group slot=%u enabled=%u gid=%u\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          static_cast<unsigned int>(query.topology_group_slot),
          static_cast<unsigned int>(group.enabled ? 1U : 0U),
          static_cast<unsigned int>(group.group_id));
    return sendDescriptorAck_(from, corr_id, "topology group staged");
  }

  if (query.type == DescriptorQueryType::TopologyStageFinalize) {
    if (!topology_stage_active_ || topology_stage_owner_ != from) {
      return sendDescriptorError_(from, corr_id, "topology stage not active");
    }
    std::string stage_error{};
    const bool ok = manager_->stageTopology(topology_stage_snapshot_, &stage_error);
    resetTopologyStageSession_();
    if (!ok) {
      if (stage_error.empty()) {
        stage_error = "topology stage failed";
      }
      logf_("[%s][TOPO] stage_finalize status=fail reason=%s\n",
            (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
            stage_error.c_str());
      return sendDescriptorError_(from, corr_id, stage_error);
    }
    logf_("[%s][TOPO] stage_finalize status=ok\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE");
    return sendDescriptorAck_(from, corr_id, "topology staged");
  }

  if (query.type == DescriptorQueryType::TopologyCommit) {
    logf_("[%s][TOPO] commit_start\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE");
    std::string commit_error{};
    if (!manager_->commitStagedTopology(&commit_error)) {
      if (commit_error.empty()) {
        commit_error = "topology commit failed";
      }
      logf_("[%s][TOPO] commit_fail reason=%s\n",
            (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
            commit_error.c_str());
      return sendDescriptorError_(from, corr_id, commit_error);
    }
    logf_("[%s][TOPO] commit_ok\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE");
    return sendDescriptorAck_(from, corr_id, "topology committed");
  }

  if (query.type == DescriptorQueryType::TopologyStatus) {
    EspNowManager::TopologyStatus status{};
    if (!manager_->getTopologyStatus(status)) {
      return sendDescriptorError_(from, corr_id, "topology status unavailable");
    }
    const uint8_t staged_state =
        static_cast<uint8_t>(status.has_staged ? status.staged.state : EspNowManager::TopologyState::None);
    const uint8_t committed_state =
        static_cast<uint8_t>(status.has_committed ? status.committed.state : EspNowManager::TopologyState::None);
    const uint8_t index_neg =
        status.has_committed ? status.committed.index_neg : (status.has_staged ? status.staged.index_neg : 0U);
    const uint8_t index_pos =
        status.has_committed ? status.committed.index_pos : (status.has_staged ? status.staged.index_pos : 0U);
    char staged_checksum_hex[11] = {0};
    char committed_checksum_hex[11] = {0};
    std::snprintf(staged_checksum_hex,
                  sizeof(staged_checksum_hex),
                  "0x%08lX",
                  static_cast<unsigned long>(status.has_staged ? status.staged.checksum : 0U));
    std::snprintf(committed_checksum_hex,
                  sizeof(committed_checksum_hex),
                  "0x%08lX",
                  static_cast<unsigned long>(status.has_committed ? status.committed.checksum : 0U));
    // Keep topology status ack compact to fit pull payload limits.
    // Include the verify-critical committed fields plus staged/committed presence bits.
    std::string message = "staged=" + std::string(status.has_staged ? "1" : "0") +
                          " committed=" + std::string(status.has_committed ? "1" : "0") +
                          " committed_ver=" + std::to_string(status.committed.topology_version) +
                          " committed_slots=" + std::to_string(static_cast<unsigned int>(status.committed.enabled_slot_count)) +
                          " committed_groups=" + std::to_string(static_cast<unsigned int>(status.committed.enabled_group_count)) +
                          " committed_state=" + std::to_string(static_cast<unsigned int>(committed_state)) +
                          " index_neg=" + std::to_string(static_cast<unsigned int>(index_neg)) +
                          " index_pos=" + std::to_string(static_cast<unsigned int>(index_pos)) +
                          " committed_checksum=" + std::string(committed_checksum_hex);
    (void)staged_state;
    (void)staged_checksum_hex;
    return sendDescriptorAck_(from, corr_id, message);
  }

  if (query.type == DescriptorQueryType::TopologyTriggerSend) {
    EspNowManager::TopologyTriggerRequest trigger{};
    trigger.target_index = query.topology_target_index;
    trigger.direction = query.topology_trigger_direction;
    trigger.delay_ms = query.topology_delay_ms;
    trigger.hold_ms = query.topology_hold_ms;
    trigger.source_virtual_index = query.topology_source_virtual_index;
    uint16_t seq = 0U;
    std::string trigger_error{};
    if (!manager_->sendTopologyTrigger(trigger, corr_id, &seq, &trigger_error)) {
      if (trigger_error.empty()) {
        trigger_error = "topology trigger failed";
      }
      return sendDescriptorError_(from, corr_id, trigger_error);
    }
    return sendDescriptorAck_(from, corr_id, "topology trigger seq=" + std::to_string(seq));
  }

  return sendDescriptorError_(from, corr_id, "topology unsupported query");
}

bool SlaveControlPlane::onPullRequest(const MacAddress& from,
                                      uint32_t corr_id,
                                      const uint8_t* payload,
                                      size_t len) {
  if (cfg_.verbose_pull_trace) {
    logf_("[%s] request corr=%lu from=%s len=%u\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          static_cast<unsigned long>(corr_id),
          macText_(from).c_str(),
          static_cast<unsigned int>(len));
  }

  DescriptorQuery query{};
  bool descriptor_parsed = false;
  if (descriptor_ != nullptr && manager_ != nullptr) {
    descriptor_parsed = manager_->decodeDescriptorQueryPayload(payload, len, query);
    if (!descriptor_parsed) {
      descriptor_parsed = parseDescriptorQuery(payload, len, query);
    }
  }

  if (descriptor_ != nullptr && manager_ != nullptr && descriptor_parsed) {
    bool topology_handled = false;
    if (handleTopologyQuery_(from, corr_id, query, topology_handled)) {
      if (topology_handled) {
        return true;
      }
    } else if (topology_handled) {
      return false;
    }

    if (query.type == DescriptorQueryType::GetNodeBundle) {
      DescriptorQuery chunk_query = query;
      const bool settings_requested = (chunk_query.bundle_mask & kNodeBundleMaskSettings) != 0U;
      if (settings_requested && !chunk_query.paged) {
        chunk_query.paged = true;
        chunk_query.cursor = 0U;
        chunk_query.page_size = 16U;
      }

      uint16_t cursor = chunk_query.cursor;
      bool first_chunk = true;
      static constexpr uint8_t kMaxBundleChunks = 32U;

      for (uint8_t chunk_idx = 0U; chunk_idx < kMaxBundleChunks; ++chunk_idx) {
        chunk_query.cursor = cursor;
        if (settings_requested && !first_chunk) {
          // First page carries full mask; follow-up pages carry settings only
          // to maximize settings throughput.
          chunk_query.bundle_mask = kNodeBundleMaskSettings;
        }

        DescriptorResponse response{};
        if (!manager_->executeDescriptorQuery(*descriptor_, chunk_query, response)) {
          response.type = DescriptorResponseType::Error;
          response.message = "descriptor handler failure";
        }

        std::vector<uint8_t> encoded{};
        if (!manager_->encodeDescriptorResponsePayload(response, encoded)) {
          return false;
        }
        if (!manager_->sendPullResponse(from, encoded.data(), encoded.size(), corr_id)) {
          return false;
        }

        DescriptorResponse emitted{};
        if (!manager_->decodeDescriptorResponsePayload(encoded.data(), encoded.size(), emitted)) {
          return true;
        }
        if (emitted.type != DescriptorResponseType::NodeBundle) {
          return true;
        }
        if (!emitted.is_paged || emitted.done) {
          return true;
        }
        if (emitted.next_cursor <= cursor) {
          DescriptorResponse err{};
          err.type = DescriptorResponseType::Error;
          err.message = "bundle paging stalled";
          std::vector<uint8_t> err_encoded{};
          if (manager_->encodeDescriptorResponsePayload(err, err_encoded)) {
            (void)manager_->sendPullResponse(from, err_encoded.data(), err_encoded.size(), corr_id);
          }
          return false;
        }

        cursor = emitted.next_cursor;
        first_chunk = false;
      }

      DescriptorResponse err{};
      err.type = DescriptorResponseType::Error;
      err.message = "bundle paging limit exceeded";
      std::vector<uint8_t> err_encoded{};
      if (manager_->encodeDescriptorResponsePayload(err, err_encoded)) {
        (void)manager_->sendPullResponse(from, err_encoded.data(), err_encoded.size(), corr_id);
      }
      return false;
    }

    DescriptorResponse response{};
    if (!manager_->executeDescriptorQuery(*descriptor_, query, response)) {
      response.type = DescriptorResponseType::Error;
      response.message = "descriptor handler failure";
    }

    std::vector<uint8_t> encoded;
    if (!manager_->encodeDescriptorResponsePayload(response, encoded)) {
      return false;
    }
    return manager_->sendPullResponse(from, encoded.data(), encoded.size(), corr_id);
  }

  uint16_t cmd = 0;
  if (manager_ == nullptr || !manager_->decodeControlCommandPayload(payload, len, cmd)) {
    if (manager_ != nullptr) {
      DescriptorResponse err{};
      err.type = DescriptorResponseType::Error;
      err.message = "unsupported pull payload";
      std::vector<uint8_t> encoded;
      if (manager_->encodeDescriptorResponsePayload(err, encoded)) {
        (void)manager_->sendPullResponse(from, encoded.data(), encoded.size(), corr_id);
      }
    }
    logf_("[%s][CTRL] unsupported payload corr=%lu from=%s len=%u\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          static_cast<unsigned long>(corr_id),
          macText_(from).c_str(),
          static_cast<unsigned int>(len));
    return false;
  }

  uint16_t result = 0x0000;
  if (cmd == kControlCmdRestart) {
    setFlag_(cfg_.restart_request_key);
    logf_("[%s][CTRL] restart requested by %s corr=%lu (flag set; main loop will reboot)\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          macText_(from).c_str(),
          static_cast<unsigned long>(corr_id));
  } else if (cmd == kControlCmdReset) {
    setFlag_(cfg_.reset_key);
    setFlag_(cfg_.restart_request_key);
    logf_("[%s][CTRL] reset requested by %s corr=%lu (flags set; reboot scheduled)\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          macText_(from).c_str(),
          static_cast<unsigned long>(corr_id));
  } else if (cmd == kControlCmdAudioPing) {
    const bool buzzer_enabled = readBoolSetting_(cfg_.audio_ping_buzzer_enable_key, true);
    const bool led_feedback_enabled = readBoolSetting_(cfg_.audio_ping_led_feedback_key, true);
    if (audio_ping_cb_ != nullptr) {
      audio_ping_cb_(buzzer_enabled, led_feedback_enabled, audio_ping_user_);
    }
    logf_("[%s][CTRL] audio ping by %s corr=%lu buzzer=%s led=%s\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          macText_(from).c_str(),
          static_cast<unsigned long>(corr_id),
          buzzer_enabled ? "on" : "off",
          led_feedback_enabled ? "on" : "off");
  } else {
    result = kControlResultUnsupported;
  }

  std::vector<uint8_t> resp;
  if (!manager_->encodeControlResultPayload(cmd, result, resp)) {
    return false;
  }
  return manager_->sendPullResponse(from, resp.data(), resp.size(), corr_id);
}

void SlaveRestartResetHook::bind(PreferencesStore* nvs,
                                 PairingStore* store,
                                 EspNowManager* manager,
                                 const MacAddress* local_mac) {
  nvs_ = nvs;
  store_ = store;
  manager_ = manager;
  local_mac_ = local_mac;
}

void SlaveRestartResetHook::setResetAppliedCallback(ResetAppliedCallback cb, void* user) {
  reset_cb_ = cb;
  reset_cb_user_ = user;
}

void SlaveRestartResetHook::logf_(const char* fmt, ...) const {
#if defined(ARDUINO)
  va_list args;
  va_start(args, fmt);
  char buf[240] = {0};
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
#else
  (void)fmt;
#endif
}

bool SlaveRestartResetHook::readAndClearFlag_(const char* key) {
  if (nvs_ == nullptr || key == nullptr || key[0] == '\0') {
    return false;
  }
  uint8_t flag = 0;
  if (!nvs_->getFixed(key, &flag, sizeof(flag)) || flag == 0U) {
    return false;
  }
  (void)nvs_->eraseFixed(key);
  return true;
}

bool SlaveRestartResetHook::applyResetIfRequested() {
  if (!readAndClearFlag_(cfg_.reset_key)) {
    return false;
  }

  bool cleared = false;
  if (manager_ != nullptr) {
    MacAddress runtime_peer{};
    if (manager_->getPairedPeer(runtime_peer)) {
      (void)manager_->removePeer(runtime_peer, true);
      cleared = true;
    }
  }

  if (local_mac_ != nullptr && store_ != nullptr) {
    std::vector<PairIndexEntry> entries{};
    if (store_->loadPairIndex(*local_mac_, entries)) {
      for (const auto& entry : entries) {
        if (!entry.valid) {
          continue;
        }
        (void)store_->erasePair(*local_mac_, entry.peer_mac);
        (void)store_->eraseChannel(*local_mac_, entry.peer_mac);
        cleared = true;
      }
      const std::vector<PairIndexEntry> empty_index{};
      (void)store_->savePairIndex(*local_mac_, empty_index);
    }
  }

  if (reset_cb_ != nullptr) {
    reset_cb_(reset_cb_user_);
  }

  logf_("[%s] reset flag processed: local link %s\n",
        (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
        cleared ? "cleared" : "already empty");
  return true;
}

void SlaveRestartResetHook::onRuntimeTick(uint32_t /*now_ms*/) {
  if (!readAndClearFlag_(cfg_.restart_request_key)) {
    return;
  }
  logf_("[%s] restart flag processed: reboot in %lums\n",
        (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
        static_cast<unsigned long>(cfg_.reboot_delay_ms));
#if defined(ARDUINO)
  if (cfg_.pre_reboot_hold_ms > 0U) {
    delay(cfg_.pre_reboot_hold_ms);
  }
#endif
  deepSleepRebootMs(cfg_.reboot_delay_ms);
}

const char* SlaveOtaTraceHook::stateName_(OtaTransferState state) {
  switch (state) {
    case OtaTransferState::Idle:
      return "idle";
    case OtaTransferState::Receiving:
      return "receiving";
    case OtaTransferState::Ready:
      return "ready";
    case OtaTransferState::Applying:
      return "applying";
    case OtaTransferState::Failed:
      return "failed";
    default:
      return "?";
  }
}

const char* SlaveOtaTraceHook::codeName_(OtaStatusCode code) {
  switch (code) {
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

void SlaveOtaTraceHook::logf_(const char* fmt, ...) const {
#if defined(ARDUINO)
  va_list args;
  va_start(args, fmt);
  char buf[280] = {0};
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
#else
  (void)fmt;
#endif
}

void SlaveOtaTraceHook::onRuntimeTick(uint32_t now_ms) {
  if (!cfg_.enabled || ota_manager_ == nullptr) {
    return;
  }

  if (static_cast<int32_t>(now_ms - last_poll_ms_) < 0) {
    last_poll_ms_ = now_ms;
  }
  if ((now_ms - last_poll_ms_) < cfg_.poll_interval_ms) {
    return;
  }
  last_poll_ms_ = now_ms;

  const OtaRuntimeStatus& s = ota_manager_->status();
  const bool state_changed = (s.state != last_state_) || (s.code != last_code_);
  const bool size_changed = (s.expected_size != last_expected_size_);
  if (state_changed || size_changed) {
    logf_("[%s][OTA][TRACE] state=%s(%u) code=%s(0x%04X) size=%lu/%lu crc=0x%08lX/0x%08lX\n",
          (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
          stateName_(s.state),
          static_cast<unsigned int>(s.state),
          codeName_(s.code),
          static_cast<unsigned int>(s.code),
          static_cast<unsigned long>(s.received_size),
          static_cast<unsigned long>(s.expected_size),
          static_cast<unsigned long>(s.actual_crc32),
          static_cast<unsigned long>(s.expected_crc32));

    if (!s.temp_path.empty() || !s.image_path.empty()) {
      logf_("[%s][OTA][TRACE] temp=%s image=%s\n",
            (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
            s.temp_path.empty() ? "-" : s.temp_path.c_str(),
            s.image_path.empty() ? "-" : s.image_path.c_str());
    }
    if (!s.message.empty()) {
      logf_("[%s][OTA][TRACE] note=%s\n",
            (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
            s.message.c_str());
    }
    next_progress_bytes_ = s.received_size + cfg_.progress_step_bytes;
  }

  if (s.state == OtaTransferState::Receiving) {
    if (s.received_size >= next_progress_bytes_ || s.received_size < last_received_size_) {
      const unsigned long pct =
          (s.expected_size == 0U)
              ? 0UL
              : static_cast<unsigned long>((static_cast<uint64_t>(s.received_size) * 100ULL) / s.expected_size);
      logf_("[%s][OTA][TRACE] rx=%lu/%lu (%lu%%)\n",
            (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
            static_cast<unsigned long>(s.received_size),
            static_cast<unsigned long>(s.expected_size),
            pct);
      next_progress_bytes_ = s.received_size + cfg_.progress_step_bytes;
    }
  }

  last_state_ = s.state;
  last_code_ = s.code;
  last_expected_size_ = s.expected_size;
  last_received_size_ = s.received_size;
}

bool SlaveLoggedTimeSink::setEpochSec(uint64_t epoch_s) {
#if defined(ARDUINO)
  struct timeval tv {};
  tv.tv_sec = static_cast<time_t>(epoch_s);
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    return false;
  }
#endif
  last_epoch_s_ = epoch_s;
#if defined(ARDUINO)
  if (cfg_.log_updates) {
    Serial.printf("[%s][TIME] sync applied epoch_s=%llu\n",
                  (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE",
                  static_cast<unsigned long long>(epoch_s));
  }
#endif
  return true;
}

int SlaveOtaVersionGate::compareVersion_(const std::string& lhs, const std::string& rhs) {
  auto tokenize = [](const std::string& v) -> std::vector<uint32_t> {
    std::vector<uint32_t> out;
    uint32_t cur = 0U;
    bool in_num = false;
    for (char c : v) {
      const unsigned char uc = static_cast<unsigned char>(c);
      if (std::isdigit(uc) != 0) {
        in_num = true;
        cur = (cur * 10U) + static_cast<uint32_t>(uc - static_cast<unsigned char>('0'));
      } else if (in_num) {
        out.push_back(cur);
        cur = 0U;
        in_num = false;
      }
    }
    if (in_num) {
      out.push_back(cur);
    }
    return out;
  };

  const std::vector<uint32_t> a = tokenize(lhs);
  const std::vector<uint32_t> b = tokenize(rhs);
  const size_t n = (a.size() > b.size()) ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i) {
    const uint32_t av = (i < a.size()) ? a[i] : 0U;
    const uint32_t bv = (i < b.size()) ? b[i] : 0U;
    if (av < bv) {
      return -1;
    }
    if (av > bv) {
      return 1;
    }
  }
  return 0;
}

OtaGateResult SlaveOtaVersionGate::prepareForTransfer(uint32_t image_size, uint32_t image_crc32) {
  (void)image_crc32;
  OtaGateResult out{};
  if (image_size == 0U) {
    out.decision = OtaGateDecision::Denied;
    out.message = "zero-sized image denied";
    return out;
  }
  out.decision = OtaGateDecision::Ready;
  out.message = "ready";
  return out;
}

OtaGateResult SlaveOtaVersionGate::prepareForTransferWithMetadata(uint32_t image_size,
                                                                  uint32_t image_crc32,
                                                                  const FirmwareImageMetadata* metadata) {
  OtaGateResult out = prepareForTransfer(image_size, image_crc32);
  if (out.decision != OtaGateDecision::Ready) {
    return out;
  }

  if (metadata == nullptr) {
    out.decision = OtaGateDecision::Denied;
    out.message = "missing fw metadata";
    return out;
  }
  if (cfg_.strict_role_check) {
    std::string target;
    target.reserve(metadata->target_role.size());
    for (char c : metadata->target_role) {
      if (std::isspace(static_cast<unsigned char>(c)) != 0) {
        continue;
      }
      target.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (target != "slave") {
      out.decision = OtaGateDecision::Denied;
      out.message = "metadata target_role must be slave";
      return out;
    }
  }

  if (!cfg_.require_newer_version) {
    return out;
  }

  if (metadata->sw_version.empty()) {
    out.decision = OtaGateDecision::Denied;
    out.message = "missing fw metadata version";
    return out;
  }

  if (!cfg_.current_sw_version.empty() &&
      compareVersion_(metadata->sw_version, cfg_.current_sw_version) <= 0) {
    out.decision = OtaGateDecision::Denied;
    out.message = "version not newer than running image";
    return out;
  }

  out.decision = OtaGateDecision::Ready;
  out.message = "ready";
  return out;
}

OtaGateResult SlaveOtaVersionGate::prepareForApply(const std::string& image_path,
                                                   uint32_t image_size,
                                                   uint32_t image_crc32) {
  (void)image_path;
  (void)image_crc32;
  OtaGateResult out{};
  if (image_size == 0U) {
    out.decision = OtaGateDecision::Denied;
    out.message = "invalid image";
    return out;
  }
  out.decision = OtaGateDecision::Ready;
  out.message = "ready";
  return out;
}

bool SlavePmsProviderBootstrap::begin(const MacAddress& local_mac, const Config& cfg) {
  cfg_ = cfg;
  local_mac_ = local_mac;
  provider_.reset();
  if (cfg_.nvs == nullptr) {
    return false;
  }
  provider_ = std::make_unique<PmsDescriptorProvider>(
      *cfg_.nvs, local_mac_, cfg_.time_sink, cfg_.storage, cfg_.ota, cfg_.provider_config);
  if (provider_ == nullptr) {
    return false;
  }
#if defined(ARDUINO)
  if (cfg_.print_report) {
    const char* prefix = (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE";
    Serial.printf("[%s][DESC] provider=%s sw=%s build=%s\n",
                  prefix,
                  cfg_.provider_config.device_type.c_str(),
                  cfg_.provider_config.sw_version.c_str(),
                  cfg_.provider_config.build_id.c_str());
  }
#endif
  return true;
}

bool SlavePmsProviderBootstrap::begin(const Config& cfg, MacAddress* out_local_mac) {
  MacAddress local_mac{};
#if defined(ESP_PLATFORM)
  esp_read_mac(local_mac.data(), ESP_MAC_WIFI_STA);
#else
  return false;
#endif
  if (out_local_mac != nullptr) {
    *out_local_mac = local_mac;
  }
  return begin(local_mac, cfg);
}

ManagerConfig SlaveNodeBootstrap::defaultManagerConfig(uint8_t channel,
                                                       const char* discovery_name,
                                                       bool auto_pair_on_discovery,
                                                       bool time_sync_enabled,
                                                       uint32_t time_sync_interval_ms) {
  ManagerConfig cfg{};
  cfg.local_role = Role::Slave;
  cfg.channel = channel;
  cfg.discovery_interval_ms = 700;
  cfg.discovery_name = discovery_name;
  cfg.auto_pair_on_discovery = auto_pair_on_discovery;
  cfg.time_sync_enabled = time_sync_enabled;
  cfg.time_sync_interval_ms = time_sync_interval_ms;
  cfg.time_sync_min_update_delta_s = 1;
  return cfg;
}

void SlaveNodeBootstrap::onResetAppliedStatic_(void* user) {
  if (user == nullptr) {
    return;
  }
  static_cast<SlaveNodeBootstrap*>(user)->onResetApplied_();
}

void SlaveNodeBootstrap::onResetApplied_() { events_.clearPeer(); }

bool SlaveNodeBootstrap::begin(const Config& cfg) {
  cfg_ = cfg;
  ready_ = false;
  has_local_mac_ = false;
  local_mac_ = {};

  manager_.reset();
  store_.reset();
  event_adapter_.reset();
  event_mux_.clearSinks();

  if (cfg_.transport == nullptr || cfg_.persistence == nullptr) {
    return false;
  }

  events_.configure(cfg_.event_config);
  control_.configure(cfg_.control_config);
  restart_hook_.configure(cfg_.restart_config);
  ota_trace_hook_.configure(cfg_.ota_trace_config);
  ota_trace_hook_.bind(cfg_.ota_manager_for_trace);

  event_adapter_ = std::make_unique<EventSinkAdapter<SlaveEventLogger>>(events_);
  if (event_adapter_ == nullptr) {
    return false;
  }
  event_mux_.addSink(event_adapter_.get());
  if (cfg_.extra_event_sink != nullptr) {
    event_mux_.addSink(cfg_.extra_event_sink);
  }

  ManagerConfig manager_cfg = cfg_.manager_config;
  manager_cfg.local_role = Role::Slave;  // hard guard: this bootstrap is slave-only
  store_ = std::make_unique<PairingStore>(manager_cfg.persistence, cfg_.persistence);
  manager_ = std::make_unique<EspNowManager>(manager_cfg,
                                             *cfg_.transport,
                                             store_.get(),
                                             &event_mux_,
                                             cfg_.hooks,
                                             cfg_.firmware_sink,
                                             &control_,
                                             nullptr,
                                             cfg_.time_sink,
                                             cfg_.telemetry_push,
                                             cfg_.logger);
  if (manager_ == nullptr) {
    return false;
  }

  cfg_.transport->bindManager(manager_.get());
  control_.bindManager(manager_.get());
  control_.bindNvs(cfg_.nvs);
  control_.bindDescriptor(cfg_.descriptor);

  restart_hook_.bind(cfg_.nvs, store_.get(), manager_.get(), &local_mac_);
  restart_hook_.setResetAppliedCallback(&SlaveNodeBootstrap::onResetAppliedStatic_, this);

  SlaveNodeRuntime::Config runtime_cfg{};
  runtime_cfg.manager = manager_.get();
  runtime_cfg.pre_tick = &restart_hook_;
  runtime_cfg.post_tick = &ota_trace_hook_;
  runtime_cfg.idle_delay_ms = cfg_.runtime_idle_delay_ms;
  if (!runtime_.begin(runtime_cfg)) {
    return false;
  }

  if (cfg_.local_profile_id == kProfileUnknown) {
    return false;
  }
  if (!manager_->setLocalProfile(cfg_.local_profile_id)) {
    return false;
  }
  if (cfg_.local_codec_id != 0U) {
    if (!manager_->setCodecById(cfg_.local_codec_id)) {
      return false;
    }
  }

  ready_ = true;
  return true;
}

void SlaveNodeBootstrap::tick(uint32_t now_ms) {
  if (!ready_) {
    return;
  }
  runtime_.tick(now_ms);
}

void SlaveNodeBootstrap::loop() {
  if (!ready_) {
    return;
  }
  runtime_.loop();
}

bool SlaveNodeBootstrap::bringUp(const MacAddress& local_mac,
                                 bool restore_link,
                                 bool apply_reset_flag,
                                 MacAddress* out_restored_peer) {
  if (!ready_ || manager_ == nullptr) {
    return false;
  }

  local_mac_ = local_mac;
  has_local_mac_ = true;

  if (out_restored_peer != nullptr) {
    *out_restored_peer = MacAddress{};
  }

  if (!manager_->begin(local_mac_)) {
    return false;
  }

#if defined(ARDUINO)
  if (cfg_.print_startup_summary) {
    const char* prefix = (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE";
    Serial.printf("[%s] profile=%s (%u) codec=%s (%u)\n",
                  prefix,
                  manager_->localProfileName(),
                  static_cast<unsigned int>(manager_->localProfileId()),
                  manager_->codec().codecName(),
                  static_cast<unsigned int>(manager_->codec().codecId()));
    Serial.printf("[%s] local mac=%s\n", prefix, macToString(local_mac_).c_str());
  }
#endif

  if (apply_reset_flag) {
    (void)restart_hook_.applyResetIfRequested();
  }

  if (!restore_link) {
    return true;
  }

  const bool restored = manager_->restore();
  if (!restored) {
    return true;
  }

  MacAddress peer{};
  if (manager_->getPairedPeer(peer)) {
    if (out_restored_peer != nullptr) {
      *out_restored_peer = peer;
    }
    // Presence announce on slave startup/reboot so master can recover offline->online state.
    (void)manager_->publishMandatoryEvent(kPresencePingEventId, 1U, 0, 0U);
#if defined(ARDUINO)
    if (cfg_.print_startup_summary) {
      const char* prefix = (cfg_.log_prefix != nullptr) ? cfg_.log_prefix : "SLAVE";
      Serial.printf("[%s] restored paired link with %s\n", prefix, macToString(peer).c_str());
      Serial.printf("[%s][LIVE] startup presence_ping queued (event=0x%04X)\n",
                    prefix,
                    static_cast<unsigned int>(kPresencePingEventId));
    }
#endif
  }
  return true;
}

bool SlaveNodeBootstrap::bringUp(bool restore_link,
                                 bool apply_reset_flag,
                                 MacAddress* out_local_mac,
                                 MacAddress* out_restored_peer) {
  MacAddress local_mac{};
#if defined(ESP_PLATFORM)
  esp_read_mac(local_mac.data(), ESP_MAC_WIFI_STA);
#else
  return false;
#endif
  if (out_local_mac != nullptr) {
    *out_local_mac = local_mac;
  }
  return bringUp(local_mac, restore_link, apply_reset_flag, out_restored_peer);
}

}  // namespace espnow_link
