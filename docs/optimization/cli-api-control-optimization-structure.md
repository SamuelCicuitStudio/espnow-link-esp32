# CLI/API Control Optimization Structure

## Purpose

Define a precise control contract where CLI and frontend API have the same operational behavior, same completion semantics, and same conflict policy.

This optimization keeps current feature scope and command IDs. It allows signature refinements for determinism and frontend integration quality.

## Hard Constraint: Feature Freeze

This document is optimization-only.

Allowed:

- signature refinements
- return/result shape refinements
- orchestration/refactoring improvements
- deterministic lifecycle and conflict policies

Not allowed:

- new command IDs
- new control domains
- new runtime capabilities
- new persistent behaviors/settings
- new topology/OTA/push feature semantics

## Non-Goals

- no new management command families
- no new device features
- no app-specific UI behavior

## Optimization Boundary (Must Not Cross)

Every optimization item must map to existing behavior from current CLI/API contracts.

If an item requires a new command capability, it is out of scope and must be rejected from this plan.

## Baseline Facts

- authority: `ManagementService`
- routing: `ManagementRuntime`
- command client: `ManagementController`
- sources: `Cli`, `Wifi`, `Ble`, `Custom`
- deferred commands already exist (pairing, channel sync, chain loop, OTA pipeline)
- pairing capacity remains 14

## Frontend Integration Goal

Frontend orchestration must be able to treat every control action with one consistent model:

1. submit request
2. get deterministic acceptance result
3. track lifecycle until terminal state
4. map terminal state to UI/result policy

No frontend path should infer success from "queued" alone.

## Master API Ease-Of-Use Contract

Master-facing frontend API must make these operations one-step and deterministic:

1. list paired devices
2. read descriptors/capabilities/settings/telemetry schema
3. read live telemetry
4. control push and auto-pull behavior
5. run topology operations
6. run OTA operations

This is optimization only. It wraps existing commands and state, and does not introduce new command IDs.

### Master API Facade (Additive)

```cpp
struct MasterControlApi {
  Result<PairedPeersView> pairedPeersGet();
  Result<PeerDescriptorBundleView> descriptorBundleGet(const MacAddress& peer,
                                                       const DescriptorBundleMask& mask);
  Result<TelemetryNowView> telemetryNowGet(const MacAddress& peer,
                                           const TelemetryNowOptions& opts);
  CommandLifecycleResult pushControl(const MacAddress& peer,
                                     const PushControlRequest& req);
  CommandLifecycleResult autoPullControl(bool enabled, uint32_t timeout_ms = 0, bool wait_terminal = true);
  Result<AutoPullStatusView> autoPullStatusGet();
  CommandLifecycleResult topologyControl(const MacAddress& peer,
                                         const TopologyControlRequest& req);
  CommandLifecycleResult otaControl(const MacAddress& peer,
                                    const OtaControlRequest& req);
};
```

Constraint:

- this facade is a wrapper/orchestrator over existing commands only
- it must not introduce new backend features

Required behavior:

- each mutating call receives explicit `peer`
- each mutating call returns lifecycle-aware result
- read calls return stable view objects ready for frontend rendering
- adapter may internally execute multiple existing commands, but returns one normalized result contract

### Current Implementation Snapshot

- CLI command handlers now route management submissions through one canonical helper (`submitRuntimeTargeted_`).
- Canonical helper supports explicit target override and request-id override (used for OTA push status correlation).
- Topology file deploy path now uses canonical targeted submit (stage + commit per peer).
- Descriptor queue immediate-send path now uses canonical targeted submit.
- `ManagementService` master role rejects peer-bound requests without explicit target (`BadPayload`), removing ambiguous active-peer execution.
- `ManagementFrontendAdapter` currently exposes:
  - `pairedPeersGet(...)`
  - `descriptorBundleGet(...)`
  - `telemetryNowGet(...)`
  - `pushControl(...)`
  - `autoPullControl(...)`
  - `autoPullStatusGet(...)`
  - `topologyControl(...)`
  - `otaControl(...)`
- CLI now exposes machine-readable dispatch outcome via `handleLineEx(...)` (no console text parsing required for automation parity harnesses).
- `handleLineEx(...)` now distinguishes local-only handled commands from validation failures when no submit/queue snapshot exists (`Ok` vs `BadPayload/validation`).
- CLI handlers now emit explicit dispatch snapshots for management-unavailable branches (`DeniedByPolicy/availability`) instead of relying on fallback interpretation.
- CLI handlers now emit explicit dispatch snapshots for target-missing validation branches (`BadPayload/target`) across settings, push, pairing, OTA, and paged-fetch entrypoints.
- CLI non-submit local branches in `sd.*`, selected `ota.*`, and logger local control paths now emit explicit dispatch snapshots (`Ok` or deterministic failure status), reducing fallback ambiguity.
- `ManagementFrontendAdapter` command execution is now transport-canonical (`transport_` required for `submit`); direct-service command fallback binding has been removed.
- `ManagementController::submit(...)` is now queue-transport-canonical (no direct-service submit backend path).
- `ManagementFrontendAdapter` still keeps `service_` only for radio transition lifecycle APIs and optional tick progression when runtime is unbound.
- `ManagementFrontendAdapter` command wait helpers (`commandRunAndWait` / `operationWait`) no longer service-tick fallback for command progression; they self-pump runtime when bound, otherwise rely on externally pumped queue/runtime.
- `ManagementFrontendAdapter` now exposes `commandTraitsGet(...)` for canonical command metadata (access/mutating/deferred/timeout/priority/transition-blocked).
- command trait access-level metadata is now sourced from `ManagementService::commandRequiredAccessLevel(...)` (single source, no duplicated access tables).
- `topologyControl(...)` and `otaControl(...)` now validate command-family membership and reject cross-domain command IDs with deterministic `DeniedByPolicy` validation errors.
- CLI management command submissions are now queue-transport-canonical (direct `ManagementService` submit construction removed from active command handlers).
- CLI short command path (`time.get`, `desc`, `telem.now`, `telem.now.child`, `live`, `ping`, `audio ping`) now requires management queue path and no longer falls back to legacy descriptor pull command text path.
- CLI `radio.drytest` now validates transition-blocked behavior through queue/runtime path only (no direct-service command submission branch).
- OTA archive management path now uses strict role-scoped manifest loading and strict `target_role` sidecar metadata writing/verification (no inferred fallback role).
- OTA descriptor adapter archive path now enforces required manifest fields and canonical metadata keys (`sw_version`, `build_id`, `target_role`) with no legacy key fallback.
- master CLI staged-OTA sidecar parsing now requires canonical metadata keys only (`sw_version`, `build_id`, `target_role`) and rejects alias/default fallback keys.
- OTA archive staged-save path now requires canonical sidecar filename (`<image>.json`) and no longer accepts `.jsn` fallback metadata files.
- CLI OTA archive staged-metadata parsing now requires canonical metadata keys only (`sw_version`, `build_id`, `target_role`) and rejects alias/default fallback keys.
- management-service sidecar + archive-verify metadata parsing now requires canonical `build_id` presence (no default/fallback build value).
- OTA image-manifest handling now uses canonical manifest filename path only (legacy `m_<fileid>.m` fallback/cleanup removed).
- Pull-response decoding now uses active codec path only (`decodePullResponseWithActiveCodec`); legacy/default decode entrypoint removed.
- Slave pull-response emission now uses manager codec encoding path only (legacy descriptor encoder fallback removed).
- Pairing persistence now uses canonical key-space only (legacy key migration/read/erase paths removed).
- Active-peer persistence key path has been removed from manager restore/sync flows; runtime pairing state now tracks through canonical pair-index/runtime peer state only.
- Queue transport overflow handling now uses explicit per-queue policy only (`RejectNew`/`DropOldest`); global fallback overflow mode has been removed.
- CLI paged descriptor fetch path is now strict: non-paged descriptor responses are treated as deterministic fetch failure (no compatibility render fallback).
- `ManagementFrontendAdapter::tick(...)` and radio-transition quiesce pumping now run runtime-only (service tick fallback branch removed).
- Descriptor settings query execution is now selector-strict (`setting_id` path never falls back to key resolution; key path never falls back to ID resolution).
- `IDescriptorProvider` now requires explicit `getSettingById(...)` and `setSettingById(...)`; default bridge implementation has been removed.
- Active providers now expose explicit ID resolvers for settings (`PmsDescriptorProvider` and app-owned `ICM/PMS/RELAY/SENS/SEMU/REMU`).
- `PmsDescriptorProvider` now emits canonical setting IDs in `getSettings(...)` and resolves single-setting operations through direct ID/key maps.
- app-owned slave profile providers (`PMS/RELAY/SENS/SEMU/REMU`) now return telemetry snapshots from runtime callback paths only; synthetic telemetry fallback generation has been removed.
- CLI cached remote-profile routing now uses resolved `ProfileId` semantics for child push/telemetry flows (`SEMU`/`REMU`) instead of string-guess branches.
- CLI command handlers now treat remote profile cache as ID-only runtime state (`remote_profile_id`); profile-name cache is no longer used in command execution paths.
- Capability responses now include canonical `profile_id` metadata so CLI/API orchestration can resolve profile contracts without profile-name lookup.
- `MasterCommTest` profile resolution + summary projection now use canonical `profile_id`/registry resolution path (capability profile-name cache removed from test runtime state).
- `ManagementFrontendAdapter` SEMU/REMU child push + child telemetry pull helpers now require explicit `peer` and track child push state per-peer; legacy global child-mask state is removed.
- CLI `push.child.start/stop` now keeps child-stream state per targeted peer (selector-scoped) instead of one global mask cache, preventing cross-peer state collisions.
- CLI persistent `active` target override has been removed; peer-bound CLI commands now rely on explicit per-command selector prefix only.
- legacy CLI `active` no-op compatibility command path has been removed; selector prefix is now the only targeting entrypoint.

### Required View Objects

```cpp
struct PairedPeersView {
  uint64_t generation = 0;
  std::vector<MacAddress> peers;
};

struct PeerDescriptorBundleView {
  MacAddress peer{};
  DeviceDescriptorView device{};
  CapabilitiesView capabilities{};
  SettingsSchemaView settings{};
  TelemetrySchemaView telemetry{};
};
```

These are read models only. They do not change service behavior.

### Frontend Operation Pipelines

Inventory pipeline:

1. `pairedPeersGet()`
2. parallel `descriptorBundleGet(peer, mask)` for visible peers

Live dashboard pipeline:

1. `telemetryNowGet(peer, opts)`
2. optional `pushControl(peer, start/update/get)` if live stream mode is enabled

Topology pipeline:

1. `topologyControl(peer, status/slots/stage/commit/deploy)`
2. track terminal state from lifecycle result

OTA pipeline:

1. `otaControl(peer, info/manifest/capacity/gate)`
2. mutate path `prepare -> push -> apply -> wait terminal`
3. verify final terminal and follow-up descriptor check

## Core Deterministic Rules

### Rule 1: Request Identity

Every tracked request key is:

- `(source, req_id, cmd_id)`

`req_id` uniqueness is required per source.

### Rule 2: Explicit Targeting For Peer Mutations

For peer-bound mutating commands:

- target peer must be explicit per call
- no hidden active-peer fallback in automation paths
- CLI runtime target resolution must not fall back to manager-owned active peer state

CLI target selection is selector-prefix only (`<paired_index|paired_mac> <command>`); there is no persistent active-target command path.

### Rule 3: Completion Contract

Accepted is not success.

Command is complete only when terminal:

- immediate command: terminal `ManagementResponse`
- deferred command: terminal event (`CmdDone`, `CmdFail`, or timeout path)

### Rule 4: Serialization By Mutation Lane

For same peer and mutation domain, only one active mutation at a time.

Mutation domains:

- pairing lifecycle
- settings/time writes
- telemetry push mutation
- topology/channel/chain mutation
- OTA mutation
- logger/storage mutation
- restart/reset

Reads can run concurrently unless service blocks them by policy.

### Rule 5: Same Timeout/Priority Semantics

CLI and API must use identical default timeout and priority behavior for the same command ID unless a documented override exists.

## Control Lifecycle Model

### Lifecycle States

```text
Parsed -> Submitted -> Accepted -> InFlight -> Terminal
```

Terminal states:

- `Done`
- `Fail`
- `Timeout`
- `RejectedBeforeAccept`

### Frontend Result Contract

Frontend should receive one normalized result shape:

```cpp
enum class CommandTerminalState : uint8_t {
  None = 0,
  Done,
  Fail,
  Timeout,
  RejectedBeforeAccept
};

struct CommandLifecycleResult {
  bool parsed = false;
  bool accepted = false;
  uint16_t cmd_id = 0;
  uint32_t req_id = 0;
  ManagementStatus status = ManagementStatus::InternalError;
  CommandTerminalState terminal = CommandTerminalState::None;
  bool terminal_from_event = false;
  const char* reject_stage = ""; // parse|target|queue|service|runtime
  const char* message = "";
};
```

This is not a new feature domain. It is a normalized reporting contract for existing behavior.

## Signature Refinements (Replacement)

### 1) `ManagementController::submit`

Replace legacy raw submit contract with:

```cpp
struct SubmitOptions {
  uint32_t req_id = 0;
  uint32_t timeout_ms = 0;
  uint8_t priority = 0;
  bool has_target_peer = false;
  MacAddress target_peer{};
};

struct SubmitResult {
  bool accepted = false;
  uint16_t cmd_id = 0;
  uint32_t req_id = 0;
  ManagementStatus status = ManagementStatus::InternalError;
  const char* reject_stage = "";
};

SubmitResult submit(uint16_t cmd_id,
                    const std::vector<uint8_t>& payload = {},
                    const SubmitOptions& opts = {});
```

Key optimization:

- target and timeout are per call, so there is no target bleed between operations.
- one canonical submit entrypoint for CLI/API orchestration
- raw `submit(...)` has no implicit target fallback; target must be in `SubmitOptions`

### 2) `ManagementFrontendAdapter::runAndTrack`

Add a terminal-tracking path that returns one object:

```cpp
struct RunOptions {
  SubmitOptions submit{};
  bool wait_terminal = true;
};

CommandLifecycleResult runAndTrack(uint16_t cmd_id,
                                   const std::vector<uint8_t>& payload = {},
                                   const RunOptions& opts = {});
```

Constraint:

- `runAndTrack` must compose existing command flow only
- no hidden extra command families

### 2b) Multi-Step Operation Runner (No New Commands)

Add optional helper to run composed flows (topology deploy, OTA update) with one lifecycle stream:

```cpp
struct OperationStep {
  uint16_t cmd_id = 0;
  std::vector<uint8_t> payload{};
  SubmitOptions submit{};
};

struct OperationRunResult {
  bool accepted = false;
  bool terminal = false;
  uint32_t operation_id = 0;
  std::vector<CommandLifecycleResult> steps{};
  CommandTerminalState terminal_state = CommandTerminalState::None;
};

OperationRunResult runOperation(const std::vector<OperationStep>& steps,
                                const OperationOptions& opts = {});
```

This is orchestration over existing commands only.

### 3) Queue Enqueue Result Detail

Keep bool enqueue methods. Add optional detailed variant:

```cpp
enum class QueueRejectReason : uint8_t {
  None = 0,
  FullRejectNew,
  FullDroppedOldest,
  Disabled
};

struct QueueEnqueueResult {
  bool accepted = false;
  QueueRejectReason reason = QueueRejectReason::None;
  uint16_t depth_after = 0;
};
```

Frontend and CLI harness can expose backpressure reasons instead of generic failure.

### 4) CLI Machine-Readable Dispatch Outcome

Keep `handleLine(...)`. Add:

```cpp
CommandLifecycleResult handleLineEx(const std::string& line);
```

This enables deterministic parity tests CLI vs API without parsing console text.

### 5) Optional Command Trait Introspection

Add a read-only trait query to avoid duplicated metadata in CLI/API docs:

```cpp
struct CommandTraits {
  ManagementAccessLevel min_access = ManagementAccessLevel::Observer;
  bool mutating = false;
  bool deferred_terminal = false;
  bool blocked_during_radio_transition = false;
  uint32_t default_timeout_ms = 0;
  uint8_t default_priority = 0;
};

bool commandTraitsGet(ManagementCommandId cmd, CommandTraits& out);
```

## Conflict Policy (Coordinator Layer)

A lightweight coordinator above submitters enforces lane ownership.

Ownership key:

- `(peer, mutation_domain)`

Policy:

1. first accepted request owns lane
2. second conflicting request is rejected locally with `RejectedBeforeAccept`
3. lane is released only on terminal state or timeout

This policy is orchestration only, not a new transport.

## Frontend Contract For Terminal Handling

Frontend state policy:

1. `accepted=false`: show immediate actionable error
2. `accepted=true` and terminal pending: show in-progress state
3. terminal `Done`: success
4. terminal `Fail` or `Timeout`: failure with retry option

Required behavior:

- do not mark success on acceptance alone
- correlate all updates with `(source, req_id, cmd_id)`
- expose final operation-level status for composed flows (topology/OTA)

## API Domain Coverage (Master)

Every domain below must have a direct API facade path:

- paired inventory
- descriptor bundle
- live telemetry
- push + auto-pull control
- topology control
- OTA control

Coverage requirement:

- no domain should require frontend-side CLI text parsing
- no domain should require frontend to manually reconstruct command sequencing
- no domain may rely on new backend features outside current implementation

## Compliance Gate: No Feature Addition

For each proposed optimization change, document:

1. existing command IDs used
2. existing payload/response/events used
3. what changed only in signatures/orchestration/format

If item (1) or (2) cannot be satisfied with current implementation, the change is rejected.

## Queue And Backpressure Baseline

Recommended defaults:

- request queue policy: `RejectNew`
- response queue policy: `RejectNew`
- event queue policy: `DropOldest` only for non-critical high-rate streams

Always monitor:

- transport queue stats (`request/response/event`)
- runtime dropped-route and transport-rejected counters

## CLI/API Parity Matrix Requirements

Parity is command-ID level, not string-output level.

For each operational topic:

- same command ID mapping
- same target requirements
- same timeout behavior
- same terminal semantics
- same failure status classes

Domains to validate:

- pairing/lifecycle
- descriptor/settings
- telemetry pull/push
- topology/channel/chain
- logger/storage
- OTA
- diagnostics

## Validation Plan

### Lane A: API Only

- run full domain matrix
- verify acceptance vs terminal correctness

### Lane B: CLI Only

- run same matrix
- verify `handleLineEx` parity with adapter run path

### Lane C: Mixed Surfaces

- CLI read with API mutate
- API read with CLI mutate
- same-peer mutation collision tests

### Lane D: Radio Transition

- verify blocked commands return `BusyRadioTransition`
- verify recovery after transition end

Acceptance gates:

- zero ambiguous target execution
- zero orphan accepted deferred commands without terminal state
- parity matrix pass across all domains

## Migration Plan

### Phase 1 (Canonical Contract)

- replace raw submit signatures with canonical submit result contract
- add `runAndTrack` and lifecycle result normalization

### Phase 2 (Adapter Migration)

- migrate frontend and CLI paths to lifecycle result objects only
- remove compatibility wrappers

### Phase 3 (Policy Enforcement)

- enforce per-call target for automation paths
- enforce lane ownership for peer mutation domains

### Phase 4 (Removal Completion)

- remove ambiguous stateful-target automation usage
- remove bool-only orchestration paths where lifecycle accuracy is required
- remove legacy selector forms (`p:<index>`) from CLI3 target parsing (completed)
- remove no-op active-peer compatibility hooks from CLI/bootstrap paths (completed)

## Acceptance Criteria

- same control depth available via CLI and frontend API
- deterministic lifecycle tracking for every submitted command
- no hidden success inference from queue acceptance
- no cross-surface target ambiguity
- no feature-scope expansion
- paired list, descriptor bundle, telemetry-now, push/autopull, topology, and OTA all callable from API facade with normalized results
- composed flows (topology deploy, OTA update) produce one operation-level terminal result
- every optimization item maps to existing command behavior without feature extension

## Code Anchors

- `include/espnow_link/management_controller.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`
- `include/espnow_link/management_queue_transport.hpp`
- `include/espnow_link/management_types.hpp`
- `src/management/management_service.cpp`
- `src/management/management_runtime.cpp`
- `src/cli/cli_master.cpp`
