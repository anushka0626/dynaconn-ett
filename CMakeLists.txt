cmake_minimum_required(VERSION 3.20)
project(DynamicGraphConnectivity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

include_directories(include)

add_executable(benchmark_runner src/main.cpp)