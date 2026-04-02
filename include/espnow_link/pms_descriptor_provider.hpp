#pragma once

#include <string>
#include <vector>

#include "espnow_link/address.hpp"
#include "espnow_link/descriptor.hpp"
#include "espnow_link/ota_descriptor_adapter.hpp"
#include "espnow_link/preferences_store.hpp"
#include "espnow_link/telemetry_push.hpp"
#include "espnow_link/time.hpp"
#include "espnow_link/types.hpp"

namespace espnow_link {

/**
 * @brief Config for PMS descriptor provider identity/version fields.
 */
struct PmsDescriptorProviderConfig {
  std::string device_type = "PMS";
  std::string hw_version = "PMS-HW1";
  std::string sw_version = "0.1.0";
  std::string build_id;
  std::string default_device_name = "PMS-Node";
};

/**
 * @brief PMS one-slave descriptor/settings/telemetry provider used by examples.
 *
 * This class keeps the PMS example descriptor logic inside the library so
 * slave `main.cpp` stays focused on board wiring and boot sequence.
 */
class PmsDescriptorProvider : public IDescriptorProvider, public ITelemetryPushProvider {
 public:
  PmsDescriptorProvider(PreferencesStore& nvs,
                        const MacAddress& local_mac,
                        ITimeSink* time_sink,
                        IStorageExplorerProvider* storage,
                        OtaDescriptorAdapter* ota,
                        const PmsDescriptorProviderConfig& cfg = {});

  bool getDeviceDescriptor(DeviceDescriptor& out) override;
  bool getCapabilities(std::vector<CapabilityDescriptor>& out) override;
  bool getTelemetrySchema(std::vector<TelemetryDescriptor>& out) override;
  bool getTelemetrySnapshot(std::vector<TelemetrySample>& out) override;
  bool getLiveness(LivenessStatus& out) override;
  bool getTime(TimeStatus& out) override;
  bool setTime(uint64_t epoch_s, std::string& out_message) override;
  bool getSettings(std::vector<SettingDescriptor>& out) override;
  bool getSetting(const std::string& key, SettingDescriptor& out) override;
  bool getSettingById(uint16_t setting_id, SettingDescriptor& out) override;
  bool setSetting(const std::string& key, const std::string& value, std::string& out_message) override;
  bool setSettingById(uint16_t setting_id, const std::string& value, std::string& out_message) override;
  bool authorizeLoggerClear(std::string& out_message) override;
  bool authorizeLoggerSetEnabled(bool enable, std::string& out_message) override;

  bool getStorageInfo(StorageInfo& out, std::string& out_message) override;
  bool listStoragePath(const std::string& path,
                       std::string& out_canonical_path,
                       std::string& out_parent_path,
                       std::vector<StorageEntry>& out_entries,
                       std::string& out_message) override;
  bool statStoragePath(const std::string& path, StorageStat& out, std::string& out_message) override;
  bool formatStorage(std::string& out_message) override;

  bool getOtaStatus(OtaStatusInfo& out, std::string& out_message) override;
  bool getOtaManifest(std::vector<OtaManifestEntry>& out, std::string& out_message) override;
  bool rebuildOtaManifest(std::string& out_message) override;
  bool clearOtaScope(const std::string& scope, std::string& out_message) override;
  bool getOtaCapacity(OtaCapacityInfo& out, std::string& out_message) override;
  bool getOtaGateInfo(OtaGateInfo& out, std::string& out_message) override;
  bool applyOtaImage(const std::string& target, std::string& out_message) override;

 private:
  std::string formatFloat_(float v) const;
  std::string loadName_() const;
  uint16_t loadU16_(const char* key, uint16_t fallback) const;
  std::string loadString_(const char* key, const char* fallback) const;
  float loadFloat_(const char* key, float fallback) const;
  bool ensureSettingsCache_() const;
  bool rebuildSettingsCache_(std::vector<SettingDescriptor>& out) const;
  void invalidateSettingsCache_();

  static constexpr const char* kSettingNameKey = "cfg_name";
  static constexpr const char* kSettingChannelKey = "cfg_ch";
  static constexpr const char* kSettingPwrModeKey = "cfg_pwr_mode";
  static constexpr const char* kSettingTripLimitCurrentKey = "cfg_trip_i";
  static constexpr const char* kSettingVCalFactorKey = "cfg_v_cal";
  static constexpr const char* kSettingICalFactorKey = "cfg_i_cal";
  static constexpr const char* kSettingVbusOvpMvKey = "cfg_vbus_ovp";
  static constexpr const char* kSettingVbusUvpMvKey = "cfg_vbus_uvp";
  static constexpr const char* kSettingIbusOcpMaKey = "cfg_ibus_ocp";
  static constexpr const char* kSettingVbatOvpMvKey = "cfg_vbat_ovp";
  static constexpr const char* kSettingVbatUvpMvKey = "cfg_vbat_uvp";
  static constexpr const char* kSettingIbatOcpMaKey = "cfg_ibat_ocp";
  static constexpr uint16_t kSettingIdDeviceName = 0x0001;
  static constexpr uint16_t kSettingIdChannel = 0x0002;
  static constexpr uint16_t kSettingIdPwrMode = 0x0101;
  static constexpr uint16_t kSettingIdTripLimitCurrent = 0x0102;
  static constexpr uint16_t kSettingIdVCalFactor = 0x0103;
  static constexpr uint16_t kSettingIdICalFactor = 0x0104;
  static constexpr uint16_t kSettingIdVbusOvpMv = 0x0201;
  static constexpr uint16_t kSettingIdVbusUvpMv = 0x0202;
  static constexpr uint16_t kSettingIdIbusOcpMa = 0x0203;
  static constexpr uint16_t kSettingIdVbatOvpMv = 0x0204;
  static constexpr uint16_t kSettingIdVbatUvpMv = 0x0205;
  static constexpr uint16_t kSettingIdIbatOcpMa = 0x0206;

  bool settingIdFromKey_(const std::string& key, uint16_t& out_id) const;
  bool settingKeyFromId_(uint16_t setting_id, const char*& out_key) const;

  PreferencesStore& nvs_;
  const MacAddress& local_mac_;
  ITimeSink* time_sink_ = nullptr;
  IStorageExplorerProvider* storage_ = nullptr;
  OtaDescriptorAdapter* ota_ = nullptr;
  PmsDescriptorProviderConfig cfg_{};
  mutable std::vector<SettingDescriptor> settings_cache_{};
  mutable bool settings_cache_valid_ = false;
};

}  // namespace espnow_link
