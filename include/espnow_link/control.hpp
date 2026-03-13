#pragma once

#include <cstddef>
#include <cstdint>

#include "espnow_link/types.hpp"

namespace espnow_link {

/**
 * @brief Pull-plane callback interface for application-level command handling.
 *
 * Manager routes pull request/response payloads here after frame decoding.
 */
class IControlPlane {
 public:
  virtual ~IControlPlane() = default;

  /**
   * @brief Handle one inbound pull request payload.
   * @param from Source peer MAC.
   * @param corr_id Request correlation ID.
   * @param payload Request bytes.
   * @param len Request payload length.
   * @return true if payload was handled.
   */
  virtual bool onPullRequest(const MacAddress& from,
                             uint32_t corr_id,
                             const uint8_t* payload,
                             size_t len) = 0;

  /**
   * @brief Handle one inbound pull response payload.
   * @param from Source peer MAC.
   * @param corr_id Response correlation ID.
   * @param payload Response bytes.
   * @param len Response payload length.
   * @return true if payload was handled.
   */
  virtual bool onPullResponse(const MacAddress& from,
                              uint32_t corr_id,
                              const uint8_t* payload,
                              size_t len) = 0;
};

/** @brief No-op control plane used when application does not provide one. */
class NullControlPlane : public IControlPlane {
 public:
  bool onPullRequest(const MacAddress&, uint32_t, const uint8_t*, size_t) override { return true; }
  bool onPullResponse(const MacAddress&, uint32_t, const uint8_t*, size_t) override { return true; }
};

}  // namespace espnow_link
