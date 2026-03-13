#include "profile_catalog/slaves/relay/relay_master_schema_package.hpp"

namespace app_owned {

MasterSchemaPackage makeRelayMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &relayProfileDefinition();
  pkg.profile_id = kAppProfileRelay;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
