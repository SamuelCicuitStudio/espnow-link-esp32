#include "espnow_link/ota_firmware_sink.hpp"

namespace espnow_link {

bool OtaFirmwareSink::begin(const MacAddress& from,
                            uint32_t corr_id,
                            uint32_t total_size,
                            uint32_t chunk_size,
                            uint32_t image_crc32) {
  const FirmwareImageMetadata meta{};
  return begin(from, corr_id, total_size, chunk_size, image_crc32, meta);
}

bool OtaFirmwareSink::begin(const MacAddress& from,
                            uint32_t corr_id,
                            uint32_t total_size,
                            uint32_t chunk_size,
                            uint32_t image_crc32,
                            const FirmwareImageMetadata& metadata) {
  last_image_path_.clear();
  return manager_.beginReceive(from,
                               corr_id,
                               total_size,
                               chunk_size,
                               image_crc32,
                               &metadata,
                               last_message_);
}

bool OtaFirmwareSink::writeChunk(const MacAddress& from,
                                 uint32_t corr_id,
                                 uint32_t offset,
                                 const uint8_t* data,
                                 size_t len) {
  return manager_.writeReceiveChunk(from, corr_id, offset, data, len, last_message_);
}

bool OtaFirmwareSink::end(const MacAddress& from,
                          uint32_t corr_id,
                          uint32_t total_size,
                          uint32_t image_crc32) {
  return manager_.endReceive(from,
                             corr_id,
                             total_size,
                             image_crc32,
                             last_image_path_,
                             last_message_);
}

void OtaFirmwareSink::abort(const MacAddress& from, uint32_t corr_id) {
  manager_.abortReceive(from, corr_id, &last_message_);
}

bool OtaFirmwareSink::peekBootCompletionNotice(FirmwareImageMetadata& out_meta, uint32_t& out_epoch_s) const {
  return manager_.peekBootCompletionNotice(out_meta, out_epoch_s);
}

bool OtaFirmwareSink::clearBootCompletionNotice(std::string* out_message) {
  std::string msg;
  const bool ok = manager_.clearBootCompletionNotice(msg);
  if (out_message != nullptr) {
    *out_message = msg;
  }
  return ok;
}

uint32_t OtaFirmwareSink::contiguousReceiveSize() const {
  return manager_.contiguousReceiveSize();
}

uint16_t OtaFirmwareSink::lastOtaStatusCode() const {
  return static_cast<uint16_t>(manager_.status().code);
}

}  // namespace espnow_link
