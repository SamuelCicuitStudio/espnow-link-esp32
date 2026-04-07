#include "profile_catalog/slaves/lock/lock_master_schema_package.hpp"

namespace app_owned {

MasterSchemaPackage makeLockMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &lockProfileDefinition();
  pkg.profile_id = kAppProfileLock;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
