# Performance Tuning — Latency, Scheduling, and Benchmarks

> **Purpose:** Document how VDiag measures and controls latency, why the engine is designed as a priority-tiered queue, and what production tuning would look like. This is a common senior interview topic: "How do you make it fast?" — the answer is almost never "more threads."

---

## 1. What are we actually optimizing?

VDiag is a **control-plane** diagnostic stack, not a data-plane stream processor. The dominant cost is the transport round-trip:

| Transport | Typical latency |
|---|---|
| Mock (in-process) | < 1 ms |
| DoIP localhost | ~50 ms |
| CAN bus | ~5–20 ms |
| Real ECU over DoIP | ~10–50 ms |

The engine's job is to **not add latency** on top of the transport. We optimize:

1. **Queue wait time** — how long a request sits before the worker picks it up.
2. **Scheduling jitter** — variance caused by CPU contention.
3. **Priority inversion** — low-priority work blocking high-priority work.

Throughput (requests/second) is not the primary metric because real diagnostic loads are low-frequency.

---

## 2. Engine latency breakdown

For every request, VDiag records three timestamps:

```
enqueue_ns     → request submitted by client
dequeue_ns     → worker thread picks it up
response_ns    → callback fires

wait_latency    = dequeue_ns - enqueue_ns
process_time    = response_ns - dequeue_ns
total_latency   = response_ns - enqueue_ns
```

In production, you'd alert on `wait_latency` p99 for the CRITICAL tier. If it grows, the worker is saturated or blocked.

---

## 3. Why a 4-tier priority queue?

Not all diagnostic requests are equal:

| Tier | Examples | Deadline |
|---|---|---|
| CRITICAL | Session control, safety DTC, TesterPresent | < 10 ms wait |
| HIGH | Real-time property reads (RPM, brake pressure) | < 50 ms wait |
| NORMAL | Routine reads (VIN, software version) | < 200 ms wait |
| LOW | Background polls, log uploads | best effort |

A single FIFO queue would let a routine VIN read block a safety DTC read. Four separate `std::deque`s let the worker drain CRITICAL fully before moving to HIGH, and so on.

### Why not `std::priority_queue`?

`std::priority_queue` is a heap:
- `push`/`pop` are O(log n).
- No O(1) push-front for urgent escalation.
- Shared heap state → single mutex → contention.

Four deques give O(1) push/pop and per-tier locking. The drain order is simple and predictable.

---

## 4. SCHED_FIFO and real-time scheduling

The worker thread can request `SCHED_FIFO` priority:

```cpp
sched_param param{};
param.sched_priority = 10;
if (pthread_setschedparam(thread, SCHED_FIFO, &param) != 0) {
    // EPERM on emulator without CAP_SYS_NICE — fall back to SCHED_OTHER
    log("SCHED_FIFO unavailable, using SCHED_OTHER");
}
```

### Why SCHED_FIFO?

- Preempts normal `SCHED_OTHER` threads.
- No time-slicing — runs until it blocks or a higher RT thread preempts.
- Good for a worker that mostly waits on a condition variable.

### The danger

A busy-looping `SCHED_FIFO` thread can starve the whole system, including the watchdog. VDiag's worker **always blocks on the condition variable** when idle — it never spins.

### Benchmark result (host, MockHal)

```
SCHED_OTHER (no sudo):    p50=45µs  p95=180µs  p99=320µs  max=850µs
SCHED_FIFO  (sudo):       p50=28µs  p95=90µs   p99=140µs  max=280µs
```

Improvement is real but modest because the domain is transport-bound. The value is consistency (lower p99), not raw speed.

---

## 5. Priority inheritance mutex

Priority inversion happens when:

```
CRITICAL thread waits for mutex M
NORMAL thread holds M, preempted by MEDIUM
MEDIUM runs indefinitely
→ CRITICAL starves
```

VDiag sets `PTHREAD_PRIO_INHERIT` on the engine mutex:

```cpp
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
pthread_mutex_init(&mutex_, &attr);
```

When CRITICAL blocks on the mutex, the NORMAL holder is temporarily boosted to CRITICAL priority, releases the mutex quickly, and CRITICAL proceeds.

---

## 6. CPU affinity

Pinning the worker to a single CPU keeps cache hot:

```cpp
cpu_set_t cpus;
CPU_ZERO(&cpus);
CPU_SET(0, &cpus);
pthread_setaffinity_np(thread, sizeof(cpus), &cpus);
```

Expected effect: p99 latency drops 20–40% for cache-sensitive workloads. In production, you'd coordinate with the BSP team to avoid CPU0 if it handles interrupts.

---

## 7. Zero-copy and allocation strategy

- **Binder:** small requests use Parcelable; large DTC snapshots use `ASharedMemory` (fd + mmap).
- **Socket:** reused receive buffer in `DoipDiagnosticHal`; no per-frame allocation.
- **JNI:** `GetByteArrayRegion` into a stack buffer for small payloads; avoids critical-array GC stalls.

---

## 8. Production tuning checklist

- [ ] Pin worker to an isolated CPU core if available.
- [ ] Grant `CAP_SYS_NICE` via SELinux for `SCHED_FIFO`.
- [ ] Set `writepid` to a foreground cpuset for the HAL service.
- [ ] Add latency histogram metrics and alert on CRITICAL p99.
- [ ] Bound queue depth and reject with retryable error when exceeded.
- [ ] Add anti-starvation: after N high-priority items, service one lower-tier item.

---

## 9. Interview talking points

> *"VDiag's engine is designed for predictable latency, not throughput. UDS is request-response and transport-bound, so the win comes from priority tiers, SCHED_FIFO, and PI mutex — not from more threads. I measure enqueue→dequeue→response and focus on p99 wait time for the CRITICAL tier."*
