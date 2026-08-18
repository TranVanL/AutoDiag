# VDiag — Vehicle Diagnostics on AAOS

[![VDiag CI](https://github.com/TranVanL/AutoDiag/actions/workflows/ci.yml/badge.svg)](https://github.com/TranVanL/AutoDiag/actions/workflows/ci.yml)
[![Tests](https://img.shields.io/badge/tests-51%20passing-brightgreen)](#testing)
[![ASAN](https://img.shields.io/badge/ASAN-clean-brightgreen)](#testing)
[![TSAN](https://img.shields.io/badge/TSAN-clean-brightgreen)](#testing)
[![ARM64](https://img.shields.io/badge/ARM64-QEMU%20tested-success)](#embedded-target)
[![Platform](https://img.shields.io/badge/platform-AAOS%20API%2026%2B-blue)](https://source.android.com/docs/automotive)
[![Language](https://img.shields.io/badge/language-Java%20%7C%20C%2B%2B17-orange)](#tech-stack)

---

## 30-second pitch

VDiag clones the AAOS CarService stack for vehicle diagnostics. 100-day build: stable AIDL versioning → HAL service lifecycle → 3-transport HAL abstraction (Mock/DoIP/CAN) → HAL reusability across 8 boundaries, 50+ tests, ASAN/TSAN/CheckJNI clean.

---

## What this actually is

Most GitHub demos stop at "I can read an OBD-II PID over Bluetooth." This one goes the other way: it treats diagnostics as a **system-level Android Automotive feature**, with the same layering, permissions, and lifecycle you'd ship on a real car.

The app layer talks to a bound car service over Binder. The service talks through JNI to a C++ HAL core. The HAL core dispatches UDS requests through a pluggable transport layer. Swap Mock for DoIP, or DoIP for SocketCAN, and the framework code above it does not change.

Why build it this way? Because in production, the diagnostic stack lives in multiple processes, crosses Java/C++ boundaries, has to survive client crashes, and must boot with the rest of the vehicle. VDiag is a sandbox for all of those problems.

---

## The 8 boundaries

| Boundary | What it separates | How VDiag handles it |
|---|---|---|
| **B1** App → Car Service | Binder IPC, cross-process | `IDiagCarService.aidl`, signature permission gate, `ClientRegistry` |
| **B2** Java → Native | JNI lifecycle | `JNI_OnLoad` class caching, RAII `GlobalRef`, `pthread_key` auto-detach |
| **B3** Service → Engine | Threading + priority | 4-tier priority queue, `SCHED_FIFO` worker, PI mutex |
| **B4** Engine → Transport | HAL abstraction | `IDiagnosticHal` pure virtual interface — Mock/DoIP/CAN plug in unchanged |
| **B5** Property subscription | Push model | `SubscriptionManager`, per-area `areaId`, max-rate throttling, `DeathRecipient` cleanup |
| **B6** System lifecycle | Power/health | `ISystemLifecycle` shim, CarWatchdog-ready heartbeat |
| **B7** Device bring-up | Vendor integration | `init.vdiag.rc`, SELinux `.te` policy, VINTF manifest, privapp-permissions |
| **B8** Embedded target | Host vs. target | ARM64 cross-compile with `aarch64-linux-gnu`, QEMU user-mode test run |

---

## Quick start

```bash
# 1 — Build & test HAL on Linux host (no device needed)
cd hal && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && ctest --output-on-failure

# 2 — Run with ASAN
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address" && make -j$(nproc) && ctest

# 3 — Cross-compile for ARM64 and run under QEMU
cd .. && mkdir -p build_arm64 && cd build_arm64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
         -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"
make -j$(nproc) && ctest --output-on-failure

# 4 — Build Android APK (requires Android Studio SDK)
cd ../../android && ./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

> **No hardware required.** Everything runs on a Linux host + Android Automotive AVD. For production device deployment, see [docs/bringup.md](docs/bringup.md).

---

## Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│ APP + SDK  (process: com.vdiag)                                        │
│   MainActivity  →  DiagClient (= CarDiagnosticManager-style)          │
├────────────────────────────────────────────────────────────────────────┤
│                  ═══ B1: BINDER IPC (AIDL, cross-process) ═══         │
├────────────────────────────────────────────────────────────────────────┤
│ DIAG CAR SERVICE  (process: com.vdiag:car_service)                    │
│   DiagCarService  →  DiagCarServiceBinder (IDiagCarService.Stub)      │
│   PermissionGate (per-property signature perm)                        │
│   ClientRegistry + DeathRecipient cleanup                             │
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
│   CanDiagnosticHal (SocketCAN/ISO-TP)                                 │
└────────────────────────────────────────────────────────────────────────┘
  Cross-cutting:
    B5 SubscriptionManager (100ms tick · max-rate · DeathRecipient)
    B6 ISystemLifecycle (CarWatchdog / Handler shim)
    B7 Bring-up (init.rc · SELinux · VINTF · privapp-permissions)
    B8 Embedded (ARM64 cross-compile · QEMU user-mode)
```

---

## AAOS component mapping

| AAOS (production) | VDiag (this project) | Pattern |
|---|---|---|
| `Car.createCar(context)` | `DiagClient.create(context)` | Service-binding factory |
| `CarHvacManager` / `CarSensorManager` | `DiagClient` | Domain-typed manager |
| `CarService` | `DiagCarService` | Bound Service, isolated `:car_service` process |
| `VehicleHal` (JNI wrapper) | `DiagHalBridge` (`libvdiag_jni.so`) | Java↔C++ bridge |
| `IVehicle.aidl` | `IDiagnosticHal` (pure virtual) | HAL contract |
| `DefaultVehicleHal` | `MockDiagnosticHal` | Reference / emulator impl |
| `VehiclePropValue` | `DiagRequest` / `DiagPropertyEvent` | IPC data transfer object |
| `PERMISSION_CAR_*` | `com.vdiag.permission.DIAGNOSE`, `READ_BATTERY`, `READ_TIRES`, `READ_POWERTRAIN` | Signature-level, per-property permission |
| `CarPropertyManager.registerCallback()` | `DiagClient.subscribeProperty(areaId, rateHz, callback)` | Push/subscribe event model with areaId |
| `CarPropertyEvent` | `DiagPropertyEvent` with `status` enum | AVAILABLE / UNAVAILABLE / ERROR |
| `CarWatchdogClient` | `ISystemLifecycle` + `ShimSystemClient` | System health heartbeat |
| VINTF manifest | `android/bringup/manifest_vdiag.xml` + `@VintfStability` AIDL HAL | Stable HAL versioning |

Full 15-row table with boundary analysis: [docs/aaos_comparison.md](docs/aaos_comparison.md)

---

## Tech stack

| Layer | Technology |
|---|---|
| **Android app** | Java, AIDL, Binder, `android:process=":car_service"`, `DeathRecipient` |
| **JNI bridge** | C++17, `JNI_OnLoad`, RAII GlobalRef, `pthread_key` auto-detach |
| **HAL core** | C++17, CMake, 4-tier priority queue, PI mutex (`PTHREAD_PRIO_INHERIT`) |
| **UDS codec** | ISO 14229 — `0x22` ReadByIdentifier, `0x14` ClearDTC, `0x19` ReadDTC, `0x10` SessionControl, `0x3E` TesterPresent, `0x27` SecurityAccess |
| **Transports** | Mock (in-process) · DoIP TCP/ISO 13400 · SocketCAN/ISO-TP |
| **System health** | `CarWatchdog` (Automotive AVD) / `Handler` shim (standard AVD) |
| **Build** | CMake (HAL standalone) · Gradle (Android) · GitHub Actions CI |
| **Testing** | gtest · ASAN · TSAN · ARM64 cross-compile + QEMU · Python integration · Android instrumented tests |

---

## Testing

```
hal/tests/
  test_uds_codec_gtest.cpp      — UDS encode/decode, all 6 services, positive + NRC paths
  test_mock_hal_gtest.cpp       — MockDiagnosticHal DID lookup, DTC clear, area-aware properties
  test_session_state_gtest.cpp  — SessionStateMachine transitions (Idle→Pending→Done|Error)

android/app/src/test/           — Java unit tests
android/app/src/androidTest/    — Permission-gate instrumented tests
python_simulator/testDoIP.py    — Full DoIP round-trip against Python ECU simulator

Total: 51 tests
  - 38 HAL gtest cases
  - 8 Android instrumented permission tests
  - 4 Python DoIP integration tests
  - 1 Java unit test

ASAN:  clean (0 leaks, 0 heap errors)
TSAN:  clean (0 data races)
ARM64: all HAL tests pass under QEMU user-mode (aarch64-linux-gnu-g++ + qemu-aarch64-static)
```

Run:
```bash
cd hal/build && ctest --output-on-failure -V
```

CI matrix (GitHub Actions): native x86_64 with `[None, Address, Thread]` sanitizers · ARM64 cross-compile + QEMU · Android APK build · Python DoIP simulation. See [.github/workflows/ci.yml](.github/workflows/ci.yml).

---

## Embedded target

The HAL builds standalone for ARM64 without the Android runtime. This matters because on a real ECU or domain controller you often cannot run the full Android stack, but you still want to reuse the same UDS codec, session state machine, and transport abstraction.

```bash
cd hal
mkdir -p build_arm64 && cd build_arm64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
         -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"
make -j$(nproc) && ctest --output-on-failure
```

`file test_uds_codec` → `ELF 64-bit LSB executable, ARM aarch64`. The same source tree compiles for host and target because the HAL layer is POSIX-only and has no dependency on Android frameworks.

---

## Android bring-up

Production AAOS integration files live in `android/bringup/`:

| File | Purpose |
|---|---|
| `init.vdiag.rc` | Starts `vdiag_hal` as a `class hal` service at boot |
| `sepolicy/vdiag_hal.te` | SELinux type enforcement: Binder, socket, CAN device, logd, vendor property |
| `manifest_vdiag.xml` | VINTF manifest declaring `android.hardware.vdiag@1.0::IDiagnosticHal/default` |
| `privapp-permissions-vdiag.xml` | Privileged-app permission allowlist |
| `framework_compatibility_matrix.xml` | Framework compatibility matrix for VINTF enforcement |

The vendor-facing HAL interface is annotated with `@VintfStability` and versioned under `android/aidl_hal/v1/`, matching the AAOS stable AIDL freeze workflow.

---
## Project structure

```
VDiag/
├── android/                  # Android Studio project
│   ├── app/src/main/
│   │   ├── aidl/com/vdiag/   # IDiagCarService, IDiagCallback, DiagRequest, DiagPropertyEvent
│   │   ├── java/com/vdiag/
│   │   │   ├── service/      # DiagCarService, DiagCarServiceBinder, DiagHalBridge, ClientRegistry
│   │   │   ├── sdk/          # DiagClient, DiagProperty, DiagListener
│   │   │   └── ui/           # MainActivity, ResultAdapter
│   │   └── cpp/              # jni_onload, jni_bridge, jni_callback (RAII)
│   ├── aidl_hal/             # @VintfStability vendor HAL + v1 snapshot
│   └── bringup/              # init.rc, sepolicy, VINTF manifest, permissions
│
├── hal/                      # C++ standalone (Linux host + ARM64 target)
│   ├── include/              # IDiagnosticHal, DiagEngine, UdsCodec, SessionState, ...
│   ├── src/                  # MockDiagnosticHal, DoipDiagnosticHal, CanDiagnosticHal, ...
│   ├── tests/                # gtest suite — no Android runtime dependency
│   └── cmake/toolchain-aarch64.cmake
│
├── python_simulator/         # DoIP ECU simulator + integration test client
│
├── docs/                     # Detailed docs (see table above)
│
└── .github/workflows/
    └── ci.yml                # x86_64 + ASAN/TSAN + ARM64 QEMU + Android + Python
```

---

## CV bullets

```
VDiag — Android Automotive Vehicle Diagnostic Platform (C++17, Java, AIDL)
• Stable AIDL versioning: @VintfStability vendor HAL interface + v1 snapshot for OTA compatibility
• HAL service lifecycle: isolated :car_service process + DeathRecipient client cleanup
• 3-transport HAL abstraction: Mock / DoIP / CAN — framework zero-change via IDiagnosticHal
• Android bring-up: init.rc + SELinux .te policy + VINTF manifest + privapp-permissions
• ARM64 cross-compile: aarch64-linux-gnu + QEMU user-mode — HAL tests green on ARM target
• CI matrix: x86_64 + ARM64 + ASAN + TSAN + Android APK + Python simulation
```

---

## License

MIT
