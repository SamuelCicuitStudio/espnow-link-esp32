# Telemetry

## Pull Path

Telemetry read commands:

- `TelemSchemaGet`
- `TelemSchemaPageGet`
- `TelemPull`

Responses decode into typed descriptor/sample payloads.

CLI pull helpers:

- `telem.now` for current snapshot
- `telem.now.child <vid>` for child-scoped view on `SEMU`/`REMU` (+ global metrics)

## Push Path

Push control commands:

- `PushStart`
- `PushUpdate`
- `PushPause`
- `PushResume`
- `PushStop`
- `PushGet`

Push command envelope is `TelemetryPushCommand` with action + config.

CLI push helpers:

- `push.start|update|pause|resume|stop|get`
- `push.one` / `push.id`
- `push.child.start` / `push.child.stop` (`SEMU` and `REMU` only)

## Push Configuration

Global stream fields:

- `stream_id`
- `enabled`
- `mode`
- `interval_ms`
- `min_report_gap_ms`

Per-metric fields (`TelemetryPushMetricConfig`):

- metric key or metric index identity
- enabled
- mode
- interval/min gap
- optional threshold (`delta_abs`)

Modes:

- `Periodic`
- `OnChange`
- `Hybrid`

## Push Validation (Manager-Enforced)

For `Start`/`Update` actions:

- stream interval must be `200..60000 ms`
- stream min gap must be `50..60000 ms`
- metric count must be `<= 16`
- metrics must resolve to known profile telemetry keys/IDs
- duplicate keys are rejected
- all-disabled metric sets are rejected

## Frontend Helper Coverage

`ManagementFrontendAdapter` includes typed helpers for common child-stream management (SEMU/REMU patterns) without CLI text parsing.

## Code Anchors

- `include/espnow_link/telemetry_push.hpp`
- `src/descriptor/telemetry_push.cpp`
- `include/espnow_link/management_controller.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`
