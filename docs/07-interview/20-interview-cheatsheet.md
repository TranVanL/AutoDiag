# VDiag Interview Cheatsheet — 1 Page

> **Purpose:** One-page reference to review 5 minutes before an interview. Print it or keep it open on a phone.

---

## 30-Second Pitch

> *"VDiag is a 130-day Android Automotive portfolio cloning the AAOS CarService stack. App → Bound Service → JNI RAII bridge → C++ priority engine → pluggable HAL (Mock/DoIP/CAN/ADAS). Stable AIDL, DeathRecipient resilience, ARM64 QEMU, full testing pyramid. ~210 interview Q&A, 8 boundaries, sanitizers clean."*

---

## 8 Boundaries (memorize order)

| # | Boundary | Key concept |
|---|---|---|
| 1 | App → Service | Binder/AIDL, `DeathRecipient`, `ConcurrentHashMap` |
| 2 | Java → Native | `JNI_OnLoad` cache, RAII `GlobalRef`, `pthread_key` auto-detach |
| 3 | Service → Engine | 4-tier priority queue, `SCHED_FIFO`, PI mutex |
| 4 | Engine → HAL | `IDiagnosticHal` pure virtual, factory swap |
| 5 | Subscription | 100 ms ticker, max-rate, on-change, `DeathRecipient` |
| 6 | System health | `ISystemLifecycle`, CarWatchdog/Handler shim |
| 7 | Bring-up | init.rc, SELinux `.te`, VINTF manifest, privapp-permissions |
| 8 | Embedded | ARM64 cross-compile, QEMU user-mode |

---

## Reflex Answers

- **Why separate process?** → stability + security + memory isolation.
- **Why `oneway`?** → prevent deadlock, isolate slow client.
- **Why pure virtual HAL?** → Open-Closed, swap = 0 engine changes.
- **1MB Binder limit fix?** → `ParcelFileDescriptor` + `ASharedMemory`.
- **SCHED_FIFO no permission?** → EPERM → graceful fallback.
- **GCM footgun?** → IV reuse catastrophic → `setRandomizedEncryptionRequired(true)`.
- **R8 + JNI?** → keep rules for native methods, Parcelable `CREATOR`, Room, AIDL.

---

## Honesty Boundaries (say first)

- Emulator + QEMU only; no real board deployment.
- ADAS HAL is a reusability proof, not production sensor fusion.
- Throughput proof is EventStreamCore, not VDiag.
- ISO 26262: understand concepts, not certified safety engineer.

---

## Numbers

- ~10K LOC
- ~210 interview Q&A
- ~51 tests (42 gtest + 9 Android instrumented + Python integration)
- ASAN/TSAN/UBSan/CheckJNI clean
- ARM64 QEMU green
- Binder pool: 16 threads
- GlobalRef cap: ~51,200
- UDS P2 = 50 ms, P2* = 5 s, S3 = 5 s

---

## Demo Narration (2 min)

1. Open app → dark automotive UI.
2. Tap **Read VIN** → 3 ms, full pipeline.
3. `adb shell ps -A | grep vdiag` → 2 processes.
4. Kill app → service survives, `binderDied` cleanup.
5. Switch HAL to DoIP → Python sim → Wireshark frames.
6. Rotate screen → ViewModel survives.
7. `dumpsys` → queue depth, subscriptions, history.

**Rule:** narrate the *boundary*, not the UI.
