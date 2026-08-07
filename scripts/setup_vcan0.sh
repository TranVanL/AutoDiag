#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-vcan0}"

echo "[setup_vcan0] Loading vcan kernel module..."
if ! modprobe vcan; then
    echo "[setup_vcan0] ERROR: failed to load vcan module. Are you running as root?" >&2
    exit 1
fi

echo "[setup_vcan0] Creating interface ${IFACE}..."
ip link add dev "${IFACE}" type vcan || {
    echo "[setup_vcan0] Interface ${IFACE} may already exist; continuing."
}

echo "[setup_vcan0] Bringing ${IFACE} up..."
ip link set up "${IFACE}"

echo "[setup_vcan0] Verify:"
ip link show "${IFACE}"
