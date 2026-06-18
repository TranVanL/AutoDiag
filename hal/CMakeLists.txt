cmake_minimum_required(VERSION 3.10) 

project(hal LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_library(hal STATIC
    src/dummy.cpp
)
