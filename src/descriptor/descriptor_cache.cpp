#include "descriptor_cache.hpp"

#include <unordered_map>

namespace espnow_link {
namespace descriptor_cache {
namespace {

struct CachedProfileSchema {
  std::vector<TelemetryDescriptor> telemetry;
  std::vector<SettingDescriptor> settings;
};

struct CacheState {
  uint64_t generation = 0U;
  std::unordered_map<ProfileId, CachedProfileSchema> by_profile_id{};
  CachedProfileSchema empty{};
};

CacheState& cacheState() {
  static CacheState state{};
  return state;
}

const IProfileDefinition* resolveRegisteredProfile(ProfileId profile_id) {
  if (profile_id == kProfileUnknown) {
    return nullptr;
  }
  const auto& snapshot = ProfileRegistry::instance().snapshot();
  const auto it = snapshot.profiles_by_id.find(profile_id);
  if (it == snapshot.profiles_by_id.end()) {
    return nullptr;
  }
  return it->second;
}

const CachedProfileSchema& buildAndCacheByProfileId(ProfileId profile_id) {
  CacheState& state = cacheState();
  const uint64_t generation = ProfileRegistry::instance().generation();
  if (state.generation != generation) {
    state.by_profile_id.clear();
    state.generation = generation;
  }
  const auto it = state.by_profile_id.find(profile_id);
  if (it != state.by_profile_id.end()) {
    return it->second;
  }

  CachedProfileSchema cache{};
  const IProfileDefinition* profile = resolveRegisteredProfile(profile_id);
  if (profile != nullptr) {
    cache.telemetry.reserve(profile->telemetryMetrics().size());
    for (const auto& m : profile->telemetryMetrics()) {
      if (m.key == nullptr || m.key[0] == '\0') {
        continue;
      }
      TelemetryDescriptor t{};
      t.metric_id = m.metric_id;
      t.key = m.key;
      t.description = "profile-schema";
      cache.telemetry.push_back(t);
    }

    cache.settings.reserve(profile->settings().size());
    for (const auto& s : profile->settings()) {
      if (s.key == nullptr || s.key[0] == '\0') {
        continue;
      }
      SettingDescriptor st{};
      st.setting_id = s.setting_id;
      st.key = s.key;
      st.value_type = SettingValueType::String;
      st.writable = false;
      st.description = "profile-schema";
      cache.settings.push_back(st);
    }
  }

  auto inserted = state.by_profile_id.emplace(profile_id, std::move(cache));
  return inserted.first->second;
}

const CachedProfileSchema& buildAndCache(const IProfileDefinition* profile) {
  if (profile == nullptr) {
    return cacheState().empty;
  }
  return buildAndCacheByProfileId(profile->profileId());
}

}  // namespace

const std::vector<TelemetryDescriptor>& telemetrySchemaForProfile(const IProfileDefinition* profile) {
  return buildAndCache(profile).telemetry;
}

const std::vector<SettingDescriptor>& settingsSchemaForProfile(const IProfileDefinition* profile) {
  return buildAndCache(profile).settings;
}

}  // namespace descriptor_cache
}  // namespace espnow_link
