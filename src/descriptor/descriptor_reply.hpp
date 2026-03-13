#pragma once

#include <string>
#include <vector>

#include "espnow_link/descriptor.hpp"

namespace espnow_link {
namespace descriptor_reply {

bool buildDescriptorAckReply(bool ok, const std::string& message, std::vector<uint8_t>& out_payload);
bool buildTelemetrySnapshotReply(const std::vector<TelemetrySample>& samples,
                                 const char* cause,
                                 std::vector<uint8_t>& out_payload);

}  // namespace descriptor_reply
}  // namespace espnow_link

