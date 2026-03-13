#include "espnow_link/pull_response_logger.hpp"

#include <cstdarg>
#include <cstdio>

#include "espnow_link/address.hpp"

namespace espnow_link {

namespace {

std::string fmt(const char* pattern, ...) {
  char buf[256] = {0};
  va_list ap;
  va_start(ap, pattern);
  std::vsnprintf(buf, sizeof(buf), pattern, ap);
  va_end(ap);
  return std::string(buf);
}

const char* settingType(SettingValueType t) {
  switch (t) {
    case SettingValueType::String:
      return "string";
    case SettingValueType::Int:
      return "int";
    case SettingValueType::Float:
      return "float";
    case SettingValueType::Bool:
      return "bool";
    default:
      return "unknown";
  }
}

}  // namespace

bool PullResponseLogger::onPullResponse(const MacAddress& from, uint32_t corr_id, const uint8_t* payload, size_t len) {
  if (pull_ == nullptr) {
    return true;
  }

  PullResponseDecoded decoded{};
  if (!pull_->decodePullResponseWithActiveCodec(payload, len, decoded)) {
    sink_.logLine(fmt("[MASTER][PULL] corr=%lu from=%s len=%u unparsed",
                      static_cast<unsigned long>(corr_id),
                      macToString(from).c_str(),
                      static_cast<unsigned int>(len)));
    return true;
  }

  if (decoded.kind == PullResponseKind::ControlResult) {
    sink_.logLine(fmt("[MASTER][CTRL] corr=%lu from=%s cmd=0x%04X result=0x%04X",
                      static_cast<unsigned long>(corr_id),
                      macToString(from).c_str(),
                      static_cast<unsigned int>(decoded.control.command_id),
                      static_cast<unsigned int>(decoded.control.result_code)));
    return true;
  }

  if (decoded.kind != PullResponseKind::Descriptor) {
    return true;
  }

  const DescriptorResponse& d = decoded.descriptor;
  switch (d.type) {
    case DescriptorResponseType::Device:
      sink_.logLine(fmt("[MASTER][DESC] %s id=%s name=%s hw=%s sw=%s build=%s",
                        d.device.device_type.c_str(),
                        d.device.device_id.c_str(),
                        d.device.device_name.c_str(),
                        d.device.hw_version.c_str(),
                        d.device.sw_version.c_str(),
                        d.device.build_id.c_str()));
      break;
    case DescriptorResponseType::Capabilities:
      sink_.logLine(fmt("[MASTER][DESC] capabilities=%u", static_cast<unsigned int>(d.capabilities.size())));
      for (const auto& c : d.capabilities) {
        sink_.logLine(fmt("  - %s: %s", c.key.c_str(), c.description.c_str()));
      }
      break;
    case DescriptorResponseType::Settings:
      sink_.logLine(fmt("[MASTER][DESC] settings=%u", static_cast<unsigned int>(d.settings.size())));
      for (const auto& s : d.settings) {
        sink_.logLine(fmt("  - id=0x%04X %s (%s rw=%s) current=%s",
                          static_cast<unsigned int>(s.setting_id),
                          s.key.c_str(),
                          settingType(s.value_type),
                          s.writable ? "yes" : "no",
                          s.current_value.empty() ? "<empty>" : s.current_value.c_str()));
      }
      if (d.message == "truncated") {
        sink_.logLine("[MASTER][DESC] note: bulk settings truncated by payload limit (use settings/settings.full)");
      }
      break;
    case DescriptorResponseType::Setting:
      sink_.logLine(fmt("[MASTER][DESC] setting id=0x%04X key=%s value=%s",
                        static_cast<unsigned int>(d.setting.setting_id),
                        d.setting.key.c_str(),
                        d.setting.current_value.c_str()));
      break;
    case DescriptorResponseType::Telemetry:
      sink_.logLine(fmt("[MASTER][DESC] telemetry schema=%u", static_cast<unsigned int>(d.telemetry.size())));
      for (const auto& t : d.telemetry) {
        sink_.logLine(fmt("  - id=0x%04X %s [%s]",
                          static_cast<unsigned int>(t.metric_id),
                          t.key.c_str(),
                          t.unit.c_str()));
      }
      break;
    case DescriptorResponseType::TelemetrySnapshot:
      sink_.logLine("[MASTER][TELEM] live samples:");
      for (const auto& s : d.telemetry_samples) {
        sink_.logLine(fmt("  - id=0x%04X %s=%s %s",
                          static_cast<unsigned int>(s.metric_id),
                          s.key.c_str(),
                          s.value.c_str(),
                          s.unit.c_str()));
      }
      break;
    case DescriptorResponseType::Liveness:
      sink_.logLine(fmt("[MASTER][LIVE] online=%s uptime_ms=%lu state=%s",
                        d.liveness.online ? "yes" : "no",
                        static_cast<unsigned long>(d.liveness.uptime_ms),
                        d.liveness.state.c_str()));
      break;
    case DescriptorResponseType::Time:
      sink_.logLine(fmt("[MASTER][TIME] epoch_s=%llu uptime_ms=%lu",
                        static_cast<unsigned long long>(d.time.epoch_s),
                        static_cast<unsigned long>(d.time.uptime_ms)));
      break;
    case DescriptorResponseType::StorageInfo:
      sink_.logLine(fmt("[MASTER][STORAGE] mode=%u available=%s mounted=%s total=%lu used=%lu free=%lu",
                        static_cast<unsigned int>(d.storage_info.mode),
                        d.storage_info.available ? "yes" : "no",
                        d.storage_info.mounted ? "yes" : "no",
                        static_cast<unsigned long>(d.storage_info.total_bytes),
                        static_cast<unsigned long>(d.storage_info.used_bytes),
                        static_cast<unsigned long>(d.storage_info.free_bytes)));
      break;
    case DescriptorResponseType::StorageList:
      sink_.logLine(fmt("[MASTER][STORAGE] list path=%s entries=%u",
                        d.storage_path.c_str(),
                        static_cast<unsigned int>(d.storage_entries.size())));
      for (const auto& e : d.storage_entries) {
        sink_.logLine(fmt("  - %s %s size=%lu",
                          e.is_dir ? "[D]" : "[F]",
                          e.name.c_str(),
                          static_cast<unsigned long>(e.size_bytes)));
      }
      break;
    case DescriptorResponseType::StorageStat:
      sink_.logLine(fmt("[MASTER][STORAGE] stat path=%s exists=%s type=%s size=%lu",
                        d.storage_stat.path.c_str(),
                        d.storage_stat.exists ? "yes" : "no",
                        d.storage_stat.is_dir ? "dir" : "file",
                        static_cast<unsigned long>(d.storage_stat.size_bytes)));
      break;
    case DescriptorResponseType::Ack:
      sink_.logLine(fmt("[MASTER][DESC] ok: %s", d.message.c_str()));
      break;
    case DescriptorResponseType::Error:
      sink_.logLine(fmt("[MASTER][DESC] error: %s", d.message.c_str()));
      break;
    default:
      sink_.logLine(fmt("[MASTER][DESC] type=%u", static_cast<unsigned int>(d.type)));
      break;
  }
  return true;
}

}  // namespace espnow_link
