#include "espnow_link/ota_storage_arduino.hpp"

#if defined(ARDUINO)

#include <algorithm>
#if defined(ESP_PLATFORM)
#include <ESP.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#endif

namespace espnow_link {

namespace {

bool isDotEntryName(const std::string& name) {
  return name == "." || name == "..";
}

}  // namespace

void ArduinoSelectableOtaStorageBackend::bindSd(fs::FS* fs) {
  resetIoCache_();
  sd_fs_ = fs;
  begun_ = false;
}

void ArduinoSelectableOtaStorageBackend::bindSpiffs(fs::FS* fs) {
  resetIoCache_();
  spiffs_fs_ = fs;
  begun_ = false;
}

void ArduinoSelectableOtaStorageBackend::setPreferredBackend(PreferredBackend preferred) {
  resetIoCache_();
  preferred_ = preferred;
  begun_ = false;
}

const char* ArduinoSelectableOtaStorageBackend::activeBackendName() const {
  if (!has_active_) return "none";
  return (active_ == PreferredBackend::Sd) ? "sd" : "spiffs";
}

bool ArduinoSelectableOtaStorageBackend::moveSpiffsToSd(const std::string& from_path,
                                                        const std::string& to_path,
                                                        std::string& out_message,
                                                        size_t chunk_bytes) {
  if (!copySpiffsToSd(from_path, to_path, out_message, chunk_bytes)) {
    return false;
  }
  const std::string src = normalizePath_(from_path);
  if (!spiffs_fs_->remove(src.c_str())) {
    out_message = "copy done but source remove failed";
    return false;
  }
  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::copySpiffsToSd(const std::string& from_path,
                                                        const std::string& to_path,
                                                        std::string& out_message,
                                                        size_t chunk_bytes) {
  resetIoCache_();
  if (spiffs_fs_ == nullptr || sd_fs_ == nullptr) {
    out_message = "spiffs/sd backend is not bound";
    return false;
  }
  if (!probeMounted_(*spiffs_fs_)) {
    out_message = "spiffs backend not mounted";
    return false;
  }
  if (!probeMounted_(*sd_fs_)) {
    out_message = "sd backend not mounted";
    return false;
  }

  const std::string src = normalizePath_(from_path);
  const std::string dst = normalizePath_(to_path);
  if (src == "/") {
    out_message = "invalid source path";
    return false;
  }
  if (dst == "/") {
    out_message = "invalid destination path";
    return false;
  }

  File src_f = spiffs_fs_->open(src.c_str(), FILE_READ);
  if (!src_f) {
    out_message = "source open failed";
    return false;
  }
  if (src_f.isDirectory()) {
    src_f.close();
    out_message = "source path is directory";
    return false;
  }

  auto ensure_dir_on_fs = [&](fs::FS& fs, const std::string& path) -> bool {
    const std::string resolved = normalizePath_(path);
    if (resolved == "/") {
      return true;
    }
    std::string current = "/";
    size_t pos = 1U;
    while (pos <= resolved.size()) {
      const size_t slash = resolved.find('/', pos);
      const std::string part = (slash == std::string::npos) ? resolved.substr(pos)
                                                             : resolved.substr(pos, slash - pos);
      if (!part.empty()) {
        current = (current == "/") ? ("/" + part) : (current + "/" + part);
        if (fs.exists(current.c_str())) {
          File d = fs.open(current.c_str(), FILE_READ);
          const bool exists_dir = d && d.isDirectory();
          if (d) d.close();
          if (!exists_dir) {
            return false;
          }
        } else if (!fs.mkdir(current.c_str())) {
          return false;
        }
      }
      if (slash == std::string::npos) break;
      pos = slash + 1U;
    }
    return true;
  };

  std::string msg;
  if (!ensure_dir_on_fs(*sd_fs_, parentPath_(dst))) {
    src_f.close();
    out_message = "destination mkdir failed";
    return false;
  }

  if (sd_fs_->exists(dst.c_str())) {
    if (!removeTree_(*sd_fs_, dst, msg)) {
      src_f.close();
      out_message = msg.empty() ? "destination cleanup failed" : msg;
      return false;
    }
  }

  File dst_f = sd_fs_->open(dst.c_str(), FILE_WRITE);
  if (!dst_f) {
    src_f.close();
    out_message = "destination open failed";
    return false;
  }

  if (chunk_bytes < 128U) {
    chunk_bytes = 128U;
  }
  std::vector<uint8_t> buf(chunk_bytes, 0U);
  bool ok = true;
  while (true) {
    const int n = src_f.read(buf.data(), buf.size());
    if (n < 0) {
      out_message = "source read failed";
      ok = false;
      break;
    }
    if (n == 0) {
      break;
    }
    const size_t w = static_cast<size_t>(dst_f.write(buf.data(), static_cast<size_t>(n)));
    if (w != static_cast<size_t>(n)) {
      out_message = "destination write failed";
      ok = false;
      break;
    }
  }

  dst_f.flush();
  dst_f.close();
  src_f.close();

  if (!ok) {
    (void)removeTree_(*sd_fs_, dst, msg);
    return false;
  }

  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::copySdToSpiffs(const std::string& from_path,
                                                        const std::string& to_path,
                                                        std::string& out_message,
                                                        size_t chunk_bytes) {
  resetIoCache_();
  if (spiffs_fs_ == nullptr || sd_fs_ == nullptr) {
    out_message = "spiffs/sd backend is not bound";
    return false;
  }
  if (!probeMounted_(*sd_fs_)) {
    out_message = "sd backend not mounted";
    return false;
  }
  if (!probeMounted_(*spiffs_fs_)) {
    out_message = "spiffs backend not mounted";
    return false;
  }

  const std::string src = normalizePath_(from_path);
  const std::string dst = normalizePath_(to_path);
  if (src == "/") {
    out_message = "invalid source path";
    return false;
  }
  if (dst == "/") {
    out_message = "invalid destination path";
    return false;
  }

  File src_f = sd_fs_->open(src.c_str(), FILE_READ);
  if (!src_f) {
    out_message = "source open failed";
    return false;
  }
  if (src_f.isDirectory()) {
    src_f.close();
    out_message = "source path is directory";
    return false;
  }

  auto ensure_dir_on_fs = [&](fs::FS& fs, const std::string& path) -> bool {
    const std::string resolved = normalizePath_(path);
    if (resolved == "/") {
      return true;
    }
    std::string current = "/";
    size_t pos = 1U;
    while (pos <= resolved.size()) {
      const size_t slash = resolved.find('/', pos);
      const std::string part = (slash == std::string::npos) ? resolved.substr(pos)
                                                             : resolved.substr(pos, slash - pos);
      if (!part.empty()) {
        current = (current == "/") ? ("/" + part) : (current + "/" + part);
        if (fs.exists(current.c_str())) {
          File d = fs.open(current.c_str(), FILE_READ);
          const bool exists_dir = d && d.isDirectory();
          if (d) d.close();
          if (!exists_dir) {
            return false;
          }
        } else if (!fs.mkdir(current.c_str())) {
          return false;
        }
      }
      if (slash == std::string::npos) break;
      pos = slash + 1U;
    }
    return true;
  };

  std::string msg;
  if (!ensure_dir_on_fs(*spiffs_fs_, parentPath_(dst))) {
    src_f.close();
    out_message = "destination mkdir failed";
    return false;
  }

  if (spiffs_fs_->exists(dst.c_str())) {
    if (!removeTree_(*spiffs_fs_, dst, msg)) {
      src_f.close();
      out_message = msg.empty() ? "destination cleanup failed" : msg;
      return false;
    }
  }

  File dst_f = spiffs_fs_->open(dst.c_str(), FILE_WRITE);
  if (!dst_f) {
    src_f.close();
    out_message = "destination open failed";
    return false;
  }

  if (chunk_bytes < 128U) {
    chunk_bytes = 128U;
  }
  std::vector<uint8_t> buf(chunk_bytes, 0U);
  bool ok = true;
  while (true) {
    const int n = src_f.read(buf.data(), buf.size());
    if (n < 0) {
      out_message = "source read failed";
      ok = false;
      break;
    }
    if (n == 0) {
      break;
    }
    const size_t w = static_cast<size_t>(dst_f.write(buf.data(), static_cast<size_t>(n)));
    if (w != static_cast<size_t>(n)) {
      out_message = "destination write failed";
      ok = false;
      break;
    }
  }

  dst_f.flush();
  dst_f.close();
  src_f.close();

  if (!ok) {
    (void)removeTree_(*spiffs_fs_, dst, msg);
    return false;
  }

  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::dumpRunningFirmwareToSd(const std::string& to_path,
                                                                 uint32_t& out_size_bytes,
                                                                 uint32_t& out_crc32,
                                                                 std::string& out_message,
                                                                 size_t chunk_bytes) {
  out_size_bytes = 0U;
  out_crc32 = 0U;
  resetIoCache_();
#if !defined(ESP_PLATFORM)
  (void)to_path;
  (void)chunk_bytes;
  out_message = "unsupported platform";
  return false;
#else
  if (sd_fs_ == nullptr) {
    out_message = "sd backend not bound";
    return false;
  }
  if (!probeMounted_(*sd_fs_)) {
    out_message = "sd backend not mounted";
    return false;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    out_message = "running partition unavailable";
    return false;
  }

  size_t image_size = static_cast<size_t>(ESP.getSketchSize());
  if (image_size == 0U || image_size > running->size) {
    out_message = "invalid running image size";
    return false;
  }

  const std::string dst = normalizePath_(to_path);
  if (dst == "/") {
    out_message = "invalid destination path";
    return false;
  }

  auto ensure_dir_on_fs = [&](fs::FS& fs, const std::string& path) -> bool {
    const std::string resolved = normalizePath_(path);
    if (resolved == "/") {
      return true;
    }
    std::string current = "/";
    size_t pos = 1U;
    while (pos <= resolved.size()) {
      const size_t slash = resolved.find('/', pos);
      const std::string part = (slash == std::string::npos) ? resolved.substr(pos)
                                                             : resolved.substr(pos, slash - pos);
      if (!part.empty()) {
        current = (current == "/") ? ("/" + part) : (current + "/" + part);
        if (fs.exists(current.c_str())) {
          File d = fs.open(current.c_str(), FILE_READ);
          const bool exists_dir = d && d.isDirectory();
          if (d) d.close();
          if (!exists_dir) {
            return false;
          }
        } else if (!fs.mkdir(current.c_str())) {
          return false;
        }
      }
      if (slash == std::string::npos) break;
      pos = slash + 1U;
    }
    return true;
  };

  std::string msg;
  if (!ensure_dir_on_fs(*sd_fs_, parentPath_(dst))) {
    out_message = "destination mkdir failed";
    return false;
  }
  if (sd_fs_->exists(dst.c_str())) {
    if (!removeTree_(*sd_fs_, dst, msg)) {
      out_message = msg.empty() ? "destination cleanup failed" : msg;
      return false;
    }
  }

  File dst_f = sd_fs_->open(dst.c_str(), FILE_WRITE);
  if (!dst_f) {
    out_message = "destination open failed";
    return false;
  }

  if (chunk_bytes < 512U) {
    chunk_bytes = 512U;
  }
  std::vector<uint8_t> buf(chunk_bytes, 0U);
  constexpr uint32_t kCrc32Poly = 0xEDB88320U;
  auto crc_update = [](uint32_t running_crc, const uint8_t* data, size_t len) -> uint32_t {
    uint32_t crc = running_crc;
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint32_t>(data[i]);
      for (uint8_t b = 0; b < 8U; ++b) {
        const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
        crc = (crc >> 1U) ^ (kCrc32Poly & mask);
      }
    }
    return crc;
  };

  uint32_t crc = 0xFFFFFFFFU;
  size_t offset = 0U;
  bool ok = true;
  while (offset < image_size) {
    const size_t req = std::min<size_t>(buf.size(), image_size - offset);
    if (esp_partition_read(running, static_cast<size_t>(offset), buf.data(), req) != ESP_OK) {
      out_message = "partition read failed";
      ok = false;
      break;
    }
    const size_t w = static_cast<size_t>(dst_f.write(buf.data(), req));
    if (w != req) {
      out_message = "destination write failed";
      ok = false;
      break;
    }
    crc = crc_update(crc, buf.data(), req);
    offset += req;
#if defined(ARDUINO)
    yield();
#endif
  }
  dst_f.flush();
  dst_f.close();
  if (!ok) {
    (void)removeTree_(*sd_fs_, dst, msg);
    return false;
  }

  out_size_bytes = static_cast<uint32_t>(image_size);
  out_crc32 = ~crc;
  out_message = "ok";
  return true;
#endif
}

bool ArduinoSelectableOtaStorageBackend::removePathOnSd(const std::string& path, std::string& out_message) {
  closeCachedWrite_();
  closeCachedRead_();
  if (sd_fs_ == nullptr) {
    out_message = "sd backend not bound";
    return false;
  }
  if (!probeMounted_(*sd_fs_)) {
    out_message = "sd backend not mounted";
    return false;
  }
  const std::string resolved = normalizePath_(path);
  if (resolved == "/") {
    out_message = "cannot remove sd root";
    return false;
  }
  return removeTree_(*sd_fs_, resolved, out_message);
}

std::string ArduinoSelectableOtaStorageBackend::normalizePath_(const std::string& path) {
  if (path.empty()) return "/";

  std::vector<std::string> parts;
  std::string token;
  auto flush = [&]() {
    if (token.empty() || token == ".") {
      token.clear();
      return;
    }
    if (token == "..") {
      if (!parts.empty()) parts.pop_back();
      token.clear();
      return;
    }
    parts.push_back(token);
    token.clear();
  };

  for (char c : path) {
    const char n = (c == '\\') ? '/' : c;
    if (n == '/') {
      flush();
      continue;
    }
    token.push_back(n);
  }
  flush();

  std::string out = "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out.push_back('/');
    out += parts[i];
  }
  return out;
}

std::string ArduinoSelectableOtaStorageBackend::joinPath_(const std::string& base, const std::string& name) {
  const std::string b = normalizePath_(base);
  if (b == "/") return "/" + name;
  return b + "/" + name;
}

std::string ArduinoSelectableOtaStorageBackend::baseName_(const char* path) {
  if (path == nullptr || path[0] == '\0') return std::string();
  std::string p(path);
  const size_t pos = p.find_last_of('/');
  if (pos == std::string::npos) return p;
  if (pos + 1U >= p.size()) return std::string();
  return p.substr(pos + 1U);
}

std::string ArduinoSelectableOtaStorageBackend::parentPath_(const std::string& path) {
  const std::string p = normalizePath_(path);
  if (p == "/") return "/";
  const size_t pos = p.find_last_of('/');
  if (pos == std::string::npos || pos == 0U) return "/";
  return p.substr(0U, pos);
}

bool ArduinoSelectableOtaStorageBackend::probeMounted_(fs::FS& fs) const {
  static constexpr const char* kProbePath = "/.__enl_ota_probe__";
  File f = fs.open(kProbePath, FILE_APPEND);
  if (!f) return false;
  f.close();
  (void)fs.remove(kProbePath);
  return true;
}

bool ArduinoSelectableOtaStorageBackend::selectActive_(std::string& out_message) {
  const bool spiffs_ready = (spiffs_fs_ != nullptr) ? probeMounted_(*spiffs_fs_) : false;

  has_active_ = false;
  if (spiffs_ready) {
    active_ = PreferredBackend::Spiffs;
    has_active_ = true;
    out_message = "spiffs";
    return true;
  }

  out_message = "spiffs backend not mounted";
  return false;
}

fs::FS* ArduinoSelectableOtaStorageBackend::activeFs_() const {
  if (!has_active_) return nullptr;
  return (active_ == PreferredBackend::Sd) ? sd_fs_ : spiffs_fs_;
}

bool ArduinoSelectableOtaStorageBackend::begin(std::string& out_message) {
  // Close any cached file handles on each begin() call so command-to-command
  // metadata writes (e.g. archive manifest) are always visible immediately.
  resetIoCache_();
  if (begun_) {
    out_message = activeBackendName();
    return true;
  }
  begun_ = selectActive_(out_message);
  return begun_;
}

bool ArduinoSelectableOtaStorageBackend::ensureDir(const std::string& path, std::string& out_message) {
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }

  const std::string resolved = normalizePath_(path);
  if (resolved == "/") {
    out_message = "ok";
    return true;
  }

  std::string current = "/";
  size_t pos = 1U;
  while (pos <= resolved.size()) {
    const size_t slash = resolved.find('/', pos);
    const std::string part = (slash == std::string::npos)
                                 ? resolved.substr(pos)
                                 : resolved.substr(pos, slash - pos);
    if (!part.empty()) {
      current = (current == "/") ? ("/" + part) : (current + "/" + part);
      if (fs->exists(current.c_str())) {
        File d = fs->open(current.c_str(), FILE_READ);
        const bool exists_dir = d && d.isDirectory();
        if (d) d.close();
        if (!exists_dir) {
          out_message = "path exists but not dir: " + current;
          return false;
        }
      } else if (!fs->mkdir(current.c_str())) {
        out_message = "mkdir failed: " + current;
        return false;
      }
    }
    if (slash == std::string::npos) break;
    pos = slash + 1U;
  }

  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::stat(const std::string& path,
                                              OtaStorageStat& out_stat,
                                              std::string& out_message) {
  out_stat = OtaStorageStat{};
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }

  const std::string resolved = normalizePath_(path);
  File f = fs->open(resolved.c_str(), FILE_READ);
  if (!f) {
    out_stat.exists = false;
    out_message = "ok";
    return true;
  }

  out_stat.exists = true;
  out_stat.is_dir = f.isDirectory();
  out_stat.size_bytes = out_stat.is_dir ? 0U : static_cast<uint32_t>(f.size());
  f.close();
  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::listDir(const std::string& path,
                                                 std::vector<std::string>& out_names,
                                                 std::string& out_message) {
  out_names.clear();
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }

  const std::string resolved = normalizePath_(path);
  File dir = fs->open(resolved.c_str(), FILE_READ);
  if (!dir) {
    out_message = "path not found";
    return false;
  }
  if (!dir.isDirectory()) {
    dir.close();
    out_message = "path is not directory";
    return false;
  }

  File entry = dir.openNextFile();
  while (entry) {
    const std::string name = baseName_(entry.name());
    entry.close();
    if (!name.empty() && !isDotEntryName(name)) {
      out_names.push_back(name);
    }
    entry = dir.openNextFile();
  }
  dir.close();

  std::sort(out_names.begin(), out_names.end());
  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::truncateFile(const std::string& path, std::string& out_message) {
  closeCachedWrite_();
  closeCachedRead_();
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }

  const std::string resolved = normalizePath_(path);
  const std::string parent = parentPath_(resolved);
  if (!ensureDir(parent, out_message)) {
    return false;
  }

  File f = fs->open(resolved.c_str(), FILE_WRITE);
  if (!f) {
    out_message = "open write failed";
    return false;
  }
  f.flush();
  f.close();
  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::removeTree_(fs::FS& fs,
                                                     const std::string& path,
                                                     std::string& out_message) const {
  const std::string resolved = normalizePath_(path);
  File node = fs.open(resolved.c_str(), FILE_READ);
  if (!node) {
    out_message = "ok";
    return true;
  }
  const bool is_dir = node.isDirectory();
  node.close();

  if (!is_dir) {
    if (!fs.remove(resolved.c_str())) {
      out_message = "remove file failed: " + resolved;
      return false;
    }
    out_message = "ok";
    return true;
  }

  File dir = fs.open(resolved.c_str(), FILE_READ);
  if (!dir || !dir.isDirectory()) {
    out_message = "open dir failed: " + resolved;
    if (dir) dir.close();
    return false;
  }
  std::vector<std::string> children;
  File entry = dir.openNextFile();
  while (entry) {
    std::string child = entry.name();
    entry.close();
    if (child.empty()) {
      entry = dir.openNextFile();
      continue;
    }
    const std::string child_name = baseName_(child.c_str());
    if (child_name.empty() || isDotEntryName(child_name)) {
      entry = dir.openNextFile();
      continue;
    }
    if (child.front() != '/') {
      child = joinPath_(resolved, child);
    }
    child = normalizePath_(child);
    if (child != resolved) {
      children.push_back(child);
    }
    entry = dir.openNextFile();
  }
  dir.close();
  for (const std::string& child : children) {
    if (!removeTree_(fs, child, out_message)) {
      return false;
    }
  }

  if (resolved != "/") {
    // SPIFFS uses virtual directories; calling rmdir() emits noisy warnings.
    if (spiffs_fs_ != nullptr && (&fs == spiffs_fs_)) {
      out_message = "ok";
      return true;
    }
    if (!fs.rmdir(resolved.c_str())) {
      out_message = "remove dir failed: " + resolved;
      return false;
    }
  }

  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::removePath(const std::string& path, std::string& out_message) {
  closeCachedWrite_();
  closeCachedRead_();
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }
  const std::string resolved = normalizePath_(path);
  if (resolved == "/") {
    out_message = "cannot remove root";
    return false;
  }
  return removeTree_(*fs, resolved, out_message);
}

bool ArduinoSelectableOtaStorageBackend::renamePath(const std::string& from,
                                                    const std::string& to,
                                                    std::string& out_message) {
  closeCachedWrite_();
  closeCachedRead_();
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }
  const std::string from_path = normalizePath_(from);
  const std::string to_path = normalizePath_(to);
  if (!ensureDir(parentPath_(to_path), out_message)) {
    return false;
  }
  if (!fs->rename(from_path.c_str(), to_path.c_str())) {
    out_message = "rename failed";
    return false;
  }
  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::writeAt(const std::string& path,
                                                 uint32_t offset,
                                                 const uint8_t* data,
                                                 size_t len,
                                                 std::string& out_message) {
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }
  if (data == nullptr || len == 0U) {
    out_message = "invalid write";
    return false;
  }

  const std::string resolved = normalizePath_(path);
  const bool sequential_cache_hit =
      cached_write_file_ && cached_write_path_ == resolved && offset == cached_write_next_offset_;
  if (!sequential_cache_hit) {
    closeCachedWrite_();

    cached_write_file_ = fs->open(resolved.c_str(), "r+");
    if (!cached_write_file_ && offset == 0U) {
      cached_write_file_ = fs->open(resolved.c_str(), FILE_WRITE);
    }
    if (!cached_write_file_) {
      out_message = "open r+ failed";
      return false;
    }
    if (offset > 0U && !cached_write_file_.seek(offset, SeekSet)) {
      closeCachedWrite_();
      out_message = "seek failed";
      return false;
    }
    cached_write_path_ = resolved;
    cached_write_next_offset_ = offset;
    cached_write_since_flush_ = 0U;
  }

  size_t written_total = 0U;
  uint8_t attempts = 0U;
  while (written_total < len && attempts < 8U) {
    const size_t remain = len - written_total;
    const size_t written =
        static_cast<size_t>(cached_write_file_.write(data + written_total, remain));
    if (written == 0U) {
      ++attempts;
#if defined(ARDUINO)
      yield();
#endif
      continue;
    }
    written_total += written;
  }
  if (written_total != len) {
    closeCachedWrite_();
    out_message = "short write";
    return false;
  }
  cached_write_next_offset_ += static_cast<uint32_t>(written_total);
  cached_write_since_flush_ += written_total;
  if (cached_write_since_flush_ >= 4096U || (offset == 0U && len <= 1024U)) {
    cached_write_file_.flush();
    cached_write_since_flush_ = 0U;
  }
  out_message = "ok";
  return true;
}

bool ArduinoSelectableOtaStorageBackend::readAt(const std::string& path,
                                                uint32_t offset,
                                                uint8_t* out,
                                                size_t max_len,
                                                size_t& out_len,
                                                std::string& out_message) {
  out_len = 0;
  fs::FS* fs = activeFs_();
  if (!begun_ || fs == nullptr) {
    out_message = "storage not ready";
    return false;
  }
  if (out == nullptr || max_len == 0U) {
    out_message = "invalid read";
    return false;
  }

  const std::string resolved = normalizePath_(path);
  static constexpr uint8_t kReadAttempts = 3;
  for (uint8_t attempt = 0; attempt < kReadAttempts; ++attempt) {
    const bool sequential_cache_hit =
        cached_read_file_ && cached_read_path_ == resolved && offset == cached_read_next_offset_;
    if (!sequential_cache_hit) {
      closeCachedRead_();
      cached_read_file_ = fs->open(resolved.c_str(), FILE_READ);
      if (!cached_read_file_) {
        out_message = "open read failed";
        continue;
      }
      if (offset > 0U && !cached_read_file_.seek(offset, SeekSet)) {
        closeCachedRead_();
        out_message = "seek failed";
        continue;
      }
      cached_read_path_ = resolved;
      cached_read_next_offset_ = offset;
    }

    const size_t file_size = static_cast<size_t>(cached_read_file_.size());
    const bool expect_data = static_cast<size_t>(offset) < file_size;
    const int n = cached_read_file_.read(out, max_len);

    if (n > 0) {
      out_len = static_cast<size_t>(n);
      cached_read_next_offset_ += static_cast<uint32_t>(out_len);
      out_message = "ok";
      return true;
    }
    if (n == 0 && !expect_data) {
      out_len = 0U;
      out_message = "eof";
      return true;
    }

    closeCachedRead_();
    out_message = "read failed";
  }

  return false;
}

void ArduinoSelectableOtaStorageBackend::closeCachedWrite_() {
  if (cached_write_file_) {
    cached_write_file_.flush();
    cached_write_file_.close();
  }
  cached_write_path_.clear();
  cached_write_next_offset_ = 0U;
  cached_write_since_flush_ = 0U;
}

void ArduinoSelectableOtaStorageBackend::closeCachedRead_() {
  if (cached_read_file_) {
    cached_read_file_.close();
  }
  cached_read_path_.clear();
  cached_read_next_offset_ = 0U;
}

void ArduinoSelectableOtaStorageBackend::resetIoCache_() {
  closeCachedWrite_();
  closeCachedRead_();
}

}  // namespace espnow_link

#endif  // defined(ARDUINO)
