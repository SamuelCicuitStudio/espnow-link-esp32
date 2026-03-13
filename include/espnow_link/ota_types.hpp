#pragma once

#include <cstdint>
#include <string>

namespace espnow_link {

/** @brief OTA operation result/status code. */
enum class OtaStatusCode : uint16_t {
  Ok = 0x0000,
  StorageNotReady = 0x0001,
  GateDenied = 0x0002,
  GateBusy = 0x0003,
  GatePrepFailed = 0x0004,
  ImageTooLarge = 0x0005,
  InvalidState = 0x0006,
  InvalidArgument = 0x0007,
  OffsetMismatch = 0x0008,
  SizeMismatch = 0x0009,
  CrcMismatch = 0x000A,
  ApplyRejected = 0x000B,
  ApplyFailed = 0x000C,
  Timeout = 0x000D,
  InternalError = 0x00FF,
};

/** @brief OTA transfer/apply lifecycle state. */
enum class OtaTransferState : uint8_t {
  Idle = 0,
  Receiving = 1,
  Ready = 2,
  Applying = 3,
  Failed = 4,
};

/** @brief Runtime OTA status snapshot. */
struct OtaRuntimeStatus {
  OtaTransferState state = OtaTransferState::Idle;
  OtaStatusCode code = OtaStatusCode::Ok;
  uint32_t expected_size = 0;
  uint32_t received_size = 0;
  uint32_t expected_crc32 = 0;
  uint32_t actual_crc32 = 0;
  std::string temp_path;
  std::string image_path;
  std::string persistent_state;
  uint32_t persistent_epoch_s = 0;
  bool boot_report_pending = false;
  std::string confirmed_sw_version;
  std::string confirmed_build_id;
  std::string message;
};

}  // namespace espnow_link
