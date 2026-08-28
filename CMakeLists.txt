cmake_minimum_required(VERSION 3.16)
project(memsentry VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

set(MEMSENTRY_SOURCES
    src/memsentry.cpp
    src/allocator_hooks.cpp
    src/stacktrace.cpp
    src/snapshot.cpp
    src/reporter.cpp
)

set(MEMSENTRY_HEADERS
    include/memsentry/memsentry.hpp
    include/memsentry/config.hpp
    include/memsentry/types.hpp
    include/memsentry/core/recursion_guard.hpp
    include/memsentry/core/header.hpp
    include/memsentry/core/sharded_tracker.hpp
    include/memsentry/core/allocator_hooks.hpp
    include/memsentry/stacktrace/stacktrace.hpp
    include/memsentry/profiler/scope_tag.hpp
    include/memsentry/profiler/snapshot.hpp
    include/memsentry/profiler/histogram.hpp
    include/memsentry/reporter/console_reporter.hpp
    include/memsentry/reporter/json_reporter.hpp
    include/memsentry/reporter/html_reporter.hpp
)

add_library(memsentry STATIC ${MEMSENTRY_SOURCES} ${MEMSENTRY_HEADERS})
add_library(memsentry::memsentry ALIAS memsentry)

target_include_directories(memsentry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if (MSVC)
    add_compile_options(/EHsc /W3 /Zc:__cplusplus /Zc:tlsGuards-)
    target_link_options(memsentry INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/WHOLEARCHIVE:$<TARGET_FILE:memsentry>>)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

if (WIN32)
    target_link_libraries(memsentry PUBLIC dbghelp Threads::Threads)
elseif (APPLE)
    target_link_libraries(memsentry PUBLIC Threads::Threads)
else()
    target_link_libraries(memsentry PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

option(MEMSENTRY_BUILD_EXAMPLES "Build MemSentry examples" ON)
option(MEMSENTRY_BUILD_TESTS "Build MemSentry tests" ON)

if (MEMSENTRY_BUILD_EXAMPLES)
    add_executable(01_basic_leak examples/01_basic_leak.cpp)
    target_link_libraries(01_basic_leak PRIVATE memsentry)

    add_executable(02_scoped_profiling examples/02_scoped_profiling.cpp)
    target_link_libraries(02_scoped_profiling PRIVATE memsentry)

    add_executable(03_snapshot_diffing examples/03_snapshot_diffing.cpp)
    target_link_libraries(03_snapshot_diffing PRIVATE memsentry)

    add_executable(04_buffer_overflow examples/04_buffer_overflow.cpp)
    target_link_libraries(04_buffer_overflow PRIVATE memsentry)

    add_executable(demo examples/demo.cpp)
    target_link_libraries(demo PRIVATE memsentry)
endif()

if (MEMSENTRY_BUILD_TESTS)
    enable_testing()

    add_executable(test_tracker tests/test_tracker.cpp)
    target_link_libraries(test_tracker PRIVATE memsentry)
    add_test(NAME test_tracker COMMAND $<TARGET_FILE:test_tracker>)

    add_executable(test_canary tests/test_canary.cpp)
    target_link_libraries(test_canary PRIVATE memsentry)
    add_test(NAME test_canary COMMAND $<TARGET_FILE:test_canary>)

    add_executable(test_suite tests/test_suite.cpp)
    target_link_libraries(test_suite PRIVATE memsentry)
    add_test(NAME test_suite COMMAND $<TARGET_FILE:test_suite>)

    add_executable(test_untracked_fallback tests/test_untracked_fallback.cpp)
    target_link_libraries(test_untracked_fallback PRIVATE memsentry)
    add_test(NAME test_untracked_fallback COMMAND $<TARGET_FILE:test_untracked_fallback>)

    add_executable(benchmark tests/benchmark.cpp)
    target_link_libraries(benchmark PRIVATE memsentry)
endif()
