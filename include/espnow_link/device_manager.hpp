#pragma once

#include <string>

#include "espnow_link/management_types.hpp"
#include "espnow_link/types.hpp"

namespace espnow_link {

/**
 * @brief Context presented to device policy/actions for critical management commands.
 *
 * It contains role/link state and source/active peer information needed to authorize
 * or queue operations like restart/reset safely.
 */
struct DeviceCommandContext {
  Role local_role = Role::Slave;
  ManagementSource source = ManagementSource::Unknown;
  ManagementAccessLevel access_level = ManagementAccessLevel::Owner;
  uint16_t command_id = 0;
  uint32_t req_id = 0;
  std::string command_arg;

  bool paired = false;
  bool pairing_in_progress = false;
  bool unpair_in_progress = false;

  bool has_active_peer = false;
  MacAddress active_peer{};

  bool has_source_peer = false;
  MacAddress source_peer{};
};

/** @brief Policy decision codes for critical command authorization. */
enum class DevicePolicyCode : uint16_t {
  AllowDeferred = 0,
  DenyNotPaired = 1,
  DenySourceNotActiveMaster = 2,
  DenyBusyPairing = 3,
  DenyUnpairInProgress = 4,
  DenyPolicy = 5,
  ErrorInternal = 6,
};

/** @brief Result of policy evaluation for one critical command request. */
struct DevicePolicyDecision {
  DevicePolicyCode code = DevicePolicyCode::AllowDeferred;
  std::string message;
};

/**
 * @brief Policy hook used before executing sensitive commands.
 */
class IDeviceManagerPolicy {
 public:
  virtual ~IDeviceManagerPolicy() = default;

  /**
   * @brief Validate whether the requested command is allowed in current context.
   * @param ctx Command context and current link state.
   * @return Decision code and optional diagnostic message.
   */
  virtual DevicePolicyDecision authorizeCriticalCommand(const DeviceCommandContext& ctx) = 0;
};

/**
 * @brief Actions hook used to queue deferred execution of critical commands.
 */
class IDeviceManagerActions {
 public:
  virtual ~IDeviceManagerActions() = default;

  /**
   * @brief Queue command execution in device main loop/task context.
   * @param ctx Command context to execute.
   * @param out_message Optional status text from implementation.
   * @return true if command was accepted for deferred execution.
   */
  virtual bool queueCriticalCommand(const DeviceCommandContext& ctx, std::string* out_message) = 0;
};

/** @brief Default policy that allows every critical command request. */
class AllowAllDevicePolicy : public IDeviceManagerPolicy {
 public:
  /** @brief Always returns `AllowDeferred`. */
  DevicePolicyDecision authorizeCriticalCommand(const DeviceCommandContext&) override {
    return {};
  }
};

/** @brief Default actions implementation that rejects all critical commands. */
class NullDeviceActions : public IDeviceManagerActions {
 public:
  /**
   * @brief Reject command because no actions backend is configured.
   * @param out_message Optional error reason.
   * @return Always false.
   */
  bool queueCriticalCommand(const DeviceCommandContext&, std::string* out_message) override {
    if (out_message != nullptr) {
      *out_message = "actions not configured";
    }
    return false;
  }
};

}  // namespace espnow_link
