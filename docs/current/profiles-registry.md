# Profiles and Registry

## Registry Contract

`ProfileRegistry` holds static `IProfileDefinition` entries used by runtime descriptor/control surfaces.

Primary operations:

- `registerProfile(...)`
- `find(profile_id)`
- `findByName(name)`
- `list()`

Helper lookups map metric/setting/event keys and IDs:

- `findProfileTelemetryById/ByKey`
- `findProfileSettingById/ByKey`
- `findProfileEventById/ByKey`

## Built-in Profiles

Built-ins registered by `registerBuiltInProfiles(...)`:

- `kProfilePms` (`1`) -> `PMS`
- `kProfileRelay` (`2`) -> `RELAY`
- `kProfileSens` (`3`) -> `SENS`
- `kProfileSemu` (`4`) -> `SEMU`
- `kProfileRemu` (`5`) -> `REMU`
- `kProfileLockAlarm` (`6`) -> `LOCK_ALARM`

Each built-in profile defines telemetry metric IDs/keys, setting IDs/keys, and event IDs/keys.

## Current Profile Key Contracts (Highlights)

### PMS

- Telemetry keys:
  - `wallv`, `battv`, `walli`, `batti`, `psrc`, `trip`, `rcut`
- Settings include:
  - power and safety: `chain_48v_enable`, `charger_enable`, `tripi`
  - calibration: `vcal`, `ical`
  - protection thresholds: `vbovp`, `vbuvp`, `ibocp`, `baovp`, `bauvp`, `biocp`
- Maps:
  - `PCAT_PMS_SETMAP`
  - `PCAT_PMS_METMAP`
  - `PCAT_PMS_EVMAP`

### RELAY

- Global output persistence toggle:
  - setting key `persist_output_state`
  - NVS key `opers`
- Core telemetry:
  - `relay_bitmap`, `uptime_ms`, `env_temp_c`
- Maps:
  - `PCAT_RELAY_SETMAP`
  - `PCAT_RELAY_METMAP`
  - `PCAT_RELAY_EVMAP`

### REMU

- Global output persistence toggle:
  - setting key `persist_output_state`
  - NVS key `opers`
- Child setting namespace:
  - `v<0..15>.<field>`
  - includes `v<vid>.output_enable`
- Core telemetry:
  - `relay_bitmap`, `relay_count`, `env_temp_c`, `uptime_ms`
  - child scoped `v<vid>.relay_bitmap`
- Maps:
  - `PCAT_REMU_SETMAP`
  - `PCAT_REMU_METMAP`
  - `PCAT_REMU_EVMAP`

### SENS

- Added/active detection-calibration settings:
  - `detect_fall_delta_cm`
  - `detect_release_delta_cm`
  - `ab_spacing_cm`
  - `tfl_a_calib_mm`
  - `tfl_b_calib_mm`
  - `detect_window_ms`
  - `detect_clear_hold_ms`
- Added global sampling settings:
  - `sample_loop_ms`
  - `sample_ring_n`
- TF-Luna telemetry includes distance + flux + temperature:
  - `tfl_a_mm`, `tfl_b_mm`, `tfl_a_flux`, `tfl_b_flux`, `tfl_a_temp_c`, `tfl_b_temp_c`
- Maps:
  - `PCAT_SENS_SETMAP`
  - `PCAT_SENS_METMAP`
  - `PCAT_SENS_EVMAP`

### SEMU

- Child setting namespace:
  - `v<0..7>.<field>`
- Per-child calibration/detection model includes:
  - `detect_fall_delta_cm`
  - `detect_release_delta_cm`
  - `ab_spacing_cm`
  - `tfl_a_calib_mm`
  - `tfl_b_calib_mm`
  - `detect_window_ms`
  - `detect_clear_hold_ms`
- Added global sampling settings:
  - `sample_loop_ms`
  - `sample_ring_n`
- TF-Luna telemetry exposed per child:
  - `v<vid>.tfl_a_mm`, `v<vid>.tfl_b_mm`
  - `v<vid>.tfl_a_flux`, `v<vid>.tfl_b_flux`
  - `v<vid>.tfl_a_temp_c`, `v<vid>.tfl_b_temp_c`
- Maps:
  - `PCAT_SEMU_SETMAP`
  - `PCAT_SEMU_METMAP`
  - `PCAT_SEMU_EVMAP`

## Notes on Legacy Aliases

`SENS` and `SEMU` keep legacy alias constants for transition (`lidar.provision.*`), but strict profile surfaces are defined by the active `SETMAP`/`METMAP` contracts above.

## Registration Rules

- null/unknown-id profiles are rejected
- empty profile names are rejected
- exact same profile pointer re-registration is idempotent
- duplicate profile id with different profile instance is rejected
- duplicate profile name with different profile instance is rejected

Registry loading is explicit: callers register profiles (app-owned and/or built-in) before runtime bootstrap.

## Codec Association

Each profile declares `defaultCodecId()` and `supportsCodec(...)`. Built-ins support built-in codec IDs.

## Code Anchors

- `include/espnow_link/profile.hpp`
- `src/descriptor/profile.cpp`
- `include/espnow_link/codec.hpp`
- `profile_catalog/include/profile_catalog/slaves/*/*_keys.hpp`
- `profile_catalog/src/slaves/*/*_profile.cpp`
