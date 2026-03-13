#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace espnow_link {

/** @brief Built-in control command IDs. */
enum class ControlCommandId : uint16_t {
  RebootDeepSleep1s = 0x0001,
  ResetKeyAndReboot = 0x0002,
  SafeUnpair = 0x0003,
  AudioPing = 0x0004,
};

/** @brief Encode control command payload. */
bool buildControlCommandPayload(uint16_t cmd_id, std::vector<uint8_t>& out);
/** @brief Decode control command payload. */
bool parseControlCommandPayload(const uint8_t* payload, size_t len, uint16_t& out_cmd_id);

/** @brief Encode control result payload. */
bool buildControlResultPayload(uint16_t cmd_id, uint16_t result_code, std::vector<uint8_t>& out);
/** @brief Decode control result payload. */
bool parseControlResultPayload(const uint8_t* payload,
                               size_t len,
                               uint16_t& out_cmd_id,
                               uint16_t& out_result_code);

}  // namespace espnow_link
