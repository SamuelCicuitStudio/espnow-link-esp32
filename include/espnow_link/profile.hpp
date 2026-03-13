#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "espnow_link/codec.hpp"

namespace espnow_link {

using ProfileId = uint16_t;

/** @brief Invalid/unknown profile ID. */
constexpr ProfileId kProfileUnknown = 0;
/** @brief EasyDriveway PMS profile. */
constexpr ProfileId kProfilePms = 1;
/** @brief EasyDriveway relay profile. */
constexpr ProfileId kProfileRelay = 2;
/** @brief EasyDriveway sensor profile. */
constexpr ProfileId kProfileSens = 3;
/** @brief EasyDriveway sensor emulator profile. */
constexpr ProfileId kProfileSemu = 4;
/** @brief EasyDriveway relay emulator profile. */
constexpr ProfileId kProfileRemu = 5;
/** @brief Lock/alarm profile. */
constexpr ProfileId kProfileLockAlarm = 6;

/** @brief Profile telemetry metric identity entry. */
struct ProfileTelemetryMetricSpec {
  uint16_t metric_id = 0;
  const char* key = "";
};

/** @brief Profile setting identity entry. */
struct ProfileSettingSpec {
  uint16_t setting_id = 0;
  const char* key = "";
};

/** @brief Profile event identity entry. */
struct ProfileEventSpec {
  uint16_t event_id = 0;
  const char* key = "";
};

/**
 * @brief Static profile definition contract.
 */
class IProfileDefinition {
 public:
  virtual ~IProfileDefinition() = default;

  /** @brief Unique profile ID. */
  virtual ProfileId profileId() const = 0;
  /** @brief Human-readable profile name. */
  virtual const char* profileName() const = 0;
  /** @brief Preferred default codec ID for this profile. */
  virtual CodecId defaultCodecId() const = 0;
  /** @brief Check if profile allows a codec ID. */
  virtual bool supportsCodec(CodecId codec_id) const = 0;

  /** @brief List supported telemetry metric identities. */
  virtual const std::vector<ProfileTelemetryMetricSpec>& telemetryMetrics() const = 0;
  /** @brief List supported setting identities. */
  virtual const std::vector<ProfileSettingSpec>& settings() const = 0;
  /** @brief List supported event identities. */
  virtual const std::vector<ProfileEventSpec>& events() const = 0;
};

/**
 * @brief Immutable profile-registry snapshot for deterministic frontend resolution.
 */
struct ProfileRegistrySnapshot {
  uint64_t generation = 0U;
  std::unordered_map<ProfileId, const IProfileDefinition*> profiles_by_id{};
  std::unordered_map<std::string, const IProfileDefinition*> profiles_by_name{};
  std::unordered_map<ProfileId, std::unordered_map<uint16_t, const ProfileTelemetryMetricSpec*>> telemetry_by_id{};
  std::unordered_map<ProfileId, std::unordered_map<std::string, const ProfileTelemetryMetricSpec*>> telemetry_by_key{};
  std::unordered_map<ProfileId, std::unordered_map<uint16_t, const ProfileSettingSpec*>> settings_by_id{};
  std::unordered_map<ProfileId, std::unordered_map<std::string, const ProfileSettingSpec*>> settings_by_key{};
  std::unordered_map<ProfileId, std::unordered_map<uint16_t, const ProfileEventSpec*>> events_by_id{};
  std::unordered_map<ProfileId, std::unordered_map<std::string, const ProfileEventSpec*>> events_by_key{};
};

/**
 * @brief Registry of profile definitions available to runtime.
 */
class ProfileRegistry {
 public:
  /** @brief Get singleton profile registry instance. */
  static ProfileRegistry& instance();

  /** @brief Register one profile definition. */
  bool registerProfile(const IProfileDefinition* profile);
  /** @brief Find profile by ID. */
  const IProfileDefinition* find(ProfileId profile_id) const;
  /** @brief Find profile by name. */
  const IProfileDefinition* findByName(const std::string& name) const;
  /** @brief List all registered profiles. */
  std::vector<const IProfileDefinition*> list() const;
  /** @brief Monotonic generation incremented on successful profile registration. */
  uint64_t generation() const { return generation_; }
  /** @brief Immutable snapshot with O(1) profile + field resolution maps. */
  const ProfileRegistrySnapshot& snapshot() const;
  /** @brief Resolve telemetry spec by `(profile_id, metric_id)`. */
  const ProfileTelemetryMetricSpec* resolveTelemetryById(ProfileId profile_id, uint16_t metric_id) const;
  /** @brief Resolve telemetry spec by `(profile_id, metric_key)`. */
  const ProfileTelemetryMetricSpec* resolveTelemetryByKey(ProfileId profile_id, const std::string& key) const;
  /** @brief Resolve setting spec by `(profile_id, setting_id)`. */
  const ProfileSettingSpec* resolveSettingById(ProfileId profile_id, uint16_t setting_id) const;
  /** @brief Resolve setting spec by `(profile_id, setting_key)`. */
  const ProfileSettingSpec* resolveSettingByKey(ProfileId profile_id, const std::string& key) const;
  /** @brief Resolve event spec by `(profile_id, event_id)`. */
  const ProfileEventSpec* resolveEventById(ProfileId profile_id, uint16_t event_id) const;
  /** @brief Resolve event spec by `(profile_id, event_key)`. */
  const ProfileEventSpec* resolveEventByKey(ProfileId profile_id, const std::string& key) const;

 private:
  void rebuildSnapshotCache_() const;

  std::vector<const IProfileDefinition*> profiles_{};
  std::unordered_map<ProfileId, const IProfileDefinition*> profiles_by_id_{};
  std::unordered_map<std::string, const IProfileDefinition*> profiles_by_name_{};
  uint64_t generation_ = 0U;
  mutable ProfileRegistrySnapshot snapshot_cache_{};
  mutable bool snapshot_cache_valid_ = false;
};

/** @brief Find telemetry spec by metric ID. */
const ProfileTelemetryMetricSpec* findProfileTelemetryById(const IProfileDefinition* profile, uint16_t metric_id);
/** @brief Find telemetry spec by profile + metric ID. */
const ProfileTelemetryMetricSpec* findProfileTelemetryById(ProfileId profile_id, uint16_t metric_id);
/** @brief Find telemetry spec by metric key. */
const ProfileTelemetryMetricSpec* findProfileTelemetryByKey(const IProfileDefinition* profile, const std::string& key);
/** @brief Find telemetry spec by profile + metric key. */
const ProfileTelemetryMetricSpec* findProfileTelemetryByKey(ProfileId profile_id, const std::string& key);
/** @brief Find setting spec by setting ID. */
const ProfileSettingSpec* findProfileSettingById(const IProfileDefinition* profile, uint16_t setting_id);
/** @brief Find setting spec by profile + setting ID. */
const ProfileSettingSpec* findProfileSettingById(ProfileId profile_id, uint16_t setting_id);
/** @brief Find setting spec by setting key. */
const ProfileSettingSpec* findProfileSettingByKey(const IProfileDefinition* profile, const std::string& key);
/** @brief Find setting spec by profile + setting key. */
const ProfileSettingSpec* findProfileSettingByKey(ProfileId profile_id, const std::string& key);
/** @brief Find event spec by event ID. */
const ProfileEventSpec* findProfileEventById(const IProfileDefinition* profile, uint16_t event_id);
/** @brief Find event spec by profile + event ID. */
const ProfileEventSpec* findProfileEventById(ProfileId profile_id, uint16_t event_id);
/** @brief Find event spec by event key. */
const ProfileEventSpec* findProfileEventByKey(const IProfileDefinition* profile, const std::string& key);
/** @brief Find event spec by profile + event key. */
const ProfileEventSpec* findProfileEventByKey(ProfileId profile_id, const std::string& key);
/**
 * @brief Register built-in profile definitions.
 * @param registry Target registry.
 *
 * Safe to call multiple times.
 */
void registerBuiltInProfiles(ProfileRegistry& registry);

}  // namespace espnow_link


