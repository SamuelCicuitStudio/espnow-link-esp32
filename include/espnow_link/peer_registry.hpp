#pragma once

#include <cstdint>
#include <vector>

#include "espnow_link/types.hpp"

namespace espnow_link {

/** @brief Logical state of a peer in registry. */
enum class LogicalPeerState : uint8_t {
  Discovered = 0,
  Pairing = 1,
  Paired = 2,
  Offline = 3,
};

/** @brief One peer registry entry. */
struct PeerRecord {
  MacAddress mac{};
  LogicalPeerState state = LogicalPeerState::Discovered;
  bool pinned = false;
  int16_t last_rssi = 0;
  uint32_t last_seen_ms = 0;
  bool last_liveness_online = false;
  uint32_t retry_count = 0;
  uint32_t backoff_until_ms = 0;
};

/**
 * @brief Logical peer registry tracking discovery/pairing/liveness metadata.
 */
class PeerRegistry {
 public:
  /** @brief Remove all peers from registry. */
  void clear();

  /** @brief Insert/update discovery state for peer. */
  bool upsertDiscovery(const MacAddress& mac, int16_t rssi, uint32_t now_ms);
  /** @brief Mark peer as pairing in progress. */
  bool markPairing(const MacAddress& mac, uint32_t now_ms);
  /** @brief Mark peer as paired. */
  bool markPaired(const MacAddress& mac, uint32_t now_ms);
  /** @brief Mark peer as offline. */
  bool markOffline(const MacAddress& mac, uint32_t now_ms);
  /** @brief Remove peer by MAC. */
  bool removePeer(const MacAddress& mac);

  /** @brief Set or clear pin flag for peer. */
  bool setPinned(const MacAddress& mac, bool pinned);
  /** @brief Update latest liveness result for peer. */
  bool updateLiveness(const MacAddress& mac, bool online, uint32_t now_ms);
  /** @brief Increase retry count and set peer backoff. */
  bool bumpRetry(const MacAddress& mac, uint32_t now_ms, uint32_t backoff_ms);

  /** @brief Read one peer record by MAC. */
  bool getPeer(const MacAddress& mac, PeerRecord& out) const;
  /** @brief Export registry snapshot. */
  void snapshot(std::vector<PeerRecord>& out) const;
  /** @brief Number of peers currently tracked. */
  size_t size() const;

 private:
  PeerRecord* findMutable(const MacAddress& mac);
  const PeerRecord* findConst(const MacAddress& mac) const;

  std::vector<PeerRecord> peers_{};
};

}  // namespace espnow_link
