# Descriptors and Settings

## Descriptor Domains

Descriptor pull domains handled by the library:

- device descriptor
- capabilities
- telemetry schema
- telemetry snapshot
- settings schema/values
- liveness
- time
- storage status/list/stat
- OTA status/manifest/capacity/gate

## Paged Descriptor Commands

Paged management commands:

- `CapsPageGet`
- `TelemSchemaPageGet`
- `SettingsPageGet`
- `OtaManifestPageGet`

Paging is deterministic and uses cursor + page size.

## CLI Render (Settings)

Current CLI rendering for settings uses normalized header and sectioned tables:

```text
[MASTER][SET] Settings snapshot=<id> source=<source> total=<n>
```

Each section prints table columns:

- `ID`
- `Key`
- `Value`
- `Default`
- `Type`
- `RW`
- `Range / Notes`

This is render-only formatting. Underlying descriptor payload fields are unchanged.

## Setting Operations

Supported setting commands:

- `SettingGet` by key
- `SettingGet` by id
- `SettingSet` by key
- `SettingSet` by id

`SettingDescriptor` includes `setting_id`, `key`, `value_type`, `writable`, `current_value`, and `default_value`.

## Resolved Value Rule

Resolved setting value for UI/control should be interpreted as:

- `current_value` when present
- else `default_value`

`ManagementFrontendAdapter` exposes resolved helpers for this behavior.

## Recommended Write Convergence

For strict state convergence:

1. send `SettingSet`
2. wait for terminal response/event state
3. read back with `SettingGet` or `SettingsGet`
4. accept only if readback matches expected value

## Profile Alignment

Stable setting ids/keys come from profile definitions and provider schema. Keep profile registry and descriptor provider aligned to avoid ambiguous key/id mapping.

## Code Anchors

- `include/espnow_link/descriptor.hpp`
- `src/descriptor/descriptor.cpp`
- `include/espnow_link/management_controller.hpp`
- `include/espnow_link/management_frontend_adapter.hpp`
