#pragma once

#include "espnow_link/management_types.hpp"

namespace espnow_link {

/**
 * @brief Adapter interface between management core and external control channels.
 *
 * Implement this to bridge CLI, Wi-Fi HTTP/WebSocket, BLE GATT, or custom control frontends
 * into the same management request/response/event model.
 */
class IManagementTransport {
 public:
  virtual ~IManagementTransport() = default;

  /** @brief Return logical source type represented by this transport instance. */
  virtual ManagementSource source() const = 0;

  /**
   * @brief Return authorization level assigned to this transport endpoint.
   *
   * Runtime applies this value to every inbound request to prevent client-side
   * spoofing of elevated access.
   */
  virtual ManagementAccessLevel accessLevel() const { return ManagementAccessLevel::Owner; }

  /**
   * @brief Poll one inbound management request from the transport.
   * @param out_request Filled request when available.
   * @return true when a request was produced.
   */
  virtual bool pollRequest(ManagementRequest& out_request) = 0;

  /**
   * @brief Send a management response back to the transport client.
   * @param response Response envelope to emit.
   * @return true when transport accepted the response.
   */
  virtual bool sendResponse(const ManagementResponse& response) = 0;

  /**
   * @brief Emit asynchronous management event to the transport client.
   * @param event Event envelope to emit.
   * @return true when transport accepted the event.
   */
  virtual bool sendEvent(const ManagementEvent& event) = 0;
};

}  // namespace espnow_link
