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

## Project Chain Authoring Profile (Fixed File)

For EasyDriveway ICM workflow, chain authoring is pinned to:

- `D:\Freelancer\cornetb5\EasyDriveway-production\data\icm\o\s\tp.json`

Locked chain format:

- root: `v`, `seed`, `chain`
- chain node: `t`, `m`, `vi`
- tokens: `S`, `R`, `SM`, `RM`

Recommended CLI extension for operators:

- `topology.chain.*` commands (show/graph/clear/add/edit/del/move/validate/fix/apply/backup/restore)
- bulk entry command: `topology.chain.set <chain_spec>`
- bulk syntax: `<TYPE>@<PEER>[#<CH>]` joined by `>`

Paired-index example:

- `topology.chain.set S@0>R@1>RM@2#11>R@1>S@0...`

## Code Anchors

- `include/espnow_link/management_types.hpp`
- `include/espnow_link/management_utils.hpp`
- `src/management/management_service.cpp`
