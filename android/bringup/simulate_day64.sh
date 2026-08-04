#!/bin/bash
# Day 64 simulation: full emulator bring-up + privapp-permissions deploy.
# Requires a userdebug emulator image with adb root support.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[Day64] Root and remount emulator"
adb root
adb remount

echo "[Day64] Push privapp-permissions allowlist"
adb push "$SCRIPT_DIR/privapp-permissions-vdiag.xml" \
    /system/etc/permissions/privapp-permissions-vdiag.xml

echo "[Day64] Reset HAL ready property"
adb shell setprop vdiag.hal.ready 0

echo "[Day64] Start DiagCarService"
adb shell am start-service -n com.vdiag/.service.DiagCarService

echo "[Day64] Trigger HAL ready property"
adb shell setprop vdiag.hal.ready 1

echo "[Day64] Verify permissions granted"
adb shell dumpsys package com.vdiag | grep -A2 "DIAGNOSE" || true

echo "[Day64] Verify processes"
adb shell ps -A | grep vdiag || true

echo "[Day64] Tail logcat (press Ctrl+C to stop)"
adb logcat -c
adb logcat | grep -E "VDiag|vdiag|DiagCarService|PermissionGate"
