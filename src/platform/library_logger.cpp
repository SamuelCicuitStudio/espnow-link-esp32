#include "espnow_link/library_logger.hpp"

#include <algorithm>
#include <cstddef>

namespace espnow_link {
namespace {

constexpr uint16_t kLogMagic = 0x454C;
constexpr uint8_t kLogVersion = 1U;
constexpr size_t kFixedRecordSize = 2U + 1U + 1U + 2U + 2U + 4U + 4U + 4U + 4U + 4U + 4U + 1U + 2U;

}  // namespace

LibraryLogger::LibraryLogger(EspLogStore* store) : store_(store) {}

void LibraryLogger::attachStore(EspLogStore* store) {
  store_ = store;
}

void LibraryLogger::setEnabled(bool enabled) {
  enabled_ = enabled;
}

void LibraryLogger::setMinLevel(LibraryLogLevel level) {
  min_level_ = level;
}

bool LibraryLogger::shouldStore(LibraryLogLevel level) const {
  return static_cast<uint8_t>(level) <= static_cast<uint8_t>(min_level_);
}

void LibraryLogger::appendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void LibraryLogger::appendU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void LibraryLogger::appendI32(std::vector<uint8_t>& out, int32_t v) {
  appendU32(out, static_cast<uint32_t>(v));
}

uint16_t LibraryLogger::crc16Ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t b = 0; b < 8U; ++b) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

bool LibraryLogger::encodeRecord(const LibraryLogRecord& record, uint32_t seq, std::vector<uint8_t>& out) const {
  const size_t ext_len = std::min<size_t>(record.ext.size(), 255U);
  out.clear();
  out.reserve(kFixedRecordSize + ext_len);

  appendU16(out, kLogMagic);
  out.push_back(kLogVersion);
  out.push_back(static_cast<uint8_t>(record.level));
  appendU16(out, record.source_id);
  appendU16(out, record.event_id);
  appendU32(out, seq);
  appendU32(out, record.epoch_s);
  appendU32(out, record.uptime_ms);
  appendI32(out, record.p0);
  appendI32(out, record.p1);
  appendI32(out, record.p2);
  out.push_back(static_cast<uint8_t>(ext_len));
  if (ext_len > 0U) {
    out.insert(out.end(), record.ext.begin(), record.ext.begin() + static_cast<std::ptrdiff_t>(ext_len));
  }
  const uint16_t crc = crc16Ccitt(out.data(), out.size());
  appendU16(out, crc);
  return true;
}

bool LibraryLogger::log(const LibraryLogRecord& record) {
  if (!enabled_) {
    return true;
  }
  if (!shouldStore(record.level)) {
    return true;
  }
  if (store_ == nullptr) {
    return false;
  }

  const uint32_t seq = last_sequence_ + 1U;
  std::vector<uint8_t> bytes;
  if (!encodeRecord(record, seq, bytes)) {
    return false;
  }
  if (!store_->append(bytes.data(), bytes.size())) {
    return false;
  }
  last_sequence_ = seq;
  return true;
}

bool LibraryLogger::readChunk(uint32_t offset,
                              uint8_t* out,
                              size_t max_len,
                              size_t& out_len,
                              uint32_t& total_size) const {
  if (store_ == nullptr) {
    out_len = 0;
    total_size = 0;
    return false;
  }
  return store_->readChunk(offset, out, max_len, out_len, total_size);
}

bool LibraryLogger::clear() {
  if (store_ == nullptr) {
    return false;
  }
  last_sequence_ = 0;
  return store_->clear();
}

bool LibraryLogger::stats(LogStorageStats& out) const {
  if (store_ == nullptr) {
    out = LogStorageStats{};
    return false;
  }
  return store_->stats(out);
}

}  // namespace espnow_link
