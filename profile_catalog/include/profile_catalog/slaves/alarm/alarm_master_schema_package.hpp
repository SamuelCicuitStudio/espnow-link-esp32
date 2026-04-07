#pragma once

#include "profile_catalog/shared/schema_package.hpp"
#include "profile_catalog/slaves/alarm/alarm_profile.hpp"

namespace app_owned {

MasterSchemaPackage makeAlarmMasterSchemaPackage(espnow_link::CodecId codec_id = espnow_link::kCodecIdCompactIndexed);

}  // namespace app_owned
