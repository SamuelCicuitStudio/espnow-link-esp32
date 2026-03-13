# Telemetry

## Pull Path

Telemetry read commands:

- `TelemSchemaGet`
- `TelemSchemaPageGet`
- `TelemPull`

Responses decode into typed descriptor/sample payloads.

## Push Path

Push control commands:

- `PushStart`
- `PushUpdate`
- `PushPause`
- `PushResume`
- `PushStop`
- `PushGet`

Push command envelope is `TelemetryPushCommand` with action + config.

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

## Frontend Helper Coverage

`ManagementFrontendAdapter` includes typed helpers for common child-stream management (SEMU/REMU patterns) without CLI text parsing.

## Code Anchors

- `include/espnow_link/telemetry_push.hpp`
- `src/descriptor/telemetry_push.cpp`
- `include/espnow_link/management_controller.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`
