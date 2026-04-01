#include "espnow_link/codec.hpp"
#include "espnow_link/protocol.hpp"

#include <cstring>

namespace espnow_link {

namespace {

constexpr uint8_t kCompactMagic = 0xC1;
constexpr uint8_t kCompactKindQuery = 0x01;
constexpr uint8_t kCompactKindResponse = 0x02;
constexpr uint8_t kControlMagic = 0xC2;
constexpr uint8_t kControlKindCommand = 0x01;
constexpr uint8_t kControlKindResult = 0x02;

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
  uint8_t b[4] = {0};
  std::memcpy(b, &v, sizeof(v));
  if (!canAppend(out, 4)) return false;
  out.insert(out.end(), b, b + 4);
  return true;
}

bool appendStr8(std::vector<uint8_t>& out, const std::string& s) {
  if (s.size() > 255U) {
    return false;
  }
  if (!canAppend(out, 1 + s.size())) {
    return false;
  }
  out.push_back(static_cast<uint8_t>(s.size()));
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

bool readStr8(const uint8_t* payload, size_t len, size_t& off, std::string& out) {
  out.clear();
  uint8_t n = 0;
  if (!readU8(payload, len, off, n)) return false;
  if (off + n > len) return false;
  out.assign(reinterpret_cast<const char*>(payload + off), reinterpret_cast<const char*>(payload + off + n));
  off += n;
  return true;
}

bool encodeSetting(std::vector<uint8_t>& out, const SettingDescriptor& st) {
  return appendU16(out, st.setting_id) && appendStr8(out, st.key) &&
         appendU8(out, static_cast<uint8_t>(st.value_type)) && appendU8(out, st.writable ? 1 : 0) &&
         appendStr8(out, st.nvs_key) && appendStr8(out, st.current_value) && appendStr8(out, st.default_value) &&
         appendStr8(out, st.description);
}

bool decodeSetting(const uint8_t* payload, size_t len, size_t& off, SettingDescriptor& st) {
  uint8_t t = 0;
  if (!readU16(payload, len, off, st.setting_id)) return false;
  if (!readStr8(payload, len, off, st.key)) return false;
  if (!readU8(payload, len, off, t)) return false;
  st.value_type = static_cast<SettingValueType>(t);
  if (!readU8(payload, len, off, t)) return false;
  st.writable = (t != 0);
  if (!readStr8(payload, len, off, st.nvs_key)) return false;
  if (!readStr8(payload, len, off, st.current_value)) return false;
  if (!readStr8(payload, len, off, st.default_value)) return false;
  if (!readStr8(payload, len, off, st.description)) return false;
  return true;
}

}  // namespace

bool CompactIndexedProfileCodec::encodeDescriptorQuery(const DescriptorQuery& query,
                                                       std::vector<uint8_t>& out_payload) const {
  if (query.type == DescriptorQueryType::GetLogStatus ||
      query.type == DescriptorQueryType::ReadLogChunk ||
      query.type == DescriptorQueryType::ClearLog ||
      query.type == DescriptorQueryType::SetLogControl ||
      query.type == DescriptorQueryType::GetStorageInfo ||
      query.type == DescriptorQueryType::ListStoragePath ||
      query.type == DescriptorQueryType::StatStoragePath ||
      query.type == DescriptorQueryType::FormatStorage ||
      query.type == DescriptorQueryType::GetNodeBundle ||
      query.type == DescriptorQueryType::GetOtaStatus ||
      query.type == DescriptorQueryType::GetOtaManifest ||
      query.type == DescriptorQueryType::RebuildOtaManifest ||
      query.type == DescriptorQueryType::ClearOtaScope ||
      query.type == DescriptorQueryType::GetOtaCapacity ||
      query.type == DescriptorQueryType::GetOtaGateInfo ||
      query.type == DescriptorQueryType::ApplyOtaImage ||
      query.type == DescriptorQueryType::TopologyStageClear ||
      query.type == DescriptorQueryType::TopologyStageBegin ||
      query.type == DescriptorQueryType::TopologyStageSlotSet ||
      query.type == DescriptorQueryType::TopologyStageGroupSet ||
      query.type == DescriptorQueryType::TopologyStageFinalize ||
      query.type == DescriptorQueryType::TopologyCommit ||
      query.type == DescriptorQueryType::TopologyStatus ||
      query.type == DescriptorQueryType::TopologyTriggerSend) {
    return ::espnow_link::encodeDescriptorQuery(query, out_payload);
  }

  out_payload.clear();
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

  if (!appendU8(out_payload, kCompactMagic) || !appendU8(out_payload, kCompactKindQuery) ||
      !appendU8(out_payload, static_cast<uint8_t>(query.type)) || !appendU8(out_payload, flags)) {
    return false;
  }

  if (has_id && !appendU16(out_payload, query.setting_id)) return false;
  if (has_time && !appendU32(out_payload, static_cast<uint32_t>(query.time_epoch_s & 0xFFFFFFFFULL))) return false;
  if (has_cursor && !appendU16(out_payload, query.cursor)) return false;
  if (has_page_size && !appendU8(out_payload, query.page_size)) return false;
  if (has_key && !appendStr8(out_payload, query.key)) return false;
  if (has_value && !appendStr8(out_payload, query.value)) return false;

  return true;
}

bool CompactIndexedProfileCodec::decodeDescriptorQuery(const uint8_t* payload,
                                                       size_t len,
                                                       DescriptorQuery& out) const {
  if (payload == nullptr || len < 4 || payload[0] != kCompactMagic || payload[1] != kCompactKindQuery) {
    return parseDescriptorQuery(payload, len, out);
  }

  out = DescriptorQuery{};
  out.type = static_cast<DescriptorQueryType>(payload[2]);
  const uint8_t flags = payload[3];

  size_t off = 4;
  if ((flags & 0x02U) != 0) {
    if (!readU16(payload, len, off, out.setting_id)) return false;
    out.has_setting_id = true;
  }
  if ((flags & 0x08U) != 0) {
    uint32_t epoch = 0;
    if (!readU32(payload, len, off, epoch)) return false;
    out.time_epoch_s = epoch;
  }
  if ((flags & 0x20U) != 0) {
    if (!readU16(payload, len, off, out.cursor)) return false;
  }
  if ((flags & 0x40U) != 0) {
    if (!readU8(payload, len, off, out.page_size)) return false;
  }
  if ((flags & 0x10U) != 0) {
    out.paged = true;
  }
  if ((flags & 0x01U) != 0) {
    if (!readStr8(payload, len, off, out.key)) return false;
  }
  if ((flags & 0x04U) != 0) {
    if (!readStr8(payload, len, off, out.value)) return false;
  }
  if (off != len || out.type == DescriptorQueryType::Unknown) {
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
  // Compact query payload does not carry topology TLV fields.
  // Reject these forms so callers can fall back to the canonical TLV parser.
  if (out.type == DescriptorQueryType::TopologyStageBegin ||
      out.type == DescriptorQueryType::TopologyStageSlotSet ||
      out.type == DescriptorQueryType::TopologyStageGroupSet ||
      out.type == DescriptorQueryType::TopologyTriggerSend) {
    return false;
  }
  return true;
}

bool CompactIndexedProfileCodec::encodeDescriptorResponse(const DescriptorResponse& response,
                                                          std::vector<uint8_t>& out_payload) const {
  if (response.type == DescriptorResponseType::LogStatus ||
      response.type == DescriptorResponseType::LogChunk ||
      response.type == DescriptorResponseType::StorageInfo ||
      response.type == DescriptorResponseType::StorageList ||
      response.type == DescriptorResponseType::StorageStat ||
      response.type == DescriptorResponseType::NodeBundle ||
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

  out_payload.clear();
  const bool is_list_response =
      response.type == DescriptorResponseType::Capabilities ||
      response.type == DescriptorResponseType::Telemetry ||
      response.type == DescriptorResponseType::TelemetrySnapshot ||
      response.type == DescriptorResponseType::Settings;
  // Keep list replies dense: message text is optional metadata and often costs one extra record slot.
  const bool has_message = !response.message.empty() && !is_list_response;
  const bool has_page_meta = response.is_paged;
  uint8_t flags = has_message ? 0x01 : 0x00;
  if (has_page_meta) {
    flags |= 0x02;
  }

  if (!appendU8(out_payload, kCompactMagic) || !appendU8(out_payload, kCompactKindResponse) ||
      !appendU8(out_payload, static_cast<uint8_t>(response.type)) || !appendU8(out_payload, flags)) {
    return false;
  }

  if (has_message && !appendStr8(out_payload, response.message)) return false;
  if (has_page_meta) {
    if (!appendU32(out_payload, response.snapshot_id) ||
        !appendU16(out_payload, response.total_count) ||
        !appendU16(out_payload, response.cursor) ||
        !appendU16(out_payload, response.returned_count) ||
        !appendU16(out_payload, response.next_cursor) ||
        !appendU8(out_payload, response.done ? 1 : 0)) {
      return false;
    }
  }

  switch (response.type) {
    case DescriptorResponseType::Device:
      return appendStr8(out_payload, response.device.device_type) && appendStr8(out_payload, response.device.device_id) &&
             appendStr8(out_payload, response.device.device_name) && appendStr8(out_payload, response.device.hw_version) &&
             appendStr8(out_payload, response.device.sw_version) && appendStr8(out_payload, response.device.build_id);

    case DescriptorResponseType::Capabilities: {
      if (response.capabilities.size() > 255U) return false;
      if (!appendU8(out_payload, 0)) return false;
      const size_t count_pos = out_payload.size() - 1;
      uint8_t encoded_count = 0;
      for (const auto& c : response.capabilities) {
        const size_t mark = out_payload.size();
        if (!appendStr8(out_payload, c.key) || !appendStr8(out_payload, c.description)) {
          out_payload.resize(mark);
          break;
        }
        ++encoded_count;
      }
      if (encoded_count == 0 && !response.capabilities.empty()) return false;
      out_payload[count_pos] = encoded_count;
      return true;
    }

    case DescriptorResponseType::Telemetry: {
      if (response.telemetry.size() > 255U) return false;
      if (!appendU8(out_payload, 0)) return false;
      const size_t count_pos = out_payload.size() - 1;
      uint8_t encoded_count = 0;
      for (const auto& t : response.telemetry) {
        const size_t mark = out_payload.size();
        if (!appendU16(out_payload, t.metric_id) || !appendStr8(out_payload, t.key) || !appendStr8(out_payload, t.unit) ||
            !appendF32(out_payload, t.min_value) || !appendF32(out_payload, t.max_value) ||
            !appendStr8(out_payload, "")) {
          out_payload.resize(mark);
          break;
        }
        ++encoded_count;
      }
      if (encoded_count == 0 && !response.telemetry.empty()) return false;
      out_payload[count_pos] = encoded_count;
      return true;
    }

    case DescriptorResponseType::TelemetrySnapshot: {
      if (response.telemetry_samples.size() > 255U) return false;
      if (!appendU8(out_payload, 0)) return false;
      const size_t count_pos = out_payload.size() - 1;
      uint8_t encoded_count = 0;
      for (const auto& t : response.telemetry_samples) {
        const size_t mark = out_payload.size();
        if (!appendU16(out_payload, t.metric_id) || !appendStr8(out_payload, t.key) || !appendStr8(out_payload, t.value) ||
            !appendStr8(out_payload, t.unit)) {
          out_payload.resize(mark);
          break;
        }
        ++encoded_count;
      }
      if (encoded_count == 0 && !response.telemetry_samples.empty()) return false;
      out_payload[count_pos] = encoded_count;
      return true;
    }

    case DescriptorResponseType::Liveness:
      return appendU8(out_payload, response.liveness.online ? 1 : 0) &&
             appendU32(out_payload, response.liveness.uptime_ms) && appendStr8(out_payload, response.liveness.state);

    case DescriptorResponseType::Time:
      return appendU32(out_payload, static_cast<uint32_t>(response.time.epoch_s & 0xFFFFFFFFULL)) &&
             appendU32(out_payload, response.time.uptime_ms);

    case DescriptorResponseType::Settings: {
      if (response.settings.size() > 255U) return false;
      if (!appendU8(out_payload, 0)) return false;
      const size_t count_pos = out_payload.size() - 1;
      uint8_t encoded_count = 0;
      for (const auto& st : response.settings) {
        const size_t mark = out_payload.size();
        if (!encodeSetting(out_payload, st)) {
          out_payload.resize(mark);
          SettingDescriptor slim = st;
          slim.nvs_key.clear();
          slim.current_value.clear();
          slim.default_value.clear();
          slim.description.clear();
          if (!encodeSetting(out_payload, slim)) {
            out_payload.resize(mark);
            break;
          }
        }
        ++encoded_count;
      }
      if (encoded_count == 0 && !response.settings.empty()) return false;
      out_payload[count_pos] = encoded_count;
      return true;
    }

    case DescriptorResponseType::Setting:
      return encodeSetting(out_payload, response.setting);

    case DescriptorResponseType::Ack:
    case DescriptorResponseType::Error:
    case DescriptorResponseType::Unknown:
    default:
      return true;
  }
}

bool CompactIndexedProfileCodec::decodeDescriptorResponse(const uint8_t* payload,
                                                          size_t len,
                                                          DescriptorResponse& out) const {
  if (payload == nullptr || len < 4 || payload[0] != kCompactMagic || payload[1] != kCompactKindResponse) {
    return ::espnow_link::decodeDescriptorResponse(payload, len, out);
  }

  out = DescriptorResponse{};
  out.type = static_cast<DescriptorResponseType>(payload[2]);
  const uint8_t flags = payload[3];

  size_t off = 4;
  if ((flags & 0x01U) != 0) {
    if (!readStr8(payload, len, off, out.message)) return false;
  }
  if ((flags & 0x02U) != 0) {
    uint8_t done = 0;
    if (!readU32(payload, len, off, out.snapshot_id) ||
        !readU16(payload, len, off, out.total_count) ||
        !readU16(payload, len, off, out.cursor) ||
        !readU16(payload, len, off, out.returned_count) ||
        !readU16(payload, len, off, out.next_cursor) ||
        !readU8(payload, len, off, done)) {
      return false;
    }
    out.is_paged = true;
    out.done = (done != 0);
  }

  switch (out.type) {
    case DescriptorResponseType::Device:
      return readStr8(payload, len, off, out.device.device_type) && readStr8(payload, len, off, out.device.device_id) &&
             readStr8(payload, len, off, out.device.device_name) && readStr8(payload, len, off, out.device.hw_version) &&
             readStr8(payload, len, off, out.device.sw_version) && readStr8(payload, len, off, out.device.build_id) &&
             off == len;

    case DescriptorResponseType::Capabilities: {
      uint8_t n = 0;
      if (!readU8(payload, len, off, n)) return false;
      out.capabilities.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        CapabilityDescriptor c{};
        if (!readStr8(payload, len, off, c.key) || !readStr8(payload, len, off, c.description)) return false;
        out.capabilities.push_back(c);
      }
      return off == len;
    }

    case DescriptorResponseType::Telemetry: {
      uint8_t n = 0;
      if (!readU8(payload, len, off, n)) return false;
      out.telemetry.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        TelemetryDescriptor t{};
        if (!readU16(payload, len, off, t.metric_id) || !readStr8(payload, len, off, t.key) ||
            !readStr8(payload, len, off, t.unit) || !readF32(payload, len, off, t.min_value) ||
            !readF32(payload, len, off, t.max_value) || !readStr8(payload, len, off, t.description)) {
          return false;
        }
        out.telemetry.push_back(t);
      }
      return off == len;
    }

    case DescriptorResponseType::TelemetrySnapshot: {
      uint8_t n = 0;
      if (!readU8(payload, len, off, n)) return false;
      out.telemetry_samples.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        TelemetrySample t{};
        if (!readU16(payload, len, off, t.metric_id) || !readStr8(payload, len, off, t.key) ||
            !readStr8(payload, len, off, t.value) || !readStr8(payload, len, off, t.unit)) {
          return false;
        }
        out.telemetry_samples.push_back(t);
      }
      return off == len;
    }

    case DescriptorResponseType::Liveness: {
      uint8_t online = 0;
      if (!readU8(payload, len, off, online)) return false;
      out.liveness.online = (online != 0);
      if (!readU32(payload, len, off, out.liveness.uptime_ms)) return false;
      if (!readStr8(payload, len, off, out.liveness.state)) return false;
      return off == len;
    }

    case DescriptorResponseType::Time: {
      uint32_t epoch = 0;
      if (!readU32(payload, len, off, epoch)) return false;
      out.time.epoch_s = epoch;
      if (!readU32(payload, len, off, out.time.uptime_ms)) return false;
      return off == len;
    }

    case DescriptorResponseType::Settings: {
      uint8_t n = 0;
      if (!readU8(payload, len, off, n)) return false;
      out.settings.reserve(n);
      for (uint8_t i = 0; i < n; ++i) {
        SettingDescriptor s{};
        if (!decodeSetting(payload, len, off, s)) return false;
        out.settings.push_back(s);
      }
      return off == len;
    }

    case DescriptorResponseType::Setting:
      return decodeSetting(payload, len, off, out.setting) && off == len;

    case DescriptorResponseType::Ack:
    case DescriptorResponseType::Error:
    case DescriptorResponseType::Unknown:
    default:
      return off == len;
  }
}

bool CompactIndexedProfileCodec::encodeControlCommand(uint16_t cmd_id,
                                                      std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  return appendU8(out_payload, kControlMagic) && appendU8(out_payload, kControlKindCommand) &&
         appendU16(out_payload, cmd_id);
}

bool CompactIndexedProfileCodec::decodeControlCommand(const uint8_t* payload,
                                                      size_t len,
                                                      uint16_t& out_cmd_id) const {
  if (payload != nullptr && len == 4 && payload[0] == kControlMagic && payload[1] == kControlKindCommand) {
    out_cmd_id = static_cast<uint16_t>(payload[2]) | (static_cast<uint16_t>(payload[3]) << 8);
    return true;
  }
  return parseControlCommandPayload(payload, len, out_cmd_id);
}

bool CompactIndexedProfileCodec::encodeControlResult(uint16_t cmd_id,
                                                     uint16_t result_code,
                                                     std::vector<uint8_t>& out_payload) const {
  out_payload.clear();
  return appendU8(out_payload, kControlMagic) && appendU8(out_payload, kControlKindResult) &&
         appendU16(out_payload, cmd_id) && appendU16(out_payload, result_code);
}

bool CompactIndexedProfileCodec::decodeControlResult(const uint8_t* payload,
                                                     size_t len,
                                                     uint16_t& out_cmd_id,
                                                     uint16_t& out_result_code) const {
  if (payload != nullptr && len == 6 && payload[0] == kControlMagic && payload[1] == kControlKindResult) {
    out_cmd_id = static_cast<uint16_t>(payload[2]) | (static_cast<uint16_t>(payload[3]) << 8);
    out_result_code = static_cast<uint16_t>(payload[4]) | (static_cast<uint16_t>(payload[5]) << 8);
    return true;
  }
  return parseControlResultPayload(payload, len, out_cmd_id, out_result_code);
}

}  // namespace espnow_link

