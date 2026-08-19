# Bring-up Troubleshooting — SELinux, VINTF, init.rc, and Permissions

> **Purpose:** A practical guide for the most common failures when bringing up an AAOS service on real hardware. Use this together with [`BRINGUP_GUIDE.md`](BRINGUP_GUIDE.md).

---

## Quick decision tree

```
Service won't start?
  └─ ps -A | grep vdiag → missing?
      ├─ binary in /vendor/bin/hw/?  → build / PRODUCT_PACKAGES issue
      ├─ init.rc loaded?             → path / class / syntax issue
      └─ binary crashes on start?    → logcat / tombstone

HAL not found by framework?
  ├─ hwservice list → missing?       → interface aidl directive / VINTF manifest
  ├─ vintf check-compat fail?        → manifest / matrix mismatch
  └─ SELinux denial?                 → dmesg | grep avc

Permission denied at runtime?
  ├─ APK in /system/priv-app/?
  ├─ permission protectionLevel correct?
  ├─ privapp-permissions allowlist present?
  └─ APK signed with platform/OEM key?

Service killed?
  ├─ by carwatchdog                  → health check timeout / blocked binder thread
  └─ by LMK                          → memory leak / too large heap

Shutdown delayed?
  └─ CarPowerManager listener not completing → drain timeout / blocked engine
```

---

## 1. HAL service not found

### Symptom

```
ServiceManager: service 'com.vdiag.hal.IDiagnosticHal/default' not found
```

### Root causes

1. Binary not installed in `/vendor/bin/hw/`.
2. `init.rc` missing `interface aidl com.vdiag.hal.IDiagnosticHal/default`.
3. VINTF manifest missing the HAL entry.
4. Framework compatibility matrix version/instance mismatch.
5. Binary not executable or wrong SELinux file context.
6. Binary crashes immediately after start.

### Debug commands

```bash
adb shell ps -A | grep vdiag                    # is the process running?
adb shell ls -lZ /vendor/bin/hw/vdiag*           # file presence + SELinux label
adb shell logcat -s init                         # init parsing / start errors
adb shell cmd hwservice list | grep IDiagnosticHal
adb shell cmd vintf check-compat                 # manifest/matrix match
adb shell dumpsys vintf                          # parsed manifests
```

### Fix workflow

1. Confirm the module is in `PRODUCT_PACKAGES`.
2. Confirm `init.rc` uses `class hal` and declares the `interface aidl` line.
3. Confirm `manifest.xml` declares `<name>com.vdiag.hal</name>` and `<fqname>IDiagnosticHal/default</fqname>`.
4. Confirm `compatibility_matrix.xml` requires the same version and instance.
5. Rebuild `vendorimage` and re-flash.

---

## 2. SELinux denials

### Symptom

```
avc: denied { call } for scontext=u:r:vdiag_hal:s0 tcontext=u:r:car_service:s0
```

### Debug

```bash
adb shell dmesg | grep "avc: denied"
adb shell logcat -b events | grep avc
adb shell ls -lZ /vendor/bin/hw/vdiag_hal_service
```

### Fix workflow

1. Run in permissive mode for the domain (or globally on a userdebug build):
   ```bash
   adb shell setenforce 0
   ```
2. Exercise the feature end-to-end.
3. Collect denials:
   ```bash
   adb shell dmesg | grep avc > denials.txt
   ```
4. Generate a draft rule:
   ```bash
   audit2allow -i denials.txt
   ```
5. Hand-review the output. Never blanket-allow `sys_admin`, `dac_override`, or broad file classes.
6. Add minimal rules to `vdiag_hal.te`, rebuild sepolicy, re-test in enforcing mode.

---

## 3. Permission denied at runtime

### Symptom

```
SecurityException: com.vdiag.permission.DIAGNOSE
```

### Checklist

1. Is the APK installed in `/system/priv-app/VDiag/`?
2. Is the permission declared in the manifest with `android:protectionLevel="signature"` (or `signature|privileged`)?
3. Is the permission allowlisted in `privapp-permissions-vdiag.xml`?
4. Is the APK signed with the platform/OEM signing key?
5. Is the service `exported="false"` so only same-package/UID clients can bind?

### Debug

```bash
adb shell dumpsys package com.vdiag | grep -A10 "requestedPermissions\|grantedPermissions"
adb shell cmd package dump com.vdiag | grep -i permission
```

---

## 4. Service killed by watchdog

### Symptom

```
carwatchdog: VDiag missed health check — killing
```

### Checklist

1. Is `CarWatchdogClient` (or the shim) registered in `DiagCarService.onCreate()`?
2. Does `onCheckHealthStatus` return within the timeout (e.g. 3 s)?
3. Does the health probe actually check the worker and HAL, or does it just return `true`?
4. Is a Binder thread blocked on a long operation (I/O, HAL round-trip, DB write)?
5. Is the main thread blocked by a synchronous call?

### Debug

```bash
adb shell logcat -s carwatchdog:V VDiag:D
adb shell dumpsys activity service com.vdiag/.service.DiagCarService
```

---

## 5. Shutdown delay

### Symptom

System waits several seconds at shutdown.

### Checklist

1. Is `CarPowerManager.CarPowerStateListener` registered?
2. Does the listener call `completePowerStateChange()` within the deadline?
3. Is the engine drain bounded (e.g. 500 ms timeout) instead of waiting forever?
4. Are subscriptions and the watchdog unregistered before returning?

### Debug

```bash
adb shell logcat -s VDiag:D CarPowerManager:D
adb shell dumpsys activity service com.vdiag/.service.DiagCarService
```

---

## 6. Crash loop / repeated restarts

### Symptom

```
init: Service 'vdiag_hal' (pid xxx) exited with status 1
init: Starting service 'vdiag_hal' ...
```

### Checklist

1. Read the tombstone or `logcat` crash signature.
2. Check for missing shared libraries (`ldd` equivalent via `readelf -d`).
3. Check for AIDL version mismatch between client and server.
4. If `critical` is set, consider removing it for a non-boot-essential HAL.

---

## 7. Interview talking points

> *"Bring-up debugging is systematic: first check if the service is running, then check VINTF, then SELinux denials, then permission allowlists, and finally watchdog/power. I use permissive mode to collect AVC denials, then hand-review `audit2allow` output rather than blindly adding rules. For shutdown delays I look for unbounded drains or missing `completePowerStateChange()` calls."*
