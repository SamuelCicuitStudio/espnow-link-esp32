#include "descriptor_reply.hpp"

#include "espnow_link/descriptor.hpp"

namespace espnow_link {
namespace descriptor_reply {

bool buildDescriptorAckReply(bool ok, const std::string& message, std::vector<uint8_t>& out_payload) {
  DescriptorResponse resp{};
  resp.type = ok ? DescriptorResponseType::Ack : DescriptorResponseType::Error;
  resp.message = message;

  std::string encoded;
  if (!encodeDescriptorResponse(resp, encoded)) {
    return false;
  }
  out_payload.assign(encoded.begin(), encoded.end());
  return true;
}

bool buildTelemetrySnapshotReply(const std::vector<TelemetrySample>& samples,
                                 const char* cause,
                                 std::vector<uint8_t>& out_payload) {
  DescriptorResponse resp{};
  resp.type = DescriptorResponseType::TelemetrySnapshot;
  resp.message = (cause == nullptr) ? "stream" : cause;
  resp.telemetry_samples = samples;

  std::string encoded;
  if (!encodeDescriptorResponse(resp, encoded)) {
    return false;
  }
  out_payload.assign(encoded.begin(), encoded.end());
  return true;
}

}  // namespace descriptor_reply
}  // namespace espnow_link
