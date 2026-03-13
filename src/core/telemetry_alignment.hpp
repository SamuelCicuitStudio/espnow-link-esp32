#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "espnow_link/control.hpp"
#include "espnow_link/profile.hpp"

namespace espnow_link {
namespace telemetry_alignment {

bool parseSampleFloat(const std::string& s, float& out);
const TelemetrySample* findSampleByKey(const std::vector<TelemetrySample>& samples, const std::string& key);

void alignSnapshotToProfileInPlace(const IProfileDefinition* profile,
                                   const std::vector<TelemetrySample>& snapshot,
                                   std::vector<TelemetrySample>& out);
std::vector<TelemetrySample> alignSnapshotToProfile(const IProfileDefinition* profile,
                                                    const std::vector<TelemetrySample>& snapshot);

void buildSnapshotIndex(const std::vector<TelemetrySample>& snapshot,
                        std::unordered_map<std::string, const TelemetrySample*>& out_index);

}  // namespace telemetry_alignment
}  // namespace espnow_link
