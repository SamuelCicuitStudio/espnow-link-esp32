#include "espnow_link/peer_window.hpp"

namespace espnow_link {

bool DirectHardwarePeerWindow::requestAttach(const MacAddress& mac,
                                             bool encrypted,
                                             const LmkKey* lmk) {
  if (transport_ == nullptr) {
    return false;
  }
  return transport_->addPeer(mac, encrypted, lmk);
}

bool DirectHardwarePeerWindow::requestEvict(const MacAddress& mac) {
  if (transport_ == nullptr) {
    return false;
  }
  return transport_->removePeer(mac);
}

}  // namespace espnow_link
