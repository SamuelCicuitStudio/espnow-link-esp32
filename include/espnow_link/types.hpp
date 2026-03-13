#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace espnow_link {

using MacAddress = std::array<uint8_t, 6>;
using LmkKey = std::array<uint8_t, 16>;

enum class Role : uint8_t {
  Master = 1,
  Slave = 2,
};

enum class MessageType : uint8_t {
  Discovery = 0x60,
  PairInit = 0x61,
  PairInitAck = 0x62,
  PairConfirm = 0x63,
  PairConfirmAck = 0x64,
  PairBusy = 0x65,
  PullRequest = 0x70,
  PullResponse = 0x71,
  ChannelSwitchPrepare = 0x72,
  ChannelSwitchAck = 0x73,
  ChannelSwitchCommitAck = 0x74,
  FirmwareBegin = 0x75,
  FirmwareChunk = 0x76,
  FirmwareEnd = 0x77,
  UnpairRequest = 0x78,
  UnpairAck = 0x79,
  EventReport = 0x7A,
  FirmwareStatus = 0x7B,
  TopologyTrigger = 0x7C,
  TopologyTriggerAck = 0x7D,
  Error = 0x7F,
};

enum class LinkState : uint8_t {
  Unpaired = 0,
  Pairing = 1,
  Secured = 2,
};

struct FrameHeader {
  uint8_t version = 2;
  MessageType type = MessageType::Error;
  uint8_t flags = 0;
  uint32_t correlation_id = 0;
  Role role = Role::Slave;
  uint8_t wire_msg_type = 0;
  uint8_t wire_service = 0;
  uint8_t wire_op_code = 0;
  uint16_t payload_length = 0;
};

struct PairSeed {
  std::array<uint8_t, 16> bytes{};
};

struct ChannelSwitchPayload {
  uint8_t new_channel = 1;
  uint32_t effective_at_ms = 0;
};

struct FirmwareBeginPayload {
  uint32_t total_size = 0;
  uint32_t chunk_size = 0;
  uint32_t image_crc32 = 0;
};

struct FirmwareEndPayload {
  uint32_t total_size = 0;
  uint32_t image_crc32 = 0;
};

constexpr uint8_t kProtocolVersion = 2;
constexpr uint8_t kProtocolMagic = 0xE2;
constexpr uint8_t kFrameFlagTimeSyncEpoch = 0x01;
constexpr size_t kTimeSyncMetadataSize = 4;
constexpr MacAddress kBroadcastMac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace espnow_link
