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
