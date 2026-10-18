cmake_minimum_required(VERSION 3.20)
project(DynamicGraphConnectivity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build types:
#   Release        -O3, asserts off. The only type to benchmark with.
#   RelWithChecks  -O3 with asserts and libstdc++ assertions on. Default; for testing optimized code.
#   Debug          -O0 -g with libstdc++ assertions.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHCHECKS "-O3 -g -D_GLIBCXX_ASSERTIONS")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -D_GLIBCXX_ASSERTIONS")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithChecks CACHE STRING "Release | RelWithChecks | Debug" FORCE)
endif()

# Record the -march value with every result. Use x86-64-v3 for runs on other machines.
set(DC_MARCH "native" CACHE STRING "Value passed to -march")
option(DC_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(DC_BUILD_TESTS "Build the test suite" ON)

add_library(dynconn INTERFACE)
target_include_directories(dynconn INTERFACE include)
target_compile_options(dynconn INTERFACE -march=${DC_MARCH} -Wall -Wextra -Wpedantic)
if(DC_SANITIZE)
    target_compile_options(dynconn INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(dynconn INTERFACE -fsanitize=address,undefined)
endif()

# Phase 2 driver: parity check + measured memory footprint (global allocation hooks only).
add_executable(benchmark_runner src/main.cpp)
target_link_libraries(benchmark_runner PRIVATE dynconn)

# Memory accounting changes container allocator types, so it is enabled per executable.
add_executable(memory_profile src/memory_profile.cpp)
target_link_libraries(memory_profile PRIVATE dynconn)
target_compile_definitions(memory_profile PRIVATE DC_MEMORY_ACCOUNTING)

if(DC_BUILD_TESTS)
    enable_testing()

    add_executable(dc_tests
        tests/test_main.cpp
        tests/test_edge_cases.cpp
        tests/test_fuzz.cpp
        tests/test_generator.cpp
        tests/test_containers.cpp)
    target_link_libraries(dc_tests PRIVATE dynconn)
    target_include_directories(dc_tests PRIVATE tests)

    add_executable(dc_memory_tests tests/test_main.cpp tests/test_memory.cpp)
    target_link_libraries(dc_memory_tests PRIVATE dynconn)
    target_include_directories(dc_memory_tests PRIVATE tests)
    target_compile_definitions(dc_memory_tests PRIVATE DC_MEMORY_ACCOUNTING)

    add_executable(dc_stats_tests tests/test_main.cpp tests/test_stats.cpp)
    target_link_libraries(dc_stats_tests PRIVATE dynconn)
    target_include_directories(dc_stats_tests PRIVATE tests)
    target_compile_definitions(dc_stats_tests PRIVATE DC_STATS)

    foreach(name
            edge_cases_baselines edge_cases_ett_layouts edge_cases_ett_khop edge_cases_dtree edge_cases_dnd edge_cases_hks edge_cases_hdt
            invariants_every_op layout_shape_identity dtree_layout_identity hks_layout_identity hdt_layout_identity
            flat_hash_map_matches_reference ordered_sets_identical_order
            rng_bounded_is_in_range_and_unbiased generator_exact_op_count_and_semantics
            generator_deterministic generator_golden_hash generator_density_control generator_tiny_graphs)
        add_test(NAME ${name} COMMAND dc_tests ${name})
    endforeach()
    foreach(size 2 8 30 200 2000)
        foreach(mix read_heavy balanced churn_heavy growth dense_steady)
            add_test(NAME differential_v${size}_${mix} COMMAND dc_tests differential_v${size}_${mix})
        endforeach()
    endforeach()
    foreach(name memory_counters_return_to_zero memory_treap_node_bytes_exact)
        add_test(NAME ${name} COMMAND dc_memory_tests ${name})
    endforeach()
    foreach(name stats_counters_consistent stats_identical_across_layouts hks_sampling_branch_exercised)
        add_test(NAME ${name} COMMAND dc_stats_tests ${name})
    endforeach()
endif()
