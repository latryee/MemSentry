#pragma once

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>

namespace memsentry::core {

struct MsvcDebugInitializer {
    MsvcDebugInitializer() noexcept {
        // Suppress all CRT debug report popups for headless CI environments
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

        // Disable CRT debug heap validation entirely.
        // MemSentry manages its own heap via HeapAlloc/HeapFree,
        // so CRT's _CrtIsValidHeapPointer checks must be turned off.
        _CrtSetDbgFlag(0);
    }
};

inline MsvcDebugInitializer g_msvc_debug_initializer;

}  // namespace memsentry::core
#endif
