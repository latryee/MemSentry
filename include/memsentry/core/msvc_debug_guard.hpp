#pragma once

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>

namespace memsentry::core {

struct MsvcDebugInitializer {
    MsvcDebugInitializer() noexcept {
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
};

inline MsvcDebugInitializer g_msvc_debug_initializer;

}  // namespace memsentry::core
#endif
