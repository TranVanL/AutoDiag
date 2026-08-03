# 📚 Day 61 — Stable AIDL HAL + `@VintfStability` Deep Dive

> **Mục tiêu:** Hiểu sâu tại sao Android Automotive HAL bắt buộc dùng Stable AIDL, `@VintfStability` làm gì, `aidl_interface` Soong rule hoạt động thế nào, và cách defend trước senior interviewer.
>
> **Dành cho:** Android Automotive / Framework / HAL / Platform interviews.

---

# Mục lục

1. [Bối cảnh lịch sử: Tại sao cần Stable AIDL?](#1-bối-cảnh-lịch-sử-tại-sao-cần-stable-aidl)
2. [Project Treble và Vendor Interface](#2-project-treble-và-vendor-interface)
3. [AIDL thường vs Stable AIDL](#3-aidl-thường-vs-stable-aidl)
4. [Hiểu sâu `@VintfStability`](#4-hiểu-sâu-vintfStability)
5. [`aidl_interface` Soong build rule](#5-aidl_interface-soong-build-rule)
6. [Cấu trúc thư mục chuẩn](#6-cấu-trúc-thư-mục-chuẩn)
7. [App-layer AIDL vs Vendor HAL AIDL](#7-app-layer-aidl-vs-vendor-hal-aidl)
8. [Tạo stable HAL AIDL từng bước](#8-tạo-stable-hal-aidl-từng-bước)
9. [Parcelable stability chi tiết](#9-parcelable-stability-chi-tiết)
10. [Backward compatibility rules](#10-backward-compatibility-rules)
11. [VINTF manifest và compatibility matrix](#11-vintf-manifest-và-compatibility-matrix)
12. [C++ / Java / NDK backend sinh ra gì?](#12-c--java--ndk-backend-sinh-ra-gì)
13. [Common mistakes & debugging](#13-common-mistakes--debugging)
14. [Hands-on validation (không cần device)](#14-hands-on-validation-không-cần-device)
15. [Mock interview Q&A](#15-mock-interview-qa)
16. [Tóm tắt flashcard](#16-tóm-tắt-flashcard)

---

# 1. Bối cảnh lịch sử: Tại sao cần Stable AIDL?

## 1.1. Android trước Project Treble (Android < 8.0)

Trước đây, Android framework và vendor code (driver, HAL, BSP) được biên dịch cùng nhau trong một bản build monolithic:

```
┌─────────────────────────────────────────┐
│           Android OS Image              │
│  ┌─────────────┐  ┌─────────────────┐   │
│  │  Framework  │──│  HAL / Drivers  │   │
│  │  (Java/C++) │  │  (C/C++)        │   │
│  └─────────────┘  └─────────────────┘   │
│         │                  │            │
│         └────── Binder ────┘            │
└─────────────────────────────────────────┘
```

**Vấn đề:**
- Mỗi lần Google release Android mới (ví dụ Android 9 → 10), OEM phải port lại toàn bộ driver và HAL.
- SoC vendor (Qualcomm, MediaTek, NXP, Samsung…) phải cung cấp BSP mới.
- Quá trình này mất 6-12 tháng → **OTA chậm**, **security patch delay**, **fragmentation cực kỳ nghiêm trọng**.

## 1.2. Project Treble (Android 8.0, 2017)

Google giới thiệu Project Treble để tách biệt framework và vendor:

```
┌─────────────────────────┐
│    System Partition     │
│   /system, /system_ext  │
│  ┌─────────────────┐    │
│  │    Framework    │    │
│  │  (Google/OEM)   │    │
│  └────────┬────────┘    │
└───────────┼─────────────┘
            │ Stable Interface
            │ (HIDL trước, Stable AIDL sau)
┌───────────┼─────────────┐
│    Vendor Partition     │
│       /vendor           │
│  ┌─────────────────┐    │
│  │   HAL / BSP     │    │
│  │ (SoC vendor)    │    │
│  └─────────────────┘    │
└─────────────────────────┘
```

**Ý tưởng cốt lõi:**
- System partition có thể update độc lập qua OTA.
- Vendor partition giữ nguyên, không cần rebuild.
- Hai bên giao tiếp qua một **interface ổn định** được định nghĩa rõ ràng.

## 1.3. HIDL → Stable AIDL

| Giai đoạn | Công nghệ interface |
|---|---|
| Android 8-9 | HIDL (HAL Interface Definition Language) |
| Android 10+ | Stable AIDL (khuyến khích) |
| Android 12+ | Nhiều HAL core chuyển sang AIDL (power, vibrator, sensors…) |
| Android 14+ | Hầu hết HAL mới đều dùng Stable AIDL |

**Tại sao chuyển từ HIDL sang Stable AIDL?**

| Tiêu chí | HIDL | Stable AIDL |
|---|---|---|
| Ngôn ngữ định nghĩa | Riêng (`.hal`) | AIDL quen thuộc (`.aidl`) |
| Tooling | Ít phổ biến hơn | Mạnh, tích hợp sâu Android Studio / Soong |
| Ngôn ngữ generate | C++, Java | C++, Java, NDK, Rust, Java (nhiều backend) |
| Learning curve | Cao hơn | Thấp hơn vì AIDL đã quen trong app dev |
| Ecosystem | Thu hẹp dần | Đang mở rộng |

Google muốn **một ngôn ngữ duy nhất** cho mọi IPC trong Android: app-to-service, framework-to-HAL, HAL-to-HAL.

---

# 2. Project Treble và Vendor Interface

## 2.1. Các partition chính trong Android

| Partition | Mount point | Nội dung | Update frequency |
|---|---|---|---|
| Boot | `/boot` | Kernel, ramdisk | Thấp |
| System | `/system` | Framework, system apps, libraries | Cao (OTA Google/OEM) |
| System_ext | `/system_ext` | OEM framework extensions | Cao |
| Product | `/product` | Product-specific apps/config | Cao |
| Vendor | `/vendor` | HALs, vendor libraries, BSP | Thấp (SoC vendor) |
| ODM | `/odm` | Device-specific customization | Thấp |

## 2.2. Treble contract

Treble định nghĩa một **contract** giữa system và vendor:

```
System partition (framework)  ──►  Vendor Interface  ◄──  Vendor partition (HAL)
```

Vendor Interface bao gồm:
1. **HAL interfaces** (Stable AIDL / HIDL).
2. **Kernel interfaces** (LKM, syscalls, device nodes).
3. **VINTF manifest** (XML mô tả HAL/version vendor cung cấp).
4. **Compatibility matrix** (XML mô tả framework yêu cầu gì).

## 2.3. VINTF (Vendor Interface)

VINTF là một khái niệm tổng hợp, bao gồm:
- **Vendor manifest**: HAL nào vendor có, version bao nhiêu.
- **Framework manifest**: HAL nào framework cung cấp.
- **Compatibility matrix**: Framework cần vendor cung cấp gì; vendor cần framework cung cấp gì.

At boot, `vintf` object (được parse từ XML) được kiểm tra:
```
framework_matrix.xml  MUST_BE_SATISFIED_BY  device_manifest.xml
vendor_matrix.xml     MUST_BE_SATISFIED_BY  framework_manifest.xml
```
Nếu không khớp → boot failure.

---

# 3. AIDL thường vs Stable AIDL

## 3.1. AIDL thường (App-layer)

Khi bạn viết AIDL trong Android app:

```aidl
// app/src/main/aidl/com/vdiag/IDiagCarService.aidl
package com.vdiag;

interface IDiagCarService {
    void getProperty(in DiagRequest req, IDiagCallback callback);
}
```

Gradle AIDL plugin sẽ:
1. Parse file `.aidl`.
2. Sinh `IDiagCarService.java` chứa `Stub` (server) và `Proxy` (client).
3. Method được gán `transaction code` theo thứ tự khai báo.

**Vấn đề của AIDL thường khi dùng cho HAL:**
- Method order quyết định transaction code.
- Nếu bạn thêm method ở giữa, tất cả method sau đó đổi transaction code.
- Parcelable không có schema cố định.
- Không có version freeze.
- Không có VINTF integration.

## 3.2. Stable AIDL

Stable AIDL là AIDL được mở rộng để đảm bảo:

1. **Binary ABI stability**: Interface và parcelable có layout cố định.
2. **Version freeze**: Mỗi version được snapshot trong `aidl_api/`.
3. **VINTF integration**: Tự động xuất hiện trong manifest/matrix.
4. **Multi-language backend**: C++, Java, NDK, Rust.
5. **Backward compatibility check**: Build system kiểm tra breaking changes.

## 3.3. Sự khác biệt ở mức compiler

| Khía cạnh | AIDL thường | Stable AIDL |
|---|---|---|
| Transaction code | Theo thứ tự khai báo, dễ đổi | Cố định theo frozen version |
| Parcelable | Flexible, dễ thay đổi | Stable layout, field order quan trọng |
| Type restrictions | Ít hạn chế | Hạn chế nhiều (không Object, không Serializable…) |
| Build module | Gradle `aidl` | Soong `aidl_interface` |
| Versioning | Không bắt buộc | Bắt buộc freeze |

---

# 4. Hiểu sâu `@VintfStability`

## 4.1. Định nghĩa chính xác

`@VintfStability` là một **AIDL annotation** được định nghĩa trong AOSP. Khi đặt trước một `interface` hoặc `parcelable`, nó báo hiệu:

> *"Type này là một phần của Vendor Interface (VINTF). Nó phải duy trì binary stability across OTA updates. Mọi thay đổi phải backward-compatible."*

## 4.2. Cú pháp

```aidl
@VintfStability
interface IDiagnosticHal {
    DiagHalResult sendAndReceive(in DiagHalRequest req);
    boolean isReady();
    void reset();
}

@VintfStability
parcelable DiagHalRequest {
    int requestId;
    int serviceId;
    int did;
    byte[] payload;
}
```

## 4.3. `@VintfStability` enforce gì?

### 4.3.1. Ở compile time

Khi compiler thấy `@VintfStability`, nó:

1. **Kiểm tra type eligibility**:
   - Chỉ cho phép primitive types: `boolean`, `byte`, `char`, `int`, `long`, `float`, `double`.
   - `String`.
   - `IBinder` (có điều kiện).
   - `ParcelFileDescriptor` (có điều kiện).
   - Other `@VintfStability` parcelable/enum/interface.
   - Arrays (`T[]`) hoặc `List<T>` của các type trên.

2. **Từ chối các type không stable**:
   - `Object`
   - `Serializable`
   - `Parcelable` không có `@VintfStability`
   - Java generic phức tạp (`Map<K, V>` không được support trong stable AIDL)

3. **Yêu cầu freeze**:
   - Nếu dùng `aidl_interface` với `stability: "vintf"`, build sẽ fail nếu chưa có `aidl_api/<name>/<version>/`.

### 4.3.2. Ở runtime

Stable AIDL sinh ra code có đặc điểm:
- Transaction code cố định.
- Parcelable serialization/deserialization theo schema đã định nghĩa.
- `android::binder::StableAIDL` C++ namespace.

## 4.4. `@VintfStability` vs `stability: "vintf"` trong Android.bp

Có hai cách để khai báo stability:

**Cách 1: Annotation trong AIDL file**
```aidl
@VintfStability
interface IDiagnosticHal { ... }
```

**Cách 2: Field trong Android.bp**
```bp
aidl_interface {
    name: "com.vdiag.hal",
    stability: "vintf",
    ...
}
```

**Sự khác biệt:**
- Annotation `@VintfStability` áp dụng cho từng type riêng lẻ.
- `stability: "vintf"` trong `aidl_interface` áp dụng cho **toàn bộ interface trong module đó**, tương đương với việc tất cả đều có `@VintfStability`.

Trong thực tế, người ta thường dùng cả hai hoặc chỉ dùng `stability: "vintf"` trong Android.bp để đơn giản.

## 4.5. Các annotation khác trong stable AIDL

| Annotation | Ý nghĩa |
|---|---|
| `@VintfStability` | Type thuộc VINTF, phải stable. |
| `@Backing(type="byte")` | Chỉ định kiểu số nguyên backing cho enum. |
| `@JavaDerive(toString=true, equals=true)` | Sinh thêm Java helper methods. |
| `@RustDerive(Clone)` | Sinh thêm Rust derive macro. |
| `@UnsupportedAppUsage` | Không liên quan HAL; đánh dấu framework internal API. |

## 4.6. Tại sao phải đặt `@VintfStability` ở cả interface lẫn parcelable?

Vì một interface stable chỉ ổn định nếu **tất cả types nó dùng cũng stable**. Nếu `IDiagnosticHal` dùng `DiagHalRequest` mà `DiagHalRequest` không stable, toàn bộ contract bị phá vỡ.

---

# 5. `aidl_interface` Soong build rule

## 5.1. Soong là gì?

Soong là build system của Android, thay thế GNU Make từ Android 7+. Nó dùng:
- **Blueprint files** (`Android.bp`) định nghĩa module.
- **Ninja** làm backend để thực thi build.

Ví dụ module types phổ biến:
- `cc_binary`, `cc_library`
- `java_library`, `android_app`
- `aidl_interface`
- `hidl_interface`

## 5.2. Tại sao cần module type `aidl_interface`?

`aidl_interface` không chỉ là wrapper quanh AIDL compiler. Nó là một module type đặc biệt vì:

1. **Quản lý version freeze**: Tự động tạo/check `aidl_api/`.
2. **Multi-backend**: Sinh code C++, Java, NDK, Rust từ cùng một AIDL source.
3. **VINTF metadata**: Tạo manifest entries.
4. **Dependency management**: Module khác có thể `deps: ["com.vdiag.hal"]`.
5. **Backward compatibility check**: `m <name>-check-api`.

## 5.3. Android.bp đầy đủ cho VDiag HAL

```bp
// android/aidl_hal/Android.bp
aidl_interface {
    name: "com.vdiag.hal",
    vendor: true,
    srcs: ["com/vdiag/hal/*.aidl"],
    stability: "vintf",
    owner: "vdiag",
    backend: {
        cpp: {
            enabled: true,
            gen_log: true,
        },
        java: {
            enabled: true,
            sdk_version: "module_current",
        },
        ndk: {
            enabled: true,
        },
    },
    versions_with_info: [
        {
            version: "1",
        },
    ],
}
```

## 5.4. Giải thích từng field

### `name`
- Tên module duy nhất trong toàn bộ AOSP build tree.
- Dùng để reference từ module khác: `deps: ["com.vdiag.hal"]`.

### `vendor: true`
- Báo hiệu module này thuộc vendor partition.
- Vendor module không được phụ thuộc vào system-only API.
- HAL service binary sẽ được cài vào `/vendor/bin/hw/`.

### `srcs`
- Glob pattern hoặc danh sách file AIDL source.
- Tất cả file trong pattern phải thuộc cùng một package.

### `stability`
- `"vintf"`: stable, dùng cho VINTF HAL.
- `""` hoặc không set: unstable (internal use).
- `"system"`: stable nhưng chỉ trong system partition (ít dùng).

### `backend`
- `cpp`: Sinh C++ headers/source dùng `libbinder`.
- `java`: Sinh Java classes dùng trong framework/app.
- `ndk`: Sinh C++ NDK wrapper để app native code dùng.
- `rust`: Sinh Rust bindings.

### `versions_with_info`
- Liệt kê các version đã freeze.
- Mỗi version tương ứng với thư mục `aidl_api/<name>/<version>/`.
- Vendor build có thể pin một version cụ thể.

## 5.5. Các target build hữu ích

```bash
# Build module
m com.vdiag.hal

# Update current/ snapshot từ srcs/
m com.vdiag.hal-update-api

# Freeze version mới
m com.vdiag.hal-freeze-api

# Check backward compatibility
m com.vdiag.hal-check-api

# Clean
m com.vdiag.hal-clean
```

---

# 6. Cấu trúc thư mục chuẩn

## 6.1. Layout đầy đủ

```
android/aidl_hal/
├── Android.bp                              # Soong module definition
├── com/
│   └── vdiag/
│       └── hal/
│           ├── IDiagnosticHal.aidl         # Main HAL interface
│           ├── DiagHalRequest.aidl         # Request parcelable
│           └── DiagHalResult.aidl          # Result parcelable
└── aidl_api/
    └── com.vdiag.hal/                      # Module name
        ├── current/                        # Work-in-progress (mutable)
        │   └── com/
        │       └── vdiag/
        │           └── hal/
        │               ├── IDiagnosticHal.aidl
        │               ├── DiagHalRequest.aidl
        │               └── DiagHalResult.aidl
        └── 1/                              # Frozen version 1 (immutable)
            └── com/
                └── vdiag/
                    └── hal/
                        ├── IDiagnosticHal.aidl
                        ├── DiagHalRequest.aidl
                        └── DiagHalResult.aidl
```

## 6.2. Ý nghĩa của `current/`

- `current/` là bản sao của AIDL source tại thời điểm gần nhất chạy `update-api`.
- Nó đại diện cho **work-in-progress** version.
- Framework build dùng `current/`.
- Khi bạn sửa AIDL source, bạn phải chạy `update-api` để `current/` đồng bộ.

## 6.3. Ý nghĩa của `1/`

- `1/` là **frozen snapshot**.
- Một khi đã freeze, **không được sửa** nội dung file trong `1/`.
- Vendor build có thể pin version 1.
- Đảm bảo rằng vendor code đã ship sẽ luôn compile với interface version 1.

## 6.4. Quy trình phát triển qua các version

```
Phase 1: Phát triển v1
  srcs/ ──update-api──► current/
  current/ ──freeze──► 1/
  Update Android.bp: versions_with_info: [{version: "1"}]

Phase 2: Phát triển v2 (thêm method mới)
  Sửa srcs/ (thêm method ở cuối interface)
  srcs/ ──update-api──► current/
  current/ ──freeze──► 2/
  Update Android.bp: versions_with_info: [{version: "1"}, {version: "2"}]

Phase 3: Vendor cũ vẫn dùng v1, framework mới dùng v2
  Framework gọi method mới → vendor v1 không implement → framework phải handle gracefully.
```

---

# 7. App-layer AIDL vs Vendor HAL AIDL

Đây là phần **quan trọng nhất** để phân biệt trong interview.

## 7.1. App-layer AIDL

```aidl
// app/src/main/aidl/com/vdiag/IDiagCarService.aidl
package com.vdiag;

interface IDiagCarService {
    void getProperty(in DiagRequest req, IDiagCallback callback);
}
```

| Đặc điểm | Giá trị |
|---|---|
| **Stability** | Không cần `@VintfStability` |
| **Scope** | Chỉ trong cùng một app hoặc giữa các app cùng sign |
| **Build** | Gradle AIDL plugin |
| **Versioning** | Không bắt buộc freeze |
| **Partition** | `/data/app` |
| **Deploy** | Cùng APK, client & server update cùng lúc |
| **VINTF** | Không liên quan |

## 7.2. Vendor HAL AIDL

```aidl
// android/aidl_hal/com/vdiag/hal/IDiagnosticHal.aidl
package com.vdiag.hal;

import com.vdiag.hal.DiagHalRequest;
import com.vdiag.hal.DiagHalResult;

@VintfStability
interface IDiagnosticHal {
    DiagHalResult sendAndReceive(in DiagHalRequest req);
    boolean isReady();
    void reset();
}
```

| Đặc điểm | Giá trị |
|---|---|
| **Stability** | **Bắt buộc** `@VintfStability` hoặc `stability: "vintf"` |
| **Scope** | Giao tiếp giữa framework (system) và vendor HAL |
| **Build** | Soong `aidl_interface` |
| **Versioning** | **Bắt buộc freeze** trong `aidl_api/` |
| **Partition** | `/vendor/bin/hw/` |
| **Deploy** | Framework và vendor có thể update độc lập |
| **VINTF** | Bắt buộc có manifest và compatibility matrix |

## 7.3. So sánh trực quan

```
┌─────────────────────────────────────────────────────────────┐
│                        APP LAYER                            │
│  ┌─────────────┐      AIDL thường      ┌─────────────┐      │
│  │  Activity   │◄─────────────────────►│   Service   │      │
│  │  (client)   │   same APK / process  │   (server)  │      │
│  └─────────────┘                       └─────────────┘      │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      FRAMEWORK / HAL                        │
│  ┌─────────────────┐    Stable AIDL    ┌─────────────────┐  │
│  │    Framework    │◄─────────────────►│   Vendor HAL    │  │
│  │  (/system)      │   cross-partition │  (/vendor)      │  │
│  └─────────────────┘                   └─────────────────┘  │
│         │                                       │            │
│         └────────── VINTF manifest ─────────────┘            │
└─────────────────────────────────────────────────────────────┘
```

## 7.4. Tại sao app-layer không cần stable?

Vì **client và server được deploy cùng lúc** trong cùng một APK. Khi user update app từ Play Store:
- Client code mới được cài.
- Service code mới được cài.
- Cả hai luôn khớp nhau.

Không có tình huống "client mới chạy với server cũ".

Ngược lại, với HAL:
- Framework mới (system OTA) có thể chạy với vendor HAL cũ (chưa update).
- Vendor HAL mới có thể chạy với framework cũ.
- Nên interface phải là contract ổn định.

---

# 8. Tạo stable HAL AIDL từng bước

## 8.1. Bước 1: Thiết kế interface

Trước khi viết code, hãy trả lời:
- HAL này expose những operation gì?
- Input/output là gì?
- Có cần callback không?
- Có stateful không?

Với VDiag HAL:
- `sendAndReceive`: gửi UDS request, nhận response.
- `isReady`: kiểm tra HAL sẵn sàng.
- `reset`: reset HAL state.

## 8.2. Bước 2: Viết AIDL files

### `IDiagnosticHal.aidl`

```aidl
package com.vdiag.hal;

import com.vdiag.hal.DiagHalRequest;
import com.vdiag.hal.DiagHalResult;

@VintfStability
interface IDiagnosticHal {
    /**
     * Send a diagnostic request to the vehicle bus and return the response.
     *
     * @param req The diagnostic request containing service ID, DID, and payload.
     * @return The diagnostic result with success flag, data, or error info.
     */
    DiagHalResult sendAndReceive(in DiagHalRequest req);

    /**
     * Returns true if the HAL is initialized and ready to accept requests.
     */
    boolean isReady();

    /**
     * Reset HAL internal state (e.g., after a transport error).
     */
    void reset();
}
```

### `DiagHalRequest.aidl`

```aidl
package com.vdiag.hal;

@VintfStability
parcelable DiagHalRequest {
    /**
     * Unique request ID used to correlate async responses.
     */
    int requestId;

    /**
     * UDS service identifier, e.g. 0x22 (ReadDataByIdentifier).
     */
    int serviceId;

    /**
     * Data Identifier (DID), e.g. 0xF190 (VIN).
     */
    int did;

    /**
     * Optional raw payload bytes.
     */
    byte[] payload;
}
```

### `DiagHalResult.aidl`

```aidl
package com.vdiag.hal;

@VintfStability
parcelable DiagHalResult {
    /**
     * True if the diagnostic request succeeded.
     */
    boolean success;

    /**
     * Negative Response Code (NRC) when success is false.
     * Meaningful only when success == false.
     */
    int nrc;

    /**
     * Raw response data bytes.
     */
    byte[] data;

    /**
     * Human-readable error message when success is false.
     */
    String errorMessage;
}
```

## 8.3. Bước 3: Viết Android.bp

```bp
aidl_interface {
    name: "com.vdiag.hal",
    vendor: true,
    srcs: ["com/vdiag/hal/*.aidl"],
    stability: "vintf",
    backend: {
        cpp: { enabled: true },
        java: { enabled: true },
        ndk: { enabled: true },
    },
}
```

## 8.4. Bước 4: Build lần đầu và update current/

```bash
source build/envsetup.sh
lunch aosp_arm64-eng    # hoặc target tương ứng
m com.vdiag.hal-update-api
```

Sau lệnh này, thư mục `aidl_api/com.vdiag.hal/current/` được tạo.

## 8.5. Bước 5: Freeze version 1

```bash
m com.vdiag.hal-freeze-api
```

Hoặc thủ công:
```bash
cp -r aidl_api/com.vdiag.hal/current aidl_api/com.vdiag.hal/1
```

Sau đó update `Android.bp`:
```bp
versions_with_info: [
    {
        version: "1",
    },
],
```

## 8.6. Bước 6: Verify backward compatibility

```bash
m com.vdiag.hal-check-api
```

Nếu pass, bạn đã có stable HAL v1.

---

# 9. Parcelable stability chi tiết

## 9.1. Parcelable trong AIDL là gì?

Parcelable là cách Android truyền complex data qua Binder. Nó gồm 2 phần:
1. **Write to parcel**: Serialize object thành byte stream.
2. **Read from parcel**: Deserialize byte stream thành object.

## 9.2. Tại sao parcelable cần stable?

Khi framework (mới) gửi `DiagHalRequest` cho vendor HAL (cũ):
- Framework viết parcel theo schema mới.
- Vendor HAL đọc parcel theo schema cũ.
- Nếu schema không compatible → đọc sai dữ liệu → crash hoặc silent corruption.

## 9.3. Stable parcelable rules

### 9.3.1. Field order

Field order trong parcelable quyết định serialization order. **Không được đổi thứ tự field đã freeze.**

```aidl
// v1
parcelable DiagHalResult {
    boolean success;    // field 1
    int nrc;            // field 2
    byte[] data;        // field 3
    String errorMessage;// field 4
}

// v2 - ĐÚNG: thêm field ở cuối
parcelable DiagHalResult {
    boolean success;
    int nrc;
    byte[] data;
    String errorMessage;
    long timestampNs;   // field 5 mới, có default
}

// v2 - SAI: thêm field ở giữa
parcelable DiagHalResult {
    boolean success;
    int nrc;
    long timestampNs;   // ❌ SAI! Làm đổi order field 3,4
    byte[] data;
    String errorMessage;
}
```

### 9.3.2. Default values

Khi stable AIDL compiler đọc parcel từ version cũ hơn, field mới phải có giá trị mặc định:

```aidl
parcelable DiagHalResult {
    boolean success;
    int nrc;
    byte[] data;
    String errorMessage;
    long timestampNs = 0;   // default value
}
```

### 9.3.3. Type restrictions

Các type được phép trong stable parcelable:

| Type | Cho phép? |
|---|---|
| `boolean`, `byte`, `char`, `int`, `long`, `float`, `double` | ✅ |
| `String` | ✅ |
| `byte[]`, `int[]`, … | ✅ |
| `List<T>` (T là type hợp lệ) | ✅ |
| `T[]` | ✅ |
| Other `@VintfStability` parcelable | ✅ |
| `@VintfStability` enum | ✅ |
| `IBinder` | ✅ (có điều kiện) |
| `ParcelFileDescriptor` | ✅ (có điều kiện) |
| `Object` | ❌ |
| `Serializable` | ❌ |
| `Parcelable` không stable | ❌ |
| `Map<K,V>` | ❌ |

## 9.4. Enum stability

```aidl
@VintfStability
enum DiagHalStatus {
    OK = 0,
    ERROR = 1,
    TIMEOUT = 2,
}
```

Rules:
- Thêm value mới ở cuối: ✅
- Xóa value: ❌
- Đổi numeric value của existing: ❌
- Nên dùng `@Backing(type="byte")` để kiểm soát kích thước.

---

# 10. Backward compatibility rules

## 10.1. Interface methods

| Thao tác | Cho phép? | Lý do chi tiết |
|---|---|---|
| Thêm method mới ở **cuối** interface | ✅ Yes | Transaction ID mới, không ảnh hưởng method cũ. Vendor cũ ignore method mới. |
| Xóa method | ❌ No | Vendor cũ vẫn implement method đó. Framework mới không tìm thấy → crash. |
| Đổi tên method | ❌ No | Tên là metadata; frozen snapshot không khớp. |
| Đổi thứ tự method | ❌ No | Transaction ID thay đổi. |
| Thêm parameter vào method existing | ❌ No | Signature thay đổi, ABI thay đổi. |
| Đổi return type | ❌ No | ABI thay đổi. |
| Đổi `oneway` / non-`oneway` | ❌ No | Semantics thay đổi. |

## 10.2. Parcelable fields

| Thao tác | Cho phép? | Lý do chi tiết |
|---|---|---|
| Thêm field ở cuối với default | ✅ Yes | Old reader đọc ít field, bỏ qua phần còn lại. New reader lấy default cho field thiếu. |
| Xóa field | ❌ No | Serialization order thay đổi. |
| Đổi thứ tự field | ❌ No | Serialization order thay đổi. |
| Đổi kiểu field | ❌ No | ABI thay đổi. |
| Đổi tên field | ⚠️ Khuyến khích không | Metadata không khớp frozen snapshot. |

## 10.3. Enum values

| Thao tác | Cho phép? |
|---|---|
| Thêm value ở cuối | ✅ |
| Xóa value | ❌ |
| Đổi numeric value | ❌ |
| Đổi tên value | ❌ |

## 10.4. Tình huống thực tế

**Câu hỏi:** Bạn đã ship v1 với 3 method. Giờ muốn thêm method `readProperty(int propId)`.

**Cách đúng:**
1. Thêm `readProperty(int propId)` vào **cuối** `IDiagnosticHal.aidl` trong `srcs/`.
2. Chạy `m com.vdiag.hal-update-api`.
3. Chạy `m com.vdiag.hal-check-api` để verify vẫn compatible với v1.
4. Freeze v2: `m com.vdiag.hal-freeze-api`.
5. Update `versions_with_info` thêm `{version: "2"}`.
6. Vendor muốn dùng feature mới thì update lên v2; vendor cũ vẫn dùng v1.

---

# 11. VINTF manifest và compatibility matrix

## 11.1. VINTF manifest là gì?

VINTF manifest là file XML mô tả:
- HAL nào được cung cấp.
- Version nào.
- Interface FQ name.
- Instance name (thường là `default`).

## 11.2. Vendor manifest cho VDiag

```xml
<!-- android/bringup/vintf/manifest_vdiag.xml -->
<?xml version="1.0" encoding="UTF-8"?>
<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>android.hardware.vdiag</name>
        <version>1</version>
        <fqname>IDiagnosticHal/default</fqname>
    </hal>
</manifest>
```

Giải thích:
- `<hal format="aidl">`: HAL dùng AIDL, không phải HIDL.
- `<name>`: Package name của HAL.
- `<version>1</version>`: Version đã freeze.
- `<fqname>`: Fully qualified name `I<InterfaceName>/<instance>`.

## 11.3. Framework compatibility matrix

```xml
<!-- framework compatibility matrix snippet -->
<compatibility-matrix version="1.0" type="framework">
    <hal format="aidl">
        <name>android.hardware.vdiag</name>
        <version>1</version>
        <interface>
            <name>IDiagnosticHal</name>
            <instance>default</instance>
        </interface>
    </hal>
</compatibility-matrix>
```

## 11.4. Quá trình kiểm tra at boot

```
Bootloader → Kernel → init → vintf verification

1. Parse vendor manifest
2. Parse framework compatibility matrix
3. Check: framework_matrix.requires  ⊆  vendor_manifest.provides
4. Check: vendor_matrix.requires     ⊆  framework_manifest.provides
5. Nếu OK → tiếp tục boot
6. Nếu FAIL → boot failure / rollback
```

## 11.5. `@VintfStability` liên hệ VINTF như thế nào?

`@VintfStability` nói với build system:
> "Interface này là một phần của VINTF. Hãy đưa nó vào VINTF manifest và enforce freeze."

Nếu không có `@VintfStability`:
- AIDL interface vẫn hoạt động trong cùng một build.
- Không xuất hiện trong VINTF manifest.
- Không được Treble bảo vệ.
- Không thể dùng làm HAL interface chính thức.

---

# 12. C++ / Java / NDK backend sinh ra gì?

## 12.1. C++ backend

Sinh ra các file như:
```
out/soong/.../com.vdiag.hal-cpp/android/hardware/vdiag/IDiagnosticHal.h
out/soong/.../com.vdiag.hal-cpp/android/hardware/vdiag/BnDiagnosticHal.h
out/soong/.../com.vdiag.hal-cpp/android/hardware/vdiag/BpDiagnosticHal.h
```

Server implementation:
```cpp
#include <android/hardware/vdiag/IDiagnosticHal.h>

using namespace android::hardware::vdiag;

class DiagnosticHal : public BnDiagnosticHal {
public:
    ::android::binder::Status sendAndReceive(
        const DiagHalRequest& req,
        DiagHalResult* _aidl_return) override {
        // implementation
        *_aidl_return = DiagHalResult{true, 0, {}, ""};
        return ::android::binder::Status::ok();
    }

    ::android::binder::Status isReady(bool* _aidl_return) override {
        *_aidl_return = true;
        return ::android::binder::Status::ok();
    }

    ::android::binder::Status reset() override {
        return ::android::binder::Status::ok();
    }
};
```

## 12.2. Java backend

Sinh ra:
```
out/soong/.../com.vdiag.hal-java/com/vdiag/hal/IDiagnosticHal.java
```

Framework client:
```java
import com.vdiag.hal.IDiagnosticHal;
import com.vdiag.hal.DiagHalRequest;
import com.vdiag.hal.DiagHalResult;

IDiagnosticHal hal = IDiagnosticHal.Stub.asInterface(binder);
DiagHalRequest req = new DiagHalRequest();
req.requestId = 1;
req.serviceId = 0x22;
req.did = 0xF190;
req.payload = new byte[0];

DiagHalResult result = hal.sendAndReceive(req);
```

## 12.3. NDK backend

Dùng cho native code trong app hoặc HAL:
```cpp
#include <aidl/com/vdiag/hal/IDiagnosticHal.h>

using aidl::com::vdiag::hal::IDiagnosticHal;
using aidl::com::vdiag::hal::DiagHalRequest;
using aidl::com::vdiag::hal::DiagHalResult;

std::shared_ptr<IDiagnosticHal> hal = ...;
DiagHalRequest req{1, 0x22, 0xF190, {}};
DiagHalResult result;
hal->sendAndReceive(req, &result);
```

---

# 13. Common mistakes & debugging

## 13.1. Lỗi thường gặp

### Lỗi 1: Quên `@VintfStability`

```
ERROR: com.vdiag.hal uses stability "vintf" but IDiagnosticHal.aidl is not annotated with @VintfStability
```

**Fix:** Thêm `@VintfStability` vào interface và tất cả parcelable/enum nó dùng.

### Lỗi 2: Type không stable

```
ERROR: DiagHalRequest.aidl:5: 'Object' is not a stable type
```

**Fix:** Thay `Object` bằng concrete stable type.

### Lỗi 3: Chưa freeze version

```
ERROR: aidl_interface com.vdiag.hal has no frozen version in aidl_api/
```

**Fix:** Chạy `m com.vdiag.hal-freeze-api`.

### Lỗi 4: Breaking change

```
ERROR: com.vdiag.hal current is not compatible with version 1:
  Method 'sendAndReceive' removed or signature changed
```

**Fix:** Không xóa/đổi method cũ. Thêm method mới ở cuối.

### Lỗi 5: VINTF mismatch at boot

```
E VintfObject: Compatibility matrix not satisfied
E VintfObject: Required HAL android.hardware.vdiag@1::IDiagnosticHal/default not available
```

**Fix:** Kiểm tra vendor manifest có declare đúng HAL/version/instance không.

## 13.2. Debug checklist

```markdown
- [ ] Tất cả AIDL files có @VintfStability
- [ ] Android.bp có stability: "vintf"
- [ ] Android.bp có vendor: true
- [ ] aidl_api/<name>/<version>/ tồn tại
- [ ] versions_with_info liệt kê đúng version
- [ ] VINTF manifest declare đúng version và fqname
- [ ] Compatibility matrix yêu cầu đúng version
- [ ] HAL service đăng ký với ServiceManager đúng instance name
```

---

# 14. Hands-on validation (không cần device)

## 14.1. Yêu cầu

- AOSP source tree (hoặc minimal tree với Soong).
- Hoặc chỉ cần Linux host để validate syntax/cấu trúc.

## 14.2. Validate cấu trúc file (không cần build)

Kiểm tra thủ công:
```bash
cd android/aidl_hal

# 1. Kiểm tra annotation
grep -R "@VintfStability" com/vdiag/hal/

# 2. Kiểm tra Android.bp
cat Android.bp | grep -E "stability|vendor|versions_with_info"

# 3. Kiểm tra aidl_api tồn tại
ls -la aidl_api/com.vdiag.hal/

# 4. So sánh current/ và 1/
diff -r aidl_api/com.vdiag.hal/current aidl_api/com.vdiag.hal/1
```

## 14.3. Validate bằng AOSP build

```bash
# Setup build environment
source build/envsetup.sh
lunch aosp_arm64-eng

# Update current/ snapshot
m com.vdiag.hal-update-api

# Check backward compatibility
m com.vdiag.hal-check-api

# Build module
m com.vdiag.hal

# Freeze new version (nếu cần)
m com.vdiag.hal-freeze-api
```

## 14.4. Validate VINTF manifest

```bash
# Trên device hoặc emulator có AOSP build
adb shell cat /vendor/etc/vintf/manifest.xml | grep vdiag
adb shell cat /system/etc/vintf/compatibility_matrix.xml | grep vdiag

# Kiểm tra VINTF object
adb shell vintf --dump
```

## 14.5. Self-check list

```markdown
- [ ] IDiagnosticHal.aidl có @VintfStability
- [ ] DiagHalRequest.aidl có @VintfStability
- [ ] DiagHalResult.aidl có @VintfStability
- [ ] Android.bp dùng aidl_interface
- [ ] Android.bp có stability: "vintf"
- [ ] Android.bp có vendor: true
- [ ] aidl_api/com.vdiag.hal/1/ đã freeze
- [ ] VINTF manifest declare <hal format="aidl"> version 1
- [ ] m com.vdiag.hal-check-api pass
- [ ] Hiểu được tại sao app-layer AIDL không cần stable
```

---

# 15. Mock interview Q&A

## Q1: "Tại sao HAL AIDL cần `@VintfStability` mà app AIDL thì không?"

**A:**
> "Vì HAL AIDL là boundary giữa system partition và vendor partition theo Project Treble. Sau OTA, framework mới có thể chạy với vendor HAL cũ. `@VintfStability` đảm bảo binary ABI ổn định qua OTA. App AIDL thì client và server cùng nằm trong APK, deploy cùng lúc, nên không cần cross-version stability."

## Q2: "Làm sao enforce backward compatibility trong stable AIDL?"

**A:**
> "Có 3 cơ chế chính. Thứ nhất, `aidl_interface` Soong rule với `stability: "vintf"`. Thứ hai, `aidl_api/<name>/<version>/` là frozen snapshot, vendor build chỉ dùng snapshot này. Thứ ba, `m <name>-check-api` so sánh `current/` với frozen version, báo lỗi nếu xóa method, đổi signature, hoặc đổi field order."

## Q3: "Nếu tôi muốn thêm feature mới vào HAL, quy trình là gì?"

**A:**
> "Tôi sẽ thêm method mới vào cuối interface trong `current/`, hoặc thêm field vào cuối parcelable với default value. Sau đó build và test với framework mới. Khi sẵn sàng release, tôi freeze version mới bằng `m <name>-freeze-api`, update `versions_with_info`, và update VINTF manifest + compatibility matrix. Vendor cũ vẫn dùng version cũ, vendor mới có thể dùng version mới."

## Q4: "Đổi tên method có được không?"

**A:**
> "Không. Đổi tên method trong AIDL source không chỉ là đổi tên — nó thay đổi metadata và có thể thay đổi transaction ID nếu method order thay đổi. Ngay cả khi giữ nguyên order, stable AIDL vẫn coi đây là breaking change vì frozen snapshot không khớp. Cách đúng là thêm method mới, giữ method cũ, rồi deprecate dần."

## Q5: "Parcelable thêm field như thế nào cho đúng?"

**A:**
> "Chỉ được thêm field ở cuối parcelable, và phải có default value. Khi old reader đọc parcel từ new writer, nó đọc đúng số field nó biết và bỏ qua phần còn lại. Khi new reader đọc parcel từ old writer, field mới sẽ lấy default value. Nếu thêm ở giữa hoặc xóa field, serialization order bị phá vỡ."

## Q6: "VINTF manifest check fail khi nào?"

**A:**
> "Khi framework compatibility matrix yêu cầu HAL version X nhưng vendor manifest chỉ declare version Y, hoặc interface FQ name không khớp. Ví dụ framework cần `IDiagnosticHal/default` version 2 nhưng vendor chỉ có version 1. Boot sẽ fail vì Treble không đảm bảo interface hoạt động đúng."

## Q7: "Sự khác biệt giữa HIDL và Stable AIDL?"

**A:**
> "HIDL là ngôn ngữ/interface definition riêng của Treble trước Android 10. Stable AIDL là AIDL thông thường được mở rộng để support stability, cho phép dùng một ngôn ngữ duy nhất cho cả app, framework, và HAL. Google khuyến khích chuyến từ HIDL sang AIDL vì đơn giản hóa codebase và tooling."

## Q8: "Tại sao `aidl_interface` cần `vendor: true`?"

**A:**
> "`vendor: true` báo hiệu module thuộc vendor partition. Điều này ảnh hưởng đến output path, dependency checking, và API availability. Vendor module không được phụ thuộc vào system-only API. HAL service binary sẽ được cài vào `/vendor/bin/hw/`."

## Q9: "`current/` và `aidl_api/<version>/` khác gì nhau?"

**A:**
> "`current/` là work-in-progress snapshot, mutable, đại diện cho AIDL source hiện tại. Framework build dùng `current/`. `aidl_api/<version>/` là frozen snapshot, immutable, đại diện cho một version đã release. Vendor build pin một version cụ thể để đảm bảo stability."

## Q10: "Nếu vendor HAL crash, điều gì xảy ra?"

**A:**
> "Nếu HAL service được khai báo `class hal` trong init.rc, init sẽ tự động restart service. Nếu khai báo `critical`, sau 4 lần crash trong 4 phút, device sẽ reboot. Framework client nên đăng ký `DeathRecipient` để detect HAL death và reconnect với exponential backoff."

---

# 16. Tóm tắt flashcard

## 16.1. 10 điểm then chốt

1. **Project Treble** tách system và vendor partition để OTA nhanh hơn.
2. **Stable AIDL** là AIDL mở rộng để đảm bảo binary ABI stability.
3. **`@VintfStability`** đánh dấu type là VINTF boundary, phải stable qua OTA.
4. **`aidl_interface`** là Soong module type để build stable AIDL.
5. **`aidl_api/<name>/<version>/`** chứa frozen snapshot.
6. **App-layer AIDL** không cần stable vì deploy cùng APK.
7. **Vendor HAL AIDL** bắt buộc stable vì framework và vendor update độc lập.
8. **Backward compat**: thêm ở cuối ✅, xóa/đổi thứ tự ❌.
9. **VINTF manifest** mô tả HAL/version vendor cung cấp; **compatibility matrix** mô tả framework yêu cầu.
10. **Validation**: `update-api` → `freeze-api` → `check-api`.

## 16.2. Câu nói để defend interview

> "VDiag HAL được thiết kế như một Android Automotive HAL thực thụ: interface `IDiagnosticHal` dùng Stable AIDL với `@VintfStability`, freeze version trong `aidl_api/com.vdiag.hal/1/`, và declare trong VINTF manifest. Điều này đảm bảo rằng khi OEM push system OTA mới, vendor HAL cũ vẫn tương thích, và ngược lại. Tôi validate bằng `m com.vdiag.hal-check-api` và VINTF manifest check."

---

# 17. Tài liệu tham khảo

1. [Stable AIDL | Android Open Source Project](https://source.android.com/docs/core/architecture/aidl/stable-aidl)
2. [AIDL for HALs | Android Open Source Project](https://source.android.com/docs/core/architecture/aidl/aidl-for-hals)
3. [Project Treble | Android Open Source Project](https://source.android.com/docs/core/architecture/vintf)
4. [VINTF Object | Android Open Source Project](https://source.android.com/docs/core/architecture/vintf/objects)
5. [Compatibility Matrices | Android Open Source Project](https://source.android.com/docs/core/architecture/vintf/comp-matrices)

---

*File được tạo để học kỹ Day 61 — Stable AIDL HAL + @VintfStability. Đọc lại trước khi interview.*
