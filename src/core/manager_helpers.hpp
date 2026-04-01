#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/manager.hpp"

namespace espnow_link {
namespace manager_helpers {

uint64_t monotonicUs();

class ScopedMetricsDuration {
 public:
  ScopedMetricsDuration(uint64_t* total_us, uint32_t* last_us, uint32_t* max_us);
  ~ScopedMetricsDuration();

 private:
  uint64_t* total_us_;
  uint32_t* last_us_;
  uint32_t* max_us_;
  uint64_t start_us_;
};

bool parseChannelSwitchPayload(const uint8_t* payload, size_t len, ChannelSwitchPayload& out);
bool parsePairInitPayload(const uint8_t* payload,
                          size_t len,
                          PairSeed& out_seed,
                          uint32_t& out_nonce,
                          uint8_t& out_profile);
bool parseNonceEchoPayload(const uint8_t* payload, size_t len, uint8_t expected_tag, uint32_t& out_nonce);
bool parsePairConfirmAckPayload(const uint8_t* payload, size_t len, bool& out_paired);

uint32_t readLe32(const uint8_t* p);
int32_t readLeI32(const uint8_t* p);
void appendLe16(std::vector<uint8_t>& out, uint16_t v);
void appendLe32(std::vector<uint8_t>& out, uint32_t v);

bool buildMandatoryEventPayload(uint16_t event_id,
                                uint8_t severity,
                                int32_t value,
                                uint32_t event_ts_s,
                                std::vector<uint8_t>& out_payload);
bool parseMandatoryEventPayload(const uint8_t* payload,
                                size_t len,
                                uint16_t& out_event_id,
                                uint8_t& out_severity,
                                int32_t& out_value,
                                uint32_t& out_event_ts_s);

/** @brief Topology lateral wire protocol discriminator. */
constexpr uint8_t kTopologyProtoId = 0xED;
/** @brief Topology lateral wire protocol version (`L2P v1`). */
constexpr uint8_t kTopologyProtoVersion = 1;
/** @brief Topology lateral trigger command message kind. */
constexpr uint8_t kTopologyMsgTrigger = 0x01;
/** @brief Topology lateral trigger ack message kind. */
constexpr uint8_t kTopologyMsgAck = 0x02;
/** @brief Topology lateral trigger batch message kind. */
constexpr uint8_t kTopologyMsgTriggerBatch = 0x03;

/** @brief L2 trigger command payload fields. */
struct TopologyTriggerPayload {
  uint32_t topology_version = 0;
  uint16_t seq = 0;
  uint8_t src_role = 0;
  uint8_t src_vid = 0xFF;
  uint8_t dst_role = 0;
  uint8_t dst_vid = 0xFF;
  uint8_t direction = 0;
  uint16_t delay_ms = 0;
  uint16_t hold_ms = 0;
};

/** @brief One entry in an L2 trigger batch command payload. */
struct TopologyTriggerBatchItem {
  uint8_t dst_vid = 0xFF;
  int8_t target_index = 0;
  uint16_t delay_ms = 0;
  uint16_t hold_ms = 0;
};

/** @brief L2 trigger batch command payload fields. */
struct TopologyTriggerBatchPayload {
  uint32_t topology_version = 0;
  uint16_t seq = 0;
  uint8_t src_role = 0;
  uint8_t src_vid = 0xFF;
  uint8_t dst_role = 0;
  uint8_t direction = 0;
  std::vector<TopologyTriggerBatchItem> items{};
};

/** @brief L2 trigger ack payload fields. */
struct TopologyTriggerAckPayload {
  uint32_t topology_version = 0;
  uint16_t seq = 0;
  uint8_t src_role = 0;
  uint8_t src_vid = 0xFF;
  uint8_t dst_role = 0;
  uint8_t dst_vid = 0xFF;
  uint16_t ack_seq = 0;
  uint8_t result = 1;  // 0=accepted, 1=rejected
  uint8_t reason = 0;
};

bool buildTopologyTriggerPayload(const TopologyTriggerPayload& in,
                                 std::vector<uint8_t>& out_payload);
bool parseTopologyTriggerPayload(const uint8_t* payload,
                                 size_t len,
                                 TopologyTriggerPayload& out);
bool buildTopologyTriggerBatchPayload(const TopologyTriggerBatchPayload& in,
                                      std::vector<uint8_t>& out_payload);
bool parseTopologyTriggerBatchPayload(const uint8_t* payload,
                                      size_t len,
                                      TopologyTriggerBatchPayload& out);
bool buildTopologyTriggerAckPayload(const TopologyTriggerAckPayload& in,
                                    std::vector<uint8_t>& out_payload);
bool parseTopologyTriggerAckPayload(const uint8_t* payload,
                                    size_t len,
                                    TopologyTriggerAckPayload& out);

bool buildFirmwareStatusPayload(uint8_t kind,
                                uint32_t transfer_corr_id,
                                uint32_t offset_bytes,
                                uint16_t status_code,
                                std::vector<uint8_t>& out_payload);
bool parseFirmwareStatusPayload(const uint8_t* payload,
                                size_t len,
                                uint8_t& out_kind,
                                uint32_t& out_transfer_corr_id,
                                uint32_t& out_offset_bytes,
                                uint16_t& out_status_code);

bool classifyPullRequest(const uint8_t* payload, size_t len, uint8_t& out_service, uint8_t& out_op);
bool isValidPullRequestWire(uint8_t service, uint8_t op);
bool isValidPullResponseWire(uint8_t service, uint8_t op);
uint8_t responseOpForRequest(uint8_t service, uint8_t request_op);

std::string decodeDiscoveryName(const uint8_t* payload, size_t payload_len);
uint8_t decodeDiscoveryRoleCode(const uint8_t* payload, size_t payload_len);
}  // namespace manager_helpers
}  // namespace espnow_link



