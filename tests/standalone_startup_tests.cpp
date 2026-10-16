// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::filesystem::path game_executable, compiler_directory;

void check_win32(bool result, const char *operation) {
    if (!result) throw std::runtime_error(std::string(operation) + "; Windows error=" + std::to_string(GetLastError()));
}

std::wstring window_text(HWND window) {
    // GetWindowText cannot read another process's edit/static control text.
    DWORD_PTR length = 0;
    if (!SendMessageTimeoutW(window, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 1000, &length)) return {};
    std::wstring text(length + 1, L'\0');
    DWORD_PTR copied = 0;
    if (!SendMessageTimeoutW(window, WM_GETTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()),
                             SMTO_ABORTIFHUNG, 1000, &copied)) return {};
    text.resize(copied);
    return text;
}
std::string bytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    MG_CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
void write(const std::filesystem::path &path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(content.data(), content.size());
    MG_CHECK(file.good());
}
void empty_package(const std::filesystem::path &path) {
    std::string content;
    // Independently encoded empty-root package, as in the resource-file contracts.
    for (uint32_t word : {4u, 12u, 0u, 158u})
        for (int shift = 0; shift < 32; shift += 8) content += char((word >> shift) & 255);
    write(path, content);
}
void rejected_configuration(const std::filesystem::path &app) {
    write(app / "CFG/robots.dat", std::string("STRG\2\0\0\0", 8));
}
struct Environment {
    std::wstring previous;
    bool existed;
    Environment() {
        const DWORD size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        existed = size != 0;
        if (existed) {
            previous.resize(size);
            previous.resize(GetEnvironmentVariableW(L"PATH", previous.data(), size));
        }
        std::array<wchar_t, 32768> executable{};
        const auto length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
        MG_CHECK(length && length < executable.size());
        // The helper stages verified D3DX beside the tests. Compiler runtimes come
        // from the configured compiler, including TestsOnly builds of the EXE.
        const auto test_directory = std::filesystem::path(executable.data()).parent_path();
        const auto path = test_directory.wstring() + L";" + compiler_directory.wstring() + L";" + previous;
        MG_CHECK(SetEnvironmentVariableW(L"PATH", path.c_str()));
    }
    ~Environment() { SetEnvironmentVariableW(L"PATH", existed ? previous.c_str() : nullptr); }
};
struct Deployment {
    std::filesystem::path root = std::filesystem::current_path() /
        ("standalone-startup-" + std::to_string(GetCurrentProcessId()));
    // Both spaces and non-ASCII characters exercise the native module-path route.
    std::filesystem::path app = root / L"Game \u0418\u0433\u0440\u0430 \u65e5\u672c";
    std::filesystem::path caller = root / L"Other working directory";
    Deployment() {
        MG_CHECK(std::filesystem::create_directory(root));
        try {
            std::filesystem::create_directory(app);
            std::filesystem::create_directory(caller);
            std::filesystem::copy_file(game_executable, app / "MatrixGame.exe");
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
            throw;
        }
    }
    ~Deployment() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        if (error) {
            std::cerr << "Cannot remove synthetic startup deployment: " << root << ": " << error.message() << '\n';
            std::abort();
        }
    }
};
struct Desktop {
    std::wstring name = L"MatrixGame-startup-" + std::to_wstring(GetCurrentProcessId());
    HDESK value = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_ENUMERATE | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    Desktop() { check_win32(value != nullptr, "CreateDesktopW"); }
    ~Desktop() { if (value) CloseDesktop(value); }
};
struct Child {
    PROCESS_INFORMATION process{};
    HANDLE job = nullptr;
    Child(const Deployment &deployment, Desktop &desktop) {
        const auto executable = deployment.app / "MatrixGame.exe";
        std::wstring command = L"\"" + executable.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.lpDesktop = desktop.name.data();
        try {
            job = CreateJobObjectW(nullptr, nullptr);
            check_win32(job != nullptr, "CreateJobObjectW");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            check_win32(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
                        "SetInformationJobObject");
            // Assign before any child code runs. The non-inherited job handle
            // makes abnormal harness termination kill the hidden child too.
            // Inherit the harness error mode to suppress loader/fault popups.
            check_win32(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                                      nullptr, deployment.caller.c_str(), &startup, &process), "CreateProcessW");
            check_win32(AssignProcessToJobObject(job, process.hProcess), "AssignProcessToJobObject");
            check_win32(ResumeThread(process.hThread) != DWORD(-1), "ResumeThread");
            CloseHandle(process.hThread);
            process.hThread = nullptr;
        } catch (...) {
            cleanup();
            throw;
        }
    }
    Child(const Child &) = delete;
    Child &operator=(const Child &) = delete;
    ~Child() { cleanup(); }
    void cleanup() noexcept {
        if (process.hThread) CloseHandle(process.hThread);
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 99);
                WaitForSingleObject(process.hProcess, 3000);
            }
            CloseHandle(process.hProcess);
        }
        if (job) CloseHandle(job);
    }
};
struct Dialog {
    DWORD process;
    HWND window = nullptr;
    std::wstring title, text;
    std::vector<HWND> buttons;
    static BOOL CALLBACK child(HWND window, LPARAM parameter) {
        auto &dialog = *reinterpret_cast<Dialog *>(parameter);
        std::array<wchar_t, 32> type{};
        GetClassNameW(window, type.data(), type.size());
        if (std::wstring_view(type.data()) == L"Static") {
            const auto text = window_text(window);
            if (!text.empty()) dialog.text += text + L"\n";
        }
        if (std::wstring_view(type.data()) == L"Button") dialog.buttons.push_back(window);
        return TRUE;
    }
    static BOOL CALLBACK top(HWND window, LPARAM parameter) {
        auto &dialog = *reinterpret_cast<Dialog *>(parameter);
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != dialog.process) return TRUE;
        std::array<wchar_t, 32> type{};
        GetClassNameW(window, type.data(), type.size());
        if (std::wstring_view(type.data()) != L"#32770") return TRUE;
        dialog.window = window;
        dialog.title = window_text(window);
        dialog.text.clear();
        dialog.buttons.clear();
        EnumChildWindows(window, child, parameter);
        if (dialog.text.empty() || dialog.buttons.size() != 1 || !IsWindowEnabled(dialog.buttons.front())) {
            dialog.window = nullptr;
            return TRUE;
        }
        return FALSE;
    }
};
struct Result { std::wstring title, text; std::string log; DWORD exit; };
Result run(const Deployment &deployment) {
    Environment environment;
    Desktop desktop; // Never SwitchDesktop or attach the caller's thread.
    Child child(deployment, desktop);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    Dialog dialog{child.process.dwProcessId, nullptr, {}, {}, {}};
    while (std::chrono::steady_clock::now() < deadline) {
        SetLastError(ERROR_SUCCESS);
        const bool enumerated = EnumDesktopWindows(desktop.value, Dialog::top, reinterpret_cast<LPARAM>(&dialog));
        if (dialog.window) break; // The callback intentionally stops after one ready diagnostic.
        if (!enumerated && GetLastError() != ERROR_SUCCESS) check_win32(false, "EnumDesktopWindows");
        if (WaitForSingleObject(child.process.hProcess, 10) == WAIT_OBJECT_0) break;
    }
    if (!dialog.window) {
        DWORD status = 0;
        GetExitCodeProcess(child.process.hProcess, &status);
        throw std::runtime_error("No startup diagnostic dialog; process status=" + std::to_string(status));
    }
    std::cout << "Startup diagnostic captured; button ID=" << GetDlgCtrlID(dialog.buttons.front()) << '\n' << std::flush;
    // MB_OK can expose IDOK or IDCANCEL depending on Windows; use the actual sole button.
    // Capture the native dialog text, then dismiss only this owned child's dialog.
    DWORD_PTR ignored = 0;
    MG_CHECK(SendMessageTimeoutW(dialog.window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(dialog.buttons.front()), BN_CLICKED),
                                 reinterpret_cast<LPARAM>(dialog.buttons.front()),
                                 SMTO_ABORTIFHUNG, 1000, &ignored));
    if (WaitForSingleObject(child.process.hProcess, 5000) != WAIT_OBJECT_0) {
        Dialog remaining{child.process.dwProcessId, nullptr, {}, {}, {}};
        EnumDesktopWindows(desktop.value, Dialog::top, reinterpret_cast<LPARAM>(&remaining));
        std::wcerr << L"Remaining dialog=" << remaining.title << L"\n" << remaining.text << std::flush;
        throw std::runtime_error("Startup process did not exit after dismissing its diagnostic");
    }
    DWORD status = 0;
    MG_CHECK(GetExitCodeProcess(child.process.hProcess, &status));
    // The logger is constructed before WinMain changes the resource working directory.
    auto log = bytes(deployment.caller / "test.log");
    std::cout << "Startup process exit=" << status << "\n" << log << std::flush;
    MG_CHECK(log.find("Standalone session cleanup failed") == std::string::npos);
    return {dialog.title, dialog.text, std::move(log), status};
}
void missing_resources() {
    Deployment deployment;
    // A decoy in the launch directory must not satisfy the executable's resource check.
    empty_package(deployment.caller / "DATA/robots.pkg");
    rejected_configuration(deployment.caller);
    const auto result = run(deployment);
    MG_CHECK(result.exit == 1);
    MG_CHECK(result.title == L"Game resources required");
    MG_CHECK(result.text.find(L"Place robots.pkg at DATA/robots.pkg.") != std::wstring::npos);
    MG_CHECK(result.log.find("Missing DATA/robots.pkg") != std::string::npos);
    MG_CHECK(result.log.find("Battle random generator:") == std::string::npos);
}
void rejected_packed_configuration() {
    Deployment deployment;
    empty_package(deployment.app / "DATA/robots.pkg");
    rejected_configuration(deployment.app);
    const auto result = run(deployment);
    MG_CHECK(result.exit == 1);
    MG_CHECK(result.title == L"Exception:");
    MG_CHECK(result.text.find(L"Invalid packed configuration: cfg\\robots.dat") != std::wstring::npos);
    MG_CHECK(result.log.find("Missing DATA/robots.pkg") == std::string::npos);
}
void missing_text_configuration() {
    Deployment deployment;
    empty_package(deployment.app / "DATA/robots.pkg");
    const auto result = run(deployment);
    MG_CHECK(result.exit == 1);
    MG_CHECK(result.title == L"Exception:");
    MG_CHECK(result.text.find(L"Error open file: cfg\\robots\\data.txt") != std::wstring::npos);
    MG_CHECK(result.log.find("Invalid packed configuration") == std::string::npos);
}
void invalid_random_configuration() {
    Deployment deployment;
    empty_package(deployment.app / "DATA/robots.pkg");
    rejected_configuration(deployment.app);
    write(deployment.app / "CFG/standalone.txt", "RandomGenerator=Invalid\n");
    const auto result = run(deployment);
    MG_CHECK(result.exit == 1);
    MG_CHECK(result.title == L"Exception:");
    MG_CHECK(result.text.find(L"Invalid packed configuration") != std::wstring::npos);
    MG_CHECK(result.log.find("Invalid RandomGenerator in CFG/standalone.txt; using ParkMiller") != std::string::npos);
    MG_CHECK(result.log.find("Battle random generator: ParkMiller") != std::string::npos);
}
void damaged_optional_packages() {
    for (int mask = 0; mask < 4; ++mask) {
        std::cout << "Optional package mask=" << mask << '\n';
        Deployment deployment;
        empty_package(deployment.app / "DATA/robots.pkg");
        rejected_configuration(deployment.app);
        if (mask & 1) write(deployment.app / "DATA/sound.pkg", "x");
        if (mask & 2) write(deployment.app / "DATA/voices.pkg", "x");
        const auto result = run(deployment);
        MG_CHECK(result.exit == 1);
        MG_CHECK(result.title == L"Exception:");
        MG_CHECK(result.text.find(L"Invalid packed configuration") != std::wstring::npos);
        MG_CHECK((result.log.find("Cannot open package DATA\\sound.pkg") != std::string::npos) == bool(mask & 1));
        MG_CHECK((result.log.find("Cannot open package DATA\\voices.pkg") != std::string::npos) == bool(mask & 2));
        MG_CHECK(result.log.find("Cannot open package DATA\\robots.pkg") == std::string::npos);
    }
}
const tests::Case cases[]{
    {"game.startup.native_missing_resources", missing_resources},
    {"game.startup.native_rejected_packed", rejected_packed_configuration},
    {"game.startup.native_missing_text", missing_text_configuration},
    {"game.startup.native_invalid_random", invalid_random_configuration},
    {"game.startup.native_damaged_optional", damaged_optional_packages},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 4) return 2;
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    if (count != 4) { LocalFree(arguments); return 2; }
    game_executable = std::filesystem::absolute(arguments[2]);
    compiler_directory = std::filesystem::absolute(arguments[3]).parent_path();
    LocalFree(arguments);
    return tests::run(2, argv, cases);
}
