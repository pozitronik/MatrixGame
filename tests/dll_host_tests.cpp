// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MatrixGameDll.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

namespace {

const char *library_path;

class HostFixture {
    std::filesystem::path previous_ = std::filesystem::current_path();
    std::filesystem::path directory_;

public:
    HostFixture() {
        std::array<wchar_t, 32768> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
        MG_CHECK(length > 0 && length < executable.size());
        directory_ = std::filesystem::path(executable.data()).parent_path() /
                     ("matrixgame-dll-host-" + std::to_string(GetCurrentProcessId()));
        MG_CHECK(std::filesystem::create_directory(directory_));
        std::filesystem::create_directory(directory_ / "CFG");
        std::ofstream file(directory_ / "CFG/robots.dat", std::ios::binary);
        constexpr std::array<unsigned char, 8> invalid = {'S', 'T', 'R', 'G', 2, 0, 0, 0};
        file.write(reinterpret_cast<const char *>(invalid.data()), invalid.size());
        MG_CHECK(file.good());
        file.close();
        std::filesystem::current_path(directory_);
    }
    ~HostFixture() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
        std::filesystem::remove_all(directory_, error);
        if (error) std::abort();
    }
};

struct UnloadModule {
    void operator()(HINSTANCE module) const noexcept {
        // Legacy initialization installs a DLL-owned native exception filter.
        SetUnhandledExceptionFilter(nullptr);
        FreeLibrary(module);
    }
};

struct DestroyWindow {
    void operator()(HWND window) const noexcept { ::DestroyWindow(window); }
};

void rejected_configuration() {
    auto absolute_library = std::filesystem::absolute(library_path);
    absolute_library.make_preferred();
    HostFixture fixture;
    const HMODULE loaded = LoadLibraryExW(absolute_library.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!loaded) {
        throw std::runtime_error("Cannot load the test DLL; Win32 error=" + std::to_string(GetLastError()));
    }
    std::unique_ptr<std::remove_pointer_t<HINSTANCE>, UnloadModule> module(loaded);
    SetUnhandledExceptionFilter(nullptr);
    auto symbol = GetProcAddress(loaded, "GetRobotInterface");
    if (!symbol) symbol = GetProcAddress(loaded, "_GetRobotInterface");
    MG_CHECK(symbol != nullptr);
    using Getter = SMGDRobotInterface *(__cdecl *)();
    Getter get_interface;
    static_assert(sizeof(get_interface) == sizeof(symbol));
    std::memcpy(&get_interface, &symbol, sizeof(symbol));
    auto *api = get_interface();
    MG_CHECK(api && api->m_Init && api->m_Run && api->m_Deinit);

    // Message-only host window: no desktop or DirectX device is required.
    std::unique_ptr<std::remove_pointer_t<HWND>, DestroyWindow> window(
        CreateWindowExW(0, L"STATIC", L"Synthetic DLL host", 0, 0, 0, 1, 1,
                        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr));
    MG_CHECK(window != nullptr);
    const LONG_PTR procedure = GetWindowLongPtrW(window.get(), GWLP_WNDPROC);
    SMGDRangersInterface callbacks{};
    api->m_Init(&callbacks);
    SRobotsSettings settings{};
    for (int attempt = 0; attempt < 3; ++attempt) {
        SRobotGameState state{41, 42, 43, 44, 45, 46};
        const auto before = state;
        int result;
        try {
            result = api->m_Run(GetModuleHandleW(nullptr), window.get(), nullptr, &settings, nullptr,
                                 nullptr, nullptr, nullptr, nullptr, &state);
        }
        catch (...) {
            throw std::runtime_error("A C++ exception escaped the loaded DLL's Run callback");
        }
        SetUnhandledExceptionFilter(nullptr);
        MG_CHECK(result == MATRIXGAME_RUN_ERROR);
        MG_CHECK(std::memcmp(&before, &state, sizeof(state)) == 0);
        MG_CHECK(IsWindow(window.get()));
        MG_CHECK(GetWindowLongPtrW(window.get(), GWLP_WNDPROC) == procedure);
        // Cleanup must release the configuration handle before returning to its host.
        const HANDLE file = CreateFileW(L"CFG/robots.dat", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        MG_CHECK(file != INVALID_HANDLE_VALUE);
        MG_CHECK(CloseHandle(file));
    }
    api->m_Deinit();
    std::ifstream file("matrixgame-dll-errors.log", std::ios::binary);
    const std::string diagnostic{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    MG_CHECK(diagnostic.find("MatrixGame DLL Run failed:") != std::string::npos);
    MG_CHECK(diagnostic.find("Invalid packed configuration") != std::string::npos);
}

constexpr tests::Case cases[] = {{"game.dll.native_rejected_configuration", rejected_configuration}};

}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3) return 2;
    library_path = argv[2];
    return tests::run(2, argv, cases);
}
