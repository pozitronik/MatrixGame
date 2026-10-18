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

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};

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
struct Caption {
    std::wstring text;
    DWORD error = ERROR_SUCCESS;
    bool complete = false;
};
Caption window_caption(HWND window) {
    // For a foreign top-level window these APIs read the cached caption without
    // waiting for its window procedure. Control contents still need WM_GETTEXT.
    SetLastError(ERROR_SUCCESS);
    const int length = GetWindowTextLengthW(window);
    if (!length) return {{}, GetLastError(), false};
    std::wstring text(length + 1, L'\0');
    SetLastError(ERROR_SUCCESS);
    const int copied = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    if (!copied) return {{}, GetLastError(), false};
    text.resize(copied);
    return {std::move(text), ERROR_SUCCESS, true};
}
std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (!length) return "[UTF-8 conversion failed]";
    std::string result(length, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                             result.data(), length, nullptr, nullptr)) return "[UTF-8 conversion failed]";
    return result;
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
    Child(const Deployment &deployment, Desktop &desktop, std::filesystem::path executable = {},
          std::wstring arguments = {}) {
        if (executable.empty()) executable = deployment.app / "MatrixGame.exe";
        std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
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
    bool caption_complete = false;
    DWORD caption_error = ERROR_SUCCESS;
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
        auto caption = window_caption(window);
        dialog.title = std::move(caption.text);
        dialog.caption_complete = caption.complete;
        dialog.caption_error = caption.error;
        dialog.text.clear();
        dialog.buttons.clear();
        EnumChildWindows(window, child, parameter);
        if (!dialog.caption_complete || dialog.title.empty() || dialog.text.empty() || dialog.buttons.size() != 1 ||
            !IsWindowEnabled(dialog.buttons.front())) {
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
        std::cerr << "Last startup caption=[" << utf8(dialog.title) << "]\n";
        std::cerr << "Caption complete=" << dialog.caption_complete << "; caption Windows error="
                  << dialog.caption_error << "; body characters=" << dialog.text.size()
                  << "; buttons=" << dialog.buttons.size() << '\n';
        throw std::runtime_error("No startup diagnostic dialog; process status=" + std::to_string(status));
    }
    std::cout << "Startup diagnostic caption=[" << utf8(dialog.title) << "]\n" << std::flush;
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

constexpr wchar_t synthetic_caption[] = L"Synthetic startup diagnostic";
constexpr wchar_t synthetic_body[] = L"Synthetic startup diagnostic body";

INT_PTR CALLBACK readiness_dialog(HWND window, UINT message, WPARAM wp, LPARAM) {
    if (message == WM_INITDIALOG) {
        if (!CreateWindowW(L"Static", synthetic_body, WS_CHILD, 0, 0, 100, 30,
                           window, nullptr, GetModuleHandleW(nullptr), nullptr) ||
            !CreateWindowW(L"Button", L"OK", WS_CHILD, 0, 35, 50, 20,
                           window, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr)) {
            PostQuitMessage(2);
        }
        return TRUE;
    }
    if (message == WM_GETTEXTLENGTH || message == WM_GETTEXT) {
        // Only the dialog procedure refuses text messages. Foreign cached-caption
        // reads must still work; the Static child handles its normal text messages.
        SetWindowLongPtrW(window, DWLP_MSGRESULT, 0);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wp) == IDOK) {
        DestroyWindow(window);
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

int readiness_child(const wchar_t *body_name, const wchar_t *allow_name, const wchar_t *caption_name) {
    Handle body{OpenEventW(EVENT_MODIFY_STATE, FALSE, body_name)};
    Handle allow{OpenEventW(SYNCHRONIZE, FALSE, allow_name)};
    Handle caption{OpenEventW(EVENT_MODIFY_STATE, FALSE, caption_name)};
    check_win32(body.value && allow.value && caption.value, "OpenEventW readiness gates");
    struct Template {
        DLGTEMPLATE dialog{};
        WORD menu = 0, window_class = 0, title = 0;
    } type;
    type.dialog.style = WS_POPUP | WS_CAPTION | DS_MODALFRAME;
    type.dialog.cx = 120;
    type.dialog.cy = 70;
    struct Window {
        HWND value;
        ~Window() { if (IsWindow(value)) DestroyWindow(value); }
    } window{CreateDialogIndirectParamW(GetModuleHandleW(nullptr), &type.dialog, nullptr, readiness_dialog, 0)};
    check_win32(window.value != nullptr, "CreateDialogIndirectParamW");
    check_win32(SetEvent(body.value), "SetEvent body ready");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool caption_set = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const DWORD count = caption_set ? 0 : 1;
        const DWORD result = MsgWaitForMultipleObjects(count, &allow.value, FALSE, 20, QS_ALLINPUT);
        check_win32(result != WAIT_FAILED, "MsgWaitForMultipleObjects readiness");
        if (!caption_set && result == WAIT_OBJECT_0) {
            check_win32(SetWindowTextW(window.value, synthetic_caption), "SetWindowTextW caption");
            caption_set = true;
            check_win32(SetEvent(caption.value), "SetEvent caption ready");
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return 3;
}

void diagnostic_readiness() {
    Environment environment;
    Deployment deployment;
    Desktop desktop;
    const auto prefix = L"Local\\MatrixGame-Diagnostic-" + std::to_wstring(GetCurrentProcessId());
    const auto body_name = prefix + L"-body", allow_name = prefix + L"-allow", caption_name = prefix + L"-caption";
    Handle body{CreateEventW(nullptr, TRUE, FALSE, body_name.c_str())};
    Handle allow{CreateEventW(nullptr, TRUE, FALSE, allow_name.c_str())};
    Handle caption{CreateEventW(nullptr, TRUE, FALSE, caption_name.c_str())};
    MG_CHECK(body.value && allow.value && caption.value);
    std::array<wchar_t, 32768> executable{};
    const auto length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
    MG_CHECK(length && length < executable.size());
    Child child(deployment, desktop, executable.data(),
                L"--diagnostic-readiness-child " + body_name + L" " + allow_name + L" " + caption_name);
    MG_CHECK(WaitForSingleObject(body.value, 5000) == WAIT_OBJECT_0);
    Dialog incomplete{child.process.dwProcessId, nullptr, {}, {}, {}};
    const bool enumerated = EnumDesktopWindows(desktop.value, Dialog::top, reinterpret_cast<LPARAM>(&incomplete));
    MG_CHECK(incomplete.window == nullptr);
    MG_CHECK(enumerated);
    MG_CHECK(incomplete.title.empty() && !incomplete.caption_complete);
    MG_CHECK(incomplete.text == std::wstring(synthetic_body) + L"\n");
    MG_CHECK(incomplete.buttons.size() == 1 && IsWindowEnabled(incomplete.buttons.front()));
    MG_CHECK(WaitForSingleObject(child.process.hProcess, 0) == WAIT_TIMEOUT);
    MG_CHECK(SetEvent(allow.value));
    MG_CHECK(WaitForSingleObject(caption.value, 5000) == WAIT_OBJECT_0);
    Dialog ready{child.process.dwProcessId, nullptr, {}, {}, {}};
    EnumDesktopWindows(desktop.value, Dialog::top, reinterpret_cast<LPARAM>(&ready));
    MG_CHECK(ready.window && ready.caption_complete && ready.caption_error == ERROR_SUCCESS);
    MG_CHECK(ready.title == synthetic_caption && ready.text == std::wstring(synthetic_body) + L"\n");
    MG_CHECK(ready.buttons.size() == 1 && IsWindowEnabled(ready.buttons.front()));
    MG_CHECK(PostMessageW(ready.window, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                          reinterpret_cast<LPARAM>(ready.buttons.front())));
    MG_CHECK(WaitForSingleObject(child.process.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD status = 1;
    MG_CHECK(GetExitCodeProcess(child.process.hProcess, &status) && status == 0);
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
    {"game.startup.native_diagnostic_readiness", diagnostic_readiness},
    {"game.startup.native_missing_resources", missing_resources},
    {"game.startup.native_rejected_packed", rejected_packed_configuration},
    {"game.startup.native_missing_text", missing_text_configuration},
    {"game.startup.native_invalid_random", invalid_random_configuration},
    {"game.startup.native_damaged_optional", damaged_optional_packages},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    if (argc == 5 && count == 5 && std::wstring_view(arguments[1]) == L"--diagnostic-readiness-child") {
        int result = 2;
        try { result = readiness_child(arguments[2], arguments[3], arguments[4]); }
        catch (const std::exception &error) { std::cerr << error.what() << '\n'; }
        LocalFree(arguments);
        return result;
    }
    if (count != 4) { LocalFree(arguments); return 2; }
    game_executable = std::filesystem::absolute(arguments[2]);
    compiler_directory = std::filesystem::absolute(arguments[3]).parent_path();
    LocalFree(arguments);
    return tests::run(2, argv, cases);
}
