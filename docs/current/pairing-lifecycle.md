# Pairing and Lifecycle

## Discovery

Management commands:

- `DiscoveryStart`
- `DiscoveryStop`
- `DiscoverySnapshotGet`

Discovery updates are emitted as management events (`DiscoveryUpdate`, `DiscoveryStopped`, `DiscoveryFinished`).

## Pairing

Management commands:

- `PairRequest`
- `UnpairRequest`
- `RemovePeerRequest`
- `PairedSnapshotGet`

Pairing/unpairing completion is lifecycle/event driven (`CmdDone`/`CmdFail` and domain events).

## Settings Cache Hydration Policy (Target)

Library cache policy direction for paired slaves:

1. On fresh pair success:
   - schedule one settings bundle hydrate (`NodeBundleGet(settings)` path).
2. On startup with already-paired peers:
   - run startup reconcile hydration for peers with empty/partial cache.
3. Keep refresh explicit:
   - force pull only on user refresh or post-write authoritative sync.

This avoids empty settings overlays after reboot and prevents repeated unsolicited pulls.

## Capacity Constraint

Current master persisted pairing limit is 14 peers.

At capacity, pair/discovery-start requests are rejected with `CapacityLimitReached` until a peer is removed.

## Target Resolution

Peer-bound commands use explicit target semantics.

- master role: peer-bound commands require explicit target peer in request envelope
- slave role: single-peer runtime context is used

For deterministic multi-frontend behavior (CLI + API), master-side calls must always provide target explicitly.

## Lifecycle Control Commands

- `RestartSlaveRequest`
- `ResetSlaveRequest`
- `RestartMasterRequest`
- `ResetMasterRequest`
- `AudioPingRequest`

Critical commands are access-level and policy gated.

## Code Anchors

- `include/espnow_link/pairing.hpp`
- `include/espnow_link/management_types.hpp`
- `src/management/management_service.cpp`
- `src/core/manager.cpp`
