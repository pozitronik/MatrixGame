// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdio>
#include <windows.h>
#include <dbghelp.h>
#endif

namespace tests {

#if defined(_MSC_VER) && defined(_DEBUG)
inline void print_native_location(DWORD64 address) {
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

inline void print_native_stack() {
    void *frames[24]{};
    const auto count = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (unsigned index = 0; index < count; ++index) {
        print_native_location(reinterpret_cast<DWORD64>(frames[index]));
    }
    std::fflush(stderr);
}

inline LONG WINAPI report_native_exception(EXCEPTION_POINTERS *exception) {
    std::fprintf(stderr, "Native exception 0x%08lx\n", exception->ExceptionRecord->ExceptionCode);
    print_native_location(reinterpret_cast<DWORD64>(exception->ExceptionRecord->ExceptionAddress));
    print_native_stack();
    std::_Exit(EXIT_FAILURE);
}

inline int __cdecl report_crt_failure(int type, char *message, int *) {
    std::fputs(message ? message : "MSVC Debug runtime failure\n", stderr);
    std::fflush(stderr);
    if (type == _CRT_ERROR || type == _CRT_ASSERT) {
        print_native_stack();
        std::_Exit(EXIT_FAILURE);
    }
    return 1;
}

inline void configure_crt_reports() {
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    SetUnhandledExceptionFilter(report_native_exception);
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, report_crt_failure);
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
}
#endif

struct Case {
    const char *name;
    void (*run)();
};

inline void require(bool condition, const char *expression, const char *file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + expression);
    }
}

inline int run(int argc, char **argv, std::span<const Case> cases) {
#if defined(_MSC_VER) && defined(_DEBUG)
    configure_crt_reports();
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--list") {
        for (const auto &test : cases) {
            std::cout << test.name << '\n';
        }
        return EXIT_SUCCESS;
    }
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <test-name> | --list\n";
        return 2;
    }
    for (const auto &test : cases) {
        if (std::string_view(argv[1]) != test.name) {
            continue;
        }
        try {
            test.run();
            std::cout << "Passed: " << test.name << '\n';
            return EXIT_SUCCESS;
        }
        catch (const std::exception &error) {
            std::cerr << "Failed: " << test.name << "\n" << error.what() << '\n';
        }
        catch (...) {
            std::cerr << "Failed: " << test.name << "\nUnexpected exception\n";
        }
        return EXIT_FAILURE;
    }
    std::cerr << "Unknown test: " << argv[1] << "\nUse --list to see available tests.\n";
    return 2;
}

}  // namespace tests

// Unlike assert(), these checks remain active in Release builds.
#define MG_CHECK(expression) ::tests::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
