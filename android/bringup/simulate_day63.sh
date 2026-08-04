#!/bin/bash
# Day 63 simulation: validate SELinux policy syntax on Linux host.
# Does NOT require a device or emulator.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SEPOLICY_DIR="$SCRIPT_DIR/sepolicy"

echo "[Day63] Validate vdiag_hal.te syntax"
checkpolicy -M -c 33 -o /tmp/vdiag_hal.pp "$SEPOLICY_DIR/vdiag_hal.te" 2>&1

echo "[Day63] Validate file_contexts format"
chkcon "$SEPOLICY_DIR/file_contexts" /tmp/vdiag_hal.pp

echo "[Day63] SELinux syntax validation passed"
