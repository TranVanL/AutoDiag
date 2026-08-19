# Multi-ECU Scaling — From One Engine to Many

> **Purpose:** Document how VDiag's architecture scales from a single ECU to multiple ECUs sharing transports. This is a common system-design interview question: "How would you handle 20 ECUs?"

---

## 1. The single-ECU baseline

Current VDiag:

```
App → DiagCarService → DiagEngine → IDiagnosticHal → Mock/DoIP/CAN
```

One engine, one HAL, one session state machine.

---

## 2. Multi-ECU design

For multiple ECUs, create one engine per ECU:

```
DiagCarService
    ├── DiagEngine[body]        → DoipHal → Body ECU      (TCP 13400)
    ├── DiagEngine[powertrain]  → DoipHal → Powertrain ECU (TCP 13401)
    ├── DiagEngine[adas]        → DoipHal → ADAS ECU       (TCP 13402)
    └── DiagEngine[chassis]     → CanHal  → Chassis ECU    (CAN bus)
```

Each engine has:
- Its own worker thread.
- Its own priority queue.
- Its own session state machine.
- Its own HAL instance.

`DiagCarService` routes requests by `DiagRequest.ecuId`. The client API is unchanged.

---

## 3. Shared transport arbitration

If multiple ECUs share one CAN bus, the transport is the serialization point:

```
DiagEngine[body] ──┐
DiagEngine[brake] ─┼→ CanArbiter → SocketCAN → vcan0
DiagEngine[steer] ─┘
```

`CanArbiter` holds a transport-level mutex and routes frames by CAN ID. Logical concurrency (engine-per-ECU) is preserved; physical bus access is serialized.

---

## 4. Why not one engine with multiple transports?

A single engine would force all ECUs to share one queue and one session state. That couples unrelated ECUs: a slow chassis ECU would delay a body ECU request. Per-ECU engines isolate failure and priority.

---

## 5. Resource limits

- One thread per engine is fine for tens of ECUs.
- Hundreds of ECUs would need a thread pool per transport or async I/O.
- For VDiag's scope, one engine per ECU is the right trade-off.

---

## 6. Interview talking points

> *"To scale VDiag to multiple ECUs, I'd create one DiagEngine per ECU, each with its own queue, session state, and HAL. The service routes by ecuId. If ECUs share a CAN bus, a CanArbiter serializes physical access while keeping logical concurrency. The client API doesn't change."*
