# VDiag — AOSP Bring-up Integration Guide

> **Mục tiêu:** Tổng hợp kiến thức cốt lõi để integrate VDiag HAL vào AOSP, build vendor image, và test trên emulator.  
> **Phạm vi:** Tập trung vào build config (`Android.bp`), boot orchestration (`init.rc`), và vendor interface contract (`VINTF`). Không đi sâu implement C++ HAL.

---

## 1. Xác nhận kiến thức nền

Để bring-up một HAL service trên Android Automotive, cần 3 artifact chính:

| Artifact | Vai trò | Nơi deploy trên target |
|----------|---------|------------------------|
| `Android.bp` | Khai báo module build: native HAL binary tên gì, source ở đâu, install vào partition nào, có phải vendor không, file `.rc` đi kèm. | Build tree (không copy trực tiếp lên target) |
| `init.vdiag.rc` | Yêu cầu khởi động HAL: thời điểm start, user/group, cpuset, interface registration, property trigger. | `/vendor/etc/init/` |
| `manifest_vdiag.xml` | VINTF contract: khai báo HAL/version/instance mà vendor cung cấp cho framework. | `/vendor/etc/vintf/` (merge vào manifest tổng) |

Ngoài ra cần:
- **AOSP source tree** để build.
- **Compatibility matrix** ở system partition khớp với manifest.
- **SELinux policy** để cấp quyền cho HAL domain.

---

## 2. Android.bp — build config (quan trọng nhất)

`Android.bp` là file khai báo module cho Soong build system. Với native HAL service, cần xác định:

- `name`: tên module → quyết định tên binary và tên trong `PRODUCT_PACKAGES`.
- `relative_install_path`: `"hw"` → binary install vào `/vendor/bin/hw/`.
- `vendor: true` → thuộc vendor partition.
- `init_rc`: file `.rc` đi kèm, tự động copy vào `/vendor/etc/init/`.
- `srcs`: source C++ của HAL service.
- `shared_libs`: thư viện liên kết, bao gồm cả AIDL NDK backend.

Ví dụ:

```bp
cc_binary {
    name: "android.hardware.vdiag@1.0-service",
    relative_install_path: "hw",
    init_rc: ["android.hardware.vdiag@1.0-service.rc"],
    vendor: true,

    srcs: [
        "main.cpp",
        "DiagnosticHal.cpp",
    ],

    shared_libs: [
        "libbase",
        "libbinder_ndk",
        "libcutils",
        "liblog",
        "libutils",
        "android.hardware.vdiag-V1-ndk",
    ],

    cflags: [
        "-Wall",
        "-Werror",
    ],
}
```

**Lưu ý:**
- Tên module phải khớp với binary path trong `init.rc`.
- `android.hardware.vdiag-V1-ndk` là thư viện được generate từ `aidl_interface` rule.
- File `.rc` trong `init_rc` phải cùng thư mục với `Android.bp` hoặc khai báo đường dẫn đúng.

---

## 3. init.vdiag.rc — boot orchestration

`init.rc` là ngôn ngữ cấu hình của `init` — process userspace đầu tiên. Nó quyết định:

- Khi nào start service (`class hal`, `class main`, property trigger).
- Service chạy với UID/GID nào.
- Service register HAL interface nào với `hwservicemanager`.
- Service thuộc cpuset nào.

Ví dụ:

```rc
service vdiag_hal /vendor/bin/hw/android.hardware.vdiag@1.0-service
    class hal
    user system
    group system
    interface android.hardware.vdiag@1.0::IDiagnosticHal default
    writepid /dev/cpuset/foreground/tasks

on boot
    setprop vdiag.hal.ready 0

on property:vdiag.hal.ready=1
    log i vdiag "VDiag HAL ready"
```

**Giải thích directive:**

| Directive | Ý nghĩa |
|-----------|---------|
| `service vdiag_hal ...` | Khai báo daemon do `init` quản lý. |
| `class hal` | Start trước zygote, sau khi vendor partition mounted. |
| `user system` / `group system` | UID/GID chạy service. |
| `interface ... default` | Register HAL instance với `hwservicemanager`. |
| `writepid /dev/cpuset/foreground/tasks` | Đưa PID vào foreground cpuset. |
| `on boot` | Trigger khi event boot xảy ra. |
| `on property:vdiag.hal.ready=1` | Trigger khi property thay đổi. |

**`class hal` vs `class main`:**

| Class | Thời điểm start | Dùng cho |
|-------|-----------------|----------|
| `hal` | Sau fs mount, trước zygote | HAL daemon |
| `main` | Sau zygote/framework | System daemon |

HAL phải dùng `class hal` để CarService bind được ngay khi boot.

---

## 4. VINTF manifest — vendor/framework contract

VINTF (Vendor Interface) đảm bảo system partition và vendor partition tương thích. Gồm 2 phần:

- **Device manifest** (vendor): khai báo HAL nào vendor cung cấp.
- **Compatibility matrix** (system): khai báo HAL nào framework yêu cầu.

Ví dụ device manifest:

```xml
<manifest version="2.0" type="device">
    <hal format="aidl">
        <name>android.hardware.vdiag</name>
        <version>1</version>
        <fqname>IDiagnosticHal/default</fqname>
    </hal>
</manifest>
```

Ví dụ framework compatibility matrix:

```xml
<compatibility-matrix version="1.0" type="framework">
    <hal format="aidl" optional="true">
        <name>android.hardware.vdiag</name>
        <version>1</version>
        <interface>
            <name>IDiagnosticHal</name>
            <instance>default</instance>
        </interface>
    </hal>
</compatibility-matrix>
```

Nếu không khớp → `vintf` check fail → boot failure.

---

## 5. Download AOSP source

### 5.1. Yêu cầu hệ thống

- Linux (Ubuntu LTS khuyến nghị).
- ~300GB disk free.
- ~16GB RAM (32GB khuyến nghị).
- Python 3, Git, repo tool.

### 5.2. Cài đặt repo

```bash
mkdir -p ~/.bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/.bin/repo
chmod a+x ~/.bin/repo
export PATH="$HOME/.bin:$PATH"
```

### 5.3. Init và sync source

```bash
mkdir -p ~/aosp && cd ~/aosp
repo init -u https://android.googlesource.com/platform/manifest -b android-14.0.0_rxx
repo sync -c -j$(nproc)
```

> Thay `android-14.0.0_rxx` bằng tag cụ thể. Với Automotive, dùng branch có hỗ trợ `aosp_car_*`.

---

## 6. Triển khai VDiag lên AOSP

### 6.1. Copy source vào AOSP tree

Giả sử VDiag source nằm ở `~/AutoDiag/CurrentCode/AutoDiag/`:

```bash
AOSP=~/aosp

# AIDL HAL interface
mkdir -p $AOSP/hardware/interfaces/vdiag/aidl
rsync -av ~/AutoDiag/CurrentCode/AutoDiag/android/aidl_hal/ \
    $AOSP/hardware/interfaces/vdiag/aidl/

# Native HAL service
mkdir -p $AOSP/hardware/interfaces/vdiag/aidl/default
rsync -av ~/AutoDiag/CurrentCode/AutoDiag/hal/src/ \
    $AOSP/hardware/interfaces/vdiag/aidl/default/

# Bring-up artifacts
mkdir -p $AOSP/device/vdiag/bringup
rsync -av ~/AutoDiag/CurrentCode/AutoDiag/android/bringup/ \
    $AOSP/device/vdiag/bringup/

# VINTF manifest
mkdir -p $AOSP/device/vdiag/vintf
cp ~/AutoDiag/CurrentCode/AutoDiag/android/bringup/manifest_vdiag.xml \
    $AOSP/device/vdiag/vintf/
```

### 6.2. Include device.mk vào device.mk chính

Trong `$AOSP/device/<oem>/<board>/device.mk`, thêm:

```makefile
include device/vdiag/bringup/device.mk
```

Hoặc nếu dùng quy tắc include tự động, đảm bảo `device/vdiag/bringup/` nằm trong search path.

### 6.3. Build

```bash
cd $AOSP
source build/envsetup.sh

# Chọn target
lunch aosp_car_x86_64-userdebug

# Build module HAL
m android.hardware.vdiag@1.0-service

# Build vendor image
m vendorimage

# Hoặc build full image
m -j$(nproc)
```

---

## 7. Test trên Android Emulator

### 7.1. Chạy emulator từ AOSP build

```bash
cd $AOSP
emulator
```

Hoặc chỉ định image:

```bash
emulator -system out/target/product/generic_x86_64/system.img \
         -vendor out/target/product/generic_x86_64/vendor.img \
         -ramdisk out/target/product/generic_x86_64/ramdisk.img \
         -kernel prebuilts/qemu-kernel/x86_64/kernel-qemu \
         -avd <tên-avd>
```

### 7.2. Verify HAL service

```bash
# Kiểm tra service đang chạy
adb shell ps -A | grep vdiag

# Kiểm tra HAL đã register với hwservicemanager
adb shell cmd hwservice list | grep IDiagnosticHal

# Kiểm tra property trigger
adb shell getprop vdiag.hal.ready

# Kiểm tra log
adb logcat -d | grep -i vdiag
```

### 7.3. Simulate property flow (nếu chưa có HAL thật)

```bash
adb shell setprop vdiag.hal.ready 0
adb shell am start-service -n com.vdiag/.service.DiagCarService
adb shell setprop vdiag.hal.ready 1
adb logcat | grep -E "VDiag|vdiag"
```

### 7.4. Kiểm tra VINTF

```bash
adb shell cmd vintf check-compat
adb shell vintf manifest
```

---

## 8. Debug checklist

| Symptom | Nguyên nhân có thể | Cách fix |
|---------|-------------------|----------|
| Service không start | `class hal` chưa được trigger, hoặc binary không có trong `/vendor/bin/hw/` | Kiểm tra `PRODUCT_PACKAGES`, build output, `init.rc` path. |
| HAL không register | `interface` directive sai, hoặc binary crash | Kiểm tra `hwservice list`, logcat, `init.svc.vdiag_hal`. |
| Boot fail | VINTF manifest/matrix mismatch | Chạy `vintf check-compat`, đối chiếu version/instance. |
| SELinux denial | Policy thiếu quyền | `adb shell dmesg -W \| grep avc`, dùng `audit2allow`. |
| Service restart liên tục | Crash loop, kiểm tra `init.svc.vdiag_hal=restarting` | Xem logcat tìm crash signature. |

---

## 9. Tóm tắt flow

```
AOSP source tree
    ├── hardware/interfaces/vdiag/aidl/        ← AIDL interface + Android.bp
    │   └── default/                           ← native HAL service + .rc
    ├── device/vdiag/bringup/                  ← device.mk + init.vdiag.rc
    └── device/vdiag/vintf/manifest_vdiag.xml  ← VINTF manifest

Build:
    source build/envsetup.sh
    lunch aosp_car_x86_64-userdebug
    m android.hardware.vdiag@1.0-service
    m vendorimage

Deploy/Test:
    emulator
    adb shell ps -A | grep vdiag
    adb shell cmd hwservice list | grep IDiagnosticHal
```

---

## 10. Interview talking points

1. **"Bring-up flow từ bootloader đến app"** — bootloader → kernel → init → class hal → HAL register → zygote → system_server → CarService → app bindService.
2. **"Tại sao dùng `class hal`?"** — đảm bảo HAL sẵn sàng trước framework client.
3. **"VINTF là gì?"** — contract giữa system và vendor partition, enable Treble/OTA.
4. **"Android.bp khai báo những gì?"** — module name, install path, vendor flag, init_rc, shared_libs.
5. **"Cách test trên emulator?"** — build AOSP car target, chạy `emulator`, verify bằng `adb` + `hwservice list`.
