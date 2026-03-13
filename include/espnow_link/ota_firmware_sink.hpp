#pragma once

#include <string>

#include "espnow_link/firmware.hpp"
#include "espnow_link/ota_manager.hpp"

namespace espnow_link {

/** @brief `IFirmwareStreamSink` bridge that routes begin/chunk/end into `OtaManager`. */
class OtaFirmwareSink final : public IFirmwareStreamSink {
 public:
  explicit OtaFirmwareSink(OtaManager& manager) : manager_(manager) {}

  bool begin(const MacAddress& from,
             uint32_t corr_id,
             uint32_t total_size,
             uint32_t chunk_size,
             uint32_t image_crc32) override;

  bool begin(const MacAddress& from,
             uint32_t corr_id,
             uint32_t total_size,
             uint32_t chunk_size,
             uint32_t image_crc32,
             const FirmwareImageMetadata& metadata) override;

  bool writeChunk(const MacAddress& from,
                  uint32_t corr_id,
                  uint32_t offset,
                  const uint8_t* data,
                  size_t len) override;

  bool end(const MacAddress& from,
           uint32_t corr_id,
           uint32_t total_size,
           uint32_t image_crc32) override;

  void abort(const MacAddress& from, uint32_t corr_id) override;

  bool peekBootCompletionNotice(FirmwareImageMetadata& out_meta, uint32_t& out_epoch_s) const override;
  bool clearBootCompletionNotice(std::string* out_message = nullptr) override;
  uint32_t contiguousReceiveSize() const override;
  uint16_t lastOtaStatusCode() const override;

  /** @brief Last sink operation message. */
  const std::string& lastMessage() const { return last_message_; }

  /** @brief Last finalized image path on successful `end()`. */
  const std::string& lastImagePath() const { return last_image_path_; }

 private:
  OtaManager& manager_;
  std::string last_message_{};
  std::string last_image_path_{};
};

}  // namespace espnow_link
