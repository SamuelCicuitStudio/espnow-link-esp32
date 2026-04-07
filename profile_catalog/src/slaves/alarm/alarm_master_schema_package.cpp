#include "profile_catalog/slaves/alarm/alarm_master_schema_package.hpp"

namespace app_owned {

MasterSchemaPackage makeAlarmMasterSchemaPackage(espnow_link::CodecId codec_id) {
  MasterSchemaPackage pkg{};
  pkg.profile = &alarmProfileDefinition();
  pkg.profile_id = kAppProfileAlarm;
  pkg.codec_id = codec_id;
  return pkg;
}

}  // namespace app_owned
