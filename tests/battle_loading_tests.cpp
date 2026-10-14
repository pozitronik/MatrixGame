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
    bool caught = false;
    try { BattleLoading::connect(name + L"-0"); }
    catch (const std::runtime_error &) { caught = true; }
    MG_CHECK(caught && !BattleLoading::has_parent());
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
    MG_CHECK(BattleLoading::wait(ready.value, child.value) == signal);
    MG_CHECK(WaitForSingleObject(child.value, 5000) == WAIT_OBJECT_0);
    DWORD exit = 0;
    MG_CHECK(GetExitCodeProcess(child.value, &exit));
    MG_CHECK(exit == (signal ? 0u : 7u));
}

constexpr tests::Case cases[] = {
    {"game.loading.names", names},
    {"game.loading.presentation", presentation},
    {"game.loading.failure", failure},
    {"game.loading.child_ready", [] { child_handoff(true); }},
    {"game.loading.child_exit", [] { child_handoff(false); }},
};
}

int main(int argc, char **argv) {
    if (argc == 3) {
        if (std::string_view(argv[1]) == "--exit") return 7;
        if (std::string_view(argv[1]) == "--signal") {
            BattleLoading::connect(std::filesystem::path(argv[2]).wstring());
            BattleLoading::presented(S_OK);
            return 0;
        }
    }
    return tests::run(argc, argv, cases);
}
