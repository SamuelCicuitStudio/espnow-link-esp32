#pragma once

#include "espnow_link/transport.hpp"
#include "espnow_link/types.hpp"

namespace espnow_link {

/**
 * @brief Hardware peer-window abstraction for attach/evict decisions.
 *
 * One-slave mode keeps direct transport behavior via the default implementation.
 * Multi-slave scheduling can later replace this with a windowed implementation.
 */
class IHardwarePeerWindow {
 public:
  virtual ~IHardwarePeerWindow() = default;

  /**
   * @brief Request installation of one hardware peer entry.
   * @param mac Peer MAC.
   * @param encrypted True for secure peer add.
   * @param lmk Optional LMK for secure peer add.
   * @return true if peer is installed/available for immediate use.
   */
  virtual bool requestAttach(const MacAddress& mac, bool encrypted, const LmkKey* lmk) = 0;

  /**
   * @brief Request eviction of one hardware peer entry.
   * @param mac Peer MAC.
   * @return true if peer was removed (or already absent).
   */
  virtual bool requestEvict(const MacAddress& mac) = 0;
};

/**
 * @brief Default direct peer-window implementation backed by `ITransport`.
 */
class DirectHardwarePeerWindow final : public IHardwarePeerWindow {
 public:
  explicit DirectHardwarePeerWindow(ITransport& transport) : transport_(&transport) {}

  void bindTransport(ITransport& transport) { transport_ = &transport; }

  bool requestAttach(const MacAddress& mac, bool encrypted, const LmkKey* lmk) override;
  bool requestEvict(const MacAddress& mac) override;

 private:
  ITransport* transport_ = nullptr;
};

}  // namespace espnow_link

