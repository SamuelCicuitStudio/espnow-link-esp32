# OTA

## Command Surface

OTA management commands include:

- Status/capacity/gate:
  - `OtaStatusGet`
  - `OtaCapacityGet`
  - `OtaGateGet`
- Manifest:
  - `OtaManifestGet`
  - `OtaManifestPageGet`
  - `OtaManifestRebuild`
- Scope cleanup and apply/rollback:
  - `OtaClearScope`
  - `OtaApply`
  - `OtaRollback`
- Transfer protocol:
  - `OtaTransferBegin`
  - `OtaTransferChunk`
  - `OtaTransferEnd`
  - `OtaTransferAbort`
- Local push/update orchestration:
  - `OtaPushStart`
  - `OtaPushAbort`
  - `OtaPushStatus`
  - `OtaUpdateStart`
  - `OtaMasterUpdateStart`
- Archive operations:
  - `OtaArchiveList`
  - `OtaArchiveSaveRunning`
  - `OtaArchiveSaveStaged`
  - `OtaArchiveRestore`
  - `OtaArchiveDelete`
  - `OtaArchiveClear`
  - `OtaArchiveVerify`

## Runtime Behavior

- Transfer and update flows are often deferred (`OkDeferred`)
- Terminal state is delivered by lifecycle events (`CmdDone`/`CmdFail`)
- OTA transfer status also emits dedicated domain events

## Status Model

Low-level OTA status codes are defined in `OtaStatusCode` (`Ok`, `OffsetMismatch`, `CrcMismatch`, `ApplyFailed`, etc.).

## Frontend/CLI Parity

CLI and frontend APIs call the same OTA command IDs. Frontend API can use `ManagementController` for typed streaming begin/chunk/end flow.

## Code Anchors

- `include/espnow_link/ota_types.hpp`
- `include/espnow_link/ota_manager.hpp`
- `include/espnow_link/ota_descriptor_adapter.hpp`
- `include/espnow_link/management_controller.hpp`
- `src/management/management_service.cpp`
