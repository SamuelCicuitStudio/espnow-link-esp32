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

## Render Output Contract (Current)

CLI text output is now normalized for operator readability.
This is a presentation contract only; command IDs, payloads, and management statuses are unchanged.

### Device Descriptor

`desc` prints a fixed 2-column table:

```text
[MASTER][DESC] Device
+-------+----------------------+
| Type  | PMS                  |
| ID    | 8C:BF:EA:84:E7:20    |
| Name  | PMS-Node             |
| HW    | PMS-HW1              |
| SW    | 3.1.1                |
| Build | pms1-311-260313082004|
+-------+----------------------+
```

### Capabilities Descriptor

`caps` prints one header plus grouped sections:

```text
[MASTER][DESC] Capabilities source=provider snapshot=1476763498

[Identity]
Key             | Value
---------------+----------------------------------------------------------
Profile         | PMS
Profile ID      | 1
Schema Rev      | 1
Schema Hash     | pms001
```

Additional sections are emitted in this order when values exist:

- `[Counts]`
- `[Maps]`
- `[Features]`
- `[Other]` (only for unmapped capability keys)

### Settings Descriptor

`settings`/`settings.raw` print as sectioned tables under a normalized header:

```text
[MASTER][SET] Settings snapshot=<id> source=<source> total=<n>
```

Sections are grouped by setting domain (for example `General`, `UI / Feedback`, `Protection`, `Topology`, ...).

### Storage Info

`sd.info` prints compact 3-line summary:

```text
[MASTER][STORAGE] SD CARD | READY | SDSC | ROOT:/ | CWD:/
[MASTER][STORAGE] FREE: 232.50 MB | USED: 5.38/237.88 MB
[MASTER][STORAGE] USAGE [#-------------------] 2.26%
```

### Frontend Note

Frontend/API integrations must consume typed management/descriptor data and should not parse CLI text.

## Code Anchors

- `include/espnow_link/cli_master.hpp`
- `src/cli/cli_master.cpp`
- `src/cli/cli_dispatch.cpp`
