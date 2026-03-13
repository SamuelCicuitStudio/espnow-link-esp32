#include "espnow_link/lifecycle.hpp"

namespace espnow_link {

bool restorePersistedPair(EspNowManager& manager,
                          PairingStore& store,
                          const MacAddress& local,
                          const MacAddress& peer,
                          PairRecord* out_record) {
  PairRecord record{};
  if (!store.loadPair(local, peer, record) || !record.valid) {
    return false;
  }

  if (!manager.restorePairedLink(record)) {
    return false;
  }

  if (out_record != nullptr) {
    *out_record = record;
  }
  return true;
}

bool clearPersistedPair(PairingStore& store,
                        const MacAddress& local,
                        const MacAddress& peer) {
  const bool a = store.erasePair(local, peer);
  const bool b = store.eraseChannel(local, peer);
  return a && b;
}

}  // namespace espnow_link
