# WiFi Callback Offload Implementation Plan (Temporary)

Status: Active working plan  
Scope: `lib/espnow-link-esp32` only (master + slave runtime paths)  
Rule: No new feature; optimize execution path only  
Legacy policy: replace old callback-heavy path, do not keep legacy parallel path  
Removal policy: delete this file immediately after all plan items are completed and verified

## Scope Lock (Mandatory)

- Do not add any feature.
- Do not remove any existing feature.
- Do not change user-visible command/API capability surface.
- Only optimize execution path and internal runtime structure (callback -> worker offload).

---

## 1) Objective

Make ESPNOW receive callback path minimal and deterministic:

- callback path does parse + lightweight validation + enqueue only
- heavy decode/dispatch/storage/logging/printing runs in runtime tick worker path
- CLI and API behavior remains functionally equivalent

---

## 2) Current Gaps (from analysis)

Already offloaded:

- slave `PullRequest` handling is queued (`pull_request_queue_`)

Still callback-heavy:

- master `PullResponse` processing (`control_plane_->onPullResponse` -> CLI decode/render)
- OTA RX handling on slave (`FirmwareBegin/Chunk/End`)
- event/hook fanout in RX path (`events_->onEvent`, `hooks_->onRxFrame`, `blinkForMessage`)
- pairing/topology handlers still perform peer/storage/hook operations inline

---

## 3) Target Runtime Structure

### 3.1 Callback Path (strict)

- decode frame header
- reject invalid/version mismatch quickly
- enqueue typed work item
- return immediately

No direct:

- descriptor decode/render
- NVS/filesystem writes
- heavy `Serial`/logging output
- OTA file flush/finalize

### 3.2 Worker Path (`tick`)

- consume bounded queue budgets per tick
- run typed handlers in normal runtime context
- emit responses/events from worker context
- preserve ordering by queue type and arrival

---

## 4) Implementation Phases

## Phase A - Master PullResponse Offload

Tasks:

- add `pull_response_queue_` in manager
- in `onRxPullResponse`: enqueue payload/context only
- add `tickPullResponseQueue()` + `processQueuedPullResponse_()`
- move `control_plane_->onPullResponse` call to worker

Acceptance:

- no direct `onPullResponse` execution from callback path
- CLI/API response flow unchanged
- no queue starvation (budget + progress under sustained responses)

## Phase B - Slave OTA RX Offload

Tasks:

- add `firmware_rx_queue_` for `FirmwareBegin/Chunk/End`
- callback path enqueues OTA messages only
- worker executes `handleFirmwareBegin/Chunk/End`
- keep correlation/order guarantees per peer/corr_id

Acceptance:

- OTA prepare/begin/chunk/end no longer performs storage work in callback
- no WiFi task stack canary panic on OTA prepare/push
- finalize status/ack behavior unchanged

## Phase C - Event/Hook Decoupling

Tasks:

- queue runtime events originating from RX path
- process event fanout in worker tick
- reduce callback hooks to optional minimal tracing only
- gate/suppress expensive blink/log behavior in callback path

Acceptance:

- callback path contains no heavy fanout loops
- event consumers (CLI, management service, extra sinks) still receive same semantics

## Phase D - Pairing/Topology Hardening

Tasks:

- identify pairing/topology RX handlers still writing persistence or calling heavy hooks
- move heavy portions to deferred command queue
- keep protocol state transitions correct and idempotent

Acceptance:

- no direct persistence-heavy operations from callback path
- pairing/topology behaviors remain unchanged at protocol level

---

## 5) Queueing Rules

- bounded fixed-size queues; explicit overflow policy (drop-oldest + metric/event)
- separate queue per workload class:
  - pull request (already exists)
  - pull response
  - firmware rx
  - optional event fanout
- per-tick budget per queue to avoid monopolizing loop
- maintain correlation metadata and source MAC in queued item

---

## 6) Instrumentation and Validation

Add/verify metrics:

- enqueue count, dequeue count, dropped count per queue
- max queue depth reached
- per-handler execution timing in worker context

Validation runs:

- `test.all` on master/slave examples
- targeted OTA flow (`ota.prepare`, push begin/chunks/end, finalize)
- pull-heavy descriptors (`caps`, `telem`, `settings.full`)
- autopull/push co-existence

Pass criteria:

- no callback-stack panic
- functional parity for CLI/API outcomes
- no regression in command acceptance/response semantics

---

## 7) Execution Order

1. Phase A (master pull response)
2. Phase B (slave OTA rx)
3. Phase C (event/hook decouple)
4. Phase D (pairing/topology heavy fragments)
5. final full test sweep
6. delete this file

---

## 8) Done Definition

All true:

- callback path reduced to lightweight parse/enqueue
- heavy work moved to worker path for both master and slave
- CLI and API contracts preserved
- tests pass and OTA crash no longer reproducible
- this temporary file removed from repo
