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
    src/hal_factory.cpp
    src/doip_diag_hal.cpp
    src/session_state.cpp
    src/diag_engine.cpp
)
target_include_directories(vdiag_hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(vdiag_hal PUBLIC Threads::Threads)
if (WIN32)
    target_link_libraries(vdiag_hal PUBLIC ws2_32)
endif()

if (AUTODIAG_HAL_BUILD_TESTS AND NOT ANDROID)
    enable_testing()

    # Fetch googletest source and build it with the active toolchain. This is
    # required for cross-compilation because the system-installed libgtest is
    # almost always x86_64-only.
    include(FetchContent)
    if(CMAKE_VERSION VERSION_LESS 3.14)
        # FetchContent_MakeAvailable is not available before 3.14; fall back to
        # the older population pattern.
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
        )
        FetchContent_GetProperties(googletest)
        if(NOT googletest_POPULATED)
            FetchContent_Populate(googletest)
            add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
        endif()
    else()
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
        )
        # For Windows: prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    function(add_hal_test NAME SOURCE)
        add_executable(${NAME} ${SOURCE})
        target_link_libraries(${NAME} PRIVATE vdiag_hal gtest_main gmock)
        target_include_directories(${NAME} PRIVATE
            ${googletest_SOURCE_DIR}/googlemock/include
            ${googletest_SOURCE_DIR}/googletest/include
        )
        add_test(NAME ${NAME} COMMAND ${NAME})
    endfunction()

    add_hal_test(test_uds_codec      tests/test_uds_codec_gtest.cpp)
    add_hal_test(test_mock_hal       tests/test_mock_hal_gtest.cpp)
    add_hal_test(test_session_state  tests/test_session_state_gtest.cpp)
endif()

