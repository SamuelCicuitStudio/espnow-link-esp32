#include "espnow_link/persistence.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace espnow_link {

namespace {

constexpr uint32_t kFnvOffset = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;
constexpr uint8_t kKeySaltPairIndex = 0xA2;
constexpr uint8_t kKeySaltPairSeqCounter = 0xA3;
constexpr uint8_t kPairIndexSchemaVersion = 1;
constexpr uint8_t kPairSeqSchemaVersion = 1;
constexpr size_t kPairIndexHeaderBytes = 4;
constexpr size_t kPairIndexEntryBytes = 11;
constexpr size_t kPairSeqBlobBytes = 5;
constexpr size_t kBlobCacheMaxEntries = 256;

void hashByte(uint32_t& h, uint8_t b) {
  h ^= b;
  h *= kFnvPrime;
}

void hashMac(uint32_t& h, const MacAddress& mac) {
  for (uint8_t b : mac) {
    hashByte(h, b);
  }
}

uint32_t readU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void appendU32Le(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

}  // namespace

PairingStore::PairingStore(const PersistenceConfig& config, IPersistenceBackend* backend)
    : config_(config), backend_(backend) {}

bool PairingStore::enabled() const { return config_.enabled && backend_ != nullptr; }

bool PairingStore::savePair(const PairRecord& record) {
  if (!enabled()) {
    return false;
  }

  std::array<uint8_t, 31> blob{};
  blob[0] = static_cast<uint8_t>(record.local_role);
  for (size_t i = 0; i < 6; ++i) {
    blob[1 + i] = record.local_mac[i];
    blob[7 + i] = record.peer_mac[i];
  }
  for (size_t i = 0; i < 16; ++i) {
    blob[13 + i] = record.lmk[i];
  }
  blob[29] = record.channel;
  blob[30] = record.valid ? 1 : 0;

  const std::string key = makeKey(RecordKind::Pair, record.local_mac, record.peer_mac);
  if (!putBlobIfChanged(key, blob.data(), blob.size())) {
    return false;
  }

  uint32_t assigned_pair_seq = 0;
  std::vector<PairIndexEntry> entries;
  if (loadPairIndex(record.local_mac, entries)) {
    for (const auto& e : entries) {
      if (e.peer_mac == record.peer_mac && e.valid && e.pair_seq != 0) {
        assigned_pair_seq = e.pair_seq;
        break;
      }
    }
  }

  if (assigned_pair_seq == 0) {
    uint32_t next_pair_seq = 1;
    if (loadPairSeqCounter(record.local_mac, next_pair_seq)) {
      if (next_pair_seq == 0) {
        next_pair_seq = 1;
      }
    } else {
      next_pair_seq = 1;
    }
    assigned_pair_seq = next_pair_seq;
    const uint32_t stored_next =
        (next_pair_seq == std::numeric_limits<uint32_t>::max()) ? 1U : (next_pair_seq + 1U);
    if (!savePairSeqCounter(record.local_mac, stored_next)) {
      return false;
    }
  }

  return upsertPairIndexEntry(record.local_mac, record.peer_mac, assigned_pair_seq);
}

bool PairingStore::loadPair(const MacAddress& local, const MacAddress& peer, PairRecord& out_record) {
  if (!enabled()) {
    return false;
  }

  std::vector<uint8_t> blob;
  const std::string key = makeKey(RecordKind::Pair, local, peer);
  if (!getBlobCached(key, blob)) {
    return false;
  }
  if (blob.size() != 31) {
    return false;
  }

  out_record.local_role = static_cast<Role>(blob[0]);
  for (size_t i = 0; i < 6; ++i) {
    out_record.local_mac[i] = blob[1 + i];
    out_record.peer_mac[i] = blob[7 + i];
  }
  for (size_t i = 0; i < 16; ++i) {
    out_record.lmk[i] = blob[13 + i];
  }
  out_record.channel = blob[29];
  out_record.valid = (blob[30] != 0);
  return true;
}

bool PairingStore::erasePair(const MacAddress& local, const MacAddress& peer) {
  if (!enabled()) {
    return false;
  }
  const bool erased_pair = eraseBlobIfExists(makeKey(RecordKind::Pair, local, peer));
  const bool erased_index = removePairIndexEntry(local, peer);
  return erased_pair && erased_index;
}

bool PairingStore::saveChannel(const MacAddress& local, const MacAddress& peer, uint8_t channel) {
  if (!enabled()) {
    return false;
  }
  const uint8_t value[1] = {channel};
  const std::string key = makeKey(RecordKind::Channel, local, peer);
  if (!putBlobIfChanged(key, value, sizeof(value))) {
    return false;
  }
  return true;
}

bool PairingStore::loadChannel(const MacAddress& local, const MacAddress& peer, uint8_t& out_channel) {
  if (!enabled()) {
    return false;
  }
  std::vector<uint8_t> blob;
  const std::string key = makeKey(RecordKind::Channel, local, peer);
  if (!getBlobCached(key, blob)) {
    return false;
  }
  if (blob.size() != 1) {
    return false;
  }
  out_channel = blob[0];
  return true;
}

bool PairingStore::eraseChannel(const MacAddress& local, const MacAddress& peer) {
  if (!enabled()) {
    return false;
  }
  return eraseBlobIfExists(makeKey(RecordKind::Channel, local, peer));
}

bool PairingStore::saveMetaBlob(const MacAddress& local,
                                const MacAddress& peer,
                                uint8_t slot,
                                const uint8_t* data,
                                size_t len) {
  if (!enabled() || data == nullptr || len == 0) {
    return false;
  }
  const std::string key = makeMetaKey(local, peer, slot);
  if (!putBlobIfChanged(key, data, len)) {
    return false;
  }
  return true;
}

bool PairingStore::loadMetaBlob(const MacAddress& local,
                                const MacAddress& peer,
                                uint8_t slot,
                                std::vector<uint8_t>& out) {
  if (!enabled()) {
    return false;
  }
  const std::string key = makeMetaKey(local, peer, slot);
  return getBlobCached(key, out);
}

bool PairingStore::eraseMetaBlob(const MacAddress& local, const MacAddress& peer, uint8_t slot) {
  if (!enabled()) {
    return false;
  }
  return eraseBlobIfExists(makeMetaKey(local, peer, slot));
}

bool PairingStore::savePairIndex(const MacAddress& local, const std::vector<PairIndexEntry>& entries) {
  if (!enabled()) {
    return false;
  }
  const std::string key = pairIndexKey(local);
  if (entries.empty()) {
    return eraseBlobIfExists(key);
  }
  if (entries.size() > static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
    return false;
  }

  std::vector<uint8_t> blob;
  blob.reserve(kPairIndexHeaderBytes + (entries.size() * kPairIndexEntryBytes));
  blob.push_back(kPairIndexSchemaVersion);
  blob.push_back(0);  // flags
  const uint16_t count = static_cast<uint16_t>(entries.size());
  blob.push_back(static_cast<uint8_t>(count & 0xFF));
  blob.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));

  for (const auto& e : entries) {
    blob.insert(blob.end(), e.peer_mac.begin(), e.peer_mac.end());
    appendU32Le(blob, e.pair_seq);
    blob.push_back(e.valid ? 1U : 0U);
  }

  if (!putBlobIfChanged(key, blob.data(), blob.size())) {
    return false;
  }
  return true;
}

bool PairingStore::loadPairIndex(const MacAddress& local, std::vector<PairIndexEntry>& out_entries) {
  out_entries.clear();
  if (!enabled()) {
    return false;
  }

  std::vector<uint8_t> blob;
  const std::string key = pairIndexKey(local);
  if (!getBlobCached(key, blob)) {
    return false;
  }
  if (blob.size() < kPairIndexHeaderBytes) {
    return false;
  }
  if (blob[0] != kPairIndexSchemaVersion) {
    return false;
  }

  const uint16_t count = static_cast<uint16_t>(blob[2]) |
                         (static_cast<uint16_t>(blob[3]) << 8);
  const size_t expected_bytes = kPairIndexHeaderBytes + (static_cast<size_t>(count) * kPairIndexEntryBytes);
  if (blob.size() != expected_bytes) {
    return false;
  }

  out_entries.reserve(count);
  size_t offset = kPairIndexHeaderBytes;
  for (uint16_t i = 0; i < count; ++i) {
    PairIndexEntry e{};
    for (size_t m = 0; m < e.peer_mac.size(); ++m) {
      e.peer_mac[m] = blob[offset + m];
    }
    e.pair_seq = readU32Le(blob.data() + offset + 6);
    e.valid = (blob[offset + 10] != 0U);
    out_entries.push_back(e);
    offset += kPairIndexEntryBytes;
  }
  return true;
}

bool PairingStore::upsertPairIndexEntry(const MacAddress& local, const MacAddress& peer, uint32_t pair_seq) {
  if (!enabled()) {
    return false;
  }

  std::vector<PairIndexEntry> entries;
  if (!loadPairIndex(local, entries)) {
    entries.clear();
  }

  for (auto& e : entries) {
    if (e.peer_mac == peer) {
      e.pair_seq = pair_seq;
      e.valid = true;
      return savePairIndex(local, entries);
    }
  }

  PairIndexEntry e{};
  e.peer_mac = peer;
  e.pair_seq = pair_seq;
  e.valid = true;
  entries.push_back(e);
  return savePairIndex(local, entries);
}

bool PairingStore::removePairIndexEntry(const MacAddress& local, const MacAddress& peer) {
  if (!enabled()) {
    return false;
  }

  std::vector<PairIndexEntry> entries;
  if (!loadPairIndex(local, entries)) {
    return true;
  }

  entries.erase(std::remove_if(entries.begin(),
                               entries.end(),
                               [&](const PairIndexEntry& e) { return e.peer_mac == peer; }),
                entries.end());
  return savePairIndex(local, entries);
}

bool PairingStore::savePairSeqCounter(const MacAddress& local, uint32_t counter) {
  if (!enabled()) {
    return false;
  }

  std::vector<uint8_t> blob;
  blob.reserve(kPairSeqBlobBytes);
  blob.push_back(kPairSeqSchemaVersion);
  appendU32Le(blob, counter);
  const std::string key = pairSeqCounterKey(local);
  if (!putBlobIfChanged(key, blob.data(), blob.size())) {
    return false;
  }
  return true;
}

bool PairingStore::loadPairSeqCounter(const MacAddress& local, uint32_t& out_counter) {
  if (!enabled()) {
    return false;
  }

  std::vector<uint8_t> blob;
  const std::string key = pairSeqCounterKey(local);
  if (!getBlobCached(key, blob)) {
    return false;
  }
  if (blob.size() != kPairSeqBlobBytes || blob[0] != kPairSeqSchemaVersion) {
    return false;
  }
  out_counter = readU32Le(blob.data() + 1);
  return true;
}

bool PairingStore::clearPairSeqCounter(const MacAddress& local) {
  if (!enabled()) {
    return false;
  }
  return eraseBlobIfExists(pairSeqCounterKey(local));
}

void PairingStore::markBlobCacheEntryUsed_(BlobCacheEntry& entry) {
  ++blob_cache_access_seq_;
  if (blob_cache_access_seq_ == 0U) {
    blob_cache_access_seq_ = 1U;
    for (auto& kv : blob_cache_) {
      kv.second.last_used = 0U;
    }
  }
  entry.last_used = blob_cache_access_seq_;
}

void PairingStore::enforceBlobCacheLimit_(const std::string* preserve_key) {
  while (blob_cache_.size() > kBlobCacheMaxEntries) {
    auto evict_it = blob_cache_.end();
    uint32_t oldest_used = std::numeric_limits<uint32_t>::max();
    for (auto it = blob_cache_.begin(); it != blob_cache_.end(); ++it) {
      if (preserve_key != nullptr && it->first == *preserve_key) {
        continue;
      }
      if (evict_it == blob_cache_.end() || it->second.last_used < oldest_used) {
        evict_it = it;
        oldest_used = it->second.last_used;
      }
    }
    if (evict_it == blob_cache_.end()) {
      break;
    }
    blob_cache_.erase(evict_it);
  }
}

bool PairingStore::putBlobIfChanged(const std::string& key, const uint8_t* data, size_t len) {
  if (!enabled() || data == nullptr || len == 0) {
    return false;
  }

  auto it = blob_cache_.find(key);
  if (it != blob_cache_.end() && it->second.known && it->second.exists) {
    markBlobCacheEntryUsed_(it->second);
    const auto& cached = it->second.value;
    if (cached.size() == len && std::memcmp(cached.data(), data, len) == 0) {
      return true;
    }
  } else {
    std::vector<uint8_t> existing;
    if (backend_->getBlob(key, existing)) {
      BlobCacheEntry& entry = blob_cache_[key];
      entry.known = true;
      entry.exists = true;
      entry.value = std::move(existing);
      markBlobCacheEntryUsed_(entry);
      enforceBlobCacheLimit_(&key);
      if (entry.value.size() == len && std::memcmp(entry.value.data(), data, len) == 0) {
        return true;
      }
    } else {
      BlobCacheEntry& entry = blob_cache_[key];
      entry.known = true;
      entry.exists = false;
      entry.value.clear();
      markBlobCacheEntryUsed_(entry);
      enforceBlobCacheLimit_(&key);
    }
  }

  if (!backend_->putBlob(key, data, len)) {
    return false;
  }

  BlobCacheEntry& entry = blob_cache_[key];
  entry.known = true;
  entry.exists = true;
  entry.value.assign(data, data + len);
  markBlobCacheEntryUsed_(entry);
  enforceBlobCacheLimit_(&key);
  return true;
}

bool PairingStore::getBlobCached(const std::string& key, std::vector<uint8_t>& out) {
  if (!enabled()) {
    return false;
  }

  auto it = blob_cache_.find(key);
  if (it != blob_cache_.end() && it->second.known) {
    markBlobCacheEntryUsed_(it->second);
    if (!it->second.exists) {
      return false;
    }
    out = it->second.value;
    return true;
  }

  std::vector<uint8_t> blob;
  if (!backend_->getBlob(key, blob)) {
    BlobCacheEntry& entry = blob_cache_[key];
    entry.known = true;
    entry.exists = false;
    entry.value.clear();
    markBlobCacheEntryUsed_(entry);
    enforceBlobCacheLimit_(&key);
    return false;
  }

  BlobCacheEntry& entry = blob_cache_[key];
  entry.known = true;
  entry.exists = true;
  entry.value = std::move(blob);
  markBlobCacheEntryUsed_(entry);
  enforceBlobCacheLimit_(&key);
  out = entry.value;
  return true;
}

bool PairingStore::eraseBlobIfExists(const std::string& key) {
  if (!enabled()) {
    return false;
  }

  auto it = blob_cache_.find(key);
  if (it != blob_cache_.end() && it->second.known && !it->second.exists) {
    markBlobCacheEntryUsed_(it->second);
    return true;
  }

  if (it == blob_cache_.end() || !it->second.known) {
    std::vector<uint8_t> existing;
    if (!backend_->getBlob(key, existing)) {
      BlobCacheEntry& entry = blob_cache_[key];
      entry.known = true;
      entry.exists = false;
      entry.value.clear();
      markBlobCacheEntryUsed_(entry);
      enforceBlobCacheLimit_(&key);
      return true;
    }
  }

  if (!backend_->eraseKey(key)) {
    return false;
  }

  BlobCacheEntry& entry = blob_cache_[key];
  entry.known = true;
  entry.exists = false;
  entry.value.clear();
  markBlobCacheEntryUsed_(entry);
  enforceBlobCacheLimit_(&key);
  return true;
}

std::string PairingStore::makeKey(RecordKind kind, const MacAddress& local, const MacAddress& peer) const {
  char prefix = 'm';
  if (kind == RecordKind::Pair) {
    prefix = 'p';
  } else if (kind == RecordKind::Channel) {
    prefix = 'c';
  }
  const uint32_t h = hashKeyMaterial(kind, local, peer, config_.schema_version, 0);
  char buf[8] = {0};
  std::snprintf(buf, sizeof(buf), "%c%06X", prefix, static_cast<unsigned int>(h & 0xFFFFFFU));
  return std::string(buf);
}

std::string PairingStore::makeMetaKey(const MacAddress& local, const MacAddress& peer, uint8_t slot) const {
  const uint32_t h = hashKeyMaterial(RecordKind::Meta, local, peer, config_.schema_version, slot);
  char buf[8] = {0};
  std::snprintf(buf, sizeof(buf), "m%06X", static_cast<unsigned int>(h & 0xFFFFFFU));
  return std::string(buf);
}

std::string PairingStore::makeLocalMetaKey(const MacAddress& local,
                                           const char* prefix,
                                           uint8_t salt) const {
  const uint32_t h = hashKeyMaterial(RecordKind::Meta, local, MacAddress{}, config_.schema_version, salt);
  char p = 'm';
  if (prefix != nullptr) {
    if (std::strcmp(prefix, "pidx") == 0) {
      p = 'i';
    } else if (std::strcmp(prefix, "pseq") == 0) {
      p = 'q';
    }
  }
  char buf[8] = {0};
  std::snprintf(buf, sizeof(buf), "%c%06X", p, static_cast<unsigned int>(h & 0xFFFFFFU));
  return std::string(buf);
}

std::string PairingStore::pairIndexKey(const MacAddress& local) const {
  return makeLocalMetaKey(local, "pidx", kKeySaltPairIndex);
}

std::string PairingStore::pairSeqCounterKey(const MacAddress& local) const {
  return makeLocalMetaKey(local, "pseq", kKeySaltPairSeqCounter);
}

uint32_t PairingStore::hashKeyMaterial(RecordKind kind,
                                       const MacAddress& local,
                                       const MacAddress& peer,
                                       uint8_t schema_version,
                                       uint8_t salt) {
  uint32_t h = kFnvOffset;
  hashByte(h, static_cast<uint8_t>(kind));
  hashByte(h, schema_version);
  hashByte(h, salt);
  hashMac(h, local);
  hashMac(h, peer);
  return h;
}

}  // namespace espnow_link
