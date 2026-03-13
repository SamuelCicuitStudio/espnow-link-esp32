# Architecture

## Core Runtime Stack

- `EspNowManager`
- `PairingEngine`
- `ProtocolCodec` and selected profile codec
- `MasterPullClient` (master-side pull/control helper)

## Management Stack

- `ManagementService` (command execution authority)
- `ManagementRuntime` (request intake + response/event routing)
- `ManagementQueueTransport` (typed mailbox transport)
- `ManagementController` (typed command submit API)
- `ManagementFrontendAdapter` (frontend orchestration/cache layer)

## Frontend Stack

- `MasterCli` (reference operator frontend)
- Wi-Fi/BLE/custom frontends using `ManagementFrontendAdapter`

All frontends terminate into the same command IDs and status/event model.

## Bootstrap Graph (Master)

Default wiring from `MasterNodeBootstrap`:

`PairingStore -> EspNowManager -> MasterPullClient -> ManagementService -> ManagementRuntime -> transports/frontends`

`MasterCli` and frontend adapters submit through queue transport mode.

## Ownership and Isolation

- `ManagementService` owns execution state and command gating
- `ManagementRuntime` owns source-aware routing
- Each frontend adapter owns its local cache and operation tracker
- `ManagementSource` isolates CLI/Wi-Fi/BLE/custom response paths

## Concurrency Baseline

CLI and frontend API can run simultaneously. Determinism depends on request discipline:

- explicit target peer for peer-bound commands
- stable request-id tracking
- no ambiguous mixed mutating operations for the same peer at the same time

## Code Anchors

- `include/espnow_link/espnow_link.hpp`
- `include/espnow_link/master_runtime_defaults.hpp`
- `include/espnow_link/management_service.hpp`
- `include/espnow_link/management_runtime.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`
