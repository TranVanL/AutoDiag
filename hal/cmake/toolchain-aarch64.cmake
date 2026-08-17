# hal/cmake/toolchain-aarch64.cmake
# Cross-compile toolchain for ARM64 (AArch64) Linux GNU target.
#
# Usage:
#   mkdir build_arm64 && cd build_arm64
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
#            -DCMAKE_BUILD_TYPE=Release
#
# The toolchain targets a generic Linux embedded/aarch64 device using glibc
# (GNU ABI). It is intentionally Android-agnostic: the HAL layer is pure C++
# logic that can be verified on ARM64 via QEMU before integration into the
# Android stack through JNI/AIDL.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Target triplet: aarch64-linux-gnu
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar)
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP        aarch64-linux-gnu-strip)
set(CMAKE_OBJCOPY      aarch64-linux-gnu-objcopy)
set(CMAKE_SIZE         aarch64-linux-gnu-size)

# Optional sysroot. On Debian/Ubuntu cross libraries live under
# /usr/aarch64-linux-gnu. Uncomment to restrict header/library searches to the
# target sysroot only.
# set(CMAKE_SYSROOT /usr/aarch64-linux-gnu)

# Search rules:
#   - PROGRAM: never search inside sysroot (we already point to the toolchain).
#   - LIBRARY/INCLUDE: only search inside the target sysroot so we never link
#     against host x86_64 libraries by accident.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Statically link the C/C++ runtime by default. This makes the binaries much
# more portable across ARM64 targets that may ship older or different versions
# of libstdc++/libgcc. It also simplifies QEMU deployment because we do not
# need to copy matching shared runtimes into the target rootfs.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")

# Tell CMake we are cross-compiling so it will not try to execute compiled
# binaries during the configure step. CTest will use the emulator defined below.
set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-aarch64;-L;/usr/aarch64-linux-gnu")