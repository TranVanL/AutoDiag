cmake_minimum_required(VERSION 3.10) 

project(hal LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

enable_testing()

add_library(hal STATIC
    src/dummy.cpp
    src/uds_codec.cpp
)

add_executable(test_uds_codec
    tests/test_uds_codec.cpp
)
target_link_libraries(test_uds_codec PRIVATE hal)


add_test(NAME uds_encode_8_tests COMMAND test_uds_codec)

