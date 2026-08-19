# 🔬 Deep Dive: Push/Subscribe Model — VDiag Day 51

> Tài liệu này giải thích chi tiết từng layer của subscription model, đủ để defend mọi câu hỏi kĩ thuật trong interview.

---

📌 Quy trình chi tiết
Client process: gọi BpBinder::transact(), đóng gói dữ liệu vào Parcel, rồi ioctl(BINDER_WRITE_READ) để gửi xuống binder driver.

Binder driver: nhận dữ liệu, copy một lần từ user space của client vào transaction buffer trong kernel.

Enqueue transaction: driver đưa transaction vào hàng đợi của server process.

Wake up server thread: một thread trong Binder thread pool của server được đánh thức.

Server process: thread đọc dữ liệu từ transaction buffer (đã được map vào không gian nhớ của server), unmarshall Parcel, gọi BnBinder::onTransact().

Kết quả: server đóng gói response vào Parcel, gửi ngược lại qua driver, driver copy một lần vào buffer của client, và client đọc ra.

## LAYER 0 — Binder IPC: Cơ chế thật sự bên dưới

### Android Binder là gì ở kernel level

```
Process A (Client)          Kernel Space             Process B (Service)
─────────────────           ────────────             ────────────────────
IDiagCarService proxy                                DiagServiceBinder.Stub
    │                                                        │
    │ write(fd, parcel_data)                                 │
    │──────────────────────→ /dev/binder ──────────────────→ │
    │                         ioctl                          │
    │                         BINDER_WRITE_READ              │
    │                         kernel copies parcel           │
    │                         wakes up server thread         │
    │ block (for sync call)                                  │ onTransact() called
    │                                                        │ executes method
    │←─────────────────────── reply parcel ─────────────────│
    │ unblock                                                │
```

**Điểm then chốt:** Binder là **shared memory + kernel driver** — không phải socket, không phải pipe. Kernel map trực tiếp parcel buffer → **zero-copy** trong nhiều trường hợp. Một IPC call = 1 syscall `ioctl(BINDER_WRITE_READ)` — cực nhanh (micro-second range).

**Mỗi process có Binder threadpool** (default 16 threads). Khi request đến, kernel chọn 1 thread nhàn rỗi để xử lí. Đây là lý do service không bị block bởi nhiều client gọi đồng thời.

---

## LAYER 1 — AIDL Compiler: Cái gì được generate

Khi bạn viết `IDiagCarService.aidl`, compiler generate ra 3 thứ:

### 1. `IDiagCarService.java` — interface
```java
public interface IDiagCarService extends android.os.IInterface {
    void subscribeProperty(int propId, float rateHz,
                           IDiagPropertyListener listener)
        throws android.os.RemoteException;
    // ... các method khác
}
```

### 2. `IDiagCarService.Stub` — server side (bạn extend cái này)
```java
public static abstract class Stub extends android.os.Binder
                                   implements IDiagCarService {
    // Transaction codes — mỗi method có 1 int ID
    static final int TRANSACTION_subscribeProperty = IBinder.FIRST_CALL_TRANSACTION + 3;

    @Override
    public boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
        switch (code) {
            case TRANSACTION_subscribeProperty: {
                data.enforceInterface(DESCRIPTOR);
                int propId = data.readInt();
                float rateHz = data.readFloat();
                // Đây là cách AIDL recreate IBinder thành interface:
                IDiagPropertyListener listener =
                    IDiagPropertyListener.Stub.asInterface(data.readStrongBinder());
                this.subscribeProperty(propId, rateHz, listener);
                reply.writeNoException();
                return true;
            }
        }
    }
}
```

### 3. `IDiagCarService.Stub.Proxy` — client side (AIDL tự dùng, bạn không thấy)
```java
private static class Proxy implements IDiagCarService {
    private IBinder mRemote;

    @Override
    public void subscribeProperty(int propId, float rateHz,
                                  IDiagPropertyListener listener) throws RemoteException {
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeInt(propId);
            data.writeFloat(rateHz);
            // Listener là 1 Binder object — serialize thành IBinder reference
            data.writeStrongBinder(listener == null ? null : listener.asBinder());
            // Đây là IPC call thật sự:
            mRemote.transact(TRANSACTION_subscribeProperty, data, reply, 0);
            reply.readException();
        } finally {
            data.recycle();
            reply.recycle();
        }
    }
}
```

**Key insight:** `listener.asBinder()` serialize cả một **callback object** thành IBinder reference. Service giữ reference này → dùng nó để gọi ngược lại client. Đây là Callback-over-Binder pattern.

---

## LAYER 2 — `oneway`: Thread model và tại sao bắt buộc

### Vấn đề nếu KHÔNG dùng `oneway`

```
Service thread (vdiag-poller):
  pollAndDispatch() {
      for (listener : subscribers) {
          listener.onPropertyChanged(event);   // BLOCK! chờ client xử lí xong
      }
  }

Client (DiagActivity — Main Thread):
  onPropertyChanged() {
      // giả sử làm heavy work: DB write, bitmap decode
      Thread.sleep(500);   // 500ms
  }
```

Kết quả: poller thread bị block 500ms vì 1 client chậm. Nếu có 5 client tương tự → poller delay 2.5s. SOC gauge lẽ ra update 1Hz thực tế không update được.

### `oneway` giải quyết thế nào

```aidl
oneway interface IDiagPropertyListener {
    void onPropertyChanged(...);
}
```

Với `oneway`:
- Client side: `transact()` gọi với flag `FLAG_ONEWAY` → **không block, trả về ngay**
- Kernel: enqueue parcel vào binder buffer của client process
- Client's binder thread pool: xử lí async lúc rảnh
- Service: tiếp tục dispatch đến listener tiếp theo ngay lập tức

**Giới hạn của `oneway`:**
- Không có return value
- Không throw checked exception qua IPC (chỉ có `DeadObjectException` nếu client chết)
- Buffer full → `TransactionTooLargeException` (giới hạn ~1MB Binder buffer)

---

## LAYER 3 — Parcelable: Marshaling chi tiết

### Tại sao không dùng `Serializable`

`Serializable` dùng Java reflection — đọc từng field qua `getDeclaredFields()`. Chi phí reflection + GC pressure = không chấp nhận được cho event 10Hz.

`Parcelable` là **manual serialization** — compiler generate code đọc/ghi thẳng vào memory buffer:

```java
// AIDL-generated code cho DiagPropertyEvent (simplified)
public static final Parcelable.Creator<DiagPropertyEvent> CREATOR =
    new Parcelable.Creator<>() {
        @Override
        public DiagPropertyEvent createFromParcel(Parcel in) {
            DiagPropertyEvent obj = new DiagPropertyEvent();
            obj.propId       = in.readInt();
            obj.areaId       = in.readInt();
            obj.timestampNs  = in.readLong();    // 8 bytes, big-endian
            obj.valueInt     = in.readInt();
            obj.valueFloat   = in.readFloat();
            obj.valueString  = in.readString16(); // length-prefixed UTF-16
            obj.status       = in.readInt();
            return obj;
        }
    };

@Override
public void writeToParcel(Parcel dest, int flags) {
    dest.writeInt(propId);
    dest.writeInt(areaId);
    dest.writeLong(timestampNs);
    dest.writeInt(valueInt);
    dest.writeFloat(valueFloat);
    dest.writeString16(valueString);
    dest.writeInt(status);
}
```

**Binder truyền Parcel thế nào:**
- Sender: `writeToParcel()` → `Parcel` nằm trong binder buffer (kernel-mapped shared memory)
- Receiver: kernel báo hiệu → binder thread gọi `createFromParcel()` đọc từ cùng buffer đó
- Nếu trong cùng process: không copy gì cả — pointer trực tiếp

---

## LAYER 4 — `SubscriptionManager`: Từng dòng code

### Cấu trúc data

```java
ConcurrentHashMap<Integer, List<SubscriptionRecord>> mTable
```

- Key = `propId` (e.g., `DiagProp.BATTERY_SOC = 1`)
- Value = list của tất cả client đang subscribe prop đó
- `ConcurrentHashMap` vì **nhiều thread cùng access**: poller thread đọc list, binder thread thêm/xóa entry

```java
private record SubscriptionRecord(
    IDiagPropertyListener listener,   // IBinder callback về client
    float rateHz,
    IBinder.DeathRecipient death       // cleanup khi client chết
) {}
```

### Flow khi client gọi `subscribeProperty(BATTERY_SOC, 1.0f, myListener)`

```
Binder thread (service process):
1. Binder gọi DiagServiceBinder.subscribeProperty()
2. PermissionGate.enforce() — check signature permission
3. mSubManager.register(propId=1, rateHz=1.0f, listener=proxy_to_client)

Inside register():
4. Tạo DeathRecipient:
     IBinder.DeathRecipient death = () -> unregisterAll(propId, listener.asBinder());
5. listener.asBinder().linkToDeath(death, 0)
     → Kernel track IBinder này. Nếu client process die → gọi death.binderDied()
6. mTable.computeIfAbsent(1, k -> synchronizedList(new ArrayList<>()))
         .add(new SubscriptionRecord(listener, 1.0f, death))
7. refreshPoller(1)   ← quan trọng
```

### `refreshPoller()` — Trái tim của SubscriptionManager

```java
private void refreshPoller(int propId) {
    stopPoller(propId);    // cancel ScheduledFuture cũ nếu có

    List<SubscriptionRecord> list = mTable.get(propId);

    // Rate policy: dùng max rate trong tất cả subscriber
    // Ví dụ: clientA=1Hz, clientB=10Hz → poller chạy 10Hz, dispatch cho cả 2
    float maxRate = list.stream()
        .map(SubscriptionRecord::rateHz)
        .reduce(0f, Math::max);

    long periodMs = maxRate > 0
        ? (long)(1000f / maxRate)   // 10Hz → 100ms
        : 1000L;                    // on-change → poll 1Hz để detect change

    ScheduledFuture<?> future = mScheduler.scheduleAtFixedRate(
        () -> pollAndDispatch(propId, maxRate == 0),
        0,           // initialDelay = 0 (bắt đầu ngay)
        periodMs,
        TimeUnit.MILLISECONDS
    );
    mPollers.put(propId, future);
}
```

**Tại sao phải cancel và reschedule (refreshPoller)?**
Vì khi client thứ 2 subscribe với rate cao hơn, cần tăng polling rate. `ScheduledFuture` không thể change rate sau khi đã schedule → cancel + tạo mới.

### `pollAndDispatch()` — Từng bước

```java
private void pollAndDispatch(int propId, boolean onChangeOnly) {
    // 1. Đọc từ HAL qua JNI
    DiagPropertyEvent event = DiagHalBridge.nativeReadProperty(propId);
    if (event == null) return;  // HAL error — skip, không crash

    // 2. On-change filter: so sánh với cached value
    if (onChangeOnly) {
        DiagPropertyEvent last = mLastValues.get(propId);
        if (last != null && valuesEqual(last, event)) {
            return;   // value không đổi → KHÔNG dispatch → tiết kiệm IPC
        }
    }

    // 3. Update cache
    mLastValues.put(propId, event);

    // 4. Fan-out: gửi event đến TẤT CẢ subscriber của propId này
    List<SubscriptionRecord> list = mTable.get(propId);
    for (SubscriptionRecord rec : list) {
        try {
            rec.listener().onPropertyChanged(event);   // oneway → không block
        } catch (RemoteException | DeadObjectException e) {
            // Client đã chết nhưng DeathRecipient chưa kịp chạy
            // Cleanup ngay tại đây
            unregisterAll(propId, rec.listener().asBinder());
        }
    }
}
```

**Vì sao `DeadObjectException` có thể xảy ra dù đã có `DeathRecipient`?**
Race condition: client die → kernel queue `binderDied()` notification → nhưng poller thread đang đồng thời gọi listener → `DeadObjectException` xảy ra trước khi `binderDied()` được xử lí. Cần handle cả hai.

---

## LAYER 5 — `DeathRecipient`: Kernel mechanism chi tiết

```java
listener.asBinder().linkToDeath(death, 0);
```

**Bên dưới:**
1. Binder driver (`binder.c` trong Linux kernel) track reference count của mỗi IBinder
2. Khi process client die → kernel call `binder_deferred_release()` → iterate tất cả binders owned by process đó
3. Với mỗi binder có `linkToDeath` → kernel send `BR_DEAD_BINDER` transaction đến service's binder thread
4. Binder thread gọi `JavaDeathRecipient.binderDied()` (native code) → dispatch đến Java `DeathRecipient.binderDied()`
5. `() -> unregisterAll(propId, listener.asBinder())` được gọi

**`unlinkToDeath()` khi unsubscribe:**
```java
binder.unlinkToDeath(r.death(), 0);
```
Nếu không unlink → `death` callback vẫn còn trong kernel → memory leak + spurious cleanup call sau này.

---

## LAYER 6 — Thread Safety toàn bộ SubscriptionManager

```
Thread A: Binder thread          Thread B: vdiag-poller-0       Thread C: vdiag-poller-1
(client register)                (pollAndDispatch prop=1)        (pollAndDispatch prop=2)
    │                                    │                               │
    ▼                                    ▼                               ▼
mTable.computeIfAbsent()          mTable.get(1)                  mTable.get(2)
    ConcurrentHashMap ops         list (synchronized)             list (synchronized)
    ← thread-safe                 ← iterate safely                ← no contention
```

**ConcurrentHashMap** cho phép:
- Concurrent reads: multiple threads đọc cùng lúc, không lock
- Concurrent writes: lock per-segment (không lock toàn map)
- `computeIfAbsent()` là atomic

**`Collections.synchronizedList()`** cho inner list:
- `add()`, `remove()`, `removeIf()` — synchronized trên list object
- `for (rec : list)` trong `pollAndDispatch` phải trong synchronized block → tránh `ConcurrentModificationException`

**`ScheduledExecutorService` với daemon threads:**
```java
Executors.newScheduledThreadPool(2,
    r -> {
        Thread t = new Thread(r, "vdiag-poller");
        t.setDaemon(true);   // QUAN TRỌNG: JVM shutdown không bị block bởi thread này
        return t;
    });
```
`daemon = true` → khi main thread chết (service bị kill), JVM không chờ poller thread finish.

---

## LAYER 7 — Rate Policy: Tại sao dùng max(rates)?

Đây là **policy giống hệt AAOS CarPropertyService:**

```
Scenario: 2 clients subscribe BATTERY_SOC
  - DiagActivity: 1Hz (update UI gauge)
  - BackupLogger: 0.1Hz (ghi log)

Option A — poll mỗi client riêng:
  2 poller threads × 2 HAL reads/s → 2 IPC calls/s đến HAL

Option B — max rate (AAOS policy):
  1 poller thread × 1Hz → 1 HAL read/s
  Fan-out: cả 2 client nhận 1Hz
  BackupLogger nhận nhiều hơn cần → OK, client tự filter
```

Option B ít tải HAL hơn. HAL là bottleneck (đọc từ ECU qua CAN/DoIP) → cần minimize.

**Edge case — Client-side subsampling:**
Client A=10Hz, Client B=0.1Hz → B nhận 10Hz dù chỉ cần 0.1Hz. `DiagClient` SDK có thể subsampling:
```java
// Trong IDiagPropertyListener.Stub của client SDK:
private long mLastDispatchNs = 0;
public void onPropertyChanged(DiagPropertyEvent event) {
    if (event.timestampNs - mLastDispatchNs < mMinIntervalNs) return; // discard
    mLastDispatchNs = event.timestampNs;
    mCallback.onPropertyChanged(event);
}
```

**Khi client A unsubscribe:**
`unregister()` → `refreshPoller()` → recalculate max rate → cancel ScheduledFuture cũ → tạo mới với rate mới. `ScheduledFuture` không thể modify rate sau khi schedule → bắt buộc cancel + reschedule.

---

## LAYER 8 — On-Change Detection: Tại sao không trivial

```java
private boolean valuesEqual(DiagPropertyEvent a, DiagPropertyEvent b) {
    return a.valueInt == b.valueInt
        && a.valueFloat == b.valueFloat
        && Objects.equals(a.valueString, b.valueString);
}
```

**Tại sao không so sánh `timestampNs`?**
Vì `timestampNs` luôn khác nhau dù value không đổi (mỗi HAL read có timestamp mới). Nếu include timestamp → không bao giờ equal → dispatch mọi lúc → on-change logic fail.

**Float comparison:**
`a.valueFloat == b.valueFloat` — trong context HAL, giá trị HAL trả về nguyên-ish (nhiệt độ 23.5°C). Nếu cần strict: `Math.abs(a.valueFloat - b.valueFloat) < EPSILON`.

---

## LAYER 9 — AAOS CarPropertyService: Mirror chính xác

### Real AAOS source (packages/services/Car/)

```java
// CarPropertyService.java (AOSP)
public class CarPropertyService extends ICarProperty.Stub {
    public void registerListener(int propId, float rate, ICarPropertyEventListener listener) {
        Client client = new Client(listener);
        client.addProperty(propId, rate);
        mClientMap.put(listener.asBinder(), client);
        listener.asBinder().linkToDeath(client, 0);
        updateSubscriptionRateLocked(propId);
    }
}
```

**VehicleHal** (AAOS equivalent của DiagHalBridge) nhận sự kiện từ HAL qua `onPropertyEvent()` callback — HAL thật push event lên (không poll). VDiag dùng **poller** thay vì HAL-push vì MockHal không có event mechanism.

**Khi bị hỏi "tại sao poll thay vì HAL push?":**
> "Production HAL (`IVehicle.aidl` trong AAOS) có `subscribe()` API — HAL tự push event lên. MockHal của tôi không có real ECU/CAN bus nên dùng ScheduledPoller để simulate. Architecture layer phía trên (`SubscriptionManager` → `IDiagPropertyListener`) giống 1:1 với AAOS. Khi swap MockHal → DoIPHal, chỉ đổi `DiagHalBridge.nativeReadProperty()` implementation — không đụng đến subscription logic."

---

## LAYER 10 — `timestampNs`: Tại sao quan trọng

```java
long timestampNs;  // System.nanoTime() at HAL read
```

**Use case 1 — Stale data detection:**
```java
public void onPropertyChanged(DiagPropertyEvent event) {
    long ageNs = System.nanoTime() - event.timestampNs;
    if (ageNs > 2_000_000_000L) {  // 2 giây
        mView.setTextColor(Color.GRAY);  // data cũ = màu xám
    } else {
        mView.setTextColor(Color.GREEN);
    }
    mView.setText(event.valueInt + "%");
}
```

**Use case 2 — Event ordering:**
Nếu HAL trả về 2 events nhanh (burst), client nhận theo FIFO nhưng có thể process out-of-order. `timestampNs` cho phép client discard event cũ hơn event đã nhận.

**Use case 3 — Latency measurement:**
```java
long latencyNs = System.nanoTime() - event.timestampNs;
// Đo round-trip HAL → JNI → Java → IPC → client
```

---

## LAYER 11 — VehicleAreaType: Tại sao `areaId` tồn tại

```
AAOS Area Types:
  GLOBAL  (0x01000000) — toàn xe: SOC, odometer, VIN
  SEAT    (0x00000001..0x00000010) — từng ghế: ghế hâm nóng
  DOOR    (0x00000001..0x00000020) — từng cửa: door open
  MIRROR  (0x00000001..0x00000004) — gương trái/phải
  WHEEL   (0x00000001..0x00000010) — từng bánh: tire pressure
```

Ví dụ: `HVAC_TEMPERATURE_CURRENT` có `areaId = SEAT_ROW_1_LEFT` hoặc `SEAT_ROW_1_RIGHT` — 2 giá trị khác nhau cho cùng 1 propId.

VDiag dùng `areaId = 0` (GLOBAL) cho diagnostic props (SOC, RPM, DTC) vì không có area distinction.

---

## LAYER 12 — `SubscriptionToken`: Tại sao không chỉ truyền propId khi unsubscribe

```java
public record SubscriptionToken(int propId, IDiagPropertyListener listener) {}
```

Nếu cùng propId subscribe 2 lần từ cùng 1 Activity với 2 callback khác nhau:
```java
SubscriptionToken t1 = client.subscribeProperty(BATTERY_SOC, 1f, cb1);
SubscriptionToken t2 = client.subscribeProperty(BATTERY_SOC, 5f, cb2);
```
Gọi `unsubscribeProperty(BATTERY_SOC)` sẽ xóa cả 2. Token mang theo `listener` reference → xóa chính xác đúng subscription.

---

## LAYER 13 — Cleanup lifecycle: onStop() vs onDestroy()

```java
@Override
protected void onStop() {
    super.onStop();
    mDiagClient.unsubscribeProperty(socToken);  // ĐÚNG — không phải onDestroy()
    mDiagClient.unsubscribeProperty(rpmToken);
}
```

**Tại sao `onStop()` không phải `onDestroy()`?**
- `onDestroy()` không được guarantee gọi (process có thể bị kill trực tiếp)
- `onStop()` được gọi khi Activity không còn visible — không cần update UI → unsubscribe ngay
- Nếu bỏ quên: `DeathRecipient` vẫn dọn khi process die. Nhưng trước khi die, phantom listener vẫn nhận event và gọi `onPropertyChanged()` trên invisible Activity → lãng phí CPU + potential NPE

---

## LAYER 14 — Toàn bộ call flow từ đầu đến cuối

```
T=0ms  DiagActivity.onStart():
         mDiagClient.subscribeProperty(BATTERY_SOC, 1.0f, executor, callback)

T=0ms  DiagClient:
         tạo IDiagPropertyListener.Stub (anonymous class)
         gọi mService.subscribeProperty(1, 1.0f, stub) via Binder IPC

T=0ms  Binder thread (service process):
         DiagServiceBinder.subscribeProperty()
         → PermissionGate.enforce()  ← check OK
         → SubscriptionManager.register(1, 1.0f, listener_proxy)
         → linkToDeath(death, 0)     ← kernel starts tracking
         → mTable[1].add(record)
         → refreshPoller(1)
         → scheduleAtFixedRate(pollAndDispatch, 0, 1000ms)

T=1000ms  vdiag-poller thread:
           pollAndDispatch(propId=1, onChangeOnly=false)
           → DiagHalBridge.nativeReadProperty(1)   ← JNI call
           → C++: MockDiagnosticHal::readProperty(BATTERY_SOC)
           → return DiagPropertyEvent{propId=1, valueInt=78, timestampNs=...}
           → mLastValues[1] = event
           → for (rec : mTable[1]):
               rec.listener().onPropertyChanged(event)   ← oneway Binder IPC

T=1000ms  Binder thread (client process):
           IDiagPropertyListener.Stub.onTransact()
           → onPropertyChanged(event) called
           → executor.execute(callback)   ← dispatch đến getMainExecutor()

T=1000ms  Main thread (UI thread):
           callback: mSocView.setText("78%")   ← UI update
```

---

## Interview Q&A — Các câu hay bị hỏi nhất

**Q: "Tại sao SubscriptionManager dùng ConcurrentHashMap mà không dùng synchronized HashMap?"**
> `synchronized HashMap`: mọi read/write đều lock toàn map. `ConcurrentHashMap`: segment-level locking — reads không cần lock. Poller thread đọc liên tục ở 10Hz → dùng CHM giảm lock contention đáng kể.

---

**Q: "Điều gì xảy ra nếu HAL readProperty() bị block (ECU không response)?"**
> Poller thread bị block → period lệch. `ScheduledExecutorService.scheduleAtFixedRate()` sẽ skip missed firings và "catch up" — nếu task chạy quá lâu, next firing xảy ra ngay khi task xong thay vì đúng interval. Production fix: wrap `nativeReadProperty()` với timeout → return `status=UNAVAILABLE` event.

---

**Q: "Client A subscribe 10Hz, Client A unsubscribe. Client B vẫn subscribe 1Hz. Poller có tự reduce về 1Hz không?"**
> Có. `unregister()` gọi `refreshPoller()` → recalculate max rate trong remaining subscribers → cancel ScheduledFuture cũ → tạo mới với period 1000ms. Đây là lý do phải cancel và reschedule — không thể modify rate của ScheduledFuture đang chạy.

---

**Q: "Nếu DiagPropertyEvent có thêm field Bitmap (ảnh camera), có vấn đề gì?"**
> Binder buffer giới hạn **~1MB per transaction**. Ảnh camera = vài MB → `TransactionTooLargeException`. Solution: dùng `ashmem` (anonymous shared memory) — truyền `FileDescriptor` qua Binder, client mmap trực tiếp. AAOS dùng `HardwareBuffer` cho camera frames với cơ chế tương tự.

---

**Q: "`oneway` có guarantee delivery không?"**
> Không có ACK, không có retry. Nếu Binder buffer đầy: drop. Nếu client dead: `DeadObjectException`. Đây là đánh đổi có chủ ý — low latency > delivery guarantee cho sensor data (dữ liệu cũ sẽ bị replace bởi data mới 1s sau).

---

**Q: "Tại sao không dùng LocalBroadcastManager hay EventBus thay vì custom AIDL listener?"**
> LocalBroadcastManager và EventBus là **intra-process** only. `IDiagPropertyListener` là cross-process callback — `DiagCarService` chạy ở process riêng (`com.vdiag:car_service`). Nếu service chạy cùng process thì dùng EventBus được — nhưng khi đó không còn demonstrate AAOS IPC pattern và interviewer sẽ hỏi "service của bạn không isolated à?".

---

**Q: "DeathRecipient và try-catch DeadObjectException đều dọn listener — có bị double-unregister không?"**
> Có thể xảy ra race. `unregisterAll()` dùng `removeIf()` trên `synchronizedList` → nếu binder đã bị remove rồi thì `removeIf` tìm không thấy → no-op. `unlinkToDeath()` lần 2 trên binder đã dead → returns `false` (không throw). Cả 2 path đều idempotent.

---

**Q: "Tại sao valuesEqual() không include timestampNs?"**
> `timestampNs` luôn khác nhau dù value không đổi (mỗi HAL read = timestamp mới). Nếu include → không bao giờ equal → on-change sẽ dispatch mọi poll → mất đi toàn bộ ý nghĩa của on-change mode.

---

**Q: "Architect khác: dùng push từ HAL thay vì poll có được không?"**
> Được và tốt hơn cho production. AAOS `IVehicle.aidl` có `subscribe(propIds, options)` — HAL tự push event qua `IVehicleCallback.onPropertyEvent()`. VDiag dùng poll vì MockHal không có real ECU. Khi upgrade sang DoIPHal thật: thêm HAL callback → `SubscriptionManager.onHalEvent()` → dispatch ngay, không cần timer. Layer phía trên (`IDiagPropertyListener`, `DiagClient`) không đổi gì.

---

## Summary: Kiến trúc tổng thể

```
┌──────────────────────────────────────────────────────────────┐
│  App / DiagActivity                                          │
│  DiagClient SDK                                              │
│    subscribeProperty(BATTERY_SOC, 1Hz, executor, callback)  │
└────────────────────────┬─────────────────────────────────────┘
                         │  Binder IPC — IDiagCarService
┌────────────────────────▼─────────────────────────────────────┐
│  DiagCarService  (process: com.vdiag:car_service)            │
│  ├── PermissionGate                                          │
│  ├── DiagServiceBinder  (AIDL Stub)                          │
│  │     subscribeProperty() → mSubManager.register()         │
│  └── SubscriptionManager                                     │
│        ConcurrentHashMap<propId, List<Record>>               │
│        ScheduledExecutorService (vdiag-poller daemon)        │
│        DeathRecipient per listener                           │
│        Max-rate policy + on-change cache                     │
└────────────────────────┬─────────────────────────────────────┘
                         │  JNI — DiagHalBridge
┌────────────────────────▼─────────────────────────────────────┐
│  C++ MockDiagnosticHal / DoIPHal                             │
│    readProperty(propId) → Result{valueInt, timestampNs}      │
└──────────────────────────────────────────────────────────────┘
```

| Component | AAOS Equivalent | Pattern |
|---|---|---|
| `DiagCarService` | `CarPropertyService` | Bound service, central hub |
| `SubscriptionManager` | Internal subscription table in CarPropertyService | Max-rate fan-out |
| `IDiagPropertyListener` (oneway) | `ICarPropertyEventListener` (oneway) | Non-blocking callback |
| `DiagPropertyEvent` (parcelable) | `CarPropertyValue` (parcelable) | Data container |
| `SubscriptionToken` | `CarPropertyEventCallback` reference | Unsubscribe handle |
| `DeathRecipient` | `DeathRecipient` in `Client` inner class | Zombie cleanup |
| `scheduleAtFixedRate` poller | HAL `subscribe()` + `onPropertyEvent()` push | Event source |
