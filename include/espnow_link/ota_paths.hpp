#pragma once

namespace espnow_link {
namespace ota_paths {

constexpr const char* kRoot = "/o";
constexpr const char* kIn = "/o/in";
constexpr const char* kStaging = "/o/s";
constexpr const char* kImage = "/o/r";
constexpr const char* kManifest = "/o/m";
constexpr const char* kState = "/o/st";

constexpr const char* kArchiveRoot = "/a";
constexpr const char* kArchiveMaster = "/a/m";
constexpr const char* kArchiveSlave = "/a/s";

constexpr const char* kStagedBinName = "fw.bin";
constexpr const char* kStagedMetaName = "fw.json";

}  // namespace ota_paths
}  // namespace espnow_link
