# VDiag — Vehicle Diagnostics on AAOS

[![VDiag CI](https://github.com/TranVanL/AutoDiag/actions/workflows/ci.yml/badge.svg)](https://github.com/TranVanL/AutoDiag/actions/workflows/ci.yml)
[![Tests](https://img.shields.io/badge/tests-~51%20passing-brightgreen)](#testing)
[![ASAN](https://img.shields.io/badge/ASAN-clean-brightgreen)](#testing)
[![TSAN](https://img.shields.io/badge/TSAN-clean-brightgreen)](#testing)
[![ARM64](https://img.shields.io/badge/ARM64-QEMU%20tested-success)](#embedded-target)
[![Platform](https://img.shields.io/badge/platform-AAOS%20API%2026%2B-blue)](https://source.android.com/docs/automotive)
[![Language](https://img.shields.io/badge/language-Java%20%7C%20C%2B%2B17-orange)](#tech-stack)

---

## TL;DR

VDiag is a hands-on AAOS diagnostics stack. It shows how a real car would run diagnostics across Android processes, JNI, C++ HAL, and network transports — not just read an OBD-II PID over Bluetooth.

You can build it on a laptop, run it on an emulator, swap transports without touching the app, and verify everything with ASAN, TSAN, ARM64 QEMU, and a Python ECU simulator.

---

## What makes this different?

Most GitHub demos stop at "I can talk to an ELM327 adapter." VDiag goes the other way: it models the **system-level stack** you would actually ship in a vehicle.

That means:

- **Multi-process by design** — the app and the car service live in separate processes, connected by Binder/AIDL.
- **Permission per property** — reading VIN, battery SOC, or tire pressure each requires its own signature permission.
- **JNI done carefully** — class/method caching in `JNI_OnLoad`, RAII `GlobalRef`, and exception-safe callbacks.
- **Pluggable transport** — same framework code talks to Mock, DoIP TCP, or SocketCAN on Linux host.
- **Crash-safe** — `DeathRecipient` cleans up when a client dies mid-request.
- **Push, not poll** — property subscriptions use area-aware, rate-throttled push events.
- **CI that actually checks things** — ASAN, TSAN, ARM64 cross-compile under QEMU, Android APK build, and Python DoIP integration.

---

## The stack in 30 seconds

```
App (Java)  →  Car Service (Java, :car_service)
                     ↓ Binder / AIDL
              JNI Bridge (libvdiag_jni.so)
                     ↓
              DiagEngine (C++17)
                     ↓
              IDiagnosticHal
         Mock  │  DoIP TCP  │  SocketCAN (Linux host)
```

The HAL layer is pure virtual. Swap the implementation, and nothing above it changes.

---

## What is actually implemented?

| Layer | Highlights |
|---|---|
| **Android app** | `DiagClient` SDK facade, `MainActivity`, bound service pattern |
| **Car service** | `DiagCarService` in isolated `:car_service` process, `DiagCarServiceBinder`, `ClientRegistry`, `PermissionGate` |
| **JNI** | `JNI_OnLoad` caching, RAII `GlobalRef`, `JniCallbackBridge`, `pthread_key` auto-detach |
| **Engine** | 4-tier priority queue, worker thread, `SessionStateMachine`, `UdsCodec` |
| **HAL** | `IDiagnosticHal` interface, `MockDiagnosticHal`, `DoipDiagnosticHal`, `CanDiagnosticHal` |
| **UDS services** | `0x22` ReadByIdentifier, `0x14` ClearDTC, `0x19` ReadDTC, `0x10` SessionControl, `0x3E` TesterPresent, `0x27` SecurityAccess |
| **Subscriptions** | `SubscriptionManager`, per-area `areaId`, 100ms tick, rate throttling, `DeathRecipient` cleanup |
| **Bring-up** | Reference `init.vdiag.rc`, SELinux `.te`, VINTF manifest, privapp-permissions, `@VintfStability` AIDL HAL v1 |
| **Embedded** | ARM64 cross-compile with `aarch64-linux-gnu`, QEMU user-mode test run |

> **Note on CAN:** `CanDiagnosticHal` runs on Linux hosts via SocketCAN (`vcan0`). It is intentionally gated out of the Android device build because production CAN access needs board-specific kernel and SELinux work.

---

## Quick start

```bash
# 1 — Build & test the HAL on a Linux host
cd hal && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && ctest --output-on-failure

# 2 — Run with AddressSanitizer
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address" && make -j$(nproc) && ctest

# 3 — Cross-compile for ARM64 and run under QEMU
cd .. && mkdir -p build_arm64 && cd build_arm64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
         -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"
make -j$(nproc) && ctest --output-on-failure

# 4 — Build the Android APK
cd ../../android && ./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

No hardware required. Everything runs on a Linux host + Android Automotive emulator.

---

## Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│  APP (com.vdiag)                                                       │
│    MainActivity → DiagClient                                           │
├────────────────────────────────────────────────────────────────────────┤
│  ═══ B1: BINDER IPC (AIDL, cross-process) ═══                         │
├────────────────────────────────────────────────────────────────────────┤
│  CAR SERVICE (com.vdiag:car_service)                                   │
│    DiagCarServiceBinder · PermissionGate · ClientRegistry              │
│    SubscriptionManager · DeathRecipient cleanup                        │
├────────────────────────────────────────────────────────────────────────┤
│  ═══ B2: JNI (GlobalRef RAII + auto-detach) ═══                       │
├────────────────────────────────────────────────────────────────────────┤
│  libvdiag_jni.so                                                       │
│    JNI_OnLoad cache · JniCallbackBridge                                │
├────────────────────────────────────────────────────────────────────────┤
│  ═══ B3: ENGINE QUEUE (4-tier priority) ═══                           │
├────────────────────────────────────────────────────────────────────────┤
│  DiagEngine (C++17)                                                    │
│    Priority queue · Worker thread · SessionStateMachine · UdsCodec     │
├────────────────────────────────────────────────────────────────────────┤
│  ═══ B4: HAL ABSTRACTION (pure virtual) ═══                           │
├────────────────────────────────────────────────────────────────────────┤
│  IDiagnosticHal                                                        │
│    MockDiagnosticHal · DoipDiagnosticHal · CanDiagnosticHal (host)     │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Testing

```
hal/tests/
  test_uds_codec_gtest.cpp      — UDS encode/decode, 6 services, positive + NRC paths
  test_mock_hal_gtest.cpp       — Mock DID lookup, DTC clear, area-aware properties
  test_session_state_gtest.cpp  — Session state transitions
  test_diag_engine_gtest.cpp    — Priority queue + worker behavior

android/app/src/androidTest/    — Permission-gate instrumented tests
python_simulator/testDoIP.py    — DoIP round-trip against Python ECU simulator
```

**~51 tests total:** 42 HAL gtest cases + 9 Android instrumented tests + Python integration sequences.

- ASAN: clean
- TSAN: clean
- ARM64: all HAL tests pass under QEMU user-mode

Run HAL tests:
```bash
cd hal/build && ctest --output-on-failure -V
```

CI runs native x86_64 with `[None, Address, Thread]` sanitizers, ARM64 QEMU, Android APK build, and Python DoIP simulation. See [.github/workflows/ci.yml](.github/workflows/ci.yml).

---

## Embedded target

The HAL builds standalone for ARM64 without the Android runtime. Same UDS codec, same session machine, same transport abstraction — reusable on an ECU or domain controller.

```bash
cd hal
mkdir -p build_arm64 && cd build_arm64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
         -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"
make -j$(nproc) && ctest --output-on-failure
```

---

## Why this matters for hiring

This project demonstrates the kind of system thinking that matters in automotive Android:

- **Architecture:** proper layering across Java/C++/network boundaries.
- **Robustness:** crash cleanup, exception-safe JNI, state machines.
- **Testing:** sanitizers, cross-compilation, integration simulation.
- **Production awareness:** permissions, multi-process lifecycle, bring-up reference files.

It is not a toy OBD reader. It is a miniature AAOS diagnostics subsystem.

---

## Documentation

| Folder | Docs |
|---|---|
| **01-architecture** | [`01-system-architecture.md`](docs/01-architecture/01-system-architecture.md) · [`02-hal-service-deep-dive.md`](docs/01-architecture/02-hal-service-deep-dive.md) · [`03-jni-lifecycle.md`](docs/01-architecture/03-jni-lifecycle.md) · [`04-stable-aidl-hal.md`](docs/01-architecture/04-stable-aidl-hal.md) |
| **02-modules** | [`01-doip-module.md`](docs/02-modules/01-doip-module.md) · [`02-transport-comparison.md`](docs/02-modules/02-transport-comparison.md) · [`03-property-subscription.md`](docs/02-modules/03-property-subscription.md) · [`04-property-subscription-deep.md`](docs/02-modules/04-property-subscription-deep.md) · [`05-carwatchdog-power.md`](docs/02-modules/05-carwatchdog-power.md) |
| **03-performance** | [`01-performance-tuning.md`](docs/03-performance/01-performance-tuning.md) · [`02-multi-ecu-scaling.md`](docs/03-performance/02-multi-ecu-scaling.md) |
| **04-testing-debugging** | [`01-testing-pyramid.md`](docs/04-testing-debugging/01-testing-pyramid.md) · [`02-debugging-observability.md`](docs/04-testing-debugging/02-debugging-observability.md) · [`03-ci-cd-deep-dive.md`](docs/04-testing-debugging/03-ci-cd-deep-dive.md) |
| **05-bringup** | [`01-bringup-guide.md`](docs/05-bringup/01-bringup-guide.md) · [`02-bringup-troubleshooting.md`](docs/05-bringup/02-bringup-troubleshooting.md) · [`03-aaos-comparison.md`](docs/05-bringup/03-aaos-comparison.md) |
| **06-project-history** | [`01-project-evolution.md`](docs/06-project-history/01-project-evolution.md) · [`02-lessons.md`](docs/06-project-history/02-lessons.md) · [`03-study-day1-day2.md`](docs/06-project-history/03-study-day1-day2.md) · [`04-sdk-deep-dive.md`](docs/06-project-history/04-sdk-deep-dive.md) |

