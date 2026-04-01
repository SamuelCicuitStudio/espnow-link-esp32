# Current Library Documentation

This folder is the canonical documentation set for the current `espnow-link-esp32` implementation.

Scope rules:

- library behavior only
- no project/app-specific workflows
- one subject per file

## Recommended Read Order

- `cli.md` for operator command model (`active` targeting, profile-aware commands, role-specific render output)
- `profiles-registry.md` for current profile key contracts and map surfaces
- `telemetry.md` for pull/push command model and push validation limits
- `descriptors-settings.md` for settings schema/get/set behavior
- `frontend-api.md` for adapter/controller cache orchestration and operation tracking

## Document Index

- `architecture.md` - Runtime modules and ownership boundaries
- `control-plane.md` - Management command contract, routing, access, statuses/events
- `cli.md` - `MasterCli` command surface and target routing model
- `frontend-api.md` - `ManagementController` and `ManagementFrontendAdapter`
- `profiles-registry.md` - Built-in profiles and registry extension contract
- `descriptors-settings.md` - Descriptor schema and settings get/set behavior
- `pairing-lifecycle.md` - Discovery/pair/unpair/remove and lifecycle controls
- `telemetry.md` - Telemetry pull/push behavior
- `topology-multislave.md` - Topology, channel sync, chain loop, multislave constraints
- `logging-storage.md` - Local/remote logging and storage commands
- `ota.md` - OTA transfer, push/update, archive flows
- `radio-transition.md` - Radio transition guard and hard deinit/reinit flows
- `testing-validation.md` - Regression checklist and verification model
- `examples-integration.md` - Library bootstrap and example integration

Optimization docs are maintained separately under `docs/optimization`.

For upcoming settings cache policy and delta-write migration, start with:

- `../optimization/slave-settings-cache-policy-and-delta-write-fix-plan.md`
