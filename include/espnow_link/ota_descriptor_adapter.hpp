#pragma once

#include <string>
#include <vector>

#include "espnow_link/descriptor.hpp"
#include "espnow_link/ota_manager.hpp"

namespace espnow_link {

/**
 * @brief OTA descriptor adapter that exposes `OtaManager` state via descriptor query methods.
 *
 * This helper is intended to be owned by app code and delegated from `IDescriptorProvider`
 * OTA methods (`getOtaStatus`, `getOtaManifest`, `applyOtaImage`, ...).
 */
class OtaDescriptorAdapter {
 public:
  OtaDescriptorAdapter(OtaManager& manager,
                       IOtaStorageBackend& storage,
                       const OtaManagerConfig& config = {});

  bool getOtaStatus(OtaStatusInfo& out, std::string& out_message);
  bool getOtaManifest(std::vector<OtaManifestEntry>& out, std::string& out_message);
  bool rebuildOtaManifest(std::string& out_message);
  bool clearOtaScope(const std::string& scope, std::string& out_message);
  bool getOtaCapacity(OtaCapacityInfo& out, std::string& out_message);
  bool getOtaGateInfo(OtaGateInfo& out, std::string& out_message);
  bool applyOtaImage(const std::string& target, std::string& out_message);

 private:
  static uint32_t hashFileId_(const std::string& file_name);
  static std::string normalizeDir_(const std::string& path);
  static std::string joinPath_(const std::string& base, const std::string& name);
  static std::string trim_(const std::string& s);
  static int compareVersions_(const std::string& lhs, const std::string& rhs);
  static bool parseTargetId_(const std::string& target, uint32_t& out_id);
  static bool endsWithIgnoreCase_(const std::string& value, const std::string& suffix);
  std::string manifestPathForImage_(const std::string& file_name) const;
  std::string imagePathForName_(const std::string& file_name) const;
  bool computeFileCrc_(const std::string& image_path,
                       uint32_t expected_size,
                       uint32_t& out_crc,
                       std::string& out_message);
  bool parseManifestFile_(const std::string& manifest_path,
                          OtaManifestEntry& inout_entry,
                          std::string& out_message);
  bool writeManifestFile_(const OtaManifestEntry& entry, std::string& out_message);
  bool listImageManifest_(std::vector<OtaManifestEntry>& out, std::string& out_message);
  bool clearDirContents_(const std::string& dir_path, std::string& out_message);
  bool clearOtaScopeInternal_(const std::string& scope,
                              std::string& out_message,
                              bool allow_archive);

  OtaManager& manager_;
  IOtaStorageBackend& storage_;
  OtaManagerConfig config_;
  uint32_t last_checked_image_bytes_ = 0;
  bool last_fit_ = true;
};

}  // namespace espnow_link
