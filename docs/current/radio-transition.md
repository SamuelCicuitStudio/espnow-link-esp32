# Radio Transition Guard

## Purpose

Protect management state while app code temporarily changes Wi-Fi/radio mode/channel.

## Service Lifecycle

`ManagementService` transition states:

- `Idle`
- `Quiescing`
- `Paused`
- `Resuming`
- `Failed`

While transition is active, mutating commands are rejected with `BusyRadioTransition`.

## Frontend Adapter Wrappers

`ManagementFrontendAdapter` exposes lifecycle helpers:

- `beginRadioTransition(...)`
- `endRadioTransition(...)`
- `hardDeinitForRadioTransition(...)`
- `hardReinitAfterRadioTransition(...)`
- `radioTransitionStatusGet(...)`

Adapter tracks transition epoch and avoids stale pre-transition traffic.

## Recommended Flow

1. begin transition
2. perform app-owned radio switch/connect/disconnect
3. end transition (or hard reinit path)
4. resync cache/snapshots as needed

## Code Anchors

- `include/espnow_link/management_service.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`
- `src/management/management_service.cpp`
