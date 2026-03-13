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
