#pragma once

#include <vector>

#include "espnow_link/descriptor.hpp"
#include "espnow_link/profile.hpp"

namespace espnow_link {
namespace descriptor_cache {

const std::vector<TelemetryDescriptor>& telemetrySchemaForProfile(const IProfileDefinition* profile);
const std::vector<SettingDescriptor>& settingsSchemaForProfile(const IProfileDefinition* profile);

}  // namespace descriptor_cache
}  // namespace espnow_link
