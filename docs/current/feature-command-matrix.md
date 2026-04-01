# Release Feature Command Matrix

This matrix is the complete release command surface from `ManagementCommandId`.

Lifecycle legend:

1. `Immediate`: terminal status comes from response path.
2. `Deferred`: terminal status must be completed via lifecycle event path.

## Important

This is not CLI-only.

1. The command IDs below are the shared control-plane contract used by all surfaces.
2. CLI uses them through `MasterCli -> ManagementController`.
3. API/frontends use the same IDs through `ManagementController` and/or `ManagementFrontendAdapter`.

## API Surface Mapping (What calls these command IDs)

| Domain | `ManagementController` (typed API) | `ManagementFrontendAdapter` (orchestration API) |
|---|---|---|
| Discovery/Pairing | `discoveryStart`, `discoveryStop`, `discoverySnapshotGet`, `pairedSnapshotGet`, `pairRequest`, `unpairRequest`, `removePeerRequest`, `statusGet` | `pairedPeersGet`, `commandRunAndWait`, `runAndTrack`, `operationSubmit/Wait` |
| Descriptor/Bundle | `descGet`, `capsGet`, `capsPageGet`, `settingsGet`, `settingsPageGet`, `telemetrySchemaGet`, `telemetrySchemaPageGet`, `telemetryPull`, `nodeBundleGet` | `descriptorBundleGet`, `nodeBundleGet`, `nodeSnapshotGet`, `telemetryNowGet` |
| Settings | `settingGetByKey`, `settingGetById`, `settingSetByKey`, `settingSetById`, `nodeBundleGet` | `settingsReadCached`, `settingsReadCachedForUi`, `settingsRefresh`, `settingsBundleRefresh`, `settingsBundleGet`, `settingsSetBatch`, `settingsAfterWriteSync`, `cachedSettingsResolved` |
| Liveness/Time/Ping | `livenessGet`, `timeGet`, `timeSet`, `pingGet`, `liveMonitorEnable`, `liveMonitorDisable`, `liveMonitorStatusGet` | `telemetryNowGet`, `autoPullControl`, `autoPullStatusGet`, `commandRunAndWait`, `runAndTrack` |
| Push | `pushCommand`, `pushStart`, `pushUpdate`, `pushPause`, `pushResume`, `pushStop`, `pushGet` | `pushControl`, `commandRunAndWait`, `runAndTrack` |
| Topology/Channel/Chain | `topologyStageSet`, `topologyCommit`, `topologyStatusGet`, `topologySlotsGet`, `topologyTriggerSend`, `channelRuntimeGet`, `channelSyncAll`, `chainLoopEnable/Disable/SetEnabled/StatusGet` | `topologyControl`, `commandRunAndWait`, `runAndTrack` |
| Critical control | `restartSlaveRequest`, `resetSlaveRequest`, `restartMasterRequest`, `resetMasterRequest`, `audioPingRequest`, `cliEnable/Disable/StatusGet` | `commandRunAndWait`, `runAndTrack` |
| Logger/Storage | `logLocalStatusGet/Read/Clear/SetEnabled`, `logRemoteStatusGet/Read/Clear/SetEnabled`, `storageInfoGet/List/Stat/Format` | `commandRunAndWait`, `runAndTrack` |
| OTA | `otaStatusGet`, `otaManifestGet/PageGet/Rebuild`, `otaClearScope`, `otaCapacityGet`, `otaGateGet`, `otaApply`, `otaRollback`, `otaTransferBegin/Chunk/End/Abort`, `otaPushStart/Abort/Status`, `otaUpdateStart`, `otaArchive*`, `otaUpdateMasterStart` | `otaControl`, `commandRunAndWait`, `runAndTrack`, `operationSubmit/Wait` |
| Diagnostics | `commTestRun/Status/Report`, `metricsGet/Reset`, `queueGet` | `commandRunAndWait`, `runAndTrack`, `commandTraitsGet`, `operationStatus` |

## 1) Discovery and Pairing

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0001` | `DiscoveryStart` | Discovery | Global | Immediate |
| `0x0002` | `StatusGet` | Runtime Status | Global | Immediate |
| `0x0003` | `PairRequest` | Pairing | Peer | Deferred |
| `0x0004` | `UnpairRequest` | Pairing | Peer | Deferred |
| `0x0005` | `DiscoveryStop` | Discovery | Global | Immediate |
| `0x0006` | `DiscoverySnapshotGet` | Discovery | Global | Immediate |
| `0x0007` | `RemovePeerRequest` | Pairing | Peer | Immediate |
| `0x0008` | `PairedSnapshotGet` | Pairing | Global | Immediate |

## 2) Descriptor, Settings, Telemetry, Liveness, Time

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0010` | `DescGet` | Descriptor | Peer | Immediate |
| `0x0011` | `CapsGet` | Descriptor | Peer | Immediate |
| `0x0012` | `SettingsGet` | Descriptor/Settings | Peer | Immediate |
| `0x0013` | `SettingGet` | Settings | Peer | Immediate |
| `0x0014` | `SettingSet` | Settings | Peer | Immediate |
| `0x0015` | `TelemSchemaGet` | Descriptor/Telemetry | Peer | Immediate |
| `0x0016` | `TelemPull` | Telemetry | Peer | Immediate |
| `0x0017` | `LiveGet` | Liveness | Peer | Immediate |
| `0x0018` | `TimeGet` | Time | Peer | Immediate |
| `0x0019` | `TimeSet` | Time | Peer | Immediate |
| `0x001A` | `CapsPageGet` | Descriptor Paging | Peer | Immediate |
| `0x001B` | `TelemSchemaPageGet` | Descriptor Paging | Peer | Immediate |
| `0x001C` | `SettingsPageGet` | Descriptor Paging | Peer | Immediate |
| `0x001D` | `PingGet` | Liveness/Ping | Peer | Immediate |
| `0x001E` | `LiveMonitorEnable` | Live Monitor | Global | Immediate |
| `0x001F` | `LiveMonitorDisable` | Live Monitor | Global | Immediate |

## 3) Push, Live Monitor Status, Topology

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0020` | `PushStart` | Telemetry Push | Peer | Immediate |
| `0x0021` | `PushUpdate` | Telemetry Push | Peer | Immediate |
| `0x0022` | `PushPause` | Telemetry Push | Peer | Immediate |
| `0x0023` | `PushResume` | Telemetry Push | Peer | Immediate |
| `0x0024` | `PushStop` | Telemetry Push | Peer | Immediate |
| `0x0025` | `PushGet` | Telemetry Push | Peer | Immediate |
| `0x0026` | `LiveMonitorStatusGet` | Live Monitor | Global | Immediate |
| `0x0027` | `TopologyStageSet` | Topology | Peer | Immediate |
| `0x0028` | `TopologyCommit` | Topology | Peer | Immediate |
| `0x0029` | `TopologyStatusGet` | Topology | Peer | Immediate |
| `0x002A` | `TopologySlotsGet` | Topology | Peer | Immediate |
| `0x002B` | `TopologyTriggerSend` | Topology | Peer | Immediate |

## 4) Critical Control

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0030` | `RestartSlaveRequest` | Lifecycle Control | Peer | Immediate |
| `0x0031` | `ResetSlaveRequest` | Lifecycle Control | Peer | Immediate |
| `0x0032` | `RestartMasterRequest` | Lifecycle Control | Global | Immediate |
| `0x0033` | `ResetMasterRequest` | Lifecycle Control | Global | Immediate |
| `0x0034` | `AudioPingRequest` | Lifecycle/Device Signal | Peer | Immediate |

## 5) Logging and Channel

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0040` | `LogLocalStatusGet` | Logger Local | Global | Immediate |
| `0x0041` | `LogLocalRead` | Logger Local | Global | Immediate |
| `0x0042` | `LogLocalClear` | Logger Local | Global | Immediate |
| `0x0043` | `LogLocalControlSet` | Logger Local | Global | Immediate |
| `0x0044` | `LogRemoteStatusGet` | Logger Remote | Peer | Immediate |
| `0x0045` | `LogRemoteRead` | Logger Remote | Peer | Immediate |
| `0x0046` | `LogRemoteClear` | Logger Remote | Peer | Immediate |
| `0x0047` | `LogRemoteControlSet` | Logger Remote | Peer | Immediate |
| `0x0048` | `ChannelRuntimeGet` | Channel Control | Global | Immediate |
| `0x0049` | `ChannelSyncAll` | Channel Control | Global | Deferred |

## 6) Storage

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0050` | `StorageInfoGet` | Storage | Peer | Immediate |
| `0x0051` | `StorageList` | Storage | Peer | Immediate |
| `0x0052` | `StorageStat` | Storage | Peer | Immediate |
| `0x0053` | `StorageFormat` | Storage | Peer | Immediate |

## 7) OTA

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0060` | `OtaStatusGet` | OTA | Peer | Immediate |
| `0x0061` | `OtaManifestGet` | OTA | Peer | Immediate |
| `0x0062` | `OtaManifestPageGet` | OTA | Peer | Immediate |
| `0x0063` | `OtaManifestRebuild` | OTA | Peer | Immediate |
| `0x0064` | `OtaClearScope` | OTA | Peer | Immediate |
| `0x0065` | `OtaCapacityGet` | OTA | Peer | Immediate |
| `0x0066` | `OtaGateGet` | OTA | Peer | Immediate |
| `0x0067` | `OtaApply` | OTA | Peer | Immediate |
| `0x0068` | `OtaRollback` | OTA | Peer | Immediate |
| `0x0069` | `OtaTransferBegin` | OTA Transfer | Peer | Immediate |
| `0x006A` | `OtaTransferChunk` | OTA Transfer | Peer | Immediate |
| `0x006B` | `OtaTransferEnd` | OTA Transfer | Peer | Immediate |
| `0x006C` | `OtaTransferAbort` | OTA Transfer | Peer | Immediate |
| `0x006D` | `OtaPushStart` | OTA Push Pipeline | Peer | Deferred |
| `0x006E` | `OtaPushAbort` | OTA Push Pipeline | Peer | Immediate |
| `0x006F` | `OtaPushStatus` | OTA Push Pipeline | Peer | Immediate |

## 8) Diagnostics, Archive, CLI Control, Bundle

| ID | Command | Domain | Scope | Lifecycle |
|---|---|---|---|---|
| `0x0070` | `CommTestRun` | Diagnostics | Peer | Immediate |
| `0x0071` | `CommTestStatus` | Diagnostics | Global | Immediate |
| `0x0072` | `CommTestReport` | Diagnostics | Global | Immediate |
| `0x0073` | `MetricsGet` | Diagnostics | Global | Immediate |
| `0x0074` | `MetricsReset` | Diagnostics | Global | Immediate |
| `0x0075` | `QueueGet` | Diagnostics | Global | Immediate |
| `0x0076` | `OtaUpdateStart` | OTA Update Pipeline | Peer | Deferred |
| `0x0077` | `OtaArchiveList` | OTA Archive | Global/Peer | Immediate |
| `0x0078` | `OtaArchiveSaveRunning` | OTA Archive | Global/Peer | Immediate |
| `0x0079` | `OtaArchiveSaveStaged` | OTA Archive | Global/Peer | Immediate |
| `0x007A` | `OtaArchiveRestore` | OTA Archive | Global/Peer | Immediate |
| `0x007B` | `OtaArchiveDelete` | OTA Archive | Global/Peer | Immediate |
| `0x007C` | `OtaArchiveClear` | OTA Archive | Global/Peer | Immediate |
| `0x007D` | `OtaMasterUpdateStart` | Master OTA Local | Global | Immediate |
| `0x007E` | `OtaArchiveVerify` | OTA Archive | Global/Peer | Immediate |
| `0x007F` | `CliControlSet` | CLI Runtime Control | Global | Immediate |
| `0x0080` | `ChainLoopControlSet` | Chain Loop Control | Global | Deferred |
| `0x0081` | `NodeBundleGet` | Descriptor Bundle | Peer | Immediate |

## 9) Release Targeting Rule

For release behavior on master nodes:

1. Any peer-bound mutation/read must be submitted with explicit peer target context.
2. Global commands must not consume stale implicit peer state.
3. CLI and API must resolve to this same command matrix with no per-surface hidden command translation.
