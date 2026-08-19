# HAL Service Deep Dive — AAOS + VDiag Day 71

> **Mục tiêu:** Hiểu sâu cách HAL service hoạt động trong Android Automotive, từ app layer xuống kernel Binder, so sánh emulator stand-in vs production native daemon, và defend được mọi câu hỏi Senior Android Automotive / Framework interview.
>
> **Scope:** Bao gồm Day 71 (`:hal_service` process separation), Day 72 (framework bind HAL), Day 73 (DeathRecipient + exponential backoff), plus production AAOS artifacts (init.rc, SELinux, VINTF, hwservicemanager).

---

## Table of Contents

1. [Tại sao phải tách HAL ra process riêng?](#1-tại-sao-phải-tách-hal-ra-process-riêng)
2. [Topology 3 process trong VDiag](#2-topology-3-process-trong-vdiag)
3. [AndroidManifest.xml — khai báo service trong process riêng](#3-androidmanifestxml--khai-báo-service-trong-process-riêng)
4. [DiagHalService.java — implementation](#4-diaghalservicejava--implementation)
5. [Mirror vendor HAL topology `/vendor/bin/hw/`](#5-mirror-vendor-hal-topology-vendorbinhw)
6. [Framework bind vào HAL — ServiceConnection + linkToDeath](#6-framework-bind-vào-hal--serviceconnection--linktodeath)
7. [DeathRecipient — Kernel mechanism chi tiết](#7-deathrecipient--kernel-mechanism-chi-tiết)
8. [Exponential Backoff Reconnect](#8-exponential-backoff-reconnect)
9. [init.rc Auto-Restart Story](#9-initrc-auto-restart-story)
10. [UI Graceful Degradation](#10-ui-graceful-degradation)
11. [Race Condition: DeathRecipient vs DeadObjectException](#11-race-condition-deathrecipient-vs-deadobjectexception)
12. [AAOS HAL Service Registration — hwservicemanager / ServiceManager](#12-aaos-hal-service-registration--hwservicemanager--servicemanager)
13. [Stable AIDL HAL — @VintfStability, aidl_interface, VINTF manifest](#13-stable-aidl-hal--vintfstability-aidl_interface-vintf-manifest)
14. [SELinux Policy cho HAL](#14-selinux-policy-cho-hal)
15. [So sánh HIDL vs Stable AIDL](#15-so-sánh-hidl-vs-stable-aidl)
16. [Lifecycle đầy đủ: Boot → HAL up → Framework connect → HAL crash → Reconnect](#16-lifecycle-đầy-đủ-boot--hal-up--framework-connect--hal-crash--reconnect)
17. [Common Interview Questions & Answers](#17-common-interview-questions--answers)
18. [Tóm tắt 20 điểm then chốt](#18-tóm-tắt-20-điểm-then-chốt)

---

## 1. Tại sao phải tách HAL ra process riêng?

Trong Android Automotive, **HAL (Hardware Abstraction Layer)** là code giao tiếp với phần cứng / ECU / vehicle bus. Nếu để HAL chung process với app UI hoặc framework service:

| Vấn đề | Hệ quả |
|---|---|
| **HAL crash** | App UI hoặc framework service cũng die → user thấy app force close |
| **Memory leak native** | JNI / C++ engine leak → giết cả app |
| **Security** | App UI có quyền của user; HAL cần quyền `NET_RAW`, `NET_ADMIN`, raw socket → không thể để chung SELinux domain |
| **Boot order** | HAL phải sẵn sàng trước khi framework dùng; cần `class hal` trong `init.rc` |
| **OTA independence** | Vendor HAL update độc lập với system app (Project Treble) |
| **Watchdog / Health** | HAL có thể bị CarWatchdog monitor riêng; crash không ảnh hưởng watchdog của service |

**Câu trả lời interview gốc:**

> *"We run HAL in its own process for **fault isolation**. A crash in the diagnostic engine — malformed UDS frame, native heap corruption, DoIP peer reset — must not cascade to the framework service or the UI. Separate process also gives us separate UID, separate SELinux domain, and independent lifecycle managed by init."*

### Ba lý do chính (nhớ để trả lời nhanh):

1. **Stability / Fault isolation** — HAL crash ≠ framework crash ≠ app crash.
2. **Security / Least privilege** — HAL cần capability riêng (raw socket, CAN, v.v.), không thể để chung với app.
3. **Lifecycle / Boot order** — HAL start sớm (`class hal`), framework/app start sau, cần decouple.

---

## 2. Topology 3 process trong VDiag

Sau Day 71, hệ thống có 3 process:

```bash
adb shell ps -A | grep vdiag
# u0_a123  com.vdiag                   ← App UI (Activity/Fragment)
# u0_a123  com.vdiag:car_service       ← Framework (DiagCarService)
# u0_a123  com.vdiag:hal_service       ← HAL implementation (DiagHalService)
```

### Luồng IPC:

```
┌─────────────────┐      Binder/AIDL       ┌─────────────────────┐      Binder/AIDL       ┌─────────────────┐
│   App Process   │  ←→  IDiagCarService   │  :car_service       │  ←→  IDiagnosticHal   │  :hal_service   │
│  (DiagActivity) │                        │  (DiagCarService)   │                        │ (DiagHalService)│
└─────────────────┘                        └─────────────────────┘                        └─────────────────┘
                                                                                                  │
                                                                                                  ▼
                                                                                         ┌─────────────────┐
                                                                                         │ C++ DiagEngine  │
                                                                                         │  (JNI bridge)   │
                                                                                         └─────────────────┘
```

### Ý nghĩa phân tách:

| Process | Trách nhiệm | Chết thì sao? |
|---|---|---|
| `com.vdiag` | UI, ViewModel, bind tới car_service | App restart, service vẫn sống |
| `:car_service` | Framework logic, permission, subscription, reconnect | UI reconnect, HAL vẫn sống |
| `:hal_service` | Native engine, transport (Mock/DoIP/CAN) | Framework detect death → backoff reconnect |

### Tại sao không phải 2 process mà là 3?

- **2-process** (app + HAL): app crash → HAL vẫn sống, nhưng framework logic (permission, subscription, reconnect) nằm trong app → app chết thì mất hết.
- **3-process**: app chỉ là thin client; framework service sống độc lập, quản lý lifecycle và resilience; HAL sống độc lập, chỉ làm transport/engine.

---

## 3. AndroidManifest.xml — khai báo service trong process riêng

```xml
<service
    android:name=".halservice.DiagHalService"
    android:process=":hal_service"
    android:exported="false">
    <intent-filter>
        <action android:name="com.vdiag.HAL_SERVICE"/>
    </intent-filter>
</service>
```

### Giải thích từng attribute:

- `android:process=":hal_service"`  
  Dấu `:` nghĩa là **private process** của app. Android sẽ tạo process `com.vdiag:hal_service` với cùng UID (`u0_a123`) nhưng **address space riêng biệt**. Crash ở đây không ảnh hưởng process chính.

- `android:exported="false"`  
  Không cho app ngoài bind. Chỉ các component cùng package (hoặc có cùng UID) mới bind được.

- `<intent-filter>` với action `com.vdiag.HAL_SERVICE`  
  Để `DiagCarService` ở `:car_service` có thể `bindService()` bằng explicit intent + action.

### Lưu ý quan trọng:

> Trong production AAOS, HAL không phải Java Service trong APK. Nó là **native daemon** `/vendor/bin/hw/android.hardware.vdiag@1.0-service`, được `init` start từ `init.rc` và register với `hwservicemanager`. Day 71 dùng Java Service làm **emulator stand-in** để demo cùng topology 3-process mà không cần AOSP build.

---

## 4. DiagHalService.java — implementation

```java
public class DiagHalService extends Service {
    private DiagHalStub mStub;

    @Override
    public void onCreate() {
        super.onCreate();
        DiagHalBridge.nativeInit("mock");   // C++ engine init trong HAL process
        mStub = new DiagHalStub();          // implements IDiagnosticHal.Stub
        Log.i(TAG, "DiagHalService onCreate — process=" + Process.myPid());
    }

    @Override
    public IBinder onBind(Intent i) {
        return mStub;                       // trả về Binder cho framework
    }

    @Override
    public void onDestroy() {
        DiagHalBridge.nativeShutdown();     // dọn native resource
    }
}
```

### Điểm then chốt:

1. **`nativeInit("mock")` chạy trong `:hal_service`**  
   JNI library được load vào HAL process, không phải app process. Điều này có nghĩa là C++ engine, heap native, socket DoIP, CAN socket — tất cả đều nằm trong process HAL.

2. **`DiagHalStub extends IDiagnosticHal.Stub`**  
   Đây là server-side AIDL. Framework nhận được `IBinder`, dùng `IDiagnosticHal.Stub.asInterface(binder)` để có proxy.

3. **Lifecycle mapping:**
   - `onCreate()` → init engine
   - `onBind()` → expose binder
   - `onDestroy()` / `onUnbind()` → shutdown engine

### Bound Service lifecycle đầy đủ:

```
onCreate() → onBind() → client bound → ... → onUnbind() → onDestroy()
```

- `onBind()` chỉ gọi **một lần** cho connection đầu tiên.
- `onUnbind()` gọi khi **tất cả** clients đều unbind.
- `BIND_AUTO_CREATE` sẽ tạo service nếu chưa tồn tại.

---

## 5. Mirror vendor HAL topology `/vendor/bin/hw/`

Trên device thật, topology tương đương là:

```bash
# Production
/system/bin/servicemanager          # hoặc hwservicemanager
/vendor/bin/hw/android.hardware.vdiag@1.0-service   # native HAL daemon
```

### File artifacts production:

#### 5.1. init.rc

```rc
# /vendor/etc/init/vdiag.rc
service vdiag_hal /vendor/bin/vdiag_hal_service
    class hal
    user vdiag
    group vdiag system
    capabilities NET_RAW NET_ADMIN
    writepid /dev/cpuset/system-background/tasks
    interface aidl com.vdiag.hal.IDiagnosticHal/default
```

**Key directives:**

- `class hal` → started by `init` during HAL phase (before app process).
- `user vdiag` / `group vdiag` → dedicated UID.
- `capabilities NET_RAW` → cần cho DoIP raw socket.
- `interface aidl` → register stable AIDL HAL với `hwservicemanager`.
- `writepid` → put process in cpuset for scheduling control.

#### 5.2. VINTF manifest

```xml
<!-- /vendor/etc/vintf/manifest.xml -->
<hal format="aidl">
    <name>com.vdiag.hal</name>
    <version>1</version>
    <fqname>IDiagnosticHal/default</fqname>
</hal>
```

#### 5.3. Compatibility matrix

```xml
<!-- /system/etc/vintf/compatibility_matrix.xml -->
<hal format="aidl" optional="false">
    <name>com.vdiag.hal</name>
    <version>1</version>
    <interface>
        <name>IDiagnosticHal</name>
        <instance>default</instance>
    </interface>
</hal>
```

### So sánh emulator vs production:

| Aspect | Emulator (Day 71) | Production AAOS |
|---|---|---|
| Process | `:hal_service` (Java Service) | `/vendor/bin/hw/...` (native daemon) |
| Started by | `bindService()` from `:car_service` | `init.rc` at boot |
| Registered via | Intent action | `hwservicemanager` / `servicemanager` |
| UID | Same as app (`u0_a123`) | Dedicated (`vdiag`) |
| SELinux | Same domain as app | Separate `vdiag_hal` domain |
| JNI | Loaded in `:hal_service` | Native binary, no JNI bridge needed |
| AIDL stability | App-level AIDL | Stable AIDL with `@VintfStability` |

**Câu trả lời interview:**

> *"On a real AAOS device, the HAL is a native daemon in `/vendor/bin/hw/` started by init.rc, registered with hwservicemanager, running under its own UID and SELinux domain. In my emulator-only portfolio, I model the same 3-process topology using a Java Service in `:hal_service`. The resilience logic — DeathRecipient, exponential backoff, fail-fast — is identical."*

---

## 6. Framework bind vào HAL — ServiceConnection + linkToDeath

Trong `DiagCarService` (process `:car_service`):

```java
private IDiagnosticHal mHal;
private final AtomicBoolean mHalUp = new AtomicBoolean(false);

private final ServiceConnection mHalConn = new ServiceConnection() {
    @Override
    public void onServiceConnected(ComponentName n, IBinder b) {
        mHal = IDiagnosticHal.Stub.asInterface(b);
        try {
            b.linkToDeath(mHalDeath, 0);   // ĐĂNG KÝ DEATH RECIPIENT
        } catch (RemoteException e) {
            // HAL already dead
        }
        mHalUp.set(true);
        notifyClientsHalUp();
    }

    @Override
    public void onServiceDisconnected(ComponentName n) {
        mHalUp.set(false);
    }
};

@Override
public void onCreate() {
    super.onCreate();
    Intent halIntent = new Intent("com.vdiag.HAL_SERVICE")
        .setPackage(getPackageName());
    bindService(halIntent, mHalConn, BIND_AUTO_CREATE);
}
```

### Phân biệt 3 callback:

| Callback | Khi nào gọi? | Thread nào? | Ý nghĩa |
|---|---|---|---|
| `onServiceConnected` | Bind thành công | Main thread | Có binder, linkToDeath |
| `onServiceDisconnected` | Service bị kill / crash / unbind | Main thread | Connection broken, cần reconnect |
| `DeathRecipient.binderDied()` | Remote process die | **Binder pool thread** | Kernel notification |

> **Quan trọng:** `onServiceDisconnected` và `binderDied()` có thể cùng fire hoặc chỉ một trong hai, tùy tình huống. Code phải handle cả hai.

### Production tương đương:

```java
// Production: dùng ServiceManager / hwservicemanager
IBinder binder = ServiceManager.waitForService("com.vdiag.hal.IDiagnosticHal/default");
IDiagnosticHal hal = IDiagnosticHal.Stub.asInterface(binder);
binder.linkToDeath(mHalDeath, 0);
```

Hoặc với stable AIDL HAL:

```java
IDiagnosticHal hal = IDiagnosticHal.Stub.asInterface(
    ServiceManager.waitForService("com.vdiag.hal.IDiagnosticHal/default")
);
```

---

## 7. DeathRecipient — Kernel mechanism chi tiết

### 7.1. `linkToDeath` làm gì?

```java
binder.linkToDeath(deathRecipient, 0);
```

Bên dưới:

1. **Binder driver** trong Linux kernel (`drivers/android/binder.c`) track reference count của mỗi `IBinder`.
2. Khi process sở hữu binder die → kernel gọi `binder_deferred_release()`.
3. Kernel iterate tất cả binders owned by dead process.
4. Với mỗi binder có `linkToDeath` → kernel gửi `BR_DEAD_BINDER` transaction đến **process đã đăng ký DeathRecipient** (tức `:car_service`).
5. Binder thread pool của `:car_service` nhận transaction → gọi `JavaDeathRecipient.binderDied()` (native) → dispatch đến Java `DeathRecipient.binderDied()`.

### 7.2. Thread semantics — câu hỏi rất hay

```java
private final IBinder.DeathRecipient mHalDeath = new IBinder.DeathRecipient() {
    @Override
    public void binderDied() {
        // CHẠY TRÊN BINDER POOL THREAD — KHÔNG PHẢI MAIN THREAD
        Log.w(TAG, "[HAL-Death] HAL process died");
        mHalUp.set(false);
        mHandler.post(() -> {
            notifyClientsHalDown();   // dispatch sang service looper
            scheduleReconnect();
        });
    }
};
```

**Phải nhớ:**

- `binderDied()` chạy trên **arbitrary Binder pool thread**, không phải main thread.
- Nó là **asynchronous** so với cái chết của remote process.
- Nếu có nhiều client/process chết cùng lúc → nhiều `binderDied()` có thể chạy **concurrently** → cần thread-safe data structure (`ConcurrentHashMap`, `AtomicBoolean`, `Handler`).
- DeathRecipient là **one-shot per link**. Sau khi fire, phải re-link khi re-bind.
- **Giữ reference** đến DeathRecipient (field), nếu không GC sẽ thu gom và callback không bao giờ fire.

### 7.3. `unlinkToDeath`

Khi reconnect thành công hoặc service shutdown:

```java
binder.unlinkToDeath(mHalDeath, 0);
```

Nếu không unlink → kernel vẫn giữ callback → memory leak + spurious cleanup sau này.

### 7.4. Kernel flow visualization

```
┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
│  :hal_service   │         │  Binder Driver  │         │  :car_service   │
│   process die   │ ──────▶ │  detect death   │ ──────▶ │ BR_DEAD_BINDER  │
└─────────────────┘         │  of IBinder     │         │  transaction    │
                            └─────────────────┘         │  queued         │
                                                        └─────────────────┘
                                                                  │
                                                                  ▼
                                                        ┌─────────────────┐
                                                        │ Binder thread   │
                                                        │ pool            │
                                                        └─────────────────┘
                                                                  │
                                                                  ▼
                                                        ┌─────────────────┐
                                                        │ JavaDeathRecipient│
                                                        │ .binderDied()   │
                                                        └─────────────────┘
                                                                  │
                                                                  ▼
                                                        ┌─────────────────┐
                                                        │ mHalDeath.      │
                                                        │ binderDied()    │
                                                        └─────────────────┘
```

---

## 8. Exponential Backoff Reconnect

```java
private static final long BACKOFF_INITIAL_MS = 1_000;
private static final long BACKOFF_MAX_MS     = 30_000;
private long mBackoffMs = BACKOFF_INITIAL_MS;

private final IBinder.DeathRecipient mHalDeath = new IBinder.DeathRecipient() {
    @Override
    public void binderDied() {
        Log.w(TAG, "[HAL-Death] HAL process died — backoff=" + mBackoffMs + "ms");
        mHalUp.set(false);
        mHandler.post(() -> {
            notifyClientsHalDown();
            scheduleReconnect();
        });
    }
};

private void scheduleReconnect() {
    mHandler.postDelayed(() -> {
        try {
            unbindService(mHalConn);                 // dọn binding cũ
            Intent i = new Intent("com.vdiag.HAL_SERVICE")
                .setPackage(getPackageName());
            bindService(i, mHalConn, BIND_AUTO_CREATE);
            mBackoffMs = BACKOFF_INITIAL_MS;         // reset on success
        } catch (Exception e) {
            mBackoffMs = Math.min(mBackoffMs * 2, BACKOFF_MAX_MS);
            scheduleReconnect();                     // retry với backoff dài hơn
        }
    }, mBackoffMs);
}
```

### Tại sao exponential backoff?

| Lý do | Giải thích |
|---|---|
| **Tránh thundering herd** | Nếu nhiều client cùng reconnect khi HAL up, không flood |
| **Cho init.rc thời gian restart** | Production HAL được init restart; backoff 1s → 2s → 4s đợi init |
| **Không block Binder thread** | Fail-fast + schedule trên Handler, không treo service |
| **Giảm battery / CPU** | Không liên tục poll khi HAL đang down |

### Có nên thêm jitter?

**Có.** Trong production, thêm jitter ngẫu nhiên (ví dụ `backoffMs + random(0, 500)`) để tránh nhiều client reconnect đồng bộ. Trong VDiag plan chưa có jitter nhưng bạn nên đề cập khi interview:

> *"In production I'd add jitter to the backoff to prevent synchronized reconnect storms across multiple clients."*

### Backoff sequence ví dụ:

```
Death #1: wait 1000ms  → reconnect
Death #2: wait 2000ms  → reconnect
Death #3: wait 4000ms  → reconnect
Death #4: wait 8000ms  → reconnect
Death #5: wait 16000ms → reconnect
Death #6+: wait 30000ms (cap)
```

---

## 9. init.rc Auto-Restart Story

Trên device thật, `init` tự restart HAL khi crash:

```rc
service vdiag_hal /vendor/bin/vdiag_hal_service
    class hal
    user vdiag
    group vdiag system
    capabilities NET_RAW NET_ADMIN
    interface aidl com.vdiag.hal.IDiagnosticHal/default
```

- `class hal` → init start service trong HAL phase.
- Khi process die → init tự động restart (trừ khi đạt giới hạn crash).
- Nếu khai báo `critical` → sau 4 lần crash trong 4 phút → device reboot.

### Các `class` trong init.rc:

| Class | Ý nghĩa | Thứ tự boot |
|---|---|---|
| `core` | init, ueventd, v.v. | Đầu tiên |
| `main` | system_server, zygote | Sau core |
| `hal` | Hardware abstraction layer | Trước main |
| `late_start` | Các service khởi động muộn | Sau boot |

### `critical` flag:

```rc
service vdiag_hal /vendor/bin/vdiag_hal_service
    class hal
    critical
```

- Nếu service crash > 4 lần trong 4 phút → device reboot.
- Dùng cho service **thực sự cần thiết** để boot (ví dụ: graphics HAL, storage HAL).
- Với diagnostic HAL thường **không nên** để `critical` vì không cần thiết để boot — để `class hal` thôi.

### Emulator simulation:

```bash
#!/bin/bash
# tools/sim_hal_crash.sh
for i in 1 2 3 4 5; do
    adb shell am stopservice -n com.vdiag/.halservice.DiagHalService
    sleep 3
    adb shell am startservice -n com.vdiag/.halservice.DiagHalService
    sleep 5
done
```

Script này **mô phỏng** init.rc auto-restart bằng cách kill rồi start lại service.

---

## 10. UI Graceful Degradation

`DiagActivity` hiển thị banner:

| Trạng thái | Màu | Ý nghĩa |
|---|---|---|
| `HAL: UP` | 🟢 | Normal |
| `HAL: RECONNECTING (4s)` | 🟡 | Backoff đang đếm ngược |
| `HAL: DOWN` | 🔴 | Backoff max, cần manual recovery |

### Behavior khi HAL chết:

1. `DeathRecipient.binderDied()` fire trong `:car_service`.
2. `:car_service` đánh dấu `mHalUp = false`, notify tất cả client đang subscribe.
3. Client nhận `onErrorEvent(ERR_HAL_DOWN)` thay vì treo.
4. UI chuyển banner amber/red.
5. `:car_service` schedule reconnect với backoff.
6. Khi HAL up lại → banner green, tiếp tục operation.

**Fail-fast thay vì hang:** Mọi request mới trong lúc HAL down đều trả về lỗi ngay, không block Binder thread chờ HAL.

---

## 11. Race Condition: DeathRecipient vs DeadObjectException

Một câu hỏi phổ biến:

> *"Nếu đã có DeathRecipient, tại sao vẫn cần try-catch DeadObjectException?"*

**Trả lời:**

```java
// Race condition:
// 1. Client process die
// 2. Kernel queue binderDied() notification
// 3. NHƯNG poller/service thread đang đồng thời gọi mHal.someMethod()
// 4. DeadObjectException xảy ra TRƯỚC khi binderDied() được xử lý
```

Vì vậy code phải handle **cả hai**:

```java
try {
    mHal.sendRequest(req);
} catch (DeadObjectException e) {
    mHalUp.set(false);
    scheduleReconnect();
}
```

Đây là **defense in depth**: DeathRecipient dọn dẹp, try-catch bắt race condition.

### Timeline của race:

```
t0: HAL process die
t1: Kernel queue BR_DEAD_BINDER cho :car_service
t2: :car_service thread gọi mHal.sendRequest()  ← DeadObjectException!
t3: Binder thread xử lý BR_DEAD_BINDER → binderDied()
```

Nếu chỉ trông chờ `binderDied()` → `sendRequest()` ở t2 sẽ crash.

---

## 12. AAOS HAL Service Registration — hwservicemanager / ServiceManager

### 12.1. ServiceManager vs hwservicemanager

| Component | Dùng cho | API |
|---|---|---|
| `ServiceManager` (legacy) | System services (Java/native) | `addService()`, `getService()`, `waitForService()` |
| `hwservicemanager` | HAL services (HIDL / Stable AIDL) | `registerAsService()`, `getService()`, `waitForService()` |

Trên Android 10+, Stable AIDL HAL có thể register với `hwservicemanager` hoặc `ServiceManager` tùy cấu hình.

### 12.2. HAL register flow (production)

```cpp
// native HAL daemon main.cpp
int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    std::shared_ptr<IDiagnosticHal> hal = ndk::SharedRefBase::make<DiagnosticHal>();
    const std::string instance = std::string(IDiagnosticHal::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(hal->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("Failed to register HAL");
        return 1;
    }
    ABinderProcess_joinThreadPool();
    return 0;
}
```

### 12.3. Framework get HAL

```java
// Java framework
IBinder binder = ServiceManager.waitForService("com.vdiag.hal.IDiagnosticHal/default");
IDiagnosticHal hal = IDiagnosticHal.Stub.asInterface(binder);
```

Hoặc NDK:

```cpp
// C++ framework
std::shared_ptr<IDiagnosticHal> hal = IDiagnosticHal::fromBinder(
    ndk::SpAIBinder(AServiceManager_waitForService("com.vdiag.hal.IDiagnosticHal/default"))
);
```

### 12.4. `waitForService` vs `getService`

| Method | Behavior | Dùng khi nào? |
|---|---|---|
| `getService` | Trả về null nếu chưa có | Khi HAL optional hoặc đã khởi động xong |
| `waitForService` | Block cho đến khi HAL available | Khi boot order cần đảm bảo HAL đã up |

---

## 13. Stable AIDL HAL — @VintfStability, aidl_interface, VINTF manifest

### 13.1. Tại sao cần Stable AIDL?

- **App AIDL**: APK deploy cùng lúc, không cần binary stability.
- **HAL AIDL**: Framework (system partition) và vendor (vendor partition) update độc lập qua OTA. Interface phải **binary stable** qua các version.

### 13.2. `@VintfStability`

```aidl
// IDiagnosticHal.aidl
@VintfStability
interface IDiagnosticHal {
    DiagResponse sendRequest(in DiagRequest request);
}
```

- Đánh dấu interface/type là **VINTF boundary**.
- Bắt buộc stable qua OTA.
- Chỉ cho phép: thêm method/field ở cuối, không xóa, không đổi thứ tự.

### 13.3. `aidl_interface` Soong module

```bp
// Android.bp
aidl_interface {
    name: "com.vdiag.hal",
    vendor: true,
    srcs: ["com/vdiag/hal/*.aidl"],
    stability: "vintf",
    versions: ["1"],
    owner: "vdiag",
}
```

- `vendor: true` → module thuộc vendor partition.
- `stability: "vintf"` → stable AIDL.
- `versions: ["1"]` → frozen version.

### 13.4. `current/` vs `aidl_api/<version>/`

| Directory | Ý nghĩa |
|---|---|
| `current/` | Work-in-progress snapshot, mutable |
| `aidl_api/<name>/<version>/` | Frozen snapshot, immutable, đã release |

### 13.5. VINTF check fail khi nào?

- Framework compatibility matrix yêu cầu HAL version X nhưng vendor manifest chỉ declare version Y.
- Interface FQ name không khớp.
- Boot fail vì Treble không đảm bảo interface hoạt động đúng.

---

## 14. SELinux Policy cho HAL

```sepolicy
# /vendor/etc/selinux/vdiag.te

# Type definitions
type vdiag_hal,         domain;
type vdiag_hal_exec,    exec_type, vendor_file_type, file_type;

# Initial domain transition: init → vdiag_hal
init_daemon_domain(vdiag_hal)

# Allow Binder communication
binder_use(vdiag_hal)
binder_call(vdiag_hal, system_server)
binder_call(vdiag_hal, car_service)

# Allow AIDL HAL registration
hal_server_domain(vdiag_hal, hal_vehicle)

# Network: DoIP needs TCP socket on port 13400
allow vdiag_hal self:tcp_socket create_stream_socket_perms;
allow vdiag_hal port:tcp_socket name_connect;

# Logging
allow vdiag_hal logd:unix_dgram_socket sendto;

# Deny everything else (default)
neverallow vdiag_hal { app_data_file system_data_file }:file *;
```

```
# /vendor/etc/selinux/file_contexts.vdiag
/vendor/bin/vdiag_hal_service    u:object_r:vdiag_hal_exec:s0
```

### Key points:

- Mỗi HAL = 1 SELinux domain riêng (least privilege).
- `init_daemon_domain(vdiag_hal)` = init transitions to `vdiag_hal` khi exec binary.
- `binder_call(A, B)` = allow A → B Binder calls.
- `neverallow` = compile-time check, build fail nếu vi phạm.
- Real device: `audit2allow` từ logcat audit messages → tinh chỉnh policy.

---

## 15. So sánh HIDL vs Stable AIDL

| Feature | HIDL | Stable AIDL |
|---|---|---|
| Ngôn ngữ | Interface definition riêng | AIDL thông thường mở rộng |
| Introduced | Android 8 (Treble) | Android 10+ |
| Google recommendation | Legacy | Preferred |
| Tooling | `hidl-gen` | `aidl` (chung với app/framework) |
| Language | C++, Java | C++, Java, Rust, NDK |
| Stability | Stable | Stable với `@VintfStability` |
| Migration path | → Stable AIDL | Native |

> Google khuyến khích chuyển từ HIDL sang Stable AIDL để đơn giản hóa codebase và tooling.

---

## 16. Lifecycle đầy đủ: Boot → HAL up → Framework connect → HAL crash → Reconnect

### 16.1. Production boot flow

```
[init] start HAL phase
    │
    ▼
[init] exec /vendor/bin/vdiag_hal_service
    │
    ▼
[vdiag_hal_service] main() → AServiceManager_addService()
    │
    ▼
[hwservicemanager] register IDiagnosticHal/default
    │
    ▼
[zygote] start system_server
    │
    ▼
[system_server] start CarService / VDiag framework service
    │
    ▼
[DiagCarService] ServiceManager.waitForService("com.vdiag.hal.IDiagnosticHal/default")
    │
    ▼
[DiagCarService] linkToDeath(mHalDeath, 0)
    │
    ▼
[DiagCarService] mHalUp.set(true) → notify clients
```

### 16.2. HAL crash flow

```
[:hal_service] crash (segfault, exception, killed by LMK)
    │
    ▼
[Binder driver] detect process death
    │
    ▼
[Binder driver] send BR_DEAD_BINDER to :car_service
    │
    ▼
[:car_service] binderDied() on Binder pool thread
    │
    ▼
[:car_service] mHalUp.set(false)
[:car_service] notifyClientsHalDown()  (via Handler)
[:car_service] scheduleReconnect() with exponential backoff
    │
    ▼
[init] detect vdiag_hal died → auto-restart (class hal)
    │
    ▼
[:hal_service] restarted, register with hwservicemanager
    │
    ▼
[:car_service] reconnect succeeds
[:car_service] linkToDeath(new binder)
[:car_service] mHalUp.set(true) → notify clients
[:car_service] reset backoff
```

### 16.3. Emulator crash flow

```
[:hal_service] killed via adb shell am stopservice
    │
    ▼
[:car_service] binderDied() / onServiceDisconnected()
    │
    ▼
[:car_service] schedule reconnect
    │
    ▼
[:car_service] bindService() again
    │
    ▼
[ActivityManager] restart :hal_service
    │
    ▼
[:car_service] onServiceConnected → linkToDeath → resume
```

---

## 17. Common Interview Questions & Answers

### Q1: "What happens when your HAL crashes?"

**A:**

> *"My HAL runs in a separate process — `:hal_service` on emulator, `/vendor/bin/hw/...` on production. When it crashes:*
>
> 1. *The framework service in `:car_service` has registered a `DeathRecipient` on the HAL binder. The kernel Binder driver fires `binderDied()` on a Binder pool thread.*
> 2. *We mark HAL down, notify all clients with `ERR_HAL_DOWN`, and the UI degrades gracefully — banner turns amber/red instead of crashing.*
> 3. *We schedule a reconnect with exponential backoff — 1s, 2s, 4s, ... capped at 30s — to give init.rc time to auto-restart the native HAL daemon.*
> 4. *New requests fail fast; we don't block Binder threads waiting for a dead HAL.*
> 5. *When HAL comes back, we re-link DeathRecipient, reset backoff, and resume normal operation."*

### Q2: "Why not just run HAL in the same process as the service?"

**A:**

> *"Three reasons: fault isolation, security, and lifecycle. A native crash in the diagnostic engine must not kill the framework service. HAL needs raw socket capabilities and a separate SELinux domain. And on production, the HAL daemon must start before zygote apps via init.rc `class hal`."*

### Q3: "How is this different from a regular bound Service?"

**A:**

> *"A regular bound Service often runs in the same process. Here we explicitly use `android:process=":hal_service"` to force a separate process, and we treat the Binder connection as a HAL proxy with `linkToDeath` and reconnect logic — mirroring how AAOS framework services consume vendor HALs through hwservicemanager."*

### Q4: "What thread does `binderDied()` run on?"

**A:**

> *"It runs on an arbitrary Binder pool thread, not the main thread. That's why we immediately post to a Handler to do the actual state update and reconnect scheduling. We also use `ConcurrentHashMap` and `AtomicBoolean` for thread-safe state."*

### Q5: "How do you test this?"

**A:**

> *"On emulator I run `adb shell am stopservice -n com.vdiag/.halservice.DiagHalService` repeatedly and observe logcat for backoff growth: 1s, 2s, 4s. I also have instrumentation tests: single death reconnect, three rapid deaths with backoff growth, backoff cap at 30s, in-flight request returns error, and all subscribers notified."*

### Q6: "Explain `class hal` vs `class main` in init.rc."

**A:**

> *"`class hal` services are started during the HAL boot phase, before `class main` services like zygote and system_server. This ensures hardware abstraction is ready before framework services try to use it. `class main` is for core system services. `class late_start` is for services that don't need to be up immediately."*

### Q7: "What is `critical` in init.rc?"

**A:**

> *"`critical` tells init that this service is essential for boot. If it crashes more than 4 times in 4 minutes, init reboots the device. I would not mark a diagnostic HAL as critical because the device can still boot and drive without diagnostics; critical is for things like graphics or storage HAL."*

### Q8: "Why Stable AIDL instead of regular AIDL for HAL?"

**A:**

> *"Regular AIDL is fine when client and server deploy together in the same APK. HAL sits at the system/vendor boundary, updated independently via OTA. Stable AIDL with `@VintfStability` guarantees binary ABI compatibility across versions, and VINTF manifest enforces that framework and vendor agree on the interface version."*

### Q9: "What is VINTF and why does it matter?"

**A:**

> *"VINTF — Vendor Interface — is the Treble mechanism that makes the system/vendor boundary explicit. The vendor manifest declares what HAL versions it provides. The framework compatibility matrix declares what it requires. At boot, VINTF checks they match; mismatch means boot failure. This ensures an OTA'd system image never tries to call a HAL interface the vendor doesn't implement."*

### Q10: "DeathRecipient vs try-catch DeadObjectException?"

**A:**

> *"They are complementary. DeathRecipient is the kernel notification that a remote process died, but it is asynchronous and may arrive after an in-flight call throws `DeadObjectException`. So we catch `DeadObjectException` to fail fast on the current call, and use DeathRecipient to clean up state and trigger reconnect."*

---

## 18. Tóm tắt 20 điểm then chốt

1. **Process isolation** là lý do chính: HAL crash không giết framework/UI.
2. **`android:process=":hal_service"`** tạo private process riêng.
3. **Emulator stand-in** = Java Service; production = native daemon `/vendor/bin/hw/`.
4. **3 processes**: app UI → `:car_service` → `:hal_service`.
5. **Framework bind HAL** qua `ServiceConnection` + `bindService`.
6. **Production tương đương**: `ServiceManager.waitForService()` / `hwservicemanager`.
7. **`linkToDeath`** đăng ký kernel notification cho HAL death.
8. **`binderDied()`** chạy trên Binder pool thread, async, one-shot.
9. **Exponential backoff** tránh reconnect storm và đợi init.rc restart.
10. **Fail-fast + graceful UI degradation** thay vì treo.
11. **Defense in depth**: DeathRecipient + try-catch `DeadObjectException` + `unlinkToDeath`.
12. **`class hal`** trong init.rc start HAL trước zygote/system_server.
13. **`critical`** flag → 4 crashes in 4 min → device reboot.
14. **Stable AIDL** cần `@VintfStability` cho system/vendor boundary.
15. **`aidl_interface`** Soong module với `vendor: true` + `stability: "vintf"`.
16. **VINTF manifest** (vendor) + **compatibility matrix** (framework) phải khớp.
17. **SELinux**: mỗi HAL 1 domain, least privilege, `init_daemon_domain`, `binder_call`.
18. **HIDL là legacy**, Stable AIDL là preferred trên Android 10+.
19. **Race condition**: `DeadObjectException` có thể xảy ra trước `binderDied()`.
20. **Giữ reference đến DeathRecipient**, nếu không GC sẽ thu gom và không fire.

---

## Appendix A: Code mẫu đầy đủ cho Day 71-73

### A.1. AndroidManifest.xml

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.vdiag">

    <application
        android:name=".VDiagApp"
        android:label="@string/app_name">

        <!-- App UI process (default) -->
        <activity android:name=".DiagActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>

        <!-- Framework service -->
        <service
            android:name=".service.DiagCarService"
            android:process=":car_service"
            android:exported="false">
            <intent-filter>
                <action android:name="com.vdiag.CAR_SERVICE"/>
            </intent-filter>
        </service>

        <!-- HAL service -->
        <service
            android:name=".halservice.DiagHalService"
            android:process=":hal_service"
            android:exported="false">
            <intent-filter>
                <action android:name="com.vdiag.HAL_SERVICE"/>
            </intent-filter>
        </service>

    </application>
</manifest>
```

### A.2. DiagHalService.java

```java
package com.vdiag.halservice;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Process;
import android.util.Log;
import com.vdiag.hal.DiagHalBridge;
import com.vdiag.hal.DiagHalStub;

public class DiagHalService extends Service {
    private static final String TAG = "VDiag.HalService";
    private DiagHalStub mStub;

    @Override
    public void onCreate() {
        super.onCreate();
        DiagHalBridge.nativeInit("mock");
        mStub = new DiagHalStub();
        Log.i(TAG, "DiagHalService onCreate — process=" + Process.myPid());
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "DiagHalService onBind — " + intent);
        return mStub;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        Log.i(TAG, "DiagHalService onUnbind");
        return false;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "DiagHalService onDestroy");
        DiagHalBridge.nativeShutdown();
        super.onDestroy();
    }
}
```

### A.3. DiagCarService.java (resilience)

```java
package com.vdiag.service;

import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.util.Log;
import com.vdiag.hal.IDiagnosticHal;
import java.util.concurrent.atomic.AtomicBoolean;

public class DiagCarService extends Service {
    private static final String TAG = "VDiag.CarService";
    private static final long BACKOFF_INITIAL_MS = 1_000;
    private static final long BACKOFF_MAX_MS = 30_000;

    private IDiagnosticHal mHal;
    private final AtomicBoolean mHalUp = new AtomicBoolean(false);
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private long mBackoffMs = BACKOFF_INITIAL_MS;

    private final ServiceConnection mHalConn = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            Log.i(TAG, "HAL connected");
            mHal = IDiagnosticHal.Stub.asInterface(binder);
            try {
                binder.linkToDeath(mHalDeath, 0);
            } catch (RemoteException e) {
                Log.e(TAG, "linkToDeath failed", e);
            }
            mHalUp.set(true);
            mBackoffMs = BACKOFF_INITIAL_MS;
            notifyClientsHalUp();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.w(TAG, "HAL disconnected");
            mHalUp.set(false);
            notifyClientsHalDown();
            scheduleReconnect();
        }
    };

    private final IBinder.DeathRecipient mHalDeath = new IBinder.DeathRecipient() {
        @Override
        public void binderDied() {
            Log.w(TAG, "[HAL-Death] HAL process died — backoff=" + mBackoffMs + "ms");
            mHalUp.set(false);
            mHandler.post(() -> {
                notifyClientsHalDown();
                scheduleReconnect();
            });
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        bindToHal();
    }

    private void bindToHal() {
        Intent intent = new Intent("com.vdiag.HAL_SERVICE")
            .setPackage(getPackageName());
        bindService(intent, mHalConn, BIND_AUTO_CREATE);
    }

    private void scheduleReconnect() {
        mHandler.removeCallbacksAndMessages(null);
        mHandler.postDelayed(() -> {
            try {
                unbindService(mHalConn);
            } catch (IllegalArgumentException ignored) {
                // not currently bound
            }
            bindToHal();
        }, mBackoffMs);
        mBackoffMs = Math.min(mBackoffMs * 2, BACKOFF_MAX_MS);
    }

    private void notifyClientsHalUp() {
        // broadcast to registered callbacks
    }

    private void notifyClientsHalDown() {
        // broadcast ERR_HAL_DOWN
    }

    @Override
    public IBinder onBind(Intent intent) {
        return new DiagServiceBinder(this);
    }
}
```

### A.4. Test script

```bash
#!/bin/bash
# tools/sim_hal_crash.sh
set -e

for i in 1 2 3 4 5; do
    echo "[round $i] killing HAL..."
    adb shell am stopservice -n com.vdiag/.halservice.DiagHalService
    sleep 3
    echo "[round $i] restarting (simulates init.rc auto-restart)..."
    adb shell am startservice -n com.vdiag/.halservice.DiagHalService
    sleep 5
done

echo "Done. Observe DiagActivity HAL banner cycling + logcat backoff growth."
```

---

## Appendix B: Sách / Tài liệu tham khảo

- Android Source: [What is Android Automotive](https://source.android.com/docs/automotive/start/what_automotive)
- Android Developers: [Bound Services](https://developer.android.com/guide/components/bound-services)
- Android Developers: [AIDL overview](https://developer.android.com/guide/components/aidl)
- AOSP: `drivers/android/binder.c` — Binder driver implementation
- AOSP: `system/core/init/README.md` — init.rc semantics
- AOSP: `system/sepolicy/README` — SELinux for Android

---

*Generated for deep study — VDiag Day 71 HAL Service + AAOS HAL topology.*
