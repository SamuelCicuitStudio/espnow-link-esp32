#include "espnow_link/protocol.hpp"

namespace espnow_link {

namespace {

enum class WireMsgType : uint8_t {
  Request = 0x01,
  ResponseOk = 0x02,
  ResponseErr = 0x03,
  Event = 0x04,
  AckOnly = 0x05,
};

enum class WireService : uint8_t {
  Pairing = 0x01,
  Descriptor = 0x02,
  Settings = 0x03,
  Telemetry = 0x04,
  Liveness = 0x05,
  Control = 0x06,
  Firmware = 0x07,
  Channel = 0x08,
  Discovery = 0x09,
};

constexpr uint8_t kControlOpPullRequest = 0x01;
constexpr uint8_t kControlOpPullResponse = 0x02;
constexpr uint8_t kControlOpEventReport = 0x10;
constexpr uint8_t kControlOpTopologyTrigger = 0x11;
constexpr uint8_t kControlOpTopologyTriggerAck = 0x12;
constexpr uint8_t kControlOpTopologyTriggerBatch = 0x13;

bool toWire(MessageType in, WireMsgType& msg, WireService& service, uint8_t& op) {
  switch (in) {
    case MessageType::Discovery:
      msg = WireMsgType::Event;
      service = WireService::Discovery;
      op = 0x01;
      return true;
    case MessageType::PairInit:
      msg = WireMsgType::Request;
      service = WireService::Pairing;
      op = 0x01;
      return true;
    case MessageType::PairInitAck:
      msg = WireMsgType::ResponseOk;
      service = WireService::Pairing;
      op = 0x02;
      return true;
    case MessageType::PairConfirm:
      msg = WireMsgType::Request;
      service = WireService::Pairing;
      op = 0x03;
      return true;
    case MessageType::PairConfirmAck:
      msg = WireMsgType::ResponseOk;
      service = WireService::Pairing;
      op = 0x04;
      return true;
    case MessageType::PairBusy:
      msg = WireMsgType::ResponseErr;
      service = WireService::Pairing;
      op = 0x07;
      return true;
    case MessageType::UnpairRequest:
      msg = WireMsgType::Request;
      service = WireService::Pairing;
      op = 0x05;
      return true;
    case MessageType::UnpairAck:
      msg = WireMsgType::ResponseOk;
      service = WireService::Pairing;
      op = 0x06;
      return true;
    case MessageType::ChannelSwitchPrepare:
      msg = WireMsgType::Request;
      service = WireService::Channel;
      op = 0x01;
      return true;
    case MessageType::ChannelSwitchAck:
      msg = WireMsgType::ResponseOk;
      service = WireService::Channel;
      op = 0x02;
      return true;
    case MessageType::ChannelSwitchCommitAck:
      msg = WireMsgType::ResponseOk;
      service = WireService::Channel;
      op = 0x03;
      return true;
    case MessageType::FirmwareBegin:
      msg = WireMsgType::Request;
      service = WireService::Firmware;
      op = 0x01;
      return true;
    case MessageType::FirmwareChunk:
      msg = WireMsgType::Request;
      service = WireService::Firmware;
      op = 0x02;
      return true;
    case MessageType::FirmwareEnd:
      msg = WireMsgType::Request;
      service = WireService::Firmware;
      op = 0x03;
      return true;
    case MessageType::FirmwareStatus:
      msg = WireMsgType::ResponseOk;
      service = WireService::Firmware;
      op = 0x04;
      return true;
    case MessageType::PullRequest:
      msg = WireMsgType::Request;
      service = WireService::Control;
      op = kControlOpPullRequest;
      return true;
    case MessageType::PullResponse:
      msg = WireMsgType::ResponseOk;
      service = WireService::Control;
      op = kControlOpPullResponse;
      return true;
    case MessageType::EventReport:
      msg = WireMsgType::Event;
      service = WireService::Control;
      op = kControlOpEventReport;
      return true;
    case MessageType::TopologyTrigger:
      msg = WireMsgType::Event;
      service = WireService::Control;
      op = kControlOpTopologyTrigger;
      return true;
    case MessageType::TopologyTriggerAck:
      msg = WireMsgType::AckOnly;
      service = WireService::Control;
      op = kControlOpTopologyTriggerAck;
      return true;
    case MessageType::TopologyTriggerBatch:
      msg = WireMsgType::Event;
      service = WireService::Control;
      op = kControlOpTopologyTriggerBatch;
      return true;
    default:
      break;
  }

  msg = WireMsgType::ResponseErr;
  service = WireService::Control;
  op = 0x00;
  return false;
}

bool fromWire(WireMsgType msg, WireService service, uint8_t op, MessageType& out) {
  if (service == WireService::Discovery && msg == WireMsgType::Event && op == 0x01) {
    out = MessageType::Discovery;
    return true;
  }

  if (service == WireService::Pairing) {
    if (msg == WireMsgType::Request && op == 0x01) {
      out = MessageType::PairInit;
      return true;
    }
    if (msg == WireMsgType::ResponseOk && op == 0x02) {
      out = MessageType::PairInitAck;
      return true;
    }
    if (msg == WireMsgType::Request && op == 0x03) {
      out = MessageType::PairConfirm;
      return true;
    }
    if (msg == WireMsgType::ResponseOk && op == 0x04) {
      out = MessageType::PairConfirmAck;
      return true;
    }
    if (msg == WireMsgType::Request && op == 0x05) {
      out = MessageType::UnpairRequest;
      return true;
    }
    if (msg == WireMsgType::ResponseOk && op == 0x06) {
      out = MessageType::UnpairAck;
      return true;
    }
    if (msg == WireMsgType::ResponseErr && op == 0x07) {
      out = MessageType::PairBusy;
      return true;
    }
  }

  if (service == WireService::Channel) {
    if (msg == WireMsgType::Request && op == 0x01) {
      out = MessageType::ChannelSwitchPrepare;
      return true;
    }
    if (msg == WireMsgType::ResponseOk && op == 0x02) {
      out = MessageType::ChannelSwitchAck;
      return true;
    }
    if (msg == WireMsgType::ResponseOk && op == 0x03) {
      out = MessageType::ChannelSwitchCommitAck;
      return true;
    }
  }

  if (service == WireService::Firmware) {
    if (msg == WireMsgType::Request) {
      if (op == 0x01) {
        out = MessageType::FirmwareBegin;
        return true;
      }
      if (op == 0x02) {
        out = MessageType::FirmwareChunk;
        return true;
      }
      if (op == 0x03) {
        out = MessageType::FirmwareEnd;
        return true;
      }
    }
    if (msg == WireMsgType::ResponseOk && op == 0x04) {
      out = MessageType::FirmwareStatus;
      return true;
    }
  }

  if (service == WireService::Control) {
    if (msg == WireMsgType::Event && op == kControlOpEventReport) {
      out = MessageType::EventReport;
      return true;
    }
    if (msg == WireMsgType::Event && op == kControlOpTopologyTrigger) {
      out = MessageType::TopologyTrigger;
      return true;
    }
    if (msg == WireMsgType::Event && op == kControlOpTopologyTriggerBatch) {
      out = MessageType::TopologyTriggerBatch;
      return true;
    }
    if (msg == WireMsgType::AckOnly && op == kControlOpTopologyTriggerAck) {
      out = MessageType::TopologyTriggerAck;
      return true;
    }
  }

  if (service == WireService::Descriptor ||
      service == WireService::Settings ||
      service == WireService::Telemetry ||
      service == WireService::Liveness ||
      service == WireService::Control) {
    if (msg == WireMsgType::Request &&
        (op == 0x01 || op == 0x03 || op == 0x05)) {
      out = MessageType::PullRequest;
      return true;
    }
    if ((msg == WireMsgType::ResponseOk || msg == WireMsgType::ResponseErr) &&
        (op == 0x02 || op == 0x04 || op == 0x06)) {
      out = MessageType::PullResponse;
      return true;
    }
  }

  out = MessageType::Error;
  return false;
}

uint16_t crc16CcittFalse(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

}  // namespace

bool ProtocolCodec::encode(const FrameHeader& header,
                           const uint8_t* payload,
                           size_t payload_len,
                           std::vector<uint8_t>& out) {
  if (payload_len > kMaxPayload) {
    return false;
  }

  if (payload_len > 0 && payload == nullptr) {
    return false;
  }

  WireMsgType wire_msg = WireMsgType::ResponseErr;
  WireService wire_service = WireService::Control;
  uint8_t wire_op = 0;
  if (!toWire(header.type, wire_msg, wire_service, wire_op)) {
    return false;
  }
  if (header.wire_msg_type >= 0x01 && header.wire_msg_type <= 0x05) {
    wire_msg = static_cast<WireMsgType>(header.wire_msg_type);
  }
  if (header.wire_service != 0) {
    wire_service = static_cast<WireService>(header.wire_service);
  }
  if (header.wire_op_code != 0) {
    wire_op = header.wire_op_code;
  }

  out.clear();
  out.reserve(kHeaderSize + payload_len + kCrcSize);

  out.push_back(kProtocolMagic);
  out.push_back(kProtocolVersion);
  out.push_back(static_cast<uint8_t>(wire_msg));
  out.push_back(header.flags);
  out.push_back(static_cast<uint8_t>(header.correlation_id & 0xFF));
  out.push_back(static_cast<uint8_t>((header.correlation_id >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((header.correlation_id >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((header.correlation_id >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>(header.role));
  out.push_back(static_cast<uint8_t>(wire_service));
  out.push_back(wire_op);
  out.push_back(static_cast<uint8_t>(payload_len & 0xFF));
  out.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xFF));

  if (payload_len > 0) {
    out.insert(out.end(), payload, payload + payload_len);
  }

  const uint16_t crc = crc16CcittFalse(out.data(), out.size());
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

  return out.size() <= kMaxFrameBytes;
}

bool ProtocolCodec::decode(const uint8_t* data,
                           size_t len,
                           FrameHeader& out_header,
                           const uint8_t*& out_payload,
                           size_t& out_payload_len) {
  if (data == nullptr || len < (kHeaderSize + kCrcSize)) {
    return false;
  }

  if (data[0] != kProtocolMagic || data[1] != kProtocolVersion) {
    return false;
  }

  const uint16_t rx_crc = static_cast<uint16_t>(data[len - 2]) |
                          (static_cast<uint16_t>(data[len - 1]) << 8);
  const uint16_t calc_crc = crc16CcittFalse(data, len - 2);
  if (rx_crc != calc_crc) {
    return false;
  }

  const auto wire_msg = static_cast<WireMsgType>(data[2]);
  const auto wire_service = static_cast<WireService>(data[9]);
  const uint8_t wire_op = data[10];
  const uint16_t payload_len = static_cast<uint16_t>(data[11]) |
                               (static_cast<uint16_t>(data[12]) << 8);

  if (payload_len > kMaxPayload) {
    return false;
  }

  if (len != static_cast<size_t>(kHeaderSize + payload_len + kCrcSize)) {
    return false;
  }

  MessageType mapped = MessageType::Error;
  if (!fromWire(wire_msg, wire_service, wire_op, mapped)) {
    return false;
  }

  out_header.version = data[1];
  out_header.type = mapped;
  out_header.flags = data[3];
  out_header.correlation_id = static_cast<uint32_t>(data[4]) |
                              (static_cast<uint32_t>(data[5]) << 8) |
                              (static_cast<uint32_t>(data[6]) << 16) |
                              (static_cast<uint32_t>(data[7]) << 24);
  out_header.role = static_cast<Role>(data[8]);
  out_header.wire_msg_type = static_cast<uint8_t>(wire_msg);
  out_header.wire_service = static_cast<uint8_t>(wire_service);
  out_header.wire_op_code = wire_op;
  out_header.payload_length = payload_len;

  out_payload = data + kHeaderSize;
  out_payload_len = payload_len;
  return true;
}

}  // namespace espnow_link
