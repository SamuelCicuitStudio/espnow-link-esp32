#include "espnow_link/codec.hpp"
#include "espnow_link/protocol.hpp"

#include <cstring>

namespace espnow_link {

namespace {

constexpr uint8_t kVarintMagicDesc = 0xE1;
constexpr uint8_t kVarintMagicCtrl = 0xE2;
constexpr uint8_t kVarintVersion = 1;
constexpr uint8_t kVarintKindQuery = 1;
constexpr uint8_t kVarintKindResponse = 2;
constexpr uint8_t kVarintKindCommand = 1;
constexpr uint8_t kVarintKindResult = 2;

bool canAppend(const std::vector<uint8_t>& out, size_t n) {
  return (out.size() + n) <= ProtocolCodec::kMaxPayload;
}

bool appendU8(std::vector<uint8_t>& out, uint8_t v) {
  if (!canAppend(out, 1)) return false;
  out.push_back(v);
  return true;
}

bool appendVarU32(std::vector<uint8_t>& out, uint32_t value) {
  size_t need = 1;
  uint32_t t = value;
  while (t >= 0x80U) {
    ++need;
    t >>= 7;
  }
  if (!canAppend(out, need)) return false;

  while (value >= 0x80U) {
    out.push_back(static_cast<uint8_t>((value & 0x7FU) | 0x80U));
    value >>= 7;
  }
  out.push_back(static_cast<uint8_t>(value & 0x7FU));
  return true;
}


bool appendF32(std::vector<uint8_t>& out, float value) {
  if (!canAppend(out, 4)) return false;
  uint8_t b[4] = {0};
  std::memcpy(b, &value, sizeof(value));
  out.insert(out.end(), b, b + 4);
  return true;
}

bool appendStr(std::vector<uint8_t>& out, const std::string& s) {
  if (!appendVarU32(out, static_cast<uint32_t>(s.size()))) return false;
  if (!canAppend(out, s.size())) return false;
  out.insert(out.end(), s.begin(), s.end());
  return true;
}

bool readU8(const uint8_t* payload, size_t len, size_t& off, uint8_t& out) {
  if (payload == nullptr || off + 1 > len) return false;
  out = payload[off++];
  return true;
}

bool readVarU32(const uint8_t* payload, size_t len, size_t& off, uint32_t& out) {
  out = 0;
  uint32_t shift = 0;
  for (uint8_t i = 0; i < 5; ++i) {
    uint8_t b = 0;
    if (!readU8(payload, len, off, b)) return false;
    out |= static_cast<uint32_t>(b & 0x7FU) << shift;
    if ((b & 0x80U) == 0) {
      return true;
    }
    shift += 7;
  }
  return false;
}

bool readF32(const uint8_t* payload, size_t len, size_t& off, float& out) {
  if (payload == nullptr || off + 4 > len) return false;
  std::memcpy(&out, payload + off, sizeof(out));
  off += 4;
  return true;
}

bool readStr(const uint8_t* payload, size_t len, size_t& off, std::string& out) {
  out.clear();
  uint32_t n = 0;
  if (!readVarU32(payload, len, off, n)) return false;
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
  if (inner == nullptr && inner_len > 0) return false;
  out_payload.clear();
  if (!appendU8(out_payload, magic) || !appendU8(out_payload, kVarintVersion) || !appendU8(out_payload, kind)) {
    return false;
  }
  if (!appendVarU32(out_payload, static_cast<uint32_t>(inner_len))) {
    return false;
  }
  if (!canAppend(out_payload, inner_len)) return false;
  if (inner_len > 0) out_payload.insert(out_payload.end(), inner, inner + inner_len);
  return true;
}

bool unwrap(const uint8_t* payload,
            size_t len,
            uint8_t expected_magic,
            uint8_t expected_kind,
            const uint8_t*& out_inner,
            size_t& out_inner_len) {
  if (payload == nullptr || len < 4) return false;
  if (payload[0] != expected_magic || payload[1] != kVarintVersion || payload[2] != expected_kind) return false;

  size_t off = 3;
  uint32_t inner_len32 = 0;
  if (!readVarU32(payload, len, off, inner_len32)) return false;
  const size_t inner_len = static_cast<size_t>(inner_len32);
  if (off + inner_len != len) return false;

  out_inner = payload + off;
  out_inner_len = inner_len;
  return true;
}

bool encodeSetting(std::vector<uint8_t>& out, const SettingDescriptor& s) {
  return appendVarU32(out, s.setting_id) && appendStr(out, s.key) &&
         appendVarU32(out, static_cast<uint32_t>(s.value_type)) && appendU8(out, s.writable ? 1 : 0) &&
         appendStr(out, s.nvs_key) && appendStr(out, s.current_value) && appendStr(out, s.default_value) &&
         appendStr(out, s.description);
}

bool decodeSetting(const uint8_t* payload, size_t len, size_t& off, SettingDescriptor& s) {
  uint32_t v = 0;
  uint8_t b = 0;
  if (!readVarU32(payload, len, off, v)) return false;
  s.setting_id = static_cast<uint16_t>(v & 0xFFFFU);
  if (!readStr(payload, len, off, s.key)) return false;
  if (!readVarU32(payload, len, off, v)) return false;
  s.value_type = static_cast<SettingValueType>(v & 0xFFU);
  if (!readU8(payload, len, off, b)) return false;
  s.writable = (b != 0);
  if (!readStr(payload, len, off, s.nvs_key)) return false;
  if (!readStr(payload, len, off, s.current_value)) return false;
  if (!readStr(payload, len, off, s.default_value)) return false;
  if (!readStr(payload, len, off, s.description)) return false;
  return true;
}

}  // namespace

bool VarintTlvProfileCodec::encodeDescriptorQuery(const DescriptorQuery& query,
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
  const bool has_time = query.time_epoch_s != 0;
  const bool has_paged = query.paged;
  const bool has_cursor = has_paged;
  const bool has_page_size = has_paged;

  if (query.type == DescriptorQueryType::GetSetting || query.type == DescriptorQueryType::SetSetting) {
    if (has_key == has_id) {
      return false;
    }
  }

  uint8_t flags = 0;
  if (has_key) flags |= 0x01;
  if (has_id) flags |= 0x02;
  if (has_value) flags |= 0x04;
  if (has_time) flags |= 0x08;
  if (has_paged) flags |= 0x10;
  if (has_cursor) flags |= 0x20;
  if (has_page_size) flags |= 0x40;

  if (!appendU8(inner, static_cast<uint8_t>(query.type)) || !appendU8(inner, flags)) return false;
  if (has_id && !appendVarU32(inner, query.setting_id)) return false;
  if (has_time && !appendVarU32(inner, static_cast<uint32_t>(query.time_epoch_s & 0xFFFFFFFFULL))) return false;
  if (has_cursor && !appendVarU32(inner, query.cursor)) return false;
  if (has_page_size && !appendVarU32(inner, query.page_size)) return false;
  if (has_key && !appendStr(inner, query.key)) return false;
  if (has_value && !appendStr(inner, query.value)) return false;

  return wrap(kVarintMagicDesc, kVarintKindQuery, inner.data(), inner.size(), out_payload);
}

bool VarintTlvProfileCodec::decodeDescriptorQuery(const uint8_t* payload,
                                                  size_t len,
                                                  DescriptorQuery& out) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kVarintMagicDesc, kVarintKindQuery, inner, inner_len)) {
    return parseDescriptorQuery(payload, len, out);
  }

  out = DescriptorQuery{};
  size_t off = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
  if (!readU8(inner, inner_len, off, type) || !readU8(inner, inner_len, off, flags)) return false;
  out.type = static_cast<DescriptorQueryType>(type);

  if ((flags & 0x02U) != 0) {
    uint32_t sid = 0;
    if (!readVarU32(inner, inner_len, off, sid)) return false;
    out.has_setting_id = true;
    out.setting_id = static_cast<uint16_t>(sid & 0xFFFFU);
  }
  if ((flags & 0x08U) != 0) {
    uint32_t epoch = 0;
    if (!readVarU32(inner, inner_len, off, epoch)) return false;
    out.time_epoch_s = epoch;
  }
  if ((flags & 0x20U) != 0) {
    uint32_t cursor = 0;
    if (!readVarU32(inner, inner_len, off, cursor)) return false;
    out.cursor = static_cast<uint16_t>(cursor & 0xFFFFU);
  }
  if ((flags & 0x40U) != 0) {
    uint32_t page_size = 0;
    if (!readVarU32(inner, inner_len, off, page_size)) return false;
    out.page_size = static_cast<uint8_t>(page_size & 0xFFU);
  }
  if ((flags & 0x10U) != 0) {
    out.paged = true;
  }
  if ((flags & 0x01U) != 0) {
    if (!readStr(inner, inner_len, off, out.key)) return false;
  }
  if ((flags & 0x04U) != 0) {
    if (!readStr(inner, inner_len, off, out.value)) return false;
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

bool VarintTlvProfileCodec::encodeDescriptorResponse(const DescriptorResponse& response,
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
  const bool has_msg = !response.message.empty();
  const bool has_page_meta = response.is_paged;
  uint8_t flags = has_msg ? 0x01 : 0x00;
  if (has_page_meta) flags |= 0x02;

  if (!appendU8(inner, static_cast<uint8_t>(response.type)) || !appendU8(inner, flags)) return false;
  if (has_msg && !appendStr(inner, response.message)) return false;
  if (has_page_meta) {
    if (!appendVarU32(inner, response.snapshot_id) ||
        !appendVarU32(inner, response.total_count) ||
        !appendVarU32(inner, response.cursor) ||
        !appendVarU32(inner, response.returned_count) ||
        !appendVarU32(inner, response.next_cursor) ||
        !appendU8(inner, response.done ? 1 : 0)) {
      return false;
    }
  }

  switch (response.type) {
    case DescriptorResponseType::Device:
      if (!appendStr(inner, response.device.device_type) || !appendStr(inner, response.device.device_id) ||
          !appendStr(inner, response.device.device_name) || !appendStr(inner, response.device.hw_version) ||
          !appendStr(inner, response.device.sw_version) || !appendStr(inner, response.device.build_id)) {
        return false;
      }
      break;

    case DescriptorResponseType::Capabilities:
      if (!appendVarU32(inner, static_cast<uint32_t>(response.capabilities.size()))) return false;
      for (const auto& c : response.capabilities) {
        if (!appendStr(inner, c.key) || !appendStr(inner, c.description)) return false;
      }
      break;

    case DescriptorResponseType::Telemetry:
      if (!appendVarU32(inner, static_cast<uint32_t>(response.telemetry.size()))) return false;
      for (const auto& t : response.telemetry) {
        if (!appendVarU32(inner, t.metric_id) || !appendStr(inner, t.key) || !appendStr(inner, t.unit) ||
            !appendF32(inner, t.min_value) || !appendF32(inner, t.max_value) || !appendStr(inner, t.description)) {
          return false;
        }
      }
      break;

    case DescriptorResponseType::TelemetrySnapshot:
      if (!appendVarU32(inner, static_cast<uint32_t>(response.telemetry_samples.size()))) return false;
      for (const auto& t : response.telemetry_samples) {
        if (!appendVarU32(inner, t.metric_id) || !appendStr(inner, t.key) || !appendStr(inner, t.value) ||
            !appendStr(inner, t.unit)) {
          return false;
        }
      }
      break;

    case DescriptorResponseType::Liveness:
      if (!appendU8(inner, response.liveness.online ? 1 : 0) ||
          !appendVarU32(inner, response.liveness.uptime_ms) || !appendStr(inner, response.liveness.state)) {
        return false;
      }
      break;

    case DescriptorResponseType::Time:
      if (!appendVarU32(inner, static_cast<uint32_t>(response.time.epoch_s & 0xFFFFFFFFULL)) ||
          !appendVarU32(inner, response.time.uptime_ms)) {
        return false;
      }
      break;

    case DescriptorResponseType::Settings:
      if (!appendVarU32(inner, static_cast<uint32_t>(response.settings.size()))) return false;
      for (const auto& s : response.settings) {
        if (!encodeSetting(inner, s)) return false;
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

  return wrap(kVarintMagicDesc, kVarintKindResponse, inner.data(), inner.size(), out_payload);
}

bool VarintTlvProfileCodec::decodeDescriptorResponse(const uint8_t* payload,
                                                     size_t len,
                                                     DescriptorResponse& out) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kVarintMagicDesc, kVarintKindResponse, inner, inner_len)) {
    return ::espnow_link::decodeDescriptorResponse(payload, len, out);
  }

  out = DescriptorResponse{};
  size_t off = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
  if (!readU8(inner, inner_len, off, type) || !readU8(inner, inner_len, off, flags)) return false;
  out.type = static_cast<DescriptorResponseType>(type);

  if ((flags & 0x01U) != 0) {
    if (!readStr(inner, inner_len, off, out.message)) return false;
  }
  if ((flags & 0x02U) != 0) {
    uint32_t snap = 0;
    uint32_t total = 0;
    uint32_t cursor = 0;
    uint32_t returned = 0;
    uint32_t next = 0;
    uint8_t done = 0;
    if (!readVarU32(inner, inner_len, off, snap) ||
        !readVarU32(inner, inner_len, off, total) ||
        !readVarU32(inner, inner_len, off, cursor) ||
        !readVarU32(inner, inner_len, off, returned) ||
        !readVarU32(inner, inner_len, off, next) ||
        !readU8(inner, inner_len, off, done)) {
      return false;
    }
    out.is_paged = true;
    out.snapshot_id = snap;
    out.total_count = static_cast<uint16_t>(total & 0xFFFFU);
    out.cursor = static_cast<uint16_t>(cursor & 0xFFFFU);
    out.returned_count = static_cast<uint16_t>(returned & 0xFFFFU);
    out.next_cursor = static_cast<uint16_t>(next & 0xFFFFU);
    out.done = (done != 0);
  }

  switch (out.type) {
    case DescriptorResponseType::Device:
      return readStr(inner, inner_len, off, out.device.device_type) && readStr(inner, inner_len, off, out.device.device_id) &&
             readStr(inner, inner_len, off, out.device.device_name) && readStr(inner, inner_len, off, out.device.hw_version) &&
             readStr(inner, inner_len, off, out.device.sw_version) && readStr(inner, inner_len, off, out.device.build_id) &&
             off == inner_len;

    case DescriptorResponseType::Capabilities: {
      uint32_t n = 0;
      if (!readVarU32(inner, inner_len, off, n)) return false;
      out.capabilities.reserve(n);
      for (uint32_t i = 0; i < n; ++i) {
        CapabilityDescriptor c{};
        if (!readStr(inner, inner_len, off, c.key) || !readStr(inner, inner_len, off, c.description)) return false;
        out.capabilities.push_back(c);
      }
      return off == inner_len;
    }

    case DescriptorResponseType::Telemetry: {
      uint32_t n = 0;
      if (!readVarU32(inner, inner_len, off, n)) return false;
      out.telemetry.reserve(n);
      for (uint32_t i = 0; i < n; ++i) {
        TelemetryDescriptor t{};
        uint32_t metric = 0;
        if (!readVarU32(inner, inner_len, off, metric) || !readStr(inner, inner_len, off, t.key) ||
            !readStr(inner, inner_len, off, t.unit) || !readF32(inner, inner_len, off, t.min_value) ||
            !readF32(inner, inner_len, off, t.max_value) || !readStr(inner, inner_len, off, t.description)) {
          return false;
        }
        t.metric_id = static_cast<uint16_t>(metric & 0xFFFFU);
        out.telemetry.push_back(t);
      }
      return off == inner_len;
    }

    case DescriptorResponseType::TelemetrySnapshot: {
      uint32_t n = 0;
      if (!readVarU32(inner, inner_len, off, n)) return false;
      out.telemetry_samples.reserve(n);
      for (uint32_t i = 0; i < n; ++i) {
        TelemetrySample t{};
        uint32_t metric = 0;
        if (!readVarU32(inner, inner_len, off, metric) || !readStr(inner, inner_len, off, t.key) ||
            !readStr(inner, inner_len, off, t.value) || !readStr(inner, inner_len, off, t.unit)) {
          return false;
        }
        t.metric_id = static_cast<uint16_t>(metric & 0xFFFFU);
        out.telemetry_samples.push_back(t);
      }
      return off == inner_len;
    }

    case DescriptorResponseType::Liveness: {
      uint8_t online = 0;
      if (!readU8(inner, inner_len, off, online)) return false;
      out.liveness.online = (online != 0);
      if (!readVarU32(inner, inner_len, off, out.liveness.uptime_ms)) return false;
      if (!readStr(inner, inner_len, off, out.liveness.state)) return false;
      return off == inner_len;
    }

    case DescriptorResponseType::Time: {
      uint32_t epoch = 0;
      if (!readVarU32(inner, inner_len, off, epoch)) return false;
      out.time.epoch_s = epoch;
      if (!readVarU32(inner, inner_len, off, out.time.uptime_ms)) return false;
      return off == inner_len;
    }

    case DescriptorResponseType::Settings: {
      uint32_t n = 0;
      if (!readVarU32(inner, inner_len, off, n)) return false;
      out.settings.reserve(n);
      for (uint32_t i = 0; i < n; ++i) {
        SettingDescriptor s{};
        if (!decodeSetting(inner, inner_len, off, s)) return false;
        out.settings.push_back(s);
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

bool VarintTlvProfileCodec::encodeControlCommand(uint16_t cmd_id,
                                                 std::vector<uint8_t>& out_payload) const {
  std::vector<uint8_t> inner;
  if (!appendVarU32(inner, cmd_id)) return false;
  return wrap(kVarintMagicCtrl, kVarintKindCommand, inner.data(), inner.size(), out_payload);
}

bool VarintTlvProfileCodec::decodeControlCommand(const uint8_t* payload,
                                                 size_t len,
                                                 uint16_t& out_cmd_id) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kVarintMagicCtrl, kVarintKindCommand, inner, inner_len)) {
    return parseControlCommandPayload(payload, len, out_cmd_id);
  }

  size_t off = 0;
  uint32_t cmd = 0;
  if (!readVarU32(inner, inner_len, off, cmd) || off != inner_len) return false;
  out_cmd_id = static_cast<uint16_t>(cmd & 0xFFFFU);
  return true;
}

bool VarintTlvProfileCodec::encodeControlResult(uint16_t cmd_id,
                                                uint16_t result_code,
                                                std::vector<uint8_t>& out_payload) const {
  std::vector<uint8_t> inner;
  if (!appendVarU32(inner, cmd_id) || !appendVarU32(inner, result_code)) return false;
  return wrap(kVarintMagicCtrl, kVarintKindResult, inner.data(), inner.size(), out_payload);
}

bool VarintTlvProfileCodec::decodeControlResult(const uint8_t* payload,
                                                size_t len,
                                                uint16_t& out_cmd_id,
                                                uint16_t& out_result_code) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kVarintMagicCtrl, kVarintKindResult, inner, inner_len)) {
    return parseControlResultPayload(payload, len, out_cmd_id, out_result_code);
  }

  size_t off = 0;
  uint32_t cmd = 0;
  uint32_t code = 0;
  if (!readVarU32(inner, inner_len, off, cmd) || !readVarU32(inner, inner_len, off, code) || off != inner_len) {
    return false;
  }
  out_cmd_id = static_cast<uint16_t>(cmd & 0xFFFFU);
  out_result_code = static_cast<uint16_t>(code & 0xFFFFU);
  return true;
}

}  // namespace espnow_link



