# Profile-Linked CLI/API Response Contract Plan

Status: Draft for implementation  
Scope: Current codebase only (`lib/espnow-link-esp32` + role adapters in `src/App/roles`)  
Primary issue: Profile-related commands apply correctly but return confusing/misaligned responses  
Rule: No feature added, no feature removed; response quality/consistency optimization only

---

## 1) Objective

Make profile-related command responses deterministic, user-meaningful, and owned by profile logic (PMS/SEMU/REMU/SENS/RELAY), not by generic library fallbacks.

This plan targets:

- `set <key>=<value>`
- `set.id <id>=<value>`
- `get <key>`
- `get.id <id>`
- profile control actions that currently reuse descriptor ack/error text

---

## 2) Scope Lock (Mandatory)

- Do not change command surface.
- Do not add new profile settings/commands.
- Do not remove existing settings/commands.
- Do not alter transport protocol format unless unavoidable.
- Keep current master/slave workflow and targeting model.
- Only optimize response semantics and consistency.

---

## 3) Current Behavior (Code-Based Baseline)

## 3.1 Descriptor layer currently overrides useful profile errors

File:

- `lib/espnow-link-esp32/src/descriptor/descriptor.cpp`

Current behavior:

- In `DescriptorQueryType::SetSetting` (by key), if set fails and key exists in profile map, message is forced to `"setting write unavailable"`.
- This can hide real provider/apply failure details.

Code reference:

- `descriptor.cpp:971-980`

Impact:

- User sees generic error while setting may have been persisted or may have failed for a specific reason.

## 3.2 Persist-then-apply model can produce "value changed + error" scenarios

Files:

- `profile_catalog/src/slaves/pms/pms_profile.cpp:983-1013`
- `profile_catalog/src/slaves/semu/semu_profile.cpp:977-1007`
- `profile_catalog/src/slaves/remu/remu_profile.cpp:789-818`
- `profile_catalog/src/slaves/sens/sens_profile.cpp:717-747`
- `profile_catalog/src/slaves/relay/relay_profile.cpp:546-576`

Current behavior:

- Persist is done first.
- Runtime apply callback runs second.
- If apply fails, overall response is error, but persisted value may remain changed.

Impact:

- From user perspective: `"error"` followed by `get` showing new value looks contradictory.
- Technically valid but poorly explained.

## 3.3 Message vocabulary differs by role and key

Files:

- `src/App/roles/device_role_pms.cpp`
- `src/App/roles/device_role_semu.cpp`
- `src/App/roles/device_role_remu.cpp`
- `src/App/roles/device_role_sens.cpp`
- `src/App/roles/device_role_relay.cpp`

Current behavior:

- Some keys return `"xxx applied"`, others `"xxx saved"`, others `"policy applied"`, others generic.
- Similar operations across roles do not always use the same phrasing.

Impact:

- Hard to reason about semantics in frontend/CLI automation.

## 3.4 Input normalization risk in CLI set path

File:

- `lib/espnow-link-esp32/src/cli/cli_dispatch.cpp:1626-1629`

Current behavior:

- Key is trimmed.
- Value is not trimmed before payload build.

Impact:

- Small formatting issues can trigger parse failures in strict profile parsers and produce misleading generic errors.

---

## 4) Target Contract (Profile-Owned Response Semantics)

Profile logic must be the source of truth for profile command outcomes.

For every profile `set`/`set.id`:

- Return one explicit outcome class:
  - `ok_applied` (persist + apply successful)
  - `ok_persisted` (persist success, no runtime apply required)
  - `error_validation` (value/key format/range invalid)
  - `error_persist` (NVS write failure)
  - `error_apply_persisted` (persist success, runtime apply failed)
  - `error_unknown_key`

Library layer behavior:

- Pass profile message through unchanged when provided.
- Use generic fallback only when profile did not provide message.

CLI behavior:

- Render exactly what provider/role returns.
- Optionally normalize output prefixing for readability, without changing meaning.

---

## 5) Implementation Workstreams

## Workstream A - Stop Generic Error Masking

Goal:

- Prevent descriptor layer from replacing profile-specific failure reasons with `"setting write unavailable"`.

Primary file:

- `lib/espnow-link-esp32/src/descriptor/descriptor.cpp`

Actions:

- Update key-based `SetSetting` error branch to only fallback when `out.message` is empty.
- Keep by-id path behavior symmetrical.
- Ensure no valid detailed message is overwritten.

Expected outcome:

- If profile returns `"led_feedback_enable expects bool"` or `"channel apply failed"`, CLI prints that exact reason.

## Workstream B - Normalize Set Input Before Dispatch

Goal:

- Remove avoidable parse ambiguity from CLI-generated values.

Primary file:

- `lib/espnow-link-esp32/src/cli/cli_dispatch.cpp`

Actions:

- Trim value in `handleSetCommand`.
- Trim value in `handleSetIdCommand`.
- Keep original command syntax unchanged.

Expected outcome:

- Fewer false validation failures caused by input spacing.

## Workstream C - Standardize Provider-Side Outcome Messages

Goal:

- Make profile provider messages explicit and phase-aware.

Primary files:

- `profile_catalog/src/slaves/pms/pms_profile.cpp`
- `profile_catalog/src/slaves/semu/semu_profile.cpp`
- `profile_catalog/src/slaves/remu/remu_profile.cpp`
- `profile_catalog/src/slaves/sens/sens_profile.cpp`
- `profile_catalog/src/slaves/relay/relay_profile.cpp`

Actions:

- Keep current persist/apply flow.
- In `finalizeSettingChange_`, ensure apply failure messages indicate persisted vs apply phase explicitly.
- Keep setting key in message where possible.
- Align unknown/validation/persist/apply wording across all roles.

Recommended message policy:

- Validation: `"<key> validation failed: <reason>"`
- Persist: `"<key> persist failed"`
- Apply after persist: `"<key> apply failed (persisted)"`
- Success with apply: `"<key> applied"`
- Success without apply: `"<key> saved"`

Expected outcome:

- User can understand exactly what failed and at which phase.

## Workstream D - Standardize Role Apply Callback Messages

Goal:

- Align app-level role adapter messages with profile contract.

Primary files:

- `src/App/roles/device_role_pms.cpp`
- `src/App/roles/device_role_semu.cpp`
- `src/App/roles/device_role_remu.cpp`
- `src/App/roles/device_role_sens.cpp`
- `src/App/roles/device_role_relay.cpp`

Actions:

- Harmonize message vocabulary for common cross-role keys:
  - `buzzer_enable`
  - `led_feedback_enable`
  - `fan_mode`
  - `channel`
  - device name key
- Keep role-specific special cases (child keys, lidar keys) but align to same grammar.

Expected outcome:

- Same semantic action returns comparable wording across all roles.

## Workstream E - CLI Render Consistency (No Command Changes)

Goal:

- Keep output understandable without altering command API.

Primary files:

- `lib/espnow-link-esp32/src/cli/cli_render.cpp`
- `lib/espnow-link-esp32/src/management/pull_response_logger.cpp`

Actions:

- Preserve existing `[MASTER][DESC] ok/error` framing.
- Ensure set/get response lines include key context when available.
- Avoid ambiguous generic lines for profile operations.

Expected outcome:

- Serial output is human-readable and traceable during tests.

---

## 6) Role-by-Role Response Contract Checklist

Each role must satisfy all items:

- `set` returns explicit reason for validation failures.
- `set` returns explicit reason for persist failures.
- `set` returns explicit reason for apply failures.
- If value persisted but apply failed, response must say so.
- `get` must reflect current persisted value.
- Messages for `buzzer_enable` / `led_feedback_enable` are clear and consistent.
- No generic `"setting write unavailable"` when provider returned a specific reason.

Roles:

- PMS
- SEMU
- REMU
- SENS
- RELAY

---

## 7) Validation Matrix (Execution After Implementation)

## 7.1 Baseline per role

For each paired target role:

- `<target> caps`
- `<target> settings`
- `<target> get led_feedback_enable`
- `<target> get buzzer_enable`

## 7.2 Set/Get clarity checks

Commands:

- `<target> set led_feedback_enable=0`
- `<target> get led_feedback_enable`
- `<target> set led_feedback_enable=1`
- `<target> get led_feedback_enable`
- `<target> set buzzer_enable=0`
- `<target> get buzzer_enable`
- `<target> set buzzer_enable=1`
- `<target> get buzzer_enable`

Pass criteria:

- Every set gives meaningful phase-aware response.
- No contradictory generic error when value changed.

## 7.3 Invalid payload checks

Commands:

- `<target> set led_feedback_enable=abc`
- `<target> set buzzer_enable=maybe`
- `<target> set chan=999` (or role channel key equivalent)

Pass criteria:

- Explicit validation errors, no generic fallback text.

## 7.4 Apply-failure observability checks

Where apply callback can fail:

- Trigger known apply-failure condition.
- Confirm response explicitly states apply phase failure.
- Confirm whether persisted value changed is clearly reflected by subsequent `get`.

---

## 8) Implementation Order

1. Workstream A (descriptor masking fix)
2. Workstream B (CLI value trim)
3. Workstream C (provider finalize + message contract)
4. Workstream D (role adapter message alignment)
5. Workstream E (render consistency)
6. Full validation matrix run
7. Update docs if command response wording changed materially

---

## 9) Done Definition

All true:

- Profile-related responses are profile-owned and meaningful.
- Library no longer masks detailed profile errors.
- Cross-role message semantics are consistent for common settings.
- Invalid inputs return deterministic validation messages.
- Persist/apply phase outcomes are explicit to user.
- No command/API feature added or removed.

