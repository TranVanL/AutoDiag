# Transport Diversity — HAL Abstraction Story

> **Day 80 / Week 16** — Boundary 4: `IDiagnosticHal` pure virtual interface.

## TL;DR

VDiag supports **three transport backends** behind a single HAL interface:

| Transport | HAL Class | Protocol | Test env |
|---|---|---|---|
| In-process mock | `MockDiagnosticHal` | None | all envs |
| DoIP (Ethernet) | `DoipDiagnosticHal` | TCP / ISO 13400 | emulator (`adb reverse`) |
| CAN bus | `CanDiagnosticHal` | SocketCAN / ISO-TP | Linux host `vcan0` |

All three implement `IDiagnosticHal`. The framework above the HAL changes **zero lines** when switching transport.

---

## Why this matters

In automotive diagnostics, the same UDS request (`22 F1 90` read VIN) must run against:

- A mocked ECU during unit tests.
- A DoIP simulator when demoing on Android emulator.
- A real CAN bus when validating on target hardware.

Without a clean boundary, every transport switch would leak socket/CAN details into the service layer. `IDiagnosticHal` isolates that complexity.

---

## Class diagram

```text
┌─────────────────────────────────────┐
│         DiagCarService              │
│   (AIDL binder, permission gate)    │
└─────────────┬───────────────────────┘
              │
┌─────────────▼───────────────────────┐
│         DiagHalBridge               │
│      (JNI, caches JVM refs)         │
└─────────────┬───────────────────────┘
              │
┌─────────────▼───────────────────────┐
│         DiagEngine                  │
│   (queue, retry, session state)     │
└─────────────┬───────────────────────┘
              │  IDiagnosticHal
┌─────────────▼───────────────────────┐
│  ┌──────────┬──────────┬─────────┐  │
│  │   Mock   │   DoIP   │   CAN   │  │
│  │ DiagnosticHal│ DiagnosticHal│ DiagnosticHal│  │
│  └──────────┴──────────┴─────────┘  │
└─────────────────────────────────────┘
```

---

## Switch point

`HalFactory::createHal(spec)` is the only place that knows about concrete transports:

```cpp
auto hal = autodiag::HalFactory::createHal("mock");        // unit tests
auto hal = autodiag::HalFactory::createHal("doip:127.0.0.1:13400"); // emulator
auto hal = autodiag::HalFactory::createHal("can:vcan0");   // Linux host / target
```

Everything above `HalFactory` sees only:

```cpp
struct IDiagnosticHal {
    virtual Result SendAndReceive(const std::vector<uint8_t>& req) = 0;
    virtual Result readProperty(uint32_t propId, uint32_t areaId = 0) = 0;
    virtual bool isReady() const = 0;
    virtual void reset() = 0;
    virtual ~IDiagnosticHal() = default;
};
```

---

## Interview story

> "Boundary 4 is `IDiagnosticHal`, a pure virtual interface. `MockDiagnosticHal` gives us deterministic unit tests, `DoipDiagnosticHal` lets us demo on Android emulator over `adb reverse`, and `CanDiagnosticHal` talks to real ECUs over SocketCAN. The framework doesn't change a single line when we swap transports — `HalFactory::create(mode)` is the only switch point. That's Open-Closed Principle applied at the HAL abstraction boundary."

---

## Build notes

- `MockDiagnosticHal` and `DoipDiagnosticHal` build on both host and Android.
- `CanDiagnosticHal` is **Linux host only** because it depends on SocketCAN (`<linux/can.h>`, `PF_CAN`, `SIOCGIFINDEX`).
- On Android, the `can:` spec throws `invalid_argument`; use `mock` or `doip:` instead.

---

## Verification checklist

- [ ] `HalFactory::createHal("mock")` → unit tests pass.
- [ ] `HalFactory::createHal("doip:127.0.0.1:13400")` → emulator demo works.
- [ ] `HalFactory::createHal("can:vcan0")` → Linux host test passes after `sudo modprobe vcan`.
- [ ] Switching `spec` requires no change in `DiagEngine`, `DiagHalBridge`, or `DiagCarService`.
