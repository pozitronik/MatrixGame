// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "BattleLoading.hpp"
#include <d3d9.h>

#include <filesystem>

namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
struct ParentReset { ~ParentReset() { BattleLoading::failed(); } };

void names() {
    MG_CHECK(BattleLoading::valid_event_name(BattleLoading::event_name()));
    for (auto invalid : {L"", L"OtherEvent", L"Local\\Other-123", L"Local\\MatrixGame-Battle-",
                         L"Local\\MatrixGame-Battle-1-2\"", L"Local\\MatrixGame-Battle-1/2"}) {
        MG_CHECK(!BattleLoading::valid_event_name(invalid));
        bool caught = false;
        try { BattleLoading::connect(invalid); }
        catch (const std::runtime_error &) { caught = true; }
        MG_CHECK(caught && !BattleLoading::has_parent());
    }
}

void presentation() {
    ParentReset cleanup;
    const auto name = BattleLoading::event_name();
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, name.c_str())};
    MG_CHECK(ready.value != nullptr);
    BattleLoading::connect(name);
    MG_CHECK(BattleLoading::has_parent());
    BattleLoading::presented(D3DERR_DEVICELOST);
    MG_CHECK(WaitForSingleObject(ready.value, 0) == WAIT_TIMEOUT);
    MG_CHECK(BattleLoading::has_parent());
    BattleLoading::presented(S_OK);
    MG_CHECK(WaitForSingleObject(ready.value, 0) == WAIT_OBJECT_0);
    MG_CHECK(!BattleLoading::has_parent());
    BattleLoading::presented(S_OK);
}

void failure() {
    ParentReset cleanup;
    const auto name = BattleLoading::event_name();
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, name.c_str())};
    MG_CHECK(ready.value != nullptr);
    BattleLoading::connect(name);
    BattleLoading::failed();
    MG_CHECK(!BattleLoading::has_parent());
    MG_CHECK(WaitForSingleObject(ready.value, 0) == WAIT_OBJECT_0);
    BattleLoading::failed();
    // The old process may already have closed its event after a dismissal.
    BattleLoading::connect(name + L"-0");
    MG_CHECK(!BattleLoading::has_parent());
}

void child_handoff(bool signal) {
    const auto name = BattleLoading::event_name();
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, name.c_str())};
    MG_CHECK(ready.value != nullptr);
    wchar_t executable[32768]{};
    MG_CHECK(GetModuleFileNameW(nullptr, executable, 32768) != 0);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" " +
        (signal ? L"--signal " : L"--exit ") + name;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    MG_CHECK(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &process));
    Handle child{process.hProcess}, thread{process.hThread};
    MG_CHECK(BattleLoading::wait(ready.value, child.value) ==
             (signal ? BattleLoading::WaitResult::Ready : BattleLoading::WaitResult::ChildExited));
    MG_CHECK(WaitForSingleObject(child.value, 5000) == WAIT_OBJECT_0);
    DWORD exit = 0;
    MG_CHECK(GetExitCodeProcess(child.value, &exit));
    MG_CHECK(exit == (signal ? 0u : 7u));
}

void bounded_wait() {
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle child{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    MG_CHECK(ready.value && child.value && cancel.value);
    const DWORD start = GetTickCount();
    MG_CHECK(BattleLoading::wait(ready.value, child.value, 20, cancel.value) == BattleLoading::WaitResult::TimedOut);
    MG_CHECK(GetTickCount() - start < 5000);
    SetEvent(cancel.value);
    MG_CHECK(BattleLoading::wait(ready.value, child.value, 100, cancel.value) == BattleLoading::WaitResult::Dismissed);
    SetEvent(child.value);
    MG_CHECK(BattleLoading::wait(ready.value, child.value, 100) == BattleLoading::WaitResult::ChildExited);
    SetEvent(ready.value);
    MG_CHECK(BattleLoading::wait(ready.value, child.value, 100, cancel.value) == BattleLoading::WaitResult::Ready);
    MG_CHECK(BattleLoading::wait(nullptr, child.value, 100) == BattleLoading::WaitResult::Failed);
}

LRESULT CALLBACK signal_message(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_APP) { SetEvent(reinterpret_cast<HANDLE>(wp)); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}

void message_pump() {
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle child{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    MG_CHECK(ready.value && child.value);
    const wchar_t *name = L"MatrixLoadingMessageTest";
    WNDCLASSW type{};
    type.lpfnWndProc = signal_message;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = name;
    MG_CHECK(RegisterClassW(&type));
    struct Window {
        HWND value;
        ~Window() { DestroyWindow(value); UnregisterClassW(L"MatrixLoadingMessageTest", GetModuleHandleW(nullptr)); }
    } window{CreateWindowW(name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr)};
    MG_CHECK(window.value);
    MG_CHECK(PostMessageW(window.value, WM_APP, reinterpret_cast<WPARAM>(ready.value), 0));
    MG_CHECK(BattleLoading::wait(ready.value, child.value, 1000) == BattleLoading::WaitResult::Ready);
}

struct Child {
    PROCESS_INFORMATION process{};
    Child(std::wstring arguments, wchar_t *desktop = nullptr) {
        wchar_t executable[32768]{};
        MG_CHECK(GetModuleFileNameW(nullptr, executable, 32768) != 0);
        std::wstring command = L"\"" + std::wstring(executable) + L"\" " + arguments;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.lpDesktop = desktop;
        MG_CHECK(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                nullptr, nullptr, &startup, &process));
    }
    Child(const Child &) = delete;
    Child &operator=(const Child &) = delete;
    ~Child() {
        // A failed assertion must not leave a test helper running.
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    void expect_success(DWORD timeout) {
        MG_CHECK(WaitForSingleObject(process.hProcess, timeout) == WAIT_OBJECT_0);
        DWORD code = 1;
        MG_CHECK(GetExitCodeProcess(process.hProcess, &code));
        MG_CHECK(code == 0);
    }
};

// Use the real loading window and control procedure, with readiness held back by
// an event instead of relying on map size, machine speed or a production delay.
void dismiss_window(std::string_view action) {
    wchar_t desktop_name[128]{};
    DWORD needed = 0;
    MG_CHECK(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
                                      desktop_name, sizeof(desktop_name), &needed));
    MG_CHECK(std::wstring_view(desktop_name).starts_with(L"MatrixLoadingTest-"));
    struct Window {
        HWND value = CreateWindowW(L"STATIC", L"Loading test source", WS_POPUP,
            0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() { if (value) DestroyWindow(value); }
    } source;
    MG_CHECK(source.value);
    ShowWindow(source.value, SW_SHOWNOACTIVATE);
    ParentReset cleanup;
    MG_CHECK(BattleLoading::show(source.value, false));
    HWND loader = FindWindowW(L"MatrixGame_Loading", nullptr);
    MG_CHECK(loader && IsWindowVisible(loader));
    DWORD owner = 0;
    GetWindowThreadProcessId(loader, &owner);
    MG_CHECK(owner == GetCurrentProcessId());
    HWND button = GetDlgItem(loader, 1);
    MG_CHECK(button && IsWindowVisible(button) && IsWindowEnabled(button));

    const auto name = BattleLoading::event_name();
    const auto gate_name = name + L"-1";
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, name.c_str())};
    Handle gate{CreateEventW(nullptr, TRUE, FALSE, gate_name.c_str())};
    MG_CHECK(ready.value && gate.value);
    Child child(L"--gate " + name + L" " + gate_name);
    MG_CHECK(WaitForSingleObject(ready.value, 0) == WAIT_TIMEOUT);
    MG_CHECK(WaitForSingleObject(child.process.hProcess, 0) == WAIT_TIMEOUT);

    if (action == "button") {
        DWORD_PTR result = 0;
        MG_CHECK(SendMessageTimeoutW(button, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, 1000, &result));
    }
    else if (action == "close") MG_CHECK(PostMessageW(loader, WM_CLOSE, 0, 0));
    else {
        // Escape must also work when the child button receives the key message.
        MG_CHECK(action == "escape");
        MG_CHECK(PostMessageW(button, WM_KEYDOWN, VK_ESCAPE, 0));
    }
    MG_CHECK(BattleLoading::wait(ready.value, child.process.hProcess, 2000) == BattleLoading::WaitResult::Dismissed);
    BattleLoading::hide();
    MG_CHECK(!IsWindow(loader));
    MG_CHECK(WaitForSingleObject(child.process.hProcess, 0) == WAIT_TIMEOUT);
    MG_CHECK(WaitForSingleObject(ready.value, 0) == WAIT_TIMEOUT);
    MG_CHECK(SetEvent(gate.value));
    child.expect_success(5000);
    MG_CHECK(WaitForSingleObject(ready.value, 0) == WAIT_OBJECT_0);
}

void private_desktop(const wchar_t *action) {
    // lpDesktop connects both the helper's main thread and the production loader
    // thread to this desktop. Never switch the user's input desktop.
    std::wstring name = L"MatrixLoadingTest-" + std::to_wstring(GetCurrentProcessId());
    struct Desktop {
        HDESK value;
        ~Desktop() { if (value) CloseDesktop(value); }
    } desktop{CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
                            DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr)};
    MG_CHECK(desktop.value);
    Child child(std::wstring(L"--ui ") + action, name.data());
    child.expect_success(20000);
}

constexpr tests::Case cases[] = {
    {"game.loading.names", names},
    {"game.loading.presentation", presentation},
    {"game.loading.failure", failure},
    {"game.loading.child_ready", [] { child_handoff(true); }},
    {"game.loading.child_exit", [] { child_handoff(false); }},
    {"game.loading.bounded_wait", bounded_wait},
    {"game.loading.message_pump", message_pump},
    {"game.loading.dismiss_button", [] { private_desktop(L"button"); }},
    {"game.loading.dismiss_close", [] { private_desktop(L"close"); }},
    {"game.loading.dismiss_escape", [] { private_desktop(L"escape"); }},
};
}

int main(int argc, char **argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--gate") {
        Handle gate{OpenEventW(SYNCHRONIZE, FALSE, std::filesystem::path(argv[3]).c_str())};
        if (!gate.value || WaitForSingleObject(gate.value, 10000) != WAIT_OBJECT_0) return 8;
        try {
            BattleLoading::connect(std::filesystem::path(argv[2]).wstring());
            if (!BattleLoading::has_parent()) return 9;
            BattleLoading::presented(S_OK);
            return 0;
        }
        catch (...) { return 10; }
    }
    if (argc == 3) {
        if (std::string_view(argv[1]) == "--ui") {
            const tests::Case ui_cases[] = {
                {"button", [] { dismiss_window("button"); }},
                {"close", [] { dismiss_window("close"); }},
                {"escape", [] { dismiss_window("escape"); }},
            };
            return tests::run(argc - 1, argv + 1, ui_cases);
        }
        if (std::string_view(argv[1]) == "--exit") return 7;
        if (std::string_view(argv[1]) == "--signal") {
            BattleLoading::connect(std::filesystem::path(argv[2]).wstring());
            BattleLoading::presented(S_OK);
            return 0;
        }
    }
    return tests::run(argc, argv, cases);
}
