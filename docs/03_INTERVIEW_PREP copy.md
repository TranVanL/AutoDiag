# 🎤 VDiag — Interview Prep HUB (170+ Q&A, split by phase)

> **Đây là file trung tâm.** Nội dung Q&A chi tiết được tách thành 4 file con theo giai đoạn (v1.0 → v3.0 → behavioral) để dễ ôn theo từng buổi. Mỗi câu có **short answer** (nói 30-60s), **deep dive** (khi bị đào "why/how"), và **trap/follow-up** (câu bẫy thường theo sau). Mục tiêu: defend được ở mức **Senior 5+ năm** cho cả 3 track — Automotive / Framework / Android Common.
>
> Tất cả gắn VDiag với **AAOS CarService stack + modern Android architecture** — đọc to trước phỏng vấn.

---

## 📚 Cấu trúc 4 file con

| File | Giai đoạn | Nội dung | # Q |
|---|---|---|---|
| **[03a_INTERVIEW_FOUNDATION.md](03a_INTERVIEW_FOUNDATION.md)** | v1.0 | Architecture + AAOS · Android system (Binder/Service/Permission/process) · JNI lifecycle · UDS + Automotive | ~42 |
| **[03b_INTERVIEW_FRAMEWORK.md](03b_INTERVIEW_FRAMEWORK.md)** | v2.0 | Subscription · CarWatchdog/Power · Bring-up (SELinux/VINTF/init.rc) · Stable AIDL · HAL resilience · CAN/DoIP/ADAS · Real-time engine · QA/sanitizers · ARM64/QEMU · Observability | ~62 |
| **[03c_INTERVIEW_SENIOR.md](03c_INTERVIEW_SENIOR.md)** | v3.0 | Advanced IPC (ASharedMemory/dumpsys) · Testing pyramid · MVVM/Room/LiveData · Background (WorkManager) · Security (Keystore/pinning/R8) | ~50 |
| **[03d_INTERVIEW_BEHAVIORAL.md](03d_INTERVIEW_BEHAVIORAL.md)** | All | Level defense · System design · Self-driving honesty · STAR stories · Demo scripts · Curveballs · Reverse questions · Red flags | ~40 |

> **Tổng: ~170+ Q&A** trên 30+ section. Gấp >3 lần bản cũ (42 Q). Ôn theo thứ tự A → B → C → D, hoặc theo JD type (xem bảng dưới).

---

## 🗺 Lộ trình ôn theo buổi (7 ngày trước phỏng vấn)

| Ngày | File | Trọng tâm | Self-check |
|---|---|---|---|
| **D-7** | 03a §A1–A2 | Architecture + AAOS mapping + Binder/Service | Whiteboard 8 boundaries từ trí nhớ |
| **D-6** | 03a §A3–A4 | JNI RAII + UDS/DoIP/CAN framing | Giải thích `JniCallbackBridge` không nhìn note |
| **D-5** | 03b §B1–B4 | Subscription + CarWatchdog + Bring-up + Stable AIDL | Đọc init.rc/SELinux snippet và giải nghĩa |
| **D-4** | 03b §B5–B8 | HAL resilience + Real-time + QA + ARM64 | Vẽ backoff reconnect + priority queue |
| **D-3** | 03c §C1–C3 | ASharedMemory + Testing pyramid + MVVM/Room | Giải thích 1MB limit fix + rotation-safe |
| **D-2** | 03c §C4–C5 + 03d §D1 | Background + Security + Level defense | Nói pitch 30s + "3 năm apply 5+" trôi chảy |
| **D-1** | 03d §D2–D8 | System design + STAR + demo + curveballs | Diễn thử demo 2 phút + 3 câu STAR |

---

## 🎯 Multi-JD framing (đọc trước khi vào phỏng vấn)

Identify JD type → chọn opening line đúng (10 giây đầu) → emphasis đúng section.

| JD type | Opening line | Section ưu tiên |
|---|---|---|
| **Senior Android Automotive** | *"I built VDiag — an Android Automotive framework portfolio that clones the AAOS CarService stack end-to-end."* | 03a §A1, 03b §B1–B4 |
| **Android Framework / Platform** | *"VDiag is a framework project — Binder IPC, JNI lifecycle, `@VintfStability` AIDL, multi-process service, bring-up artifacts."* | 03a §A2–A3, 03b §B3–B4 |
| **Android HAL / System Software** | *"VDiag is a HAL portfolio — pure-virtual `IDiagnosticHal`, 4 swappable impls, JNI RAII bridge, ARM64 + QEMU."* | 03a §A3, 03b §B5–B8 |
| **Android Common / App** | *"VDiag's app layer is modern Android — MVVM, Room, WorkManager, Keystore, full testing pyramid, R8."* | 03c toàn bộ |
| **Self-Driving / ADAS C++** | *"My main C++ depth is EventStreamCore — 10M ev/s lock-free. VDiag adds Android system integration + an `IAdasSensorHal` extension."* | 03d §D3, 03b §B5 |
| **Generic Senior Embedded** | *"Two complementary projects: EventStreamCore for data-plane real-time C++, VDiag for control-plane Android Automotive."* | 03a §A4, 03b §B6–B8 |

> **Honesty rule (ADAS/fusion):** *"My ADAS HAL is a reusability proof — same factory pattern extended to a sensor domain, with a complementary IMU filter and a nearest-neighbor radar tracker. It's not production sensor fusion. My deep C++ is in EventStreamCore."* — chi tiết [03d §D3](03d_INTERVIEW_BEHAVIORAL.md).

---

## ⚡ One-page cheat sheet (liếc 5 phút trước khi vào phòng)

**8 boundaries (thuộc lòng thứ tự):**
1. Binder IPC → DeathRecipient + ConcurrentHashMap
2. JNI → RAII GlobalRef + AttachCurrentThread + pthread_key auto-detach
3. Engine queue → 4-tier priority + SCHED_FIFO + PI mutex
4. HAL abstraction → pure virtual + factory (Mock/DoIP/CAN/ADAS)
5. Subscription → single-ticker 100ms + max-rate + on-change + DeathRecipient
6. System health → ISystemLifecycle: CarWatchdog / Handler shim
7. Bring-up → @VintfStability freeze · init.rc · SELinux .te · VINTF manifest
8. Embedded → aarch64 cross-compile + QEMU user-mode, all gtest pass

**Kill-shot numbers:** ~10K LOC · 140+ tests · Jacoco 80% · gcov 85% · ASan/TSan/UBSan/CheckJNI clean · 8 boundaries · 4 HAL impls · P2=50ms/P2\*=5s/S3=5s · Binder 1MB · pool 16 threads · GlobalRef cap ~51,200.

**Reflex answers:**
- *Why separate process?* → stability + security + memory isolation
- *Why oneway?* → prevent deadlock + isolate slow client
- *Why ConcurrentHashMap?* → binderDied on any of 16 Binder threads, concurrent
- *Why pure virtual HAL?* → Open-Closed, swap = 0 engine changes
- *1MB limit fix?* → ParcelFileDescriptor + ASharedMemory (fd crosses, mmap data)
- *SCHED_FIFO no perm?* → EPERM → graceful fallback SCHED_OTHER
- *GCM footgun?* → IV reuse catastrophic → setRandomizedEncryptionRequired(true)
- *R8 + JNI?* → keep rules cho native methods / Parcelable CREATOR / Room / AIDL

**Honesty boundaries (nói chủ động):** chưa deploy real board (emulator + QEMU) · ADAS = reusability proof, không phải production fusion · throughput proof ở EventStreamCore, không ở VDiag · ISO 26262 hiểu concept, chưa certified.

---

## 🎬 Demo pitch 30 giây (memorize)

> *"VDiag is a 130-day Senior Android portfolio covering three role tracks in one project. Below the AIDL boundary it clones the AAOS CarService stack — Bound Service, JNI RAII bridge, C++ engine with a SCHED_FIFO priority-queue worker, a pure-virtual HAL with four swappable transports, `@VintfStability` freeze, ARM64 QEMU CI. Above the AIDL boundary it's a modern Android app — MVVM with ViewModel + Repository + LiveData, Room with migration testing, WorkManager, Android Keystore AES-256-GCM, cert pinning, R8. Full testing pyramid: gtest, JUnit, Mockito, Robolectric, Espresso. 140+ tests; sanitizers clean. All emulator-only — reproducible on any laptop."*

Demo 2-phút / 5-phút chi tiết → [03d §D5](03d_INTERVIEW_BEHAVIORAL.md).

---

## 🔗 Liên kết design docs (khi cần đào sâu 1 module)

| Chủ đề | Q&A | Design doc |
|---|---|---|
| Subscription internals | 03b §B1 | [06b_PROPERTY_SUBSCRIPTION_DEEP.md](06b_PROPERTY_SUBSCRIPTION_DEEP.md) |
| CarWatchdog / Power | 03b §B2 | [07_CARWATCHDOG_POWER.md](07_CARWATCHDOG_POWER.md) |
| Bring-up | 03b §B3 | [05_BRINGUP_NOTES.md](05_BRINGUP_NOTES.md) |
| DoIP protocol | 03a §A4 | [04_MODULE_DOIP.md](04_MODULE_DOIP.md) |
| Real-time engine | 03b §B6 | [09_REALTIME_ENGINE.md](09_REALTIME_ENGINE.md) |
| ADAS HAL | 03d §D3 | [08_ADAS_SENSOR_HAL.md](08_ADAS_SENSOR_HAL.md) |
| Embedded ARM64 | 03b §B8 | [10_EMBEDDED_TARGET.md](10_EMBEDDED_TARGET.md) |
| Advanced IPC | 03c §C1 | [11_ADVANCED_IPC.md](11_ADVANCED_IPC.md) |
| Testing pyramid | 03c §C2 | [12_TESTING_PYRAMID.md](12_TESTING_PYRAMID.md) |
| MVVM + Room | 03c §C3 | [13_DATA_ROOM_ARCH.md](13_DATA_ROOM_ARCH.md) |
| Security | 03c §C5 | [14_SECURITY_LAYER.md](14_SECURITY_LAYER.md) |

---

## ✅ Cách dùng hiệu quả

1. **Đọc to** short answer đến khi thuộc — đừng đọc thầm.
2. **Chỉ bung deep dive khi bị hỏi** — tránh info-dump làm interviewer mất mạch.
3. **Chuẩn bị sẵn trap follow-up** — mỗi câu đều có phần "Trap" để không bị bắt bài.
4. **Luyện nói trade-off** ("why-not X"), không chỉ "what" — đó là tín hiệu senior.
5. **Honesty chủ động** — nói trước ranh giới (emulator, ADAS proof) để build trust, đừng chờ bị bắt.
6. **Demo phải narrate boundary** đang được cross, không phải UI.

> Chúc phỏng vấn tốt. Calibrated honesty > confident bluffing. 🚀
