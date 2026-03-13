#include "espnow_link/master_comm_test.hpp"

#include <algorithm>
#include <cstdlib>

namespace espnow_link {

namespace {

bool isStatusOk(ManagementStatus s) {
  return s == ManagementStatus::Ok || s == ManagementStatus::OkDeferred;
}

}  // namespace

MasterCommTest::MasterCommTest(ManagementService& mgmt, MasterPullClient& pull)
    : mgmt_(mgmt), pull_(pull) {}

bool MasterCommTest::start(uint32_t now_ms) {
  return start(now_ms, Options{});
}

bool MasterCommTest::start(uint32_t now_ms, const Options& options) {
  if (running_) {
    return false;
  }

  reset();
  options_ = options;
  started_ms_ = now_ms;
  running_ = true;
  completed_ = false;
  passed_ = true;

  buildBasePlan();
  return true;
}

void MasterCommTest::tick(uint32_t now_ms) {
  if (!running_) {
    return;
  }

  if (active_valid_) {
    const uint32_t elapsed = now_ms - active_.sent_ms;
    if (elapsed > options_.step_timeout_ms) {
      markStepResult(active_, false, ManagementStatus::Timeout, "step timeout");
      passed_ = false;
      active_valid_ = false;
    }
  }

  if (!active_valid_) {
    if (!dispatchNext(now_ms)) {
      finalizeIfDone(now_ms);
    }
  }
}

bool MasterCommTest::onResponse(const ManagementResponse& response) {
  if (!running_ || !active_valid_) {
    return false;
  }

  if (response.req_id != active_.req_id || response.cmd_id != static_cast<uint16_t>(active_.step.cmd)) {
    return false;
  }

  bool ok = isStatusOk(response.status);
  std::string detail = ok ? "ok" : "non-ok management status";

  if (ok && active_.step.require_desc) {
    bool desc_ok = true;
    if (!handleDescriptorValidation(active_, response, detail, desc_ok)) {
      ok = false;
      detail = detail.empty() ? "descriptor decode failed" : detail;
    } else {
      ok = desc_ok;
    }
  }

  if (!ok) {
    passed_ = false;
  }

  markStepResult(active_, ok, response.status, detail);
  active_valid_ = false;
  return true;
}

void MasterCommTest::onEvent(const ManagementEvent& event) {
  if (!running_) {
    return;
  }
  if (event_trace_.size() >= 48U) {
    event_trace_.erase(event_trace_.begin());
  }
  event_trace_.push_back(std::to_string(static_cast<unsigned int>(event.event_id)) + ":" +
                         std::to_string(static_cast<unsigned long>(event.req_id)) + ":" +
                         std::to_string(static_cast<unsigned int>(event.status)));
}

void MasterCommTest::reset() {
  running_ = false;
  completed_ = false;
  passed_ = true;
  started_ms_ = 0;
  ended_ms_ = 0;
  plan_.clear();
  active_ = ActiveStep{};
  active_valid_ = false;
  results_.clear();
  event_trace_.clear();
  next_req_id_ = 30000;
  remote_profile_id_ = kProfileUnknown;
  remote_profile_ = nullptr;
  settings_sweep_enqueued_ = false;
  expected_settings_count_ = 0;
  expected_telemetry_count_ = 0;
  seen_telemetry_count_ = 0;
  seen_settings_.clear();
}

MasterCommTestSummary MasterCommTest::summary() const {
  MasterCommTestSummary s{};
  s.running = running_;
  s.completed = completed_;
  s.passed = completed_ ? passed_ : false;
  s.started_ms = started_ms_;
  s.ended_ms = ended_ms_;
  s.total = static_cast<uint16_t>(results_.size() + plan_.size() + (active_valid_ ? 1 : 0));
  s.passed_count = static_cast<uint16_t>(std::count_if(results_.begin(), results_.end(),
                                                       [](const MasterCommTestStepResult& r) { return r.passed; }));
  s.failed_count = static_cast<uint16_t>(results_.size() - s.passed_count);
  if (remote_profile_ != nullptr && remote_profile_->profileName() != nullptr) {
    s.profile_name = remote_profile_->profileName();
  } else if (remote_profile_id_ != kProfileUnknown) {
    s.profile_name = "id:" + std::to_string(static_cast<unsigned long>(remote_profile_id_));
  } else {
    s.profile_name.clear();
  }
  s.expected_settings_count = expected_settings_count_;
  s.seen_settings_count = static_cast<uint32_t>(seen_settings_.size());
  s.expected_telemetry_count = expected_telemetry_count_;
  s.seen_telemetry_count = seen_telemetry_count_;
  return s;
}

void MasterCommTest::buildBasePlan() {
  plan_.clear();

  Step step{};
  step.name = "status.get";
  step.cmd = ManagementCommandId::StatusGet;
  enqueueStep(step);

  step = Step{};
  step.name = "desc.get";
  step.cmd = ManagementCommandId::DescGet;
  step.require_desc = true;
  step.expected_desc = DescriptorResponseType::Device;
  enqueueStep(step);

  step = Step{};
  step.name = "caps.get";
  step.cmd = ManagementCommandId::CapsGet;
  step.require_desc = true;
  step.expected_desc = DescriptorResponseType::Capabilities;
  step.parse_caps = true;
  enqueueStep(step);

  step = Step{};
  step.name = "telem.schema.get";
  step.cmd = ManagementCommandId::TelemSchemaGet;
  step.require_desc = true;
  step.expected_desc = DescriptorResponseType::Telemetry;
  step.parse_telem_schema = true;
  enqueueStep(step);

  step = Step{};
  step.name = "telem.pull";
  step.cmd = ManagementCommandId::TelemPull;
  step.require_desc = true;
  step.expected_desc = DescriptorResponseType::TelemetrySnapshot;
  step.require_telem_snapshot_non_empty = true;
  enqueueStep(step);

  step = Step{};
  step.name = "live.get";
  step.cmd = ManagementCommandId::LiveGet;
  step.require_desc = true;
  step.expected_desc = DescriptorResponseType::Liveness;
  enqueueStep(step);

  step = Step{};
  step.name = "time.get";
  step.cmd = ManagementCommandId::TimeGet;
  step.require_desc = true;
  step.expected_desc = DescriptorResponseType::Time;
  enqueueStep(step);

  if (options_.include_push_get) {
    TelemetryPushCommand cmd{};
    cmd.action = TelemetryPushAction::Get;
    std::vector<uint8_t> payload;
    if (encodeTelemetryPushCommand(cmd, payload)) {
      step = Step{};
      step.name = "push.get";
      step.cmd = ManagementCommandId::PushGet;
      step.payload = payload;
      enqueueStep(step);
    }
  }
}

void MasterCommTest::enqueueStep(const Step& step) {
  plan_.push_back(step);
}

bool MasterCommTest::dispatchNext(uint32_t now_ms) {
  if (plan_.empty()) {
    return false;
  }

  Step step = plan_.front();
  plan_.pop_front();

  ManagementRequest req{};
  req.source = options_.source;
  req.cmd_id = static_cast<uint16_t>(step.cmd);
  req.req_id = nextReqId();
  req.timeout_ms = options_.step_timeout_ms;
  req.payload = step.payload;

  if (!mgmt_.submit(req)) {
    passed_ = false;
    MasterCommTestStepResult r{};
    r.name = step.name;
    r.cmd_id = req.cmd_id;
    r.req_id = req.req_id;
    r.passed = false;
    r.status = ManagementStatus::QueueFull;
    r.detail = "submit failed";
    results_.push_back(r);
    return !plan_.empty();
  }

  ActiveStep active{};
  active.step = step;
  active.req_id = req.req_id;
  active.sent_ms = now_ms;
  active_ = active;
  active_valid_ = true;
  return true;
}

void MasterCommTest::finalizeIfDone(uint32_t now_ms) {
  if (active_valid_ || !plan_.empty()) {
    return;
  }
  if (options_.include_settings_full && expected_settings_count_ > 0U &&
      static_cast<uint32_t>(seen_settings_.size()) != expected_settings_count_) {
    passed_ = false;
    MasterCommTestStepResult r{};
    r.name = "settings.count.check";
    r.cmd_id = static_cast<uint16_t>(ManagementCommandId::SettingGet);
    r.req_id = 0;
    r.passed = false;
    r.status = ManagementStatus::InternalError;
    r.detail = "settings count mismatch";
    results_.push_back(r);
  }

  if (expected_telemetry_count_ > 0U && seen_telemetry_count_ > 0U &&
      seen_telemetry_count_ != expected_telemetry_count_) {
    passed_ = false;
    MasterCommTestStepResult r{};
    r.name = "telemetry.count.check";
    r.cmd_id = static_cast<uint16_t>(ManagementCommandId::TelemSchemaGet);
    r.req_id = 0;
    r.passed = false;
    r.status = ManagementStatus::InternalError;
    r.detail = "telemetry count mismatch";
    results_.push_back(r);
  }

  running_ = false;
  completed_ = true;
  ended_ms_ = now_ms;
}

void MasterCommTest::markStepResult(const ActiveStep& active,
                                    bool passed,
                                    ManagementStatus status,
                                    const std::string& detail) {
  MasterCommTestStepResult r{};
  r.name = active.step.name;
  r.cmd_id = static_cast<uint16_t>(active.step.cmd);
  r.req_id = active.req_id;
  r.passed = passed;
  r.status = status;
  r.detail = detail;
  results_.push_back(r);
}

bool MasterCommTest::handleDescriptorValidation(const ActiveStep& active,
                                                const ManagementResponse& response,
                                                std::string& out_detail,
                                                bool& out_passed) {
  PullResponseDecoded decoded{};
  if (!pull_.decodePullResponseWithActiveCodec(response.payload.data(), response.payload.size(), decoded)) {
    out_detail = "descriptor decode failed";
    out_passed = false;
    return true;
  }
  if (decoded.kind != PullResponseKind::Descriptor) {
    out_detail = "decoded kind mismatch";
    out_passed = false;
    return true;
  }

  const DescriptorResponse& d = decoded.descriptor;
  if (active.step.expected_desc != DescriptorResponseType::Unknown && d.type != active.step.expected_desc) {
    out_detail = "descriptor response type mismatch";
    out_passed = false;
    return true;
  }

  if (active.step.parse_caps) {
    deriveProfileFromCapabilities(d);
    if (options_.include_settings_full && !settings_sweep_enqueued_) {
      enqueueSettingsSweepFromProfile();
      settings_sweep_enqueued_ = true;
    }
  }

  if (active.step.parse_telem_schema) {
    seen_telemetry_count_ = static_cast<uint32_t>(d.telemetry.size());
  }

  if (active.step.parse_setting && d.type == DescriptorResponseType::Setting) {
    seen_settings_.insert(d.setting.setting_id);
  }

  if (active.step.require_telem_snapshot_non_empty && d.telemetry_samples.empty()) {
    out_detail = "telemetry snapshot is empty";
    out_passed = false;
    return true;
  }

  out_detail = "ok";
  out_passed = true;
  return true;
}

void MasterCommTest::deriveProfileFromCapabilities(const DescriptorResponse& d) {
  for (const auto& c : d.capabilities) {
    if (c.key == "profile_id") {
      const unsigned long profile_id = std::strtoul(c.description.c_str(), nullptr, 10);
      if (profile_id > 0U && profile_id <= 0xFFFFUL) {
        remote_profile_id_ = static_cast<ProfileId>(profile_id);
      }
    } else if (c.key == "settings_count") {
      expected_settings_count_ = static_cast<uint32_t>(std::strtoul(c.description.c_str(), nullptr, 10));
    } else if (c.key == "telemetry_count") {
      expected_telemetry_count_ = static_cast<uint32_t>(std::strtoul(c.description.c_str(), nullptr, 10));
    }
  }

  remote_profile_ = nullptr;
  if (remote_profile_id_ != kProfileUnknown) {
    remote_profile_ = ProfileRegistry::instance().find(remote_profile_id_);
  }
}

void MasterCommTest::enqueueSettingsSweepFromProfile() {
  if (remote_profile_ == nullptr) {
    return;
  }

  const auto& settings = remote_profile_->settings();
  if (expected_settings_count_ == 0U) {
    expected_settings_count_ = static_cast<uint32_t>(settings.size());
  }
  if (settings.empty()) {
    return;
  }

  // Insert in reverse at front so first setting remains first executed.
  for (size_t i = settings.size(); i > 0; --i) {
    const auto& spec = settings[i - 1];
    Step s{};
    s.name = "setting.get.id." + std::to_string(static_cast<unsigned int>(spec.setting_id));
    s.cmd = ManagementCommandId::SettingGet;
    s.payload.push_back(1);  // mode by id
    s.payload.push_back(static_cast<uint8_t>(spec.setting_id & 0xFF));
    s.payload.push_back(static_cast<uint8_t>((spec.setting_id >> 8) & 0xFF));
    s.require_desc = true;
    s.expected_desc = DescriptorResponseType::Setting;
    s.parse_setting = true;
    plan_.push_front(s);
  }
}

uint32_t MasterCommTest::nextReqId() {
  return next_req_id_++;
}

}  // namespace espnow_link
