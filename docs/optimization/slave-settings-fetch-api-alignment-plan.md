# Slave Settings Fetch API Alignment Plan (One-Request Bundle)

Status note (2026-03-18): this plan remains relevant for transport-level NodeBundle alignment.
For end-to-end cache policy and changed-only write behavior, use
`slave-settings-cache-policy-and-delta-write-fix-plan.md` as primary.

Date: 2026-03-17  
Scope: `espnow-link-esp32` library only (no ICM app code changes in this phase).

## 1) Execution Goal

For API consumers (ICM web/backend), one slave snapshot must be:

1. **one request from master to slave**, and  
2. **one logical bundled result returned to API caller** (even if transport must stream chunks internally).

Per-key/per-command flows stay for CLI/manual workflows.

## 2) Verified Codebase Facts (Precise)

## 2.1 Existing "bundle" helpers are still multi-command fan-out

- `descriptorBundleGet(...)` in `management_frontend_adapter.hpp` sends:
  - `DescGet`
  - `CapsGet`
  - `SettingsGet`
  - `TelemSchemaGet`
- `nodeSnapshotGet(...)` sends:
  - `DescGet`
  - optional `LiveGet`
  - optional `TimeGet`
  - `SettingsGet`
  - optional `TelemPull`

So current "bundle" at adapter level is not one slave request.

## 2.2 Settings path today still depends on settle/cache timing

- `settingsGetResolved(...)` runs `SettingsGet`, then checks cache freshness.
- If cache not fresh, it loops `runtime_->tick(...) + drainToCache(...)` up to capped settle timeout.

This explains accepted-but-empty windows seen by ICM routes.

## 2.3 Wire payload hard limit is strict

- `ProtocolCodec::kMaxPayload = 235` bytes (`protocol.hpp` / `protocol.cpp`).
- `encodeDescriptorResponse(...)` truncates payload when data does not fit and can emit `"truncated"` message.
- Paging exists for `GetSettings`, `PullTelemetry`, etc., but paging currently requires additional requests.

## 2.4 Current management service expects one response per request-id

- `ManagementService::onPullResponse(...)` finds pending request by `(req_id, peer)`, then erases it on first response.
- This blocks multi-response aggregation under a single request-id today.

## 2.5 Command/query enums currently have no node-bundle command

- `ManagementCommandId` has `DescGet`, `SettingsGet`, `TelemPull`, etc., but no node-bundle command.
- `DescriptorQueryType`/`DescriptorResponseType` have no dedicated one-shot bundle type.

## 3) Required Direction (What we are building)

Add a **new API-only bundled path** that performs one slave request and returns one logical bundle to caller.

We do **not** remove existing commands.

## 4) Protocol/Runtime Design (Bundle V1)

## 4.1 New command and query/response types

- Add `ManagementCommandId::NodeBundleGet` (new ID after existing range).
- Add `DescriptorQueryType::GetNodeBundle`.
- Add `DescriptorResponseType::NodeBundle` (or equivalent dedicated bundle response type).

## 4.2 Bundle request payload (mask-based)

Bundle query carries compact flags:

- `include_device`
- `include_liveness`
- `include_time`
- `include_settings`
- `include_telemetry`
- optional `settings_only` / `telemetry_only` mode bits

This keeps one command flexible for PMS/RELAY/SENS/SEMU/REMU and future profiles.

## 4.3 Bundle response shape (UI-ready)

One canonical payload object must include:

- identity (`profile_id`, `profile_name`, device fields)
- optional liveness/time
- telemetry samples
- settings list (`setting_id`, `key`, `value_type`, `writable`, `current_value`, `default_value`, `description`)

## 4.4 Handling >235B safely without new request loops

A single request may need multiple response frames.  
Implementation rule:

- slave may emit multiple pull-responses with the same correlation ID for this command
- service aggregates chunks internally
- API caller sees one final logical response

This satisfies user requirement (one request to slave) while respecting ESP-NOW payload limits.

## 5) Library Changes Needed (File-Accurate)

## 5.1 Command/contract additions

- `include/espnow_link/management_types.hpp`
  - add `NodeBundleGet`
- `include/espnow_link/descriptor.hpp`
  - add `GetNodeBundle`
  - add `NodeBundle` response type
  - add bundle query/response metadata fields needed for chunking/finalization

## 5.2 Descriptor codec and handler

- `src/descriptor/descriptor.cpp`
  - encode/decode new query fields
  - handle `GetNodeBundle` in `handleDescriptorQuery(...)`
  - encode/decode bundle response payload
  - chunk-aware serialization path for large settings/telemetry sets

## 5.3 Pull client + service routing

- `include/espnow_link/master_pull_client.hpp`
- `src/descriptor/master_pull_client.cpp`
  - add `requestNodeBundle(...)`

- `src/management/management_service.cpp`
  - map `NodeBundleGet` in `runDescriptorPull(...)`
  - update access/priority/timeout metadata
  - change `onPullResponse(...)` to support multi-response aggregation for bundle command

- `include/espnow_link/management_service.hpp`
  - extend `PendingDescriptorPull` with aggregation state

## 5.4 Controller + adapter API surface

- `include/espnow_link/management_controller.hpp/.cpp`
  - add `nodeBundleGet(...)` submit wrapper

- `include/espnow_link/management_frontend_adapter.hpp`
  - add API-first helpers:
    - `nodeBundleGet(...)`
    - `settingsBundleGet(...)` (settings-focused view built from node bundle)
    - `settingsBundleRefresh(...)`
  - these must use the new single request command path, not fan-out.

## 6) Profile Impact

No profile schema rewrite is required for this feature.

Profile changes only happen if a slave provider is missing data in:

- `getSettings(...)`
- `getTelemetrySnapshot(...)`
- `getDeviceDescriptor(...)`

Primary work is in management/descriptor transport and adapter APIs, not in `profile_catalog/*`.

## 7) Backward Compatibility

- Keep all existing commands/functions.
- New bundle path is additive.
- For older slaves without bundle support:
  - return explicit `UnsupportedCommand`
  - optional fallback to legacy fan-out can remain as compatibility mode (disabled by default for API-optimized path).

## 8) Performance Rules (Embedded-Focused)

- No JSON in radio payload; keep compact binary descriptor codecs.
- Avoid duplicated decode/encode passes when aggregating chunks.
- Bound memory in aggregation state (stream/merge, avoid giant temporary copies).
- Keep one terminal response to API caller; intermediate chunk statuses are internal (`OkDeferred` behavior).

## 9) Validation Criteria

1. One API call triggers one slave request (`NodeBundleGet`) for PMS.
2. PMS settings overlay populates fully without extra pull requests.
3. Same flow works for REMU/RELAY/SENS/SEMU.
4. No `"settings_not_ready"` race from accepted-but-empty state on bundle path.
5. CLI per-key commands remain unchanged.

## 10) Expected Outcome

- Deterministic settings/telemetry population from API.
- Elimination of multi-request choreography in ICM for slave snapshots.
- Cleaner split: CLI remains human/manual, API becomes optimized machine path.

## 11) Implementation Status (Library, 2026-03-17)

Implemented in codebase:

- Added `NodeBundleGet` command path (management + descriptor query/response).
- Added bundle mask contract (`device/liveness/time/settings/telemetry`) and wire tags.
- Added `MasterPullClient::requestNodeBundle(...)`.
- Added `ManagementService` routing for `NodeBundleGet`.
- Added pending pull handling for multi-response bundle completion (`OkDeferred` until final `done` page).
- Added adapter-side ingestion for `DescriptorResponseType::NodeBundle`.
- Updated adapter settings/snapshot helpers to prefer NodeBundle API path with legacy fallback.
- Added adapter API helpers:
  - `nodeBundleGet(peer, mask, ...)`
  - `settingsBundleRefresh(peer, ...)`
  - `settingsBundleGet(peer, ...)`
- Added slave runtime streaming for `GetNodeBundle`:
  - one inbound request
  - multiple outbound pull responses with same `corr_id` when paged
  - settings-first follow-up pages to improve throughput
  - guardrails for stalled/non-progress paging

Remaining integration work (outside this library phase):

- ICM-side API route wiring and UI dialog consumption updates.
- App-role updates for slaves if any provider lacks full settings/telemetry fields.
