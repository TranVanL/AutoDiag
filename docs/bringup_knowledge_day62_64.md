# VDiag — Bring-up Knowledge: Day 62 → Day 64

> **Mục tiêu:** Tổng hợp kiến thức cốt lõi về Android Automotive HAL bring-up, từ `init.rc`, `Android.bp`, VINTF, SELinux, đến privapp-permissions. Không chứa implement C++ HAL — chỉ tập trung artifact, config, và flow.
>
> **Dùng cho:** interview senior, onboard AOSP platform team, và self-check trước khi integrate lên device/emulator.

---

## 1. Xác nhận kiến thức nền

Để bring-up một HAL service trên Android Automotive, cần **5 artifact chính**:

| # | Artifact | Vai trò | Nơi deploy trên target |
|---|----------|---------|------------------------|
| 1 | `Android.bp` | Khai báo module build: native HAL binary tên gì, source ở đâu, install vào partition nào, có phải vendor không, file `.rc` đi kèm. | Build tree (Soong parse, không copy trực tiếp) |
| 2 | `init.vdiag.rc` | Yêu cầu khởi động HAL: thời điểm start, user/group, cpuset, interface registration, property trigger. | `/vendor/etc/init/` |
| 3 | `manifest_vdiag.xml` | VINTF contract: khai báo HAL/version/instance mà vendor cung cấp cho framework. | `/vendor/etc/vintf/` (merge vào manifest tổng) |
| 4 | `vdiag_hal.te` + `file_contexts` + `property_contexts` | SELinux policy: domain, transition, Binder call, socket, file label, property label. | `/vendor/etc/selinux/` |
| 5 | `privapp-permissions-vdiag.xml` | Allowlist quyền privileged cho system app `com.vdiag`. | `/system/etc/permissions/` hoặc `/vendor/etc/permissions/` |

Ngoài ra cần:
- **AOSP source tree** để build.
- **Framework compatibility matrix** khớp với vendor manifest.
- **Device target** (`lunch`) đúng cho emulator hoặc board thật.

---

## 2. Android.bp — build config (quan trọng nhất)

`Android.bp` là file khai báo module cho **Soong build system**. Với native HAL service, cần xác định:

- `name`: tên module → quyết định tên binary và tên trong `PRODUCT_PACKAGES`.
- `relative_install_path: "hw"`: binary install vào `/vendor/bin/hw/`.
- `vendor: true`: module thuộc vendor partition.
- `init_rc`: file `.rc` đi kèm, tự động copy vào `/vendor/etc/init/`.
- `srcs`: source C++ của HAL service.
- `shared_libs`: thư viện liên kết, bao gồm cả AIDL NDK backend.

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

**Device manifest:**

```xml
<manifest version="2.0" type="device">
    <hal format="aidl">
        <name>android.hardware.vdiag</name>
        <version>1</version>
        <fqname>IDiagnosticHal/default</fqname>
    </hal>
</manifest>
```

**Framework compatibility matrix:**

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

## 5. SELinux policy (Day 63)

SELinux là **mandatory access control (MAC)**. Mỗi process/file/property có một **security context**. Policy quyết định ai được phép làm gì.

### 5.1. Các file SELinux cần tạo

| File | Vai trò |
|------|---------|
| `vdiag_hal.te` | Type enforcement rules: domain, transition, allow, neverallow. |
| `file_contexts` | Label file/path → security context. |
| `property_contexts` | Label property name → security context. |

### 5.2. `vdiag_hal.te`

```te
# Type declaration
type vdiag_hal, domain;
type vdiag_hal_exec, exec_type, vendor_file_type, file_type;
type vdiag_hal_data_file, file_type, data_file_type;

# Init transition
init_daemon_domain(vdiag_hal)

# Binder communication with CarService
binder_call(vdiag_hal, system_server)
binder_call(system_server, vdiag_hal)

# Access to hardware (CAN socket)
allow vdiag_hal self:socket create_socket_perms;
allow vdiag_hal self:rawip_socket create_socket_perms;

# Allow access to /dev/can0
allow vdiag_hal can_device:chr_file rw_file_perms;

# Logging
allow vdiag_hal logd:unix_dgram_socket sendto;
```

### 5.3. `file_contexts`

```
/vendor/bin/hw/android\.hardware\.vdiag.*  u:object_r:vdiag_hal_exec:s0
/data/vendor/vdiag(/.*)?                   u:object_r:vdiag_hal_data_file:s0
```

### 5.4. `property_contexts`

```
vdiag\.hal\..*  u:object_r:vendor_vdiag_prop:s0
```

### 5.5. Validate syntax trên Linux host

```bash
# Install SELinux tools
sudo apt-get install -y policycoreutils selinux-policy-dev

# Syntax check .te file
checkpolicy -M -c 33 -o /tmp/vdiag_hal.pp vdiag_hal.te 2>&1
# Nếu OK: "libsepol.policydb_write: writing policy version 33"
# Nếu error: fix syntax rồi retry

# Validate file_contexts format
chkcon file_contexts /tmp/vdiag_hal.pp
```

### 5.6. Test SELinux trên emulator

```bash
# Check current mode
adb shell getenforce    # Enforcing hoặc Permissive

# Xem AVC log thực tế khi chạy app (ngay cả permissive mode log vẫn hiện)
adb logcat | grep -i "avc: denied"
# → Hiểu được deny rule nào sẽ cần thêm vào .te file
```

### 5.7. Audit2allow workflow (real device)

```bash
# 1. Boot device, attempt operation
adb shell dmesg | grep "avc:  denied" > denials.txt

# 2. Generate candidate policy
audit2allow -i denials.txt

# 3. Review, trim overly broad rules, add to .te file

# 4. Rebuild policy image, flash, verify no new denials
```

---

## 6. privapp-permissions (Day 64)

Apps đặt trong `/system/priv-app/` là **privileged apps**. Từ Android 9+, mọi quyền privileged bắt buộc phải khai báo trong allowlist XML. Thiếu allowlist → `SecurityException` hoặc boot fail.

```xml
<privapp-permissions package="com.vdiag">
    <permission name="com.vdiag.permission.DIAGNOSE"/>
    <permission name="android.car.permission.CAR_DIAGNOSTICS"/>
</privapp-permissions>
```

**Deploy target:** `/system/etc/permissions/privapp-permissions-vdiag.xml` hoặc `/vendor/etc/permissions/`.

---

## 7. Download AOSP source

### 7.1. Yêu cầu hệ thống

- Linux (Ubuntu LTS khuyến nghị).
- ~300GB disk free.
- ~16GB RAM (32GB khuyến nghị).
- Python 3, Git, repo tool.

### 7.2. Cài đặt repo

```bash
mkdir -p ~/.bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/.bin/repo
chmod a+x ~/.bin/repo
export PATH="$HOME/.bin:$PATH"
```

### 7.3. Init và sync source

```bash
mkdir -p ~/aosp && cd ~/aosp
repo init -u https://android.googlesource.com/platform/manifest -b android-14.0.0_rxx
repo sync -c -j$(nproc)
```

> Thay `android-14.0.0_rxx` bằng tag cụ thể. Với Automotive, dùng branch có hỗ trợ `aosp_car_*`.

---

## 8. Triển khai VDiag lên AOSP

### 8.1. Copy source vào AOSP tree

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

### 8.2. Include device.mk vào device.mk chính

Trong `$AOSP/device/<oem>/<board>/device.mk`, thêm:

```makefile
include device/vdiag/bringup/device.mk
```

### 8.3. Build

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

## 9. Test trên Android Emulator

### 9.1. Chạy emulator từ AOSP build

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

### 9.2. Verify HAL service

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

### 9.3. Simulate property flow (nếu chưa có HAL thật)

```bash
adb shell setprop vdiag.hal.ready 0
adb shell am start-service -n com.vdiag/.service.DiagCarService
adb shell setprop vdiag.hal.ready 1
adb logcat | grep -E "VDiag|vdiag"
```

### 9.4. Kiểm tra VINTF

```bash
adb shell cmd vintf check-compat
adb shell vintf manifest
```

### 9.5. privapp-permissions simulation

```bash
# Step 1: Root emulator (userdebug image hỗ trợ adb root)
adb root
adb remount   # unlock /system write

# Step 2: Push privapp-permissions (simulate production deploy)
adb push privapp-permissions-vdiag.xml \
    /system/etc/permissions/privapp-permissions-vdiag.xml

# Step 3: Kill PackageManager để reload permissions
adb shell pm clear com.vdiag || true
adb shell am start-service -n com.vdiag/.service.DiagCarService

# Step 4: Verify permissions granted
adb shell dumpsys package com.vdiag | grep -A2 "DIAGNOSE"
# grantedPermissions: com.vdiag.permission.DIAGNOSE

# Step 5: Test permission enforcement (negative test)
adb shell am start -n com.vdiag.test/.PermissionDeniedActivity 2>&1
# Expected: SecurityException in logcat

# Step 6: End-to-end logcat verify
adb logcat -s VDiag:D PermissionGate:D
```

---

## 10. Debug checklist

| Symptom | Nguyên nhân có thể | Cách fix |
|---------|-------------------|----------|
| Service không start | `class hal` chưa được trigger, hoặc binary không có trong `/vendor/bin/hw/` | Kiểm tra `PRODUCT_PACKAGES`, build output, `init.rc` path. |
| HAL không register | `interface` directive sai, hoặc binary crash | Kiểm tra `hwservice list`, logcat, `init.svc.vdiag_hal`. |
| Boot fail | VINTF manifest/matrix mismatch | Chạy `vintf check-compat`, đối chiếu version/instance. |
| SELinux denial | Policy thiếu quyền | `adb shell dmesg -W \| grep avc`, dùng `audit2allow`. |
| Service restart liên tục | Crash loop, kiểm tra `init.svc.vdiag_hal=restarting` | Xem logcat tìm crash signature. |
| Privileged permission denied | Thiếu allowlist | Kiểm tra `/system/etc/permissions/privapp-permissions-vdiag.xml`. |

---

## 11. Tóm tắt flow

```
AOSP source tree
    ├── hardware/interfaces/vdiag/aidl/        ← AIDL interface + Android.bp
    │   └── default/                           ← native HAL service + .rc
    ├── device/vdiag/bringup/                  ← device.mk + init.vdiag.rc + sepolicy + privapp
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
    adb shell getenforce
    adb logcat | grep -i "avc: denied"
```

---

## 12. Interview talking points

1. **"Bring-up flow từ bootloader đến app"** — bootloader → kernel → init → class hal → HAL register → zygote → system_server → CarService → app bindService.
2. **"Tại sao dùng `class hal`?"** — đảm bảo HAL sẵn sàng trước framework client.
3. **"VINTF là gì?"** — contract giữa system và vendor partition, enable Treble/OTA.
4. **"Android.bp khai báo những gì?"** — module name, install path, vendor flag, init_rc, shared_libs.
5. **"SELinux least privilege"** — mỗi HAL một domain, `neverallow` compile-time check, `audit2allow` workflow.
6. **"privapp-permissions"** — Android 9+ bắt buộc allowlist, thiếu là boot fail.
7. **"Cách test trên emulator?"** — build AOSP car target, chạy `emulator`, verify bằng `adb` + `hwservice list` + `getenforce`.
