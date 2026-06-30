cmake_minimum_required(VERSION 3.18.1)
project(vdiag_jni)

add_library(vdiag_jni SHARED
        jni_onload.cpp
        jni_bridge.cpp
        jni_callback.cpp
)

set(AUTODIAG_HAL_BUILD_TESTS OFF CACHE BOOL "Disable HAL tests for Android build" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../../hal ${CMAKE_BINARY_DIR}/hal_build)

find_library(log-lib log)

target_link_libraries(vdiag_jni
        vdiag_hal
        ${log-lib}
)