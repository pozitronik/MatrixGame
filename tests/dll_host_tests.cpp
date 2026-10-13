// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MatrixGameDll.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace {

const char *library_path;

void write_rejected_configuration(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    constexpr std::array<unsigned char, 8> invalid = {'S', 'T', 'R', 'G', 2, 0, 0, 0};
    file.write(reinterpret_cast<const char *>(invalid.data()), invalid.size());
    MG_CHECK(file.good());
}

class HostFixture {
    std::filesystem::path previous_ = std::filesystem::current_path();
    std::filesystem::path directory_;

public:
    explicit HostFixture(const std::filesystem::path &configuration = "CFG/robots.dat") {
        std::array<wchar_t, 32768> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
        MG_CHECK(length > 0 && length < executable.size());
        directory_ = std::filesystem::path(executable.data()).parent_path() /
                     ("matrixgame-dll-host-" + std::to_string(GetCurrentProcessId()));
        MG_CHECK(std::filesystem::create_directory(directory_));
        write_rejected_configuration(directory_ / configuration);
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

using Getter = SMGDRobotInterface *(__cdecl *)();

Getter as_getter(FARPROC symbol) {
    Getter get_interface;
    static_assert(sizeof(get_interface) == sizeof(symbol));
    std::memcpy(&get_interface, &symbol, sizeof(symbol));
    return get_interface;
}

struct LoadedLibrary {
    std::unique_ptr<std::remove_pointer_t<HINSTANCE>, UnloadModule> module;
    Getter get_interface;

    LoadedLibrary() {
        auto absolute_library = std::filesystem::absolute(library_path);
        absolute_library.make_preferred();
        const HMODULE loaded = LoadLibraryExW(absolute_library.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!loaded) {
            throw std::runtime_error("Cannot load the test DLL; Win32 error=" + std::to_string(GetLastError()));
        }
        module.reset(loaded);
        SetUnhandledExceptionFilter(nullptr);
        auto symbol = GetProcAddress(loaded, "GetRobotInterface");
        if (!symbol) symbol = GetProcAddress(loaded, "_GetRobotInterface");
        MG_CHECK(symbol != nullptr);
        get_interface = as_getter(symbol);
    }
};

// Message-only host window: no desktop or DirectX device is required.
std::unique_ptr<std::remove_pointer_t<HWND>, DestroyWindow> host_window() {
    std::unique_ptr<std::remove_pointer_t<HWND>, DestroyWindow> window(
        CreateWindowExW(0, L"STATIC", L"Synthetic DLL host", 0, 0, 0, 1, 1,
                        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr));
    MG_CHECK(window != nullptr);
    return window;
}

std::string diagnostics() {
    std::ifstream file("matrixgame-dll-errors.log", std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Cleanup must release the configuration handle before returning to its host.
bool released(const wchar_t *path) {
    const HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

// Host callbacks that only count invocations.
int host_calls;
void __stdcall host_sound(wchar *) { ++host_calls; }
dword __stdcall host_sound_create(wchar *, int, int) {
    ++host_calls;
    return 0;
}
void __stdcall host_sound_id(dword) { ++host_calls; }
int __stdcall host_sound_playing(dword) {
    ++host_calls;
    return 0;
}
void __stdcall host_sound_value(dword, float) { ++host_calls; }
float __stdcall host_sound_get(dword) {
    ++host_calls;
    return 0.0f;
}
void __stdcall host_text(wchar *, wchar *, DWORD, int, int, int, int, int, int, int, Base::CRect *,
                         SMGDRangersInterfaceText *) {
    ++host_calls;
}
void __stdcall host_text_clear(SMGDRangersInterfaceText *) { ++host_calls; }
void __stdcall host_value(float) { ++host_calls; }
void __stdcall host_event() { ++host_calls; }
float __stdcall host_music() {
    ++host_calls;
    return 1.0f;
}

SMGDRangersInterface recording_host() {
    return {host_sound,       host_sound_create, host_sound_id,    host_sound_id, host_sound_playing,
            host_sound_value, host_sound_value,  host_sound_get,   host_sound_get, host_text,
            host_text_clear,  host_value,        host_event,       host_event,    host_music,
            host_value};
}

void rejected_configuration() {
    HostFixture fixture;
    LoadedLibrary library;
    auto *api = library.get_interface();
    MG_CHECK(api && api->m_Init && api->m_Run && api->m_Deinit);

    auto window = host_window();
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
        MG_CHECK(released(L"CFG/robots.dat"));
    }
    api->m_Deinit();
    const auto diagnostic = diagnostics();
    MG_CHECK(diagnostic.find("MatrixGame DLL Run failed:") != std::string::npos);
    MG_CHECK(diagnostic.find("Invalid packed configuration") != std::string::npos);
}

void entry_table() {
    LoadedLibrary library;
    // The original host resolves the undecorated cdecl export.
    const auto exact = GetProcAddress(library.module.get(), "GetRobotInterface");
    MG_CHECK(exact != nullptr);
    auto *api = as_getter(exact)();
    MG_CHECK(api && api == library.get_interface() && api->m_Init && api->m_Deinit && api->m_Support);
    MG_CHECK(api->m_Run);
    // Registering and releasing the borrowed callback table invokes none of its callbacks.
    host_calls = 0;
    SMGDRangersInterface host = recording_host();
    api->m_Init(&host);
    api->m_Init(&host);
    api->m_Deinit();
    api->m_Deinit();
    MG_CHECK(host_calls == 0);
}

void language_configuration() {
    // A host language selects CFG/<lang>/robots.dat instead of CFG/robots.dat.
    HostFixture fixture("CFG/xx/robots.dat");
    write_rejected_configuration("CFG/robots.dat");
    LoadedLibrary library;
    auto *api = library.get_interface();
    auto window = host_window();
    const LONG_PTR procedure = GetWindowLongPtrW(window.get(), GWLP_WNDPROC);
    host_calls = 0;
    SMGDRangersInterface host = recording_host();
    const SMGDRangersInterface host_before = host;
    api->m_Init(&host);

    // Host strings and settings are borrowed input; the entry point must not modify them.
    wchar map[] = L"host.cmap", lang[] = L"xx", start[] = L"1EzStart", win[] = L"Win", loss[] = L"Loss";
    wchar planet[] = L"Planet", other[] = L"yy";
    const std::wstring strings_before = std::wstring(map) + lang + start + win + loss + planet + other;
    SRobotsSettings settings;
    std::memset(&settings, 0, sizeof(settings));
    settings.m_ShowStencilShadows = settings.m_VSync = true;
    settings.m_SkyBox = 2;
    settings.m_BPP = 32;
    settings.m_ResolutionX = 1024;
    settings.m_ResolutionY = 768;
    settings.m_Brightness = settings.m_Contrast = 0.5f;
    std::array<unsigned char, sizeof(SRobotsSettings)> settings_before;
    std::memcpy(settings_before.data(), &settings, sizeof(settings));
    SRobotGameState state{41, 42, 43, 44, 45, 46};
    const auto state_before = state;

    MG_CHECK(api->m_Run(GetModuleHandleW(nullptr), window.get(), map, &settings, lang, start, win, loss, planet,
                        &state) == MATRIXGAME_RUN_ERROR);
    SetUnhandledExceptionFilter(nullptr);
    MG_CHECK(diagnostics().find("Invalid packed configuration: cfg\\xx\\robots.dat") != std::string::npos);
    MG_CHECK(released(L"CFG/xx/robots.dat") && released(L"CFG/robots.dat"));

    // Without a language-specific file the root packed file is not used; startup falls back to text data.
    MG_CHECK(api->m_Run(GetModuleHandleW(nullptr), window.get(), map, &settings, other, start, win, loss, planet,
                        &state) == MATRIXGAME_RUN_ERROR);
    SetUnhandledExceptionFilter(nullptr);
    const auto diagnostic = diagnostics();
    MG_CHECK(diagnostic.find("cfg\\robots\\data.txt") != std::string::npos);
    MG_CHECK(diagnostic.find("cfg\\yy\\robots.dat") == std::string::npos);
    MG_CHECK(released(L"CFG/robots.dat"));

    api->m_Deinit();
    MG_CHECK(host_calls == 0 && std::memcmp(&host, &host_before, sizeof(host)) == 0);
    MG_CHECK(std::wstring(map) + lang + start + win + loss + planet + other == strings_before);
    MG_CHECK(std::memcmp(&settings, settings_before.data(), sizeof(settings)) == 0);
    MG_CHECK(std::memcmp(&state, &state_before, sizeof(state)) == 0);
    MG_CHECK(IsWindow(window.get()) && GetWindowLongPtrW(window.get(), GWLP_WNDPROC) == procedure);
}

constexpr tests::Case cases[] = {
    {"game.dll.native_rejected_configuration", rejected_configuration},
    {"game.dll.native_entry_table", entry_table},
    {"game.dll.native_language_configuration", language_configuration},
};

}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3) return 2;
    library_path = argv[2];
    return tests::run(2, argv, cases);
}
