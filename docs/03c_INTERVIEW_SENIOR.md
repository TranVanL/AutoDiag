# 🎤 VDiag Interview — Part C: Senior Android (v3.0)

> **Scope:** Advanced IPC (ASharedMemory / dumpsys / BinderStats / transaction ring) · Testing pyramid (JUnit / Mockito / Robolectric / Espresso / Jacoco) · MVVM + Room + LiveData + Repository · Background work (WorkManager / BroadcastReceiver / DataStore) · Security (Keystore / NetworkSecurityConfig / StrictMode / R8).
> **Level target:** Senior Android Common — these prove you build apps that are **testable, rotation-safe, observable, and secure**, not just functional.

Index: [Hub](03_INTERVIEW_PREP.md) · [A: Foundation](03a_INTERVIEW_FOUNDATION.md) · [B: Framework](03b_INTERVIEW_FRAMEWORK.md) · **C: Senior** · [D: Behavioral](03d_INTERVIEW_BEHAVIORAL.md)

---

## 🔗 SECTION C1 — Advanced IPC (Q1–Q10)

### Q1. Binder's 1MB limit — how do you actually exceed it?

**Short:** You don't grow the transaction; you pass a **file descriptor** instead of data. `ASharedMemory_create` gives an ashmem fd; the producer `mmap`s it and writes; the fd is wrapped in a `ParcelFileDescriptor` and sent over AIDL (only the fd + a few ints cross Binder); the consumer `mmap`s the *same physical page* → zero-copy read, no size ceiling.

**Deep dive — the wire:** `ParcelFileDescriptor.adoptFd(fd)` transfers ownership into the parcel; the kernel dups the fd into the receiver via the Binder driver's fd-translation. The 500KB DTC snapshot never touches the 1MB transaction buffer. `try-with-resources` on the PFD closes it deterministically; the kernel ref-counts the ashmem region until all fds close.

**Trap — "Who owns/closes the fd?"** → Ownership transfers to the parcel on `adoptFd`; the receiver owns the dup'd fd and must close it (I use try-with-resources). Forgetting to close leaks fds → eventually `EMFILE`.

---

### Q2. ashmem vs `/dev/shm` vs `memfd_create` vs a tmpfile.

| | ashmem (`ASharedMemory`) | `/dev/shm` (POSIX) | `memfd_create` | tmpfile+PFD |
|---|---|---|---|---|
| Android-native | ✓ (API 1) | ✗ blocked by SELinux | ✓ (public API 30+) | ✓ |
| Share via Binder fd | ✓ | filename only | ✓ | ✓ |
| Auto-cleanup on all-close | ✓ | needs `unlink` | ✓ | depends |
| Purgeable pages | ✓ | ✗ | ✗ | ✗ |
| SELinux-friendly | ✓ | ✗ | ✓ | ✓ |

**One-liner:** *"ashmem is Android's own shared-memory primitive, designed for cross-process fd sharing over Binder, supports purgeable pages, works under SELinux out of the box. `memfd_create` is the Linux equivalent but needs API 30+ for the public API."*

---

### Q3. When is ASharedMemory the wrong choice?

For small payloads (< ~100KB) a regular parcelable `List<DtcRecord>` is simpler, type-safe, and fast enough — ashmem adds mmap/lifetime complexity and a manual serialization format. I use a size threshold: small → AIDL list; large/streaming → ashmem. Over-using shared memory reintroduces the manual-memory bugs Binder was designed to avoid.

---

### Q4. dumpsys — how do you wire it and why does it matter?

**Short:** Override `Service.dump(FileDescriptor, PrintWriter, String[])`. `adb shell dumpsys <service>` (or `activity service`) invokes it. VDiag prints six sections: HAL state, engine queue depth/worker liveness, active subscriptions, Binder stats, transaction history (`--history`), and a proto hook (`--proto`). It's how you debug a *running production* service with no debugger attached.

**Deep dive:** Args let you scope output (`--history`, `--proto`) so default dump stays cheap. This is exactly how framework services (`dumpsys activity`, `dumpsys meminfo`) expose internal state — implementing it signals production-operations maturity.

**Trap — "Thread-safety of dump()?"** → It runs on a Binder thread while the service is live, so I read atomics/concurrent structures and snapshot under a short lock — never hold a lock across the whole print (that would stall the service).

---

### Q5. BinderStats — what do you track and why?

Atomic counters: total transactions, in-flight, `maxConcurrent` (via `updateAndGet`), per-method counts, rejects. It reveals **Binder pool saturation** — if `maxConcurrent` approaches 16, callers are queuing and I need to shorten Stub methods or offload to the engine. Each transaction uses a try-with-resources `Handle` that increments on entry, decrements on close (exception-safe).

---

### Q6. Transaction history ring — design and use.

A lock-free 256-slot ring (power-of-two, atomic write index with mask) records `{code, latencyNs, tier, result}` per transaction. On `dumpsys --history` I compute p50/p95/p99 on demand by copying the ring and sorting (bounded 256 → cheap). Lock-free because it's on the hot path; approximate ordering under wrap is acceptable for diagnostics.

**Trap — "Why not a synchronized list?"** → A lock on every transaction adds contention to the hot path and can invert priority. A ring with a single atomic index is wait-free for writers; readers tolerate a torn snapshot because it's diagnostic, not authoritative.

---

### Q7. How does fd translation across Binder actually work?

When a parcel contains an fd (flat_binder_object of type FD), the Binder driver, during copy, `dup`s the fd from the sender's fd table into the receiver's and rewrites the number. So the receiver gets a *different* integer pointing at the same open file description (same ashmem region, shared offset/flags). That's why both sides `mmap` the same physical pages.

---

### Q8. Could you use a `Pipe` (`ParcelFileDescriptor.createPipe`) instead?

Yes for **streaming** producer→consumer (one-directional byte stream, backpressure via pipe buffer). ashmem is better for **random-access/whole-snapshot** (both sides mmap and index). I'd use a pipe for a live log tail, ashmem for a DTC blob. Choosing between them is a senior discriminator.

---

### Q9. Security implications of sharing memory across processes.

The receiver can read everything in the mapped region, so I only share non-sensitive DTC metadata; sensitive fields (technician notes) are encrypted at rest (Keystore, Q30) *before* being placed in shared memory, or excluded. I also set `ASharedMemory_setProt(fd, PROT_READ)` on the consumer side to prevent accidental writes. SELinux constrains which domains can receive the fd.

---

### Q10. How do you test ASharedMemory without two real processes?

An instrumented test creates the fd, writes a known blob, re-mmaps it read-only in the same process, and asserts round-trip parse + that a write attempt on the read-only mapping faults. For cross-process I use a small AIDL test service (Robolectric can't do real Binder, so this is an instrumented/androidTest).

---

## 🧪 SECTION C2 — Testing Pyramid (Q11–Q20)

### Q11. Explain the testing pyramid and why not "just Espresso".

**Short:** Bottom-heavy: ~100 JUnit (pure logic, ~1ms), ~50 Mockito (mocked Android deps, ~5ms), ~30 Robolectric (JVM-shadowed Android, ~50–500ms), ~10 Espresso (real AVD UI, 20–60s). Espresso-only = 30+ min CI, slow feedback, devs stop running it. Pyramid keeps CI fast: 90%+ tests run on the JVM in seconds.

---

### Q12. JUnit layer — what belongs here?

Pure logic with no Android: UDS request builders, response parsers, DTC severity classifier, enum round-trips, DTO validation. Example: `readDid(0xF190)` → `assertArrayEquals({0x22,0xF1,0x90})`; `readDid(0x10000)` → `IllegalArgumentException`. ~100 tests, deterministic, sub-10ms each.

---

### Q13. Mockito — what do you mock and what's the anti-pattern?

Mock the **boundaries**: `IDiagCarService`, `Context`, `IBinder`, `DiagListener`. Verify `DiagClient.getProperty` forwards with the right `DiagRequest` (`argThat`), and that a `RemoteException` from the service is reported to the listener (`doThrow`). Anti-pattern: mocking types you own with complex logic (test the real thing) or over-verifying interactions (brittle). Mock across-process/framework seams, not your own value objects.

**Trap — "Mockito can't mock final/static — how?"** → `mockito-inline` (or the default inline mock maker in recent versions) handles final; for statics use `mockStatic` (Mockito 3.4+) or refactor to inject a seam. I prefer refactoring to a seam over static mocking.

---

### Q14. Robolectric — what is it and when over an instrumented test?

Robolectric runs Android code on the **JVM** using shadow objects (fake `Handler`, `SharedPreferences`, `Looper`) — no emulator, ~50–500ms/test. Use it for Android-dependent logic that doesn't need real rendering: ViewModel + LiveData, Repository with Room (in-memory), BroadcastReceiver dispatch, lifecycle. Use a real instrumented (Espresso) test only when you need real Binder, real GPU rendering, or real hardware behavior.

---

### Q15. How do you test a ViewModel + LiveData?

`InstantTaskExecutorRule` forces LiveData to post synchronously; inject a fake `DiagRepository`; call `viewModel.refreshDtcs()`; assert the `LiveData<UiState>` transitions Idle→Loading→Success and `dtcList()` emits the fake data. No Android UI needed — pure JVM via Robolectric/architecture-testing.

---

### Q16. How do you test Room (migrations especially)?

In-memory DB (`Room.inMemoryDatabaseBuilder`) for DAO tests (fast, isolated). For **migrations**, `MigrationTestHelper` creates the DB at schema v1 (from exported schema JSON), runs `Migration_1_2`, and validates the v2 schema + that data survived. Exported schemas (`room.schemaLocation`) are committed so migrations are verifiable in CI.

**Trap — "Why export schemas?"** → Without the exported JSON, `MigrationTestHelper` has nothing to migrate *from*, and you can't detect an accidental schema change that lacks a migration → runtime `IllegalStateException` on users' devices. Committing schemas makes migrations a compile/test-time concern.

---

### Q17. Espresso — what do you actually assert and how do you avoid flakiness?

Assert the end-to-end UI path: tap "Read DTCs" → `onView(withId(R.id.dtc_list)).check(matches(hasDescendant(withText(...))))`. Flakiness control: `IdlingResource` for async work (so Espresso waits for the engine instead of `sleep`), disable animations on the test device, and keep Espresso to ~10 critical journeys, not exhaustive coverage.

---

### Q18. Jacoco vs gcov — two languages, two tools.

Jacoco instruments JVM bytecode for Java/Kotlin coverage (unit + Robolectric merged report), target ~80%. gcov/lcov covers the C++ HAL/engine, target ~85%. I merge both into the CI dashboard so the whole stack has a coverage story. Coverage is a floor, not a goal — paired with behavioral assertions.

---

### Q19. What's the difference between `test/` and `androidTest/`?

`src/test/` = JVM/host tests (JUnit, Mockito, Robolectric) — no device, run in seconds. `src/androidTest/` = instrumented tests on a device/emulator (Espresso, real Room migration on device, real Binder) — slower, need an AVD. CI runs `test/` on every push, `androidTest/` on a merge/nightly to keep the fast loop fast.

---

### Q20. How do you make async engine callbacks testable?

Inject an `Executor`/`IdlingResource` seam so tests can run callbacks synchronously (direct executor) instead of on a background thread. For native, the JNI layer is behind an interface (`DiagHalBridge` interface + a fake) so ViewModel/Repository tests never touch real JNI. This is why the MVVM refactor (C3) matters — it creates the seams.

---

## 🏛 SECTION C3 — MVVM + Room + Repository (Q21–Q32)

### Q21. Why refactor from direct-Service-call to MVVM?

**Short:** The v1 pattern (Activity → DiagClient → Binder → callback → `runOnUiThread`) breaks on **rotation** (Activity destroyed, callback NPEs), is **untestable** (Activity coupled to Binder), and has **no single source of truth** (is the value from Binder or cache?). MVVM fixes all three: ViewModel survives rotation, Repository is the single source of truth, LiveData drives reactive UI, each layer is mockable.

---

### Q22. How does ViewModel survive rotation and why not just a static?

`ViewModelProvider` scopes the ViewModel to the Activity's `ViewModelStore`, which is retained across configuration changes (the Activity is recreated but the store isn't). A static would leak the Activity/Context and never clear (`onCleared` gives deterministic cleanup). ViewModel must never hold a `View`/Activity `Context` — only `Application` (hence `AndroidViewModel`).

**Trap — "What clears the ViewModel?"** → When the Activity finishes for real (not rotation), `ViewModelStore.clear()` calls `onCleared()`, where I remove the repository observer to avoid a leak. On rotation, `onCleared` is *not* called — that's the whole point.

---

### Q23. LiveData vs StateFlow vs RxJava — why LiveData here?

LiveData is **lifecycle-aware**: it only emits to active observers and auto-unsubscribes on `onDestroy`, preventing the classic "update a dead View" crash. StateFlow (Kotlin coroutines) is more powerful but this is a Java project; RxJava is heavier and not lifecycle-aware without extra glue. LiveData matches the Java + Architecture Components stack and the AAOS-adjacent idiom.

**Trap — "LiveData pitfalls?"** → It only holds the latest value (not a stream of events) → the "SingleLiveEvent"/one-shot problem for navigation/toasts. I use an event wrapper (consumed flag) for one-shot signals like errors, and plain LiveData for state.

---

### Q24. Repository pattern — what's the offline-first flow?

`DiagRepository` returns Room `LiveData` **immediately** (cached DTCs render instantly), then concurrently calls `DiagClient` to refresh; on response it writes to Room, and because the UI observes Room's LiveData, it updates automatically. Single source of truth = the database. Network/Binder is just a way to *update* the database, never queried directly by the UI.

**Deep dive:** All DB writes go through an `IoExecutor` (never main thread). The repository is a process singleton via double-checked locking (`DiagRepositoryProvider`) so all ViewModels share one cache/observer set.

---

### Q25. Why decouple `DtcEntity` (Room) from `DtcRecord` (domain)?

The Entity is a persistence detail (annotations, column types, indices); the domain model is what the UI/business logic uses. Decoupling means a schema change (rename a column, split a table) doesn't ripple into the ViewModel/UI, and domain logic is testable without Room. A mapper converts at the repository boundary.

---

### Q26. Room `@Transaction` — where and why?

`DtcDao.replaceAll()` is a `@Transaction` default method that `deleteAll()` + `insertAll()` atomically — a reader never sees an empty table mid-refresh. Also `@Transaction` on multi-table reads (DTC + notes) for a consistent snapshot. Without it, a concurrent observer could read a half-updated state.

---

### Q27. `@Index` — when and what's the cost?

Index columns used in hot `WHERE`/`ORDER BY` (e.g., `PropertyHistoryEntity` by `timestamp`, DTC by `status`). Cost: slower writes + more storage (the index must be maintained). I index the query-heavy read paths (history charts) and skip indices on write-heavy, rarely-queried columns. Justifying the trade-off is the senior signal, not "index everything".

---

### Q28. Room migration — walk a real v1→v2.

v2 adds a `severity` column. `Migration_1_2`: `database.execSQL("ALTER TABLE dtc ADD COLUMN severity INTEGER NOT NULL DEFAULT 0")`. Register it in `Room.databaseBuilder(...).addMigrations(MIGRATION_1_2)`. `MigrationTestHelper` creates v1, runs the migration, asserts the schema and that existing rows got the default. Never `fallbackToDestructiveMigration()` in production — that wipes user data.

**Trap — "When is destructive migration acceptable?"** → Only for caches you can rebuild (e.g., a purely derived table) or in early dev. For DTC history (user/vehicle data) it's data loss — unacceptable.

---

### Q29. DiffUtil in the RecyclerView adapter — why?

`DiffUtil` computes minimal list changes (`areItemsTheSame`/`areContentsTheSame`) off the main thread (via `AsyncListDiffer`/`ListAdapter`), so only changed rows re-bind and animate — vs `notifyDataSetChanged()` which re-binds everything and kills scroll performance. For a live-updating DTC list at a few Hz this matters.

---

### Q30. TypeConverter to encrypt at rest — how does it tie to Keystore?

`EncryptedStringConverter` `@TypeConverter` calls `KeystoreCrypto.encrypt/decrypt` (AES-256-GCM, HW-backed key) so sensitive columns (technician notes) are ciphertext BLOBs in SQLite, plaintext only in memory. A DB dump on a rooted device yields ciphertext. The key never leaves the Keystore/TEE. Detail in Q30–Q33 below (Security).

---

### Q31. How do you avoid doing DB work on the main thread?

Room throws by default if you run a blocking query on the main thread. Reads return `LiveData` (async) or use `Executor`-backed suspend-equivalents; writes go through `IoExecutor`. StrictMode `detectDiskReads/Writes` with `penaltyDeath` (debug) catches any accidental main-thread I/O in tests/dev.

---

### Q32. What's your package architecture and why?

`ipc/` (Binder/AIDL), `data/` (Repository + `db/` + `model/` + `prefs/`), `domain/` (pure enums + rules, no Android), `ui/` (Activity/ViewModel/Adapter), `service/` (DiagCarService). The dependency rule points inward: `ui → data → domain`, and `domain` depends on nothing Android → it's unit-testable in isolation. This is Clean-Architecture-lite, appropriate for the size (not over-engineered).

---

## 🔄 SECTION C4 — Background Work + System Events (Q33–Q39)

### Q33. WorkManager vs JobScheduler vs AlarmManager vs a Service.

**Short:** WorkManager for **deferrable, guaranteed** background work (survives reboot/process death, honors constraints) — it wraps JobScheduler (API 23+) / AlarmManager under the hood. AlarmManager for **exact-time** wakeups (alarm clock). Foreground Service for **immediate, user-visible ongoing** work. A bare Service is killed under Doze/background limits. VDiag's nightly DTC scan is deferrable + guaranteed → WorkManager.

---

### Q34. Describe the NightlyDtcScanWorker and its constraints.

A `PeriodicWorkRequest` (24h) with `Constraints`: `setRequiredNetworkType(CONNECTED)` (needs cloud), `setRequiresBatteryNotLow(true)` (don't drain). `doWork()` binds the service, reads DTCs, uploads, returns `Result.success/retry/failure`. On `retry`, WorkManager applies backoff. It survives reboot because WorkManager persists jobs in its own DB.

**Trap — "PeriodicWork minimum interval / flex?"** → Minimum period is 15 minutes; there's a flex window at the end of each interval where it may run. For "roughly nightly" that's fine; for exact time I'd use AlarmManager + WorkManager.

---

### Q35. BroadcastReceiver — static vs dynamic, and the Android 8 change.

Android 8 (Oreo) banned most **implicit** broadcasts from static (manifest) receivers to curb wakeups. So: **dynamic** receivers (registered in code, e.g., `ConnectivityReceiver` in `Application`) for implicit broadcasts while the app runs; **static** receivers only for the allowlisted intents (`BOOT_COMPLETED`, etc.). `BootCompletedReceiver` (static, allowlisted) re-schedules WorkManager on reboot.

**Trap — "Why re-schedule on boot if WorkManager persists?"** → WorkManager does persist across reboot on its own; the boot receiver is a belt-and-suspenders / for pre-WorkManager alarms and to re-arm anything not managed by WorkManager. I'm precise that WorkManager alone usually suffices.

---

### Q36. DataStore vs SharedPreferences — why migrate?

DataStore is async (Flow/`RxDataStore`), transactional, and type-safe (Proto DataStore), avoiding SharedPreferences' synchronous disk I/O on the main thread (`commit()`) and its silent `apply()` failures. VDiag uses DataStore (`DiagPrefs`) for user settings (units, refresh rate). SharedPreferences' main-thread `getX` is exactly what StrictMode flags.

---

### Q37. How do you test a Worker?

`WorkManagerTestInitHelper` + `TestListenableWorkerBuilder` builds the worker with test constraints/inputs and runs `doWork()` synchronously; assert `Result.success()` and side effects (upload called, DTCs read). Constraints/backoff are tested via the test driver that lets you simulate constraints being met. No waiting 24h, no real network.

---

### Q38. Doze / App Standby — how do they affect your background work?

In Doze (device idle), network + jobs are deferred to maintenance windows; WorkManager jobs run then (unless `expedited`). App Standby buckets throttle rarely-used apps. Because the nightly scan is deferrable, Doze-deferral is acceptable — I don't fight the battery system. For urgent work I'd use an expedited WorkRequest (with a foreground notification) and justify the battery cost.

---

### Q39. Guaranteed execution — what does WorkManager actually guarantee?

That the work *will run* eventually once constraints are met, surviving app death and reboot — not *when*. It persists the request, retries with backoff on `Result.retry()`, and dedups via unique work names (`enqueueUniquePeriodicWork` with `KEEP`/`REPLACE`). I use a unique name so a re-schedule on boot doesn't create duplicate periodic jobs.

---

## 🔐 SECTION C5 — Security (Q40–Q50)

### Q40. Android Keystore vs an in-process crypto library (BouncyCastle).

**Short:** Keystore keys are **hardware-backed** (TEE/StrongBox) and **non-extractable** — the key material never enters app memory, so a memdump or rooted device can't exfiltrate it; you can only ask the Keystore to encrypt/decrypt. BouncyCastle holds the key as a `byte[]` on the heap → extractable. Rule: any key that must not leave the process = Keystore.

**Deep dive:** I generate an AES-256-GCM key with `KeyGenParameterSpec` (`PURPOSE_ENCRYPT|DECRYPT`, `BLOCK_MODE_GCM`, `ENCRYPTION_PADDING_NONE`, `setRandomizedEncryptionRequired(true)`). `KeyInfo.isInsideSecureHardware()` verifies HW backing (emulator falls back to software but the *code path is identical*).

---

### Q41. Why AES-GCM over AES-CBC?

GCM is **AEAD** — encryption + integrity/authentication in one primitive (the 16-byte tag detects tampering). CBC needs a separate HMAC (encrypt-then-MAC), which is easy to get wrong (MAC-then-encrypt is insecure). GCM is the 2024 default. VDiag wire format: `[iv_len][iv(12B)][ciphertext+tag(16B)]`.

**Trap — "GCM's fatal footgun?"** → **IV/nonce reuse** with the same key is catastrophic — it leaks the XOR of plaintexts and can forge tags. I enforce `setRandomizedEncryptionRequired(true)` and let `Cipher.init(ENCRYPT_MODE)` generate a fresh random IV each time; I never hand-pick a counter without guaranteeing uniqueness.

---

### Q42. StrongBox vs TEE — what's the difference?

TEE (Trusted Execution Environment) = a secure OS on the main CPU (ARM TrustZone). StrongBox = a **separate, dedicated security chip** (Titan M on Pixel, some Samsung) — stronger against physical/side-channel attacks, opt-in via `setIsStrongBoxBacked(true)`. Emulator has neither → software fallback. For diagnostic notes TEE is sufficient; StrongBox is for payment-grade keys.

---

### Q43. Key attestation — what problem does it solve?

`KeyStore.getCertificateChain(alias)` returns a chain rooted at a Google attestation key that proves, to a **remote server**, that the key is HW-backed on a genuine device with a given boot state. Used by Play Integrity/SafetyNet. For VDiag, the OEM cloud could attest the diagnostic key before accepting uploads, preventing a spoofed/rooted tester.

---

### Q44. NetworkSecurityConfig — what do you lock down and why?

`res/xml/network_security_config.xml`: `cleartextTrafficPermitted="false"` (HTTPS only), a `<domain-config>` for the cloud host with **certificate pinning** (`<pin-set>` of SPKI SHA-256 hashes) so a compromised/rogue CA can't MITM, and I exclude debug overrides from release. Declared once, enforced by the platform for all `HttpsURLConnection`/OkHttp traffic — no per-call code.

**Trap — "Cert pinning risk?"** → If the server rotates its cert/key and the pin isn't updated, the app bricks its own network. Mitigation: pin the **intermediate CA** or include a **backup pin** for the next key, and ship pin updates ahead of rotation. Knowing this operational risk is the senior part.

---

### Q45. StrictMode — what and where?

`DiagApplication.enableStrictMode()` in debug: `ThreadPolicy.detectDiskReads/Writes/Network().penaltyDeath()` catches main-thread I/O; `VmPolicy.detectLeakedClosableObjects/detectActivityLeaks/detectLeakedSqlLiteObjects().penaltyLog()`. `penaltyDeath` in debug makes violations impossible to ignore; disabled in release (it's a dev tool, not a runtime guard).

---

### Q46. Permission audit / least privilege — how do you approach it?

Enumerate every manifest permission and justify it; remove any not directly used. Diagnostic permission is signature (OEM-only). No `INTERNET` unless the cloud upload feature is on (feature-gated). Runtime dangerous permissions requested just-in-time with rationale UI. The goal is a manifest a security reviewer can approve line-by-line.

---

### Q47. R8 / ProGuard — what does it do and what must you keep?

R8 shrinks (dead-code/resource removal), optimizes, and obfuscates. **Keep rules** are mandatory for reflection/serialization seams: AIDL Stub/Proxy, JNI-called methods (`@Keep` or `-keepclasseswithmembers native <methods>`), Parcelable `CREATOR`, Room entities/DAOs, and any class referenced by name. Without keeps, R8 renames a JNI target → `NoSuchMethodError` at runtime.

**Trap — "How do you find what R8 broke?"** → Test the **release** build in CI (not just debug), read `mapping.txt` to de-obfuscate crash stacks, and add missing keeps. Shipping an untested R8 build is a classic senior mistake I avoid.

---

### Q48. Where do JNI + R8 + Keystore intersect as a risk?

R8 can rename the Java methods JNI calls by string name → break the bridge; so JNI-target classes/methods need keep rules. Keystore-encrypted Room fields must have their TypeConverter kept too. It's a chain: obfuscation must preserve every *reflective/native* seam. I maintain a curated `proguard-rules.pro` and verify with a release smoke test.

---

### Q49. Threat model for VDiag — what are you actually defending?

At-rest: DTC/notes encrypted (rooted-device dump → ciphertext). In-transit: HTTPS + pinning (MITM). Access: signature permission + per-property permission (a read-only tester can't ClearDTC). Process: separate `:car_service` + SELinux domain. IPC: `enforceCallingPermission` on the Binder thread (caller can't spoof uid). I frame it as at-rest / in-transit / access / process / IPC — a reviewer's mental model.

---

### Q50. If a security reviewer had 30 minutes, what would you show?

The manifest permission justification, the NetworkSecurityConfig (cleartext off + pins), the Keystore key spec (`isInsideSecureHardware` log), the encrypted Room columns, StrictMode config, and the R8 keep rules + a release smoke test. That's the checklist that turns "it runs" into "it's shippable by an OEM."

---

*Continue → [Part D: Behavioral, System Design & Demo](03d_INTERVIEW_BEHAVIORAL.md)*
