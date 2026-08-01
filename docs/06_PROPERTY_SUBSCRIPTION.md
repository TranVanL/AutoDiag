# 📡 Module — Property Subscription (CarPropertyEventCallback Pattern)

> **Phase 11** (Tuần 11 — MUST nếu muốn ngang tầm AAOS CarService thật). Chuyển VDiag từ **pull** (request/response) sang **push** (event-driven subscription). Đây là pattern core của `CarPropertyManager` — interviewer thấy ngay candidate hiểu AAOS 
real production behavior.

---

## 1. Tại sao cần subscription model?

Request-response (W7) đủ cho UDS one-shot query (đọc VIN, clear DTC). **Nhưng** AAOS ngoài thực tế dùng **push/subscribe** cho mọi thứ real-time:
.
| Use case | Pull (request) | Push (subscribe) |
|---|---|---|
| Đọc VIN | ✓ | không cần |
| Battery SOC real-time | quá chậm | ✓ 1Hz |
| Motor RPM dashboard | quá nhiều poll | ✓ 10Hz |
| DTC event (mới xuất hiện) | poll liên tục? | ✓ on-change |
| Door open alert | không thể | ✓ on-change |

AAOS solution: `CarPropertyManager.registerCallback(callback, propId, rate)` → HAL gửi event khi data thay đổi hoặc theo rate định sẵn.

**VDiag V2 mirror hoàn toàn pattern này.**

---

## 2. Architecture delta — thêm subscription lane

```
Before (V1 — pull only):
App → getProperty(req) → AIDL → JNI → Engine → HAL → response

After (V2 — pull + push):
App → subscribeProperty(propId, rate) → AIDL ─────────────────────────────────┐
                                                                               ↓
                                                              DiagCarService:SubscriptionManager
                                                                               ↓
                                                              PropertyPoller (timer thread)
                                                                               ↓
                                                              HAL.readProperty(propId)
                                                                               ↓
                                         App ←── onPropertyChanged(event) ── JNI callback
```

---

## 3. AIDL changes

### `IDiagCarService.aidl` — thêm 2 method

```aidl
interface IDiagCarService {
    // --- existing (V1) ---
    void getProperty(in DiagRequest req, IDiagCallback cb);
    List<DiagDtcInfo> getDtcList();
    void clearDtc();

    // --- V2: subscription ---
    /**
     * Subscribe to a property. HAL will push events at 'rateHz' (0 = on-change only).
     * Mirror of CarPropertyManager.registerCallback().
     */
    void subscribeProperty(int propId, float rateHz, IDiagPropertyListener listener);

    /**
     * Unsubscribe. Listener auto-removed on client death via DeathRecipient.
     */
    void unsubscribeProperty(int propId, IDiagPropertyListener listener);
}
```

### `IDiagPropertyListener.aidl` — new file

```aidl
// oneway: service → client, KHÔNG block service thread
// Mirror of CarPropertyEventCallback in AAOS
oneway interface IDiagPropertyListener {
    void onPropertyChanged(in DiagPropertyEvent event);
    void onErrorEvent(int propId, int areaId, int errorCode);
}
```

### `DiagPropertyEvent.aidl` — new parcelable

```aidl
parcelable DiagPropertyEvent {
    int     propId;           // e.g. DiagProp.BATTERY_SOC
    int     areaId;           // VehicleAreaType (0 = GLOBAL)
    long    timestampNs;      // System.nanoTime() at HAL read
    int     valueInt;         // for int/percent props
    float   valueFloat;       // for float props
    String  valueString;      // for string props (VIN)
    int     status;           // AVAILABLE | UNAVAILABLE | ERROR
}
```

> **VehicleAreaType mapping (quan trọng):** AAOS chia property theo "area" — GLOBAL (toàn xe), SEAT (từng ghế), DOOR (từng cửa), MIRROR, WHEEL. VDiag chỉ dùng GLOBAL (0x01000000) cho diagnostic props, nhưng biết phân biệt thì sẽ được hỏi trong interview.

---

## 4. Java: SubscriptionManager (server side)

```java
// DiagCarService.java — thêm field
private final SubscriptionManager mSubManager = new SubscriptionManager();

// DiagServiceBinder.java
@Override
public void subscribeProperty(int propId, float rateHz, IDiagPropertyListener listener)
        throws RemoteException {
    PermissionGate.enforce(getBaseContext());              // reuse existing gate
    mSubManager.register(propId, rateHz, listener);       // store + start poller
}

@Override
public void unsubscribeProperty(int propId, IDiagPropertyListener listener) {
    mSubManager.unregister(propId, listener);
}
```

```java
/**
 * SubscriptionManager — mirrors CarPropertyService subscription table.
 *
 * Design:
 *   - ConcurrentHashMap<propId, List<SubscriptionRecord>>
 *   - Each record: listener + rateHz + DeathRecipient (auto-cleanup on client crash)
 *   - Per-propId ScheduledFuture (PropertyPoller) runs at max(rateHz of subscribers)
 *   - on-change (rateHz=0): poller runs at 1Hz, compares with cached value
 */
public class SubscriptionManager {

    private record SubscriptionRecord(
        IDiagPropertyListener listener,
        float rateHz,
        IBinder.DeathRecipient death   // auto-unregister on client crash
    ) {}

    private final ConcurrentHashMap<Integer, List<SubscriptionRecord>> mTable
            = new ConcurrentHashMap<>();

    private final ScheduledExecutorService mScheduler
            = Executors.newScheduledThreadPool(2,
                  r -> { Thread t = new Thread(r, "vdiag-poller"); t.setDaemon(true); return t; });

    private final ConcurrentHashMap<Integer, ScheduledFuture<?>> mPollers
            = new ConcurrentHashMap<>();

    // Cache last value per prop — for on-change detection
    private final ConcurrentHashMap<Integer, DiagPropertyEvent> mLastValues
            = new ConcurrentHashMap<>();

    public void register(int propId, float rateHz, IDiagPropertyListener listener) {
        IBinder.DeathRecipient death = () -> unregisterAll(propId, listener.asBinder());
        try { listener.asBinder().linkToDeath(death, 0); } catch (RemoteException ignored) {}

        mTable.computeIfAbsent(propId, k -> Collections.synchronizedList(new ArrayList<>()))
              .add(new SubscriptionRecord(listener, rateHz, death));

        refreshPoller(propId);   // recalculate rate + reschedule
    }

    public void unregister(int propId, IDiagPropertyListener listener) {
        unregisterAll(propId, listener.asBinder());
    }

    private void unregisterAll(int propId, IBinder binder) {
        List<SubscriptionRecord> list = mTable.get(propId);
        if (list == null) return;
        list.removeIf(r -> {
            if (r.listener().asBinder() == binder) {
                binder.unlinkToDeath(r.death(), 0);
                return true;
            }
            return false;
        });
        if (list.isEmpty()) { mTable.remove(propId); stopPoller(propId); }
        else refreshPoller(propId);
    }

    private void refreshPoller(int propId) {
        stopPoller(propId);
        List<SubscriptionRecord> list = mTable.get(propId);
        if (list == null || list.isEmpty()) return;

        // Use max rate among all subscribers (same as AAOS CarPropertyService policy)
        float maxRate = list.stream()
            .map(SubscriptionRecord::rateHz)
            .reduce(0f, Math::max);

        long periodMs = maxRate > 0 ? (long)(1000f / maxRate) : 1000L; // on-change → poll 1Hz

        ScheduledFuture<?> future = mScheduler.scheduleAtFixedRate(
            () -> pollAndDispatch(propId, maxRate == 0),
            0, periodMs, TimeUnit.MILLISECONDS
        );
        mPollers.put(propId, future);
    }

    private void stopPoller(int propId) {
        ScheduledFuture<?> f = mPollers.remove(propId);
        if (f != null) f.cancel(false);
    }

    private void pollAndDispatch(int propId, boolean onChangeOnly) {
        // Read from HAL via JNI
        DiagPropertyEvent event = DiagHalBridge.nativeReadProperty(propId);
        if (event == null) return;

        if (onChangeOnly) {
            DiagPropertyEvent last = mLastValues.get(propId);
            if (last != null && valuesEqual(last, event)) return; // no change, skip
        }
        mLastValues.put(propId, event);

        List<SubscriptionRecord> list = mTable.get(propId);
        if (list == null) return;
        for (SubscriptionRecord rec : list) {
            try { rec.listener().onPropertyChanged(event); }
            catch (RemoteException | DeadObjectException e) {
                unregisterAll(propId, rec.listener().asBinder()); // zombie client
            }
        }
    }

    private boolean valuesEqual(DiagPropertyEvent a, DiagPropertyEvent b) {
        return a.valueInt == b.valueInt && a.valueFloat == b.valueFloat
            && Objects.equals(a.valueString, b.valueString);
    }
}
```

---

## 5. C++ HAL — thêm `readProperty()`

```cpp
// IDiagnosticHal.h — thêm 1 method
class IDiagnosticHal {
public:
    // existing
    virtual Result sendAndReceive(std::span<uint8_t> request) = 0;
    virtual bool   isReady() const = 0;
    virtual void   reset() = 0;

    // V2: point-in-time read (for poller)
    virtual Result readProperty(uint32_t propId) = 0;

    virtual ~IDiagnosticHal() = default;
};

// MockDiagnosticHal.cpp — implement readProperty
Result MockDiagnosticHal::readProperty(uint32_t propId) {
    switch (propId) {
        case DiagProp::BATTERY_SOC:
            return Result{.valueInt = simulate_battery_soc()};  // slow drift 78→79
        case DiagProp::MOTOR_RPM:
            return Result{.valueInt = simulate_rpm()};           // 3000-3500 random
        case DiagProp::VIN:
            return Result{.valueStr = "VINFAST12345678901"};
        default:
            return Result{.status = Status::NOT_SUPPORTED};
    }
}
```

---

## 6. Client SDK — DiagClient V2

```java
public class DiagClient {
    // --- V1 existing ---
    public ListenableFuture<DiagResponse> getProperty(DiagProperty prop) { ... }

    // --- V2 subscribe ---
    /**
     * Subscribe to property changes. Mirrors CarPropertyManager.registerCallback().
     *
     * @param prop     The property to monitor
     * @param rateHz   Update rate. 0 = on-change only. Max 10Hz for battery, 50Hz for RPM.
     * @param executor Executor for callback dispatch (avoid main thread blocking)
     * @param callback Invoked on each new value
     * @return         Registration token — pass to unsubscribe()
     */
    public @NonNull SubscriptionToken subscribeProperty(
            @NonNull DiagProperty prop,
            float rateHz,
            @NonNull Executor executor,
            @NonNull DiagPropertyCallback callback) {
        // wrap callback in IDiagPropertyListener.Stub
        IDiagPropertyListener stub = new IDiagPropertyListener.Stub() {
            @Override
            public void onPropertyChanged(DiagPropertyEvent event) {
                executor.execute(() -> callback.onPropertyChanged(
                    DiagProperty.fromId(event.propId), event));
            }
            @Override
            public void onErrorEvent(int propId, int areaId, int errorCode) {
                executor.execute(() -> callback.onError(
                    DiagProperty.fromId(propId), errorCode));
            }
        };
        mService.subscribeProperty(prop.id(), rateHz, stub);
        return new SubscriptionToken(prop.id(), stub);
    }

    public void unsubscribeProperty(@NonNull SubscriptionToken token) {
        mService.unsubscribeProperty(token.propId(), token.listener());
    }
}
```

### Usage in DiagActivity

```java
// Subscribe to battery SOC at 1Hz
SubscriptionToken socToken = mDiagClient.subscribeProperty(
    DiagProperty.BATTERY_SOC,
    1.0f,                                // 1 Hz
    getMainExecutor(),
    (prop, event) -> mSocView.setText(event.valueInt + "%")
);

// Subscribe to RPM on-change
SubscriptionToken rpmToken = mDiagClient.subscribeProperty(
    DiagProperty.MOTOR_RPM,
    0f,                                  // on-change only
    getMainExecutor(),
    (prop, event) -> mRpmGauge.setValue(event.valueInt)
);

// Unsubscribe in onStop() — prevent ghost listeners
@Override
protected void onStop() {
    super.onStop();
    mDiagClient.unsubscribeProperty(socToken);
    mDiagClient.unsubscribeProperty(rpmToken);
}
```

---

## 7. Rate policy table (interview-ready)

| Property | Typical rate | AAOS equivalent |
|---|---|---|
| Battery SOC | 1 Hz | `VehicleProperty.EV_BATTERY_LEVEL` |
| Motor RPM | 10 Hz | `VehicleProperty.PERF_VEHICLE_SPEED` |
| Odometer | 1 Hz | `VehicleProperty.PERF_ODOMETER` |
| DTC появление | on-change | `VehicleProperty.OBD2_FREEZE_FRAME` |
| Door open | on-change | `VehicleProperty.DOOR_OPEN` |
| Cabin temp | 0.5 Hz | `VehicleProperty.HVAC_TEMPERATURE_CURRENT` |

---

## 8. Test plan

```
test_subscription_manager/
├── test_register_unregister.cpp        ← register + unregister clears table
├── test_death_recipient_cleanup.cpp    ← kill binder → auto-unregister
├── test_rate_policy.cpp                ← max(1Hz, 10Hz) = 10Hz poller used
├── test_on_change_no_dup.cpp           ← same value → listener NOT called twice
└── test_multi_client_same_prop.cpp     ← 3 clients, 1 poller at max rate
```

---

## 9. Interview story (nói trong 2 phút)

> *"V1 VDiag chỉ có request-response — đủ cho one-shot query. Nhưng dashboard real-time cần push model. Tôi thêm `subscribeProperty()` mirror theo `CarPropertyManager.registerCallback()` của AAOS. Server có một `SubscriptionManager` — table `ConcurrentHashMap<propId, List<listener>>`, mỗi propId có một `ScheduledFuture` polling HAL theo max rate trong số tất cả subscriber. Rate policy giống hệt AAOS: nếu client A subscribe 1Hz và client B subscribe 10Hz thì poller chạy 10Hz, dispatch cho cả hai. On-change thì poll 1Hz nhưng cache last value, chỉ dispatch khi value thực sự thay đổi. Khi client crash, `DeathRecipient` tự cleanup entry — không leak phantom listener. Tổng thêm ~400 LOC Java, 3 AIDL file, 2 C++ method, 5 test."*
