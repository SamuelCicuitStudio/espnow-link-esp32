# CLI Surface

`MasterCli` is the library reference operator frontend for master-side control.

## Runtime Integration

`MasterCli` issues typed management commands through `ManagementController`.

Supported backends:

- Queue mode (`ManagementQueueTransport` + `ManagementRuntime`)

CLI uses source `ManagementSource::Cli`, so responses/events stay source-isolated from Wi-Fi/BLE/custom frontends.

## Targeting Rules

Peer-bound command selectors:

- `<paired_index> <command>`
- `<MAC> <command>`

Peer-bound CLI commands require explicit selector prefix (no persistent runtime target override).

## Command Topics

Help topics exposed by CLI:

- `core`
- `paired`
- `pairing`
- `target`
- `topology`
- `desc`
- `settings`
- `push`
- `time`
- `control`
- `test`
- `log`
- `logger`
- `sd`
- `ota`

These map to the same management commands available through `ManagementController`.

## Deterministic Coexistence With API Frontends

When CLI and frontend API are both active:

- use explicit peer selectors for mutating peer commands
- track `req_id` and terminal events (`CmdDone`/`CmdFail`/`Timeout`)
- avoid issuing overlapping mutating commands for the same peer from both paths

## Interfaces

- `IMasterCliIo`
- `IMasterCliActions`
- `IMasterOtaFrontendHook`

## Code Anchors

- `include/espnow_link/cli_master.hpp`
- `src/cli/cli_master.cpp`
- `src/cli/cli_dispatch.cpp`
