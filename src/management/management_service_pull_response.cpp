#include "espnow_link/management_service.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "espnow_link/descriptor.hpp"
#include "espnow_link/management_utils.hpp"

namespace espnow_link {

namespace {

constexpr uint8_t kTopologyStageStepNone = 0U;
constexpr uint8_t kTopologyStageStepWaitClearAck = 1U;
constexpr uint8_t kTopologyStageStepWaitBeginAck = 2U;
constexpr uint8_t kTopologyStageStepWaitGroupAck = 3U;
constexpr uint8_t kTopologyStageStepWaitSlotAck = 4U;
constexpr uint8_t kTopologyStageStepWaitFinalizeAck = 5U;

void mergeCoverageRange_(std::vector<std::pair<uint16_t, uint16_t>>& ranges,
                         uint16_t start,
                         uint16_t end) {
  if (end <= start) {
    return;
  }
  ranges.emplace_back(start, end);
  std::sort(ranges.begin(), ranges.end(), [](const std::pair<uint16_t, uint16_t>& a,
                                             const std::pair<uint16_t, uint16_t>& b) {
    if (a.first == b.first) {
      return a.second < b.second;
    }
    return a.first < b.first;
  });
  size_t out = 0U;
  for (const auto& r : ranges) {
    if (out == 0U) {
      ranges[out++] = r;
      continue;
    }
    auto& last = ranges[out - 1U];
    if (r.first <= last.second) {
      if (r.second > last.second) {
        last.second = r.second;
      }
    } else {
      ranges[out++] = r;
    }
  }
  ranges.resize(out);
}

uint16_t contiguousCoverageFromZero_(const std::vector<std::pair<uint16_t, uint16_t>>& ranges) {
  uint16_t covered = 0U;
  for (const auto& r : ranges) {
    if (r.first > covered) {
      break;
    }
    if (r.second > covered) {
      covered = r.second;
    }
  }
  return covered;
}

ManagementStatus topologyDescriptorErrorToStatus_(const std::string& message) {
  const std::string err = management_utils::lowerAscii(management_utils::trim(message));
  if (err.empty()) {
    return ManagementStatus::InternalError;
  }
  if (err == "topology_not_staged") {
    return ManagementStatus::TopologyNotStaged;
  }
  if (err == "topology_version_stale" || err == "topology stale") {
    return ManagementStatus::TopologyVersionStale;
  }
  if (err == "topology_apply_failed" ||
      err == "topology stage failed" ||
      err == "topology commit failed" ||
      err == "topology stage not active" ||
      err == "topology_stage_not_active") {
    return ManagementStatus::TopologyApplyFailed;
  }
  if (err == "topology source unauthorized" ||
      err == "topology_source_unauthorized" ||
      err == "source unauthorized") {
    return ManagementStatus::DeniedByRole;
  }
  if (management_utils::startsWith(err, "invalid_") ||
      management_utils::startsWith(err, "invalid ") ||
      err == "out_of_window" ||
      err == "index_unmapped" ||
      err == "duplicate_logical_peer" ||
      err == "duplicate_relative_index" ||
      err == "group_id_missing") {
    return ManagementStatus::BadPayload;
  }
  if (err == "pull request unsupported" ||
      err == "topology unsupported query" ||
      err == "unsupported") {
    return ManagementStatus::UnsupportedCommand;
  }
  return ManagementStatus::InternalError;
}

ManagementStatus descriptorErrorToStatus_(ManagementCommandId cmd,
                                          const std::string& message) {
  switch (cmd) {
    case ManagementCommandId::TopologyStageSet:
    case ManagementCommandId::TopologyCommit:
    case ManagementCommandId::TopologyStatusGet:
    case ManagementCommandId::TopologySlotsGet:
      return topologyDescriptorErrorToStatus_(message);
    default:
      return ManagementStatus::InternalError;
  }
}

}  // namespace

bool ManagementService::onPullResponse(const MacAddress& from,
                                       uint32_t corr_id,
                                       const uint8_t* payload,
                                       size_t len) {
  std::lock_guard<std::recursive_mutex> lk(state_mx_);
  if (corr_id == 0U || pending_descriptor_pulls_.empty()) {
    return true;
  }

  auto it = std::find_if(pending_descriptor_pulls_.begin(),
                         pending_descriptor_pulls_.end(),
                         [&](const PendingDescriptorPull& pending) {
                            if (pending.peer != from) {
                              return false;
                            }
                            if (pending.wait_topology_stage_done) {
                              return corr_id == pending.corr_last;
                            }
                            const uint32_t corr_first = (pending.corr_first == 0U) ? pending.req_id : pending.corr_first;
                            const uint32_t corr_last = (pending.corr_last == 0U) ? pending.req_id : pending.corr_last;
                            return corr_id >= corr_first && corr_id <= corr_last;
                          });
  if (it == pending_descriptor_pulls_.end()) {
    return true;
  }

  PendingDescriptorPull& pending = *it;
  if (pending.wait_topology_stage_done && corr_id != pending.corr_last) {
    // Ack-paced stage flow allows exactly one in-flight correlation.
    // Ignore stale/out-of-order retries from earlier correlations.
    return true;
  }

  std::vector<uint8_t> queued_payload{};
  if (payload != nullptr && len > 0U) {
    queued_payload.assign(payload, payload + len);
  }

  ManagementStatus status = queued_payload.empty() ? ManagementStatus::InternalError : ManagementStatus::Ok;
  PullResponseDecoded decoded{};
  bool decoded_ok = false;
  if (!queued_payload.empty() && pull_ != nullptr) {
    if (!pull_->decodePullResponseWithActiveCodec(queued_payload.data(), queued_payload.size(), decoded)) {
      status = ManagementStatus::InternalError;
    } else if (decoded.kind == PullResponseKind::Descriptor) {
      pull_descriptor_payload_scratch_.clear();
      if (!encodeDescriptorResponse(decoded.descriptor, pull_descriptor_payload_scratch_)) {
        status = ManagementStatus::InternalError;
      } else {
        queued_payload.resize(pull_descriptor_payload_scratch_.size());
        if (!pull_descriptor_payload_scratch_.empty()) {
          std::memcpy(queued_payload.data(),
                      pull_descriptor_payload_scratch_.data(),
                      pull_descriptor_payload_scratch_.size());
        }
      }
      if (decoded.descriptor.type == DescriptorResponseType::Error) {
        status = descriptorErrorToStatus_(static_cast<ManagementCommandId>(pending.cmd_id),
                                          decoded.descriptor.message);
      }
      decoded_ok = true;
    } else if (decoded.kind == PullResponseKind::ControlResult) {
      status = (decoded.control.result_code == 0U) ? ManagementStatus::Ok : ManagementStatus::InternalError;
      decoded_ok = true;
    } else {
      status = ManagementStatus::InternalError;
    }
  }

  bool keep_pending = false;
  if (pending.wait_topology_stage_done) {
    const ManagementCommandId pending_cmd = static_cast<ManagementCommandId>(pending.cmd_id);
    if (!decoded_ok || decoded.kind != PullResponseKind::Descriptor) {
      status = ManagementStatus::InternalError;
      keep_pending = false;
    } else if (decoded.descriptor.type == DescriptorResponseType::Error) {
      status = descriptorErrorToStatus_(pending_cmd, decoded.descriptor.message);
      keep_pending = false;
    } else if (decoded.descriptor.type != DescriptorResponseType::Ack) {
      status = ManagementStatus::InternalError;
      keep_pending = false;
    } else {
        const auto dispatch_next_stage_step = [&](uint8_t next_step) -> bool {
          if (pull_ == nullptr) {
            status = ManagementStatus::InternalError;
            keep_pending = false;
            return false;
          }

          uint32_t send_corr = allocateInternalCorrelation_();
          if (send_corr == 0U) {
            send_corr = pending.corr_last + 1U;
            if (send_corr == 0U) {
              send_corr = 1U;
            }
          }

          auto mark_dispatched = [&]() {
            pending.corr_first = send_corr;
            pending.corr_last = send_corr;
            pending.topology_stage_next_corr = send_corr + 1U;
            if (pending.topology_stage_next_corr == 0U) {
              pending.topology_stage_next_corr = 1U;
            }
          pending.topology_stage_step = next_step;
          if (pending.topology_stage_expected_responses < 0xFFFFU) {
            ++pending.topology_stage_expected_responses;
          }
          pending.topology_stage_seen_bitmap.push_back(0U);
          keep_pending = true;
          status = ManagementStatus::OkDeferred;
        };

        const auto& snap = pending.topology_stage_snapshot;
        if (next_step == kTopologyStageStepWaitBeginAck) {
          if (!pull_->requestTopologyStageBegin(pending.peer,
                                                snap.schema_version,
                                                snap.topology_version,
                                                snap.index_neg,
                                                snap.index_pos,
                                                send_corr)) {
            status = ManagementStatus::InternalError;
            keep_pending = false;
            return false;
          }
          mark_dispatched();
          return true;
        }
        if (next_step == kTopologyStageStepWaitGroupAck) {
          if (pending.topology_stage_group_cursor >= snap.groups.size()) {
            status = ManagementStatus::InternalError;
            keep_pending = false;
            return false;
          }
          const ManagementTopologyGroupSeedPayload& group =
              snap.groups[pending.topology_stage_group_cursor];
          if (!pull_->requestTopologyStageGroupSet(pending.peer, group, send_corr)) {
            status = ManagementStatus::InternalError;
            keep_pending = false;
            return false;
          }
          ++pending.topology_stage_group_cursor;
          mark_dispatched();
          return true;
        }
        if (next_step == kTopologyStageStepWaitSlotAck) {
          if (pending.topology_stage_slot_cursor >= snap.slots.size()) {
            status = ManagementStatus::InternalError;
            keep_pending = false;
            return false;
          }
          const ManagementTopologySlotPayload& slot =
              snap.slots[pending.topology_stage_slot_cursor];
          if (!pull_->requestTopologyStageSlotSet(pending.peer, slot, send_corr)) {
            status = ManagementStatus::InternalError;
            keep_pending = false;
            return false;
          }
          ++pending.topology_stage_slot_cursor;
          mark_dispatched();
          return true;
        }
        if (next_step == kTopologyStageStepWaitFinalizeAck) {
          if (!pull_->requestTopologyStageFinalize(pending.peer, send_corr)) {
            status = ManagementStatus::InternalError;
            keep_pending = false;
            return false;
          }
          pending.topology_stage_finalize_corr = send_corr;
          mark_dispatched();
          return true;
        }

        status = ManagementStatus::InternalError;
        keep_pending = false;
        return false;
      };

      const auto dispatch_next_data_or_finalize = [&]() -> bool {
        const auto& snap = pending.topology_stage_snapshot;
        if (pending.topology_stage_group_cursor < snap.groups.size()) {
          return dispatch_next_stage_step(kTopologyStageStepWaitGroupAck);
        }
        if (pending.topology_stage_slot_cursor < snap.slots.size()) {
          return dispatch_next_stage_step(kTopologyStageStepWaitSlotAck);
        }
        return dispatch_next_stage_step(kTopologyStageStepWaitFinalizeAck);
      };

      if (pending.topology_stage_seen_responses < 0xFFFFU) {
        ++pending.topology_stage_seen_responses;
      }
      switch (pending.topology_stage_step) {
        case kTopologyStageStepWaitClearAck:
          (void)dispatch_next_stage_step(kTopologyStageStepWaitBeginAck);
          break;
        case kTopologyStageStepWaitBeginAck:
          (void)dispatch_next_data_or_finalize();
          break;
        case kTopologyStageStepWaitGroupAck:
          (void)dispatch_next_data_or_finalize();
          break;
        case kTopologyStageStepWaitSlotAck:
          (void)dispatch_next_data_or_finalize();
          break;
        case kTopologyStageStepWaitFinalizeAck:
          pending.topology_stage_finalize_seen = true;
          pending.topology_stage_step = kTopologyStageStepNone;
          status = ManagementStatus::Ok;
          keep_pending = false;
          break;
        default:
          status = ManagementStatus::InternalError;
          keep_pending = false;
          break;
      }

      if (!keep_pending &&
          status == ManagementStatus::Ok &&
          pending.topology_stage_finalize_corr != 0U &&
          corr_id != pending.topology_stage_finalize_corr) {
        status = ManagementStatus::InternalError;
      }
    }
  } else if (pending.wait_node_bundle_done &&
      decoded_ok &&
      decoded.kind == PullResponseKind::Descriptor &&
      status == ManagementStatus::Ok) {
    if (decoded.descriptor.type == DescriptorResponseType::Error) {
      status = ManagementStatus::InternalError;
      keep_pending = false;
    } else {
      // NodeBundleGet may be emitted in multiple paged descriptor chunks.
      // Keep request pending until the paging terminal marker is observed.
      bool bundle_done = (!decoded.descriptor.is_paged || decoded.descriptor.done);
      if (pending.wait_node_bundle_strict_done &&
          decoded.descriptor.type == DescriptorResponseType::NodeBundle) {
        // Strict settings bundle completion (settings cache path):
        // require full contiguous coverage [0..total_count) and at least one
        // observed final marker. This prevents premature terminal completion
        // when paged frames arrive out-of-order.
        if (!decoded.descriptor.is_paged) {
          pending.node_bundle_settings_ranges.clear();
          const uint16_t one_shot_count = static_cast<uint16_t>(
              std::min<size_t>(decoded.descriptor.settings.size(), 0xFFFFU));
          mergeCoverageRange_(pending.node_bundle_settings_ranges, 0U, one_shot_count);
          pending.node_bundle_done_seen = true;
          pending.node_bundle_total_known = true;
          pending.node_bundle_expected_total = one_shot_count;
          bundle_done = true;
        } else {
          const uint16_t start = decoded.descriptor.cursor;
          const uint16_t count = decoded.descriptor.returned_count;
          const uint16_t end = static_cast<uint16_t>(
              std::min<uint32_t>(static_cast<uint32_t>(start) + static_cast<uint32_t>(count), 0xFFFFU));
          mergeCoverageRange_(pending.node_bundle_settings_ranges, start, end);
          if (decoded.descriptor.total_count > 0U || decoded.descriptor.done) {
            pending.node_bundle_total_known = true;
            pending.node_bundle_expected_total = decoded.descriptor.total_count;
          }
          if (decoded.descriptor.done) {
            pending.node_bundle_done_seen = true;
          }
          const uint16_t contiguous = contiguousCoverageFromZero_(pending.node_bundle_settings_ranges);
          bool coverage_complete = false;
          if (pending.node_bundle_total_known) {
            coverage_complete = (pending.node_bundle_expected_total == 0U) ||
                                (contiguous >= pending.node_bundle_expected_total);
          }
          bundle_done = pending.node_bundle_done_seen && coverage_complete;
          std::printf("[ESPNOW][BUNDLE] req=%lu peer=%s cursor=%u returned=%u next=%u total=%u done=%u covered=%u complete=%u\n",
                      static_cast<unsigned long>(pending.req_id),
                      macToString(from).c_str(),
                      static_cast<unsigned int>(decoded.descriptor.cursor),
                      static_cast<unsigned int>(decoded.descriptor.returned_count),
                      static_cast<unsigned int>(decoded.descriptor.next_cursor),
                      static_cast<unsigned int>(pending.node_bundle_expected_total),
                      pending.node_bundle_done_seen ? 1U : 0U,
                      static_cast<unsigned int>(contiguous),
                      bundle_done ? 1U : 0U);
        }
      }
      if (pending.wait_node_bundle_strict_done &&
          decoded.descriptor.type != DescriptorResponseType::NodeBundle) {
        status = ManagementStatus::InternalError;
        keep_pending = false;
      } else if (!bundle_done) {
        status = ManagementStatus::OkDeferred;
        keep_pending = true;
      }
    }
  }

  const ManagementSource response_source = pending.source;
  const uint16_t response_cmd_id = pending.cmd_id;
  const uint32_t response_req_id = pending.req_id;
  const PeerResolveContext response_peer_ctx = pending.peer_ctx;
  const MacAddress response_peer = pending.peer;
  const bool topology_stage_terminal =
      pending.wait_topology_stage_done && !keep_pending &&
      response_cmd_id == static_cast<uint16_t>(ManagementCommandId::TopologyStageSet);
  const ManagementStatus topology_stage_status = status;

  if (keep_pending) {
    pending.response_chunks += 1U;
    uint32_t extend_ms = commandTimeoutMs(pending.cmd_id);
    if (extend_ms == 0U) {
      extend_ms = 1500U;
    }
    pending.deadline_ms = now_ms_ + extend_ms;
  } else {
    pending_descriptor_pulls_.erase(it);
  }

  queueResponse(response_source,
                response_cmd_id,
                response_req_id,
                status,
                queued_payload,
                &response_peer_ctx);
  if (topology_stage_terminal) {
    dispatchDeferredTopologyCommitsForPeer_(response_peer, topology_stage_status);
  }
  return true;
}

}  // namespace espnow_link
