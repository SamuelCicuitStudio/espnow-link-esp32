/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Internal include surface and shared declarations for split dispatch implementation units.
 **************************************************************/
#pragma once

#include "espnow_link/cli_master.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <limits>
#if defined(ARDUINO)
#include <Preferences.h>
#endif
#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include "espnow_link/address.hpp"
#include "espnow_link/management_controller.hpp"
#include "espnow_link/management_runtime.hpp"
#include "espnow_link/management_service.hpp"
#include "espnow_link/management_utils.hpp"
#include "espnow_link/nvs_contract.hpp"
#include "espnow_link/ota_paths.hpp"
#include "espnow_link/profile.hpp"
#include "espnow_link/security.hpp"
#include "profile_catalog/masters/icm/icm_keys.hpp"
#include "../cli_helpers.hpp"

namespace espnow_link {

// Shared internal declarations can be moved here as the split evolves.

}  // namespace espnow_link
