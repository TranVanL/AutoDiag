#!/usr/bin/env bash
# scripts/build_aarch64.sh
# One-shot cross-compile + QEMU test for the HAL layer on aarch64.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HAL_DIR="${SCRIPT_DIR}/../hal"
BUILD_DIR="${HAL_DIR}/build_arm64"

echo "==> Installing ARM64 toolchain + QEMU (if missing)..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    qemu-user \
    qemu-user-static

echo "==> Verifying toolchain..."
aarch64-linux-gnu-g++ --version | head -n 1
qemu-aarch64 --version | head -n 1

echo "==> Configuring HAL for aarch64..."
rm -rf "${BUILD_DIR}"
cmake -S "${HAL_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${HAL_DIR}/cmake/toolchain-aarch64.cmake" \
    -DCMAKE_BUILD_TYPE=Release

echo "==> Building..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "==> Verifying ELF architecture..."
for binary in test_uds_codec test_mock_hal test_session_state test_diag_engine test_isotp_codec; do
    path="${BUILD_DIR}/${binary}"
    if [[ -f "${path}" ]]; then
        file "${path}"
    fi
done

echo "==> Running tests under QEMU..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "==> ARM64 cross-build + QEMU test: OK"