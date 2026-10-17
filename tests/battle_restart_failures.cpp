// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_device.hpp"
#include "battle_geometry.hpp"
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixSkinManager.hpp"
#include "SessionCleanup.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceButton.h"
#include "Interface/MatrixHint.hpp"
#include "input.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

LRESULT CALLBACK L3G_WndProc(HWND, UINT, WPARAM, LPARAM);

namespace {
struct MessageWindow {
    HWND value{};
    MessageWindow() {
        WNDCLASSW type{};
        type.lpfnWndProc = L3G_WndProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"MatrixRestartFailureTest";
        MG_CHECK(RegisterClassW(&type) != 0);
        value = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, type.hInstance, nullptr);
        MG_CHECK(value != nullptr);
        g_Wnd = value;
    }
    ~MessageWindow() {
        if (value) DestroyWindow(value);
        UnregisterClassW(L"MatrixRestartFailureTest", GetModuleHandleW(nullptr));
    }
    void key(uint8_t key) {
        MG_CHECK(PostMessageW(value, WM_KEYDOWN, key, 0));
        MG_CHECK(PostMessageW(value, WM_KEYUP, key, 0));
    }
};

// Every object below is allocated with the production ownership contract.
// In particular, CGame::Deinit may delete heap/data/map/interface; none is a
// borrowed stack object from the normal battle scenario fixture.
struct OwnedBattle {
    tests::BattleDevice device;
    MessageWindow window;
    CFormMatrixGame *form{};
    bool timer_active{};
    std::unique_ptr<CBitmap> bitmap;
    std::filesystem::path path = std::filesystem::current_path() /
        ("restart-failure-" + std::to_string(GetCurrentProcessId()) + ".map");
    bool cleaned{};

    void setup() {
        MG_CHECK(g_MatrixHeap == nullptr && g_MatrixData == nullptr && g_MatrixMap == nullptr);
        MG_CHECK(!std::filesystem::exists(path));
        g_Flags = 0;
        g_Config.SetDefaults();
        g_ScreenX = 1280;
        g_ScreenY = 720;
        Input::onFocusLost();
        CForm::StaticInit();
        // Mirror CGame::static_init for each owned lifetime; Clear frees the
        // skin arrays without resetting their pointers.
        CSkinManager::StaticInit();
        g_MatrixHeap = HNew(nullptr) CHeap;
        g_MatrixData = HNew(g_MatrixHeap) CBlockPar;
        g_MatrixData->BlockGetAdd(L"Chars")->BlockGetAdd(L"Heads");
        g_MatrixData->BlockGetAdd(L"Chars")->BlockGetAdd(L"ChassisSounds")->BlockGetAdd(L"2")
            ->ParSetAdd(L"MoveTo", L"Sound.Synthetic");
        g_MatrixData->BlockGetAdd(L"Sounds")->BlockGetAdd(L"dummy")->ParSetAdd(L"path", L"Sound.Synthetic");
        CacheInit();
        g_MatrixMap = HNew(g_MatrixHeap) CMatrixMapLogic;
        auto &map = *g_MatrixMap;
        g_IFaceList = HNew(g_MatrixHeap) CIFaceList;
        map.m_Ids = static_cast<std::wstring *>(HAlloc(sizeof(std::wstring), g_MatrixHeap));
        new(map.m_Ids) std::wstring(path.wstring());
        map.m_IdsCnt = 1;
        map.m_Side = static_cast<CMatrixSideUnit *>(HAllocClear(2 * sizeof(CMatrixSideUnit), g_MatrixHeap));
        for (int i = 0; i < 2; ++i) {
            new(map.m_Side + i) CMatrixSideUnit;
            ++map.m_SideCnt;
            map.m_Side[i].m_Id = i + 1;
            map.m_Side[i].SetStatus(SS_ACTIVE);
        }
        map.m_PlayerSide = map.m_Side;
        map.GetPlayerSide()->InitPlayerSide();
        tests::BattleGeometry::configure(map);
        auto *robot = tests::BattleGeometry::create_robot(map, PLAYER_SIDE, 100, 200);
        tests::BattleGeometry::create_robot(map, 2, 500, 500);
        map.GetPlayerSide()->CreateGroupFromCurrent(robot);
        map.GetPlayerSide()->m_CurrSel = ROBOT_SELECTED;
        map.m_TraceStopObj = TRACE_STOP_LANDSCAPE;
        map.m_TraceStopPos = D3DXVECTOR3(500, 300, 0);
        map.GetPlayerSide()->OnRButtonDown(CPoint(0, 0));
        MG_CHECK(map.GetPlayerSide()->m_PlayerGroup[robot->GetGroupLogic()].Order() == mpo_MoveTo);

        auto *hints = HNew(g_MatrixHeap) CInterface;
        g_IFaceList->m_Hints = hints;
        g_IFaceList->m_First = g_IFaceList->m_Last = hints;
        bitmap = std::make_unique<CBitmap>();
        bitmap->CreateRGBA(2, 2);
        std::memset(bitmap->Data(), 255, bitmap->Pitch() * bitmap->SizeY());
        CMatrixHint::SetRuntimeBitmap(L"restart-failure-pixel", bitmap.get());
        g_MatrixData->BlockGetAdd(PAR_REPLACE);
        for (const auto *name : {IF_HINTS_OK, IF_HINTS_CANCEL, IF_HINTS_CANCEL_MENU,
             IF_HINTS_CONTINUE, IF_HINTS_SURRENDER, IF_HINTS_RESET, IF_HINTS_EXIT, IF_HINTS_HELP}) {
            auto *button = HNew(g_MatrixHeap) CIFaceButton;
            button->m_Type = IFACE_PUSH_BUTTON;
            button->m_strName = name;
            MG_CHECK(hints->AddElement(button));
        }
        for (const auto *name : {TEMPLATE_DIALOG_MENU, TEMPLATE_DIALOG_RESET}) {
            std::wstring markup = L"-1|_BITMAP:restart-failure-pixel:";
            for (int i = 0; i < 6; ++i) markup += L"|_MOD:COPY|_BITMAP:restart-failure-pixel:";
            g_MatrixData->BlockGetAdd(PAR_TEMPLATES)->ParSetAdd(name, markup);
        }
        form = HNew(nullptr) CFormMatrixGame;
    }
    void cleanup() {
        CMatrixHint::SetRuntimeBitmap(L"restart-failure-pixel", nullptr);
        bitmap.reset();
        const bool released = Session::cleanup_standalone(form, timer_active);
        const bool device_cleared = g_D3DD == nullptr;
        // Re-arm the fixture-owned alias before any assertion can unwind.
        g_D3DD = device.get();
        cleaned = true;
        MG_CHECK(released);
        MG_CHECK(!form && !timer_active && !g_FormCur && !g_FormFirst && !g_FormLast);
        MG_CHECK(!g_MatrixMap && !g_MatrixData && !g_MatrixHeap && !g_IFaceList);
        MG_CHECK(!g_Cache && !g_CacheHeap && device_cleared && !g_D3D && !g_Wnd);
        MG_CHECK(!CMatrixMapStatic::GetFirstLogic());
        MG_CHECK(device.stats().live_buffers == 0 && device.stats().live_textures == 0);
    }
    ~OwnedBattle() {
        if (!cleaned) {
            // Preserve an existing assertion during stack unwinding. These
            // cleanup attempts contain exceptions and perform no assertions.
            Session::cleanup([&] { CMatrixHint::SetRuntimeBitmap(L"restart-failure-pixel", nullptr); },
                             [&] { bitmap.reset(); });
            Session::cleanup_standalone(form, timer_active);
            g_D3DD = device.get();
        }
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

enum class Reload { None, Missing, Corrupt };
void one_lifetime(Reload reload, random::Mode mode) {
    CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    random::seed(79, mode);
    OwnedBattle battle;
    battle.setup();
    if (reload == Reload::Corrupt) {
        const std::array<unsigned char, 12> invalid_header{};
        std::ofstream stream(battle.path, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(invalid_header.data()), invalid_header.size());
        MG_CHECK(stream.good());
    }
    if (reload != Reload::None) {
        battle.window.key(VK_ESCAPE);
        battle.window.key('R');
        battle.window.key(VK_RETURN);
    }
    // A failed command must not hang the test if the exception is lost.
    MG_CHECK(PostMessageW(battle.window.value, WM_CLOSE, 0, 0));
    battle.timer_active = true;
    CGame::RunGameLoop(battle.form);
    MG_CHECK(battle.device.stats().light_updates == 1);
    MG_CHECK(battle.device.stats().light_enable_updates == 1);
    D3DLIGHT9 light{};
    BOOL enabled = FALSE;
    MG_CHECK(battle.device.get()->GetLight(0, &light) == D3D_OK && light.Type == D3DLIGHT_DIRECTIONAL);
    MG_CHECK(battle.device.get()->GetLightEnable(0, &enabled) == D3D_OK && enabled);
    if (reload != Reload::None) {
        MG_CHECK(g_ExitState == 100 && FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!CMatrixMapStatic::GetFirstLogic());
    } else {
        MG_CHECK(g_ExitState == 0 && FLAG(g_Flags, GFLAG_APPCLOSE));
        MG_CHECK(CMatrixMapStatic::GetFirstLogic());
    }
    battle.cleanup();
}
void reload_failure(Reload reload) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            one_lifetime(reload, mode);
#ifdef MEM_SPY_ENABLE
            MG_CHECK(Base::SMemHeader::first_mem_block == nullptr);
#endif
        }
    }
}
void owned_cleanup() { reload_failure(Reload::None); }
void missing() { reload_failure(Reload::Missing); }
void corrupt() { reload_failure(Reload::Corrupt); }
constexpr tests::Case cases[] = {
    {"game.restart.game_owned_cleanup", owned_cleanup},
    {"probe.restart.missing_file_cleanup", missing},
    {"probe.restart.corrupt_file_cleanup", corrupt},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
