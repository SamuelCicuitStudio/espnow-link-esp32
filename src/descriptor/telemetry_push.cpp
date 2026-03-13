#include "espnow_link/telemetry_push.hpp"

namespace espnow_link {
namespace {

constexpr uint8_t kSchemaVersion = 1;
constexpr uint8_t kTagSchema = 0x01;
constexpr uint8_t kTagMagic = 0x02;
constexpr uint8_t kTagAction = 0x10;
constexpr uint8_t kTagMode = 0x11;
constexpr uint8_t kTagIntervalMs = 0x12;
constexpr uint8_t kTagMinGapMs = 0x13;
constexpr uint8_t kTagStreamId = 0x14;
constexpr uint8_t kTagMetricKey = 0x20;
constexpr uint8_t kTagMetricIndex = 0x26;
constexpr uint8_t kTagMetricDelta = 0x21;
constexpr uint8_t kTagMetricEnable = 0x22;
constexpr uint8_t kTagMetricMode = 0x23;
constexpr uint8_t kTagMetricIntervalMs = 0x24;
constexpr uint8_t kTagMetricMinGapMs = 0x25;
constexpr uint16_t kMagicWord = 0x5450;  // 'TP'

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

bool appendU8(std::vector<uint8_t>& out, uint8_t tag, uint8_t v) {
  return appendTlv(out, tag, 0x01, &v, 1);
}

bool appendU16(std::vector<uint8_t>& out, uint8_t tag, uint16_t v) {
  uint8_t b[2] = {
      static_cast<uint8_t>(v & 0xFF),
      static_cast<uint8_t>((v >> 8) & 0xFF),
  };
  return appendTlv(out, tag, 0x02, b, 2);
}

bool appendU32(std::vector<uint8_t>& out, uint8_t tag, uint32_t v) {
  uint8_t b[4] = {
      static_cast<uint8_t>(v & 0xFF),
      static_cast<uint8_t>((v >> 8) & 0xFF),
      static_cast<uint8_t>((v >> 16) & 0xFF),
      static_cast<uint8_t>((v >> 24) & 0xFF),
  };
  return appendTlv(out, tag, 0x03, b, 4);
}

bool appendF32(std::vector<uint8_t>& out, uint8_t tag, float v) {
  uint8_t b[4] = {0};
  static_assert(sizeof(float) == 4, "float must be 4 bytes");
  const uint8_t* src = reinterpret_cast<const uint8_t*>(&v);
  b[0] = src[0];
  b[1] = src[1];
  b[2] = src[2];
  b[3] = src[3];
  return appendTlv(out, tag, 0x07, b, 4);
}

bool appendUtf8(std::vector<uint8_t>& out, uint8_t tag, const std::string& s) {
  if (s.size() > 65535U) {
    return false;
  }
  return appendTlv(out,
                   tag,
                   0x09,
                   reinterpret_cast<const uint8_t*>(s.data()),
                   static_cast<uint16_t>(s.size()));
}

bool readU32(const uint8_t* v, uint16_t len, uint32_t& out) {
  if (v == nullptr || len != 4) {
    return false;
  }
  out = static_cast<uint32_t>(v[0]) |
        (static_cast<uint32_t>(v[1]) << 8) |
        (static_cast<uint32_t>(v[2]) << 16) |
        (static_cast<uint32_t>(v[3]) << 24);
  return true;
}

bool readU16(const uint8_t* v, uint16_t len, uint16_t& out) {
  if (v == nullptr || len != 2) {
    return false;
  }
  out = static_cast<uint16_t>(v[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(v[1]) << 8);
  return true;
}

bool readF32(const uint8_t* v, uint16_t len, float& out) {
  if (v == nullptr || len != 4) {
    return false;
  }
  uint8_t* dst = reinterpret_cast<uint8_t*>(&out);
  dst[0] = v[0];
  dst[1] = v[1];
  dst[2] = v[2];
  dst[3] = v[3];
  return true;
}

bool validMode(TelemetryPushMode mode) {
  return mode == TelemetryPushMode::Periodic ||
         mode == TelemetryPushMode::OnChange ||
         mode == TelemetryPushMode::Hybrid;
}

bool validAction(TelemetryPushAction action) {
  return action == TelemetryPushAction::Start ||
         action == TelemetryPushAction::Update ||
         action == TelemetryPushAction::Pause ||
         action == TelemetryPushAction::Resume ||
         action == TelemetryPushAction::Stop ||
         action == TelemetryPushAction::Get;
}

}  // namespace

bool encodeTelemetryPushCommand(const TelemetryPushCommand& cmd, std::vector<uint8_t>& out_payload) {
  if (!validAction(cmd.action) || !validMode(cmd.config.mode)) {
    return false;
  }

  out_payload.clear();
  if (!appendU8(out_payload, kTagSchema, kSchemaVersion) ||
      !appendU16(out_payload, kTagMagic, kMagicWord) ||
      !appendU8(out_payload, kTagAction, static_cast<uint8_t>(cmd.action))) {
    return false;
  }

  if (cmd.action == TelemetryPushAction::Pause ||
      cmd.action == TelemetryPushAction::Resume ||
      cmd.action == TelemetryPushAction::Stop ||
      cmd.action == TelemetryPushAction::Get) {
    return true;
  }

  if (!appendU16(out_payload, kTagStreamId, cmd.config.stream_id) ||
      !appendU8(out_payload, kTagMode, static_cast<uint8_t>(cmd.config.mode)) ||
      !appendU32(out_payload, kTagIntervalMs, cmd.config.interval_ms) ||
      !appendU32(out_payload, kTagMinGapMs, cmd.config.min_report_gap_ms)) {
    return false;
  }

  for (const auto& m : cmd.config.metrics) {
    const bool has_key = !m.key.empty();
    const bool has_index = m.has_metric_index;
    if ((has_key == has_index) || !validMode(m.mode)) {
      return false;
    }

    bool id_ok = false;
    if (has_key) {
      id_ok = appendUtf8(out_payload, kTagMetricKey, m.key);
    } else {
      id_ok = appendU16(out_payload, kTagMetricIndex, m.metric_index);
    }

    if (!id_ok) {
      return false;
    }

    // Keep per-metric payload compact: only emit fields that differ from stream defaults.
    if (!m.enabled) {
      if (!appendU8(out_payload, kTagMetricEnable, 0U)) {
        return false;
      }
    }
    if (m.mode != cmd.config.mode) {
      if (!appendU8(out_payload, kTagMetricMode, static_cast<uint8_t>(m.mode))) {
        return false;
      }
    }
    if (m.interval_ms != 0U && m.interval_ms != cmd.config.interval_ms) {
      if (!appendU32(out_payload, kTagMetricIntervalMs, m.interval_ms)) {
        return false;
      }
    }
    if (m.min_report_gap_ms != 0U && m.min_report_gap_ms != cmd.config.min_report_gap_ms) {
      if (!appendU32(out_payload, kTagMetricMinGapMs, m.min_report_gap_ms)) {
        return false;
      }
    }

    if (m.use_threshold && m.mode != TelemetryPushMode::Periodic) {
      if (!appendF32(out_payload, kTagMetricDelta, m.delta_abs)) {
        return false;
      }
    }
  }

  return true;
}

bool parseTelemetryPushCommand(const uint8_t* payload, size_t len, TelemetryPushCommand& out_cmd) {
  out_cmd = TelemetryPushCommand{};
  if (payload == nullptr || len < 12) {
    return false;
  }

  bool have_magic = false;
  bool have_action = false;

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

    const uint8_t* v = payload + off;
    if (tag == kTagMagic && type == 0x02) {
      uint16_t magic = 0;
      if (!readU16(v, tlv_len, magic) || magic != kMagicWord) {
        return false;
      }
      have_magic = true;
    } else if (tag == kTagAction && type == 0x01 && tlv_len == 1) {
      out_cmd.action = static_cast<TelemetryPushAction>(v[0]);
      have_action = true;
    } else if (tag == kTagStreamId && type == 0x02 && tlv_len == 2) {
      if (!readU16(v, tlv_len, out_cmd.config.stream_id)) {
        return false;
      }
    } else if (tag == kTagMode && type == 0x01 && tlv_len == 1) {
      out_cmd.config.mode = static_cast<TelemetryPushMode>(v[0]);
    } else if (tag == kTagIntervalMs && type == 0x03 && tlv_len == 4) {
      if (!readU32(v, tlv_len, out_cmd.config.interval_ms)) {
        return false;
      }
    } else if (tag == kTagMinGapMs && type == 0x03 && tlv_len == 4) {
      if (!readU32(v, tlv_len, out_cmd.config.min_report_gap_ms)) {
        return false;
      }
    } else if (tag == kTagMetricKey && type == 0x09) {
      TelemetryPushMetricConfig m{};
      m.key.assign(reinterpret_cast<const char*>(v), reinterpret_cast<const char*>(v) + tlv_len);
      m.has_metric_index = false;
      m.mode = out_cmd.config.mode;
      m.interval_ms = out_cmd.config.interval_ms;
      m.min_report_gap_ms = out_cmd.config.min_report_gap_ms;
      m.use_threshold = (m.mode != TelemetryPushMode::Periodic);
      out_cmd.config.metrics.push_back(m);
    } else if (tag == kTagMetricIndex && type == 0x02 && tlv_len == 2) {
      uint16_t idx = 0;
      if (!readU16(v, tlv_len, idx)) {
        return false;
      }
      TelemetryPushMetricConfig m{};
      m.has_metric_index = true;
      m.metric_index = idx;
      m.mode = out_cmd.config.mode;
      m.interval_ms = out_cmd.config.interval_ms;
      m.min_report_gap_ms = out_cmd.config.min_report_gap_ms;
      m.use_threshold = (m.mode != TelemetryPushMode::Periodic);
      out_cmd.config.metrics.push_back(m);
    } else if (tag == kTagMetricEnable && type == 0x01 && tlv_len == 1) {
      if (out_cmd.config.metrics.empty()) {
        return false;
      }
      out_cmd.config.metrics.back().enabled = (v[0] != 0);
    } else if (tag == kTagMetricMode && type == 0x01 && tlv_len == 1) {
      if (out_cmd.config.metrics.empty()) {
        return false;
      }
      TelemetryPushMetricConfig& metric = out_cmd.config.metrics.back();
      metric.mode = static_cast<TelemetryPushMode>(v[0]);
      metric.use_threshold = (metric.mode != TelemetryPushMode::Periodic);
      if (!metric.use_threshold) {
        metric.delta_abs = 0.0f;
      }
    } else if (tag == kTagMetricIntervalMs && type == 0x03 && tlv_len == 4) {
      if (out_cmd.config.metrics.empty()) {
        return false;
      }
      if (!readU32(v, tlv_len, out_cmd.config.metrics.back().interval_ms)) {
        return false;
      }
    } else if (tag == kTagMetricMinGapMs && type == 0x03 && tlv_len == 4) {
      if (out_cmd.config.metrics.empty()) {
        return false;
      }
      if (!readU32(v, tlv_len, out_cmd.config.metrics.back().min_report_gap_ms)) {
        return false;
      }
    } else if (tag == kTagMetricDelta && type == 0x07 && tlv_len == 4) {
      if (out_cmd.config.metrics.empty()) {
        return false;
      }
      float d = 0.0f;
      if (!readF32(v, tlv_len, d)) {
        return false;
      }
      TelemetryPushMetricConfig& metric = out_cmd.config.metrics.back();
      metric.use_threshold = true;
      metric.delta_abs = d;
    }

    off += tlv_len;
  }

  if (!have_magic || !have_action || !validAction(out_cmd.action)) {
    return false;
  }

  if (out_cmd.action == TelemetryPushAction::Start || out_cmd.action == TelemetryPushAction::Update) {
    if (!validMode(out_cmd.config.mode) || out_cmd.config.metrics.empty()) {
      return false;
    }
    for (const auto& m : out_cmd.config.metrics) {
      const bool has_key = !m.key.empty();
      const bool has_index = m.has_metric_index;
      if ((has_key == has_index) || !validMode(m.mode)) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace espnow_link








