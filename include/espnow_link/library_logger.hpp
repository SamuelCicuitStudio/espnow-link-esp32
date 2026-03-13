#pragma once

#include <cstdint>
#include <vector>

#include "espnow_link/log_storage.hpp"

namespace espnow_link {

/**
 * @brief Severity level stored in compact binary log records.
 */
enum class LibraryLogLevel : uint8_t {
  Error = 0,
  Warn = 1,
  Info = 2,
  Debug = 3,
};

/**
 * @brief Compact numeric log event payload.
 */
struct LibraryLogRecord {
  LibraryLogLevel level = LibraryLogLevel::Info;
  uint16_t source_id = 0;
  uint16_t event_id = 0;
  uint32_t epoch_s = 0;
  uint32_t uptime_ms = 0;
  int32_t p0 = 0;
  int32_t p1 = 0;
  int32_t p2 = 0;
  std::vector<uint8_t> ext{};
};

/**
 * @brief Binary logger that encodes records and appends them to `EspLogStore`.
 */
class LibraryLogger {
 public:
  /**
   * @brief Construct logger.
   * @param store Optional store attachment.
   */
  explicit LibraryLogger(EspLogStore* store = nullptr);

  /**
   * @brief Attach or replace backing store.
   * @param store Store pointer (nullable).
   */
  void attachStore(EspLogStore* store);

  /**
   * @brief Enable/disable persistent logging.
   * @param enabled True to enable.
   */
  void setEnabled(bool enabled);

  /** @brief Current enabled state. */
  bool enabled() const { return enabled_; }

  /**
   * @brief Set minimum accepted log level.
   * @param level Maximum verbosity level to keep.
   */
  void setMinLevel(LibraryLogLevel level);

  /** @brief Current minimum log level. */
  LibraryLogLevel minLevel() const { return min_level_; }

  /**
   * @brief Encode and append one record.
   * @param record Input event record.
   * @return True when handled successfully.
   */
  bool log(const LibraryLogRecord& record);

  /**
   * @brief Read raw encoded chunk from backing store.
   * @param offset Byte offset.
   * @param out Output buffer.
   * @param max_len Requested max bytes.
   * @param out_len Returned bytes.
   * @param total_size Current file size.
   */
  bool readChunk(uint32_t offset,
                 uint8_t* out,
                 size_t max_len,
                 size_t& out_len,
                 uint32_t& total_size) const;

  /** @brief Clear stored logs. */
  bool clear();

  /**
   * @brief Get backing store stats.
   * @param out Stats output.
   */
  bool stats(LogStorageStats& out) const;

  /** @brief Last assigned sequence id. */
  uint32_t lastSequence() const { return last_sequence_; }

 private:
  bool shouldStore(LibraryLogLevel level) const;
  bool encodeRecord(const LibraryLogRecord& record, uint32_t seq, std::vector<uint8_t>& out) const;
  static void appendU16(std::vector<uint8_t>& out, uint16_t v);
  static void appendU32(std::vector<uint8_t>& out, uint32_t v);
  static void appendI32(std::vector<uint8_t>& out, int32_t v);
  static uint16_t crc16Ccitt(const uint8_t* data, size_t len);

  EspLogStore* store_ = nullptr;
  bool enabled_ = true;
  LibraryLogLevel min_level_ = LibraryLogLevel::Info;
  uint32_t last_sequence_ = 0;
};

}  // namespace espnow_link

