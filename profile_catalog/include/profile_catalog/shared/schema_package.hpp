#pragma once

#include "espnow_link/descriptor.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/telemetry_push.hpp"

namespace app_owned {

struct SlaveSchemaPackage {
  const espnow_link::IProfileDefinition* profile = nullptr;
  espnow_link::IDescriptorProvider* descriptor = nullptr;
  espnow_link::ITelemetryPushProvider* telemetry_push = nullptr;
  espnow_link::ProfileId profile_id = espnow_link::kProfileUnknown;
  espnow_link::CodecId codec_id = espnow_link::kCodecIdCompactIndexed;

  bool registerProfile() const;
  bool valid() const;
};

struct MasterSchemaPackage {
  const espnow_link::IProfileDefinition* profile = nullptr;
  espnow_link::ProfileId profile_id = espnow_link::kProfileUnknown;
  espnow_link::CodecId codec_id = espnow_link::kCodecIdCompactIndexed;

  bool registerProfile() const;
  bool valid() const;
};

}  // namespace app_owned
