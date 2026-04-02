#include "espnow_link/management_service.hpp"

#include <utility>
#include <vector>

#include "espnow_link/descriptor.hpp"
#include "espnow_link/management_utils.hpp"

namespace espnow_link {

namespace {

constexpr uint8_t kTopologyStageStepNone = 0U;
constexpr uint8_t kTopologyStageStepWaitClearAck = 1U;

}  // namespace
void ManagementService::trackPendingDescriptorPull_(const ManagementRequest& request,
                                                    const PeerResolveContext& peer_ctx,
                                                    const MacAddress& peer,
                                                    uint32_t corr_first,
                                                    uint32_t corr_last,
                                                    bool wait_topology_stage_done,
                                                    uint32_t topology_stage_finalize_corr,
                                                    const ManagementTopologySnapshotPayload* topology_stage_snapshot) {
  if (request.req_id == 0U) {
    return;
  }
  PendingDescriptorPull pending{};
  pending.req_id = request.req_id;
  pending.cmd_id = request.cmd_id;
  pending.source = request.source;
  pending.peer = peer;
  pending.peer_ctx = peer_ctx;
  pending.corr_first = (corr_first == 0U) ? request.req_id : corr_first;
  pending.corr_last = (corr_last == 0U) ? pending.corr_first : corr_last;
  if (pending.corr_last < pending.corr_first) {
    pending.corr_last = pending.corr_first;
  }
  pending.wait_node_bundle_done =
      (static_cast<ManagementCommandId>(request.cmd_id) == ManagementCommandId::NodeBundleGet);
  pending.wait_node_bundle_strict_done = false;
  pending.wait_topology_stage_done = wait_topology_stage_done;
  pending.topology_stage_step = kTopologyStageStepNone;
  pending.topology_stage_group_cursor = 0U;
  pending.topology_stage_slot_cursor = 0U;
  pending.topology_stage_next_corr = 0U;
  pending.topology_stage_finalize_corr =
      (topology_stage_finalize_corr == 0U) ? pending.corr_last : topology_stage_finalize_corr;
  pending.topology_stage_finalize_seen = false;
  pending.topology_stage_snapshot = ManagementTopologySnapshotPayload{};
  if (pending.wait_topology_stage_done) {
    if (topology_stage_snapshot != nullptr) {
      pending.topology_stage_snapshot = *topology_stage_snapshot;
    }
    pending.topology_stage_step = kTopologyStageStepWaitClearAck;
    pending.topology_stage_next_corr =
        (pending.corr_last == 0xFFFFFFFFU) ? pending.corr_last : (pending.corr_last + 1U);
    pending.topology_stage_expected_responses = 1U;
    pending.topology_stage_seen_responses = 0U;
    pending.topology_stage_seen_bitmap.assign(1U, 0U);
  } else {
    pending.topology_stage_expected_responses = 0U;
    pending.topology_stage_seen_responses = 0U;
    pending.topology_stage_seen_bitmap.clear();
  }
  if (pending.wait_node_bundle_done) {
    uint8_t bundle_mask = 0U;
    if (management_utils::parseNodeBundleGetPayload(request.payload, bundle_mask)) {
      pending.wait_node_bundle_strict_done = ((bundle_mask & kNodeBundleMaskSettings) != 0U);
    }
  }
  pending.response_chunks = 0U;
  pending.node_bundle_done_seen = false;
  pending.node_bundle_total_known = false;
  pending.node_bundle_expected_total = 0U;
  pending.node_bundle_settings_ranges.clear();
  uint32_t timeout_ms = request.timeout_ms;
  if (timeout_ms == 0U) {
    timeout_ms = commandTimeoutMs(request.cmd_id);
  }
  if (timeout_ms == 0U) {
    timeout_ms = 1500U;
  }
  pending.deadline_ms = now_ms_ + timeout_ms;

  for (auto& existing : pending_descriptor_pulls_) {
    if (existing.source == pending.source &&
        existing.peer == pending.peer &&
        existing.corr_first == pending.corr_first &&
        existing.corr_last == pending.corr_last) {
      existing = pending;
      return;
    }
  }
  pending_descriptor_pulls_.push_back(std::move(pending));
}

void ManagementService::dispatchDeferredTopologyCommitsForPeer_(const MacAddress& peer,
                                                                ManagementStatus stage_status) {
  if (deferred_topology_commits_.empty()) {
    return;
  }

  std::vector<PendingDeferredTopologyCommit> keep{};
  keep.reserve(deferred_topology_commits_.size());
  for (auto& deferred : deferred_topology_commits_) {
    if (deferred.peer != peer) {
      keep.push_back(std::move(deferred));
      continue;
    }

    if (stage_status != ManagementStatus::Ok) {
      queueResponse(deferred.source,
                    deferred.cmd_id,
                    deferred.req_id,
                    stage_status,
                    {},
                    &deferred.peer_ctx);
      continue;
    }

    if (pull_ == nullptr || !pull_->requestTopologyCommit(deferred.peer, deferred.req_id)) {
      queueResponse(deferred.source,
                    deferred.cmd_id,
                    deferred.req_id,
                    ManagementStatus::InternalError,
                    {},
                    &deferred.peer_ctx);
      continue;
    }

    ManagementRequest synthetic{};
    synthetic.source = deferred.source;
    synthetic.cmd_id = deferred.cmd_id;
    synthetic.req_id = deferred.req_id;
    synthetic.timeout_ms =
        (deferred.deadline_ms > now_ms_) ? static_cast<uint32_t>(deferred.deadline_ms - now_ms_) : 0U;
    trackPendingDescriptorPull_(synthetic,
                                deferred.peer_ctx,
                                deferred.peer,
                                deferred.req_id,
                                deferred.req_id,
                                false,
                                0U);
  }
  deferred_topology_commits_.swap(keep);
}

void ManagementService::prunePendingDescriptorPulls_() {
  if (pending_descriptor_pulls_.empty()) {
    // Continue to prune deferred topology commits even without pending pulls.
  } else {
    auto it = pending_descriptor_pulls_.begin();
    while (it != pending_descriptor_pulls_.end()) {
      if (static_cast<int32_t>(now_ms_ - it->deadline_ms) >= 0) {
        it = pending_descriptor_pulls_.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (!deferred_topology_commits_.empty()) {
    std::vector<PendingDeferredTopologyCommit> keep{};
    keep.reserve(deferred_topology_commits_.size());
    for (const auto& deferred : deferred_topology_commits_) {
      if (static_cast<int32_t>(now_ms_ - deferred.deadline_ms) >= 0) {
        queueResponse(deferred.source,
                      deferred.cmd_id,
                      deferred.req_id,
                      ManagementStatus::Timeout,
                      {},
                      &deferred.peer_ctx);
      } else {
        keep.push_back(deferred);
      }
    }
    deferred_topology_commits_.swap(keep);
  }
}


}  // namespace espnow_link

