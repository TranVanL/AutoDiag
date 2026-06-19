# VDiag — Architecture Overview

## Layer Diagram

```
┌─────────────────────────────────────────────────┐
│  VDiag App (com.vdiag)        process: main      │
│  ┌─────────────┐                                 │
│  │ MainActivity│ ──bindService()──────────────►  │
│  └─────────────┘                                 │
└─────────────────────────────────────────────────┘
        │ AIDL / Binder IPC
        ▼
┌─────────────────────────────────────────────────┐
│  DiagCarService (com.vdiag:car_service)          │
│  ┌───────────────────┐  ┌──────────────────────┐ │
│  │ DiagCarService    │  │ DiagCarServiceBinder  │ │
│  │  (Service)        │  │  (IDiagCarService.Stub│ │
│  └───────────────────┘  └──────────────────────┘ │
│  ┌───────────────┐  ┌──────────────────────────┐ │
│  │ PermissionGate│  │ ClientRegistry (Day 9)   │ │
│  │  (signature)  │  │  DeathRecipient          │ │
│  └───────────────┘  └──────────────────────────┘ │
└─────────────────────────────────────────────────┘
        │ JNI (Day 11+)
        ▼
┌─────────────────────────────────────────────────┐
│  HAL / Native (C++17)                           │
│  libvdiag_jni.so                                │
│  DiagHalBridge → UDS protocol stack             │
└─────────────────────────────────────────────────┘
        │ Socket / DoIP
        ▼
┌─────────────────────────────────────────────────┐
│  ECU / Vehicle Network (CAN, Ethernet)          │
└─────────────────────────────────────────────────┘
```

## Key Components

| Component | Location | Role |
|---|---|---|
| `IDiagCarService.aidl` | `aidl/com/vdiag/` | AIDL interface — server side |
| `IDiagCallback.aidl` | `aidl/com/vdiag/` | `oneway` async callback |
| `DiagRequest.aidl` | `aidl/com/vdiag/` | Parcelable request object |
| `DiagCarService` | `service/` | Android Service, process `:car_service` |
| `DiagCarServiceBinder` | `service/` | AIDL Stub implementation |
| `PermissionGate` | `service/` | `signature`-level permission check |
| `ClientRegistry` | `service/` *(Day 9)* | DeathRecipient + binder lifecycle |

## Security Model

- Permission: `com.vdiag.permission.DIAGNOSE` — `protectionLevel="signature"`
- Only apps signed with the same key can bind the service
- Every IPC call checks `Binder.getCallingPid/Uid` before processing

## AIDL Call Flow

```
Client                      Server (DiagCarService)
  │                               │
  ├─ getProperty(req, callback) ──►│
  │                               ├─ PermissionGate.enforce()
  │                               ├─ getDummyResponse(propertyId)
  │                               ├─ callback.onResult(id, value, 0)
  │◄─ onResult(id, value, 0) ─────┤
```