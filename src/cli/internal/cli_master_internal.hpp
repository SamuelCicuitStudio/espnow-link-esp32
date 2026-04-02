/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Internal include surface and shared declarations for split Master CLI implementation units.
 **************************************************************/
#pragma once

#include "espnow_link/cli_master.hpp"

#include <algorithm>
#if defined(ARDUINO)
#include <Arduino.h>
#endif
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "espnow_link/address.hpp"
#include "espnow_link/management_controller.hpp"
#include "espnow_link/management_queue_transport.hpp"
#include "espnow_link/management_runtime.hpp"
#include "espnow_link/management_utils.hpp"
#include "espnow_link/ota_paths.hpp"
#include "espnow_link/power.hpp"
#include "espnow_link/profile.hpp"
#include "../cli_helpers.hpp"

namespace espnow_link {

// Shared internal declarations can be moved here as the split evolves.

}  // namespace espnow_link
