# espnow-link-esp32

`espnow-link-esp32` is a reusable ESP-NOW library for ESP32 nodes. It provides:

- Core master/slave runtime (`EspNowManager`, `PairingEngine`)
- Profile and codec aware descriptor/control transport
- A shared management control plane used by both CLI and frontend APIs
- Optional runtime helpers for logging, storage, OTA, and bootstrap wiring

This repository section documents the library implementation only.

## Current Runtime Baseline

- Roles: one master controlling multiple persisted slaves
- Persisted paired capacity (master): 14 peers
- Shared typed command surface for all control frontends:
  - CLI (`ManagementSource::Cli`)
  - Wi-Fi frontend (`ManagementSource::Wifi`)
  - BLE frontend (`ManagementSource::Ble`)
  - Custom frontend (`ManagementSource::Custom`)
- Source-aware response/event routing through `ManagementRuntime`
- Access-level enforcement in `ManagementService` (`Observer`/`Operator`/`Maintainer`/`Owner`)

## Profile and Codec Model

Built-in profiles are registered through `registerBuiltInProfiles(...)`:

- `kProfilePms` -> `PMS`
- `kProfileRelay` -> `RELAY`
- `kProfileSens` -> `SENS`
- `kProfileSemu` -> `SEMU`
- `kProfileRemu` -> `REMU`
- `kProfileLockAlarm` -> `LOCK_ALARM`

Built-in codecs include default TLV and compact/packed/varint/delta/CBOR-lite/binary-map variants.

## CLI and Frontend API Parity

CLI and frontend APIs call the same management command IDs via `ManagementController`.

- CLI path: `MasterCli` -> `ManagementController` -> service/runtime
- Frontend API path: `ManagementFrontendAdapter::commands()` -> `ManagementController` -> service/runtime

For deterministic coexistence between API and CLI:

- Use explicit peer targeting for peer-bound commands
- Correlate every action by `req_id`
- Treat deferred operations as complete only after terminal response/event
- Confirm setting writes with readback when strict state convergence is required

## API-Only Fast Path for ICM UI

To keep ICM UI updates fast and deterministic, use the frontend adapter API path only.

- UI/API path:
  - `ManagementFrontendAdapter` -> `ManagementController` -> `ManagementService`/runtime
- Do not depend on CLI parsing/printing for UI data population.
- Do not route non-topology profiles through topology commands.

Current-settings fetch contract per profile (optimized path):

1. target peer/profile once
2. `settingsBundleRefresh(peer, ...)` or `nodeBundleGet(peer, mask, ...)` once per refresh action
3. `settingsGetResolved(peer, out, ...)` for cache-first fast read
4. `settingsBundleGet(peer, out, ...)` or `settingsRefresh(peer, out, ...)` only when an explicit authoritative pull is required
5. `cachedSettingsResolved(peer, out)` for immediate re-render without re-pull

Cache policy migration (library-first, in progress):

1. open-settings path should be cache-only by default
2. explicit refresh should be the only force-pull path
3. per-slave cache state should be tracked (`empty|warming|partial|ready|stale|error`)
4. startup reconcile should hydrate cache for already-paired peers (not only fresh pair events)
5. settings writes should send/apply changed keys only

Role scope:

- topology commands: `SEMU`, `SENS`, `RELAY`, `REMU` only
- generic settings fetch: `PMS`, `SEMU`, `SENS`, `RELAY`, `REMU`, `LOCK_ALARM`

Latency/throughput guidance for UI:

- Prefer one `NodeBundleGet(settings)` refresh over multiple per-key `SettingGet` calls.
- Reuse cached resolved settings during dialog open/edit cycles.
- Trigger fresh pull on explicit refresh/apply, not on every render tick.
- Keep telemetry pull independent from settings pull to avoid UI contention.

Reference plans for this fix direction:

- `docs/optimization/slave-settings-cache-policy-and-delta-write-fix-plan.md` (primary)
- `docs/optimization/slave-settings-fetch-api-alignment-plan.md` (NodeBundle transport alignment)

## CLI Quick Reference (Current)

Master CLI targeting now supports both sticky and one-shot selection:

- Sticky target:
  - `active <paired_index|MAC>`
  - `active`
  - `active clear`
- One-shot override:
  - `<paired_index> <command>`
  - `<MAC> <command>`

Common command groups:

- Descriptor/profile:
  - `desc`, `caps`, `telem`, `telem.now`, `telem.now.child <vid>`
- Settings:
  - `settings`, `settings.full`, `settings.raw`
  - `get <key>`, `get.id <id>`, `set <key>=<value>`, `set.id <id>=<value>`
- Push:
  - `push.start|update|pause|resume|stop|get`
  - `push.one`, `push.id`
  - `push.child.start <vid> ...`, `push.child.stop <vid>`

Profile-aware behavior:

- `settings.full` and `push.start/update` build from the active profile schema.
- If profile is unresolved, CLI queues a probe and asks retry.
- `telem.now.child` supports `SEMU` (`0..7`) and `REMU` (`0..15`).

Telemetry rendering by role:

- `PMS`: power table (`wallv`, `battv`, `walli`, `batti`, `psrc`, `trip`, `rcut`)
- `SENS/SEMU`: environment + TF-Luna A/B rows (mm/flux/temp)
- `RELAY/REMU`: relay system rows + per-output rows

Push validation limits (runtime-enforced):

- stream interval: `200..60000 ms`
- min report gap: `50..60000 ms`
- max metrics per stream: `16`

## Profile Updates (Current Highlights)

- `RELAY` and `REMU` now expose `persist_output_state` (global output-state persistence toggle).
- `REMU` child key namespace includes `v<vid>.output_enable`.
- `SENS` and `SEMU` include direction/calibration settings:
  - `detect_fall_delta_cm`, `detect_release_delta_cm`
  - `ab_spacing_cm`
  - `tfl_a_calib_mm`, `tfl_b_calib_mm`
  - `detect_window_ms`, `detect_clear_hold_ms`
- `SENS` and `SEMU` include sampling settings:
  - `sample_loop_ms`
  - `sample_ring_n`
- `SENS` and `SEMU` telemetry includes TF-Luna distance, flux, and temperature metrics.

## Documentation Map

Canonical docs are in `docs/current`:

- `docs/current/README.md`
- `docs/current/architecture.md`
- `docs/current/control-plane.md`
- `docs/current/cli.md`
- `docs/current/frontend-api.md`
- `docs/current/profiles-registry.md`

The rest of the subject docs under `docs/current` cover pairing, telemetry, topology, OTA, logging/storage, radio transition, testing, and examples.

Optimization and evolution structures are in `docs/optimization`:

- `docs/optimization/README.md`
- `docs/optimization/cli-api-control-optimization-structure.md`
