#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "espnow_link/types.hpp"

#ifndef ESPNOW_LINK_PMK
#error "ESPNOW_LINK_PMK must be defined as a 16-byte string literal (for example \"0123456789ABCDEF\")."
#endif

static_assert(sizeof(ESPNOW_LINK_PMK) == 17,
              "ESPNOW_LINK_PMK must be exactly 16 characters (plus null terminator).");

namespace espnow_link {

/** @brief PMK length required by ESP-NOW encrypted peer configuration. */
constexpr size_t kPmkLength = 16;

/** @brief Get PMK bytes from compile-time `ESPNOW_LINK_PMK` define. */
inline const uint8_t* getPmkBytes() {
  return reinterpret_cast<const uint8_t*>(ESPNOW_LINK_PMK);
}

/**
 * @brief Derive 16-byte LMK from pair seed and endpoint identities.
 *
 * Derivation: `trunc16(HMAC-SHA256(PMK, master_mac||slave_mac||pair_seed||pairing_nonce_le||"ENL2LMK"))`.
 */
bool deriveLmkFromSeed(const PairSeed& seed,
                       const MacAddress& master,
                       const MacAddress& slave,
                       uint32_t pairing_nonce,
                       LmkKey& out_lmk);

/**
 * @brief Derive deterministic lateral-link LMK from one topology group seed.
 *
 * Derivation:
 * `trunc16(HMAC-SHA256(group_seed_32, "L2L-v1"||icm_mac||min(peer_a,peer_b)||max(peer_a,peer_b)||group_id))`.
 */
bool deriveLmkFromTopologySeed(const std::array<uint8_t, 32>& group_seed,
                               const MacAddress& icm_mac,
                               const MacAddress& peer_a,
                               const MacAddress& peer_b,
                               uint8_t group_id,
                               LmkKey& out_lmk);

/**
 * @brief Generate pair seed for master pairing initiation.
 * @param corr_id Pair correlation ID.
 * @param master Master MAC.
 * @param slave Slave MAC.
 * @param out_seed Output seed bytes.
 */
void makePairSeed(uint32_t corr_id,
                  const MacAddress& master,
                  const MacAddress& slave,
                  PairSeed& out_seed);

}  // namespace espnow_link
