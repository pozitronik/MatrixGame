// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_device.hpp"
#include "battle_geometry.hpp"
#include "test_support.hpp"
#include "CStorage.hpp"
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
    std::unique_ptr<CTextureManaged> mouse_texture;
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
        hints->m_strName = IF_HINTS;
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
        for (const auto *name : {TEMPLATE_DIALOG_MENU, TEMPLATE_DIALOG_RESET, TEMPLATE_DIALOG_BEGIN}) {
            std::wstring markup = L"-1|_BITMAP:restart-failure-pixel:";
            for (int i = 0; i < 6; ++i) markup += L"|_MOD:COPY|_BITMAP:restart-failure-pixel:";
            g_MatrixData->BlockGetAdd(PAR_TEMPLATES)->ParSetAdd(name, markup);
        }
        form = HNew(nullptr) CFormMatrixGame;
    }
    void queue_mouse_confirmation() {
        for (auto key : {VK_ESCAPE, int('R')}) {
            form->Keyboard(true, static_cast<uint8_t>(key));
            form->Keyboard(false, static_cast<uint8_t>(key));
        }
        MG_CHECK(g_MatrixMap->m_DialogModeName &&
                 wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) == 0 &&
                 g_MatrixMap->m_DialogModeHints.size() == 2);
        mouse_texture = std::make_unique<CTextureManaged>();
        mouse_texture->LoadFromBitmap(*bitmap, false, 1);
        for (auto *element = g_IFaceList->m_Hints->m_FirstElement; element; element = element->m_NextElement) {
            const bool confirm = element->m_strName == IF_HINTS_OK;
            element->SetVisibility(confirm);
            if (!confirm) continue;
            // Synthetic button geometry and focus; hit testing, alpha lookup,
            // press/release, the hint callback and Win32 dispatch remain real.
            element->m_PosElInX = element->m_PosElInY = 100;
            element->m_xSize = element->m_ySize = 2;
            for (auto &image : element->m_StateImages) {
                image.pImage = mouse_texture.get();
                image.xTexPos = image.yTexPos = 0;
                for (int i = 0; i < 4; ++i) {
                    image.m_Geom[i].p.x = 100.f + ((i & 1) ? 2.f : 0.f);
                    image.m_Geom[i].p.y = 100.f + ((i & 2) ? 2.f : 0.f);
                }
            }
            element->SetState(IFACE_FOCUSED);
            MG_CHECK(element->ElementCatch(CPoint(101, 101)) && element->ElementAlpha(CPoint(101, 101)));
        }
        g_IFaceList->m_Hints->m_VisibleAlpha = 255;
        g_IFaceList->m_InFocus = INTERFACE;
        g_MatrixMap->m_Cursor.SetPos(101, 101);
        MG_CHECK(PostMessageW(window.value, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(101, 101)));
        MG_CHECK(PostMessageW(window.value, WM_LBUTTONUP, 0, MAKELPARAM(101, 101)));
    }
    void cleanup() {
        CMatrixHint::SetRuntimeBitmap(L"restart-failure-pixel", nullptr);
        mouse_texture.reset();
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
                             [&] { mouse_texture.reset(); },
                             [&] { bitmap.reset(); });
            Session::cleanup_standalone(form, timer_active);
            g_D3DD = device.get();
        }
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

enum class Reload { None, Missing, Corrupt, UnsupportedVersion, NoProperties, MissingColumn, ShortColumn,
                    PartialRobots, PartialEffects, Empty, EmptyOptional };

void write_empty_scene(const std::filesystem::path &path, Reload kind) {
    CStorage storage;
    if (kind == Reload::NoProperties) { storage.Save(path.c_str()); return; }
    const auto table = [&](const wchar_t *name,
                           std::initializer_list<std::pair<const wchar_t *, EStorageType>> columns) {
        CStorageRecord record(name);
        for (const auto &[column, type] : columns) {
            if (kind == Reload::MissingColumn && wcscmp(name, DATA_BUILDINGS) == 0 &&
                wcscmp(column, DATA_BUILDINGS_SIDE) == 0) continue;
            record.AddItem(CStorageRecordItem(column, type));
        }
        storage.AddRecord(record);
        for (const auto &[column, type] : columns)
            if (type != ST_WCHAR)
                if (auto *buffer = storage.GetBuf(name, column, type)) buffer->AddArray();
    };
    table(DATA_PROPERTIES, {{DATA_PROPERTIES_NAME, ST_WCHAR}, {DATA_PROPERTIES_VALUE, ST_WCHAR}});
    table(DATA_OBJECTS, {{DATA_OBJECTS_X, ST_FLOAT}, {DATA_OBJECTS_Y, ST_FLOAT},
        {DATA_OBJECTS_ANGLE_Z, ST_FLOAT}, {DATA_OBJECTS_SCALE, ST_FLOAT}, {DATA_OBJECTS_TYPE, ST_DWORD}});
    table(DATA_BUILDINGS, {{DATA_BUILDINGS_X, ST_FLOAT}, {DATA_BUILDINGS_Y, ST_FLOAT},
        {DATA_BUILDINGS_SIDE, ST_BYTE}, {DATA_BUILDINGS_KIND, ST_BYTE}, {DATA_BUILDINGS_SHADOW, ST_BYTE},
        {DATA_BUILDINGS_SHADOWSIZE, ST_INT32}, {DATA_BUILDINGS_ANGLE, ST_BYTE}});
    table(DATA_CANNONS, {{DATA_CANNONS_X, ST_FLOAT}, {DATA_CANNONS_Y, ST_FLOAT},
        {DATA_CANNONS_SIDE, ST_BYTE}, {DATA_CANNONS_KIND, ST_BYTE}, {DATA_CANNONS_SHADOW, ST_BYTE},
        {DATA_CANNONS_SHADOWSIZE, ST_INT32}, {DATA_CANNONS_ANGLE, ST_FLOAT}});
    if (kind == Reload::ShortColumn) {
        const float x = 100;
        storage.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_X, ST_FLOAT)->AddToArray<float>(0, &x, 1);
    }
    if (kind == Reload::PartialRobots) table(DATA_ROBOTS, {{DATA_ROBOTS_X, ST_FLOAT}});
    if (kind == Reload::PartialEffects) table(DATA_EFFECTS, {{DATA_EFFECTS_X, ST_FLOAT}});
    if (kind == Reload::EmptyOptional) {
        // A zero-row robot table needs no per-robot units strings.
        table(DATA_ROBOTS, {{DATA_ROBOTS_X, ST_FLOAT}, {DATA_ROBOTS_Y, ST_FLOAT},
            {DATA_ROBOTS_SIDE, ST_BYTE}, {DATA_ROBOTS_SHADOW, ST_BYTE},
            {DATA_ROBOTS_SHADOWSIZE, ST_INT32}, {DATA_ROBOTS_GROUP, ST_INT32}});
        table(DATA_EFFECTS, {{DATA_EFFECTS_X, ST_FLOAT}, {DATA_EFFECTS_Y, ST_FLOAT},
            {DATA_EFFECTS_Z, ST_FLOAT}, {DATA_EFFECTS_TYPE, ST_INT32}});
    }
    storage.Save(path.c_str());
}

void one_lifetime(Reload reload, random::Mode mode, bool mouse = false) {
    CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    random::seed(79, mode);
    OwnedBattle battle;
    battle.setup();
    if (reload == Reload::Corrupt || reload == Reload::UnsupportedVersion) {
        const std::array<uint32_t, 3> invalid_header = reload == Reload::Corrupt
            ? std::array<uint32_t, 3>{} : std::array<uint32_t, 3>{0x47525453u, 2u, 0u};
        std::ofstream stream(battle.path, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(invalid_header.data()), sizeof(invalid_header));
        MG_CHECK(stream.good());
    } else if (reload != Reload::None && reload != Reload::Missing) {
        write_empty_scene(battle.path, reload);
    }
    if (mouse) battle.queue_mouse_confirmation();
    else if (reload != Reload::None) {
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
    if (reload != Reload::None && reload != Reload::Empty && reload != Reload::EmptyOptional) {
        MG_CHECK(g_ExitState == 100 && FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!CMatrixMapStatic::GetFirstLogic());
    } else {
        MG_CHECK(g_ExitState == 0 && FLAG(g_Flags, GFLAG_APPCLOSE));
        MG_CHECK(bool(CMatrixMapStatic::GetFirstLogic()) == (reload == Reload::None));
        if (reload == Reload::Empty || reload == Reload::EmptyOptional)
            MG_CHECK(g_MatrixMap->m_DialogModeName && wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_BEGIN) == 0);
    }
    battle.cleanup();
}
void reload_failure(Reload reload, bool mouse = false) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            one_lifetime(reload, mode, mouse);
#ifdef MEM_SPY_ENABLE
            MG_CHECK(Base::SMemHeader::first_mem_block == nullptr);
#endif
        }
    }
}
void owned_cleanup() { reload_failure(Reload::None); }
void missing() { reload_failure(Reload::Missing); }
void missing_mouse() { reload_failure(Reload::Missing, true); }
void corrupt() { reload_failure(Reload::Corrupt); }
void malformed() {
    for (auto kind : {Reload::UnsupportedVersion, Reload::NoProperties, Reload::MissingColumn, Reload::ShortColumn,
                      Reload::PartialRobots, Reload::PartialEffects}) reload_failure(kind);
}
void empty_storage() { reload_failure(Reload::Empty); reload_failure(Reload::EmptyOptional); }

// A bounded callback source for timing and diagnostic contracts. The window
// procedure, message dispatch, exception transport and loop remain production.
struct TransportForm : CForm {
    HWND window;
    bool fail_in_tick{};
    int keys{}, ticks{}, draws{};
    explicit TransportForm(HWND target) : window(target) {}
    void Enter() override {}
    void Leave() override {}
    void Draw() override {
        ++draws;
        throw std::runtime_error("draw after failed callback");
    }
    void Takt(int) override {
        ++ticks;
        if (fail_in_tick) SendMessageW(window, WM_KEYDOWN, 'F', 0);
    }
    void MouseMove(int, int) override {}
    void MouseKey(ButtonStatus, int, int, int) override {}
    void Keyboard(bool down, uint8_t key) override {
        if (!down) return;
        ++keys;
        if (key == 'F') throw std::runtime_error("original callback diagnostic");
    }
    void SystemEvent(ESysEvent) override {}
};

void callback_transport(bool during_tick, bool abandon = false) {
    tests::BattleDevice device;
    MessageWindow window;
    g_Flags = GFLAG_APPACTIVE;
    {
        TransportForm form(window.value);
        FormChange(&form);
        form.fail_in_tick = during_tick;
        if (!during_tick) {
            // The exception is retained, rather than escaping a synchronous
            // USER32 call before the main loop has even started.
            SendMessageW(window.value, WM_KEYDOWN, 'F', 0);
            MG_CHECK(form.keys == 1);
            SendMessageW(window.value, WM_KEYDOWN, 'S', 0);
            MG_CHECK(form.keys == 1);
        }
        if (!abandon) {
            bool caught = false;
            try { L3GRun(); }
            catch (const std::runtime_error &error) {
                caught = true;
                MG_CHECK(std::string(error.what()) == "original callback diagnostic");
            }
            MG_CHECK(caught && FLAG(g_Flags, GFLAG_EXITLOOP));
        }
        MG_CHECK(form.draws == 0 && form.keys == 1);
        MG_CHECK(form.ticks == (during_tick ? 1 : 0));
        // Consuming the exception must not reopen input on the partial session.
        SendMessageW(window.value, WM_KEYDOWN, 'S', 0);
        MG_CHECK(form.keys == 1);
        FormChange(nullptr);
    }
    L3GDeinit();
    MG_CHECK(!g_D3DD && !g_Wnd);
    g_D3DD = device.get();
    g_Wnd = window.value;
    g_Flags = GFLAG_APPACTIVE;
    {
        TransportForm next(window.value);
        FormChange(&next);
        window.key('S');
        MG_CHECK(PostMessageW(window.value, WM_CLOSE, 0, 0));
        MG_CHECK(L3GRun() == 1);
        MG_CHECK(next.keys == 1 && next.ticks == 0 && next.draws == 0);
        FormChange(nullptr);
    }
    L3GDeinit();
    g_D3DD = device.get();
}
void callback_before_loop() { callback_transport(false); }
void callback_during_tick() { callback_transport(true); }
void callback_abandoned() { callback_transport(false, true); }

constexpr tests::Case cases[] = {
    {"game.restart.game_owned_cleanup", owned_cleanup},
    {"game.restart.missing_file_cleanup", missing},
    {"game.restart.missing_file_mouse_cleanup", missing_mouse},
    {"game.restart.callback_before_loop", callback_before_loop},
    {"game.restart.callback_during_tick", callback_during_tick},
    {"game.restart.callback_abandoned", callback_abandoned},
    {"game.restart.corrupt_file_cleanup", corrupt},
    {"game.restart.malformed_storage_cleanup", malformed},
    {"game.restart.empty_storage", empty_storage},
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
