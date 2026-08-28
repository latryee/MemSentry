cmake_minimum_required(VERSION 3.16)
project(memsentry VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

option(MEMSENTRY_ENABLE_ASAN "Enable Address & UB Sanitizers" OFF)
option(MEMSENTRY_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(MEMSENTRY_ENABLE_COVERAGE "Enable GCC Code Coverage" OFF)

if (MEMSENTRY_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if (MEMSENTRY_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if (MEMSENTRY_ENABLE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

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
    include/memsentry/core/suppression_engine.hpp
    include/memsentry/stacktrace/stacktrace.hpp
    include/memsentry/profiler/scope_tag.hpp
    include/memsentry/profiler/snapshot.hpp
    include/memsentry/profiler/histogram.hpp
    include/memsentry/profiler/fragmentation.hpp
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
    add_compile_options(/EHsc /W3 /Zc:__cplusplus /Zc:tlsGuards- /D_CRT_SECURE_NO_WARNINGS)
    target_link_options(memsentry INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/FORCE:MULTIPLE>)
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

# Installation and CMake package config export
install(TARGETS memsentry
    EXPORT memsentryTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT memsentryTargets
    FILE memsentryTargets.cmake
    NAMESPACE memsentry::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memsentry
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/memsentryConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/memsentryConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/memsentryConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memsentry
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/memsentryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/memsentryConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memsentry
)

option(MEMSENTRY_BUILD_EXAMPLES "Build MemSentry examples" ON)
option(MEMSENTRY_BUILD_TESTS "Build MemSentry tests" ON)
option(MEMSENTRY_BUILD_FUZZERS "Build libFuzzer targets" OFF)

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

    add_executable(test_c_alloc_hooks tests/test_c_alloc_hooks.cpp)
    target_link_libraries(test_c_alloc_hooks PRIVATE memsentry)
    add_test(NAME test_c_alloc_hooks COMMAND $<TARGET_FILE:test_c_alloc_hooks>)

    add_executable(test_canary_race tests/test_canary_race.cpp)
    target_link_libraries(test_canary_race PRIVATE memsentry)
    add_test(NAME test_canary_race COMMAND $<TARGET_FILE:test_canary_race>)

    add_executable(test_sanitizer_matrix tests/test_sanitizer_matrix.cpp)
    target_link_libraries(test_sanitizer_matrix PRIVATE memsentry)
    add_test(NAME test_sanitizer_matrix COMMAND $<TARGET_FILE:test_sanitizer_matrix>)

    add_executable(test_suppression tests/test_suppression.cpp)
    target_link_libraries(test_suppression PRIVATE memsentry)
    add_test(NAME test_suppression COMMAND $<TARGET_FILE:test_suppression>)

    add_executable(test_realloc tests/test_realloc.cpp)
    target_link_libraries(test_realloc PRIVATE memsentry)
    add_test(NAME test_realloc COMMAND $<TARGET_FILE:test_realloc>)

    add_executable(test_fragmentation tests/test_fragmentation.cpp)
    target_link_libraries(test_fragmentation PRIVATE memsentry)
    add_test(NAME test_fragmentation COMMAND $<TARGET_FILE:test_fragmentation>)

    add_executable(test_sampling tests/test_sampling.cpp)
    target_link_libraries(test_sampling PRIVATE memsentry)
    add_test(NAME test_sampling COMMAND $<TARGET_FILE:test_sampling>)

    add_executable(test_watchdog tests/test_watchdog.cpp)
    target_link_libraries(test_watchdog PRIVATE memsentry)
    add_test(NAME test_watchdog COMMAND $<TARGET_FILE:test_watchdog>)

    add_executable(benchmark tests/benchmark.cpp)
    target_link_libraries(benchmark PRIVATE memsentry)
endif()

if (MEMSENTRY_BUILD_FUZZERS)
    add_executable(fuzz_allocator fuzz/fuzz_allocator.cpp)
    target_compile_options(fuzz_allocator PRIVATE -fsanitize=fuzzer,address)
    target_link_options(fuzz_allocator PRIVATE -fsanitize=fuzzer,address)
    target_link_libraries(fuzz_allocator PRIVATE memsentry)
endif()
