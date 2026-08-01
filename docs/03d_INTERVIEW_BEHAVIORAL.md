# 🎤 VDiag Interview — Part D: Behavioral, System Design & Demo

> **Scope:** Behavioral / leadership (STAR) · System design open-ended · Self-driving / ADAS honesty framing · The "5+ years with 3 years experience" defense · Salary / level negotiation · Curveball & deep-dive questions · Demo scripts (30s / 2min / 5min) · Reverse questions (ask the interviewer) · Red-flag answers to avoid.
> **Level target:** Senior. Technical depth is in Parts A–C; this file is how you *land* it — narrative, honesty, and composure under pressure.

Index: [Hub](03_INTERVIEW_PREP.md) · [A: Foundation](03a_INTERVIEW_FOUNDATION.md) · [B: Framework](03b_INTERVIEW_FRAMEWORK.md) · [C: Senior](03c_INTERVIEW_SENIOR.md) · **D: Behavioral**

---

## 🎯 SECTION D1 — The Level Defense (Q1–Q6)

> Câu quan trọng nhất khi apply Senior (5+) với ~3 năm experience. Chuẩn bị kỹ, nói tự tin, không phòng thủ.

### Q1. You have ~3 years, this role wants 5+. Why should we consider you?

> *"My 3 years are production C++ embedded at LG — shipping AUTOSAR on real hardware, with the discipline that comes with safety-relevant code: reviews, coding standards, HiL testing. On top of that, VDiag proves I self-taught the full AAOS stack — Binder, JNI, stable AIDL, HAL, UDS, bring-up — at production-pattern level, not tutorial level. The gap I'm honest about is production AAOS *platform-team* tenure and perception/self-driving domain. But embedded systems, Android HAL, Binder/JNI boundary work, and C++ — I contribute on day one. And I demonstrably learn fast: VDiag is 130 days of deliberate depth."*

**Why it works:** Reframes "years" as "depth + evidence + honesty." Never apologize for the gap; name it, then bound it.

---

### Q2. What's the difference between a mid and a senior engineer, in your view?

> *"A mid engineer makes the feature work. A senior owns the failure modes — what happens on client death, HAL crash, rotation, power-off, a malformed frame, an obfuscated release build. In VDiag every boundary has an explicit resilience story: DeathRecipient cleanup, exponential-backoff HAL reconnect, fail-fast instead of hang, RAII so exceptions don't leak refs. Senior is also about *judgment*: I deliberately did NOT chase microseconds in VDiag because the domain is transport-bound — I proved that skill separately in EventStreamCore. Knowing what *not* to build is senior."*

---

### Q3. How do you handle not knowing something in this interview?

> *"I say what I know, mark the boundary, and reason from fundamentals. For example, if you ask about a HIDL detail I haven't used, I'll say 'I built on stable AIDL; HIDL is the pre-Android-11 HAL IDL with `.hal` files and `@1.0::IFoo` versioning — I'd expect the migration concern to be X.' I'd rather show honest reasoning than bluff. Bluffing is a senior red flag; calibrated confidence isn't."*

---

### Q4. Why are you leaving / why this role?

> *"I want to own the embedded HAL and system-software layer of a production automotive platform — the code between hardware and the app stack. LG gave me C++ safety-critical depth; I built VDiag to bridge into Android Automotive specifically because that's where I want to go deep. VinFast's VF8/VF9 run AAOS — this is the exact stack I've been preparing for."*

---

### Q5. Where do you want to be in 3 years?

> *"Owning a subsystem — a HAL, a diagnostic/telematics stack, or a platform integration layer — at production scale, mentoring on the boundary-engineering discipline (IPC, JNI, resilience) that I care about, while keeping my C++ systems depth sharp. I'm not looking to leave hands-on code; I want more surface area and ownership."*

---

### Q6. What's your biggest weakness (real answer)?

> *"I tend to over-invest in getting a boundary *provably* correct before moving on — e.g., I spent extra days making the JNI RAII bridge handle every exception path. It's the right instinct for boundary code, but I've learned to time-box it for non-critical paths and lean on tests/sanitizers to catch the long tail rather than hand-proving everything. VDiag's 'MUST vs NICE' cut list is literally me managing that tendency."*

**Avoid:** fake weaknesses ("I work too hard"). Give a real one *with a mitigation you already apply.*

---

## 🧩 SECTION D2 — System Design (open-ended) (Q7–Q14)

### Q7. Design a vehicle diagnostic platform from scratch. (whiteboard)

Structure the answer: **clarify → layers → data flow → failure modes → scale → observability.**

1. **Clarify:** How many ECUs? Transports (CAN/DoIP)? On-device only or cloud upload? Read-only or flashing? Real-time constraints?
2. **Layers:** App/SDK → system service (separate process) → HAL abstraction → transport impls. (Draw the AAOS stack.)
3. **Data flow:** typed request → permission gate → engine queue (priority) → UDS codec → transport → response → callback.
4. **Failure modes:** client death (DeathRecipient), HAL death (backoff), timeout (P2/P2*), malformed frame (NRC), power-off (flush).
5. **Scale:** engine-per-ECU, transport arbitration for shared buses.
6. **Observability:** per-request metrics, dumpsys, transaction ring, Perfetto.
7. **Security:** signature + per-property permission, encrypted at rest, pinned in transit.

*"This is literally VDiag — I can walk any layer to code depth."*

---

### Q8. Design the property subscription system for 100 clients × 50 properties.

Single ticker (100ms) driving a `Map<(propId,areaId), List<Subscription>>`; per-property max-rate + on-change dead-band to coalesce; `oneway` dispatch so a slow client can't stall the tick; `DeathRecipient` per listener for cleanup; back-pressure via bounded async buffers (catch `TransactionTooLargeException` per client). 100×50 = 5000 subscriptions but coalesced to at most 10Hz × distinct changed properties — CPU-bound by *changes*, not subscribers. If it grows, shard the ticker by property range across a small thread pool.

---

### Q9. Design OTA-triggered nightly diagnostics with cloud upload.

WorkManager `PeriodicWorkRequest` (constraints: connected + battery-not-low) → bind service → read DTC snapshot (ASharedMemory for bulk) → encrypt sensitive fields (Keystore) → upload over pinned HTTPS → `Result.retry` with backoff on failure → dedup via unique work name → re-arm on boot. Idempotent upload (server dedups by snapshot hash). Doze-tolerant because deferrable.

---

### Q10. How would you add real-time streaming telemetry (not request/response)?

That's a different plane — I'd point to the EventStreamCore design: a lock-free SPSC ring per producer, batched flush, back-pressure via ring-full policy, and a separate transport (not UDS). I'd keep it *out* of the diagnostic engine (which is request/response) and bridge summaries into the diagnostic store. Recognizing it's a different architecture, not a tweak to the engine, is the point.

---

### Q11. A property read is intermittently slow in production. Debug approach.

`dumpsys vdiag --history` → p50/p95/p99 per tier from the transaction ring: is it queue wait (engine saturated) or processing (transport slow)? If wait: check queue depth + Binder pool saturation (BinderStats maxConcurrent). If processing: check HAL round-trip (DoIP retransmits? `0x78` ResponsePending loops?). Perfetto capture to see the cross-thread timeline. Fix targets the *measured* stage, not a guess.

---

### Q12. Design for testability from day one — what do you insist on?

Interfaces at every boundary (HAL, system-lifecycle, JNI bridge, repository) so each is mockable; a fault-injecting HAL decorator; exported Room schemas for migration tests; direct-executor seams so async is synchronous in tests; contract tests shared across HAL impls. The MVVM refactor exists specifically to create these seams. Testability is an architecture property, not an afterthought.

---

### Q13. How do you version and evolve the HAL without breaking vendors?

Stable AIDL `@VintfStability` frozen in `aidl_api/`; add methods at the end (new version), append parcelable fields with defaults, never reorder/remove. Clients feature-gate on `getInterfaceVersion()`. VINTF manifest/matrix guards system↔vendor compatibility at boot. This lets framework and vendor update independently (Treble).

---

### Q14. Multi-ECU, mixed transport (some CAN, some DoIP) — routing design.

`DiagCarService` maps `ecuId → engine`; each engine has its transport HAL. Shared CAN bus → a single `CanDiagnosticHal` arbiter with per-ECU CAN IDs and a transport lock; independent DoIP ECUs → separate sockets, no lock. Logical concurrency (engine-per-ECU) is decoupled from physical arbitration (per-bus). Client API unchanged.

---

## 🚗 SECTION D3 — Self-Driving / ADAS Honesty (Q15–Q20)

### Q15. Do you have ADAS / sensor-fusion experience?

> *"Honestly framed: I built an `IAdasSensorHal` that reuses my diagnostic HAL's factory pattern for a sensor domain — a complementary IMU filter and a nearest-neighbor radar tracker with a track lifecycle. It's a **HAL-reusability proof**, showing my abstraction generalizes beyond diagnostics. It is *not* production sensor fusion — that needs EKF/Kalman and a real noise model, which I don't claim. My deep C++ real-time engineering is EventStreamCore. My value to a self-driving team is the infrastructure layer: HALs, IPC, data pipelines, fault monitoring."*

---

### Q16. Where does diagnostics fit in an autonomous stack?

Three planes — perception, planning, control — plus a **cross-cutting diagnostics/health plane**. VDiag is that health plane: ECU health (UDS/DTC), sensor health (DTC-from-sensor via the HAL), actuator health (brake/steer ECU faults), and monitoring the fusion output's freshness (alert if the occupancy grid goes stale). *"Perception owns the algorithms; I own the infrastructure beneath — hardware talk, fault detection, reporting."*

---

### Q17. What is sensor fusion, conceptually?

Combining multiple sensors (camera high-res/rain-weak, radar depth/rain-ok, LiDAR 3D) into an estimate more accurate than any single one. VDiag doesn't implement the fusion algorithm (ML/estimation domain); it's the layer below — reading sensor health, monitoring update rates, reporting faults. *"I can speak to the systems integration; I won't pretend to own the estimator."*

---

### Q18. Which safety concepts do you actually understand?

From LG AUTOSAR: **watchdog** (stuck-process detection → CarWatchdog analog), **error containment** (process isolation so app crash ≠ service crash), **fail-safe** (HAL error → defined NRC, not a crash), **ASIL** levels (a diagnostic monitor is typically ASIL-B), **ISO 26262** Part 6 software process (I followed it via guidelines/reviews; I'm not a certified safety engineer). I state the certification boundary explicitly.

---

### Q19. First 30 days if you joined the self-driving team?

> *"Weeks 1–2: learn the codebase — what HAL IDL (stable AIDL vs legacy HIDL), the ECU topology and DoIP/CAN config, and the CI/CD pipeline. Weeks 3–4: land a small hands-on task — add a DID, fix a DTC-parsing bug, or raise HAL test coverage — to earn trust through quality, not speed. Day 30: one merged PR and I've reviewed a teammate's code."*

---

### Q20. Why should a C++/self-driving team hire an "Android person"?

> *"Because the boundary between Android and the real-time C++ world is exactly where projects bleed — JNI lifecycle, HAL abstraction, IPC resilience, cross-arch builds. I live on that boundary. EventStreamCore shows I can do the data-plane C++; VDiag shows I can integrate it into an Android/HAL system and deploy it (ARM64/QEMU, bring-up). I'm the glue that makes the C++ core shippable in an Android vehicle."*

---

## 🗣 SECTION D4 — Behavioral / STAR (Q21–Q28)

> Format: **Situation → Task → Action → Result.** Keep to 90 seconds. Có sẵn 1 câu chuyện thật (LG) + VDiag làm ví dụ minh hoạ.

### Q21. Biggest technical challenge you solved. (LG, real)

> *"**S:** At LG we had a deadlock in the AUTOSAR SWC communication layer that only appeared under high CAN-bus load. **T:** Find it without normal logging (which perturbed the timing). **A:** I added lightweight trace timestamps at each IPC boundary — the same boundary-instrumentation mindset I later used for VDiag's 8 boundaries — reproduced the load in the HiL simulator, and found two SWCs acquiring locks in reverse order. **R:** Fixed with a strict lock hierarchy — never lock across boundaries — the same rule I enforce in DiagEngine. Zero recurrence, and I wrote up the lock-ordering guideline for the team."*

---

### Q22. A time you disagreed with a senior/lead.

> *"A lead wanted to add a second worker thread to a request/response path 'for speed.' I disagreed — the transport serializes anyway, and it'd add race surface. I brought a small benchmark showing no throughput gain and a TSAN report showing new race potential. We kept it single-threaded with a priority queue. I disagreed with data, not opinion, and accepted I might be wrong until the numbers came in."*

---

### Q23. A time you made a mistake / caused a bug.

> *"Early in VDiag I stored a JNI local ref beyond the native call — crashed after return. **A:** I used CheckJNI + ASan to diagnose, learned GlobalRef semantics, and built the RAII `JniCallbackBridge` so the whole *class* of bug is impossible, not just that instance. **R:** Zero JNI ref bugs since, CheckJNI clean. **Lesson:** fix the category, not the symptom — boundary code needs systematic resource management."*

---

### Q24. How do you handle code review feedback / give it?

> *"Receiving: I separate taste from correctness — I push back on correctness with evidence, and just take taste feedback. Giving: I anchor on failure modes and readability, not style nitpicks (that's what clang-tidy/format are for), and I always say *why*, with a suggested fix. For boundary code I'm stricter because the cost of a leak/race is high."*

---

### Q25. Tell me about learning something hard, fast.

> *"AAOS. I'd never shipped Android platform code. I read `packages/services/Car/`, mapped every CarService component to a VDiag equivalent, and built it boundary by boundary over 130 evenings — each with a demo and a 'what I learned' note. The structure (MUST vs NICE, weekly tags) is how I keep self-learning from becoming a rabbit hole."*

---

### Q26. A time you had to cut scope under deadline.

> *"VDiag has an explicit cut list. When a phase slipped, I cut Observability/Perfetto first (it's a bonus, the story survives in docs), then downgraded ADAS to 'design-only' — because the *architecture story* still holds without the full implementation. I protect the core (Binder/JNI/Engine/HAL) and ship, rather than delivering nothing polished. Knowing the cut order in advance is the discipline."*

---

### Q27. How do you keep quality high under time pressure?

> *"Non-negotiables that are cheap: sanitizers in CI, CheckJNI, a lint gate, and one demo per milestone. Those catch the expensive bugs automatically so pressure doesn't erode correctness. I trade *scope* under pressure, never *correctness gates*."*

---

### Q28. Describe your ideal team / how you collaborate.

> *"Small, ownership-driven, with strong boundaries between components so people can move fast without stepping on each other — mirrors how I architect systems. I like teams that write things down (VDiag has 15 design docs) so decisions are reviewable and onboarding is fast."*

---

## 🎬 SECTION D5 — Demo Scripts

### 30-second pitch (memorize)

> *"VDiag is a 130-day Senior Android portfolio covering three role tracks in one project. Below the AIDL boundary it clones the AAOS CarService stack — Bound Service, JNI RAII bridge, C++ engine with a SCHED_FIFO priority-queue worker, a pure-virtual HAL with four swappable transports, `@VintfStability` freeze, ARM64 QEMU CI. Above the AIDL boundary it's a modern Android app — MVVM with ViewModel + Repository + LiveData, Room with migration testing, WorkManager, Android Keystore AES-256-GCM, cert pinning, R8. Full testing pyramid: gtest, JUnit, Mockito, Robolectric, Espresso. 140+ tests; ASan/TSan/UBSan/CheckJNI clean. All emulator-only — reproducible on any laptop."*

### 2-minute walkthrough (live demo order)

1. `adb shell ps -A | grep vdiag` → two processes (app + `:car_service`).
2. Tap "Read VIN" → response < 5ms; show logcat 4 tags crossing boundaries.
3. Kill the app process → service survives, `binderDied` cleanup in logcat (no leak).
4. Swap HAL Mock → DoIP (config flag) → same UI, Wireshark shows DoIP frames from the Python ECU sim.
5. Rotate the screen mid-request → ViewModel survives, no crash, UI restores (MVVM).
6. `dumpsys vdiag --history` → HAL state, queue depth, subscriptions, p50/p95/p99.

### 5-minute deep version (add)

7. Show `ctest` (gtest + ASan) green, then the **aarch64 QEMU** job green.
8. Show `aidl_api/IDiagnosticHal/1/` frozen + explain the freeze workflow.
9. Show the Room migration test + Keystore `isInsideSecureHardware` log.
10. Show the CI matrix (x86/ARM/lint/coverage/Java) all green + Jacoco 80% / gcov 85%.

**Rule:** narrate the *boundary* being crossed at each step — that's the story, not the UI.

---

## 🔥 SECTION D6 — Curveballs & Rapid-Fire (Q29–Q40)

> Câu hỏi bẫy / đào sâu bất ngờ. Trả lời ngắn, chính xác.

**Q29. What's the single biggest risk in your architecture?** → The JNI + thread boundary — it's where lifecycle bugs hide. That's why it has the most defensive design (RAII, CheckJNI gate, auto-detach) and the most tests.

**Q30. If you deleted one layer, which and what breaks?** → Delete the HAL abstraction → the engine couples to a transport → I lose swap/test/CI-on-Mock. The abstraction is the cheapest layer with the highest leverage.

**Q31. Why not Kotlin?** → The project targets AAOS/framework parity where Java is still dominant, and it keeps interop with the C++/JNI story clean. I'm comfortable in Kotlin (coroutines/Flow) and would use it for a greenfield app layer; here Java matches the platform idiom.

**Q32. Why not gRPC instead of Binder?** → On-device Android IPC = Binder (kernel-optimized, zero-copy, death notification, the platform standard). gRPC is for network services. Using gRPC on-device would ignore the platform and lose `linkToDeath`/`oneway`.

**Q33. Biggest thing you'd refactor if starting over?** → Introduce the MVVM seams from day one instead of retrofitting in v3 — the early direct-Service-call code cost me test seams I had to add later.

**Q34. How big is the codebase and how do you know it's not AI-slop?** → ~10K LOC, 140+ tests, every boundary sanitizer-clean, and I can whiteboard any layer to line level. The tests + the fact I can defend design *trade-offs* (not just what, but why-not) is the proof.

**Q35. What happens at exactly the 1MB Binder boundary?** → `TransactionTooLargeException`; fix is ParcelFileDescriptor + ashmem so only the fd crosses. The async (`oneway`) buffer is smaller (~half), so flooding oneway callbacks hits it sooner.

**Q36. Can two `oneway` calls arrive out of order?** → From the same thread to the same binder: ordered. Across threads/binders: no global order guarantee.

**Q37. What's the cost of `ConcurrentHashMap` vs the risk it prevents?** → Slightly higher per-op cost and weakly-consistent iteration; prevents `ConcurrentModificationException`/corruption from concurrent `binderDied` on 16 Binder threads. Correct trade for a registry.

**Q38. SCHED_FIFO worker that busy-loops — what breaks?** → It can starve the whole system including the watchdog. So the worker must block on a condvar when idle, never spin. RT priority + blocking wait.

**Q39. GCM IV reuse — consequence?** → Catastrophic: leaks XOR of plaintexts, enables tag forgery. Enforced fresh random IV via `setRandomizedEncryptionRequired(true)`.

**Q40. Your service passes health checks but returns wrong data — how would a watchdog help?** → It wouldn't — liveness ≠ correctness. That's why my health check probes the engine (worker alive, queue not stuck) and why correctness is guarded by contract tests + sanitizers, not the watchdog. Naming this distinction is the senior answer.

---

## ❓ SECTION D7 — Questions to Ask the Interviewer

> Hỏi lại = tín hiệu senior. Chọn 3-4 câu phù hợp với vòng phỏng vấn.

**On the tech:**
- Are your HALs on stable AIDL or still HIDL? Where are you in the migration?
- How is the vehicle-to-cloud diagnostic pipeline structured — on-device buffering, upload cadence?
- What's your ECU topology and transport mix (CAN vs DoIP vs Ethernet backbone)?
- How do you test platform changes — HiL, SIL, emulator, on-vehicle?

**On the team/role:**
- What does the boundary look like between the platform team and the perception/self-driving team?
- What would success look like for this role in the first 6 months?
- How are architecture decisions made and documented?

**On growth:**
- How does the team keep C++ systems depth sharp while working in an Android platform?
- What's the biggest technical debt you'd want this hire to help with?

---

## 🚫 SECTION D8 — Red-Flag Answers to Avoid

| Don't say | Say instead |
|---|---|
| "I built production sensor fusion." | "I built a HAL-reusability proof; production fusion needs EKF/ML I don't claim." |
| "My engine does 10M ev/s." (it doesn't) | "VDiag is transport-bound by design; the throughput proof is EventStreamCore." |
| "I deployed to a real vehicle." | "I documented bring-up from AAOS source; I validated on emulator + ARM64 QEMU." |
| Bluffing a HIDL/Kernel detail | "I haven't used that; here's how I'd reason about it from fundamentals…" |
| "I work too hard" (fake weakness) | A real weakness + the mitigation you already apply. |
| Blaming a past team/manager | Neutral facts + what *you* changed/learned. |
| Over-claiming years/scope | Name the gap, bound it, pivot to evidence. |

> **Golden rule:** Calibrated honesty beats confident bluffing at senior level. Interviewers probe exactly where you over-claim. Every "I don't know / here's the boundary" *builds* trust.

---

*Back to → [Hub / Index](03_INTERVIEW_PREP.md)*
