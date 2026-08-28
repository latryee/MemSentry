#include "memsentry/stacktrace/stacktrace.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <sstream>
#include <iomanip>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tchar.h>
#if defined(_MSC_VER)
#pragma comment(lib, "dbghelp.lib")
#endif
#elif defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <cstdlib>
#endif

namespace memsentry::stacktrace {

StackTraceProvider& StackTraceProvider::instance() noexcept {
    alignas(StackTraceProvider) static char storage[sizeof(StackTraceProvider)];
    static StackTraceProvider* inst = []() {
        core::RecursionGuard guard;
        return new (storage) StackTraceProvider();
    }();
    return *inst;
}

StackTraceProvider::StackTraceProvider() {
    core::RecursionGuard guard;
}

StackTraceProvider::~StackTraceProvider() = default;

void StackTraceProvider::initialize() {
    // Lazy initialization in resolve_frame to avoid early loader/symbol issues on MSVC Debug
}

void StackTraceProvider::cleanup() {
    core::RecursionGuard guard;
    std::lock_guard<std::mutex> lock(symbol_mutex_);
    if (!initialized_) return;

#if defined(_WIN32)
    SymCleanup(GetCurrentProcess());
#endif

    initialized_ = false;
    symbol_cache_.clear();
}

uint16_t StackTraceProvider::capture(void** out_frames, uint32_t max_depth, uint32_t skip_frames) noexcept {
    if (!out_frames || max_depth == 0) return 0;

#if defined(_WIN32)
#if defined(_MSC_VER)
    __try {
        USHORT captured = CaptureStackBackTrace(
            static_cast<ULONG>(skip_frames),
            static_cast<ULONG>(max_depth),
            out_frames,
            nullptr
        );
        return static_cast<uint16_t>(captured);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
#else
    USHORT captured = CaptureStackBackTrace(
        static_cast<ULONG>(skip_frames),
        static_cast<ULONG>(max_depth),
        out_frames,
        nullptr
    );
    return static_cast<uint16_t>(captured);
#endif
#elif defined(__linux__) || defined(__APPLE__)
    constexpr int MAX_CAPTURE = 64;
    int depth = static_cast<int>(max_depth + skip_frames);
    if (depth > MAX_CAPTURE) depth = MAX_CAPTURE;
    void* buffer[MAX_CAPTURE];
    int count = backtrace(buffer, depth);
    if (count <= static_cast<int>(skip_frames)) return 0;

    uint16_t captured = 0;
    for (int i = static_cast<int>(skip_frames); i < count && captured < max_depth; ++i) {
        out_frames[captured++] = buffer[i];
    }
    return captured;
#else
    return 0;
#endif
}

StackFrame StackTraceProvider::resolve_frame(uintptr_t address) {
    core::RecursionGuard guard;
    std::lock_guard<std::mutex> lock(symbol_mutex_);

    auto it = symbol_cache_.find(address);
    if (it != symbol_cache_.end()) {
        return it->second;
    }

    StackFrame frame;
    frame.address = address;

    std::ostringstream addr_oss;
    addr_oss << "0x" << std::hex << std::setw(sizeof(void*) * 2) << std::setfill('0') << address;
    frame.symbol_name = addr_oss.str();

#if defined(_WIN32)
    HANDLE process = GetCurrentProcess();
    if (!initialized_) {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);
        initialized_ = true;
    }

    alignas(SYMBOL_INFO) uint8_t buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 displacement = 0;
    if (SymFromAddr(process, static_cast<DWORD64>(address), &displacement, symbol)) {
        if (symbol->Name[0] != '\0') {
            frame.symbol_name = symbol->Name;
        }
    }

    IMAGEHLP_LINE64 line_info{};
    line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD line_displacement = 0;
    if (SymGetLineFromAddr64(process, static_cast<DWORD64>(address), &line_displacement, &line_info)) {
        if (line_info.FileName) {
            frame.file_name = line_info.FileName;
        }
        frame.line_number = line_info.LineNumber;
    }
#elif defined(__linux__) || defined(__APPLE__)
    Dl_info dlinfo;
    if (dladdr(reinterpret_cast<void*>(address), &dlinfo) && dlinfo.dli_sname) {
        int status = 0;
        char* demangled = abi::__cxa_demangle(dlinfo.dli_sname, nullptr, nullptr, &status);
        if (status == 0 && demangled) {
            frame.symbol_name = demangled;
            std::free(demangled);
        } else {
            frame.symbol_name = dlinfo.dli_sname;
        }

        if (dlinfo.dli_fname) {
            frame.file_name = dlinfo.dli_fname;
        }
    }
#endif

    symbol_cache_[address] = frame;
    return frame;
}

std::vector<StackFrame> StackTraceProvider::resolve(const void* const* frames, uint16_t count) {
    std::vector<StackFrame> resolved;
    resolved.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (!frames[i]) continue;
        resolved.push_back(resolve_frame(reinterpret_cast<uintptr_t>(frames[i])));
    }
    return resolved;
}

}
