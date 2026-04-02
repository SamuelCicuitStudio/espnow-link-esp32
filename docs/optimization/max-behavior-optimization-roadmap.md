# ESPNow-Link Unified Optimization Roadmap (Behavior-Preserving)

Date: 2026-04-02  
Status: Single source of truth for optimization on both Master and Slave roles.

## 0) Current State (Completed Structural Work)

Completed splits (no behavior change):

- CLI split completed.
  - `src/cli/cli_dispatch.cpp` is now a thin facade.
  - Active logic is in `src/cli/dispatch/*.cpp`, `src/cli/master/*.cpp`, `src/cli/render/*.cpp`.
- Manager split completed.
  - `src/core/manager.cpp` is now a thin facade.
  - Active logic is in `src/core/manager/*.cpp`.
  - Shared internals are in `src/core/internal/*.hpp`.

Optimization must target split translation units and active runtime/provider files, not legacy monolith facades.

## 1) Scope Lock (Mandatory)

- Keep exactly the same behavior:
  - same wire protocol and frame semantics
  - same `ManagementCommandId` / status / event behavior
  - same CLI/API externally visible behavior
- No new features.
- No feature removal.
- No protocol redesign.
- Optimization only (memory + latency + determinism).

## 2) Deterministic Memory + Work-Execution Policy (Master + Slave)

### A) Allocation policy

- Avoid lazy allocations in hot paths (`onRx`, `tick*`, `send*`, descriptor/control handlers).
- Pre-allocate and reuse scratch buffers where possible.
- Avoid repeated temporary `std::vector` churn in per-frame/per-command flow.
- Any unavoidable dynamic allocation must be bounded, measurable, and role-safe.

### B) Queue and work policy

- Fixed-capacity queues only.
- Explicit deterministic overflow policy (`drop-oldest` or `reject-new`) per queue.
- Fixed per-tick dispatch budgets per queue class.
- Sequential bounded processing is preferred to avoid burst memory spikes.
- Preserve queue order and contract semantics.

### C) Concurrency policy

- No uncontrolled fan-out.
- Keep in-flight work bounded by constants.
- No bypass path around queue budgets.
- No hidden background path that grows caches or worksets without cap.

### D) Settings-cache policy (all slave providers)

- Use one deterministic cache model across all slave providers (PMS/SENS/RELAY/SEMU/REMU).
- Keep cache bounded and definition-driven (no unbounded maps).
- Prefer write-through cache updates after successful `setSetting*` writes.
- Avoid full `getSettings` rebuild for single-key reads (`getSetting`, `getSettingById`).
- Frontend/API path policy:
  - cache-first by default for settings reads to keep UI/API latency low
  - network pull/descriptor refresh only on explicit demand path
  - demand-refresh failure may fall back to existing cache when available, with explicit stale metadata
  - no hidden/implicit refresh from cache-only read APIs
- Keep cache invalidation explicit and deterministic:
  - full invalidation only on provider reset/rebind/profile reset
  - targeted invalidation for dependent/derived fields only when needed
- Keep feature behavior unchanged:
  - same value formatting
  - same default fallback behavior
  - same descriptor payload content/order semantics

## 3) Codebase Hotspots from Current Analysis

### A) Shared transport/data movement hotspots (affect Master and Slave)

- `include/espnow_link/management_queue_transport.hpp`
  - `requests_`, `responses_`, `events_` enqueue/poll currently copy objects.
- `src/management/management_service.cpp`
  - front-pop request handling still copies in hot path (`const PendingRequest p = request_queue_.front();`).
  - response/event queueing still pushes by copy.
- `src/core/rx_dispatch.cpp`
  - repeated payload vector copies (`assign(payload, payload + payload_len)`) on RX enqueue paths.
- `src/core/manager/manager_tx.cpp` and `src/pairing/pairing.cpp`
  - repeated temp vectors (`wrapped_payload`, `bytes`) on send paths.

### B) Master-focused hotspots

- `include/espnow_link/management_frontend_adapter.hpp`
  - linear status lookup and event-ring front erase behavior under pressure.
- `src/management/management_service.cpp` (~6k lines)
  - large control surface, high copy/branch density in request/response/event path.
- `src/descriptor/descriptor.cpp`
  - alignment path and payload copy/build overhead.

### C) Slave-focused hotspots

- `src/runtime/slave_runtime_defaults.cpp`
  - `GetNodeBundle` paging loop encodes + sends, then decodes the same encoded bytes just to inspect paging progression (`next_cursor/done`).
  - repeated temporary encoded vectors in descriptor/control response paths.
- `src/core/manager/manager_telemetry_push.cpp`
  - `tickTelemetryPush` rebuilds snapshot/alignment/index every cycle.
  - key validation paths (`isKnownTelemetryKey`, `telemetryKeyFromIndex`) can re-fetch schema/snapshot vectors when profile metadata is not already resolved.
- Slave descriptor providers:
  - `src/runtime/pms_descriptor_provider.cpp`
  - `profile_catalog/src/slaves/{pms,sens,relay,semu,remu}/*_profile.cpp`
  - repeated NVS reads and repeated full settings-vector reconstruction in `getSettings/getSetting` hot usage patterns.
  - `getSetting*` in profile providers currently routes through full `getSettings(...)` rebuild:
    - `profile_catalog/src/slaves/pms/pms_profile.cpp`
    - `profile_catalog/src/slaves/sens/sens_profile.cpp`
    - `profile_catalog/src/slaves/relay/relay_profile.cpp`
    - `profile_catalog/src/slaves/semu/semu_profile.cpp`
    - `profile_catalog/src/slaves/remu/remu_profile.cpp`
  - Existing static caches in SEMU/REMU are schema spec caches (`ProfileSettingSpec`/`ProfileTelemetryMetricSpec`), not runtime setting-value caches.
- `include/espnow_link/management_frontend_adapter.hpp`
  - settings API surface must stay cache-first for fast frontend response.
  - refresh/pull path must remain explicit demand only (no implicit fetch in cache-only APIs).
- `src/platform/persistence.cpp` + `include/espnow_link/persistence.hpp`
  - `blob_cache_` remains unbounded and needs deterministic cap/eviction.

### D) Legacy compatibility branches (guarded cleanup later)

- `include/espnow_link/cli_master.hpp`
- `src/cli/master/cli_master_lifecycle.cpp`
- `src/cli/dispatch/cli_dispatch_fetch_mailbox_tick.cpp`
- `include/espnow_link/management_utils.hpp`

## 4) Important Memory Constraint

Do not replace queue payload vectors with fixed per-entry max-payload arrays blindly.

Reason:

- `ProtocolCodec::kMaxPayload` is 235 bytes.
- Queue caps x max inline reservation can create unnecessary static pressure.
- Preferred strategy: bounded shared scratch/payload arena or reuse pools with measured high-water marks.

## 5) Metrics Plan (Updated for Master + Slave)

Capture baseline before and after each ticket.

### Keep existing metrics

- `ManagerRuntimeMetrics`
- `ManagementRuntime::Stats`
- `ManagementQueueTransport::QueueStats`

### Add deterministic role-aware metrics

- Per-queue high-water depth and overflow reason counters.
- Per-queue budget-hit and budget-overrun counters.
- RX payload storage high-water and TX scratch high-water.
- Copy-pressure counters:
  - bytes copied on RX enqueue
  - bytes copied in management request/response/event transport
  - bytes copied in TX wrap/encode path
- Latency percentiles (p50/p95/p99) by role:
  - Master command response latency by command class
  - Slave pull-request service latency by command class
- Slave telemetry push metrics:
  - `tickTelemetryPush` duration
  - metrics scanned vs metrics sent
  - schema/snapshot fetch count from provider
- Provider setting path metrics:
  - NVS read/write count per request class (`getSettings`, `getSetting`, `setSetting`)
  - settings cache hit/miss ratio
  - single-key read path latency (`getSetting`, `getSettingById`)
  - full-scan rebuild count (`getSettings` full materialization count)
  - write-through cache refresh count and refresh-failure count
  - targeted invalidate count vs full invalidate count
  - per-provider cache memory footprint (bytes)
- Frontend settings API metrics:
  - cache-only read latency (`cachedSettingsResolved`, `cachedSettingResolved`)
  - demand-refresh latency (`refresh*` + read)
  - explicit refresh invocation count
  - cache-fallback-on-refresh-fail count
- Platform memory floor:
  - min free heap
  - min largest free block

## 6) Validation Suite (Role-Balanced)

1. Master burst: 200 mixed commands (`desc/caps/settings/telem/time/get/set`).
2. Slave burst: 200 mixed pull requests against paired slave under queue pressure.
3. Queue-near-capacity pressure on request/response/event lanes (deterministic overflow only).
4. Descriptor `GetNodeBundle` paged sequence (multi-chunk) with cursor progression checks.
5. Topology stage/commit/status sequence.
6. OTA manifest/list/verify + begin/chunk/end/finalize sequence.
7. Telemetry push stress: periodic + hybrid metrics (long run, high metric count).
8. CLI + frontend coexistence against same peer (deterministic routing preserved).
9. 30+ minute soak (pull/push/telemetry/OTA mixed).
10. Determinism checks: no unbounded queue/cache/scratch growth and budgets remain respected.
11. Settings-cache parity loop per slave profile:
    - repeated `getSetting/getSettingById/getSettings` under mixed writes
    - output parity against baseline formatting/order/default fallback
    - verify no stale values after `setSetting*` success
12. SEMU/REMU child-settings stress:
    - high-frequency scoped child setting reads/writes
    - confirm bounded memory and no key-string churn regressions in steady state
13. Frontend settings API contract checks:
    - cache-only calls never trigger network pull
    - demand-refresh calls trigger refresh exactly once per call path
    - on refresh failure, cached data is returned when available with stale metadata
    - response payload/shape remains unchanged

## 7) Ticket Backlog (Priority Order)

### P0 (Immediate)

#### R-01: Move semantics in queue transport + management hot path

Targets:

- `include/espnow_link/management_queue_transport.hpp`
- `include/espnow_link/management_controller.hpp`
- `src/management/management_controller.cpp`
- `src/management/management_service.cpp`
- `include/espnow_link/management_service.hpp`

Goal: remove avoidable request/response/event payload copies.

Status update (2026-04-02):

- Done: queue transport dequeue paths now move (`pollRequest/pollResponse/pollEvent`).
- Done: controller submit path now moves payload and request into transport queue.
- Done: service submit/tick request path now moves request envelopes through queueing/dequeue.
- Done: runtime now submits requests to service via move.
- Done: service response/event queue helpers now accept by-value and move into deques.
- Done: `ManagementService::onPullResponse` no longer duplicates raw pull payload into two vectors; decode and queue now share a single payload buffer path.
- Done: descriptor re-encode response path in `ManagementService::onPullResponse` now reuses one service-owned string scratch buffer (`pull_descriptor_payload_scratch_`) to reduce repeated allocation churn.
- Done: high-frequency `ManagementService::onEvent` payload builders now pre-reserve vector capacity for discovery/pairing/mandatory-event/ota-status/error event envelopes to reduce small-buffer growth churn.

#### R-02: TX scratch reuse in manager + pairing

Targets:

- `src/core/manager/manager_tx.cpp`
- `src/pairing/pairing.cpp`
- `include/espnow_link/manager.hpp`
- `include/espnow_link/pairing.hpp`

Goal: reduce alloc/free churn in send paths.

Status update (2026-04-02):

- Done: `EspNowManager::sendTyped` now reuses manager-owned TX scratch buffers for wrapped payload and encoded frame bytes.
- Done: manager constructor now pre-reserves TX scratch capacities (`kMaxPayload` / `kMaxFrameBytes`).
- Done: `PairingEngine::sendControl` now reuses pairing-owned TX scratch buffers for wrapped payload and encoded frame bytes.
- Done: pairing constructor now pre-reserves TX scratch capacities.

#### S-01: Slave `GetNodeBundle` paging fast-path cleanup

Target:

- `src/runtime/slave_runtime_defaults.cpp`

Goal: remove encode-then-decode loop for paging progression, keep same paging behavior and responses.

Status update (2026-04-02):

- Done: removed encoded-response re-decode in `GetNodeBundle` paging loop; progression now uses already-built `DescriptorResponse`.
- Done: paging loop now reuses one encoded buffer (`encoded.clear()` per chunk) instead of re-allocating each iteration.

#### S-02: Slave telemetry key/schema resolution caching

Targets:

- `src/core/manager/manager_telemetry_push.cpp`
- `include/espnow_link/manager.hpp`

Goal: avoid repeated provider schema/snapshot fetches in key/index resolution and lower tick cost.

Status update (2026-04-02):

- Done: telemetry metric identity validation in push start/update now preloads provider schema/snapshot once per command path and reuses it.
- Done: restore-path metric key validation now preloads provider schema/snapshot once per restore call and reuses it across all metrics.
- Done: `tickTelemetryPush` now short-circuits before snapshot fetch/alignment/index rebuild when no metric is due for periodic or change-mode evaluation.

#### S-03: Unified slave settings cache + frontend API cache-first contract

Targets:

- `src/runtime/pms_descriptor_provider.cpp`
- `include/espnow_link/pms_descriptor_provider.hpp`
- `profile_catalog/src/slaves/pms/pms_profile.cpp`
- `profile_catalog/include/profile_catalog/slaves/pms/pms_profile.hpp`
- `profile_catalog/src/slaves/sens/sens_profile.cpp`
- `profile_catalog/include/profile_catalog/slaves/sens/sens_profile.hpp`
- `profile_catalog/src/slaves/relay/relay_profile.cpp`
- `profile_catalog/include/profile_catalog/slaves/relay/relay_profile.hpp`
- `profile_catalog/src/slaves/semu/semu_profile.cpp`
- `profile_catalog/include/profile_catalog/slaves/semu/semu_profile.hpp`
- `profile_catalog/src/slaves/remu/remu_profile.cpp`
- `profile_catalog/include/profile_catalog/slaves/remu/remu_profile.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`

Goal: keep behavior identical while reducing repeated NVS reads/full rebuilds and making settings API path cache-first unless refresh is explicitly demanded.

Status update (2026-04-02):

- Done: provider-side settings cache added for `PMS/SENS/RELAY/SEMU/REMU` plus runtime PMS provider.
- Done: `getSetting`/`getSettingById` now resolve from provider cache instead of rebuilding full settings list per query.
- Done: frontend API `settingsGetResolved(...)` moved to cache-first fast path with fallback targeted refresh.
- Done: `settingsRefresh(...)` hot path no longer resolves settings twice when fresh cache sequence is detected.
- Done: successful `setSetting*` writes now refresh provider cache immediately (write-through refresh) so next frontend reads remain cache-fast without an extra rebuild roundtrip.

Implementation direction (no feature change):

- Add per-provider fixed-bounded settings-value cache, indexed by `setting_id` and key.
- Ensure `getSetting` / `getSettingById` resolve directly from cache/index without rebuilding full settings list.
- Keep `getSettings` deterministic by reading from cache materialization (not re-reading NVS for every field).
- Define and enforce API-path contract:
  - cache-only API helpers must be network-silent
  - explicit refresh helpers perform targeted pull, then serve cache
  - no implicit refresh side-effects on read-only cache APIs
- Keep write-through behavior:
  - after successful `setSetting*`, update cache entry immediately
  - do not require full-cache rebuild on every write
- Keep explicit invalidation hooks for reset/rebind paths.
- Preserve formatting and descriptor compatibility exactly.

### P1 (High value)

#### R-03: Bounded payload reuse for RX queues

Targets:

- `src/core/rx_dispatch.cpp`
- `src/core/manager/manager_tick_and_queues.cpp`
- `include/espnow_link/manager.hpp`

Goal: reduce fragmentation and stabilize memory ceiling in both roles.

Status update (2026-04-02):

- Done: RX enqueue paths for `control/pull_request/pull_response/firmware` now reuse dropped-oldest queue slots when capacity is reached, preserving payload vector capacity instead of reallocating fresh frame payload buffers.
- Done: RX dequeue paths now recycle one processed frame slot per queue type (`control/pull_request/pull_response/firmware`) for deterministic payload-buffer reuse even before queues are saturated.
- Done: enqueue payload fill now uses `clear + reserve-if-needed + insert` on reused frame payload vectors to avoid avoidable allocation churn while preserving exact queue order/overflow semantics.

#### M-01: Frontend adapter lookup/ring tuning

Target:

- `include/espnow_link/management_frontend_adapter.hpp`

Goal: reduce linear scan and front-erase cost at high event/response rates.

Status update (2026-04-02):

- Done: command-status lookup now uses an adapter-local `req_id -> index` map with validation fallback, reducing repeated linear scans in response/event tracking hot paths.
- Done: req-status trim path now rebuilds the lookup map after evictions to keep indexing deterministic and coherent.
- Done: event ring storage switched to `std::deque`, and trimming now uses bounded `pop_front()` instead of vector front-range erase shifts.

#### D-01: Descriptor alignment + encode copy reduction

Target:

- `src/descriptor/descriptor.cpp`

Goal: lower CPU and transient allocations in descriptor response construction.

Status update (2026-04-02):

- Done: paged descriptor paths now slice vectors in place (`capabilities/telemetry/settings/node-bundle-settings/ota-manifest`) and then move into response payload fields, removing extra page-copy vectors.
- Done: telemetry pull paging no longer duplicates full sample vectors before paging; snapshot hash is computed once, then the same vector is trimmed in place.
- Done: descriptor response encoder now pre-reserves `kMaxPayload` buffer capacity to avoid repeated growth allocations during TLV append.

### P2 (After core wins)

#### R-04: Persistence cache bounding

Targets:

- `src/platform/persistence.cpp`
- `include/espnow_link/persistence.hpp`

Goal: deterministic cache growth limits for long uptime.

Status update (2026-04-02):

- Done: `PairingStore` blob cache now includes deterministic bounded capacity (`kBlobCacheMaxEntries`) with LRU-style eviction based on per-entry last-used sequence.
- Done: cache entries are now marked on read/write/erase paths, and limit enforcement runs after cache insert/update paths while preserving the actively touched key.
- Done: `getBlobCached` miss path now avoids one extra blob copy by moving backend-loaded bytes into cache first, then copying once to output.

#### R-05: OTA buffer reuse/parse tightening

Targets:

- `src/ota/ota_manager.cpp`
- `src/ota/ota_descriptor_adapter.cpp`

Goal: reduce OTA heap churn while keeping OTA behavior/contract unchanged.

Status update (2026-04-02):

- Done: added reusable OTA I/O scratch buffers in both `OtaManager` and `OtaDescriptorAdapter` (`io_scratch_`) with deterministic resize/reuse helpers.
- Done: status-record and manifest-file reads now reuse member scratch buffers instead of allocating fresh vectors per read.
- Done: OTA file CRC computation now reuses adapter scratch buffer for chunked reads instead of constructing a temporary buffer each call.

#### R-06: Remaining oversized unit decomposition

Targets:

- `src/management/management_service.cpp`
- `include/espnow_link/management_frontend_adapter.hpp`

Goal: keep optimization work maintainable and auditable.

Status update (2026-04-02):

- Done: split inbound event handling out of `management_service.cpp` into `src/management/management_service_events.cpp` (`onEvent`, `onPullRequest`) to reduce unit size while preserving behavior/contracts.
- Done in this split slice: moved event/pull-request logic without changing command/status/event payload semantics or routing behavior.
- Done in this split slice: moved pull-response handling into `src/management/management_service_pull_response.cpp` (`onPullResponse` + local helper logic), keeping correlation/routing/paging behavior unchanged.
- Done in this split slice: moved queue/status/topology-sync/live-monitor runtime state and payload-building methods into `src/management/management_service_runtime_state.cpp` (method-body parity preserved).
- Done in this split slice: moved critical-command policy/deferred lifecycle helpers into `src/management/management_service_critical.cpp` (`runMasterCritical/runSlaveCritical/register/consume/statusFromPolicy/makeDeviceContext`) with unchanged behavior.
- Done in this split slice: moved command metadata functions (`commandPriority/isAsyncTerminalCommand/commandTimeoutMs`) into the same critical unit to keep policy logic localized.
- Done in this split slice: moved OTA local push/update pump-stop state machine methods into `src/management/management_service_ota_pump.cpp` to isolate high-churn loop logic without changing OTA command behavior.
- Done in this split slice: moved descriptor tracking/deferred topology commit pruning methods into `src/management/management_service_descriptor_tracking.cpp`.
- Done in this split slice: moved descriptor/OTA/push command handlers (`runDescriptorPull`, `runPushCommand`, `runOtaTransferCommand`, `startOtaPushLocalSession`, `runOtaPushLocalCommand`, `runOtaArchiveCommand`, `runOtaUpdateLocalCommand`) into `src/management/management_service_commands.cpp`.
- Done in this split slice: moved `executeRequest(...)` into the command unit; `management_service.cpp` now focuses on constructor/begin-submit-tick queue lifecycle and radio transition flow.
- Done in this split slice: decomposed `management_frontend_adapter.hpp` into a thin facade header plus internal fragments:
  - `include/espnow_link/internal/management_frontend_adapter_public.inl`
  - `include/espnow_link/internal/management_frontend_adapter_private.inl`
  preserving class API/inline behavior while reducing top-level header size.

### P3 (Only after compatibility verification)

#### R-07: Guarded legacy compatibility prune

Targets:

- `include/espnow_link/cli_master.hpp`
- `src/cli/master/cli_master_lifecycle.cpp`
- `src/cli/dispatch/cli_dispatch_fetch_mailbox_tick.cpp`
- `include/espnow_link/management_utils.hpp`

Goal: remove dead/legacy branches with zero behavior drift.

## 8) Execution Order

1. R-01
2. S-01
3. S-02
4. S-03
5. R-02
6. Baseline compare checkpoint
7. R-03
8. M-01 + D-01
9. R-04 + R-05
10. R-06
11. R-07

## 9) Definition of Done (Per Ticket)

All must be true:

- behavior parity tests pass (master and slave)
- no command/status/event contract drift
- measured latency and/or memory is improved (or explicitly neutral with simplification gain)
- bounded memory and bounded budget metrics stay within policy
- rollback remains simple (single-ticket revert)

## 10) Single-File Rule

This file is the only optimization planning source in `docs/optimization`.
Any new optimization planning update must be applied here instead of creating separate plan markdown files.
