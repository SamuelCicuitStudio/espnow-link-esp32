# Examples and Integration

## Master Integration

Use `MasterNodeBootstrap` to wire:

- `PairingStore`
- `EspNowManager`
- `MasterPullClient`
- `ManagementService`
- `ManagementRuntime`
- `MasterCli`

Frontend adapters can be created with:

- `makeWifiFrontendAdapter(...)`
- `makeBleFrontendAdapter(...)`
- `makeCustomFrontendAdapter(...)`

## Slave Integration

Use `SlaveNodeBootstrap` and role-specific providers/hooks for descriptor, telemetry, storage, OTA, and policy behavior.

## Example Projects

- `examples/master`
- `examples/slave_pms`
- `examples/slave_relay`
- `examples/slave_sens`
- `examples/slave_semu`
- `examples/slave_remu`

## Bring-Up Notes

When using Arduino transport, app code is responsible for Wi-Fi mode/channel bring-up before manager startup.

## Code Anchors

- `include/espnow_link/master_runtime_defaults.hpp`
- `include/espnow_link/slave_runtime_defaults.hpp`
- `examples/master/src/main.cpp`
