// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "native_session_assets.hpp"
#include "native_outcome_assets.hpp"
#include "native_packed_config.hpp"
#include "native_pointer_guard.hpp"
#include "standalone_session_protocol.hpp"
#include "CMain.hpp"
#include "StandaloneSettings.hpp"
#include "CBlockPar.hpp"
#include "CStorage.hpp"
#include <windows.h>
#include <chrono>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <new>
#include <optional>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace protocol = tests::native_session;
fs::path probe_executable, compiler_directory;
enum class ConfigurationProfile { Text, Packed };
ConfigurationProfile configuration_profile = ConfigurationProfile::Text;
void write(const fs::path &path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(text.data(), text.size());
    MG_CHECK(stream.good());
}
void restore_bytes(const fs::path &backup, const fs::path &original) {
    std::ifstream source(backup, std::ios::binary);
    MG_CHECK(source.good());
    const std::string bytes{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
    MG_CHECK(!source.bad());
    write(original, bytes);
}
struct Environment {
    std::wstring key, previous;
    bool existed;
    Environment(std::wstring name, std::wstring value) : key(std::move(name)) {
        auto size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
        existed = size != 0;
        if (size) { previous.resize(size); previous.resize(GetEnvironmentVariableW(key.c_str(), previous.data(), size)); }
        MG_CHECK(SetEnvironmentVariableW(key.c_str(), value.c_str()));
    }
    ~Environment() { SetEnvironmentVariableW(key.c_str(), existed ? previous.c_str() : nullptr); }
};
struct Deployment {
    const ConfigurationProfile configuration;
    fs::path previous = fs::current_path();
    fs::path root = previous / ("native-exe-session-" + std::to_string(GetCurrentProcessId()));
    fs::path app = root / "app", caller = root / "caller";
    explicit Deployment(ConfigurationProfile profile = configuration_profile) : configuration(profile) {
        MG_CHECK(!fs::exists(root));
        fs::create_directories(app); fs::create_directories(caller);
        try {
            fs::current_path(app);
            tests::write_native_session_assets();
            if (configuration == ConfigurationProfile::Packed) {
                tests::pack_native_session_configuration();
                fs::remove_all(L"parked-text-config");
                MG_CHECK(!fs::exists(L"CFG/robots") && !fs::exists(L"parked-text-config"));
            }
            fs::create_directories("Matrix/Map");
            tests::write_native_session_map(L"Matrix/Map/first.cmap");
            tests::write_native_session_map(L"Matrix/Map/second.cmap");
            std::string package;
            for (uint32_t word : {4u, 12u, 0u, 158u})
                for (int shift = 0; shift < 32; shift += 8) package += char(word >> shift);
            write("DATA/robots.pkg", package);
            write("CFG/standalone.txt", "WindowMode=Windowed\nResolutionWidth=640\nResolutionHeight=480\n"
                "StencilShadows=Off\nProjectedShadows=Off\nRobotShadows=Off\nMap=Matrix\\Map\\first.cmap\nBattleBonus=None\n");
            fs::copy_file(probe_executable, "MatrixGame.exe");
            fs::current_path(previous);
        } catch (...) { fs::current_path(previous); fs::remove_all(root); throw; }
    }
    ~Deployment() noexcept(false) {
        std::error_code error;
        fs::current_path(previous, error);
        // EXIT_PROCESS precedes final image/file-handle release on some Windows builds.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            fs::remove_all(root, error);
            if (!error) break;
            Sleep(20);
        } while (std::chrono::steady_clock::now() < deadline);
        if (error) {
            const auto message = "Cannot remove native test deployment " + root.string() + ": " + error.message();
            if (std::uncaught_exceptions()) std::cerr << message << '\n';
            else throw std::runtime_error(message);
        }
    }
};
void same_tree(const Base::CBlockPar &left, const Base::CBlockPar &right) {
    MG_CHECK(left.ParCount() == right.ParCount() && left.BlockCount() == right.BlockCount());
    for (int i = 0; i < left.ParCount(); ++i)
        MG_CHECK(left.ParGetName(i) == right.ParGetName(i) && left.ParGet(i) == right.ParGet(i));
    for (int i = 0; i < left.BlockCount(); ++i) {
        MG_CHECK(left.BlockGetName(i) == right.BlockGetName(i));
        same_tree(*left.BlockGet(i), *right.BlockGet(i));
    }
}
void configuration_assets() {
    Base::CBlockPar text_data, text_interface;
    for (auto profile : {ConfigurationProfile::Text, ConfigurationProfile::Packed}) {
        Deployment deployment(profile);
        Base::CBlockPar data, interface_config;
        const auto config = deployment.app / "CFG";
        if (profile == ConfigurationProfile::Packed) {
            MG_CHECK(fs::is_regular_file(config / "robots.dat") && !fs::exists(config / "robots"));
            MG_CHECK(!fs::exists(deployment.app / "parked-text-config"));
            Base::CStorage archive;
            MG_CHECK(archive.Load((config / "robots.dat").c_str()));
            archive.RestoreBlockPar(L"da", data);
            archive.RestoreBlockPar(L"if", interface_config);
        } else {
            MG_CHECK(!fs::exists(config / "robots.dat"));
            data.LoadFromTextFile((config / "robots/data.txt").c_str());
            interface_config.LoadFromTextFile((config / "robots/iface.txt").c_str());
            text_data.LoadFromTextFile((config / "robots/data.txt").c_str());
            text_interface.LoadFromTextFile((config / "robots/iface.txt").c_str());
        }
        MG_CHECK(data.BlockGet(L"Config")->ParGet(L"SoftwareCursor") == L"1");
        MG_CHECK(interface_config.BlockCount() > 0);
        same_tree(text_data, data);
        same_tree(text_interface, interface_config);
    }
}
void pointer_guard_policy() {
    const RECT panel{50, 50, 690, 530};
    int cursor_queries{}, panel_queries{};
    const auto check = [&](bool visible, POINT point, bool cursor_ok = true, bool panel_ok = true,
                           RECT rectangle = RECT{50, 50, 690, 530}) {
        tests::require_pointer_outside_panel(visible, [&](POINT &result) {
            ++cursor_queries; result = point; return cursor_ok;
        }, [&](RECT &result) { ++panel_queries; result = rectangle; return panel_ok; });
    };
    check(false, POINT{50, 50}, false, false);
    MG_CHECK(cursor_queries == 0 && panel_queries == 0);
    for (auto point : {POINT{49, 50}, POINT{50, 49}, POINT{panel.right, 50}, POINT{50, panel.bottom}})
        check(true, point);
    const auto rejected = [&](POINT point, bool cursor_ok, bool panel_ok, RECT rectangle, const char *message) {
        bool failed = false;
        try { check(true, point, cursor_ok, panel_ok, rectangle); }
        catch (const std::runtime_error &error) { failed = std::string_view(error.what()).find(message) != std::string_view::npos; }
        MG_CHECK(failed);
    };
    for (auto point : {POINT{50, 50}, POINT{689, 529}, POINT{300, 200}})
        rejected(point, true, true, panel, "Physical pointer entered");
    rejected(POINT{0, 0}, false, true, panel, "Cannot read physical pointer");
    rejected(POINT{0, 0}, true, false, panel, "panel rectangle");
    rejected(POINT{0, 0}, true, true, RECT{50, 50, 50, 530}, "panel rectangle");
    rejected(POINT{0, 0}, true, true, RECT{50, 530, 690, 50}, "panel rectangle");
    MG_CHECK(cursor_queries == 11 && panel_queries == 10);
}
struct Process {
    HANDLE handle{};
    bool exited{};
    DWORD exit{};
    bool initial_breakpoint{};
};
struct Harness {
    fs::path application;
    std::wstring name = L"Local\\MatrixGameNativeTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(protocol::Shared), name.c_str());
    protocol::Shared *shared{};
    HANDLE job{};
    std::map<DWORD, Process> processes;
    DWORD root_pid{};
    Environment environment{protocol::environment, name};
    std::optional<Environment> path;
    explicit Harness(const Deployment &deployment) : application(deployment.app) {
        try {
            const bool packed = deployment.configuration == ConfigurationProfile::Packed;
            MG_CHECK(fs::is_regular_file(application / "CFG/robots.dat") == packed);
            MG_CHECK(fs::is_regular_file(application / "CFG/robots/data.txt") == !packed);
            MG_CHECK(fs::is_regular_file(application / "CFG/robots/iface.txt") == !packed);
            MG_CHECK(!fs::exists(application / "parked-text-config"));
            std::cout << "configuration=" << (packed ? "Packed" : "Text")
                      << "; containment=" << (protocol::visible_windows ? "Visible" : "Hidden") << '\n' << std::flush;
            MG_CHECK(mapping);
            shared = static_cast<protocol::Shared *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(protocol::Shared)));
            MG_CHECK(shared); new (shared) protocol::Shared{};
            std::vector<wchar_t> previous_path(32768);
            GetEnvironmentVariableW(L"PATH", previous_path.data(), previous_path.size());
            path.emplace(L"PATH", probe_executable.parent_path().wstring() + L";" + compiler_directory.wstring() + L";" + previous_path.data());
            job = CreateJobObjectW(nullptr, nullptr); MG_CHECK(job);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            MG_CHECK(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
            STARTUPINFOW startup{}; startup.cb = sizeof(startup);
            PROCESS_INFORMATION child{};
            const auto executable = deployment.app / "MatrixGame.exe";
            auto command = L"\"" + executable.wstring() + L"\"";
            MG_CHECK(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, DEBUG_PROCESS | CREATE_SUSPENDED,
                nullptr, deployment.caller.c_str(), &startup, &child));
            root_pid = child.dwProcessId;
            if (!AssignProcessToJobObject(job, child.hProcess)) {
                TerminateProcess(child.hProcess, 99); CloseHandle(child.hThread); CloseHandle(child.hProcess);
                throw std::runtime_error("Cannot contain native EXE process tree");
            }
            const auto resumed = ResumeThread(child.hThread);
            CloseHandle(child.hThread); CloseHandle(child.hProcess);
            MG_CHECK(resumed != DWORD(-1));
        } catch (...) { cleanup(); throw; }
    }
    void check_pointer() {
        if constexpr (protocol::visible_windows) {
            for (auto &entry : shared->processes) {
                const auto panel_value = InterlockedCompareExchange(&entry.panel, 0, 0);
                if (!panel_value) continue; // The probe checks before showing, and clears before destruction.
                const auto panel = reinterpret_cast<HWND>(panel_value);
                const auto pid = DWORD(InterlockedCompareExchange(&entry.id, 0, 0));
                if (const auto found = processes.find(pid); found != processes.end() &&
                    (found->second.exited || WaitForSingleObject(found->second.handle, 0) == WAIT_OBJECT_0)) continue;
                DWORD owner{};
                const auto thread = GetWindowThreadProcessId(panel, &owner);
                RECT rectangle{};
                const bool valid = thread && owner == pid && GetWindowRect(panel, &rectangle);
                if (!valid) {
                    if (InterlockedCompareExchange(&entry.panel, 0, 0) == 0) continue;
                    if (const auto found = processes.find(pid); found != processes.end() &&
                        WaitForSingleObject(found->second.handle, 0) == WAIT_OBJECT_0) continue;
                    throw std::runtime_error("Cannot verify owned visible native test panel");
                }
                tests::require_pointer_outside_panel(true, [](POINT &point) { return GetCursorPos(&point); },
                    [&](RECT &result) { result = rectangle; return true; });
            }
        }
    }
    bool pump(DWORD timeout = 10, bool guard_pointer = true) {
        if (guard_pointer) check_pointer();
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, timeout)) return false;
        DWORD disposition = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            HANDLE retained{};
            MG_CHECK(DuplicateHandle(GetCurrentProcess(), event.u.CreateProcessInfo.hProcess, GetCurrentProcess(),
                &retained, 0, FALSE, DUPLICATE_SAME_ACCESS));
            processes.emplace(event.dwProcessId, Process{retained});
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            std::cout << "Created native process " << event.dwProcessId << '\n' << std::flush;
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            auto &process = processes.at(event.dwProcessId);
            process.exited = true; process.exit = event.u.ExitProcess.dwExitCode;
            std::cout << "Exited native process " << event.dwProcessId << " code=" << process.exit << '\n' << std::flush;
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile) {
            CloseHandle(event.u.LoadDll.hFile);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            auto &process = processes.at(event.dwProcessId);
            if (event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT && !process.initial_breakpoint) {
                process.initial_breakpoint = true;
            } else disposition = DBG_EXCEPTION_NOT_HANDLED;
            if (!event.u.Exception.dwFirstChance)
                std::cerr << "Second-chance exception " << std::hex << event.u.Exception.ExceptionRecord.ExceptionCode << std::dec << '\n';
        }
        MG_CHECK(ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition));
        return true;
    }
    protocol::Process *slot(DWORD pid) {
        for (auto &entry : shared->processes) if (DWORD(InterlockedCompareExchange(&entry.id, 0, 0)) == pid) return &entry;
        return nullptr;
    }
    HWND window(DWORD pid) {
        auto *entry = slot(pid);
        return entry ? reinterpret_cast<HWND>(InterlockedCompareExchange(&entry->window, 0, 0)) : nullptr;
    }
    bool message(DWORD pid, UINT code, WPARAM wp = 0, LPARAM lp = 0) {
        const auto target = window(pid);
        if (!target || !IsWindow(target)) return false;
        DWORD owner{};
        GetWindowThreadProcessId(target, &owner);
        if (owner != pid) return false;
        if constexpr (protocol::visible_windows) {
            if (!IsWindowVisible(target)) return false; // Creation can precede the first ShowWindow.
        } else {
            MG_CHECK(!IsWindowVisible(target));
        }
        check_pointer();
        return PostMessageW(target, code, wp, lp) != 0;
    }
    std::optional<protocol::Snapshot> snapshot(DWORD pid, bool diagnostic = false) {
        auto *entry = slot(pid);
        if (!entry) return std::nullopt;
        const auto request = InterlockedIncrement(&entry->request);
        if (!message(pid, diagnostic ? protocol::diagnostic_snapshot_message : protocol::snapshot_message, request)) return std::nullopt;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (std::chrono::steady_clock::now() < deadline) {
            pump(1);
            const auto sequence = InterlockedCompareExchange(&entry->snapshot_generation, 0, 0);
            if (sequence & 1) continue;
            const auto result = entry->snapshot;
            MemoryBarrier();
            if (sequence == InterlockedCompareExchange(&entry->snapshot_generation, 0, 0) && result.request == request)
                return result;
        }
        return std::nullopt;
    }
    void check_diagnostics(DWORD pid) {
        if (auto *entry = slot(pid); entry && InterlockedCompareExchange(&entry->diagnostics, 0, 0)) {
            std::wcerr << entry->diagnostic_title << L": " << entry->diagnostic_text << '\n';
            throw std::runtime_error("Unexpected native diagnostic");
        }
    }
    template<class Predicate> protocol::Snapshot wait(DWORD pid, Predicate ready, const char *description, int seconds = 20) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            pump();
            while (pump(0)) {}
            check_diagnostics(pid);
            if (auto found = processes.find(pid); found != processes.end() && found->second.exited)
                throw std::runtime_error("Process exited while waiting for " + std::string(description) + ": " + std::to_string(found->second.exit));
            if (auto state = snapshot(pid); state && ready(*state)) return *state;
        }
        std::cerr << "Observed slot=" << slot(pid) << " window=" << window(pid) << '\n';
        if (auto state = snapshot(pid, true)) std::wcerr << L"Last form=" << state->form << L" flags=" << state->flags
            << L" time=" << state->time << L" friendly health=" << state->friendly_health << L" enemy health=" << state->enemy_health
            << L" selected=" << state->selected << L" preorder=" << state->move_preorder << L" moving=" << state->friendly_move
            << L" pos=" << state->friendly_x << L',' << state->friendly_y << L" trace=" << state->trace_x << L',' << state->trace_y
            << L" trace land=" << state->trace_land << L" focus=" << state->interface_focus
            << L" map_flags=0x" << std::hex << state->map_flags << L" device_status=0x" << state->device_status << std::dec
            << L" draw_fps=" << state->draw_fps << L" backbuffer=" << state->backbuffer_width << L'x' << state->backbuffer_height
            << L" windowed=" << state->device_windowed << '\n';
        RECT child_rectangle{}, parent_rectangle{};
        if (const auto child = window(pid); child && GetWindowRect(child, &child_rectangle) &&
            GetWindowRect(GetParent(child), &parent_rectangle))
            std::cerr << "Child rectangle=" << child_rectangle.left << ',' << child_rectangle.top << ','
                      << child_rectangle.right << ',' << child_rectangle.bottom << " parent rectangle="
                      << parent_rectangle.left << ',' << parent_rectangle.top << ',' << parent_rectangle.right << ','
                      << parent_rectangle.bottom << '\n';
        throw std::runtime_error("Timed out waiting for " + std::string(description));
    }
    void key(DWORD pid, WPARAM key) { MG_CHECK(message(pid, WM_KEYDOWN, key)); MG_CHECK(message(pid, WM_KEYUP, key)); }
    DWORD child(DWORD parent) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
            pump();
            for (const auto &[pid, process] : processes) if (pid != parent) return pid;
        }
        throw std::runtime_error("Map selection did not create a child process");
    }
    void exit(DWORD pid, DWORD expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
            pump();
            if (processes.contains(pid) && processes.at(pid).exited) {
                if (expected == 0 || processes.at(pid).exit != expected) check_diagnostics(pid);
                MG_CHECK(processes.at(pid).exit == expected);
                return;
            }
        }
        throw std::runtime_error("Native process did not exit");
    }
    void cleanup() noexcept {
        if (job) { CloseHandle(job); job = nullptr; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            bool active = root_pid && !processes.contains(root_pid);
            for (const auto &[id, child] : processes) if (!child.exited) active = true;
            if (!active) break;
            try { pump(10, false); } catch (...) { break; }
        }
        for (const auto &[id, child] : processes) {
            WaitForSingleObject(child.handle, 1000);
            CloseHandle(child.handle);
        }
        if (shared) UnmapViewOfFile(shared);
        if (mapping) CloseHandle(mapping);
    }
    ~Harness() { cleanup(); }
};
void startup_selector_exit(bool close_selector) {
    Deployment deployment;
    Harness run(deployment);
    const auto pid = run.root_pid;
    run.wait(pid, [](const auto &state) { return state.form[0] && !state.initialized; }, "startup form");
    MG_CHECK(run.message(pid, WM_ACTIVATEAPP, TRUE));
    run.key(pid, 'N');
    run.wait(pid, [](const auto &state) { return state.selector && !state.initialized; }, "startup selector");
    if (close_selector) MG_CHECK(run.message(pid, WM_CLOSE));
    else {
        run.key(pid, VK_ESCAPE);
        run.wait(pid, [](const auto &state) { return !state.selector && !state.initialized; }, "startup cancel");
        run.key(pid, VK_ESCAPE);
    }
    run.exit(pid, 0);
    MG_CHECK(run.processes.size() == 1);
    MG_CHECK(!fs::exists(deployment.app / "Cache"));
}
void startup_cancel() { startup_selector_exit(false); }
void startup_selector_close() { startup_selector_exit(true); }
protocol::Snapshot start_battle(Harness &run, bool second = false) {
    const auto pid = run.root_pid;
    run.wait(pid, [](const auto &state) { return state.form[0] && !state.initialized; }, "startup form");
    MG_CHECK(run.message(pid, WM_ACTIVATEAPP, TRUE));
    run.key(pid, 'N');
    run.wait(pid, [](const auto &state) { return state.selector; }, "startup selector");
    run.key(pid, second ? VK_END : VK_HOME);
    run.key(pid, VK_RETURN);
    const auto battle = run.wait(pid, [](const auto &state) { return state.initialized; }, "initialized battle");
    MG_CHECK(std::wstring(battle.map) == (second ? L"Matrix\\Map\\second.cmap" : L"Matrix\\Map\\first.cmap"));
    MG_CHECK(std::wstring(battle.dialog) == L"Begin");
    MG_CHECK(battle.robots == 2);
    MG_CHECK(fs::is_regular_file(run.application / (second ? "Cache/second.cmap.0" : "Cache/first.cmap.0")));
    run.key(pid, VK_RETURN);
    run.wait(pid, [](const auto &state) { return state.initialized && !state.paused && !state.dialog[0] && state.time > 100; }, "running battle");
    return battle;
}
void startup_battle() {
    Deployment deployment;
    Harness run(deployment);
    start_battle(run);
    const auto pid = run.root_pid;
    run.key(pid, VK_ESCAPE);
    run.wait(pid, [](const auto &state) { return state.dialog[0] && state.paused; }, "battle menu");
    run.key(pid, 'N');
    run.wait(pid, [](const auto &state) { return state.selector; }, "battle selector");
    run.key(pid, VK_ESCAPE);
    run.wait(pid, [](const auto &state) { return !state.selector && state.dialog[0] && state.paused; }, "battle selector cancel");
    run.key(pid, VK_ESCAPE);
    run.wait(pid, [](const auto &state) { return !state.dialog[0] && !state.paused; }, "battle resumed");
    MG_CHECK(run.message(pid, WM_CLOSE));
    run.exit(pid, 0);
    MG_CHECK(run.processes.size() == 1);
}
void battle_relaunch(bool different, bool edit_settings = false) {
    Deployment deployment;
    Harness run(deployment);
    const auto parent = run.root_pid;
    const auto initial = start_battle(run);
    run.key(parent, VK_ESCAPE);
    run.wait(parent, [](const auto &state) { return state.paused && state.dialog[0]; }, "battle menu");
    if (edit_settings) {
        run.key(parent, 'C');
        run.wait(parent, [](const auto &state) { return state.settings; }, "settings window");
        for (int row = 0; row < 7; ++row) run.key(parent, VK_DOWN); // Resolution -> Skybox; group labels are skipped.
        run.key(parent, VK_LEFT); // High -> Low, applied only by the next Init.
        run.key(parent, VK_DOWN); // Robot shadows.
        run.key(parent, VK_DOWN); // Camera speed.
        run.key(parent, VK_RIGHT); // 100 -> 125%, immediate.
        run.key(parent, VK_DOWN); // RNG.
        run.key(parent, VK_RIGHT); // ParkMiller -> LegacyCRT, applied only by the next Init.
        run.key(parent, VK_RETURN);
        const auto changed = run.wait(parent, [](const auto &state) { return !state.settings; }, "settings applied");
        MG_CHECK(changed.saved_camera == 125 && changed.saved_skybox == 1 && changed.saved_random == 1);
        MG_CHECK(std::abs(changed.runtime_camera - initial.runtime_camera * 1.25f) < 0.00001f);
        MG_CHECK(changed.runtime_skybox == 2 && changed.runtime_random == 0 && changed.restart_pending);
        const auto saved = StandaloneSettings::load((deployment.app / "CFG/standalone.txt").c_str());
        MG_CHECK(saved.invalid.empty());
        MG_CHECK(saved.values.camera_speed == 125 && saved.values.skybox == StandaloneSettings::Skybox::Low && saved.values.random == random::Mode::LegacyCRT);
        // Closing Settings returns to the menu.
        MG_CHECK(std::wstring(changed.dialog) == L"Menu");
    }
    run.key(parent, 'N');
    run.wait(parent, [](const auto &state) { return state.selector; }, "battle selector");
    run.key(parent, different ? VK_END : VK_HOME);
    run.key(parent, VK_RETURN);
    const auto child = run.child(parent);
    auto loaded = run.wait(child, [](const auto &state) { return state.initialized; }, "child battlefield initialized");
    MG_CHECK(loaded.direct_start);
    MG_CHECK(std::wstring(loaded.map) == (different ? L"Matrix\\Map\\second.cmap" : L"Matrix\\Map\\first.cmap"));
    MG_CHECK(loaded.robots == 2);
    MG_CHECK(fs::is_regular_file(deployment.app / (different ? "Cache/second.cmap.0" : "Cache/first.cmap.0")));
    if (edit_settings) {
        MG_CHECK(loaded.saved_camera == 125 && loaded.saved_skybox == 1 && loaded.saved_random == 1);
        MG_CHECK(std::abs(loaded.runtime_camera - initial.runtime_camera * 1.25f) < 0.00001f);
        MG_CHECK(loaded.runtime_skybox == 1 && loaded.runtime_random == 1 && !loaded.restart_pending);
    }
    MG_CHECK(run.message(child, WM_ACTIVATEAPP, TRUE));
    run.wait(child, [](const auto &state) { return state.initialized && !state.parent_handshake; }, "child first successful Present");
    run.exit(parent, 0);
    run.key(child, VK_RETURN);
    run.wait(child, [](const auto &state) { return !state.paused && !state.dialog[0] && state.time > 100; }, "child simulation");
    MG_CHECK(run.message(child, WM_CLOSE));
    run.exit(child, 0);
    MG_CHECK(run.processes.size() == 2);
}
void failed_child_recovery(bool corrupt_map = false) {
    Deployment deployment;
    const bool packed = deployment.configuration == ConfigurationProfile::Packed;
    const auto original = deployment.app / (corrupt_map ? "Matrix/Map/second.cmap" : packed ? "CFG/robots.dat" : "CFG/robots/iface.txt");
    const auto backup = deployment.app / "fault-backup";
    {
        Harness run(deployment);
        start_battle(run);
        const auto parent = run.root_pid;
        run.key(parent, VK_ESCAPE);
        run.wait(parent, [](const auto &state) { return state.paused && state.dialog[0]; }, "battle menu");
        run.key(parent, 'N');
        run.wait(parent, [](const auto &state) { return state.selector; }, "battle selector");
        if (corrupt_map) {
            fs::copy_file(original, backup);
            tests::remove_native_map_width(original);
        } else if (packed) {
            fs::copy_file(original, backup);
            write(original, "BAD!");
        } else fs::rename(original, backup);
        run.key(parent, VK_END);
        run.key(parent, VK_RETURN);
        const auto child = run.child(parent);
        run.exit(child, 1);
        run.exit(parent, 0); // A successful old parent is not evidence of a successful new battle.
        MG_CHECK(run.processes.size() == 2);
        const auto *failure = run.slot(child);
        MG_CHECK(failure && failure->diagnostics == 1);
        const auto expected = corrupt_map ? L"Unable to load map" : packed ? L"Invalid packed configuration: cfg\\robots.dat" : L"iface.txt";
        MG_CHECK(std::wstring(failure->diagnostic_text).find(expected) != std::wstring::npos);
        // A bad archive fails before startup_map/begin_map_load; that path has
        // not attempted the remembered map and therefore retains its choice.
        const auto saved_map = StandaloneSettings::load((deployment.app / "CFG/standalone.txt").c_str()).values.map;
        MG_CHECK(saved_map == (packed && !corrupt_map ? L"Matrix\\Map\\second.cmap" : L""));
    }
    if (corrupt_map || packed) restore_bytes(backup, original);
    else fs::rename(backup, original);
    {
        Harness fresh(deployment);
        start_battle(fresh, corrupt_map || packed);
        MG_CHECK(fresh.message(fresh.root_pid, WM_CLOSE));
        fresh.exit(fresh.root_pid, 0);
        MG_CHECK(fresh.processes.size() == 1);
    }
}
void unreadable_map_rejected() {
    Deployment deployment;
    const auto map = deployment.app / "Matrix/Map/second.cmap";
    const auto backup = deployment.app / "map-backup";
    fs::copy_file(map, backup);
    Harness run(deployment);
    start_battle(run);
    const auto parent = run.root_pid;
    run.key(parent, VK_ESCAPE);
    run.wait(parent, [](const auto &state) { return state.paused && state.hints == 1; }, "battle menu");
    run.key(parent, 'N');
    run.wait(parent, [](const auto &state) { return state.selector; }, "battle selector");
    // Discovery succeeded; accepting the same listed file must recheck its header.
    write(map, std::string(4, '\0'));
    run.key(parent, VK_END);
    run.key(parent, VK_RETURN);
    const auto rejected = run.wait(parent, [](const auto &state) { return state.selector && state.paused; }, "unreadable selection retained");
    MG_CHECK(std::wstring(rejected.map) == L"Matrix\\Map\\first.cmap");
    MG_CHECK(run.processes.size() == 1);
    restore_bytes(backup, map);
    run.key(parent, VK_RETURN);
    const auto child = run.child(parent);
    const auto loaded = run.wait(child, [](const auto &state) { return state.initialized; }, "restored map child");
    MG_CHECK(std::wstring(loaded.map) == L"Matrix\\Map\\second.cmap" && loaded.robots == 2);
    MG_CHECK(run.message(child, WM_ACTIVATEAPP, TRUE));
    run.wait(child, [](const auto &state) { return !state.parent_handshake; }, "restored map Present");
    run.exit(parent, 0);
    run.key(child, VK_RETURN);
    run.wait(child, [](const auto &state) { return !state.paused && !state.dialog[0] && state.time > 100; }, "restored map simulation");
    MG_CHECK(run.message(child, WM_CLOSE));
    run.exit(child, 0);
    MG_CHECK(run.processes.size() == 2);
}
void battle_end(bool surrender, bool cancel, bool close_selector = false) {
    Deployment deployment;
    Harness run(deployment);
    start_battle(run);
    const auto parent = run.root_pid;
    run.key(parent, VK_ESCAPE);
    run.wait(parent, [](const auto &state) { return state.paused && state.hints == 1; }, "battle menu");
    run.key(parent, surrender ? 'S' : 'E');
    run.wait(parent, [](const auto &state) { return state.hints == 2; }, "exit confirmation");
    run.key(parent, VK_RETURN);
    const auto statistics = run.wait(parent, [](const auto &state) { return std::wstring(state.dialog) == L"Stat"; }, "battle statistics");
    MG_CHECK(statistics.exit_state == (surrender ? 4 : 1));
    MG_CHECK(!statistics.selector);
    run.key(parent, VK_RETURN);
    if (!surrender) {
        run.exit(parent, 0);
        MG_CHECK(run.processes.size() == 1);
        return;
    }
    run.wait(parent, [](const auto &state) { return state.selector; }, "battle-end selector");
    if (cancel) {
        if (close_selector) MG_CHECK(run.message(parent, WM_CLOSE));
        else run.key(parent, VK_ESCAPE);
        run.exit(parent, 0);
        MG_CHECK(run.processes.size() == 1);
        return;
    }
    run.key(parent, VK_END);
    run.key(parent, VK_RETURN);
    const auto child = run.child(parent);
    auto loaded = run.wait(child, [](const auto &state) { return state.initialized; }, "next battlefield");
    MG_CHECK(std::wstring(loaded.map) == L"Matrix\\Map\\second.cmap" && loaded.direct_start && loaded.robots == 2);
    MG_CHECK(run.message(child, WM_ACTIVATEAPP, TRUE));
    run.wait(child, [](const auto &state) { return !state.parent_handshake; }, "next battle ready");
    run.exit(parent, 0);
    run.key(child, VK_RETURN);
    run.wait(child, [](const auto &state) { return !state.paused && !state.dialog[0] && state.time > 100; }, "next battle simulation");
    MG_CHECK(run.message(child, WM_CLOSE));
    run.exit(child, 0);
    MG_CHECK(run.processes.size() == 2);
}
enum class PendingAction { None, Restart, Exit };
protocol::Snapshot submit_native_move(Harness &run, DWORD pid) {
    run.key(pid, VK_OEM_PERIOD); // Production next-robot selection and camera centering.
    auto selected = run.wait(pid, [](const auto &state) { return state.selected == 1; }, "friendly robot selected");
    run.key(pid, 'M');
    run.wait(pid, [](const auto &state) { return state.move_preorder; }, "Move command cursor");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool aimed = false;
    LPARAM point{};
    protocol::Snapshot last{};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto state = run.wait(pid, [](const auto &state) { return state.initialized; }, "movement destination projection");
        last = state;
        if (!std::isfinite(state.destination_x) || !std::isfinite(state.destination_y)) continue;
        MG_CHECK(state.render_width > 0 && state.render_height > 0 && state.client_width > 0 && state.client_height > 0);
        // Invert Display::map_mouse in the posting caller's client-coordinate space.
        // Windows virtualizes GetClientRect according to this controller's DPI context.
        RECT client{};
        MG_CHECK(GetClientRect(run.window(pid), &client));
        const int x = int(std::lround(state.destination_x * client.right / state.render_width));
        const int y = int(std::lround(state.destination_y * client.bottom / state.render_height));
        if (x <= 5 || x >= client.right - 5 || y <= 5 || y >= client.bottom - 5) continue;
        point = MAKELPARAM(x, y);
        MG_CHECK(run.message(pid, WM_MOUSEMOVE, 0, point));
        const auto traced = run.wait(pid, [](const auto &state) { return state.initialized; }, "movement destination trace");
        if (traced.interface_focus == 0 && traced.trace_land && std::abs(traced.trace_x - 200) < 10 && std::abs(traced.trace_y - 100) < 10) {
            aimed = true;
            break;
        }
        run.pump();
    }
    if (!aimed) {
        RECT client{}; GetClientRect(run.window(pid), &client);
        std::cerr << "Destination projection=" << last.destination_x << ',' << last.destination_y
                  << " trace=" << last.trace_x << ',' << last.trace_y << " focus=" << last.interface_focus
                  << " client=" << client.right << 'x' << client.bottom
                  << " native client=" << last.client_width << 'x' << last.client_height
                  << " cursor=" << last.cursor_x << ',' << last.cursor_y << '\n';
    }
    MG_CHECK(aimed);
    MG_CHECK(run.message(pid, WM_LBUTTONDOWN, MK_LBUTTON, point));
    MG_CHECK(run.message(pid, WM_LBUTTONUP, 0, point));
    const auto moving = run.wait(pid, [&](const auto &state) {
        return state.friendly_move && std::hypot(state.friendly_x - selected.friendly_x, state.friendly_y - selected.friendly_y) > 1;
    }, "accepted Move and actual displacement");
    return moving;
}
void active_order_relaunch(bool different, random::Mode mode) {
    Deployment deployment;
    const auto settings_path = deployment.app / "CFG/standalone.txt";
    auto settings = StandaloneSettings::load(settings_path.c_str()).values;
    settings.random = mode;
    settings.display.width = 1280;
    settings.display.height = 720;
    std::string save_error;
    MG_CHECK(StandaloneSettings::save(settings, save_error, settings_path.c_str()));
    Harness run(deployment);
    start_battle(run);
    const auto parent = run.root_pid;
    const auto before = StandaloneSettings::load(settings_path.c_str()).values;
    submit_native_move(run, parent);
    run.key(parent, VK_ESCAPE);
    const auto menu = run.wait(parent, [](const auto &state) { return std::wstring(state.dialog) == L"Menu" && state.paused; }, "active-order menu");
    std::cout << "Menu Move=" << menu.friendly_move << " selected=" << menu.selected << " position=" << menu.friendly_x << ',' << menu.friendly_y << '\n';
    MG_CHECK(menu.friendly_move && menu.selected == 0 && menu.runtime_random == static_cast<int>(mode));
    run.key(parent, 'N');
    const auto selector = run.wait(parent, [](const auto &state) { return state.selector; }, "active-order selector");
    MG_CHECK(selector.paused && selector.friendly_move && selector.time == menu.time);
    MG_CHECK(selector.friendly_x == menu.friendly_x && selector.friendly_y == menu.friendly_y);
    MG_CHECK(selector.order_x == menu.order_x && selector.order_y == menu.order_y);
    const auto hold_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < hold_until) run.pump();
    run.key(parent, VK_ESCAPE);
    const auto canceled = run.wait(parent, [](const auto &state) { return !state.selector; }, "active-order selector canceled");
    MG_CHECK(std::wstring(canceled.dialog) == L"Menu" && canceled.paused && canceled.friendly_move && canceled.selected == menu.selected);
    MG_CHECK(canceled.time == menu.time && canceled.friendly_x == menu.friendly_x && canceled.friendly_y == menu.friendly_y);
    MG_CHECK(canceled.order_x == menu.order_x && canceled.order_y == menu.order_y);
    MG_CHECK(!canceled.relaunch_requested && run.processes.size() == 1);
    const auto after = StandaloneSettings::load(settings_path.c_str()).values;
    MG_CHECK(after.map == before.map && after.battle_bonus == before.battle_bonus);
    run.key(parent, VK_ESCAPE);
    run.wait(parent, [&](const auto &state) {
        return !state.paused && state.friendly_move && state.order_x == menu.order_x && state.order_y == menu.order_y &&
            std::hypot(state.friendly_x - menu.friendly_x, state.friendly_y - menu.friendly_y) > 1;
    }, "same Move resumes after selector Cancel");
    run.key(parent, VK_ESCAPE);
    run.wait(parent, [](const auto &state) { return state.paused && state.friendly_move; }, "resumed active-order menu");
    run.key(parent, 'N');
    run.wait(parent, [](const auto &state) { return state.selector; }, "active-order selector reopened");
    run.key(parent, different ? VK_END : VK_HOME);
    run.key(parent, VK_RETURN);
    const auto child = run.child(parent);
    const auto loaded = run.wait(child, [](const auto &state) { return state.initialized; }, "active-order next battle");
    MG_CHECK(std::wstring(loaded.map) == (different ? L"Matrix\\Map\\second.cmap" : L"Matrix\\Map\\first.cmap"));
    MG_CHECK(loaded.direct_start && loaded.robots == 2 && loaded.runtime_random == static_cast<int>(mode));
    MG_CHECK(!loaded.friendly_move && loaded.selected == 0 && loaded.friendly_x == 100 && loaded.friendly_y == 100);
    MG_CHECK(loaded.result_countdown == 0 && !loaded.win);
    MG_CHECK(run.message(child, WM_ACTIVATEAPP, TRUE));
    run.wait(child, [](const auto &state) { return !state.parent_handshake; }, "active-order child first Present");
    run.exit(parent, 0);
    run.key(child, VK_RETURN);
    run.wait(child, [](const auto &state) { return !state.paused && !state.dialog[0] && state.time > 100; }, "active-order child simulation");
    submit_native_move(run, child);
    MG_CHECK(run.message(child, WM_CLOSE));
    run.exit(child, 0);
    MG_CHECK(run.processes.size() == 2);
}
void natural_result(bool player_wins, random::Mode mode, PendingAction action = PendingAction::None) {
    Deployment deployment;
    tests::write_native_outcome_map(deployment.app / "Matrix/Map/first.cmap", player_wins);
    const auto settings_path = deployment.app / "CFG/standalone.txt";
    auto settings = StandaloneSettings::load(settings_path.c_str()).values;
    settings.random = mode;
    std::string save_error;
    MG_CHECK(StandaloneSettings::save(settings, save_error, settings_path.c_str()));
    Harness run(deployment);
    const auto parent = run.root_pid;
    const auto initial = start_battle(run);
    MG_CHECK(initial.friendly_robots == 1 && initial.enemy_robots == 1);
    MG_CHECK(initial.friendly_weapons == (player_wins ? 1 : 0));
    MG_CHECK(initial.enemy_weapons == (player_wins ? 0 : 1));
    MG_CHECK(initial.runtime_random == static_cast<int>(mode));
    const auto damaged = run.wait(parent, [&](const auto &state) {
        return player_wins ? state.enemy_health < initial.enemy_health : state.friendly_health < initial.friendly_health;
    }, "real laser damage");
    std::cout << "Laser damage friendly=" << damaged.friendly_health << " enemy=" << damaged.enemy_health << " time=" << damaged.time << '\n';
    const auto pending = run.wait(parent, [](const auto &state) { return state.pending_result && !state.dialog[0]; }, "natural pending result", 60);
    MG_CHECK(pending.win == player_wins);
    MG_CHECK(pending.result_countdown == 1);
    const int old_result_deadline = pending.status_scan_timestamp + 1001;
    MG_CHECK(pending.time <= old_result_deadline);
    std::cout << "Pending result time=" << pending.time << " scan timestamp=" << pending.status_scan_timestamp
              << " countdown=" << pending.result_countdown << " due after=" << old_result_deadline << '\n' << std::flush;
    MG_CHECK(pending.friendly_robots == (player_wins ? 1 : 0) && pending.enemy_robots == (player_wins ? 0 : 1));
    if (action != PendingAction::None) {
        run.key(parent, VK_PAUSE);
        const auto paused = run.wait(parent, [](const auto &state) { return state.paused && state.pending_result && !state.dialog[0]; }, "paused pending result");
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
        while (std::chrono::steady_clock::now() < until) run.pump();
        const auto held = run.wait(parent, [](const auto &state) { return state.paused && !state.dialog[0]; }, "held pending result");
        MG_CHECK(held.time == paused.time && held.pending_result && held.win == player_wins);
        MG_CHECK(held.result_countdown == 1 && held.status_scan_timestamp == pending.status_scan_timestamp);
        run.key(parent, VK_ESCAPE);
        run.wait(parent, [](const auto &state) { return std::wstring(state.dialog) == L"Menu" && state.hints == 1; }, "pending result menu");
        run.key(parent, action == PendingAction::Restart ? 'R' : 'E');
        run.wait(parent, [](const auto &state) { return state.hints == 2; }, "pending action confirmation");
        run.key(parent, VK_RETURN);
        if (action == PendingAction::Restart) {
            const auto restarted = run.wait(parent, [](const auto &state) { return std::wstring(state.dialog) == L"Begin"; }, "pending result restart");
            MG_CHECK(restarted.friendly_robots == 1 && restarted.enemy_robots == 1 && !restarted.win);
            MG_CHECK(restarted.friendly_health == initial.friendly_health && restarted.enemy_health == initial.enemy_health);
            MG_CHECK(restarted.runtime_random == static_cast<int>(mode));
            std::cout << "Restart time=" << restarted.time << " scan timestamp=" << restarted.status_scan_timestamp
                      << " countdown=" << restarted.result_countdown << '\n' << std::flush;
            MG_CHECK(restarted.time == held.time && restarted.status_scan_timestamp == pending.status_scan_timestamp);
            MG_CHECK(restarted.result_countdown == 0);
            run.key(parent, VK_RETURN);
            // The status scan tests now - previous > 1001, then stores now + 1001.
            // Restart retains that timestamp; use the observed deadline, not a guessed delay.
            const auto fresh = run.wait(parent, [&](const auto &state) { return state.time > old_result_deadline; }, "restart passes old result deadline");
            MG_CHECK(fresh.status_scan_timestamp > pending.status_scan_timestamp && fresh.result_countdown == 0);
            std::cout << "Fresh battle time=" << fresh.time << " scan timestamp=" << fresh.status_scan_timestamp
                      << " countdown=" << fresh.result_countdown << '\n' << std::flush;
            MG_CHECK(!fresh.paused && !fresh.dialog[0] && !fresh.pending_result && !fresh.win);
            MG_CHECK(fresh.friendly_robots == 1 && fresh.enemy_robots == 1);
            MG_CHECK(run.message(parent, WM_CLOSE));
        } else {
            const auto statistics = run.wait(parent, [](const auto &state) { return std::wstring(state.dialog) == L"Stat"; }, "pending exit statistics");
            MG_CHECK(statistics.exit_state == 1 && !statistics.selector);
            run.key(parent, VK_RETURN);
        }
        run.exit(parent, 0);
        MG_CHECK(run.processes.size() == 1);
        return;
    }
    const auto result = run.wait(parent, [&](const auto &state) { return std::wstring(state.dialog) == (player_wins ? L"Win" : L"Loose"); }, "natural result dialog");
    MG_CHECK(result.paused);
    run.key(parent, VK_RETURN);
    const auto statistics = run.wait(parent, [](const auto &state) { return std::wstring(state.dialog) == L"Stat"; }, "result statistics");
    MG_CHECK(statistics.exit_state == (player_wins ? 3 : 2));
    run.key(parent, VK_RETURN);
    run.wait(parent, [](const auto &state) { return state.selector; }, "natural result selector");
    run.key(parent, player_wins ? VK_END : VK_HOME);
    run.key(parent, VK_RETURN);
    const auto child = run.child(parent);
    const auto loaded = run.wait(child, [](const auto &state) { return state.initialized; }, "natural result next battle");
    MG_CHECK(std::wstring(loaded.map) == (player_wins ? L"Matrix\\Map\\second.cmap" : L"Matrix\\Map\\first.cmap"));
    MG_CHECK(loaded.direct_start && loaded.friendly_robots == 1 && loaded.enemy_robots == 1 && !loaded.win);
    MG_CHECK(loaded.friendly_health == initial.friendly_health && loaded.enemy_health == initial.enemy_health);
    MG_CHECK(loaded.runtime_random == static_cast<int>(mode));
    MG_CHECK(run.message(child, WM_ACTIVATEAPP, TRUE));
    run.wait(child, [](const auto &state) { return !state.parent_handshake; }, "natural result next battle Present");
    run.exit(parent, 0);
    run.key(child, VK_RETURN);
    run.wait(child, [](const auto &state) { return !state.paused && !state.dialog[0] && state.time > 100; }, "natural result next simulation");
    MG_CHECK(run.message(child, WM_CLOSE));
    run.exit(child, 0);
    MG_CHECK(run.processes.size() == 2);
}
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    if (std::string_view(argv[1]).ends_with("_packed")) configuration_profile = ConfigurationProfile::Packed;
    probe_executable = fs::absolute(argv[2]);
    compiler_directory = fs::absolute(argv[3]).parent_path();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    tests::Case cases[] = {{"game.session.exe_configuration_assets", configuration_assets},
        {"game.session.exe_pointer_guard_policy", pointer_guard_policy},
        {"game.session.exe_startup_selector_close", startup_selector_close},
        {"game.session.exe_surrender_close", [] { battle_end(true, true, true); }},
        {"game.session.exe_startup_cancel", startup_cancel}, {"game.session.exe_startup_battle", startup_battle},
        {"game.session.exe_same_map", [] { battle_relaunch(false); }}, {"game.session.exe_different_map", [] { battle_relaunch(true); }},
        {"game.session.exe_settings_relaunch", [] { battle_relaunch(true, true); }},
        {"game.session.exe_failed_child_recovery", [] { failed_child_recovery(); }},
        {"game.session.exe_failed_map_recovery", [] { failed_child_recovery(true); }},
        {"game.session.exe_unreadable_map_rejected", unreadable_map_rejected},
        {"game.session.exe_surrender_cancel", [] { battle_end(true, true); }},
        {"game.session.exe_surrender_next", [] { battle_end(true, false); }},
        {"game.session.exe_menu_exit", [] { battle_end(false, false); }},
        {"game.session.exe_active_order_same_park_miller", [] { active_order_relaunch(false, random::Mode::ParkMiller); }},
        {"game.session.exe_active_order_different_park_miller", [] { active_order_relaunch(true, random::Mode::ParkMiller); }},
        {"game.session.exe_active_order_same_legacy_crt", [] { active_order_relaunch(false, random::Mode::LegacyCRT); }},
        {"game.session.exe_active_order_different_legacy_crt", [] { active_order_relaunch(true, random::Mode::LegacyCRT); }},
        {"game.session.exe_natural_win_park_miller", [] { natural_result(true, random::Mode::ParkMiller, PendingAction::None); }},
        {"game.session.exe_natural_win_restart_park_miller", [] { natural_result(true, random::Mode::ParkMiller, PendingAction::Restart); }},
        {"game.session.exe_natural_win_exit_park_miller", [] { natural_result(true, random::Mode::ParkMiller, PendingAction::Exit); }},
        {"game.session.exe_natural_loss_park_miller", [] { natural_result(false, random::Mode::ParkMiller, PendingAction::None); }},
        {"game.session.exe_natural_loss_restart_park_miller", [] { natural_result(false, random::Mode::ParkMiller, PendingAction::Restart); }},
        {"game.session.exe_natural_loss_exit_park_miller", [] { natural_result(false, random::Mode::ParkMiller, PendingAction::Exit); }},
        {"game.session.exe_natural_win_legacy_crt", [] { natural_result(true, random::Mode::LegacyCRT, PendingAction::None); }},
        {"game.session.exe_natural_win_restart_legacy_crt", [] { natural_result(true, random::Mode::LegacyCRT, PendingAction::Restart); }},
        {"game.session.exe_natural_win_exit_legacy_crt", [] { natural_result(true, random::Mode::LegacyCRT, PendingAction::Exit); }},
        {"game.session.exe_natural_loss_legacy_crt", [] { natural_result(false, random::Mode::LegacyCRT, PendingAction::None); }},
        {"game.session.exe_natural_loss_restart_legacy_crt", [] { natural_result(false, random::Mode::LegacyCRT, PendingAction::Restart); }},
        {"game.session.exe_natural_loss_exit_legacy_crt", [] { natural_result(false, random::Mode::LegacyCRT, PendingAction::Exit); }}};
    std::array<std::string, std::size(cases)> packed_names;
    if (configuration_profile == ConfigurationProfile::Packed)
        for (size_t i = 0; i < std::size(cases); ++i) {
            packed_names[i] = std::string(cases[i].name) + "_packed";
            cases[i].name = packed_names[i].c_str();
        }
    const int result = tests::run(2, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
