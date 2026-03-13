#pragma once

#include <vector>

#include "profile_catalog/shared/schema_package.hpp"
#include "espnow_link/profile.hpp"

namespace app_owned {

/**
 * Build master-side schema packages for every slave type supported by this app.
 *
 * Current implementation includes:
 * - PMS
 * - RELAY
 * - SENS
 * - SEMU
 * - REMU
 *
 * Extend this file as additional slave packages are implemented.
 */
std::vector<MasterSchemaPackage> makeSupportedSlaveMasterSchemas(
    espnow_link::CodecId codec_id = espnow_link::kCodecIdCompactIndexed);

/**
 * Register all supported slave schema profiles into ProfileRegistry.
 *
 * Returns true only when every package is valid and registered.
 */
bool registerSupportedSlaveMasterSchemas(
    espnow_link::CodecId codec_id = espnow_link::kCodecIdCompactIndexed);

}  // namespace app_owned
