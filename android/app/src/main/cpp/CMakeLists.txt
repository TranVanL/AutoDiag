cmake_minimum_required(VERSION 3.18.1)
project(vdiag_jni)

add_library(vdiag_jni SHARED
    jni_onload.cpp
    jni_bridge.cpp
)

find_library(log-lib log)

target_link_libraries(vdiag_jni
        ${log-lib}
)