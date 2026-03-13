#include "espnow_link/codec.hpp"
#include "espnow_link/protocol.hpp"

namespace espnow_link {

namespace {

constexpr uint8_t kPackedMagicDesc = 0xD1;
constexpr uint8_t kPackedMagicCtrl = 0xD2;
constexpr uint8_t kPackedVersion = 1;
constexpr uint8_t kPackedKindQuery = 1;
constexpr uint8_t kPackedKindResponse = 2;
constexpr uint8_t kPackedKindCommand = 1;
constexpr uint8_t kPackedKindResult = 2;

bool appendU8(std::vector<uint8_t>& out, uint8_t v) {
  if (out.size() + 1 > ProtocolCodec::kMaxPayload) return false;
  out.push_back(v);
  return true;
}

bool appendU16(std::vector<uint8_t>& out, uint16_t v) {
  if (out.size() + 2 > ProtocolCodec::kMaxPayload) return false;
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  return true;
}

bool appendBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
  if (data == nullptr && len > 0) return false;
  if (out.size() + len > ProtocolCodec::kMaxPayload) return false;
  if (len > 0) out.insert(out.end(), data, data + len);
  return true;
}

bool unwrap(const uint8_t* payload,
            size_t len,
            uint8_t expected_magic,
            uint8_t expected_kind,
            const uint8_t*& out_inner,
            size_t& out_inner_len) {
  if (payload == nullptr || len < 5) return false;
  if (payload[0] != expected_magic) return false;
  if (payload[1] != kPackedVersion) return false;
  if (payload[2] != expected_kind) return false;
  const uint16_t inner_len = static_cast<uint16_t>(payload[3]) |
                             (static_cast<uint16_t>(payload[4]) << 8);
  if (len != static_cast<size_t>(5 + inner_len)) return false;
  out_inner = payload + 5;
  out_inner_len = inner_len;
  return true;
}

bool wrap(uint8_t magic,
          uint8_t kind,
          const uint8_t* inner,
          size_t inner_len,
          std::vector<uint8_t>& out_payload) {
  if (inner_len > 0xFFFFU) return false;
  out_payload.clear();
  return appendU8(out_payload, magic) && appendU8(out_payload, kPackedVersion) &&
         appendU8(out_payload, kind) && appendU16(out_payload, static_cast<uint16_t>(inner_len)) &&
         appendBytes(out_payload, inner, inner_len);
}

}  // namespace

bool PackedTlvProfileCodec::encodeDescriptorQuery(const DescriptorQuery& query,
                                                  std::vector<uint8_t>& out_payload) const {
  std::vector<uint8_t> inner;
  if (!::espnow_link::encodeDescriptorQuery(query, inner)) {
    return false;
  }
  return wrap(kPackedMagicDesc, kPackedKindQuery, inner.data(), inner.size(), out_payload);
}

bool PackedTlvProfileCodec::decodeDescriptorQuery(const uint8_t* payload,
                                                  size_t len,
                                                  DescriptorQuery& out) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kPackedMagicDesc, kPackedKindQuery, inner, inner_len)) {
    return parseDescriptorQuery(payload, len, out);
  }
  return parseDescriptorQuery(inner, inner_len, out);
}

bool PackedTlvProfileCodec::encodeDescriptorResponse(const DescriptorResponse& response,
                                                     std::vector<uint8_t>& out_payload) const {
  std::string encoded;
  if (!::espnow_link::encodeDescriptorResponse(response, encoded)) {
    return false;
  }
  return wrap(kPackedMagicDesc,
              kPackedKindResponse,
              reinterpret_cast<const uint8_t*>(encoded.data()),
              encoded.size(),
              out_payload);
}

bool PackedTlvProfileCodec::decodeDescriptorResponse(const uint8_t* payload,
                                                     size_t len,
                                                     DescriptorResponse& out) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kPackedMagicDesc, kPackedKindResponse, inner, inner_len)) {
    return ::espnow_link::decodeDescriptorResponse(payload, len, out);
  }
  return ::espnow_link::decodeDescriptorResponse(inner, inner_len, out);
}

bool PackedTlvProfileCodec::encodeControlCommand(uint16_t cmd_id,
                                                 std::vector<uint8_t>& out_payload) const {
  std::vector<uint8_t> inner;
  if (!buildControlCommandPayload(cmd_id, inner)) {
    return false;
  }
  return wrap(kPackedMagicCtrl, kPackedKindCommand, inner.data(), inner.size(), out_payload);
}

bool PackedTlvProfileCodec::decodeControlCommand(const uint8_t* payload,
                                                 size_t len,
                                                 uint16_t& out_cmd_id) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kPackedMagicCtrl, kPackedKindCommand, inner, inner_len)) {
    return parseControlCommandPayload(payload, len, out_cmd_id);
  }
  return parseControlCommandPayload(inner, inner_len, out_cmd_id);
}

bool PackedTlvProfileCodec::encodeControlResult(uint16_t cmd_id,
                                                uint16_t result_code,
                                                std::vector<uint8_t>& out_payload) const {
  std::vector<uint8_t> inner;
  if (!buildControlResultPayload(cmd_id, result_code, inner)) {
    return false;
  }
  return wrap(kPackedMagicCtrl, kPackedKindResult, inner.data(), inner.size(), out_payload);
}

bool PackedTlvProfileCodec::decodeControlResult(const uint8_t* payload,
                                                size_t len,
                                                uint16_t& out_cmd_id,
                                                uint16_t& out_result_code) const {
  const uint8_t* inner = nullptr;
  size_t inner_len = 0;
  if (!unwrap(payload, len, kPackedMagicCtrl, kPackedKindResult, inner, inner_len)) {
    return parseControlResultPayload(payload, len, out_cmd_id, out_result_code);
  }
  return parseControlResultPayload(inner, inner_len, out_cmd_id, out_result_code);
}

}  // namespace espnow_link

