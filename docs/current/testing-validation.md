# Testing and Validation

## Priority Regression Areas

- discovery/pair/unpair/remove lifecycle
- descriptor and paged schema retrieval
- setting set/get convergence behavior
- telemetry pull and push lifecycle
- topology stage/commit/status/trigger
- channel sync and chain loop deferred completion
- logger and storage commands
- OTA transfer/push/update/archive flows
- radio transition blocking and recovery

## Validation Style

Use deterministic command scripts from both CLI and frontend API and verify:

- request acceptance/rejection
- response status code
- terminal lifecycle event
- expected runtime/cache side effects

## Coexistence Checks (CLI + Frontend API)

- source isolation correctness (`Cli` vs `Wifi`/`Ble`/`Custom`)
- explicit target peer routing correctness
- no lost terminal states for deferred commands
- queue pressure behavior (rejected/dropped-oldest counters)

## Code Anchors

- `src/management/management_runtime.cpp`
- `src/management/management_service.cpp`
- `include/espnow_link/management_frontend_adapter.hpp`
