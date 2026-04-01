# Slave Settings Cache Policy and Delta Write Fix Plan

Date: 2026-03-18  
Scope: Library-first (`espnow-link-esp32`), then ICM integration.

## 1) Objective

Lock one deterministic model for all masters and all slave profiles (`pms`, `relay/remu`, `sensor/semu`):

1. Settings dialog/open paths always read from cache first.
2. Network pull happens only on explicit refresh or post-write sync.
3. Settings write path sends/applies changed keys only.
4. Cache behavior is per-slave, explicit, and traceable.
5. Frontend API command set stays lightweight by default (cache/delta first, full bundle only when explicitly requested).

---

## 2) Verified Current Library State (Code Scan)

## 2.1 Cache APIs already exist

In `management_frontend_adapter.hpp`:

- `settingsReadCached(peer, out, meta)` -> cache-only read (no pull)
- `settingsRefresh(peer, out, timeout, out_run, out_meta)` -> force `NodeBundleGet(settings)` + settle + cache read
- `settingsBundleRefresh(peer, out_req_id, timeout)` -> async targeted refresh submit
- `settingsAfterWriteSync(peer, ...)` -> currently delegates to `settingsRefresh(...)`

Cache metadata available today:

- `SettingsCacheMeta.cache_hit`
- `SettingsCacheMeta.completeness` (`Empty|Partial|Full`)
- `SettingsCacheMeta.settings_seq`
- `SettingsCacheMeta.cache_updated_ms`
- `SettingsCacheMeta.cache_age_ms`
- `SettingsCacheMeta.refresh_performed`
- `SettingsCacheMeta.refresh_status`
- `SettingsCacheMeta.has_error/error_message`

## 2.2 Pair bootstrap exists but startup reconcile is missing

- Pair event path can trigger one settings bootstrap (`setAutoPairSettingsBootstrap` + `PairResult` handling).
- For already-paired peers restored after reboot/session start, there is no guaranteed one-shot hydrate pass.

Result: first cache-only open can still be empty/partial until manual refresh.

## 2.3 API ambiguity still exists for non-targeted refresh helpers

Adapter also exposes:

- `settingsCacheRefresh(...)`
- `settingsCacheRefreshKey(...)`
- `settingsCacheRefreshId(...)`

These methods are not peer-explicit, while multi-slave masters need per-peer deterministic targeting.

## 2.4 Batch write still processes caller payload as-is

- `settingsSetBatch(peer, items, results, options)` iterates all items.
- It confirms each set (optional), then does refresh-cache step.
- There is no built-in unchanged-key elimination before submit.

If frontend sends full form payload, transport does unnecessary work.

## 2.5 Paged settings responses can be dropped when status is deferred

`NodeBundleGet(settings)` is paged and emits intermediate chunks with `OkDeferred` status before the final `Ok`.

If adapter cache ingestion ignores deferred chunks, cached settings remain incomplete even though transport reported full coverage.

Result:

- cache stays `Partial`
- startup reconcile keeps re-submitting settings bundles
- frontend sees repeated heavy pulls and eventual overload risk.

---

## 3) Policy to Enforce (Library Contract)

## 3.1 Cache-first read is mandatory for UI/API

Open/read settings flow must use:

- `settingsReadCached(peer, out, meta)` only

No implicit pull from open/read path.

## 3.2 Explicit refresh is the only pull path

User refresh button (or API equivalent) must use:

- `settingsRefresh(peer, out, timeout, out_run, out_meta)`  
or  
- `settingsBundleRefresh(peer, out_req_id, timeout)` + later cache read

## 3.3 Post-write authoritative sync remains explicit

After a successful write:

- `settingsAfterWriteSync(peer, ...)` once

No continuous polling loop.

## 3.4 Frontend command contract for all slave roles

Use these commands by intent:

1. Open settings overlay: cache-only (`settings_only=true`, `force_refresh=false`)
2. Manual refresh button: targeted settings bundle (`settings_only=true`, `force_refresh=true`)
3. Telemetry tick: telemetry-only (`topology.slave.telemetry`)
4. Save settings: changed keys only (`topology.slave.config.set` with delta payload)

Do not use full snapshot pulls on tab switch as a default behavior.

---

## 4) Library Changes Required (Primary Work)

## 4.1 Per-slave cache state model

Add explicit policy state per cached peer:

- `Empty`
- `Warming`
- `Partial`
- `Ready`
- `Stale`
- `Error`

Store in cached node policy metadata (adapter-local) and map to public metadata fields.

## 4.2 Startup reconcile hydrator (already-paired peers)

When paired snapshot is ingested:

1. iterate paired peers
2. if peer cache is `Empty` or `Partial` and no refresh inflight:
   - schedule one targeted `settingsBundleRefresh(peer, ...)`
3. throttle:
   - max concurrent refreshes
   - cooldown/backoff per peer

This closes the first-open-empty gap after reboot.

## 4.3 Strict overlay readiness

Add a strict cache-read API for UI:

- `settingsReadCachedForUi(peer, out, meta)` (new)

Behavior:

- returns success only when cache is `Full/Ready` for UI use
- returns metadata when partial/empty without triggering pull

Required metadata additions:

- `ready_for_ui`
- `refresh_inflight`
- `last_refresh_origin` (`pair_bootstrap|startup_reconcile|manual_refresh|post_write_sync`)

## 4.4 Peer-explicit cache update API (required)

Add peer-targeted explicit refresh APIs and mark non-targeted ones deprecated for multi-slave usage:

- `settingsCacheRefreshPeer(peer, out_req_id, timeout_ms)` (new wrapper)
- `settingsCacheRefreshPeerKey(peer, key, out_req_id, timeout_ms)` (new)
- `settingsCacheRefreshPeerId(peer, setting_id, out_req_id, timeout_ms)` (new)

All must submit with `SubmitOptions.has_target_peer=true` and `target_peer=peer`.

## 4.5 Delta guard inside `settingsSetBatch`

Before sending writes:

1. read cached resolved settings for peer
2. compare requested values to cached current resolved value
3. drop unchanged items
4. expose counters in results summary:
   - `requested`
   - `changed`
   - `skipped_unchanged`

If `changed==0`, return fast success (`no_changes`) without transport submit.

## 4.6 Mandatory deferred-page cache ingest fix

Adapter cache ingestion must accept and merge descriptor payloads for:

- `ManagementStatus::Ok`
- `ManagementStatus::OkDeferred` (paged intermediate chunks)

Only hard failures should bypass descriptor merge.

This is required so bundle pages accumulate into one full cache view per peer.

---

## 5) ICM/API Integration Rules (After Library)

## 5.1 Settings dialog open

- call cache-only strict read API
- never force pull
- if cache not ready: show state (warming/partial), keep refresh button available

## 5.2 Refresh button

- call explicit peer refresh API only
- then render from cache

## 5.3 Save flow

Frontend computes delta from baseline loaded values and sends changed-only keys.  
Library still performs unchanged-key skip as safety.

---

## 6) Logging/Tracing Requirements

Add deterministic logs for each peer:

- `[CACHE][SET] read peer=.. hit=.. completeness=.. ready=.. age_ms=..`
- `[CACHE][SET] reconcile enqueue/skip peer=.. reason=..`
- `[CACHE][SET] refresh begin/end peer=.. origin=.. status=.. seq=..`
- `[CACHE][SET] write peer=.. requested=.. changed=.. skipped=..`

This must explain any empty/partial first-open case immediately.

---

## 7) Implementation Order

1. Adapter: add policy state + metadata extensions.
2. Adapter: add startup reconcile hydrator on paired snapshot ingestion.
3. Adapter: add peer-explicit refresh APIs (`Peer/PeerKey/PeerId`).
4. Adapter: add strict UI cache-read API.
5. Adapter: add unchanged-key filtering to `settingsSetBatch`.
6. ICM: switch open to strict cache-read + save delta-only payload.

---

## 8) Acceptance Criteria

1. For every slave role, settings open uses cache without triggering a pull by default.
2. Paged settings bundles are fully merged into cache (no `Partial` lock due to deferred-page drop).
3. Refresh button performs one explicit targeted refresh.
4. Saving one changed field sends/applies one changed setting only.
5. Saving without edits returns `no_changes` with zero setting writes.
6. Tab switch does not force full snapshot pulls.
7. Logs show clear per-peer cache/read/refresh/write decisions.

---

## 9) File Anchors

Primary implementation files:

- `lib/espnow-link-esp32/include/espnow_link/management_frontend_adapter.hpp`
- `lib/espnow-link-esp32/src/management/management_controller.cpp` (if helper wrappers needed)
- `lib/espnow-link-esp32/src/management/management_service.cpp` (only if additional status propagation needed)

Integration files (after library phase):

- `data/icm/web/js/m/t/cd.js`
- `data/icm/web/js/m/t/rc.js`
- `src/WiFi/wifi_manager_routes.cpp`
