# CLI Surface

`MasterCli` is the reference operator console for the master role.

## Runtime Integration

`MasterCli` sends typed management commands through `ManagementController` (queue/runtime path) with source tag `ManagementSource::Cli`.

## Targeting Model (Current)

Peer-bound commands support two targeting modes:

- Sticky runtime target:
  - `active <paired_index|MAC>`
  - `active`
  - `active clear`
- One-shot prefix override:
  - `<paired_index> <command>`
  - `<MAC> <command>`

Rules:

- Prefix override applies only to that command.
- Sticky `active` target is reused by subsequent peer-bound commands.
- If sticky target is removed from persisted peers, CLI clears it automatically.

Profile resolution behavior:

- `active` show/set resolves profile from cached/hinted runtime info.
- `active` does not trigger a `caps` pull/probe.
- Commands that require a resolved profile can queue a probe when unknown.

## Command Topics

`help` topics:

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

## Command Highlights

Core:

- `list`
- `paired`
- `status`
- `active [<index|mac>|clear]`
- `live enable|disable|status`
- `cli on|off|status`

Pairing:

- `pair <index|mac>`
- `unpair`
- `remove [index|mac|slave]`

Descriptor/profile:

- `desc`
- `caps`
- `telem`
- `telem.now`
- `telem.now.child <vid>`
- `live`
- `ping`

Settings:

- `settings`
- `settings.full`
- `settings.raw`
- `get <key>`
- `get.id <id>`
- `set <key>=<value>`
- `set.id <id>=<value>`

Push:

- `push.start [mode] [interval_ms] [delta_abs] [gap_ms]`
- `push.update [mode] [interval_ms] [delta_abs] [gap_ms]`
- `push.pause|resume|stop|get`
- `push.one <metric_key> <mode> <interval_ms> <delta_abs> <gap_ms>`
- `push.id <metric_id> <mode> <interval_ms> <delta_abs> <gap_ms>`
- `push.child.start <vid> [mode] [interval_ms] [delta_abs] [gap_ms]`
- `push.child.stop <vid>`
- `autopull on [ms] | autopull off`

Topology (operator/editor/file):

- `topology.status`
- `topology.slots [committed|staged]`
- `topology.trigger <idx> <forward|reverse|1|2> [delay_ms] [hold_ms] [src_vid]`
- `topology.stage.hex <hex>`
- `topology.stage.file <path>`
- `topology.commit`
- `topology.apply.hex <hex>`
- `topology.apply.file <path>`
- `topology.plan.file <path>`
- `topology.deploy.file <path>`
- `topology.edit.*` workflow (`new|add|del|clear|show|validate|save|load`)

Project-specific fixed-chain extension (proposed):

- fixed file target: `D:\Freelancer\cornetb5\EasyDriveway-production\data\icm\o\s\tp.json`
- no path argument in these commands:
  - `topology.chain.show`
  - `topology.chain.graph`
  - `topology.chain.clear`
  - `topology.chain.add <S|R|SM|RM> <paired_index|MAC> [vi]`
  - `topology.chain.edit <index> <S|R|SM|RM> <paired_index|MAC> [vi]`
  - `topology.chain.del <index>`
  - `topology.chain.move <from_index> <to_index>`
  - `topology.chain.validate`
  - `topology.chain.fix`
  - `topology.chain.apply`
  - `topology.chain.backup`
  - `topology.chain.restore`
  - `topology.chain.set <chain_spec>`
  - `topology.chain.set.help`

Bulk one-line chain input syntax:

- `<TYPE>@<PEER>[#<CH>]` joined by `>`
- types: `S`, `R`, `SM`, `RM`
- channel only for `SM`/`RM` (`SM:1..8`, `RM:1..16`)

Storage/logger/OTA commands are available via `help logger`, `help sd`, `help ota`.

## Profile-Aware CLI Behavior

`settings.full`:

- Pulls full profile settings by iterating the active profile setting schema.
- If profile is unknown, it queues a profile probe and asks to retry.

`push.start`/`push.update`:

- Builds metrics from the active profile telemetry schema.
- If profile is unknown, it queues a profile probe and asks to retry.

`push.child.start`/`push.child.stop`:

- Valid only for `SEMU` and `REMU`.
- Uses child VID ranges:
  - `SEMU`: `0..7`
  - `REMU`: `0..15`

`telem.now.child`:

- Child-filtered telemetry view for `SEMU`/`REMU` plus global metrics.
- If profile is unresolved, fallback VID range is `0..15` until profile resolves.

## Render Output Contract (Current)

CLI text formatting is presentation-only. Management payloads/statuses are unchanged.

Descriptor/settings:

- `desc`: compact identity table
- `caps`: grouped sections (`Identity`, `Counts`, `Maps`, `Features`, `Other`)
- `settings`: sectioned setting tables with typed columns

Telemetry snapshot (`telem.now`) role-specific rendering:

- `SENS`/`SEMU`: environment block + A/B TF-Luna rows (mm/flux/temp)
- `RELAY`/`REMU`: system rows + per-child output state rows
- `PMS`: system power table (`wallv`, `battv`, `walli`, `batti`, `psrc`, `trip`, `rcut`)
- fallback: flat key/value list when role pattern is unknown

## Push Validation Limits (Enforced by Manager)

For `PushStart`/`PushUpdate`:

- stream interval: `200..60000 ms`
- stream min gap: `50..60000 ms`
- max metrics per stream: `16`
- duplicate/unknown metrics are rejected

## Frontend Note

Frontend/API clients should consume typed management/descriptors and never parse CLI text output.

## Code Anchors

- `include/espnow_link/cli_master.hpp`
- `src/cli/cli_master.cpp`
- `src/cli/cli_dispatch.cpp`
- `src/cli/cli_render.cpp`
