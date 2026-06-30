cmake_minimum_required(VERSION 3.10) 

project(hal LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(AUTODIAG_HAL_BUILD_TESTS "Build HAL host tests" ON)

find_package(Threads REQUIRED)

add_library(vdiag_hal STATIC
    src/dummy.cpp
    src/uds_codec.cpp
    src/mock_diag_hal.cpp
    src/session_state.cpp
    src/diag_engine.cpp
)
target_include_directories(vdiag_hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(vdiag_hal PUBLIC Threads::Threads)

if (AUTODIAG_HAL_BUILD_TESTS AND NOT ANDROID)
    enable_testing()

    add_executable(test_uds_codec
        tests/test_uds_codec.cpp
    )

    add_executable(test_mock_hal
        tests/test_mock_hal.cpp
    )
    target_link_libraries(test_mock_hal PRIVATE vdiag_hal)

    add_executable(test_session_state
        tests/test_session_state.cpp
    )
    target_link_libraries(test_session_state PRIVATE vdiag_hal)

    add_executable(test_diag_engine
        tests/test_diag_engine.cpp
    )
    target_link_libraries(test_diag_engine PRIVATE vdiag_hal)

    target_link_libraries(test_uds_codec PRIVATE vdiag_hal)

    add_test(NAME uds_encode_decode_20_tests COMMAND test_uds_codec)
    add_test(NAME mock_hal_5_tests COMMAND test_mock_hal)
    add_test(NAME session_state_5_tests COMMAND test_session_state)
    add_test(NAME diag_engine_4_tests COMMAND test_diag_engine)
endif()

