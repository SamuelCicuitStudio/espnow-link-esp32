#pragma once

#include "espnow_link/log_storage.hpp"

#if defined(ARDUINO)
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#endif

namespace espnow_link {

#if defined(ARDUINO)

/**
 * @brief SPIFFS-backed logger storage backend for Arduino builds.
 */
class ArduinoSpiffsLogStorageBackend : public ILogStorageBackend {
 public:
  /** @brief Filesystem ownership policy for SPIFFS mounting. */
  enum class OwnershipMode : uint8_t {
    /** @brief Backend calls `SPIFFS.begin(format_on_fail)` in `begin()`. */
    AutoMount = 0,
    /** @brief Application must mount SPIFFS before backend `begin()`. */
    ExternalMounted = 1,
  };

  /**
   * @brief Construct backend wrapper.
   * @param format_on_fail Forwarded to `SPIFFS.begin(format_on_fail)`.
   * @param ownership Filesystem ownership policy.
   */
  explicit ArduinoSpiffsLogStorageBackend(bool format_on_fail = true,
                                          OwnershipMode ownership = OwnershipMode::ExternalMounted);

  bool begin() override;
  bool read(const std::string& path,
            uint32_t offset,
            uint8_t* out,
            size_t max_len,
            size_t& out_len) override;
  bool append(const std::string& path, const uint8_t* data, size_t len) override;
  bool size(const std::string& path, uint32_t& out_size) override;
  bool truncate(const std::string& path) override;
  bool remove(const std::string& path) override;

 private:
  bool probeMounted_() const;

  bool format_on_fail_ = true;
  OwnershipMode ownership_ = OwnershipMode::AutoMount;
  bool begun_ = false;
};

/**
 * @brief Selectable Arduino logger storage backend (SD or SPIFFS).
 *
 * App can prefer SD with automatic fallback to SPIFFS, or prefer SPIFFS with
 * fallback to SD. Default preference is SPIFFS.
 */
class ArduinoSelectableLogStorageBackend : public ILogStorageBackend {
 public:
  /** @brief Preferred backend choice. */
  enum class PreferredBackend : uint8_t {
    Spiffs = 0,
    Sd = 1,
  };

  /** @brief SPIFFS ownership policy. */
  enum class SpiffsOwnershipMode : uint8_t {
    /** @brief Backend calls `SPIFFS.begin(format_on_fail)` in `begin()`. */
    AutoMount = 0,
    /** @brief Application must mount SPIFFS before backend `begin()`. */
    ExternalMounted = 1,
  };

  /**
   * @brief Construct selectable backend.
   * @param format_on_fail Forwarded to `SPIFFS.begin(format_on_fail)` when SPIFFS AutoMount is enabled.
   * @param ownership SPIFFS mount ownership policy.
   */
  explicit ArduinoSelectableLogStorageBackend(
      bool format_on_fail = true,
      SpiffsOwnershipMode ownership = SpiffsOwnershipMode::ExternalMounted);

  /** @brief Bind SD backend (already mounted by app). */
  void bindSd(fs::FS* fs);

  /** @brief Bind SPIFFS backend (already mounted by app). */
  void bindSpiffs(fs::FS* fs);

  /** @brief Set preferred backend for next `begin()` selection. */
  void setPreferredBackend(PreferredBackend preferred);

  /** @brief Query currently preferred backend. */
  PreferredBackend preferredBackend() const { return preferred_; }

  /** @brief Query active backend selected during last `begin()`. */
  PreferredBackend activeBackend() const { return active_; }

  /** @brief Human-readable active backend label (`sd`, `spiffs`, `none`). */
  const char* activeBackendName() const;

  bool begin() override;
  bool read(const std::string& path,
            uint32_t offset,
            uint8_t* out,
            size_t max_len,
            size_t& out_len) override;
  bool append(const std::string& path, const uint8_t* data, size_t len) override;
  bool size(const std::string& path, uint32_t& out_size) override;
  bool truncate(const std::string& path) override;
  bool remove(const std::string& path) override;

 private:
  bool probeMounted_(fs::FS& fs) const;
  bool ensureSpiffsReady_();
  bool selectActiveBackend_();
  fs::FS* activeFs_() const;

  bool format_on_fail_ = true;
  SpiffsOwnershipMode spiffs_ownership_ = SpiffsOwnershipMode::ExternalMounted;
  PreferredBackend preferred_ = PreferredBackend::Spiffs;
  PreferredBackend active_ = PreferredBackend::Spiffs;
  bool has_active_ = false;
  bool begun_ = false;

  fs::FS* sd_fs_ = &SD;
  fs::FS* spiffs_fs_ = &SPIFFS;
  bool spiffs_is_default_ = true;
};

#endif  // defined(ARDUINO)

}  // namespace espnow_link
