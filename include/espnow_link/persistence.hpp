#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "espnow_link/config.hpp"
#include "espnow_link/types.hpp"

namespace espnow_link {

/** @brief Persistence record family used for key derivation. */
enum class RecordKind : uint8_t {
  Pair = 1,
  Channel = 2,
  Meta = 3,
};

/** @brief Stored secure pairing record. */
struct PairRecord {
  bool valid = false;
  Role local_role = Role::Slave;
  MacAddress local_mac{};
  MacAddress peer_mac{};
  LmkKey lmk{};
  uint8_t channel = 1;
};

/** @brief One persisted paired-peer index entry used for deterministic restore ordering. */
struct PairIndexEntry {
  MacAddress peer_mac{};
  uint32_t pair_seq = 0;
  bool valid = true;
};

/**
 * @brief Generic storage backend abstraction used by `PairingStore`.
 */
class IPersistenceBackend {
 public:
  virtual ~IPersistenceBackend() = default;

  /** @brief Save raw blob under backend key. */
  virtual bool putBlob(const std::string& key, const uint8_t* data, size_t len) = 0;
  /** @brief Load raw blob by backend key. */
  virtual bool getBlob(const std::string& key, std::vector<uint8_t>& out) = 0;
  /** @brief Erase backend key if present. */
  virtual bool eraseKey(const std::string& key) = 0;
};

/**
 * @brief Keyed persistence helper for pairs, channels, and metadata.
 */
class PairingStore {
 public:
  /**
   * @brief Construct store with config and backend.
   * @param config Persistence behavior and namespace settings.
   * @param backend Backend implementation; may be null when disabled.
   */
  PairingStore(const PersistenceConfig& config, IPersistenceBackend* backend);

  /** @brief Check whether persistence is currently enabled and usable. */
  bool enabled() const;

  /** @brief Save full pair record. */
  bool savePair(const PairRecord& record);
  /** @brief Load full pair record by local+peer MAC. */
  bool loadPair(const MacAddress& local, const MacAddress& peer, PairRecord& out_record);
  /** @brief Erase pair record by local+peer MAC. */
  bool erasePair(const MacAddress& local, const MacAddress& peer);

  /** @brief Save channel binding for local+peer pair. */
  bool saveChannel(const MacAddress& local, const MacAddress& peer, uint8_t channel);
  /** @brief Load channel binding for local+peer pair. */
  bool loadChannel(const MacAddress& local, const MacAddress& peer, uint8_t& out_channel);
  /** @brief Erase channel binding for local+peer pair. */
  bool eraseChannel(const MacAddress& local, const MacAddress& peer);

  /** @brief Save opaque metadata blob scoped by local+peer+slot. */
  bool saveMetaBlob(const MacAddress& local, const MacAddress& peer, uint8_t slot, const uint8_t* data, size_t len);
  /** @brief Load opaque metadata blob scoped by local+peer+slot. */
  bool loadMetaBlob(const MacAddress& local, const MacAddress& peer, uint8_t slot, std::vector<uint8_t>& out);
  /** @brief Erase opaque metadata blob scoped by local+peer+slot. */
  bool eraseMetaBlob(const MacAddress& local, const MacAddress& peer, uint8_t slot);

  /** @brief Save full paired-peer index for one local node. */
  bool savePairIndex(const MacAddress& local, const std::vector<PairIndexEntry>& entries);
  /** @brief Load full paired-peer index for one local node. */
  bool loadPairIndex(const MacAddress& local, std::vector<PairIndexEntry>& out_entries);
  /** @brief Insert/update one paired-peer index entry. */
  bool upsertPairIndexEntry(const MacAddress& local, const MacAddress& peer, uint32_t pair_seq);
  /** @brief Remove one paired-peer index entry by peer MAC. */
  bool removePairIndexEntry(const MacAddress& local, const MacAddress& peer);

  /** @brief Save next pair-sequence counter for one local node. */
  bool savePairSeqCounter(const MacAddress& local, uint32_t counter);
  /** @brief Load next pair-sequence counter for one local node. */
  bool loadPairSeqCounter(const MacAddress& local, uint32_t& out_counter);
  /** @brief Clear pair-sequence counter for one local node. */
  bool clearPairSeqCounter(const MacAddress& local);

  /**
   * @brief Build deterministic storage key for record kind and pair tuple.
   * @param kind Record category.
   * @param local Local MAC.
   * @param peer Peer MAC.
   * @return Derived backend key string.
   */
  std::string makeKey(RecordKind kind, const MacAddress& local, const MacAddress& peer) const;

 private:
  struct BlobCacheEntry {
    bool known = false;
    bool exists = false;
    std::vector<uint8_t> value{};
  };

  bool putBlobIfChanged(const std::string& key, const uint8_t* data, size_t len);
  bool getBlobCached(const std::string& key, std::vector<uint8_t>& out);
  bool eraseBlobIfExists(const std::string& key);

  std::string makeMetaKey(const MacAddress& local, const MacAddress& peer, uint8_t slot) const;
  std::string makeLocalMetaKey(const MacAddress& local, const char* prefix, uint8_t salt) const;
  std::string pairIndexKey(const MacAddress& local) const;
  std::string pairSeqCounterKey(const MacAddress& local) const;
  static uint32_t hashKeyMaterial(RecordKind kind,
                                  const MacAddress& local,
                                  const MacAddress& peer,
                                  uint8_t schema_version,
                                  uint8_t salt);

  PersistenceConfig config_;
  IPersistenceBackend* backend_ = nullptr;
  std::unordered_map<std::string, BlobCacheEntry> blob_cache_{};
};

}  // namespace espnow_link
