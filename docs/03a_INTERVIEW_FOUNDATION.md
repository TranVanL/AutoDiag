# 🎤 VDiag Interview — Part A: Foundation (v1.0)

> **Scope:** Architecture + AAOS mapping · Android system (Binder / Service / Permission / process) · JNI lifecycle · UDS + Automotive protocol.
> **Level target:** Senior (5+ năm). Mỗi câu có **short answer** (nói trong 30-60s) + **deep dive** (khi interviewer đào tiếp) + **trap/follow-up** (câu hỏi bẫy thường theo sau).
> **Cách dùng:** Đọc to short answer đến khi thuộc. Deep dive chỉ bung ra khi bị hỏi "why" / "how exactly". Đừng info-dump.

Index: [Hub](03_INTERVIEW_PREP.md) · **A: Foundation** · [B: Framework](03b_INTERVIEW_FRAMEWORK.md) · [C: Senior](03c_INTERVIEW_SENIOR.md) · [D: Behavioral](03d_INTERVIEW_BEHAVIORAL.md)

---

## 🏗️ SECTION A1 — Architecture + AAOS (Q1–Q12)

### Q1. Describe the architecture of VDiag end-to-end.

**Short:** VDiag clones the **AAOS CarService stack** for the diagnostics domain — 5 layers, 8 boundaries. App uses a `CarDiagnosticManager`-style SDK → Bound Service in a separate `:car_service` process via Binder AIDL → JNI RAII bridge → C++ engine with a 4-tier priority queue and UDS codec (ISO 14229) → pure-virtual `IDiagnosticHal` with swappable implementations (Mock / DoIP / CAN / ADAS). Each boundary is its own technical story: thread-safety, error propagation, resource cleanup.

**Deep dive — the 5 layers:**
1. **App + SDK** — `DiagClient` (= `CarDiagnosticManager`) gives a typed API, hides Binder.
2. **Bound Service** — `DiagCarService` in `:car_service`, receives AIDL, enforces permission, registers callbacks with `linkToDeath`.
3. **JNI Bridge** — `DiagHalBridge` (= `VehicleHal` JNI wrapper), `JniCallbackBridge` manages GlobalRef lifecycle.
4. **C++ Engine** — worker thread, UDS encode/decode, session state machine (Idle→Pending→Done/Error).
5. **HAL** — `IDiagnosticHal` pure virtual (= `IVehicle.aidl`), factory-selected implementation.

**Trap follow-up — "Which layer is hardest and why?"** → JNI boundary: it crosses both the language boundary (Java GC vs C++ RAII) *and* the thread boundary (Binder pool thread creates the callback, engine worker thread invokes it). That's where I invested the RAII `JniCallbackBridge`.

---

### Q2. Map VDiag to real AAOS components (1-to-1).

| AAOS | VDiag | Pattern proven |
|---|---|---|
| `Car.createCar(ctx)` | `DiagClient.create(ctx)` | Service binding |
| `CarHvacManager` / `CarSensorManager` | `DiagClient` | Typed manager façade |
| `CarService` (system_server) | `DiagCarService` (Bound Service, `:car_service`) | System service process |
| `VehicleHal` (JNI) | `DiagHalBridge` | Java↔C++ bridge |
| `IVehicle.aidl` | `IDiagnosticHal` (pure virtual) | HAL contract |
| `DefaultVehicleHal` | `MockDiagnosticHal` | Reference impl |
| `VehiclePropValue` | `DiagRequest` (parcelable) | Data transport |
| `CarPropertyManager.registerCallback` | `DiagClient.subscribeProperty` | Push/subscribe |
| `CarWatchdogClient` | `CarApiSystemClient` | Health heartbeat |

**Interview line:** *"I studied `packages/services/Car/` — CarService uses Bound Service → VehicleHal JNI → IVehicle HAL. VDiag mirrors that 1-to-1 so the naming itself communicates I understand AAOS."*

**Trap — "Did you copy AAOS code?"** → No, I re-implemented the *pattern* from reading the source; VDiag is my own ~10K LOC. The value is the architecture literacy, not the code.

---

### Q3. Why a separate process (`:car_service`) instead of same-process?

**Short:** Three reasons matching AAOS: **stability** (app crash → service survives, `DeathRecipient` cleans up), **security** (separate process = separate SELinux domain + UID), **memory isolation** (Android LMK kills the foreground app first; the service stays alive).

**Deep dive — cost/benefit:** Separate process costs a Binder round-trip (~0.1ms serialize) and a second JVM's memory. But for a system service that must outlive any single client, that's the correct trade. Same-process would be faster but couples lifecycles — exactly what AAOS avoids by putting CarService in system_server, not in each app.

**Trap — "How do you verify it's actually two processes?"** → `adb shell ps -A | grep vdiag` shows `com.vdiag` and `com.vdiag:car_service` with different PIDs.

---

### Q4. Why AIDL, not Messenger or ContentProvider or a socket?

| Option | Why not |
|---|---|
| **Messenger** | Single Handler thread, no typed methods, no `oneway` granularity |
| **ContentProvider** | Built for CRUD/URI data, not RPC with callbacks |
| **Raw socket** | No type safety, manual marshaling, no death notification |
| **AIDL** ✓ | Generated Proxy/Stub, type-safe, Binder thread pool (16), `oneway`, `linkToDeath` |

AAOS uses AIDL for `IVehicle` HAL and every Car API — matching that is the point.

**Trap — "AIDL vs the newer Stable AIDL HAL?"** → I use `@VintfStability` stable AIDL for the HAL contract (see [Part B, Q31](03b_INTERVIEW_FRAMEWORK.md)); app↔service is regular AIDL because both ship in the same APK/signature domain.

---

### Q5. What is `oneway` and why is it critical for the callback interface?

**Short:** `oneway` = fire-and-forget. The caller writes the transaction to the Binder buffer and returns immediately without waiting for the callee. For `IDiagCallback` / `IDiagPropertyListener` it prevents a deadlock: if the service blocked waiting for the client's callback to finish while the client was blocked waiting on the service → circular wait. It also isolates a slow client from stalling the service's dispatch loop.

**Deep dive — thread model:** With `oneway`, `transact()` is called with `FLAG_ONEWAY`; the kernel enqueues the parcel into the *target* process's async buffer, and one of the target's Binder pool threads drains it later. The service's poller never blocks on a slow listener — a 500ms client can't delay a 1Hz SOC gauge for other subscribers.

**Trade-offs:** no return value, no propagated exception (only `DeadObjectException` on death), and the async buffer is smaller (~1/2 of the 1MB) → `TransactionTooLargeException` if you flood it.

**Trap — "Is ordering guaranteed?"** → Yes, `oneway` calls *to the same binder from the same thread* are delivered in order; across different threads/binders, no global ordering.

---

### Q6. How does Parcelable work, and why not Serializable?

**Short:** `DiagRequest` is an AIDL `parcelable` → the compiler generates `writeToParcel`/`createFromParcel` that read/write fields directly into the kernel-mapped Binder buffer. `Serializable` uses reflection (`getDeclaredFields`) + creates garbage — unacceptable for 10Hz events.

**Deep dive — the wire:** Sender's `writeToParcel` writes into the Binder transaction buffer (shared, kernel-mapped). The kernel does a single copy into the receiver's mapped region; the receiver's `createFromParcel` reads from that same buffer. Strings are length-prefixed UTF-16 (`writeString16`), longs are 8 bytes. `IBinder` fields are serialized as strong binder references, not data — that's how a *callback object* crosses the boundary (`writeStrongBinder(listener.asBinder())`).

**Trap — "1MB limit — what breaks and how do you fix it?"** → The whole transaction buffer per process is ~1MB shared across in-flight transactions. A large firmware blob would throw `TransactionTooLargeException`. Fix: pass a `ParcelFileDescriptor` wrapping `ASharedMemory` — only the fd crosses Binder, data via `mmap` (see [Part C, Q49](03c_INTERVIEW_SENIOR.md)).

---

### Q7. Explain the Binder thread pool and its implications.

**Short:** Each process has a Binder thread pool (default max 16). Every incoming transaction runs on one pool thread, so **every Stub method must be thread-safe**. If all 16 are busy, the caller blocks — natural backpressure.

**Deep dive:** The pool starts with 1 thread and spawns up to `maxThreads` on demand via `BINDER_SET_MAX_THREADS`. Because `binderDied` and inbound calls all land on arbitrary pool threads, my `ClientRegistry` uses `ConcurrentHashMap` and the native engine serializes work behind a mutex+condvar. I never assume a call arrives on a specific thread.

**Trap — "What if you need more than 16 concurrent?"** → You can raise the pool size, but usually the fix is to make Stub methods short and offload work to the engine queue (which I do) so Binder threads return quickly and aren't the bottleneck.

---

### Q8. Why is the HAL interface pure-virtual? Which SOLID principle?

**Short:** Open-Closed Principle. `IDiagnosticHal` is pure virtual, so I add `Mock`/`DoIP`/`CAN`/`ADAS` implementations without touching engine code. A factory selects the impl at init. Exactly how AAOS `IVehicle` allows `DefaultVehicleHal` vs a real vendor HAL.

**Deep dive — the contract:** `sendAndReceive(span<uint8_t>) → Result`, `isReady()`, `reset()`, virtual dtor. The engine depends only on that contract (Dependency Inversion). Swapping transport = 0 engine changes; I prove it in CI by running the same engine tests against Mock and a fake DoIP.

**Trap — "How do you test the abstraction actually holds?"** → A contract test suite (gtest typed-test) runs the same assertions against every `IDiagnosticHal` implementation, so a new transport must satisfy identical behavior.

---

### Q9. How does `Stub.asInterface(binder)` decide Proxy vs local?

**Short:** Binder locality optimization. `asInterface` calls `queryLocalInterface` — if the binder lives in the *same* process it returns the `Stub` directly (a plain virtual call, no IPC); otherwise it wraps it in a `Proxy` that serializes through `/dev/binder`. Same interface type, transparent to the caller.

**Trap — "Why does this matter for performance?"** → In-process Car API calls (e.g., a system component talking to CarService when co-located) skip serialization entirely. It's why you code against the interface, never assume IPC cost.

---

### Q10. Walk a single "Read VIN" request end-to-end.

```
tap → DiagClient.getProperty(VIN)
  → [B1 Binder] serialize DiagRequest → /dev/binder
  → DiagServiceBinder.getProperty(req, cb)
       PermissionGate.enforce()  ·  ClientRegistry.register(cb)+linkToDeath
  → DiagHalBridge.nativeGetProperty(1, 0xF190, cb)
       → [B2 JNI] NewGlobalRef(cb)
  → DiagEngine.submit(req, bridge)          [B3 queue → worker]
       uds::encode(0x22,0xF190) → {0x22,0xF1,0x90}
       → MockHal.sendAndReceive(...)        [B4 HAL lookup DID]
         → {0x62,0xF1,0x90,'V','I','N',...}
       uds::decode → positive
       → bridge(result) → AttachCurrentThread + CallVoidMethod
  → [B1 oneway] IDiagCallback.onResult(1,"VINFAST...",ts)
  → DiagListener.onPropertyReceived → runOnUiThread(updateUI)
```

**Latency budget (Mock):** Binder ~0.1ms + JNI ~0.01ms + queue/lookup ~0.05ms + return ~0.15ms ≈ **<1ms**; DoIP over TCP localhost ≈ **~50ms**.

---

### Q11. Where does VDiag stop being "like AAOS" and become a simplification?

**Short (honesty signal):** Two places. (1) Real CarService is registered in `SystemServer` via `CarServiceHelperService` and started at boot; I use a Bound Service for emulator simplicity. (2) Real HAL runs as a `/vendor` binder service registered in VINTF; my Mock/CAN run in-process, DoIP/ADAS over TCP. The *API contract and patterns* are identical; the *deployment plumbing* is documented (bring-up notes) but simulated on emulator.

**Why this answer wins:** Senior interviewers probe for over-claiming. Naming the simplification explicitly builds trust and shows you know the real thing.

---

### Q12. Compare VDiag vs EventStreamCore — why two projects?

**Short:** Complementary planes. **EventStreamCore** = data plane, 10M ev/s, lock-free SPSC, cache-line optimized → proves modern C++ real-time. **VDiag** = control plane, low-frequency RPC (UDS), AAOS pattern → proves Android system integration. Together = full-stack Android Automotive.

**Trap — "Isn't the VDiag engine slow then?"** → Deliberately. UDS is request/response with one pending per session; latency is dominated by the ECU/transport, not my code. I don't chase microseconds where the domain doesn't need it — and I show I *can* (EventStreamCore) where it does. Knowing which problem needs which tool is the senior signal.

---

## 📱 SECTION A2 — Android System Internals (Q13–Q24)

### Q13. DeathRecipient — mechanism, thread, and failure mode without it.

**Short:** `binder.linkToDeath(recipient, 0)` registers a kernel notification. When the client process dies, the Binder driver fires `binderDied()` on *some* Binder pool thread (async). `ClientRegistry` (a `ConcurrentHashMap`) removes the entry and `unlinkToDeath`. Without it, the callback GlobalRef/strong binder leaks → memory grows → eventual OOM.

**Deep dive:** The notification is edge-triggered and one-shot per link. I keep the `DeathRecipient` reference alive (a field), otherwise it's GC'd and never fires. On re-bind, I re-link. For subscriptions I also drop the client's poll entries so I don't dispatch to a dead binder (which would throw `DeadObjectException`).

**Trap — "Which thread does binderDied run on? Can two fire at once?"** → Any Binder pool thread, and yes — multiple clients can die simultaneously → concurrent map mutation → that's the whole reason for `ConcurrentHashMap`, not `HashMap`.

---

### Q14. Why `ConcurrentHashMap` and not `synchronized HashMap`?

**Short:** `binderDied` + inbound registrations happen on arbitrary Binder threads concurrently. `synchronized HashMap` serializes *all* access (one lock) → a burst of deaths blocks new registrations. `ConcurrentHashMap` is lock-striped: concurrent reads are lock-free, writes lock only a bin. Also avoids `ConcurrentModificationException` while iterating during dispatch.

**Trap — "Can you get a stale read during iteration?"** → Yes, `ConcurrentHashMap` iterators are weakly consistent (no CME, may or may not reflect concurrent writes). For a client registry that's fine — a client that registers mid-dispatch just gets the next cycle.

---

### Q15. Permission model — signature vs dangerous vs privileged.

| Type | Grant | VDiag use |
|---|---|---|
| **signature** | Only apps signed with the same cert; no prompt | `permission.DIAGNOSE` — OEM-only diagnostic |
| **dangerous** | Runtime user prompt (camera/location) | Not used for core diag |
| **privileged / signatureOrSystem** | App in `/system/priv-app` + allowlist | Production path |

**Short:** VDiag uses a **signature** permission because diagnostics is OEM-internal — only the OEM-signed app is granted, no user dialog. Production would also mark it privileged and allowlist it in `privapp-permissions-vdiag.xml`.

**Deep dive — enforcement:** `DiagServiceBinder` calls `context.enforceCallingPermission("com.vdiag.permission.DIAGNOSE", msg)` at the top of each method. `getCallingPid/Uid` come from Binder; the check runs in the *service* process against the *caller's* identity — you can't spoof it from the client.

**Trap — "Where is `enforceCallingPermission` vs `checkCallingPermission`?"** → `enforce*` throws `SecurityException`; `check*` returns a value. On the Binder thread I use `enforce*` so a denied caller gets a hard failure, not a silent path.

---

### Q16. Bound Service lifecycle vs Started Service.

**Short:** `bindService(intent, conn, BIND_AUTO_CREATE)` → `onCreate` → `onBind` returns `IBinder` → client's `onServiceConnected`. Service lives while ≥1 client is bound; last unbind → `onUnbind` → `onDestroy`. A Started Service (`startService`) manages its own lifecycle and must `stopSelf`. VDiag uses Bound because clients need an RPC channel, not a fire-and-forget job.

**Trap — "What if you need it to survive all clients unbinding?"** → Combine both: `startService` + `bindService`; then it outlives unbinds until `stopSelf`. AAOS effectively does this by living in system_server.

---

### Q17. What is `android:process=":car_service"` exactly?

The `:` prefix declares a **private** process named `com.vdiag:car_service` — a second JVM in the same UID/signature, isolated memory, reachable only via Binder. A name without `:` (e.g. `com.other:x`) would be a global process shareable across packages. VDiag uses the private form so the service is package-internal.

---

### Q18. Handler / Looper / MessageQueue — where do they appear in VDiag?

**Short:** The `ShimSystemClient` heartbeat (emulator fallback for CarWatchdog) uses a `Handler` on a dedicated `HandlerThread` posting a 3s health check. `DiagClient`'s listener dispatch marshals onto the main `Looper` via `Handler(Looper.getMainLooper())` so `onPropertyReceived` reaches the UI thread safely.

**Deep dive:** A `Looper` owns a `MessageQueue`; `Handler.post` enqueues a `Message`/`Runnable` with a target time; the loop `next()`s in order, blocking on `epoll` when idle. Delayed heartbeats use `postDelayed`. This is the same primitive `CarWatchdog`/`CarPowerManager` shims lean on.

**Trap — "Why not `Timer`/`ScheduledExecutor` for the heartbeat?"** → `Handler` integrates with the Android lifecycle/looper and is trivially cancellable via `removeCallbacks`; a `Timer` thread would need separate lifecycle management and can silently die on uncaught exceptions.

---

### Q19. How would ANR happen here and how do you prevent it?

**Short:** ANR = main thread blocked >5s (input) / broadcast timeout. Risk points: doing Binder calls or DB I/O on the UI thread. Prevention: `DiagClient` calls are async (submit + `oneway` callback), the callback hops to the engine worker thread in native, and only the final UI update runs on main via `runOnUiThread`. StrictMode `penaltyDeath` (debug) catches any accidental main-thread I/O.

---

### Q20. What happens to in-flight requests when the client dies mid-request?

**Short:** The engine keeps processing (it holds a `shared_ptr` `JniCallbackBridge`), but when it tries to invoke the callback the binder is dead → `DeadObjectException` is caught, the bridge's dtor releases the GlobalRef, and `binderDied` has already purged the registry entry. No leak, no crash. The result is simply dropped.

**Trap — "Race between binderDied and the callback firing?"** → Both paths are idempotent: the registry remove is safe on `ConcurrentHashMap`, and the callback catches `DeadObjectException`. Worst case is one wasted HAL round-trip.

---

### Q21. How do you bring VDiag up on real AAOS hardware? (overview)

1. **Cross-compile** the `hal/` daemon (`aarch64-linux-gnu-g++`), APK to `/system/priv-app/VDiag`.
2. **`init.vdiag.rc`** — `service vdiag_hal /vendor/bin/vdiag_hal`, `class late_start`, auto-restart, `writepid`.
3. **SELinux** — define `vdiag_hal` domain, allow Binder to `car_service`, `audit2allow` from denials.
4. **VINTF** — register the HAL version + `@VintfStability` freeze in `manifest.xml`.
5. **Permissions** — `privapp-permissions-vdiag.xml` allowlist.

*"I haven't flashed a board; I documented the flow from AAOS source — full detail in [05_BRINGUP_NOTES.md](05_BRINGUP_NOTES.md) and [Part B, Q43](03b_INTERVIEW_FRAMEWORK.md)."*

---

### Q22. CarService vs a regular Android Service — the real difference.

Regular Service: started by an app intent, lives in the app process. CarService: a **system service** registered in `SystemServer`/`CarServiceHelperService`, started at boot, running with system UID and access to car-specific SELinux domains + VHAL. VDiag uses a Bound Service to *demonstrate the API pattern* (Manager → Service → HAL) without needing a platform build.

---

### Q23. What is StrongBinder / weak binder, and where does it bite?

A strong binder reference keeps the remote object alive across the boundary; the `IDiagCallback` you register is held strongly by the service (that's why cleanup matters). Holding strong references to *client* binders without `unlinkToDeath` is the classic system-service leak. VDiag holds callbacks strongly *only* while registered, and `DeathRecipient` guarantees release.

---

### Q24. Multi-user (AAOS) — does your service behave per-user?

**Short:** AAOS is multi-user (driver, passengers, secondary profiles). A system service must scope data per `UserHandle` and react to `USER_SWITCHED`. VDiag documents this: the DTC store keys by user, and a `UserLifecycleListener` (or broadcast) clears per-user caches on switch. On emulator I simulate with a single user but keep the keying so the design is correct.

**Trap — "Foreground vs background user?"** → Diagnostic writes belong to the *system*, not a user, so the store is system-scoped; only UI preferences are per-user. I call that boundary out explicitly.

---

## 🔧 SECTION A3 — JNI Deep Dive (Q25–Q34)

### Q25. The three biggest JNI pitfalls and your one-design fix.

1. **Local ref stored globally** → dangling after the native method returns → crash. Fix: `NewGlobalRef` in ctor.
2. **`FindClass` on a worker thread** → no ClassLoader → `null`/`ClassNotFound`. Fix: cache class + methodIDs in `JNI_OnLoad`.
3. **`AttachCurrentThread` without detach** → JVM thread-local leak. Fix: `pthread_key_create` with a destructor → auto-detach on thread exit.

All three solved by one RAII type: **`JniCallbackBridge`**.

---

### Q26. Walk the JniCallbackBridge RAII design in detail.

**Ctor (on Binder thread, has JNIEnv):** `NewGlobalRef(callback)` — pins it against GC. **Move into engine queue** via `shared_ptr`. **Invoke (on engine worker thread):** `getEnv()` — if `GetEnv` returns `JNI_EDETACHED`, call `AttachCurrentThread` and register a `pthread_key` whose destructor detaches on thread exit; then `CallVoidMethod(gRef, gOnResult, ...)`. **Dtor:** `DeleteGlobalRef` when the last `shared_ptr` drops. **Move-only** (deleted copy) so two owners can't both `DeleteGlobalRef`.

**Trap — "Why shared_ptr, not unique_ptr?"** → The callback may be referenced by both the in-flight work item and a retry path; `shared_ptr` ref-count gives deterministic release exactly when the last user finishes — and it's exception-safe.

---

### Q27. JNI method signature format — decode `(ILjava/lang/String;J)V`.

Params `(int, String, long)`, returns `void`. `I`=int, `J`=long, `[B`=byte[], `L…;`=object, `V`=void, `Z`=boolean, `F`=float. Generate with `javap -s -p`. VDiag: `onResult(ILjava/lang/String;J)V`, `onError(IILjava/lang/String;)V`.

**Trap — "Signature mismatch symptom?"** → `GetMethodID` returns null and a later call crashes with `NoSuchMethodError` (or CheckJNI aborts). I cache IDs in `JNI_OnLoad` so a mismatch fails fast at load, not at runtime.

---

### Q28. Why cache class/method IDs in JNI_OnLoad specifically?

`JNI_OnLoad` runs on the thread that called `System.loadLibrary()` — it has the app ClassLoader. C++ worker threads created later have no ClassLoader, so `FindClass` there fails. MethodIDs are stable once the class is loaded, so caching is safe and avoids per-call lookup. I also `NewGlobalRef` the cached `jclass` (FindClass returns a local ref that would otherwise die).

---

### Q29. What is CheckJNI and what does it catch?

Debug validator: `adb shell setprop debug.checkjni 1` (on by default in debuggable builds). Catches wrong-thread JNIEnv use, invalid/stale refs, ref-table overflow, signature mismatches, pending-exception violations. ~10% overhead. **VDiag runs clean (0 warnings)** — a required gate before release.

**Trap — "Give a concrete bug CheckJNI found for you."** → Early on I passed a JNIEnv captured on the Binder thread into the worker thread; CheckJNI flagged "accessed JNIEnv from wrong thread" → that's what drove the `AttachCurrentThread`/`GetEnv` design.

---

### Q30. What happens if you forget DeleteGlobalRef?

The JNI global reference table grows (default cap ~51,200). Each `nativeGetProperty` adds one. After ~50K requests → `JNI ERROR: global reference table overflow` → abort. The RAII dtor releases regardless of the exception path, so a thrown exception mid-callback still frees the ref.

---

### Q31. Local vs Global vs Weak Global references.

- **Local:** valid only during the native call, on that thread; auto-freed on return; limited slots (`EnsureLocalCapacity`/`PushLocalFrame`).
- **Global:** survives across calls/threads until `DeleteGlobalRef`; used for cached classes + the callback.
- **Weak Global:** doesn't prevent GC; must `NewLocalRef` and null-check before use — useful to cache without pinning. VDiag uses Global for callbacks (must pin) and Global for cached classes.

**Trap — "When would a WeakGlobalRef be right here?"** → If I cached a listener I *don't* own the lifecycle of and want it collectable when the app drops it — but for an actively-registered callback I must pin it, so Global is correct.

---

### Q32. How do you propagate a C++ exception/error back to Java safely?

C++ side never throws across JNI. The engine returns a `DiagResult{status, nrc, payload}`. On error the bridge calls `onError(reqId, nrc, msg)` instead of `onResult`. Before any subsequent JNI call I check `env->ExceptionCheck()` and clear if needed — a pending Java exception makes the next JNI call illegal (CheckJNI aborts).

---

### Q33. Passing a `byte[]` efficiently across JNI — copy vs critical.

`GetByteArrayElements` may copy; `GetPrimitiveArrayCritical` gives a direct pointer but forbids other JNI/blocking calls while held (it can pause GC). For small UDS payloads (~tens of bytes) I use `GetByteArrayRegion` into a stack buffer — no lifetime management, no GC pause. For a large snapshot I'd use `ASharedMemory` (Part C) rather than a big array copy.

**Trap — "Why not always Critical for speed?"** → Holding a critical section across the HAL round-trip (blocking I/O) would stall GC and risk deadlock. Never do blocking work inside `GetPrimitiveArrayCritical`.

---

### Q34. JNI vs JNA vs direct NDK — why JNI here?

JNI is the platform-standard, zero-extra-dependency bridge and exactly what `VehicleHal` uses. JNA (reflection-based, slower, mostly desktop) isn't idiomatic on Android. "Direct NDK" still uses JNI underneath. Matching AAOS means JNI with hand-written glue + a small RAII layer.

---

## 🔌 SECTION A4 — UDS + Automotive Protocol (Q35–Q42)

### Q35. UDS ISO 14229 request/response framing.

- **Request:** `[SID, subFn/DID hi, DID lo, data…]`
- **Positive:** `[SID+0x40, echo, data…]`
- **Negative:** `[0x7F, SID, NRC]`

Example ReadDID 0xF190: `{0x22,0xF1,0x90}` → `{0x62,0xF1,0x90,'V','I','N',...}`. Key NRCs: `0x31` Request Out Of Range, `0x78` Response Pending, `0x11` Service Not Supported, `0x33` Security Access Denied.

---

### Q36. Which UDS services did you implement and why those?

`0x22` ReadDataByIdentifier, `0x2E` WriteDataByIdentifier, `0x14` ClearDTC, `0x19` ReadDTCInformation, `0x10` DiagnosticSessionControl, `0x3E` TesterPresent. Those six cover the realistic read/monitor/session lifecycle a diagnostic manager needs; each has gtest round-trip encode/decode with edge cases (truncated frame, wrong SID echo, NRC paths).

---

### Q37. UDS timing — P2, P2*, S3. Where do they live in your code?

- **P2 = 50ms:** ECU must respond, or send `0x78` within ~25ms. My engine's `sendAndReceive` timeout is 50ms.
- **P2\* = 5000ms:** after `0x78`, final response due within 5s → I retry-wait with a 5s budget.
- **S3 = 5s:** TesterPresent must be sent every 5s to keep a non-default session → `SubscriptionManager` sends `0x3E` heartbeat when session == Extended.

**Trap — "What does 0x78 mean and why send it?"** → Response Pending: the ECU acknowledges but needs more time (e.g., flash erase). It resets the P2 timer to P2* so the tester doesn't time out. Knowing this signals you've read the spec, not a tutorial.

---

### Q38. Why mock the ECU instead of a real one?

Deterministic CI (exact expected bytes), hardware-free demo, and it *proves the abstraction*: swapping `MockDiagnosticHal` → `DoipDiagnosticHal` is 0 engine changes. Mock also injects delays/errors/edge cases I can't reliably trigger on real hardware.

---

### Q39. Why a single worker thread in the engine (vs a thread pool)?

UDS is request/response with **one pending request per session** (spec). Parallel workers would just serialize at the transport anyway. Single worker = correct + simpler. For multiple ECUs I run **one engine per ECU** (each own worker/queue/session) since sessions to *different* target addresses are independent — see [Part B, Q40](03b_INTERVIEW_FRAMEWORK.md).

---

### Q40. DoIP (ISO 13400) vs raw TCP.

DoIP is UDS-over-TCP with an 8-byte header: magic `0x02FD`, protocol version + inverse, payload type, payload length, then source/target logical addresses, then the UDS bytes. Default port 13400. `DoipDiagnosticHal` frames UDS → DoIP → TCP send → recv → strip header → return UDS. A Python ECU simulator validates it in CI (`adb forward tcp:13400`).

**Trap — "What DoIP payload types matter?"** → `0x8001` Diagnostic message, `0x8002/0x8003` ack/nack, `0x0005` routing activation request/response (you must activate routing before diagnostics). I handle routing activation before the first UDS frame.

---

### Q41. CAN / ISO-TP — how does segmentation work?

ISO 15765-2 (ISO-TP) segments UDS over 8-byte CAN frames: **SF** (single frame ≤7B), **FF** (first frame, length + first 6B), **CF** (consecutive frames with a 4-bit sequence counter), **FC** (flow control: continue/wait/overflow + block size + STmin). `CanDiagnosticHal` on `vcan0` reassembles multi-frame responses and honors STmin spacing. Detail in [Part B, Q42](03b_INTERVIEW_FRAMEWORK.md).

---

### Q42. How does a DTC read (0x19) actually work, and what's a DTC?

A **DTC** (Diagnostic Trouble Code) is a fault identifier (e.g., `P0A80` battery pack fault) plus a status byte (test failed, confirmed, pending…). `0x19` with subfunction `0x02` (reportDTCByStatusMask) returns matching DTCs. VDiag decodes the 3-byte DTC + 1-byte status, classifies severity (`DtcClassifier`), and persists to Room (Part C). `0x14` ClearDTC wipes them.

**Trap — "Difference between pending, confirmed, permanent DTC?"** → Pending = seen once (may be intermittent); confirmed = seen across enough drive cycles; permanent = confirmed + can't be cleared by tester (only the ECU clears after verifying the fix). Emissions regs care about permanent DTCs.

---

*Continue → [Part B: Framework depth (v2.0)](03b_INTERVIEW_FRAMEWORK.md)*
