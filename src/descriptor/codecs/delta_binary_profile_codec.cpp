#include "espnow_link/codec.hpp"
#include "espnow_link/protocol.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace espnow_link {

namespace {

constexpr uint8_t kDeltaMagicDesc = 0xF1;
constexpr uint8_t kDeltaMagicCtrl = 0xF2;
constexpr uint8_t kDeltaVersion = 1;
constexpr uint8_t kDeltaKindQuery = 1;
constexpr uint8_t kDeltaKindResponse = 2;
constexpr uint8_t kDeltaKindCommand = 1;
constexpr uint8_t kDeltaKindResult = 2;

bool canAppend(const std::vector<uint8_t>& out, size_t n) {
  return (out.size() + n) <= ProtocolCodec::kMaxPayload;
}

bool appendU8(std::vector<uint8_t>& out, uint8_t v) {
  if (!canAppend(out, 1)) return false;
  out.push_back(v);
  return true;
}

bool appendU16(std::vector<uint8_t>& out, uint16_t v) {
  if (!canAppend(out, 2)) return false;
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  return true;
}

bool appendU32(std::vector<uint8_t>& out, uint32_t v) {
  if (!canAppend(out, 4)) return false;
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  return true;
}

bool appendF32(std::vector<uint8_t>& out, float v) {
  if (!canAppend(out, 4)) return false;
  uint8_t b[4] = {0};
  std::memcpy(b, &v, sizeof(v));
  out.insert(out.end(), b, b + 4);
  return true;
}

bool appendStr16(std::vector<uint8_t>& out, const std::string& s) {
  if (s.size() > 0xFFFFU) return false;
  if (!appendU16(out, static_cast<uint16_t>(s.size()))) return false;
  if (!canAppend(out, s.size())) return false;
  out.insert(out.end(), s.begin(), s.end());
  return true;
}

bool readU8(const uint8_t* payload, size_t len, size_t& off, uint8_t& out) {
  if (payload == nullptr || off + 1 > len) return false;
  out = payload[off++];
  return true;
}

bool readU16(const uint8_t* payload, size_t len, size_t& off, uint16_t& out) {
  if (payload == nullptr || off + 2 > len) return false;
  out = static_cast<uint16_t>(payload[off]) | (static_cast<uint16_t>(payload[off + 1]) << 8);
  off += 2;
  return true;
}

bool readU32(const uint8_t* payload, size_t len, size_t& off, uint32_t& out) {
  if (payload == nullptr || off + 4 > len) return false;
  out = static_cast<uint32_t>(payload[off]) | (static_cast<uint32_t>(payload[off + 1]) << 8) |
        (static_cast<uint32_t>(payload[off + 2]) << 16) | (static_cast<uint32_t>(payload[off + 3]) << 24);
  off += 4;
  return true;
}

bool readF32(const uint8_t* payload, size_t len, size_t& off, float& out) {
  if (payload == nullptr || off + 4 > len) return false;
  std::memcpy(&out, payload + off, sizeof(out));
  off += 4;
  return true;
}

bool readStr16(const uint8_t* payload, size_t len, size_t& off, std::string& out) {
  out.clear();
  uint16_t n = 0;
  if (!readU16(payload, len, off, n)) return false;
  if (off + n > len) return false;
  out.assign(reinterpret_cast<const char*>(payload + off), reinterpret_cast<const char*>(payload + off + n));
  off += n;
  return true;
}

bool wrap(uint8_t magic,
          uint8_t kind,
          const uint8_t* inner,
          size_t inner_len,
          std::vector<uint8_t>& out_payload) {
  if (inner_len > 0xFFFFU) return false;
  out_payload.clear();
  return appendU8(out_payload, magic) && appendU8(out_payload, kDeltaVersion) && appendU8(out_payload, kind) &&
         appendU16(out_payload, static_cast<uint16_t>(inner_len)) &&
         (inner_len == 0 || (canAppend(out_payload, inner_len) &&
                             (out_payload.insert(out_payload.end(), inner, inner + inner_len), true)));
}

bool unwrap(const uint8_t* payload,
            size_t len,
            uint8_t expected_magic,
            uint8_t expected_kind,
            const uint8_t*& out_inner,
            size_t& out_inner_len) {
  if (payload == nullptr || len < 5) return false;
  if (payload[0] != expected_magic || payload[1] != kDeltaVersion || payload[2] != expected_kind) return false;
  const uint16_t n = static_cast<uint16_t>(payload[3]) | (static_cast<uint16_t>(payload[4]) << 8);
  if (len != static_cast<size_t>(5 + n)) return false;
  out_inner = payload + 5;
  out_inner_len = n;
  return true;
}

bool tryParseFloat(const std::string& s, float& out) {
  char* end = nullptr;
  out = std::strtof(s.c_str(), &end);
  return end != nullptr && *end == '\0';
}

bool encodeSetting(std::vector<uint8_t>& out, const SettingDescriptor& s) {
  return appendU16(out, s.setting_id) && appendStr16(out, s.key) &&
         appendU8(out, static_cast<uint8_t>(s.value_type)) && appendU8(out, s.writable ? 1 : 0) &&
         appendStr16(out, s.nvs_key) && appendStr16(out, s.current_value) && appendStr16(out, s.default_value) &&
         appendStr16(out, s.description);
}

bool decodeSetting(const uint8_t* payload, size_t len, size_t& off, SettingDescriptor& s) {
  uint8_t t = 0;
  if (!readU16(payload, len, off, s.setting_id)) return false;
  if (!readStr16(payload, len, off, s.key)) return false;
  if (!readU8(payload, len, off, t)) return false;
  s.value_type = static_cast<SettingValueType>(t);
  if (!readU8(payload, len, off, t)) return false;
  s.writable = (t != 0);
  if (!readStr16(payload, len, off, s.nvs_key)) return false;
  if (!readStr16(payload, len, off, s.current_value)) return false;
  if (!readStr16(payload, len, off, s.default_value)) return false;
  if (!readStr16(payload, len, off, s.description)) return false;
  return true;
}

}  // namespace

bool DeltaBinaryProfileCodec::encodeDescriptorQuery(const DescriptorQuery& query,
                                                    std::vector<uint8_t>& out_payload) const {
  if (query.type == DescriptorQueryType::GetLogStatus ||
      query.type == DescriptorQueryType::ReadLogChunk ||
      query.type == DescriptorQueryType::ClearLog ||
      query.type == DescriptorQueryType::SetLogControl ||
      query.type == DescriptorQueryType::GetStorageInfo ||
      query.type == DescriptorQueryType::ListStoragePath ||
      query.type == DescriptorQueryType::StatStoragePath ||
      query.type == DescriptorQueryType::FormatStorage ||
      query.type == DescriptorQueryType::GetOtaStatus ||
      query.type == DescriptorQueryType::GetOtaManifest ||
      query.type == DescriptorQueryType::RebuildOtaManifest ||
      query.type == DescriptorQueryType::ClearOtaScope ||
      query.type == DescriptorQueryType::GetOtaCapacity ||
      query.type == DescriptorQueryType::GetOtaGateInfo ||
      query.type == DescriptorQueryType::ApplyOtaImage) {
    return ::espnow_link::encodeDescriptorQuery(query, out_payload);
  }

  std::vector<uint8_t> inner;
  const bool has_key = !query.key.empty();
  const bool has_id = query.has_setting_id;
  const bool has_value = !query.value.empty();
  if (query.type == DescriptorQueryType::GetSetting || query.type == DescriptorQueryType::SetSetting) {
    if (has_key == has_id) {
      return false;
    }
  }

  uint8_t flags = 0;
  if (has_key) flags |= 0x01;
  if (has_id) flags |= 0x02;
  if (has_value) flags |= 0x04;
  if (query.time_epoch_s != 0) flags |= 0x08;
  if (query.paged) flags |= 0x10;
  if (query.paged) flags |= 0x20;
  if (query.paged) flags |= 0x40;

  if (!appendU8(inner, static_cast<uint8_t>(query.type)) || !appendU8(inner, flags)) return false;
  if ((flags & 0x02U) && !appendU16(inner, query.setting_id)) return false;
  if ((flags & 0x08U) && !appendU32(inner, static_cast<uint32_t>(query.time_epoch_s & 0xFFFFFFFFULL))) return false;
  if ((flags & 0x20U) && !appendU16(inner, query.cursor)) return false;
  if ((flags & 0x40U) && !appendU8(inner, query.page_size)) return false;
  if ((flags & 0x01U) && !appendStr16(inner, query.key)) return false;
  if ((flags & 0x04U) && !appendStr16(inner, query.value)) return false;

  return wrap(kDeltaMagicDesc, kDeltaKindQuery, inner.data(), inner.size(), out_payload);
}

bool DeltaBinaryProfileCodec::decodeDescriptorQuery(const uint8_t* payload,
                                                    size_t len,
                                                    DescriptorQuery& out) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kDeltaMagicDesc, kDeltaKindQuery, inner, inner_len)) {
    return parseDescriptorQuery(payload, len, out);
  }

  out = DescriptorQuery{};
  size_t off = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
  if (!readU8(inner, inner_len, off, type) || !readU8(inner, inner_len, off, flags)) return false;
  out.type = static_cast<DescriptorQueryType>(type);

  if ((flags & 0x02U) != 0) {
    if (!readU16(inner, inner_len, off, out.setting_id)) return false;
    out.has_setting_id = true;
  }
  if ((flags & 0x08U) != 0) {
    uint32_t epoch = 0;
    if (!readU32(inner, inner_len, off, epoch)) return false;
    out.time_epoch_s = epoch;
  }
  if ((flags & 0x20U) != 0) {
    if (!readU16(inner, inner_len, off, out.cursor)) return false;
  }
  if ((flags & 0x40U) != 0) {
    uint8_t page_size = 0;
    if (!readU8(inner, inner_len, off, page_size)) return false;
    out.page_size = page_size;
  }
  if ((flags & 0x10U) != 0) {
    out.paged = true;
  }
  if ((flags & 0x01U) != 0) {
    if (!readStr16(inner, inner_len, off, out.key)) return false;
  }
  if ((flags & 0x04U) != 0) {
    if (!readStr16(inner, inner_len, off, out.value)) return false;
  }

  if (off != inner_len || out.type == DescriptorQueryType::Unknown) {
    return false;
  }
  if (out.type == DescriptorQueryType::GetSetting || out.type == DescriptorQueryType::SetSetting) {
    const bool has_selector_key = !out.key.empty();
    const bool has_selector_id = out.has_setting_id;
    if (has_selector_key == has_selector_id) {
      return false;
    }
  }
  if (out.type == DescriptorQueryType::SetSetting && out.value.empty()) {
    return false;
  }
  return true;
}

bool DeltaBinaryProfileCodec::encodeDescriptorResponse(const DescriptorResponse& response,
                                                       std::vector<uint8_t>& out_payload) const {
  if (response.type == DescriptorResponseType::LogStatus ||
      response.type == DescriptorResponseType::LogChunk ||
      response.type == DescriptorResponseType::StorageInfo ||
      response.type == DescriptorResponseType::StorageList ||
      response.type == DescriptorResponseType::StorageStat ||
      response.type == DescriptorResponseType::OtaStatus ||
      response.type == DescriptorResponseType::OtaManifest ||
      response.type == DescriptorResponseType::OtaCapacity ||
      response.type == DescriptorResponseType::OtaGateInfo) {
    std::string encoded;
    if (!::espnow_link::encodeDescriptorResponse(response, encoded)) {
      return false;
    }
    out_payload.assign(encoded.begin(), encoded.end());
    return true;
  }

  std::vector<uint8_t> inner;
  uint8_t flags = 0;
  if (!response.message.empty()) flags |= 0x01;
  if (response.is_paged) flags |= 0x02;

  if (!appendU8(inner, static_cast<uint8_t>(response.type)) || !appendU8(inner, flags)) return false;
  if ((flags & 0x01U) && !appendStr16(inner, response.message)) return false;
  if ((flags & 0x02U) &&
      (!appendU32(inner, response.snapshot_id) ||
       !appendU16(inner, response.total_count) ||
       !appendU16(inner, response.cursor) ||
       !appendU16(inner, response.returned_count) ||
       !appendU16(inner, response.next_cursor) ||
       !appendU8(inner, response.done ? 1 : 0))) {
    return false;
  }

  switch (response.type) {
    case DescriptorResponseType::Device:
      if (!appendStr16(inner, response.device.device_type) || !appendStr16(inner, response.device.device_id) ||
          !appendStr16(inner, response.device.device_name) || !appendStr16(inner, response.device.hw_version) ||
          !appendStr16(inner, response.device.sw_version) || !appendStr16(inner, response.device.build_id)) {
        return false;
      }
      break;

    case DescriptorResponseType::Capabilities:
      if (response.capabilities.size() > 255U || !appendU8(inner, static_cast<uint8_t>(response.capabilities.size()))) {
        return false;
      }
      for (const auto& c : response.capabilities) {
        if (!appendStr16(inner, c.key) || !appendStr16(inner, c.description)) return false;
      }
      break;

    case DescriptorResponseType::Telemetry:
      if (response.telemetry.size() > 255U || !appendU8(inner, static_cast<uint8_t>(response.telemetry.size()))) {
        return false;
      }
      for (const auto& t : response.telemetry) {
        if (!appendU16(inner, t.metric_id) || !appendStr16(inner, t.key) || !appendStr16(inner, t.unit) ||
            !appendF32(inner, t.min_value) || !appendF32(inner, t.max_value) || !appendStr16(inner, t.description)) {
          return false;
        }
      }
      break;

    case DescriptorResponseType::TelemetrySnapshot:
      if (response.telemetry_samples.size() > 255U ||
          !appendU8(inner, static_cast<uint8_t>(response.telemetry_samples.size()))) {
        return false;
      }
      for (const auto& s : response.telemetry_samples) {
        if (!appendU16(inner, s.metric_id) || !appendStr16(inner, s.key) || !appendStr16(inner, s.unit)) return false;
        float fv = 0.0f;
        if (tryParseFloat(s.value, fv)) {
          if (!appendU8(inner, 1) || !appendF32(inner, fv)) return false;
        } else {
          if (!appendU8(inner, 0) || !appendStr16(inner, s.value)) return false;
        }
      }
      break;

    case DescriptorResponseType::Liveness:
      if (!appendU8(inner, response.liveness.online ? 1 : 0) || !appendU32(inner, response.liveness.uptime_ms) ||
          !appendStr16(inner, response.liveness.state)) {
        return false;
      }
      break;

    case DescriptorResponseType::Time:
      if (!appendU32(inner, static_cast<uint32_t>(response.time.epoch_s & 0xFFFFFFFFULL)) ||
          !appendU32(inner, response.time.uptime_ms)) {
        return false;
      }
      break;

    case DescriptorResponseType::Settings:
      if (response.settings.size() > 255U || !appendU8(inner, static_cast<uint8_t>(response.settings.size()))) {
        return false;
      }
      for (const auto& st : response.settings) {
        if (!encodeSetting(inner, st)) return false;
      }
      break;

    case DescriptorResponseType::Setting:
      if (!encodeSetting(inner, response.setting)) return false;
      break;

    case DescriptorResponseType::Ack:
    case DescriptorResponseType::Error:
    case DescriptorResponseType::Unknown:
    default:
      break;
  }

  return wrap(kDeltaMagicDesc, kDeltaKindResponse, inner.data(), inner.size(), out_payload);
}

bool DeltaBinaryProfileCodec::decodeDescriptorResponse(const uint8_t* payload,
                                                       size_t len,
                                                       DescriptorResponse& out) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kDeltaMagicDesc, kDeltaKindResponse, inner, inner_len)) {
    return ::espnow_link::decodeDescriptorResponse(payload, len, out);
  }

  out = DescriptorResponse{};
  size_t off = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
  if (!readU8(inner, inner_len, off, type) || !readU8(inner, inner_len, off, flags)) return false;
  out.type = static_cast<DescriptorResponseType>(type);

  if ((flags & 0x01U) != 0) {
    if (!readStr16(inner, inner_len, off, out.message)) return false;
  }
  if ((flags & 0x02U) != 0) {
    uint8_t done = 0;
    if (!readU32(inner, inner_len, off, out.snapshot_id) ||
        !readU16(inner, inner_len, off, out.total_count) ||
        !readU16(inner, inner_len, off, out.cursor) ||
        !readU16(inner, inner_len, off, out.returned_count) ||
        !readU16(inner, inner_len, off, out.next_cursor) ||
        !readU8(inner, inner_len, off, done)) {
      return false;
    }
    out.is_paged = true;
    out.done = (done != 0);
  }

  switch (out.type) {
    case DescriptorResponseType::Device:
      return readStr16(inner, inner_len, off, out.device.device_type) &&
             readStr16(inner, inner_len, off, out.device.device_id) &&
             readStr16(inner, inner_len, off, out.device.device_name) &&
             readStr16(inner, inner_len, off, out.device.hw_version) &&
             readStr16(inner, inner_len, off, out.device.sw_version) &&
             readStr16(inner, inner_len, off, out.device.build_id) && off == inner_len;

    case DescriptorResponseType::Capabilities: {
      uint8_t n = 0;
      if (!readU8(inner, inner_len, off, n)) return false;
      out.capabilities.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        CapabilityDescriptor c{};
        if (!readStr16(inner, inner_len, off, c.key) || !readStr16(inner, inner_len, off, c.description)) {
          return false;
        }
        out.capabilities.push_back(c);
      }
      return off == inner_len;
    }

    case DescriptorResponseType::Telemetry: {
      uint8_t n = 0;
      if (!readU8(inner, inner_len, off, n)) return false;
      out.telemetry.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        TelemetryDescriptor t{};
        if (!readU16(inner, inner_len, off, t.metric_id) || !readStr16(inner, inner_len, off, t.key) ||
            !readStr16(inner, inner_len, off, t.unit) || !readF32(inner, inner_len, off, t.min_value) ||
            !readF32(inner, inner_len, off, t.max_value) || !readStr16(inner, inner_len, off, t.description)) {
          return false;
        }
        out.telemetry.push_back(t);
      }
      return off == inner_len;
    }

    case DescriptorResponseType::TelemetrySnapshot: {
      uint8_t n = 0;
      if (!readU8(inner, inner_len, off, n)) return false;
      out.telemetry_samples.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        TelemetrySample s{};
        uint8_t numeric = 0;
        if (!readU16(inner, inner_len, off, s.metric_id) || !readStr16(inner, inner_len, off, s.key) ||
            !readStr16(inner, inner_len, off, s.unit) || !readU8(inner, inner_len, off, numeric)) {
          return false;
        }
        if (numeric != 0) {
          float fv = 0.0f;
          if (!readF32(inner, inner_len, off, fv)) return false;
          char buf[24] = {0};
          std::snprintf(buf, sizeof(buf), "%.3f", fv);
          s.value = buf;
        } else {
          if (!readStr16(inner, inner_len, off, s.value)) return false;
        }
        out.telemetry_samples.push_back(s);
      }
      return off == inner_len;
    }

    case DescriptorResponseType::Liveness: {
      uint8_t online = 0;
      if (!readU8(inner, inner_len, off, online)) return false;
      out.liveness.online = (online != 0);
      if (!readU32(inner, inner_len, off, out.liveness.uptime_ms)) return false;
      if (!readStr16(inner, inner_len, off, out.liveness.state)) return false;
      return off == inner_len;
    }

    case DescriptorResponseType::Time: {
      uint32_t epoch = 0;
      if (!readU32(inner, inner_len, off, epoch)) return false;
      out.time.epoch_s = epoch;
      if (!readU32(inner, inner_len, off, out.time.uptime_ms)) return false;
      return off == inner_len;
    }

    case DescriptorResponseType::Settings: {
      uint8_t n = 0;
      if (!readU8(inner, inner_len, off, n)) return false;
      out.settings.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        SettingDescriptor st{};
        if (!decodeSetting(inner, inner_len, off, st)) return false;
        out.settings.push_back(st);
      }
      return off == inner_len;
    }

    case DescriptorResponseType::Setting:
      return decodeSetting(inner, inner_len, off, out.setting) && off == inner_len;

    case DescriptorResponseType::Ack:
    case DescriptorResponseType::Error:
    case DescriptorResponseType::Unknown:
    default:
      return off == inner_len;
  }
}

bool DeltaBinaryProfileCodec::encodeControlCommand(uint16_t cmd_id,
                                                   std::vector<uint8_t>& out_payload) const {
  std::vector<uint8_t> inner;
  if (!appendU16(inner, cmd_id)) return false;
  return wrap(kDeltaMagicCtrl, kDeltaKindCommand, inner.data(), inner.size(), out_payload);
}

bool DeltaBinaryProfileCodec::decodeControlCommand(const uint8_t* payload,
                                                   size_t len,
                                                   uint16_t& out_cmd_id) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kDeltaMagicCtrl, kDeltaKindCommand, inner, inner_len)) {
    return parseControlCommandPayload(payload, len, out_cmd_id);
  }
  size_t off = 0;
  return readU16(inner, inner_len, off, out_cmd_id) && off == inner_len;
}

bool DeltaBinaryProfileCodec::encodeControlResult(uint16_t cmd_id,
                                                  uint16_t result_code,
                                                  std::vector<uint8_t>& out_payload) const {
  std::vector<uint8_t> inner;
  if (!appendU16(inner, cmd_id) || !appendU16(inner, result_code)) return false;
  return wrap(kDeltaMagicCtrl, kDeltaKindResult, inner.data(), inner.size(), out_payload);
}

bool DeltaBinaryProfileCodec::decodeControlResult(const uint8_t* payload,
                                                  size_t len,
                                                  uint16_t& out_cmd_id,
                                                  uint16_t& out_result_code) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kDeltaMagicCtrl, kDeltaKindResult, inner, inner_len)) {
    return parseControlResultPayload(payload, len, out_cmd_id, out_result_code);
  }
  size_t off = 0;
  return readU16(inner, inner_len, off, out_cmd_id) && readU16(inner, inner_len, off, out_result_code) &&
         off == inner_len;
}

}  // namespace espnow_link


