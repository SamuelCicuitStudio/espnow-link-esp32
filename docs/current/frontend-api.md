# Frontend API

Frontend control is split into two layers:

- `ManagementController`: typed command submit API
- `ManagementFrontendAdapter`: orchestration, cache, and operation tracking

## ManagementController

`ManagementController` supports:

- direct binding to `ManagementService`
- queue binding to `ManagementQueueTransport`
- explicit source/access/timeout/request-id configuration
- explicit per-submit target peer via `SubmitOptions.has_target_peer` + `SubmitOptions.target_peer`

Command coverage includes discovery, pairing, descriptor/settings/telemetry/time/liveness, topology, lifecycle, logger/storage, OTA, metrics/queue.

## ManagementFrontendAdapter

Adapter capabilities on top of controller:

- submit/wait helpers:
  - `submit`
  - `commandRunAndWait`
  - `operationSubmit`
  - `operationStatus`
  - `operationWait`
- cache ingestion:
  - `pollResponseCached`
  - `pollEventCached`
  - `drainToCache`
  - `ingestResponse`
  - `ingestEvent`
- cached views:
  - `cachedPairedPeers`
  - `cachedNode` / `cachedNodes`
  - `cachedSettingResolved` / `cachedSettingsResolved`
- settings helpers:
  - `settingsGetResolved`
  - `settingsSetBatch`
- explicit-target helpers:
  - `audioPingRequest(peer, ...)`
  - `restartTargetRequest(peer, ...)`
  - `resetTargetRequest(peer, ...)`
  - `pmsChain48vSet(peer, ...)`
  - `pmsChargerSet(peer, ...)`
  - `relayOutputSet(peer, ...)`
  - `lidarProvision*` explicit-peer variants
- event ring snapshots:
  - `eventsSnapshot`

## Settings Cache Policy (Current + Migration Target)

Current baseline:

- adapter cache is readable via `cachedSettingResolved` / `cachedSettingsResolved`
- refresh helpers (`settingsBundleRefresh`, `settingsBundleGet`) can hydrate cache from transport
- `settingsGetResolved` is cache-first and only triggers a targeted refresh when full cache is not ready

Migration target (library-first policy):

1. cache-first read path for settings overlay/dialog open
2. explicit refresh path for user-forced update only
3. per-peer cache metadata surfaced to caller:
   - `cache_hit`
   - `completeness`
   - `cache_age_ms`
   - `refresh_performed`
   - `refresh_status`
4. startup reconcile hydration for already-paired peers
5. write path supports changed-only/delta updates with unchanged-key skip

Implementation planning reference:

- `../optimization/slave-settings-cache-policy-and-delta-write-fix-plan.md`

## Radio Transition Helpers

Adapter wrappers over service lifecycle:

- `beginRadioTransition`
- `endRadioTransition`
- `hardDeinitForRadioTransition`
- `hardReinitAfterRadioTransition`
- `radioTransitionStatusGet`

During transition, mutating commands are blocked and stale pre-transition traffic is dropped by epoch handling.

## Safety Guards

Compile-time guards:

- `ESPNOW_LINK_FRONTEND_ADAPTER_STRICT_RAW_POLL`
- `ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_CHECK`
- `ESPNOW_LINK_FRONTEND_ADAPTER_DEBUG_OWNER_ASSERT`

Owner-check mode enforces single-owner context usage per adapter instance.

## Coexistence Contract (API + CLI)

To avoid CLI/API conflicts:

- keep each frontend on its own `ManagementSource`
- use explicit peer target for peer-bound mutations
- use operation or request-id tracking until terminal state
- perform setting readback after write when strict convergence is needed

## Code Anchors

- `include/espnow_link/management_controller.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`
- `include/espnow_link/management_runtime.hpp`
- `include/espnow_link/management_queue_transport.hpp`

## Related

- `../optimization/cli-api-control-optimization-structure.md` (shared optimization structure with CLI)
