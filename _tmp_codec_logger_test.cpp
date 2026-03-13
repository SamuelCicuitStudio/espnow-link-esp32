#include <cstdio>
#include <vector>
#include <string>
#include "espnow_link/codec.hpp"
#include "espnow_link/descriptor.hpp"

using namespace espnow_link;

int main() {
  CompactIndexedProfileCodec codec;
  DescriptorQuery q{};
  q.type = DescriptorQueryType::GetLogStatus;
  std::vector<uint8_t> payload;
  bool ok = codec.encodeDescriptorQuery(q, payload);
  std::printf("encode status ok=%d len=%zu first=%02X\n", ok?1:0, payload.size(), payload.empty()?0:payload[0]);

  DescriptorQuery dq{};
  ok = codec.decodeDescriptorQuery(payload.data(), payload.size(), dq);
  std::printf("decode status ok=%d type=%u\n", ok?1:0, (unsigned)dq.type);

  DescriptorQuery qr{};
  qr.type = DescriptorQueryType::ReadLogChunk;
  qr.log_offset = 123;
  qr.log_max_bytes = 128;
  payload.clear();
  ok = codec.encodeDescriptorQuery(qr, payload);
  std::printf("encode read ok=%d len=%zu\n", ok?1:0, payload.size());
  dq = DescriptorQuery{};
  ok = codec.decodeDescriptorQuery(payload.data(), payload.size(), dq);
  std::printf("decode read ok=%d type=%u off=%u max=%u\n", ok?1:0, (unsigned)dq.type, dq.log_offset, dq.log_max_bytes);

  DescriptorResponse r{};
  r.type = DescriptorResponseType::LogStatus;
  r.logger_available = true;
  r.logger_enabled = true;
  r.logger_min_level = 2;
  r.log_bytes_used = 1234;
  r.log_bytes_dropped = 5;
  r.log_records_appended = 66;
  r.log_rotations = 7;
  r.log_total_size = 1234;
  payload.clear();
  ok = codec.encodeDescriptorResponse(r, payload);
  std::printf("encode logstatus resp ok=%d len=%zu first=%02X\n", ok?1:0, payload.size(), payload.empty()?0:payload[0]);
  DescriptorResponse rd{};
  ok = codec.decodeDescriptorResponse(payload.data(), payload.size(), rd);
  std::printf("decode logstatus resp ok=%d type=%u avail=%d total=%u\n", ok?1:0, (unsigned)rd.type, rd.logger_available?1:0, rd.log_total_size);

  DescriptorResponse c{};
  c.type = DescriptorResponseType::LogChunk;
  c.log_chunk_offset = 0;
  c.log_total_size = 10;
  c.log_chunk = {1,2,3,4,5};
  payload.clear();
  ok = codec.encodeDescriptorResponse(c, payload);
  std::printf("encode logchunk resp ok=%d len=%zu\n", ok?1:0, payload.size());
  rd = DescriptorResponse{};
  ok = codec.decodeDescriptorResponse(payload.data(), payload.size(), rd);
  std::printf("decode logchunk resp ok=%d type=%u chunk=%zu\n", ok?1:0, (unsigned)rd.type, rd.log_chunk.size());

  return 0;
}
