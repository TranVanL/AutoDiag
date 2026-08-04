#!/bin/bash
# Simulate Day 62 property-triggered bring-up flow on an emulator/device.
# This only exercises the Java/app side; native HAL must be built into the image.

set -e

echo "[Day62] Reset HAL ready property"
adb shell setprop vdiag.hal.ready 0

echo "[Day62] Start DiagCarService (simulate init.rc service directive)"
adb shell am start-service -n com.vdiag/.service.DiagCarService

echo "[Day62] Trigger HAL ready property"
adb shell setprop vdiag.hal.ready 1

echo "[Day62] Verify processes"
adb shell ps -A | grep vdiag || true

echo "[Day62] Tail logcat (press Ctrl+C to stop)"
adb logcat -c
adb logcat | grep -E "VDiag|vdiag|DiagCarService"
