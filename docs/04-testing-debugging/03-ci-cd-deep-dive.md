# CI/CD Deep Dive — `.github/workflows/ci.yml`

> **Purpose:** Explain every job in the VDiag CI pipeline, why it exists, and what a red job tells you.
>
> **File:** [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)

---

## 1. Trigger strategy

```yaml
on:
  push:
    branches: ["main", "dev"]
    paths:
      - 'android/**'
      - 'hal/**'
      - 'scripts/**'
      - '.github/workflows/ci.yml'
  pull_request:
    branches: ["main"]
```

- CI runs on every push to `main` or `dev` **only when relevant paths change**. This avoids wasting runner minutes on documentation-only commits.
- It also runs on every pull request targeting `main`, giving reviewers a green/red signal before merge.

---

## 2. Job overview

| Job | What it validates | Why it matters |
|---|---|---|
| `build-native` | C++ HAL/engine on x86-64 with `None`, `Address`, and `Thread` sanitizers | Catches memory bugs, races, and UB in native code |
| `build-arm64` | Cross-compile to ARM64 and run tests under QEMU | Catches architecture-specific issues without hardware |
| `build-android` | Gradle `assembleDebug` + APK verification | Confirms the Android app builds and R8/lint rules are valid |
| `python-simulation` | Python DoIP simulator + `testDoIP.py` | End-to-end protocol correctness |
| `aidl-compat-check` | `scripts/freeze_aidl.sh --diff current 1` | Prevents accidental AIDL ABI breaks |
| `ci-status` | Aggregates results | Fails the workflow if any critical job fails |

---

## 3. `build-native` — C++ with sanitizers

```yaml
build-native:
  name: Build Native + ctest + (ASAN / TSAN)
  strategy:
    matrix:
      sanitizer: [None, Address, Thread]
```

This job builds the standalone `hal/` CMake project three times, each with a different sanitizer flag:

- **None:** plain debug build. Fastest sanity check that the code compiles and tests pass without instrumentation.
- **Address:** `-fsanitize=address`. Detects heap/stack/global buffer overflows, use-after-free, double-free, and memory leaks.
- **Thread:** `-fsanitize=thread`. Detects data races and lock-order inversions.

### Why ASan and TSan run separately

AddressSanitizer and ThreadSanitizer use incompatible runtime instrumentation. Running them in the same binary would produce false positives or crash the runtime. The standard practice is two separate CI jobs.

### Caching

```yaml
uses: actions/cache@v4
with:
  path: hal/build
  key: cmake-${{ matrix.sanitizer }}-${{ hashFiles('hal/CMakeLists.txt', 'hal/src/**', 'hal/include/**') }}
```

The CMake build directory is cached per sanitizer. The cache key includes the CMake file and source headers so the cache is invalidated when native code changes.

### TSan options

```yaml
env:
  TSAN_OPTIONS: "halt_on_error=1:second_deadlock_stack=1:history_size=7"
```

- `halt_on_error=1` — fail the test immediately on the first race.
- `second_deadlock_stack=1` — print both lock acquisition stacks for deadlock reports.
- `history_size=7` — keep enough history to report the racing access stack.

### Failure artifacts

If `ctest` fails, the job uploads `LastTest.log` so you can inspect the exact failing assertion and sanitizer stack trace.

---

## 4. `build-arm64` — cross-compile + QEMU

```yaml
build-arm64:
  name: Build ARM64 with toolchain-aarch64
```

This job installs the `gcc-aarch64-linux-gnu` cross toolchain and QEMU user-mode, then builds `hal/` with the ARM64 toolchain file and runs `ctest` through `qemu-aarch64-static`.

### Why this job exists

x86-64 and ARM64 differ in ways that unit tests on the host will not catch:

- **Alignment faults:** `*(uint32_t*)(buf+1)` works on x86 but raises `SIGBUS` on ARM.
- **Calling conventions:** ARM64 has different register usage and stack alignment rules.
- **Type-size assumptions:** `sizeof(long)` and pointer sizes can differ on other architectures.

QEMU user-mode translates ARM instructions to the host kernel syscalls. It is fast enough for gtest and proves the ARM binary is functional without a physical board.

### CMake cross-compile emulator

```yaml
-DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"
```

This tells CTest to run each test binary through QEMU with the ARM sysroot for shared libraries.

---

## 5. `build-android` — APK build

```yaml
build-android:
  name: Android APK Build (assembleDebug)
```

Steps:

1. Check out the repo.
2. Set up Java 17 (Temurin).
3. Set up the Android SDK.
4. Cache Gradle packages and the Android NDK.
5. Run `./gradlew assembleDebug --stacktrace -i`.
6. Verify `app-debug.apk` exists.
7. Upload the APK as a workflow artifact.

### Why this job is critical

The native HAL can build perfectly on Linux while the Android Gradle build fails because of:

- AIDL generation mismatches,
- NDK CMake integration issues,
- Missing `keep` rules that only surface in a release build,
- Manifest merge conflicts.

Building the APK in CI catches Android-specific integration problems early.

### Caching

Gradle caches and the NDK are cached by hash of Gradle files and NDK version. This keeps the job fast on repeated runs.

---

## 6. `python-simulation` — DoIP integration

```yaml
python-simulation:
  name: Python Simulation Tests
```

Steps:

1. Check out the repo.
2. Set up Python 3.10.
3. Install dependencies from `python_simulator/requirements.txt` if present.
4. Start `DoIP_Simulator.py` in the background.
5. Wait 2 seconds for the server to bind.
6. Run `testDoIP.py`.
7. Kill the server and exit with the test result code.

### What it proves

This job closes the loop between the C++ HAL design and a real transport protocol:

- DoIP framing is correct.
- Routing activation is handled.
- UDS ReadDID, ReadDTC, ClearDTC, and TesterPresent round-trip correctly.
- The Python simulator is not just documentation — it is exercised in CI.

### Failure artifacts

If the integration test fails, any `*.log` files in `python_simulator/` are uploaded for inspection.

---

## 7. `aidl-compat-check` — stable AIDL guard

```yaml
aidl-compat-check:
  name: AIDL backward-compat diff check
  run: bash scripts/freeze_aidl.sh --diff current 1
```

`scripts/freeze_aidl.sh` compares the current AIDL sources against the frozen `aidl_api/com.vdiag.hal/1/` snapshot.

### Why this matters

`@VintfStability` HAL interfaces must remain binary-compatible across OTA updates. This job fails if you:

- Remove a method,
- Change a method signature,
- Reorder parcelable fields,
- Insert a field in the middle of a parcelable.

Allowed changes are additive: append a new method or append a new parcelable field with a sensible default.

---

## 8. `ci-status` — aggregate gate

```yaml
ci-status:
  needs: [build-native, build-android, python-simulation, aidl-compat-check]
  if: always()
```

This job runs even if some needed jobs fail (`if: always()`). It prints the result of each job and fails the workflow if any of the following are red:

- `build-native`
- `build-android`
- `aidl-compat-check`

`python-simulation` is treated as non-critical in the final gate: a failure is reported with a warning, but the workflow still prints `✅ CI PASSED`. In practice you should treat it as blocking; the distinction exists to let the matrix finish collecting signal.

---

## 9. What each red job means

| Red job | Likely cause |
|---|---|
| `build-native / None` | Compile error or failing gtest in plain debug mode. |
| `build-native / Address` | Memory bug: overflow, use-after-free, leak. Read the ASan stack trace. |
| `build-native / Thread` | Data race or lock-order inversion. Read the TSan report. |
| `build-arm64` | Cross-compile error or architecture-specific runtime failure under QEMU. |
| `build-android` | Gradle/AIDL/NDK build failure or missing APK output. |
| `python-simulation` | DoIP framing bug, routing activation issue, or simulator/test mismatch. |
| `aidl-compat-check` | Breaking change to a frozen stable AIDL interface. |

---

## 10. Local equivalents

You can reproduce every CI job locally:

```bash
# build-native / None
cd hal && mkdir -p build && cd build
cmake .. && make -j$(nproc) && ctest --output-on-failure

# build-native / Address
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address" && make -j$(nproc) && ctest

# build-native / Thread
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=thread" && make -j$(nproc) && ctest

# build-arm64
cd .. && mkdir -p build_arm64 && cd build_arm64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
         -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"
make -j$(nproc) && ctest --output-on-failure

# build-android
cd ../../android && ./gradlew assembleDebug

# python-simulation
cd ../python_simulator
python3 DoIP_Simulator.py &
sleep 2
python3 testDoIP.py
kill %1

# aidl-compat-check
cd ..
bash scripts/freeze_aidl.sh --diff current 1
```

---

## 11. Interview talking points

> *"VDiag's CI is designed to catch bugs at the boundary. Native code is tested with ASan and TSan in separate jobs because their instrumentations conflict. ARM64 cross-compile plus QEMU catches alignment and calling-convention issues without a board. The Android APK build validates the full Gradle/AIDL/NDK integration. A Python DoIP simulator runs end-to-end protocol tests. Finally, an AIDL backward-compat check guards the stable HAL contract. The aggregate job fails the workflow if native, Android, or AIDL compatibility breaks."*
