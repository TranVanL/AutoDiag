

> **Mục tiêu:** Sau 2 buổi này, mày phải có thể trả lời bất kỳ câu nào về AAOS architecture, Bound Service lifecycle, AIDL, Binder — ở mức senior Android system engineer.

---
  
# ═══════════════════════════════════════════════════════
# DAY 1 — AAOS Architecture Deep Dive
# ═══════════════════════════════════════════════════════

## 1. Android Automotive OS (AAOS) là gì — và tại sao khác Android phone?

```
Android Phone Stack:           Android Automotive OS Stack:
──────────────────             ──────────────────────────────
Apps                           Apps (Navigation, Media, Phone)
  ↓                              ↓
Android Framework              Android Framework
  ↓                              ↓ + Car API (android.car.*)
Hardware                       CarService (com.android.car)
                                 ↓
                               Vehicle HAL (IVehicle.aidl)
                                 ↓
                               CAN/LIN/Ethernet ECU
                               (real vehicle hardware)
```

**3 điểm khác biệt cốt lõi:**

| Aspect | Android Phone | AAOS |
|--------|--------------|------|
| Boot time | 20-30s (chấp nhận được) | **< 2s** cho cluster, < 5s cho IVI (safety requirement) |
| Process lifecycle | LMK kill app bất kỳ lúc | CarService = **privileged system service**, không bị kill |
| Hardware access | Camera, mic, GPS | **Vehicle bus** (CAN, LIN, Ethernet), OBD, ECU |

**VinFast dùng AAOS cho:** VF8, VF9 — confirmed bởi Google AAOS partner page. Các system engineer tại đó code chính xác những gì mày đang học.

---

## 2. AAOS Layer Model — 7 layers, nhớ thuộc lòng

```
┌─────────────────────────────────────────────────────────┐
│  Layer 7: OEM App (Navigation, Climate, Diagnostics)    │
├─────────────────────────────────────────────────────────┤
│  Layer 6: Car API (android.car.*)                       │
│           CarDiagnosticManager, CarHvacManager,         │
│           CarSensorManager, CarPropertyManager          │
├─────────────────────────────────────────────────────────┤
│  Layer 5: CarService (com.android.car package)          │
│           - Runs as system_process (persistent)         │
│           - Owns all Car* managers                      │
│           - Bound Service: apps connect via IPC         │
├─────────────────────────────────────────────────────────┤
│  Layer 4: VehicleHal JNI (libvehiclenetwork.so)         │
│           - Java → C++ bridge inside CarService         │
│           - Caches JNIEnv, method IDs in JNI_OnLoad     │
├─────────────────────────────────────────────────────────┤
│  Layer 3: IVehicle.aidl (HAL Interface)                 │
│           - Pure AIDL contract (like pure virtual C++)  │
│           - Versioned: android.hardware.automotive.     │
│             vehicle@2.0 → AIDL HAL (Android 12+)        │
├─────────────────────────────────────────────────────────┤
│  Layer 2: DefaultVehicleHal (C++ implementation)        │
│           - AOSP reference impl                         │
│           - OEM replaces this with real CAN/LIN driver  │
├─────────────────────────────────────────────────────────┤
│  Layer 1: Vehicle Bus (CAN, LIN, Automotive Ethernet)   │
│           - Runs on MCU/ECU (ARM Cortex-M, real-time)   │
└─────────────────────────────────────────────────────────┘
```

**Câu trả lời interview khi bị hỏi "Explain AAOS stack":**
> *"AAOS có 7 layers. App dùng Car API như CarDiagnosticManager — đây là typed Java wrapper. Underneath là CarService chạy persistent trong system_server. CarService dùng JNI để gọi xuống C++ VehicleHal. VehicleHal implement IVehicle.aidl interface — tương tự pure virtual class. OEM (như VinFast) replace DefaultVehicleHal bằng real CAN/LIN driver. Boundary quan trọng nhất là JNI bridge và HAL interface vì đó là nơi Java world gặp C++ world và hardware world."*

---

## 3. Bound Service — Lifecycle chi tiết (PHẢI thuộc)

### 3.1 Bound Service vs Started Service

```
Started Service:                    Bound Service:
────────────────                    ─────────────
startService() →                    bindService() →
  onCreate()                          onCreate()
  onStartCommand()                    onBind() → return IBinder
  [runs indefinitely]                 [lives as long as clients bound]
stopSelf() →                        unbindService() / all clients die →
  onDestroy()                          onUnbind()
                                       onDestroy()
```

**VDiag dùng Bound Service** vì:
- CarService pattern = Bound Service
- App cần reference đến binder để gọi methods
- Service lifecycle tied với client lifecycle (app die → service clean up)

### 3.2 Full Lifecycle Flow (draw this on whiteboard)

```
CLIENT (App process)                    SERVER (DiagCarService process)
─────────────────────                   ────────────────────────────────

bindService(intent,                     ← Android framework routes →
  serviceConn,
  BIND_AUTO_CREATE)
                                        onCreate()
                                          [init DiagHalBridge, DiagEngine]

                                        onBind(intent)
                                          [return DiagServiceBinder]

onServiceConnected(                     ←── IBinder via Binder driver ───
  component, binder)
  stub = IDiagCarService
           .Stub.asInterface(binder)
  [now can call methods]

stub.getProperty(req, cb)              → [Binder thread pool thread]
                                          getProperty(req, cb)
                                          [process, callback]

cb.onResult(id, val, latency)          ← [Binder thread pool thread]
  [oneway — async]

unbindService(serviceConn)
  OR
  process dies →                       onUnbind()
    DeathRecipient.binderDied()
                                       onDestroy()
                                         [shutdown engine, cleanup]
```

### 3.3 BIND_AUTO_CREATE vs BIND_IMPORTANT — senior knowledge

| Flag | Meaning | Khi nào dùng |
|------|---------|-------------|
| `BIND_AUTO_CREATE` | Auto create service nếu chưa tồn tại | Default, CarService pattern |
| `BIND_IMPORTANT` | Elevate service priority = foreground | Safety-critical services |
| `BIND_NOT_FOREGROUND` | Không nâng priority service | Background data sync |
| `BIND_ABOVE_CLIENT` | Service priority > client | Persistent critical service |

**VDiag** dùng `BIND_AUTO_CREATE`. **Real CarService** dùng `BIND_IMPORTANT | BIND_AUTO_CREATE` vì CarService không được bị kill khi app bị LMK.

### 3.4 ServiceConnection callbacks

```java
ServiceConnection conn = new ServiceConnection() {
    @Override
    public void onServiceConnected(ComponentName name, IBinder binder) {
        // Gọi trên MAIN THREAD
        // binder != null guaranteed
        mService = IDiagCarService.Stub.asInterface(binder);
        // Stub.asInterface: same process → Stub trực tiếp
        //                   diff process → Proxy (IPC)
    }

    @Override
    public void onServiceDisconnected(ComponentName name) {
        // Gọi trên MAIN THREAD
        // Service process CRASHED (not normal unbind)
        // mService reference là dangling → set null
        mService = null;
        // Thường rebind ở đây
    }
};
```

**Senior trap:** `onServiceDisconnected` chỉ gọi khi service **crash**, KHÔNG gọi khi client tự `unbindService()`. Nhiều candidate nhầm điểm này.

---

## 4. AIDL Deep Dive — từ file đến IPC

### 4.1 Tại sao AIDL?

```
3 cách IPC trong Android:
1. Intent (startActivity/sendBroadcast): fire-and-forget, no return
2. Messenger: serializes calls, single-threaded Handler queue
3. AIDL: multi-threaded, typed interface, supports complex objects
                ↑ AAOS dùng cái này
```

### 4.2 Từ file .aidl → generated code

```
IDiagCarService.aidl (bạn viết):
───────────────────────────────
package com.vdiag;

interface IDiagCarService {
    void getProperty(in DiagRequest req, IDiagCallback callback);
    void clearDtc();
    String getSoftwareVersion();
}
```

**AIDL tool generates** `IDiagCarService.java` chứa 3 class:

```java
public interface IDiagCarService extends IInterface {

    // ── Binder constants ──────────────────────────────────────────
    static final String DESCRIPTOR = "com.vdiag.IDiagCarService";
    static final int TRANSACTION_getProperty    = IBinder.FIRST_CALL_TRANSACTION + 0;
    static final int TRANSACTION_clearDtc       = IBinder.FIRST_CALL_TRANSACTION + 1;
    static final int TRANSACTION_getSoftwareVersion = IBinder.FIRST_CALL_TRANSACTION + 2;

    // ── Stub (SERVER side) ────────────────────────────────────────
    abstract class Stub extends Binder implements IDiagCarService {

        public static IDiagCarService asInterface(IBinder binder) {
            // KEY: locality optimization
            IInterface existing = binder.queryLocalInterface(DESCRIPTOR);
            if (existing instanceof IDiagCarService) {
                return (IDiagCarService) existing;  // same process, no IPC!
            }
            return new Proxy(binder);  // different process, use Proxy
        }

        @Override
        public boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            switch (code) {
                case TRANSACTION_getProperty:
                    data.enforceInterface(DESCRIPTOR);
                    DiagRequest req = DiagRequest.CREATOR.createFromParcel(data);
                    IDiagCallback cb = IDiagCallback.Stub.asInterface(
                                          data.readStrongBinder());
                    this.getProperty(req, cb);
                    // no reply.writeXxx() because void method
                    return true;
                // ...
            }
        }
    }

    // ── Proxy (CLIENT side) ───────────────────────────────────────
    final class Proxy implements IDiagCarService {
        @Override
        public void getProperty(DiagRequest req, IDiagCallback callback) {
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(DESCRIPTOR);
                req.writeToParcel(data, 0);             // serialize req
                data.writeStrongBinder(callback.asBinder()); // serialize cb
                mRemote.transact(TRANSACTION_getProperty, data, reply, 0);
                // reply.readException() check for remote exceptions
            } finally {
                data.recycle();
                reply.recycle();
            }
        }
    }
}
```

### 4.3 `oneway` — cơ chế thật sự

```
WITHOUT oneway (synchronous):
──────────────────────────────
CLIENT thread:
  transact() ──── [blocks] ──────────────────→ SERVER processes
                                                return reply
  [unblocks] ←── [reply Parcel] ────────────── 
  continue...

WITH oneway (asynchronous):
─────────────────────────────
CLIENT thread:
  transact(flags=FLAG_ONEWAY) ──→ [puts in Binder buffer] → returns immediately
  continue...                                                SERVER processes
                                                             later (no reply)
```

**Tại sao IDiagCallback phải `oneway`:**

```
Scenario without oneway — DEADLOCK:
────────────────────────────────────
App thread A:          Service Binder thread B:
  call getProperty()     receives request
  [BLOCKED waiting]      call callback.onResult()  ← wait for App?
                         [BLOCKED waiting reply]
     ↑ App main thread busy with getProperty → deadlock!

Solution: oneway callback
  App calls getProperty → service processes async
  Service calls callback.onResult() → oneway, no wait
  App receives callback on Binder thread pool, dispatches to main
```

### 4.4 Parcelable — serialization deep dive

```java
// AIDL parcelable:
parcelable DiagRequest;  ← declares, you implement in Java

// Java implementation:
public class DiagRequest implements Parcelable {
    public int requestId;
    public int propertyId;
    public byte[] payload;

    // CREATOR: factory to reconstruct from parcel
    public static final Creator<DiagRequest> CREATOR = new Creator<>() {
        @Override
        public DiagRequest createFromParcel(Parcel in) {
            DiagRequest r = new DiagRequest();
            r.requestId = in.readInt();
            r.propertyId = in.readInt();
            r.payload = in.createByteArray();
            return r;
        }
        @Override
        public DiagRequest[] newArray(int size) { return new DiagRequest[size]; }
    };

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(requestId);
        dest.writeInt(propertyId);
        dest.writeByteArray(payload);
    }

    @Override public int describeContents() { return 0; }
    // describeContents() = 1 nếu chứa FileDescriptor (shared memory)
}
```

**Binder transaction size limit: 1MB per process.** Gửi large data (firmware image) → dùng `ParcelFileDescriptor` (file descriptor passing qua socket, không copy data qua Binder buffer).

---

## 5. Binder Driver — kernel level (senior-only knowledge)

```
/dev/binder — character device in Linux kernel:
─────────────────────────────────────────────────

Process A (Client)          Kernel (/dev/binder)        Process B (Service)
──────────────────          ────────────────────         ──────────────────
transact(code, data)
  ↓
ioctl(BINDER_WRITE_READ) ──→ binder_transaction         
                              [copies data once          
                               into kernel buffer,        
                               maps into B's address      
                               space — ZERO COPY]        ──→ onTransact()
                                                              processes
                             ←── binder_transaction ────←── reply
ioctl returns ←─────────────  [reply mapped to A]
```

**"One copy" trick** — why Binder faster than traditional IPC (pipe, socket needs 2 copies):
- Pipe: A writes to kernel buffer (1 copy), B reads from kernel buffer (2nd copy)  
- Binder: kernel maps memory directly into B's address space — **1 copy total**

**Binder identity = IBinder token:**
- Unique object per process
- `IBinder.linkToDeath()` — kernel notifies when remote process dies
- That's how `DeathRecipient` works at kernel level

---

## 6. DeathRecipient — PHẢI thuộc chi tiết này

```java
// Pattern: register khi bind, cleanup khi die
private final Map<IBinder, ClientEntry> mClients = new ConcurrentHashMap<>();

void register(IDiagCallback callback) {
    IBinder token = callback.asBinder();

    IBinder.DeathRecipient dr = () -> {
        // Được gọi trên Binder thread pool (bất kỳ thread nào)
        // → ConcurrentHashMap để thread-safe
        mClients.remove(token);
        Log.w(TAG, "☠ Client died — auto-removed from registry");
    };

    try {
        token.linkToDeath(dr, 0);
        mClients.put(token, new ClientEntry(callback, dr));
    } catch (RemoteException e) {
        // client đã chết trước khi link → ignore, don't add
    }
}

void unregister(IDiagCallback callback) {
    IBinder token = callback.asBinder();
    ClientEntry entry = mClients.remove(token);
    if (entry != null) {
        // unlinkToDeath để tránh callback sau khi unregister
        token.unlinkToDeath(entry.deathRecipient, 0);
    }
}
```

**Tại sao `ConcurrentHashMap` chứ không phải `HashMap` + `synchronized`?**

```
Scenario:
- Binder thread 1: binderDied() gọi mClients.remove()
- Binder thread 2: register() gọi mClients.put()
- Main thread: getAllClients() iterate

HashMap + synchronized: toàn bộ critical section lock → latency
ConcurrentHashMap: segment-level lock → 
  - remove + put có thể concurrent (different segments)  
  - iterator weak-consistency (không throw ConcurrentModificationException)
  → AAOS CarService dùng pattern này
```

---

## 7. Process & Security (IPC security model)

### 7.1 Signature Permission

```xml
<!-- Trong AndroidManifest.xml của SERVICE app -->
<permission
    android:name="com.vdiag.permission.DIAGNOSE"
    android:protectionLevel="signature"/>
    <!-- signature: chỉ app cùng signing key mới được grant -->
    <!-- signatureOrSystem: system apps cũng được -->
    <!-- normal: user-granted runtime -->
    <!-- dangerous: user sees dialog -->
```

**Tại sao `signature` cho diagnostic?**
- `normal` → bất kỳ app nào install được grant → malware đọc được DTC
- `dangerous` → user có thể grant cho app random → social engineering attack
- `signature` → chỉ app ký cùng key (= tức là chính OEM build) → production security

### 7.2 Enforce trong Stub

```java
@Override
public void getProperty(DiagRequest req, IDiagCallback cb) {
    // CHECK 1: permission (signature-level)
    mContext.enforceCallingOrSelfPermission(
        "com.vdiag.permission.DIAGNOSE",
        "Caller lacks DIAGNOSE permission"
    );

    // CHECK 2: calling UID (extra paranoia)
    int callingUid = Binder.getCallingUid();
    // Binder.getCallingUid() returns actual UID of calling process
    // (Binder driver fills this in kernel, cannot be spoofed)

    // CHECK 3: rate limiting (DoS prevention)
    if (mRateLimiter.tryAcquire(callingUid)) {
        processRequest(req, cb);
    } else {
        throw new SecurityException("Rate limit exceeded");
    }
}
```

---

## 8. AAOS Source Code Navigation (khi interviewer hỏi "have you read source?")

```
AOSP paths mày phải biết:
─────────────────────────

packages/services/Car/          ← CarService source
  car-lib/                        ← Car API (CarDiagnosticManager, etc)
  service/                        ← com.android.car package
    CarService.java                 ← main service entry
    CarDiagnosticService.java       ← diagnostic subsystem
    hal/                            ← VehicleHal JNI bridge

hardware/interfaces/automotive/vehicle/
  aidl/                           ← IVehicle.aidl (Android 12+)
  2.0/                            ← HIDL version (Android 10-11)

frameworks/opt/car/services/     ← additional car services

system/libhidl/                  ← HIDL runtime (pre-AIDL HAL)

Sample adb commands mày dùng khi debug real AAOS:
  adb shell dumpsys car_service                ← dump CarService state
  adb shell dumpsys activity services com.android.car
  adb shell cmd car_service help
  adb logcat -s CarService:V VehicleHal:V
```

---

## 9. Câu hỏi senior interview về Bound Service + AIDL (với đáp án)

### Q: "What happens if the client calls an AIDL method after service crashes?"

> Proxy.transact() → Binder driver discovers remote is dead → throws `DeadObjectException` (extends `RemoteException`). Client code phải catch `RemoteException`. `onServiceDisconnected()` cũng được gọi trên main thread. Binder handle in Proxy becomes invalid.

### Q: "How does Binder handle backpressure?"

> Binder thread pool = 16 threads max. If all busy → new incoming transactions block at kernel level. Client's transact() blocks. This is **natural backpressure** — no explicit flow control needed. For `oneway`: transactions queue in Binder buffer (limited, ~1MB) → if full → `TransactionTooLargeException`. Solution for high-throughput: split into smaller transactions or use shared memory via `ParcelFileDescriptor`.

### Q: "Can AIDL interface be called from multiple threads?"

> Yes, Stub methods are always called on Binder thread pool (multiple threads). You MUST make your Stub implementation thread-safe. AIDL compiler does NOT add any synchronization. Common mistake: using unsynchronized `ArrayList` for client registry.

### Q: "What's the difference between IBinder and IInterface?"

> `IBinder` = low-level kernel token (opaque handle, can `linkToDeath`, can `transact` raw bytes). `IInterface` = typed interface (the generated AIDL interface). `Stub` extends both → it IS an `IBinder` (can be sent across process) AND an `IInterface` (has typed methods). `Stub.asInterface()` converts between IBinder → IInterface safely.

### Q: "Why does AIDL use DESCRIPTOR string?"

> During `asInterface()`, Proxy calls `queryLocalInterface(DESCRIPTOR)` on the IBinder. If same process, Stub registered itself with this descriptor → returns Stub directly (no IPC). If different process, no registration found → creates Proxy. The DESCRIPTOR is the fully-qualified interface name = globally unique. Also used in `enforceInterface()` to prevent confused deputy attacks.

---

## 10. Diagram để vẽ trên whiteboard (practice this)

```
                    ┌─────── /dev/binder ────────┐
                    │                             │
Process A           │           Kernel            │     Process B
(com.vdiag)         │                             │  (com.vdiag:car_service)
                    │                             │
DiagActivity        │                             │   DiagCarService
  ↓                 │                             │
DiagClient          │                             │   DiagServiceBinder
  ↓                 │      ── transaction ──→     │     .getProperty()
IDiagCarService     │                             │       ↓
  .Proxy            │                             │   [Binder thread pool]
  .getProperty() ───┼──→ binder_transaction  ─────┼──→ onTransact()
                    │      (1 copy)                │       ↓
IDiagCallback       │ ←── oneway callback ─────── │   IDiagCallback.Proxy
  .Stub             │                             │     .onResult()
  .onResult() ←─────┼──────────────────────────── │
[Binder thread]     │                             │
dispatch to main    │                             │
thread              │                             │
                    └─────────────────────────────┘
```
