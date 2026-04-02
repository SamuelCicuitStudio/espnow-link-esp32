#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(ARDUINO)
#include <Arduino.h>
#if __has_include(<freertos/FreeRTOS.h>) && __has_include(<freertos/task.h>)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define ESPNOW_LINK_FRONTEND_ADAPTER_HAS_RTOS_TASK_ID 1
#else
#define ESPNOW_LINK_FRONTEND_ADAPTER_HAS_RTOS_TASK_ID 0
#endif
#else
#include <functional>
#include <thread>
#endif

#include "espnow_link/descriptor.hpp"
#include "espnow_link/management_controller.hpp"
#include "espnow_link/management_queue_transport.hpp"
#include "espnow_link/management_runtime.hpp"
#include "espnow_link/management_service.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/management_utils.hpp"

#ifndef ESPNOW_LINK_FRONTEND_ADAPTER_STRICT_RAW_POLL
#define ESPNOW_LINK_FRONTEND_ADAPTER_STRICT_RAW_POLL 0
#endif

#ifndef ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_CHECK
#define ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_CHECK 0
#endif

#ifndef ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_ASSERT
#define ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_ASSERT 0
#endif

namespace espnow_link {

/**
 * @brief Frontend-facing adapter for typed management control and feedback.
 *
 * This adapter is designed for WiFi/BLE/custom frontends to mirror CLI-level
 * control without string command parsing.
 *
 * Command execution is transport-canonical:
 * - submit through `ManagementQueueTransport`
 * - progress via `ManagementRuntime`
 *
 * `ManagementService` remains bound only for radio lifecycle helpers and
 * service-facing introspection utilities.
 */
class ManagementFrontendAdapter {
 public:
#include "espnow_link/internal/management_frontend_adapter_public.inl"

 private:
#include "espnow_link/internal/management_frontend_adapter_private.inl"
};

}  // namespace espnow_link

