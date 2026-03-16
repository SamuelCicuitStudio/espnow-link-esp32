#include "espnow_link/profile.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace espnow_link {

namespace {

class PmsProfile final : public IProfileDefinition {
 public:
  ProfileId profileId() const override { return kProfilePms; }
  const char* profileName() const override { return "PMS"; }
  CodecId defaultCodecId() const override { return kCodecIdDefault; }
  bool supportsCodec(CodecId codec_id) const override {
    return isBuiltInCodecId(codec_id);
  }
  const std::vector<ProfileTelemetryMetricSpec>& telemetryMetrics() const override { return telemetry_; }
  const std::vector<ProfileSettingSpec>& settings() const override { return settings_; }
  const std::vector<ProfileEventSpec>& events() const override { return events_; }

 private:
  const std::vector<ProfileTelemetryMetricSpec> telemetry_ = {
      {0x01, "wall_v"},
      {0x02, "batt_v"},
      {0x03, "wall_i"},
      {0x04, "batt_i"},
      {0x05, "power_source"},
      {0x06, "trip"},
      {0x07, "relay_cut"},
  };

  const std::vector<ProfileSettingSpec> settings_ = {
      {0x0001, "device_name"},
      {0x0002, "channel"},
      {0x0101, "pwr_mode"},
      {0x0102, "trip_limit_current"},
      {0x0103, "v_cal_factor"},
      {0x0104, "i_cal_factor"},
      {0x0201, "vbus_ovp_mv"},
      {0x0202, "vbus_uvp_mv"},
      {0x0203, "ibus_ocp_ma"},
      {0x0204, "vbat_ovp_mv"},
      {0x0205, "vbat_uvp_mv"},
      {0x0206, "ibat_ocp_ma"},
  };

  const std::vector<ProfileEventSpec> events_ = {
      {0xE001, "trip_state_changed"},
      {0xE002, "power_fault"},
      {0xE003, "reset_requested"},
  };
};

class RelayProfile final : public IProfileDefinition {
 public:
  ProfileId profileId() const override { return kProfileRelay; }
  const char* profileName() const override { return "RELAY"; }
  CodecId defaultCodecId() const override { return kCodecIdDefault; }
  bool supportsCodec(CodecId codec_id) const override {
    return isBuiltInCodecId(codec_id);
  }
  const std::vector<ProfileTelemetryMetricSpec>& telemetryMetrics() const override { return telemetry_; }
  const std::vector<ProfileSettingSpec>& settings() const override { return settings_; }
  const std::vector<ProfileEventSpec>& events() const override { return events_; }

 private:
  const std::vector<ProfileTelemetryMetricSpec> telemetry_ = {
      {0x01, "relay_bitmap"},
      {0x02, "uptime_ms"},
  };

  const std::vector<ProfileSettingSpec> settings_ = {
      {0x0001, "device_name"},
      {0x0002, "channel"},
      {0x0101, "split_idx"},
      {0x0102, "pulse_ms"},
      {0x0103, "hold_ms"},
      {0x0104, "interlock"},
      {0x0105, "rt_limit_c"},
      {0x0106, "sensor_a_mac"},
      {0x0107, "sensor_b_mac"},
      {0x030B, "persist_output_state"},
      {0x0901, "topo_version"},
      {0x0902, "topo_seed_id"},
      {0x0903, "topo_state"},
      {0x0906, "topo_prev_mac"},
      {0x0907, "topo_next_mac"},
      {0x0908, "topo_allowed_sources_blob"},
  };

  const std::vector<ProfileEventSpec> events_ = {
      {0xE101, "relay_state_changed"},
      {0xE102, "interlock_blocked"},
      {0xE103, "relay_fault"},
      {0xE1A1, "topology_staged"},
      {0xE1A2, "topology_committed"},
      {0xE1A3, "peer_pair_request_rejected"},
  };
};

class SensProfile final : public IProfileDefinition {
 public:
  ProfileId profileId() const override { return kProfileSens; }
  const char* profileName() const override { return "SENS"; }
  CodecId defaultCodecId() const override { return kCodecIdDefault; }
  bool supportsCodec(CodecId codec_id) const override {
    return isBuiltInCodecId(codec_id);
  }
  const std::vector<ProfileTelemetryMetricSpec>& telemetryMetrics() const override { return telemetry_; }
  const std::vector<ProfileSettingSpec>& settings() const override { return settings_; }
  const std::vector<ProfileEventSpec>& events() const override { return events_; }

 private:
  const std::vector<ProfileTelemetryMetricSpec> telemetry_ = {
      {0x01, "tfl_a_mm"},
      {0x02, "tfl_b_mm"},
      {0x03, "tfl_a_flux"},
      {0x04, "tfl_b_flux"},
      {0x05, "tfl_a_temp_c"},
      {0x06, "tfl_b_temp_c"},
      {0x07, "env_temp_c"},
      {0x08, "env_hum_pct"},
      {0x09, "env_press_pa"},
      {0x0A, "lux"},
  };

  const std::vector<ProfileSettingSpec> settings_ = {
      {0x0001, "device_name"},
      {0x0002, "channel"},
      {0x0101, "prev_mac"},
      {0x0102, "next_mac"},
      {0x0103, "pos_relays"},
      {0x0104, "neg_relays"},
      {0x0201, "detect_fall_delta_cm"},
      {0x0202, "detect_release_delta_cm"},
      {0x0203, "ab_spacing_cm"},
      {0x0204, "als_t0_lux"},
      {0x0205, "als_t1_lux"},
      {0x0206, "detect_window_ms"},
      {0x0207, "detect_clear_hold_ms"},
      {0x0208, "relay_on_ms"},
      {0x0209, "relay_off_ms"},
      {0x020A, "lead_count"},
      {0x020B, "lead_step_ms"},
      {0x020C, "sample_loop_ms"},
      {0x020D, "sample_ring_n"},
      {0x0301, "LoopAuto"},
      {0x0901, "topo_version"},
      {0x0902, "topo_seed_id"},
      {0x0903, "topo_state"},
      {0x0904, "topo_relay_targets_blob"},
      {0x0905, "topo_commit_epoch_s"},
  };

  const std::vector<ProfileEventSpec> events_ = {
      {0xE201, "vehicle_detected"},
      {0xE202, "vehicle_cleared"},
      {0xE203, "sensor_fault"},
      {0xE2A1, "topology_staged"},
      {0xE2A2, "topology_committed"},
      {0xE2A3, "peer_pair_link_result"},
  };
};

class SemuProfile final : public IProfileDefinition {
 public:
  ProfileId profileId() const override { return kProfileSemu; }
  const char* profileName() const override { return "SEMU"; }
  CodecId defaultCodecId() const override { return kCodecIdDefault; }
  bool supportsCodec(CodecId codec_id) const override {
    return isBuiltInCodecId(codec_id);
  }
  const std::vector<ProfileTelemetryMetricSpec>& telemetryMetrics() const override { return telemetry_; }
  const std::vector<ProfileSettingSpec>& settings() const override { return settings_; }
  const std::vector<ProfileEventSpec>& events() const override { return events_; }

 private:
  const std::vector<ProfileTelemetryMetricSpec> telemetry_ = {
      {0x01, "v0_tfl_a_mm"},
      {0x02, "v0_tfl_b_mm"},
      {0x03, "v0_tfl_a_flux"},
      {0x04, "v0_tfl_b_flux"},
      {0x05, "v0_tfl_a_temp_c"},
      {0x06, "v0_tfl_b_temp_c"},
      {0x07, "env_temp_c"},
      {0x08, "env_hum_pct"},
      {0x09, "env_press_pa"},
      {0x0A, "lux"},
  };

  const std::vector<ProfileSettingSpec> settings_ = {
      {0x0001, "device_name"},
      {0x0002, "channel"},
      {0x0101, "sensor_count"},
      {0x0102, "prev_mac"},
      {0x0103, "next_mac"},
      {0x0104, "pos_relays"},
      {0x0105, "neg_relays"},
      {0x0110, "von_ms"},
      {0x0111, "vlead_count"},
      {0x0112, "vlead_ms"},
      {0x0114, "sample_loop_ms"},
      {0x0115, "sample_ring_n"},
      {0x0901, "topo_version"},
      {0x0902, "topo_seed_id"},
      {0x0903, "topo_state"},
      {0x0904, "topo_relay_targets_blob"},
      {0x0905, "topo_commit_epoch_s"},
  };

  const std::vector<ProfileEventSpec> events_ = {
      {0xE301, "virtual_sensor_started"},
      {0xE302, "virtual_sensor_fault"},
      {0xE3A1, "topology_staged"},
      {0xE3A2, "topology_committed"},
      {0xE3A3, "peer_pair_link_result"},
  };
};

class RemuProfile final : public IProfileDefinition {
 public:
  ProfileId profileId() const override { return kProfileRemu; }
  const char* profileName() const override { return "REMU"; }
  CodecId defaultCodecId() const override { return kCodecIdDefault; }
  bool supportsCodec(CodecId codec_id) const override {
    return isBuiltInCodecId(codec_id);
  }
  const std::vector<ProfileTelemetryMetricSpec>& telemetryMetrics() const override { return telemetry_; }
  const std::vector<ProfileSettingSpec>& settings() const override { return settings_; }
  const std::vector<ProfileEventSpec>& events() const override { return events_; }

 private:
  const std::vector<ProfileTelemetryMetricSpec> telemetry_ = {
      {0x01, "relay_bitmap"},
      {0x02, "relay_count"},
  };

  const std::vector<ProfileSettingSpec> settings_ = {
      {0x0001, "device_name"},
      {0x0002, "channel"},
      {0x0101, "relay_count"},
      {0x0102, "split_idx"},
      {0x0103, "global_pulse_ms"},
      {0x0104, "global_hold_ms"},
      {0x0105, "repeat_ms"},
      {0x0106, "interlock_json"},
      {0x0107, "sensor_a_mac"},
      {0x0108, "sensor_b_mac"},
      {0x030B, "persist_output_state"},
      {0x0901, "topo_version"},
      {0x0902, "topo_seed_id"},
      {0x0903, "topo_state"},
      {0x0906, "topo_prev_mac"},
      {0x0907, "topo_next_mac"},
      {0x0908, "topo_allowed_sources_blob"},
  };

  const std::vector<ProfileEventSpec> events_ = {
      {0xE401, "virtual_relay_state_changed"},
      {0xE402, "virtual_relay_fault"},
      {0xE4A1, "topology_staged"},
      {0xE4A2, "topology_committed"},
      {0xE4A3, "peer_pair_request_rejected"},
  };
};

class LockAlarmProfile final : public IProfileDefinition {
 public:
  ProfileId profileId() const override { return kProfileLockAlarm; }
  const char* profileName() const override { return "LOCK_ALARM"; }
  CodecId defaultCodecId() const override { return kCodecIdDefault; }
  bool supportsCodec(CodecId codec_id) const override {
    return isBuiltInCodecId(codec_id);
  }
  const std::vector<ProfileTelemetryMetricSpec>& telemetryMetrics() const override { return telemetry_; }
  const std::vector<ProfileSettingSpec>& settings() const override { return settings_; }
  const std::vector<ProfileEventSpec>& events() const override { return events_; }

 private:
  const std::vector<ProfileTelemetryMetricSpec> telemetry_ = {
      {0x01, "armed"},
      {0x02, "motion_alarm_enabled"},
      {0x03, "breach"},
      {0x04, "battery_pct"},
      {0x07, "door_open"},
      {0x08, "shock_active"},
      {0x09, "reed_active"},
      {0x0A, "uptime_ms"},
      {0x20, "lock_state"},
      {0x23, "fingerprint_enabled"},
  };

  const std::vector<ProfileSettingSpec> settings_ = {
      {0x0001, "device_name"},
      {0x0002, "device_id"},
      {0x0003, "master_id"},
      {0x0004, "configured"},
      {0x0005, "channel"},
      {0x0101, "armed_state"},
      {0x0102, "breach_state"},
      {0x0103, "motion_trigger_alarm"},
      {0x0201, "lock_timeout_ms"},
      {0x0202, "lock_emag_mode"},
      {0x0203, "fingerprint_enabled"},
      {0x0301, "cap_open_switch"},
      {0x0302, "cap_shock_sensor"},
      {0x0303, "cap_reed_switch"},
      {0x0304, "cap_fingerprint"},
      {0x0401, "shock_sensor_type"},
      {0x0402, "shock_threshold"},
  };

  const std::vector<ProfileEventSpec> events_ = {
      {0xE001, "breach"},
      {0xE002, "critical"},
      {0xE003, "battery_low"},
      {0xE004, "battery_recovered"},
      {0xE005, "door_opened_after_unlock"},
      {0xE006, "door_closed_after_unlock"},
      {0xE007, "alarm_cleared"},
      {0xE008, "reed_state"},
      {0xE020, "fp_match"},
      {0xE021, "fp_fail"},
  };
};

PmsProfile g_pms_profile{};
RelayProfile g_relay_profile{};
SensProfile g_sens_profile{};
SemuProfile g_semu_profile{};
RemuProfile g_remu_profile{};
LockAlarmProfile g_lock_alarm_profile{};

template <typename SpecT, typename IdFn, typename KeyFn>
bool validateSpecSet_(const std::vector<SpecT>& specs, IdFn id_of, KeyFn key_of) {
  std::unordered_set<uint16_t> ids{};
  ids.reserve(specs.size());
  std::unordered_set<std::string> keys{};
  keys.reserve(specs.size());
  for (const auto& spec : specs) {
    const uint16_t id = id_of(spec);
    if (id == 0U) {
      return false;
    }
    const char* key_raw = key_of(spec);
    if (key_raw == nullptr || key_raw[0] == '\0') {
      return false;
    }
    const std::string key(key_raw);
    if (!ids.insert(id).second || !keys.insert(key).second) {
      return false;
    }
  }
  return true;
}

bool validateProfileDefinition_(const IProfileDefinition* profile) {
  if (profile == nullptr) {
    return false;
  }
  const CodecId default_codec = profile->defaultCodecId();
  if (!profile->supportsCodec(default_codec)) {
    return false;
  }
  if (!validateSpecSet_(profile->telemetryMetrics(),
                        [](const ProfileTelemetryMetricSpec& s) { return s.metric_id; },
                        [](const ProfileTelemetryMetricSpec& s) { return s.key; })) {
    return false;
  }
  if (!validateSpecSet_(profile->settings(),
                        [](const ProfileSettingSpec& s) { return s.setting_id; },
                        [](const ProfileSettingSpec& s) { return s.key; })) {
    return false;
  }
  if (!validateSpecSet_(profile->events(),
                        [](const ProfileEventSpec& s) { return s.event_id; },
                        [](const ProfileEventSpec& s) { return s.key; })) {
    return false;
  }
  return true;
}

}  // namespace

ProfileRegistry& ProfileRegistry::instance() {
  static ProfileRegistry g_registry;
  return g_registry;
}

bool ProfileRegistry::registerProfile(const IProfileDefinition* profile) {
  if (profile == nullptr || profile->profileId() == kProfileUnknown || profile->profileName() == nullptr) {
    return false;
  }
  const std::string profile_name(profile->profileName());
  if (profile_name.empty()) {
    return false;
  }
  if (!validateProfileDefinition_(profile)) {
    return false;
  }

  if (std::find(profiles_.begin(), profiles_.end(), profile) != profiles_.end()) {
    return true;
  }
  const auto id_it = profiles_by_id_.find(profile->profileId());
  if (id_it != profiles_by_id_.end() && id_it->second != profile) {
    return false;
  }
  const auto name_it = profiles_by_name_.find(profile_name);
  if (name_it != profiles_by_name_.end() && name_it->second != profile) {
    return false;
  }

  profiles_.push_back(profile);
  profiles_by_id_[profile->profileId()] = profile;
  profiles_by_name_[profile_name] = profile;
  ++generation_;
  snapshot_cache_valid_ = false;
  return true;
}

const IProfileDefinition* ProfileRegistry::find(ProfileId profile_id) const {
  const auto it = profiles_by_id_.find(profile_id);
  if (it == profiles_by_id_.end()) {
    return nullptr;
  }
  return it->second;
}

const IProfileDefinition* ProfileRegistry::findByName(const std::string& name) const {
  const auto it = profiles_by_name_.find(name);
  if (it == profiles_by_name_.end()) {
    return nullptr;
  }
  return it->second;
}

std::vector<const IProfileDefinition*> ProfileRegistry::list() const {
  return profiles_;
}

void ProfileRegistry::rebuildSnapshotCache_() const {
  ProfileRegistrySnapshot snapshot{};
  snapshot.generation = generation_;

  snapshot.profiles_by_id.reserve(profiles_by_id_.size());
  snapshot.profiles_by_name.reserve(profiles_by_name_.size());
  snapshot.telemetry_by_id.reserve(profiles_.size());
  snapshot.telemetry_by_key.reserve(profiles_.size());
  snapshot.settings_by_id.reserve(profiles_.size());
  snapshot.settings_by_key.reserve(profiles_.size());
  snapshot.events_by_id.reserve(profiles_.size());
  snapshot.events_by_key.reserve(profiles_.size());

  for (const IProfileDefinition* profile : profiles_) {
    if (profile == nullptr) {
      continue;
    }
    const ProfileId profile_id = profile->profileId();
    const char* profile_name_raw = profile->profileName();
    if (profile_id == kProfileUnknown || profile_name_raw == nullptr || profile_name_raw[0] == '\0') {
      continue;
    }

    snapshot.profiles_by_id.emplace(profile_id, profile);
    snapshot.profiles_by_name.emplace(profile_name_raw, profile);

    auto& telemetry_id_map = snapshot.telemetry_by_id[profile_id];
    auto& telemetry_key_map = snapshot.telemetry_by_key[profile_id];
    telemetry_id_map.reserve(profile->telemetryMetrics().size());
    telemetry_key_map.reserve(profile->telemetryMetrics().size());
    for (const auto& spec : profile->telemetryMetrics()) {
      telemetry_id_map.emplace(spec.metric_id, &spec);
      if (spec.key != nullptr && spec.key[0] != '\0') {
        telemetry_key_map.emplace(spec.key, &spec);
      }
    }

    auto& setting_id_map = snapshot.settings_by_id[profile_id];
    auto& setting_key_map = snapshot.settings_by_key[profile_id];
    setting_id_map.reserve(profile->settings().size());
    setting_key_map.reserve(profile->settings().size());
    for (const auto& spec : profile->settings()) {
      setting_id_map.emplace(spec.setting_id, &spec);
      if (spec.key != nullptr && spec.key[0] != '\0') {
        setting_key_map.emplace(spec.key, &spec);
      }
    }

    auto& event_id_map = snapshot.events_by_id[profile_id];
    auto& event_key_map = snapshot.events_by_key[profile_id];
    event_id_map.reserve(profile->events().size());
    event_key_map.reserve(profile->events().size());
    for (const auto& spec : profile->events()) {
      event_id_map.emplace(spec.event_id, &spec);
      if (spec.key != nullptr && spec.key[0] != '\0') {
        event_key_map.emplace(spec.key, &spec);
      }
    }
  }

  snapshot_cache_ = std::move(snapshot);
  snapshot_cache_valid_ = true;
}

const ProfileRegistrySnapshot& ProfileRegistry::snapshot() const {
  if (!snapshot_cache_valid_ || snapshot_cache_.generation != generation_) {
    rebuildSnapshotCache_();
  }
  return snapshot_cache_;
}

const ProfileTelemetryMetricSpec* ProfileRegistry::resolveTelemetryById(ProfileId profile_id, uint16_t metric_id) const {
  if (profile_id == kProfileUnknown || metric_id == 0U) {
    return nullptr;
  }
  const auto& snap = snapshot();
  const auto profile_it = snap.telemetry_by_id.find(profile_id);
  if (profile_it == snap.telemetry_by_id.end()) {
    return nullptr;
  }
  const auto it = profile_it->second.find(metric_id);
  if (it == profile_it->second.end()) {
    return nullptr;
  }
  return it->second;
}

const ProfileTelemetryMetricSpec* ProfileRegistry::resolveTelemetryByKey(ProfileId profile_id, const std::string& key) const {
  if (profile_id == kProfileUnknown || key.empty()) {
    return nullptr;
  }
  const auto& snap = snapshot();
  const auto profile_it = snap.telemetry_by_key.find(profile_id);
  if (profile_it == snap.telemetry_by_key.end()) {
    return nullptr;
  }
  const auto it = profile_it->second.find(key);
  if (it == profile_it->second.end()) {
    return nullptr;
  }
  return it->second;
}

const ProfileSettingSpec* ProfileRegistry::resolveSettingById(ProfileId profile_id, uint16_t setting_id) const {
  if (profile_id == kProfileUnknown || setting_id == 0U) {
    return nullptr;
  }
  const auto& snap = snapshot();
  const auto profile_it = snap.settings_by_id.find(profile_id);
  if (profile_it == snap.settings_by_id.end()) {
    return nullptr;
  }
  const auto it = profile_it->second.find(setting_id);
  if (it == profile_it->second.end()) {
    return nullptr;
  }
  return it->second;
}

const ProfileSettingSpec* ProfileRegistry::resolveSettingByKey(ProfileId profile_id, const std::string& key) const {
  if (profile_id == kProfileUnknown || key.empty()) {
    return nullptr;
  }
  const auto& snap = snapshot();
  const auto profile_it = snap.settings_by_key.find(profile_id);
  if (profile_it == snap.settings_by_key.end()) {
    return nullptr;
  }
  const auto it = profile_it->second.find(key);
  if (it == profile_it->second.end()) {
    return nullptr;
  }
  return it->second;
}

const ProfileEventSpec* ProfileRegistry::resolveEventById(ProfileId profile_id, uint16_t event_id) const {
  if (profile_id == kProfileUnknown || event_id == 0U) {
    return nullptr;
  }
  const auto& snap = snapshot();
  const auto profile_it = snap.events_by_id.find(profile_id);
  if (profile_it == snap.events_by_id.end()) {
    return nullptr;
  }
  const auto it = profile_it->second.find(event_id);
  if (it == profile_it->second.end()) {
    return nullptr;
  }
  return it->second;
}

const ProfileEventSpec* ProfileRegistry::resolveEventByKey(ProfileId profile_id, const std::string& key) const {
  if (profile_id == kProfileUnknown || key.empty()) {
    return nullptr;
  }
  const auto& snap = snapshot();
  const auto profile_it = snap.events_by_key.find(profile_id);
  if (profile_it == snap.events_by_key.end()) {
    return nullptr;
  }
  const auto it = profile_it->second.find(key);
  if (it == profile_it->second.end()) {
    return nullptr;
  }
  return it->second;
}

const ProfileTelemetryMetricSpec* findProfileTelemetryById(const IProfileDefinition* profile, uint16_t metric_id) {
  if (profile == nullptr) {
    return nullptr;
  }
  return ProfileRegistry::instance().resolveTelemetryById(profile->profileId(), metric_id);
}

const ProfileTelemetryMetricSpec* findProfileTelemetryById(ProfileId profile_id, uint16_t metric_id) {
  return ProfileRegistry::instance().resolveTelemetryById(profile_id, metric_id);
}

const ProfileTelemetryMetricSpec* findProfileTelemetryByKey(const IProfileDefinition* profile, const std::string& key) {
  if (profile == nullptr || key.empty()) {
    return nullptr;
  }
  return ProfileRegistry::instance().resolveTelemetryByKey(profile->profileId(), key);
}

const ProfileTelemetryMetricSpec* findProfileTelemetryByKey(ProfileId profile_id, const std::string& key) {
  return ProfileRegistry::instance().resolveTelemetryByKey(profile_id, key);
}

const ProfileSettingSpec* findProfileSettingById(const IProfileDefinition* profile, uint16_t setting_id) {
  if (profile == nullptr) {
    return nullptr;
  }
  return ProfileRegistry::instance().resolveSettingById(profile->profileId(), setting_id);
}

const ProfileSettingSpec* findProfileSettingById(ProfileId profile_id, uint16_t setting_id) {
  return ProfileRegistry::instance().resolveSettingById(profile_id, setting_id);
}

const ProfileSettingSpec* findProfileSettingByKey(const IProfileDefinition* profile, const std::string& key) {
  if (profile == nullptr || key.empty()) {
    return nullptr;
  }
  return ProfileRegistry::instance().resolveSettingByKey(profile->profileId(), key);
}

const ProfileSettingSpec* findProfileSettingByKey(ProfileId profile_id, const std::string& key) {
  return ProfileRegistry::instance().resolveSettingByKey(profile_id, key);
}

const ProfileEventSpec* findProfileEventById(const IProfileDefinition* profile, uint16_t event_id) {
  if (profile == nullptr) {
    return nullptr;
  }
  return ProfileRegistry::instance().resolveEventById(profile->profileId(), event_id);
}

const ProfileEventSpec* findProfileEventById(ProfileId profile_id, uint16_t event_id) {
  return ProfileRegistry::instance().resolveEventById(profile_id, event_id);
}

const ProfileEventSpec* findProfileEventByKey(const IProfileDefinition* profile, const std::string& key) {
  if (profile == nullptr || key.empty()) {
    return nullptr;
  }
  return ProfileRegistry::instance().resolveEventByKey(profile->profileId(), key);
}

const ProfileEventSpec* findProfileEventByKey(ProfileId profile_id, const std::string& key) {
  return ProfileRegistry::instance().resolveEventByKey(profile_id, key);
}

void registerBuiltInProfiles(ProfileRegistry& registry) {
  (void)registry.registerProfile(&g_pms_profile);
  (void)registry.registerProfile(&g_relay_profile);
  (void)registry.registerProfile(&g_sens_profile);
  (void)registry.registerProfile(&g_semu_profile);
  (void)registry.registerProfile(&g_remu_profile);
  (void)registry.registerProfile(&g_lock_alarm_profile);
}

}  // namespace espnow_link









