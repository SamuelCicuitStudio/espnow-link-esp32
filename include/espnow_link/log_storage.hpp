#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace espnow_link {

/**
 * @brief Configuration for the on-device binary log store.
 */
struct LogStoreConfig {
  std::string log_path = "/elg.bin";
  std::string index_path = "/elg.idx";
  uint32_t max_bytes = 1572864U;  // 1.5 MiB
};

/**
 * @brief Runtime statistics for the log store.
 */
struct LogStorageStats {
  bool available = false;
  uint32_t bytes_used = 0;
  uint32_t bytes_dropped = 0;
  uint32_t records_appended = 0;
  uint32_t rotations = 0;
};

/**
 * @brief Abstract filesystem-like backend used by `EspLogStore`.
 *
 * Implementations can wrap SPIFFS/LittleFS/other storage.
 */
class ILogStorageBackend {
 public:
  virtual ~ILogStorageBackend() = default;

  /** @brief Initialize backend and mount storage if needed. */
  virtual bool begin() = 0;

  /**
   * @brief Read bytes from file path.
   * @param path File path.
   * @param offset Byte offset.
   * @param out Output buffer.
   * @param max_len Maximum bytes to read.
   * @param out_len Number of bytes actually read.
   */
  virtual bool read(const std::string& path,
                    uint32_t offset,
                    uint8_t* out,
                    size_t max_len,
                    size_t& out_len) = 0;

  /**
   * @brief Append bytes to file path.
   * @param path File path.
   * @param data Bytes to append.
   * @param len Number of bytes to append.
   */
  virtual bool append(const std::string& path, const uint8_t* data, size_t len) = 0;

  /**
   * @brief Get file size in bytes.
   * @param path File path.
   * @param out_size Output size.
   */
  virtual bool size(const std::string& path, uint32_t& out_size) = 0;

  /**
   * @brief Truncate file to zero bytes.
   * @param path File path.
   */
  virtual bool truncate(const std::string& path) = 0;

  /**
   * @brief Remove file if present.
   * @param path File path.
   */
  virtual bool remove(const std::string& path) = 0;
};

/**
 * @brief Bounded binary log file store with deterministic rotate-on-full behavior.
 *
 * Current phase uses "truncate and continue" rotation when max size is exceeded.
 */
class EspLogStore {
 public:
  /**
   * @brief Construct log store with backend and config.
   * @param backend Storage backend.
   * @param config Store configuration.
   */
  explicit EspLogStore(ILogStorageBackend& backend, const LogStoreConfig& config = {});

  /** @brief Initialize store and refresh runtime stats. */
  bool begin();

  /**
   * @brief Append one encoded log record.
   * @param data Encoded bytes.
   * @param len Byte count.
   */
  bool append(const uint8_t* data, size_t len);

  /**
   * @brief Read a chunk from the current log file.
   * @param offset Byte offset.
   * @param out Output buffer.
   * @param max_len Max bytes requested.
   * @param out_len Bytes returned.
   * @param total_size Current total file size.
   */
  bool readChunk(uint32_t offset,
                 uint8_t* out,
                 size_t max_len,
                 size_t& out_len,
                 uint32_t& total_size) const;

  /** @brief Clear log file contents. */
  bool clear();

  /**
   * @brief Get store statistics snapshot.
   * @param out Stats output.
   */
  bool stats(LogStorageStats& out) const;

  /** @brief Get immutable config. */
  const LogStoreConfig& config() const { return config_; }

  /**
   * @brief Update max rotate size in bytes.
   * @param max_bytes New max size (must be > 0).
   */
  void setMaxBytes(uint32_t max_bytes);

 private:
  bool refreshSize();

  ILogStorageBackend& backend_;
  LogStoreConfig config_;
  mutable LogStorageStats stats_{};
};

}  // namespace espnow_link
