// MatrixGame - licensed under GPLv2 or any later version.
// Linked alongside the unchanged production WinMain in a separate test EXE.
#include "standalone_session_protocol.hpp"
#include "native_window_containment.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "MapWindow.hpp"
#include "StartupMenu.hpp"
#include "BattleLoading.hpp"
#include "SettingsWindow.hpp"
#include "StandaloneRuntime.hpp"
#include "MatrixConfig.hpp"
#include <array>
#include <algorithm>
#include <cwchar>
#include <memory>
#include <string>

namespace {
namespace protocol = tests::native_session;
protocol::Process *observed;
template<size_t N> void copy(wchar_t (&target)[N], const wchar_t *source) {
    std::fill(std::begin(target), std::end(target), L'\0');
    if (source) std::copy_n(source, std::min(std::wcslen(source), N - 1), target);
}
void diagnostic(const wchar_t *title, const wchar_t *text) {
    copy(observed->diagnostic_title, title);
    copy(observed->diagnostic_text, text);
    InterlockedIncrement(&observed->diagnostics);
}
// Taking a protected member's pointer in a derived class is permitted; applying
// that base-class member pointer reads the genuine CMatrixMap without a downcast.
struct MapObservation : CMatrixMap {
    static int status_timestamp(const CMatrixMap &map) { return map.*(&MapObservation::m_PrevTimeCheckStatus); }
    static int result_countdown(const CMatrixMap &map) { return map.*(&MapObservation::m_BeforeWinLooseDialogCount); }
};
void snapshot(LONG request) {
    protocol::Snapshot value;
    value.request = request;
    const bool battle_form = dynamic_cast<CFormMatrixGame *>(g_FormCur) != nullptr;
    if (g_FormCur) copy(value.form, battle_form ? L"battle" : L"other");
    value.flags = g_Flags;
    value.exit_state = g_ExitState;
    value.selector = MapWindow::active();
    value.direct_start = StartupMenu::direct_start();
    value.parent_handshake = BattleLoading::has_parent();
    if (g_MatrixMap && battle_form) {
        value.initialized = true;
        const auto &settings = StandaloneSettings::current();
        value.settings = SettingsWindow::active();
        value.hints = g_MatrixMap->m_DialogModeHints.size();
        value.saved_camera = settings.camera_speed;
        value.saved_skybox = static_cast<int>(settings.skybox);
        value.saved_random = static_cast<int>(settings.random);
        value.runtime_camera = g_Config.m_CamMoveSpeed;
        value.runtime_skybox = g_Config.m_SkyBox;
        value.runtime_random = static_cast<int>(random::mode());
        value.restart_pending = StandaloneSettings::restart_pending(settings);
        copy(value.dialog, g_MatrixMap->m_DialogModeName);
        copy(value.map, g_MatrixMap->MapName().c_str());
        value.paused = g_MatrixMap->IsPaused();
        value.time = g_MatrixMap->GetTime();
        value.win = FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN);
        value.status_scan_timestamp = MapObservation::status_timestamp(*g_MatrixMap);
        value.result_countdown = MapObservation::result_countdown(*g_MatrixMap);
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (!object->IsLiveRobot()) continue;
            ++value.robots;
            const auto *robot = object->AsRobot();
            int weapons = 0;
            for (int i = 0; i < robot->m_UnitCnt; ++i) if (robot->m_Unit[i].m_Type == MRT_WEAPON) ++weapons;
            if (object->GetSide() == PLAYER_SIDE) {
                ++value.friendly_robots;
                value.friendly_weapons = weapons;
                value.friendly_health = robot->GetHitPoint();
            } else {
                ++value.enemy_robots;
                value.enemy_weapons = weapons;
                value.enemy_health = robot->GetHitPoint();
            }
        }
        value.pending_result = !value.dialog[0] && value.result_countdown == 1;
    }
    InterlockedIncrement(&observed->snapshot_generation);
    observed->snapshot = value;
    InterlockedIncrement(&observed->snapshot_generation);
}
LRESULT CALLBACK messages(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        const auto &message = *reinterpret_cast<CWPSTRUCT *>(lp);
        std::array<wchar_t, 80> name{};
        GetClassNameW(message.hwnd, name.data(), name.size());
        if (std::wstring_view(name.data()) == L"MatrixGame_wc") {
            InterlockedExchange(&observed->window, reinterpret_cast<LONG>(message.hwnd));
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
LRESULT CALLBACK queued_messages(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && wp == PM_REMOVE) {
        const auto &message = *reinterpret_cast<MSG *>(lp);
        if (message.message == protocol::snapshot_message && message.hwnd == g_Wnd) snapshot(message.wParam);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
struct Runtime {
    HANDLE mapping{};
    protocol::Shared *shared{};
    HHOOK hook{};
    HHOOK queue_hook{};
    std::unique_ptr<tests::NativeWindowContainment> windows;
    Runtime() {
        try {
            std::array<wchar_t, 128> name{};
            const auto length = GetEnvironmentVariableW(protocol::environment, name.data(), name.size());
            if (!length || length >= name.size() || !std::wstring_view(name.data()).starts_with(L"Local\\MatrixGameNativeTest-"))
                ExitProcess(97); // This test executable never launches interactively on its own.
            mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.data());
            if (!mapping) ExitProcess(97);
            shared = static_cast<protocol::Shared *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(protocol::Shared)));
            if (!shared || shared->protocol != protocol::version) ExitProcess(97);
            for (auto &slot : shared->processes) {
                if (InterlockedCompareExchange(&slot.id, GetCurrentProcessId(), 0) == 0) { observed = &slot; break; }
            }
            if (!observed) ExitProcess(97);
            SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
            windows = std::make_unique<tests::NativeWindowContainment>(diagnostic);
            hook = SetWindowsHookExW(WH_CALLWNDPROC, messages, nullptr, GetCurrentThreadId());
            queue_hook = SetWindowsHookExW(WH_GETMESSAGE, queued_messages, nullptr, GetCurrentThreadId());
            if (!hook || !queue_hook) ExitProcess(97);
        } catch (...) { ExitProcess(97); }
    }
    ~Runtime() {
        if (hook) UnhookWindowsHookEx(hook);
        if (queue_hook) UnhookWindowsHookEx(queue_hook);
        windows.reset();
        if (shared) UnmapViewOfFile(shared);
        if (mapping) CloseHandle(mapping);
    }
} runtime;
}
