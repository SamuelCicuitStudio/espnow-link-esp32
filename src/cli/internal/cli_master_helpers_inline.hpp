/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Inline helper implementations shared by split Master CLI translation units.
 **************************************************************/
#pragma once

namespace espnow_link {

using namespace cli_helpers;

namespace {

std::string otaSidecarJsonPath(const std::string& bin_path) {
  const size_t slash = bin_path.find_last_of('/');
  const size_t dot = bin_path.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    return bin_path.substr(0U, dot) + ".json";
  }
  return bin_path + ".json";
}

std::string otaFileNameFromPath(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::string p = path;
  for (char& c : p) {
    if (c == '\\') {
      c = '/';
    }
  }
  const size_t sep = p.find_last_of('/');
  if (sep == std::string::npos) {
    return p;
  }
  if (sep + 1U >= p.size()) {
    return {};
  }
  return p.substr(sep + 1U);
}

std::string otaImageNameFromCorr(uint32_t corr_id) {
  (void)corr_id;
  return std::string("u.bin");
}

std::string resolveStagedPathInput(const std::string& staged_name, const std::string& cwd) {
  std::string path;
  const bool has_sep =
      (staged_name.find('/') != std::string::npos) || (staged_name.find('\\') != std::string::npos);
  if (has_sep) {
    path = staged_name;
    for (char& c : path) {
      if (c == '\\') {
        c = '/';
      }
    }
    if (!path.empty() && path.front() != '/') {
      std::string normalized_cwd = cwd;
      for (char& c : normalized_cwd) {
        if (c == '\\') {
          c = '/';
        }
      }
      if (normalized_cwd.empty() || normalized_cwd.back() != '/') {
        normalized_cwd.push_back('/');
      }
      path = normalized_cwd + path;
    }
    return path;
  }
  if (staged_name.empty()) {
    return std::string(ota_paths::kStaging) + "/" + ota_paths::kStagedBinName;
  }
  return std::string(ota_paths::kStaging) + "/" + MasterCli::shortOtaName(staged_name);
}

const char* otaStatusCodeName(uint16_t code) {
  switch (static_cast<OtaStatusCode>(code)) {
    case OtaStatusCode::Ok:
      return "ok";
    case OtaStatusCode::StorageNotReady:
      return "storage_not_ready";
    case OtaStatusCode::GateDenied:
      return "gate_denied";
    case OtaStatusCode::GateBusy:
      return "gate_busy";
    case OtaStatusCode::GatePrepFailed:
      return "gate_prep_failed";
    case OtaStatusCode::ImageTooLarge:
      return "image_too_large";
    case OtaStatusCode::InvalidState:
      return "invalid_state";
    case OtaStatusCode::InvalidArgument:
      return "invalid_argument";
    case OtaStatusCode::OffsetMismatch:
      return "offset_mismatch";
    case OtaStatusCode::SizeMismatch:
      return "size_mismatch";
    case OtaStatusCode::CrcMismatch:
      return "crc_mismatch";
    case OtaStatusCode::ApplyRejected:
      return "apply_rejected";
    case OtaStatusCode::ApplyFailed:
      return "apply_failed";
    case OtaStatusCode::Timeout:
      return "timeout";
    case OtaStatusCode::InternalError:
      return "internal_error";
    default:
      return "?";
  }
}

bool readTextFile(IOtaStorageBackend& storage,
                  const std::string& path,
                  std::string& out_text,
                  std::string& out_error) {
  out_text.clear();
  OtaStorageStat st{};
  std::string msg;
  if (!storage.stat(path, st, msg)) {
    out_error = msg.empty() ? "stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    out_error = "file missing";
    return false;
  }
  if (st.size_bytes > 4096U) {
    out_error = "file too large";
    return false;
  }

  std::vector<uint8_t> buf(st.size_bytes, 0U);
  size_t out_len = 0U;
  if (!storage.readAt(path, 0U, buf.data(), buf.size(), out_len, msg)) {
    out_error = msg.empty() ? "read failed" : msg;
    return false;
  }
  if (out_len == 0U) {
    out_error = "empty file";
    return false;
  }

  out_text.assign(reinterpret_cast<const char*>(buf.data()),
                  reinterpret_cast<const char*>(buf.data() + out_len));
  out_error.clear();
  return true;
}

bool extractJsonStringField(const std::string& json,
                            const char* key,
                            std::string& out_value) {
  out_value.clear();
  if (key == nullptr || key[0] == '\0') {
    return false;
  }
  const std::string pattern = std::string("\"") + key + "\"";
  const size_t key_pos = json.find(pattern);
  if (key_pos == std::string::npos) {
    return false;
  }
  size_t pos = json.find(':', key_pos + pattern.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos >= json.size()) {
    return false;
  }
  if (json[pos] == '"') {
    ++pos;
    std::string value;
    value.reserve(32U);
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
        out_value = trim(value);
        return !out_value.empty();
      }
      value.push_back(c);
    }
    return false;
  }

  size_t end = pos;
  while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n' && json[end] != '\r') {
    ++end;
  }
  out_value = trim(json.substr(pos, end - pos));
  return !out_value.empty();
}

bool loadFirmwareMetadataFromSidecar(IOtaStorageBackend& storage,
                                     const std::string& bin_path,
                                     FirmwareImageMetadata& out_meta,
                                     std::string& out_sidecar_path,
                                     std::string& out_error) {
  out_meta = FirmwareImageMetadata{};
  out_sidecar_path = otaSidecarJsonPath(bin_path);

  std::string json;
  if (!readTextFile(storage, out_sidecar_path, json, out_error)) {
    return false;
  }

  std::string version;
  (void)extractJsonStringField(json, "sw_version", version);
  if (version.empty()) {
    out_error = "missing sw_version";
    return false;
  }

  std::string build;
  (void)extractJsonStringField(json, "build_id", build);
  if (build.empty()) {
    out_error = "missing build_id";
    return false;
  }

  std::string target_role;
  (void)extractJsonStringField(json, "target_role", target_role);
  target_role = lowerCopy(trim(target_role));
  if (target_role != "master" && target_role != "slave") {
    out_error = "missing/invalid target_role (master|slave)";
    return false;
  }

  if (version.size() > 63U || build.size() > 63U || target_role.size() > 15U) {
    out_error = "metadata field too long";
    return false;
  }

  out_meta.sw_version = version;
  out_meta.build_id = build;
  out_meta.target_role = target_role;
  out_error.clear();
  return true;
}

bool extractProfileIdFromCapabilities(const DescriptorResponse& d, ProfileId& out_profile_id) {
  out_profile_id = kProfileUnknown;
  if (d.type != DescriptorResponseType::Capabilities) {
    return false;
  }
  for (const auto& cap : d.capabilities) {
    if (cap.key != "profile_id") {
      continue;
    }
    const unsigned long parsed = std::strtoul(cap.description.c_str(), nullptr, 10);
    if (parsed > 0U && parsed <= 0xFFFFUL) {
      out_profile_id = static_cast<ProfileId>(parsed);
      return true;
    }
  }
  return false;
}

ProfileId profileIdFromRoleCode(uint8_t role_code) {
  if (role_code == static_cast<uint8_t>(kProfilePms & 0xFFU)) {
    return kProfilePms;
  }
  if (role_code == static_cast<uint8_t>(kProfileRelay & 0xFFU)) {
    return kProfileRelay;
  }
  if (role_code == static_cast<uint8_t>(kProfileSens & 0xFFU)) {
    return kProfileSens;
  }
  if (role_code == static_cast<uint8_t>(kProfileSemu & 0xFFU)) {
    return kProfileSemu;
  }
  if (role_code == static_cast<uint8_t>(kProfileRemu & 0xFFU)) {
    return kProfileRemu;
  }
  if (role_code == static_cast<uint8_t>(kProfileLockAlarm & 0xFFU)) {
    return kProfileLockAlarm;
  }
  return kProfileUnknown;
}

}  // namespace

}  // namespace espnow_link


