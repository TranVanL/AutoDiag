# Debugging & Observability — dumpsys, atrace, and Metrics

> **Purpose:** Document how VDiag makes the running system observable without attaching a debugger. This is a senior differentiator: anyone can make code run; production engineers make it *debuggable*.

---

## 1. `dumpsys` integration

`DiagCarService` overrides `Service.dump()` to expose internal state:

```bash
adb shell dumpsys activity service com.vdiag/.service.DiagCarService
```

### Default output sections

1. **HAL state** — up/down, transport type, reconnect backoff.
2. **Engine queue** — depth per tier, worker alive flag.
3. **Subscriptions** — active propIds, subscriber count, max rate.
4. **Binder stats** — total transactions, max concurrent, rejects.
5. **Transaction history** — last 256 requests with latency (use `--history`).
6. **Proto hook** — machine-readable snapshot (use `--proto`).

### Why it matters

In production, you can't attach Android Studio. `dumpsys` is how field engineers and bugreport tools inspect a live service. It's the same mechanism used by `dumpsys activity`, `dumpsys meminfo`, etc.

---

## 2. Transaction history ring buffer

A lock-free 256-slot ring records:

```cpp
struct TxRecord {
    int      code;        // AIDL transaction code
    int64_t  latencyNs;   // total round-trip
    Priority tier;        // CRITICAL/HIGH/NORMAL/LOW
    Result   result;      // OK / ERROR / TIMEOUT
};
```

The ring uses a single atomic write index. Writers are wait-free; readers copy and sort the snapshot to compute p50/p95/p99 on demand.

---

## 3. Perfetto / atrace

VDiag adds `ATrace_beginSection`/`endSection` around:

- Request enqueue → dequeue → response.
- JNI callback bridge invocation.
- HAL `sendAndReceive` round-trip.

A Perfetto capture visualizes the cross-thread pipeline and reveals where time is spent.

---

## 4. Health probes

The `ISystemLifecycle` health check probes:

- `nativeIsWorkerAlive()` — worker thread not deadlocked.
- `nativeGetQueueDepth()` — queue not backed up.
- `nativeIsHalReady()` — HAL responsive.

These feed the CarWatchdog heartbeat (or the Handler shim on non-Automotive AVD).

---

## 5. Logging discipline

- Use structured tags: `VDiag.Binder`, `VDiag.JNI`, `VDiag.Engine`, `VDiag.HAL`.
- Log property IDs and status, not values, in release builds.
- Use `Log.isLoggable()` to avoid formatting cost when disabled.

---

## 6. Interview talking points

> *"I treat observability as a first-class feature. VDiag exposes HAL state, queue depth, subscriptions, Binder stats, and a 256-slot transaction history via `dumpsys`. It also traces the cross-thread pipeline with atrace/Perfetto. In production, these are the tools that turn 'it's slow' into 'p99 wait time on the CRITICAL tier is 12ms because the HAL is taking 10ms.'"*
