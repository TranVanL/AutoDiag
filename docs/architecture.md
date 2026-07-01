# VDiag — Architecture Overview

## Layer Diagram

```
┌───────────────────────────────────────────────────────────────┐
│ VDiag App (com.vdiag) - main process                         │
│ MainActivity                                                  │
│   - bindService() to DiagCarService                          │
│   - send DiagRequest (6 properties)                          │
│   - receive IDiagCallback.onResult/onError                   │
└───────────────────────────────────────────────────────────────┘
                                                                        │ AIDL / Binder IPC
                                                                        ▼
┌───────────────────────────────────────────────────────────────┐
│ DiagCarService (com.vdiag:car_service)                       │
│ DiagCarServiceBinder (IDiagCarService.Stub)                  │
│   - PermissionGate.enforce()                                 │
│   - ClientRegistry (register/unregister + DeathRecipient)    │
│   - forward request to JNI bridge                            │
└───────────────────────────────────────────────────────────────┘
                                                                        │ JNI
                                                                        ▼
┌───────────────────────────────────────────────────────────────┐
│ libvdiag_jni.so (C++17)                                      │
│ jni_onload.cpp                                                │
│   - cache JavaVM + callback method IDs                       │
│ jni_bridge.cpp                                                │
│   - nativeInit: create/start DiagEngine(MockDiagnosticHal)   │
│   - nativeGetProperty: build DiagRequest -> engine.submit()  │
│ jni_callback.cpp                                              │
│   - RAII GlobalRef + AttachCurrentThread auto-detach         │
└───────────────────────────────────────────────────────────────┘
                                                                        │
                                                                        ▼
┌───────────────────────────────────────────────────────────────┐
│ HAL Core (standalone CMake)                                  │
│ DiagEngine -> uds_codec -> MockDiagnosticHal                 │
│   - worker thread queue                                      │
│   - UDS encode/decode                                        │
│   - mock DID + DTC data                                      │
└───────────────────────────────────────────────────────────────┘
```

## Key Components

| Component | Location | Role |
|---|---|---|
| IDiagCarService.aidl | android/app/src/main/aidl/com/vdiag/ | AIDL service contract |
| IDiagCallback.aidl | android/app/src/main/aidl/com/vdiag/ | Async callback contract |
| DiagRequest.aidl | android/app/src/main/aidl/com/vdiag/ | Parcelable request object |
| MainActivity | android/app/src/main/java/com/vdiag/ | Client UI and property requests |
| DiagCarService | android/app/src/main/java/com/vdiag/service/ | Service lifecycle and JNI init/shutdown |
| DiagCarServiceBinder | android/app/src/main/java/com/vdiag/service/ | Permission + callback lookup + JNI dispatch |
| PermissionGate | android/app/src/main/java/com/vdiag/service/ | Signature-level enforcement |
| ClientRegistry | android/app/src/main/java/com/vdiag/service/ | Binder death handling and callback registry |
| JNI bridge | android/app/src/main/cpp/ | Java <-> C++ integration layer |
| DiagEngine | hal/src/diag_engine.cpp | Worker queue, submit/shutdown, callback execution |
| MockDiagnosticHal | hal/src/mock_diag_hal.cpp | Deterministic mock responses for 6 properties |
| UDS codec | hal/src/uds_codec.cpp | UDS encode/decode and valueString mapping |

## Property Contract (Day34)

| Property | Request | HAL/Codec expected output |
|---|---|---|
| VIN | DID 0xF190 | VINFAST12345678901 |
| SOC | DID 0x0105 | 78 |
| RPM | DID 0x010C | 3200 |
| SW version | DID 0xF187 | SW_V3.2.1_AAOS |
| DTC list | UDS 0x19, subFunction 0x02 | P0A00, P0562 |
| Clear DTC | UDS 0x14 | OK |

## Security Model

- Permission: com.vdiag.permission.DIAGNOSE with protectionLevel="signature"
- Only apps signed with the same key can bind the service
- Every IPC call enforces permission before processing
- Callback lifecycle is managed via ClientRegistry and binder death recipient

## End-to-End Call Flow

```
MainActivity
        -> IDiagCarService.getProperty(DiagRequest)
DiagCarServiceBinder
        -> PermissionGate.enforce()
        -> lookup callback by caller in ClientRegistry
        -> DiagHalBridge.nativeGetProperty(...)
JNI (jni_bridge.cpp)
        -> map property to UDS request
        -> g_engine->submit(request, lambda)
DiagEngine worker
        -> uds_codec.encode(request)
        -> MockDiagnosticHal.SendAndReceive(frame)
        -> uds_codec.decode(response)
        -> callback lambda (JniCallbackBridge.onResult/onError)
Binder callback
        -> IDiagCallback.onResult/onError on app process
MainActivity
        -> update UI result text
```

## Verification Snapshot

- HAL regression suite: 35 tests pass (uds 20, mock 5, session 5, engine 4)
- Full property path implemented for all 6 properties including Clear DTC
- Android local build still depends on host SDK setup (ANDROID_HOME or local.properties)