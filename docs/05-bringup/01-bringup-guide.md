# VDiag — Bring-up Guide

> **Purpose:** One-stop reference for bringing VDiag up on a real AAOS device. Covers build artifacts, `init.rc`, SELinux, VINTF, permissions, and the full boot flow.
>
> **Scope:** Reference documentation. Syntax has been verified on a Linux host; real-device flashing has not been performed because no hardware is available. The patterns follow AAOS source (`packages/services/Car/`, `system/core/init/`, `system/sepolicy/`).
>
> **Companion doc:** [`19_BRINGUP_TROUBLESHOOTING.md`](19_BRINGUP_TROUBLESHOOTING.md) for debug procedures.

---

## 1. What "bring-up" means for VDiag

VDiag is designed as a system-level diagnostic stack. On a production AAOS device it is not a regular Play Store app; it ships as:

- a **system/priv-app APK** (`com.vdiag`) for the UI and SDK,
- a **vendor HAL daemon** (`vdiag_hal_service`) for the native engine and transport,
- a set of **platform configuration files** (`init.rc`, SELinux `.te`, VINTF XML, privapp-permissions XML).

This guide lists every artifact, explains what each line does, and shows how they fit together at boot.

---

## 2. Artifact inventory

```
/system/priv-app/VDiag/
    VDiag.apk                              ← platform-signed system app

/vendor/bin/hw/
    vdiag_hal_service                      ← native HAL daemon (ARM64)

/vendor/etc/init/
    vdiag.rc                               ← boot orchestration

/vendor/etc/selinux/
    vdiag_hal.te                           ← type enforcement policy
    vdiag_file_contexts                    ← file labels
    vdiag_property_contexts                ← property labels (optional)

/vendor/etc/vintf/
    manifest.xml                           ← vendor HAL manifest (snippet)

/system/etc/vintf/
    compatibility_matrix.xml               ← framework requirement (snippet)

/system/etc/permissions/
    privapp-permissions-vdiag.xml          ← privileged permission allowlist

/hardware/interfaces/vdiag/aidl/           ← AOSP build tree
    Android.bp
    current/com/vdiag/hal/IDiagnosticHal.aidl
    aidl_api/com.vdiag.hal/1/...
```

---

## 3. `Android.bp` — build declaration

`Android.bp` tells Soong what to build, where to install it, and which AIDL backend to generate.

```bp
// hardware/interfaces/vdiag/aidl/Android.bp
aidl_interface {
    name: "com.vdiag.hal",
    vendor: true,
    srcs: ["com/vdiag/hal/*.aidl"],
    stability: "vintf",
    backend: {
        ndk: { enabled: true },
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

// hardware/interfaces/vdiag/aidl/default/Android.bp
cc_binary {
    name: "vdiag_hal_service",
    relative_install_path: "hw",
    init_rc: ["vdiag.rc"],
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
        "com.vdiag.hal-V1-ndk",
    ],

    cflags: [
        "-Wall",
        "-Werror",
    ],
}
```

**Key fields:**

| Field | Why it matters |
|---|---|
| `vendor: true` | Puts the output in `/vendor` partition. |
| `relative_install_path: "hw"` | Binary lands in `/vendor/bin/hw/`. |
| `init_rc: ["vdiag.rc"]` | Soong copies the rc file to `/vendor/etc/init/`. |
| `stability: "vintf"` | Freezes the AIDL interface; ABI changes require a new version. |
| `frozen: true` | Build enforces backward compatibility against `aidl_api/`. |

---

## 4. `init.rc` — boot orchestration

`/vendor/etc/init/vdiag.rc`:

```rc
# VDiag HAL service — vehicle diagnostics over UDS / DoIP / ISO-TP
service vdiag_hal /vendor/bin/hw/vdiag_hal_service
    class hal
    user vdiag
    group vdiag system net_raw
    capabilities NET_RAW NET_ADMIN
    writepid /dev/cpuset/system-background/tasks
    interface aidl com.vdiag.hal.IDiagnosticHal/default

# Manual restart trigger for OTA hot-swap
on property:vendor.vdiag.restart=1
    restart vdiag_hal
    setprop vendor.vdiag.restart 0
```

**Directive-by-directive:**

| Directive | Meaning |
|---|---|
| `class hal` | Start during the HAL phase, before zygote/system_server. |
| `user vdiag` / `group vdiag` | Run under a dedicated UID/GID (least privilege). |
| `group net_raw` | Needed for raw sockets used by DoIP. |
| `capabilities NET_RAW NET_ADMIN` | Fine-grained Linux capabilities instead of root. |
| `writepid /dev/cpuset/...` | Places the daemon in a cpuset for scheduler accounting. |
| `interface aidl ...` | Registers the stable AIDL HAL with `hwservicemanager`. |

**Why `class hal`?**  
Framework services such as `CarService` bind to the HAL during boot. If the HAL used `class main`, the framework could start before the HAL is ready and fail to find it.

---

## 5. SELinux policy

### 5.1 Type enforcement — `vdiag_hal.te`

```sepolicy
# Type definitions
type vdiag_hal,         domain;
type vdiag_hal_exec,    exec_type, vendor_file_type, file_type;

# Init domain transition
init_daemon_domain(vdiag_hal)

# Binder IPC
binder_use(vdiag_hal)
binder_call(vdiag_hal, system_server)
binder_call(vdiag_hal, car_service)

# Stable AIDL HAL registration
hal_server_domain(vdiag_hal, hal_vehicle)
add_hwservice(vdiag_hal, hal_vehicle_hwservice)

# Network: DoIP TCP port 13400
allow vdiag_hal self:tcp_socket create_stream_socket_perms;
allow vdiag_hal port:tcp_socket name_connect;
allow vdiag_hal self:rawip_socket create_socket_perms;

# Logging
allow vdiag_hal logd:unix_dgram_socket sendto;

# Read-only vendor config
allow vdiag_hal vendor_configs_file:file r_file_perms;

# Hard deny: must not touch user or system data
neverallow vdiag_hal { app_data_file system_data_file }:file *;
neverallow vdiag_hal { sdcard_type }:file *;
```

### 5.2 File contexts — `vdiag_file_contexts`

```
/vendor/bin/hw/vdiag_hal_service   u:object_r:vdiag_hal_exec:s0
```

### 5.3 Property contexts (optional)

```
vdiag\.hal\..*  u:object_r:vendor_vdiag_prop:s0
```

### 5.4 Iteration workflow on a real device

1. Boot in permissive mode for the domain or globally: `adb shell setenforce 0`.
2. Exercise the feature.
3. Collect denials: `adb shell dmesg | grep "avc: denied"`.
4. Generate a candidate rule: `audit2allow -i denials.txt`.
5. Hand-review and trim overly broad rules.
6. Add to `vdiag_hal.te`, rebuild, and re-test in enforcing mode.

---

## 6. VINTF manifest and compatibility matrix

### 6.1 Vendor manifest — `/vendor/etc/vintf/manifest.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<manifest version="2.0" type="device">
    <hal format="aidl">
        <name>com.vdiag.hal</name>
        <version>1</version>
        <fqname>IDiagnosticHal/default</fqname>
    </hal>
</manifest>
```

### 6.2 Framework compatibility matrix — `/system/etc/vintf/compatibility_matrix.xml`

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

**Why VINTF matters:**  
VINTF makes the system/vendor boundary explicit. A mismatch between the vendor manifest and the framework matrix blocks boot, preventing an OTA system image from calling a HAL interface the vendor does not implement.

---

## 7. Privileged permission allowlist

`/system/etc/permissions/privapp-permissions-vdiag.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<permissions>
    <privapp-permissions package="com.vdiag">
        <permission name="com.vdiag.permission.DIAGNOSE"/>
        <permission name="android.permission.INTERACT_ACROSS_USERS"/>
    </privapp-permissions>
</permissions>
```

Since Android 9, privileged apps must be explicitly allowlisted. Missing this file causes a `SecurityException` at startup even if the permission is declared in the manifest.

---

## 8. Full boot sequence

```
[bootloader] → [kernel]
      │
      ▼
[init] parses /vendor/etc/init/vdiag.rc
      │
      ├── class core      → logd, servicemanager, hwservicemanager
      ├── class hal       → vdiag_hal starts
      │                       - SELinux transition init → vdiag_hal
      │                       - registers IDiagnosticHal/default
      │
      ├── zygote          → system_server forks
      │                       - CarService starts
      │                       - binds to IDiagnosticHal/default
      │
      └── app processes
              com.vdiag launches
              DiagActivity binds DiagCarService
```

---

## 9. AOSP integration steps

1. **Sync AOSP source** (automotive branch, e.g. `aosp_car_x86_64-userdebug`).
2. **Copy VDiag HAL sources** into the AOSP tree:
   - `hardware/interfaces/vdiag/aidl/` — AIDL interface + `Android.bp`
   - `hardware/interfaces/vdiag/aidl/default/` — native HAL service + `vdiag.rc`
   - `device/vdiag/bringup/` — `device.mk`, SELinux, privapp-permissions
   - `device/vdiag/vintf/manifest_vdiag.xml`
3. **Include the device makefile** in the board `device.mk`:
   ```makefile
   include device/vdiag/bringup/device.mk
   ```
4. **Build:**
   ```bash
   source build/envsetup.sh
   lunch aosp_car_x86_64-userdebug
   m vdiag_hal_service
   m vendorimage
   ```
5. **Flash or run emulator**, then verify with the commands below.

---

## 10. Verification commands

```bash
# Is the HAL running?
adb shell ps -A | grep vdiag
adb shell cmd hwservice list | grep IDiagnosticHal

# SELinux state and denials
adb shell getenforce
adb shell dmesg -W | grep "avc:"

# VINTF compliance
adb shell cmd vintf check-compat
adb shell vintf manifest

# App permissions
adb shell dumpsys package com.vdiag | grep -A5 "grantedPermissions"

# Service state
adb shell dumpsys activity service com.vdiag/.service.DiagCarService
```

---

## 11. Emulator-only development note

VDiag's current CI and demos run on a Linux host and the Android Automotive emulator. The HAL is exercised through:

- `MockDiagnosticHal` (in-process),
- `DoipDiagnosticHal` over `adb reverse tcp:13400`,
- `CanDiagnosticHal` on Linux host `vcan0`.

The bring-up artifacts in this guide are the production path. They are documented and syntax-checked, but not flashed to real hardware.

---

## 12. Interview talking points

> *"To bring up VDiag on a real AAOS device I would produce six artifacts: an `Android.bp` declaring the native HAL daemon, an `init.rc` with `class hal` and `interface aidl`, a dedicated SELinux domain with least-privilege allow rules, a VINTF manifest and matching framework matrix, and a `privapp-permissions` allowlist. The boot flow is init → class hal → HAL registers with hwservicemanager → zygote → CarService binds the HAL → app binds CarService. I haven't flashed a board, but I verified the policy syntax with `checkpolicy` and the XML with the VINTF tools, and I can debug denials with `audit2allow`."*

---

## 13. Cross-reference

- [`19_BRINGUP_TROUBLESHOOTING.md`](19_BRINGUP_TROUBLESHOOTING.md) — step-by-step debug for HAL not found, SELinux denials, permission failures, watchdog kills, and shutdown delays.
- [`HAL_SERVICE_DEEP_DIVE.md`](HAL_SERVICE_DEEP_DIVE.md) — 3-process topology, DeathRecipient, exponential backoff reconnect.
- [`StableAIDL_HAL_DeepDive.md`](StableAIDL_HAL_DeepDive.md) — stable AIDL freeze workflow and versioning.
