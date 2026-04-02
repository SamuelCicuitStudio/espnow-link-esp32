/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Outbound send wrappers and TX-side policy helpers.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
bool EspNowManager::sendPullRequest(const MacAddress& to,
                                    const uint8_t* payload,
                                    size_t len,
                                    uint32_t corr_id) {
  if (config_.local_role != Role::Master) {
    return false;
  }

  uint8_t wire_service = 0x06;
  uint8_t wire_op = 0x01;
  (void)classifyPullRequest(payload, len, wire_service, wire_op);
  if (!isValidPullRequestWire(wire_service, wire_op)) {
    wire_service = 0x06;
    wire_op = 0x01;
  }
  return sendTyped(to, MessageType::PullRequest, payload, len, corr_id, wire_service, wire_op, 0x01);
}

bool EspNowManager::sendPullResponse(const MacAddress& to,
                                     const uint8_t* payload,
                                     size_t len,
                                     uint32_t corr_id) {
  if (config_.master_initiated_only && config_.local_role != Role::Slave) {
    return false;
  }

  uint8_t wire_service = 0x06;
  uint8_t wire_op = 0x02;
  if (pending_pull_.valid && pending_pull_.peer == to && pending_pull_.corr_id == corr_id) {
    wire_service = pending_pull_.service;
    wire_op = responseOpForRequest(pending_pull_.service, pending_pull_.op);
    pending_pull_.valid = false;
  }
  if (!isValidPullResponseWire(wire_service, wire_op)) {
    wire_service = 0x06;
    wire_op = 0x02;
  }

  return sendTyped(to, MessageType::PullResponse, payload, len, corr_id, wire_service, wire_op, 0x02);
}

bool EspNowManager::sendFirmwareBegin(const MacAddress& to,
                                      uint32_t total_size,
                                      uint32_t chunk_size,
                                      uint32_t image_crc32,
                                      uint32_t corr_id,
                                      const FirmwareImageMetadata* metadata) {
  if (config_.local_role != Role::Master || total_size == 0 || chunk_size == 0) {
    return false;
  }

  FirmwareImageMetadata meta{};
  if (metadata != nullptr) {
    meta = *metadata;
  }
  if (meta.sw_version.size() > 63U || meta.build_id.size() > 63U || meta.target_role.size() > 15U) {
    return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(12U + kFirmwareMetaHeaderSizeV2 + meta.sw_version.size() + meta.build_id.size() +
                  meta.target_role.size());
  appendLe32(payload, total_size);
  appendLe32(payload, chunk_size);
  appendLe32(payload, image_crc32);
  if (!meta.empty()) {
    const bool use_v2 = !meta.target_role.empty();
    payload.push_back(kFirmwareMetaMagic);
    payload.push_back(use_v2 ? kFirmwareMetaVersionV2 : kFirmwareMetaVersionV1);
    payload.push_back(static_cast<uint8_t>(meta.sw_version.size() & 0xFFU));
    payload.push_back(static_cast<uint8_t>(meta.build_id.size() & 0xFFU));
    if (use_v2) {
      payload.push_back(static_cast<uint8_t>(meta.target_role.size() & 0xFFU));
    }
    payload.insert(payload.end(), meta.sw_version.begin(), meta.sw_version.end());
    payload.insert(payload.end(), meta.build_id.begin(), meta.build_id.end());
    if (use_v2) {
      payload.insert(payload.end(), meta.target_role.begin(), meta.target_role.end());
    }
  }

  return sendTyped(to, MessageType::FirmwareBegin, payload.data(), payload.size(), corr_id, 0x07, 0x01, 0x01);
}

bool EspNowManager::sendFirmwareChunk(const MacAddress& to,
                                      uint32_t offset,
                                      const uint8_t* data,
                                      size_t len,
                                      uint32_t corr_id) {
  if (config_.local_role != Role::Master || data == nullptr || len == 0) {
    return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(4 + len);
  appendLe32(payload, offset);
  payload.insert(payload.end(), data, data + len);

  return sendTyped(to, MessageType::FirmwareChunk, payload.data(), payload.size(), corr_id, 0x07, 0x02, 0x01);
}

bool EspNowManager::sendFirmwareEnd(const MacAddress& to,
                                    uint32_t total_size,
                                    uint32_t image_crc32,
                                    uint32_t corr_id) {
  if (config_.local_role != Role::Master || total_size == 0) {
    return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(8);
  appendLe32(payload, total_size);
  appendLe32(payload, image_crc32);

  return sendTyped(to, MessageType::FirmwareEnd, payload.data(), payload.size(), corr_id, 0x07, 0x03, 0x01);
}

bool EspNowManager::sendFirmwareFinalizeAck(const MacAddress& to,
                                            uint32_t corr_id,
                                            uint32_t finalized_offset,
                                            uint16_t status_code) {
  return sendFirmwareStatus(to,
                            corr_id,
                            kFirmwareStatusKindFinalizeAck,
                            finalized_offset,
                            status_code);
}

bool EspNowManager::sendFirmwareStatus(const MacAddress& to,
                                       uint32_t corr_id,
                                       uint8_t kind,
                                       uint32_t offset_bytes,
                                       uint16_t status_code) {
  std::vector<uint8_t> payload;
  if (!buildFirmwareStatusPayload(kind, corr_id, offset_bytes, status_code, payload)) {
    return false;
  }
  return sendTyped(to, MessageType::FirmwareStatus, payload.data(), payload.size(), corr_id, 0x07, 0x04, 0x02);
}

bool EspNowManager::sendTelemetryPushCommand(const MacAddress& to,
                                             const TelemetryPushCommand& cmd,
                                             uint32_t corr_id) {
  if (config_.local_role != Role::Master) {
    return false;
  }

  std::vector<uint8_t> payload;
  if (!encodeTelemetryPushCommand(cmd, payload)) {
    return false;
  }

  return sendTyped(to,
                   MessageType::PullRequest,
                   payload.data(),
                   payload.size(),
                   corr_id,
                   0x04,
                   0x01,
                   0x01);
}

bool EspNowManager::requestChannelChange(const MacAddress& peer,
                                         uint8_t new_channel,
                                         uint32_t now_ms,
                                         uint32_t corr_id) {
  return pairing_.requestChannelSwitch(peer, new_channel, now_ms, corr_id);
}

bool EspNowManager::sendTyped(const MacAddress& to,
                              MessageType type,
                              const uint8_t* payload,
                              size_t len,
                              uint32_t corr_id,
                              uint8_t wire_service,
                              uint8_t wire_op,
                              uint8_t wire_msg_type,
                              std::string* out_error) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ScopedMetricsDuration tx_duration(&metrics_.tx_send_total_us,
                                    &metrics_.tx_send_last_us,
                                    &metrics_.tx_send_max_us);
#endif
  FrameHeader h;
  h.version = kProtocolVersion;
  h.type = type;
  h.flags = 0;
  h.correlation_id = corr_id;
  h.role = config_.local_role;
  h.wire_service = wire_service;
  h.wire_op_code = wire_op;
  h.wire_msg_type = wire_msg_type;

  tx_wrapped_payload_scratch_.clear();
  (void)appendTimeSyncMetadata(payload, len, h.flags, tx_wrapped_payload_scratch_);

  const uint8_t* wire_payload = payload;
  size_t wire_len = len;
  if (!tx_wrapped_payload_scratch_.empty()) {
    wire_payload = tx_wrapped_payload_scratch_.data();
    wire_len = tx_wrapped_payload_scratch_.size();
  }

  h.payload_length = static_cast<uint16_t>(wire_len);

  tx_encoded_frame_scratch_.clear();
  if (!ProtocolCodec::encode(h, wire_payload, wire_len, tx_encoded_frame_scratch_)) {
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
    ++metrics_.tx_failures;
#endif
    if (out_error != nullptr) {
      *out_error = "encode_failed";
    }
    if (hooks_ != nullptr) {
      hooks_->onTxFrame(to, type, corr_id, wire_len, false);
    }
    emitRuntimeLog(LibraryLogLevel::Error,
                   kLogEvtTxEncodeFail,
                   corr_id,
                   static_cast<int32_t>(type),
                   static_cast<int32_t>(wire_len),
                   to.data(),
                   to.size());
    return false;
  }

#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  ++metrics_.tx_frames;
  metrics_.tx_bytes += static_cast<uint64_t>(tx_encoded_frame_scratch_.size());
#endif

  const bool ok = transport_.send(to,
                                  tx_encoded_frame_scratch_.data(),
                                  tx_encoded_frame_scratch_.size());
#if ESPNOW_LINK_ENABLE_RUNTIME_METRICS
  if (!ok) {
    ++metrics_.tx_failures;
  }
#endif
  const bool suppress_tx_hook = isOtaWireMessage(type);
  if (hooks_ != nullptr && !suppress_tx_hook) {
    hooks_->onTxFrame(to, type, corr_id, wire_len, ok);
  }
  if (!ok) {
    if (out_error != nullptr) {
      *out_error = "transport_send_failed";
    }
    emitRuntimeLog(LibraryLogLevel::Error,
                   kLogEvtTxSendFail,
                   corr_id,
                   static_cast<int32_t>(type),
                   static_cast<int32_t>(wire_len),
                   to.data(),
                   to.size());
  }
  if (ok && out_error != nullptr) {
    *out_error = "ok";
  }
  return ok;
}

void EspNowManager::blinkForMessage(MessageType type) {
  if (hooks_ == nullptr) {
    return;
  }

  switch (type) {
    case MessageType::Discovery:
      hooks_->onRgbBlink(0, 0, 255, 50);
      break;
    case MessageType::PairInit:
    case MessageType::PairInitAck:
    case MessageType::PairConfirm:
    case MessageType::PairConfirmAck:
      hooks_->onRgbBlink(255, 165, 0, 80);
      break;
    case MessageType::UnpairRequest:
    case MessageType::PairBusy:
    case MessageType::UnpairAck:
      hooks_->onRgbBlink(255, 0, 0, 120);
      break;
    case MessageType::ChannelSwitchPrepare:
    case MessageType::ChannelSwitchAck:
    case MessageType::ChannelSwitchCommitAck:
      hooks_->onRgbBlink(255, 0, 255, 100);
      break;
    case MessageType::PullRequest:
    case MessageType::PullResponse:
      hooks_->onRgbBlink(0, 255, 0, 40);
      break;
    case MessageType::EventReport:
      hooks_->onRgbBlink(255, 220, 0, 60);
      break;
    case MessageType::TopologyTrigger:
      hooks_->onRgbBlink(0, 180, 255, 45);
      break;
    case MessageType::TopologyTriggerBatch:
      hooks_->onRgbBlink(0, 200, 255, 50);
      break;
    case MessageType::TopologyTriggerAck:
      hooks_->onRgbBlink(0, 120, 220, 35);
      break;
    case MessageType::FirmwareBegin:
      hooks_->onRgbBlink(0, 255, 255, 120);
      break;
    case MessageType::FirmwareChunk:
      // Suppress per-chunk blink to keep WiFi callback path lightweight during OTA bursts.
      break;
    case MessageType::FirmwareEnd:
      hooks_->onRgbBlink(255, 255, 255, 180);
      break;
    case MessageType::FirmwareStatus:
      // Keep OTA data/control path as lightweight as possible in WiFi callback context.
      break;
    default:
      hooks_->onRgbBlink(128, 128, 128, 20);
      break;
  }
}

}  // namespace espnow_link

