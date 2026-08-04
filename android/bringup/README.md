# VDiag Bring-up Artifacts

This directory contains AOSP bring-up artifacts for the VDiag HAL service.

## Files

| File | Purpose | Deploy target in AOSP |
|------|---------|----------------------|
| `init.vdiag.rc` | init.rc service definition for `vdiag_hal` | Copied to `/vendor/etc/init/` via `init_rc` in `Android.bp` |
| `device.mk` | Product packages, VINTF manifest, SELinux dirs, privapp-permissions | Included by device `<board>/device.mk` |
| `manifest_vdiag.xml` | VINTF device manifest fragment | Merged into `/vendor/etc/vintf/manifest.xml` |
| `framework_compatibility_matrix.xml` | VINTF framework matrix fragment | Merged into `/system/etc/vintf/compatibility_matrix.xml` |
| `Android.bp.reference` | Reference build rule for native HAL service | Place at `hardware/interfaces/vdiag/aidl/default/Android.bp` |
| `simulate_day62.sh` | Emulator simulation script for property-triggered flow | Run manually from host |
| `simulate_day63.sh` | Linux host SELinux syntax validation | Run manually from host |
| `simulate_day64.sh` | Full emulator bring-up + privapp-permissions deploy | Run manually from host |
| `sepolicy/vdiag_hal.te` | SELinux type enforcement rules | Compiled into vendor sepolicy image |
| `sepolicy/file_contexts` | SELinux file labels | Compiled into vendor sepolicy image |
| `sepolicy/property_contexts` | SELinux property labels | Compiled into vendor sepolicy image |
| `privapp-permissions-vdiag.xml` | Privileged permission allowlist for `com.vdiag` | `/system/etc/permissions/` |

## Integration steps

1. Copy AIDL HAL interface and native service source into AOSP tree:
   - `hardware/interfaces/vdiag/aidl/` — AIDL interface + `aidl_interface` rule.
   - `hardware/interfaces/vdiag/aidl/default/` — native service source + `Android.bp`.

2. Copy bring-up artifacts:
   - `device/vdiag/bringup/` — this directory.
   - `device/vdiag/vintf/manifest_vdiag.xml` — VINTF manifest.

3. Include `device/vdiag/bringup/device.mk` from the board's `device.mk`.

4. Build:
   ```bash
   source build/envsetup.sh
   lunch aosp_car_x86_64-userdebug
   m android.hardware.vdiag@1.0-service
   m vendorimage
   ```

5. Test on emulator:
   ```bash
   emulator
   adb shell ps -A | grep vdiag
   adb shell cmd hwservice list | grep IDiagnosticHal
   adb shell getenforce
   adb logcat | grep -i "avc: denied"
   ```

## Notes

- These artifacts are documentation/reference code for interview preparation.
- Real deployment requires a full AOSP source tree and a matching device target.
