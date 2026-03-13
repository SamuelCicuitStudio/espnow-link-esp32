# Control Plane

## Wire Contract

Shared envelopes:

- `ManagementRequest`
- `ManagementResponse`
- `ManagementEvent`

Shared command IDs are defined in `ManagementCommandId` and used by CLI and frontend APIs without translation.

CLI log/render text formatting (`[MASTER][DESC]`, `[MASTER][SET]`, `[MASTER][STORAGE]`, etc.) is presentation-only and not part of the wire contract.

## Source and Access Model

Source identity (`ManagementSource`):

- `Cli`
- `Wifi`
- `Ble`
- `Custom`
- `Unknown`

Access levels (`ManagementAccessLevel`):

- `Observer`
- `Operator`
- `Maintainer`
- `Owner`

`ManagementRuntime` stamps source/access from transport on inbound requests, preventing client-side spoofing.

## Command Families

- Discovery, pairing, peer management
- Descriptor/settings/telemetry/liveness/time
- Push control
- Topology stage/commit/status/trigger
- Restart/reset/audio ping and CLI control
- Local/remote logger and channel/chain controls
- Storage control
- OTA status/manifest/transfer/push/update/archive
- Diagnostics (`comm test`, `metrics`, `queue`)

## Status Codes

Core success/deferred:

- `Ok`
- `OkDeferred`

Validation/policy/routing failures:

- `UnsupportedCommand`
- `BadPayload`
- `NotPaired`
- `QueueFull`
- `BusyPairing`
- `UnpairInProgress`
- `SourceNotActiveMaster`
- `DeniedByPolicy`
- `Timeout`
- `DeniedByRole`
- `CapacityLimitReached`
- `TopologyNotStaged`
- `TopologyVersionStale`
- `TopologyApplyFailed`
- `BusyRadioTransition`
- `InternalError`

## Lifecycle and Domain Events

Command lifecycle:

- `CmdRx`
- `CmdDone`
- `CmdFail`
- `Timeout`
- `QueueFull`

Domain events include discovery, pair/unpair, peer removal, OTA transfer status, peer liveness transition, and topology events.

## Routing Rules

- Responses are routed to transports whose source matches `response.source`
- Events are source-routed, except `event.source == Unknown`, which broadcasts
- Peer execution metadata may be attached to responses/events:
  - requested peer
  - executed peer
  - activation performed flag
  - activation latency

## Queue Transport Behavior

`ManagementQueueTransport` has independent request/response/event queues with per-queue overflow policy:

- `RejectNew`
- `DropOldest`

Queue admission counters expose enqueued/rejected/dropped-oldest counts per queue.

## Code Anchors

- `include/espnow_link/management_types.hpp`
- `include/espnow_link/management_transport.hpp`
- `include/espnow_link/management_queue_transport.hpp`
- `src/management/management_runtime.cpp`
- `src/management/management_service.cpp`

## Related

- `../optimization/cli-api-control-optimization-structure.md` (coexistence and optimization structure)
- `cli.md` (current CLI render/output contract examples)
