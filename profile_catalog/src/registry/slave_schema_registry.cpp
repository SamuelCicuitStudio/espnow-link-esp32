#include "profile_catalog/registry/slave_schema_registry.hpp"

#include "profile_catalog/slaves/pms/pms_master_schema_package.hpp"
#include "profile_catalog/slaves/relay/relay_master_schema_package.hpp"
#include "profile_catalog/slaves/sens/sens_master_schema_package.hpp"
#include "profile_catalog/slaves/semu/semu_master_schema_package.hpp"
#include "profile_catalog/slaves/remu/remu_master_schema_package.hpp"
#include "profile_catalog/slaves/lock/lock_master_schema_package.hpp"
#include "profile_catalog/slaves/alarm/alarm_master_schema_package.hpp"

namespace app_owned {

std::vector<MasterSchemaPackage> makeSupportedSlaveMasterSchemas(espnow_link::CodecId codec_id) {
  std::vector<MasterSchemaPackage> out;
  out.reserve(7);

  // Implemented slave package(s)
  out.push_back(makePmsMasterSchemaPackage(codec_id));
  out.push_back(makeRelayMasterSchemaPackage(codec_id));
  out.push_back(makeSensMasterSchemaPackage(codec_id));
  out.push_back(makeSemuMasterSchemaPackage(codec_id));
  out.push_back(makeRemuMasterSchemaPackage(codec_id));
  out.push_back(makeLockMasterSchemaPackage(codec_id));
  out.push_back(makeAlarmMasterSchemaPackage(codec_id));

  return out;
}

bool registerSupportedSlaveMasterSchemas(espnow_link::CodecId codec_id) {
  const std::vector<MasterSchemaPackage> pkgs = makeSupportedSlaveMasterSchemas(codec_id);
  if (pkgs.empty()) {
    return false;
  }
  bool ok = true;
  for (const auto& pkg : pkgs) {
    if (!pkg.valid() || !pkg.registerProfile()) {
      ok = false;
    }
  }
  return ok;
}

}  // namespace app_owned
