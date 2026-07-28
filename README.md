# VDiag — Vehicle Diagnostics on AAOS

[![VDiag CI](https://github.com/TranVanL/AutoDiag/actions/workflows/ci.yml/badge.svg)](https://github.com/TranVanL/AutoDiag/actions/workflows/ci.yml)
[![Tests](https://img.shields.io/badge/tests-35%20passing-brightgreen)](#testing)
[![ASAN](https://img.shields.io/badge/ASAN-clean-brightgreen)](#testing)
[![TSAN](https://img.shields.io/badge/TSAN-clean-brightgreen)](#testing)
[![Platform](https://img.shields.io/badge/platform-AAOS%20API%2026%2B-blue)](https://source.android.com/docs/automotive)
[![Language](https://img.shields.io/badge/language-Java%20%7C%20C%2B%2B17-orange)](#tech-stack)

---

## 30-second pitch

> VDiag is a **full-stack Android Automotive OS vehicle-diagnostics demo** that deliberately clones the AAOS CarService architecture — `Bound Service → JNI bridge → HAL interface` — applied to the UDS diagnostic domain. Every component maps 1-to-1 to a production AAOS equivalent. Eight architectural boundaries, each with thread safety, error propagation, and resource cleanup.

---

## Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│ APP + SDK  (process: com.vdiag)                                        │
│   DiagActivity  →  DiagClient (= CarDiagnosticManager-style)          │
├────────────────────────────────────────────────────────────────────────┤
│                  ═══ B1: BINDER IPC (AIDL, cross-process) ═══         │
├────────────────────────────────────────────────────────────────────────┤
│ DIAG CAR SERVICE  (process: com.vdiag:car_service)                    │
│   DiagCarService  →  DiagServiceBinder (IDiagCarService.Stub)         │
│   PermissionGate (signature perm)  ·  ClientRegistry + DeathRecipient │
├────────────────────────────────────────────────────────────────────────┤
│                  ═══ B2: JNI (GlobalRef RAII + auto-detach) ═══       │
├────────────────────────────────────────────────────────────────────────┤
│ HAL BRIDGE  (libvdiag_jni.so)                                         │
│   JNI_OnLoad: cache jclass + jmethodID  ·  JniCallbackBridge (RAII)  │
├────────────────────────────────────────────────────────────────────────┤
│         ═══ B3: ENGINE QUEUE (4-tier priority + PI mutex) ═══         │
├────────────────────────────────────────────────────────────────────────┤
│ DIAG ENGINE  (C++17, libvdiag_hal.a)                                  │
│   4-tier queue (CRITICAL>HIGH>NORMAL>LOW) · SCHED_FIFO worker         │
│   SessionStateMachine · UdsCodec (ISO 14229, 6 services)              │
├────────────────────────────────────────────────────────────────────────┤
│               ═══ B4: HAL ABSTRACTION (pure virtual) ═══              │
├────────────────────────────────────────────────────────────────────────┤
│ DIAGNOSTIC HAL  (IDiagnosticHal pure virtual)                         │
│   MockDiagnosticHal  │  DoipDiagnosticHal (TCP/ISO 13400)             │
│   CanDiagnosticHal (SocketCAN/ISO-TP)  │  MockAdasHal / TcpAdasHal   │
└────────────────────────────────────────────────────────────────────────┘
  Cross-cutting:
    B5 SubscriptionManager (100ms tick · max-rate · DeathRecipient)
    B6 ISystemLifecycle (CarWatchdog / Handler shim)
    B7 Bring-up (init.rc · SELinux · VINTF · privapp-permissions)
    B8 Embedded (ARM64 cross-compile · QEMU user-mode)
```

---

## Quick start (3 commands)

```bash
# 1 — Build & test HAL (Linux host, no device needed)
cd hal && mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && ctest --output-on-failure

# 2 — Run with ASAN
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address" && make -j$(nproc) && ctest

# 3 — Build Android APK (requires Android Studio SDK)
cd android && ./gradlew assembleDebug
# Install: adb install app/build/outputs/apk/debug/app-debug.apk
```

> **No hardware required.** Everything runs on Linux host + Android Studio Automotive AVD. See [docs/bringup.md](docs/bringup.md) for production device deployment.

---

## AAOS component mapping

| AAOS (production) | VDiag (this project) | Pattern |
|---|---|---|
| `Car.createCar(context)` | `DiagClient.create(context)` | Service-binding factory |
| `CarHvacManager` / `CarSensorManager` | `DiagClient` | Domain-typed manager |
| `CarService` | `DiagCarService` | Bound Service, isolated process |
| `VehicleHal` (JNI wrapper) | `DiagHalBridge` (`libvdiag_jni.so`) | Java↔C++ bridge |
| `IVehicle.aidl` | `IDiagnosticHal` (pure virtual) | HAL contract |
| `DefaultVehicleHal` | `MockDiagnosticHal` | Reference / emulator impl |
| `VehiclePropValue` | `DiagRequest` (parcelable) | IPC data transfer object |
| `PERMISSION_CAR_DIAGNOSTIC_READ_ALL` | `com.vdiag.permission.DIAGNOSE` | Signature-level permission |
| `CarPropertyManager.registerCallback()` | `DiagClient.subscribeProperty()` | Push/subscribe event model |
| `CarWatchdogClient` | `CarApiSystemClient` | System health heartbeat |
| `CarPowerManager.setListener()` | `DiagPowerListener` | Shutdown/suspend lifecycle |
| VINTF manifest | `docs/bringup.md` + `@VintfStability` | Stable HAL versioning |

Full 15-row table with boundary analysis: [docs/aaos_comparison.md](docs/aaos_comparison.md)

---

## Tech stack

| Layer | Technology |
|---|---|
| **Android app** | Java, AIDL, Binder, `android:process`, `DeathRecipient` |
| **JNI bridge** | C++17, `JNI_OnLoad`, RAII GlobalRef, `pthread_key` auto-detach |
| **HAL core** | C++17, CMake, 4-tier priority queue, PI mutex (`PTHREAD_PRIO_INHERIT`) |
| **UDS codec** | ISO 14229 — `0x22` ReadByIdentifier, `0x14` ClearDTC, `0x19` ReadDTC, `0x10` SessionControl, `0x3E` TesterPresent, `0x27` SecurityAccess |
| **Transports** | Mock (in-process) · DoIP TCP/ISO 13400 · SocketCAN/ISO-TP · ADAS TCP sim |
| **System health** | `CarWatchdog` (Automotive AVD) / `Handler` shim (standard AVD) |
| **Build** | CMake (HAL standalone) · Gradle (Android) · GitHub Actions CI |
| **Testing** | Custom test runner · ASAN · TSAN · ARM64 cross-compile + QEMU |

---

## Testing

```
hal/tests/
  test_uds_codec.cpp     — UDS encode/decode, all 6 services, positive + NRC paths
  test_mock_hal.cpp      — MockDiagnosticHal DID lookup, DTC clear, boundary cases
  test_session_state.cpp — SessionStateMachine transitions (Idle→Pending→Done|Error)

Total: 35 tests
ASAN:  clean (0 leaks, 0 heap errors)
TSAN:  clean (0 data races)
ARM64: all 35 pass under QEMU user-mode (aarch64-linux-gnu-g++ + qemu-aarch64)
```

Run:
```bash
cd hal/build && ctest --output-on-failure -V
```

CI matrix (GitHub Actions): `[None, Address, Thread]` sanitizers × native + Android jobs. See [.github/workflows/ci.yml](.github/workflows/ci.yml).

---

## Documentation

| Doc | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Layer diagram, component roles |
| [docs/aaos_comparison.md](docs/aaos_comparison.md) | AAOS ↔ VDiag 15-component mapping, 8 boundaries, data flow |
| [docs/jni_lifecycle.md](docs/jni_lifecycle.md) | 3 JNI pitfalls (GlobalRef leak, wrong-thread crash, double-delete) + solutions |
| [docs/bringup.md](docs/bringup.md) | init.rc · SELinux policy · VINTF manifest · privapp-permissions |
| [docs/lessons.md](docs/lessons.md) | Weekly lessons learned |
| [AutoDiag/01_ARCHITECTURE.md](../../01_ARCHITECTURE.md) | Full 8-boundary architecture deep-dive |
| [AutoDiag/03_INTERVIEW_PREP.md](../../03_INTERVIEW_PREP.md) | Interview Q&A (Q1–Q30) |

---

## Project structure

```
VDiag/
├── android/                  # Android Studio project
│   └── app/src/main/
│       ├── aidl/com/vdiag/   # IDiagCarService, IDiagCallback, DiagRequest
│       ├── java/com/vdiag/
│       │   ├── service/      # DiagCarService, DiagServiceBinder, DiagHalBridge
│       │   ├── sdk/          # DiagClient, DiagProperty, SubscriptionManager
│       │   └── ui/           # DiagActivity (6-button dark UI)
│       └── cpp/              # jni_onload, jni_bridge, jni_callback (RAII)
│
├── hal/                      # C++ standalone (Linux host)
│   ├── include/              # IDiagnosticHal, DiagEngine, UdsCodec, ...
│   ├── src/                  # MockDiagnosticHal, DoipDiagnosticHal, CanHal, ...
│   └── tests/                # 35 tests — no Android runtime dependency
│
├── python_simulator/         # DoIP ECU + ADAS sensor simulators
│
├── docs/                     # Detailed docs (see table above)
│
└── .github/workflows/
    └── ci.yml                # 3-job matrix: native ASAN/TSAN + Android APK
```

---

## License

MIT
