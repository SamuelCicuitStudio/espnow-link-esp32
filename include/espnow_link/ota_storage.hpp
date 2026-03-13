#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace espnow_link {

/** @brief Minimal path metadata used by OTA storage operations. */
struct OtaStorageStat {
  bool exists = false;
  bool is_dir = false;
  uint32_t size_bytes = 0;
};

/**
 * @brief Backend abstraction for OTA file operations.
 *
 * App code provides an implementation backed by mounted SD or SPIFFS/LittleFS.
 */
class IOtaStorageBackend {
 public:
  virtual ~IOtaStorageBackend() = default;

  /** @brief Ensure backend is ready for file operations. */
  virtual bool begin(std::string& out_message) = 0;

  /** @brief Ensure directory exists (create if missing). */
  virtual bool ensureDir(const std::string& path, std::string& out_message) = 0;

  /** @brief Query path metadata. */
  virtual bool stat(const std::string& path, OtaStorageStat& out_stat, std::string& out_message) = 0;

  /** @brief List direct children names under directory path. */
  virtual bool listDir(const std::string& path,
                       std::vector<std::string>& out_names,
                       std::string& out_message) = 0;

  /** @brief Create/truncate file at path. */
  virtual bool truncateFile(const std::string& path, std::string& out_message) = 0;

  /** @brief Remove file or empty directory at path. */
  virtual bool removePath(const std::string& path, std::string& out_message) = 0;

  /** @brief Rename/move a file path. */
  virtual bool renamePath(const std::string& from, const std::string& to, std::string& out_message) = 0;

  /** @brief Write bytes at file offset. */
  virtual bool writeAt(const std::string& path,
                       uint32_t offset,
                       const uint8_t* data,
                       size_t len,
                       std::string& out_message) = 0;

  /** @brief Read bytes at file offset. */
  virtual bool readAt(const std::string& path,
                      uint32_t offset,
                      uint8_t* out,
                      size_t max_len,
                      size_t& out_len,
                      std::string& out_message) = 0;

  /**
   * @brief Move one file from SPIFFS to SD.
   *
   * Default implementation returns unsupported.
   */
  virtual bool moveSpiffsToSd(const std::string& from_path,
                              const std::string& to_path,
                              std::string& out_message,
                              size_t chunk_bytes = 1024U) {
    (void)from_path;
    (void)to_path;
    (void)chunk_bytes;
    out_message = "unsupported";
    return false;
  }

  /**
   * @brief Copy one file from SPIFFS to SD (source preserved).
   *
   * Default implementation returns unsupported.
   */
  virtual bool copySpiffsToSd(const std::string& from_path,
                              const std::string& to_path,
                              std::string& out_message,
                              size_t chunk_bytes = 1024U) {
    (void)from_path;
    (void)to_path;
    (void)chunk_bytes;
    out_message = "unsupported";
    return false;
  }

  /**
   * @brief Copy one file from SD to SPIFFS.
   *
   * Default implementation returns unsupported.
   */
  virtual bool copySdToSpiffs(const std::string& from_path,
                              const std::string& to_path,
                              std::string& out_message,
                              size_t chunk_bytes = 1024U) {
    (void)from_path;
    (void)to_path;
    (void)chunk_bytes;
    out_message = "unsupported";
    return false;
  }

  /**
   * @brief Dump running firmware image bytes directly to SD.
   *
   * Used by OTA archive "save running image" flows.
   */
  virtual bool dumpRunningFirmwareToSd(const std::string& to_path,
                                       uint32_t& out_size_bytes,
                                       uint32_t& out_crc32,
                                       std::string& out_message,
                                       size_t chunk_bytes = 4096U) {
    (void)to_path;
    (void)out_size_bytes;
    (void)out_crc32;
    (void)chunk_bytes;
    out_message = "unsupported";
    return false;
  }

  /**
   * @brief Remove file/directory path on SD backend (when SD is mounted/bound).
   *
   * This helper is used by OTA archive cleanup flows.
   */
  virtual bool removePathOnSd(const std::string& path, std::string& out_message) {
    (void)path;
    out_message = "unsupported";
    return false;
  }
};

}  // namespace espnow_link
