# Master-Wide CLI/API Separation Optimization Plan

Date: 2026-03-17
Scope: library only (`espnow-link`) across all master integrations (bootstrap and custom).

## 1) Goal

Make CLI output/processing source-correct for every master runtime shape:

- CLI should process/print only CLI-owned control lifecycle by default.
- Non-CLI control surfaces (`Wifi/Ble/Custom`) must not trigger CLI command output.
- No new command IDs, no new profile fields, no feature expansion.

This is a control-plane optimization only.

## 2) Alignment With Execution Goal Lock

This plan remains phase-1 control convergence and respects hard constraints:

- no new command IDs
- no new profile IDs
- no new telemetry/settings/events schema
- no new topology/OTA semantics

Only routing, ownership filtering, and runtime orchestration are refined.

## 3) Library Scan Findings (Why Leak Exists)

## 3.1 Dual processing paths in current master runtime

Current `MasterNodeRuntime::tick(...)` order:

1. `manager->tick(...)`
2. `management->tick(...)`
3. `cli->tick(...)`

So manager callbacks can hit CLI before management queue lifecycle is processed.

## 3.2 Path A (source-aware, good)

`ManagementRuntime` routes by `ManagementSource`:

- response: only transports where `transport.source() == response.source`
- event: same, except broadcast when `event.source == Unknown`

This path is already typed and source-isolated.

## 3.3 Path B (source-blind leak)

`MasterCli` is also wired as global observer:

- `ControlPlaneForwarder -> MasterCli::onPullResponse(...)`
- `MultiEventSink -> MasterCli::onEvent(...)`

These callbacks do not carry `ManagementSource` and currently decode/print traffic regardless of origin.

## 3.4 Resulting duplication

When API submits pull/control work:

- CLI can still decode/print through observer callbacks.
- CLI can also process queue mailbox events in `pumpManagementMailbox()`.

This creates the user-visible "API action still prints in CLI" problem and unnecessary decode/format work.

## 4) Non-Obvious Edge Cases (Must Be Handled)

## 4.1 Broadcast management events

`ManagementService::onEvent(...)` emits many events with `source = Unknown` (broadcast):

- discovery updates
- pair progress/result
- OTA transfer lifecycle/status
- topology trigger notifications

These broadcast events can reach CLI mailbox even when action was initiated by API.

## 4.2 CLI direct pull flows (not purely management-queue)

CLI still has direct pull-driven workflows (`onPullResponse` consumption), including:

- descriptor rendering
- autopull/liveness probes
- parts of OTA/logger flows

So we cannot simply "disable all observer callbacks" without ownership gating.

## 4.3 Request-ID namespace collision risk

`ManagementController` default `next_req_id` starts at `1` per controller instance.

Different sources can reuse the same `req_id`. Since observer pull responses are source-blind, req-id-only ownership filters can misattribute responses if ranges overlap.

## 5) Master Integration Shapes Covered

Fix must work for all master usage patterns:

1. `MasterNodeBootstrap` with CLI only
2. `MasterNodeBootstrap` with CLI + WiFi adapter
3. `MasterNodeBootstrap` with CLI + multiple adapters (`Wifi/Ble/Custom`)
4. Custom runtime wiring (manual manager/runtime/controller graph)
5. Headless API master (no CLI bound)

## 6) Target Contract

1. CLI command text/log output is source-scoped by default.
2. API polling/mutations do not produce CLI command rendering.
3. CLI-owned commands remain fully functional.
4. Legacy observer behavior remains opt-in for debugging/compatibility.

## 7) Optimized Separation Design

## 7.1 Add explicit CLI observation policy

```cpp
enum class CliTrafficPolicy : uint8_t {
  Auto = 0,            // default
  ManagementOnly = 1,  // strict source separation
  LegacyObserver = 2   // old observer behavior
};
```

Policy behavior:

- `Auto`: `ManagementOnly` when CLI management transport exists; else `LegacyObserver`.
- `ManagementOnly`: enforce ownership filtering; observer prints suppressed unless owned.
- `LegacyObserver`: preserve current behavior.

## 7.2 Ownership-aware filtering (core)

Introduce a CLI-owned request tracker (req/corr ownership table with TTL) used by both paths.

Rules:

- Mailbox responses/events:
  - accept if `source == Cli`
  - for `source == Unknown`, accept only if `req_id` belongs to CLI-owned active set
  - otherwise ignore
- Observer pull responses/events:
  - process only if correlation belongs to CLI-owned active set (or explicit CLI local flow)
  - otherwise fast-ignore (no decode/print)

This removes API-origin rendering while preserving CLI workflows.

## 7.3 Preserve ambient local diagnostics safely

Some CLI local features (discovery window, local queue status, optional diagnostics) are not tied to API requests.

Allowlist only required ambient paths under `ManagementOnly`:

- local discovery collection while list window is active
- explicit local diagnostic commands

Everything else should be ownership-gated.

## 7.4 Request-ID partitioning (recommended hardening)

To avoid cross-source req-id ambiguity, assign default req-id bands per source (library default, override allowed):

- CLI: high range
- WiFi: mid range
- BLE/Custom: separate ranges

No wire/protocol change; just safer local id generation policy.

## 8) Implementation Anchors

Primary code locations:

- `include/espnow_link/cli_master.hpp`
- `src/cli/cli_master.cpp`
- `src/cli/cli_dispatch.cpp` (`pumpManagementMailbox`, status output)
- `include/espnow_link/master_runtime_defaults.hpp`
- `src/runtime/master_runtime_defaults.cpp` (policy wiring defaults)
- `include/espnow_link/management_controller.hpp` (+ default req-id partition hooks if added)

Contract docs to sync:

- `docs/current/cli.md`
- `docs/current/frontend-api.md`

## 9) Validation Matrix

### A) Bootstrap + CLI only

- `Auto` falls back to legacy observer behavior.
- no regressions in current CLI command rendering.

### B) Bootstrap + CLI + WiFi

- API polling/mutations produce zero CLI command-text output.
- CLI commands still render normal lifecycle/output.

### C) Multi-adapter (`Wifi/Ble/Custom`)

- no cross-source CLI leakage.
- queue/runtime stats remain stable.

### D) Edge validation

- OTA lifecycle events: CLI sees only CLI-owned flow.
- discovery broadcast events: only local list-window handling remains.
- req-id collision simulation: no false-positive CLI rendering when partitioning enabled.

## 10) Acceptance Criteria

1. Zero non-CLI command rendering in CLI output under `Auto` with queue-backed CLI transport.
2. Zero regressions for CLI-only legacy masters.
3. No command ID/payload/profile contract changes.
4. CLI/API lifecycle parity remains deterministic (`accepted -> terminal`).
5. No frontend dependency on CLI text parsing.

## 11) Risks and Mitigations

Risk: some existing logs rely on observer-side prints.

Mitigation:

- keep `LegacyObserver`
- default to `Auto`
- expose ignored/accepted counters in CLI status for runtime tuning

Risk: ownership misclassification when req-id overlaps.

Mitigation:

- request ownership table with TTL
- optional source req-id partition defaults

## 12) Rollout Sequence

1. Add policy enum + bootstrap config plumbing (`Auto` default).
2. Add CLI-owned request tracking table.
3. Apply filtering in observer callbacks and mailbox pump.
4. Add diagnostics counters (ignored vs processed).
5. Run matrix validation.
6. Update docs and close phase-1 item.

## 13) Out-Of-Scope (Future Feature)

- new management event families for presentation only
- new profile/settings/telemetry fields
- transport-specific behavior forks beyond source/ownership policy
