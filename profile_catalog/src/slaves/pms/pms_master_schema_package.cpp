#include "profile_catalog/slaves/pms/pms_master_schema_package.hpp"

namespace app_owned {

MasterSchemaPackage makePmsMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &pmsProfileDefinition();
  pkg.profile_id = kAppProfilePms;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned

