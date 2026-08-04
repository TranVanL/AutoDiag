# VDiag Stable AIDL HAL (`com.vdiag.hal`)

This directory contains the **vendor-facing stable AIDL HAL** for the VDiag diagnostic stack.

## Day 61 vs Day 66 — phân biệt rõ

| Day | Mục tiêu | Output |
|-----|----------|--------|
| **Day 61** | Hiểu `@VintfStability` + tạo AIDL files | `com/vdiag/hal/*.aidl` + `Android.bp` |
| **Day 66** | Freeze workflow, snapshot version | `aidl_api/com.vdiag.hal/1/` (formal) hoặc `v1/` (learning artifact) |

## Key points từ Stable AIDL

1. **`@VintfStability`**  
   - Bắt buộc cho mọi interface/parcelable nằm trên vendor partition.  
   - Đảm bảo backward-compatible across OTA; Soong sẽ reject breaking changes sau khi đã freeze.

2. **`aidl_interface` Soong rule** (`Android.bp`)  
   - `vendor: true` → build vào vendor partition.  
   - `stability: "vintf"` → kích hoạt VINTF manifest enforcement.  
   - `frozen: true` + `versions: ["1"]` → version 1 đã đóng băng.

3. **App-layer AIDL vs vendor HAL AIDL**  
   - `IDiagCarService` (app → service) **không cần** `@VintfStability`.  
   - `IDiagnosticHal` (framework → vendor HAL) **bắt buộc** `@VintfStability`.

## Structure

```
android/aidl_hal/
├── com/vdiag/hal/
│   ├── IDiagnosticHal.aidl      # @VintfStability interface
│   ├── DiagHalRequest.aidl      # @VintfStability parcelable
│   └── DiagHalResult.aidl       # @VintfStability parcelable
├── Android.bp                   # aidl_interface Soong rule
└── v1/                          # frozen version 1 snapshot (learning artifact)
    └── com/vdiag/hal/*.aidl
```

> **Note:** Trong production AAOS, snapshot đóng băng chính thức nằm ở `aidl_api/<interface-name>/<version>/` sau khi chạy `m <name>-freeze-api`. Thư mục `v1/` ở đây là bản snapshot đơn giản để minh họa Day 61.

## Story cho interview

> "Production VDiag expose `IDiagnosticHal` qua AIDL HAL versioning — tương tự `IVehicle.aidl`. `@VintfStability` và version freeze cho phép OTA update không break vendor compatibility."
