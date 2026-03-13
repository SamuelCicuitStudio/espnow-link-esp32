#include "profile_catalog/slaves/sens/sens_master_schema_package.hpp"

namespace app_owned {

MasterSchemaPackage makeSensMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &sensProfileDefinition();
  pkg.profile_id = kAppProfileSens;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
