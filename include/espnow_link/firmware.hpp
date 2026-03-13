#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "espnow_link/types.hpp"

namespace espnow_link {

/** @brief Firmware image metadata carried with transfer begin frame. */
struct FirmwareImageMetadata {
  std::string sw_version;
  std::string build_id;
  std::string target_role;  // "master" or "slave"

  bool empty() const { return sw_version.empty() && build_id.empty() && target_role.empty(); }
};

/**
 * @brief Firmware stream sink contract for begin/chunk/end OTA flows.
 */
class IFirmwareStreamSink {
 public:
  virtual ~IFirmwareStreamSink() = default;

  /**
   * @brief Start a new firmware stream.
   * @param from Source peer MAC.
   * @param corr_id Correlation ID.
   * @param total_size Total expected image size.
   * @param chunk_size Advertised chunk size.
   * @param image_crc32 Expected image CRC32.
   * @return true if stream start accepted.
   */
  virtual bool begin(const MacAddress& from,
                     uint32_t corr_id,
                     uint32_t total_size,
                     uint32_t chunk_size,
                     uint32_t image_crc32) = 0;

  /**
   * @brief Start firmware stream with optional image metadata.
   * @param from Source peer MAC.
   * @param corr_id Correlation ID.
   * @param total_size Total expected image size.
   * @param chunk_size Advertised chunk size.
   * @param image_crc32 Expected image CRC32.
   * @param metadata Optional firmware metadata (version/build/target_role).
   * @return true if stream start accepted.
   */
  virtual bool begin(const MacAddress& from,
                     uint32_t corr_id,
                     uint32_t total_size,
                     uint32_t chunk_size,
                     uint32_t image_crc32,
                     const FirmwareImageMetadata& metadata) {
    (void)metadata;
    return begin(from, corr_id, total_size, chunk_size, image_crc32);
  }

  /**
   * @brief Write one firmware chunk.
   * @param from Source peer MAC.
   * @param corr_id Correlation ID.
   * @param offset Chunk offset in image.
   * @param data Chunk payload bytes.
   * @param len Chunk length in bytes.
   * @return true when chunk accepted.
   */
  virtual bool writeChunk(const MacAddress& from,
                          uint32_t corr_id,
                          uint32_t offset,
                          const uint8_t* data,
                          size_t len) = 0;

  /**
   * @brief Finalize firmware stream.
   * @param from Source peer MAC.
   * @param corr_id Correlation ID.
   * @param total_size Final image size.
   * @param image_crc32 Final image CRC32.
   * @return true when finalization succeeded.
   */
  virtual bool end(const MacAddress& from,
                   uint32_t corr_id,
                   uint32_t total_size,
                   uint32_t image_crc32) = 0;

  /**
   * @brief Abort current firmware stream.
   * @param from Source peer MAC.
   * @param corr_id Correlation ID.
   */
  virtual void abort(const MacAddress& from, uint32_t corr_id) = 0;

  /**
   * @brief Check whether a boot-confirmed OTA completion notice is pending.
   * @param out_meta Confirmed image metadata.
   * @param out_epoch_s Confirmation epoch.
   * @return true when a one-shot completion notice is pending.
   */
  virtual bool peekBootCompletionNotice(FirmwareImageMetadata& out_meta, uint32_t& out_epoch_s) const {
    (void)out_meta;
    (void)out_epoch_s;
    return false;
  }

  /**
   * @brief Clear pending boot-confirmed OTA completion notice.
   * @param out_message Optional status message.
   * @return true on success.
   */
  virtual bool clearBootCompletionNotice(std::string* out_message = nullptr) {
    if (out_message != nullptr) {
      out_message->clear();
    }
    return true;
  }

  /**
   * @brief Report highest contiguous byte offset fully received for active transfer.
   *
   * Used by sender-side recovery logic to ACK/NACK based on contiguous progress.
   * Default implementation returns 0 for sinks that do not provide this detail.
   */
  virtual uint32_t contiguousReceiveSize() const {
    return 0U;
  }

  /**
   * @brief Last OTA status code produced by sink operations.
   *
   * Used to propagate precise finalize failure reason back to sender.
   * Default maps to `invalid_state` (0x0006).
   */
  virtual uint16_t lastOtaStatusCode() const {
    return 0x0006U;
  }
};

}  // namespace espnow_link
