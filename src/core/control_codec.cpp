#include "espnow_link/control_codec.hpp"

namespace espnow_link {
namespace {

bool appendTlv(std::vector<uint8_t>& out, uint8_t tag, uint8_t type, const uint8_t* value, uint16_t len) {
  out.push_back(tag);
  out.push_back(type);
  out.push_back(static_cast<uint8_t>(len & 0xFF));
  out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  if (len > 0) {
    if (value == nullptr) {
      return false;
    }
    out.insert(out.end(), value, value + len);
  }
  return true;
}

}  // namespace

bool buildControlCommandPayload(uint16_t cmd_id, std::vector<uint8_t>& out) {
  out.clear();
  const uint8_t schema = 1;
  if (!appendTlv(out, 0x01, 0x01, &schema, 1)) {
    return false;
  }

  uint8_t c[2] = {
      static_cast<uint8_t>(cmd_id & 0xFF),
      static_cast<uint8_t>((cmd_id >> 8) & 0xFF),
  };
  return appendTlv(out, 0x10, 0x02, c, 2);
}

bool parseControlCommandPayload(const uint8_t* payload, size_t len, uint16_t& out_cmd_id) {
  if (payload == nullptr) {
    return false;
  }

  size_t off = 0;
  while (off + 4 <= len) {
    const uint8_t tag = payload[off + 0];
    const uint8_t type = payload[off + 1];
    const uint16_t tlv_len = static_cast<uint16_t>(payload[off + 2]) |
                             (static_cast<uint16_t>(payload[off + 3]) << 8);
    off += 4;
    if ((off + tlv_len) > len) {
      return false;
    }

    if (tag == 0x10 && type == 0x02 && tlv_len == 2) {
      out_cmd_id = static_cast<uint16_t>(payload[off + 0]) |
                   (static_cast<uint16_t>(payload[off + 1]) << 8);
      return true;
    }

    off += tlv_len;
  }

  return false;
}

bool buildControlResultPayload(uint16_t cmd_id, uint16_t result_code, std::vector<uint8_t>& out) {
  out.clear();

  uint8_t c[2] = {
      static_cast<uint8_t>(cmd_id & 0xFF),
      static_cast<uint8_t>((cmd_id >> 8) & 0xFF),
  };
  uint8_t r[2] = {
      static_cast<uint8_t>(result_code & 0xFF),
      static_cast<uint8_t>((result_code >> 8) & 0xFF),
  };

  if (!appendTlv(out, 0x20, 0x02, c, 2)) {
    return false;
  }
  return appendTlv(out, 0x21, 0x02, r, 2);
}

bool parseControlResultPayload(const uint8_t* payload,
                               size_t len,
                               uint16_t& out_cmd_id,
                               uint16_t& out_result_code) {
  if (payload == nullptr) {
    return false;
  }

  bool have_cmd = false;
  bool have_result = false;

  size_t off = 0;
  while (off + 4 <= len) {
    const uint8_t tag = payload[off + 0];
    const uint8_t type = payload[off + 1];
    const uint16_t tlv_len = static_cast<uint16_t>(payload[off + 2]) |
                             (static_cast<uint16_t>(payload[off + 3]) << 8);
    off += 4;
    if ((off + tlv_len) > len) {
      return false;
    }

    if (tag == 0x20 && type == 0x02 && tlv_len == 2) {
      out_cmd_id = static_cast<uint16_t>(payload[off + 0]) |
                   (static_cast<uint16_t>(payload[off + 1]) << 8);
      have_cmd = true;
    } else if (tag == 0x21 && type == 0x02 && tlv_len == 2) {
      out_result_code = static_cast<uint16_t>(payload[off + 0]) |
                        (static_cast<uint16_t>(payload[off + 1]) << 8);
      have_result = true;
    }

    off += tlv_len;
  }

  return have_cmd && have_result;
}

}  // namespace espnow_link
