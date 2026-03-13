# Profiles Frontend Optimization Structure

## Purpose

Define a precise, frontend-first optimization contract for profile handling across:

- `ICM` (app profile)
- `PMS`, `RELAY`, `SENS`, `SEMU`, `REMU`, `LOCK_ALARM` (library profiles)

This document allows signature refinements for speed and simplicity, but does not add new features or new profile domains.

## Hard Constraint: Feature Freeze

This document is optimization-only.

Allowed:

- metadata enrichment for existing fields
- resolver/indexing improvements
- adapter contract refinements
- deterministic validation and caching rules

Not allowed:

- new profile IDs
- new settings/telemetry/events
- new dynamic child behavior semantics
- new topology roles/capabilities

## Non-Goals

- no new profile IDs
- no new telemetry/setting/event fields
- no new command families
- no frontend-specific business logic in library core

## Optimization Boundary (Must Not Cross)

Every proposed optimization must be expressible using current profile content and current command behavior.

If a proposal requires adding a new field/capability, it is out of scope and must be rejected.

## Current Profile Surfaces

Library (`espnow_link`):

- `kProfilePms` (`PMS`)
- `kProfileRelay` (`RELAY`)
- `kProfileSens` (`SENS`)
- `kProfileSemu` (`SEMU`)
- `kProfileRemu` (`REMU`)
- `kProfileLockAlarm` (`LOCK_ALARM`)

App catalog (`profile_catalog`):

- `kAppProfileIcm` (`0x21`, ICM)
- schema packages and registration helpers (`registerSupportedSlaveMasterSchemas(...)`)

## Optimization Targets

1. Constant-time field resolution by profile+id and profile+key.
2. Zero guesswork parsing in frontend.
3. Deterministic cache invalidation (`revision/hash/generation`).
4. Explicit dynamic child metadata for SEMU/REMU (`vN.*` keys).
5. One contract shape consumed by CLI adapters and frontend API adapters.
6. Master API can consume profile data with minimal orchestration logic.
7. Slave profile behavior for topology-related profiles is explicit and easy to drive.

## Current Implementation Updates

- `ProfileRegistry::registerProfile(...)` now rejects invalid profile field contracts:
  - zero IDs
  - empty keys
  - duplicate ID mappings within telemetry/settings/events
  - duplicate key mappings within telemetry/settings/events
- `ProfileRegistry` now maintains O(1) lookup maps for `profile_id` and `profile_name`.
- `ProfileRegistry` now exposes monotonic `generation()` so frontend caches can invalidate deterministically when profile registrations change.
- `ProfileRegistry` now exposes immutable `snapshot()` with indexed maps for:
  - `profile_id -> profile`
  - `profile_name -> profile`
  - `(profile_id, id)` and `(profile_id, key)` for telemetry/settings/events
- `ProfileRegistry` now exposes strict profile-id resolvers:
  - `resolveTelemetryById/Key(...)`
  - `resolveSettingById/Key(...)`
  - `resolveEventById/Key(...)`
- Free-function profile resolvers now include profile-id overloads for frontend/adapter integration:
  - `findProfileTelemetryById/Key(profile_id, ...)`
  - `findProfileSettingById/Key(profile_id, ...)`
  - `findProfileEventById/Key(profile_id, ...)`
- Pointer-based helper resolvers now route through canonical profile-id snapshot resolvers; separate pointer cache paths were removed.
- CLI capability caching now stores resolved remote `ProfileId` and reuses profile-id routing in child-profile control paths; runtime command paths do not consume profile-name cache.
- Descriptor schema cache now keys by `ProfileId` and invalidates on registry `generation` changes; pointer-keyed descriptor schema cache path is removed.
- Capabilities contract now emits canonical `profile_id` metadata for profile-bearing nodes, enabling direct ID-based profile resolution.
- App-owned slave providers (`PMS/RELAY/SENS/SEMU/REMU`) now use runtime-only telemetry snapshot generation; synthetic fallback telemetry branches and toggle flags are removed.

## Frontend-First Contract

The frontend should consume one immutable snapshot shape, not reconstruct profile semantics from descriptor text.

### Canonical Types

```cpp
struct ProfileSchemaMeta {
  uint16_t revision = 1;
  uint32_t hash = 0;
  const char* family = ""; // library|app
};

struct DynamicScopeMeta {
  bool enabled = false;
  uint8_t min_index = 0;
  uint8_t max_index = 0;
  const char* key_template = ""; // example: "v{index}.tf_far_mm"
  const char* id_rule = "";      // documented rule used already by profile definition
};

struct FieldCommonMeta {
  uint16_t id = 0;
  const char* key = "";
  const char* label = "";
  const char* unit = "";
  const char* group = ""; // ui grouping hint, no behavior change
};

struct SettingFieldMeta {
  FieldCommonMeta common{};
  SettingValueType type = SettingValueType::String;
  bool writable = false;
  const char* enum_values = ""; // optional csv metadata
  float min_value = 0.0f;
  float max_value = 0.0f;
  DynamicScopeMeta dynamic{};
};

struct TelemetryFieldMeta {
  FieldCommonMeta common{};
  float min_value = 0.0f;
  float max_value = 0.0f;
  DynamicScopeMeta dynamic{};
};

struct EventFieldMeta {
  FieldCommonMeta common{};
  DynamicScopeMeta dynamic{};
};

struct ProfileFrontendContract {
  uint8_t profile_id = 0;
  const char* profile_name = "";
  ProfileSchemaMeta schema{};
  const char* codec_default = "";
  std::vector<SettingFieldMeta> settings{};
  std::vector<TelemetryFieldMeta> telemetry{};
  std::vector<EventFieldMeta> events{};
};
```

Rules:

- no field appears without both `id` and `key`
- `profile_id + id` and `profile_id + key` must map to exactly one field
- `dynamic.enabled=true` must include index bounds and template
- metadata is descriptive only; it does not change runtime behavior

## Snapshot Model

Build one read-only snapshot after profile registration.

### Required Indexes

- `profile_id -> ProfileFrontendContract`
- `(profile_id, setting_id) -> SettingFieldMeta`
- `(profile_id, setting_key) -> SettingFieldMeta`
- `(profile_id, metric_id) -> TelemetryFieldMeta`
- `(profile_id, metric_key) -> TelemetryFieldMeta`
- `(profile_id, event_id) -> EventFieldMeta`
- `(profile_id, event_key) -> EventFieldMeta`

### Snapshot API

```cpp
struct ProfileRegistrySnapshot {
  uint32_t generation = 0;
  std::unordered_map<uint8_t, ProfileFrontendContract> profiles;
  // plus indexed maps for O(1) lookups
};

const ProfileRegistrySnapshot& snapshot() const;
```

Frontend contract:

- fetch snapshot once
- retain `generation`
- rebuild frontend cache only when `generation` changes

## Signature Refinements (No New Features)

These are additive refinements for determinism and frontend simplicity.

### 1) Enrich `IProfileDefinition`

Add explicit schema metadata and optional rich field metadata.

```cpp
virtual ProfileSchemaMeta schemaMeta() const = 0;
virtual bool settingMetaGet(uint16_t id, SettingFieldMeta& out) const = 0;
virtual bool telemetryMetaGet(uint16_t id, TelemetryFieldMeta& out) const = 0;
virtual bool eventMetaGet(uint16_t id, EventFieldMeta& out) const = 0;
```

If a profile does not override rich metadata yet, adapters may fill from existing descriptor fields and keep current behavior.

### 2) Add Snapshot Builder

```cpp
bool buildFrontendSnapshot(ProfileRegistrySnapshot& out, std::string* error = nullptr);
```

Behavior:

- returns false only on deterministic contract breakage (duplicate key/id mapping, invalid dynamic range)

### 3) Add Frontend Resolver Helpers

```cpp
bool resolveSettingByKey(uint8_t profile_id, const std::string& key, SettingFieldMeta& out) const;
bool resolveSettingById(uint8_t profile_id, uint16_t id, SettingFieldMeta& out) const;
bool resolveTelemetryByKey(uint8_t profile_id, const std::string& key, TelemetryFieldMeta& out) const;
bool resolveTelemetryById(uint8_t profile_id, uint16_t id, TelemetryFieldMeta& out) const;
```

These helpers must use snapshot indexes only, no repeated scans.

## Dynamic Child-Scope Contract

For `SEMU` and `REMU`:

- every `vN.*` field must expose `dynamic` metadata
- parser and generator must share the same template rule
- frontend must not use custom regex per field family

Normalization requirement:

- resolve `vN.<suffix>` to base field metadata + concrete `N`
- enforce index bounds from metadata (`min_index..max_index`)

## Master API Profile Consumption Contract

Master-side frontend API should consume profile information through a single snapshot-driven path.

Required master read models:

- `PairedPeersView` for peer list
- `PeerDescriptorBundleView` for `desc/caps/settings/telemetry`
- `TelemetryNowView` for live samples
- `ProfileFieldResolveView` for key/id resolution

Required behavior:

- profile resolution must not parse descriptor free text
- profile resolution must use snapshot indexes only
- `settings/get/set` and `telem` adapters use same resolver layer
- adapters must map to existing profile fields only

This keeps paired list, descriptors, telemetry, and push control easy to integrate in frontend API code.

## Slave-Side Simplicity Contract

Slave behavior must remain feature-identical, but easier to consume and reason about from master/frontend.

### Required Slave Profile Metadata

Each slave profile exposes explicit topology/runtime metadata:

```cpp
enum class TopologyRoleKind : uint8_t {
  None = 0,
  Sensor,
  Relay,
  SemuChild,
  RemuChild
};

struct SlaveProfileRuntimeMeta {
  TopologyRoleKind role = TopologyRoleKind::None;
  bool topology_participates = false;
  bool telemetry_push_supported = true;
  bool dynamic_children = false;
  uint8_t max_children = 0;
};
```

This is metadata only and does not add capabilities.

### Profile Role Matrix (Topology-Focused)

- `SENS`: `role=Sensor`, static fields, no dynamic children.
- `RELAY`: `role=Relay`, static fields, no dynamic children.
- `SEMU`: `role=SemuChild`, dynamic children enabled, bounded index range.
- `REMU`: `role=RemuChild`, dynamic children enabled, bounded index range.
- `PMS`: explicit non-topology role metadata if not participating in chain topology.
- `LOCK_ALARM`: explicit non-topology role metadata unless topology participation is already implemented.

### Slave Optimization Rules

1. Precompute key/id lookup maps during profile registration.
2. Precompute dynamic child templates for SEMU/REMU.
3. Keep topology role metadata immutable at runtime.
4. Avoid runtime regex parsing for `vN.*` on critical paths.
5. Expose consistent writable/type metadata for all slave settings.
6. Do not add new runtime profile behavior.

## Topology + Profile Convergence Rules

For topology operations involving `SEMU`, `REMU`, `SENS`, and `RELAY`:

- role metadata drives frontend rendering and validation
- invalid role/operation combinations fail fast before submission
- same role interpretation must be used by CLI and API adapters
- no profile-specific hidden topology behavior in one surface only

## ICM Optimization Contract

ICM remains the same feature set; optimization is in access and grouping only.

Required metadata on each setting:

- stable `group` value for UI sections
- stable `label` for UI
- numeric range when applicable
- enum metadata when applicable

Recommended ICM groups (metadata only):

- `core`
- `pairing_topology`
- `network`
- `security`
- `notification`
- `orchestration`

Frontend usage pattern:

1. render hot screen using snapshot group filters
2. request detailed values by existing API/CLI paths
3. lazy-render less-used groups when opened

## CLI/API Parity Requirements (Profile Domain)

For each profile field:

- same `id <-> key` mapping in CLI and API paths
- same type and writable contract
- same readback convergence behavior after write
- same validation bounds for numeric fields
- same topology-role interpretation for topology-related profiles

No CLI-only or API-only profile interpretation path is allowed.

## Compliance Gate: No Feature Addition

For each optimization item, include:

1. existing profile fields used
2. existing dynamic child rules used
3. what changed only in metadata/indexing/adapter orchestration

If item (1) or (2) cannot be satisfied with current profile definitions, the change is rejected.

## Validation Matrix

### Contract Integrity

- no duplicate `(profile_id, key)`
- no duplicate `(profile_id, id)`
- dynamic fields have valid bounds and templates
- schema metadata present for all profiles

### Resolution Correctness

- `resolveByKey` and `resolveById` return same field identity
- child-scoped keys resolve correctly at min/max boundaries
- invalid child index fails fast
- topology role metadata resolves identically for CLI and API adapters

### Frontend Simplicity

- settings page renders from snapshot metadata only
- no profile-specific hardcoded parsing branches
- topology UI logic uses role metadata, not ad-hoc profile condition trees

### Parity

- CLI `get/set` and API read/write resolve same field contracts
- no divergence in writable/type constraints
- topology and push flows use the same profile-role and dynamic-child constraints

## Migration Plan

### Phase 1 (Canonical Snapshot Base)

- add metadata and snapshot APIs
- remove legacy field-access entrypoints from active adapter paths

### Phase 2 (Adapter Switch)

- move frontend adapters and CLI helper paths to snapshot-based resolvers
- remove fallback wrappers

### Phase 3 (Hardening)

- reject registry build on duplicate/ambiguous mappings
- deprecate guesswork parsing paths in adapters

## Acceptance Criteria

- frontend field lookup path is O(1) by key/id
- zero ambiguous profile field mappings
- deterministic cache invalidation by `generation` and schema metadata
- dynamic child fields handled without per-profile regex logic
- no feature-domain changes introduced
- master API can consume paired/descriptors/telemetry/profile resolution with one snapshot-driven layer
- topology-related slave profiles (`SEMU/REMU/SENS/RELAY`) are explicit via role metadata and produce consistent adapter behavior
- every optimization item is traceable to existing profile content without adding capabilities

## Code Anchors

- `include/espnow_link/profile.hpp`
- `src/descriptor/profile.cpp`
- `profile_catalog/include/profile_catalog/shared/profile_ids.hpp`
- `profile_catalog/include/profile_catalog/shared/schema_package.hpp`
- `profile_catalog/include/profile_catalog/masters/icm/icm_profile.hpp`
- `profile_catalog/src/registry/slave_schema_registry.cpp`
