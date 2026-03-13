# Execution Goal Lock

## North-Star Goal

Deliver deterministic CLI/API convergence and frontend-ready profile resolution using only existing library capabilities.

## Hard Constraints

- no new features
- no new command IDs
- no new profile IDs
- no new telemetry/setting/event fields
- no new topology or OTA behavior semantics

If any task requires a new capability, stop and move it to a separate future-feature document.

## Locked Phase Order

1. Control-plane optimization first.
2. Profile/frontend optimization second.

No phase-2 implementation starts until phase-1 exit criteria are met.

## Phase 1 Scope: CLI/API Control Convergence

Reference:

- `docs/optimization/cli3-current-command-contract.md`
- `docs/optimization/cli-api-control-optimization-structure.md`

Phase-1 mandatory outcomes:

- explicit per-call target for peer mutating operations
- deterministic request lifecycle tracking (`accepted`, `done/fail/timeout`)
- same terminal semantics in CLI and API paths
- mutation-lane conflict policy enforced for same peer/domain
- API facade wrappers for paired list, descriptor bundle, telemetry-now, push/autopull, topology, and OTA using existing commands only

Phase-1 exit criteria:

- zero ambiguous target executions in validation suite
- zero accepted deferred requests without terminal outcome
- parity matrix pass across pairing, descriptors/settings, telemetry push, topology, OTA, logger/storage, diagnostics

## Phase 2 Scope: Profile + Frontend Optimization

Reference:

- `docs/optimization/profiles-frontend-optimization-structure.md`

Phase-2 mandatory outcomes:

- immutable snapshot with indexed lookup maps
- O(1) resolution by `(profile_id, key)` and `(profile_id, id)`
- deterministic schema invalidation via `generation` plus schema metadata
- dynamic child handling (`SEMU`/`REMU`) through metadata rules, not ad-hoc regex paths
- consistent role metadata usage for topology-related profiles (`SEMU`, `REMU`, `SENS`, `RELAY`)

Phase-2 exit criteria:

- zero duplicate/ambiguous profile mappings
- no frontend profile-guessing path in active adapters
- CLI/API profile resolution parity checks pass

## Definition Of Done

Optimization work is done only when all conditions below are true:

- hard constraints are not violated
- both phase exit criteria are satisfied
- docs reflect final behavior and signatures
- no reliance on CLI text parsing in frontend integration flows

## Change Control Rules

- every implementation change must map to a line item in phase scope
- every PR/task must state: existing commands used, existing fields used, and optimization-only impact
- any out-of-scope discovery must be logged as `Future Feature` and excluded from current implementation

## Stop Conditions

Stop implementation and update docs before continuing if:

- a requirement implies a new command or new field
- parity cannot be achieved without changing feature behavior
- a signature change cannot be propagated to all active CLI/API/frontend paths in the same phase
- runtime behavior differs from documented baseline contracts

## Implementation Rhythm

For each step:

1. select one scoped item
2. implement canonical replacement change
3. run parity/validation checks for that item
4. update optimization docs and status notes
5. proceed to next scoped item

## Phase 1 Progress Notes (Implementation Snapshot)

- done: CLI runtime target resolution no longer falls back to manager-owned active peer.
- done: master-side peer-bound commands require explicit target in `ManagementService`.
- done: CLI discovery `list` path uses management command path only (no direct fallback route).
- done: CLI targeted submissions are funneled through canonical helper, including topology deploy batch and descriptor queue immediate-send path.
- done: `ManagementFrontendAdapter` lifecycle + facade methods are in place for paired peers, descriptor bundle, telemetry-now, push control, topology control, and OTA control.
- done: `ManagementFrontendAdapter` exposes auto-pull wrappers using existing live-monitor commands (`autoPullControl`, `autoPullStatusGet`).
- done: CLI exposes `handleLineEx` machine-readable dispatch outcomes for parity automation.
- done: frontend adapter exposes command trait introspection (`commandTraitsGet`) using existing command metadata/behavior.
- done: command access-level introspection is centralized through `ManagementService::commandRequiredAccessLevel` and reused by adapter traits.
- done: `handleLineEx` now reports `BadPayload/validation` for non-local handled commands when no submit/queue snapshot is produced.
- done: management-unavailable command branches now report deterministic dispatch status (`DeniedByPolicy/availability`) instead of implicit fallback.
- done: target-missing pre-submit branches now report deterministic dispatch status (`BadPayload/target`) instead of implicit fallback.
- done: non-submit local command paths in storage/OTA/logger now publish explicit dispatch snapshots (`Ok` or deterministic failure), tightening CLI/API parity for frontend automation.
- done: frontend adapter command submission now enforces queue transport binding only (direct-service command fallback removed).
- done: `ManagementController` submission path is now queue-transport-only; direct `ManagementService` submit backend path removed from controller core.
- done: frontend adapter command wait loops removed service-tick fallback for command progression (runtime self-pump only; otherwise external pump).
- done: topology/OTA facade methods reject wrong command-family IDs with deterministic validation error output.
- done: CLI command submission now enforces queue transport binding only across command handlers and OTA/frontend hooks (direct-service submit fallback removed).
- done: CLI short `time/desc/telem/live/ping/audio` command paths now require management queue path and no longer fall back to descriptor pull text path.
- done: `radio.drytest` blocked-command validation now runs on queue/runtime path only (no direct-service submit path).
- done: OTA archive command path now enforces role-scoped manifest loading and strict metadata `target_role` handling (no inferred fallback role in sidecar write/verify paths).
- done: OTA descriptor adapter archive flow now enforces canonical manifest/metadata parsing (required fields only, canonical keys only; no legacy alias/default fallback).
- done: OTA image manifest flow now uses canonical manifest filename only (legacy `m_<fileid>.m` read/cleanup path removed).
- done: master CLI staged-OTA sidecar parser now accepts canonical metadata keys only (`sw_version`, `build_id`, `target_role`) and rejects alias/default fallback fields.
- done: OTA staged-archive save path now requires canonical sidecar filename (`<image>.json`) with no `.jsn` fallback.
- done: CLI OTA archive staged metadata parser (`otaArchiveReadStageMeta`) now accepts canonical metadata keys only (`sw_version`, `build_id`, `target_role`) and rejects alias/default fallback fields.
- done: management-service sidecar + archive-verify metadata parsing now requires `build_id` (no default/fallback value inference).
- done: pull-response decode path is now active-codec only (legacy/default decode API removed from `MasterPullClient`).
- done: slave pull-response emission now uses manager codec path only (legacy descriptor encode fallback removed).
- done: pairing persistence now runs on canonical key-space only (legacy key fallback/migration paths removed).
- done: `ManagementFrontendAdapter` pump path is now runtime-only (`service_->tick` fallback removed from adapter tick/quiesce loops).
- done: descriptor settings handling now enforces strict selector semantics (`setting_id` path stays ID-only; `key` path stays key-only; cross-selector fallback removed).
- done: `IDescriptorProvider` now requires explicit ID-based setting accessors; default key-bridge implementation has been removed.
- done: all active providers (`PMS` runtime + app-owned `ICM/PMS/RELAY/SENS/SEMU/REMU`) now implement `getSettingById(...)` and `setSettingById(...)`.
- done: core `PmsDescriptorProvider` now emits canonical setting IDs and uses direct ID/key maps for single-setting reads and writes.
- done: `ProfileRegistry` now enforces strict per-profile field contract validation (non-zero IDs, non-empty keys, and unique id/key mappings per telemetry/settings/events category).
- done: `ProfileRegistry` now maintains deterministic O(1) profile lookup maps (`profile_id`, `profile_name`) and exposes monotonic `generation()` for frontend cache invalidation.
- done: `ProfileRegistry` now provides immutable `snapshot()` indexes and strict O(1) profile-id resolvers for telemetry/settings/events by `(profile_id, id)` and `(profile_id, key)`.
- done: profile pointer helper resolvers now route through canonical profile-id snapshot resolvers; separate pointer-cache resolver path removed.
- done: CLI remote-profile session state now tracks canonical `ProfileId` and uses ID-based routing for `SEMU/REMU` child push + `telem.now.child` flows (no string-guess branching).
- done: CLI runtime profile command paths now use `remote_profile_id` only (name-cache fallback removed from execution handlers).
- done: descriptor schema cache now keys by canonical `ProfileId` with registry-generation invalidation (pointer-keyed cache path removed).
- done: capabilities payload now includes canonical `profile_id` metadata and comm-test profile resolution is ID-based (`findByName` runtime path removed).
- done: app-owned slave profile telemetry paths (`PMS/RELAY/SENS/SEMU/REMU`) now run runtime-only snapshot emission; synthetic telemetry fallback branches and config toggles were removed.
- done: frontend adapter child push/pull helpers are now explicit-peer APIs and no longer use one global child-stream cache; adapter state is per-peer and submissions run through canonical `pushControl(...)` targeted path.
- done: CLI `push.child.*` state cache is now peer-scoped (`<paired_index|paired_mac>` selector) instead of one global SEMU/REMU mask state, eliminating cross-peer child-push state bleed.
- done: profile source selection fallback modes were removed from runtime behavior; manager start now requires explicit local profile binding and does not auto-load built-in profiles.
- done: `ProfileRegistry::registerProfile(...)` now rejects duplicate profile-id/name conflicts (except exact same profile pointer re-register), eliminating silent registry override/fallback.
- done: CLI persistent `active` target override path was removed; peer-bound CLI operations now use explicit per-command selector prefix (`<paired_index|paired_mac> <command>`) and no hidden target state.
- done: CLI no-op `active` compatibility command hook was removed; legacy persistent-target command text path is now unsupported.
- done: manager runtime no longer depends on active-peer persistence writes/reads for paired-state sync/restore logic; reset cleanup now uses canonical pair-index enumeration instead of active-peer key fallback.
- done: `ManagementQueueTransport` overflow behavior is now explicit per queue (`RejectNew`/`DropOldest`) and no longer uses global fallback mode.
- done: CLI paged descriptor flows now fail fast on non-paged responses (compatibility fallback to plain descriptor rendering removed).
- pending: phase-1 parity/validation pass across full CLI/API matrix (pairing, descriptors/settings, telemetry push, topology, OTA, logger/storage, diagnostics).
