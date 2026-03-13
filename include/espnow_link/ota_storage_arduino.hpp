#pragma once

#include "espnow_link/ota_storage.hpp"

#if defined(ARDUINO)
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#endif

namespace espnow_link {

#if defined(ARDUINO)

/**
 * @brief OTA storage backend using SPIFFS paths for OTA operations.
 *
 * Ownership is external: app mounts filesystems, backend only probes/uses them.
 * SD can still be bound for app integration, but OTA file ops are SPIFFS-only.
 */
class ArduinoSelectableOtaStorageBackend : public IOtaStorageBackend {
 public:
  /** @brief Preferred backend for selection. */
  enum class PreferredBackend : uint8_t {
    Spiffs = 0,
    Sd = 1,
  };

  ArduinoSelectableOtaStorageBackend() = default;

  /** @brief Bind SD filesystem (already mounted by app). */
  void bindSd(fs::FS* fs);

  /** @brief Bind SPIFFS filesystem (already mounted by app). */
  void bindSpiffs(fs::FS* fs);

  /** @brief Set preferred backend selected at `begin()`. */
  void setPreferredBackend(PreferredBackend preferred);

  /** @brief Query active backend chosen by last `begin()`. */
  PreferredBackend activeBackend() const { return active_; }

  /** @brief Human-readable active backend label (`sd`, `spiffs`, `none`). */
  const char* activeBackendName() const;

  /**
   * @brief Move one file from SPIFFS to SD (copy then remove source).
   * @param from_path Source path on SPIFFS (absolute or relative).
   * @param to_path Destination path on SD (absolute or relative).
   * @param out_message Result details.
   * @param chunk_bytes Copy buffer size.
   * @return true on success.
   */
  bool moveSpiffsToSd(const std::string& from_path,
                      const std::string& to_path,
                      std::string& out_message,
                      size_t chunk_bytes = 1024U) override;
  bool copySpiffsToSd(const std::string& from_path,
                      const std::string& to_path,
                      std::string& out_message,
                      size_t chunk_bytes = 1024U) override;
  bool copySdToSpiffs(const std::string& from_path,
                      const std::string& to_path,
                      std::string& out_message,
                      size_t chunk_bytes = 1024U) override;
  bool dumpRunningFirmwareToSd(const std::string& to_path,
                               uint32_t& out_size_bytes,
                               uint32_t& out_crc32,
                               std::string& out_message,
                               size_t chunk_bytes = 4096U) override;
  bool removePathOnSd(const std::string& path, std::string& out_message) override;

  bool begin(std::string& out_message) override;
  bool ensureDir(const std::string& path, std::string& out_message) override;
  bool stat(const std::string& path, OtaStorageStat& out_stat, std::string& out_message) override;
  bool listDir(const std::string& path,
               std::vector<std::string>& out_names,
               std::string& out_message) override;
  bool truncateFile(const std::string& path, std::string& out_message) override;
  bool removePath(const std::string& path, std::string& out_message) override;
  bool renamePath(const std::string& from, const std::string& to, std::string& out_message) override;
  bool writeAt(const std::string& path,
               uint32_t offset,
               const uint8_t* data,
               size_t len,
               std::string& out_message) override;
  bool readAt(const std::string& path,
              uint32_t offset,
              uint8_t* out,
              size_t max_len,
              size_t& out_len,
              std::string& out_message) override;

 private:
  static std::string normalizePath_(const std::string& path);
  static std::string joinPath_(const std::string& base, const std::string& name);
  static std::string baseName_(const char* path);
  static std::string parentPath_(const std::string& path);

  bool probeMounted_(fs::FS& fs) const;
  bool selectActive_(std::string& out_message);
  fs::FS* activeFs_() const;
  bool removeTree_(fs::FS& fs, const std::string& path, std::string& out_message) const;
  void closeCachedWrite_();
  void closeCachedRead_();
  void resetIoCache_();

  PreferredBackend preferred_ = PreferredBackend::Spiffs;
  PreferredBackend active_ = PreferredBackend::Spiffs;
  bool has_active_ = false;
  bool begun_ = false;

  fs::FS* sd_fs_ = &SD;
  fs::FS* spiffs_fs_ = &SPIFFS;

  File cached_write_file_{};
  std::string cached_write_path_{};
  uint32_t cached_write_next_offset_ = 0U;
  size_t cached_write_since_flush_ = 0U;
  File cached_read_file_{};
  std::string cached_read_path_{};
  uint32_t cached_read_next_offset_ = 0U;
};

#endif  // defined(ARDUINO)

}  // namespace espnow_link
