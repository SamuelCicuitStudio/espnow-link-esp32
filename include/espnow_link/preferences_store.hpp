#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Preferences.h>
#include "espnow_link/nvs_contract.hpp"
#if defined(ARDUINO)
#include <WString.h>
#endif

namespace espnow_link {

/**
 * @brief ESP32 Preferences-backed storage helper used by examples/adapters.
 */
class PreferencesStore {
 public:
  /**
   * @brief Construct preferences store wrapper.
   * @param ns Preferences namespace.
   */
  explicit PreferencesStore(const char* ns = kSharedNvsNamespace,
                            const char* partition = kSharedNvsPartition)
      : ns_(ns == nullptr ? kSharedNvsNamespace : ns),
        partition_(partition == nullptr ? kSharedNvsPartition : partition) {}

  /** @brief Open preferences namespace for read/write. */
  bool begin() { return prefs_.begin(ns_.c_str(), false, partition_.c_str()); }

  /** @brief Save binary blob by key. */
  bool putBlob(const std::string& key, const uint8_t* data, size_t len) {
    return prefs_.putBytes(key.c_str(), data, len) == len;
  }

  /** @brief Load binary blob by key. */
  bool getBlob(const std::string& key, std::vector<uint8_t>& out) {
    if (!prefs_.isKey(key.c_str())) {
      return false;
    }
    const size_t len = prefs_.getBytesLength(key.c_str());
    if (len == 0) {
      return false;
    }
    out.resize(len);
    return prefs_.getBytes(key.c_str(), out.data(), len) == len;
  }

  /** @brief Erase key if it exists. */
  bool eraseKey(const std::string& key) {
    if (!prefs_.isKey(key.c_str())) {
      return true;
    }
    return prefs_.remove(key.c_str());
  }

  /** @brief Save fixed-length bytes using C-string key. */
  bool putFixed(const char* key, const uint8_t* data, size_t len) {
    return prefs_.putBytes(key, data, len) == len;
  }

  /** @brief Load fixed-length bytes using C-string key. */
  bool getFixed(const char* key, uint8_t* out, size_t len) {
    if (!prefs_.isKey(key)) {
      return false;
    }
    return prefs_.getBytes(key, out, len) == len;
  }

  /** @brief Erase fixed key if it exists. */
  bool eraseFixed(const char* key) {
    if (!prefs_.isKey(key)) {
      return true;
    }
    return prefs_.remove(key);
  }

  /** @brief Save std::string value by key. */
  bool putString(const char* key, const std::string& value) {
    return prefs_.putString(key, value.c_str()) == value.size();
  }

  /** @brief Load std::string value by key. */
  bool getString(const char* key, std::string& out) {
    if (!prefs_.isKey(key)) {
      return false;
    }
    out = std::string(prefs_.getString(key, "").c_str());
    return true;
  }

#if defined(ARDUINO)
  /** @brief Save Arduino `String` value by key. */
  bool putString(const char* key, const String& value) {
    return prefs_.putString(key, value) == value.length();
  }

  /** @brief Load Arduino `String` value by key. */
  bool getString(const char* key, String& out) {
    if (!prefs_.isKey(key)) {
      return false;
    }
    out = prefs_.getString(key, "");
    return true;
  }
#endif

  /** @brief Save uint16 value by key. */
  bool putU16(const char* key, uint16_t value) { return prefs_.putUShort(key, value) == sizeof(uint16_t); }

  /** @brief Load uint16 value by key. */
  bool getU16(const char* key, uint16_t& out) {
    if (!prefs_.isKey(key)) {
      return false;
    }
    out = prefs_.getUShort(key, 0);
    return true;
  }

  /** @brief Save uint32 value by key. */
  bool putU32(const char* key, uint32_t value) { return prefs_.putUInt(key, value) == sizeof(uint32_t); }

  /** @brief Load uint32 value by key. */
  bool getU32(const char* key, uint32_t& out) {
    if (!prefs_.isKey(key)) {
      return false;
    }
    out = prefs_.getUInt(key, 0);
    return true;
  }

  /** @brief Save bool value by key. */
  bool putBool(const char* key, bool value) { return prefs_.putBool(key, value) == sizeof(uint8_t); }

  /** @brief Load bool value by key. */
  bool getBool(const char* key, bool& out) {
    if (!prefs_.isKey(key)) {
      return false;
    }
    out = prefs_.getBool(key, false);
    return true;
  }

  /** @brief Save float value by key. */
  bool putFloat(const char* key, float value) { return prefs_.putFloat(key, value) == sizeof(float_t); }

  /** @brief Load float value by key. */
  bool getFloat(const char* key, float& out) {
    if (!prefs_.isKey(key)) {
      return false;
    }
    out = prefs_.getFloat(key, 0.0f);
    return true;
  }

 private:
  std::string ns_;
  std::string partition_;
  Preferences prefs_;
};

}  // namespace espnow_link
