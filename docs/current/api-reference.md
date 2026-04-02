# Release API Reference

This document details the release API surfaces, what each API expects, and what it returns.

Scope:

1. `ManagementController` (typed command submit API)
2. `ManagementFrontendAdapter` (orchestration/cache API)

Reference headers:

1. `include/espnow_link/management_controller.hpp`
2. `include/espnow_link/management_frontend_adapter.hpp`
3. `include/espnow_link/management_types.hpp`
4. `include/espnow_link/descriptor.hpp`

## 1) Global API Rules

1. Queue transport is required for command submission.
2. On master, peer-bound calls must use explicit target peer context.
3. `bool` return from controller wrappers means queue-accept only, not terminal success.
4. Terminal success/failure/timeout comes from response/event lifecycle tracking.
5. `timeout_ms = 0` means "use configured defaults."

## 2) Shared Types and What They Expect

## 2.1 `ManagementController::SubmitOptions`

| Field | Expects | Details |
|---|---|---|
| `req_id` | `0` for auto or explicit non-zero | Correlation id per source. |
| `timeout_ms` | `0` or custom timeout | `0` uses controller default timeout. |
| `priority` | optional | Reserved for future wire contract; currently ignored by controller submit. |
| `has_target_peer` | `true/false` | Set `true` for explicit peer-bound command targeting. |
| `target_peer` | `MacAddress` | Used only when `has_target_peer = true`. |

## 2.2 `ManagementController::SubmitResult`

| Field | Meaning |
|---|---|
| `accepted` | `true` only when request entered queue transport. |
| `cmd_id` | Submitted command id. |
| `req_id` | Request id used for submission. |
| `status` | Submit-stage status (`Ok`, `QueueFull`, `DeniedByPolicy`, etc). |
| `reject_stage` | Submit reject location hint (`bind`, `queue`, etc). |

## 2.3 Adapter lifecycle and cache results

| Type | What It Contains |
|---|---|
| `CommandRunResult` | accepted flag, terminal state, status, optional response/event, structured error. |
| `CommandLifecycleResult` | normalized lifecycle result for `runAndTrack`/domain controls. |
| `OperationHandle` | stable `operation_id`/`req_id` handle for async tracking. |
| `OperationStatus` | normalized operation state (`Queued/Running/Succeeded/Failed/Timeout/Canceled`) plus payload/error context. |
| `SettingsCacheMeta` | cache hit/completeness/age/refresh flags/errors for settings reads. |
| `SettingsBatchOptions` | `confirm`, `stop_on_error`, `refresh_cache`, `timeout_ms`. |
| `SettingsBatchResultItem` | per-key set result (`submitted/applied/confirmed/skipped_unchanged/status/error`). |

## 3) `ManagementController` API (Typed Submit Layer)

## 3.1 Binding and defaults

| API | Expects | Returns | Details |
|---|---|---|---|
| `bind(transport)` | queue transport instance | `void` | Sets source/access from transport. |
| `clearBindings()` | none | `void` | Controller becomes unbound. |
| `ready()` | none | `bool` | `true` when queue transport is bound. |
| `setSource(source)` / `source()` | valid `ManagementSource` | `void` / enum | Sets source metadata for requests. |
| `setAccessLevel(level)` / `accessLevel()` | valid `ManagementAccessLevel` | `void` / enum | Sets request access metadata. |
| `setDefaultTimeoutMs(ms)` / `defaultTimeoutMs()` | timeout value | `void` / `uint32_t` | Default timeout when per-call timeout is `0`. |
| `setNextReqId(id)` / `nextReqId()` | next request id | `void` / `uint32_t` | Controls auto req-id sequence. |
| `submit(cmd_id, payload, options)` | command id + payload + submit options | `SubmitResult` | Canonical submit entrypoint. |

## 3.2 Discovery and pairing

| API | Expects | Returns | Details |
|---|---|---|---|
| `discoveryStart(window_ms, out_req_id, timeout_ms)` | discovery window in ms | `bool` | Queue acceptance only. |
| `discoveryStop(...)` | none | `bool` | Stops discovery. |
| `discoverySnapshotGet(...)` | none | `bool` | Snapshot pull request. |
| `pairedSnapshotGet(...)` | none | `bool` | Paired list snapshot request. |
| `statusGet(...)` | none | `bool` | Runtime status request. |
| `pairRequest(peer, ...)` | peer `MacAddress` | `bool` | Pair request to selected peer. |
| `unpairRequest(...)` | none | `bool` | Unpair lifecycle request. |
| `removePeerRequest(peer, ...)` | peer `MacAddress` | `bool` | Remove persisted paired peer. |

## 3.3 Descriptor, settings, telemetry, liveness, time

| API | Expects | Returns | Details |
|---|---|---|---|
| `descGet(...)` | none | `bool` | Device descriptor request. |
| `capsGet(...)` | none | `bool` | Capabilities request. |
| `capsPageGet(cursor, page_size, ...)` | paging cursor/size | `bool` | Explicit paged capabilities request. |
| `nodeBundleGet(bundle_mask, ...)` | bundle mask bits | `bool` | `GetNodeBundle` request path. |
| `settingsGet(...)` | none | `bool` | Settings descriptor request. |
| `settingsPageGet(cursor, page_size, ...)` | paging cursor/size | `bool` | Explicit paged settings request. |
| `settingGetByKey(key, ...)` | non-empty key | `bool` | Single setting fetch by key. |
| `settingGetById(setting_id, ...)` | setting id | `bool` | Single setting fetch by id. |
| `settingSetByKey(key, value, ...)` | key and encoded value string | `bool` | Single setting set by key. |
| `settingSetById(setting_id, value, ...)` | setting id and value string | `bool` | Single setting set by id. |
| `telemetrySchemaGet(...)` | none | `bool` | Telemetry schema request. |
| `telemetrySchemaPageGet(cursor, page_size, ...)` | paging cursor/size | `bool` | Paged telemetry schema request. |
| `telemetryPull(...)` | none | `bool` | Current telemetry snapshot request. |
| `telemetryPullPageGet(cursor, page_size, ...)` | cursor/page_size | `bool` | Paged telemetry pull request. |
| `livenessGet(...)` | none | `bool` | Liveness request. |
| `liveMonitorEnable(...)` | none | `bool` | Enable live monitor/autopull. |
| `liveMonitorDisable(...)` | none | `bool` | Disable live monitor/autopull. |
| `liveMonitorStatusGet(...)` | none | `bool` | Live monitor status request. |
| `pingGet(...)` | none | `bool` | Ping request. |
| `timeGet(...)` | none | `bool` | Time read request. |
| `timeSet(epoch_s, ...)` | unix epoch seconds | `bool` | Time set request. |

## 3.4 Push and topology/channel/chain

| API | Expects | Returns | Details |
|---|---|---|---|
| `pushCommand(cmd, ...)` | valid `TelemetryPushCommand` | `bool` | Generic push control submit. |
| `pushStart/Update/Pause/Resume/Stop/Get(...)` | none | `bool` | Push lifecycle commands. |
| `topologyStageSet(snapshot, ...)` | valid topology snapshot payload | `bool` | Topology stage request. |
| `topologyCommit(...)` | none | `bool` | Commit staged topology. |
| `topologyStatusGet(...)` | none | `bool` | Topology status request. |
| `topologySlotsGet(committed, ...)` | committed/staged selector | `bool` | Slot table request. |
| `topologyTriggerSend(trigger, ...)` | trigger payload | `bool` | Trigger dispatch request. |
| `channelRuntimeGet(...)` | none | `bool` | Channel runtime status request. |
| `channelSyncAll(channel, ...)` | channel `1..14` expected by service policy | `bool` | Channel sync lifecycle request. |
| `chainLoopSetEnabled/Enable/Disable/StatusGet(...)` | enabled flag/status query | `bool` | Chain-loop policy control and read. |

## 3.5 Critical, logger, storage

| API | Expects | Returns | Details |
|---|---|---|---|
| `restartSlaveRequest/resetSlaveRequest(...)` | none | `bool` | Peer restart/reset control. |
| `restartMasterRequest/resetMasterRequest(...)` | none | `bool` | Local master restart/reset control. |
| `audioPingRequest(...)` | none | `bool` | Audio ping control request. |
| `cliSetEnabled/cliEnable/cliDisable/cliStatusGet(...)` | enabled flag or query | `bool` | CLI runtime control path. |
| `logLocalStatusGet(...)` | none | `bool` | Local logger status request. |
| `logLocalRead(offset, max_bytes, ...)` | byte offset and max bytes | `bool` | Local logger chunk read. |
| `logLocalClear(...)` | none | `bool` | Local logger clear. |
| `logLocalSetEnabled(enabled, ...)` | enabled flag | `bool` | Local logger enable/disable. |
| `logRemoteStatusGet(...)` | none | `bool` | Remote logger status request. |
| `logRemoteRead(offset, max_bytes, ...)` | byte offset and max bytes | `bool` | Remote logger chunk read. |
| `logRemoteClear(...)` | none | `bool` | Remote logger clear. |
| `logRemoteSetEnabled(enabled, ...)` | enabled flag | `bool` | Remote logger enable/disable. |
| `storageInfoGet(...)` | none | `bool` | Storage backend info request. |
| `storageList(path, ...)` | storage path | `bool` | Directory list request. |
| `storageStat(path, ...)` | storage path | `bool` | Path stat request. |
| `storageFormat(...)` | none | `bool` | Storage format request. |

## 3.6 OTA and diagnostics

| API | Expects | Returns | Details |
|---|---|---|---|
| `otaStatusGet(...)` | none | `bool` | OTA status request. |
| `otaManifestGet/PageGet/Rebuild(...)` | optional paging fields | `bool` | OTA manifest operations. |
| `otaClearScope(scope, ...)` | scope string (`in/img/man/all`) | `bool` | OTA scope cleanup request. |
| `otaCapacityGet(...)` | none | `bool` | OTA capacity preflight request. |
| `otaGateGet(...)` | none | `bool` | OTA gate status request. |
| `otaApply(target, ...)` | target image id/name | `bool` | OTA apply request. |
| `otaRollback(...)` | none | `bool` | OTA rollback request. |
| `otaTransferBegin(total, chunk, crc, metadata, ...)` | transfer metadata and CRC | `bool` | Manual OTA transfer begin. |
| `otaTransferChunk(offset, data, ...)` | chunk offset and bytes | `bool` | Manual OTA chunk transfer. |
| `otaTransferEnd(total, crc, ...)` | final size and CRC | `bool` | Manual OTA transfer finalize. |
| `otaTransferAbort(...)` | none | `bool` | Manual OTA transfer abort. |
| `otaPushStart(local_path, chunk_bytes, ...)` | local image path | `bool` | Managed OTA push start. |
| `otaPushAbort(...)` | none | `bool` | Managed OTA push abort. |
| `otaPushStatus(...)` | none | `bool` | Managed OTA push status. |
| `otaUpdateStart(local_path, chunk_bytes, ...)` | local image path | `bool` | Managed OTA update pipeline start. |
| `otaArchiveList/SaveRunning/SaveStaged(...)` | role char (`m`/`s`), optional remote flag | `bool` | OTA archive list/save operations. |
| `otaArchiveRestore/Delete/Clear/Verify(...)` | archive id, role, optional remote flag | `bool` | OTA archive mutation and verify. |
| `otaUpdateMasterStart(local_path, ...)` | local staged image path | `bool` | Local master OTA update start. |
| `commTestRun/Status/Report(...)` | none | `bool` | Comm test operations. |
| `metricsGet/Reset(...)` | none | `bool` | Runtime metrics operations. |
| `queueGet(...)` | none | `bool` | Queue status request. |

## 4) `ManagementFrontendAdapter` API (Orchestration + Cache Layer)

## 4.1 Binding, ownership, and defaults

| API | Expects | Returns | Details |
|---|---|---|---|
| `bind(transport, runtime, service, source, access)` | queue transport and optional runtime/service handles | `void` | Binds adapter and configures controller path. |
| `ready()` | none | `bool` | `true` when queue-backed command path is available. |
| `setSource` / `setAccessLevel` | metadata values | `void` | Applied to underlying controller submit metadata. |
| `setNextReqId`, `setDefaultTimeoutMs` | request id and timeout defaults | `void` | Controls submit defaults. |
| `setOrchestrationWaitDefaultMs` | wait timeout | `void` | Default wait for blocking helpers. |
| `setBatchDefaults(confirm, refresh_cache)` | booleans | `void` | Defaults used by batch settings helper overload. |
| `setAutoPairSettingsBootstrap(enabled, timeout_ms)` | enable + bootstrap timeout (default `4500`) | `void` | Pair-result-triggered settings cache bootstrap policy. |
| `commands()` | none | controller ref | Direct access to typed command API. |

## 4.2 Lifecycle/orchestration APIs

| API | Expects | Returns | Details |
|---|---|---|---|
| `submit(cmd_id, payload, options)` | command id, payload, submit options | `SubmitResult` | Adapter-tracked submit with mutation-lane/radio checks. |
| `commandTraitsGet(cmd, out_traits)` | command enum | `bool` | Returns access/mutating/deferred/timeout traits. |
| `commandRunAndWait(...)` | cmd + payload + submit/timeout | `bool` | Blocks until terminal state or timeout. |
| `runAndTrack(...)` | cmd + payload + run options | `CommandLifecycleResult` | Normalized accepted/terminal/error contract. |
| `commandStatusGet(req_id, out)` | request id | `bool` | Reads tracked command lifecycle state. |
| `operationSubmit(...)` | cmd + payload + submit options | `bool` | Returns `OperationHandle` for async status polling. |
| `operationStatus(operation_id, out)` | operation id or handle | `bool` | Reads normalized operation status. |
| `operationWait(operation_id, out, timeout_ms)` | operation id | `bool` | Waits terminal; true only on succeeded state. |

## 4.3 Frontend read models and domain helpers

| API | Expects | Returns | Details |
|---|---|---|---|
| `pairedSnapshotGetResolved(out_peers, timeout_ms)` | output vector | `bool` | Runs request+wait+decode to `(peer, role_code)` rows. |
| `pairedPeersGet(out_view, refresh, timeout_ms)` | paired view output | `bool` | `refresh=true` submits command, `false` serves cache view. |
| `descriptorBundleGet(peer, mask, out_view, timeout_ms)` | peer + bundle mask | `bool` | Fetches desc/caps/settings/telem schema bundle view. |
| `telemetryNowGet(peer, out_view, options)` | peer + options | `bool` | Pulls telemetry snapshot (paged or single-page mode). |
| `pushControl(peer, push_cmd, timeout_ms, wait_terminal)` | peer + push command | `CommandLifecycleResult` | Maps push action to proper push command id and runs lifecycle. |
| `autoPullControl(enabled, timeout_ms, wait_terminal)` | enable/disable | `CommandLifecycleResult` | Uses live-monitor commands. |
| `autoPullStatusGet(out_view, timeout_ms)` | status view output | `bool` | Reads and decodes live-monitor status payload. |
| `topologyControl(peer, cmd_id, payload, timeout_ms, wait_terminal)` | topology cmd id only | `CommandLifecycleResult` | Rejects non-topology ids (`DeniedByPolicy`). |
| `otaControl(peer, cmd_id, payload, timeout_ms, wait_terminal)` | OTA cmd id only | `CommandLifecycleResult` | Rejects non-OTA ids (`DeniedByPolicy`). |

## 4.4 Explicit-target utility helpers

| API | Expects | Returns | Details |
|---|---|---|---|
| `audioPingRequest(peer, ...)` | peer | `bool` | Targeted audio ping submit. |
| `restartTargetRequest(peer, ...)` | peer | `bool` | Targeted restart submit. |
| `resetTargetRequest(peer, ...)` | peer | `bool` | Targeted reset submit. |
| `pmsChain48vSet(peer, enabled, ...)` | PMS peer and bool | `bool` | Writes `chain_48v_enable` setting. |
| `pmsChargerSet(peer, enabled, ...)` | PMS peer and bool | `bool` | Writes `charger_enable` setting. |
| `relayOutputSet(peer, output_idx, enabled, ...)` | relay index and state | `bool` | Writes relay output setting. |
| `remuOutputSet(peer, vid, enabled, ...)` | child vid and state | `bool` | Writes REMU child output setting. |
| `cliEnable/Disable/StatusGet(...)` | none | `bool` | Wrapper to CLI control commands. |
| `chainLoopEnable/Disable/Set/StatusGet(...)` | enable flag or query | `bool` | Chain-loop control wrappers. |

## 4.5 Settings cache APIs (high priority for UI/API flows)

| API | Expects | Returns | Details |
|---|---|---|---|
| `nodeBundleGet(peer, bundle_mask, out_run, timeout_ms)` | explicit peer + mask | `bool` | Runs one bundle pull and ingests into cache. |
| `settingsReadCached(peer, out_settings, out_meta)` | explicit peer | `bool` | Cache-only read; no transport pull. |
| `settingsReadCachedForUi(peer, out_settings, out_meta)` | explicit peer | `bool` | Cache-only read requiring full/ready cache. |
| `settingsBundleRefresh(peer, out_req_id, timeout_ms)` | explicit peer | `bool` | Async targeted settings refresh submit. |
| `settingsBundleGet(peer, out_settings, timeout_ms, out_run)` | explicit peer | `bool` | Blocking bundle refresh + resolved settings read. |
| `settingsRefresh(peer, out_settings, timeout_ms, out_run, out_meta)` | explicit peer | `bool` | Explicit forced refresh path; metadata includes refresh status/errors. |
| `settingsAfterWriteSync(peer, out_settings, timeout_ms, out_run, out_meta)` | explicit peer | `bool` | Post-write authoritative refresh helper. |
| `settingsCacheRefreshPeer(peer, out_req_id, timeout_ms)` | explicit peer | `bool` | Non-blocking targeted refresh submit. |
| `settingsCacheRefreshPeerKey(peer, key, out_req_id, timeout_ms)` | explicit peer and key | `bool` | Targeted setting key refresh submit. |
| `settingsCacheRefreshPeerId(peer, setting_id, out_req_id, timeout_ms)` | explicit peer and setting id | `bool` | Targeted setting id refresh submit. |
| `settingsGetResolved(peer, out_settings, timeout_ms, out_run)` | explicit peer | `bool` | Cache-first resolved settings read; falls back to one targeted refresh when full cache is unavailable. |
| `settingsSetBatch(peer, items, out_results, options)` | explicit peer + key/value list | `bool` | Delta-aware writes; unchanged keys are skipped and reported. |
| `cachedSettingResolved(peer, key, out)` | cached peer + key | `bool` | Returns resolved value (`current` else `default`). |
| `cachedSettingsResolved(peer, out)` | cached peer | `bool` | Returns all resolved settings for peer. |

Notes for settings APIs:

1. Read path should be cache-first.
2. Pull path should be explicit refresh only.
3. Deferred/paged `OkDeferred` responses are merged into cache.
4. Batch write can confirm via readback and optional refresh-cache step.

## 4.6 Node snapshot and child stream helpers

| API | Expects | Returns | Details |
|---|---|---|---|
| `nodeSnapshotGet(peer, out_snapshot, timeout_ms, include_liveness, include_time, include_telemetry)` | explicit peer + include flags | `bool` | Builds one snapshot view from `NodeBundleGet` and cache. |
| `semuChildPushStart(peer, vid, mode, interval_ms, delta_abs, gap_ms, out)` | SEMU peer + child vid | `bool` | Child-targeted push build/submit helper. |
| `remuChildPushStart(peer, vid, mode, interval_ms, delta_abs, gap_ms, out)` | REMU peer + child vid | `bool` | Child-targeted push build/submit helper. |
| `semuChildPushStop(peer, vid, out)` | SEMU peer + child vid | `bool` | Removes SEMU child from push set. |
| `remuChildPushStop(peer, vid, out)` | REMU peer + child vid | `bool` | Removes REMU child from push set. |
| `semuChildTelemetryPull(peer, vid, out_view, options)` | SEMU peer + child vid | `bool` | Child-filtered telemetry helper. |
| `remuChildTelemetryPull(peer, vid, out_view, options)` | REMU peer + child vid | `bool` | Child-filtered telemetry helper. |

## 4.7 OTA/archive/storage wrappers

| API | Expects | Returns | Details |
|---|---|---|---|
| `otaArchiveList/SaveRunning/SaveStaged(role, ...)` | role (`m`/`s`) | `bool` | Archive management wrappers. |
| `otaArchiveRestore/Delete/Clear/Verify(...)` | archive id + role | `bool` | Archive mutation/verify wrappers. |
| `otaUpdateMasterStart(local_path, ...)` | staged image path | `bool` | Local master OTA update wrapper. |
| `storageInfoGet/List/Stat/Format(...)` | storage args where required | `bool` | Storage command wrappers. |

## 4.8 Polling, ingest, cache, metrics

| API | Expects | Returns | Details |
|---|---|---|---|
| `pollResponse` / `pollEvent` | output envelope refs | `bool` | Raw queue poll without auto-cache ingestion. |
| `ingestResponse` / `ingestEvent` | one response/event | `bool` | Manual merge into adapter trackers/caches. |
| `pollResponseCached` / `pollEventCached` | output envelope refs | `bool` | Poll + auto-ingest one item. |
| `drainToCache(max_responses, max_events)` | optional limits | `size_t` | Drains queue and merges into caches. |
| `cacheClear()` | none | `void` | Clears adapter caches/tracked states/event ring. |
| `cachedNode(peer, out)` / `cachedNodes(out)` | peer or vector output | `bool`/`void` | Reads cached node snapshots. |
| `stateGenerationsGet(out)` / `pairedGeneration` / `discoveryGeneration` | output refs | values | Generation counters for frontend invalidation logic. |
| `runtimeStats(out_stats)` | runtime stats output | `bool` | Reads `ManagementRuntime` stats if bound. |
| `tick(now_ms, req_budget, resp_budget, evt_budget)` | scheduler budget values | `void` | Pumps runtime + queue/cache processing. |

## 4.9 Radio transition lifecycle APIs

| API | Expects | Returns | Details |
|---|---|---|---|
| `beginRadioTransition(options, out_result)` | transition begin options | `bool` | Enters protected transition mode. |
| `endRadioTransition(options, out_result)` | transition end options | `bool` | Resumes normal operation and optional resync. |
| `hardDeinitRadio(options, out_result)` | hard deinit options | `bool` | Best-effort full deinit path. |
| `hardReinitRadio(options, out_result)` | hard reinit options | `bool` | Reinit and optional state resync path. |
| `radioTransitionStatusGet(out)` | status output | `bool` | Returns transition active/state/epoch/error snapshot. |
| `radioTransitionActive()` / `radioEpoch()` | none | values | Fast transition state accessors. |

## 5) What to Use in New App Code

Recommended release usage:

1. Use `ManagementFrontendAdapter` for app/frontend orchestration.
2. Use `settingsReadCachedForUi` on open and `settingsRefresh` only on explicit refresh.
3. Use `settingsSetBatch` for writes (delta behavior + optional confirm).
4. Use `runAndTrack`/`operationSubmit`/`operationWait` for terminal-safe command handling.
5. Keep peer targeting explicit on all peer-bound calls.

## 6) Common Pitfalls

1. Treating queue acceptance as command success.
2. Submitting peer-bound master calls without explicit target.
3. Opening settings with forced refresh every time (breaks cache-first policy).
4. Ignoring deferred terminal events for `PairRequest`, `UnpairRequest`, `ChannelSyncAll`, `ChainLoopControlSet`, `OtaPushStart`, `OtaUpdateStart`.
5. Mixing CLI text parsing into API data flows instead of using typed responses/cache.
