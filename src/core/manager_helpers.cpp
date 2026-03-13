#include "manager_helpers.hpp"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "espnow_link/control_codec.hpp"
#include "espnow_link/descriptor.hpp"

namespace espnow_link {
namespace manager_helpers {

uint64_t monotonicUs() {
#if defined(ARDUINO)
  return static_cast<uint64_t>(micros());
#else
  return static_cast<uint64_t>((std::clock() * 1000000ULL) / CLOCKS_PER_SEC);
#endif
}

ScopedMetricsDuration::ScopedMetricsDuration(uint64_t* total_us, uint32_t* last_us, uint32_t* max_us)
    : total_us_(total_us), last_us_(last_us), max_us_(max_us), start_us_(monotonicUs()) {}

ScopedMetricsDuration::~ScopedMetricsDuration() {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  if (total_us_ == nullptr || last_us_ == nullptr || max_us_ == nullptr) {
    return;
  }

  const uint64_t elapsed = monotonicUs() - start_us_;
  const uint32_t bounded =
      (elapsed > static_cast<uint64_t>(UINT32_MAX)) ? UINT32_MAX : static_cast<uint32_t>(elapsed);

  *last_us_ = bounded;
  *total_us_ += elapsed;
  if (bounded > *max_us_) {
    *max_us_ = bounded;
  }
#else
  (void)total_us_;
  (void)last_us_;
  (void)max_us_;
#endif
}

bool parseChannelSwitchPayload(const uint8_t* payload, size_t len, ChannelSwitchPayload& out) {
  if (payload == nullptr) {
    return false;
  }
  bool have_ch = false;
  bool have_time = false;
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
      out.new_channel = v[0];
      have_ch = true;
    } else if (tag == 0x11 && type == 0x03 && tlv_len == 4) {
      out.effective_at_ms = static_cast<uint32_t>(v[0]) |
                            (static_cast<uint32_t>(v[1]) << 8) |
                            (static_cast<uint32_t>(v[2]) << 16) |
                            (static_cast<uint32_t>(v[3]) << 24);
      have_time = true;
    }
    off += tlv_len;
  }
  return have_ch && have_time;
}

bool parsePairInitPayload(const uint8_t* payload,
                          size_t len,
                          PairSeed& out_seed,
                          uint32_t& out_nonce,
                          uint8_t& out_profile) {
  if (payload == nullptr) {
    return false;
  }
  bool have_seed = false;
  bool have_nonce = false;
  bool have_profile = false;
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
    if (tag == 0x10 && type == 0x08 && tlv_len == out_seed.bytes.size()) {
      std::memcpy(out_seed.bytes.data(), v, out_seed.bytes.size());
      have_seed = true;
    } else if (tag == 0x11 && type == 0x03 && tlv_len == 4) {
      out_nonce = static_cast<uint32_t>(v[0]) |
                  (static_cast<uint32_t>(v[1]) << 8) |
                  (static_cast<uint32_t>(v[2]) << 16) |
                  (static_cast<uint32_t>(v[3]) << 24);
      have_nonce = true;
    } else if (tag == 0x12 && type == 0x01 && tlv_len == 1) {
      out_profile = v[0];
      have_profile = true;
    }
    off += tlv_len;
  }
  return have_seed && have_nonce && have_profile;
}

bool parseNonceEchoPayload(const uint8_t* payload, size_t len, uint8_t expected_tag, uint32_t& out_nonce) {
  if (payload == nullptr) {
    return false;
  }
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
    if (tag == expected_tag && type == 0x03 && tlv_len == 4) {
      out_nonce = static_cast<uint32_t>(v[0]) |
                  (static_cast<uint32_t>(v[1]) << 8) |
                  (static_cast<uint32_t>(v[2]) << 16) |
                  (static_cast<uint32_t>(v[3]) << 24);
      return true;
    }
    off += tlv_len;
  }
  return false;
}

bool parsePairConfirmAckPayload(const uint8_t* payload, size_t len, bool& out_paired) {
  if (payload == nullptr) {
    return false;
  }
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
    if (tag == 0x40 && type == 0x01 && tlv_len == 1) {
      out_paired = (payload[off] == 1);
      return true;
    }
    off += tlv_len;
  }
  return false;
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

int32_t readLeI32(const uint8_t* p) {
  return static_cast<int32_t>(readLe32(p));
}

void appendLe16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendLe32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

bool buildMandatoryEventPayload(uint16_t event_id,
                                uint8_t severity,
                                int32_t value,
                                uint32_t event_ts_s,
                                std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  out_payload.reserve(11);
  appendLe16(out_payload, event_id);
  out_payload.push_back(severity);
  appendLe32(out_payload, static_cast<uint32_t>(value));
  appendLe32(out_payload, event_ts_s);
  return true;
}

bool parseMandatoryEventPayload(const uint8_t* payload,
                                size_t len,
                                uint16_t& out_event_id,
                                uint8_t& out_severity,
                                int32_t& out_value,
                                uint32_t& out_event_ts_s) {
  if (payload == nullptr || len < 11) {
    return false;
  }

  out_event_id = static_cast<uint16_t>(payload[0]) | (static_cast<uint16_t>(payload[1]) << 8);
  out_severity = payload[2];
  out_value = readLeI32(payload + 3);
  out_event_ts_s = readLe32(payload + 7);
  return true;
}

bool buildTopologyTriggerPayload(const TopologyTriggerPayload& in,
                                 std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  out_payload.reserve(18U);
  out_payload.push_back(kTopologyProtoId);
  out_payload.push_back(kTopologyProtoVersion);
  out_payload.push_back(kTopologyMsgTrigger);
  appendLe32(out_payload, in.topology_version);
  appendLe16(out_payload, in.seq);
  out_payload.push_back(in.src_role);
  out_payload.push_back(in.src_vid);
  out_payload.push_back(in.dst_role);
  out_payload.push_back(in.dst_vid);
  out_payload.push_back(in.direction);
  appendLe16(out_payload, in.delay_ms);
  appendLe16(out_payload, in.hold_ms);
  return true;
}

bool parseTopologyTriggerPayload(const uint8_t* payload,
                                 size_t len,
                                 TopologyTriggerPayload& out) {
  out = TopologyTriggerPayload{};
  if (payload == nullptr || len < 18U) {
    return false;
  }
  if (payload[0] != kTopologyProtoId ||
      payload[1] != kTopologyProtoVersion ||
      payload[2] != kTopologyMsgTrigger) {
    return false;
  }
  out.topology_version = readLe32(payload + 3U);
  out.seq = static_cast<uint16_t>(payload[7U]) |
            (static_cast<uint16_t>(payload[8U]) << 8);
  out.src_role = payload[9U];
  out.src_vid = payload[10U];
  out.dst_role = payload[11U];
  out.dst_vid = payload[12U];
  out.direction = payload[13U];
  out.delay_ms = static_cast<uint16_t>(payload[14U]) |
                 (static_cast<uint16_t>(payload[15U]) << 8);
  out.hold_ms = static_cast<uint16_t>(payload[16U]) |
                (static_cast<uint16_t>(payload[17U]) << 8);
  return true;
}

bool buildTopologyTriggerAckPayload(const TopologyTriggerAckPayload& in,
                                    std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  out_payload.reserve(17U);
  out_payload.push_back(kTopologyProtoId);
  out_payload.push_back(kTopologyProtoVersion);
  out_payload.push_back(kTopologyMsgAck);
  appendLe32(out_payload, in.topology_version);
  appendLe16(out_payload, in.seq);
  out_payload.push_back(in.src_role);
  out_payload.push_back(in.src_vid);
  out_payload.push_back(in.dst_role);
  out_payload.push_back(in.dst_vid);
  appendLe16(out_payload, in.ack_seq);
  out_payload.push_back(in.result);
  out_payload.push_back(in.reason);
  return true;
}

bool parseTopologyTriggerAckPayload(const uint8_t* payload,
                                    size_t len,
                                    TopologyTriggerAckPayload& out) {
  out = TopologyTriggerAckPayload{};
  if (payload == nullptr || len < 17U) {
    return false;
  }
  if (payload[0] != kTopologyProtoId ||
      payload[1] != kTopologyProtoVersion ||
      payload[2] != kTopologyMsgAck) {
    return false;
  }
  out.topology_version = readLe32(payload + 3U);
  out.seq = static_cast<uint16_t>(payload[7U]) |
            (static_cast<uint16_t>(payload[8U]) << 8);
  out.src_role = payload[9U];
  out.src_vid = payload[10U];
  out.dst_role = payload[11U];
  out.dst_vid = payload[12U];
  out.ack_seq = static_cast<uint16_t>(payload[13U]) |
                (static_cast<uint16_t>(payload[14U]) << 8);
  out.result = payload[15U];
  out.reason = payload[16U];
  return true;
}

bool buildFirmwareStatusPayload(uint8_t kind,
                                uint32_t transfer_corr_id,
                                uint32_t offset_bytes,
                                uint16_t status_code,
                                std::vector<uint8_t>& out_payload) {
  out_payload.clear();
  out_payload.reserve(11U);
  out_payload.push_back(kind);
  appendLe32(out_payload, transfer_corr_id);
  appendLe32(out_payload, offset_bytes);
  appendLe16(out_payload, status_code);
  return true;
}

bool parseFirmwareStatusPayload(const uint8_t* payload,
                                size_t len,
                                uint8_t& out_kind,
                                uint32_t& out_transfer_corr_id,
                                uint32_t& out_offset_bytes,
                                uint16_t& out_status_code) {
  if (payload == nullptr || len < 11U) {
    return false;
  }
  out_kind = payload[0];
  out_transfer_corr_id = readLe32(payload + 1);
  out_offset_bytes = readLe32(payload + 5);
  out_status_code = static_cast<uint16_t>(payload[9]) |
                    (static_cast<uint16_t>(payload[10]) << 8);
  return true;
}

bool classifyPullRequest(const uint8_t* payload, size_t len, uint8_t& out_service, uint8_t& out_op) {
  DescriptorQuery q{};
  if (parseDescriptorQuery(payload, len, q)) {
    switch (q.type) {
      case DescriptorQueryType::GetDevice:
      case DescriptorQueryType::GetCapabilities:
      case DescriptorQueryType::GetTime:
      case DescriptorQueryType::SetTime:
        out_service = 0x02;
        out_op = 0x01;
        return true;
      case DescriptorQueryType::GetSettings:
        out_service = 0x03;
        out_op = 0x05;
        return true;
      case DescriptorQueryType::GetSetting:
        out_service = 0x03;
        out_op = 0x01;
        return true;
      case DescriptorQueryType::SetSetting:
        out_service = 0x03;
        out_op = 0x03;
        return true;
      case DescriptorQueryType::GetTelemetry:
        out_service = 0x04;
        out_op = 0x01;
        return true;
      case DescriptorQueryType::PullTelemetry:
        out_service = 0x04;
        out_op = 0x03;
        return true;
      case DescriptorQueryType::GetLiveness:
        out_service = 0x05;
        out_op = 0x01;
        return true;
      case DescriptorQueryType::GetLogStatus:
        out_service = 0x02;
        out_op = 0x01;
        return true;
      case DescriptorQueryType::ReadLogChunk:
        out_service = 0x02;
        out_op = 0x03;
        return true;
      case DescriptorQueryType::ClearLog:
        out_service = 0x02;
        out_op = 0x05;
        return true;
      case DescriptorQueryType::SetLogControl:
        out_service = 0x02;
        out_op = 0x05;
        return true;
      case DescriptorQueryType::GetStorageInfo:
        out_service = 0x02;
        out_op = 0x01;
        return true;
      case DescriptorQueryType::ListStoragePath:
        out_service = 0x02;
        out_op = 0x03;
        return true;
      case DescriptorQueryType::StatStoragePath:
        out_service = 0x02;
        out_op = 0x01;
        return true;
      case DescriptorQueryType::TopologyStageClear:
      case DescriptorQueryType::TopologyStageBegin:
      case DescriptorQueryType::TopologyStageSlotSet:
      case DescriptorQueryType::TopologyStageGroupSet:
      case DescriptorQueryType::TopologyStageFinalize:
      case DescriptorQueryType::TopologyCommit:
      case DescriptorQueryType::TopologyStatus:
        out_service = 0x05;
        out_op = 0x03;
        return true;
      default:
        break;
    }
  }

  uint16_t cmd = 0;
  if (parseControlCommandPayload(payload, len, cmd)) {
    out_service = 0x06;
    out_op = 0x01;
    return true;
  }

  return false;
}

bool isValidPullRequestWire(uint8_t service, uint8_t op) {
  const bool service_ok = (service == 0x02) || (service == 0x03) || (service == 0x04) ||
                          (service == 0x05) || (service == 0x06);
  const bool op_ok = (op == 0x01) || (op == 0x03) || (op == 0x05);
  return service_ok && op_ok;
}

bool isValidPullResponseWire(uint8_t service, uint8_t op) {
  const bool service_ok = (service == 0x02) || (service == 0x03) || (service == 0x04) ||
                          (service == 0x05) || (service == 0x06);
  const bool op_ok = (op == 0x02) || (op == 0x04) || (op == 0x06);
  return service_ok && op_ok;
}

uint8_t responseOpForRequest(uint8_t service, uint8_t request_op) {
  if (service == 0x02) {
    return 0x02;
  }
  if (service == 0x03) {
    if (request_op == 0x01) return 0x02;
    if (request_op == 0x03) return 0x04;
    if (request_op == 0x05) return 0x06;
  }
  if (service == 0x04) {
    if (request_op == 0x01) return 0x02;
    if (request_op == 0x03) return 0x04;
  }
  if (service == 0x05) {
    return 0x02;
  }
  if (service == 0x06) {
    return 0x02;
  }
  if (service == 0x07) {
    if (request_op == 0x01) return 0x02;
    if (request_op == 0x03) return 0x04;
    if (request_op == 0x05) return 0x06;
    if (request_op == 0x07) return 0x08;
    return 0x02;
  }
  return 0x02;
}

std::string decodeDiscoveryName(const uint8_t* payload, size_t payload_len) {
  if (payload == nullptr || payload_len < 14) {
    return std::string();
  }

  const uint8_t proto_major = payload[0];
  const uint8_t name_id = payload[5];
  if (proto_major != kProtocolVersion) {
    return std::string();
  }

  char out[16] = {0};
  std::snprintf(out, sizeof(out), "id_%02X", name_id);
  return std::string(out);
}

uint8_t decodeDiscoveryRoleCode(const uint8_t* payload, size_t payload_len) {
  if (payload == nullptr || payload_len < 7U) {
    return 0U;
  }
  if (payload[0] != kProtocolVersion) {
    return 0U;
  }
  return payload[6];
}
}  // namespace manager_helpers
}  // namespace espnow_link

