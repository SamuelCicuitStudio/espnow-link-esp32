# Logging and Storage

## Logging Surfaces

Local logger commands:

- `LogLocalStatusGet`
- `LogLocalRead`
- `LogLocalClear`
- `LogLocalControlSet`

Remote logger commands:

- `LogRemoteStatusGet`
- `LogRemoteRead`
- `LogRemoteClear`
- `LogRemoteControlSet`

CLI also provides remote pull/export convenience flow on top of these commands.

## Storage Surfaces

- `StorageInfoGet`
- `StorageList`
- `StorageStat`
- `StorageFormat`

Storage and OTA descriptor data are served through `IStorageExplorerProvider` paths.

## CLI Render (Storage Info)

Current `sd.info` CLI rendering is:

```text
[MASTER][STORAGE] SD CARD | READY | SDSC | ROOT:/ | CWD:/
[MASTER][STORAGE] FREE: 232.50 MB | USED: 5.38/237.88 MB
[MASTER][STORAGE] USAGE [#-------------------] 2.26%
```

`backend/state/card type/path` line is followed by capacity summary and fixed-width usage bar.

## Frontend/API Use

Frontend API and CLI both use the same command IDs. Typed parsing should be done via descriptor/management utility decode helpers or adapter cache ingestion.

## Code Anchors

- `include/espnow_link/library_logger.hpp`
- `include/espnow_link/log_storage.hpp`
- `include/espnow_link/storage_explorer_arduino.hpp`
- `src/platform/log_storage.cpp`
- `src/management/management_service.cpp`
