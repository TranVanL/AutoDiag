# 🎤 VDiag Interview — Part B: Framework Depth (v2.0)

> **Scope:** Property subscription · System health (CarWatchdog / CarPowerManager) · Bring-up (init.rc / SELinux / VINTF) · Stable AIDL versioning · HAL service registration & resilience · CAN transport · ADAS reusability · Real-time engine (SCHED_FIFO / PI mutex) · QA (sanitizers / gcov / clang-tidy) · Embedded ARM64 / QEMU · Observability.
> **Level target:** Senior. These are the questions that separate a 3-year candidate from a 5+ year one — deployment, versioning, resilience, and toolchain literacy.

Index: [Hub](03_INTERVIEW_PREP.md) · [A: Foundation](03a_INTERVIEW_FOUNDATION.md) · **B: Framework** · [C: Senior](03c_INTERVIEW_SENIOR.md) · [D: Behavioral](03d_INTERVIEW_BEHAVIORAL.md)

---

## 🔔 SECTION B1 — Property Subscription (Q1–Q8)

### Q1. Describe the push/subscribe model vs polling.

**Short:** Instead of the client polling `getProperty` in a loop, it calls `subscribeProperty(propId, rateHz, listener)`. The service runs a **single 100ms ticker thread** that evaluates all subscriptions, applies a **max-rate** policy per property, and pushes changes via a `oneway` `IDiagPropertyListener`. On-change properties (RPM) only fire when the cached value differs; fixed-rate properties (SOC 1Hz) fire on schedule. Mirrors `CarPropertyManager.registerCallback`.

**Deep dive — the single-ticker rationale:** One 100ms `ScheduledExecutor`/Handler tick drives every subscription rather than one timer per subscription. N subscriptions = 1 thread, not N. Each subscription stores `nextDueNs`; the tick dispatches those due and reschedules. Rate is quantized to multiples of the 100ms tick (so max 10Hz), which matches how CarService coalesces property events.

**Trap — "Why not one thread per subscriber?"** → Thread-per-subscriber doesn't scale (context-switch storm, memory) and gives no coalescing. A single ticker also makes back-pressure and max-rate trivial to enforce centrally.

---

### Q2. What is the max-rate / on-change policy and why?

**Short:** Each property declares an update mode: **CONTINUOUS** (sampled at min(requested, maxSupported) Hz) or **ON_CHANGE** (fire only when value crosses a threshold/differs). Max-rate caps a greedy client from asking 1000Hz on a sensor that only updates 10Hz — the service clamps to the property's `maxSampleRate`. Prevents one client from saturating the transport/CPU.

**Deep dive:** ON_CHANGE keeps a `lastValue` cache; a new sample within a dead-band (e.g., RPM ±5) is suppressed. This cuts callback traffic dramatically for noisy signals. CONTINUOUS honors a `minSampleRate`/`maxSampleRate` window like real `CarPropertyConfig`.

---

### Q3. How do you clean up a dead subscriber?

Each `subscribe` registers a `DeathRecipient` on the listener binder. On `binderDied`, the subscription entries for that binder are removed from the ticker's map so it stops dispatching to a dead client (which would throw `DeadObjectException`). Same zombie-prevention pattern as the request `ClientRegistry` — but keyed by listener binder.

**Trap — "Client subscribes twice to the same property?"** → I key by `(binder, propId)` and treat re-subscribe as an update (new rate replaces old), not a duplicate entry — otherwise you'd double-dispatch.

---

### Q4. Walk the Binder internals of a subscribe call (senior深挖).

Client `Proxy.subscribeProperty` writes an interface token + `propId` + `rateHz` + `writeStrongBinder(listener.asBinder())` into a `Parcel`, calls `mRemote.transact(code, data, reply, 0)` → `ioctl(BINDER_WRITE_READ)` → kernel single-copies into the service's mapped transaction buffer → wakes a Binder pool thread → `Stub.onTransact` switches on the code, `enforceInterface`, reads args, and `asInterface(readStrongBinder())` reconstructs the `IDiagPropertyListener` proxy. That proxy is the handle the ticker later uses to push events back.

---

### Q5. Why Parcelable (not Serializable) for the event object at 10Hz?

`Serializable` = reflection + garbage per event → GC pressure at 10Hz. `Parcelable` writes fields directly into the kernel-mapped Binder buffer (`writeInt/writeLong/writeString16`) with generated code — no reflection, no per-event allocation churn. At 10Hz × many properties that difference is real.

---

### Q6. How does the ticker avoid drift and jitter?

I schedule on absolute deadlines (`nextDueNs += periodNs`) rather than `sleep(period)` after work, so processing time doesn't accumulate drift. If a tick runs late, I don't burst-catch-up beyond one period (avoids thundering dispatch). Heavy dispatch is `oneway` so a slow client can't stretch the tick.

---

### Q7. What's the failure mode if a listener callback blocks?

Because the callback is `oneway`, `transact` returns immediately — the ticker never blocks on the client. The client's own Binder pool drains the async buffer. If the client floods its async buffer, a further `oneway` can throw `TransactionTooLargeException`, which I catch per-listener and log, without affecting other subscribers.

---

### Q8. How would you extend subscription to areaId (zoned properties)?

Real `CarProperty` values are zoned (`areaId`: driver door, passenger seat…). I'd add `areaId` to `DiagPropertyEvent` and subscribe per `(propId, areaId)`. The ticker keys on the pair; a whole-vehicle property uses `areaId=0` (GLOBAL). This matches `CarPropertyConfig.getAreaIds()`. See Q35 for the CarProperty v2 work.

---

## 🩺 SECTION B2 — System Health: CarWatchdog + CarPowerManager (Q9–Q16)

### Q9. What is CarWatchdog and why does a diagnostic service need it?

**Short:** CarWatchdog monitors process health (and I/O + memory overuse) in AAOS. A client registers and must **respond to a health-check ping within a deadline** (I use 3s); miss it → watchdog can kill/restart the process. A stuck diagnostic service is dangerous (it hides real faults), so it must prove liveness.

**Deep dive — the `ISystemLifecycle` abstraction:** Because a plain AVD has no CarWatchdog, I put it behind `ISystemLifecycle` with two impls: `CarApiSystemClient` (Automotive AVD — real `CarWatchdogManager.registerClient` + `onCheckHealthStatus`) and `ShimSystemClient` (regular AVD — a `Handler` posting a 3s self-check). A factory picks based on `hasSystemFeature("android.hardware.type.automotive")`. Zero call-site changes.

**Trap — "What does the watchdog actually check besides liveness?"** → Also I/O overuse and memory overuse (`ResourceOveruseStats`) — a client thrashing disk gets flagged. I mention it to show I know it's more than a heartbeat.

---

### Q10. How does the heartbeat prove the *whole* service is healthy, not just the thread?

The health check doesn't just return "alive" — it probes the engine: `nativeIsWorkerAlive()` and `nativeGetQueueDepth()`. If the worker thread died or the queue is stuck growing, I report unhealthy *before* the watchdog kills me, so the restart is clean. A naive heartbeat that always says "ok" is worse than none.

---

### Q11. CarPowerManager — which power states matter and what do you do?

**Short:** AAOS power states: ON → SHUTDOWN_PREPARE → (SUSPEND / hibernate) / SHUTDOWN. On `SHUTDOWN_PREPARE` I get a listener callback with a completion token; I flush in-flight DTC writes to Room, cancel subscriptions, release the HAL, then signal completion so the system can proceed. Missing the token = the system waits/force-kills.

**Deep dive:** The listener must finish within a bounded time and call `completePowerStateChange()` (or the AAOS equivalent). VDiag models this with `DiagPowerListener`; on emulator the shim simulates the state transitions so I can demo the flush path.

**Trap — "Difference between suspend-to-RAM and shutdown for your data?"** → Suspend: engine state can survive in RAM, I just quiesce. Shutdown: I must persist everything to Room because RAM is gone. I branch on the target state.

---

### Q12. How do you test the shutdown flush path without real power events?

The `ShimSystemClient` exposes a test hook to inject `SHUTDOWN_PREPARE`; a Robolectric/instrumented test asserts that in-flight DTCs are persisted and subscriptions cancelled before `complete` fires. Deterministic, no hardware.

---

### Q13. What happens if the HAL process dies while the service is up?

Covered by HAL resilience (Q37): the service's `DeathRecipient` on the HAL binder fires, marks HAL down, starts **exponential backoff reconnect**, and fails fast (`NRC` / HAL-down error) for new requests instead of hanging. Once reconnected it resumes. The watchdog sees the service is still responsive (it reports "degraded," not dead).

---

### Q14. Watchdog deadline missed — what's the recovery story?

If the service genuinely hangs, the watchdog kills the process; `init.rc` (`onrestart`/auto-restart, or `class` restart policy) relaunches it; on restart `onCreate` re-inits the HAL and re-registers with the watchdog. Clients re-bind (their `ServiceConnection.onServiceDisconnected` → re-`bindService`). The system self-heals.

---

### Q15. Why abstract system health behind an interface instead of `#ifdef`-style checks?

Testability + emulator parity. The engine and service depend on `ISystemLifecycle`, not on Car APIs, so unit tests inject a fake, the emulator uses the shim, and real hardware uses the Car API — one code path, swapped by factory. It's the same Open-Closed discipline as the HAL.

---

### Q16. How is this different from a Linux systemd watchdog?

Conceptually similar (sd_notify WATCHDOG=1 vs `onCheckHealthStatus`), but CarWatchdog adds car-specific policy: per-client deadlines, I/O/memory overuse governance, and integration with AAOS power/user lifecycle. I mention the analogy to show transferable systems knowledge, then the AAOS specifics.

---

## 🚀 SECTION B3 — Bring-up: init.rc + SELinux + VINTF (Q17–Q24)

### Q17. Full bring-up checklist for VDiag on real hardware.

1. **Partition:** APK → `/system/priv-app/VDiag/`; native HAL → `/vendor/bin/vdiag_hal`.
2. **init.rc:** define the service, class, user/group, restart policy, `writepid` to cgroups.
3. **SELinux:** new domain `vdiag_hal`, type-transitions, allow rules for binder + the sockets/files it touches.
4. **VINTF:** register the stable AIDL HAL + version in the vendor manifest; matrix compatibility check.
5. **Permissions:** `privapp-permissions-vdiag.xml` allowlist for the signature/privileged permission.
6. **sepolicy_version / compatibility:** ensure `system`/`vendor` sepolicy versions are compatible (Treble).

---

### Q18. Read this init.rc snippet — what does each line do?

```
service vdiag_hal /vendor/bin/vdiag_hal
    class late_start
    user system
    group system inet
    capabilities NET_ADMIN
    writepid /dev/cpuset/system-background/tasks
    onrestart restart car_service
```
- `class late_start` — starts after core services, in the late-start group (not needed for boot-critical path).
- `user/group system` — drops root; `inet` for TCP (DoIP); minimal privilege.
- `capabilities NET_ADMIN` — only if it configures CAN/network; otherwise omit (least privilege).
- `writepid …/tasks` — places the process in a cpuset/cgroup for scheduling isolation.
- `onrestart restart car_service` — dependency: if HAL restarts, restart the consumer.

**Trap — "Why not `class core`?"** → Diagnostics isn't boot-critical; `core` would delay boot and make a HAL crash boot-fatal. `late_start` isolates failure.

---

### Q19. SELinux — how do you author and verify a policy for the HAL?

**Short:** Define a domain `type vdiag_hal, domain;` and a file type `type vdiag_hal_exec, exec_type, file_type, vendor_file_type;`, plus a `type_transition` so init runs the binary in the `vdiag_hal` domain. Then `allow` rules for exactly what it needs (binder call to `car_service`, read its config file, bind the DoIP socket). I verify syntax with `checkpolicy`/`secilc` and generate missing rules from denials via `audit2allow`.

**Deep dive — the workflow:** Run in permissive for the domain, collect `avc: denied` from `dmesg`/`logcat`, `audit2allow -p policy` to draft rules, then hand-review (never blanket-allow), switch to enforcing. I keep neverallow rules in mind (e.g., a vendor HAL must not get `sys_admin`).

**Trap — "What's the difference between MLS/MAC here and DAC?"** → DAC = uid/gid file perms (discretionary). SELinux = mandatory access control: even root is constrained by the policy. A misconfigured HAL can be root and *still* be denied binder to CarService if the `.te` doesn't allow it — that's the classic bring-up bug.

---

### Q20. What is VINTF and `@VintfStability`?

**Short:** VINTF = Vendor Interface Object; it declares which HAL interfaces + versions the vendor image provides (`manifest.xml`) and what the framework requires (`compatibility_matrix.xml`). `@VintfStability` marks an AIDL interface as a stable vendor interface whose layout is **frozen** — you can only add, never change/remove — enabling independent system/vendor updates (Project Treble).

**Deep dive:** On boot, `vintf` checks manifest vs matrix; a mismatch (vendor offers v1, framework needs v2) blocks boot. So freezing the interface (`aidl_api/` snapshots) is mandatory before shipping.

---

### Q21. Walk the Stable AIDL freeze workflow.

`m <iface>-freeze-api` snapshots the current interface into `aidl_api/<iface>/<version>/` and creates a `.hash`. After freezing, the build enforces backward compatibility: adding a method at the end is allowed (new version), reordering/removing/retyping fails the build. Unfrozen changes go into the `current` version until you freeze the next. VDiag keeps `aidl_api/IDiagnosticHal/1/` frozen.

**Trap — "How do you add a field to a frozen parcelable safely?"** → Append it with a default value (so old readers ignore it, new readers get the default when talking to old writers). Never insert in the middle — the field order is the wire format.

---

### Q22. SELinux denied your service silently — how do you debug?

`adb shell dmesg | grep avc` or `logcat -b events | grep avc` for `avc: denied { call } scontext=…vdiag_hal tcontext=…car_service`. That tells me the exact permission (`call`), source domain, target domain, and class (`binder`). I add the minimal `allow vdiag_hal car_service:binder call;`, rebuild sepolicy, re-test. I never run production in permissive.

---

### Q23. What's the difference between HIDL and stable AIDL HAL — and why AIDL?

HIDL (`.hal`) was the Treble HAL IDL through Android 10; from Android 11 Google moved HALs to **stable AIDL** (simpler, one IDL for both framework and vendor, versioned via `aidl_api/`). VDiag uses stable AIDL because it's the current direction and reuses the same AIDL toolchain as the app boundary. I can discuss HIDL (`@1.0::IFoo`, `.hal` files) if a legacy codebase needs it.

---

### Q24. privapp-permissions — why is it needed and what breaks without it?

A **privileged** app (in `priv-app`) requesting a signature|privileged permission must be explicitly allowlisted in `/etc/permissions/privapp-permissions-*.xml`, else the platform *denies* the grant at boot and logs it. Without the allowlist, `DiagCarService` calls fail with `SecurityException` even though the manifest declares the permission. It's a Treble hardening step people forget.

---

## 🧩 SECTION B4 — Stable AIDL Versioning + CarProperty v2 (Q25–Q31... mapped Q25–Q31)

### Q25. How did you version the CarProperty-style API (v1 → v2)?

**Short:** v1 was `getProperty(propId)`. v2 adds `areaId` (zoned), a typed `DiagPropertyConfig` (min/max sample rate, change mode, permission per property), and **per-property permission** enforcement. I froze v1 in `aidl_api/` and added v2 methods at the end so old clients still link.

**Deep dive — per-property permission:** Real `CarPropertyManager` gates each property by a specific permission (e.g., `PERMISSION_CAR_DIAGNOSTIC_READ_ALL` vs `_CLEAR`). VDiag maps each `DiagProperty` to a required permission and enforces it in the Stub, so a client with read permission can't ClearDTC. This is the "per-property permission" bullet many JDs list.

---

### Q26. What's `areaId` and how does it change the config model?

`areaId` is a bitmask identifying a physical zone (a door, a seat, a wheel). A zoned property has a `DiagPropertyConfig` per area with independent min/max values. `getAreaIds()` enumerates them. It changes subscription keying to `(propId, areaId)` and config lookup to per-area. VDiag models a couple of zoned diagnostics (per-axle brake health) to demonstrate it.

---

### Q27. How do you keep backward compatibility when you add a property?

Property IDs are additive constants — never reuse or renumber. New properties get new IDs; old clients simply don't request them. The AIDL interface methods are appended (frozen-safe). A parcelable gains fields only by appending with defaults. So a v1 client on a v2 service keeps working.

---

### Q28. Explain the `DiagPropertyConfig` and why config-driven beats hardcoding.

Each property carries metadata: type, change mode, min/max sample rate, zoned area list, required permission. The service and SDK read config instead of hardcoding rules, so adding a property = adding a config row, not editing dispatch logic. This mirrors `CarPropertyConfig` and makes the system data-driven and testable.

---

### Q29. Version negotiation — how does a client know the service version?

Stable AIDL generates `getInterfaceVersion()`/`getInterfaceHash()`. The client queries it and feature-gates: if the service is v1, it avoids v2-only methods. VDiag's SDK checks the version once at bind and exposes only supported capabilities, so a newer app on an older service degrades gracefully instead of crashing with `null`/`UnsupportedOperationException`.

---

### Q30. What breaks if you edit a frozen AIDL file?

The build fails: the AIDL compatibility check compares against the `aidl_api/<n>/` snapshot + `.hash`. Reordering methods, changing a type, removing a method, or inserting a parcelable field all fail. This is intentional — it prevents an ABI break that would brick a mixed system/vendor image. You must bump to a new version instead.

---

### Q31. When would you NOT use `@VintfStability`?

For an interface that never crosses the system/vendor boundary (e.g., app↔service inside the same APK/signature). `@VintfStability` adds freeze ceremony and forbids convenient changes; using it internally is over-engineering. VDiag uses it only for the HAL contract, plain AIDL elsewhere — and I can justify that boundary.

---

## 🛰 SECTION B5 — HAL Resilience + Transport Diversity: CAN / DoIP / ADAS (Q32–Q42)

### Q32. Describe the 3-process HAL topology.

App process ↔ (Binder) ↔ `:car_service` process ↔ (Binder to a `/vendor` HAL binder, or in-process for Mock) ↔ HAL. In production the HAL is its own process registered in VINTF; the service holds a proxy and `linkToDeath` on it. Three processes = three isolation domains; a HAL crash can't take down the service or the app.

---

### Q33. How does HAL service registration work (ServiceManager)?

The HAL registers with `servicemanager` under an instance name (`IDiagnosticHal/default`); the consumer does `waitForService`/`getService`. `waitForService` blocks until it's up (handles boot ordering). VDiag models this with a registry lookup + retry; on emulator the Mock is created directly, but the *pattern* (lookup → proxy → linkToDeath) is preserved for DoIP/ADAS.

---

### Q34. Explain the DeathRecipient + exponential backoff reconnect.

**Short:** The service registers a `DeathRecipient` on the HAL binder. On HAL death: mark HAL down, fail new requests fast with a HAL-down error (don't hang), and start reconnect with exponential backoff (e.g., 100ms → 200 → 400 → … cap 5s, with jitter). On success, mark up and resume. This is the resilience story that distinguishes senior work.

**Deep dive — why fail-fast + backoff, not block:** Blocking a Binder thread waiting for a dead HAL would exhaust the pool and cascade. Fail-fast keeps the service responsive (and watchdog-happy); backoff avoids a reconnect storm. Jitter prevents synchronized retries across clients.

**Trap — "How do you avoid losing the request that hit during downtime?"** → I either reject it immediately (client retries per policy) or queue a bounded number with a deadline; unbounded queuing during an outage is how you OOM. I choose bounded + reject-with-retryable-error.

---

### Q35. CAN Bus HAL — how does ISO-TP segmentation work in your code?

`CanDiagnosticHal` on `vcan0` implements ISO 15765-2: **SF** (≤7 bytes, PCI `0x0X`), **FF** (`0x1X` + 12-bit length, first 6 bytes), then it waits for **FC** (`0x3X`: ContinueToSend/Wait/Overflow + block size + STmin), then sends **CF** (`0x2X` + 4-bit sequence 1..15 wrapping) honoring STmin spacing. On receive it reassembles CF by sequence, validating no gaps.

**Trap — "What's STmin and block size for?"** → STmin = minimum separation time between consecutive frames (receiver's rate limit); block size = how many CFs before the sender must wait for another FC. They exist so a slow ECU isn't overrun. Ignoring them causes frame loss on real buses.

---

### Q36. Why `vcan0` (virtual CAN) instead of real CAN hardware?

`vcan0` is a Linux kernel virtual CAN interface — same SocketCAN API (`PF_CAN`, `struct can_frame`, `bind` to `sockaddr_can`) as real hardware, no adapter needed. My ISO-TP code is byte-for-byte identical to what would run on a real `can0`; only the interface name changes. CI-friendly and proves the transport logic.

---

### Q37. Transport diversity — prove the abstraction with a comparison.

| Transport | Framing | Latency | Where |
|---|---|---|---|
| Mock | none (in-proc lookup) | <1ms | CI/unit |
| DoIP | ISO 13400 header over TCP | ~50ms localhost | integration |
| CAN | ISO-TP over SocketCAN | ~5–20ms | vcan0 |
| ADAS | sensor frames over TCP | streaming | reusability demo |

The engine code is identical across all four — only the `IDiagnosticHal` impl changes. That's the Open-Closed proof, demonstrated, not claimed.

---

### Q38. ADAS Sensor HAL — what does it actually do and how honest are you about it?

**Short:** `IAdasSensorHal` reuses the exact `IDiagnosticHal` factory pattern for a sensor domain: an IMU **complementary filter** (α≈0.98 fusing gyro integration with accelerometer tilt) and a **nearest-neighbor radar tracker** (track lifecycle Tentative→Confirmed→Lost). It's a **HAL reusability proof**, not production sensor fusion.

**Honesty line:** *"This demonstrates that my HAL abstraction generalizes beyond diagnostics — same factory, same lifecycle, different domain. It is not a production perception stack; that needs Kalman/EKF and ML, which isn't what this project claims. My deep C++ real-time work is EventStreamCore."*

**Trap — "Why complementary filter not Kalman?"** → Complementary filter is the honest choice for a demo: O(1), no covariance tuning, correct for the gyro-drift/accel-noise trade-off. A Kalman/EKF would be over-claiming without a real noise model. Knowing *why* the simpler filter is appropriate is the senior signal.

---

### Q39. Scale to multiple ECUs simultaneously — design.

One `DiagEngine` per ECU (`engine[body]`, `engine[powertrain]`, `engine[adas]`), each with its own worker thread, priority queue, session state machine, and HAL (different DoIP target address / port). `DiagCarService` routes by `DiagRequest.ecuId`. UDS allows concurrent sessions to *different* target addresses, so no cross-engine locking. Client API is unchanged.

**Trap — "Shared transport (one CAN bus, many ECUs)?"** → Then the transport is the serialization point: engines share one `CanDiagnosticHal` guarded by a transport-level lock/arbiter with per-ECU addressing (different CAN IDs). I separate "logical engine per ECU" from "physical transport arbitration."

---

### Q40. What's a session state machine and why per-engine?

`Idle → Pending → Done | Error`, plus session type (Default / Extended / Programming via `0x10`). It enforces one pending request per session and tracks whether a non-default session is open (so `SubscriptionManager` sends TesterPresent). Per-engine because each ECU session is independent.

---

### Q41. How do you test HAL resilience deterministically?

A `FaultInjectingHal` decorator wraps any `IDiagnosticHal` and can drop the connection, delay, or return NRCs on command. Tests assert: death → fail-fast error, backoff sequence timing (with a fake clock), and clean resume after reconnect. No real hardware flakiness.

---

### Q42. DoIP routing activation — what and why?

Before any diagnostic message, a DoIP tester must send a **routing activation request** (`0x0005`) with its source address; the gateway replies with activation response (accepted/denied). Only then are `0x8001` diagnostic messages routed to the target ECU. `DoipDiagnosticHal` performs activation on connect and re-activates on reconnect. Skipping it → the gateway silently drops your UDS frames.

---

## ⚡ SECTION B6 — Real-time Engine (Q43–Q48)

### Q43. Why a 4-tier priority queue, and why separate deques not `priority_queue`?

**Short:** UDS requests have different deadlines: safety DTC/session (CRITICAL) must preempt routine VIN reads (NORMAL) and background polls (LOW). Four separate `std::deque`s drained CRITICAL→HIGH→NORMAL→LOW means a high-priority item never waits behind a low-priority one. `std::priority_queue` is a binary heap: O(log n) push, no O(1) push-front, and awkward for FIFO-within-tier. Separate deques give O(1) push/pop and clean FIFO ordering per tier.

**Trap — "Starvation of LOW?"** → Possible if CRITICAL is saturated. I add anti-starvation: after N high-priority items, service one lower-tier item (aging). For diagnostics the arrival rate makes true starvation rare, but I mention aging to show I know the failure mode.

---

### Q44. SCHED_FIFO — what is it and how do you handle no-permission?

`SCHED_FIFO` is a real-time scheduling policy: a FIFO thread runs until it blocks or a higher-priority RT thread preempts it (no time-slicing among equal priority). Setting it needs `CAP_SYS_NICE`. On a dev machine/emulator without it, `pthread_setschedparam` returns `EPERM`; I **gracefully fall back** to `SCHED_OTHER` and log it — the code path is production-ready but doesn't crash where it lacks privilege.

**Trap — "Danger of SCHED_FIFO?"** → A busy-looping FIFO thread at high priority can starve the whole system (including the watchdog). So the worker must *block* on the condvar when idle, never spin — which it does. RT priority + blocking wait is the correct pattern.

---

### Q45. Priority inheritance mutex (PI mutex) — what problem does it solve?

Classic **priority inversion**: a LOW-priority thread holds a lock the CRITICAL thread needs, while a MEDIUM thread preempts LOW → CRITICAL waits indefinitely (the Mars Pathfinder bug). A `PTHREAD_PRIO_INHERIT` mutex temporarily boosts the lock holder to the waiter's priority so it releases quickly. I set `pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT)` on the engine mutex.

**Trap — "Priority ceiling vs inheritance?"** → Ceiling raises the holder to a *predefined* ceiling on lock (deterministic, needs known max priority); inheritance boosts *dynamically* to the actual waiter. Inheritance is simpler when priorities aren't statically known; ceiling is preferred in hard-RT with static analysis.

---

### Q46. How do you avoid data races between submit and the worker?

`submit` locks the engine mutex, pushes to the tier deque, `notify_one` the condvar; the worker waits on the condvar with a predicate (`any queue non-empty || stopping`), pops under the lock, then *releases* the lock before the blocking HAL call (so submits aren't blocked during I/O). The callback fires outside the lock. Verified with TSAN.

---

### Q47. Graceful shutdown of the worker — how?

A `stopping_` atomic + `notify_all`; the worker's condvar predicate wakes, drains/aborts pending items (invoking callbacks with a cancelled result so no client hangs), then exits. The `pthread_key` destructor detaches JNI on exit. `join()` in the engine dtor. No detached threads, no leaked GlobalRefs.

---

### Q48. Where's the honest boundary vs EventStreamCore?

**Short:** VDiag's engine proves *correct* real-time structure (priority tiers, PI mutex, SCHED_FIFO, blocking wait) for a request/response domain where latency is transport-bound. **Sub-microsecond, lock-free, 10M ev/s** is EventStreamCore's job (SPSC ring, cache-line padding, no locks). I deliberately don't benchmark VDiag for throughput — that would be measuring the wrong thing.

---

## 🧪 SECTION B7 — QA / Sanitizers / Coverage (Q49–Q55)

### Q49. Which sanitizers do you run and what does each catch?

- **ASan** (AddressSanitizer): heap/stack/global overflow, use-after-free, double-free.
- **UBSan** (UndefinedBehavior): signed overflow, misaligned loads, invalid enum, null deref.
- **TSan** (ThreadSanitizer): data races, lock-order inversions (deadlock potential).
- **CheckJNI:** JNI ref/thread misuse.
- **Valgrind (memcheck):** leaks + uninitialized reads on host.

VDiag CI runs ASan+UBSan together and TSan separately (they're incompatible). All clean.

**Trap — "Why can't you run ASan and TSan together?"** → They both instrument memory access with incompatible runtimes/shadow-memory schemes. Standard practice: two CI jobs.

---

### Q50. gcov/lcov — what coverage do you target and what's the trap in coverage numbers?

**Short:** `--coverage` (gcov) + `lcov` HTML, target ~85% line coverage on the HAL/engine. The trap: line coverage ≠ correctness — you can cover a branch without asserting the right outcome. So I pair coverage with *behavioral* assertions (UDS byte-exact, NRC paths) and track branch coverage on the codec, not just lines.

---

### Q51. clang-tidy — which checks matter for this code?

`bugprone-*`, `cppcoreguidelines-*` (esp. `-pro-type-*`, `-owning-memory`), `performance-*`, `modernize-*`, `misc-*`. For JNI/engine I care about `bugprone-use-after-move`, `cppcoreguidelines-prefer-member-initializer`, `performance-unnecessary-value-param`. VDiag CI is 0-warning with a curated `.clang-tidy`.

---

### Q52. Zero-copy / I/O optimization — what did you actually do?

**Short:** For bulk transfer I use `ParcelFileDescriptor` + `ASharedMemory` (mmap, no serialize copy) instead of a giant parcelable array (Part C). For socket transport I read into a reused buffer (no per-frame allocation) and use `writev`/scatter-gather to avoid concatenating DoIP header + payload. On the JNI edge I use `GetByteArrayRegion` into a stack buffer for small payloads (no critical-section GC stall).

**Trap — "Measure the zero-copy win?"** → For a 500KB DTC snapshot, the parcelable path copies through the Binder buffer (and hits the 1MB limit); the ashmem path maps the same physical page → one `memcpy` on write, zero on read, and no size ceiling. The win is both latency and correctness (no `TransactionTooLargeException`).

---

### Q53. How do you test byte-level protocol correctness?

Table-driven gtest: `{input UDS bytes} → {expected encoded/decoded}` for every service, including negative frames (`0x7F` + NRC), truncated frames, and wrong-SID-echo. `assertArrayEquals`-style with hex dumps on failure. This is where most real bugs live (off-by-one in DID length, endianness).

---

### Q54. What's your CI matrix?

Jobs: (1) x86-64 build + gtest + ASan/UBSan, (2) x86-64 TSan, (3) **aarch64 cross-compile + QEMU gtest**, (4) clang-tidy lint gate, (5) gcov→lcov coverage upload, (6) Java: JUnit/Mockito/Robolectric on JVM + Jacoco, (7) Espresso on an AVD. A red job blocks merge.

---

### Q55. Valgrind vs ASan — when each?

ASan is faster (~2×) and catches most memory bugs at runtime with better stack traces; it's my default in CI. Valgrind memcheck is slower (~20×) but needs no recompile and catches *uninitialized reads* ASan misses (MSan does too but needs instrumented libs). I run Valgrind on the host engine as a belt-and-suspenders check.

---

## 🔩 SECTION B8 — Embedded ARM64 + QEMU + Observability (Q56–Q62)

### Q56. Why cross-compile to ARM64 and run under QEMU if you have no board?

Three concrete risks a board would expose that x86 hides: **alignment faults** (`*(uint32_t*)(buf+1)` → SIGBUS on ARM), **type-size assumptions**, and **accidental Android-only API leakage** into `hal/`. QEMU user-mode runs the ARM ELF and all gtests pass → architecture-level correctness verified without hardware. It's also a strong CI artifact ("binary runs on emulated ARM").

---

### Q57. Explain the CMake toolchain file for aarch64.

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)  # host tools only
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)   # target libs only
set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-aarch64;-L;/usr/aarch64-linux-gnu")
```
`FIND_ROOT_PATH_MODE_*` stops CMake from linking host x86 libs; `CROSSCOMPILING_EMULATOR` lets `ctest` run the ARM binaries transparently via QEMU with the ARM sysroot for `-L`.

---

### Q58. QEMU user-mode vs system-mode — which and why?

**User-mode** (`qemu-aarch64 ./test`): translates ARM Linux syscalls to the host — runs a single ARM binary on the host kernel, fast, perfect for running gtests. **System-mode** emulates a whole machine (kernel, devices) — needed for driver/boot work, much heavier. I use user-mode because I'm validating userspace C++ correctness, not kernel/board bring-up.

**Trap — "What user-mode QEMU won't catch?"** → Timing/real-time behavior, cache effects, actual device drivers, and CPU-errata-specific bugs. It validates *functional/architectural* correctness, not performance. I state that limit explicitly.

---

### Q59. Endianness — is it actually a risk here?

Both x86-64 and aarch64 are little-endian, so byte-order bugs won't diverge between them. But UDS/DoIP wire format is **big-endian** (network order) regardless of CPU, so my codec uses explicit `be16toh`/shift-and-mask, never `memcpy` of a struct. QEMU won't catch endianness (same LE), but a big-endian target (some MIPS/PowerPC ECUs) would — so I code to the spec, not the CPU.

---

### Q60. Observability — how do you trace the pipeline (Perfetto/atrace)?

**Short:** I add `ATrace_beginSection/endSection` (NDK `atrace`) around engine stages (enqueue, dequeue, HAL round-trip, callback) so a Perfetto capture shows the per-request timeline across threads. Java side uses `Trace.beginSection`. A capture visualizes queue wait vs processing vs return, and reveals if CRITICAL is being delayed.

**Deep dive — metrics:** The engine records `enqueue_ns`, `dequeue_ns`, `response_ns`, `tier` per request → `wait = dequeue-enqueue`, `processing = response-dequeue`. Exposed via `nativeGetMetrics()` → JNI → an in-app stats view, and via `dumpsys` (Part C). Alert if CRITICAL p99 > 10ms.

---

### Q61. How would you find a latency regression in CI?

A perf gtest submits a fixed workload and asserts p95/p99 wait-latency under a threshold (with margin for QEMU/CI noise). A regression flips the job red. I keep it as a *soft* gate (warn) on CI runners because shared runners are noisy — hard latency asserts belong on dedicated hardware, which I'm honest about not having.

---

### Q62. What does the transaction history ring buffer give you?

A lock-free 256-slot ring records recent transactions (code, latency, tier, result). `dumpsys vdiag --history` prints the last N and computes p50/p95/p99 on demand — so in production you can diagnose "it's slow right now" without a profiler attached. Detail in [Part C, Q52](03c_INTERVIEW_SENIOR.md).

---

*Continue → [Part C: Senior — IPC / Testing / MVVM / Security (v3.0)](03c_INTERVIEW_SENIOR.md)*
