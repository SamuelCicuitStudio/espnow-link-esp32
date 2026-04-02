/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: System/meta/list/status/pairing command handling paths.
 **************************************************************/
#include "../internal/cli_dispatch_internal.hpp"
#include "../internal/cli_dispatch_helpers_inline.hpp"

namespace espnow_link {

using namespace cli_helpers;

bool MasterCli::handleTestAndLocalCommands(const std::string& lower) {
  if (lower == "test.all" || lower == "selftest" || lower == "comm.test") {
    enum class StepPolicy : uint8_t {
      Run = 0,
      SkipRequiresInput,
      SkipDestructive,
      SkipProfileChildOnly,
      SkipHeavy,
      SkipRequiresActive,
    };
    struct TestStep {
      const char* area;
      const char* cmd;
      bool needs_target;
      StepPolicy policy;
    };

    static constexpr TestStep kSteps[] = {
        {"master", "status", false, StepPolicy::Run},
        {"master", "paired", false, StepPolicy::Run},
        {"master", "cli status", false, StepPolicy::Run},
        {"test/diag", "radio.drytest", false, StepPolicy::Run},

        {"desc/profile", "desc", true, StepPolicy::Run},
        {"desc/profile", "caps", true, StepPolicy::Run},
        {"desc/profile", "telem", true, StepPolicy::Run},
        {"desc/profile", "telem.now", true, StepPolicy::Run},
        {"desc/profile", "telem.now.child 0", true, StepPolicy::SkipProfileChildOnly},
        {"desc/profile", "live", true, StepPolicy::Run},
        {"desc/profile", "ping", true, StepPolicy::Run},
        {"desc/profile", "audio ping", true, StepPolicy::Run},

        {"settings", "settings", true, StepPolicy::Run},
        {"settings", "settings.full", true, StepPolicy::Run},
        {"settings", "settings.raw", true, StepPolicy::Run},
        {"settings", "get <setting_key>", true, StepPolicy::SkipRequiresInput},
        {"settings", "get.id <setting_id>", true, StepPolicy::SkipRequiresInput},
        {"settings", "set <setting_key>=<value>", true, StepPolicy::SkipDestructive},
        {"settings", "set.id <setting_id>=<value>", true, StepPolicy::SkipDestructive},

        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.start hybrid 2000 0.1 200", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.update hybrid 2000 0.1 200", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.pause", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.resume", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.stop", true, StepPolicy::Run},
        {"push", "push.get", true, StepPolicy::Run},
        {"push", "push.one <key> <mode> <int> <d> <g>", true, StepPolicy::SkipRequiresInput},
        {"push", "push.id <id> <mode> <int> <d> <g>", true, StepPolicy::SkipRequiresInput},
        {"push", "push.child.start <vid> [mode] [int] [d] [g]", true, StepPolicy::SkipProfileChildOnly},
        {"push", "push.child.stop <vid>", true, StepPolicy::SkipProfileChildOnly},
        {"push", "autopull on 2000", true, StepPolicy::Run},
        {"push", "autopull off", false, StepPolicy::Run},

        {"time", "time.get", true, StepPolicy::Run},
        {"time", "time.set <epoch_s>", true, StepPolicy::SkipDestructive},
        {"time", "time.set.now", true, StepPolicy::SkipDestructive},
        {"time", "time.local", false, StepPolicy::Run},

        {"lifecycle", "restart master", false, StepPolicy::SkipDestructive},
        {"lifecycle", "reset master", false, StepPolicy::SkipDestructive},
        {"lifecycle", "restart slave", true, StepPolicy::SkipDestructive},
        {"lifecycle", "reset slave", true, StepPolicy::SkipDestructive},

        {"test/diag", "comm.test.status", true, StepPolicy::Run},
        {"test/diag", "comm.test.report", true, StepPolicy::Run},
        {"test/diag", "radio.drytest", false, StepPolicy::Run},
        {"test/diag", "metrics", false, StepPolicy::Run},
        {"test/diag", "metrics.reset", false, StepPolicy::Run},
        {"test/diag", "queue", false, StepPolicy::Run},

        {"log", "log", false, StepPolicy::Run},
        {"log", "log error", false, StepPolicy::Run},
        {"log", "log info", false, StepPolicy::Run},
        {"log", "log debug", false, StepPolicy::Run},

        {"logger.local", "logger.status", false, StepPolicy::Run},
        {"logger.local", "logger.enable", false, StepPolicy::Run},
        {"logger.local", "logger.disable", false, StepPolicy::Run},
        {"logger.local", "logger.clear", false, StepPolicy::SkipDestructive},
        {"logger.local", "logger.read 0 64", false, StepPolicy::Run},

        {"logger.remote", "logger.remote.status", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.enable", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.disable", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.clear", true, StepPolicy::SkipDestructive},
        {"logger.remote", "logger.remote.read 0 64", true, StepPolicy::Run},
        {"logger.remote", "logger.remote.pull 64", true, StepPolicy::SkipHeavy},
        {"logger.remote", "logger.remote.stop", true, StepPolicy::SkipRequiresActive},
        {"channel/chain", "channel.runtime.status", false, StepPolicy::Run},
        {"channel/chain", "channel.sync <1..14>", false, StepPolicy::SkipDestructive},
        {"channel/chain", "chain.loop.status", false, StepPolicy::Run},
        {"channel/chain", "chain.loop.on", false, StepPolicy::SkipDestructive},
        {"channel/chain", "chain.loop.off", false, StepPolicy::SkipDestructive},

        {"storage.local", "sd.info", false, StepPolicy::Run},
        {"storage.local", "sd.pwd", false, StepPolicy::Run},
        {"storage.local", "sd.ls /", false, StepPolicy::Run},
        {"storage.local", "sd.cd /", false, StepPolicy::Run},
        {"storage.local", "sd.up", false, StepPolicy::Run},
        {"storage.local", "sd.stat /", false, StepPolicy::Run},
        {"storage.local", "sd.format", false, StepPolicy::SkipDestructive},

        {"storage.remote", "sd.remote.info", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.pwd", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.ls /", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.cd /", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.up", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.stat /", true, StepPolicy::Run},
        {"storage.remote", "sd.remote.format", true, StepPolicy::SkipDestructive},
    };

    MacAddress runtime_peer{};
    const bool has_runtime_peer = resolveRuntimePeer(runtime_peer);
    const std::string target_peer_text = has_runtime_peer ? macToPrintable(runtime_peer) : "none";
    std::string target_prefix{};
    if (has_runtime_peer) {
      target_prefix = target_peer_text;
      target_prefix.push_back(' ');
    }

    const bool child_profile =
        (remote_profile_id_ == kProfileSemu) || (remote_profile_id_ == kProfileRemu);

    auto policyReason = [&](StepPolicy policy) -> const char* {
      switch (policy) {
        case StepPolicy::SkipRequiresInput:
          return "requires explicit argument(s)";
        case StepPolicy::SkipDestructive:
          return "destructive/mutating command";
        case StepPolicy::SkipProfileChildOnly:
          return "child telemetry command (SEMU/REMU only)";
        case StepPolicy::SkipHeavy:
          return "heavy/long-running operation";
        case StepPolicy::SkipRequiresActive:
          return "requires active runtime state";
        case StepPolicy::Run:
        default:
          return "";
      }
    };

    auto logLevelLabel = [](CliLogLevel level) -> const char* {
      switch (level) {
        case CliLogLevel::Error:
          return "error";
        case CliLogLevel::Debug:
          return "debug";
        case CliLogLevel::Info:
        default:
          return "info";
      }
    };

    const CliLogLevel saved_log_level = log_level_;
    if (log_level_ != CliLogLevel::Debug) {
      log_level_ = CliLogLevel::Debug;
    }

    io_.writeln("[MASTER][TEST] test.all dispatch sweep started");
    writef("  target=%s profile_id=0x%04X",
           target_peer_text.c_str(),
           static_cast<unsigned int>(remote_profile_id_));
    io_.writeln("  mode=non-destructive (dangerous commands are skipped)");
    io_.writeln("  detail=dispatch pass/fail now; async remote failures still print later as [MASTER][MGMT]/[MASTER][DESC]");

    auto hasMgmtQueuePending = [&]() -> bool {
      if (management_transport_ == nullptr) {
        return false;
      }
      return (management_transport_->pendingRequestCount() != 0U) ||
             (management_transport_->pendingResponseCount() != 0U) ||
             (management_transport_->pendingEventCount() != 0U);
    };

    auto pumpInfra = [&](uint32_t budget_ms) {
      const uint32_t start_ms = nowMs();
      while (static_cast<uint32_t>(nowMs() - start_ms) < budget_ms) {
        const uint32_t now = nowMs();
        if (shouldTickManagementRuntimeFromCli_()) {
          management_runtime_->tick(now, 4U, 16U, 32U);
        }
        pumpManagementMailbox();
        pumpDescriptorQueue(now);
        if (!hasMgmtQueuePending() && descriptor_request_queue_.empty()) {
          break;
        }
        yieldCliBusyWaitLoop();
      }
    };

    auto waitPagedFetch = [&](const char* label, uint32_t timeout_ms) -> bool {
      if (paged_fetch_kind_ == PagedFetchKind::None) {
        return true;
      }
      const uint32_t start_ms = nowMs();
      while (paged_fetch_kind_ != PagedFetchKind::None &&
             static_cast<uint32_t>(nowMs() - start_ms) < timeout_ms) {
        pumpInfra(40U);
      }
      if (paged_fetch_kind_ != PagedFetchKind::None) {
        writef("[MASTER][TEST][WARN] paged fetch still active after timeout: %s", label);
        return false;
      }
      return true;
    };

    uint32_t run_total = 0U;
    uint32_t run_pass = 0U;
    uint32_t run_fail = 0U;
    uint32_t skipped = 0U;

    for (size_t i = 0; i < (sizeof(kSteps) / sizeof(kSteps[0])); ++i) {
      const TestStep& step = kSteps[i];
      const std::string step_cmd(step.cmd);

      if (step_cmd == "settings.full" && remote_profile_id_ == kProfileUnknown) {
        pumpInfra(600U);
        if (remote_profile_id_ == kProfileUnknown) {
          ++skipped;
          writef("[MASTER][TEST][SKIP] #%u %s :: %s (profile still unknown after caps sync)",
                 static_cast<unsigned int>(i + 1U),
                 step.area,
                 step.cmd);
          continue;
        }
      }

      if (step_cmd == "radio.drytest" &&
          (hasMgmtQueuePending() || !descriptor_request_queue_.empty())) {
        ++skipped;
        writef("[MASTER][TEST][SKIP] #%u %s :: %s (management queues are busy)",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd);
        continue;
      }

      if (step.policy == StepPolicy::SkipRequiresActive &&
          step_cmd == "logger.remote.stop" &&
          !remote_log_pull_active_) {
        ++skipped;
        writef("[MASTER][TEST][SKIP] #%u %s :: %s (no active remote pull)",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd);
        continue;
      }

      if (step.policy != StepPolicy::Run) {
        if (step.policy == StepPolicy::SkipProfileChildOnly && child_profile) {
          // SEMU/REMU selected: run child-only steps when explicit args are fixed in step text.
        } else if (step.policy == StepPolicy::SkipRequiresActive &&
                   step_cmd == "logger.remote.stop" &&
                   remote_log_pull_active_) {
          // Active pull is present: run stop command as part of flow validation.
        } else {
          ++skipped;
          writef("[MASTER][TEST][SKIP] #%u %s :: %s (%s)",
                 static_cast<unsigned int>(i + 1U),
                 step.area,
                 step.cmd,
                 policyReason(step.policy));
          continue;
        }
      }

      if (step.needs_target && !has_runtime_peer) {
        ++skipped;
        writef("[MASTER][TEST][SKIP] #%u %s :: %s (target not selected)",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd);
        continue;
      }

      const std::string cmd_line = step.needs_target ? (target_prefix + step.cmd) : std::string(step.cmd);
      const CommandDispatchResult r = handleLineEx(cmd_line);

      ++run_total;
      bool ok = r.parsed && r.handled;
      if (ok && r.submitted) {
        ok = r.accepted;
      } else if (ok && !r.submitted && r.status != ManagementStatus::Ok) {
        ok = r.accepted;
      }

      if (ok) {
        ++run_pass;
        writef("[MASTER][TEST][PASS] #%u %s :: %s submitted=%s cmd=0x%04X req=%lu status=%s",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd,
               r.submitted ? "yes" : "no",
               static_cast<unsigned int>(r.cmd_id),
               static_cast<unsigned long>(r.req_id),
               management_utils::managementStatusToString(r.status));
      } else {
        ++run_fail;
        writef("[MASTER][TEST][FAIL] #%u %s :: %s submitted=%s accepted=%s status=%s stage=%s",
               static_cast<unsigned int>(i + 1U),
               step.area,
               step.cmd,
               r.submitted ? "yes" : "no",
               r.accepted ? "yes" : "no",
               management_utils::managementStatusToString(r.status),
               r.reject_stage.empty() ? "n/a" : r.reject_stage.c_str());
      }

      if (step_cmd == "caps" || step_cmd == "telem" ||
          step_cmd == "settings" || step_cmd == "settings.raw") {
        (void)waitPagedFetch(step.cmd, 2500U);
      } else {
        pumpInfra(120U);
      }
    }

    log_level_ = saved_log_level;
    writef("[MASTER][TEST] summary run=%lu pass=%lu fail=%lu skipped=%lu",
           static_cast<unsigned long>(run_total),
           static_cast<unsigned long>(run_pass),
           static_cast<unsigned long>(run_fail),
           static_cast<unsigned long>(skipped));
    writef("[MASTER][TEST] log level restored=%s", logLevelLabel(log_level_));
    return true;
  }

  if (lower == "comm.test.status" || lower == "comm.test.report") {
    const char* probe_kind = "none";
    if (probe_pending_kind_ == ProbePendingKind::Live) {
      probe_kind = "live";
    } else if (probe_pending_kind_ == ProbePendingKind::Ping) {
      probe_kind = "ping";
    }
    MacAddress runtime_peer{};
    const bool has_runtime_peer = resolveRuntimePeer(runtime_peer);
    io_.writeln("[MASTER][CLI] comm.test status (this build uses selftest queue mode):");
    writef("  paired=%s peer=%s",
           manager_.isPaired() ? "yes" : "no",
           has_runtime_peer ? macToPrintable(runtime_peer).c_str() : "none");
    writef("  probe_pending=%s age_ms=%lu",
           probe_kind,
           (probe_pending_kind_ != ProbePendingKind::None) ? static_cast<unsigned long>(nowMs() - probe_sent_ms_) : 0UL);
    writef("  autopull=%s interval_ms=%lu live_online=%s",
           auto_pull_enabled_ ? "on" : "off",
           static_cast<unsigned long>(auto_pull_interval_ms_),
           auto_pull_.slaveOnline() ? "yes" : "no");
    io_.writeln("  trigger full flow with: test.all");
    printQueueStatus();
    return true;
  }

  if (lower == "radio.drytest") {
    if (management_ == nullptr) {
      io_.writeln("[MASTER][RADIO][DRYTEST] management service unavailable");
      return true;
    }
    if (management_transport_ == nullptr || management_runtime_ == nullptr) {
      io_.writeln("[MASTER][RADIO][DRYTEST] management runtime transport unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }

    auto stateLabel = [](ManagementService::RadioTransitionState s) -> const char* {
      switch (s) {
        case ManagementService::RadioTransitionState::Idle:
          return "idle";
        case ManagementService::RadioTransitionState::Quiescing:
          return "quiescing";
        case ManagementService::RadioTransitionState::Paused:
          return "paused";
        case ManagementService::RadioTransitionState::Resuming:
          return "resuming";
        case ManagementService::RadioTransitionState::Failed:
          return "failed";
        default:
          return "unknown";
      }
    };

    const size_t tx_req = management_transport_->pendingRequestCount();
    const size_t tx_resp = management_transport_->pendingResponseCount();
    const size_t tx_evt = management_transport_->pendingEventCount();
    const size_t svc_req = management_->pendingRequestCount();
    const size_t svc_resp = management_->pendingResponseCount();
    const size_t svc_evt = management_->pendingEventCount();
    if (tx_req != 0U || tx_resp != 0U || tx_evt != 0U || svc_req != 0U || svc_resp != 0U || svc_evt != 0U) {
      writef("[MASTER][RADIO][DRYTEST] busy queues tx(req=%u resp=%u evt=%u) svc(req=%u resp=%u evt=%u); rerun when idle",
             static_cast<unsigned int>(tx_req),
             static_cast<unsigned int>(tx_resp),
             static_cast<unsigned int>(tx_evt),
             static_cast<unsigned int>(svc_req),
             static_cast<unsigned int>(svc_resp),
             static_cast<unsigned int>(svc_evt));
      return true;
    }

    ManagementService::RadioTransitionStatus before{};
    management_->radioTransitionStatusGet(before);
    writef("[MASTER][RADIO][DRYTEST] pre active=%s state=%s epoch=%lu",
           before.active ? "yes" : "no",
           stateLabel(before.state),
           static_cast<unsigned long>(before.radio_epoch));
    if (before.active) {
      io_.writeln("[MASTER][RADIO][DRYTEST] abort: transition already active");
      return true;
    }

    const bool begin_ok = management_->beginRadioTransition();
    ManagementService::RadioTransitionStatus after_begin{};
    management_->radioTransitionStatusGet(after_begin);
    writef("[MASTER][RADIO][DRYTEST] begin=%s active=%s state=%s epoch=%lu",
           begin_ok ? "ok" : "fail",
           after_begin.active ? "yes" : "no",
           stateLabel(after_begin.state),
           static_cast<unsigned long>(after_begin.radio_epoch));
    if (!begin_ok) {
      io_.writeln("[MASTER][RADIO][DRYTEST] FAIL begin transition");
      return true;
    }

    uint32_t dry_req_id = 0U;
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool submit_ok = submitRuntimeTargeted_(mgmt,
                                                  static_cast<uint16_t>(ManagementCommandId::DiscoveryStart),
                                                  management_utils::buildDiscoveryStartPayload(1000U),
                                                  &dry_req_id,
                                                  1000U,
                                                  false);
    correlation_id_ = mgmt.nextReqId();

    writef("[MASTER][RADIO][DRYTEST] blocked_cmd=DiscoveryStart submit=%s req=%lu path=%s",
           submit_ok ? "ok" : "fail",
           static_cast<unsigned long>(dry_req_id),
           "queue_runtime");

    bool status_seen = false;
    ManagementStatus status_value = ManagementStatus::InternalError;
    const uint32_t deadline_ms = nowMs() + 1500U;
    while (!status_seen && static_cast<int32_t>(nowMs() - deadline_ms) < 0) {
      if (shouldTickManagementRuntimeFromCli_()) {
        management_runtime_->tick(nowMs(), 4U, 16U, 32U);
      }

      ManagementResponse resp{};
      while (management_transport_->pollResponse(resp)) {
        if (resp.req_id == dry_req_id) {
          status_seen = true;
          status_value = resp.status;
          break;
        }
      }
      if (status_seen) {
        break;
      }

      ManagementEvent evt{};
      while (management_transport_->pollEvent(evt)) {
        if (evt.req_id == dry_req_id) {
          status_seen = true;
          status_value = evt.status;
          break;
        }
      }
      yieldCliBusyWaitLoop();
    }

    const bool end_ok = management_->endRadioTransition();
    ManagementService::RadioTransitionStatus after_end{};
    management_->radioTransitionStatusGet(after_end);
    writef("[MASTER][RADIO][DRYTEST] end=%s active=%s state=%s epoch=%lu",
           end_ok ? "ok" : "fail",
           after_end.active ? "yes" : "no",
           stateLabel(after_end.state),
           static_cast<unsigned long>(after_end.radio_epoch));

    const bool blocked_ok = submit_ok &&
                            status_seen &&
                            status_value == ManagementStatus::BusyRadioTransition;
    writef("[MASTER][RADIO][DRYTEST] blocked_status seen=%s value=%s(0x%04X)",
           status_seen ? "yes" : "no",
           management_utils::managementStatusToString(status_value),
           static_cast<unsigned int>(status_value));

    const bool pass = begin_ok && blocked_ok && end_ok && !after_end.active;
    io_.writeln(pass ? "[MASTER][RADIO][DRYTEST] PASS" : "[MASTER][RADIO][DRYTEST] FAIL");
    return true;
  }

  if (lower == "time.local") {
    const uint64_t now_s = static_cast<uint64_t>(std::time(nullptr));
    writef("[MASTER][TIME] local_epoch_s=%llu", static_cast<unsigned long long>(now_s));
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");
    return true;
  }

  if (lower == "cli.baud" || lower == "cli.baud.get" ||
      lower == "cli.speed" || lower == "cli.speed.get") {
    uint32_t baud = static_cast<uint32_t>(PCAT_ICM_SET_CLIBD_DEF);
    const bool read_ok = loadPersistedIcmCliBaud(baud);
    if (!isSupportedIcmCliBaud(baud)) {
      baud = static_cast<uint32_t>(PCAT_ICM_SET_CLIBD_DEF);
    }
    writef("[MASTER][CLI] cli.baud current=%lu default=%lu supported=[%s]",
           static_cast<unsigned long>(baud),
           static_cast<unsigned long>(PCAT_ICM_SET_CLIBD_DEF),
           supportedIcmCliBaudList());
    captureDispatchSnapshot_(read_ok, 0U, 0U, read_ok ? ManagementStatus::Ok : ManagementStatus::InternalError,
                             read_ok ? "" : "nvs_read");
    return true;
  }

  if (startsWith(lower, "cli.baud set ") || startsWith(lower, "cli.speed set ")) {
    const size_t prefix_len = startsWith(lower, "cli.baud set ") ? 13U : 14U;
    const std::string arg = trim(lower.substr(prefix_len));
    uint32_t baud = 0U;
    if (!parseU32Token(arg, baud) || !isSupportedIcmCliBaud(baud)) {
      io_.writeln("[MASTER][CLI] usage: cli.baud set <baud>");
      writef("[MASTER][CLI] supported baud values: %s", supportedIcmCliBaudList());
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::BadPayload, "validation");
      return true;
    }

    if (!savePersistedIcmCliBaud(baud)) {
      io_.writeln("[MASTER][CLI] failed to persist cli baud");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::InternalError, "nvs_write");
      return true;
    }

    writef("[MASTER][CLI] cli.baud saved=%lu (restart required)",
           static_cast<unsigned long>(baud));
    captureDispatchSnapshot_(true, 0U, 0U, ManagementStatus::Ok, "");

    if (actions_ != nullptr) {
      io_.writeln("[MASTER][CLI] restarting master to apply CLI baud");
      actions_->requestMasterRestart(false);
    } else {
      io_.writeln("[MASTER][CLI] restart hook unavailable; run: restart master");
    }
    return true;
  }

  return false;
}

bool MasterCli::handleRestartResetCommands(const std::string& lower) {
  if (lower == "restart master" || lower == "reset master") {
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    const bool reset = (lower == "reset master");
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool submitted = reset
                               ? submitRuntimeTargeted_(mgmt,
                                                        static_cast<uint16_t>(ManagementCommandId::ResetMasterRequest),
                                                        {},
                                                        nullptr,
                                                        0U,
                                                        false)
                               : submitRuntimeTargeted_(mgmt,
                                                        static_cast<uint16_t>(ManagementCommandId::RestartMasterRequest),
                                                        {},
                                                        nullptr,
                                                        0U,
                                                        false);
    correlation_id_ = mgmt.nextReqId();
    if (submitted) {
      io_.writeln("[MASTER][CLI] master restart/reset requested via management");
    } else {
      io_.writeln("[MASTER][CLI] master restart/reset request failed (management queue full)");
    }
    return true;
  }

  if (lower == "restart slave" || lower == "reset slave") {
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
    const bool sent = (lower == "restart slave")
                          ? submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::RestartSlaveRequest))
                          : submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::ResetSlaveRequest));
    correlation_id_ = mgmt.nextReqId();
    MacAddress runtime_peer{};
    (void)resolveRuntimePeer(runtime_peer);

    writef("[MASTER][CLI] %s slave request cmd=%s peer=%s",
           (lower == "reset slave") ? "reset" : "restart",
           sent ? "sent" : "send-failed/queue-full",
           macToPrintable(runtime_peer).c_str());
    return true;
  }

  return false;
}
  
bool MasterCli::handleLogLevelCommand(const std::string& lower) {
  if (lower == "log") {
    const char* lvl = (log_level_ == CliLogLevel::Debug)
                          ? "debug"
                          : (log_level_ == CliLogLevel::Info ? "info" : "error");
    writef("[MASTER][CLI] log level=%s", lvl);
    return true;
  }

  if (lower == "log error") {
    log_level_ = CliLogLevel::Error;
    io_.writeln("[MASTER][CLI] log level=error");
    return true;
  }
  if (lower == "log info") {
    log_level_ = CliLogLevel::Info;
    io_.writeln("[MASTER][CLI] log level=info");
    return true;
  }
  if (lower == "log debug") {
    log_level_ = CliLogLevel::Debug;
    io_.writeln("[MASTER][CLI] log level=debug");
    return true;
  }

  return false;
}

bool MasterCli::handleCliMetaCommands(const std::string& lower) {
  if (lower == "help") {
    printHelp();
    return true;
  }

  if (startsWith(lower, "help ")) {
    const std::string topic = trim(lower.substr(5));
    if (topic.empty()) {
      printHelp();
      return true;
    }
    if (!printTopicHelp(topic)) {
      writef("[MASTER][CLI] unknown help topic: %s", topic.c_str());
      io_.writeln("  run: help");
      io_.writeln("  topics: core paired pairing target topology desc settings push time control test log logger sd ota");
    }
    return true;
  }

  static constexpr const char* kHelpSuffix = " help";
  if (lower.size() > std::strlen(kHelpSuffix) &&
      lower.compare(lower.size() - std::strlen(kHelpSuffix), std::strlen(kHelpSuffix), kHelpSuffix) == 0) {
    const std::string topic = trim(lower.substr(0U, lower.size() - std::strlen(kHelpSuffix)));
    if (topic.empty()) {
      printHelp();
      return true;
    }
    if (!printTopicHelp(topic)) {
      writef("[MASTER][CLI] unknown help topic: %s", topic.c_str());
      io_.writeln("  run: help");
      io_.writeln("  topics: core paired pairing target topology desc settings push time control test log logger sd ota");
    }
    return true;
  }

  if (handleLogLevelCommand(lower)) {
    return true;
  }

  if (lower == "queue") {
    printQueueStatus();
    return true;
  }

  if (lower == "cli status") {
    const char* lvl = (log_level_ == CliLogLevel::Debug)
                          ? "debug"
                          : (log_level_ == CliLogLevel::Info ? "info" : "error");
    writef("[MASTER][CLI] enabled=%s key=%s log=%s",
           cli_enabled_ ? "yes" : "no",
           enable_key_.c_str(),
           lvl);
    printQueueStatus();
    return true;
  }

  if (lower == "cli on") {
    if (!setCliEnabled(true, true)) {
      io_.writeln("[MASTER][CLI] failed to persist enable state");
    }
    io_.writeln("[MASTER][CLI] enabled");
    return true;
  }

  if (lower == "metrics") {
    const ManagerRuntimeMetrics& m = manager_.runtimeMetrics();
    io_.writeln("[MASTER][METRICS] counters:");
    writef("  tick_count=%llu", static_cast<unsigned long long>(m.tick_count));
    writef("  rx_frames=%llu rx_bytes=%llu", static_cast<unsigned long long>(m.rx_frames), static_cast<unsigned long long>(m.rx_bytes));
    writef("  tx_frames=%llu tx_bytes=%llu tx_failures=%llu",
           static_cast<unsigned long long>(m.tx_frames),
           static_cast<unsigned long long>(m.tx_bytes),
           static_cast<unsigned long long>(m.tx_failures));
    io_.writeln("[MASTER][METRICS] timing_us:");
    writef("  tick last=%lu max=%lu avg=%lu",
           static_cast<unsigned long>(m.tick_last_us),
           static_cast<unsigned long>(m.tick_max_us),
           static_cast<unsigned long>((m.tick_count == 0) ? 0 : (m.tick_total_us / m.tick_count)));
    writef("  rx   last=%lu max=%lu avg=%lu",
           static_cast<unsigned long>(m.rx_handler_last_us),
           static_cast<unsigned long>(m.rx_handler_max_us),
           static_cast<unsigned long>((m.rx_frames == 0) ? 0 : (m.rx_handler_total_us / m.rx_frames)));
    writef("  tx   last=%lu max=%lu avg=%lu",
           static_cast<unsigned long>(m.tx_send_last_us),
           static_cast<unsigned long>(m.tx_send_max_us),
           static_cast<unsigned long>((m.tx_frames == 0) ? 0 : (m.tx_send_total_us / m.tx_frames)));

    if (management_runtime_ != nullptr) {
      const ManagementRuntime::Stats& ms = management_runtime_->stats();
      io_.writeln("[MASTER][METRICS][MGMT] runtime:");
      writef("  submitted=%lu dropped_req=%lu",
             static_cast<unsigned long>(ms.submitted_requests),
             static_cast<unsigned long>(ms.dropped_requests));
      writef("  dropped_req.service_rejected=%lu",
             static_cast<unsigned long>(ms.dropped_requests_service_rejected));
      writef("  dispatched_resp=%lu dropped_resp=%lu",
             static_cast<unsigned long>(ms.dispatched_responses),
             static_cast<unsigned long>(ms.dropped_responses));
      writef("  dropped_resp.no_route=%lu dropped_resp.transport_rejected=%lu",
             static_cast<unsigned long>(ms.dropped_responses_no_route),
             static_cast<unsigned long>(ms.dropped_responses_transport_rejected));
      writef("  dispatched_evt=%lu dropped_evt=%lu transports=%u",
             static_cast<unsigned long>(ms.dispatched_events),
             static_cast<unsigned long>(ms.dropped_events),
             static_cast<unsigned int>(management_runtime_->transportCount()));
      writef("  dropped_evt.no_route=%lu dropped_evt.transport_rejected=%lu",
             static_cast<unsigned long>(ms.dropped_events_no_route),
             static_cast<unsigned long>(ms.dropped_events_transport_rejected));
    }
    if (management_transport_ != nullptr) {
      io_.writeln("[MASTER][METRICS][MGMT] cli_queue:");
      writef("  req=%u resp=%u evt=%u",
             static_cast<unsigned int>(management_transport_->pendingRequestCount()),
             static_cast<unsigned int>(management_transport_->pendingResponseCount()),
             static_cast<unsigned int>(management_transport_->pendingEventCount()));
      const ManagementQueueTransport::QueueStats& req_stats = management_transport_->requestStats();
      const ManagementQueueTransport::QueueStats& resp_stats = management_transport_->responseStats();
      const ManagementQueueTransport::QueueStats& evt_stats = management_transport_->eventStats();
      writef("  req_stats enq=%lu rej_new=%lu drop_oldest=%lu",
             static_cast<unsigned long>(req_stats.enqueued),
             static_cast<unsigned long>(req_stats.rejected_new),
             static_cast<unsigned long>(req_stats.dropped_oldest));
      writef("  resp_stats enq=%lu rej_new=%lu drop_oldest=%lu",
             static_cast<unsigned long>(resp_stats.enqueued),
             static_cast<unsigned long>(resp_stats.rejected_new),
             static_cast<unsigned long>(resp_stats.dropped_oldest));
      writef("  evt_stats enq=%lu rej_new=%lu drop_oldest=%lu",
             static_cast<unsigned long>(evt_stats.enqueued),
             static_cast<unsigned long>(evt_stats.rejected_new),
             static_cast<unsigned long>(evt_stats.dropped_oldest));
    }
    if (management_ != nullptr) {
      io_.writeln("[MASTER][METRICS][MGMT] service_queue:");
      writef("  req=%u resp=%u evt=%u",
             static_cast<unsigned int>(management_->pendingRequestCount()),
             static_cast<unsigned int>(management_->pendingResponseCount()),
             static_cast<unsigned int>(management_->pendingEventCount()));
    }
    return true;
  }

  if (lower == "metrics.reset") {
    manager_.resetRuntimeMetrics();
    if (management_runtime_ != nullptr) {
      management_runtime_->resetStats();
    }
    if (management_transport_ != nullptr) {
      management_transport_->resetStats();
    }
    io_.writeln("[MASTER][METRICS] reset");
    return true;
  }

  if (lower == "cli off") {
    if (!setCliEnabled(false, true)) {
      io_.writeln("[MASTER][CLI] failed to persist enable state");
    }
    return true;
  }

  return false;
}
bool MasterCli::handleListAndStatusCommands(const std::string& lower) {
  if (sticky_target_active_ && !manager_.hasPersistedPair(sticky_target_peer_)) {
    clearActiveTarget_();
  }

  auto profileHint = [&](ProfileId profile_id) -> std::string {
    if (profile_id == kProfileUnknown) {
      return "unknown";
    }
    const IProfileDefinition* def = ProfileRegistry::instance().find(profile_id);
    if (def != nullptr && def->profileName() != nullptr && def->profileName()[0] != '\0') {
      return std::string(def->profileName());
    }
    char buf[16] = {0};
    std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned int>(profile_id));
    return std::string(buf);
  };

  if (lower == "active") {
    MacAddress active{};
    if (!resolveRuntimePeer(active)) {
      io_.writeln("[MASTER][CLI] active target=none");
      return true;
    }
    ProfileId cached_profile = kProfileUnknown;
    (void)ensureRuntimeProfileKnown_(cached_profile, false);
    writef("[MASTER][CLI] active target=%s profile=%s",
           macToPrintable(active).c_str(),
           profileHint(cached_profile).c_str());
    return true;
  }

  if (lower == "active clear") {
    clearActiveTarget_();
    io_.writeln("[MASTER][CLI] active target cleared");
    return true;
  }

  if (startsWith(lower, "active ")) {
    const std::string arg = trim(lower.substr(6));
    MacAddress selected{};
    if (!setActiveTargetBySelector_(arg, &selected)) {
      io_.writeln("[MASTER][CLI] usage: active <paired_index|paired_mac> | active clear");
      return true;
    }
    ProfileId resolved_profile = kProfileUnknown;
    const bool known = ensureRuntimeProfileKnown_(resolved_profile, false);
    writef("[MASTER][CLI] active target=%s profile=%s",
           macToPrintable(selected).c_str(),
           profileHint(known ? resolved_profile : kProfileUnknown).c_str());
    return true;
  }

  if (lower == "list") {
    constexpr uint32_t kListWindowMs = 10000U;
    discovered_.clear();
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }

    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    uint32_t req_id = 0U;
    if (!submitRuntimeTargeted_(mgmt,
                                static_cast<uint16_t>(ManagementCommandId::DiscoveryStart),
                                management_utils::buildDiscoveryStartPayload(kListWindowMs),
                                &req_id,
                                0U,
                                false)) {
      io_.writeln("[MASTER][CLI] discovery window start failed");
      return true;
    }
    correlation_id_ = mgmt.nextReqId();
    collect_discovery_ = true;
    list_window_active_ = true;
    list_window_deadline_ms_ = nowMs() + kListWindowMs;
    writef("[MASTER][CLI] discovery window started (10s) req=%lu",
           static_cast<unsigned long>(req_id));
    return true;
  }

  if (lower == "paired" || lower == "paired.list") {
    std::vector<MacAddress> persisted{};
    manager_.getPersistedPeers(persisted);
    MacAddress active{};
    const bool has_active = resolveRuntimePeer(active);
    writef("[MASTER][CLI] paired_slots=%u/14",
           static_cast<unsigned int>(persisted.size()));
    if (persisted.empty()) {
      io_.writeln("[MASTER][CLI] no persisted paired peer");
      return true;
    }
    io_.writeln("[MASTER][CLI] persisted paired peers:");
    for (size_t i = 0; i < persisted.size(); ++i) {
      const bool is_active = has_active && persisted[i] == active;
      writef("  %u) %s%s",
             static_cast<unsigned int>(i),
             macToPrintable(persisted[i]).c_str(),
             is_active ? "  [active]" : "");
    }
    return true;
  }

  if (lower == "status") {
    const size_t paired_slots = manager_.persistedPairCount();
    writef("[MASTER][CLI] paired=%s",
           manager_.isPaired() ? "yes" : "no");
    writef("[MASTER][CLI] paired_slots=%u/14",
           static_cast<unsigned int>(paired_slots));

    io_.writeln("[MASTER][CLI] target_mode=active_with_prefix_override");
    writef("[MASTER][CLI] active_target=%s", sticky_target_active_ ? "set" : "none");

    MacAddress runtime_target{};
    if (resolveRuntimePeer(runtime_target)) {
      writef("[MASTER][CLI] runtime_target=%s", macToPrintable(runtime_target).c_str());
      ProfileId cached_profile = kProfileUnknown;
      (void)ensureRuntimeProfileKnown_(cached_profile, false);
      writef("[MASTER][CLI] runtime_profile=%s", profileHint(cached_profile).c_str());
    } else {
      io_.writeln("[MASTER][CLI] runtime_target=none");
    }

    if (list_window_active_) {
      const uint32_t now_ms = nowMs();
      const uint32_t remaining_ms = (list_window_deadline_ms_ > now_ms)
                                        ? (list_window_deadline_ms_ - now_ms)
                                        : 0U;
      writef("[MASTER][CLI] discovery_window=active remaining_ms=%lu",
             static_cast<unsigned long>(remaining_ms));
    } else {
      io_.writeln("[MASTER][CLI] discovery_window=idle");
    }

    printQueueStatus();
    if (logEnabled(CliLogLevel::Debug)) {
      writef("[MASTER][CLI] queue_budget_per_tick=%u", static_cast<unsigned int>(descriptor_send_budget_per_tick_));
    }
    return true;
  }

  return false;
}

bool MasterCli::handlePairingCommands(const std::string& line, const std::string& lower) {
  if (startsWith(lower, "pair")) {
    std::string arg = trim(line.substr(4));
    if (arg.empty()) {
      io_.writeln("[MASTER][CLI] usage: pair <index|mac>  (index: discovered first, then persisted)");
      return true;
    }
    MacAddress target{};
    bool ok = false;
    bool all_digits = true;
    for (char c : arg) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        all_digits = false;
        break;
      }
    }
    if (all_digits) {
      const int idx = std::atoi(arg.c_str());
      if (idx >= 0) {
        ok = peerByIndex(static_cast<size_t>(idx), target);
        if (!ok) {
          std::vector<MacAddress> persisted{};
          manager_.getPersistedPeers(persisted);
          if (static_cast<size_t>(idx) < persisted.size()) {
            target = persisted[static_cast<size_t>(idx)];
            ok = true;
          }
        }
      }
    } else {
      ok = parseMac(arg, target);
    }
    if (!ok) {
      io_.writeln("[MASTER][CLI] invalid peer selector");
      return true;
    }
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool submitted = submitRuntimeTargeted_(mgmt,
                                                  static_cast<uint16_t>(ManagementCommandId::PairRequest),
                                                  std::vector<uint8_t>(target.begin(), target.end()),
                                                  nullptr,
                                                  0U,
                                                  false);
    correlation_id_ = mgmt.nextReqId();
    if (submitted) {
      writef("[MASTER][CLI] pair requested with %s", macToPrintable(target).c_str());
      auto_pull_.resetState();
    } else {
      io_.writeln("[MASTER][CLI] pair request failed");
    }
    return true;
  }

  if (lower == "unpair") {
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
    const bool submitted = submitRuntimeTargeted_(mgmt, static_cast<uint16_t>(ManagementCommandId::UnpairRequest));
    correlation_id_ = mgmt.nextReqId();
    if (submitted) {
      io_.writeln("[MASTER][CLI] unpair requested");
    } else {
      io_.writeln("[MASTER][CLI] unpair failed");
    }
    return true;
  }

  if (lower == "remove" || startsWith(lower, "remove ")) {
    std::string arg = trim(line.substr(6));
    MacAddress target{};
    bool has_target = false;

    if (arg.empty() || arg == "slave" || arg == "peer") {
      has_target = resolveRuntimePeer(target);
    } else {
      bool all_digits = true;
      for (char c : arg) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
          all_digits = false;
          break;
        }
      }
      if (all_digits) {
        const int idx = std::atoi(arg.c_str());
        if (idx >= 0) {
          has_target = peerByIndex(static_cast<size_t>(idx), target);
          if (!has_target) {
            std::vector<MacAddress> persisted{};
            manager_.getPersistedPeers(persisted);
            if (static_cast<size_t>(idx) < persisted.size()) {
              target = persisted[static_cast<size_t>(idx)];
              has_target = true;
            }
          }
        }
      } else {
        has_target = parseMac(arg, target);
      }
    }

    if (!has_target) {
      io_.writeln("[MASTER][CLI] usage: remove [index|AA:BB:CC:DD:EE:FF|slave]  (index: discovered first, then persisted)");
      return true;
    }

    MacAddress runtime_peer{};
    const bool is_runtime_peer = resolveRuntimePeer(runtime_peer) && (runtime_peer == target);
    if (management_transport_ == nullptr) {
      io_.writeln("[MASTER][CLI] management path unavailable");
      captureDispatchSnapshot_(false, 0U, 0U, ManagementStatus::DeniedByPolicy, "availability");
      return true;
    }
    ManagementController mgmt(*management_transport_);
    mgmt.setNextReqId(correlation_id_);
    const bool removed = submitRuntimeTargeted_(mgmt,
                                                static_cast<uint16_t>(ManagementCommandId::RemovePeerRequest),
                                                management_utils::buildMacPayload(target),
                                                nullptr,
                                                0U,
                                                false);
    correlation_id_ = mgmt.nextReqId();
    if (removed) {
      eraseCachedRemoteProfile_(target);
      if (sticky_target_active_ && sticky_target_peer_ == target) {
        clearActiveTarget_();
      }
      if (is_runtime_peer) {
        clearPeerSessionState_();
      }
      writef("[MASTER][CLI] remove peer=%s requested (management)", macToPrintable(target).c_str());
    } else {
      io_.writeln("[MASTER][CLI] remove request failed (management queue full)");
    }
    return true;
  }

  return false;
}

}  // namespace espnow_link
