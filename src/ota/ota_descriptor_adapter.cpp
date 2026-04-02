#include "espnow_link/ota_descriptor_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "espnow_link/ota_types.hpp"
#include "espnow_link/ota_update_gate.hpp"

namespace espnow_link {

namespace {

constexpr uint32_t kFnvOffset = 2166136261U;
constexpr uint32_t kFnvPrime = 16777619U;

uint32_t fnv1a(const std::string& s) {
  uint32_t h = kFnvOffset;
  for (char c : s) {
    h ^= static_cast<uint8_t>(c);
    h *= kFnvPrime;
  }
  return h;
}

struct OtaArchiveEntry {
  std::string id;
  std::string bin_name;
  std::string meta_name;
  uint32_t size_bytes = 0;
  uint32_t crc32 = 0;
  std::string sw_version;
  std::string build_id;
  std::string target_role;
  std::string source;
  uint32_t created_epoch_s = 0;
};

std::string trimWs(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
  if (b >= s.size()) return std::string();
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
  return s.substr(b, e - b);
}

bool extractJsonStringFieldLoose(const std::string& json, const char* key, std::string& out) {
  out.clear();
  if (key == nullptr || key[0] == '\0') return false;
  const std::string pattern = std::string("\"") + key + "\"";
  const size_t key_pos = json.find(pattern);
  if (key_pos == std::string::npos) return false;
  size_t pos = json.find(':', key_pos + pattern.size());
  if (pos == std::string::npos) return false;
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
  if (pos >= json.size() || json[pos] != '"') return false;
  ++pos;
  std::string value;
  bool escaped = false;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (escaped) {
      value.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      out = trimWs(value);
      return !out.empty();
    }
    value.push_back(c);
  }
  return false;
}

bool extractJsonU32FieldLoose(const std::string& json, const char* key, uint32_t& out) {
  out = 0U;
  std::string s;
  if (!extractJsonStringFieldLoose(json, key, s)) {
    const std::string pattern = std::string("\"") + key + "\"";
    const size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos) return false;
    size_t pos = json.find(':', key_pos + pattern.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
    if (pos >= json.size()) return false;
    size_t end = pos;
    while (end < json.size() &&
           json[end] != ',' &&
           json[end] != '}' &&
           std::isspace(static_cast<unsigned char>(json[end])) == 0) {
      ++end;
    }
    s = trimWs(json.substr(pos, end - pos));
    if (s.empty()) return false;
  }
  char* endp = nullptr;
  const unsigned long v = std::strtoul(s.c_str(), &endp, 0);
  if (endp == nullptr || *endp != '\0') return false;
  out = static_cast<uint32_t>(v);
  return true;
}

std::string jsonEscapeLoose(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8U);
  for (char c : in) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

bool normalizeArchiveRole(const std::string& token, char& out_role) {
  const std::string t = trimWs(token);
  if (t == "m" || t == "M" || t == "master") {
    out_role = 'm';
    return true;
  }
  if (t == "s" || t == "S" || t == "slave") {
    out_role = 's';
    return true;
  }
  return false;
}

std::string normalizeArchiveId(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isxdigit(uc) == 0) continue;
    out.push_back(static_cast<char>(std::toupper(uc)));
  }
  if (out.size() > 6U) {
    out = out.substr(out.size() - 6U);
  }
  return out;
}

std::string formatArchiveId(uint32_t v) {
  char b[7] = {0};
  std::snprintf(b, sizeof(b), "%06lX", static_cast<unsigned long>(v & 0xFFFFFFUL));
  return std::string(b);
}

}  // namespace

OtaDescriptorAdapter::OtaDescriptorAdapter(OtaManager& manager,
                                           IOtaStorageBackend& storage,
                                           const OtaManagerConfig& config)
    : manager_(manager), storage_(storage), config_(config) {
  config_.root_path = normalizeDir_(config_.root_path);
  config_.in_dir = normalizeDir_(config_.in_dir);
  config_.stg_dir = normalizeDir_(config_.stg_dir);
  config_.img_dir = normalizeDir_(config_.img_dir);
  config_.man_dir = normalizeDir_(config_.man_dir);
  config_.st_dir = normalizeDir_(config_.st_dir);
}

uint32_t OtaDescriptorAdapter::hashFileId_(const std::string& file_name) {
  return fnv1a(file_name);
}

std::string OtaDescriptorAdapter::normalizeDir_(const std::string& path) {
  if (path.empty()) return "/";
  std::string out = path;
  if (out[0] != '/') out.insert(out.begin(), '/');
  while (out.size() > 1U && out.back() == '/') out.pop_back();
  return out;
}

std::string OtaDescriptorAdapter::joinPath_(const std::string& base, const std::string& name) {
  if (base.empty() || base == "/") return "/" + name;
  return base + "/" + name;
}

std::string OtaDescriptorAdapter::trim_(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
  if (b >= s.size()) return std::string();
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
  return s.substr(b, e - b);
}

int OtaDescriptorAdapter::compareVersions_(const std::string& lhs, const std::string& rhs) {
  auto tokenize = [](const std::string& v) -> std::vector<uint32_t> {
    std::vector<uint32_t> out;
    uint32_t current = 0U;
    bool in_num = false;
    for (char c : v) {
      const unsigned char uc = static_cast<unsigned char>(c);
      if (std::isdigit(uc) != 0) {
        in_num = true;
        current = (current * 10U) + static_cast<uint32_t>(uc - static_cast<unsigned char>('0'));
      } else if (in_num) {
        out.push_back(current);
        current = 0U;
        in_num = false;
      }
    }
    if (in_num) {
      out.push_back(current);
    }
    return out;
  };

  const std::vector<uint32_t> a = tokenize(lhs);
  const std::vector<uint32_t> b = tokenize(rhs);
  if (!a.empty() && !b.empty()) {
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
      const uint32_t av = (i < a.size()) ? a[i] : 0U;
      const uint32_t bv = (i < b.size()) ? b[i] : 0U;
      if (av < bv) return -1;
      if (av > bv) return 1;
    }
    return 0;
  }

  if (lhs < rhs) return -1;
  if (lhs > rhs) return 1;
  return 0;
}

bool OtaDescriptorAdapter::parseTargetId_(const std::string& target, uint32_t& out_id) {
  const std::string t = trim_(target);
  if (t.empty()) return false;
  char* endp = nullptr;
  int base = 10;
  const char* raw = t.c_str();
  if (t.size() > 2U && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
    base = 16;
  }
  const unsigned long v = std::strtoul(raw, &endp, base);
  if (endp == nullptr || *endp != '\0') return false;
  out_id = static_cast<uint32_t>(v);
  return true;
}

bool OtaDescriptorAdapter::endsWithIgnoreCase_(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) return false;
  const size_t off = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[off + i])));
    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
    if (a != b) return false;
  }
  return true;
}

std::string OtaDescriptorAdapter::manifestPathForImage_(const std::string& file_name) const {
  char buf[20] = {0};
  std::snprintf(buf, sizeof(buf), "m%lX", static_cast<unsigned long>(hashFileId_(file_name)));
  return joinPath_(config_.man_dir, std::string(buf));
}

std::string OtaDescriptorAdapter::imagePathForName_(const std::string& file_name) const {
  return joinPath_(config_.img_dir, file_name);
}

bool OtaDescriptorAdapter::computeFileCrc_(const std::string& image_path,
                                           uint32_t expected_size,
                                           uint32_t& out_crc,
                                           std::string& out_message) {
  out_crc = 0U;
  OtaStorageStat st{};
  std::string msg;
  if (!storage_.stat(image_path, st, msg)) {
    out_message = msg.empty() ? "stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir) {
    out_message = "image not found";
    return false;
  }
  const uint32_t target_size = (expected_size == 0U) ? st.size_bytes : expected_size;
  if (target_size == 0U || st.size_bytes < target_size) {
    out_message = "invalid image size";
    return false;
  }

  constexpr uint32_t kCrc32Poly = 0xEDB88320U;
  auto crcUpdate = [](uint32_t running_crc, const uint8_t* data, size_t len) -> uint32_t {
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
  };

  if (!ensureIoScratch_(4096U)) {
    out_message = "crc buffer allocation failed";
    return false;
  }
  uint32_t offset = 0U;
  uint32_t running_crc = 0xFFFFFFFFU;
  while (offset < target_size) {
    const size_t req = std::min<size_t>(io_scratch_.size(), static_cast<size_t>(target_size - offset));
    size_t out_len = 0U;
    if (!storage_.readAt(image_path, offset, io_scratch_.data(), req, out_len, msg)) {
      out_message = msg.empty() ? "read failed" : msg;
      return false;
    }
    if (out_len == 0U) {
      out_message = "read returned zero";
      return false;
    }
    running_crc = crcUpdate(running_crc, io_scratch_.data(), out_len);
    offset += static_cast<uint32_t>(out_len);
#if defined(ARDUINO)
    if ((offset & 0xFFFFU) == 0U) {
      delay(0);
    }
#endif
  }

  out_crc = ~running_crc;
  out_message = "ok";
  return true;
}

bool OtaDescriptorAdapter::parseManifestFile_(const std::string& manifest_path,
                                              OtaManifestEntry& inout_entry,
                                              std::string& out_message) {
  OtaStorageStat st{};
  std::string msg;
  if (!storage_.stat(manifest_path, st, msg)) {
    out_message = msg.empty() ? "manifest stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    out_message = "manifest missing";
    return false;
  }
  if (st.size_bytes > 4096U) {
    out_message = "manifest too large";
    return false;
  }

  if (!ensureIoScratch_(static_cast<size_t>(st.size_bytes))) {
    out_message = "manifest buffer allocation failed";
    return false;
  }
  size_t read_len = 0U;
  if (!storage_.readAt(manifest_path, 0U, io_scratch_.data(), static_cast<size_t>(st.size_bytes), read_len, msg)) {
    out_message = msg.empty() ? "manifest read failed" : msg;
    return false;
  }
  if (read_len == 0U) {
    out_message = "manifest empty";
    return false;
  }

  auto parseU32 = [](const std::string& s, uint32_t& out) -> bool {
    if (s.empty()) return false;
    char* endp = nullptr;
    const unsigned long v = std::strtoul(s.c_str(), &endp, 0);
    if (endp == nullptr || *endp != '\0') return false;
    out = static_cast<uint32_t>(v);
    return true;
  };

  const std::string text(reinterpret_cast<const char*>(io_scratch_.data()),
                         reinterpret_cast<const char*>(io_scratch_.data() + read_len));
  size_t pos = 0U;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = trim_(text.substr(pos, end - pos));
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      const size_t eq = line.find('=');
      if (eq != std::string::npos && eq > 0U) {
        const std::string key = trim_(line.substr(0U, eq));
        const std::string value = trim_(line.substr(eq + 1U));
        uint32_t v = 0U;
        if (key == "file_id" && parseU32(value, v)) {
          inout_entry.file_id = v;
        } else if (key == "file_name") {
          inout_entry.file_name = value;
        } else if (key == "size_bytes" && parseU32(value, v)) {
          inout_entry.size_bytes = v;
        } else if (key == "crc32" && parseU32(value, v)) {
          inout_entry.crc32 = v;
        } else if (key == "version") {
          inout_entry.version = value;
        } else if (key == "build_id") {
          inout_entry.build_id = value;
        } else if (key == "created_epoch_s" && parseU32(value, v)) {
          inout_entry.created_epoch_s = v;
        } else if (key == "state") {
          inout_entry.state = value;
        } else if (key == "required_app_bytes" && parseU32(value, v)) {
          inout_entry.required_app_bytes = v;
        }
      }
    }
    pos = (end >= text.size()) ? text.size() : (end + 1U);
  }

  out_message = "ok";
  return true;
}

bool OtaDescriptorAdapter::writeManifestFile_(const OtaManifestEntry& entry, std::string& out_message) {
  std::string msg;
  if (!storage_.ensureDir(config_.man_dir, msg)) {
    out_message = msg.empty() ? "manifest dir unavailable" : msg;
    return false;
  }

  const std::string path = manifestPathForImage_(entry.file_name);
  char body[768] = {0};
  const int n = std::snprintf(body,
                              sizeof(body),
                              "file_id=%lu\n"
                              "file_name=%s\n"
                              "size_bytes=%lu\n"
                              "crc32=0x%08lX\n"
                              "version=%s\n"
                              "build_id=%s\n"
                              "created_epoch_s=%lu\n"
                              "state=%s\n"
                              "required_app_bytes=%lu\n",
                              static_cast<unsigned long>(entry.file_id),
                              entry.file_name.c_str(),
                              static_cast<unsigned long>(entry.size_bytes),
                              static_cast<unsigned long>(entry.crc32),
                              entry.version.c_str(),
                              entry.build_id.c_str(),
                              static_cast<unsigned long>(entry.created_epoch_s),
                              entry.state.c_str(),
                              static_cast<unsigned long>(entry.required_app_bytes));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(body)) {
    out_message = "manifest format failed";
    return false;
  }
  if (!storage_.truncateFile(path, msg)) {
    out_message = msg.empty() ? "manifest truncate failed" : msg;
    return false;
  }
  if (!storage_.writeAt(path,
                        0U,
                        reinterpret_cast<const uint8_t*>(body),
                        static_cast<size_t>(n),
                        msg)) {
    out_message = msg.empty() ? "manifest write failed" : msg;
    return false;
  }
  out_message = "ok";
  return true;
}

bool OtaDescriptorAdapter::listImageManifest_(std::vector<OtaManifestEntry>& out, std::string& out_message) {
  out.clear();

  const OtaRuntimeStatus& runtime = manager_.status();
  const std::string persisted_state = runtime.persistent_state;
  const std::string persisted_image_name = [&]() -> std::string {
    const std::string& path = runtime.image_path;
    if (path.empty()) {
      return std::string();
    }
    const size_t pos = path.find_last_of('/');
    if (pos == std::string::npos || (pos + 1U) >= path.size()) {
      return path;
    }
    return path.substr(pos + 1U);
  }();

  std::vector<std::string> names;
  std::string msg;
  if (!storage_.listDir(config_.img_dir, names, msg)) {
    out_message = msg.empty() ? "ota image directory unavailable" : msg;
    return false;
  }

  std::sort(names.begin(), names.end());
  bool had_missing_manifest = false;
  for (const auto& name : names) {
    if (name.empty() || !endsWithIgnoreCase_(name, ".bin")) continue;

    const std::string path = imagePathForName_(name);
    OtaStorageStat st{};
    if (!storage_.stat(path, st, msg) || !st.exists || st.is_dir) continue;

    OtaManifestEntry entry{};
    entry.file_id = hashFileId_(name);
    entry.file_name = name;
    entry.size_bytes = st.size_bytes;
    entry.crc32 = 0;
    entry.version.clear();
    entry.build_id.clear();
    entry.created_epoch_s = 0;
    entry.state = "ready";
    entry.required_app_bytes = st.size_bytes;

    const std::string manifest_path = manifestPathForImage_(name);
    OtaStorageStat mst{};
    if (storage_.stat(manifest_path, mst, msg) && mst.exists && !mst.is_dir) {
      (void)parseManifestFile_(manifest_path, entry, msg);
    } else {
      had_missing_manifest = true;
    }

    entry.file_name = name;
    entry.size_bytes = st.size_bytes;
    if (entry.file_id == 0U) {
      entry.file_id = hashFileId_(name);
    }
    if (entry.required_app_bytes == 0U) {
      entry.required_app_bytes = entry.size_bytes;
    }
    if (entry.state.empty()) {
      entry.state = "ready";
    }
    if (!persisted_image_name.empty() && entry.file_name == persisted_image_name) {
      if (persisted_state == "pending_boot") {
        entry.state = "pending_boot";
      } else if (persisted_state == "boot_ok") {
        entry.state = "boot_ok";
        if (entry.version.empty()) {
          entry.version = runtime.confirmed_sw_version;
        }
        if (entry.build_id.empty()) {
          entry.build_id = runtime.confirmed_build_id;
        }
      }
    }
    out.push_back(entry);
  }

  out_message = had_missing_manifest ? "manifest incomplete (run ota.manifest.rebuild)" : "ok";
  return true;
}

bool OtaDescriptorAdapter::clearDirContents_(const std::string& dir_path, std::string& out_message) {
  std::vector<std::string> names;
  std::string msg;
  if (!storage_.listDir(dir_path, names, msg)) {
    out_message = msg.empty() ? "list failed" : msg;
    return false;
  }
  for (const auto& name : names) {
    if (name.empty() || name == "." || name == "..") continue;
    const std::string child_path = joinPath_(dir_path, name);
    if (!storage_.removePath(child_path, msg)) {
      if (msg.empty()) {
        out_message = "remove failed: " + child_path;
      } else {
        out_message = msg + " | path=" + child_path;
      }
      return false;
    }
  }
  out_message = "ok";
  return true;
}

bool OtaDescriptorAdapter::ensureIoScratch_(size_t size) {
  if (io_scratch_.capacity() < size) {
    io_scratch_.reserve(size);
  }
  io_scratch_.resize(size);
  return io_scratch_.size() == size;
}

bool OtaDescriptorAdapter::getOtaStatus(OtaStatusInfo& out, std::string& out_message) {
  manager_.tick();
  const OtaRuntimeStatus& s = manager_.status();
  out = OtaStatusInfo{};
  out.transfer_state = static_cast<uint8_t>(s.state);
  out.status_code = static_cast<uint16_t>(s.code);
  out.expected_size = s.expected_size;
  out.received_size = s.received_size;
  out.expected_crc32 = s.expected_crc32;
  out.actual_crc32 = s.actual_crc32;
  out.temp_path = s.temp_path;
  out.image_path = s.image_path;
  out.persistent_state = s.persistent_state;
  out.persistent_epoch_s = s.persistent_epoch_s;
  out.confirmed_sw_version = s.confirmed_sw_version;
  out.confirmed_build_id = s.confirmed_build_id;

  out_message = s.message.empty() ? "ok" : s.message;
  if (s.persistent_state == "pending_boot") {
    out_message += " | pending boot confirmation";
  } else if (s.persistent_state == "boot_ok") {
    out_message += " | updated correctly";
    out_message += s.boot_report_pending ? " | report_pending=yes" : " | report_pending=no";
    if (!s.confirmed_sw_version.empty()) {
      out_message += " sw=" + s.confirmed_sw_version;
    }
    if (!s.confirmed_build_id.empty()) {
      out_message += " build=" + s.confirmed_build_id;
    }
  }
  return true;
}

bool OtaDescriptorAdapter::getOtaManifest(std::vector<OtaManifestEntry>& out, std::string& out_message) {
  manager_.tick();
  return listImageManifest_(out, out_message);
}

bool OtaDescriptorAdapter::rebuildOtaManifest(std::string& out_message) {
  manager_.tick();
  std::vector<OtaManifestEntry> tmp;
  if (!listImageManifest_(tmp, out_message)) {
    return false;
  }

  uint32_t rebuilt = 0U;
  for (auto& entry : tmp) {
    std::string msg;
    uint32_t crc = 0U;
    if (!computeFileCrc_(imagePathForName_(entry.file_name), entry.size_bytes, crc, msg)) {
      out_message = msg.empty() ? "manifest rebuild crc failed" : msg;
      return false;
    }
    entry.crc32 = crc;
    if (entry.created_epoch_s == 0U) {
      entry.created_epoch_s = static_cast<uint32_t>(std::time(nullptr));
    }
    if (entry.state.empty()) {
      entry.state = "ready";
    }
    if (entry.required_app_bytes == 0U) {
      entry.required_app_bytes = entry.size_bytes;
    }
    if (!writeManifestFile_(entry, msg)) {
      out_message = msg;
      return false;
    }
    ++rebuilt;
  }

  char buf[64] = {0};
  std::snprintf(buf, sizeof(buf), "manifest rebuilt count=%lu", static_cast<unsigned long>(rebuilt));
  out_message = std::string(buf);
  return true;
}

bool OtaDescriptorAdapter::clearOtaScope(const std::string& scope, std::string& out_message) {
  return clearOtaScopeInternal_(scope, out_message, false);
}

bool OtaDescriptorAdapter::clearOtaScopeInternal_(const std::string& scope,
                                                  std::string& out_message,
                                                  bool allow_archive) {
  manager_.tick();
  const std::string s = trim_(scope);
  if (s.size() > 4U && s.find("arc.") == 0U) {
    if (!allow_archive) {
      out_message = "invalid ota clear scope";
      return false;
    }
    std::vector<std::string> parts;
    size_t pos = 0U;
    while (pos <= s.size()) {
      const size_t dot = s.find('.', pos);
      if (dot == std::string::npos) {
        parts.push_back(s.substr(pos));
        break;
      }
      parts.push_back(s.substr(pos, dot - pos));
      pos = dot + 1U;
    }

    if (parts.size() < 3U) {
      out_message = "invalid archive scope";
      return false;
    }
    std::string op = parts[1];
    if (op == "save_staged") {
      op = "save.staged";
    }
    char role = 's';
    if (!normalizeArchiveRole(parts[2], role)) {
      out_message = "invalid archive role";
      return false;
    }
    const std::string archive_bucket = (role == 's') ? "/a/s" : "/a/m";
    const std::string archive_manifest = joinPath_(config_.st_dir, std::string("a") + role + ".jsn");

    auto loadArchiveManifest = [&](std::vector<OtaArchiveEntry>& entries, std::string& msg_out) -> bool {
      entries.clear();
      OtaStorageStat st{};
      std::string msg;
      if (!storage_.stat(archive_manifest, st, msg)) {
        msg_out = msg.empty() ? "archive manifest stat failed" : msg;
        return false;
      }
      if (!st.exists) {
        msg_out = "ok";
        return true;
      }
      if (st.is_dir || st.size_bytes == 0U) {
        msg_out = "ok";
        return true;
      }
      if (st.size_bytes > 16384U) {
        msg_out = "archive manifest too large";
        return false;
      }
      std::vector<uint8_t> buf(st.size_bytes, 0U);
      size_t read_len = 0U;
      if (!storage_.readAt(archive_manifest, 0U, buf.data(), buf.size(), read_len, msg)) {
        msg_out = msg.empty() ? "archive manifest read failed" : msg;
        return false;
      }
      const std::string text(reinterpret_cast<const char*>(buf.data()),
                             reinterpret_cast<const char*>(buf.data() + read_len));
      size_t p = 0U;
      while (true) {
        p = text.find("{\"id\":\"", p);
        if (p == std::string::npos) break;
        const size_t end = text.find('}', p);
        if (end == std::string::npos || end <= p) break;
        const std::string obj = text.substr(p, end - p + 1U);
        OtaArchiveEntry e{};
        if (!extractJsonStringFieldLoose(obj, "id", e.id) ||
            !extractJsonStringFieldLoose(obj, "bin", e.bin_name) ||
            !extractJsonStringFieldLoose(obj, "meta", e.meta_name) ||
            !extractJsonStringFieldLoose(obj, "sw", e.sw_version) ||
            !extractJsonStringFieldLoose(obj, "build", e.build_id) ||
            !extractJsonStringFieldLoose(obj, "target", e.target_role) ||
            !extractJsonStringFieldLoose(obj, "source", e.source) ||
            !extractJsonU32FieldLoose(obj, "size", e.size_bytes) ||
            !extractJsonU32FieldLoose(obj, "crc", e.crc32) ||
            !extractJsonU32FieldLoose(obj, "ts", e.created_epoch_s)) {
          msg_out = "archive manifest entry missing required fields";
          return false;
        }
        if (e.id.empty() || e.bin_name.empty() || e.meta_name.empty() || e.sw_version.empty() ||
            e.build_id.empty() || e.target_role.empty() || e.source.empty()) {
          msg_out = "archive manifest entry has empty required fields";
          return false;
        }
        if (e.size_bytes == 0U) {
          msg_out = "archive manifest entry invalid size";
          return false;
        }
        for (char& c : e.target_role) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e.target_role != "master" && e.target_role != "slave") {
          msg_out = "archive manifest entry invalid target role";
          return false;
        }
        const char entry_role = (e.target_role == "slave") ? 's' : 'm';
        if (entry_role != role) {
          msg_out = "archive manifest entry role mismatch";
          return false;
        }
        entries.push_back(e);
        p = end + 1U;
      }
      msg_out = "ok";
      return true;
    };

    auto saveArchiveManifest = [&](const std::vector<OtaArchiveEntry>& entries,
                                   std::string& msg_out) -> bool {
      std::string msg;
      if (!storage_.ensureDir(config_.st_dir, msg)) {
        msg_out = msg.empty() ? "state dir unavailable" : msg;
        return false;
      }
      std::string json = "{\"ver\":1,\"role\":\"";
      json.push_back(role);
      json += "\",\"entries\":[";
      for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (i > 0U) json += ",";
        char num[32] = {0};
        json += "{\"id\":\"" + jsonEscapeLoose(e.id) + "\"";
        json += ",\"bin\":\"" + jsonEscapeLoose(e.bin_name) + "\"";
        json += ",\"meta\":\"" + jsonEscapeLoose(e.meta_name) + "\"";
        json += ",\"sw\":\"" + jsonEscapeLoose(e.sw_version) + "\"";
        json += ",\"build\":\"" + jsonEscapeLoose(e.build_id) + "\"";
        json += ",\"target\":\"" + jsonEscapeLoose(e.target_role) + "\"";
        json += ",\"source\":\"" + jsonEscapeLoose(e.source) + "\"";
        std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(e.size_bytes));
        json += ",\"size\":" + std::string(num);
        std::snprintf(num, sizeof(num), "0x%08lX", static_cast<unsigned long>(e.crc32));
        json += ",\"crc\":\"" + std::string(num) + "\"";
        std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(e.created_epoch_s));
        json += ",\"ts\":" + std::string(num);
        json += "}";
      }
      json += "]}\n";

      if (!storage_.truncateFile(archive_manifest, msg)) {
        msg_out = msg.empty() ? "archive manifest truncate failed" : msg;
        return false;
      }
      if (!json.empty() &&
          !storage_.writeAt(archive_manifest,
                            0U,
                            reinterpret_cast<const uint8_t*>(json.data()),
                            json.size(),
                            msg)) {
        msg_out = msg.empty() ? "archive manifest write failed" : msg;
        return false;
      }
      msg_out = "ok";
      return true;
    };

    auto findEntryById = [](std::vector<OtaArchiveEntry>& entries,
                            const std::string& id) -> std::vector<OtaArchiveEntry>::iterator {
      return std::find_if(entries.begin(), entries.end(), [&](const OtaArchiveEntry& e) {
        return e.id == id;
      });
    };

    if (op == "list") {
      std::vector<OtaArchiveEntry> entries;
      std::string msg;
      if (!loadArchiveManifest(entries, msg)) {
        out_message = msg;
        return false;
      }
      if (entries.empty()) {
        out_message = std::string("archive role=") + role + " entries=0";
        return true;
      }
      std::string summary = std::string("archive role=") + role + " entries=" +
                            std::to_string(static_cast<unsigned int>(entries.size())) + " ids=";
      const size_t max_show = std::min<size_t>(entries.size(), 8U);
      for (size_t i = 0; i < max_show; ++i) {
        if (i > 0U) summary += ",";
        summary += entries[i].id;
      }
      out_message = summary;
      return true;
    }

    if (op == "verify") {
      if (parts.size() < 4U) {
        out_message = "archive id is required";
        return false;
      }
      const std::string id = normalizeArchiveId(parts[3]);
      if (id.empty()) {
        out_message = "invalid archive id";
        return false;
      }

      std::vector<OtaArchiveEntry> entries;
      std::string msg;
      if (!loadArchiveManifest(entries, msg)) {
        out_message = msg;
        return false;
      }
      auto it = findEntryById(entries, id);
      if (it == entries.end()) {
        out_message = "archive id not found";
        return false;
      }
      const char entry_role = (it->target_role == "slave") ? 's' : 'm';
      if (entry_role != role) {
        out_message = "archive role mismatch";
        return false;
      }

      const std::string bin_path = joinPath_(archive_bucket, it->bin_name);
      OtaStorageStat bst{};
      if (!storage_.stat(bin_path, bst, msg) || !bst.exists || bst.is_dir || bst.size_bytes == 0U) {
        out_message = "archive verify failed: bin missing/invalid";
        return false;
      }
      if (it->size_bytes > 0U && bst.size_bytes != it->size_bytes) {
        out_message = "archive verify failed: size mismatch";
        return false;
      }

      uint32_t crc = 0U;
      if (!computeFileCrc_(bin_path, bst.size_bytes, crc, msg)) {
        out_message = msg.empty() ? "archive verify failed: crc read failed" : msg;
        return false;
      }
      if (it->crc32 != 0U && crc != it->crc32) {
        out_message = "archive verify failed: crc mismatch";
        return false;
      }

      const std::string meta_path = joinPath_(archive_bucket, it->meta_name);
      OtaStorageStat mst{};
      if (!storage_.stat(meta_path, mst, msg) || !mst.exists || mst.is_dir || mst.size_bytes == 0U) {
        out_message = "archive verify failed: metadata missing/invalid";
        return false;
      }
      std::vector<uint8_t> meta_buf(mst.size_bytes, 0U);
      size_t meta_len = 0U;
      if (!storage_.readAt(meta_path, 0U, meta_buf.data(), meta_buf.size(), meta_len, msg)) {
        out_message = msg.empty() ? "archive verify failed: metadata read failed" : msg;
        return false;
      }
      std::string meta_text(reinterpret_cast<const char*>(meta_buf.data()),
                            reinterpret_cast<const char*>(meta_buf.data() + meta_len));

      std::string sw;
      std::string build;
      std::string target;
      (void)extractJsonStringFieldLoose(meta_text, "sw_version", sw);
      (void)extractJsonStringFieldLoose(meta_text, "build_id", build);
      (void)extractJsonStringFieldLoose(meta_text, "target_role", target);
      for (char& c : target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      if (sw.empty()) {
        out_message = "archive verify failed: metadata missing sw_version";
        return false;
      }
      if (build.empty()) {
        out_message = "archive verify failed: metadata missing build_id";
        return false;
      }
      if (target != "master" && target != "slave") {
        out_message = "archive verify failed: metadata missing/invalid target_role";
        return false;
      }

      std::string expected_target = it->target_role;
      for (char& c : expected_target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (expected_target != "master" && expected_target != "slave") {
        out_message = "archive verify failed: manifest target_role invalid";
        return false;
      }
      if (target != expected_target) {
        out_message = "archive verify failed: metadata target mismatch";
        return false;
      }
      if (!it->sw_version.empty() && sw != it->sw_version) {
        out_message = "archive verify failed: metadata sw mismatch";
        return false;
      }
      if (!it->build_id.empty() && build != it->build_id) {
        out_message = "archive verify failed: metadata build mismatch";
        return false;
      }

      char crc_buf[16] = {0};
      std::snprintf(crc_buf, sizeof(crc_buf), "0x%08lX", static_cast<unsigned long>(crc));
      out_message = std::string("archive verified id=") + id +
                    " role=" + role +
                    " size=" + std::to_string(bst.size_bytes) +
                    " crc=" + crc_buf +
                    " target=" + target +
                    " sw=" + sw +
                    " build=" + build;
      return true;
    }

    if (op == "save" || op == "save.staged") {
      const bool save_staged = (op == "save.staged");
      if (!save_staged && role != 's') {
        out_message = "running save requires role=slave";
        return false;
      }

      std::vector<OtaArchiveEntry> entries;
      OtaArchiveEntry entry{};
      entry.created_epoch_s = static_cast<uint32_t>(std::time(nullptr));
      auto assign_unique_id = [&](void) -> bool {
        std::string msg;
        if (!loadArchiveManifest(entries, msg)) {
          out_message = msg;
          return false;
        }
        uint32_t seed = static_cast<uint32_t>(std::time(nullptr)) ^
                        static_cast<uint32_t>(entries.size() * 2654435761UL);
        std::string id = formatArchiveId(seed);
        for (uint16_t i = 0; i < 1024U && findEntryById(entries, id) != entries.end(); ++i) {
          ++seed;
          id = formatArchiveId(seed);
        }
        entry.id = id;
        entry.bin_name = id + ".bin";
        entry.meta_name = id + ".jsn";
        return true;
      };

      std::string msg;
      if (save_staged) {
        std::string stage_bin = joinPath_(config_.stg_dir, "fw.bin");
        OtaStorageStat st{};
        if (!storage_.stat(stage_bin, st, msg) || !st.exists || st.is_dir || st.size_bytes == 0U) {
          std::vector<std::string> names;
          if (!storage_.listDir(config_.stg_dir, names, msg)) {
            out_message = msg.empty() ? "archive save failed: staging dir unavailable" : msg;
            return false;
          }
          auto isBinName = [](const std::string& n) {
            if (n.size() < 5U) return false;
            std::string ext = n.substr(n.size() - 4U);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return ext == ".bin";
          };
          const auto it_bin = std::find_if(names.begin(), names.end(), isBinName);
          if (it_bin == names.end()) {
            out_message = "archive save failed: no staged .bin";
            return false;
          }
          stage_bin = joinPath_(config_.stg_dir, *it_bin);
          if (!storage_.stat(stage_bin, st, msg) || !st.exists || st.is_dir || st.size_bytes == 0U) {
            out_message = "archive save failed: staged bin missing";
            return false;
          }
        }

        std::string stage_meta;
        {
          std::string base = stage_bin;
          const size_t dot = base.find_last_of('.');
          if (dot != std::string::npos) {
            base = base.substr(0U, dot);
          }
          stage_meta = base + ".json";
          OtaStorageStat mst{};
          if (!storage_.stat(stage_meta, mst, msg) || !mst.exists || mst.is_dir || mst.size_bytes == 0U) {
            out_message = "archive save failed: staged metadata missing";
            return false;
          }
        }

        uint32_t crc = 0U;
        if (!computeFileCrc_(stage_bin, st.size_bytes, crc, msg)) {
          out_message = msg.empty() ? "archive save crc failed" : msg;
          return false;
        }

        std::string meta_text;
        {
          OtaStorageStat mst{};
          if (!storage_.stat(stage_meta, mst, msg) || !mst.exists || mst.is_dir || mst.size_bytes == 0U) {
            out_message = "archive save failed: staged metadata missing";
            return false;
          }
          std::vector<uint8_t> buf(mst.size_bytes, 0U);
          size_t out_len = 0U;
          if (!storage_.readAt(stage_meta, 0U, buf.data(), buf.size(), out_len, msg)) {
            out_message = msg.empty() ? "archive save metadata read failed" : msg;
            return false;
          }
          meta_text.assign(reinterpret_cast<const char*>(buf.data()),
                           reinterpret_cast<const char*>(buf.data() + out_len));
        }

        std::string sw;
        std::string build;
        std::string target;
        (void)extractJsonStringFieldLoose(meta_text, "sw_version", sw);
        (void)extractJsonStringFieldLoose(meta_text, "build_id", build);
        (void)extractJsonStringFieldLoose(meta_text, "target_role", target);
        for (char& c : target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (sw.empty()) {
          out_message = "archive save failed: metadata missing sw_version";
          return false;
        }
        if (build.empty()) {
          out_message = "archive save failed: metadata missing build_id";
          return false;
        }
        if (target != "master" && target != "slave") {
          out_message = "archive save failed: metadata missing/invalid target_role";
          return false;
        }
        const char target_role = (target == "slave") ? 's' : 'm';
        if (target_role != role) {
          out_message = "archive save failed: role mismatch";
          return false;
        }

        if (!assign_unique_id()) {
          return false;
        }
        entry.size_bytes = st.size_bytes;
        entry.crc32 = crc;
        entry.sw_version = sw;
        entry.build_id = build;
        entry.target_role = target;
        entry.source = "staged";

        if (!storage_.copySpiffsToSd(stage_bin, joinPath_(archive_bucket, entry.bin_name), msg)) {
          out_message = msg.empty() ? "archive save failed: bin copy failed" : msg;
          return false;
        }
        if (!storage_.copySpiffsToSd(stage_meta, joinPath_(archive_bucket, entry.meta_name), msg)) {
          out_message = msg.empty() ? "archive save failed: meta copy failed" : msg;
          return false;
        }
      } else {
        if (!assign_unique_id()) {
          return false;
        }
        entry.target_role = "slave";
        entry.source = "running";
        entry.sw_version = "running";
        entry.build_id = "na";
        if (!storage_.dumpRunningFirmwareToSd(joinPath_(archive_bucket, entry.bin_name),
                                              entry.size_bytes,
                                              entry.crc32,
                                              msg)) {
          out_message = msg.empty() ? "archive save failed: running image dump failed" : msg;
          return false;
        }
        const std::string tmp_meta = joinPath_(config_.st_dir, "_arc_tmp.jsn");
        const std::string meta_json = std::string("{\"sw_version\":\"") + jsonEscapeLoose(entry.sw_version) +
                                      "\",\"build_id\":\"" + jsonEscapeLoose(entry.build_id) +
                                      "\",\"target_role\":\"slave\",\"source\":\"running\"}\n";
        if (!storage_.ensureDir(config_.st_dir, msg)) {
          out_message = msg.empty() ? "archive save failed: state dir unavailable" : msg;
          return false;
        }
        if (!storage_.truncateFile(tmp_meta, msg)) {
          out_message = msg.empty() ? "archive save failed: temp meta truncate failed" : msg;
          return false;
        }
        if (!storage_.writeAt(tmp_meta,
                              0U,
                              reinterpret_cast<const uint8_t*>(meta_json.data()),
                              meta_json.size(),
                              msg)) {
          out_message = msg.empty() ? "archive save failed: temp meta write failed" : msg;
          return false;
        }
        if (!storage_.copySpiffsToSd(tmp_meta, joinPath_(archive_bucket, entry.meta_name), msg)) {
          (void)storage_.removePath(tmp_meta, msg);
          out_message = msg.empty() ? "archive save failed: running meta copy failed" : msg;
          return false;
        }
        (void)storage_.removePath(tmp_meta, msg);
      }

      entries.push_back(entry);
      if (!saveArchiveManifest(entries, msg)) {
        out_message = msg;
        return false;
      }
      out_message = std::string("archive saved id=") + entry.id + " role=" + role +
                    " source=" + entry.source;
      return true;
    }

    if (op == "restore" || op == "delete") {
      if (parts.size() < 4U) {
        out_message = "archive id is required";
        return false;
      }
      const std::string id = normalizeArchiveId(parts[3]);
      if (id.empty()) {
        out_message = "invalid archive id";
        return false;
      }

      std::vector<OtaArchiveEntry> entries;
      std::string msg;
      if (!loadArchiveManifest(entries, msg)) {
        out_message = msg;
        return false;
      }
      auto it = findEntryById(entries, id);
      if (it == entries.end()) {
        out_message = "archive id not found";
        return false;
      }
      const char entry_role = (it->target_role == "slave") ? 's' : 'm';
      if (entry_role != role) {
        out_message = "archive role mismatch";
        return false;
      }

      if (op == "restore") {
        const std::string stage_bin = joinPath_(config_.stg_dir, "fw.bin");
        const std::string stage_meta = joinPath_(config_.stg_dir, "fw.json");
        if (!storage_.copySdToSpiffs(joinPath_(archive_bucket, it->bin_name), stage_bin, msg)) {
          out_message = msg.empty() ? "archive restore bin failed" : msg;
          return false;
        }
        if (!storage_.copySdToSpiffs(joinPath_(archive_bucket, it->meta_name), stage_meta, msg)) {
          out_message = msg.empty() ? "archive restore meta failed" : msg;
          return false;
        }
        out_message = std::string("archive restored id=") + id;
        return true;
      }

      if (!storage_.removePathOnSd(joinPath_(archive_bucket, it->bin_name), msg)) {
        out_message = msg.empty() ? "archive delete bin failed" : msg;
        return false;
      }
      if (!storage_.removePathOnSd(joinPath_(archive_bucket, it->meta_name), msg)) {
        out_message = msg.empty() ? "archive delete meta failed" : msg;
        return false;
      }
      entries.erase(it);
      if (!saveArchiveManifest(entries, msg)) {
        out_message = msg;
        return false;
      }
      out_message = std::string("archive deleted id=") + id;
      return true;
    }

    if (op == "clear") {
      std::vector<OtaArchiveEntry> entries;
      std::string msg;
      if (!loadArchiveManifest(entries, msg)) {
        out_message = msg;
        return false;
      }
      for (const auto& entry : entries) {
        if (!storage_.removePathOnSd(joinPath_(archive_bucket, entry.bin_name), msg)) {
          out_message = msg.empty() ? "archive clear bin failed" : msg;
          return false;
        }
        if (!storage_.removePathOnSd(joinPath_(archive_bucket, entry.meta_name), msg)) {
          out_message = msg.empty() ? "archive clear meta failed" : msg;
          return false;
        }
      }
      entries.clear();
      if (!saveArchiveManifest(entries, msg)) {
        out_message = msg;
        return false;
      }
      out_message = std::string("archive cleared role=") + role;
      return true;
    }

    out_message = "invalid archive operation";
    return false;
  }

  if (s == "in") {
    manager_.abortActiveReceive(nullptr);
    return clearDirContents_(config_.in_dir, out_message);
  }
  if (s == "img") {
    if (!clearDirContents_(config_.img_dir, out_message)) return false;
    if (!clearDirContents_(config_.man_dir, out_message)) return false;
    out_message = "ota images cleared";
    return true;
  }
  if (s == "man") return clearDirContents_(config_.man_dir, out_message);
  if (s == "all") {
    manager_.abortActiveReceive(nullptr);
    if (!clearDirContents_(config_.in_dir, out_message)) return false;
    if (!clearDirContents_(config_.img_dir, out_message)) return false;
    if (!clearDirContents_(config_.man_dir, out_message)) return false;
    if (!clearDirContents_(config_.st_dir, out_message)) return false;
    out_message = "ota scope cleared";
    return true;
  }
  out_message = "invalid ota clear scope";
  return false;
}

bool OtaDescriptorAdapter::getOtaCapacity(OtaCapacityInfo& out, std::string& out_message) {
  manager_.tick();
  out = OtaCapacityInfo{};
  out.max_fw_bytes = manager_.maxFirmwareBytes();
  out.last_checked_image_bytes = last_checked_image_bytes_;
  out.last_fit = last_fit_;
  out_message = "ok";
  return true;
}

bool OtaDescriptorAdapter::getOtaGateInfo(OtaGateInfo& out, std::string& out_message) {
  manager_.tick();
  out = OtaGateInfo{};
  const OtaRuntimeStatus& s = manager_.status();
  switch (s.code) {
    case OtaStatusCode::GateDenied:
      out.decision = static_cast<uint8_t>(OtaGateDecision::Denied);
      break;
    case OtaStatusCode::GateBusy:
      out.decision = static_cast<uint8_t>(OtaGateDecision::Busy);
      break;
    case OtaStatusCode::GatePrepFailed:
      out.decision = static_cast<uint8_t>(OtaGateDecision::PrepFailed);
      break;
    default:
      out.decision = static_cast<uint8_t>(OtaGateDecision::Ready);
      break;
  }
  out.detail = s.message;
  out_message = "ok";
  return true;
}

bool OtaDescriptorAdapter::applyOtaImage(const std::string& target, std::string& out_message) {
  manager_.tick();
  const std::string t = trim_(target);
  if (t.empty()) {
    out_message = "ota apply target is empty";
    return false;
  }
  if (t.size() > 4U && t.find("arc.") == 0U) {
    // Archive operations are routed through ota.apply targets for management command parity.
    return clearOtaScopeInternal_(t, out_message, true);
  }
  if (t == "rollback" || t == "@rollback") {
    return manager_.rollback(out_message);
  }

  std::vector<OtaManifestEntry> manifest;
  std::string msg;
  if (!listImageManifest_(manifest, msg)) {
    out_message = msg;
    return false;
  }

  const OtaManifestEntry* selected = nullptr;
  uint32_t wanted_id = 0;
  if (parseTargetId_(t, wanted_id)) {
    for (const auto& item : manifest) {
      if (item.file_id == wanted_id) {
        selected = &item;
        break;
      }
    }
  } else {
    for (const auto& item : manifest) {
      if (item.file_name == t) {
        selected = &item;
        break;
      }
    }
  }

  if (selected == nullptr) {
    out_message = "ota image not found";
    return false;
  }

  if (selected->version.empty()) {
    out_message = "ota metadata missing version";
    return false;
  }
  const std::string current = trim_(manager_.status().confirmed_sw_version);
  if (!current.empty()) {
    const int cmp = compareVersions_(selected->version, current);
    if (cmp <= 0) {
      out_message = "ota version is not newer than running image";
      return false;
    }
  }

  last_checked_image_bytes_ = selected->size_bytes;
  const uint32_t max_fw = manager_.maxFirmwareBytes();
  last_fit_ = (max_fw == 0U) || (selected->size_bytes <= max_fw);
  if (!last_fit_) {
    out_message = "image too large";
    return false;
  }

  OtaManifestEntry apply_entry = *selected;
  const std::string image_path = imagePathForName_(apply_entry.file_name);
  if (apply_entry.crc32 == 0U) {
    std::string msg;
    uint32_t crc = 0U;
    if (!computeFileCrc_(image_path, apply_entry.size_bytes, crc, msg)) {
      out_message = msg.empty() ? "crc compute failed" : msg;
      return false;
    }
    apply_entry.crc32 = crc;
  }

  const bool ok = manager_.applyImage(image_path,
                                      apply_entry.size_bytes,
                                      apply_entry.crc32,
                                      out_message);
  if (ok) {
    apply_entry.state = "applied";
    if (apply_entry.created_epoch_s == 0U) {
      apply_entry.created_epoch_s = static_cast<uint32_t>(std::time(nullptr));
    }
    apply_entry.required_app_bytes = apply_entry.size_bytes;
    std::string msg;
    (void)writeManifestFile_(apply_entry, msg);
  }
  return ok;
}

}  // namespace espnow_link
