#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/control_codec.hpp"
#include "espnow_link/descriptor.hpp"

namespace espnow_link {

using CodecId = uint16_t;
/** @brief Default TLV codec. */
constexpr CodecId kCodecIdDefault = 1;
/** @brief Compact indexed codec for reduced payload overhead. */
constexpr CodecId kCodecIdCompactIndexed = 2;
/** @brief Packed TLV codec variant. */
constexpr CodecId kCodecIdPackedTlv = 3;
/** @brief Varint-TLV codec variant. */
constexpr CodecId kCodecIdVarintTlv = 4;
/** @brief Delta-binary codec variant. */
constexpr CodecId kCodecIdDeltaBinary = 5;
/** @brief CBOR-lite codec variant. */
constexpr CodecId kCodecIdCborLite = 6;
/** @brief Binary map codec variant. */
constexpr CodecId kCodecIdBinaryMap = 7;

/**
 * @brief Check whether a codec ID belongs to built-in codecs.
 * @param codec_id Codec identifier.
 * @return true when codec ID is built in this library.
 */
bool isBuiltInCodecId(CodecId codec_id);

/**
 * @brief Descriptor/control codec contract used by manager at runtime.
 */
class IProfileCodec {
 public:
  virtual ~IProfileCodec() = default;

  /** @brief Get stable codec identifier. */
  virtual CodecId codecId() const = 0;
  /** @brief Get human-readable codec name. */
  virtual const char* codecName() const = 0;

  /** @brief Encode descriptor query payload. */
  virtual bool encodeDescriptorQuery(const DescriptorQuery& query,
                                     std::vector<uint8_t>& out_payload) const = 0;
  /** @brief Decode descriptor query payload. */
  virtual bool decodeDescriptorQuery(const uint8_t* payload,
                                     size_t len,
                                     DescriptorQuery& out) const = 0;

  /** @brief Encode descriptor response payload. */
  virtual bool encodeDescriptorResponse(const DescriptorResponse& response,
                                        std::vector<uint8_t>& out_payload) const = 0;
  /** @brief Decode descriptor response payload. */
  virtual bool decodeDescriptorResponse(const uint8_t* payload,
                                        size_t len,
                                        DescriptorResponse& out) const = 0;

  /** @brief Encode control command payload. */
  virtual bool encodeControlCommand(uint16_t cmd_id,
                                    std::vector<uint8_t>& out_payload) const = 0;
  /** @brief Decode control command payload. */
  virtual bool decodeControlCommand(const uint8_t* payload,
                                    size_t len,
                                    uint16_t& out_cmd_id) const = 0;

  /** @brief Encode control result payload. */
  virtual bool encodeControlResult(uint16_t cmd_id,
                                   uint16_t result_code,
                                   std::vector<uint8_t>& out_payload) const = 0;
  /** @brief Decode control result payload. */
  virtual bool decodeControlResult(const uint8_t* payload,
                                   size_t len,
                                   uint16_t& out_cmd_id,
                                   uint16_t& out_result_code) const = 0;
};

/** @brief Built-in default TLV codec implementation. */
class DefaultProfileCodec : public IProfileCodec {
 public:
  CodecId codecId() const override { return kCodecIdDefault; }
  const char* codecName() const override { return "default_tlv_v1"; }

  bool encodeDescriptorQuery(const DescriptorQuery& query,
                             std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorQuery(const uint8_t* payload,
                             size_t len,
                             DescriptorQuery& out) const override;

  bool encodeDescriptorResponse(const DescriptorResponse& response,
                                std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorResponse(const uint8_t* payload,
                                size_t len,
                                DescriptorResponse& out) const override;

  bool encodeControlCommand(uint16_t cmd_id,
                            std::vector<uint8_t>& out_payload) const override;
  bool decodeControlCommand(const uint8_t* payload,
                            size_t len,
                            uint16_t& out_cmd_id) const override;

  bool encodeControlResult(uint16_t cmd_id,
                           uint16_t result_code,
                           std::vector<uint8_t>& out_payload) const override;
  bool decodeControlResult(const uint8_t* payload,
                           size_t len,
                           uint16_t& out_cmd_id,
                           uint16_t& out_result_code) const override;
};

/** @brief Built-in compact indexed codec implementation. */
class CompactIndexedProfileCodec : public IProfileCodec {
 public:
  CodecId codecId() const override { return kCodecIdCompactIndexed; }
  const char* codecName() const override { return "compact_indexed_v1"; }

  bool encodeDescriptorQuery(const DescriptorQuery& query,
                             std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorQuery(const uint8_t* payload,
                             size_t len,
                             DescriptorQuery& out) const override;

  bool encodeDescriptorResponse(const DescriptorResponse& response,
                                std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorResponse(const uint8_t* payload,
                                size_t len,
                                DescriptorResponse& out) const override;

  bool encodeControlCommand(uint16_t cmd_id,
                            std::vector<uint8_t>& out_payload) const override;
  bool decodeControlCommand(const uint8_t* payload,
                            size_t len,
                            uint16_t& out_cmd_id) const override;

  bool encodeControlResult(uint16_t cmd_id,
                           uint16_t result_code,
                           std::vector<uint8_t>& out_payload) const override;
  bool decodeControlResult(const uint8_t* payload,
                           size_t len,
                           uint16_t& out_cmd_id,
                           uint16_t& out_result_code) const override;
};

/** @brief Built-in packed TLV codec implementation. */
class PackedTlvProfileCodec : public IProfileCodec {
 public:
  CodecId codecId() const override { return kCodecIdPackedTlv; }
  const char* codecName() const override { return "packed_tlv_v1"; }

  bool encodeDescriptorQuery(const DescriptorQuery& query,
                             std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorQuery(const uint8_t* payload,
                             size_t len,
                             DescriptorQuery& out) const override;

  bool encodeDescriptorResponse(const DescriptorResponse& response,
                                std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorResponse(const uint8_t* payload,
                                size_t len,
                                DescriptorResponse& out) const override;

  bool encodeControlCommand(uint16_t cmd_id,
                            std::vector<uint8_t>& out_payload) const override;
  bool decodeControlCommand(const uint8_t* payload,
                            size_t len,
                            uint16_t& out_cmd_id) const override;

  bool encodeControlResult(uint16_t cmd_id,
                           uint16_t result_code,
                           std::vector<uint8_t>& out_payload) const override;
  bool decodeControlResult(const uint8_t* payload,
                           size_t len,
                           uint16_t& out_cmd_id,
                           uint16_t& out_result_code) const override;
};

/** @brief Built-in varint TLV codec implementation. */
class VarintTlvProfileCodec : public IProfileCodec {
 public:
  CodecId codecId() const override { return kCodecIdVarintTlv; }
  const char* codecName() const override { return "varint_tlv_v1"; }

  bool encodeDescriptorQuery(const DescriptorQuery& query,
                             std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorQuery(const uint8_t* payload,
                             size_t len,
                             DescriptorQuery& out) const override;

  bool encodeDescriptorResponse(const DescriptorResponse& response,
                                std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorResponse(const uint8_t* payload,
                                size_t len,
                                DescriptorResponse& out) const override;

  bool encodeControlCommand(uint16_t cmd_id,
                            std::vector<uint8_t>& out_payload) const override;
  bool decodeControlCommand(const uint8_t* payload,
                            size_t len,
                            uint16_t& out_cmd_id) const override;

  bool encodeControlResult(uint16_t cmd_id,
                           uint16_t result_code,
                           std::vector<uint8_t>& out_payload) const override;
  bool decodeControlResult(const uint8_t* payload,
                           size_t len,
                           uint16_t& out_cmd_id,
                           uint16_t& out_result_code) const override;
};

/** @brief Built-in delta-binary codec implementation. */
class DeltaBinaryProfileCodec : public IProfileCodec {
 public:
  CodecId codecId() const override { return kCodecIdDeltaBinary; }
  const char* codecName() const override { return "delta_binary_v1"; }

  bool encodeDescriptorQuery(const DescriptorQuery& query,
                             std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorQuery(const uint8_t* payload,
                             size_t len,
                             DescriptorQuery& out) const override;

  bool encodeDescriptorResponse(const DescriptorResponse& response,
                                std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorResponse(const uint8_t* payload,
                                size_t len,
                                DescriptorResponse& out) const override;

  bool encodeControlCommand(uint16_t cmd_id,
                            std::vector<uint8_t>& out_payload) const override;
  bool decodeControlCommand(const uint8_t* payload,
                            size_t len,
                            uint16_t& out_cmd_id) const override;

  bool encodeControlResult(uint16_t cmd_id,
                           uint16_t result_code,
                           std::vector<uint8_t>& out_payload) const override;
  bool decodeControlResult(const uint8_t* payload,
                           size_t len,
                           uint16_t& out_cmd_id,
                           uint16_t& out_result_code) const override;
};

/** @brief Built-in CBOR-lite codec implementation. */
class CborLiteProfileCodec : public IProfileCodec {
 public:
  CodecId codecId() const override { return kCodecIdCborLite; }
  const char* codecName() const override { return "cbor_lite_v1"; }

  bool encodeDescriptorQuery(const DescriptorQuery& query,
                             std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorQuery(const uint8_t* payload,
                             size_t len,
                             DescriptorQuery& out) const override;

  bool encodeDescriptorResponse(const DescriptorResponse& response,
                                std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorResponse(const uint8_t* payload,
                                size_t len,
                                DescriptorResponse& out) const override;

  bool encodeControlCommand(uint16_t cmd_id,
                            std::vector<uint8_t>& out_payload) const override;
  bool decodeControlCommand(const uint8_t* payload,
                            size_t len,
                            uint16_t& out_cmd_id) const override;

  bool encodeControlResult(uint16_t cmd_id,
                           uint16_t result_code,
                           std::vector<uint8_t>& out_payload) const override;
  bool decodeControlResult(const uint8_t* payload,
                           size_t len,
                           uint16_t& out_cmd_id,
                           uint16_t& out_result_code) const override;
};

/** @brief Built-in binary-map codec implementation. */
class BinaryMapProfileCodec : public IProfileCodec {
 public:
  CodecId codecId() const override { return kCodecIdBinaryMap; }
  const char* codecName() const override { return "binary_map_v1"; }

  bool encodeDescriptorQuery(const DescriptorQuery& query,
                             std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorQuery(const uint8_t* payload,
                             size_t len,
                             DescriptorQuery& out) const override;

  bool encodeDescriptorResponse(const DescriptorResponse& response,
                                std::vector<uint8_t>& out_payload) const override;
  bool decodeDescriptorResponse(const uint8_t* payload,
                                size_t len,
                                DescriptorResponse& out) const override;

  bool encodeControlCommand(uint16_t cmd_id,
                            std::vector<uint8_t>& out_payload) const override;
  bool decodeControlCommand(const uint8_t* payload,
                            size_t len,
                            uint16_t& out_cmd_id) const override;

  bool encodeControlResult(uint16_t cmd_id,
                           uint16_t result_code,
                           std::vector<uint8_t>& out_payload) const override;
  bool decodeControlResult(const uint8_t* payload,
                           size_t len,
                           uint16_t& out_cmd_id,
                           uint16_t& out_result_code) const override;
};

/**
 * @brief Runtime registry for available codec implementations.
 */
class CodecRegistry {
 public:
  /** @brief Get singleton codec registry instance. */
  static CodecRegistry& instance();

  /**
   * @brief Register one codec instance.
   * @param codec Codec pointer with static lifetime.
   * @return true on success.
   */
  bool registerCodec(IProfileCodec* codec);
  /**
   * @brief Find codec by ID.
   * @param codec_id Codec identifier.
   * @return Codec pointer or nullptr.
   */
  IProfileCodec* find(CodecId codec_id) const;
  /**
   * @brief Find codec by name.
   * @param codec_name Codec name string.
   * @return Codec pointer or nullptr.
   */
  IProfileCodec* findByName(const std::string& codec_name) const;
  /**
   * @brief List all registered codecs.
   * @return Vector of registered codec pointers.
   */
  std::vector<IProfileCodec*> list() const;

 private:
  std::vector<IProfileCodec*> codecs_{};
};

/**
 * @brief Register all built-in codec implementations.
 * @param registry Target registry.
 *
 * Safe to call multiple times.
 */
void registerBuiltInCodecs(CodecRegistry& registry);

}  // namespace espnow_link
