#pragma once

#include "espnow_link/codec.hpp"

namespace espnow_link {
namespace codec_impl {

inline bool encodeDescriptorQueryPassthrough(const DescriptorQuery& query, std::vector<uint8_t>& out_payload) {
  return encodeDescriptorQuery(query, out_payload);
}

inline bool decodeDescriptorQueryPassthrough(const uint8_t* payload, size_t len, DescriptorQuery& out) {
  return parseDescriptorQuery(payload, len, out);
}

inline bool encodeDescriptorResponsePassthrough(const DescriptorResponse& response,
                                                std::vector<uint8_t>& out_payload) {
  std::string encoded;
  if (!encodeDescriptorResponse(response, encoded)) {
    return false;
  }
  out_payload.assign(encoded.begin(), encoded.end());
  return true;
}

inline bool decodeDescriptorResponsePassthrough(const uint8_t* payload, size_t len, DescriptorResponse& out) {
  return decodeDescriptorResponse(payload, len, out);
}

inline bool encodeControlCommandPassthrough(uint16_t cmd_id, std::vector<uint8_t>& out_payload) {
  return buildControlCommandPayload(cmd_id, out_payload);
}

inline bool decodeControlCommandPassthrough(const uint8_t* payload, size_t len, uint16_t& out_cmd_id) {
  return parseControlCommandPayload(payload, len, out_cmd_id);
}

inline bool encodeControlResultPassthrough(uint16_t cmd_id,
                                           uint16_t result_code,
                                           std::vector<uint8_t>& out_payload) {
  return buildControlResultPayload(cmd_id, result_code, out_payload);
}

inline bool decodeControlResultPassthrough(const uint8_t* payload,
                                           size_t len,
                                           uint16_t& out_cmd_id,
                                           uint16_t& out_result_code) {
  return parseControlResultPayload(payload, len, out_cmd_id, out_result_code);
}

}  // namespace codec_impl

#define ESPNOW_LINK_IMPLEMENT_CODEC_PASSTHROUGH(CLASSNAME)                                              \
  bool CLASSNAME::encodeDescriptorQuery(const DescriptorQuery& query,                                   \
                                        std::vector<uint8_t>& out_payload) const {                      \
    return codec_impl::encodeDescriptorQueryPassthrough(query, out_payload);                            \
  }                                                                                                      \
  bool CLASSNAME::decodeDescriptorQuery(const uint8_t* payload,                                         \
                                        size_t len,                                                      \
                                        DescriptorQuery& out) const {                                   \
    return codec_impl::decodeDescriptorQueryPassthrough(payload, len, out);                             \
  }                                                                                                      \
  bool CLASSNAME::encodeDescriptorResponse(const DescriptorResponse& response,                           \
                                           std::vector<uint8_t>& out_payload) const {                   \
    return codec_impl::encodeDescriptorResponsePassthrough(response, out_payload);                      \
  }                                                                                                      \
  bool CLASSNAME::decodeDescriptorResponse(const uint8_t* payload,                                      \
                                           size_t len,                                                   \
                                           DescriptorResponse& out) const {                             \
    return codec_impl::decodeDescriptorResponsePassthrough(payload, len, out);                          \
  }                                                                                                      \
  bool CLASSNAME::encodeControlCommand(uint16_t cmd_id,                                                 \
                                       std::vector<uint8_t>& out_payload) const {                       \
    return codec_impl::encodeControlCommandPassthrough(cmd_id, out_payload);                            \
  }                                                                                                      \
  bool CLASSNAME::decodeControlCommand(const uint8_t* payload,                                          \
                                       size_t len,                                                       \
                                       uint16_t& out_cmd_id) const {                                    \
    return codec_impl::decodeControlCommandPassthrough(payload, len, out_cmd_id);                       \
  }                                                                                                      \
  bool CLASSNAME::encodeControlResult(uint16_t cmd_id,                                                  \
                                      uint16_t result_code,                                              \
                                      std::vector<uint8_t>& out_payload) const {                        \
    return codec_impl::encodeControlResultPassthrough(cmd_id, result_code, out_payload);                \
  }                                                                                                      \
  bool CLASSNAME::decodeControlResult(const uint8_t* payload,                                           \
                                      size_t len,                                                        \
                                      uint16_t& out_cmd_id,                                              \
                                      uint16_t& out_result_code) const {                                \
    return codec_impl::decodeControlResultPassthrough(payload, len, out_cmd_id, out_result_code);       \
  }

}  // namespace espnow_link
