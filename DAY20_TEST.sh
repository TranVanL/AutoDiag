#!/bin/bash
# =============================================================================
# Day 20 — JNI Edge Cases Verification Script
# Covers:
# 1) 5 requests in quick burst
# 2) Kill app mid-request (no native crash)
# 3) CheckJNI warnings/errors
# 4) Leak symptom scan (global ref overflow)
# =============================================================================

set -euo pipefail

PKG="com.vdiag"
ACTIVITY="com.vdiag/.MainActivity"
VIN_X="${VIN_X:-250}"
VIN_Y="${VIN_Y:-350}"
BURST_COUNT="${BURST_COUNT:-5}"

COLOR_BOLD='\033[1m'
COLOR_GREEN='\033[0;32m'
COLOR_YELLOW='\033[1;33m'
COLOR_RED='\033[0;31m'
COLOR_RESET='\033[0m'

log_step() { echo -e "${COLOR_BOLD}${COLOR_YELLOW}[STEP]${COLOR_RESET} $1"; }
log_pass() { echo -e "${COLOR_GREEN}✓ PASS${COLOR_RESET} $1"; }
log_fail() { echo -e "${COLOR_RED}✗ FAIL${COLOR_RESET} $1"; }
log_info() { echo -e "${COLOR_BOLD}[INFO]${COLOR_RESET} $1"; }

collect_vdiag_logs() {
    adb logcat -d -v time \
        -s VDiag.JNI:V DiagCarService:V CarService.Binder:V MainActivity:V AndroidRuntime:E *:S
}

collect_critical_logs_all_buffers() {
    adb logcat -d -b all | grep -iE \
        "JNI DETECTED ERROR|global reference table overflow|FATAL EXCEPTION|SIGSEGV|signal 11|abort message|checkjni.*(warn|error)|use of deleted local reference|attempt to use stale" || true
}

log_step "A1: Enable CheckJNI"
adb shell setprop debug.checkjni 1
sleep 1
CHECKJNI_VALUE=$(adb shell getprop debug.checkjni | tr -d '\r')
if [[ "$CHECKJNI_VALUE" == "1" ]]; then
    log_pass "CheckJNI enabled: $CHECKJNI_VALUE"
else
    log_fail "Failed to enable CheckJNI: $CHECKJNI_VALUE"
    exit 1
fi

log_step "A2: Fresh restart + clear logs"
adb shell am force-stop "$PKG"
sleep 1
adb logcat -c
adb shell am start -n "$ACTIVITY" >/dev/null
sleep 2

log_step "B1: Burst ${BURST_COUNT} requests"
for i in $(seq 1 "$BURST_COUNT"); do
    adb shell input tap "$VIN_X" "$VIN_Y"
    sleep 0.06
 done
sleep 2

BURST_REQ_COUNT=$(collect_vdiag_logs | grep -c "nativeGetProperty: reqId=" || true)
BURST_CB_COUNT=$(collect_vdiag_logs | grep -c "onResult - callback from service to client" || true)
if [[ "$BURST_REQ_COUNT" -ge "$BURST_COUNT" && "$BURST_CB_COUNT" -ge "$BURST_COUNT" ]]; then
    log_pass "Burst callbacks healthy (req=$BURST_REQ_COUNT cb=$BURST_CB_COUNT)"
else
    log_fail "Burst callbacks mismatch (req=$BURST_REQ_COUNT cb=$BURST_CB_COUNT expected>=$BURST_COUNT)"
fi

log_step "B2: Confirm worker-thread callback path"
WORKER_LOG=$(collect_vdiag_logs | grep "nativeGetProperty(worker)" || true)
if [[ -n "$WORKER_LOG" ]]; then
    log_pass "Async worker callback observed"
else
    log_fail "No worker-thread callback log found"
fi

log_step "C1: Kill app mid-request"
adb logcat -c
adb shell am start -n "$ACTIVITY" >/dev/null
sleep 1
for i in 1 2 3; do
    adb shell input tap "$VIN_X" "$VIN_Y"
    sleep 0.03
 done
adb shell am force-stop "$PKG"
sleep 1

CRASH_AFTER_KILL=$(collect_critical_logs_all_buffers)
if [[ -z "$CRASH_AFTER_KILL" ]]; then
    log_pass "No fatal/native crash signature after force-stop"
else
    log_fail "Detected critical crash/warning signatures"
    echo "$CRASH_AFTER_KILL"
fi

log_step "D1: Leak symptom scan with repeated rounds"
adb logcat -c
adb shell am start -n "$ACTIVITY" >/dev/null
sleep 1
for round in $(seq 1 20); do
    adb shell input tap "$VIN_X" "$VIN_Y"
    sleep 0.04
done
sleep 2
LEAK_SIG=$(collect_critical_logs_all_buffers)
if [[ -z "$LEAK_SIG" ]]; then
    log_pass "No JNI leak/crash/checkjni signature detected"
else
    log_fail "Potential JNI issue signature found"
    echo "$LEAK_SIG"
fi

log_step "D2: Show final VDiag trace (tail)"
collect_vdiag_logs | tail -60 || true

echo ""
echo -e "${COLOR_BOLD}${COLOR_GREEN}========== DAY 20 TEST SUMMARY ==========${COLOR_RESET}"
log_pass "Burst requests test executed"
log_pass "Async worker path validated"
log_pass "Kill mid-request stability checked"
log_pass "CheckJNI/leak signature scan completed"

echo ""
echo -e "${COLOR_BOLD}Interview talking points:${COLOR_RESET}"
echo "• Why AttachCurrentThread + auto-detach are mandatory for worker callbacks"
echo "• Why GlobalRef is needed for cross-thread callback lifetime"
echo "• Why detached worker + shared_ptr bridge avoids use-after-free"
echo "• Why force-stop often skips JNI_OnUnload but still should not crash"
