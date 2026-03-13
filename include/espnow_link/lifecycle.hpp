#pragma once

#include "espnow_link/manager.hpp"
#include "espnow_link/persistence.hpp"

namespace espnow_link {

/**
 * @brief Restore one persisted pair from store into manager runtime.
 * @param manager Manager to restore into.
 * @param store Persistence store.
 * @param local Local MAC.
 * @param peer Peer MAC.
 * @param out_record Optional restored record output.
 * @return true on successful restore.
 */
bool restorePersistedPair(EspNowManager& manager,
                          PairingStore& store,
                          const MacAddress& local,
                          const MacAddress& peer,
                          PairRecord* out_record = nullptr);

/**
 * @brief Erase persisted pair/channel state for one local+peer tuple.
 * @param store Persistence store.
 * @param local Local MAC.
 * @param peer Peer MAC.
 * @return true on successful erase.
 */
bool clearPersistedPair(PairingStore& store,
                        const MacAddress& local,
                        const MacAddress& peer);

}  // namespace espnow_link
