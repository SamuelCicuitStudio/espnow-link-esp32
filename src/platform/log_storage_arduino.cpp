#include "espnow_link/log_storage_arduino.hpp"

#if defined(ARDUINO)

namespace espnow_link {

ArduinoSpiffsLogStorageBackend::ArduinoSpiffsLogStorageBackend(bool format_on_fail, OwnershipMode ownership)
    : format_on_fail_(format_on_fail), ownership_(ownership) {}

bool ArduinoSpiffsLogStorageBackend::probeMounted_() const {
  static constexpr const char* kProbePath = "/.__enl_probe__";
  File f = SPIFFS.open(kProbePath, FILE_APPEND);
  if (!f) {
    return false;
  }
  f.close();
  (void)SPIFFS.remove(kProbePath);
  return true;
}

bool ArduinoSpiffsLogStorageBackend::begin() {
  if (begun_) {
    return true;
  }
  if (ownership_ == OwnershipMode::AutoMount) {
    begun_ = SPIFFS.begin(format_on_fail_);
  } else {
    begun_ = probeMounted_();
  }
  return begun_;
}

bool ArduinoSpiffsLogStorageBackend::read(const std::string& path,
                                          uint32_t offset,
                                          uint8_t* out,
                                          size_t max_len,
                                          size_t& out_len) {
  out_len = 0;
  if (!begun_ || out == nullptr || max_len == 0U) {
    return false;
  }
  File f = SPIFFS.open(path.c_str(), FILE_READ);
  if (!f) {
    return false;
  }
  const size_t file_size = static_cast<size_t>(f.size());
  if (offset >= file_size) {
    f.close();
    return true;
  }
  if (!f.seek(offset, SeekSet)) {
    f.close();
    return false;
  }
  out_len = static_cast<size_t>(f.read(out, max_len));
  f.close();
  // If caller requested a range that should exist, zero-byte read is an error.
  if (out_len == 0U && offset < file_size) {
    return false;
  }
  return true;
}

bool ArduinoSpiffsLogStorageBackend::append(const std::string& path, const uint8_t* data, size_t len) {
  if (!begun_ || data == nullptr || len == 0U) {
    return false;
  }
  File f = SPIFFS.open(path.c_str(), FILE_APPEND);
  if (!f) {
    f = SPIFFS.open(path.c_str(), FILE_WRITE);
  }
  if (!f) {
    return false;
  }
  const size_t written = static_cast<size_t>(f.write(data, len));
  f.flush();
  f.close();
  return written == len;
}

bool ArduinoSpiffsLogStorageBackend::size(const std::string& path, uint32_t& out_size) {
  out_size = 0;
  if (!begun_) {
    return false;
  }
  if (!SPIFFS.exists(path.c_str())) {
    return true;
  }
  File f = SPIFFS.open(path.c_str(), FILE_READ);
  if (!f) {
    return false;
  }
  out_size = static_cast<uint32_t>(f.size());
  f.close();
  return true;
}

bool ArduinoSpiffsLogStorageBackend::truncate(const std::string& path) {
  if (!begun_) {
    return false;
  }
  File f = SPIFFS.open(path.c_str(), FILE_WRITE);
  if (!f) {
    return false;
  }
  f.flush();
  f.close();
  return true;
}

bool ArduinoSpiffsLogStorageBackend::remove(const std::string& path) {
  if (!begun_) {
    return false;
  }
  if (!SPIFFS.exists(path.c_str())) {
    return true;
  }
  return SPIFFS.remove(path.c_str());
}

ArduinoSelectableLogStorageBackend::ArduinoSelectableLogStorageBackend(
    bool format_on_fail,
    SpiffsOwnershipMode ownership)
    : format_on_fail_(format_on_fail), spiffs_ownership_(ownership) {}

void ArduinoSelectableLogStorageBackend::bindSd(fs::FS* fs) {
  sd_fs_ = fs;
  begun_ = false;
}

void ArduinoSelectableLogStorageBackend::bindSpiffs(fs::FS* fs) {
  spiffs_fs_ = fs;
  spiffs_is_default_ = (fs == &SPIFFS);
  begun_ = false;
}

void ArduinoSelectableLogStorageBackend::setPreferredBackend(PreferredBackend preferred) {
  preferred_ = preferred;
  begun_ = false;
}

const char* ArduinoSelectableLogStorageBackend::activeBackendName() const {
  if (!has_active_) {
    return "none";
  }
  return (active_ == PreferredBackend::Sd) ? "sd" : "spiffs";
}

bool ArduinoSelectableLogStorageBackend::probeMounted_(fs::FS& fs) const {
  static constexpr const char* kProbePath = "/.__enl_probe__";
  File f = fs.open(kProbePath, FILE_APPEND);
  if (!f) {
    return false;
  }
  f.close();
  (void)fs.remove(kProbePath);
  return true;
}

bool ArduinoSelectableLogStorageBackend::ensureSpiffsReady_() {
  if (spiffs_fs_ == nullptr) {
    return false;
  }

  if (spiffs_is_default_ && spiffs_ownership_ == SpiffsOwnershipMode::AutoMount) {
    if (!SPIFFS.begin(format_on_fail_)) {
      return false;
    }
  }

  return probeMounted_(*spiffs_fs_);
}

bool ArduinoSelectableLogStorageBackend::selectActiveBackend_() {
  const bool sd_ready = (sd_fs_ != nullptr) ? probeMounted_(*sd_fs_) : false;
  const bool spiffs_ready = ensureSpiffsReady_();

  has_active_ = false;
  if (preferred_ == PreferredBackend::Sd) {
    if (sd_ready) {
      active_ = PreferredBackend::Sd;
      has_active_ = true;
      return true;
    }
    if (spiffs_ready) {
      active_ = PreferredBackend::Spiffs;
      has_active_ = true;
      return true;
    }
    return false;
  }

  if (spiffs_ready) {
    active_ = PreferredBackend::Spiffs;
    has_active_ = true;
    return true;
  }
  if (sd_ready) {
    active_ = PreferredBackend::Sd;
    has_active_ = true;
    return true;
  }
  return false;
}

fs::FS* ArduinoSelectableLogStorageBackend::activeFs_() const {
  if (!has_active_) {
    return nullptr;
  }
  return (active_ == PreferredBackend::Sd) ? sd_fs_ : spiffs_fs_;
}

bool ArduinoSelectableLogStorageBackend::begin() {
  if (begun_) {
    return true;
  }
  begun_ = selectActiveBackend_();
  return begun_;
}

bool ArduinoSelectableLogStorageBackend::read(const std::string& path,
                                              uint32_t offset,
                                              uint8_t* out,
                                              size_t max_len,
                                              size_t& out_len) {
  out_len = 0;
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr || out == nullptr || max_len == 0U) {
    return false;
  }

  const uint8_t attempts = (active_ == PreferredBackend::Sd) ? 3U : 1U;
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
    File f = fs->open(path.c_str(), FILE_READ);
    if (!f) {
      continue;
    }

    const size_t file_size = static_cast<size_t>(f.size());
    if (offset >= file_size) {
      f.close();
      return true;
    }
    if (!f.seek(offset, SeekSet)) {
      f.close();
      continue;
    }

    out_len = static_cast<size_t>(f.read(out, max_len));
    f.close();
    if (out_len > 0U) {
      return true;
    }
  }

  return false;
}

bool ArduinoSelectableLogStorageBackend::append(const std::string& path, const uint8_t* data, size_t len) {
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr || data == nullptr || len == 0U) {
    return false;
  }

  File f = fs->open(path.c_str(), FILE_APPEND);
  if (!f) {
    f = fs->open(path.c_str(), FILE_WRITE);
  }
  if (!f) {
    return false;
  }
  const size_t written = static_cast<size_t>(f.write(data, len));
  f.flush();
  f.close();
  return written == len;
}

bool ArduinoSelectableLogStorageBackend::size(const std::string& path, uint32_t& out_size) {
  out_size = 0;
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    return false;
  }
  if (!fs->exists(path.c_str())) {
    return true;
  }
  File f = fs->open(path.c_str(), FILE_READ);
  if (!f) {
    return false;
  }
  out_size = static_cast<uint32_t>(f.size());
  f.close();
  return true;
}

bool ArduinoSelectableLogStorageBackend::truncate(const std::string& path) {
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    return false;
  }
  File f = fs->open(path.c_str(), FILE_WRITE);
  if (!f) {
    return false;
  }
  f.flush();
  f.close();
  return true;
}

bool ArduinoSelectableLogStorageBackend::remove(const std::string& path) {
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    return false;
  }
  if (!fs->exists(path.c_str())) {
    return true;
  }
  return fs->remove(path.c_str());
}

}  // namespace espnow_link

#endif  // defined(ARDUINO)
