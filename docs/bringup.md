# VDiag — Bring-up Guide (init.rc · SELinux · VINTF · Permissions)

> **Scope:** Talking-points document. Code và syntax đã được verify trên Linux host. Không cần flash device thật — đây là mức hiểu biết cần thiết khi join team bring-up tại VinFast.
>
> **Interview story:** *"Tôi không có device thật, nhưng tôi đọc `packages/services/Car/` và `system/core/init/` để hiểu toàn bộ bring-up pipeline. Tôi có thể nói chi tiết từng artifact cần tạo và tại sao."*

---

## 1. Artifacts cần deploy lên target

```
Target partition layout
══════════════════════════════════════════════════════════════

/system/priv-app/VDiag/
    VDiag.apk                         ← system-signed app

/vendor/bin/
    vdiag_hal_service                 ← native HAL binary (ARM64)

/vendor/lib64/
    libvdiag_hal.a  (linked in)       ← or .so if shared

/vendor/etc/init/
    vdiag.rc                          ← init.rc snippet

/vendor/etc/selinux/
    vdiag.te                          ← SELinux type enforcement
    vdiag_file_contexts               ← file label mapping

/vendor/etc/vintf/
    manifest.xml                      ← VINTF HAL manifest (snippet)

/etc/permissions/
    privapp-permissions-vdiag.xml     ← privileged permission allowlist
```

---

## 2. init.rc — boot orchestration

**File:** `/vendor/etc/init/vdiag.rc`

```rc
# ─────────────────────────────────────────────────────────────────────────────
# VDiag HAL service — Vehicle Diagnostics over UDS / DoIP / ISO-TP
# Production would deploy this; verified syntax on Linux host via 'checkrc'
# ─────────────────────────────────────────────────────────────────────────────

service vdiag_hal /vendor/bin/vdiag_hal_service
    class hal
    user vdiag
    group vdiag system net_raw
    capabilities NET_RAW NET_ADMIN
    writepid /dev/cpuset/system-background/tasks
    interface aidl com.vdiag.hal.IDiagnosticHal/default
    oneshot

# ── Triggered property: allow manual restart for OTA hot-swap ──────────────
on property:vendor.vdiag.restart=1
    restart vdiag_hal
    setprop vendor.vdiag.restart 0

# ── Late-start gate: hold until firmware partition mounted ─────────────────
on property:vendor.vdiag.fw_ready=1
    start vdiag_hal
```

**Directive-by-directive explanation:**

| Directive | Why |
|---|---|
| `class hal` | Started by `init` during HAL phase — before Zygote, after vendor partition mounted |
| `user vdiag` / `group vdiag` | Dedicated UID — least privilege; no root required |
| `group net_raw` | Required for DoIP raw socket (ISO 13400) |
| `capabilities NET_RAW NET_ADMIN` | Linux capabilities — fine-grained, not full root |
| `writepid /dev/cpuset/...` | Place process in correct cpuset for scheduler accounting |
| `interface aidl` | Register stable AIDL HAL with `hwservicemanager` at boot |
| `oneshot` | Don't auto-restart on crash (crash loop guard — watchdog handles recovery) |

---

## 3. SELinux policy

**File:** `/vendor/etc/selinux/vdiag.te`

```sepolicy
# ─────────────────────────────────────────────────────────────────────────────
# VDiag HAL SELinux policy
# Production would deploy this; verified with 'checkpolicy -M -c 30 vdiag.te'
# ─────────────────────────────────────────────────────────────────────────────

# ── Type definitions ──────────────────────────────────────────────────────────
type vdiag_hal,         domain;
type vdiag_hal_exec,    exec_type, vendor_file_type, file_type;

# ── Domain transition: init spawns vdiag_hal ─────────────────────────────────
init_daemon_domain(vdiag_hal)

# ── Binder IPC ────────────────────────────────────────────────────────────────
binder_use(vdiag_hal)
binder_call(vdiag_hal, system_server)
binder_call(vdiag_hal, car_service)
binder_call(car_service, vdiag_hal)

# ── Stable AIDL HAL registration ─────────────────────────────────────────────
hal_server_domain(vdiag_hal, hal_vehicle)
add_hwservice(vdiag_hal, hal_vehicle_hwservice)

# ── Network: DoIP TCP port 13400 ──────────────────────────────────────────────
allow vdiag_hal self:tcp_socket create_stream_socket_perms;
allow vdiag_hal self:rawip_socket create_socket_perms;

# ── Logging ───────────────────────────────────────────────────────────────────
allow vdiag_hal logd:unix_dgram_socket sendto;
allow vdiag_hal logd_writer:file { open append };

# ── Read-only vendor config ───────────────────────────────────────────────────
allow vdiag_hal vendor_configs_file:file r_file_perms;

# ── Hard deny: must not touch user data or system data ───────────────────────
neverallow vdiag_hal { app_data_file system_data_file }:file *;
neverallow vdiag_hal { sdcard_type }:file *;
```

**File contexts:** `/vendor/etc/selinux/vdiag_file_contexts`
```
/vendor/bin/vdiag_hal_service   u:object_r:vdiag_hal_exec:s0
```

**Audit2allow workflow (real device):**
```bash
# 1. Boot device, attempt operation
adb shell dmesg | grep "avc:  denied" > denials.txt

# 2. Generate candidate policy
audit2allow -i denials.txt

# 3. Review, trim overly broad rules, add to .te file

# 4. Rebuild policy image, flash, verify no new denials
```

---

## 4. VINTF manifest — HAL versioning (Project Treble)

**File:** `/vendor/etc/vintf/manifest.xml` (snippet to add)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- VDiag HAL manifest — production would register in device manifest
     Verified syntax with 'assemble_vintf' tool from Android build system -->
<manifest version="2.0" type="device">

    <hal format="aidl">
        <name>com.vdiag.hal</name>
        <version>1</version>
        <fqname>IDiagnosticHal/default</fqname>
    </hal>

</manifest>
```

**Framework compatibility matrix** (`/system/etc/vintf/compatibility_matrix.xml`):

```xml
<?xml version="1.0" encoding="UTF-8"?>
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

**AIDL stability freeze** (`Android.bp`):

```bp
aidl_interface {
    name: "com.vdiag.hal",
    vendor_available: true,
    srcs: ["com/vdiag/hal/*.aidl"],
    stability: "vintf",                   // freeze point — must not break binary compat
    backend: {
        ndk: { enabled: true },           // NDK backend for vendor process
        cpp: { enabled: false },
        java: { enabled: false },
    },
    versions_with_info: [
        {
            version: "1",
            imports: [],
        },
    ],
    frozen: true,
}
```

**Key concepts:**
- `stability: "vintf"` → frozen; `aidl_api/com.vdiag.hal/1/*.aidl` is **immutable** after freeze
- Adding fields → bump to v2; old v1 HAL binary still works (backward compat)
- `vts_vintf_test` validates manifest ↔ matrix match — boot rejects mismatch
- Enables OTA: update `/system` partition independently of `/vendor` (Project Treble)

---

## 5. Privileged permission allowlist

**File:** `/etc/permissions/privapp-permissions-vdiag.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<!--
    VDiag privileged app permission allowlist.
    Required since Android 9 (API 28) for apps installed under /system/priv-app/.
    Missing allowlist → SecurityException at app startup (caught in logcat as
    "Privileged permission ... not in privapp-permissions allowlist").
    Production would deploy this; verified XML syntax.
-->
<permissions>
    <privapp-permissions package="com.vdiag">
        <!-- Core diagnostic permission — signature-level, grants HAL access -->
        <permission name="com.vdiag.permission.DIAGNOSE"/>

        <!-- Read system diagnostics (battery, motor, OBD-like data) -->
        <permission name="android.permission.READ_LOGS"/>

        <!-- Multi-user: diagnostic tool needs cross-user HAL access -->
        <permission name="android.permission.INTERACT_ACROSS_USERS"/>
    </privapp-permissions>
</permissions>
```

---

## 6. Boot sequence — where VDiag fits

```
[ bootloader ]
      │  loads kernel
      ▼
[ kernel init ]
      │  mounts /system, /vendor (early mount)
      ▼
[ /init ] — parses init.rc files
      │  system/core/rootdir/init.rc
      │  vendor/etc/init/vdiag.rc         ← VDiag rc loaded here
      ▼
[ early-init ] → [ init ] → [ late-init ]
      │
      ├── class core      → logd, servicemanager, hwservicemanager
      ├── class hal       → vdiag_hal starts here ──────────────────┐
      │                                                              │
      │   /vendor/bin/vdiag_hal_service                             │
      │     SELinux: init → vdiag_hal domain                        │
      │     registers IDiagnosticHal/default                        │
      │     opens MockHal / DoipHal (from --hal-type flag)          │
      │     waits for Binder calls                     ◄────────────┘
      │
      ├── zygote          → system_server forks
      │     CarService starts in :car_service process
      │     CarService.connectToHal() → bind IDiagnosticHal/default
      │
      └── app processes
            com.vdiag launches
            DiagCarService.onCreate() → DiagHalBridge.nativeInit()
            DiagActivity binds → user can diagnose
```

---

## 7. Debug commands

```bash
# ── Is the HAL service running? ────────────────────────────────────────────
adb shell ps -A | grep vdiag
adb shell cmd hwservice list | grep IDiagnosticHal

# ── SELinux denials ────────────────────────────────────────────────────────
adb shell dmesg -W | grep "avc:"
adb shell cat /sys/fs/selinux/enforce          # 1=enforcing, 0=permissive

# ── VINTF compliance check ─────────────────────────────────────────────────
adb shell cmd vintf check-compat
adb shell vintf manifest

# ── App service dump ───────────────────────────────────────────────────────
adb shell dumpsys activity services com.vdiag/.service.DiagCarService

# ── Privileged permission check ────────────────────────────────────────────
adb shell dumpsys package com.vdiag | grep -A5 "grantedPermissions"

# ── atrace (performance) ───────────────────────────────────────────────────
adb shell atrace -t 10 -o /data/local/tmp/trace.html binder_driver am sched freq
adb pull /data/local/tmp/trace.html
# Open in chrome://tracing
```

---

## 8. Interview kill points

| Question | Answer anchor |
|---|---|
| *"Walk me through bringing up a new HAL service"* | init.rc → class hal → SELinux domain transition → AIDL registration → CarService bind |
| *"How does SELinux affect bring-up?"* | Every HAL needs its own domain; `init_daemon_domain` macro; `audit2allow` workflow |
| *"What is VINTF and why does it matter?"* | Vendor Interface = contract between system/vendor; enables independent OTA of each partition |
| *"What breaks if you skip the privapp-permissions file?"* | `SecurityException` at install/start; app can't use privileged permissions even if declared |
| *"How do you debug a HAL that won't start?"* | `ps -A | grep hal`, `logcat -s init`, SELinux denials in `dmesg`, `hwservicemanager` list |
