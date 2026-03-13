#include "espnow_link/ota_apply_arduino.hpp"

#if defined(ARDUINO)

#include <algorithm>
#include <cstdio>
#include <vector>
#include <esp_ota_ops.h>

namespace espnow_link {

namespace {

constexpr uint32_t kCrc32Poly = 0xEDB88320U;

}  // namespace

void ArduinoOtaApplyExecutor::setChunkBytes(size_t bytes) {
  if (bytes < 128U) {
    chunk_bytes_ = 128U;
    return;
  }
  if (bytes > 4096U) {
    chunk_bytes_ = 4096U;
    return;
  }
  chunk_bytes_ = bytes;
}

OtaApplyResult ArduinoOtaApplyExecutor::applyImage(const OtaApplyRequest& request) {
  OtaApplyResult out{};
  if (request.image_path.empty() || request.image_size == 0U) {
    out.ok = false;
    out.message = "invalid apply request";
    return out;
  }

  OtaStorageStat st{};
  std::string msg;
  if (!storage_.stat(request.image_path, st, msg)) {
    out.ok = false;
    out.message = msg.empty() ? "stat failed" : msg;
    return out;
  }
  if (!st.exists || st.is_dir) {
    out.ok = false;
    out.message = "image file not found";
    return out;
  }
  if (st.size_bytes < request.image_size) {
    out.ok = false;
    out.message = "staged image shorter than declared size";
    return out;
  }

  if (!Update.begin(request.image_size, U_FLASH)) {
    out.ok = false;
    out.message = std::string("update begin failed: ") + updateErrorString_();
    return out;
  }

  std::vector<uint8_t> buf(std::max<size_t>(chunk_bytes_, 128U), 0U);
  uint32_t offset = 0U;
  uint32_t running_crc = 0xFFFFFFFFU;
  while (offset < request.image_size) {
    const size_t req = std::min<size_t>(buf.size(),
                                        static_cast<size_t>(request.image_size - offset));
    size_t read_len = 0U;
    if (!storage_.readAt(request.image_path, offset, buf.data(), req, read_len, msg)) {
      Update.abort();
      out.ok = false;
      out.message = msg.empty() ? "read image failed" : msg;
      return out;
    }
    if (read_len == 0U) {
      Update.abort();
      out.ok = false;
      out.message = "read image returned zero";
      return out;
    }

    const size_t written = Update.write(buf.data(), read_len);
    if (written != read_len) {
      Update.abort();
      out.ok = false;
      out.message = std::string("update write failed: ") + updateErrorString_();
      return out;
    }

    running_crc = crc32Update_(running_crc, buf.data(), read_len);
    offset += static_cast<uint32_t>(read_len);
  }

  if (request.image_crc32 != 0U) {
    const uint32_t final_crc = crc32Finalize_(running_crc);
    if (final_crc != request.image_crc32) {
      Update.abort();
      out.ok = false;
      out.message = "apply crc mismatch";
      return out;
    }
  }

  if (!Update.end(true)) {
    out.ok = false;
    out.message = std::string("update finalize failed: ") + updateErrorString_();
    return out;
  }

  if (!Update.isFinished()) {
    out.ok = false;
    out.message = "update not finished";
    return out;
  }

  out.ok = true;
  if (reboot_on_success_) {
    out.message = "apply succeeded; rebooting";
    delay(100);
    if (restart_cb_ != nullptr) {
      restart_cb_();
    } else {
      ESP.restart();
    }
  } else {
    out.message = "apply succeeded; reboot required";
  }
  return out;
}

OtaApplyResult ArduinoOtaApplyExecutor::rollbackImage() {
  OtaApplyResult out{};
  const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
  if (err != ESP_OK) {
    out.ok = false;
    char buf[64] = {0};
    std::snprintf(buf, sizeof(buf), "rollback failed err=0x%lX", static_cast<unsigned long>(err));
    out.message = buf;
    return out;
  }
  out.ok = true;
  out.message = "rollback requested; rebooting";
  return out;
}

uint32_t ArduinoOtaApplyExecutor::crc32Update_(uint32_t running_crc, const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0U) {
    return running_crc;
  }
  uint32_t crc = running_crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t b = 0; b < 8U; ++b) {
      const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
      crc = (crc >> 1U) ^ (kCrc32Poly & mask);
    }
  }
  return crc;
}

uint32_t ArduinoOtaApplyExecutor::crc32Finalize_(uint32_t running_crc) {
  return ~running_crc;
}

const char* ArduinoOtaApplyExecutor::updateErrorString_() {
  const uint8_t err = Update.getError();
  switch (err) {
    case UPDATE_ERROR_OK:
      return "ok";
    case UPDATE_ERROR_WRITE:
      return "write";
    case UPDATE_ERROR_ERASE:
      return "erase";
    case UPDATE_ERROR_READ:
      return "read";
    case UPDATE_ERROR_SPACE:
      return "space";
    case UPDATE_ERROR_SIZE:
      return "size";
    case UPDATE_ERROR_STREAM:
      return "stream";
    case UPDATE_ERROR_MD5:
      return "md5";
    case UPDATE_ERROR_MAGIC_BYTE:
      return "magic";
    case UPDATE_ERROR_ACTIVATE:
      return "activate";
    case UPDATE_ERROR_NO_PARTITION:
      return "no_partition";
    case UPDATE_ERROR_BAD_ARGUMENT:
      return "bad_argument";
    case UPDATE_ERROR_ABORT:
      return "aborted";
    default:
      return "unknown";
  }
}

}  // namespace espnow_link

#endif  // defined(ARDUINO)
