# CLI 3 Current Command Contract

This file is the current implementation baseline for the ICM/master CLI.

Goal:

- list the command surface that exists today
- show what the ICM prints for each command
- separate immediate command acceptance lines from later async result lines
- give a precise baseline before CLI/API optimization work

Scope:

- library only
- current implementation only
- no proposed features
- no guessed behavior

Source of truth used for this baseline:

- `src/cli/cli_master.cpp`
- `src/cli/cli_dispatch.cpp`
- `src/cli/cli_render.cpp`

## Conventions

- `ICM` means the master console output. Most lines start with `[MASTER]`.
- `<...>` means runtime values that change per call.
- `Immediate` means the line printed as soon as the command is parsed/submitted.
- `Later` means the line printed later when a management response, event, or descriptor arrives.
- `Targeted command` means a peer-bound command. It uses either:
  - `<index> <peer-command>`
  - `<mac> <peer-command>`

## 1. Core Shell And Routing

### `help`

Purpose: print the full help page.

Immediate:

```text
[CORE]
  help
  help <topic>
  ...
```

Notes:

- This is a long multi-section help page.
- It includes core, pairing, topology, descriptor, settings, push, time, control, test, logger, storage, and OTA.

### `help <topic>` and `<topic> help`

Purpose: print the topic-specific help page.

Immediate:

```text
[MASTER][CLI][HELP] <topic>
  purpose: <text>
  <topic-specific command list>
```

Unknown topic:

```text
[MASTER][CLI] unknown help topic: <topic>
  run: help
  topics: core paired pairing target topology desc settings push time control test log logger sd ota
```

### `queue`

Purpose: show descriptor queue state.

Immediate:

```text
[MASTER][CLI] queue depth=<depth> max=<max> sent=<sent> fail=<fail> drop=<drop>
```

### `cli status`

Purpose: show CLI enable state and queue state.

Immediate:

```text
[MASTER][CLI] enabled=<yes|no> key=<persist_key> log=<error|info|debug>
[MASTER][CLI] queue depth=<depth> max=<max> sent=<sent> fail=<fail> drop=<drop>
```

### `cli on`

Purpose: enable the CLI and persist the state.

Immediate success:

```text
[MASTER][CLI] enabled
```

Persistence failure still prints enable:

```text
[MASTER][CLI] failed to persist enable state
[MASTER][CLI] enabled
```

### `cli off`

Purpose: disable the CLI and persist the state. It also disables autopull.

Visible result today:

- there is no dedicated success line
- only persistence failure is printed

Failure:

```text
[MASTER][CLI] failed to persist enable state
```

### `log`

Purpose: show current CLI log level.

Immediate:

```text
[MASTER][CLI] log level=<error|info|debug>
```

### `log error`

Immediate:

```text
[MASTER][CLI] log level=error
```

### `log info`

Immediate:

```text
[MASTER][CLI] log level=info
```

### `log debug`

Immediate:

```text
[MASTER][CLI] log level=debug
```

### `status`

Purpose: show paired/discovery/queue state plus targeting mode.

Immediate:

```text
[MASTER][CLI] paired=<yes|no>
[MASTER][CLI] paired_slots=<count>/14
[MASTER][CLI] target_mode=explicit_selector_only
[MASTER][CLI] runtime_target=<mac|none>
[MASTER][CLI] discovery_window=<idle|active remaining_ms=<ms>>
[MASTER][CLI] queue depth=<depth> max=<max> sent=<sent> fail=<fail> drop=<drop>
```

Debug-only extra line:

```text
[MASTER][CLI] queue_budget_per_tick=<n>
```

### `active` and `active <...>` (removed)

Purpose: legacy persistent-target command path has been removed.

Immediate:

```text
[MASTER][CLI] unknown command (type: help)
```

### Target Prefixes

Commands:

- `<index> <peer-command>`
- `<mac> <peer-command>`

Visible result:

- these prefixes do not print their own line
- they only change the target used by the nested command
- the visible output is the nested command output

## 2. Discovery And Pairing

### `list`

Purpose: open a 10-second discovery window.

Immediate via management:

```text
[MASTER][CLI] discovery window started (10s) req=<req>
```

Failure:

```text
[MASTER][CLI] discovery window start failed
```

Later:

```text
[MASTER][CLI] discovery window finished
```

Then either:

```text
[MASTER] no discovered slave
```

or:

```text
[MASTER] discovered slaves:
  0) <mac>  rssi=<rssi>  name=<name>
  1) <mac>  rssi=<rssi>  name=<name>
```

Info-level discovery notifications may also appear during the window:

```text
[MASTER] discovered[<index>] <mac>
```

### `paired` and `paired.list`

Purpose: show persisted paired peers.

Immediate when empty:

```text
[MASTER][CLI] paired_slots=0/14
[MASTER][CLI] no persisted paired peer
```

Immediate when populated:

```text
[MASTER][CLI] paired_slots=<count>/14
[MASTER][CLI] persisted paired peers:
  0) <mac>
  1) <mac>  [active]
```

### `pair <index|mac>`

Purpose: request pairing with a discovered or persisted peer selector.

Immediate success:

```text
[MASTER][CLI] pair requested with <mac>
```

Failure / validation:

```text
[MASTER][CLI] usage: pair <index|mac>  (index: discovered first, then persisted)
[MASTER][CLI] invalid peer selector
[MASTER][CLI] management path unavailable
[MASTER][CLI] pair request failed
```

Note:

- current CLI-visible contract is the request line above
- there is no separate pretty-printed pair completion line in `MasterCli`

### `unpair`

Purpose: request unpair from the active/selected target.

Immediate success:

```text
[MASTER][CLI] unpair requested
```

Failure:

```text
[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)
[MASTER][CLI] management path unavailable
[MASTER][CLI] unpair failed
```

### `remove [index|mac|slave]`

Purpose: remove one paired peer.

Immediate success:

```text
[MASTER][CLI] remove peer=<mac> requested (management)
```

Failure / validation:

```text
[MASTER][CLI] usage: remove [index|AA:BB:CC:DD:EE:FF|slave]  (index: discovered first, then persisted)
[MASTER][CLI] invalid peer selector
[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)
[MASTER][CLI] management path unavailable
[MASTER][CLI] remove request failed (management queue full)
```

## 3. Descriptor, Profile, And Online State

These are targeted peer commands.

### `desc`

Immediate:

```text
[MASTER][CLI] desc requested
```

Failure:

```text
[MASTER][CLI] descriptor request failed
```

Later:

```text
[MASTER][DESC] device
  type  : <type>
  id    : <id>
  name  : <name>
  hw    : <hw>
  sw    : <sw>
  build : <build>
```

### `caps`

Immediate:

```text
[MASTER][CLI] caps paged fetch started
```

Later:

```text
[MASTER][DESC] capabilities (source=<provider|profile-schema|profile-primary|profile-primary+provider>):
  1. <key> - <description>
  2. <key> - <description>
```

Current contract note:

- profile-backed capability pages include canonical `profile_id` in addition to `profile` name.

Empty:

```text
[MASTER][DESC] capabilities (source=<source>):
  (none)
```

### `telem`

Immediate:

```text
[MASTER][CLI] telemetry paged fetch started
```

Later:

```text
[MASTER][DESC] telemetry schema (source=<source>):
  1. id=0x<id> <key> [<unit>] range=<min>..<max> | <description>
```

Empty:

```text
[MASTER][DESC] telemetry schema (source=<source>):
  (none)
```

### `telem.now`

Immediate:

```text
[MASTER][CLI] requested live telemetry
```

Failure:

```text
[MASTER][CLI] live telemetry request failed
```

Later:

```text
[MASTER][TELEM] live samples:
  1. id=0x<id> <key>=<value> <unit>
```

Empty:

```text
[MASTER][TELEM] live samples:
  (none)
```

### `telem.now.child <vid>`

Purpose: SEMU/REMU child telemetry plus global metrics.

Immediate success:

```text
[MASTER][CLI] requested SEMU child telemetry vid=<vid> (+global)
```

or:

```text
[MASTER][CLI] requested REMU child telemetry vid=<vid> (+global)
```

Failure / validation:

```text
[MASTER][CLI] telem.now.child expects SEMU/REMU target (run caps on selected peer first)
[MASTER][CLI] usage: telem.now.child <vid:0..<max>>
[MASTER][CLI] invalid child vid (0..<max>)
[MASTER][CLI] child telemetry request failed
```

Later:

```text
[MASTER][TELEM] live samples:
  filter: child=<vid> + global
  1. id=0x<id> <key>=<value> <unit>
```

### `live`

Purpose: one-shot liveness query to the selected peer.

Immediate:

```text
[MASTER][CLI] requested liveness
```

Failure:

```text
[MASTER][CLI] liveness request failed
```

Later:

```text
[MASTER][LIVE] online=<yes|no> uptime_ms=<uptime> state=<state>
```

Optional recovery line:

```text
[MASTER][LIVE] slave recovered
```

### `ping`

Purpose: lightweight reachability probe.

Immediate:

```text
[MASTER][PING] request queued
```

Failure:

```text
[MASTER][PING] request failed
```

Later:

```text
[MASTER][PING] pong peer=<mac> rtt_ms=<ms> online=<yes|no>
```

Timeout:

```text
[MASTER][PING] timeout; no response from slave
```

### `audio ping`

Immediate:

```text
[MASTER][AUDIO] ping request queued
```

Failure:

```text
[MASTER][AUDIO] ping request failed
```

## 4. Global Live Monitor

These commands are global monitor controls, not targeted peer commands.

### `live enable`

Immediate:

```text
[MASTER][LIVE] monitor enable requested
```

Failure:

```text
[MASTER][LIVE] monitor enable request failed
```

### `live disable`

Immediate:

```text
[MASTER][LIVE] monitor disable requested
```

Failure:

```text
[MASTER][LIVE] monitor disable request failed
```

### `live status`

Immediate:

```text
[MASTER][LIVE] monitor status requested
```

Failure:

```text
[MASTER][LIVE] monitor status request failed
```

Later:

```text
[MASTER][LIVE] status
  monitor : <ON|OFF>
  tracked : <n>
  online  : <n>
  offline : <n>
  ignore  : <label>
```

Monitor events may also appear:

```text
[MASTER][LIVE] peer=<mac> <online|offline ...>
[MASTER][LIVE] request timeout; no response from slave
[MASTER][LIVE] timeout waiting for liveness response; slave offline
[MASTER][LIVE] stale liveness; slave offline
```

## 5. Settings

These are targeted peer commands.

Paged-fetch strict failure (all paged descriptor domains):

```text
[MASTER][CLI] paged fetch failed: non-paged response
```

### `settings`

Purpose: full paged settings fetch.

Immediate:

```text
[MASTER][CLI] settings paged fetch started
```

Later completion summary:

```text
[MASTER][CLI] paged fetch settings complete snapshot=<snapshot> total=<total> received=<received> missing=<missing> page_size=<page_size>
```

Then the rendered settings block:

```text
[MASTER][DESC] settings (source=<source>):
  1. id=0x<id> <key> (<string|int|float|bool>, rw=<yes|no>) current=<value> default=<value>
     <description>
```

### `settings.full`

Purpose: profile-resolved per-key settings fetch.

Immediate:

```text
[MASTER][CLI] settings.full queued <queued>/<total> requests for profile=<profile>
```

Failure path:

```text
[MASTER][CLI] profile unknown; run caps first
[MASTER][CLI] unknown profile id in registry: 0x<id>
[MASTER][CLI] profile settings empty
```

```text
[MASTER][DESC] settings (source=<source>):
  1. id=0x<id> <key> (<type>, rw=<yes|no>) current=<value> default=<value>
     <description>
```

### `settings.raw`

Immediate:

```text
[MASTER][CLI] settings.raw paged fetch started
```

Later:

- same settings block renderer as above

### `get <setting_key>`

Immediate:

```text
[MASTER][CLI] requested setting <key>
```

Failure / validation:

```text
[MASTER][CLI] usage: get <setting_key>  (child: v<vid>.<field>)
[MASTER][CLI] get setting request failed
```

Later:

```text
[MASTER][DESC] setting (source=<source>):
  - id=0x<id> <key> (<type>, rw=<yes|no>)
    nvs=<nvs_key> current=<value> default=<value>
    desc=<description>
```

### `get.id <setting_id>`

Immediate:

```text
[MASTER][CLI] requested setting id=0x<id>
```

Failure / validation:

```text
[MASTER][CLI] usage: get.id <setting_id>
[MASTER][CLI] invalid setting_id
[MASTER][CLI] get.id request failed
```

Later:

- same single-setting renderer as `get <key>`

### `set <setting_key>=<value>`

Immediate:

```text
[MASTER][CLI] set requested: <key>=<value>
```

Failure / validation:

```text
[MASTER][CLI] usage: set <setting_key>=<value>  (child: v<vid>.<field>)
[MASTER][CLI] set setting request failed
```

Later:

- current CLI does not have a special pretty-printer for successful write completion
- follow-up verification is normally done with `get` or `settings`

### `set.id <setting_id>=<value>`

Immediate:

```text
[MASTER][CLI] set.id requested: 0x<id>=<value>
```

Failure / validation:

```text
[MASTER][CLI] usage: set.id <setting_id>=<value>
[MASTER][CLI] invalid setting_id
[MASTER][CLI] set.id request failed
```

Later:

- same note as `set <key>=<value>`

## 6. Telemetry Push

These are targeted peer commands.

### `autopull on [ms]`

Immediate:

```text
[MASTER][CLI] autopull enabled interval_ms=<ms>
```

### `autopull off`

Immediate:

```text
[MASTER][CLI] autopull disabled
```

### `push.pause`

Immediate:

```text
[MASTER][CLI] push.pause sent corr=<corr>
```

### `push.resume`

Immediate:

```text
[MASTER][CLI] push.resume sent corr=<corr>
```

### `push.stop`

Immediate:

```text
[MASTER][CLI] push.stop sent corr=<corr>
```

### `push.get`

Immediate:

```text
[MASTER][CLI] push.get sent corr=<corr>
```

Current note:

- there is no dedicated pretty-printed push status response in `MasterCli`

### `push.start [mode] [interval_ms] [delta_abs] [gap_ms]`

Immediate:

```text
[MASTER][CLI] push.start sent mode=<hybrid|periodic|change> metrics=<count> interval_ms=<ms> delta=<delta> gap_ms=<ms> corr=<corr>
```

Failure / validation:

```text
[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)
[MASTER][CLI] management path unavailable
[MASTER][CLI] invalid push mode (use: hybrid|periodic|change)
[MASTER][CLI] invalid interval_ms (>=200)
[MASTER][CLI] invalid delta_abs (>=0)
[MASTER][CLI] invalid gap_ms (>=50)
[MASTER][CLI] telemetry profile unknown; run caps first
[MASTER][CLI] no telemetry metrics in profile
[MASTER][CLI] push command send failed
```

### `push.update [mode] [interval_ms] [delta_abs] [gap_ms]`

Immediate:

```text
[MASTER][CLI] push.update sent mode=<hybrid|periodic|change> metrics=<count> interval_ms=<ms> delta=<delta> gap_ms=<ms> corr=<corr>
```

Failure / validation:

- same as `push.start`

### `push.one <metric_key> <mode> <interval_ms> <delta_abs> <gap_ms>`

Immediate:

```text
[MASTER][CLI] push.one sent metric=<metric_key> mode=<mode> interval_ms=<ms> delta=<delta> gap_ms=<ms> corr=<corr>
```

Validation:

```text
[MASTER][CLI] invalid metric_key
[MASTER][CLI] push command send failed
```

### `push.id <metric_id> <mode> <interval_ms> <delta_abs> <gap_ms>`

Immediate:

```text
[MASTER][CLI] push.id sent metric_id=0x<id> mode=<mode> interval_ms=<ms> delta=<delta> gap_ms=<ms> corr=<corr>
```

Validation:

```text
[MASTER][CLI] invalid metric_id
[MASTER][CLI] push command send failed
```

### `push.child.start <vid> [mode] [interval_ms] [delta_abs] [gap_ms]`

Immediate:

```text
[MASTER][CLI] push.child.start sent profile=<SEMU|REMU> vid=<vid> active_children=<count> metrics=<count> corr=<corr>
```

Failure / validation:

```text
[MASTER][CLI] child push requires SEMU or REMU target (run caps on selected peer first)
[MASTER][CLI] usage: push.child.start <vid:0..<max>> [hybrid|periodic|change] [interval_ms] [delta_abs] [gap_ms]
[MASTER][CLI] invalid child vid (0..<max>)
[MASTER][CLI] failed to build child push config
[MASTER][CLI] failed to build child push update
[MASTER][CLI] child push command send failed
```

### `push.child.stop <vid>`

Immediate:

```text
[MASTER][CLI] push.child.stop sent profile=<SEMU|REMU> vid=<vid> active_children=<count> corr=<corr>
```

Validation:

```text
[MASTER][CLI] child push requires SEMU or REMU target (run caps on selected peer first)
[MASTER][CLI] usage: push.child.stop <vid:0..<max>>
[MASTER][CLI] invalid child vid (0..<max>)
[MASTER][CLI] child <vid> push already stopped
[MASTER][CLI] child stop command send failed
```

### Unknown / generic push validation

```text
[MASTER][CLI] invalid push command
[MASTER][CLI] unknown push command
```

## 7. Time

These are targeted peer commands except `time.local`.

### `time.get`

Immediate:

```text
[MASTER][CLI] requested slave time
```

Failure:

```text
[MASTER][CLI] slave time request failed
```

Later:

```text
[MASTER][TIME] epoch_s=<epoch> uptime_ms=<uptime>
```

### `time.set <epoch_s>`

Immediate:

```text
[MASTER][CLI] requested slave time set to <epoch_s>
```

Failure / validation:

```text
[MASTER][CLI] usage: time.set <epoch_s>
[MASTER][CLI] invalid epoch_s
[MASTER][CLI] slave time set request failed
```

### `time.set.now`

Immediate:

```text
[MASTER][CLI] requested slave time set to <epoch_s>
```

Failure:

```text
[MASTER][CLI] slave time set request failed
```

### `time.local`

Immediate:

```text
[MASTER][TIME] local_epoch_s=<epoch_s>
```

## 8. Test, Metrics, And Control

### `test.all`, `selftest`, `comm.test`

Immediate:

```text
[MASTER][CLI] test.all queued desc=<ok|fail> caps=<ok|fail> settings=<ok|fail> telem=<ok|fail> telem.now=<ok|fail> live=<ok|fail> time=<ok|fail>
```

Later:

- normal descriptor, telemetry, live, and time renderers arrive as each queued request completes

### `comm.test.status` and `comm.test.report`

Immediate:

```text
[MASTER][CLI] comm.test status (this build uses selftest queue mode):
  paired=<yes|no> peer=<mac|none>
  probe_pending=<none|live|ping> age_ms=<ms>
  autopull=<on|off> interval_ms=<ms> live_online=<yes|no>
  trigger full flow with: test.all
[MASTER][CLI] queue depth=<depth> max=<max> sent=<sent> fail=<fail> drop=<drop>
```

### `metrics`

Immediate:

```text
[MASTER][METRICS] counters:
  tick_count=<n>
  rx_frames=<n> rx_bytes=<n>
  tx_frames=<n> tx_bytes=<n> tx_failures=<n>
[MASTER][METRICS] timing_us:
  tick last=<n> max=<n> avg=<n>
  rx   last=<n> max=<n> avg=<n>
  tx   last=<n> max=<n> avg=<n>
```

If management runtime is bound:

```text
[MASTER][METRICS][MGMT] runtime:
  submitted=<n> dropped_req=<n>
  dropped_req.service_rejected=<n>
  dispatched_resp=<n> dropped_resp=<n>
  dropped_resp.no_route=<n> dropped_resp.transport_rejected=<n>
  dispatched_evt=<n> dropped_evt=<n> transports=<n>
  dropped_evt.no_route=<n> dropped_evt.transport_rejected=<n>
```

If management queue transport is bound:

```text
[MASTER][METRICS][MGMT] cli_queue:
  req=<n> resp=<n> evt=<n>
  req_stats enq=<n> rej_new=<n> drop_oldest=<n>
  resp_stats enq=<n> rej_new=<n> drop_oldest=<n>
  evt_stats enq=<n> rej_new=<n> drop_oldest=<n>
```

If management service is bound:

```text
[MASTER][METRICS][MGMT] service_queue:
  req=<n> resp=<n> evt=<n>
```

### `metrics.reset`

Immediate:

```text
[MASTER][METRICS] reset
```

### `radio.drytest`

Busy-queue refusal:

```text
[MASTER][RADIO][DRYTEST] busy queues tx(req=<n> resp=<n> evt=<n>) svc(req=<n> resp=<n> evt=<n>); rerun when idle
```

Main dry-run sequence:

```text
[MASTER][RADIO][DRYTEST] pre active=<yes|no> state=<state> epoch=<epoch>
[MASTER][RADIO][DRYTEST] begin=<ok|fail> active=<yes|no> state=<state> epoch=<epoch>
[MASTER][RADIO][DRYTEST] blocked_cmd=DiscoveryStart submit=<yes|no> req=<req> path=<transport|service>
[MASTER][RADIO][DRYTEST] blocked_status seen=<yes|no> value=<status>(0x<code>)
[MASTER][RADIO][DRYTEST] end=<ok|fail> active=<yes|no> state=<state> epoch=<epoch>
[MASTER][RADIO][DRYTEST] PASS
```

or:

```text
[MASTER][RADIO][DRYTEST] FAIL
```

### `restart master` and `reset master`

Immediate success:

```text
[MASTER][CLI] master restart/reset requested via management
```

Failure:

```text
[MASTER][CLI] management path unavailable
[MASTER][CLI] master restart/reset request failed (management queue full)
```

### `restart slave`

Immediate:

```text
[MASTER][CLI] restart slave request cmd=sent peer=<mac>
```

Failure forms:

```text
[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)
[MASTER][CLI] management path unavailable
[MASTER][CLI] restart slave request cmd=send-failed/queue-full peer=<mac>
```

### `reset slave`

Immediate:

```text
[MASTER][CLI] reset slave request cmd=sent peer=<mac>
```

Failure forms:

```text
[MASTER][CLI] target not selected (use <paired_index|paired_mac> command prefix)
[MASTER][CLI] management path unavailable
[MASTER][CLI] reset slave request cmd=send-failed/queue-full peer=<mac>
```

## 9. Topology

These are management-driven topology commands.

### `topology.status`

Immediate:

```text
[MASTER][TOPO] status requested req=<req>
```

Failure:

```text
[MASTER][TOPO] status request failed
```

Later:

```text
[MASTER][TOPO] status
  schema         : v<schema>
  staged         : <yes|no>
  committed      : <yes|no>
  staged_ver     : <ver>
  committed_ver  : <ver>
  staged_state   : <none|staged|committed>
  committed_state: <none|staged|committed>
  staged_slots   : <n>
  committed_slots: <n>
  staged_groups  : <n>
  committed_groups: <n>
```

Async acceptance case:

```text
[MASTER][TOPO] status request accepted (remote/async)
```

### `topology.commit`

Immediate:

```text
[MASTER][TOPO] commit requested req=<req>
```

Failure:

```text
[MASTER][TOPO] commit request failed
```

Later:

```text
[MASTER][TOPO] deploy summary queued=<queued> failed=<failed>
```

### `topology.slots [committed|staged]`

Immediate:

```text
[MASTER][TOPO] slots requested req=<req> state=<committed|staged>
```

Failure / validation:

```text
[MASTER][TOPO] usage: topology.slots [committed|staged]
[MASTER][TOPO] slots request failed
```

Later:

```text
[MASTER][TOPO] slots state=<committed|staged|none> total=<total> enabled=<enabled>
  slot=<slot> peer=<mac> role=<role_code> gid=<gid> rid=<relative_index> lvid=<local_vid> pvid=<peer_vid> axis=<axis> dly=<delay_ms> hold=<hold_ms>
```

Async acceptance case:

```text
[MASTER][TOPO] slots request accepted (remote/async)
```

### `topology.trigger <idx> <dir> [delay_ms] [hold_ms] [src_vid]`

Immediate:

```text
[MASTER][TOPO] trigger requested req=<req> idx=<idx> dir=<1|2> delay=<delay_ms> hold=<hold_ms> src_vid=<vid>
```

Failure / validation:

```text
[MASTER][TOPO] usage: topology.trigger <idx> <forward|reverse|1|2> [delay_ms] [hold_ms] [src_vid]
[MASTER][TOPO] invalid target index (allowed -<max>..-1 or 1..<max>)
[MASTER][TOPO] invalid direction (use forward|reverse|1|2)
[MASTER][TOPO] invalid delay_ms
[MASTER][TOPO] invalid hold_ms
[MASTER][TOPO] invalid src_vid
[MASTER][TOPO] trigger request failed
```

Later response:

```text
[MASTER][TOPO] trigger queued seq=<seq>
```

Or async acceptance:

```text
[MASTER][TOPO] trigger request accepted (async)
```

Related events:

```text
[MASTER][TOPO][RX] peer=<mac> seq=<seq> OK dir=<dir> delay=<delay> hold=<hold>
[MASTER][TOPO][RX] peer=<mac> seq=<seq> DUP dir=<dir> delay=<delay> hold=<hold>
[MASTER][TOPO][RX] peer=<mac> seq=<seq> REJECT reason=<reason>
[MASTER][TOPO][ACK] peer=<mac> ack_seq=<ack_seq> result=<result> reason=<reason>
```

### `topology.stage.hex <hex>` and `topology.stage.file <path>`

Immediate:

```text
[MASTER][TOPO] stage requested req=<req> version=<version> slots=<slots> groups=<groups>
```

Failure / validation:

```text
[MASTER][TOPO] invalid hex payload
[MASTER][TOPO] invalid topology payload
[MASTER][TOPO] local storage backend unavailable
[MASTER][TOPO] usage: topology.stage.file <path>
[MASTER][TOPO] file read failed path=<path> reason=<reason>
[MASTER][TOPO] payload decode failed path=<path>
[MASTER][TOPO] stage request failed
```

### `topology.apply.hex <hex>` and `topology.apply.file <path>`

Immediate success:

```text
[MASTER][TOPO] apply requested stage_req=<stage_req> commit_req=<commit_req> version=<version>
```

Failure:

```text
[MASTER][TOPO] apply failed: stage submit failed
[MASTER][TOPO] apply failed: commit submit failed
```

### `topology.plan.file <path>`

Purpose: dry-run planning only, no RF send.

Immediate dry-run report begins with:

```text
[MASTER][TOPO][PLAN] file=<path> topo_ver=<ver> nodes=<n> groups=<n> targets=<n>
[MASTER][TOPO][PLAN] icm_mac=<mac>
[MASTER][TOPO][PLAN] mode=dry-run (no RF send)
```

Then the planner prints chain, seed, per-device, per-view, secure-add, and peer list sections, for example:

```text
[MASTER][TOPO][CHAIN] <chain_view>
[MASTER][TOPO][SEEDS]
  gid=<gid> seed_u32=<seed_u32> seed32=<seed_hex>
[MASTER][TOPO][DEVICE] mac=<mac> slots=<n> neg=<n> pos=<n> status=<OK|OVER_CAP>
  index_map=[...]
  groups=[...]
  secure_add:
    rid=+1 peer=<mac> peer_role=<role> slot_gid=<gid> key_gid=<gid> local_vi=<vi> peer_vi=<vi> lmk=<hex>
  peers=<mac>, <mac>
[MASTER][TOPO][PLAN] done dry-run only
```

Failure / validation:

```text
[MASTER][TOPO] topology.local.plan.file is not supported
[MASTER][TOPO] usage: topology.plan.file <path>
[MASTER][TOPO] dry plan failed: local storage unavailable
[MASTER][TOPO] dry plan read failed path=<path> reason=<reason>
[MASTER][TOPO] dry plan expects JSON chain file path=<path>
[MASTER][TOPO] dry plan parse failed path=<path> reason=<reason>
```

### `topology.deploy.file <path>`

JSON chain deployment path:

```text
[MASTER][TOPO] target=<mac> slots=<slots> groups=<groups> peers=<peer_count>
[MASTER][TOPO]   peer=<mac>
[MASTER][TOPO] deploy queued peer=<mac> stage_req=<stage_req> commit_req=<commit_req>
...
[MASTER][TOPO] deploy summary targets=<targets> paired=<paired_targets> queued=<queued> failed=<failed> skipped_not_paired=<skipped> topo_ver=<ver>
```

Failure / validation:

```text
[MASTER][TOPO] topology.local.deploy.file is not supported
[MASTER][TOPO] usage: topology.deploy.file <path>
[MASTER][TOPO] deploy file must be topology chain json
[MASTER][TOPO] chain json parse failed path=<path> reason=<reason>
[MASTER][TOPO] no paired peers to deploy
[MASTER][TOPO] skip target=<mac> reason=not_paired
[MASTER][TOPO] deploy submit failed peer=<mac>
```

### `topology.local.*`

Purpose: same command family as `topology.*` but forced local-only.

Visible rule:

- outputs are the same as the corresponding `topology.*` command
- target peer is cleared before submission

Unsupported forms:

```text
[MASTER][TOPO] topology.local.plan.file is not supported
[MASTER][TOPO] topology.local.deploy.file is not supported
```

## 10. Logger

### `logger.status`

Immediate:

```text
[MASTER][LOGGER] enabled=<yes|no> min_level=<level> store=<ready|unavailable> used=<bytes> dropped=<bytes> records=<count> rotations=<count>
```

Failure:

```text
[MASTER][LOGGER] status failed: <status>
[MASTER][LOGGER] status parse failed
```

### `logger.enable`

Immediate:

```text
[MASTER][LOGGER] enabled
```

Failure:

```text
[MASTER][LOGGER] enable failed: <status>
```

### `logger.disable`

Immediate:

```text
[MASTER][LOGGER] disabled
```

Failure:

```text
[MASTER][LOGGER] disable failed: <status>
```

### `logger.clear`

Immediate:

```text
[MASTER][LOGGER] cleared
```

Failure:

```text
[MASTER][LOGGER] clear failed: <status>
```

### `logger.read <offset> [max_bytes]`

Immediate:

```text
[MASTER][LOGGER] chunk offset=<offset> total=<total> bytes=<len>
  00000000: <hex bytes...>
```

Empty:

```text
[MASTER][LOGGER] chunk offset=<offset> total=<total> bytes=0
  (empty)
```

Failure / validation:

```text
[MASTER][LOGGER] usage: logger.read <offset> [max_bytes]
[MASTER][LOGGER] invalid offset
[MASTER][LOGGER] invalid max_bytes
[MASTER][LOGGER] read failed: <status>
[MASTER][LOGGER] read parse failed
```

### `logger.remote.status`

Immediate request:

```text
[MASTER][LOGGER] remote status requested
```

Failure:

```text
[MASTER][LOGGER][REMOTE] pull is active; stop it first or wait for completion
[MASTER][LOGGER] remote status request failed: <status>
[MASTER][LOGGER] management queue full
[MASTER][LOGGER] management response pending
```

Later:

```text
[MASTER][LOGGER][REMOTE] available=<yes|no> enabled=<yes|no> level=<level> size=<bytes> dropped=<bytes> records=<count> rotations=<count>
```

### `logger.remote.enable`

Immediate:

```text
[MASTER][LOGGER] remote enable requested
```

Failure:

```text
[MASTER][LOGGER] remote enable request failed: <status>
```

### `logger.remote.disable`

Immediate:

```text
[MASTER][LOGGER] remote disable requested
```

Failure:

```text
[MASTER][LOGGER] remote disable request failed: <status>
```

### `logger.remote.clear`

Immediate:

```text
[MASTER][LOGGER] remote clear requested
```

Failure:

```text
[MASTER][LOGGER] remote clear request failed: <status>
```

### `logger.remote.read <offset> [max_bytes<=128]`

Immediate request:

```text
[MASTER][LOGGER] remote read requested offset=<offset> max_bytes=<max_bytes>
```

Failure / validation:

```text
[MASTER][LOGGER][REMOTE] pull is active; stop it first or wait for completion
[MASTER][LOGGER] usage: logger.remote.read <offset> [max_bytes<=128]
[MASTER][LOGGER] invalid offset
[MASTER][LOGGER] invalid max_bytes
[MASTER][LOGGER] remote read request failed: <status>
```

Later:

```text
[MASTER][LOGGER][REMOTE] chunk offset=<offset> total=<total> bytes=<len>
  00000000: <hex bytes...>
```

### `logger.remote.pull [chunk_bytes<=128]`

Immediate start:

```text
[MASTER][LOGGER][REMOTE] pull started chunk=<chunk_bytes>
```

Validation:

```text
[MASTER][LOGGER][REMOTE] usage: logger.remote.pull [chunk_bytes<=128]
[MASTER][LOGGER][REMOTE] invalid chunk_bytes (1..128)
[MASTER][LOGGER][REMOTE] target not selected
[MASTER][LOGGER][REMOTE] pull already active
[MASTER][LOGGER][REMOTE] export store unavailable
[MASTER][LOGGER][REMOTE] failed to clear export store
```

Progress:

```text
[MASTER][LOGGER][REMOTE] pull progress <bytes>/<total> bytes chunks=<chunks>
```

Successful end:

```text
[MASTER][LOGGER][REMOTE] remote log pull complete bytes=<bytes> chunks=<chunks> elapsed_ms=<ms> file=<path>
[MASTER][LOGGER][REMOTE] decode with: python tools/log_decode.py <exported_file>
```

Other successful empty case:

```text
[MASTER][LOGGER][REMOTE] remote logger empty bytes=0 chunks=0 elapsed_ms=<ms> file=<path>
[MASTER][LOGGER][REMOTE] decode with: python tools/log_decode.py <exported_file>
```

Failure end:

```text
[MASTER][LOGGER][REMOTE] pull failed: <reason> bytes=<bytes> chunks=<chunks> elapsed_ms=<ms>
```

### `logger.remote.stop`

If no pull is active:

```text
[MASTER][LOGGER][REMOTE] no active pull
```

If active:

```text
[MASTER][LOGGER][REMOTE] pull failed: stopped by user bytes=<bytes> chunks=<chunks> elapsed_ms=<ms>
```

## 11. Channel And Chain Control

### `channel.runtime.status`

Immediate:

```text
[MASTER][CHANNEL] current=<channel> peers=<count>
  peer=<mac> channel=<channel> key=<key>
```

Failure:

```text
[MASTER][CHANNEL] runtime status failed: <status>
[MASTER][CHANNEL] runtime status parse failed
```

### `channel.sync <1..14>`

Immediate:

```text
[MASTER][CHANNEL] sync started target=<channel>
```

Validation / rejection:

```text
[MASTER][CHANNEL] usage: channel.sync <1..14>
[MASTER][CHANNEL] invalid channel (1..14)
[MASTER][CHANNEL] sync rejected: <status>
```

Later:

```text
[MASTER][CHANNEL] sync done target=<channel> acked=<acked>/<total>
```

or:

```text
[MASTER][CHANNEL] sync failed target=<channel> acked=<acked>/<total>
```

Fallback if payload not parsed:

```text
[MASTER][CHANNEL] sync done
[MASTER][CHANNEL] sync failed
```

### `chain.loop.status`

Immediate:

```text
[MASTER][CHAIN] loop_auto=<on|off>
```

Failure:

```text
[MASTER][CHAIN] status failed: <status>
[MASTER][CHAIN] status parse failed
```

### `chain.loop.on`

Immediate:

```text
[MASTER][CHAIN] loop_auto apply started target=on
```

Rejection:

```text
[MASTER][CHAIN] loop_auto apply rejected: <status>
```

Later:

```text
[MASTER][CHAIN] loop_auto done state=on acked=<acked>/<total>
```

or:

```text
[MASTER][CHAIN] loop_auto failed state=on acked=<acked>/<total>
```

### `chain.loop.off`

Immediate:

```text
[MASTER][CHAIN] loop_auto apply started target=off
```

Rejection:

```text
[MASTER][CHAIN] loop_auto apply rejected: <status>
```

Later:

```text
[MASTER][CHAIN] loop_auto done state=off acked=<acked>/<total>
```

or:

```text
[MASTER][CHAIN] loop_auto failed state=off acked=<acked>/<total>
```

## 12. Storage Explorer

### `sd.pwd`

Immediate:

```text
[MASTER][SD][LOCAL] cwd=<path>
```

### `sd.info`

Immediate:

```text
[MASTER][STORAGE] <disabled|sd|spiffs|unknown> <unavailable|ready|not-mounted> | used <used_mb>/<total_mb> MB (<pct>%) | free <free_mb> MB
[MASTER][STORAGE] path root=<root> cwd=<cwd>
```

Optional extra line:

```text
[MASTER][STORAGE] card type=<type> size=<mb>MB
```

or:

```text
[MASTER][STORAGE] note=<text>
```

### `sd.ls [path]`

Immediate:

```text
[MASTER][STORAGE] list path=<path> parent=<parent> count=<count>
  1. [D] <name> size=<bytes>
  2. [F] <name> size=<bytes>
```

Empty:

```text
[MASTER][STORAGE] list path=<path> parent=<parent> count=0
  (empty)
```

Validation:

```text
[MASTER][SD][LOCAL] usage: sd.ls [path]
```

### `sd.stat <path>`

Immediate:

```text
[MASTER][STORAGE] stat path=<path> exists=<yes|no> type=<dir|file> size=<bytes>
```

Validation:

```text
[MASTER][SD][LOCAL] usage: sd.stat <path>
```

### `sd.cd <path>`

Immediate success:

```text
[MASTER][SD][LOCAL] cwd=<path>
```

Validation / failure:

```text
[MASTER][SD][LOCAL] usage: sd.cd <path>
[MASTER][SD][LOCAL] cd failed: target is not directory
```

### `sd.up`

Immediate success:

```text
[MASTER][SD][LOCAL] cwd=<path>
```

Failure:

```text
[MASTER][SD][LOCAL] up failed: parent is not directory
```

### `sd.format`

Immediate:

```text
[MASTER][SD][LOCAL] format started (erasing and rebuilding layout)...
```

Then:

```text
[MASTER][SD][LOCAL] format done
```

or a backend-provided message line.

### `sd.remote.pwd`

Immediate:

```text
[MASTER][SD][REMOTE] cwd=<path>
```

### `sd.remote.info`

Immediate request:

```text
[MASTER][SD][REMOTE] info requested
```

Failure:

```text
[MASTER][SD][REMOTE] info request failed
[MASTER][CLI] management path unavailable
```

Later:

- same storage info renderer as `sd.info`

### `sd.remote.ls [path]`

Immediate request:

```text
[MASTER][SD][REMOTE] ls requested path=<path>
```

Validation / failure:

```text
[MASTER][SD][REMOTE] usage: sd.remote.ls [path]
[MASTER][SD][REMOTE] ls request failed
```

Later:

- same storage list renderer as `sd.ls`

### `sd.remote.stat <path>`

Immediate request:

```text
[MASTER][SD][REMOTE] stat requested path=<path>
```

Validation / failure:

```text
[MASTER][SD][REMOTE] usage: sd.remote.stat <path>
[MASTER][SD][REMOTE] stat request failed
```

Later:

- same storage stat renderer as `sd.stat`

### `sd.remote.cd <path>`

Immediate request:

```text
[MASTER][SD][REMOTE] cd requested path=<path>
```

Failure:

```text
[MASTER][SD][REMOTE] usage: sd.remote.cd <path>
[MASTER][SD][REMOTE] cd request failed
```

Later success:

```text
[MASTER][STORAGE] stat path=<path> exists=yes type=dir size=<bytes>
[MASTER][SD][REMOTE] cwd=<path>
```

Later failure:

```text
[MASTER][STORAGE] stat path=<path> exists=<no|yes> type=<file|dir> size=<bytes>
[MASTER][SD][REMOTE] cd failed path=<path>
```

### `sd.remote.up`

Immediate request:

```text
[MASTER][SD][REMOTE] up requested path=<path>
```

Failure:

```text
[MASTER][SD][REMOTE] up request failed
```

Later:

- same `cwd` / `cd failed` contract as `sd.remote.cd`

### `sd.remote.format`

Immediate:

```text
[MASTER][SD][REMOTE] format requested
```

Failure:

```text
[MASTER][SD][REMOTE] format request failed
```

### Unknown storage command

```text
[MASTER][SD] invalid command
[MASTER][SD] unknown storage command
```

## 13. OTA

### `ota.info`

Immediate:

```text
[MASTER][OTA] status requested
```

Failure:

```text
[MASTER][OTA] status request failed
```

Later:

```text
[MASTER][OTA] state=<idle|receiving|ready|applying|failed|unknown>(<id>) code=<ok|storage_not_ready|gate_denied|gate_busy|gate_prep_failed|image_too_large|invalid_state|invalid_argument|offset_mismatch|size_mismatch|crc_mismatch|apply_rejected|apply_failed|timeout|internal_error|unknown>(0x<code>) size=<received>/<expected> crc=0x<actual>/0x<expected>
```

Optional extra lines:

```text
[MASTER][OTA] temp=<temp_path> image=<image_path>
[MASTER][OTA] persisted=<state> epoch=<epoch> sw=<sw> build=<build>
[MASTER][OTA] note=<text>
```

### `ota.manifest`

Immediate:

```text
[MASTER][OTA] manifest paged fetch queued
```

Later page header:

```text
[MASTER][OTA] manifest page snapshot=<snapshot> total=<total> cursor=<cursor> returned=<returned> next=<next> done=<yes|no>
```

Later merged view:

```text
[MASTER][OTA] manifest entries=<count>
  1. id=<id> name=<name> size=<bytes>B crc=0x<crc> state=<state>
     version=<version> build=<build> created=<epoch> app_required=<bytes>B
```

Empty:

```text
[MASTER][OTA] manifest entries=0
  (empty)
```

Optional note:

```text
[MASTER][OTA] note=<text>
```

### `ota.manifest.rebuild`

Immediate:

```text
[MASTER][OTA] manifest rebuild requested
```

Failure:

```text
[MASTER][OTA] manifest rebuild request failed
```

### `ota.capacity`

Immediate:

```text
[MASTER][OTA] capacity requested
```

Failure:

```text
[MASTER][OTA] capacity request failed
```

Later:

```text
[MASTER][OTA] capacity max_fw=<mb>MB last_image=<mb>MB fit=<yes|no>
```

Optional note:

```text
[MASTER][OTA] note=<text>
```

### `ota.gate`

Immediate:

```text
[MASTER][OTA] gate status requested
```

Failure:

```text
[MASTER][OTA] gate request failed
```

Later:

```text
[MASTER][OTA] gate=<ready|denied|busy|prep_failed|unknown>(<id>) detail=<detail>
```

Optional note:

```text
[MASTER][OTA] note=<text>
```

### `ota.clear <in|img|man|all>`

Immediate:

```text
[MASTER][OTA] clear requested scope=<in|img|man|all>
```

Validation / failure:

```text
[MASTER][OTA] usage: ota.clear <in|img|man|all>
[MASTER][OTA] invalid clear scope (use in|img|man|all)
[MASTER][OTA] clear request failed
```

### `ota.clear.images`

Immediate:

```text
[MASTER][OTA] clear requested scope=img
```

Failure:

```text
[MASTER][OTA] clear request failed
```

### `ota.local.clear.images`

Immediate success:

```text
[MASTER][OTA] local images cleared: /o/img
```

Failure:

```text
[MASTER][OTA] local clear unavailable (no local OTA storage backend bound)
[MASTER][OTA] local clear failed: storage not ready (<reason>)
[MASTER][OTA] local clear failed: <reason>
```

### `ota.apply <image_id|name>`

Immediate:

```text
[MASTER][OTA] apply requested target=<target>
```

Validation / failure:

```text
[MASTER][OTA] usage: ota.apply <image_id|image_name>
[MASTER][OTA] apply request failed
```

### `ota.rollback slave`

Immediate:

```text
[MASTER][OTA] slave rollback requested
```

Failure:

```text
[MASTER][OTA] slave rollback request failed
```

### `ota.rollback master`

Success:

```text
[MASTER][OTA] master rollback requested
```

or:

```text
[MASTER][OTA] master rollback requested: <message>
```

Failure:

```text
[MASTER][OTA] master rollback unavailable (no actions hook)
[MASTER][OTA] master rollback failed
[MASTER][OTA] master rollback failed: <message>
```

### `ota.prepare`

Immediate:

```text
[MASTER][OTA] remote prepare requested (OTA.CLEAR in)
```

Failure:

```text
[MASTER][OTA] usage: ota.prepare
[MASTER][OTA] remote prepare request failed
```

### `ota.push <local_path> [chunk_bytes<=220]`

Immediate start:

```text
[MASTER][OTA] ota.push started path=<path> size=<bytes> chunk=<chunk> corr=<corr>
[MASTER][OTA] waiting for slave begin status (management scheduler)...
```

Validation / failure:

```text
[MASTER][OTA] usage: ota.push <local_path> [chunk_bytes<=220]
[MASTER][OTA] invalid chunk_bytes (32..220)
[MASTER][OTA] ota.push unavailable (no local OTA storage backend bound)
[MASTER][OTA] ota.push unavailable (management path unavailable)
[MASTER][OTA] ota.push already active
[MASTER][OTA] ota.push stat failed: <reason>
[MASTER][OTA] ota.push invalid file path
[MASTER][OTA] ota.push start request failed
```

Later during transfer:

```text
[MASTER][OTA] begin acknowledged; streaming chunks...
```

or:

```text
[MASTER][OTA] begin acknowledged by slave status; streaming chunks...
```

Possible issue lines:

```text
[MASTER][OTA] nack received offset=<offset> code=0x<code>
[MASTER][OTA] finalize fail code=0x<code> offset=<offset>
```

Terminal success:

```text
[MASTER][OTA] ota.push done bytes=<sent>/<total> chunks=<chunks> elapsed_ms=<ms> path=<path>
```

Terminal failure:

```text
[MASTER][OTA] ota.push failed: <reason> bytes=<sent>/<total> chunks=<chunks> elapsed_ms=<ms>
```

### `ota.push.abort`

When inactive:

```text
[MASTER][OTA] ota.push is not active (sending remote abort anyway)
```

If paired and management abort is sent:

```text
[MASTER][OTA] management abort requested
```

Abort send failure:

```text
[MASTER][OTA] management abort request failed
```

No peer:

```text
[MASTER][OTA] target not selected
```

### `ota.update <path> [chunk_bytes<=220]`

```text
[MASTER][OTA] update pipeline started path=<path> chunk=<chunk>
[MASTER][OTA] management-owned steps: prepare -> push -> apply -> wait boot-complete
```

Validation / failure:

```text
[MASTER][OTA] usage: ota.update <local_path> [chunk_bytes<=220]
[MASTER][OTA] invalid chunk_bytes (32..220)
[MASTER][OTA] target not selected
[MASTER][OTA] update pipeline start failed: <reason>
```

Later pipeline lines can include:

```text
[MASTER][OTA] update pipeline: prepare acknowledged
[MASTER][OTA] ota.push started path=<path> size=<bytes> chunk=<chunk> corr=<corr>
[MASTER][OTA] begin acknowledged by slave status; streaming chunks...
[MASTER][OTA] update pipeline complete
[MASTER][OTA] auto verify queued: desc
```

Failure path can include:

```text
[MASTER][OTA] update pipeline failed: prepare rejected
[MASTER][OTA] prepare note=<text>
[MASTER][OTA] update pipeline failed during push
[MASTER][OTA] update pipeline failed (status=<status>)
```

After slave reboot:

```text
[MASTER][OTA] slave reports update completed after reboot
[MASTER][OTA] slave finalize event received corr=<corr>
```

### `ota.update.master [path]`

Immediate:

```text
[MASTER][OTA] master update requested
```

or:

```text
[MASTER][OTA] master update requested: <message>
```

Validation / failure:

```text
[MASTER][OTA] ota.update.master is local-only
[MASTER][OTA] usage: ota.update.master [local_path]
[MASTER][OTA] master update failed: <reason>
```

### `ota.update.from.arc <id> [chunk_bytes<=220] [master|slave]`

Immediate restore step:

```text
[MASTER][OTA] archive restore complete id=<id> role=<m|s> -> <stage_bin_path>
```

Then either local master flow:

```text
[MASTER][OTA] master update started from archive id=<id> path=<stage_bin_path>
```

or remote slave flow:

```text
[MASTER][OTA] update pipeline started from archive id=<id> path=<stage_bin_path> chunk=<chunk>
[MASTER][OTA] management-owned steps: prepare -> push -> apply -> wait boot-complete
```

Validation / failure:

```text
[MASTER][OTA] usage: ota.update.from.arc <id6hex> [chunk_bytes<=220] [master|slave]
[MASTER][OTA] invalid archive id
[MASTER][OTA] duplicate role argument
[MASTER][OTA] duplicate chunk argument
[MASTER][OTA] invalid chunk_bytes (32..220)
[MASTER][OTA] archive update unavailable (no local OTA storage backend bound)
[MASTER][OTA] archive update failed: storage not ready (<reason>)
[MASTER][OTA] archive update failed: manifest load failed (<reason>)
[MASTER][OTA] archive update failed: id not found (<id>)
[MASTER][OTA] archive update failed: role mismatch id=<id> target=<role> arg_role=<role>
[MASTER][OTA] archive update failed: restore bin failed (<reason>)
[MASTER][OTA] archive update failed: restore metadata failed (<reason>)
[MASTER][OTA] archive update failed: <reason>
[MASTER][OTA] target not selected
[MASTER][OTA] archive update pipeline start failed: <reason>
```

### `ota.arc.save [master|slave]`
### `ota.arc.save.staged [master|slave]`
### `ota.arc.list [master|slave]`
### `ota.arc.verify <id> [role]`
### `ota.arc.restore <id> [role]`
### `ota.arc.delete <id> [role]`
### `ota.arc.clear [master|slave]`

Immediate request:

```text
[MASTER][OTA] archive requested action=<action> role=<m|s>< optional id=<id>>
```

Remote variant:

```text
[MASTER][OTA] archive requested action=<action> role=<m|s>< optional id=<id>>
```

Failure:

```text
[MASTER][OTA] archive request failed
```

Later string result:

```text
[MASTER][OTA][ARC] <message>
```

Later failure string:

```text
[MASTER][OTA][ARC] failed: <message>
```

Archive help:

```text
[MASTER][OTA] archive commands:
  ota.arc.save [master|slave]          (save running firmware)
  ota.arc.save.staged [master|slave]   (save /o/s/fw.bin + fw.json)
  ota.arc.list [master|slave]
  ota.arc.verify <id6hex> [master|slave]
  ota.arc.restore <id6hex> [master|slave]
  ota.arc.delete <id6hex> [master|slave]
  ota.arc.clear [master|slave]
```

### Removed stage command

Command:

- `ota.stage`

Current result:

```text
[MASTER][OTA] ota.stage removed
[MASTER][OTA] place firmware directly in /o/s and use ota.push <name>
```

## 14. Debug And Low-Level Management Traces

These are not primary user-facing result lines, but they do exist in current CLI output depending on log level and status.

### Management response trace

Shown in debug, and also for selected error statuses:

```text
[MASTER][MGMT][RESP] req=<req> cmd=0x<cmd> status=0x<status> req_peer=<mac|-> exec_peer=<mac|-> activated=<yes|no> latency_ms=<ms>
```

### Management event trace

Shown in debug:

```text
[MASTER][MGMT][EVT] id=0x<event_id> cmd=0x<cmd> req=<req> status=0x<status> req_peer=<mac|-> exec_peer=<mac|-> activated=<yes|no> latency_ms=<ms>
```

### Control result frame

When a decoded control result is printed:

```text
[MASTER][CTRL] corr=<corr> from=<mac> cmd=0x<cmd> result=0x<result>
```

## 15. Optimization Notes From This Baseline

This baseline shows the current CLI behavior clearly enough to guide the optimization work.

The important current-state facts are:

1. Some commands already have a clean two-phase contract:
   - request line
   - later rendered result

2. Some commands only expose the request line:
   - pair / unpair / remove
   - set / set.id
   - several push control commands

3. OTA, topology, logger pull, and channel/chain already behave like long-running workflows:
   - immediate acceptance
   - later progress / done / fail lines

4. `cli off` is asymmetric today:
   - no success line
   - only persistence failure is visible

5. For CLI/API parity work, the commands that need the strictest output contract cleanup first are:
   - pairing mutations
   - settings writes
   - push state reads
   - any command where the current CLI only says "requested" without a terminal user-facing result
