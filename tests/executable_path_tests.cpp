// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "ExecutablePath.hpp"

#include <algorithm>

namespace {
std::wstring modulePath;
int queries;

DWORD WINAPI supplied_path(HMODULE module, LPWSTR buffer, DWORD capacity) {
    MG_CHECK(module == nullptr);
    ++queries;
    const auto count = std::min(static_cast<size_t>(capacity - 1), modulePath.size());
    std::copy_n(modulePath.data(), count, buffer);
    buffer[count] = L'\0';
    if (modulePath.size() >= capacity) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return capacity;
    }
    return static_cast<DWORD>(modulePath.size());
}

void unicode_module_path() {
    modulePath = L"C:\\synthetic folder\\\u0418\u0433\u0440\u0430\\MatrixGame.exe";
    queries = 0;
    const auto path = Startup::ExecutablePath(supplied_path);
    MG_CHECK(path.native() == modulePath);
    MG_CHECK(path.filename() == L"MatrixGame.exe");
    MG_CHECK(!path.parent_path().empty());
    MG_CHECK(queries == 1);
}

void long_module_path() {
    modulePath = L"C:\\";
    for (int part = 0; part < 4; ++part) modulePath += std::wstring(220, L'a') + L"\\";
    modulePath += L"MatrixGame.exe";
    queries = 0;
    MG_CHECK(Startup::ExecutablePath(supplied_path).native() == modulePath);
    MG_CHECK(queries == 3);
}

DWORD WINAPI failed_path(HMODULE, LPWSTR, DWORD) {
    SetLastError(ERROR_ACCESS_DENIED);
    return 0;
}

void module_failure_diagnostic() {
    bool caught = false;
    try { Startup::ExecutablePath(failed_path); }
    catch (const std::runtime_error &error) {
        const std::string_view message(error.what());
        caught = message.find("GetModuleFileNameW") != message.npos && message.find("error=5") != message.npos;
    }
    MG_CHECK(caught);
}

DWORD WINAPI truncated_path(HMODULE, LPWSTR buffer, DWORD capacity) {
    ++queries;
    std::fill_n(buffer, capacity, L'x');
    return capacity;
}

void truncated_query_is_bounded() {
    queries = 0;
    bool caught = false;
    try { Startup::ExecutablePath(truncated_path); }
    catch (const std::runtime_error &error) {
        caught = std::string_view(error.what()).find("32767") != std::string_view::npos;
    }
    MG_CHECK(caught);
    MG_CHECK(queries <= 8);
}

void native_module_path() {
    const auto path = Startup::ExecutablePath();
    MG_CHECK(path.is_absolute());
    MG_CHECK(std::filesystem::is_regular_file(path));
    MG_CHECK(path.filename() == L"matrixgame_path_tests.exe");
}

constexpr tests::Case cases[] = {
    {"game.startup.module_unicode", unicode_module_path},
    {"game.startup.module_long", long_module_path},
    {"game.startup.module_failure", module_failure_diagnostic},
    {"game.startup.module_truncated", truncated_query_is_bounded},
    {"game.startup.module_native", native_module_path},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    return tests::run(argc, argv, cases);
}
