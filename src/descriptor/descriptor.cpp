#include "espnow_link/profile.hpp"
#include "espnow_link/descriptor.hpp"
#include "espnow_link/management_types.hpp"
#include "espnow_link/library_logger.hpp"
#include "espnow_link/protocol.hpp"
#include "descriptor_cache.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace espnow_link {
namespace {

constexpr uint8_t kSchemaVersion = 1;
constexpr uint8_t kTopologyMaxSlots = kManagementTopologyMaxSlots;
constexpr uint8_t kTopologyMaxGroups = kManagementTopologyMaxGroups;

bool appendTlv(std::vector<uint8_t>& out, uint8_t tag, uint8_t type, const uint8_t* value, uint16_t len) {
  out.push_back(tag);
  out.push_back(type);
  out.push_back(static_cast<uint8_t>(len & 0xFF));
  out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  if (len > 0) {
    if (value == nullptr) {
      return false;
    }
    out.insert(out.end(), value, value + len);
  }
  return true;
}

bool appendU8(std::vector<uint8_t>& out, uint8_t tag, uint8_t v) {
  return appendTlv(out, tag, 0x01, &v, 1);
}

bool appendBool(std::vector<uint8_t>& out, uint8_t tag, bool v) {
  const uint8_t b = v ? 1 : 0;
  return appendTlv(out, tag, 0x06, &b, 1);
}

bool appendU32(std::vector<uint8_t>& out, uint8_t tag, uint32_t v) {
  uint8_t b[4] = {
      static_cast<uint8_t>(v & 0xFF),
      static_cast<uint8_t>((v >> 8) & 0xFF),
      static_cast<uint8_t>((v >> 16) & 0xFF),
      static_cast<uint8_t>((v >> 24) & 0xFF),
  };
  return appendTlv(out, tag, 0x03, b, 4);
}

bool appendU16(std::vector<uint8_t>& out, uint8_t tag, uint16_t v) {
  uint8_t b[2] = {
      static_cast<uint8_t>(v & 0xFFU),
      static_cast<uint8_t>((v >> 8) & 0xFFU),
  };
  return appendTlv(out, tag, 0x02, b, 2);
}

bool appendF32(std::vector<uint8_t>& out, uint8_t tag, float v) {
  uint8_t b[4] = {0};
  std::memcpy(b, &v, sizeof(v));
  return appendTlv(out, tag, 0x07, b, 4);
}

bool appendUtf8(std::vector<uint8_t>& out, uint8_t tag, const std::string& s) {
  if (s.size() > 65535U) {
    return false;
  }
  return appendTlv(out, tag, 0x09, reinterpret_cast<const uint8_t*>(s.data()), static_cast<uint16_t>(s.size()));
}


std::vector<TelemetrySample> alignSamplesToProfile(const IProfileDefinition* profile,
                                                   const std::vector<TelemetrySample>& provider_samples) {
  if (profile == nullptr) {
    return provider_samples;
  }

  std::vector<TelemetrySample> out;
  out.reserve(profile->telemetryMetrics().size());
  for (const auto& metric : profile->telemetryMetrics()) {
    if (metric.key == nullptr || metric.key[0] == '\0') {
      continue;
    }
    for (const auto& sample : provider_samples) {
      if (sample.key != metric.key) {
        continue;
      }

      TelemetrySample aligned = sample;
      aligned.metric_id = metric.metric_id;
      if (aligned.key.empty()) {
        aligned.key = metric.key;
      }
      out.push_back(aligned);
      break;
    }
  }
  return out;
}
void appendCapability(std::vector<CapabilityDescriptor>& out,
                      const char* key,
                      const char* description) {
  if (key == nullptr || key[0] == '\0' || description == nullptr) {
    return;
  }
  CapabilityDescriptor c{};
  c.key = key;
  c.description = description;
  out.push_back(c);
}

void appendProfileTopologyCapabilities(const IProfileDefinition* profile,
                                       std::vector<CapabilityDescriptor>& out) {
  if (profile == nullptr) {
    return;
  }

  switch (profile->profileId()) {
    case kProfileSens:
    case kProfileSemu:
      appendCapability(out, "topology_orchestrated", "1");
      appendCapability(out, "peer_pair_initiator", "1");
      appendCapability(out, "allowed_peer_profiles", "RELAY,REMU");
      appendCapability(out, "topology_seed_required", "1");
      appendCapability(out, "uplink_path", "sensor_to_relay_time_stamped");
      break;

    case kProfileRelay:
    case kProfileRemu:
      appendCapability(out, "topology_orchestrated", "1");
      appendCapability(out, "peer_pair_acceptor", "1");
      appendCapability(out, "allowed_peer_profiles", "SENS,SEMU");
      appendCapability(out, "topology_seed_required", "1");
      appendCapability(out, "accepts_time_stamped_uplink", "1");
      break;

    default:
      break;
  }
}
bool readU32(const uint8_t* v, uint16_t len, uint32_t& out) {
  if (v == nullptr || len != 4) {
    return false;
  }
  out = static_cast<uint32_t>(v[0]) |
        (static_cast<uint32_t>(v[1]) << 8) |
        (static_cast<uint32_t>(v[2]) << 16) |
        (static_cast<uint32_t>(v[3]) << 24);
  return true;
}

bool readF32(const uint8_t* v, uint16_t len, float& out) {
  if (v == nullptr || len != 4) {
    return false;
  }
  std::memcpy(&out, v, sizeof(out));
  return true;
}

bool readU16(const uint8_t* v, uint16_t len, uint16_t& out) {
  if (v == nullptr || len != 2U) {
    return false;
  }
  out = static_cast<uint16_t>(v[0]) |
        (static_cast<uint16_t>(v[1]) << 8);
  return true;
}

bool supportsPaging(DescriptorQueryType type) {
  return type == DescriptorQueryType::GetCapabilities ||
         type == DescriptorQueryType::GetTelemetry ||
         type == DescriptorQueryType::GetSettings ||
         type == DescriptorQueryType::GetOtaManifest;
}

uint16_t effectivePageSize(const DescriptorQuery& query) {
  static constexpr uint16_t kDefaultPage = 6;
  static constexpr uint16_t kMaxPage = 16;
  if (!query.paged || !supportsPaging(query.type)) {
    return 0;
  }
  uint16_t ps = (query.page_size == 0) ? kDefaultPage : query.page_size;
  if (ps == 0) {
    ps = kDefaultPage;
  }
  if (ps > kMaxPage) {
    ps = kMaxPage;
  }
  return ps;
}

uint32_t fnv1aInit() {
  return 2166136261u;
}

void fnv1aMixByte(uint32_t& h, uint8_t b) {
  h ^= b;
  h *= 16777619u;
}

void fnv1aMixU16(uint32_t& h, uint16_t v) {
  fnv1aMixByte(h, static_cast<uint8_t>(v & 0xFF));
  fnv1aMixByte(h, static_cast<uint8_t>((v >> 8) & 0xFF));
}

void fnv1aMixU32(uint32_t& h, uint32_t v) {
  fnv1aMixByte(h, static_cast<uint8_t>(v & 0xFFU));
  fnv1aMixByte(h, static_cast<uint8_t>((v >> 8) & 0xFFU));
  fnv1aMixByte(h, static_cast<uint8_t>((v >> 16) & 0xFFU));
  fnv1aMixByte(h, static_cast<uint8_t>((v >> 24) & 0xFFU));
}

void fnv1aMixStr(uint32_t& h, const std::string& s) {
  for (char c : s) {
    fnv1aMixByte(h, static_cast<uint8_t>(c));
  }
  fnv1aMixByte(h, 0x00);
}

std::string clipForPage(const std::string& v, size_t max_len) {
  if (v.size() <= max_len) {
    return v;
  }
  return v.substr(0, max_len);
}

void finalizePageMeta(const DescriptorQuery& query,
                      size_t total_count,
                      size_t returned_count,
                      uint32_t snapshot_id,
                      DescriptorResponse& out) {
  const size_t start = std::min<size_t>(query.cursor, total_count);
  const size_t next = start + returned_count;
  out.is_paged = true;
  out.snapshot_id = snapshot_id;
  out.total_count = static_cast<uint16_t>(std::min<size_t>(total_count, 0xFFFFU));
  out.cursor = static_cast<uint16_t>(std::min<size_t>(start, 0xFFFFU));
  out.returned_count = static_cast<uint16_t>(std::min<size_t>(returned_count, 0xFFFFU));
  out.next_cursor = static_cast<uint16_t>(std::min<size_t>(next, total_count));
  out.done = (next >= total_count);
}

uint32_t snapshotCapabilities(const std::vector<CapabilityDescriptor>& list, const std::string& message) {
  uint32_t h = fnv1aInit();
  fnv1aMixByte(h, static_cast<uint8_t>(DescriptorResponseType::Capabilities));
  fnv1aMixStr(h, message);
  for (const auto& item : list) {
    fnv1aMixStr(h, item.key);
    fnv1aMixStr(h, item.description);
  }
  return h;
}

uint32_t snapshotTelemetry(const std::vector<TelemetryDescriptor>& list, const std::string& message) {
  uint32_t h = fnv1aInit();
  fnv1aMixByte(h, static_cast<uint8_t>(DescriptorResponseType::Telemetry));
  fnv1aMixStr(h, message);
  for (const auto& item : list) {
    fnv1aMixU16(h, item.metric_id);
    fnv1aMixStr(h, item.key);
    fnv1aMixStr(h, item.unit);
    fnv1aMixStr(h, item.description);
  }
  return h;
}

uint32_t snapshotSettings(const std::vector<SettingDescriptor>& list, const std::string& message) {
  uint32_t h = fnv1aInit();
  fnv1aMixByte(h, static_cast<uint8_t>(DescriptorResponseType::Settings));
  fnv1aMixStr(h, message);
  for (const auto& item : list) {
    fnv1aMixU16(h, item.setting_id);
    fnv1aMixStr(h, item.key);
    fnv1aMixByte(h, static_cast<uint8_t>(item.value_type));
    fnv1aMixByte(h, item.writable ? 1U : 0U);
    fnv1aMixStr(h, item.current_value);
    fnv1aMixStr(h, item.default_value);
    fnv1aMixStr(h, item.description);
  }
  return h;
}

uint32_t snapshotOtaManifest(const std::vector<OtaManifestEntry>& list, const std::string& message) {
  uint32_t h = fnv1aInit();
  fnv1aMixByte(h, static_cast<uint8_t>(DescriptorResponseType::OtaManifest));
  fnv1aMixStr(h, message);
  for (const auto& item : list) {
    fnv1aMixU32(h, item.file_id);
    fnv1aMixStr(h, item.file_name);
    fnv1aMixU32(h, item.size_bytes);
    fnv1aMixU32(h, item.crc32);
    fnv1aMixStr(h, item.version);
    fnv1aMixStr(h, item.build_id);
    fnv1aMixU32(h, item.created_epoch_s);
    fnv1aMixStr(h, item.state);
    fnv1aMixU32(h, item.required_app_bytes);
  }
  return h;
}

}  // namespace

bool encodeDescriptorQuery(const DescriptorQuery& query, std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  if (!appendU8(out_payload, 0x01, kSchemaVersion)) {
    return false;
  }
  if (!appendU8(out_payload, 0x10, static_cast<uint8_t>(query.type))) {
    return false;
  }

  if (query.type == DescriptorQueryType::GetSetting || query.type == DescriptorQueryType::SetSetting) {
    const bool has_setting_id = query.has_setting_id;
    const bool has_key = !query.key.empty();
    if (has_setting_id == has_key) {
      return false;
    }

    if (has_setting_id) {
      const uint8_t b[2] = {
          static_cast<uint8_t>(query.setting_id & 0xFF),
          static_cast<uint8_t>((query.setting_id >> 8) & 0xFF),
      };
      if (!appendTlv(out_payload, 0x14, 0x02, b, 2)) {
        return false;
      }
    } else {
      if (!appendUtf8(out_payload, 0x11, query.key)) {
        return false;
      }
    }
  }
  if (query.type == DescriptorQueryType::SetSetting) {
    if (!appendUtf8(out_payload, 0x12, query.value)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::SetTime) {
    if (!appendU32(out_payload, 0x13, static_cast<uint32_t>(query.time_epoch_s & 0xFFFFFFFFULL))) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::ReadLogChunk) {
    if (!appendU32(out_payload, 0x30, query.log_offset)) {
      return false;
    }
    const uint8_t max_le[2] = {
        static_cast<uint8_t>(query.log_max_bytes & 0xFF),
        static_cast<uint8_t>((query.log_max_bytes >> 8) & 0xFF),
    };
    if (!appendTlv(out_payload, 0x31, 0x02, max_le, 2)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::SetLogControl) {
    if (!appendBool(out_payload, 0x32, query.log_enable)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::ListStoragePath || query.type == DescriptorQueryType::StatStoragePath) {
    if (!query.storage_path.empty() && !appendUtf8(out_payload, 0x33, query.storage_path)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::ClearOtaScope && !query.ota_scope.empty()) {
    if (!appendUtf8(out_payload, 0x34, query.ota_scope)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::ApplyOtaImage && !query.ota_target.empty()) {
    if (!appendUtf8(out_payload, 0x35, query.ota_target)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::TopologyStageBegin) {
    if (!appendU8(out_payload, 0x40, query.topology_schema_version) ||
        !appendU32(out_payload, 0x41, query.topology_version) ||
        !appendU8(out_payload, 0x42, query.topology_index_neg) ||
        !appendU8(out_payload, 0x43, query.topology_index_pos)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::TopologyStageSlotSet) {
    if (!appendU8(out_payload, 0x44, query.topology_slot_index) ||
        !appendBool(out_payload, 0x45, query.topology_slot_enabled) ||
        !appendTlv(out_payload, 0x46, 0x08, query.topology_peer.data(), 6) ||
        !appendU8(out_payload, 0x47, query.topology_peer_role) ||
        !appendU8(out_payload, 0x48, query.topology_group_id) ||
        !appendU8(out_payload, 0x49, static_cast<uint8_t>(query.topology_relative_index)) ||
        !appendU8(out_payload, 0x4A, query.topology_local_virtual_index) ||
        !appendU8(out_payload, 0x4B, query.topology_peer_virtual_index) ||
        !appendU8(out_payload, 0x4C, static_cast<uint8_t>(query.topology_axis_order)) ||
        !appendU16(out_payload, 0x4D, query.topology_delay_ms) ||
        !appendU16(out_payload, 0x4E, query.topology_hold_ms)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::TopologyStageGroupSet) {
    if (!appendU8(out_payload, 0x50, query.topology_group_slot) ||
        !appendBool(out_payload, 0x51, query.topology_group_enabled) ||
        !appendU8(out_payload, 0x52, query.topology_group_id)) {
      return false;
    }
    if (query.topology_group_enabled &&
        !appendTlv(out_payload, 0x53, 0x08, query.topology_group_seed.data(), 32U)) {
      return false;
    }
  }
  if (query.type == DescriptorQueryType::TopologyTriggerSend) {
    if (!appendU8(out_payload, 0x54, static_cast<uint8_t>(query.topology_target_index)) ||
        !appendU8(out_payload, 0x55, query.topology_trigger_direction) ||
        !appendU16(out_payload, 0x56, query.topology_delay_ms) ||
        !appendU16(out_payload, 0x57, query.topology_hold_ms) ||
        !appendU8(out_payload, 0x58, query.topology_source_virtual_index)) {
      return false;
    }
  }
  if (query.paged && supportsPaging(query.type)) {
    if (!appendBool(out_payload, 0x15, true)) {
      return false;
    }
    const uint8_t cursor_le[2] = {
        static_cast<uint8_t>(query.cursor & 0xFF),
        static_cast<uint8_t>((query.cursor >> 8) & 0xFF),
    };
    if (!appendTlv(out_payload, 0x16, 0x02, cursor_le, 2)) {
      return false;
    }
    if (!appendU8(out_payload, 0x17, query.page_size)) {
      return false;
    }
  }

  return true;
}

bool parseDescriptorQuery(const uint8_t* payload, size_t len, DescriptorQuery& out) {
  out = DescriptorQuery{};
  if (payload == nullptr || len < 8) {
    return false;
  }

  bool have_type = false;
  size_t off = 0;
  while (off + 4 <= len) {
    const uint8_t tag = payload[off + 0];
    const uint8_t type = payload[off + 1];
    const uint16_t tlv_len = static_cast<uint16_t>(payload[off + 2]) |
                             (static_cast<uint16_t>(payload[off + 3]) << 8);
    off += 4;
    if ((off + tlv_len) > len) {
      return false;
    }

    const uint8_t* v = payload + off;
    if (tag == 0x10 && type == 0x01 && tlv_len == 1) {
      out.type = static_cast<DescriptorQueryType>(v[0]);
      have_type = true;
    } else if (tag == 0x11 && type == 0x09) {
      out.key.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x12 && type == 0x09) {
      out.value.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x13 && type == 0x03 && tlv_len == 4) {
      uint32_t epoch = 0;
      if (!readU32(v, tlv_len, epoch)) {
        return false;
      }
      out.time_epoch_s = epoch;
    } else if (tag == 0x14 && type == 0x02 && tlv_len == 2) {
      out.setting_id = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
      out.has_setting_id = true;
    } else if (tag == 0x15 && type == 0x06 && tlv_len == 1) {
      out.paged = (v[0] != 0);
    } else if (tag == 0x16 && type == 0x02 && tlv_len == 2) {
      out.cursor = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x17 && type == 0x01 && tlv_len == 1) {
      out.page_size = v[0];
    } else if (tag == 0x30 && type == 0x03 && tlv_len == 4) {
      uint32_t offset = 0;
      if (!readU32(v, tlv_len, offset)) {
        return false;
      }
      out.log_offset = offset;
    } else if (tag == 0x31 && type == 0x02 && tlv_len == 2) {
      out.log_max_bytes = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x32 && type == 0x06 && tlv_len == 1) {
      out.has_log_enable = true;
      out.log_enable = (v[0] != 0);
    } else if (tag == 0x33 && type == 0x09) {
      out.storage_path.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x34 && type == 0x09) {
      out.ota_scope.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x35 && type == 0x09) {
      out.ota_target.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x40 && type == 0x01 && tlv_len == 1) {
      out.topology_schema_version = v[0];
    } else if (tag == 0x41 && type == 0x03 && tlv_len == 4) {
      uint32_t topo_version = 0;
      if (!readU32(v, tlv_len, topo_version)) {
        return false;
      }
      out.topology_version = topo_version;
    } else if (tag == 0x42 && type == 0x01 && tlv_len == 1) {
      out.topology_index_neg = v[0];
    } else if (tag == 0x43 && type == 0x01 && tlv_len == 1) {
      out.topology_index_pos = v[0];
    } else if (tag == 0x44 && type == 0x01 && tlv_len == 1) {
      out.topology_slot_index = v[0];
    } else if (tag == 0x45 && type == 0x06 && tlv_len == 1) {
      out.topology_slot_enabled = (v[0] != 0U);
    } else if (tag == 0x46 && type == 0x08 && tlv_len == 6) {
      std::memcpy(out.topology_peer.data(), v, 6U);
    } else if (tag == 0x47 && type == 0x01 && tlv_len == 1) {
      out.topology_peer_role = v[0];
    } else if (tag == 0x48 && type == 0x01 && tlv_len == 1) {
      out.topology_group_id = v[0];
    } else if (tag == 0x49 && type == 0x01 && tlv_len == 1) {
      out.topology_relative_index = static_cast<int8_t>(v[0]);
    } else if (tag == 0x4A && type == 0x01 && tlv_len == 1) {
      out.topology_local_virtual_index = v[0];
    } else if (tag == 0x4B && type == 0x01 && tlv_len == 1) {
      out.topology_peer_virtual_index = v[0];
    } else if (tag == 0x4C && type == 0x01 && tlv_len == 1) {
      out.topology_axis_order = static_cast<int8_t>(v[0]);
    } else if (tag == 0x4D && type == 0x02 && tlv_len == 2) {
      uint16_t delay = 0;
      if (!readU16(v, tlv_len, delay)) {
        return false;
      }
      out.topology_delay_ms = delay;
    } else if (tag == 0x4E && type == 0x02 && tlv_len == 2) {
      uint16_t hold = 0;
      if (!readU16(v, tlv_len, hold)) {
        return false;
      }
      out.topology_hold_ms = hold;
    } else if (tag == 0x50 && type == 0x01 && tlv_len == 1) {
      out.topology_group_slot = v[0];
    } else if (tag == 0x51 && type == 0x06 && tlv_len == 1) {
      out.topology_group_enabled = (v[0] != 0U);
    } else if (tag == 0x52 && type == 0x01 && tlv_len == 1) {
      out.topology_group_id = v[0];
    } else if (tag == 0x53 && type == 0x08 && tlv_len == 32) {
      std::memcpy(out.topology_group_seed.data(), v, 32U);
    } else if (tag == 0x54 && type == 0x01 && tlv_len == 1) {
      out.topology_target_index = static_cast<int8_t>(v[0]);
    } else if (tag == 0x55 && type == 0x01 && tlv_len == 1) {
      out.topology_trigger_direction = v[0];
    } else if (tag == 0x56 && type == 0x02 && tlv_len == 2) {
      uint16_t delay = 0;
      if (!readU16(v, tlv_len, delay)) {
        return false;
      }
      out.topology_delay_ms = delay;
    } else if (tag == 0x57 && type == 0x02 && tlv_len == 2) {
      uint16_t hold = 0;
      if (!readU16(v, tlv_len, hold)) {
        return false;
      }
      out.topology_hold_ms = hold;
    } else if (tag == 0x58 && type == 0x01 && tlv_len == 1) {
      out.topology_source_virtual_index = v[0];
    }

    off += tlv_len;
  }

  if (!have_type) {
    return false;
  }

  if (out.type == DescriptorQueryType::GetSetting || out.type == DescriptorQueryType::SetSetting) {
    const bool has_setting_id = out.has_setting_id;
    const bool has_key = !out.key.empty();
    if (has_setting_id == has_key) {
      return false;
    }
  }
  if (out.type == DescriptorQueryType::SetSetting && out.value.empty()) {
    return false;
  }
  if (out.type == DescriptorQueryType::SetTime && out.time_epoch_s == 0) {
    return false;
  }
  if (out.type == DescriptorQueryType::ReadLogChunk && out.log_max_bytes == 0) {
    return false;
  }
  if (out.type == DescriptorQueryType::SetLogControl && !out.has_log_enable) {
    return false;
  }
  if (out.type == DescriptorQueryType::ClearOtaScope && out.ota_scope.empty()) {
    return false;
  }
  if (out.type == DescriptorQueryType::ApplyOtaImage && out.ota_target.empty()) {
    return false;
  }
  if (out.type == DescriptorQueryType::TopologyStageBegin && out.topology_version == 0U) {
    return false;
  }
  if (out.type == DescriptorQueryType::TopologyStageSlotSet) {
    if (out.topology_slot_index >= kTopologyMaxSlots) {
      return false;
    }
    if (out.topology_slot_enabled) {
      if (out.topology_peer_role == 0U ||
          out.topology_relative_index == 0 ||
          out.topology_group_id == 0U) {
        return false;
      }
    }
  }
  if (out.type == DescriptorQueryType::TopologyStageGroupSet) {
    if (out.topology_group_slot >= kTopologyMaxGroups) {
      return false;
    }
    if (out.topology_group_enabled && out.topology_group_id == 0U) {
      return false;
    }
  }
  if (out.type == DescriptorQueryType::TopologyTriggerSend) {
    if (out.topology_target_index == 0 ||
        out.topology_target_index < -static_cast<int8_t>(kTopologyMaxSlots) ||
        out.topology_target_index > static_cast<int8_t>(kTopologyMaxSlots)) {
      return false;
    }
    if (out.topology_trigger_direction != 1U && out.topology_trigger_direction != 2U) {
      return false;
    }
    if (out.topology_source_virtual_index != 0xFFU &&
        out.topology_source_virtual_index > 0x0FU) {
      return false;
    }
  }

  return out.type != DescriptorQueryType::Unknown;
}

bool handleDescriptorQuery(IDescriptorProvider& provider,
                           const DescriptorQuery& query,
                           DescriptorResponse& out,
                           const IProfileDefinition* profile,
                           LibraryLogger* logger) {
  out = DescriptorResponse{};

  auto backfillSettingId = [&](SettingDescriptor& st) {
    if (st.setting_id != 0 || profile == nullptr || st.key.empty()) {
      return;
    }
    const ProfileSettingSpec* spec = findProfileSettingByKey(profile, st.key);
    if (spec != nullptr) {
      st.setting_id = spec->setting_id;
    }
  };

  auto backfillMetricId = [&](TelemetryDescriptor& td) {
    if (td.metric_id != 0 || profile == nullptr || td.key.empty()) {
      return;
    }
    const ProfileTelemetryMetricSpec* spec = findProfileTelemetryByKey(profile, td.key);
    if (spec != nullptr) {
      td.metric_id = spec->metric_id;
    }
  };

  auto backfillSampleMetricId = [&](TelemetrySample& s) {
    if (s.metric_id != 0 || profile == nullptr || s.key.empty()) {
      return;
    }
    const ProfileTelemetryMetricSpec* spec = findProfileTelemetryByKey(profile, s.key);
    if (spec != nullptr) {
      s.metric_id = spec->metric_id;
    }
  };

  switch (query.type) {
    case DescriptorQueryType::GetDevice:
      out.type = provider.getDeviceDescriptor(out.device) ? DescriptorResponseType::Device : DescriptorResponseType::Error;
      out.message = (out.type == DescriptorResponseType::Error) ? "device descriptor unavailable" : "";
      return true;

    case DescriptorQueryType::GetCapabilities: {
      std::vector<CapabilityDescriptor> full_caps;
      std::vector<CapabilityDescriptor> provider_caps;
      const bool have_provider_caps = provider.getCapabilities(provider_caps);
      if (profile != nullptr) {
        CapabilityDescriptor c{};
        c.key = "profile";
        c.description = profile->profileName() != nullptr ? profile->profileName() : "UNKNOWN";
        full_caps.push_back(c);

        c = CapabilityDescriptor{};
        c.key = "telemetry_count";
        c.description = std::to_string(static_cast<unsigned long>(profile->telemetryMetrics().size()));
        full_caps.push_back(c);

        c = CapabilityDescriptor{};
        c.key = "profile_id";
        c.description = std::to_string(static_cast<unsigned long>(profile->profileId()));
        full_caps.push_back(c);

        c = CapabilityDescriptor{};
        c.key = "settings_count";
        c.description = std::to_string(static_cast<unsigned long>(profile->settings().size()));
        full_caps.push_back(c);

        appendProfileTopologyCapabilities(profile, full_caps);

        c = CapabilityDescriptor{};
        c.key = "events_count";
        c.description = std::to_string(static_cast<unsigned long>(profile->events().size()));
        full_caps.push_back(c);

        for (const auto& pc : provider_caps) {
          full_caps.push_back(pc);
        }
        out.type = DescriptorResponseType::Capabilities;
        out.message = have_provider_caps ? "profile-primary+provider" : "profile-primary";
      } else {
        full_caps = provider_caps;
        out.type = have_provider_caps ? DescriptorResponseType::Capabilities : DescriptorResponseType::Error;
        out.message = have_provider_caps ? "" : "capabilities unavailable";
      }

      if (out.type != DescriptorResponseType::Capabilities) {
        return true;
      }

      const uint16_t page_size = effectivePageSize(query);
      if (page_size == 0) {
        out.capabilities = std::move(full_caps);
        return true;
      }

      const size_t total = full_caps.size();
      const size_t start = std::min<size_t>(query.cursor, total);
      const size_t end = std::min<size_t>(total, start + page_size);
      out.capabilities.assign(full_caps.begin() + static_cast<std::ptrdiff_t>(start),
                              full_caps.begin() + static_cast<std::ptrdiff_t>(end));
      for (auto& cap : out.capabilities) {
        cap.key = clipForPage(cap.key, 32);
        cap.description = clipForPage(cap.description, 56);
      }
      finalizePageMeta(query, total, end - start, snapshotCapabilities(full_caps, out.message), out);
      return true;
    }

    case DescriptorQueryType::GetTelemetry: {
      std::vector<TelemetryDescriptor> provider_schema;
      const bool have_provider_schema = provider.getTelemetrySchema(provider_schema);
      std::vector<TelemetryDescriptor> full_telem;

      if (profile != nullptr) {
        full_telem = descriptor_cache::telemetrySchemaForProfile(profile);

        for (auto& t : full_telem) {
          for (const auto& ps : provider_schema) {
            if (ps.key == t.key) {
              t.unit = ps.unit;
              t.min_value = ps.min_value;
              t.max_value = ps.max_value;
              if (!ps.description.empty()) {
                t.description = ps.description;
              }
              break;
            }
          }
        }

        out.type = full_telem.empty() ? DescriptorResponseType::Error : DescriptorResponseType::Telemetry;
        out.message = full_telem.empty()
                          ? "telemetry schema unavailable"
                          : (have_provider_schema ? "profile-primary+provider" : "profile-primary");
      } else {
        full_telem = provider_schema;
        out.type = have_provider_schema ? DescriptorResponseType::Telemetry : DescriptorResponseType::Error;
        out.message = have_provider_schema ? "" : "telemetry schema unavailable";
        if (out.type == DescriptorResponseType::Telemetry) {
          for (auto& t : full_telem) {
            backfillMetricId(t);
          }
        }
      }

      if (out.type != DescriptorResponseType::Telemetry) {
        return true;
      }

      const uint16_t page_size = effectivePageSize(query);
      if (page_size == 0) {
        out.telemetry = std::move(full_telem);
        return true;
      }

      const size_t total = full_telem.size();
      const size_t start = std::min<size_t>(query.cursor, total);
      const size_t end = std::min<size_t>(total, start + page_size);
      out.telemetry.assign(full_telem.begin() + static_cast<std::ptrdiff_t>(start),
                           full_telem.begin() + static_cast<std::ptrdiff_t>(end));
      for (auto& t : out.telemetry) {
        t.key = clipForPage(t.key, 32);
        t.unit = clipForPage(t.unit, 8);
        t.description = clipForPage(t.description, 56);
      }
      finalizePageMeta(query, total, end - start, snapshotTelemetry(full_telem, out.message), out);
      return true;
    }

    case DescriptorQueryType::PullTelemetry: {
      std::vector<TelemetrySample> provider_samples;
      if (!provider.getTelemetrySnapshot(provider_samples)) {
        out.type = DescriptorResponseType::Error;
        out.message = "telemetry pull unavailable";
        return true;
      }

      out.telemetry_samples = alignSamplesToProfile(profile, provider_samples);
      if (profile == nullptr) {
        for (auto& s : out.telemetry_samples) {
          backfillSampleMetricId(s);
        }
      }

      out.type = DescriptorResponseType::TelemetrySnapshot;
      out.message.clear();
      return true;
    }

    case DescriptorQueryType::GetLiveness:
      out.type = provider.getLiveness(out.liveness) ? DescriptorResponseType::Liveness : DescriptorResponseType::Error;
      out.message = (out.type == DescriptorResponseType::Error) ? "liveness unavailable" : "";
      return true;

    case DescriptorQueryType::GetTime:
      out.type = provider.getTime(out.time) ? DescriptorResponseType::Time : DescriptorResponseType::Error;
      out.message = (out.type == DescriptorResponseType::Error) ? "time unavailable" : "";
      return true;

    case DescriptorQueryType::SetTime:
      out.type = provider.setTime(query.time_epoch_s, out.message) ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
      out.message = (out.type == DescriptorResponseType::Ack && out.message.empty()) ? "time updated" : out.message;
      return true;

    case DescriptorQueryType::GetSettings: {
      std::vector<SettingDescriptor> full_settings;

      if (profile != nullptr) {
        full_settings = descriptor_cache::settingsSchemaForProfile(profile);
        bool have_provider_settings = false;

        for (auto& st : full_settings) {
          const uint16_t schema_setting_id = st.setting_id;
          const std::string schema_setting_key = st.key;
          SettingDescriptor resolved{};
          if (provider.getSettingById(schema_setting_id, resolved)) {
            st = resolved;
            have_provider_settings = true;
          }
          st.setting_id = schema_setting_id;
          st.key = schema_setting_key;
        }

        out.type = full_settings.empty() ? DescriptorResponseType::Error : DescriptorResponseType::Settings;
        out.message = full_settings.empty()
                          ? "settings unavailable"
                          : (have_provider_settings ? "profile-primary+provider" : "profile-primary");
      } else {
        std::vector<SettingDescriptor> provider_settings;
        const bool have_provider_settings = provider.getSettings(provider_settings);
        full_settings = provider_settings;
        out.type = have_provider_settings ? DescriptorResponseType::Settings : DescriptorResponseType::Error;
        out.message = have_provider_settings ? "" : "settings unavailable";
        if (out.type == DescriptorResponseType::Settings) {
          for (auto& st : full_settings) {
            backfillSettingId(st);
          }
        }
      }

      if (out.type != DescriptorResponseType::Settings) {
        return true;
      }

      const uint16_t page_size = effectivePageSize(query);
      if (page_size == 0) {
        out.settings = std::move(full_settings);
        return true;
      }

      const size_t total = full_settings.size();
      const size_t start = std::min<size_t>(query.cursor, total);
      const size_t end = std::min<size_t>(total, start + page_size);
      out.settings.assign(full_settings.begin() + static_cast<std::ptrdiff_t>(start),
                          full_settings.begin() + static_cast<std::ptrdiff_t>(end));
      for (auto& st : out.settings) {
        st.key = clipForPage(st.key, 32);
        st.nvs_key = clipForPage(st.nvs_key, 20);
        st.current_value = clipForPage(st.current_value, 40);
        st.default_value = clipForPage(st.default_value, 40);
        st.description = clipForPage(st.description, 56);
      }
      finalizePageMeta(query, total, end - start, snapshotSettings(full_settings, out.message), out);
      return true;
    }

    case DescriptorQueryType::GetSetting: {
      if (query.has_setting_id) {
        if (!provider.getSettingById(query.setting_id, out.setting)) {
          out.type = DescriptorResponseType::Error;
          out.message = "setting id not found";
          return true;
        }
        out.type = DescriptorResponseType::Setting;
        out.message.clear();
        if (out.setting.setting_id == 0) {
          out.setting.setting_id = query.setting_id;
        }
        if (out.setting.key.empty()) {
          if (profile != nullptr) {
            const ProfileSettingSpec* spec = findProfileSettingById(profile, query.setting_id);
            if (spec != nullptr && spec->key != nullptr) {
              out.setting.key = spec->key;
            }
          }
        }
        backfillSettingId(out.setting);
        return true;
      }

      if (query.key.empty()) {
        out.type = DescriptorResponseType::Error;
        out.message = "setting key missing";
        return true;
      }

      if (!provider.getSetting(query.key, out.setting)) {
        out.type = DescriptorResponseType::Error;
        out.message = "setting not found";
        return true;
      }

      out.type = DescriptorResponseType::Setting;
      out.message.clear();
      if (out.setting.key.empty()) {
        out.setting.key = query.key;
      }
      backfillSettingId(out.setting);
      return true;
    }

    case DescriptorQueryType::SetSetting: {
      if (query.has_setting_id) {
        out.type = provider.setSettingById(query.setting_id, query.value, out.message)
                       ? DescriptorResponseType::Ack
                       : DescriptorResponseType::Error;
        if (out.type == DescriptorResponseType::Ack && out.message.empty()) {
          char idbuf[24]{};
          std::snprintf(idbuf, sizeof(idbuf), "setting[0x%04X] updated", static_cast<unsigned>(query.setting_id));
          out.message = idbuf;
        }
        if (out.type == DescriptorResponseType::Error && out.message.empty()) {
          char idbuf[34]{};
          std::snprintf(idbuf, sizeof(idbuf), "setting[0x%04X] write unavailable", static_cast<unsigned>(query.setting_id));
          out.message = idbuf;
        }
        return true;
      }

      if (query.key.empty()) {
        out.type = DescriptorResponseType::Error;
        out.message = "setting key missing";
        return true;
      }

      out.type = provider.setSetting(query.key, query.value, out.message) ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
      if (out.type == DescriptorResponseType::Ack && out.message.empty()) {
        out.message = query.key.empty() ? "setting updated" : (query.key + " updated");
      }
      if (out.type == DescriptorResponseType::Error && out.message.empty() && profile != nullptr) {
        const ProfileSettingSpec* spec = findProfileSettingByKey(profile, query.key);
        if (spec != nullptr) {
          out.message = query.key.empty() ? "setting write unavailable" : (query.key + " write unavailable");
        }
      }
      return true;
    }

    case DescriptorQueryType::GetLogStatus: {
      if (logger == nullptr) {
        out.type = DescriptorResponseType::Error;
        out.message = "logger unavailable";
        return true;
      }
      LogStorageStats stats{};
      const bool ok = logger->stats(stats);
      out.type = DescriptorResponseType::LogStatus;
      out.logger_available = ok && stats.available;
      out.logger_enabled = logger->enabled();
      out.logger_min_level = static_cast<uint8_t>(logger->minLevel());
      out.log_bytes_used = stats.bytes_used;
      out.log_bytes_dropped = stats.bytes_dropped;
      out.log_records_appended = stats.records_appended;
      out.log_rotations = stats.rotations;
      out.log_total_size = stats.bytes_used;
      out.message = out.logger_available ? "logger ready" : "logger storage unavailable";
      return true;
    }

    case DescriptorQueryType::ReadLogChunk: {
      if (logger == nullptr) {
        out.type = DescriptorResponseType::Error;
        out.message = "logger unavailable";
        return true;
      }

      uint16_t max_bytes = query.log_max_bytes;
      if (max_bytes == 0) {
        max_bytes = 96;
      }
      if (max_bytes > 128) {
        max_bytes = 128;
      }

      std::vector<uint8_t> chunk(max_bytes);
      size_t out_len = 0;
      uint32_t total_size = 0;
      if (!logger->readChunk(query.log_offset, chunk.data(), chunk.size(), out_len, total_size)) {
        out.type = DescriptorResponseType::Error;
        out.message = "logger read failed";
        return true;
      }

      out.type = DescriptorResponseType::LogChunk;
      out.log_chunk_offset = query.log_offset;
      out.log_total_size = total_size;
      out.log_chunk.assign(chunk.begin(), chunk.begin() + out_len);
      out.message = (out_len == 0) ? "empty" : "ok";
      return true;
    }

    case DescriptorQueryType::ClearLog: {
      if (logger == nullptr) {
        out.type = DescriptorResponseType::Error;
        out.message = "logger unavailable";
        return true;
      }
      std::string policy_message;
      if (!provider.authorizeLoggerClear(policy_message)) {
        out.type = DescriptorResponseType::Error;
        out.message = policy_message.empty() ? "logger clear denied" : policy_message;
        return true;
      }
      const bool ok = logger->clear();
      out.type = ok ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
      out.message = ok ? "logger cleared" : "logger clear failed";
      return true;
    }

    case DescriptorQueryType::SetLogControl: {
      if (logger == nullptr) {
        out.type = DescriptorResponseType::Error;
        out.message = "logger unavailable";
        return true;
      }
      if (!query.has_log_enable) {
        out.type = DescriptorResponseType::Error;
        out.message = "logger control missing enable";
        return true;
      }
      std::string policy_message;
      if (!provider.authorizeLoggerSetEnabled(query.log_enable, policy_message)) {
        out.type = DescriptorResponseType::Error;
        out.message = policy_message.empty() ? "logger control denied" : policy_message;
        return true;
      }
      logger->setEnabled(query.log_enable);
      out.type = DescriptorResponseType::Ack;
      out.message = query.log_enable ? "logger enabled" : "logger disabled";
      return true;
    }

    case DescriptorQueryType::GetStorageInfo: {
      std::string msg;
      if (!provider.getStorageInfo(out.storage_info, msg)) {
        out.type = DescriptorResponseType::Error;
        out.message = msg.empty() ? "storage info unavailable" : msg;
        return true;
      }
      out.type = DescriptorResponseType::StorageInfo;
      out.message = msg;
      return true;
    }

    case DescriptorQueryType::ListStoragePath: {
      const std::string path = query.storage_path.empty() ? "/" : query.storage_path;
      std::vector<StorageEntry> entries;
      std::string canonical;
      std::string parent;
      std::string msg;
      if (!provider.listStoragePath(path, canonical, parent, entries, msg)) {
        out.type = DescriptorResponseType::Error;
        out.message = msg.empty() ? "storage list failed" : msg;
        return true;
      }
      out.type = DescriptorResponseType::StorageList;
      out.storage_path = canonical.empty() ? path : canonical;
      out.storage_parent_path = parent;
      out.storage_entries = std::move(entries);
      out.message = msg;
      return true;
    }

    case DescriptorQueryType::StatStoragePath: {
      const std::string path = query.storage_path.empty() ? "/" : query.storage_path;
      StorageStat st{};
      std::string msg;
      if (!provider.statStoragePath(path, st, msg)) {
        out.type = DescriptorResponseType::Error;
        out.message = msg.empty() ? "storage stat failed" : msg;
        return true;
      }
      out.type = DescriptorResponseType::StorageStat;
      out.storage_stat = st;
      out.storage_path = st.path;
      out.message = msg;
      return true;
    }

    case DescriptorQueryType::FormatStorage: {
      bool restore_logger = false;
      bool logger_enabled_before = false;
      if (logger != nullptr) {
        logger_enabled_before = logger->enabled();
        if (logger_enabled_before) {
          logger->setEnabled(false);
          restore_logger = true;
        }
      }

      const bool ok = provider.formatStorage(out.message);

      if (restore_logger && logger != nullptr) {
        logger->setEnabled(logger_enabled_before);
      }

      out.type = ok ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
      if (out.type == DescriptorResponseType::Ack && out.message.empty()) {
        out.message = "storage formatted";
      }
      return true;
    }

    case DescriptorQueryType::GetOtaStatus: {
      std::string msg;
      if (!provider.getOtaStatus(out.ota_status, msg)) {
        out.type = DescriptorResponseType::Error;
        out.message = msg.empty() ? "ota status unavailable" : msg;
        return true;
      }
      out.type = DescriptorResponseType::OtaStatus;
      out.message = msg;
      return true;
    }

    case DescriptorQueryType::GetOtaManifest: {
      std::vector<OtaManifestEntry> full_manifest;
      std::string msg;
      if (!provider.getOtaManifest(full_manifest, msg)) {
        out.type = DescriptorResponseType::Error;
        out.message = msg.empty() ? "ota manifest unavailable" : msg;
        return true;
      }

      if (query.paged && supportsPaging(query.type)) {
        const uint16_t page_size = effectivePageSize(query);
        if (page_size == 0) {
          out.type = DescriptorResponseType::Error;
          out.message = "invalid page size";
          return true;
        }
        const size_t total = full_manifest.size();
        const size_t start = std::min<size_t>(query.cursor, total);
        const size_t end = std::min<size_t>(total, start + page_size);

        out.type = DescriptorResponseType::OtaManifest;
        out.ota_manifest.assign(full_manifest.begin() + static_cast<std::ptrdiff_t>(start),
                                full_manifest.begin() + static_cast<std::ptrdiff_t>(end));
        finalizePageMeta(query,
                         total,
                         end - start,
                         snapshotOtaManifest(full_manifest, msg),
                         out);
        out.message = msg;
        return true;
      }

      out.type = DescriptorResponseType::OtaManifest;
      out.ota_manifest = std::move(full_manifest);
      out.message = msg;
      return true;
    }

    case DescriptorQueryType::RebuildOtaManifest: {
      out.type = provider.rebuildOtaManifest(out.message) ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
      if (out.type == DescriptorResponseType::Ack && out.message.empty()) {
        out.message = "ota manifest rebuilt";
      }
      return true;
    }

    case DescriptorQueryType::ClearOtaScope: {
      out.type = provider.clearOtaScope(query.ota_scope, out.message) ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
      if (out.type == DescriptorResponseType::Ack && out.message.empty()) {
        out.message = "ota scope cleared";
      }
      return true;
    }

    case DescriptorQueryType::GetOtaCapacity: {
      std::string msg;
      if (!provider.getOtaCapacity(out.ota_capacity, msg)) {
        out.type = DescriptorResponseType::Error;
        out.message = msg.empty() ? "ota capacity unavailable" : msg;
        return true;
      }
      out.type = DescriptorResponseType::OtaCapacity;
      out.message = msg;
      return true;
    }

    case DescriptorQueryType::GetOtaGateInfo: {
      std::string msg;
      if (!provider.getOtaGateInfo(out.ota_gate, msg)) {
        out.type = DescriptorResponseType::Error;
        out.message = msg.empty() ? "ota gate unavailable" : msg;
        return true;
      }
      out.type = DescriptorResponseType::OtaGateInfo;
      out.message = msg;
      return true;
    }

    case DescriptorQueryType::ApplyOtaImage: {
      out.type = provider.applyOtaImage(query.ota_target, out.message) ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
      if (out.type == DescriptorResponseType::Ack && out.message.empty()) {
        out.message = "ota apply requested";
      }
      return true;
    }

    default:
      out.type = DescriptorResponseType::Error;
      out.message = "unsupported descriptor query";
      return true;
  }
}

bool encodeDescriptorResponse(const DescriptorResponse& response, std::string& out_payload) {
  std::vector<uint8_t> out;
  bool truncated = false;
  uint16_t paged_emitted_count = 0;
  uint16_t paged_returned_count = response.returned_count;
  uint16_t paged_next_cursor = response.next_cursor;
  bool paged_done = response.done;

  static constexpr size_t kPagedMetaReserveBytes = 42;
  bool reserve_paged_meta = response.is_paged;

  auto canFit = [&](size_t bytes) {
    const size_t reserve = reserve_paged_meta ? kPagedMetaReserveBytes : 0U;
    return (out.size() + bytes + reserve) <= ProtocolCodec::kMaxPayload;
  };

  auto tryAppendU8 = [&](uint8_t tag, uint8_t v) {
    return canFit(5) && appendU8(out, tag, v);
  };
  auto tryAppendBool = [&](uint8_t tag, bool v) {
    return canFit(5) && appendBool(out, tag, v);
  };
  auto tryAppendU32 = [&](uint8_t tag, uint32_t v) {
    return canFit(8) && appendU32(out, tag, v);
  };
  auto tryAppendF32 = [&](uint8_t tag, float v) {
    return canFit(8) && appendF32(out, tag, v);
  };
  auto tryAppendUtf8 = [&](uint8_t tag, const std::string& v) {
    return canFit(4 + v.size()) && appendUtf8(out, tag, v);
  };

  if (!tryAppendU8(0x01, kSchemaVersion)) {
    return false;
  }
  if (!tryAppendU8(0x10, static_cast<uint8_t>(response.type))) {
    return false;
  }
  if (!response.message.empty() && !tryAppendUtf8(0x11, response.message)) {
    return false;
  }

  if (response.type == DescriptorResponseType::Device) {
    if (!tryAppendUtf8(0x20, response.device.device_type) || !tryAppendUtf8(0x21, response.device.device_id) ||
        !tryAppendUtf8(0x22, response.device.device_name) || !tryAppendUtf8(0x23, response.device.hw_version) ||
        !tryAppendUtf8(0x24, response.device.sw_version) || !tryAppendUtf8(0x25, response.device.build_id)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::Capabilities) {
    for (const auto& c : response.capabilities) {
      const size_t need = 4 + c.key.size() + 4 + c.description.size();
      if (!canFit(need)) {
        truncated = true;
        break;
      }
      if (!appendUtf8(out, 0x30, c.key) || !appendUtf8(out, 0x31, c.description)) {
        return false;
      }
      ++paged_emitted_count;
    }
  } else if (response.type == DescriptorResponseType::Telemetry) {
    for (const auto& t : response.telemetry) {
      const size_t base_need = 6 + (4 + t.key.size()) + (4 + t.unit.size());
      if (!canFit(base_need)) {
        truncated = true;
        break;
      }
      if (!appendTlv(out, 0x3F, 0x02, reinterpret_cast<const uint8_t*>(&t.metric_id), 2) ||
          !appendUtf8(out, 0x40, t.key) || !appendUtf8(out, 0x41, t.unit)) {
        return false;
      }
      ++paged_emitted_count;

      const bool has_minmax = !(t.min_value == 0.0f && t.max_value == 0.0f);
      if (has_minmax) {
        constexpr size_t kMinMaxNeed = 8 + 8;
        if (canFit(kMinMaxNeed)) {
          if (!appendF32(out, 0x42, t.min_value) || !appendF32(out, 0x43, t.max_value)) {
            return false;
          }
        } else {
          truncated = true;
        }
      }

      if (!t.description.empty()) {
        const size_t desc_need = 4 + t.description.size();
        if (canFit(desc_need)) {
          if (!appendUtf8(out, 0x44, t.description)) {
            return false;
          }
        } else {
          truncated = true;
        }
      }
    }
  } else if (response.type == DescriptorResponseType::TelemetrySnapshot) {
    for (const auto& t : response.telemetry_samples) {
      const size_t need = 6 + (4 + t.key.size()) + (4 + t.value.size()) + (4 + t.unit.size());
      if (!canFit(need)) {
        truncated = true;
        break;
      }
      if (!appendTlv(out, 0x4F, 0x02, reinterpret_cast<const uint8_t*>(&t.metric_id), 2) ||
          !appendUtf8(out, 0x50, t.key) || !appendUtf8(out, 0x51, t.value) || !appendUtf8(out, 0x52, t.unit)) {
        return false;
      }
    }
  } else if (response.type == DescriptorResponseType::Liveness) {
    if (!tryAppendBool(0x60, response.liveness.online) || !tryAppendU32(0x61, response.liveness.uptime_ms) ||
        !tryAppendUtf8(0x62, response.liveness.state)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::Time) {
    if (!tryAppendU32(0x70, static_cast<uint32_t>(response.time.epoch_s & 0xFFFFFFFFULL)) ||
        !tryAppendU32(0x71, response.time.uptime_ms)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::Settings) {
    for (const auto& st : response.settings) {
      const size_t need = 6 + (4 + st.key.size()) + 5 + 5 + (4 + st.nvs_key.size()) +
                          (4 + st.current_value.size()) + (4 + st.default_value.size()) + (4 + st.description.size());
      if (!canFit(need)) {
        truncated = true;
        break;
      }
      if (!appendTlv(out, 0x7F, 0x02, reinterpret_cast<const uint8_t*>(&st.setting_id), 2) ||
          !appendUtf8(out, 0x80, st.key) || !appendU8(out, 0x81, static_cast<uint8_t>(st.value_type)) ||
          !appendBool(out, 0x82, st.writable) || !appendUtf8(out, 0x83, st.nvs_key) ||
          !appendUtf8(out, 0x84, st.current_value) || !appendUtf8(out, 0x85, st.default_value) ||
          !appendUtf8(out, 0x86, st.description)) {
        return false;
      }
      ++paged_emitted_count;
    }
  } else if (response.type == DescriptorResponseType::Setting) {
    const auto& st = response.setting;
    if (!canFit(6) || !appendTlv(out, 0x7F, 0x02, reinterpret_cast<const uint8_t*>(&st.setting_id), 2) ||
        !tryAppendUtf8(0x80, st.key) || !tryAppendU8(0x81, static_cast<uint8_t>(st.value_type)) ||
        !tryAppendBool(0x82, st.writable) || !tryAppendUtf8(0x83, st.nvs_key) || !tryAppendUtf8(0x84, st.current_value) ||
        !tryAppendUtf8(0x85, st.default_value) || !tryAppendUtf8(0x86, st.description)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::LogStatus) {
    if (!tryAppendBool(0x90, response.logger_available) ||
        !tryAppendBool(0x91, response.logger_enabled) ||
        !tryAppendU8(0x92, response.logger_min_level) ||
        !tryAppendU32(0x93, response.log_bytes_used) ||
        !tryAppendU32(0x94, response.log_bytes_dropped) ||
        !tryAppendU32(0x95, response.log_records_appended) ||
        !tryAppendU32(0x96, response.log_rotations) ||
        !tryAppendU32(0x97, response.log_total_size)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::LogChunk) {
    const size_t header_need = 8 + 8;
    if (!canFit(header_need) ||
        !appendU32(out, 0xA0, response.log_chunk_offset) ||
        !appendU32(out, 0xA1, response.log_total_size)) {
      return false;
    }
    size_t max_data = 0;
    if (ProtocolCodec::kMaxPayload > out.size() + 4) {
      max_data = ProtocolCodec::kMaxPayload - out.size() - 4;
    }
    const size_t emit = std::min<size_t>(response.log_chunk.size(), max_data);
    if (!appendTlv(out,
                   0xA2,
                   0x08,
                   emit == 0 ? nullptr : response.log_chunk.data(),
                   static_cast<uint16_t>(emit))) {
      return false;
    }
    if (emit != response.log_chunk.size()) {
      truncated = true;
    }
  } else if (response.type == DescriptorResponseType::StorageInfo) {
    if (!tryAppendU8(0xB0, static_cast<uint8_t>(response.storage_info.mode)) ||
        !tryAppendBool(0xB1, response.storage_info.available) ||
        !tryAppendBool(0xB2, response.storage_info.mounted) ||
        !tryAppendU32(0xB3, response.storage_info.total_bytes) ||
        !tryAppendU32(0xB4, response.storage_info.used_bytes) ||
        !tryAppendU32(0xB5, response.storage_info.free_bytes) ||
        !tryAppendUtf8(0xB6, response.storage_info.root_path) ||
        !tryAppendUtf8(0xB7, response.storage_info.cwd)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::StorageList) {
    if (!tryAppendUtf8(0xB8, response.storage_path) || !tryAppendUtf8(0xB9, response.storage_parent_path)) {
      return false;
    }
    for (const auto& e : response.storage_entries) {
      const size_t need = (4 + e.name.size()) + 5 + 8;
      if (!canFit(need)) {
        truncated = true;
        break;
      }
      if (!appendUtf8(out, 0xBA, e.name) ||
          !appendBool(out, 0xBB, e.is_dir) ||
          !appendU32(out, 0xBC, e.size_bytes)) {
        return false;
      }
    }
  } else if (response.type == DescriptorResponseType::StorageStat) {
    const std::string stat_path = response.storage_stat.path.empty() ? response.storage_path : response.storage_stat.path;
    if (!tryAppendUtf8(0xC0, stat_path) ||
        !tryAppendBool(0xC1, response.storage_stat.exists) ||
        !tryAppendBool(0xC2, response.storage_stat.is_dir) ||
        !tryAppendU32(0xC3, response.storage_stat.size_bytes)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::OtaStatus) {
    const uint8_t st = response.ota_status.transfer_state;
    const uint8_t code_le[2] = {
        static_cast<uint8_t>(response.ota_status.status_code & 0xFF),
        static_cast<uint8_t>((response.ota_status.status_code >> 8) & 0xFF),
    };
    if (!tryAppendU8(0xD0, st) ||
        !canFit(6) || !appendTlv(out, 0xD1, 0x02, code_le, 2) ||
        !tryAppendU32(0xD2, response.ota_status.expected_size) ||
        !tryAppendU32(0xD3, response.ota_status.received_size) ||
        !tryAppendU32(0xD4, response.ota_status.expected_crc32) ||
        !tryAppendU32(0xD5, response.ota_status.actual_crc32) ||
        !tryAppendUtf8(0xD6, response.ota_status.temp_path) ||
        !tryAppendUtf8(0xD7, response.ota_status.image_path) ||
        !tryAppendUtf8(0xD8, response.ota_status.persistent_state) ||
        !tryAppendU32(0xD9, response.ota_status.persistent_epoch_s) ||
        !tryAppendUtf8(0xDA, response.ota_status.confirmed_sw_version) ||
        !tryAppendUtf8(0xDB, response.ota_status.confirmed_build_id)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::OtaManifest) {
    for (const auto& item : response.ota_manifest) {
      const size_t need = 8 + (4 + item.file_name.size()) + 8 + 8 + (4 + item.version.size()) +
                          (4 + item.build_id.size()) + 8 + (4 + item.state.size()) + 8;
      if (!canFit(need)) {
        truncated = true;
        break;
      }
      if (!appendU32(out, 0xE0, item.file_id) ||
          !appendUtf8(out, 0xE1, item.file_name) ||
          !appendU32(out, 0xE2, item.size_bytes) ||
          !appendU32(out, 0xE3, item.crc32) ||
          !appendUtf8(out, 0xE4, item.version) ||
          !appendUtf8(out, 0xE5, item.build_id) ||
          !appendU32(out, 0xE6, item.created_epoch_s) ||
          !appendUtf8(out, 0xE7, item.state) ||
          !appendU32(out, 0xE8, item.required_app_bytes)) {
        return false;
      }
      ++paged_emitted_count;
    }
  } else if (response.type == DescriptorResponseType::OtaCapacity) {
    if (!tryAppendU32(0xF0, response.ota_capacity.max_fw_bytes) ||
        !tryAppendU32(0xF1, response.ota_capacity.last_checked_image_bytes) ||
        !tryAppendBool(0xF2, response.ota_capacity.last_fit)) {
      return false;
    }
  } else if (response.type == DescriptorResponseType::OtaGateInfo) {
    if (!tryAppendU8(0xF3, response.ota_gate.decision) ||
        !tryAppendUtf8(0xF4, response.ota_gate.detail)) {
      return false;
    }
  }

  if (response.is_paged) {
    if (response.type == DescriptorResponseType::Capabilities ||
        response.type == DescriptorResponseType::Telemetry ||
        response.type == DescriptorResponseType::Settings ||
        response.type == DescriptorResponseType::OtaManifest) {
      if (paged_emitted_count != response.returned_count) {
        const uint32_t next = static_cast<uint32_t>(response.cursor) + static_cast<uint32_t>(paged_emitted_count);
        paged_returned_count = paged_emitted_count;
        paged_next_cursor = static_cast<uint16_t>(std::min<uint32_t>(next, response.total_count));
        paged_done = (paged_next_cursor >= response.total_count);
        truncated = true;
      }
    }

    reserve_paged_meta = false;
    const uint8_t cursor_le[2] = {
        static_cast<uint8_t>(response.cursor & 0xFF),
        static_cast<uint8_t>((response.cursor >> 8) & 0xFF),
    };
    const uint8_t ret_le[2] = {
        static_cast<uint8_t>(paged_returned_count & 0xFF),
        static_cast<uint8_t>((paged_returned_count >> 8) & 0xFF),
    };
    const uint8_t total_le[2] = {
        static_cast<uint8_t>(response.total_count & 0xFF),
        static_cast<uint8_t>((response.total_count >> 8) & 0xFF),
    };
    const uint8_t next_le[2] = {
        static_cast<uint8_t>(paged_next_cursor & 0xFF),
        static_cast<uint8_t>((paged_next_cursor >> 8) & 0xFF),
    };
    if (!tryAppendBool(0x18, true) ||
        !tryAppendU32(0x19, response.snapshot_id) ||
        !canFit(6) || !appendTlv(out, 0x1A, 0x02, total_le, 2) ||
        !canFit(6) || !appendTlv(out, 0x1B, 0x02, cursor_le, 2) ||
        !canFit(6) || !appendTlv(out, 0x1C, 0x02, ret_le, 2) ||
        !canFit(6) || !appendTlv(out, 0x1D, 0x02, next_le, 2) ||
        !tryAppendBool(0x1E, paged_done)) {
      return false;
    }
  }

  if (truncated) {
    const std::string tmsg = "truncated";
    if (canFit(4 + tmsg.size())) {
      (void)appendUtf8(out, 0x11, tmsg);
    }
  }

  out_payload.assign(reinterpret_cast<const char*>(out.data()), out.size());
  return true;
}

bool decodeDescriptorResponse(const uint8_t* payload, size_t len, DescriptorResponse& out) {
  out = DescriptorResponse{};
  if (payload == nullptr || len < 8) {
    return false;
  }

  CapabilityDescriptor cur_cap{};
  bool have_cap_key = false;
  TelemetryDescriptor cur_telem{};
  int telem_stage = 0;
  TelemetrySample cur_sample{};
  int sample_stage = 0;
  SettingDescriptor cur_setting{};
  int setting_stage = 0;
  StorageEntry cur_storage_entry{};
  int storage_entry_stage = 0;
  OtaManifestEntry cur_ota_entry{};
  int ota_entry_stage = 0;

  auto flush_cap = [&]() {
    if (have_cap_key) {
      out.capabilities.push_back(cur_cap);
      cur_cap = CapabilityDescriptor{};
      have_cap_key = false;
    }
  };
  auto flush_telem = [&]() {
    if (telem_stage > 0) {
      out.telemetry.push_back(cur_telem);
      cur_telem = TelemetryDescriptor{};
      telem_stage = 0;
    }
  };
  auto flush_sample = [&]() {
    if (sample_stage > 0) {
      out.telemetry_samples.push_back(cur_sample);
      cur_sample = TelemetrySample{};
      sample_stage = 0;
    }
  };
  auto flush_setting = [&]() {
    if (setting_stage > 0) {
      out.settings.push_back(cur_setting);
      cur_setting = SettingDescriptor{};
      setting_stage = 0;
    }
  };
  auto flush_storage_entry = [&]() {
    if (storage_entry_stage > 0) {
      out.storage_entries.push_back(cur_storage_entry);
      cur_storage_entry = StorageEntry{};
      storage_entry_stage = 0;
    }
  };
  auto flush_ota_entry = [&]() {
    if (ota_entry_stage > 0) {
      out.ota_manifest.push_back(cur_ota_entry);
      cur_ota_entry = OtaManifestEntry{};
      ota_entry_stage = 0;
    }
  };

  size_t off = 0;
  while (off + 4 <= len) {
    const uint8_t tag = payload[off + 0];
    const uint8_t type = payload[off + 1];
    const uint16_t tlv_len = static_cast<uint16_t>(payload[off + 2]) |
                             (static_cast<uint16_t>(payload[off + 3]) << 8);
    off += 4;
    if ((off + tlv_len) > len) {
      return false;
    }
    const uint8_t* v = payload + off;

    if (tag == 0x10 && type == 0x01 && tlv_len == 1) {
      out.type = static_cast<DescriptorResponseType>(v[0]);
    } else if (tag == 0x11 && type == 0x09) {
      out.message.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x18 && type == 0x06 && tlv_len == 1) {
      out.is_paged = (v[0] != 0);
    } else if (tag == 0x19 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.snapshot_id);
    } else if (tag == 0x1A && type == 0x02 && tlv_len == 2) {
      out.total_count = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x1B && type == 0x02 && tlv_len == 2) {
      out.cursor = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x1C && type == 0x02 && tlv_len == 2) {
      out.returned_count = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x1D && type == 0x02 && tlv_len == 2) {
      out.next_cursor = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x1E && type == 0x06 && tlv_len == 1) {
      out.done = (v[0] != 0);
    } else if (tag == 0x20 && type == 0x09) {
      out.device.device_type.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x21 && type == 0x09) {
      out.device.device_id.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x22 && type == 0x09) {
      out.device.device_name.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x23 && type == 0x09) {
      out.device.hw_version.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x24 && type == 0x09) {
      out.device.sw_version.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x25 && type == 0x09) {
      out.device.build_id.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x30 && type == 0x09) {
      flush_cap();
      cur_cap.key.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      have_cap_key = true;
    } else if (tag == 0x31 && type == 0x09) {
      cur_cap.description.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (have_cap_key) {
        flush_cap();
      }
    } else if (tag == 0x3F && type == 0x02 && tlv_len == 2) {
      cur_telem.metric_id = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x40 && type == 0x09) {
      flush_telem();
      cur_telem.key.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      telem_stage = 1;
    } else if (tag == 0x41 && type == 0x09) {
      cur_telem.unit.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (telem_stage < 2) telem_stage = 2;
    } else if (tag == 0x42 && type == 0x07) {
      (void)readF32(v, tlv_len, cur_telem.min_value);
      if (telem_stage < 3) telem_stage = 3;
    } else if (tag == 0x43 && type == 0x07) {
      (void)readF32(v, tlv_len, cur_telem.max_value);
      if (telem_stage < 4) telem_stage = 4;
    } else if (tag == 0x44 && type == 0x09) {
      cur_telem.description.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      flush_telem();
    } else if (tag == 0x4F && type == 0x02 && tlv_len == 2) {
      cur_sample.metric_id = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x50 && type == 0x09) {
      flush_sample();
      cur_sample.key.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      sample_stage = 1;
    } else if (tag == 0x51 && type == 0x09) {
      cur_sample.value.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (sample_stage < 2) sample_stage = 2;
    } else if (tag == 0x52 && type == 0x09) {
      cur_sample.unit.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      flush_sample();
    } else if (tag == 0x60 && type == 0x06 && tlv_len == 1) {
      out.liveness.online = (v[0] == 1);
    } else if (tag == 0x61 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.liveness.uptime_ms);
    } else if (tag == 0x62 && type == 0x09) {
      out.liveness.state.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0x70 && type == 0x03 && tlv_len == 4) {
      uint32_t epoch = 0;
      (void)readU32(v, tlv_len, epoch);
      out.time.epoch_s = epoch;
    } else if (tag == 0x71 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.time.uptime_ms);
    } else if (tag == 0x7F && type == 0x02 && tlv_len == 2) {
      cur_setting.setting_id = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0x80 && type == 0x09) {
      flush_setting();
      cur_setting.key.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      setting_stage = 1;
    } else if (tag == 0x81 && type == 0x01 && tlv_len == 1) {
      cur_setting.value_type = static_cast<SettingValueType>(v[0]);
      if (setting_stage < 2) setting_stage = 2;
    } else if (tag == 0x82 && type == 0x06 && tlv_len == 1) {
      cur_setting.writable = (v[0] == 1);
      if (setting_stage < 3) setting_stage = 3;
    } else if (tag == 0x83 && type == 0x09) {
      cur_setting.nvs_key.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (setting_stage < 4) setting_stage = 4;
    } else if (tag == 0x84 && type == 0x09) {
      cur_setting.current_value.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (setting_stage < 5) setting_stage = 5;
    } else if (tag == 0x85 && type == 0x09) {
      cur_setting.default_value.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (setting_stage < 6) setting_stage = 6;
    } else if (tag == 0x86 && type == 0x09) {
      cur_setting.description.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      flush_setting();
    } else if (tag == 0x90 && type == 0x06 && tlv_len == 1) {
      out.logger_available = (v[0] != 0);
    } else if (tag == 0x91 && type == 0x06 && tlv_len == 1) {
      out.logger_enabled = (v[0] != 0);
    } else if (tag == 0x92 && type == 0x01 && tlv_len == 1) {
      out.logger_min_level = v[0];
    } else if (tag == 0x93 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.log_bytes_used);
    } else if (tag == 0x94 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.log_bytes_dropped);
    } else if (tag == 0x95 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.log_records_appended);
    } else if (tag == 0x96 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.log_rotations);
    } else if (tag == 0x97 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.log_total_size);
    } else if (tag == 0xA0 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.log_chunk_offset);
    } else if (tag == 0xA1 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.log_total_size);
    } else if (tag == 0xA2 && type == 0x08) {
      out.log_chunk.assign(v, v + tlv_len);
    } else if (tag == 0xB0 && type == 0x01 && tlv_len == 1) {
      out.storage_info.mode = static_cast<StorageBackendMode>(v[0]);
    } else if (tag == 0xB1 && type == 0x06 && tlv_len == 1) {
      out.storage_info.available = (v[0] != 0);
    } else if (tag == 0xB2 && type == 0x06 && tlv_len == 1) {
      out.storage_info.mounted = (v[0] != 0);
    } else if (tag == 0xB3 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.storage_info.total_bytes);
    } else if (tag == 0xB4 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.storage_info.used_bytes);
    } else if (tag == 0xB5 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.storage_info.free_bytes);
    } else if (tag == 0xB6 && type == 0x09) {
      out.storage_info.root_path.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xB7 && type == 0x09) {
      out.storage_info.cwd.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xB8 && type == 0x09) {
      out.storage_path.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xB9 && type == 0x09) {
      out.storage_parent_path.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xBA && type == 0x09) {
      flush_storage_entry();
      cur_storage_entry.name.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      storage_entry_stage = 1;
    } else if (tag == 0xBB && type == 0x06 && tlv_len == 1) {
      cur_storage_entry.is_dir = (v[0] != 0);
      if (storage_entry_stage < 2) storage_entry_stage = 2;
    } else if (tag == 0xBC && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, cur_storage_entry.size_bytes);
      if (storage_entry_stage < 3) storage_entry_stage = 3;
    } else if (tag == 0xC0 && type == 0x09) {
      out.storage_stat.path.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xC1 && type == 0x06 && tlv_len == 1) {
      out.storage_stat.exists = (v[0] != 0);
    } else if (tag == 0xC2 && type == 0x06 && tlv_len == 1) {
      out.storage_stat.is_dir = (v[0] != 0);
    } else if (tag == 0xC3 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.storage_stat.size_bytes);
    } else if (tag == 0xD0 && type == 0x01 && tlv_len == 1) {
      out.ota_status.transfer_state = v[0];
    } else if (tag == 0xD1 && type == 0x02 && tlv_len == 2) {
      out.ota_status.status_code = static_cast<uint16_t>(v[0]) | (static_cast<uint16_t>(v[1]) << 8);
    } else if (tag == 0xD2 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.ota_status.expected_size);
    } else if (tag == 0xD3 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.ota_status.received_size);
    } else if (tag == 0xD4 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.ota_status.expected_crc32);
    } else if (tag == 0xD5 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.ota_status.actual_crc32);
    } else if (tag == 0xD6 && type == 0x09) {
      out.ota_status.temp_path.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xD7 && type == 0x09) {
      out.ota_status.image_path.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xD8 && type == 0x09) {
      out.ota_status.persistent_state.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xD9 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.ota_status.persistent_epoch_s);
    } else if (tag == 0xDA && type == 0x09) {
      out.ota_status.confirmed_sw_version.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xDB && type == 0x09) {
      out.ota_status.confirmed_build_id.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    } else if (tag == 0xE0 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, cur_ota_entry.file_id);
    } else if (tag == 0xE1 && type == 0x09) {
      flush_ota_entry();
      cur_ota_entry.file_name.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      ota_entry_stage = 1;
    } else if (tag == 0xE2 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, cur_ota_entry.size_bytes);
      if (ota_entry_stage < 2) ota_entry_stage = 2;
    } else if (tag == 0xE3 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, cur_ota_entry.crc32);
      if (ota_entry_stage < 3) ota_entry_stage = 3;
    } else if (tag == 0xE4 && type == 0x09) {
      cur_ota_entry.version.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (ota_entry_stage < 4) ota_entry_stage = 4;
    } else if (tag == 0xE5 && type == 0x09) {
      cur_ota_entry.build_id.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (ota_entry_stage < 5) ota_entry_stage = 5;
    } else if (tag == 0xE6 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, cur_ota_entry.created_epoch_s);
      if (ota_entry_stage < 6) ota_entry_stage = 6;
    } else if (tag == 0xE7 && type == 0x09) {
      cur_ota_entry.state.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      if (ota_entry_stage < 7) ota_entry_stage = 7;
    } else if (tag == 0xE8 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, cur_ota_entry.required_app_bytes);
      if (ota_entry_stage < 8) ota_entry_stage = 8;
    } else if (tag == 0xF0 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.ota_capacity.max_fw_bytes);
    } else if (tag == 0xF1 && type == 0x03 && tlv_len == 4) {
      (void)readU32(v, tlv_len, out.ota_capacity.last_checked_image_bytes);
    } else if (tag == 0xF2 && type == 0x06 && tlv_len == 1) {
      out.ota_capacity.last_fit = (v[0] != 0);
    } else if (tag == 0xF3 && type == 0x01 && tlv_len == 1) {
      out.ota_gate.decision = v[0];
    } else if (tag == 0xF4 && type == 0x09) {
      out.ota_gate.detail.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
    }

    off += tlv_len;
  }

  flush_cap();
  flush_telem();
  flush_sample();
  flush_setting();
  flush_storage_entry();
  flush_ota_entry();

  if (out.type == DescriptorResponseType::Setting && !out.settings.empty()) {
    out.setting = out.settings.front();
    out.settings.clear();
  }

  return out.type != DescriptorResponseType::Unknown;
}

}  // namespace espnow_link




