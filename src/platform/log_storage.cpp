#include "espnow_link/log_storage.hpp"

#include <algorithm>

namespace espnow_link {

EspLogStore::EspLogStore(ILogStorageBackend& backend, const LogStoreConfig& config)
    : backend_(backend), config_(config) {}

bool EspLogStore::refreshSize() {
  uint32_t size = 0;
  if (!backend_.size(config_.log_path, size)) {
    stats_.bytes_used = 0;
    return false;
  }
  stats_.bytes_used = size;
  return true;
}

bool EspLogStore::begin() {
  stats_ = LogStorageStats{};
  stats_.available = backend_.begin();
  if (!stats_.available) {
    return false;
  }
  (void)refreshSize();
  return true;
}

bool EspLogStore::append(const uint8_t* data, size_t len) {
  if (!stats_.available || data == nullptr || len == 0U) {
    return false;
  }
  if (len > config_.max_bytes) {
    return false;
  }

  uint32_t current = 0;
  if (!backend_.size(config_.log_path, current)) {
    return false;
  }

  if (current + static_cast<uint32_t>(len) > config_.max_bytes) {
    if (!backend_.truncate(config_.log_path)) {
      return false;
    }
    stats_.bytes_dropped += current;
    stats_.rotations += 1U;
    current = 0;
  }

  if (!backend_.append(config_.log_path, data, len)) {
    return false;
  }

  stats_.records_appended += 1U;
  stats_.bytes_used = std::min<uint32_t>(config_.max_bytes, current + static_cast<uint32_t>(len));
  return true;
}

bool EspLogStore::readChunk(uint32_t offset,
                            uint8_t* out,
                            size_t max_len,
                            size_t& out_len,
                            uint32_t& total_size) const {
  out_len = 0;
  total_size = 0;
  if (!stats_.available || out == nullptr || max_len == 0U) {
    return false;
  }
  uint32_t size = 0;
  if (!backend_.size(config_.log_path, size)) {
    return false;
  }
  total_size = size;
  if (offset >= size) {
    return true;
  }
  return backend_.read(config_.log_path, offset, out, max_len, out_len);
}

bool EspLogStore::clear() {
  if (!stats_.available) {
    return false;
  }
  if (!backend_.truncate(config_.log_path)) {
    return false;
  }
  stats_.bytes_used = 0;
  return true;
}

bool EspLogStore::stats(LogStorageStats& out) const {
  out = stats_;
  uint32_t size = 0;
  if (stats_.available && backend_.size(config_.log_path, size)) {
    out.bytes_used = size;
  }
  return true;
}

void EspLogStore::setMaxBytes(uint32_t max_bytes) {
  if (max_bytes == 0U) {
    return;
  }
  config_.max_bytes = max_bytes;
}

}  // namespace espnow_link
