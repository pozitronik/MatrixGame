// MatrixGame - licensed under GPLv2 or any later version.
#include "native_calcvis_assets.hpp"
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixFlyer.hpp"
#include "Interface/CInterface.h"
#include "SessionCleanup.hpp"
#include "StartupMenu.hpp"
#include "StandaloneSettings.hpp"
#include "CStorage.hpp"
#include "input.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <string>
#include <cmath>
#include <iostream>
#include <shellapi.h>

namespace fs = std::filesystem;
// The observer owns its cache/screenshot output inside the explicit synthetic export.
// This replaces only this test executable's destination resolver, never the game's.
std::wstring PathToOutputFiles(const wchar_t *destination) {
    const auto result = fs::current_path() / L"native-view-output" / destination;
    fs::create_directories(result);
    return result.wstring();
}

namespace {
bool accepts(int argc, const wchar_t *const *argv) {
    return argc == 5 && std::wstring_view(argv[1]) == L"--view" && argv[2][0] &&
           (std::wstring_view(argv[3]) == L"Text" || std::wstring_view(argv[3]) == L"Packed") &&
           std::wstring_view(argv[4]) == L"--allow-visible-window";
}

fs::path controlled_map_path() {
    fs::path map = L"Matrix/Map/calcvis-ridge.cmap";
    // The legacy initializer detects qualified paths by backslash, not generic separators.
    map.make_preferred();
    return map;
}

void argument_guard() {
    const wchar_t *arguments[] = {L"observer", L"--view", L"export", L"Text", L"--allow-visible-window"};
    MG_CHECK(accepts(5, arguments));
    MG_CHECK(controlled_map_path().native() == L"Matrix\\Map\\calcvis-ridge.cmap");
    MG_CHECK(!accepts(4, arguments));
    arguments[4] = L"--other";
    MG_CHECK(!accepts(5, arguments));
    arguments[4] = L"--allow-visible-window";
    arguments[3] = L"Other";
    MG_CHECK(!accepts(5, arguments));
}

bool observe_transition(D3DXVECTOR2 &previous, const D3DXVECTOR2 &target, const D3DXVECTOR3 &origin,
                        const D3DXVECTOR3 &direction, float height) {
    // A queued mouse message is not proof that production has rendered another frame.
    if (std::abs(target.x - previous.x) + std::abs(target.y - previous.y) <= 10) return false;
    if (std::abs(direction.z) < 0.001f) return false;
    const float distance = (height - origin.z) / direction.z;
    const auto expected = D3DXVECTOR2(origin.x + direction.x * distance, origin.y + direction.y * distance);
    MG_CHECK(std::isfinite(target.x) && std::isfinite(target.y));
    MG_CHECK(std::abs(target.x - expected.x) < 0.05f && std::abs(target.y - expected.y) < 0.05f);
    previous = target;
    return true;
}

void transition_guard() {
    D3DXVECTOR2 previous(200, 280);
    const D3DXVECTOR3 origin(120, 369, 120), direction(1, 0, -1);
    MG_CHECK(!observe_transition(previous, previous, origin, direction, 70));
    MG_CHECK(previous == D3DXVECTOR2(200, 280));
    MG_CHECK(observe_transition(previous, D3DXVECTOR2(170, 369), origin, direction, 70));
    // A later input can change the current ray before the next frame updates that target too.
    MG_CHECK(!observe_transition(previous, previous, origin, D3DXVECTOR3(-1, 0, -1), 70));
    MG_CHECK(!observe_transition(previous, D3DXVECTOR2(70, 369), origin, D3DXVECTOR3(1, 0, 0), 70));
    MG_CHECK(previous == D3DXVECTOR2(170, 369));
    bool rejected = false;
    try {
        observe_transition(previous, D3DXVECTOR2(70, 369), origin, direction, 70);
    }
    catch (const std::runtime_error &) {
        rejected = true;
    }
    MG_CHECK(rejected && previous == D3DXVECTOR2(170, 369));
    MG_CHECK(observe_transition(previous, D3DXVECTOR2(70, 369), origin, D3DXVECTOR3(-1, 0, -1), 70));
}

std::string bytes(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    MG_CHECK(f.good());
    return {std::istreambuf_iterator<char>(f), {}};
}

#if defined(_DEBUG) && !defined(_RELDEBUG)
struct Observation {
    CMatrixFlyer *flyer{};
    WNDPROC original{};
    UINT_PTR timer{};
    DWORD started{};
    int calculations{}, target_changes{};
    D3DXVECTOR2 last_target{};
    CHelper *sentinel{};
    std::string failure;
} *current;

void fail(std::string_view text) noexcept {
    if (current && current->failure.empty()) current->failure = text;
    SETFLAG(g_Flags, GFLAG_EXITLOOP);
    ClipCursor(nullptr);
}

LRESULT CALLBACK observe(HWND window, UINT message, WPARAM key, LPARAM data) {
    if (!current) return DefWindowProcW(window, message, key, data);
    try {
        if (message == WM_KEYDOWN && key == VK_ESCAPE) {
            SETFLAG(g_Flags, GFLAG_EXITLOOP);
            return 0;
        }
        if (message == WM_KEYDOWN && key == VK_F7) {
            tests::check_calcvis_runtime(*g_MatrixMap);
            const auto result = CallWindowProcW(current->original, window, message, key, data);
            int markers = 0;
            for (auto *helper = current->sentinel->Next(); helper; helper = helper->Next())
                if (helper->Group() == 888) ++markers;
            MG_CHECK(markers > 0 && g_MatrixMap->m_GroupVis);
            ++current->calculations;
            std::cout << "F7 returned; Debug CalcVis helpers=" << markers << '\n' << std::flush;
            return result;
        }
        return CallWindowProcW(current->original, window, message, key, data);
    }
    catch (const std::exception &error) {
        fail(error.what());
    }
    catch (...) {
        fail("Unexpected observer window callback failure");
    }
    return 0;
}

void CALLBACK sample(HWND, UINT, UINT_PTR, DWORD) {
    try {
        MG_CHECK(current && g_MatrixMap && current->flyer);
        if (DWORD(GetTickCount() - current->started) > 120000) {
            fail("Native observation exceeded its 120-second deadline");
            return;
        }
        tests::check_calcvis_runtime(*g_MatrixMap);
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(g_MatrixMap->GetPlayerSide()->m_ActiveObject == current->flyer);
        if (!Input::isKeyPressed(KA_AUTO) || g_IFaceList->m_InFocus == INTERFACE ||
            g_MatrixMap->m_TraceStopObj == current->flyer) return;
        const auto target = current->flyer->GetTarget();
        if (observe_transition(current->last_target, target, g_MatrixMap->m_Camera.GetFrustumCenter(),
                               g_MatrixMap->m_MouseDir, current->flyer->GetPos().z)) {
            ++current->target_changes;
            std::cout << "Production flyer target=" << target.x << ',' << target.y
                      << "; horizontal-plane hit verified\n" << std::flush;
        }
    }
    catch (const std::exception &error) {
        fail(error.what());
    }
    catch (...) {
        fail("Unexpected native observer sample failure");
    }
}

int view(const fs::path &root, bool packed) {
    MG_CHECK(fs::equivalent(fs::current_path(), root));
    const fs::path map = controlled_map_path();
    tests::validate_native_calcvis(map, true);
    MG_CHECK(fs::is_regular_file(L"CFG/robots.dat") == packed);
    MG_CHECK(fs::is_regular_file(L"CFG/robots/data.txt") == !packed);
    for (const auto &entry : fs::directory_iterator(L"CFG"))
        MG_CHECK(entry.path().filename().wstring().rfind(L"robots.dat.", 0) != 0);
    for (const auto *package : {L"sound.pkg", L"voices.pkg", L"music.pkg"})
        MG_CHECK(!fs::exists(fs::path(L"DATA") / package));
    {
        CBlockPar data;
        if (packed) {
            CStorage archive;
            MG_CHECK(archive.Load(L"CFG/robots.dat"));
            archive.RestoreBlockPar(L"da", data);
        }
        else data.LoadFromTextFile(L"CFG/robots/data.txt");
        tests::validate_native_calcvis_configuration(data);
        const auto settings = StandaloneSettings::load();
        MG_CHECK(settings.invalid.empty() && settings.values.display.mode == Display::Mode::Windowed);
        MG_CHECK(settings.values.map == map.wstring() || settings.values.map == map.generic_wstring());
        MG_CHECK(settings.values.display.width == 1280 && settings.values.display.height == 720);
        MG_CHECK(!settings.values.stencil_shadows && !settings.values.projected_shadows &&
                 !settings.values.robot_shadows);
    }
    const auto original_map = bytes(map);
    CFormMatrixGame *form = nullptr;
    bool timer_active = false, started = false;
    Observation record;
    current = &record;
    int result = 1;
    try {
        StartupMenu::set_direct_start(true);
        started = true;
        CGame::Init(GetModuleHandleW(nullptr), nullptr, map.c_str(), 87);
        MG_CHECK(g_MatrixMap && g_Wnd);
        tests::check_calcvis_runtime(*g_MatrixMap);
        form = HNew(nullptr) CFormMatrixGame();
        FormChange(form);
        g_MatrixMap->LeaveDialogMode();
        g_MatrixMap->Pause(true);
        auto *flyer = g_MatrixMap->StaticAdd<CMatrixFlyer>(true);
        flyer->SetSide(PLAYER_SIDE);
        flyer->m_FlyerKind = static_cast<EFlyerKind>(1);
        flyer->RNeed(MR_Graph | MR_Matrix);
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt < MAX_ALWAYS_DRAW_OBJ);
        g_MatrixMap->m_AD_Obj[g_MatrixMap->m_AD_Obj_cnt++] = flyer;
        // Explicit frozen fixture precondition: normal gameplay does not expose flyer selection.
        g_MatrixMap->GetPlayerSide()->m_ActiveObject = flyer;
        g_MatrixMap->GetPlayerSide()->m_CurrSel = FLYER_SELECTED;
        record.flyer = flyer;
        record.last_target = flyer->GetTarget();
        record.sentinel = CHelper::Create(0, 889);
        // A valid invisible/off-map primitive avoids drawing an empty helper with a null vertex pointer.
        record.sentinel->Line(D3DXVECTOR3(0, 0, -1000), D3DXVECTOR3(1, 0, -1000), 0, 0);
        record.original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_Wnd, GWLP_WNDPROC));
        MG_CHECK(record.original && SetWindowLongPtrW(g_Wnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(observe)));
        record.started = GetTickCount();
        record.timer = SetTimer(g_Wnd, 0, 250, sample);
        MG_CHECK(record.timer);
        ShowWindow(g_Wnd, SW_SHOWNORMAL);
        UpdateWindow(g_Wnd);
        std::cout <<
        "Visible synthetic observer: simulation paused; no normal flyer selection is claimed.\n"
        "Press F7 once. Keep pointer above battlefield (outside UI), hold right mouse and move between two points.\n"
        "Console must report positive Debug helpers and two distinct verified production flyer targets.\n"
        "Press Escape to close; automatic deadline is 120 seconds.\n" << std::flush;
        L3GRun();
        MG_CHECK(record.failure.empty() && record.calculations > 0 && record.target_changes >= 2);
        MG_CHECK(bytes(map) == original_map);
        result = 0;
    }
    catch (const CException &error) {
        std::wcerr << error.Info() << '\n';
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    catch (...) {
        std::cerr << "Unexpected native observer failure\n";
    }
    if (!record.failure.empty()) std::cerr << record.failure << '\n';
    if (g_Wnd && IsWindow(g_Wnd)) {
        if (record.timer) KillTimer(g_Wnd, record.timer);
        if (record.original) SetWindowLongPtrW(g_Wnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(record.original));
    }
    current = nullptr;
    if (started && !Session::cleanup_standalone(form, timer_active)) result = 1;
    ClipCursor(nullptr);
    std::cout << "Native observer exit=" << result << "; F7=" << record.calculations
              << "; target transitions=" << record.target_changes << '\n';
    return result;
}
#endif
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc >= 2 && std::string_view(argv[1]) == "--view") {
        try {
            int count = 0;
            auto **wide = CommandLineToArgvW(GetCommandLineW(), &count);
            MG_CHECK(wide);
            struct Arguments {
                wchar_t **value;
                ~Arguments() {
                    LocalFree(value);
                }
            } arguments{wide};
            MG_CHECK(accepts(count, wide));
#if defined(_DEBUG) && !defined(_RELDEBUG)
            return view(fs::absolute(wide[2]), std::wstring_view(wide[3]) == L"Packed");
#else
            throw std::runtime_error("The visible diagnostic observer requires a Debug standalone build");
#endif
        }
        catch (const CException &error) {
            std::wcerr << error.Info() << '\n';
            return 1;
        }
        catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    const tests::Case cases[] = {{"game.presentation.calcvis_view_argument_guard", argument_guard},
                                {"game.presentation.calcvis_view_transition_guard", transition_guard}};
    return tests::run(argc, argv, cases);
}
