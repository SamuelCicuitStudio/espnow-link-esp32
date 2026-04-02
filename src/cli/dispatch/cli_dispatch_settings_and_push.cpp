/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Settings/time/autopull/push command handling paths.
 **************************************************************/
#include "../internal/cli_dispatch_internal.hpp"
#include "../internal/cli_dispatch_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

bool MasterCli::handleGetIdCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "get.id ")) {
    return false;
  }

  const std::string arg = trim(line.substr(7));
  if (arg.empty()) {
    io_.writeln("[MASTER][CLI] usage: get.id <setting_id>");
    return true;
  }

  const unsigned long id = std::strtoul(arg.c_str(), nullptr, 0);
  if (id == 0 || id > 0xFFFFUL) {
    io_.writeln("[MASTER][CLI] invalid setting_id");
    return true;
  }

  if (!hasRuntimePeer()) {
    io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingGet),
                                         management_utils::buildSettingGetByIdPayload(static_cast<uint16_t>(id)));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] requested setting id=0x%04lX", id);
  } else {
    io_.writeln("[MASTER][CLI] get.id request failed");
  }
  return true;
}

bool MasterCli::handleSetIdCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "set.id ")) {
    return false;
  }

  const std::string body = trim(line.substr(7));
  const size_t eq = body.find('=');
  if (body.empty() || eq == std::string::npos || eq == 0) {
    io_.writeln("[MASTER][CLI] usage: set.id <setting_id>=<value>");
    return true;
  }

  const std::string sid = trim(body.substr(0, eq));
  const std::string value = trim(body.substr(eq + 1));
  const unsigned long id = std::strtoul(sid.c_str(), nullptr, 0);
  if (id == 0 || id > 0xFFFFUL) {
    io_.writeln("[MASTER][CLI] invalid setting_id");
    return true;
  }

  if (!hasRuntimePeer()) {
    io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingSet),
                                         management_utils::buildSettingSetByIdPayload(static_cast<uint16_t>(id), value));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] set.id requested: 0x%04lX=%s", id, value.c_str());
  } else {
    io_.writeln("[MASTER][CLI] set.id request failed");
  }
  return true;
}

bool MasterCli::handleGetCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "get ")) {
    return false;
  }

  const std::string key = trim(line.substr(4));
  if (key.empty()) {
    io_.writeln("[MASTER][CLI] usage: get <setting_key>  (child: v<vid>.<field>)");
    return true;
  }

  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingGet),
                                         management_utils::buildSettingGetByKeyPayload(key));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] requested setting %s", key.c_str());
  } else {
    io_.writeln("[MASTER][CLI] get setting request failed");
  }
  return true;
}

bool MasterCli::handleSetCommand(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "set ")) {
    return false;
  }

  const std::string body = trim(line.substr(4));
  if (body.empty() || body.find('=') == std::string::npos) {
    io_.writeln("[MASTER][CLI] usage: set <setting_key>=<value>  (child: v<vid>.<field>)");
    return true;
  }

  const size_t eq = body.find('=');
  const std::string key = trim(body.substr(0U, eq));
  const std::string value = trim(body.substr(eq + 1U));
  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }
  if (key.empty()) {
    io_.writeln("[MASTER][CLI] usage: set <setting_key>=<value>  (child: v<vid>.<field>)");
    return true;
  }
  ManagementController mgmt(*management_transport_);
  mgmt.setNextReqId(correlation_id_);
  const bool ok = submitRuntimeTargeted_(mgmt,
                                         static_cast<uint16_t>(ManagementCommandId::SettingSet),
                                         management_utils::buildSettingSetByKeyPayload(key, value));
  correlation_id_ = mgmt.nextReqId();
  if (ok) {
    writef("[MASTER][CLI] set requested: %s", body.c_str());
  } else {
    io_.writeln("[MASTER][CLI] set setting request failed");
  }
  return true;
}

bool MasterCli::handleTimeCommand(const std::string& line, const std::string& lower) {
  if (lower == "time.get") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::TimeGet));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      io_.writeln("[MASTER][CLI] requested slave time");
    } else {
      io_.writeln("[MASTER][CLI] slave time request failed");
    }
    return true;
  }

  if (lower == "time.set.now") {
    const uint64_t now_s = static_cast<uint64_t>(std::time(nullptr));
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::TimeSet),
                                           management_utils::buildTimeSetPayload(now_s));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][CLI] requested slave time set to %lu", static_cast<unsigned long>(now_s));
    } else {
      io_.writeln("[MASTER][CLI] slave time set request failed");
    }
    return true;
  }

  if (startsWith(lower, "time.set ")) {
    const std::string arg = trim(line.substr(9));
    if (arg.empty()) {
      io_.writeln("[MASTER][CLI] usage: time.set <epoch_s>");
      return true;
    }
    const uint64_t epoch_s = static_cast<uint64_t>(std::strtoull(arg.c_str(), nullptr, 10));
    if (epoch_s == 0) {
      io_.writeln("[MASTER][CLI] invalid epoch_s");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimeTargeted_(mgmt,
                                           static_cast<uint16_t>(ManagementCommandId::TimeSet),
                                           management_utils::buildTimeSetPayload(epoch_s));
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][CLI] requested slave time set to %s", arg.c_str());
    } else {
      io_.writeln("[MASTER][CLI] slave time set request failed");
    }
    return true;
  }

  return false;
}

bool MasterCli::handleAutopullCommand(const std::string& lower) {
  if (!startsWith(lower, "autopull ")) {
    return false;
  }

  const std::string arg = trim(lower.substr(9));
  if (arg == "off") {
    setAutoPull(false, auto_pull_interval_ms_);
    io_.writeln("[MASTER][CLI] autopull disabled");
    return true;
  }
  if (startsWith(arg, "on")) {
    MacAddress runtime_peer{};
    if (!resolveRuntimePeer(runtime_peer)) {
      io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
      return true;
    }
    uint32_t interval = auto_pull_interval_ms_;
    const size_t sp = arg.find(' ');
    if (sp != std::string::npos) {
      const unsigned long parsed = std::strtoul(arg.substr(sp + 1).c_str(), nullptr, 10);
      if (parsed >= 300UL) {
        interval = static_cast<uint32_t>(parsed);
      }
    }
    auto_pull_has_target_peer_ = true;
    auto_pull_target_peer_ = runtime_peer;
    setAutoPull(true, interval);
    writef("[MASTER][CLI] autopull enabled peer=%s interval_ms=%lu",
           macToPrintable(runtime_peer).c_str(),
           static_cast<unsigned long>(auto_pull_interval_ms_));
    return true;
  }

  io_.writeln("[MASTER][CLI] usage: autopull on [ms] | autopull off");
  return true;
}

bool MasterCli::handlePushCommands(const std::string& line, const std::string& lower) {
  if (!startsWith(lower, "push.")) {
    return false;
  }

  if (!hasRuntimePeer()) {
    io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }
  MacAddress runtime_peer{};
  if (!resolveRuntimePeer(runtime_peer)) {
    io_.writeln("[MASTER][CLI] target not selected (use active <paired_index|paired_mac> or command prefix)");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "target");
    return true;
  }
  ChildPushPeerState& child_push_state = ensureChildPushState_(runtime_peer);
  if (management_transport_ == nullptr) {
    io_.writeln("[MASTER][CLI] management path unavailable");
    captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
    return true;
  }

  const std::vector<std::string> tokens = splitTokens(line);
  if (tokens.empty()) {
    io_.writeln("[MASTER][CLI] invalid push command");
    return true;
  }

  const std::string cmd = lowerCopy(tokens[0]);
  TelemetryPushCommand push{};

  auto send = [&](const char* label) {
    uint32_t corr = 0;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      writef("[MASTER][CLI] %s sent corr=%lu", label, static_cast<unsigned long>(corr));
    } else {
      io_.writeln("[MASTER][CLI] push command send failed");
    }
    return true;
  };

  if (cmd == "push.pause") {
    push.action = TelemetryPushAction::Pause;
    return send("push.pause");
  }
  if (cmd == "push.resume") {
    push.action = TelemetryPushAction::Resume;
    return send("push.resume");
  }
  if (cmd == "push.stop") {
    child_push_state.semu_mask = 0U;
    child_push_state.remu_mask = 0U;
    push.action = TelemetryPushAction::Stop;
    return send("push.stop");
  }
  if (cmd == "push.get") {
    push.action = TelemetryPushAction::Get;
    return send("push.get");
  }

  auto fillMetricTiming = [&](TelemetryPushMetricConfig& metric,
                              TelemetryPushMode mode,
                              uint32_t interval_ms,
                              float delta_abs,
                              uint32_t gap_ms) {
    metric.enabled = true;
    metric.mode = mode;
    metric.interval_ms = interval_ms;
    metric.min_report_gap_ms = gap_ms;
    metric.use_threshold = (mode != TelemetryPushMode::Periodic);
    metric.delta_abs = (mode == TelemetryPushMode::Periodic) ? 0.0f : delta_abs;
  };

  auto parsePushTiming = [&](size_t mode_index,
                             TelemetryPushMode& out_mode,
                             uint32_t& out_interval_ms,
                             float& out_delta_abs,
                             uint32_t& out_gap_ms) -> bool {
    out_mode = TelemetryPushMode::Hybrid;
    out_interval_ms = 2000;
    out_delta_abs = 0.10f;
    out_gap_ms = 200;

    if (tokens.size() > mode_index) {
      if (!parsePushModeToken(tokens[mode_index], out_mode)) {
        io_.writeln("[MASTER][CLI] invalid push mode (use: hybrid|periodic|change)");
        return false;
      }
    }
    if (tokens.size() > mode_index + 1) {
      if (!parseU32Token(tokens[mode_index + 1], out_interval_ms) || out_interval_ms < 200U) {
        io_.writeln("[MASTER][CLI] invalid interval_ms (>=200)");
        return false;
      }
    }
    if (tokens.size() > mode_index + 2) {
      if (!parseFloatToken(tokens[mode_index + 2], out_delta_abs) || out_delta_abs < 0.0f) {
        io_.writeln("[MASTER][CLI] invalid delta_abs (>=0)");
        return false;
      }
    }
    if (tokens.size() > mode_index + 3) {
      if (!parseU32Token(tokens[mode_index + 3], out_gap_ms) || out_gap_ms < 50U) {
        io_.writeln("[MASTER][CLI] invalid gap_ms (>=50)");
        return false;
      }
    }
    return true;
  };

  auto bitCount8 = [](uint8_t v) -> uint8_t {
    uint8_t c = 0U;
    while (v != 0U) {
      c = static_cast<uint8_t>(c + (v & 0x01U));
      v = static_cast<uint8_t>(v >> 1U);
    }
    return c;
  };

  auto bitCount16 = [](uint16_t v) -> uint8_t {
    uint8_t c = 0U;
    while (v != 0U) {
      c = static_cast<uint8_t>(c + static_cast<uint8_t>(v & 0x0001U));
      v = static_cast<uint16_t>(v >> 1U);
    }
    return c;
  };

  enum class ChildPushProfile : uint8_t {
    Unknown = 0,
    Semu = 1,
    Remu = 2,
  };

  auto resolveRemoteProfileId = [&]() -> ProfileId {
    ProfileId resolved = kProfileUnknown;
    if (ensureRuntimeProfileKnown_(resolved, false)) {
      return resolved;
    }
    return kProfileUnknown;
  };

  auto activeChildPushProfile = [&]() -> ChildPushProfile {
    const ProfileId profile_id = resolveRemoteProfileId();
    if (profile_id == kProfileSemu) {
      return ChildPushProfile::Semu;
    }
    if (profile_id == kProfileRemu) {
      return ChildPushProfile::Remu;
    }
    return ChildPushProfile::Unknown;
  };

  auto buildSemuChildPush = [&](uint8_t child_mask,
                                TelemetryPushAction action,
                                TelemetryPushMode mode,
                                uint32_t interval_ms,
                                float delta_abs,
                                uint32_t gap_ms,
                                TelemetryPushCommand& out_cmd) -> bool {
    out_cmd = TelemetryPushCommand{};
    out_cmd.action = action;
    out_cmd.config.stream_id = 1;
    out_cmd.config.mode = mode;
    out_cmd.config.interval_ms = interval_ms;
    out_cmd.config.min_report_gap_ms = gap_ms;
    if (action == TelemetryPushAction::Stop || action == TelemetryPushAction::Pause ||
        action == TelemetryPushAction::Resume || action == TelemetryPushAction::Get) {
      return true;
    }
    for (uint8_t vid = 0U; vid < 8U; ++vid) {
      if ((child_mask & static_cast<uint8_t>(1U << vid)) == 0U) {
        continue;
      }
      TelemetryPushMetricConfig a{};
      a.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".tfl_a_mm";
      fillMetricTiming(a, mode, interval_ms, delta_abs, gap_ms);
      out_cmd.config.metrics.push_back(a);

      TelemetryPushMetricConfig b{};
      b.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".tfl_b_mm";
      fillMetricTiming(b, mode, interval_ms, delta_abs, gap_ms);
      out_cmd.config.metrics.push_back(b);
    }
    return !out_cmd.config.metrics.empty();
  };

  auto buildRemuChildPush = [&](uint16_t child_mask,
                                TelemetryPushAction action,
                                TelemetryPushMode mode,
                                uint32_t interval_ms,
                                float delta_abs,
                                uint32_t gap_ms,
                                TelemetryPushCommand& out_cmd) -> bool {
    out_cmd = TelemetryPushCommand{};
    out_cmd.action = action;
    out_cmd.config.stream_id = 1;
    out_cmd.config.mode = mode;
    out_cmd.config.interval_ms = interval_ms;
    out_cmd.config.min_report_gap_ms = gap_ms;
    if (action == TelemetryPushAction::Stop || action == TelemetryPushAction::Pause ||
        action == TelemetryPushAction::Resume || action == TelemetryPushAction::Get) {
      return true;
    }
    for (uint8_t vid = 0U; vid < 16U; ++vid) {
      if ((child_mask & static_cast<uint16_t>(1U << vid)) == 0U) {
        continue;
      }
      TelemetryPushMetricConfig relay_state{};
      relay_state.key = "v" + std::to_string(static_cast<unsigned int>(vid)) + ".relay_bitmap";
      fillMetricTiming(relay_state, mode, interval_ms, delta_abs, gap_ms);
      out_cmd.config.metrics.push_back(relay_state);
    }
    return !out_cmd.config.metrics.empty();
  };

  if (cmd == "push.child.start" || cmd == "push.child.update") {
    const ChildPushProfile child_profile = activeChildPushProfile();
    if (child_profile == ChildPushProfile::Unknown) {
      ProfileId ignored = kProfileUnknown;
      (void)ensureRuntimeProfileKnown_(ignored, true);
      io_.writeln("[MASTER][CLI] child push requires SEMU/REMU profile; probe queued, retry in a moment");
      return true;
    }
    const uint32_t max_vid = (child_profile == ChildPushProfile::Semu) ? 7U : 15U;
    if (tokens.size() < 2U || tokens.size() > 6U) {
      writef("[MASTER][CLI] usage: push.child.start <vid:0..%u> [hybrid|periodic|change] [interval_ms] [delta_abs] [gap_ms]",
             static_cast<unsigned int>(max_vid));
      return true;
    }
    uint32_t vid = 0U;
    if (!parseU32Token(tokens[1], vid) || vid > max_vid) {
      writef("[MASTER][CLI] invalid child vid (0..%u)", static_cast<unsigned int>(max_vid));
      return true;
    }

    TelemetryPushMode mode = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_mode : child_push_state.remu_mode;
    uint32_t interval_ms = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_interval_ms
                                                                      : child_push_state.remu_interval_ms;
    float delta_abs = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_delta_abs
                                                                 : child_push_state.remu_delta_abs;
    uint32_t gap_ms = (child_profile == ChildPushProfile::Semu) ? child_push_state.semu_gap_ms : child_push_state.remu_gap_ms;
    if (!parsePushTiming(2U, mode, interval_ms, delta_abs, gap_ms)) {
      return true;
    }
    uint8_t active_children = 0U;
    if (child_profile == ChildPushProfile::Semu) {
      const uint8_t old_mask = child_push_state.semu_mask;
      const TelemetryPushMode old_mode = child_push_state.semu_mode;
      const uint32_t old_interval = child_push_state.semu_interval_ms;
      const float old_delta = child_push_state.semu_delta_abs;
      const uint32_t old_gap = child_push_state.semu_gap_ms;

      child_push_state.semu_mode = mode;
      child_push_state.semu_interval_ms = interval_ms;
      child_push_state.semu_delta_abs = delta_abs;
      child_push_state.semu_gap_ms = gap_ms;
      child_push_state.semu_mask =
          static_cast<uint8_t>(child_push_state.semu_mask | static_cast<uint8_t>(1U << static_cast<uint8_t>(vid)));

      const TelemetryPushAction action = (old_mask == 0U) ? TelemetryPushAction::Start : TelemetryPushAction::Update;
      if (!buildSemuChildPush(child_push_state.semu_mask,
                              action,
                              child_push_state.semu_mode,
                              child_push_state.semu_interval_ms,
                              child_push_state.semu_delta_abs,
                              child_push_state.semu_gap_ms,
                              push)) {
        io_.writeln("[MASTER][CLI] failed to build child push config");
        child_push_state.semu_mask = old_mask;
        child_push_state.semu_mode = old_mode;
        child_push_state.semu_interval_ms = old_interval;
        child_push_state.semu_delta_abs = old_delta;
        child_push_state.semu_gap_ms = old_gap;
        return true;
      }
      active_children = bitCount8(child_push_state.semu_mask);
    } else {
      const uint16_t old_mask = child_push_state.remu_mask;
      const TelemetryPushMode old_mode = child_push_state.remu_mode;
      const uint32_t old_interval = child_push_state.remu_interval_ms;
      const float old_delta = child_push_state.remu_delta_abs;
      const uint32_t old_gap = child_push_state.remu_gap_ms;

      child_push_state.remu_mode = mode;
      child_push_state.remu_interval_ms = interval_ms;
      child_push_state.remu_delta_abs = delta_abs;
      child_push_state.remu_gap_ms = gap_ms;
      child_push_state.remu_mask =
          static_cast<uint16_t>(child_push_state.remu_mask | static_cast<uint16_t>(1U << static_cast<uint8_t>(vid)));

      const TelemetryPushAction action = (old_mask == 0U) ? TelemetryPushAction::Start : TelemetryPushAction::Update;
      if (!buildRemuChildPush(child_push_state.remu_mask,
                              action,
                              child_push_state.remu_mode,
                              child_push_state.remu_interval_ms,
                              child_push_state.remu_delta_abs,
                              child_push_state.remu_gap_ms,
                              push)) {
        io_.writeln("[MASTER][CLI] failed to build child push config");
        child_push_state.remu_mask = old_mask;
        child_push_state.remu_mode = old_mode;
        child_push_state.remu_interval_ms = old_interval;
        child_push_state.remu_delta_abs = old_delta;
        child_push_state.remu_gap_ms = old_gap;
        return true;
      }
      active_children = bitCount16(child_push_state.remu_mask);
    }
    uint32_t corr = 0U;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (!ok) {
      io_.writeln("[MASTER][CLI] child push command send failed");
      return true;
    }
    writef("[MASTER][CLI] push.child.start sent profile=%s vid=%u active_children=%u metrics=%u corr=%lu",
           (child_profile == ChildPushProfile::Semu) ? "SEMU" : "REMU",
           static_cast<unsigned int>(vid),
           static_cast<unsigned int>(active_children),
           static_cast<unsigned int>(push.config.metrics.size()),
           static_cast<unsigned long>(corr));
    return true;
  }

  if (cmd == "push.child.stop") {
    const ChildPushProfile child_profile = activeChildPushProfile();
    if (child_profile == ChildPushProfile::Unknown) {
      ProfileId ignored = kProfileUnknown;
      (void)ensureRuntimeProfileKnown_(ignored, true);
      io_.writeln("[MASTER][CLI] child stop requires SEMU/REMU profile; probe queued, retry in a moment");
      return true;
    }
    const uint32_t max_vid = (child_profile == ChildPushProfile::Semu) ? 7U : 15U;
    if (tokens.size() != 2U) {
      writef("[MASTER][CLI] usage: push.child.stop <vid:0..%u>", static_cast<unsigned int>(max_vid));
      return true;
    }
    uint32_t vid = 0U;
    if (!parseU32Token(tokens[1], vid) || vid > max_vid) {
      writef("[MASTER][CLI] invalid child vid (0..%u)", static_cast<unsigned int>(max_vid));
      return true;
    }
    uint8_t active_children = 0U;
    if (child_profile == ChildPushProfile::Semu) {
      const uint8_t bit = static_cast<uint8_t>(1U << static_cast<uint8_t>(vid));
      if ((child_push_state.semu_mask & bit) == 0U) {
        writef("[MASTER][CLI] child %u push already stopped", static_cast<unsigned int>(vid));
        return true;
      }
      const uint8_t old_mask = child_push_state.semu_mask;
      child_push_state.semu_mask = static_cast<uint8_t>(child_push_state.semu_mask & static_cast<uint8_t>(~bit));
      if (child_push_state.semu_mask == 0U) {
        push = TelemetryPushCommand{};
        push.action = TelemetryPushAction::Stop;
        push.config.stream_id = 1;
      } else if (!buildSemuChildPush(child_push_state.semu_mask,
                                     TelemetryPushAction::Update,
                                     child_push_state.semu_mode,
                                     child_push_state.semu_interval_ms,
                                     child_push_state.semu_delta_abs,
                                     child_push_state.semu_gap_ms,
                                     push)) {
        child_push_state.semu_mask = old_mask;
        io_.writeln("[MASTER][CLI] failed to build child push update");
        return true;
      }
      active_children = bitCount8(child_push_state.semu_mask);
    } else {
      const uint16_t bit = static_cast<uint16_t>(1U << static_cast<uint8_t>(vid));
      if ((child_push_state.remu_mask & bit) == 0U) {
        writef("[MASTER][CLI] child %u push already stopped", static_cast<unsigned int>(vid));
        return true;
      }
      const uint16_t old_mask = child_push_state.remu_mask;
      child_push_state.remu_mask = static_cast<uint16_t>(child_push_state.remu_mask & static_cast<uint16_t>(~bit));
      if (child_push_state.remu_mask == 0U) {
        push = TelemetryPushCommand{};
        push.action = TelemetryPushAction::Stop;
        push.config.stream_id = 1;
      } else if (!buildRemuChildPush(child_push_state.remu_mask,
                                     TelemetryPushAction::Update,
                                     child_push_state.remu_mode,
                                     child_push_state.remu_interval_ms,
                                     child_push_state.remu_delta_abs,
                                     child_push_state.remu_gap_ms,
                                     push)) {
        child_push_state.remu_mask = old_mask;
        io_.writeln("[MASTER][CLI] failed to build child push update");
        return true;
      }
      active_children = bitCount16(child_push_state.remu_mask);
    }

    uint32_t corr = 0U;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (!ok) {
      io_.writeln("[MASTER][CLI] child stop command send failed");
      return true;
    }
    writef("[MASTER][CLI] push.child.stop sent profile=%s vid=%u active_children=%u corr=%lu",
           (child_profile == ChildPushProfile::Semu) ? "SEMU" : "REMU",
           static_cast<unsigned int>(vid),
           static_cast<unsigned int>(active_children),
           static_cast<unsigned long>(corr));
    return true;
  }

  if (cmd == "push.start" || cmd == "push.update") {
    if (tokens.size() > 5U) {
      io_.writeln("[MASTER][CLI] usage: push.start [hybrid|periodic|change] [interval_ms] [delta_abs] [gap_ms]");
      return true;
    }

    TelemetryPushMode mode = TelemetryPushMode::Hybrid;
    uint32_t interval_ms = 2000;
    float delta_abs = 0.10f;
    uint32_t gap_ms = 200;
    if (!parsePushTiming(1U, mode, interval_ms, delta_abs, gap_ms)) {
      return true;
    }

    const IProfileDefinition* profile = nullptr;
    const ProfileId remote_profile_id = resolveRemoteProfileId();
    if (remote_profile_id != kProfileUnknown) {
      profile = ProfileRegistry::instance().find(remote_profile_id);
    }
    if (profile == nullptr || profile->telemetryMetrics().empty()) {
      ProfileId ignored = kProfileUnknown;
      (void)ensureRuntimeProfileKnown_(ignored, true);
      io_.writeln("[MASTER][CLI] telemetry profile unresolved; probe queued, retry command");
      return true;
    }

    push.action = (cmd == "push.start") ? TelemetryPushAction::Start : TelemetryPushAction::Update;
    push.config.stream_id = 1;
    push.config.mode = mode;
    push.config.interval_ms = interval_ms;
    push.config.min_report_gap_ms = gap_ms;
    push.config.metrics.reserve(profile->telemetryMetrics().size());

    for (const auto& spec : profile->telemetryMetrics()) {
      if (spec.key == nullptr || spec.key[0] == '\0') {
        continue;
      }
      TelemetryPushMetricConfig metric{};
      if (spec.metric_id != 0U) {
        metric.has_metric_index = true;
        metric.metric_index = spec.metric_id;
      } else {
        metric.key = spec.key;
      }
      fillMetricTiming(metric, mode, interval_ms, delta_abs, gap_ms);
      push.config.metrics.push_back(metric);
    }

    if (push.config.metrics.empty()) {
      io_.writeln("[MASTER][CLI] no telemetry metrics in profile");
      return true;
    }

    uint32_t corr = 0;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      child_push_state.semu_mask = 0U;
      child_push_state.remu_mask = 0U;
      writef("[MASTER][CLI] %s sent mode=%s metrics=%u interval_ms=%lu delta=%.3f gap_ms=%lu corr=%lu",
             cmd.c_str(),
             (mode == TelemetryPushMode::Periodic)
                 ? "periodic"
                 : ((mode == TelemetryPushMode::OnChange) ? "change" : "hybrid"),
             static_cast<unsigned int>(push.config.metrics.size()),
             static_cast<unsigned long>(interval_ms),
             static_cast<double>(delta_abs),
             static_cast<unsigned long>(gap_ms),
             static_cast<unsigned long>(corr));
    } else {
      io_.writeln("[MASTER][CLI] push command send failed");
    }
    return true;
  }

  if (cmd == "push.one" || cmd == "push.id") {
    if (tokens.size() != 6U) {
      io_.writeln("[MASTER][CLI] usage:");
      io_.writeln("  push.one <metric_key> <hybrid|periodic|change> <interval_ms> <delta_abs> <gap_ms>");
      io_.writeln("  push.id <metric_id> <hybrid|periodic|change> <interval_ms> <delta_abs> <gap_ms>");
      return true;
    }

    TelemetryPushMode mode = TelemetryPushMode::Hybrid;
    uint32_t interval_ms = 2000;
    float delta_abs = 0.10f;
    uint32_t gap_ms = 200;
    if (!parsePushTiming(2U, mode, interval_ms, delta_abs, gap_ms)) {
      return true;
    }

    TelemetryPushMetricConfig metric{};
    if (cmd == "push.id") {
      uint16_t metric_id = 0;
      if (!parseU16Token(tokens[1], metric_id) || metric_id == 0) {
        io_.writeln("[MASTER][CLI] invalid metric_id");
        return true;
      }
      metric.has_metric_index = true;
      metric.metric_index = metric_id;
    } else {
      metric.key = trim(tokens[1]);
      if (metric.key.empty()) {
        io_.writeln("[MASTER][CLI] invalid metric_key");
        return true;
      }
    }
    fillMetricTiming(metric, mode, interval_ms, delta_abs, gap_ms);

    push.action = TelemetryPushAction::Start;
    push.config.stream_id = 1;
    push.config.mode = mode;
    push.config.interval_ms = interval_ms;
    push.config.min_report_gap_ms = gap_ms;
    push.config.metrics.push_back(metric);

    uint32_t corr = 0;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool ok = submitRuntimePushCommand_(mgmt, push, &corr);
    correlation_id_ = mgmt.nextReqId();
    if (ok) {
      child_push_state.semu_mask = 0U;
      child_push_state.remu_mask = 0U;
      if (cmd == "push.id") {
        writef("[MASTER][CLI] push.id sent metric_id=0x%04X mode=%s interval_ms=%lu delta=%.3f gap_ms=%lu corr=%lu",
               static_cast<unsigned int>(metric.metric_index),
               (mode == TelemetryPushMode::Periodic)
                   ? "periodic"
                   : ((mode == TelemetryPushMode::OnChange) ? "change" : "hybrid"),
               static_cast<unsigned long>(interval_ms),
               static_cast<double>(delta_abs),
               static_cast<unsigned long>(gap_ms),
               static_cast<unsigned long>(corr));
      } else {
        writef("[MASTER][CLI] push.one sent metric=%s mode=%s interval_ms=%lu delta=%.3f gap_ms=%lu corr=%lu",
               metric.key.c_str(),
               (mode == TelemetryPushMode::Periodic)
                   ? "periodic"
                   : ((mode == TelemetryPushMode::OnChange) ? "change" : "hybrid"),
               static_cast<unsigned long>(interval_ms),
               static_cast<double>(delta_abs),
               static_cast<unsigned long>(gap_ms),
               static_cast<unsigned long>(corr));
      }
    } else {
      io_.writeln("[MASTER][CLI] push command send failed");
    }
    return true;
  }

  io_.writeln("[MASTER][CLI] unknown push command");
  captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::UnsupportedCommand, "parse");
  return true;
}

}  // namespace espnow_link
