# 🚀 VDiag — Bring-up Notes (init.rc + SELinux + VINTF)

> **Phase 10 talking points.** Tôi không deploy lên device thật, nhưng document đầy đủ để **biết phải làm gì** khi join VinFast — đây chính là câu trả lời cho JD1 *"Experience bringing up Android devices"*.

---

## 1. Big picture: từ APK đến boot service

Trên AAOS device thật, một system service như VDiag cần:

```
┌────────────────────────────────────────────────────────────┐
│  1. Build artifacts                                        │
│     APK (system app) + native HAL service binary           │
│                                                            │
│  2. System partition layout                                │
│     /system/priv-app/VDiag/VDiag.apk                       │
│     /vendor/bin/vdiag_hal_service                          │
│     /vendor/lib64/libvdiag_hal.so                          │
│                                                            │
│  3. Boot orchestration                                     │
│     /vendor/etc/init/vdiag.rc        ← starts HAL          │
│                                                            │
│  4. SELinux                                                │
│     /vendor/etc/selinux/vdiag.te     ← policy             │
│                                                            │
│  5. HAL versioning                                         │
│     /vendor/etc/vintf/manifest.xml   ← register HAL        │
│                                                            │
│  6. App permission                                         │
│     /etc/permissions/privapp-permissions-vdiag.xml         │
└────────────────────────────────────────────────────────────┘
```

---

## 2. `init.rc` — start HAL at boot

`/vendor/etc/init/vdiag.rc`:

```rc
# VDiag HAL service — vehicle diagnostic over UDS/DoIP
# Started after vendor partition mounted, before zygote

service vdiag_hal /vendor/bin/vdiag_hal_service
    class hal
    user vdiag
    group vdiag system
    capabilities NET_RAW NET_ADMIN
    writepid /dev/cpuset/system-background/tasks
    interface aidl com.vdiag.hal.IDiagnosticHal/default

# Auto-restart on crash, max 5 in 60s then give up
on property:vendor.vdiag.restart=1
    restart vdiag_hal
```

**Key directives explained:**
- `class hal` → started by `init` during HAL phase (before app process)
- `user vdiag` / `group vdiag` → dedicated UID created in `system/core/libcutils/include/private/android_filesystem_config.h`
- `capabilities NET_RAW` → cần cho DoIP raw socket
- `interface aidl` → register stable AIDL HAL với `hwservicemanager`
- `writepid` → put process in cpuset for scheduling control

---

## 3. SELinux policy

`/vendor/etc/selinux/vdiag.te`:

```sepolicy
# VDiag HAL — vehicle diagnostic service
# Type definitions
type vdiag_hal,         domain;
type vdiag_hal_exec,    exec_type, vendor_file_type, file_type;

# Initial domain transition: init → vdiag_hal
init_daemon_domain(vdiag_hal)

# Allow Binder communication
binder_use(vdiag_hal)
binder_call(vdiag_hal, system_server)
binder_call(vdiag_hal, car_service)

# Allow AIDL HAL registration
hal_server_domain(vdiag_hal, hal_vehicle)

# Network: DoIP needs TCP socket on port 13400
allow vdiag_hal self:tcp_socket create_stream_socket_perms;
allow vdiag_hal port:tcp_socket name_connect;

# Logging
allow vdiag_hal logd:unix_dgram_socket sendto;

# Deny everything else (default)
neverallow vdiag_hal { app_data_file system_data_file }:file *;
```

`/vendor/etc/selinux/file_contexts.vdiag`:
```
/vendor/bin/vdiag_hal_service    u:object_r:vdiag_hal_exec:s0
```

**Talking points:**
- Mỗi HAL = 1 SELinux domain riêng (least privilege principle)
- `init_daemon_domain(vdiag_hal)` macro = init transitions to vdiag_hal khi exec binary
- `binder_call(A, B)` = allow A → B Binder calls
- `neverallow` rules = compile-time check, build fail nếu policy vi phạm
- Real device: `audit2allow` from logcat audit messages → tinh chỉnh policy

---

## 4. VINTF Manifest — HAL versioning

`/vendor/etc/vintf/manifest.xml` (snippet):

```xml
<manifest version="2.0" type="device">
    <hal format="aidl">
        <name>com.vdiag.hal</name>
        <version>1</version>
        <fqname>IDiagnosticHal/default</fqname>
    </hal>
</manifest>
```

`/system/etc/vintf/compatibility_matrix.xml`:

```xml
<compatibility-matrix version="1.0" type="framework">
    <hal format="aidl" optional="true">
        <name>com.vdiag.hal</name>
        <version>1</version>
        <interface>
            <name>IDiagnosticHal</name>
            <instance>default</instance>
        </interface>
    </hal>
</compatibility-matrix>
```

**Talking points:**
- VINTF = **Vendor Interface** = contract giữa system & vendor partition
- Manifest (vendor) declares "I provide HAL X v1"
- Compatibility matrix (system) declares "I require HAL X v1"
- `vts10_compatibility_test` validate match → boot fail nếu mismatch
- Cho phép update system partition độc lập vendor (Project Treble)

---

## 5. AIDL stable HAL versioning

`Android.bp`:

```bp
aidl_interface {
    name: "com.vdiag.hal",
    vendor_available: true,
    srcs: ["com/vdiag/hal/*.aidl"],
    stability: "vintf",
    backend: {
        cpp:  { enabled: false },
        ndk:  { enabled: true  },
        java: { enabled: false },
    },
    versions: ["1"],
}
```

**Key:**
- `stability: "vintf"` → frozen, backward-compat enforced
- After freeze: file `aidl_api/com.vdiag.hal/1/com/vdiag/hal/IDiagnosticHal.aidl` immutable
- Adding fields → bump to v2, keep v1 server-side

---

## 6. App privileged permission allowlist

`/etc/permissions/privapp-permissions-vdiag.xml`:

```xml
<permissions>
    <privapp-permissions package="com.vdiag">
        <permission name="com.vdiag.permission.DIAGNOSE"/>
        <permission name="android.permission.INTERACT_ACROSS_USERS"/>
    </privapp-permissions>
</permissions>
```

**Talking points:**
- Apps in `/system/priv-app/` cần allowlist explicit từ Android 9+
- Nếu thiếu allowlist → boot fail với `SecurityException` trong logcat
- Tool: `frameworks/base/data/etc/services.core.protected-permissions.xml` reference

---

## 7. Boot sequence (where VDiag fits)

```
0. bootloader → kernel
1. /init parses init.rc files (system, vendor, odm)
2. early-init: mount /system, /vendor
3. on init: create users/groups (incl. vdiag)
4. on boot: start core services
5. class hal: start vdiag_hal  ← VDiag HAL spawns here
   ↓
   - exec /vendor/bin/vdiag_hal_service
   - SELinux transitions init → vdiag_hal domain
   - registers IDiagnosticHal/default with hwservicemanager
6. zygote forks system_server
7. system_server starts CarService (in :car_service)
   ↓
   - CarService binds to vdiag_hal via AIDL HAL interface
8. zygote forks app processes
9. com.vdiag app launches → bindService(DiagCarService)
   ↓
   - DiagCarService delegates to vendor HAL via JNI
```

---

## 8. Debug commands (real device)

```bash
# Check HAL is running
adb shell ps -A | grep vdiag
adb shell cmd hwservice list | grep IDiagnosticHal

# SELinux denials
adb shell dmesg -W | grep avc:
# audit2allow -i denials.log → suggest policy

# Manifest verification
adb shell cmd vintf check-compat
adb shell vintf manifest

# Service binding from app
adb shell dumpsys activity service com.vdiag/.service.DiagCarService

# Atrace for performance
adb shell atrace -t 10 -o /sdcard/trace.html sched freq am wm view
adb pull /sdcard/trace.html
# Open in chrome://tracing
```

---

## 9. Interview kill points

1. **"Bring-up flow"** — kể từ bootloader → init.rc → SELinux domain → AIDL HAL register → CarService bind → app bindService
2. **"Project Treble"** — VINTF manifest enables system OTA update không đụng vendor partition
3. **"AIDL stable HAL"** — replaced HIDL since Android 11, `stability: "vintf"`, frozen versioning
4. **"SELinux least privilege"** — mỗi HAL 1 domain riêng, `neverallow` compile-time check, `audit2allow` workflow
5. **"Privileged app allowlist"** — Android 9+ requires `privapp-permissions.xml`, otherwise boot fail
6. **"Why HAL chạy trên vendor partition?"** — OEM-specific, system partition kept generic for OTA

---

## 10. Honest disclaimer

> **In interview**: *"Tôi chưa deploy VDiag lên device thật vì không có hardware. Nhưng tôi documented full bring-up flow trong `docs/bringup.md` — init.rc, SELinux .te, VINTF manifest, privapp-permissions. Khi join VinFast và có device, tôi biết chính xác phải làm gì, bởi vì các pattern này tôi đã apply tương tự khi build LG MSM (AUTOSAR Adaptive lifecycle, manifest.json, exec_manifest)."*

Cross-reference experience: LG MSM sử dụng AUTOSAR Adaptive Application Manifest (file JSON) tương tự VINTF — declarative service registration. Tương đồng pattern, khác ecosystem.
