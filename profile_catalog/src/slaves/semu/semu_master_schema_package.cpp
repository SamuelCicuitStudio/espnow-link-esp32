#include "profile_catalog/slaves/semu/semu_master_schema_package.hpp"

namespace app_owned {

MasterSchemaPackage makeSemuMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &semuProfileDefinition();
  pkg.profile_id = kAppProfileSemu;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
