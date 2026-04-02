#pragma once

#include <string>
#include <vector>

#include "profile_catalog/shared/build_id.hpp"
#include "profile_catalog/shared/profile_ids.hpp"
#include "profile_catalog/shared/schema_package.hpp"
#include "profile_catalog/slaves/remu/remu_keys.hpp"
#include "espnow_link/address.hpp"
#include "espnow_link/descriptor.hpp"
#include "espnow_link/ota_descriptor_adapter.hpp"
#include "espnow_link/preferences_store.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/telemetry_push.hpp"
#include "espnow_link/time.hpp"

namespace app_owned {

struct RemuRuntimeTelemetrySnapshot {
  uint32_t relay_bitmap = 0U;
  uint32_t relay_count = 0U;
  float env_temp_c = 0.0f;
  bool valid = false;
};

using RemuReadTelemetryCallback = bool (*)(void* user, RemuRuntimeTelemetrySnapshot& out);
using RemuApplySettingCallback = bool (*)(void* user,
                                          const std::string& key,
                                          const std::string& value,
                                          std::string& out_message);
using RemuSettingFeedbackCallback = void (*)(void* user,
                                             const std::string& key,
                                             const std::string& value,
                                             bool ok);

struct RemuDescriptorAppConfig {
  std::string device_type = PCAT_REMU_DEV_TYPE;
  std::string hw_version = PCAT_REMU_HW_VER;
  std::string sw_version = PCAT_REMU_SW_VER;
  std::string build_id = buildid::defaultBuildId(PCAT_REMU_BUILD_ID, PCAT_REMU_BUILD_TAG, PCAT_REMU_SW_VER);
  std::string default_device_name = PCAT_REMU_DEF_NAME;
  void* runtime_user = nullptr;
  RemuReadTelemetryCallback read_telemetry = nullptr;
  RemuApplySettingCallback apply_setting = nullptr;
  RemuSettingFeedbackCallback setting_feedback = nullptr;
};

class RemuAppDescriptorProvider : public espnow_link::IDescriptorProvider, public espnow_link::ITelemetryPushProvider {
 public:
  RemuAppDescriptorProvider(espnow_link::PreferencesStore& nvs,
                            const espnow_link::MacAddress& local_mac,
                            espnow_link::ITimeSink* time_sink,
                            espnow_link::IStorageExplorerProvider* storage,
                            espnow_link::OtaDescriptorAdapter* ota,
                            const RemuDescriptorAppConfig& cfg = {});

  bool getDeviceDescriptor(espnow_link::DeviceDescriptor& out) override;
  bool getCapabilities(std::vector<espnow_link::CapabilityDescriptor>& out) override;
  bool getTelemetrySchema(std::vector<espnow_link::TelemetryDescriptor>& out) override;
  bool getTelemetrySnapshot(std::vector<espnow_link::TelemetrySample>& out) override;
  bool getLiveness(espnow_link::LivenessStatus& out) override;
  bool getTime(espnow_link::TimeStatus& out) override;
  bool setTime(uint64_t epoch_s, std::string& out_message) override;
  bool getSettings(std::vector<espnow_link::SettingDescriptor>& out) override;
  bool getSetting(const std::string& key, espnow_link::SettingDescriptor& out) override;
  bool getSettingById(uint16_t setting_id, espnow_link::SettingDescriptor& out) override;
  bool setSetting(const std::string& key, const std::string& value, std::string& out_message) override;
  bool setSettingById(uint16_t setting_id, const std::string& value, std::string& out_message) override;

  bool getStorageInfo(espnow_link::StorageInfo& out, std::string& out_message) override;
  bool listStoragePath(const std::string& path,
                       std::string& out_canonical_path,
                       std::string& out_parent_path,
                       std::vector<espnow_link::StorageEntry>& out_entries,
                       std::string& out_message) override;
  bool statStoragePath(const std::string& path, espnow_link::StorageStat& out, std::string& out_message) override;
  bool formatStorage(std::string& out_message) override;

  bool getOtaStatus(espnow_link::OtaStatusInfo& out, std::string& out_message) override;
  bool getOtaManifest(std::vector<espnow_link::OtaManifestEntry>& out, std::string& out_message) override;
  bool rebuildOtaManifest(std::string& out_message) override;
  bool clearOtaScope(const std::string& scope, std::string& out_message) override;
  bool getOtaCapacity(espnow_link::OtaCapacityInfo& out, std::string& out_message) override;
  bool getOtaGateInfo(espnow_link::OtaGateInfo& out, std::string& out_message) override;
 bool applyOtaImage(const std::string& target, std::string& out_message) override;

 private:
  bool finalizeSettingChange_(const std::string& key,
                              const std::string& value,
                              bool persisted_ok,
                              std::string& out_message);
  bool ensureSettingsCache_() const;
  bool rebuildSettingsCache_(std::vector<espnow_link::SettingDescriptor>& out) const;
  void invalidateSettingsCache_();
  bool appendTelemetryFromRuntime_(std::vector<espnow_link::TelemetrySample>& out);
  uint32_t loadU32_(const char* key, uint32_t fallback) const;
  bool loadBool_(const char* key, bool fallback) const;
  float loadFloat_(const char* key, float fallback) const;
  std::string loadString_(const char* key, const char* fallback) const;
  std::string formatFloat_(float value) const;

  espnow_link::PreferencesStore& nvs_;
  const espnow_link::MacAddress& local_mac_;
  espnow_link::ITimeSink* time_sink_ = nullptr;
  espnow_link::IStorageExplorerProvider* storage_ = nullptr;
  espnow_link::OtaDescriptorAdapter* ota_ = nullptr;
  RemuDescriptorAppConfig cfg_{};
  mutable std::vector<espnow_link::SettingDescriptor> settings_cache_{};
  mutable bool settings_cache_valid_ = false;
};

class RemuAppProfileDefinition final : public espnow_link::IProfileDefinition {
 public:
  espnow_link::ProfileId profileId() const override;
  const char* profileName() const override;
  espnow_link::CodecId defaultCodecId() const override;
  bool supportsCodec(espnow_link::CodecId codec_id) const override;
  const std::vector<espnow_link::ProfileTelemetryMetricSpec>& telemetryMetrics() const override;
  const std::vector<espnow_link::ProfileSettingSpec>& settings() const override;
  const std::vector<espnow_link::ProfileEventSpec>& events() const override;
};

const RemuAppProfileDefinition& remuProfileDefinition();

SlaveSchemaPackage makeRemuSlaveSchemaPackage(RemuAppDescriptorProvider& provider,
                                              espnow_link::CodecId codec_id = espnow_link::kCodecIdCompactIndexed);

}  // namespace app_owned
