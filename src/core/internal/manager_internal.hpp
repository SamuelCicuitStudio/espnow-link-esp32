#pragma once

#include "espnow_link/manager.hpp"
#include "espnow_link/descriptor.hpp"
#include "espnow_link/control_codec.hpp"
#include "espnow_link/ota_types.hpp"
#include "espnow_link/security.hpp"
#include "espnow_link/telemetry_push.hpp"
#include "../manager_helpers.hpp"
#include "../telemetry_alignment.hpp"
#include "../../descriptor/descriptor_reply.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "manager_constants.hpp"
