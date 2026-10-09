// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#if defined(_MSC_VER) && defined(_DEBUG)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <crtdbg.h>
#include <cstdio>

namespace tests {
namespace {

void print_native_location(DWORD64 address) {
    alignas(SYMBOL_INFO) unsigned char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
        std::fprintf(stderr, "  %s+0x%llx", symbol->Name, static_cast<unsigned long long>(displacement));
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD offset = 0;
        if (SymGetLineFromAddr64(GetCurrentProcess(), address, &offset, &line)) {
            std::fprintf(stderr, " %s:%lu", line.FileName, line.LineNumber);
        }
        std::fputc('\n', stderr);
    } else {
        std::fprintf(stderr, "  address=0x%llx\n", static_cast<unsigned long long>(address));
    }
}

void print_native_stack() {
    void *frames[24]{};
    const auto count = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (unsigned index = 0; index < count; ++index) {
        print_native_location(reinterpret_cast<DWORD64>(frames[index]));
    }
    std::fflush(stderr);
}

LONG WINAPI report_native_exception(EXCEPTION_POINTERS *exception) {
    std::fprintf(stderr, "Native exception 0x%08lx\n", exception->ExceptionRecord->ExceptionCode);
    print_native_location(reinterpret_cast<DWORD64>(exception->ExceptionRecord->ExceptionAddress));
    print_native_stack();
    std::_Exit(EXIT_FAILURE);
}

int __cdecl report_crt_failure(int type, char *message, int *) {
    std::fputs(message ? message : "MSVC Debug runtime failure\n", stderr);
    std::fflush(stderr);
    if (type == _CRT_ERROR || type == _CRT_ASSERT) {
        print_native_stack();
        std::_Exit(EXIT_FAILURE);
    }
    return 1;
}

}  // namespace

void configure_crt_reports() {
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    SetUnhandledExceptionFilter(report_native_exception);
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, report_crt_failure);
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
}

}  // namespace tests
#endif
