# Topology and Multi-Slave

## Runtime Limits

Current management topology payload limits:

- max topology slots: 13 (`kManagementTopologyMaxSlots`)
- max group seeds: 12 (`kManagementTopologyMaxGroups`)

Master persisted paired capacity is 14 peers.

## Topology Commands

- `TopologyStageSet`
- `TopologyCommit`
- `TopologyStatusGet`
- `TopologySlotsGet`
- `TopologyTriggerSend`

Lifecycle/result events include staged/committed/commit-failed/trigger received/rejected/ack.

## Aggregate Controls

- `ChannelSyncAll`
- `ChainLoopControlSet`

These flows are deferred and complete through lifecycle events (`CmdDone`/`CmdFail`) with aggregate result payloads.

## Deterministic Behavior

- no automatic peer eviction at capacity
- explicit operator-driven remove/reconfigure
- explicit targeting recommended for peer-bound topology operations

## Code Anchors

- `include/espnow_link/management_types.hpp`
- `include/espnow_link/management_utils.hpp`
- `src/management/management_service.cpp`
