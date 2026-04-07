#pragma once

#include "espnow_link/profile.hpp"

namespace app_owned {

// Keep PMS on legacy ID for wire compatibility while moving schema ownership to app code.
constexpr espnow_link::ProfileId kAppProfilePms = espnow_link::kProfilePms;
constexpr espnow_link::ProfileId kAppProfileRelay = espnow_link::kProfileRelay;
constexpr espnow_link::ProfileId kAppProfileSens = espnow_link::kProfileSens;
constexpr espnow_link::ProfileId kAppProfileSemu = espnow_link::kProfileSemu;
constexpr espnow_link::ProfileId kAppProfileRemu = espnow_link::kProfileRemu;
// Keep custom profile IDs <= 255 because pair-init currently carries profile in one byte.
constexpr espnow_link::ProfileId kAppProfileIcm = 0x21;
constexpr espnow_link::ProfileId kAppProfileLock = 0x22;
constexpr espnow_link::ProfileId kAppProfileAlarm = 0x23;

}  // namespace app_owned
