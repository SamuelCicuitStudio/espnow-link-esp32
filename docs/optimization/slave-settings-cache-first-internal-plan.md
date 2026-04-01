# Slave Settings Cache-First Internal Plan

Status note (2026-03-18): this draft is superseded by
`slave-settings-cache-policy-and-delta-write-fix-plan.md`, which is the primary implementation plan.

## 1. Goal

Make slave settings handling **cache-first** inside `espnow-link-esp32` so masters do not repeatedly poll the same settings for UI rendering.

This plan is **replacement-only** (no legacy compatibility path).

Target behavior:

1. Each master keeps per-slave settings cache.
2. API-facing calls return cached settings first.
3. Network pull is done only when:
   - cache is missing,
   - user explicitly refreshes,
   - a setting write occurred and we need authoritative readback.

This removes redundant polling and reduces latency, bandwidth, and race conditions in user code.

---

## 2. Problem Summary

Current UX issues come from repeated live pulls during UI actions:

- settings sometimes appear empty/partial on first open;
- repeated snapshot calls create timing races;
- user code compensates with extra retries/polls;
- frontend and backend state drift during fast interactions.

Root cause pattern: settings reads are treated as always-live transport operations instead of cache-backed reads with explicit refresh policy.

---

## 3. Desired Model (Library Ownership)

Settings cache is a **library-managed source of truth per master process**:

- key: slave MAC
- value: resolved settings map + metadata

Cache metadata per slave:

- `settings_seq` (monotonic update sequence)
- `updated_ms` (last successful full settings update)
- `origin` (`pair_bootstrap`, `manual_refresh`, `post_write_refresh`, `background_repair`)
- `completeness` (`full`, `partial`, `empty`)
- `last_error` (optional status/message)

User code should not rebuild settings state itself; it should consume library-provided cached view.

---

## 4. Lifecycle Rules

## 4.1 Pairing Bootstrap (one-time immediate fill)

On successful pair:

1. schedule one `NodeBundleGet(settings)` for that slave;
2. ingest all pages/chunks;
3. mark cache `full` when complete.

This gives a ready baseline before first settings overlay open.

## 4.2 Overlay Open (cache-first read)

When master app asks for settings:

1. return cache immediately if present;
2. if cache missing/invalid, run one fetch and update cache, then return.

No constant background settings polling.

## 4.3 Manual Refresh (force update)

Refresh action must:

1. bypass freshness checks;
2. force one full settings bundle pull;
3. overwrite cache atomically on completion.

## 4.4 Setting Write Path

After `SettingSet` / batch set:

1. optimistic update can mark pending values locally (optional);
2. run one authoritative refresh (`NodeBundleGet(settings)`);
3. commit final cache state from slave response.

If refresh fails, keep previous cache and expose error metadata.

---

## 5. API Contract Direction (Cache-Backed)

Library API should expose two explicit read modes:

1. **Cache read** (default):
   - returns cached settings + cache metadata.
2. **Force refresh read**:
   - performs transport pull then returns updated cache.

Recommended response metadata fields:

- `cache_hit` (bool)
- `cache_completeness` (`full|partial|empty`)
- `cache_updated_ms`
- `cache_age_ms`
- `refresh_performed` (bool)
- `refresh_status`

This lets frontend/backend reason about state without custom retry loops.

### Replacement rule

- Old multi-poll settings flows are removed from library public API paths.
- Cache-backed read/refresh becomes the only supported settings read contract for frontend/API usage.
- Any remaining per-key read/write functions are treated as CLI/debug primitives, not the frontend settings source path.

---

## 6. Internal Library Work Items

## 6.1 Cache State Structure

Consolidate per-peer settings cache record in frontend adapter cache layer:

- resolved settings vector/map
- update sequence
- completeness flag
- timing/error metadata

## 6.2 Command Completion Correctness

For paged `NodeBundleGet(settings)`:

- do not mark request terminal until final page (`done=true`);
- keep partial pages as non-terminal (`OkDeferred`) state.

## 6.3 Pair Hook Integration

Trigger a single bootstrap settings pull from pair success path (or immediate follow-up orchestration path) and hydrate cache.

## 6.4 Cache-First Read API

Add explicit library methods:

- `settingsReadCached(peer, out, meta)`
- `settingsRefresh(peer, out, meta)` (force path)
- `settingsAfterWriteSync(peer, out, meta)` (authoritative post-write reconciliation)

Replacement policy:

- remove legacy settings bundle/get fan-out codepaths from frontend/API-facing methods;
- migrate existing adapter/frontend-facing settings entrypoints to the cache-first methods above;
- fail fast on deprecated calls during migration window (compile-time deprecation and runtime warnings), then remove.

## 6.5 Post-Write Refresh Policy

Centralize behavior so write APIs can optionally auto-refresh cache once and return coherent settings state.

---

## 7. User-Code Simplification Rules

After this plan is implemented, master user code should:

1. stop periodic settings polling loops;
2. call cache-read for normal UI population;
3. call force-refresh only on user request or after writes;
4. trust library metadata instead of inventing local retry logic.

This avoids redundancy between library and application layers.

---

## 8. Rollout Phases

## Phase A: Internal correctness and metadata

- finalize terminal gating for paged settings pulls;
- expose cache metadata with current cache reads.

## Phase B: Pair bootstrap hydration

- one automatic settings fetch per newly paired slave;
- verify cache is full before first UI open in normal conditions.

## Phase C: API normalization

- add/standardize cache-read and force-refresh API methods;
- remove old settings read wrappers/fallback fan-out logic.

## Phase D: App migration

- migrate ICM user code to cache-first reads;
- remove redundant settings polling and custom fallback loops.

---

## 9. Acceptance Criteria

1. First settings overlay open after pairing shows cached data without repeated manual retries.
2. Refresh button triggers exactly one forced update cycle.
3. Setting change is reflected after one post-write refresh path.
4. No repeated settings polling loop is required in master user code.
5. Logs clearly show cache hit/miss/refresh outcome for each settings read.

---

## 10. Notes

- This plan is library-wide and must support all masters, not only EasyDriveway ICM.
- Per-key get/set may remain for CLI/diagnostics, but it is no longer the frontend settings population path.
- Cache-first behavior is for API/frontends where low-latency coherent state is required.
