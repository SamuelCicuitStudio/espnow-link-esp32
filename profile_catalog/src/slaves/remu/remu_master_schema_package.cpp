#include "profile_catalog/slaves/remu/remu_master_schema_package.hpp"

namespace app_owned {

MasterSchemaPackage makeRemuMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &remuProfileDefinition();
  pkg.profile_id = kAppProfileRemu;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
