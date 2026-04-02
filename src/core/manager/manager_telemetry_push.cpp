/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 *  File Purpose: Telemetry push control, reporting, and persistence behavior.
 **************************************************************/
#include "../internal/manager_internal.hpp"

namespace espnow_link {

using namespace manager_helpers;
using namespace telemetry_alignment;
bool EspNowManager::publishMandatoryEvent(uint16_t event_id,
                                          uint8_t severity,
                                          int32_t value,
                                          uint32_t event_ts_s) {
  if (config_.local_role != Role::Slave || event_id == 0) {
    return false;
  }

  PendingMandatoryEvent ev{};
  ev.event_id = event_id;
  ev.severity = severity;
  ev.value = value;
  ev.event_ts_s = event_ts_s;
  ev.corr_id = mandatory_event_corr_seq_++;

  for (auto& queued : mandatory_event_queue_) {
    if (queued.event_id == ev.event_id) {
      queued = ev;
      return true;
    }
  }

  if (mandatory_event_queue_.size() >= config_.mandatory_event_max_queue) {
    if (events_ != nullptr) {
      Event dropped{};
      dropped.type = Event::Type::MandatoryEventDropped;
      dropped.event_id = mandatory_event_queue_.front().event_id;
      dropped.severity = mandatory_event_queue_.front().severity;
      dropped.event_value = mandatory_event_queue_.front().value;
      dropped.event_ts_s = mandatory_event_queue_.front().event_ts_s;
      dropped.message = "event queue full";
      events_->onEvent(dropped);
    }
    mandatory_event_queue_.pop_front();
  }

  mandatory_event_queue_.push_back(ev);
  return true;
}

bool EspNowManager::buildDescriptorReply(bool ok,
                                         const std::string& message,
                                         std::vector<uint8_t>& out_payload) {
  return descriptor_reply::buildDescriptorAckReply(ok, message, out_payload);
}

bool EspNowManager::handleTelemetryPushControl(const MacAddress& from,
                                               uint32_t corr_id,
                                               const uint8_t* payload,
                                               size_t len) {
  if (config_.local_role != Role::Slave || telemetry_push_provider_ == nullptr) {
    return false;
  }

  TelemetryPushCommand cmd{};
  if (!parseTelemetryPushCommand(payload, len, cmd)) {
    return false;
  }

  bool ok = true;
  std::string msg = "stream ok";

  auto reject = [&](const char* why) {
    ok = false;
    msg = why;
  };

  MacAddress paired_peer{};
  if (!pairing_.getPairedPeer(paired_peer) || paired_peer != from) {
    reject("stream rejected: peer not active master");
  }

  if (ok && push_session_.has_master && push_session_.master != from) {
    reject("stream rejected: owner mismatch");
  }

  auto is_interval_valid = [](uint32_t v) {
    return v >= 200 && v <= 60000;
  };
  auto is_gap_valid = [](uint32_t v) {
    return v >= 50 && v <= 60000;
  };

  if (ok && (cmd.action == TelemetryPushAction::Start || cmd.action == TelemetryPushAction::Update)) {
    if (cmd.config.metrics.empty()) {
      reject("stream invalid: no metrics");
    }
    if (!is_interval_valid(cmd.config.interval_ms)) {
      reject("stream invalid: stream interval out of bounds");
    }
    if (!is_gap_valid(cmd.config.min_report_gap_ms)) {
      reject("stream invalid: stream min_report_gap out of bounds");
    }
    if (cmd.config.metrics.size() > 16U) {
      reject("stream invalid: too many metrics");
    }
  }

  if (ok) {
    switch (cmd.action) {
      case TelemetryPushAction::Start:
      case TelemetryPushAction::Update: {
        std::vector<TelemetryDescriptor> schema_lookup_cache{};
        std::vector<TelemetrySample> snapshot_lookup_cache{};
        const std::vector<TelemetryDescriptor>* schema_lookup = nullptr;
        const std::vector<TelemetrySample>* snapshot_lookup = nullptr;
        if (local_profile_ == nullptr && telemetry_push_provider_ != nullptr) {
          if (telemetry_push_provider_->getTelemetrySchema(schema_lookup_cache)) {
            schema_lookup = &schema_lookup_cache;
          }
          if (telemetry_push_provider_->getTelemetrySnapshot(snapshot_lookup_cache)) {
            snapshot_lookup = &snapshot_lookup_cache;
          }
        }

        TelemetryPushSession next{};
        next.enabled = true;
        next.paused = false;
        next.stream_id = cmd.config.stream_id;
        next.mode = cmd.config.mode;
        next.interval_ms = cmd.config.interval_ms;
        next.min_gap_ms = cmd.config.min_report_gap_ms;
        next.master = from;
        next.has_master = true;
        next.metrics.reserve(cmd.config.metrics.size());

        for (const auto& m : cmd.config.metrics) {
          std::string resolved_key;
          if (!resolveTelemetryMetricIdentity(m, resolved_key, schema_lookup, snapshot_lookup)) {
            reject("stream invalid: unknown metric key");
            break;
          }

          bool dup = false;
          for (const auto& ex : next.metrics) {
            if (ex.key == resolved_key) {
              dup = true;
              break;
            }
          }
          if (dup) {
            reject("stream invalid: duplicate metric key");
            break;
          }

          PushMetricState ps{};
          ps.key = resolved_key;
          ps.enabled = m.enabled;
          ps.mode = m.mode;
          ps.interval_ms = (m.interval_ms == 0) ? next.interval_ms : m.interval_ms;
          ps.min_gap_ms = (m.min_report_gap_ms == 0) ? next.min_gap_ms : m.min_report_gap_ms;
          ps.next_periodic_ms = current_now_ms_ + ps.interval_ms;
          ps.use_threshold = m.use_threshold;
          ps.delta_abs = m.delta_abs;

          if ((ps.mode == TelemetryPushMode::Periodic || ps.mode == TelemetryPushMode::Hybrid) &&
              !is_interval_valid(ps.interval_ms)) {
            reject("stream invalid: metric interval out of bounds");
            break;
          }
          if ((ps.mode == TelemetryPushMode::OnChange || ps.mode == TelemetryPushMode::Hybrid) &&
              !is_gap_valid(ps.min_gap_ms)) {
            reject("stream invalid: metric min_report_gap out of bounds");
            break;
          }

          next.metrics.push_back(ps);
        }
        if (ok) {
          bool any_enabled = false;
          for (const auto& m : next.metrics) {
            if (m.enabled) {
              any_enabled = true;
              break;
            }
          }
          if (!any_enabled) {
            reject("stream invalid: all metrics disabled");
          }
        }

        if (ok) {
          push_session_ = next;
          if (!persistTelemetryPushConfig()) {
            reject("persist_failed");
            push_session_ = TelemetryPushSession{};
          } else {
            msg = (cmd.action == TelemetryPushAction::Start) ? "stream started" : "stream updated";
          }
        }
      } break;

      case TelemetryPushAction::Pause:
        if (!push_session_.enabled) {
          reject("stream not active");
        } else {
          push_session_.paused = true;
          if (!persistTelemetryPushConfig()) {
            reject("persist_failed");
          } else {
            msg = "stream paused";
          }
        }
        break;

      case TelemetryPushAction::Resume:
        if (!push_session_.enabled) {
          reject("stream not active");
        } else {
          push_session_.paused = false;
          if (!persistTelemetryPushConfig()) {
            reject("persist_failed");
          } else {
            msg = "stream resumed";
          }
        }
        break;

      case TelemetryPushAction::Stop:
        push_session_ = TelemetryPushSession{};
        if (!clearTelemetryPushConfig(from)) {
          reject("persist_failed");
        } else {
          msg = "stream stopped";
        }
        break;

      case TelemetryPushAction::Get:
        msg = push_session_.enabled ? (push_session_.paused ? "stream active paused" : "stream active")
                                    : "stream inactive";
        break;

      default:
        reject("stream invalid action");
        break;
    }
  }

  std::vector<uint8_t> reply;
  if (!buildDescriptorReply(ok, msg, reply)) {
    return false;
  }
  return sendPullResponse(from, reply.data(), reply.size(), corr_id);
}

bool EspNowManager::sendTelemetryPushReport(const MacAddress& to,
                                            const std::vector<TelemetrySample>& samples,
                                            uint32_t corr_id,
                                            const char* cause) {
  std::vector<uint8_t> payload;
  if (!descriptor_reply::buildTelemetrySnapshotReply(samples, cause, payload)) {
    return false;
  }

  return sendTyped(to,
                   MessageType::PullResponse,
                   payload.data(),
                   payload.size(),
                   corr_id,
                   0x04,
                   0x04,
                   0x02);
}

bool EspNowManager::sendMandatoryEventReport(const MacAddress& to, const PendingMandatoryEvent& ev) {
  std::vector<uint8_t> payload;
  if (!buildMandatoryEventPayload(ev.event_id, ev.severity, ev.value, ev.event_ts_s, payload)) {
    return false;
  }

  return sendTyped(to,
                   MessageType::EventReport,
                   payload.data(),
                   payload.size(),
                   ev.corr_id,
                   0x06,
                   0x10,
                   0x04);
}

void EspNowManager::tickOtaBootCompletionNotice(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || firmware_sink_ == nullptr || !pairing_.isPaired()) {
    return;
  }
  if (ota_boot_notice_last_try_ms_ != 0U &&
      (now_ms - ota_boot_notice_last_try_ms_) < 1000U) {
    return;
  }

  FirmwareImageMetadata meta{};
  uint32_t epoch_s = 0U;
  if (!firmware_sink_->peekBootCompletionNotice(meta, epoch_s)) {
    return;
  }

  ota_boot_notice_last_try_ms_ = now_ms;
  const int32_t packed = static_cast<int32_t>(meta.sw_version.empty() ? 1U : (static_cast<uint32_t>(meta.sw_version.size()) & 0x7FFFU));
  (void)publishMandatoryEvent(kOtaBootCompleteEventId, 1U, packed, epoch_s);
}

void EspNowManager::tickOtaFinalizeStatusRetry(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || !ota_finalize_pending_) {
    return;
  }
  if (!pairing_.isPaired()) {
    return;
  }
  if (static_cast<int32_t>(now_ms - ota_finalize_started_ms_) >= static_cast<int32_t>(kFinalizeStatusRetryWindowMs)) {
    ota_finalize_pending_ = false;
    ota_finalize_corr_id_ = 0U;
    ota_finalize_offset_ = 0U;
    ota_finalize_status_code_ = 0U;
    ota_finalize_kind_ = 0U;
    ota_finalize_started_ms_ = 0U;
    ota_finalize_last_tx_ms_ = 0U;
    return;
  }
  if (static_cast<int32_t>(now_ms - ota_finalize_last_tx_ms_) < static_cast<int32_t>(kFinalizeStatusRetryIntervalMs)) {
    return;
  }
  if (sendFirmwareStatus(ota_finalize_peer_,
                         ota_finalize_corr_id_,
                         ota_finalize_kind_,
                         ota_finalize_offset_,
                         ota_finalize_status_code_)) {
    ota_finalize_last_tx_ms_ = now_ms;
  }
}

void EspNowManager::tickMandatoryEventQueue(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || mandatory_event_queue_.empty()) {
    return;
  }

  MacAddress master{};
  if (!pairing_.getPairedPeer(master)) {
    return;
  }

  if (mandatory_event_backoff_until_ms_ != 0 &&
      static_cast<int32_t>(now_ms - mandatory_event_backoff_until_ms_) < 0) {
    return;
  }

  if (mandatory_event_last_tx_ms_ != 0 &&
      (now_ms - mandatory_event_last_tx_ms_) < config_.mandatory_event_min_gap_ms) {
    return;
  }

  PendingMandatoryEvent ev = mandatory_event_queue_.front();
  if (!sendMandatoryEventReport(master, ev)) {
    mandatory_event_last_tx_ms_ = now_ms;
    mandatory_event_queue_.front().retry_count++;
    mandatory_event_fail_streak_++;

    if (mandatory_event_queue_.front().retry_count > config_.mandatory_event_retry_max) {
      if (events_ != nullptr) {
        Event dropped{};
        dropped.type = Event::Type::MandatoryEventDropped;
        dropped.peer = master;
        dropped.correlation_id = ev.corr_id;
        dropped.event_id = ev.event_id;
        dropped.severity = ev.severity;
        dropped.event_value = ev.value;
        dropped.event_ts_s = ev.event_ts_s;
        dropped.message = "event retry exceeded";
        events_->onEvent(dropped);
      }
      mandatory_event_queue_.pop_front();
    }

    if (mandatory_event_fail_streak_ >= config_.mandatory_event_fail_backoff_threshold) {
      mandatory_event_backoff_until_ms_ = now_ms + config_.mandatory_event_backoff_ms;
      mandatory_event_fail_streak_ = 0;
    }
    return;
  }

  mandatory_event_last_tx_ms_ = now_ms;
  mandatory_event_backoff_until_ms_ = 0;
  mandatory_event_fail_streak_ = 0;
  if (ev.event_id == kOtaBootCompleteEventId && firmware_sink_ != nullptr) {
    std::string msg;
    (void)firmware_sink_->clearBootCompletionNotice(&msg);
  }
  mandatory_event_queue_.pop_front();

  if (events_ != nullptr) {
    Event sent{};
    sent.type = Event::Type::MandatoryEventSent;
    sent.peer = master;
    sent.correlation_id = ev.corr_id;
    sent.event_id = ev.event_id;
    sent.severity = ev.severity;
    sent.event_value = ev.value;
    sent.event_ts_s = ev.event_ts_s;
    sent.message = "mandatory event tx";
    events_->onEvent(sent);
  }
}

void EspNowManager::tickTelemetryPush(uint32_t now_ms) {
  if (config_.local_role != Role::Slave || !pairing_.isPaired()) {
    return;
  }
  if (!push_session_.enabled || push_session_.paused || !push_session_.has_master || telemetry_push_provider_ == nullptr) {
    return;
  }
  if (push_session_.backoff_until_ms != 0 && static_cast<int32_t>(now_ms - push_session_.backoff_until_ms) < 0) {
    return;
  }

  bool any_metric_due = false;
  for (const auto& metric : push_session_.metrics) {
    if (!metric.enabled) {
      continue;
    }
    const bool periodic_mode =
        (metric.mode == TelemetryPushMode::Periodic || metric.mode == TelemetryPushMode::Hybrid);
    const bool change_mode =
        (metric.mode == TelemetryPushMode::OnChange || metric.mode == TelemetryPushMode::Hybrid);
    if (periodic_mode &&
        (metric.next_periodic_ms == 0 || static_cast<int32_t>(now_ms - metric.next_periodic_ms) >= 0)) {
      any_metric_due = true;
      break;
    }
    if (change_mode &&
        metric.has_last &&
        (now_ms - metric.last_report_ms) >= metric.min_gap_ms) {
      any_metric_due = true;
      break;
    }
  }
  if (!any_metric_due) {
    return;
  }

  telemetry_snapshot_cache_.clear();
  if (!telemetry_push_provider_->getTelemetrySnapshot(telemetry_snapshot_cache_)) {
    return;
  }

  alignSnapshotToProfileInPlace(local_profile_, telemetry_snapshot_cache_, telemetry_aligned_snapshot_cache_);
  if (telemetry_aligned_snapshot_cache_.empty()) {
    return;
  }

  buildSnapshotIndex(telemetry_aligned_snapshot_cache_, telemetry_snapshot_index_cache_);

  const size_t metric_count = push_session_.metrics.size();
  telemetry_metric_samples_cache_.assign(metric_count, nullptr);
  telemetry_metric_values_cache_.assign(metric_count, 0.0f);
  telemetry_metric_flags_cache_.assign(metric_count, 0U);
  telemetry_send_samples_cache_.clear();
  telemetry_send_samples_cache_.reserve(metric_count);

  static constexpr uint8_t kMetricValueOk = 0x01;
  static constexpr uint8_t kMetricSent = 0x02;

  bool periodic_cause = false;
  bool threshold_cause = false;

  // Phase 1: evaluate each metric once and decide whether it should be sent now.
  for (size_t i = 0; i < metric_count; ++i) {
    auto& metric = push_session_.metrics[i];
    if (!metric.enabled) {
      continue;
    }

    auto it = telemetry_snapshot_index_cache_.find(metric.key);
    if (it == telemetry_snapshot_index_cache_.end()) {
      continue;
    }

    const TelemetrySample* sample = it->second;
    telemetry_metric_samples_cache_[i] = sample;
    if (sample == nullptr) {
      continue;
    }

    float parsed_value = 0.0f;
    if (parseSampleFloat(sample->value, parsed_value)) {
      telemetry_metric_values_cache_[i] = parsed_value;
      telemetry_metric_flags_cache_[i] |= kMetricValueOk;
    }

    const bool periodic_mode =
        (metric.mode == TelemetryPushMode::Periodic || metric.mode == TelemetryPushMode::Hybrid);
    const bool change_mode =
        (metric.mode == TelemetryPushMode::OnChange || metric.mode == TelemetryPushMode::Hybrid);

    bool should_send = false;
    if (periodic_mode && (metric.next_periodic_ms == 0 || static_cast<int32_t>(now_ms - metric.next_periodic_ms) >= 0)) {
      should_send = true;
      periodic_cause = true;
    }

    if (change_mode &&
        (telemetry_metric_flags_cache_[i] & kMetricValueOk) != 0 &&
        metric.has_last &&
        (now_ms - metric.last_report_ms) >= metric.min_gap_ms) {
      const float delta = std::fabs(telemetry_metric_values_cache_[i] - metric.last_value);
      const float threshold = metric.use_threshold ? metric.delta_abs : 0.0f;
      if (delta >= threshold) {
        should_send = true;
        threshold_cause = true;
      }
    }

    if (should_send) {
      bool already_added = false;
      for (const auto& ex : telemetry_send_samples_cache_) {
        if (ex.key == sample->key) {
          already_added = true;
          break;
        }
      }
      if (!already_added) {
        telemetry_send_samples_cache_.push_back(*sample);
      }
      telemetry_metric_flags_cache_[i] |= kMetricSent;
    }
  }

  if (telemetry_send_samples_cache_.empty()) {
    return;
  }

  const char* cause = periodic_cause && threshold_cause
                          ? "periodic+threshold"
                          : (threshold_cause ? "threshold_cross" : "periodic_tick");

  const bool sent =
      sendTelemetryPushReport(push_session_.master, telemetry_send_samples_cache_, telemetry_push_seq_++, cause);
  if (!sent) {
    if (++push_session_.consecutive_tx_fail >= 5) {
      push_session_.backoff_until_ms = now_ms + 2000;
      push_session_.consecutive_tx_fail = 0;
    }
    return;
  }

  push_session_.consecutive_tx_fail = 0;
  push_session_.backoff_until_ms = 0;

  // Phase 2: update metric state only after successful transmit.
  for (size_t i = 0; i < metric_count; ++i) {
    auto& metric = push_session_.metrics[i];
    if (!metric.enabled) {
      continue;
    }

    if (metric.mode == TelemetryPushMode::Periodic || metric.mode == TelemetryPushMode::Hybrid) {
      metric.next_periodic_ms = now_ms + metric.interval_ms;
    }

    if ((telemetry_metric_flags_cache_[i] & kMetricValueOk) == 0U) {
      continue;
    }

    metric.has_last = true;
    metric.last_value = telemetry_metric_values_cache_[i];
    if ((telemetry_metric_flags_cache_[i] & kMetricSent) != 0U) {
      metric.last_report_ms = now_ms;
    }
  }
}

bool EspNowManager::isKnownTelemetryKey(const std::string& key,
                                        const std::vector<TelemetryDescriptor>* schema_cache,
                                        const std::vector<TelemetrySample>* snapshot_cache) const {
  if (key.empty()) {
    return false;
  }

  if (local_profile_ != nullptr) {
    return findProfileTelemetryByKey(local_profile_, key) != nullptr;
  }

  if (telemetry_push_provider_ == nullptr) {
    return false;
  }

  if (schema_cache != nullptr) {
    for (const auto& t : *schema_cache) {
      if (!t.key.empty() && t.key == key) {
        return true;
      }
    }
  } else {
    std::vector<TelemetryDescriptor> schema;
    if (telemetry_push_provider_->getTelemetrySchema(schema) && !schema.empty()) {
      for (const auto& t : schema) {
        if (t.key == key) {
          return true;
        }
      }
    }
  }

  if (snapshot_cache != nullptr) {
    for (const auto& s : *snapshot_cache) {
      if (!s.key.empty() && s.key == key) {
        return true;
      }
    }
  } else {
    std::vector<TelemetrySample> snap;
    if (telemetry_push_provider_->getTelemetrySnapshot(snap)) {
      for (const auto& s : snap) {
        if (s.key == key) {
          return true;
        }
      }
    }
  }
  return false;
}

bool EspNowManager::telemetryKeyFromIndex(uint16_t metric_index,
                                          std::string& out_key,
                                          const std::vector<TelemetryDescriptor>* schema_cache) const {
  out_key.clear();

  if (local_profile_ != nullptr) {
    const ProfileTelemetryMetricSpec* spec = findProfileTelemetryById(local_profile_, metric_index);
    if (spec != nullptr && spec->key != nullptr && spec->key[0] != '\0') {
      out_key = spec->key;
      return true;
    }
    return false;
  }

  if (telemetry_push_provider_ == nullptr) {
    return false;
  }

  const std::vector<TelemetryDescriptor>* schema_ptr = schema_cache;
  std::vector<TelemetryDescriptor> schema_fallback{};
  if (schema_ptr == nullptr) {
    if (!telemetry_push_provider_->getTelemetrySchema(schema_fallback)) {
      return false;
    }
    schema_ptr = &schema_fallback;
  }
  if (schema_ptr->empty()) return false;

  if (static_cast<size_t>(metric_index) >= schema_ptr->size()) {
    return false;
  }

  out_key = (*schema_ptr)[metric_index].key;
  return !out_key.empty();
}

bool EspNowManager::resolveTelemetryMetricIdentity(const TelemetryPushMetricConfig& metric,
                                                   std::string& out_key,
                                                   const std::vector<TelemetryDescriptor>* schema_cache,
                                                   const std::vector<TelemetrySample>* snapshot_cache) const {
  out_key.clear();

  const bool has_key = !metric.key.empty();
  const bool has_index = metric.has_metric_index;

  // Exactly one metric identity form is allowed.
  if (has_key == has_index) {
    return false;
  }

  if (has_key) {
    out_key = metric.key;
    return isKnownTelemetryKey(out_key, schema_cache, snapshot_cache);
  }

  return telemetryKeyFromIndex(metric.metric_index, out_key, schema_cache);
}

bool EspNowManager::persistTelemetryPushConfig() {
  if (store_ == nullptr || !store_->enabled() || !push_session_.has_master) {
    return false;
  }

  static constexpr uint8_t kTelemetryMetaSlot = 1;
  std::vector<uint8_t> blob;
  blob.reserve(128);

  auto putU8 = [&](uint8_t v) { blob.push_back(v); };
  auto putU16 = [&](uint16_t v) {
    blob.push_back(static_cast<uint8_t>(v & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  };
  auto putU32 = [&](uint32_t v) {
    blob.push_back(static_cast<uint8_t>(v & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    blob.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto putF32 = [&](float f) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&f);
    blob.push_back(p[0]);
    blob.push_back(p[1]);
    blob.push_back(p[2]);
    blob.push_back(p[3]);
  };

  putU8(1);
  putU8(push_session_.enabled ? 1 : 0);
  putU8(push_session_.paused ? 1 : 0);
  putU8(static_cast<uint8_t>(push_session_.mode));
  putU16(push_session_.stream_id);
  putU32(push_session_.interval_ms);
  putU32(push_session_.min_gap_ms);

  if (push_session_.metrics.size() > 255U) {
    return false;
  }
  putU8(static_cast<uint8_t>(push_session_.metrics.size()));

  for (const auto& m : push_session_.metrics) {
    if (m.key.empty() || m.key.size() > 63U) {
      return false;
    }
    putU8(static_cast<uint8_t>(m.key.size()));
    blob.insert(blob.end(), m.key.begin(), m.key.end());
    putU8(m.enabled ? 1 : 0);
    putU8(static_cast<uint8_t>(m.mode));
    putU32(m.interval_ms);
    putU32(m.min_gap_ms);
    putU8(m.use_threshold ? 1 : 0);
    putF32(m.delta_abs);
  }

  return store_->saveMetaBlob(local_mac_, push_session_.master, kTelemetryMetaSlot, blob.data(), blob.size());
}

bool EspNowManager::restoreTelemetryPushConfig(const MacAddress& master) {
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }

  static constexpr uint8_t kTelemetryMetaSlot = 1;
  std::vector<uint8_t> blob;
  if (!store_->loadMetaBlob(local_mac_, master, kTelemetryMetaSlot, blob)) {
    return false;
  }
  if (blob.size() < 15) {
    (void)store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
    return false;
  }

  size_t off = 0;
  auto getU8 = [&](uint8_t& out) {
    if (off + 1 > blob.size()) return false;
    out = blob[off++];
    return true;
  };
  auto getU16 = [&](uint16_t& out) {
    if (off + 2 > blob.size()) return false;
    out = static_cast<uint16_t>(blob[off]) | (static_cast<uint16_t>(blob[off + 1]) << 8);
    off += 2;
    return true;
  };
  auto getU32 = [&](uint32_t& out) {
    if (off + 4 > blob.size()) return false;
    out = static_cast<uint32_t>(blob[off]) |
          (static_cast<uint32_t>(blob[off + 1]) << 8) |
          (static_cast<uint32_t>(blob[off + 2]) << 16) |
          (static_cast<uint32_t>(blob[off + 3]) << 24);
    off += 4;
    return true;
  };
  auto getF32 = [&](float& out) {
    if (off + 4 > blob.size()) return false;
    uint8_t* p = reinterpret_cast<uint8_t*>(&out);
    p[0] = blob[off + 0];
    p[1] = blob[off + 1];
    p[2] = blob[off + 2];
    p[3] = blob[off + 3];
    off += 4;
    return true;
  };

  uint8_t version = 0;
  if (!getU8(version) || version != 1) {
    (void)store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
    return false;
  }

  TelemetryPushSession restored{};
  restored.master = master;
  restored.has_master = true;

  uint8_t tmp = 0;
  if (!getU8(tmp)) return false;
  restored.enabled = (tmp != 0);
  if (!getU8(tmp)) return false;
  restored.paused = (tmp != 0);
  if (!getU8(tmp)) return false;
  restored.mode = static_cast<TelemetryPushMode>(tmp);
  if (!getU16(restored.stream_id)) return false;
  if (!getU32(restored.interval_ms)) return false;
  if (!getU32(restored.min_gap_ms)) return false;

  uint8_t metric_count = 0;
  if (!getU8(metric_count)) return false;
  restored.metrics.reserve(metric_count);

  std::vector<TelemetryDescriptor> schema_lookup_cache{};
  std::vector<TelemetrySample> snapshot_lookup_cache{};
  const std::vector<TelemetryDescriptor>* schema_lookup = nullptr;
  const std::vector<TelemetrySample>* snapshot_lookup = nullptr;
  if (local_profile_ == nullptr && telemetry_push_provider_ != nullptr) {
    if (telemetry_push_provider_->getTelemetrySchema(schema_lookup_cache)) {
      schema_lookup = &schema_lookup_cache;
    }
    if (telemetry_push_provider_->getTelemetrySnapshot(snapshot_lookup_cache)) {
      snapshot_lookup = &snapshot_lookup_cache;
    }
  }

  for (uint8_t i = 0; i < metric_count; ++i) {
    uint8_t key_len = 0;
    if (!getU8(key_len) || key_len == 0 || (off + key_len) > blob.size()) {
      return false;
    }

    PushMetricState s{};
    s.key.assign(reinterpret_cast<const char*>(blob.data() + off),
                 reinterpret_cast<const char*>(blob.data() + off + key_len));
    off += key_len;

    if (!getU8(tmp)) return false;
    s.enabled = (tmp != 0);
    if (!getU8(tmp)) return false;
    s.mode = static_cast<TelemetryPushMode>(tmp);
    if (!getU32(s.interval_ms)) return false;
    if (!getU32(s.min_gap_ms)) return false;
    if (!getU8(tmp)) return false;
    s.use_threshold = (tmp != 0);
    if (!getF32(s.delta_abs)) return false;

    if (!isKnownTelemetryKey(s.key, schema_lookup, snapshot_lookup)) {
      continue;
    }
    s.next_periodic_ms = current_now_ms_ + s.interval_ms;
    restored.metrics.push_back(s);
  }

  if (restored.enabled && restored.metrics.empty()) {
    (void)store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
    return false;
  }

  push_session_ = restored;
  return true;
}

bool EspNowManager::clearTelemetryPushConfig(const MacAddress& master) {
  if (store_ == nullptr || !store_->enabled()) {
    return false;
  }

  static constexpr uint8_t kTelemetryMetaSlot = 1;

  // Treat missing key as success (idempotent stop/reset/unpair behavior).
  std::vector<uint8_t> existing;
  const bool has_existing = store_->loadMetaBlob(local_mac_, master, kTelemetryMetaSlot, existing);
  bool erased = true;
  if (has_existing) {
    erased = store_->eraseMetaBlob(local_mac_, master, kTelemetryMetaSlot);
  }

  if (push_session_.has_master && push_session_.master == master) {
    push_session_ = TelemetryPushSession{};
  }
  return erased;
}

}  // namespace espnow_link

