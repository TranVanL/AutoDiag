# 🎤 VDiag — Interview Prep (130+ Q&A + Demo Script)

> Mọi câu trả lời gắn VDiag với **AAOS CarService stack + modern Android architecture** — dual USP cho Senior roles (Automotive / Framework / Common). Đọc to lớn tiếng trước phỏng vấn.
>
> **16 sections coverage:**
> - Parts 1-6: v1.0 foundation (Architecture, Binder, JNI, HAL, Subscription, Bring-up)
> - Parts 7-12: v2.0 depth (Stable AIDL, HAL resilience, CAN/ADAS, QA, Embedded, Observability)
> - **Parts 13-16: v3.0 Senior (Advanced IPC, Testing Pyramid, MVVM/Room, Background/Security)**

---

## 🎯 Multi-JD framing (đọc trước khi vào phỏng vấn)

VDiag designed để cover NHIỀU loại Android Automotive JD. Trước phỏng vấn, identify JD type rồi chọn opening line đúng:

| JD type | Opening line (10 giây đầu) | Emphasis trong câu chuyện |
|---|---|---|
| **Senior Android Automotive** | *"I built VDiag — an Android Automotive framework portfolio that clones the AAOS CarService stack end-to-end."* | CarPropertyManager pattern · CarWatchdog/PowerManager · multi-process service · signature permission |
| **Android Framework / Platform** | *"VDiag is a framework engineering project — Binder IPC, JNI lifecycle, AIDL `@VintfStability`, multi-process Android service, bring-up artifacts."* | DeathRecipient internals · `oneway` semantics · AIDL versioning · init.rc/SELinux/VINTF |
| **Android HAL / System Software** | *"VDiag is a HAL design portfolio — pure-virtual `IDiagnosticHal` with 4 swappable implementations, JNI RAII bridge, NDK standalone library, ARM64 cross-compile + QEMU."* | HAL abstraction · `IDiagnosticHal` swap demo · standalone `hal/` library · QEMU CI |
| **Self-Driving / ADAS C++** | *"My main C++ depth is in EventStreamCore — 10M ev/s lock-free engine. VDiag adds the Android system integration angle plus an `IAdasSensorHal` extension."* | Lead with EventStreamCore · use VDiag's ADAS HAL as Android/HAL angle |
| **Generic Senior Embedded** | *"Two complementary projects: EventStreamCore for data-plane real-time C++, VDiag for control-plane Android Automotive integration."* | Mention both projects equally |

> **Honesty rule khi hỏi ADAS / sensor fusion:** *"My ADAS HAL is a reusability proof — same `IDiagnosticHal` factory pattern extended to a sensor domain, with a complementary IMU filter and a nearest-neighbor radar tracker. It's not production sensor fusion. My deep C++ engineering is in EventStreamCore."*

---

## 🏗️ PART 1: Architecture + AAOS (Q1-10)

### Q1. Describe the architecture of VDiag.

VDiag follows the **Android Automotive OS CarService pattern**. Five layers, four boundaries:

1. **App + SDK** — DiagClient (like CarDiagnosticManager) provides typed API
2. **Bound Service** — DiagCarService runs in `:car_service` process, receives AIDL calls
3. **JNI Bridge** — DiagHalBridge connects Java service to C++ HAL with RAII reference lifecycle
4. **C++ Engine** — DiagEngine with worker thread, UDS codec (ISO 14229), session state machine
5. **HAL** — IDiagnosticHal pure virtual, same pattern as IVehicle.aidl in AAOS

Each boundary handles thread safety, error propagation, resource cleanup.

### Q2. How does VDiag map to real AAOS?

| AAOS | VDiag | Same Pattern? |
|---|---|---|
| Car.createCar(ctx) | DiagClient.create(ctx) | ✓ Service binding |
| CarHvacManager | DiagClient | ✓ Typed manager |
| CarService | DiagCarService | ✓ Bound Service + separate process |
| VehicleHal | DiagHalBridge | ✓ JNI bridge to C++ |
| IVehicle.aidl | IDiagnosticHal | ✓ Pure virtual HAL interface |
| DefaultVehicleHal | MockDiagnosticHal | ✓ Reference implementation |

I studied AAOS source code (`packages/services/Car/`) to build this. VinFast VF8 uses same pattern for HVAC, sensor, cabin control.

### Q3. Why separate process (`:car_service`)?

Three reasons matching AAOS design:
1. **Stability** — app crash → service survives, DeathRecipient cleans up
2. **Security** — separate process = separate SELinux context
3. **Memory isolation** — Android LMK kills app first, service more persistent

Verify: `adb shell ps | grep vdiag` shows 2 processes.

### Q4. Why AIDL, not Messenger or ContentProvider?

- **Messenger:** single-threaded Handler, no custom types
- **ContentProvider:** designed for CRUD data, not RPC
- **AIDL:** generated Proxy/Stub, type-safe, multi-threaded (Binder pool 16 threads), supports `oneway`

AAOS uses AIDL for IVehicle HAL and all Car APIs.

### Q5. What is `oneway` and why critical for IDiagCallback?

`oneway` = asynchronous one-way call. Service sends transaction to Binder buffer and returns immediately, doesn't wait for client to process. Prevents deadlock: if service waits for callback while client waits for service → circular wait. Trade-off: no return value, no delivery guarantee. AAOS uses oneway for all HAL callbacks.

### Q6. How does Parcelable work for DiagRequest?

DiagRequest is AIDL `parcelable` → auto-generates `writeToParcel/createFromParcel`. Fields serialized to byte stream → copied through Binder kernel driver (`/dev/binder`) → deserialized in receiver process. Transaction limit: 1MB. For large data (firmware): use `ParcelFileDescriptor` with shared memory.

### Q7. What is Binder thread pool?

Default 16 threads per process. Every incoming Binder transaction runs on one pool thread. Implication: **all Stub methods must be thread-safe**. I use `ConcurrentHashMap` for ClientRegistry, native engine uses mutex/cv for queue. If all 16 busy → caller blocks (natural backpressure).

### Q8. Why pure virtual HAL interface?

Open-Closed Principle. `IDiagnosticHal` is pure virtual → MockDiagnosticHal for testing, DoipDiagnosticHal for real ECUs, future CanDiagnosticHal — add new implementations without changing engine code. Exactly how AAOS IVehicle works.

### Q9. Compare VDiag with EventStreamCore.

Complementary, not redundant.
- **EventStreamCore**: data plane, 10M events/s, lock-free SPSC, cache-line optimized → proves C++ performance
- **VDiag**: control plane, low-frequency RPC (UDS), AAOS pattern → proves Android system integration

Together = full-stack Android Automotive engineer.

### Q10. How does `Stub.asInterface(binder)` work?

**Binder locality optimization**. Same process → returns Stub directly (no IPC, just local method call). Different process → returns Proxy (serialize → Binder driver → deserialize). Same interface, transparent to caller. Core Binder pattern, all AAOS Car APIs use it.

---

## 📱 PART 2: Android System (Q11-18)

### Q11. DeathRecipient — how does it work?

`IBinder.linkToDeath(DeathRecipient, flags)` registers kernel notification. When client process dies, Binder driver fires `binderDied()` on **any** Binder pool thread (asynchronous). My ClientRegistry uses ConcurrentHashMap (thread-safe), removes dead client entry, calls unlinkToDeath. Without this → callback ref leaks → OOM over time.

### Q12. Permission model: signature vs dangerous?

- **signature** — only apps signed with same certificate granted, no user prompt. Perfect for OEM system services
- **dangerous** — runtime user approval (camera, location)

VDiag uses signature because diagnostic = OEM-only. Production: add `privileged` (app in `/system/priv-app/`) + privapp-permissions.xml allowlist.

### Q13. What is `android:process=":car_service"`?

`:` prefix = private process to this package. Full name: `com.vdiag:car_service`. Separate JVM, separate memory. Communication only via Binder IPC. AAOS CarService uses similar pattern.

### Q14. Bound Service lifecycle?

`bindService(intent, conn, BIND_AUTO_CREATE)` → system creates service if needed → `onCreate()` → `onBind()` returns IBinder → `ServiceConnection.onServiceConnected()` fires in client. All clients unbind → `onUnbind()` → `onDestroy()`. Lives as long as ≥1 client bound. Different from Started Service (manages own lifecycle).

### Q15. How would you bring up VDiag on a real AAOS device?

1. **init.rc:** `service vdiag /system/bin/vdiag_hal_service` — start at boot
2. **SELinux:** define `vdiag_hal` type, allow Binder communication with CarService
3. **VINTF manifest:** register HAL version in `/vendor/etc/vintf/manifest.xml`
4. **privapp-permissions:** allowlist DIAGNOSE permission for app
5. **System partition:** APK in `/system/priv-app/VDiag/`

I haven't deployed to real device, but I documented the bring-up flow from studying AAOS source — see `docs/bringup.md`.

### Q16. What if Binder 1MB transaction limit hit?

DiagRequest ~50 bytes, never a problem. For large data (ECU firmware, memory dump): use `ParcelFileDescriptor` for shared memory or pipe. Only file descriptor crosses Binder, actual data via mmap. AAOS camera HAL uses this for frame buffers.

### Q17. CarService vs regular Android Service?

CarService is a **system service** registered in SystemServerRegistry, started during boot by SystemServer. Regular Service started by app intent. VDiag uses Bound Service for demo simplicity. Production AAOS would extend via `CarServiceHelperService`. API pattern (Manager → Service → HAL) is identical.

### Q18. Why ConcurrentHashMap in ClientRegistry?

`binderDied()` fires on **any** of 16 Binder pool threads asynchronously. Multiple clients can die simultaneously → concurrent modifications. `HashMap` would crash with `ConcurrentModificationException`. `ConcurrentHashMap` is lock-striped, allows concurrent reads + safe concurrent writes.

---

## 🔧 PART 3: JNI (Q19-24)

### Q19. Three biggest JNI pitfalls and your solutions?

1. **Local ref stored globally** → dangling → crash. **VDiag:** `JniCallbackBridge` ctor calls `NewGlobalRef`, dtor `DeleteGlobalRef` — RAII.
2. **FindClass on worker thread** → no ClassLoader → null. **VDiag:** Cache in `JNI_OnLoad` (runs on thread with ClassLoader).
3. **AttachCurrentThread without detach** → JVM resource leak. **VDiag:** `pthread_key_create` with destructor → auto-detach on thread exit.

### Q20. JniCallbackBridge RAII design?

Created on Binder thread (has JNIEnv) when `nativeGetProperty` called. **Constructor** calls `NewGlobalRef(callback)` — prevents GC. Moved via `shared_ptr` into engine work queue. Invoked on engine worker thread: `getEnv()` checks if attached, if not → `AttachCurrentThread` + setup pthread_key auto-detach. **Destructor** calls `DeleteGlobalRef` when shared_ptr ref count → 0. Move-only semantics prevent double `DeleteGlobalRef`.

### Q21. JNI method signature format?

`(ILjava/lang/String;J)V` = parameters (int, String, long), return void.

Rules: `I`=int, `J`=long, `[B`=byte[], `L<class>;`=object, `V`=void.

Tool: `javap -s -p MyClass` generates signatures.

VDiag callbacks: `onResult(ILjava/lang/String;J)V`, `onError(IILjava/lang/String;)V`.

### Q22. Why cache class/method in JNI_OnLoad?

`JNI_OnLoad` runs on thread that called `System.loadLibrary()` — has ClassLoader. C++ worker threads created later have NO ClassLoader → `FindClass` fails. Method IDs are stable once class loaded → cache once, use forever. Also avoids ~1μs overhead per call (negligible but clean).

### Q23. What is CheckJNI?

Debug mode: `adb shell setprop debug.checkjni 1`. Enables extended validation: local/global ref validity, wrong-thread access, ref table overflow, null pointer dereference, exception state. ~10% perf penalty. **VDiag passes with 0 warnings**. Must run clean before release.

### Q24. What if you forget DeleteGlobalRef?

JNI global ref table grows. Default ~51,200 refs. Each `nativeGetProperty` creates one GlobalRef. After ~50K requests → `JNI ERROR: global reference table overflow` → JVM abort. VDiag's RAII destructor prevents this regardless of exception path.

---

## 🔌 PART 4: UDS + Automotive (Q25-28)

### Q25. UDS ISO 14229 request/response format?

- **Request:** `[ServiceID, SubFunction/DID, payload]`
- **Positive response:** `[ServiceID + 0x40, data]`
- **Negative:** `[0x7F, ServiceID, NRC]`

Example: ReadDID 0xF190 → request `{0x22, 0xF1, 0x90}` → positive `{0x62, 0xF1, 0x90, 'V', 'I', 'N', ...}`.

NRC 0x31 = Request Out Of Range, 0x78 = Response Pending.

### Q26. Why mock instead of real ECU?

1. **Demo without hardware** — CI/CD friendly
2. **Deterministic** — exact expected responses for testing
3. **Architecture proof** — `IDiagnosticHal` swap = 0 engine changes (Open-Closed)

Mock also allows injecting delays, errors, edge cases.

### Q27. Why single worker thread in DiagEngine?

UDS is request-response, **one pending request per session** (ISO 14229 spec). Multiple workers would serialize at transport layer anyway. Single worker = simpler, correct for domain.

**Real-time upgrade (B7):** VDiag uses a **priority-tiered queue** — safety-critical requests (DTC read, session control) get higher priority than routine reads (VIN, software version). Worker runs at `SCHED_FIFO` priority → preempts normal threads. If needed: multiple engines for multiple ECUs, each with own worker.

### Q28. How does DoIP differ from raw TCP?

DoIP (ISO 13400) is **UDS-over-TCP framing**: DoIP header (8 bytes: magic 0x02FD + payload type + length + addresses) wraps UDS payload. Default port 13400. My DoipDiagnosticHal wraps UDS bytes in DoIP frame → TCP send → recv → strip DoIP → return UDS response. Python ECU simulator validates this in CI.

---

## 🤝 PART 5: Behavioral (Q29-30)

### Q29. Biggest challenge in this project?

**JNI callback lifecycle across thread boundaries.** Initial approach: stored callback as local ref → crash after native method returned. Used CheckJNI + ASAN to diagnose → learned about GlobalRef. Then discovered engine worker thread can't call Java without `AttachCurrentThread` → added auto-detach via `pthread_key`. Built `JniCallbackBridge` as RAII solving all 3 pitfalls in one design.

**Lesson:** boundary code (JNI, IPC) requires more careful resource management than business logic.

### Q30. Why did you build VDiag?

My background is C++ systems (LG AUTOSAR production). VinFast JD requires **Android system software, Android HALs, Java, and embedded systems**. VDiag fills the gap: proves I understand the full AAOS stack (App → HAL) AND embedded target deployment (ARM cross-compile). Not a toy — uses production patterns (CarService architecture, DeathRecipient, RAII JNI, SCHED_FIFO, UDS protocol). Combined with production C++ experience = full-stack Android Automotive engineer.

---

## � PART 6: System Design & Scalability (Q31-35)

> Các câu hỏi về thiết kế hệ thống và khả năng mở rộng — hoàn toàn defend được bằng code và design docs.

### Q31. Why did you add a priority queue to DiagEngine?

Single FIFO queue gây **priority inversion**: LOW priority routine read (VIN) block CRITICAL safety alert (DTC). Trong xe tự lái, safety-critical requests phải được xử lý trước.

**VDiag solution:** 4 separate queues — no shared mutex between tiers:

```cpp
std::deque<WorkItem> critical_queue_;  // session control, safety DTC
std::deque<WorkItem> high_queue_;      // real-time property reads
std::deque<WorkItem> normal_queue_;    // routine VIN, software version
std::deque<WorkItem> low_queue_;       // background polls
```

Worker drain order: CRITICAL → HIGH → NORMAL → LOW. High-priority work **never blocks** on low-priority mutex because they're separate queues. Classic pattern from RTOS design (FreeRTOS dùng separate `xQueueHandle` per priority).

*Why not single priority_queue with comparator?* — `std::priority_queue` doesn't support O(1) push to front for CRITICAL. Deques give O(1) push_front if needed + simpler drain logic.

### Q32. How would you scale VDiag to handle multiple ECUs simultaneously?

Current design: 1 engine + 1 HAL = 1 ECU connection. Multi-ECU architecture:

```
DiagCarService
   │
   ├── DiagEngine[body]  → DoipHal → Body ECU (TCP 13400)
   ├── DiagEngine[powertrain] → DoipHal → Powertrain ECU (TCP 13401)
   └── DiagEngine[adas]  → DoipHal → ADAS ECU (TCP 13402)
```

Each engine: own worker thread, own priority queue, own session state machine. UDS spec allows concurrent sessions to **different** ECUs (different target addresses). No cross-engine locking needed.

`DiagCarService` routes request to correct engine based on `DiagRequest.ecuId`. Client API unchanged — same `getProperty(req, cb)`.

### Q33. What bring-up steps if VDiag were deployed on real AAOS hardware?

*(Documented in [05_BRINGUP_NOTES.md](05_BRINGUP_NOTES.md))*

1. **Cross-compile:** HAL layer (`hal/`) uses CMake với no Android dependency — compile bằng NDK cho Android, hoặc `aarch64-linux-gnu-g++` cho standalone daemon
2. **Partition layout:** APK → `/system/priv-app/VDiag/`, native service → `/vendor/bin/vdiag_hal`
3. **`init.rc`:** `service vdiag_hal /vendor/bin/vdiag_hal`, class `hal`, auto-restart
4. **SELinux:** define `vdiag_hal` domain type, allow Binder communication
5. **VINTF:** register HAL version in manifest.xml
6. **Permission:** `privapp-permissions-vdiag.xml` allowlist cho signature permission

Tôi chưa deploy lên real device, nhưng document đầy đủ từ AAOS source code. Trong production tôi sẽ làm việc với BSP team (step 1-2) và security team (step 4).

### Q34. How does VDiag handle UDS timing requirements?

ISO 14229 defines strict response timeouts:
- **P2 = 50ms:** ECU must respond within 50ms, hoặc gửi `0x78 ResponsePending` trong 25ms
- **P2\* = 5000ms:** Sau `0x78`, ECU phải trả lời final trong 5s
- **S3 = 5s:** TesterPresent phải gửi mỗi 5s để giữ ExtendedDiagnostic session

VDiag `DiagEngine`: timeout 50ms cho `sendAndReceive`. Nếu `0x78` → retry với 5s budget. `SubscriptionManager` gửi TesterPresent heartbeat khi session type == Extended.

*Interview follow-up: "NRC 0x78 = Response Pending — ECU đang xử lý, chưa sẵn sàng. Đây là UDS non-default session timing. Interviewer hỏi câu này để check anh có đọc spec thật không."*

### Q35. How would you add metrics and observability to VDiag?

Diagnostic của một diagnostic system — tầng monitoring:

```
DiagEngine records per-request:
  - enqueue_ns     (timestamp khi submit)
  - dequeue_ns     (timestamp khi worker pick up)
  - response_ns    (timestamp khi callback fires)
  - queue_tier     (CRITICAL/HIGH/NORMAL/LOW)

→ wait_latency    = dequeue_ns - enqueue_ns
→ processing_time = response_ns - dequeue_ns
→ total_latency   = response_ns - enqueue_ns
```

Expose qua `DiagEngine::getMetrics()` → JNI → Java → Android metrics API (StatsLog hoặc CarDiagnosticManager equivalent). Alert nếu P99 latency của CRITICAL tier > 10ms.

---

## 🚗 PART 7: Automotive Context + Secondary Self-Driving Angle (Q36-39)

> Câu hỏi về domain knowledge — trả lời honest, không claim implement những gì chưa có hardware.

### Q36. How does vehicle diagnostics relate to self-driving?

Autonomous vehicle stack cần **3 planes**:

```
Perception   → camera, radar, lidar → object detection, localization
Planning     → path planning, decision making
Control      → throttle, brake, steering actuators

          ↕ (cross-cutting)
Diagnostics → health monitoring cho cả 3 planes
```

VDiag covers the **diagnostics plane**:
- ECU health (UDS DID reads, DTC monitoring) → đã implement
- Sensor health (là dạng "DTC từ sensor" thay vì ECU) → kiến trúc hỗ trợ qua `IDiagnosticHal` abstraction
- Actuator health (brake ECU DTC, steer-by-wire fault) → same pipeline

*"VDiag là health monitoring backbone. Perception team lo algorithm, tôi lo infrastructure layer bên dưới — hardware talk, fault detection, reporting pipeline."*

### Q37. What is sensor fusion? Where does VDiag fit?

Sensor fusion = combine inputs từ nhiều sensor → estimate chính xác hơn bất kỳ sensor đơn lẻ nào.

```
Camera  (high-res, fails in rain) ─┐
Radar   (accurate depth, rain-ok) ─┼→ Fusion → confident object bbox + depth
LiDAR   (3D point cloud)          ─┘
```

VDiag không implement fusion algorithm (đó là ML/CV domain, perception team). VDiag là **infrastructure layer phía dưới**:

```
[Real sensor]          [VDiag diagnostic role]
Radar ECU          →   Read DTC, check health, report to OTA
Camera health      →   Monitor driver health status via HAL
Fusion output      →   Monitor update rate, alert if occupancy grid stale
```

*Honest line: "Tôi không build perception/fusion algorithms — đó là domain riêng cần ML background. Tôi build embedded system layer bên dưới — HAL, data pipeline, fault monitoring. Hai team cộng tác."*

### Q38. What safety concepts are most relevant to your work?

From AUTOSAR production experience tại LG:

| Concept | Meaning | VDiag example |
|---|---|---|
| **Watchdog** | Detect stuck/dead process | `DiagWatchdogClient` → CarWatchdog 3s heartbeat |
| **Error containment** | Fault in A ≠ cascade to B | Process isolation: app crash ≠ service crash (DeathRecipient) |
| **Fail-safe** | Fault → defined safe state, not random | HAL error → NRC returned, not exception crash |
| **ASIL** | Safety Integrity Level A-D | Diagnostic monitor là ASIL-B (low risk, high coverage) |
| **ISO 26262** | Automotive functional safety standard | Tôi hiểu concept từ AUTOSAR, chưa cert engineer |

*"Tôi hiểu những khái niệm này từ LG AUTOSAR work. ISO 26262 Part 6 software development process — tôi đã follow qua coding guidelines và review, không phải safety engineer certified."*

### Q39. If you joined VinFast self-driving team, what would your first 30 days look like?

Honest, grounded answer:

> *"Tuần 1-2: study codebase — hiểu HAL layer đang dùng gì (AIDL stable HAL? HIDL legacy?), hiểu ECU topology và DoIP/CAN configuration, hiểu CI/CD pipeline. Tuần 3-4: pick up một task nhỏ để get hands-on — ví dụ add 1 DID support, fix 1 DTC parsing bug, hoặc improve test coverage cho HAL layer. Mục tiêu: gây ấn tượng bằng quality of work, không phải tốc độ. Day 30: đã có 1 merged PR, đã review được code của team member."*

---

## 🤝 PART 8: Behavioral (Q40-42)

### Q40. You have 3 years experience but this role expects 5+. Why apply?

> *"3 năm của tôi là production C++ embedded tại LG — ship AUTOSAR trên real hardware, không phải side projects. VDiag chứng minh tôi tự học được AAOS stack (AIDL, Binder, JNI, HAL, UDS) ở mức production pattern — không phải tutorial copy-paste. Gap của tôi là production AAOS platform team experience và self-driving perception domain — tôi honest về điều đó. Nhưng embedded systems, Android HAL, Binder/JNI boundary work, C++ — tôi contribute được ngay từ ngày đầu. Và tôi học nhanh."*

### Q41. Biggest technical challenge you solved?

*(Adapt từ LG AUTOSAR experience — template):*

> *"Tại LG, chúng tôi có deadlock trong AUTOSAR SWC communication layer, chỉ xuất hiện khi CAN bus load cao. Normal logging che khuất timing. Tôi thêm lightweight trace timestamps tại mỗi IPC boundary — tương tự cách tôi thiết kế 6 boundaries trong VDiag — replicate load condition trong HiL simulator, tìm ra 2 SWC gọi nhau ở reverse lock order. Fix bằng strict lock hierarchy — cùng nguyên tắc tôi dùng trong DiagEngine: không bao giờ lock across boundaries."*

### Q42. Where do you want to be in 3 years?

> *"Tôi muốn là engineer sở hữu embedded HAL và system software layer cho một automotive platform production — viết code nằm giữa hardware và software stack, đảm bảo data reliable, diagnostics real-time, system tự hồi phục được. VDiag là bước đầu. Mục tiêu của tôi là đi sâu vào Android Automotive / platform integration ở production scale, đồng thời giữ C++ systems depth mạnh."*

---

## 🎬 DEMO SCRIPT — 2 phút (clean version)

### 30-Second Pitch:

> *"VDiag là vehicle diagnostic platform follow Android Automotive OS CarService architecture. App dùng CarDiagnosticManager-style SDK → Bound Service qua Binder AIDL → JNI bridge với RAII reference lifecycle → C++ engine với priority queue → Vehicle HAL abstraction (mock cho test, DoIP cho real ECU). Same pattern VinFast VF8 dùng cho CarHvacManager, CarSensorManager — tôi apply cho diagnostics. Có 6 boundaries, mỗi cái là 1 deep technical story."*

### Live Demo Steps:

1. **Open app** → dark automotive theme, 6 buttons
2. **Tap "Read VIN"** → `VINFAST12345678901 (3ms)`
3. **Tap "Battery SOC"** → `78%`
4. **Tap "Read DTC"** → `P0A00, P0562`
5. **Tap "Clear DTC"** → `OK` → tap Read DTC lại → `(empty)` ← mock state mutated
6. **Show Logcat** filter `VDiag` → 4 tags theo thứ tự: `App → Binder → JNI → App`
7. **Show 2 processes**: `adb shell ps | grep vdiag` → `com.vdiag` + `com.vdiag:car_service`
8. **Kill app**: `adb shell am force-stop com.vdiag` → Logcat: `☠ Client died — auto-removed`

### Bonus (nếu DoIP done):
9. **Switch HAL** sang DoIP → **Start Python sim**: `python3 tools/ecu_simulator/doip_server.py &`
10. **Tap "Read VIN"** → `VINFAST...` từ Python (~50ms) — show Wireshark screenshot DoIP frames

---

## 🎯 Closing line — v1.0 (Automotive / Framework focus)

> *"Trong 20 tuần đầu tôi build VDiag không phải để demo Android skills nói chung — mà để chứng minh tôi sẵn sàng cho VinFast Android Automotive stack ngay từ ngày đầu: CarService pattern, Binder/AIDL, JNI HAL bridge, UDS, SCHED_FIFO engine, CAN bus, ADAS sensor monitoring, ARM64 cross-compile — production patterns xuyên suốt từ App layer xuống tới embedded target. 3 năm LG production C++ + 100 ngày VDiag = tôi contribute được ngay."*
>
> *(v3.0 closing line ở cuối file — dùng cho Senior Android Common track.)*

---

## 🚗 PART 9: Android Bring-up (Q43-52)

> Các câu hỏi về "Experience bringing up Android devices" trong JD.

### Q43. What is `@VintfStability` and why does it matter?

`@VintfStability` là annotation trong AIDL đánh dấu interface là "stable" — nghĩa là có thể cross vendor/system partition boundary. Khi apply:
- Interface phải được **frozen** vào `vX/` snapshot trước khi ship
- **Backward compatible** — không được xóa method, không thay đổi method signature
- Android sẽ **verify at runtime** (VINTF check at boot) version matching

Không có `@VintfStability` → interface chỉ dùng được trong cùng partition. Tất cả AAOS vendor HAL (`IVehicle.aidl`, `IVehicleCallback.aidl`) đều có annotation này.

> **VDiag:** `IDiagnosticHal.aidl` trong `aidl_hal/` có `@VintfStability`, frozen vào `v1/` snapshot. Khi cần thêm method → tạo `v2/`, implement backward-compatible.

### Q44. What is VINTF and what happens if it's wrong?

VINTF (Vendor Interface) = contract between vendor partition và Android framework, khai báo trong `/vendor/etc/vintf/manifest.xml`.

**Boot flow:**
```
init → read /vendor/etc/vintf/manifest.xml
     → check each <hal> entry matches installed binary
     → if mismatch → ABI check fail → device may refuse to boot HAL
```

**Debugging khi sai:**
```bash
adb shell dumpsys vintf           # xem manifest parsed
adb logcat | grep -i vintf        # error messages
adb shell hwservicemanager list   # xem HAL nào đã register
```

Common mistakes: forgot to add VINTF entry, version number mismatch, typo trong `fqname`.

### Q45. Explain the init.rc `class` directive. What is `class hal`?

`class` group services together cho init startup ordering:
- `class core` — khởi động đầu tiên (ueventd, logd)
- `class hal` — HAL services, sau core, trước main
- `class main` — system services (Zygote, SurfaceFlinger)
- `class late_start` — sau user unlock

`class hal` đảm bảo VDiag HAL service chạy **trước** CarService cố gắng connect. Nếu dùng `class main` → CarService.onCreate() có thể chạy trước HAL ready → AIDL hal not found exception.

### Q46. What is SELinux `audit2allow` and when do you use it?

Khi deploy trên device và thấy `avc: denied`:
```bash
adb logcat | grep "avc: denied" | head -20
# avc: denied { binder_call } for ... scontext=u:r:vdiag_hal:s0 ...

# Convert AVC denial to allow rule
adb logcat | grep "avc: denied" | audit2allow -M vdiag_patch
# → creates vdiag_patch.pp policy module

cat vdiag_patch.te  # review generated .te rule
# → add appropriate rules to vdiag_hal.te
```

**Emulator workflow:** Run app on emulator (userdebug) in permissive mode → `adb logcat | grep avc` → collect all denials → add to `.te` file → `checkpolicy -M` validate → document for production deployment.

### Q47. What is `writepid /dev/cpuset/foreground/tasks`?

`writepid` trong init.rc ghi PID của service vào cgroup control file → đặt process vào CPU scheduling group.

- `/dev/cpuset/foreground/tasks` → foreground CPU set (tất cả cores, high frequency)
- `/dev/cpuset/system-background/tasks` → background (restricted cores)
- `/dev/cpuset/restricted/tasks` → minimal resources

VDiag HAL service cần foreground để đáp ứng diagnostic latency requirement. Nếu để default background → Linux scheduler có thể throttle → `sendAndReceive` timeout.

### Q48. What is `privapp-permissions` and why was it introduced?

Trước Android 8: apps trong `/system/priv-app/` tự động được cấp mọi permission mà chúng declare. Rủi ro: malicious system app có thể declare bất kỳ permission nào.

Từ Android 8+: `privileged` app phải được **explicitly whitelisted** trong `/etc/permissions/privapp-permissions-<package>.xml`. Nếu không → `INSTALL_FAILED_MISSING_SPLIT`. Đây là **principle of least privilege** cho system apps.

> **VDiag:** `privapp-permissions-vdiag.xml` whitelist `com.vdiag.permission.DIAGNOSE` cho package `com.vdiag`.

### Q49. What happens when a HAL service crashes in production?

Init.rc: không có `critical` tag → restart vô thời hạn.

Với `critical` tag:
```rc
service vdiag_hal /vendor/bin/vdiag_hal
    class hal
    critical          ← nếu crash 4 lần trong 4 phút → device reboot
```

Fallback strategy trong VDiag:
1. HAL crash → `DeathRecipient` của DiagCarService fired → service log + attempt reconnect
2. Reconnect: `DiagHalBridge.nativeInit()` lại (retry với exponential backoff)
3. Nếu HAL không recover → service return NRC 0x25 (No Response From Subnet) cho mọi request thay vì crash
4. CarWatchdog vẫn nhận heartbeat → service không bị kill

### Q50. How do you test bring-up flow without a real device?

3 approaches:

**1. Emulator + adb:** Test service start/stop, permission enforcement, property propagation:
```bash
adb root && adb shell am start-service -n com.vdiag/.service.DiagCarService
adb shell setprop vdiag.hal.ready 1
adb shell dumpsys package com.vdiag | grep -i permission
```

**2. Syntax validation on Linux host:**
```bash
checkpolicy -M -c 33 -o /tmp/vdiag.pp vdiag_hal.te  # SELinux syntax
xmllint --schema vintf_manifest.xsd manifest_vdiag.xml  # VINTF XML
```

**3. Documentation + code review:** Write init.rc, SELinux, VINTF từ AAOS source — hiểu pattern, có thể review PR, có thể explain tại sao từng dòng. Với interviewer: honest rằng chưa deploy lên hardware nhưng document đầy đủ và biết trình tự debug.

### Q51. Explain the full boot sequence from power-on to app ready.

```
t=0s   Power on → bootloader → kernel
t=0.5s init reads /vendor/etc/init/vdiag.rc
       → vdiag_hal_service starts (class hal)
       → registers IDiagnosticHal with hwservicemanager
t=1s   zygote starts (Java VM)
t=3s   system_server → CarService.onCreate()
t=4s   DiagCarService.onCreate() (if autostart)
       → DiagHalBridge.nativeInit("doip:...")
       → ISystemLifecycle.start() → watchdog register
t=5s   DiagActivity.onStart()
       → DiagClient.create() → bindService(BIND_AUTO_CREATE)
t=5.5s DiagCarService.onBind() → return DiagServiceBinder
t=5.6s ServiceConnection.onServiceConnected() → UI ready
t=∞    Watchdog ping every 3s · Subscription ticks every 100ms
```

### Q52. What is the difference between system app and privileged app?

| Type | Location | Capabilities | VDiag |
|---|---|---|---|
| Regular app | `/data/app/` | Only declared permissions | DiagActivity (demo) |
| System app | `/system/app/` | Signature permissions via manifest | VDiag app in production |
| Privileged app | `/system/priv-app/` | + signature|privileged permissions + privapp whitelist | VDiag app production (full) |
| Platform-signed | Any | `android.permission.INSTALL_PACKAGES` etc. | Not needed for VDiag |

Production VDiag: app đặt trong `/system/priv-app/VDiag/`, `com.vdiag.permission.DIAGNOSE` có `android:protectionLevel="signature"`, whitelisted trong `privapp-permissions-vdiag.xml`.

---

## ⚡ PART 10: Real-time Engine + RTOS (Q53-60)

> Các câu hỏi về "preemptive, multitasking real-time operating systems".

### Q53. Explain SCHED_FIFO vs SCHED_OTHER vs SCHED_RR.

| Policy | Type | Behavior |
|---|---|---|
| `SCHED_OTHER` | Non-RT | Default Linux time-sharing (CFS), priority 0, fair scheduling |
| `SCHED_FIFO` | Real-time | Run until yield/block/preempted by higher RT priority. No time quantum. Priority 1-99 |
| `SCHED_RR` | Real-time | Like FIFO but with round-robin time quantum at same priority level |
| `SCHED_DEADLINE` | Real-time | EDF (Earliest Deadline First), specify deadline + runtime budget |

**VDiag DiagEngine:** `SCHED_FIFO` priority 10 → worker thread preempts all `SCHED_OTHER` threads → guarantees diagnostic request is processed before any background work.

**Emulator behavior:** EPERM when calling `pthread_setschedpolicy(SCHED_FIFO)` (app lacks `CAP_SYS_NICE`). VDiag handles gracefully with fallback to SCHED_OTHER + logs warning. Production system app granted `CAP_SYS_NICE` via SELinux.

### Q54. What is priority inversion and how does VDiag solve it?

**Scenario:**
```
CRITICAL task blocked waiting for mutex M
NORMAL   task holds mutex M, preempted by MEDIUM
MEDIUM   task running (doesn't need M)
→ CRITICAL starved by MEDIUM indefinitely
```

**Solution 1: Priority inheritance mutex** (VDiag's choice)
```cpp
pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
// → lock-holder's priority raised to max waiter's priority
// → NORMAL raised to CRITICAL → preempts MEDIUM → releases M → CRITICAL runs
```

**Solution 2: Priority ceiling** — pre-assign ceiling priority to mutex (simpler, less flexible)

VDiag uses `PTHREAD_PRIO_INHERIT` on the queue mutex. Classic example from Mars Pathfinder 1997 — priority inversion bug caused system resets.

### Q55. Why 4 separate queues instead of one `std::priority_queue`?

`std::priority_queue` uses a heap — `pop()` is O(log n) but **does not support O(1) priority bump** (escalating a request from NORMAL to HIGH mid-flight). Also: heap has shared state → single mutex → CRITICAL request must contend with LOW request for the same lock.

4 separate `std::deque` queues:
```cpp
std::deque<WorkItem> queues_[4];  // indexed by Priority enum
```
Worker: drain CRITICAL fully → drain HIGH fully → NORMAL → LOW. Any CRITICAL push wakes worker immediately via `cv_.notify_all()`. Lock scope is per-queue for push, drain order is sequential (no shared-state complexity).

### Q56. How does CPU affinity help with real-time latency?

Without affinity: Linux scheduler may migrate worker thread across cores → CPU cache invalidation → latency spikes.

```cpp
cpu_set_t cpus;
CPU_ZERO(&cpus);
CPU_SET(0, &cpus);   // pin to CPU 0
pthread_setaffinity_np(workerThread_, sizeof(cpus), &cpus);
```

Effect: all HAL data structures (queue, mutex, condition variable) stay in CPU0's L1/L2 cache. Expected reduction: p99 latency drop ~20-40% in benchmarks (depends on workload).

**Production consideration:** Coordinate with system team — CPU0 might also handle interrupts. Could pin to isolated core (no IRQ affinity). VDiag uses CPU0 as default, configurable at runtime.

### Q57. What is your benchmark result? How did you measure it?

```bash
# Linux host, DiagEngine + MockHal (no I/O)
# SCHED_OTHER (no sudo):    p50=45µs  p95=180µs  p99=320µs  max=850µs
# SCHED_FIFO  (sudo):       p50=28µs  p95=90µs   p99=140µs  max=280µs
# SCHED_FIFO improvement:   p99 ×2.3 better
```

Methodology: `latency_bench.cpp` — 1000 iterations, `steady_clock::now()` at enqueue + callback, sort → percentile. Warm-up 100 iterations discarded. Run on idle system.

**Why MockHal?** DoIP adds ~50ms network round-trip (emulator → host TCP). For engine latency measurement: MockHal isolates transport from scheduler behavior.

### Q58. What is `PTHREAD_PRIO_INHERIT` vs `PTHREAD_PRIO_PROTECT`?

| | `PRIO_INHERIT` | `PRIO_PROTECT` |
|---|---|---|
| Mechanism | Lock-holder raised to highest waiter | Lock-holder raised to pre-set ceiling |
| When applied | When a higher-prio thread blocks | Always when lock held |
| Overhead | Only when contention occurs | Every lock/unlock |
| Usage | General RT systems | Bounded worst-case |

VDiag uses `PRIO_INHERIT` — simpler, no need to predict ceiling. `PRIO_PROTECT` used in hard real-time systems where worst-case must be bounded.

### Q59. How would you use SCHED_DEADLINE for VDiag?

`SCHED_DEADLINE` requires specifying `period`, `runtime`, `deadline` per task:
```
CRITICAL UDS request: period=50ms, runtime=5ms, deadline=50ms
```

If engine overruns its runtime budget → kernel defers it to next period.

**Why not use it for VDiag?** Harder to configure correctly — wrong `runtime` estimate → throttling. `SCHED_FIFO` is simpler and sufficient for UDS diagnostic rates (≤ 100 req/s). SCHED_DEADLINE would be appropriate if mixing VDiag with real-time control loops on same CPU.

### Q60. How does TesterPresent (0x3E) relate to real-time constraints?

ISO 14229 Extended Diagnostic Session requires TesterPresent every **S3 = 5 seconds** or ECU resets session → all pending requests rejected.

VDiag `SubscriptionManager`: when active extended session, schedules TesterPresent every 4.5s (margin). If engine is overloaded and TesterPresent queued as LOW → session expires → all HIGH requests get NRC.

**Fix:** TesterPresent submitted as **HIGH** priority. Never at LOW. Deadline constraint directly encoded in priority tier.

---

## 🔌 PART 11: CAN Bus + ISO-TP (Q61-66)

> Các câu hỏi về "Interface with hardware design and development".

### Q61. What is CAN bus and why does automotive use it?

CAN (Controller Area Network, ISO 11898): multi-master serial bus, 1 pair twisted wire, up to 1 Mb/s (classic) / 5-8 Mb/s (CAN-FD). Used in 99% of automotive ECUs because:
- **Fault tolerant:** 2-wire differential signaling, immune to noise
- **Multi-master:** any node can transmit when bus idle
- **Priority by ID:** lower ID = higher priority (arbitration without collision)
- **Deterministic:** worst-case latency calculable

Modern vehicles have 3-5 CAN networks (powertrain, body, chassis, comfort) bridged by a central gateway ECU.

### Q62. What is ISO-TP and why is it needed?

**Problem:** UDS ReadVIN response = 17 bytes VIN. CAN frame max = 8 bytes.

ISO 15765-2 (ISO-TP) defines segmentation protocol over CAN:

| Frame type | First byte | Usage |
|---|---|---|
| SF (Single Frame) | `0x0N` | N ≤ 7 bytes → fits in 1 CAN frame |
| FF (First Frame) | `0x1NNN` | N = total length, data[0-5] = first 6 bytes |
| CF (Consecutive Frame) | `0x2S` | S = sequence number 1-15, data[0-6] = 7 bytes |
| FC (Flow Control) | `0x30` | BS = block size, STmin = inter-frame delay |

VIN (17 bytes) → FF (length=17, 6 bytes) + 2 CF (7 bytes each) = 3 CAN frames total.

VDiag `IsoTpCodec` implements encode (UDS→CAN frames) and decode (CAN frames→UDS).

### Q63. Explain SocketCAN API.

Linux kernel built-in CAN driver interface:
```c
int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
struct ifreq ifr;
strncpy(ifr.ifr_name, "vcan0", IFNAMSIZ);
ioctl(sock, SIOCGIFINDEX, &ifr);
struct sockaddr_can addr = { .can_family = AF_CAN,
                             .can_ifindex = ifr.ifr_ifindex };
bind(sock, (struct sockaddr*)&addr, sizeof(addr));

// Send
struct can_frame frame = { .can_id = 0x7DF, .can_dlc = 3,
                           .data = {0x22, 0xF1, 0x90} };
write(sock, &frame, sizeof(frame));

// Receive
read(sock, &frame, sizeof(frame));
```

`vcan0` = virtual CAN interface (Linux kernel module `vcan`) — same API as physical CAN, no hardware needed.

### Q64. Where does CAN fit in the VDiag HAL architecture?

```
DiagEngine (unchanged) → IDiagnosticHal::sendAndReceive(uds_bytes)
                                  ↓
                         CanDiagnosticHal
                                  ↓
                         IsoTpCodec::encode(uds_bytes, txId=0x7DF)
                                  ↓  (1-3 CAN frames)
                         SocketCAN write(sock, frame)  → vcan0 → Python CAN sim
                                  ↓  (response frames)
                         SocketCAN read (with 150ms timeout)
                                  ↓
                         IsoTpCodec::decode(frames) → uds_response
                                  ↓
                         DiagEngine callback
```

`HalFactory::create("can:vcan0:0x7DF:0x7E8")` — zero engine change from Mock/DoIP.

### Q65. What is the P2 timer in UDS?

ISO 14229 timing parameters:
- **P2 = 50ms** — max time ECU takes to respond. If no response → timeout → NRC treated
- **P2* = 5000ms** — after NRC 0x78 (ResponsePending), max additional time
- **P3 = 5000ms** — min time after end of response before next request
- **P2Server = 25ms** — server must respond (or 0x78) within this time

VDiag `CanDiagnosticHal::sendAndReceive`: `recvtimeo = 150ms` (3× P2 margin). If timeout → return `Result{false, {}, "P2 timeout"}`. DiagEngine logs NRC.

### Q66. How would you debug a CAN communication issue?

```bash
# 1. Verify interface up
ip link show vcan0

# 2. Monitor raw frames
candump vcan0   # requires can-utils

# 3. Send test frame
cansend vcan0 7DF#02220000

# 4. Check filter
canbusload vcan0@500000  # bus load %

# 5. IsoTP layer test
isotpsend -s 7DF -d 7E8 vcan0 22 F1 90
isotprecv -s 7E8 -d 7DF vcan0
```

VDiag adds detailed logging in `CanDiagnosticHal`: log each frame write/read with hex dump, timestamp, ISO-TP state. In CI: vcan0 tests skipped (no kernel module), compile still verified.

---

## 🤖 PART 12: ADAS Sensor HAL + Secondary Self-Driving Angle (Q67-73)

### Q67. How does VDiag relate to the self-driving stack?

```
Self-driving stack:
  Perception  → camera/radar/lidar → object detection
  Planning    → path planning
  Control     → throttle/brake/steer actuators
  ↕ (cross-cutting)
  Diagnostics → health monitor cho cả 3 tầng + ECU
```

VDiag covers the **diagnostic plane**:
- **ECU diagnostics** (B1-B6): UDS DTC, VIN, SOC, RPM → implemented
- **Sensor health monitoring** (ADAS HAL): radar/IMU/camera health → IAdasSensorHal
- **Real-time alerting** (B3): safety-critical DTC → CRITICAL queue → < 50ms

*"Perception team lo algorithm. Tôi lo infrastructure layer bên dưới: hardware talk, fault detection, reporting pipeline."*

### Q68. Explain complementary filter for IMU sensor fusion.

**Problem:** Gyroscope → accurate short-term, drifts long-term. Accelerometer → accurate long-term, noisy short-term.

**Complementary filter (Mahony/Madgwick simplified):**
```cpp
// α = 0.98: trust gyro 98%, correct drift with accel 2%
roll_  = alpha_ * (roll_  + gyro.x * dt) + (1-alpha_) * accel_angle_x;
pitch_ = alpha_ * (pitch_ + gyro.y * dt) + (1-alpha_) * accel_angle_y;
```

**Why better than pure gyro?** No drift. **Why better than pure accel?** Not noisy under vibration.

**Production alternative:** Kalman filter (EKF/UKF) — more accurate, handles nonlinearity, but significantly more compute. Complementary filter: sufficient for health monitoring (not navigation-grade).

**VDiag usage:** `ComplementaryFilter::isStable(threshold=5°)` — if |roll|+|pitch| > 5° while vehicle expected stationary → possible IMU mount issue → inject DTC.

### Q69. How does the radar tracker work?

Nearest-neighbor tracker with state machine:

```
Tentative (< 3 hits) → Confirmed (≥ 3 hits) → Lost (5 misses)
```

Each frame:
1. **Predict**: `range_new = range + velocity * dt` (constant velocity model)
2. **Associate**: for each detection, find nearest predicted track (greedy, not Hungarian)
3. **Update**: if match found → update + increment hitCount; else → increment missCount
4. **Lifecycle**: hitCount ≥ 3 → Confirmed; missCount ≥ 5 → remove

**Limitation vs production:** Production uses EKF-based tracker with Mahalanobis distance gating. VDiag uses simplified version to demonstrate pattern — same state machine concept.

### Q70. What is the ADAS Python simulator and how does the emulator connect?

`tools/adas_simulator/adas_server.py` — TCP server port 14500, generates synthetic sensor data every 100ms:
- IMU: sinusoidal vibration + tilt drift (simulate road)
- Radar: 2-3 objects moving realistically
- Camera: fps=30, random drop events
- JSON line protocol: `{"type":"imu","accel":[...],"gyro":[...],"ts_ns":...}\n`

**Emulator connection:**
```bash
python3 tools/adas_simulator/adas_server.py &   # host
adb reverse tcp:14500 tcp:14500                 # forward emulator → host
# App connects to 127.0.0.1:14500 from within emulator
# → adb reverse → host 14500 → Python sim
```

**Note: TCP not UDP** — `adb reverse` only supports TCP. All simulators in VDiag use TCP.

### Q71. What happens if a sensor is DEGRADED vs FAILED?

| Health | Meaning | VDiag response |
|---|---|---|
| OK | Normal operation | No DTC |
| DEGRADED | Partial function loss (SNR low, fps=15) | Warning DTC (non-critical queue) |
| FAILED | Complete loss | Critical DTC (CRITICAL queue) → immediate callback |
| UNKNOWN | No data received (timeout) | NRC 0x25 + DEGRADED assumed |

In `MockAdasHal`, fault injection via method `injectFault(SensorType, SensorHealth)` → used in gtest to verify DTC pipeline fires correctly.

### Q72. What is the DID namespace for ADAS sensors vs ECU?

Standard UDS DID space: `0x0000-0xDFFF` (manufacturer-defined above `0xF000`).

VDiag convention:
- `0xF000-0xFFFF` — standard OBD/UDS (VIN=0xF190, etc.)
- `0xA000-0xAFFF` — Radar sensors (0xA001=radar front, 0xA010=radar health)
- `0xB000-0xBFFF` — IMU/inertial sensors
- `0xC000-0xCFFF` — Camera sensors
- `0xD000-0xDFFF` — Fusion output (occupancy grid freshness, etc.)

Same DID format, same `sendAndReceive` pipeline — ADAS HAL maps sensor state to DID responses. Engine does not know it's talking to sensor vs ECU.

### Q73. What safety concepts apply to ADAS sensor monitoring?

| Concept | Application in VDiag |
|---|---|
| **Error containment** | HAL failure returns NRC, doesn't crash engine |
| **Fail-safe** | Sensor FAILED → DTC injected → reported, not silently ignored |
| **Redundancy** | Multiple radar DIDs (front + rear) — if one fails, other still reports |
| **Heartbeat** | ADAS HAL recv thread must send data every 200ms, else UNKNOWN health |
| **FMEA awareness** | Camera BLOCKED → single point of failure if no lidar fallback |

*"Tôi hiểu những concept này từ AUTOSAR background. VDiag implement error containment và fail-safe patterns. ASIL certification là domain riêng của safety team — tôi build infrastructure layer theo pattern, họ certify."*

---

## 🏗️ PART 13: ARM64 + Embedded Target (Q74-78)

### Q74. Explain your cross-compile setup.

**Toolchain file** `hal/cmake/toolchain-aarch64.cmake`:
```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
```

**Build command:**
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
         -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/usr/aarch64-linux-gnu"
make -j$(nproc) && ctest --output-on-failure
```

`file test_uds_codec` → `ELF 64-bit LSB executable, ARM aarch64` — proof of cross-compile.

### Q75. What is QEMU user-mode and how is it different from system-mode?

| Mode | QEMU user-mode | QEMU system-mode |
|---|---|---|
| What it emulates | CPU instruction set only | Full machine (CPU + devices + RAM) |
| Kernel | Host Linux kernel | Separate guest kernel |
| Use case | Run ARM binary on x86 host | Full OS emulation |
| Speed | Fast (only translate instructions) | Slow (full virtualization) |
| Command | `qemu-aarch64 ./test_uds_codec` | `qemu-system-aarch64 -kernel ...` |

VDiag uses **user-mode** (`qemu-aarch64-static`). ARM binary runs on x86 Linux kernel via syscall translation. Sufficient for running gtest — no need for full OS.

### Q76. Why does hal/ have no Android dependency?

Design choices:
- Standalone CMake (no Android.mk / Soong)
- No JNI headers, no `#include <android/...>` in `hal/`
- POSIX-only threading (`pthread_t`, not Java `Handler`)
- Return-type error propagation (no exceptions — RTOS `-fno-exceptions` compatible)
- No `std::filesystem` (bare-metal compatibility)

**Result:** Same `hal/` compiles on:
- x86-64 Linux (daily dev + CI)
- ARM64 via cross-toolchain + QEMU (embedded target simulation)
- Android NDK (linked into `libvdiag_jni.so`)
- (Future) QNX, VxWorks if needed — only POSIX calls required

### Q77. Why run gtest via QEMU in CI if you have x86 tests?

Architecture coverage: a bug that only manifests on ARM (alignment fault, endianness, pointer size assumption) will NOT appear on x86 tests. Example: `uint32_t* ptr = (uint8_t*) buf + 1` — works on x86 (unaligned access allowed), SIGBUS on ARM.

QEMU ARM64 catches:
- Unaligned memory access
- ARM-specific calling convention issues  
- `sizeof(long)` = 8 on ARM64 (same as x86-64 in this case, but worth verifying)
- Endianness (ARM LE by default, same as x86 — but documented)

### Q78. How does `hal/` link into both Android and standalone?

```
hal/ (libvdiag_hal.a — static library)
  ↓
  ├── Android NDK build:
  │     android/app/src/main/cpp/CMakeLists.txt
  │     add_subdirectory(hal) → libvdiag_hal.a
  │     libvdiag_jni.so links libvdiag_hal.a
  │     installed in APK: lib/arm64-v8a/libvdiag_jni.so
  │
  └── Standalone build (Linux / ARM64):
        cd hal/build && cmake .. && make → test_* binaries
        Each test binary statically links libvdiag_hal.a
        No JVM, no Android, no NDK needed
```

This is the **HAL portability guarantee**: same C++ code, two deployment targets, zero conditional compilation needed.

---

## 🎬 DEMO SCRIPT v2.0 — 3 phút

### 30-Second Pitch:

> *"VDiag là vehicle diagnostic platform clone AAOS CarService stack — 100 ngày build, 8 technical boundaries. App → Bound Service (Binder/AIDL) → JNI bridge (RAII GlobalRef) → C++ engine (priority queue + SCHED_FIFO fallback) → HAL abstraction (Mock/DoIP/CAN/ADAS). Stable AIDL versioning + HAL service lifecycle resilience, ARM64 cross-compiled. Mỗi component mirror AAOS exact từ Car.createCar() xuống đến IVehicle.aidl."*

### Live Demo Steps (emulator):

1. **Open app** → dark automotive theme, 6 buttons + ADAS tab
2. **Tap "Read VIN"** → `VINFAST12345678901 (3ms)` — full pipeline in 3ms
3. **Tap "Battery SOC"** → `78%`
4. **Tap "Read DTC"** → `P0A00, P0562`
5. **Tap "Clear DTC"** → `OK` → tap Read DTC → `(empty)` ← mock state mutated
6. **Switch to ADAS tab** → `RADAR: 2 objects | IMU: roll=1.2° pitch=0.8° | CAMERA: 30fps ✅`
7. **Show subscribe mode** → SOC label changes every ~1s without tapping
8. **Show Logcat** → 4 tags: `App → Binder → JNI → Engine`
9. **Show 2 processes**: `adb shell ps -A | grep vdiag`
10. **Kill app**: `adb shell am force-stop com.vdiag` → `☠ Client died — auto-removed` in Logcat

### Bonus (if Python sims running):
11. `adb reverse tcp:13400 tcp:13400` → **Tap "Read VIN"** → ~50ms (DoIP path)
12. Show `docs/benchmark.md` — SCHED_FIFO vs SCHED_OTHER p99 comparison

---

## 🎯 Closing line v2.0

> *"100 ngày tôi build VDiag để chứng minh tôi sẵn sàng cho **Senior Android Automotive / Framework / HAL roles** ngay từ ngày đầu: Stable AIDL versioning, HAL service lifecycle + DeathRecipient resilience, 3-transport HAL abstraction (Mock/DoIP/CAN), JNI RAII bridge, SELinux bring-up, ARM64 cross-compile — production patterns xuyên suốt từ App layer xuống tới embedded target. 90+ tests, ASAN/TSAN/UBSAN/CheckJNI clean, ARM64 QEMU green. 3 năm LG production C++ + 100 ngày VDiag = tôi contribute được ngay từ day 1."*



---

# 🚀 v3.0 EXPANSION — Senior Android Q&A (Days 101-130)

> Bổ sung 32+ Q&A across 4 sections (Advanced IPC + Testing Pyramid + MVVM/Room + Security). Đây là mảng phân biệt Senior Android khỏi mid-level trong bất kỳ role nào — Automotive / Framework / Common.

---

## 🔗 PART 13: Advanced IPC + ASharedMemory (Q101-108)

### Q101. How do you send > 1MB across Binder?

3 tiers:
1. **< 100KB:** regular Parcelable / AIDL list — simple, fast enough.
2. **Any size, one-shot:** `ParcelFileDescriptor` wrapping tmpfile fd → receiver reads via `FileInputStream`.
3. **Bulk + reusable + zero-copy:** `ASharedMemory_create(name, size)` on server side, transfer fd via AIDL as `ParcelFileDescriptor`, receiver `mmap` on same physical page.

In VDiag, DTC snapshot export uses tier 3 — 500KB blob delivered in < 10ms with zero heap copy.

### Q102. What's inside `ASharedMemory`?

`ASharedMemory_create` calls the Android `ashmem` driver (`/dev/ashmem` before API 29, `memfd_create + MFD_ALLOW_SEALING` after). Returns an fd whose backing memory can be `mmap`ed cross-process. Distinct from POSIX shm because it supports **purgeable pages** and is SELinux-friendly by default. My code path is `#include <android/sharedmem.h>` — Android NDK, API 26+.

### Q103. What is `dumpsys` and how did you integrate it?

`dumpsys` is Android's user-space tool that invokes `IBinder.dump(fd, args)` on every service. I override `Service.dump()` in `DiagCarService` — prints 6 sections: HAL state, engine queue, subscriptions, Binder stats, transaction history (with `--history`), and configurable proto (with `--proto`). Command: `adb shell dumpsys activity service com.vdiag/...`. Integrates automatically with `adb bugreport`.

### Q104. How do you observe Binder pool saturation?

`BinderStats.enter()` returns an `AutoCloseable` handle; wrap every AIDL method with try-with-resources. Handle tracks `currentInflight` atomic counter and updates `maxConcurrent` via `updateAndGet`. Threshold: if concurrent > 12 (of 16 default pool), log warning. Dump exposes p50/p95/p99 latency from a 256-slot lock-free ring buffer.

### Q105. Binder thread pool defaults — what and how to tune?

- **App process:** 15 spawned + 1 caller-owned = max 16 concurrent Binder transactions.
- **system_server:** typically 31.
- Tune via `BinderInternal.setMaxThreads(int)` before first transaction — rarely done at app level (usually only for RPC-heavy services).
- Growth is on-demand, no shrink. Pool is per-process, shared across all Binder objects.

### Q106. `oneway` vs synchronous — impact on pool?

`oneway` transactions use an async buffer (per-process, ~1MB total). Server call returns immediately without a reply. **Pool thread is still consumed** during dispatch — so oneway is not "free". If you flood oneway calls, server pool saturates and callback-style APIs stop firing. In VDiag, `IDiagPropertyListener` is `oneway` to avoid client-blocks-server deadlock.

### Q107. `linkToDeath` — where does the callback run?

`DeathRecipient.binderDied()` fires on **any Binder pool thread** at the process that registered the recipient, asynchronously to the peer's death. Don't do long work there — schedule to handler / executor. In VDiag: `binderDied()` sets `mHalUp.set(false)`, notifies subscribers via oneway broadcast, then `mHandler.postDelayed(reconnectRunnable, backoffMs)` — actual reconnect happens on main thread.

### Q108. How would you diagnose a "slow AIDL call" report from QA?

1. Check `dumpsys` `--history` for latency percentiles (p95/p99).
2. Correlate with `atrace` capture around the incident timestamp.
3. Verify pool saturation: `BinderStats.maxConcurrent` at the time.
4. Check if callee is doing heavy work on Binder thread (should post to executor).
5. If oneway backlog: reduce publish rate or add flow-control.
6. Enable systrace `-c binder_driver` to see kernel-side wait times.

---

## 🧪 PART 14: Java Testing Pyramid (Q109-116)

### Q109. Why a pyramid, not "just Espresso"?

Espresso is ~30 seconds per test (Activity start, IdlingResource wait). 50 Espresso tests = 25-min CI = devs stop running locally. Pyramid: 100 JUnit + 50 Mockito + 30 Robolectric run in < 30 seconds (JVM-only); 10 Espresso guard critical happy paths. Feedback loop stays under 1 min for changes below UI.

### Q110. Robolectric vs instrumented (Espresso) — when each?

- **Robolectric:** any Android class that doesn't need real device features. Runs in host JVM using shadow objects — fast, deterministic, CI-friendly. Perfect for Service/BroadcastReceiver/Handler/Room lifecycle.
- **Espresso:** actual UI rendering, view interactions, real Binder transactions to actual services, screen navigation. Reserve for "does user flow X work end-to-end".

### Q111. What is `InstantTaskExecutorRule`?

Rule for tests that use `LiveData` / `Architecture Components` executors. Forces all background threads to run synchronously in the test thread — so `setValue` propagates to observers immediately, no `Thread.sleep`. Without it, LiveData observer never fires because JUnit runner finishes before Android's default executor dispatches.

### Q112. Mockito: `@Mock` vs `mock()` vs `spy()`?

- `@Mock` — field-level annotation, initialized by `MockitoJUnitRunner` or `MockitoAnnotations.openMocks(this)`. Cleaner than `mock()` per test.
- `mock(Class)` — runtime creation, needed if class is passed to helper method.
- `spy()` — partial mock: real methods called by default, only stubbed methods return canned values. Useful for testing 1 method while calling real dependencies.

### Q113. What's IdlingResource and why?

Espresso auto-waits for main-thread messages to drain, but doesn't know about **your** async work (network, database, WorkManager). Register an `IdlingResource` implementing `isIdleNow()` + `registerIdleTransitionCallback`. Espresso pauses until you call `callback.onTransitionToIdle()`. Replaces fragile `Thread.sleep(2000)`.

### Q114. How to test Room without emulator?

`Room.inMemoryDatabaseBuilder(ctx, DiagDatabase.class).allowMainThreadQueries().build()` inside Robolectric. Database lives in JVM memory only, disappears after test. `allowMainThreadQueries()` is a **test-only shortcut** — never in production.

### Q115. Jacoco coverage number — is it meaningful?

Line coverage 80% means 80% of executable lines ran during tests. It does **not** prove correctness — you can have 100% coverage with zero assertions. Add mutation testing (PIT) to check tests actually detect regressions. Coverage is a **floor**: below 60% = fear zone; above 80% = maintenance zone. My gate is 80% Java + 85% C++.

### Q116. How would you test a `PeriodicWorkRequest`?

Use `WorkManagerTestInitHelper.initializeTestWorkManager(context)` in `@Before`. Then `WorkManagerTestInitHelper.getTestDriver(context).setPeriodDelayMet(request.getId())` to fast-forward. Assert on `WorkInfo.State` and progress data. Run inside Robolectric — no emulator needed.

---

## 🏛 PART 15: MVVM Architecture + Room (Q117-124)

### Q117. Why MVVM over MVP/MVC?

- **ViewModel survives config change** — no manual `onSaveInstanceState` juggling.
- **LiveData is lifecycle-aware** — auto-unsubscribes when Activity STOP/DESTROY, no memory leak.
- **Boilerplate:** MVP requires manual `Presenter.attachView/detachView`; MVVM handled by framework.
- Google's official recommendation since 2017 (Architecture Components).

### Q118. Where should Context live? Why not in ViewModel?

Storing an `Activity` Context in a ViewModel leaks the Activity across rotations (ViewModel outlives Activity). Use `AndroidViewModel(Application)` when Application Context is unavoidable (Room DB path, SharedPreferences). Pass through Repository — ViewModel talks to Repository, Repository holds Application Context.

### Q119. LiveData vs StateFlow?

LiveData: Java-friendly, main-thread-only observers, single-value + latest replay, lifecycle-aware built-in. StateFlow (Kotlin): coroutines native, supports operators (`map`, `combine`, `debounce`), collect requires lifecycle scope. VDiag is Java → LiveData. New Kotlin projects → StateFlow.

### Q120. Repository pattern — what does "single source of truth" mean?

Only one class knows where the current copy of data comes from — Repository. UI never asks "should I query Room or the network?". Repository decides:
- Cache hit → return immediately.
- Also refresh from network → update cache → notify observers.
Result: data displayed is always consistent; changing data source (add cloud fallback) requires zero UI change.

### Q121. Room `@Transaction` — how does it work on a default interface method?

Room's annotation processor generates `DtcDao_Impl` at compile time. It wraps `@Transaction`-annotated methods with `db.beginTransaction() / setTransactionSuccessful() / endTransaction()`. Default methods can call other `@Query`-annotated methods safely; all execute in the same transaction — deleteAll then insertAll rolls back atomically on exception.

### Q122. How do you handle Room schema migration?

Two options:
1. **Explicit `Migration(from, to)`:** provide SQL migration steps. Committed schema JSON (`exportSchema=true`) lets Room validate.
2. **`fallbackToDestructiveMigration()`:** wipe + recreate. OK for dev, catastrophic for prod (user data lost).

Test migrations with `MigrationTestHelper` — creates DB at old version with sample data, runs migration, validates result.

### Q123. `viewLifecycleOwner` vs `this` in Fragment `observe()`?

Fragment's `View` outlives Activity but is destroyed on `onDestroyView` (backstack). Using `this` (Fragment lifecycle) → observer keeps firing while view is gone → NPE. Always use `viewLifecycleOwner` for view-bound observers.

### Q124. DiffUtil in RecyclerView — why care?

`notifyDataSetChanged` re-binds every ViewHolder → jarring, no animation, wastes GPU. `DiffUtil` computes minimal ops (insert/remove/move/change), triggers targeted `notifyItemXxx` → RecyclerView animates only real changes, keeps 60fps even with 1000 items. `ListAdapter` handles this automatically.

---

## 🔐 PART 16: Background Work + Security (Q125-132)

### Q125. WorkManager vs AlarmManager vs JobScheduler?

- **WorkManager:** the modern umbrella (since Android 5+). Under the hood uses JobScheduler (API 23+), AlarmManager (< 23), or GcmNetworkManager. Handles constraints, retries, chaining, observability.
- **AlarmManager:** exact-time wake-up (calendar reminders). Ignored for deferrable work.
- **JobScheduler:** raw API. Use only if you need capabilities WorkManager doesn't expose.

Rule: default = WorkManager, unless you have exact-timing.

### Q126. Why min PeriodicWorkRequest interval = 15 min?

Battery. Below 15 minutes the OS can't batch jobs across apps efficiently → wakes CPU too often → user battery complaints. If you need faster, either use a foreground service (user-visible) or reduce frequency.

### Q127. Static vs dynamic BroadcastReceiver — post Android 8?

Since Android 8, most **implicit** broadcasts can't have static receivers (`AndroidManifest.xml`) — restriction to save battery. Exceptions: `BOOT_COMPLETED`, package events, a small allowlist. For general broadcasts (battery, connectivity), register **dynamically** in a running Service or Activity.

### Q128. `DataStore` vs `SharedPreferences`?

DataStore:
- Async (Kotlin Flow / RxJava) — no `commit()` blocking main thread.
- Type safety via `Preferences.Key<T>`.
- Better error handling (Flow exception, not silent SharedPreferences swallow).
- Backed by Proto (`ProtoDataStore`) or Preferences.
DataStore is Google's replacement — new code should use it.

### Q129. Why `AES-GCM` over `AES-CBC + HMAC`?

GCM is authenticated encryption (AEAD): one primitive gives both confidentiality and integrity. CBC + HMAC requires you to compose two — error-prone (encrypt-then-MAC vs MAC-then-encrypt, wrong IV handling). GCM is the 2024 default. Never invent your own AEAD.

### Q130. Certificate pinning — what breaks it and how to survive?

**Breaks:** server cert rotation. Solution: always ship 2 pins — the current cert's public key AND the next-in-rotation. Rotate: publish app with (current + next) → rotate server to next → publish app with (next + next-next). Also set `<pin-set expiration="...">` — if all pins invalid and past expiration, fall back to system trust to avoid app bricking.

### Q131. StrictMode `penaltyDeath` in production?

**No.** `penaltyDeath` crashes the app on the first violation — will hit users hard because some violations occur only in edge cases you never hit in dev. Use `penaltyLog` in production (log-only) or Firebase Crashlytics non-fatal report. `penaltyDeath` is dev-only, guarded by `BuildConfig.DEBUG`.

### Q132. R8 obfuscation broke my `Parcelable` — why and how to fix?

R8 removes the `CREATOR` static field because it's not referenced from Java code (only used via reflection by the platform). Fix with ProGuard rule:
```pro
-keepclassmembers class * implements android.os.Parcelable {
    public static final android.os.Parcelable$Creator CREATOR;
}
```
Also keep AIDL Stub/Proxy, JNI callback interfaces (referenced from C++ FindClass), Room `_Impl` classes, WorkManager Worker constructors.

---

## 🎬 DEMO SCRIPT v3.0 — 3.5 phút (updated)

### 30-Second Pitch v3.0:

> *"VDiag is my 130-day Senior Android portfolio — AAOS CarService clone plus a modern Android architecture layer. Full stack: App with MVVM+Room+LiveData → Bound Service on separate process → JNI RAII bridge → C++ engine with priority queue → HAL abstraction with 4 swappable implementations (Mock/DoIP/CAN/ADAS). Ship-quality: `@VintfStability` AIDL, `aidl_api/` versioning, DeathRecipient exponential backoff reconnect, Android Keystore AES-256-GCM, R8 obfuscation, WorkManager background jobs, complete testing pyramid (gtest + Mockito + Robolectric + Espresso), ARM64 cross-compile with QEMU CI. 140+ tests, 8 boundaries, 3 major versions."*

### Live Demo (5 minutes):

1. **Open app** — dark automotive theme, MVVM structure, RecyclerView driven by LiveData.
2. **Tap "Read VIN"** → 3ms — full pipeline.
3. **Rotate device** — data survives (ViewModel), no re-fetch. Show `adb shell settings put system ...` orientation flip.
4. **Read DTC** → RecyclerView animates only new items via DiffUtil.
5. **Kill HAL** — banner turns amber "HAL: RECONNECTING (2s)" → green — DeathRecipient backoff cycle.
6. **`adb shell dumpsys activity service com.vdiag/...`** — 6 sections including Binder pool stats + transaction history p50/p95/p99.
7. **`adb shell dumpsys activity service com.vdiag/... --history`** — last 50 transactions.
8. **Show WorkManager job** — `adb shell dumpsys jobscheduler | grep vdiag` → nightly DTC scan scheduled with battery constraint.
9. **Show Room DB** — `adb shell run-as com.vdiag sqlite3 databases/diag.db "SELECT COUNT(*) FROM dtc;"`.
10. **Show release build** — `ls -lh app-release.apk` = 7MB vs `app-debug.apk` = 12MB (R8 shrink 40%).
11. **`adb shell dumpsys package com.vdiag | grep -A5 requestedPermissions`** — signature perms + POST_NOTIFICATIONS runtime.
12. **`git log --oneline | wc -l`** → 130.

### Closing line v3.0

> *"130 ngày tôi build VDiag để chứng minh sẵn sàng cho **Senior Android roles** ở nhiều mảng: Automotive (AAOS stack + CarPropertyManager + CarWatchdog), Framework (Binder internals + JNI + Stable AIDL + HAL lifecycle), và Common (MVVM + Room + WorkManager + Keystore + testing pyramid). Complete portfolio: 8 architectural boundaries · 140+ tests · ASAN/TSAN/UBSAN/CheckJNI clean · Jacoco 80% Java + gcov 85% C++ · ARM64 QEMU green · R8 release build · Android Keystore HW-backed · 3 major versions shipped. Kết hợp với 3 năm production C++ tại LG — tôi contribute được ngay từ ngày đầu ở bất kỳ Senior Android team nào."*
