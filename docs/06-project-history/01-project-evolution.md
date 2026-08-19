# Project Evolution — v1.0 → v2.0 → v3.0

> **Purpose:** Show how VDiag grew iteratively. This is useful for interviews when asked "How did you build this?" or "How do you prioritize?"

---

## v1.0 — Foundation (Days 1–40)

**Goal:** Prove the core AAOS architecture works.

- App + SDK (`DiagClient`)
- Bound Service in `:car_service`
- AIDL `IDiagCarService`
- Permission gate + `ClientRegistry`
- JNI bridge with RAII GlobalRef
- C++ `DiagEngine` with basic queue
- `MockDiagnosticHal`
- UDS codec (6 services)

**Milestone:** Read VIN from mock HAL in < 5 ms.

---

## v2.0 — Framework Depth (Days 41–80)

**Goal:** Add production AAOS patterns.

- Property subscription (`SubscriptionManager`)
- DoIP transport + Python simulator
- CAN/SocketCAN transport
- CarWatchdog + CarPowerManager abstraction
- Stable AIDL + VINTF reference
- init.rc / SELinux / bring-up docs
- ARM64 cross-compile + QEMU
- Real-time engine: priority tiers, SCHED_FIFO, PI mutex

**Milestone:** Swap Mock → DoIP → CAN without changing framework code.

---

## v3.0 — Senior Android (Days 81–130)

**Goal:** Prove modern Android engineering.

- MVVM + Repository + Room + LiveData
- WorkManager background jobs
- Android Keystore AES-256-GCM
- Certificate pinning + NetworkSecurityConfig
- R8 / ProGuard keep rules
- Testing pyramid: JUnit, Mockito, Robolectric, Espresso
- `dumpsys`, transaction history, atrace
- Advanced IPC: `ASharedMemory`, BinderStats

**Milestone:** Release APK with R8, rotation-safe UI, and full CI matrix green.

---

## Cut List (what was deprioritized)

| Feature | Priority | Reason |
|---|---|---|
| Cloud OTA upload | NICE | Out of scope for core architecture proof |
| Full ADAS sensor fusion | NICE | ML/perception domain, kept as HAL reusability proof |
| Perfetto UI integration | NICE | Observability already proven via dumpsys |
| Real hardware bring-up | BLOCKED | No board access; documented instead |

---

## Interview talking points

> *"VDiag evolved in three phases. v1.0 proved the AAOS stack end-to-end. v2.0 added production patterns: subscription, DoIP/CAN, watchdog, bring-up, ARM64. v3.0 added modern Android architecture: MVVM, Room, WorkManager, Keystore, R8, and a full testing pyramid. I maintained an explicit MUST/NICE cut list so scope didn't explode."*
