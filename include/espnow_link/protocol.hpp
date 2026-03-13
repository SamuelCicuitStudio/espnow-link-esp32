#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "espnow_link/types.hpp"

namespace espnow_link {

/**
 * @brief Frame codec for low-level ESP-NOW wire frames (header + payload + CRC).
 */
class ProtocolCodec {
 public:
  static constexpr size_t kMaxFrameBytes = 250;
  static constexpr size_t kHeaderSize = 13;
  static constexpr size_t kCrcSize = 2;
  static constexpr size_t kFixedOverhead = kHeaderSize + kCrcSize;
  static constexpr size_t kMaxPayload = kMaxFrameBytes - kFixedOverhead;

  /**
   * @brief Encode frame header and payload into wire bytes.
   * @param header Frame header fields.
   * @param payload Payload bytes (may be null when payload_len is 0).
   * @param payload_len Payload length in bytes.
   * @param out Encoded output frame.
   * @return true on success.
   */
  static bool encode(const FrameHeader& header,
                     const uint8_t* payload,
                     size_t payload_len,
                     std::vector<uint8_t>& out);

  /**
   * @brief Decode wire frame and validate CRC/header.
   * @param data Input frame bytes.
   * @param len Input length in bytes.
   * @param out_header Decoded header output.
   * @param out_payload Pointer into input buffer for payload start.
   * @param out_payload_len Payload length output.
   * @return true on successful decode and validation.
   */
  static bool decode(const uint8_t* data,
                     size_t len,
                     FrameHeader& out_header,
                     const uint8_t*& out_payload,
                     size_t& out_payload_len);
};

}  // namespace espnow_link
