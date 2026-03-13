#include "profile_catalog/shared/schema_package.hpp"

#include "espnow_link/profile.hpp"

namespace app_owned {

bool SlaveSchemaPackage::registerProfile() const {
  if (profile == nullptr) {
    return false;
  }
  return espnow_link::ProfileRegistry::instance().registerProfile(profile);
}

bool SlaveSchemaPackage::valid() const {
  return profile != nullptr &&
         descriptor != nullptr &&
         telemetry_push != nullptr &&
         profile_id != espnow_link::kProfileUnknown;
}

bool MasterSchemaPackage::registerProfile() const {
  if (profile == nullptr) {
    return false;
  }
  return espnow_link::ProfileRegistry::instance().registerProfile(profile);
}

bool MasterSchemaPackage::valid() const {
  return profile != nullptr && profile_id != espnow_link::kProfileUnknown;
}

}  // namespace app_owned
