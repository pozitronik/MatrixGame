// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "CStorage.hpp"
#include "stupid_logger.hpp"

logger_type lgr{std::cerr};

namespace {

void single_message() {
    for (const bool use_string : {false, true}) {
        bool caught = false;
        try {
            if (use_string) {
                ERROR_S(std::wstring(L"Synthetic startup failure"));
            }
            ERROR_S(L"Synthetic startup failure");
        }
        catch (const Base::CException &error) {
            MG_CHECK(error.Info().find(L"Text: Synthetic startup failure") != std::wstring::npos);
            caught = true;
        }
        MG_CHECK(caught);
    }
}

void empty_trace() {
    bool caught = false;
    try {
        throw Base::CException("synthetic-startup.cpp", 41);
    }
    catch (const Base::CException &error) {
        const auto diagnostic = error.Info();
        MG_CHECK(diagnostic.find(L"File=synthetic-startup.cpp\nLine=41\n") != std::wstring::npos);
        caught = true;
    }
    MG_CHECK(caught);
}

void missing_file() {
    constexpr auto filename = L"__matrixgame_test_missing_directory__/map.cmap";
    MG_CHECK(GetFileAttributesW(filename) == INVALID_FILE_ATTRIBUTES);
    MG_CHECK(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND);
    Base::CStorage storage;
    bool caught = false;
    try {
        storage.Load(filename);
    }
    catch (const Base::CException &error) {
        const auto diagnostic = error.Info();
        MG_CHECK(diagnostic.find(L"Error open file:") != std::wstring::npos);
        MG_CHECK(diagnostic.find(filename) != std::wstring::npos);
        caught = true;
    }
    MG_CHECK(caught);
}

constexpr tests::Case cases[] = {
    {"base.error.single_message", single_message},
    {"base.error.empty_trace", empty_trace},
    {"base.error.missing_file", missing_file},
};

}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result;
    if (argc == 2 && std::string_view(argv[1]) == "base.error.empty_trace") {
        result = tests::run(argc, argv, cases);
    }
    else {
        DTRACE();
        result = tests::run(argc, argv, cases);
    }
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Error test leaked tracked heap allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
