#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/descriptor.hpp"
#include "espnow_link/ota_paths.hpp"

#if defined(ARDUINO)
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>

namespace espnow_link {

namespace {

const char* sdCardTypeName(uint8_t card_type) {
  switch (card_type) {
    case CARD_NONE:
      return "none";
    case CARD_MMC:
      return "mmc";
    case CARD_SD:
      return "sdsc";
    case CARD_SDHC:
      return "sdhc_or_sdxc";
    default:
      return "unknown";
  }
}

double bytesToMb(uint64_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

}  // namespace

/**
 * @brief Arduino FS-backed storage explorer adapter (SD/SPIFFS).
 *
 * App owns mounting and pin setup. This adapter only reads already-mounted FS instances.
 */
class ArduinoStorageExplorer : public IStorageExplorerProvider {
 public:
  ArduinoStorageExplorer() = default;

  /** @brief Bind SD backend instance (already mounted by app). */
  void bindSd(fs::FS* fs, const std::string& root_path = "/") {
    sd_fs_ = fs;
    sd_typed_ = nullptr;
    sd_root_path_ = normalizePath(root_path);
  }

  /** @brief Bind SD backend with typed API support (capacity/usage stats). */
  void bindSd(fs::SDFS* fs, const std::string& root_path = "/") {
    sd_fs_ = fs;
    sd_typed_ = fs;
    sd_root_path_ = normalizePath(root_path);
  }

  /** @brief Bind SPIFFS/LittleFS backend instance (already mounted by app). */
  void bindSpiffs(fs::FS* fs, const std::string& root_path = "/") {
    spiffs_fs_ = fs;
    spiffs_typed_ = nullptr;
    spiffs_root_path_ = normalizePath(root_path);
  }

  /** @brief Bind SPIFFS backend with typed API support (capacity/usage stats). */
  void bindSpiffs(fs::SPIFFSFS* fs, const std::string& root_path = "/") {
    spiffs_fs_ = fs;
    spiffs_typed_ = fs;
    spiffs_root_path_ = normalizePath(root_path);
  }

  /** @brief Select active storage mode for explorer operations. */
  void setMode(StorageBackendMode mode) {
    mode_ = mode;
  }

  /** @brief Disable explorer operations. */
  void disable() {
    mode_ = StorageBackendMode::Disabled;
  }

  /** @brief Get currently selected backend mode. */
  StorageBackendMode mode() const {
    return mode_;
  }

  bool getStorageInfo(StorageInfo& out, std::string& out_message) override {
    out = StorageInfo{};
    out.mode = mode_;
    out.root_path = activeRoot();
    out.cwd = out.root_path.empty() ? "/" : out.root_path;

    fs::FS* fs = activeFs();
    if (fs == nullptr || mode_ == StorageBackendMode::Disabled) {
      out.available = false;
      out.mounted = false;
      out_message = "storage backend not ready";
      return true;
    }

    out.available = true;
    out.mounted = true;
    uint64_t total = 0;
    uint64_t used = 0;

    if (mode_ == StorageBackendMode::Sd && sd_typed_ != nullptr) {
      total = static_cast<uint64_t>(sd_typed_->totalBytes());
      used = static_cast<uint64_t>(sd_typed_->usedBytes());
      const uint64_t card = static_cast<uint64_t>(sd_typed_->cardSize());
      const uint8_t card_type = sd_typed_->cardType();
      char mb_buf[24] = {0};
      std::snprintf(mb_buf, sizeof(mb_buf), "%.2f", bytesToMb(card));
      out_message = std::string("sd card_type=") + sdCardTypeName(card_type) + " card_mb=" + mb_buf;
    } else if (mode_ == StorageBackendMode::Spiffs && spiffs_typed_ != nullptr) {
      total = static_cast<uint64_t>(spiffs_typed_->totalBytes());
      used = static_cast<uint64_t>(spiffs_typed_->usedBytes());
      out_message.clear();
    } else {
      out_message = "size stats unavailable";
    }

    out.total_bytes = clampU32(total);
    out.used_bytes = clampU32(used);
    out.free_bytes = clampU32((total > used) ? (total - used) : 0U);
    return true;
  }

  bool listStoragePath(const std::string& path,
                       std::string& out_canonical_path,
                       std::string& out_parent_path,
                       std::vector<StorageEntry>& out_entries,
                       std::string& out_message) override {
    out_entries.clear();
    out_canonical_path.clear();
    out_parent_path.clear();

    fs::FS* fs = activeFs();
    if (fs == nullptr || mode_ == StorageBackendMode::Disabled) {
      out_message = "storage backend not ready";
      return false;
    }

    const std::string resolved = normalizePath(path.empty() ? "/" : path);
    File dir = fs->open(resolved.c_str());
    if (!dir) {
      out_message = "path not found";
      return false;
    }
    if (!dir.isDirectory()) {
      out_message = "path is not directory";
      return false;
    }

    out_canonical_path = resolved;
    out_parent_path = parentPath(resolved);

    File entry = dir.openNextFile();
    while (entry) {
      StorageEntry item{};
      item.name = baseName(entry.name());
      item.is_dir = entry.isDirectory();
      item.size_bytes = item.is_dir ? 0U : clampU32(static_cast<uint64_t>(entry.size()));
      out_entries.push_back(item);
      entry = dir.openNextFile();
    }

    std::sort(out_entries.begin(), out_entries.end(), [](const StorageEntry& a, const StorageEntry& b) {
      if (a.is_dir != b.is_dir) {
        return a.is_dir && !b.is_dir;
      }
      return a.name < b.name;
    });

    out_message.clear();
    return true;
  }

  bool statStoragePath(const std::string& path, StorageStat& out, std::string& out_message) override {
    out = StorageStat{};
    fs::FS* fs = activeFs();
    if (fs == nullptr || mode_ == StorageBackendMode::Disabled) {
      out_message = "storage backend not ready";
      return false;
    }

    const std::string resolved = normalizePath(path.empty() ? "/" : path);
    out.path = resolved;

    File file = fs->open(resolved.c_str());
    if (!file) {
      out.exists = false;
      out.is_dir = false;
      out.size_bytes = 0;
      out_message.clear();
      return true;
    }

    out.exists = true;
    out.is_dir = file.isDirectory();
    out.size_bytes = out.is_dir ? 0U : clampU32(static_cast<uint64_t>(file.size()));
    out_message.clear();
    return true;
  }

  bool formatStorage(std::string& out_message) override {
    fs::FS* fs = activeFs();
    if (fs == nullptr || mode_ == StorageBackendMode::Disabled) {
      out_message = "storage backend not ready";
      return false;
    }

    std::string msg;
    std::vector<std::string> preserve_dirs;
    if (!listTopLevelDirs_(*fs, preserve_dirs, msg)) {
      out_message = msg.empty() ? "format pre-scan failed" : msg;
      return false;
    }
    if (!clearRootContents_(*fs, msg)) {
      out_message = msg.empty() ? "format clear failed" : msg;
      return false;
    }
    if (!ensureLibraryLayout_(*fs, msg)) {
      out_message = msg.empty() ? "format layout recreate failed" : msg;
      return false;
    }
    if (!restorePreservedDirs_(*fs, preserve_dirs, msg)) {
      out_message = msg.empty() ? "format preserved-dir restore failed" : msg;
      return false;
    }
    out_message = "[MASTER][SD][LOCAL] format done (library layout recreated)";
    return true;
  }

 private:
  static uint32_t clampU32(uint64_t value) {
    return (value > static_cast<uint64_t>(UINT32_MAX)) ? UINT32_MAX : static_cast<uint32_t>(value);
  }

  static std::string normalizePath(const std::string& input) {
    if (input.empty()) {
      return "/";
    }
    std::vector<std::string> parts;
    std::string token;
    auto flush = [&]() {
      if (token.empty() || token == ".") {
        token.clear();
        return;
      }
      if (token == "..") {
        if (!parts.empty()) {
          parts.pop_back();
        }
        token.clear();
        return;
      }
      parts.push_back(token);
      token.clear();
    };
    for (char c : input) {
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
      if (i > 0) {
        out.push_back('/');
      }
      out += parts[i];
    }
    return out;
  }

  static std::string parentPath(const std::string& path) {
    const std::string normalized = normalizePath(path);
    if (normalized == "/") {
      return "/";
    }
    const size_t pos = normalized.find_last_of('/');
    if (pos == std::string::npos || pos == 0U) {
      return "/";
    }
    return normalized.substr(0, pos);
  }

  static std::string baseName(const char* path) {
    if (path == nullptr || path[0] == '\0') {
      return std::string();
    }
    std::string p(path);
    const size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) {
      return p;
    }
    if (pos + 1U >= p.size()) {
      return p;
    }
    return p.substr(pos + 1U);
  }

  static std::string stripMountPrefix_(const std::string& normalized) {
    if (normalized == "/sd") return "/";
    if (normalized == "/spiffs") return "/";
    if (normalized == "/littlefs") return "/";
    if (normalized.rfind("/sd/", 0U) == 0U) {
      return normalized.substr(3U);
    }
    if (normalized.rfind("/spiffs/", 0U) == 0U) {
      return normalized.substr(7U);
    }
    if (normalized.rfind("/littlefs/", 0U) == 0U) {
      return normalized.substr(9U);
    }
    return normalized;
  }

  static std::string resolveChildPath_(const std::string& parent, const char* child_name) {
    if (child_name == nullptr || child_name[0] == '\0') {
      return std::string();
    }
    const std::string child_raw(child_name);
    if (child_raw.empty()) {
      return std::string();
    }

    // Match MKSDNAND recursive-delete behavior: keep full child path when provided.
    if (!child_raw.empty() && child_raw[0] == '/') {
      return stripMountPrefix_(normalizePath(child_raw));
    }

    const std::string base = normalizePath(parent);
    if (base == "/") {
      return normalizePath(std::string("/") + child_raw);
    }
    return normalizePath(base + "/" + child_raw);
  }

  static bool removeRecursive_(fs::FS& fs, const std::string& path, std::string& out_message) {
    const std::string resolved = normalizePath(path);
    File node = fs.open(resolved.c_str());
    if (!node) {
      out_message.clear();
      return true;
    }
    const bool is_dir = node.isDirectory();
    node.close();
    if (!is_dir) {
      if (!fs.remove(resolved.c_str())) {
        out_message = std::string("remove failed: ") + resolved;
        return false;
      }
      out_message.clear();
      return true;
    }

    while (true) {
      File dir = fs.open(resolved.c_str());
      if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        break;
      }
      File entry = dir.openNextFile();
      if (!entry) {
        dir.close();
        break;
      }

      const char* n = entry.name();
      const std::string child = (n != nullptr && n[0] != '\0')
                                    ? resolveChildPath_(resolved, n)
                                    : std::string();
      const bool bad_child = child.empty() || child == "/" || child == resolved;
      entry.close();
      dir.close();
      if (!bad_child) {
        if (!removeRecursive_(fs, child, out_message)) {
          return false;
        }
      }

#if defined(ARDUINO)
      yield();
#endif
    }

    if (resolved != "/" && !resolved.empty()) {
      // SPIFFS-style backends may not materialize directories as standalone nodes.
      if (fs.exists(resolved.c_str())) {
        if (!fs.rmdir(resolved.c_str())) {
          out_message = std::string("rmdir failed: ") + resolved;
          return false;
        }
      }
    }
    out_message.clear();
    return true;
  }

  static bool clearRootContents_(fs::FS& fs, std::string& out_message) {
    while (true) {
      File root = fs.open("/");
      if (!root || !root.isDirectory()) {
        out_message = "root open failed";
        return false;
      }

      File entry = root.openNextFile();
      if (!entry) {
        root.close();
        break;
      }

      const char* n = entry.name();
      const std::string child = (n != nullptr && n[0] != '\0')
                                    ? resolveChildPath_("/", n)
                                    : std::string();
      entry.close();
      root.close();

      if (child.empty() || child == "/") {
        continue;
      }
      if (!removeRecursive_(fs, child, out_message)) {
        return false;
      }

#if defined(ARDUINO)
      yield();
#endif
    }
    out_message.clear();
    return true;
  }

  static bool ensureDirRecursive_(fs::FS& fs, const std::string& path, std::string& out_message) {
    const std::string normalized = normalizePath(path);
    if (normalized == "/") {
      out_message.clear();
      return true;
    }
    auto ensure_one = [&](const std::string& dir_path) -> bool {
      if (dir_path.empty() || dir_path == "/") {
        return true;
      }
      if (fs.mkdir(dir_path.c_str())) {
        return true;
      }
      File d = fs.open(dir_path.c_str());
      if (d && d.isDirectory()) {
        d.close();
        return true;
      }
      if (d) d.close();
      out_message = std::string("mkdir failed: ") + dir_path;
      return false;
    };

    std::string current;
    current.reserve(normalized.size());
    for (char c : normalized) {
      current.push_back(c);
      if (c != '/' || current.size() <= 1U) {
        continue;
      }
      const std::string partial = normalizePath(current);
      if (!ensure_one(partial)) {
        return false;
      }
    }
    if (!ensure_one(normalized)) {
      return false;
    }
    out_message.clear();
    return true;
  }

  static bool ensureFile_(fs::FS& fs, const std::string& path, std::string& out_message) {
    const std::string normalized = normalizePath(path);
    const size_t slash = normalized.find_last_of('/');
    if (slash != std::string::npos && slash > 0U) {
      if (!ensureDirRecursive_(fs, normalized.substr(0U, slash), out_message)) {
        return false;
      }
    }
    File f = fs.open(normalized.c_str(), FILE_WRITE);
    if (!f) {
      out_message = std::string("open failed: ") + normalized;
      return false;
    }
    f.close();
    out_message.clear();
    return true;
  }

  static bool ensureLibraryLayout_(fs::FS& fs, std::string& out_message) {
    static const char* kDirs[] = {
        ota_paths::kRoot,
        ota_paths::kIn,
        ota_paths::kStaging,
        ota_paths::kImage,
        ota_paths::kManifest,
        ota_paths::kState,
        ota_paths::kArchiveRoot,
        ota_paths::kArchiveMaster,
        ota_paths::kArchiveSlave,
    };
    for (const char* d : kDirs) {
      if (!ensureDirRecursive_(fs, d, out_message)) {
        return false;
      }
    }

    static const char* kFiles[] = {
        "/elg.bin",
        "/elg.idx",
    };
    for (const char* f : kFiles) {
      if (!ensureFile_(fs, f, out_message)) {
        return false;
      }
    }
    out_message.clear();
    return true;
  }

  static bool listTopLevelDirs_(fs::FS& fs,
                                std::vector<std::string>& out_dirs,
                                std::string& out_message) {
    out_dirs.clear();
    File root = fs.open("/");
    if (!root || !root.isDirectory()) {
      out_message = "root open failed";
      return false;
    }

    File entry = root.openNextFile();
    while (entry) {
      if (entry.isDirectory()) {
        const char* n = entry.name();
        if (n != nullptr && n[0] != '\0') {
          const std::string resolved = resolveChildPath_("/", n);
          if (!resolved.empty() && resolved != "/") {
            out_dirs.push_back(resolved);
          }
        }
      }
      entry.close();
      entry = root.openNextFile();
    }
    root.close();
    out_message.clear();
    return true;
  }

  static bool restorePreservedDirs_(fs::FS& fs,
                                    const std::vector<std::string>& dirs,
                                    std::string& out_message) {
    for (const auto& d : dirs) {
      if (d.empty() || d == "/") continue;
      if (!ensureDirRecursive_(fs, d, out_message)) {
        return false;
      }
    }
    out_message.clear();
    return true;
  }

  fs::FS* activeFs() const {
    switch (mode_) {
      case StorageBackendMode::Sd:
        return sd_fs_;
      case StorageBackendMode::Spiffs:
        return spiffs_fs_;
      case StorageBackendMode::Disabled:
      case StorageBackendMode::Unknown:
      default:
        return nullptr;
    }
  }

  std::string activeRoot() const {
    switch (mode_) {
      case StorageBackendMode::Sd:
        return sd_root_path_;
      case StorageBackendMode::Spiffs:
        return spiffs_root_path_;
      case StorageBackendMode::Disabled:
      case StorageBackendMode::Unknown:
      default:
        return "/";
    }
  }

  fs::FS* sd_fs_ = nullptr;
  fs::FS* spiffs_fs_ = nullptr;
  fs::SDFS* sd_typed_ = nullptr;
  fs::SPIFFSFS* spiffs_typed_ = nullptr;
  StorageBackendMode mode_ = StorageBackendMode::Disabled;
  std::string sd_root_path_ = "/";
  std::string spiffs_root_path_ = "/";
};

}  // namespace espnow_link

#else

namespace espnow_link {

class ArduinoStorageExplorer : public IStorageExplorerProvider {
 public:
  ArduinoStorageExplorer() = default;
  void setMode(StorageBackendMode) {}
  void disable() {}
};

}  // namespace espnow_link

#endif
