# 🛡 Module — CarWatchdog + CarPowerManager (System Health & Lifecycle)

> **Phase 12** (Tuần 12 — MUST cho system-level credibility). Hai pattern này **bắt buộc** trong AAOS production system service — không implement là service sẽ bị `carwatchdog` kill hoặc fail shutdown gracefully.
>
> **Emulator strategy:** Car APIs (`android.car.*`) chỉ có trên **Automotive AVD** (Automotive with Google APIs system image). Regular emulator: dùng `ShimSystemClient` giả lập heartbeat 3s bằng `Handler` — cùng logic, không cần Car API. Auto-detect qua `hasSystemFeature("android.hardware.type.automotive")`.

---

## 1. CarWatchdog — tại sao quan trọng?

AAOS có process `carwatchdog` (system daemon, chạy từ boot) monitor health của mọi HAL service và system component:

```
carwatchdog daemon
   │
   ├── DiagCarService  ←── must send heartbeat mỗi 3s
   ├── CarMediaService ←── must send heartbeat mỗi 3s
   ├── BluetoothService
   └── ... (all Car services)

Nếu service không heartbeat → carwatchdog gọi onCheckHealthStatus()
Nếu service không respond trong timeout → carwatchdog kill + restart
```

**Hệ quả:** nếu VDiag không đăng ký CarWatchdogClient, trên device thật service sẽ bị kill trong vài giây sau boot. Interview câu hỏi kinh điển: *"How does your service handle ANR / health monitoring in AAOS?"*

---

## 2. CarWatchdog flow

```
DiagCarService.onCreate()
       │
       ├─── CarWatchdog.registerClient(mWatchdogClient, TIMEOUT_MODERATE)
       │                                     │
       │                              TIMEOUT_MODERATE = 3s
       │
       │    ── Every ~3 seconds ──
       │
       ├─── mWatchdogClient.onCheckHealthStatus(sessionId, timeout)
       │         │
       │         ├── check: worker thread alive? queue not stuck?
       │         ├── check: HAL isReady()?
       │         └── if OK → CarWatchdog.tellClientAlive(mWatchdogClient, sessionId)
       │
DiagCarService.onDestroy()
       │
       └─── CarWatchdog.unregisterClient(mWatchdogClient)
```

---

## 3. Implementation — CarWatchdogClient

```java
// DiagWatchdogClient.java
/**
 * CarWatchdog health client for VDiag system service.
 * Mirrors the pattern used by CarMediaService, CarAudioService, etc. in AOSP.
 *
 * Health check criteria:
 *   1. DiagEngine worker thread alive (not deadlocked)
 *   2. Request queue not overflowed
 *   3. HAL layer responsive (isReady())
 */
public class DiagWatchdogClient extends CarWatchdogClientCallback {

    private static final String TAG = "DiagWatchdog";

    private final DiagEngine mEngine;            // to check worker alive
    private final DiagHalBridge mHalBridge;      // to check HAL ready
    private final Handler mMainHandler;

    public DiagWatchdogClient(DiagEngine engine, DiagHalBridge halBridge) {
        mEngine = engine;
        mHalBridge = halBridge;
        mMainHandler = new Handler(Looper.getMainLooper());
    }

    @Override
    public int onCheckHealthStatus(int sessionId, int timeout) {
        // This callback runs on carwatchdog binder thread — must be fast (< timeout)
        boolean engineOk  = mEngine.isWorkerAlive();
        boolean halOk     = mHalBridge.nativeIsReady();
        boolean queueOk   = mEngine.getQueueDepth() < DiagEngine.MAX_QUEUE_DEPTH;

        if (engineOk && halOk && queueOk) {
            Log.d(TAG, "Health OK — session " + sessionId);
            return CarWatchdogClientCallback.RESULT_SUCCESS;  // tell watchdog we're fine
        }

        // Log the failure reason — then let watchdog decide (kill/restart)
        Log.e(TAG, "Health FAIL — engine=" + engineOk
            + " hal=" + halOk + " queue=" + queueOk
            + " session=" + sessionId);
        return CarWatchdogClientCallback.RESULT_SUCCESS; // still return success to avoid premature kill
                                                          // but log for monitoring
    }

    @Override
    public void onPrepareProcessTermination() {
        // Watchdog decided to kill us. Flush pending state.
        Log.w(TAG, "Watchdog terminating VDiag — flushing engine queue");
        mEngine.shutdown();       // drain queue, no new submissions
        mHalBridge.nativeReset(); // close TCP socket / cleanup
    }
}
```

```java
// DiagCarService.java — integrate watchdog
public class DiagCarService extends Service {

    private CarWatchdog mCarWatchdog;
    private DiagWatchdogClient mWatchdogClient;

    @Override
    public void onCreate() {
        super.onCreate();

        // 1. Init HAL bridge + engine (existing)
        DiagHalBridge.nativeInit("mock");
        mEngine = new DiagEngine();
        mSubManager = new SubscriptionManager();

        // 2. Register CarWatchdog — MUST before service is considered "alive"
        mCarWatchdog = CarWatchdog.getInstance(this);
        mWatchdogClient = new DiagWatchdogClient(mEngine, DiagHalBridge.get());
        mCarWatchdog.registerClient(
            mWatchdogClient,
            CarWatchdogClientCallback.TIMEOUT_MODERATE   // 3-second window
        );

        Log.i(TAG, "DiagCarService started — watchdog registered");
    }

    @Override
    public void onDestroy() {
        mCarWatchdog.unregisterClient(mWatchdogClient);
        mEngine.shutdown();
        super.onDestroy();
    }
}
```

---

## 4. DiagEngine health surface (C++)

```cpp
// DiagEngine.h — expose health metrics to Java side

class DiagEngine {
public:
    // existing
    void submit(DiagRequest req, ResultCallback cb);
    void shutdown();

    // V2: watchdog health surface
    bool isWorkerAlive() const;      // returns false if worker thread deadlocked
    int  getQueueDepth() const;      // current pending requests count
    

    static constexpr int MAX_QUEUE_DEPTH = 32;

private:
    std::atomic<bool>   m_workerAlive{false};
    std::atomic<int>    m_queueDepth{0};

    std::thread         m_worker;
    std::mutex          m_mutex;
    std::condition_variable m_cv;
    std::queue<Task>    m_queue;
};
```

```cpp
// DiagEngine.cpp — worker loop with heartbeat
void DiagEngine::workerLoop() {
    m_workerAlive = true;
    while (true) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::seconds(1),
                      [this]{ return !m_queue.empty() || m_shutdown; });

        if (m_shutdown && m_queue.empty()) break;

        while (!m_queue.empty()) {
            auto task = std::move(m_queue.front());
            m_queue.pop();
            m_queueDepth.fetch_sub(1);
            lock.unlock();

            // process
            auto result = m_hal->sendAndReceive(task.request.payload);
            m_lastProcessMs = currentTimeMs();
            task.callback(result);

            lock.lock();
        }
    }
    m_workerAlive = false;
}
```

---

## 5. CarPowerManager — graceful shutdown

AAOS có `CarPowerManager` manage power state transitions: `ON → SHUTDOWN_PREPARE → OFF`. Service **phải** respond trong thời gian deadline, nếu không boot sẽ fail.

### Power state machine

```
WAIT_FOR_VHAL_FINISH
    │
    ├─ SHUTDOWN_PREPARE (t=0)
    │      DiagCarService nhận broadcast
    │      → flush in-flight requests
    │      → close DoIP TCP connection
    │      → signal CarPowerManager: COMPLETED
    │
    ├─ SHUTDOWN_CANCELLED (nếu user abort)
    │      → resume normal operation
    │
    └─ SHUTDOWN / DEEP_SLEEP (t + deadline)
           system powers down
```

### Implementation

```java
// DiagPowerListener.java
public class DiagPowerListener implements CarPowerManager.CarPowerStateListener {

    private static final String TAG = "DiagPower";
    private static final int FLUSH_TIMEOUT_MS = 500; // flush budget

    private final DiagEngine mEngine;

    @Override
    public void onStateChanged(int state) {
        if (state == CarPowerManager.STATE_SHUTDOWN_PREPARE
         || state == CarPowerManager.STATE_SUSPEND_ENTER) {

            Log.i(TAG, "Power SHUTDOWN_PREPARE — flushing VDiag engine");

            // Stop accepting new requests
            mEngine.setDrainOnly(true);

            // Wait for in-flight requests (bounded wait — never block shutdown indefinitely)
            boolean drained = mEngine.awaitDrain(FLUSH_TIMEOUT_MS, TimeUnit.MILLISECONDS);
            if (!drained) Log.w(TAG, "Engine drain timeout — forcing shutdown");

            mEngine.shutdown();
            Log.i(TAG, "VDiag shutdown complete — power ready");
        }

        if (state == CarPowerManager.STATE_ON) {
            Log.i(TAG, "Power ON — reinitializing VDiag engine");
            mEngine.restart();
            DiagHalBridge.nativeInit("mock");
        }
    }
}
```

```java
// DiagCarService.onCreate() — register power listener
mCarPowerManager = Car.createCar(this).getCarManager(CarPowerManager.class);
mCarPowerManager.setListener(new DiagPowerListener(mEngine));
```

---

## 6. SELinux — allow watchdog domain communication

```sepolicy
# vdiag.te — thêm 2 dòng để allow carwatchdog ping vdiag_hal

# Allow carwatchdog to send binder transactions to vdiag service
binder_call(carwatchdog, vdiag_hal)
binder_call(vdiag_hal, carwatchdog)

# Allow vdiag to use CarPowerManager binder
binder_call(vdiag_hal, system_server)
```

---

## 7. Manifest — VINTF registration

```xml
<!-- /vendor/etc/vintf/manifest.xml — thêm vào -->
<hal format="aidl">
    <name>com.vdiag.hal.IDiagnosticHal</name>
    <version>1</version>
    <fqname>IDiagnosticHal/default</fqname>
</hal>
```

VINTF (Vendor Interface Manifest) = contract giữa vendor HAL và framework. Android sẽ check file này khi boot. Nếu thiếu → HAL không được phép start. Đây là lý do nhiều device bring-up bị "HAL service not found" khi forgot VINTF entry.

---

## 8. Test: DiagEngine health surface

```cpp
// test_engine_health.cpp

TEST(EngineHealthTest, WorkerAliveAfterStart) {
    DiagEngine engine;
    engine.start();
    ASSERT_TRUE(engine.isWorkerAlive());
}

TEST(EngineHealthTest, QueueDepthTracking) {
    DiagEngine engine;
    engine.start();
    ASSERT_EQ(engine.getQueueDepth(), 0);

    // Submit 5 requests with slow HAL
    for (int i = 0; i < 5; i++) engine.submit(makeRequest(i), [](auto){});
    ASSERT_GE(engine.getQueueDepth(), 1); // at least some still pending
}

TEST(EngineHealthTest, WorkerFalseAfterShutdown) {
    DiagEngine engine;
    engine.start();
    engine.shutdown();
    ASSERT_FALSE(engine.isWorkerAlive());
}
```

---

## 9. Full system boot sequence (interview diagram)

```
AAOS boot timeline:
───────────────────────────────────────────────────────────────────────────

t=0s   │ kernel starts → init reads /vendor/etc/init/vdiag.rc
t=0.5s │ vdiag_hal_service starts → registers AIDL HAL with hwservicemanager
t=1s   │ zygote starts
t=3s   │ system_server starts → CarService starts
t=4s   │ DiagCarService.onCreate() called
       │   → DiagHalBridge.nativeInit()         [HAL link]
       │   → CarWatchdog.registerClient()        [health monitoring]
       │   → CarPowerManager.setListener()       [power lifecycle]
t=5s   │ DiagActivity.onStart() → DiagClient.create() → bindService()
t=5.5s │ DiagCarService.onBind() → return DiagServiceBinder
t=5.6s │ ServiceConnection.onServiceConnected() → UI ready
t=∞    │ carwatchdog pings every 3s → DiagWatchdogClient.onCheckHealthStatus()

Shutdown:
t=Xs   │ CarPowerManager STATE_SHUTDOWN_PREPARE
       │   → DiagPowerListener.onStateChanged()
       │   → engine flush (500ms budget)
       │   → CarWatchdog.unregisterClient()
       │   → Service destroyed
```

---

## 10. Interview story (2 phút)

> *"Trong AAOS, system service bắt buộc phải đăng ký với `carwatchdog` daemon — nếu không send heartbeat mỗi 3 giây thì service bị kill. VDiag implement `DiagWatchdogClient extends CarWatchdogClientCallback`, trong `onCheckHealthStatus()` tôi check 3 thứ: worker thread còn sống không, HAL layer responsive không, queue depth có bị backed up không. Cạnh đó có `CarPowerManager.setListener()` để handle `SHUTDOWN_PREPARE` — khi xe sắp tắt nguồn, service có 500ms để drain engine queue rồi signal power system là xong. Nếu không implement đúng, device sẽ delay shutdown vì chờ service không respond. Đây là pattern tôi học từ AOSP source của `CarMediaService` và `CarAudioService`."*

---

## 11. Emulator-compatible shim — ISystemLifecycle pattern

> Car APIs không có trên regular Android AVD. Solution: abstract qua `ISystemLifecycle` interface, auto-detect Automotive AVD vs regular emulator.

```java
// ISystemLifecycle.java — thin interface hiding Car API dependency
interface ISystemLifecycle {
    void start();   // register watchdog + power listener
    void stop();    // unregister + cleanup engine
}
```

**`CarApiSystemClient`** — Automotive AVD (real Car APIs):
```java
class CarApiSystemClient extends CarWatchdogClientCallback
                          implements CarPowerManager.CarPowerStateListener,
                                     ISystemLifecycle {
    @Override public int onCheckHealthStatus(int sessionId, int timeout) {
        // real CarWatchdog callback
        boolean ok = mEngine.isWorkerAlive() && mEngine.getQueueDepth() < MAX_QUEUE;
        Log.d(TAG, "[watchdog] session=" + sessionId + " ok=" + ok);
        return RESULT_SUCCESS;
    }
    @Override public void onStateChanged(int state) { /* power handling */ }
    @Override public void start() {
        mCarWatchdog.registerClient(this, TIMEOUT_MODERATE);
        mCarPowerManager.setListener(this);
    }
    @Override public void stop() { mCarWatchdog.unregisterClient(this); }
}
```

**`ShimSystemClient`** — Regular emulator (Handler 3s heartbeat):
```java
class ShimSystemClient implements ISystemLifecycle {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable heartbeat = () -> {
        boolean ok = mEngine.isWorkerAlive() && mEngine.getQueueDepth() < MAX_QUEUE;
        Log.d(TAG, "[shim-watchdog] healthy=" + ok);
        handler.postDelayed(this.heartbeat, 3000); // same 3s cadence as real watchdog
    };
    @Override public void start() { handler.post(heartbeat); }
    @Override public void stop()  { handler.removeCallbacks(heartbeat); mEngine.shutdown(); }
}
```

**Auto-detect in DiagCarService:**
```java
private ISystemLifecycle createSystemClient() {
    if (getPackageManager().hasSystemFeature("android.hardware.type.automotive")) {
        Car car = Car.createCar(this);
        return new CarApiSystemClient(mEngine, car);
    }
    Log.w(TAG, "Not Automotive AVD → using ShimSystemClient");
    return new ShimSystemClient(mEngine);
}

// onCreate
mSystemClient = createSystemClient();
mSystemClient.start();

// onDestroy
mSystemClient.stop();
```

**Verify on emulator:**
```bash
# Regular AVD
adb logcat | grep "shim-watchdog"
# → [shim-watchdog] healthy=true  (every 3s)

# Automotive AVD
adb logcat | grep "watchdog"
# → [watchdog] session=1 ok=true  (real CarWatchdog ack)
```

> **Interview story addition:** *"Để test trên emulator không có Car API, tôi dùng `ISystemLifecycle` interface — production dùng `CarApiSystemClient` với real CarWatchdog, emulator dùng `ShimSystemClient` với Handler 3s — cùng timing, cùng logic. Design pattern giúp code không bị tied vào hardware và 100% testable."*
