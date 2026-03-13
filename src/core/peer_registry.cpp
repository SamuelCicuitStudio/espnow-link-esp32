#include "espnow_link/peer_registry.hpp"

namespace espnow_link {

void PeerRegistry::clear() {
  peers_.clear();
}

bool PeerRegistry::upsertDiscovery(const MacAddress& mac, int16_t rssi, uint32_t now_ms) {
  PeerRecord* rec = findMutable(mac);
  if (rec == nullptr) {
    PeerRecord created{};
    created.mac = mac;
    created.state = LogicalPeerState::Discovered;
    created.last_rssi = rssi;
    created.last_seen_ms = now_ms;
    peers_.push_back(created);
    return true;
  }

  rec->last_rssi = rssi;
  rec->last_seen_ms = now_ms;
  if (rec->state == LogicalPeerState::Offline) {
    rec->state = LogicalPeerState::Discovered;
  }
  return true;
}

bool PeerRegistry::markPairing(const MacAddress& mac, uint32_t now_ms) {
  PeerRecord* rec = findMutable(mac);
  if (rec == nullptr) {
    PeerRecord created{};
    created.mac = mac;
    created.state = LogicalPeerState::Pairing;
    created.last_seen_ms = now_ms;
    peers_.push_back(created);
    return true;
  }

  rec->state = LogicalPeerState::Pairing;
  rec->last_seen_ms = now_ms;
  return true;
}

bool PeerRegistry::markPaired(const MacAddress& mac, uint32_t now_ms) {
  PeerRecord* rec = findMutable(mac);
  if (rec == nullptr) {
    PeerRecord created{};
    created.mac = mac;
    created.state = LogicalPeerState::Paired;
    created.last_seen_ms = now_ms;
    created.last_liveness_online = true;
    peers_.push_back(created);
    return true;
  }

  rec->state = LogicalPeerState::Paired;
  rec->last_seen_ms = now_ms;
  rec->last_liveness_online = true;
  rec->retry_count = 0;
  rec->backoff_until_ms = 0;
  return true;
}

bool PeerRegistry::markOffline(const MacAddress& mac, uint32_t now_ms) {
  PeerRecord* rec = findMutable(mac);
  if (rec == nullptr) {
    return false;
  }
  rec->state = LogicalPeerState::Offline;
  rec->last_liveness_online = false;
  rec->last_seen_ms = now_ms;
  return true;
}

bool PeerRegistry::removePeer(const MacAddress& mac) {
  for (size_t i = 0; i < peers_.size(); ++i) {
    if (peers_[i].mac == mac) {
      peers_.erase(peers_.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

bool PeerRegistry::setPinned(const MacAddress& mac, bool pinned) {
  PeerRecord* rec = findMutable(mac);
  if (rec == nullptr) {
    return false;
  }
  rec->pinned = pinned;
  return true;
}

bool PeerRegistry::updateLiveness(const MacAddress& mac, bool online, uint32_t now_ms) {
  PeerRecord* rec = findMutable(mac);
  if (rec == nullptr) {
    return false;
  }
  rec->last_liveness_online = online;
  rec->last_seen_ms = now_ms;
  rec->state = online ? LogicalPeerState::Paired : LogicalPeerState::Offline;
  return true;
}

bool PeerRegistry::bumpRetry(const MacAddress& mac, uint32_t now_ms, uint32_t backoff_ms) {
  PeerRecord* rec = findMutable(mac);
  if (rec == nullptr) {
    return false;
  }
  ++rec->retry_count;
  rec->last_seen_ms = now_ms;
  rec->backoff_until_ms = now_ms + backoff_ms;
  return true;
}

bool PeerRegistry::getPeer(const MacAddress& mac, PeerRecord& out) const {
  const PeerRecord* rec = findConst(mac);
  if (rec == nullptr) {
    return false;
  }
  out = *rec;
  return true;
}

void PeerRegistry::snapshot(std::vector<PeerRecord>& out) const {
  out = peers_;
}

size_t PeerRegistry::size() const {
  return peers_.size();
}

PeerRecord* PeerRegistry::findMutable(const MacAddress& mac) {
  for (auto& p : peers_) {
    if (p.mac == mac) {
      return &p;
    }
  }
  return nullptr;
}

const PeerRecord* PeerRegistry::findConst(const MacAddress& mac) const {
  for (const auto& p : peers_) {
    if (p.mac == mac) {
      return &p;
    }
  }
  return nullptr;
}

}  // namespace espnow_link

