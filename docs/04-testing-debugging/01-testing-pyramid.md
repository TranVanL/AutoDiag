# Testing Pyramid — Strategy, Tools, and Trade-offs

> **Purpose:** Document VDiag's testing strategy across Java and C++, why the pyramid is shaped the way it is, and how each layer catches different classes of bugs.

---

## 1. The pyramid

```
        ▲
       /█\      Espresso (10 tests) — end-to-end UI
      /███\     Robolectric (30 tests) — Android JVM
     /█████\    Mockito (50 tests) — unit with mocked deps
    /███████\   JUnit (100 tests) — pure logic
   /█████████\  gtest (42 tests) — C++ HAL/engine
  /███████████\ ASAN/TSAN/UBSan — runtime correctness
```

The bottom is wide because fast, deterministic tests should catch most bugs. The top is narrow because slow, flaky tests are expensive.

---

## 2. C++ layer — gtest + sanitizers

### What is tested

| Test file | Coverage |
|---|---|
| `test_uds_codec_gtest.cpp` | Encode/decode all 6 UDS services, positive + NRC paths |
| `test_mock_hal_gtest.cpp` | DID lookup, DTC clear, area-aware properties |
| `test_session_state_gtest.cpp` | FSM transitions Idle→Pending→Done/Error |
| `test_diag_engine_gtest.cpp` | Priority queue drain order, worker lifecycle |

### Sanitizers

- **ASan:** heap/stack/global overflow, use-after-free, double-free.
- **UBSan:** signed overflow, misaligned loads, invalid enum values.
- **TSan:** data races, lock-order inversions.
- **CheckJNI:** JNI ref/thread misuse.

ASan and TSan run in separate CI jobs because their instrumentations conflict.

---

## 3. Java layer — JUnit / Mockito / Robolectric / Espresso

### `src/test/` — JVM tests

- **JUnit:** pure logic like request builders, enum validation, DTC severity classifier.
- **Mockito:** `DiagClient` forwarding, `RemoteException` handling, permission gate logic.
- **Robolectric:** ViewModel + LiveData, Room DAO, BroadcastReceiver, WorkManager.

### `src/androidTest/` — instrumented tests

- **Permission tests:** verify signature permission enforcement.
- **Espresso:** critical user journeys (read VIN, read DTC, clear DTC, rotation).

---

## 4. Integration — Python simulators

- `DoIP_Simulator.py` + `testDoIP.py`: full DoIP round-trip against a Python ECU.
- `ecu_can_sim.py`: SocketCAN simulation on `vcan0`.

These close the loop between the Android client and a real transport protocol.

---

## 5. Coverage targets

| Layer | Tool | Target |
|---|---|---|
| Java | Jacoco | 80% line coverage |
| C++ | gcov/lcov | 85% line coverage |

Coverage is a floor, not a goal. A test with no assertion can have 100% coverage and zero value. VDiag pairs coverage with behavioral assertions (byte-exact UDS, NRC paths, state transitions).

---

## 6. CI matrix

| Job | What it runs | Why it matters |
|---|---|---|
| `build-native` | x86-64 gtest + ASan/UBSan | Catches memory and UB bugs |
| `build-tsan` | x86-64 gtest + TSan | Catches races |
| `build-arm64` | aarch64 cross-compile + QEMU | Catches architecture-specific bugs |
| `build-android` | Gradle assembleDebug + lint | APK builds, R8 rules valid |
| `python-simulation` | DoIP simulator + integration tests | End-to-end protocol correctness |

---

## 7. Testability seams

VDiag is testable because every boundary has an interface:

- `IDiagnosticHal` → `MockDiagnosticHal`, `FaultInjectingHal`.
- `ISystemLifecycle` → `FakeSystemClient`.
- `DiagHalBridge` → fake bridge for ViewModel tests.
- `Executor` injection → synchronous direct executor in tests.

---

## 8. Interview talking points

> *"My testing strategy is bottom-heavy: 100+ fast JVM tests, 42 gtest cases, sanitizers, then a small number of Espresso journeys. The C++ layer is verified with ASAN, TSAN, and ARM64 QEMU. Coverage is 80% Java / 85% C++, but I care more about behavioral assertions than the number itself."*
