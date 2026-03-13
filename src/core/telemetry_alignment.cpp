#include "telemetry_alignment.hpp"

#include <cstdlib>

namespace espnow_link {
namespace telemetry_alignment {

bool parseSampleFloat(const std::string& s, float& out) {
  char* endp = nullptr;
  out = std::strtof(s.c_str(), &endp);
  return endp != nullptr && *endp == '\0';
}

const TelemetrySample* findSampleByKey(const std::vector<TelemetrySample>& samples, const std::string& key) {
  for (const auto& s : samples) {
    if (s.key == key) {
      return &s;
    }
  }
  return nullptr;
}

void alignSnapshotToProfileInPlace(const IProfileDefinition* profile,
                                   const std::vector<TelemetrySample>& snapshot,
                                   std::vector<TelemetrySample>& out) {
  out.clear();

  if (profile == nullptr) {
    out = snapshot;
    return;
  }

  std::unordered_map<std::string, const TelemetrySample*> by_key;
  by_key.reserve(snapshot.size());
  for (const auto& sample : snapshot) {
    if (sample.key.empty()) {
      continue;
    }
    // Keep first occurrence to preserve previous behavior.
    by_key.emplace(sample.key, &sample);
  }

  out.reserve(profile->telemetryMetrics().size());
  for (const auto& spec : profile->telemetryMetrics()) {
    if (spec.key == nullptr || spec.key[0] == '\0') {
      continue;
    }
    auto it = by_key.find(spec.key);
    if (it == by_key.end() || it->second == nullptr) {
      continue;
    }

    TelemetrySample aligned = *(it->second);
    aligned.metric_id = spec.metric_id;
    if (aligned.key.empty()) {
      aligned.key = spec.key;
    }
    out.push_back(std::move(aligned));
  }
}

std::vector<TelemetrySample> alignSnapshotToProfile(const IProfileDefinition* profile,
                                                    const std::vector<TelemetrySample>& snapshot) {
  std::vector<TelemetrySample> out;
  alignSnapshotToProfileInPlace(profile, snapshot, out);
  return out;
}

void buildSnapshotIndex(const std::vector<TelemetrySample>& snapshot,
                        std::unordered_map<std::string, const TelemetrySample*>& out_index) {
  out_index.clear();
  out_index.reserve(snapshot.size());
  for (const auto& sample : snapshot) {
    if (sample.key.empty()) {
      continue;
    }
    // Keep first occurrence.
    out_index.emplace(sample.key, &sample);
  }
}

}  // namespace telemetry_alignment
}  // namespace espnow_link
