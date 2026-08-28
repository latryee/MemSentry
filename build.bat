@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo               MemSentry - Build Script
echo =======================================================

if not exist bin mkdir bin

set CLANG_BIN=
where clang++ >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set CLANG_BIN=clang++
) else if exist "%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe" (
    set "CLANG_BIN=%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe"
)

if defined CLANG_BIN (
    echo [*] Detected Clang compiler: !CLANG_BIN!
    set CXXFLAGS=-static -std=c++20 -O2 -Iinclude -Wall -Wextra -Wno-unused-variable -Wno-unused-but-set-variable
    set LIBS=-ldbghelp
    set SOURCES=src/memsentry.cpp src/allocator_hooks.cpp src/stacktrace.cpp src/snapshot.cpp src/reporter.cpp

    echo [*] Compiling Examples...
    "!CLANG_BIN!" !CXXFLAGS! examples/01_basic_leak.cpp !SOURCES! -o bin/01_basic_leak.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! examples/02_scoped_profiling.cpp !SOURCES! -o bin/02_scoped_profiling.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! examples/03_snapshot_diffing.cpp !SOURCES! -o bin/03_snapshot_diffing.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! examples/04_buffer_overflow.cpp !SOURCES! -o bin/04_buffer_overflow.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! examples/demo.cpp !SOURCES! -o bin/demo.exe !LIBS!

    echo [*] Compiling Test Suites...
    "!CLANG_BIN!" !CXXFLAGS! tests/test_tracker.cpp !SOURCES! -o bin/test_tracker.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_canary.cpp !SOURCES! -o bin/test_canary.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_suite.cpp !SOURCES! -o bin/test_suite.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_untracked_fallback.cpp !SOURCES! -o bin/test_untracked_fallback.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_c_alloc_hooks.cpp !SOURCES! -o bin/test_c_alloc_hooks.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_canary_race.cpp !SOURCES! -o bin/test_canary_race.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_sanitizer_matrix.cpp !SOURCES! -o bin/test_sanitizer_matrix.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_suppression.cpp !SOURCES! -o bin/test_suppression.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_realloc.cpp !SOURCES! -o bin/test_realloc.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_fragmentation.cpp !SOURCES! -o bin/test_fragmentation.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_sampling.cpp !SOURCES! -o bin/test_sampling.exe !LIBS!
    "!CLANG_BIN!" !CXXFLAGS! tests/test_watchdog.cpp !SOURCES! -o bin/test_watchdog.exe !LIBS!

    echo [*] Compiling Benchmark Suite...
    "!CLANG_BIN!" !CXXFLAGS! -O3 tests/benchmark.cpp !SOURCES! -o bin/benchmark.exe !LIBS!

    echo =======================================================
    echo  Build complete! Binaries are in the 'bin\' folder.
    echo =======================================================
    exit /b 0
)

echo [*] Attempting MSVC build...
where cl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    )
)

set CXXFLAGS=/EHsc /std:c++20 /O2 /Iinclude /W3 /Zi /FS /D_CRT_SECURE_NO_WARNINGS
set LIBS=dbghelp.lib
set SOURCES=src\memsentry.cpp src\allocator_hooks.cpp src\stacktrace.cpp src\snapshot.cpp src\reporter.cpp

echo [*] Compiling Examples...
cl %CXXFLAGS% examples\01_basic_leak.cpp %SOURCES% /Fe:bin\01_basic_leak.exe /link %LIBS%
cl %CXXFLAGS% examples\02_scoped_profiling.cpp %SOURCES% /Fe:bin\02_scoped_profiling.exe /link %LIBS%
cl %CXXFLAGS% examples\03_snapshot_diffing.cpp %SOURCES% /Fe:bin\03_snapshot_diffing.exe /link %LIBS%
cl %CXXFLAGS% examples\04_buffer_overflow.cpp %SOURCES% /Fe:bin\04_buffer_overflow.exe /link %LIBS%
cl %CXXFLAGS% examples\demo.cpp %SOURCES% /Fe:bin\demo.exe /link %LIBS%

echo [*] Compiling Tests and Benchmarks...
cl %CXXFLAGS% tests\test_tracker.cpp %SOURCES% /Fe:bin\test_tracker.exe /link %LIBS%
cl %CXXFLAGS% tests\test_canary.cpp %SOURCES% /Fe:bin\test_canary.exe /link %LIBS%
cl %CXXFLAGS% tests\test_suite.cpp %SOURCES% /Fe:bin\test_suite.exe /link %LIBS%
cl %CXXFLAGS% tests\test_untracked_fallback.cpp %SOURCES% /Fe:bin\test_untracked_fallback.exe /link %LIBS%
cl %CXXFLAGS% tests\test_c_alloc_hooks.cpp %SOURCES% /Fe:bin\test_c_alloc_hooks.exe /link %LIBS%
cl %CXXFLAGS% tests\test_canary_race.cpp %SOURCES% /Fe:bin\test_canary_race.exe /link %LIBS%
cl %CXXFLAGS% tests\test_sanitizer_matrix.cpp %SOURCES% /Fe:bin\test_sanitizer_matrix.exe /link %LIBS%
cl %CXXFLAGS% tests\test_suppression.cpp %SOURCES% /Fe:bin\test_suppression.exe /link %LIBS%
cl %CXXFLAGS% tests\test_realloc.cpp %SOURCES% /Fe:bin\test_realloc.exe /link %LIBS%
cl %CXXFLAGS% tests\test_fragmentation.cpp %SOURCES% /Fe:bin\test_fragmentation.exe /link %LIBS%
cl %CXXFLAGS% tests\test_sampling.cpp %SOURCES% /Fe:bin\test_sampling.exe /link %LIBS%
cl %CXXFLAGS% tests\test_watchdog.cpp %SOURCES% /Fe:bin\test_watchdog.exe /link %LIBS%
cl %CXXFLAGS% tests\benchmark.cpp %SOURCES% /Fe:bin\benchmark.exe /link %LIBS%

del *.obj >nul 2>&1

echo =======================================================
echo  Build complete! Binaries are in the 'bin\' folder.
echo =======================================================
