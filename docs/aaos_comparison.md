# AAOS ↔ VDiag Side-by-Side Comparison

> **Purpose:** Demonstrate that VDiag is a deliberate clone of the Android Automotive OS (AAOS) CarService stack, applied to the vehicle-diagnostics domain. Every VDiag component has a 1-to-1 equivalent in production AAOS.
>
> **Source studied:** `packages/services/Car/` (AOSP), `hardware/interfaces/automotive/vehicle/` (AIDL HAL).

---

## 1. Stack overview

```
╔══════════════════════════════════╦══════════════════════════════════╗
║          AAOS (production)       ║          VDiag (this project)    ║
╠══════════════════════════════════╬══════════════════════════════════╣
║  App                             ║  App                             ║
║    android.car.Car               ║    DiagClient                    ║
║    CarHvacManager / etc.         ║    (DiagProperty-typed API)      ║
╠══════════════════════════════════╬══════════════════════════════════╣
║  Framework / System Server       ║  DiagCarService                  ║
║    CarService (AIDL Stub)        ║    (Bound Service, AIDL Stub)    ║
║    android:process=car_service   ║    android:process=:car_service  ║
╠══════════════════════════════════╬══════════════════════════════════╣
║  JNI bridge                      ║  DiagHalBridge                   ║
║    VehicleHal (JNI wrapper)      ║    (JNI wrapper — libvdiag_jni)  ║
╠══════════════════════════════════╬══════════════════════════════════╣
║  HAL interface                   ║  IDiagnosticHal                  ║
║    IVehicle.aidl (pure AIDL HAL) ║    (pure virtual C++ interface)  ║
╠══════════════════════════════════╬══════════════════════════════════╣
║  HAL implementation(s)           ║  MockDiagnosticHal               ║
║    DefaultVehicleHal             ║  DoipDiagnosticHal               ║
║    (emulator reference impl)     ║  CanDiagnosticHal                ║
╚══════════════════════════════════╩══════════════════════════════════╝
```

---

## 2. Component mapping table

| # | AAOS Component | VDiag Equivalent | Pattern / Insight |
|---|---|---|---|
| 1 | `Car.createCar(context)` | `DiagClient.create(context)` | Service-binding factory; hides `ServiceConnection` boilerplate from caller |
| 2 | `CarHvacManager`, `CarSensorManager` | `DiagClient` | Domain-typed manager API; caller never touches raw AIDL |
| 3 | `CarService` (runs in `system_server` or dedicated process) | `DiagCarService` (`extends Service`) | Service hosts the Binder stub, runs in isolated process `:car_service` |
| 4 | `VehicleHal` (JNI wrapper in CarService) | `DiagHalBridge` (`libvdiag_jni.so`) | Single JNI bridge between Java service and C++ HAL; owns GlobalRef lifecycle |
| 5 | `IVehicle.aidl` (stable HIDL/AIDL HAL contract) | `IDiagnosticHal` (pure virtual C++) | HAL contract decouples service from transport; swap without touching framework |
| 6 | `DefaultVehicleHal` (emulator/reference impl) | `MockDiagnosticHal` | In-memory DID database; deterministic for unit tests |
| 7 | `VehiclePropValue` (AIDL parcelable) | `DiagRequest` (AIDL parcelable) | Serializable data transfer object across Binder boundary |
| 8 | `VehicleProperty` enum | `DiagProperty` enum | Named property IDs; UI code never uses raw hex |
| 9 | `PERMISSION_CAR_DIAGNOSTIC_READ_ALL` | `com.vdiag.permission.DIAGNOSE` | `protectionLevel="signature"` → only same-key APK can bind |
| 10 | `CarPropertyManager.registerCallback()` | `DiagClient.subscribeProperty()` | Push / subscribe event model; caller registers interest, service polls HAL |
| 11 | `CarPropertyEventCallback` | `IDiagPropertyListener` (`oneway`) | `oneway` AIDL = fire-and-forget; never blocks service thread waiting for client |
| 12 | `CarWatchdogClient` | `CarApiSystemClient` | System health heartbeat — must respond within timeout (3 s) or OS kills service |
| 13 | *(gap — emulator only)* | `ShimSystemClient` | Handler-based 3 s heartbeat shim; swapped in at runtime on non-Automotive AVD |
| 14 | `CarPowerManager.setListener()` | `DiagPowerListener` | Shutdown/suspend lifecycle — service must flush and ACK before power-off |
| 15 | HAL manifest in `compatibility_matrix.xml` | VINTF manifest + `@VintfStability` | Stable HAL versioning; OTA update can replace HAL without breaking framework |

---

## 3. Interface boundary comparison (8 boundaries)

| Boundary | AAOS analog | VDiag implementation | Risk handled |
|---|---|---|---|
| **B1 — Binder IPC** | `android.os.IBinder` + `Parcelable` | `DiagRequest` parcelable + `IDiagCarService.Stub` | `DeathRecipient` auto-cleans up dead clients in `ClientRegistry` |
| **B2 — JNI** | `VehicleHal` JNI bridge | `JniCallbackBridge` (RAII GlobalRef + `pthread_key` detach) | Prevents GlobalRef leak + dangling JNIEnv on native thread |
| **B3 — Engine queue** | `VehicleHal` internal request queue | 4-tier priority queue + PI mutex (`PTHREAD_PRIO_INHERIT`) | Priority inversion; SCHED_FIFO worker (graceful EPERM fallback) |
| **B4 — HAL abstraction** | `IVehicle` swap (HIDL service replacement) | `IDiagnosticHal` pure virtual + `HalFactory` | Swap Mock ↔ DoIP ↔ CAN ↔ ADAS without touching DiagEngine |
| **B5 — Subscription** | `CarPropertyManager` subscription registry | `SubscriptionManager` max-rate + `DeathRecipient` | Ghost listeners after client death; 100 ms tick max-rate policy |
| **B6 — System health** | `CarWatchdog` + `CarPowerManager` | `ISystemLifecycle` factory (Automotive AVD / shim) | Silent crash on heartbeat miss; graceful shutdown on power event |
| **B7 — Bring-up** | `init.rc` + SELinux + VINTF | `init.vdiag.rc` + `.te` policy + manifest XML | `avc: denied` at boot; VINTF mismatch blocking service start |
| **B8 — Embedded target** | ARM64 SoC (real hardware) | ARM64 cross-compile + QEMU user-mode | Alignment faults; all gtest pass on ARM64, zero Android runtime dependency |

---

## 4. Permission model comparison

```
AAOS                                  VDiag
────────────────────────────────────  ────────────────────────────────────
android.car.permission               com.vdiag.permission
  .CAR_DIAGNOSTIC_READ_ALL             .DIAGNOSE
  protectionLevel: signature           protectionLevel: signature

Granted by: CarService PackageManager  Granted by: same signing key (debug
  on Automotive builds                   keystore in dev, OEM key in prod)

Enforcement point:                     Enforcement point:
  ICarDiagnostic.Stub.onTransact()       PermissionGate.enforce(ctx)
  → enforceCallingOrSelfPermission()     → enforceCallingOrSelfPermission()
```

**Interview line:**
> *"Tôi chọn `protectionLevel="signature"` thay vì `dangerous` vì diagnostic data (DTC, battery SOC) là sensitive. Chỉ VinFast-signed app được grant — đúng model của AAOS trên production vehicle."*

---

## 5. Data flow comparison

### AAOS: Read a vehicle property
```
App
  CarSensorManager.getProperty(SPEED)
    → [Binder] CarPropertyService.getProperty()
    → VehicleHal.get(VehicleProp.SPEED)
    → [JNI] DefaultVehicleHal::get()
    → return VehiclePropValue{floatValues: [60.0]}
    → [Binder callback] CarPropertyEventCallback.onChangeEvent()
App receives speed update
```

### VDiag: Read a diagnostic property
```
App
  DiagClient.getProperty(DiagProperty.VIN)
    → [Binder B1] DiagServiceBinder.getProperty(req, callback)
    → PermissionGate.enforce()  |  ClientRegistry.register() + linkToDeath
    → DiagHalBridge.nativeGetProperty(reqId, 0xF190, callback)
    → [JNI B2] JniCallbackBridge: NewGlobalRef(callback)
    → DiagEngine.submit(req, bridge_lambda)
    → [Worker thread B3] UdsCodec.encode(0x22, 0xF190)
    → [HAL B4] MockDiagnosticHal.sendAndReceive({0x22, 0xF1, 0x90})
    → UdsCodec.decode({0x62, 0xF1, 0x90, 'V','I','N',...})
    → bridge_lambda → AttachCurrentThread → CallVoidMethod(onResult)
    → [Binder callback B1] IDiagCallback.onResult(1, "VINFAST12345678901", 12000)
App receives VIN
```

---

## 6. Key differences (intentional)

| Aspect | AAOS | VDiag | Reason for difference |
|---|---|---|---|
| HAL interface | AIDL (`IVehicle.aidl`) → binderized HAL | Pure virtual C++ (`IDiagnosticHal`) | No Android BSP needed; runs as standalone binary on Linux host |
| Transport protocols | VHAL property types (CAN, etc. hidden) | Explicit: Mock / DoIP (ISO 13400) / CAN (ISO-TP) / ADAS | Showcase multi-transport architecture for interview |
| Real-time | Not guaranteed (CarService in Android) | SCHED_FIFO worker + PI mutex (with EPERM fallback) | Demonstrate RT awareness even in emulator context |
| `@VintfStability` | Required for system image boundary | Verified syntax; documented as production would deploy | No hardware flashing needed to demonstrate understanding |

---

## 7. Interview talking points

1. **Pattern recognition:** *"Tôi mở `packages/services/Car/` trong AOSP, đọc `CarService.java` và `VehicleHal.cpp`, rồi clone pattern đó: Bound Service → JNI bridge → HAL interface. Mỗi component VDiag match 1-1 với AAOS."*

2. **Why Bound Service, not Started Service:** *"Bound Service cho phép client nhận callback, có lifetime tied to binding — khi tất cả client unbind, service có thể stop. Đúng model của CarService."*

3. **Why signature permission:** *"Diagnostic commands (DTC clear, ECU reset) nguy hiểm. `signature` permission đảm bảo chỉ OEM-signed app được bind — không thể bypass bằng install từ apk lạ."*

4. **Scalability to real AAOS:** *"Để deploy lên hardware thật: thay `IDiagnosticHal` implementation bằng binderized AIDL HAL, đăng ký VINTF manifest, viết SELinux policy. Framework code (`DiagCarService`, `DiagClient`) không đổi dòng nào."*
