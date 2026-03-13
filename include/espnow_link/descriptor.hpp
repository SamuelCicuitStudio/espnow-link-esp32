#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include "espnow_link/types.hpp"

namespace espnow_link {

class IProfileDefinition;
class LibraryLogger;

/** @brief Device identity/version metadata returned by descriptor query. */
struct DeviceDescriptor {
  std::string device_type;
  std::string device_id;
  std::string device_name;
  std::string hw_version;
  std::string sw_version;
  std::string build_id;
};

/** @brief Generic key/value capability entry. */
struct CapabilityDescriptor {
  std::string key;
  std::string description;
};

/** @brief Telemetry schema entry describing one metric. */
struct TelemetryDescriptor {
  uint16_t metric_id = 0;
  std::string key;
  std::string unit;
  float min_value = 0.0f;
  float max_value = 0.0f;
  std::string description;

  TelemetryDescriptor() = default;
  TelemetryDescriptor(std::string in_key, std::string in_unit, float in_min, float in_max, std::string in_desc)
      : key(std::move(in_key)),
        unit(std::move(in_unit)),
        min_value(in_min),
        max_value(in_max),
        description(std::move(in_desc)) {}
};

/** @brief Telemetry sample value for one metric at one time. */
struct TelemetrySample {
  uint16_t metric_id = 0;
  std::string key;
  std::string value;
  std::string unit;

  TelemetrySample() = default;
  TelemetrySample(std::string in_key, std::string in_value, std::string in_unit)
      : key(std::move(in_key)), value(std::move(in_value)), unit(std::move(in_unit)) {}
};

/** @brief Liveness snapshot reported by a slave. */
struct LivenessStatus {
  bool online = true;
  uint32_t uptime_ms = 0;
  std::string state;
};

/** @brief Time snapshot (epoch + uptime) reported by a node. */
struct TimeStatus {
  uint64_t epoch_s = 0;
  uint32_t uptime_ms = 0;
};

/** @brief Value type of a configurable setting. */
enum class SettingValueType : uint8_t {
  String = 0,
  Int = 1,
  Float = 2,
  Bool = 3,
};

/** @brief Metadata and current/default values for one setting. */
struct SettingDescriptor {
  uint16_t setting_id = 0;
  std::string key;
  SettingValueType value_type = SettingValueType::String;
  bool writable = false;
  std::string nvs_key;
  std::string current_value;
  std::string default_value;
  std::string description;

  SettingDescriptor() = default;
};

/** @brief Active backend mode used by storage explorer operations. */
enum class StorageBackendMode : uint8_t {
  Unknown = 0,
  Disabled = 1,
  Sd = 2,
  Spiffs = 3,
};

/** @brief Storage backend runtime snapshot. */
struct StorageInfo {
  StorageBackendMode mode = StorageBackendMode::Unknown;
  bool available = false;
  bool mounted = false;
  uint32_t total_bytes = 0;
  uint32_t used_bytes = 0;
  uint32_t free_bytes = 0;
  std::string root_path = "/";
  std::string cwd = "/";
};

/** @brief One directory entry returned by storage list operation. */
struct StorageEntry {
  std::string name;
  bool is_dir = false;
  uint32_t size_bytes = 0;
};

/** @brief Path stat snapshot for one file or directory. */
struct StorageStat {
  std::string path;
  bool exists = false;
  bool is_dir = false;
  uint32_t size_bytes = 0;
};

/** @brief One OTA manifest image entry. */
struct OtaManifestEntry {
  uint32_t file_id = 0;
  std::string file_name;
  uint32_t size_bytes = 0;
  uint32_t crc32 = 0;
  std::string version;
  std::string build_id;
  uint32_t created_epoch_s = 0;
  std::string state;
  uint32_t required_app_bytes = 0;
};

/** @brief OTA runtime status snapshot. */
struct OtaStatusInfo {
  uint8_t transfer_state = 0;
  uint16_t status_code = 0;
  uint32_t expected_size = 0;
  uint32_t received_size = 0;
  uint32_t expected_crc32 = 0;
  uint32_t actual_crc32 = 0;
  std::string temp_path;
  std::string image_path;
  std::string persistent_state;
  uint32_t persistent_epoch_s = 0;
  std::string confirmed_sw_version;
  std::string confirmed_build_id;
};

/** @brief OTA capacity info used for preflight validation. */
struct OtaCapacityInfo {
  uint32_t max_fw_bytes = 0;
  uint32_t last_checked_image_bytes = 0;
  bool last_fit = true;
};

/** @brief Last OTA gate decision info. */
struct OtaGateInfo {
  uint8_t decision = 0;
  std::string detail;
};

/**
 * @brief Optional storage explorer provider used by descriptor/CLI paths.
 *
 * Implementations are expected to be app-owned adapters bound to mounted storage (SD/SPIFFS).
 */
class IStorageExplorerProvider {
 public:
  virtual ~IStorageExplorerProvider() = default;

  /** @brief Query active storage backend mode and usage stats. */
  virtual bool getStorageInfo(StorageInfo& out, std::string& out_message) {
    (void)out;
    out_message = "storage explorer not supported";
    return false;
  }

  /** @brief List directory entries for provided absolute/relative path. */
  virtual bool listStoragePath(const std::string& path,
                               std::string& out_canonical_path,
                               std::string& out_parent_path,
                               std::vector<StorageEntry>& out_entries,
                               std::string& out_message) {
    (void)path;
    out_entries.clear();
    out_canonical_path.clear();
    out_parent_path.clear();
    out_message = "storage explorer not supported";
    return false;
  }

  /** @brief Stat one path and return existence/type/size metadata. */
  virtual bool statStoragePath(const std::string& path, StorageStat& out, std::string& out_message) {
    (void)path;
    out = StorageStat{};
    out_message = "storage explorer not supported";
    return false;
  }

  /** @brief Format active storage backend and recreate library-owned layout. */
  virtual bool formatStorage(std::string& out_message) {
    out_message = "storage format not supported";
    return false;
  }

  /** @brief Get OTA transfer/runtime status for this node. */
  virtual bool getOtaStatus(OtaStatusInfo& out, std::string& out_message) {
    (void)out;
    out_message = "ota not supported";
    return false;
  }

  /** @brief Get OTA manifest image entries. */
  virtual bool getOtaManifest(std::vector<OtaManifestEntry>& out, std::string& out_message) {
    out.clear();
    out_message = "ota not supported";
    return false;
  }

  /** @brief Rebuild OTA manifest from storage scan. */
  virtual bool rebuildOtaManifest(std::string& out_message) {
    out_message = "ota not supported";
    return false;
  }

  /** @brief Clear OTA storage scope (`in|img|man|all`). */
  virtual bool clearOtaScope(const std::string& scope, std::string& out_message) {
    (void)scope;
    out_message = "ota not supported";
    return false;
  }

  /** @brief Query OTA max image capacity and last preflight check. */
  virtual bool getOtaCapacity(OtaCapacityInfo& out, std::string& out_message) {
    (void)out;
    out_message = "ota not supported";
    return false;
  }

  /** @brief Query last OTA gate decision/status. */
  virtual bool getOtaGateInfo(OtaGateInfo& out, std::string& out_message) {
    (void)out;
    out_message = "ota not supported";
    return false;
  }

  /** @brief Apply OTA image identified by file name or ID string. */
  virtual bool applyOtaImage(const std::string& target, std::string& out_message) {
    (void)target;
    out_message = "ota not supported";
    return false;
  }
};

/**
 * @brief Descriptor/settings/telemetry provider implemented by device application layer.
 *
 * Manager uses this provider to answer pull descriptor requests from peers.
 */
class IDescriptorProvider : public IStorageExplorerProvider {
 public:
  virtual ~IDescriptorProvider() = default;

  /** @brief Get static and runtime device identity data. */
  virtual bool getDeviceDescriptor(DeviceDescriptor& out) = 0;
  /** @brief Get capability list supported by this node. */
  virtual bool getCapabilities(std::vector<CapabilityDescriptor>& out) = 0;
  /** @brief Get telemetry schema list supported by this node. */
  virtual bool getTelemetrySchema(std::vector<TelemetryDescriptor>& out) = 0;
  /** @brief Get current telemetry values snapshot. */
  virtual bool getTelemetrySnapshot(std::vector<TelemetrySample>& out) = 0;
  /** @brief Get liveness status of this node. */
  virtual bool getLiveness(LivenessStatus& out) = 0;
  /** @brief Get node time status. */
  virtual bool getTime(TimeStatus& out) = 0;
  /** @brief Apply new node epoch time. */
  virtual bool setTime(uint64_t epoch_s, std::string& out_message) = 0;
  /** @brief Get all settings schema and values. */
  virtual bool getSettings(std::vector<SettingDescriptor>& out) = 0;
  /** @brief Get one setting by key. */
  virtual bool getSetting(const std::string& key, SettingDescriptor& out) = 0;
  /** @brief Set one setting by key. */
  virtual bool setSetting(const std::string& key, const std::string& value, std::string& out_message) = 0;

  /** @brief Get one setting by numeric ID. */
  virtual bool getSettingById(uint16_t setting_id, SettingDescriptor& out) = 0;
  /** @brief Set one setting by numeric ID. */
  virtual bool setSettingById(uint16_t setting_id, const std::string& value, std::string& out_message) = 0;

  /**
   * @brief Optional policy hook before clearing logger storage.
   * @param out_message Optional deny reason.
   * @return true to allow operation.
   */
  virtual bool authorizeLoggerClear(std::string& out_message) {
    out_message.clear();
    return true;
  }

  /**
   * @brief Optional policy hook before enabling/disabling logger storage.
   * @param enable Requested logger enabled flag.
   * @param out_message Optional deny reason.
   * @return true to allow operation.
   */
  virtual bool authorizeLoggerSetEnabled(bool enable, std::string& out_message) {
    (void)enable;
    out_message.clear();
    return true;
  }
};

/** @brief Descriptor request type sent over pull channel. */
enum class DescriptorQueryType : uint8_t {
  Unknown = 0,
  GetDevice,
  GetCapabilities,
  GetTelemetry,
  PullTelemetry,
  GetLiveness,
  GetTime,
  SetTime,
  GetSettings,
  GetSetting,
  SetSetting,
  GetLogStatus,
  ReadLogChunk,
  ClearLog,
  SetLogControl,
  GetStorageInfo,
  ListStoragePath,
  StatStoragePath,
  FormatStorage,
  GetOtaStatus,
  GetOtaManifest,
  RebuildOtaManifest,
  ClearOtaScope,
  GetOtaCapacity,
  GetOtaGateInfo,
  ApplyOtaImage,
  TopologyStageClear,
  TopologyStageBegin,
  TopologyStageSlotSet,
  TopologyStageGroupSet,
  TopologyStageFinalize,
  TopologyCommit,
  TopologyStatus,
  TopologyTriggerSend,
};

/** @brief Encoded descriptor query object. */
struct DescriptorQuery {
  DescriptorQueryType type = DescriptorQueryType::Unknown;
  std::string key;
  bool has_setting_id = false;
  uint16_t setting_id = 0;
  std::string value;
  uint64_t time_epoch_s = 0;
  bool paged = false;
  uint16_t cursor = 0;
  uint8_t page_size = 0;
  uint32_t log_offset = 0;
  uint16_t log_max_bytes = 0;
  bool has_log_enable = false;
  bool log_enable = false;
  std::string storage_path;
  std::string ota_scope;
  std::string ota_target;
  uint8_t topology_schema_version = 1;
  uint32_t topology_version = 0;
  uint8_t topology_index_neg = 0;
  uint8_t topology_index_pos = 0;
  uint8_t topology_slot_index = 0;
  bool topology_slot_enabled = false;
  MacAddress topology_peer{};
  uint8_t topology_peer_role = 0;
  uint8_t topology_group_id = 0;
  int8_t topology_relative_index = 0;
  uint8_t topology_local_virtual_index = 0xFF;
  uint8_t topology_peer_virtual_index = 0xFF;
  int8_t topology_axis_order = 0;
  uint16_t topology_delay_ms = 0;
  uint16_t topology_hold_ms = 0;
  uint8_t topology_group_slot = 0;
  bool topology_group_enabled = false;
  std::array<uint8_t, 32> topology_group_seed{};
  int8_t topology_target_index = 0;
  uint8_t topology_trigger_direction = 0;
  uint8_t topology_source_virtual_index = 0xFF;
};

/** @brief Descriptor response payload category. */
enum class DescriptorResponseType : uint8_t {
  Unknown = 0,
  Device,
  Capabilities,
  Telemetry,
  TelemetrySnapshot,
  Liveness,
  Time,
  Settings,
  Setting,
  LogStatus,
  LogChunk,
  StorageInfo,
  StorageList,
  StorageStat,
  Ack,
  Error,
  OtaStatus,
  OtaManifest,
  OtaCapacity,
  OtaGateInfo,
};

/** @brief Decoded descriptor response object. */
struct DescriptorResponse {
  DescriptorResponseType type = DescriptorResponseType::Unknown;
  std::string message;
  bool is_paged = false;
  uint32_t snapshot_id = 0;
  uint16_t total_count = 0;
  uint16_t cursor = 0;
  uint16_t returned_count = 0;
  uint16_t next_cursor = 0;
  bool done = false;
  DeviceDescriptor device;
  std::vector<CapabilityDescriptor> capabilities;
  std::vector<TelemetryDescriptor> telemetry;
  std::vector<TelemetrySample> telemetry_samples;
  LivenessStatus liveness;
  TimeStatus time;
  std::vector<SettingDescriptor> settings;
  SettingDescriptor setting;
  bool logger_available = false;
  bool logger_enabled = false;
  uint8_t logger_min_level = 0;
  uint32_t log_bytes_used = 0;
  uint32_t log_bytes_dropped = 0;
  uint32_t log_records_appended = 0;
  uint32_t log_rotations = 0;
  uint32_t log_total_size = 0;
  uint32_t log_chunk_offset = 0;
  std::vector<uint8_t> log_chunk;
  StorageInfo storage_info;
  std::string storage_path;
  std::string storage_parent_path;
  std::vector<StorageEntry> storage_entries;
  StorageStat storage_stat;
  OtaStatusInfo ota_status;
  std::vector<OtaManifestEntry> ota_manifest;
  OtaCapacityInfo ota_capacity;
  OtaGateInfo ota_gate;
};

/**
 * @brief Encode descriptor query to wire payload.
 * @param query Input query object.
 * @param out_payload Output bytes.
 * @return true on success.
 */
bool encodeDescriptorQuery(const DescriptorQuery& query, std::vector<uint8_t>& out_payload);

/**
 * @brief Decode descriptor query from wire payload.
 * @param payload Input bytes.
 * @param len Input length in bytes.
 * @param out Output query object.
 * @return true on success.
 */
bool parseDescriptorQuery(const uint8_t* payload, size_t len, DescriptorQuery& out);

/**
 * @brief Execute a descriptor query against provider/profile.
 * @param provider Descriptor provider implementation.
 * @param query Query to execute.
 * @param out Response object.
 * @param profile Optional active profile for ID/key alignment and schema-first behavior.
 * @return true if query was handled.
 */
bool handleDescriptorQuery(IDescriptorProvider& provider,
                           const DescriptorQuery& query,
                           DescriptorResponse& out,
                           const IProfileDefinition* profile = nullptr,
                           LibraryLogger* logger = nullptr);

/**
 * @brief Encode descriptor response as TLV string payload.
 * @param response Response object.
 * @param out_payload Output payload string (binary-safe).
 * @return true on success.
 */
bool encodeDescriptorResponse(const DescriptorResponse& response, std::string& out_payload);

/**
 * @brief Decode descriptor response from wire payload.
 * @param payload Input bytes.
 * @param len Input length in bytes.
 * @param out Output response object.
 * @return true on success.
 */
bool decodeDescriptorResponse(const uint8_t* payload, size_t len, DescriptorResponse& out);

}  // namespace espnow_link








