#include "espnow_link/security.hpp"

#include <cstring>

#include <esp_system.h>
#include <mbedtls/md.h>

namespace espnow_link {

namespace {

constexpr char kDeriveTag[] = "ENL2LMK";
constexpr char kTopologyDeriveTag[] = "L2L-v1";

int compareMac(const MacAddress& lhs, const MacAddress& rhs) {
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] < rhs[i]) return -1;
    if (lhs[i] > rhs[i]) return 1;
  }
  return 0;
}

void writeLe32(uint32_t v, uint8_t out[4]) {
  out[0] = static_cast<uint8_t>(v & 0xFFU);
  out[1] = static_cast<uint8_t>((v >> 8) & 0xFFU);
  out[2] = static_cast<uint8_t>((v >> 16) & 0xFFU);
  out[3] = static_cast<uint8_t>((v >> 24) & 0xFFU);
}

}  // namespace

bool deriveLmkFromSeed(const PairSeed& seed,
                       const MacAddress& master,
                       const MacAddress& slave,
                       uint32_t pairing_nonce,
                       LmkKey& out_lmk) {
  uint8_t msg[6 + 6 + 16 + 4 + sizeof(kDeriveTag) - 1] = {0};
  size_t off = 0;

  std::memcpy(msg + off, master.data(), master.size());
  off += master.size();
  std::memcpy(msg + off, slave.data(), slave.size());
  off += slave.size();
  std::memcpy(msg + off, seed.bytes.data(), seed.bytes.size());
  off += seed.bytes.size();

  uint8_t nonce_le[4] = {0};
  writeLe32(pairing_nonce, nonce_le);
  std::memcpy(msg + off, nonce_le, sizeof(nonce_le));
  off += sizeof(nonce_le);

  std::memcpy(msg + off, kDeriveTag, sizeof(kDeriveTag) - 1);
  off += (sizeof(kDeriveTag) - 1);

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    return false;
  }

  uint8_t digest[32] = {0};
  const int rc = mbedtls_md_hmac(info,
                                 getPmkBytes(),
                                 kPmkLength,
                                 msg,
                                 off,
                                 digest);
  if (rc != 0) {
    return false;
  }

  for (size_t i = 0; i < out_lmk.size(); ++i) {
    out_lmk[i] = digest[i];
  }

  std::memset(digest, 0, sizeof(digest));
  std::memset(msg, 0, sizeof(msg));
  std::memset(nonce_le, 0, sizeof(nonce_le));
  return true;
}

bool deriveLmkFromTopologySeed(const std::array<uint8_t, 32>& group_seed,
                               const MacAddress& icm_mac,
                               const MacAddress& peer_a,
                               const MacAddress& peer_b,
                               uint8_t group_id,
                               LmkKey& out_lmk) {
  const MacAddress* a = &peer_a;
  const MacAddress* b = &peer_b;
  if (compareMac(peer_a, peer_b) > 0) {
    a = &peer_b;
    b = &peer_a;
  }

  uint8_t msg[(sizeof(kTopologyDeriveTag) - 1) + 6 + 6 + 6 + 1] = {0};
  size_t off = 0;
  std::memcpy(msg + off, kTopologyDeriveTag, sizeof(kTopologyDeriveTag) - 1);
  off += (sizeof(kTopologyDeriveTag) - 1);
  std::memcpy(msg + off, icm_mac.data(), icm_mac.size());
  off += icm_mac.size();
  std::memcpy(msg + off, a->data(), a->size());
  off += a->size();
  std::memcpy(msg + off, b->data(), b->size());
  off += b->size();
  msg[off++] = group_id;

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    return false;
  }

  uint8_t digest[32] = {0};
  const int rc = mbedtls_md_hmac(info,
                                 group_seed.data(),
                                 group_seed.size(),
                                 msg,
                                 off,
                                 digest);
  if (rc != 0) {
    return false;
  }

  for (size_t i = 0; i < out_lmk.size(); ++i) {
    out_lmk[i] = digest[i];
  }

  std::memset(digest, 0, sizeof(digest));
  std::memset(msg, 0, sizeof(msg));
  return true;
}

void makePairSeed(uint32_t corr_id,
                  const MacAddress& master,
                  const MacAddress& slave,
                  PairSeed& out_seed) {
  (void)corr_id;
  (void)master;
  (void)slave;

  esp_fill_random(out_seed.bytes.data(), out_seed.bytes.size());

  bool all_zero = true;
  for (const uint8_t b : out_seed.bytes) {
    if (b != 0U) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    out_seed.bytes[0] = static_cast<uint8_t>(esp_random() & 0xFFU);
    if (out_seed.bytes[0] == 0U) {
      out_seed.bytes[0] = 0x5AU;
    }
  }
}

}  // namespace espnow_link
