// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_material.hpp"
#include "battle_flyer.hpp"
#include "MatrixFlyer.hpp"
#include "battle_mesh.hpp"
#include "MatrixObject.hpp"
#include <array>
#include <bit>
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceButton.h"
#include "Interface/MatrixHint.hpp"
#include "WindowDisplay.hpp"
#include <cstring>
#include "Interface/CIFaceElement.h"
#include "input.hpp"
#include "cheats.hpp"
#include "SoundBridge.hpp"
#include "loop_script.hpp"
#include "audio_fixture.hpp"
#include <climits>
#include <cmath>
#include <float.h>
#include <string>
#include <sstream>

namespace {
using namespace tests::battle;
CMatrixSideUnit &side() { return *g_MatrixMap->GetPlayerSide(); }
void press(CFormMatrixGame &form, uint8_t key) { form.Keyboard(true, key); form.Keyboard(false, key); }
void advance_to(Session &session, int time) {
    MG_CHECK(time >= session.time());
    while (session.time() < time) {
        MG_CHECK(!g_MatrixMap->IsPaused());
        session.advance();
    }
    MG_CHECK(session.time() == time);
}
struct DialogData {
    int previous_exit = g_ExitState;
    DialogData() {
        MG_CHECK(g_IFaceList->m_First == nullptr);
        auto *hints = HNew(g_MatrixHeap) CInterface;
        g_IFaceList->m_Hints = hints;
        g_IFaceList->m_First = g_IFaceList->m_Last = hints;
        g_MatrixData->BlockGetAdd(PAR_TEMPLATES);
        g_ExitState = 0;
    }
    ~DialogData() { g_ExitState = previous_exit; }
};
// Real hint construction and callback binding, with generated pixels instead of text/art.
struct CallbackDialogs : DialogData {
    CBitmap bitmap, reset_bitmap, surrender_bitmap;
    CallbackDialogs() {
        MG_CHECK(!Display::enabled()); // Native standalone display/map-selection continuation is a separate profile.
        bitmap.CreateRGBA(2, 2);
        std::memset(bitmap.Data(), 255, bitmap.Pitch() * bitmap.SizeY());
        CMatrixHint::SetRuntimeBitmap(L"outcome-callback-pixel", &bitmap);
        reset_bitmap.CreateRGBA(3, 3);
        surrender_bitmap.CreateRGBA(4, 4);
        std::memset(reset_bitmap.Data(), 255, reset_bitmap.Pitch() * reset_bitmap.SizeY());
        std::memset(surrender_bitmap.Data(), 255, surrender_bitmap.Pitch() * surrender_bitmap.SizeY());
        CMatrixHint::SetRuntimeBitmap(L"outcome-callback-reset", &reset_bitmap);
        CMatrixHint::SetRuntimeBitmap(L"outcome-callback-surrender", &surrender_bitmap);
        g_MatrixData->BlockGetAdd(PAR_REPLACE);
        g_MatrixData->BlockGetAdd(L"Side")->ParSetAdd(L"2", L"Enemy");
        for (const auto *name : {IF_HINTS_OK, IF_HINTS_CANCEL, IF_HINTS_CANCEL_MENU,
             IF_HINTS_CONTINUE, IF_HINTS_SURRENDER, IF_HINTS_RESET, IF_HINTS_EXIT, IF_HINTS_HELP}) {
            auto *button = HNew(g_MatrixHeap) CIFaceButton;
            button->m_Type = IFACE_PUSH_BUTTON;
            button->m_strName = name;
            MG_CHECK(g_IFaceList->m_Hints->AddElement(button));
        }
        for (const auto *name : {TEMPLATE_DIALOG_WIN, TEMPLATE_DIALOG_LOOSE, TEMPLATE_DIALOG_STATISTICS,
             TEMPLATE_DIALOG_MENU, TEMPLATE_DIALOG_EXIT, TEMPLATE_DIALOG_RESET, TEMPLATE_DIALOG_SURRENDER}) {
            const auto *image = wcscmp(name, TEMPLATE_DIALOG_RESET) == 0 ? L"outcome-callback-reset" :
                wcscmp(name, TEMPLATE_DIALOG_SURRENDER) == 0 ? L"outcome-callback-surrender" : L"outcome-callback-pixel";
            std::wstring markup = std::wstring(L"-1|_BITMAP:") + image + L":";
            const int copies = wcscmp(name, TEMPLATE_DIALOG_MENU) == 0 ? 6 : 2;
            for (int i = 0; i < copies; ++i) markup += L"|_MOD:COPY|_BITMAP:outcome-callback-pixel:";
            g_MatrixData->BlockGet(PAR_TEMPLATES)->ParSetAdd(name, markup);
        }
    }
    ~CallbackDialogs() {
        g_MatrixMap->LeaveDialogMode();
        CMatrixHint::SetRuntimeBitmap(L"outcome-callback-pixel", nullptr);
        CMatrixHint::SetRuntimeBitmap(L"outcome-callback-reset", nullptr);
        CMatrixHint::SetRuntimeBitmap(L"outcome-callback-surrender", nullptr);
    }
};
struct CaptureData {
    tests::EnginePtr<CMatrixBuilding> building = tests::make_engine_object<CMatrixBuilding>();
    CMatrixRobotAI *robot = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    CaptureData(int owner = 0) {
        side().m_Color = 0x0000ff00;
        g_MatrixMap->GetSideById(2)->m_Color = 0x00ff0000;
        building->m_Pos = D3DXVECTOR2(100, 200);
        building->m_Kind = BUILDING_TITAN;
        building->m_State = BASE_CLOSED;
        building->InitMaxHitpoint(1000);
        building->m_Side = owner;
        building->m_TrueColor.m_Color = owner ? 0xff000000 | g_MatrixMap->GetSideColor(owner) : 0;
        building->m_TrueColor.m_ColoredCnt = owner ? MAX_ZAHVAT_POINTS : 0;
        // This component fixture supplies Capture calls explicitly. Disable the
        // separate periodic acquisition scanner while testing rollback/income.
        building->m_CaptureSeekRobotNextTime = INT_MAX;
        building->SetCapturedBy(robot);
        g_Config.m_Timings[RESOURCE_TITAN] = 1000;
    }
    ~CaptureData() { building->ResetCaptured(); }
    ECaptureStatus capture() { return building->Capture(robot); }
};
void neutral_capture() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=neutral_capture" << '\n';
        CaptureData fixture;
        auto &building = *fixture.building;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 0 && building.GetSide() == 0);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 1);
        advance_to(session, 500 / step * step);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 1);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 2);
        while (session.time() < 6500 / step * step) { session.advance(); fixture.capture(); }
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 13 && building.GetSide() == 0);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 14 && building.GetSide() == 0);
        advance_to(session, 7000 / step * step);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_DONE);
        MG_CHECK(building.GetSide() == PLAYER_SIDE);
        MG_CHECK(building.m_TrueColor.m_Color == 0xff00ff00);
        const int player_before = side().GetResourcesAmount(TITAN);
        const int enemy_before = g_MatrixMap->GetSideById(2)->GetResourcesAmount(TITAN);
        building.LogicTakt(999);
        MG_CHECK(side().GetResourcesAmount(TITAN) == player_before);
        building.LogicTakt(1);
        MG_CHECK(side().GetResourcesAmount(TITAN) == player_before + RESOURCES_INCOME);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetResourcesAmount(TITAN) == enemy_before);
    }
}
void enemy_capture() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=enemy_capture" << '\n';
        CaptureData fixture(2);
        auto &building = *fixture.building;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        while (session.time() < 9750 / step * step) { session.advance(); fixture.capture(); }
        MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_ColoredCnt == 1);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_ColoredCnt == 0);
        advance_to(session, 10500 / step * step);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.GetSide() == 0 && building.m_TrueColor.m_Color == 0);
        const int enemy = g_MatrixMap->GetSideById(2)->GetResourcesAmount(TITAN);
        building.LogicTakt(1000);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetResourcesAmount(TITAN) == enemy);
        ECaptureStatus result = CAPTURE_BUSY;
        for (int i = 0; i < 1000 && result != CAPTURE_DONE; ++i) {
            session.advance(); result = fixture.capture();
        }
        MG_CHECK(result == CAPTURE_DONE);
        MG_CHECK(building.GetSide() == PLAYER_SIDE && building.m_TrueColor.m_ColoredCnt == 14);
    }
}
void capture_rejection() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=capture_rejection" << '\n';
        CaptureData fixture;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        auto *farther = side().GetCurGroup()->GetObjectByN(1)->AsRobot();
        MG_CHECK(fixture.building->Capture(farther) == CAPTURE_TOOFAR);
        MG_CHECK(fixture.building->m_TrueColor.m_ColoredCnt == 0);
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(fixture.building->m_TrueColor.m_ColoredCnt == 1);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY); // A second caller cannot advance the same boundary.
        MG_CHECK(fixture.building->m_TrueColor.m_ColoredCnt == 1);
    }
}
void competing_capturers() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=competing_capturers" << '\n';
        CaptureData fixture;
        fixture.building->m_Pos = D3DXVECTOR2(100, 235);
        auto *closer = side().GetCurGroup()->GetObjectByN(1)->AsRobot();
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_TOOFAR); // Both are in radius; the nearer robot wins.
        MG_CHECK(fixture.building->m_TrueColor.m_ColoredCnt == 0);
        MG_CHECK(fixture.building->Capture(closer) == CAPTURE_INPROGRESS);
        MG_CHECK(fixture.building->m_TrueColor.m_ColoredCnt == 1);
        MG_CHECK(fixture.building->GetSide() == 0);
    }
}
void competing_color() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=competing_color" << '\n';
        CaptureData fixture;
        auto &building = *fixture.building;
        // A neutral building carries two points painted by the other side.
        building.m_TrueColor.m_Color = 0xffff0000;
        building.m_TrueColor.m_ColoredCnt = 2;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 1 && building.m_TrueColor.m_Color == 0xffff0000);
        advance_to(session, 750 / step * step);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 0 && building.m_TrueColor.m_Color == 0xffff0000);
        advance_to(session, 1500 / step * step);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 0 && building.m_TrueColor.m_Color == 0);
        advance_to(session, 2250 / step * step);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        // The erase clock still gates entry; painting then catches up two points.
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 2 && building.m_TrueColor.m_Color == 0xff00ff00);
        MG_CHECK(building.GetSide() == 0);
    }
}
void protected_base_profile(random::Mode mode) {
    Session session({mode});
    CaptureData fixture(2);
    auto &building = *fixture.building;
    building.ResetCaptured();
    building.m_Kind = BUILDING_BASE;
    building.m_Pos = D3DXVECTOR2(180, 400);
    building.m_TurretsHave = 1;
    building.AddLT();
    auto cannon = tests::make_engine_object<CMatrixCannon>();
    cannon->SetSide(2);
    cannon->m_ParentBuilding = &building;
    cannon->m_Pos = D3DXVECTOR2(200, 400);
    cannon->AddLT();
    fixture.robot->GetEnv()->AddToList(cannon.get());
    g_MatrixMap->m_TraceStopObj = &building;
    side().OnRButtonDown(CPoint(0, 0));
    const auto &group = side().m_PlayerGroup[fixture.robot->GetGroupLogic()];
    MG_CHECK(group.Order() == mpo_Capture && group.m_Obj == &building);
    MG_CHECK(group.IsWar());
    MG_CHECK(!fixture.robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_ColoredCnt == 14);
    MG_CHECK(building.CanBeCaptured()); // Intent remains capture; execution diverts to defence first.
    cannon.reset(); // Production turret removal updates the parent's protection count.
    MG_CHECK(building.m_TurretsHave == 0);
    side().OnRButtonDown(CPoint(0, 0));
    int assigned = 0;
    for (int index = 0; index < 2; ++index) {
        auto *robot = side().GetCurGroup()->GetObjectByN(index)->AsRobot();
        if (robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY)) ++assigned;
    }
    MG_CHECK(assigned == 1);
    CFormMatrixGame form;
    press(form, 'S');
    for (int index = 0; index < 2; ++index)
        MG_CHECK(!side().GetCurGroup()->GetObjectByN(index)->AsRobot()->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_ColoredCnt == 14);
}
void protected_base_command() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) protected_base_profile(mode);
}
void capture_rollback() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=capture_rollback" << '\n';
        CaptureData fixture;
        auto &building = *fixture.building;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 1);
        // Stop invoking Capture: only production timeout/rollback logic runs next.
        building.LogicTakt(1249);
        MG_CHECK(building.m_InCaptureTime == 1 && building.m_TrueColor.m_ColoredCnt == 1);
        building.LogicTakt(1);
        MG_CHECK(building.m_InCaptureTime == 0 && building.m_TrueColor.m_ColoredCnt == 1);
        building.LogicTakt(0); // Strict time comparison: equality is not a rollback step.
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 1);
        session.advance();
        building.LogicTakt(step);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 0 && building.GetSide() == 0);
    }
}
void outcome(bool player_loses, bool full_auto, bool simultaneous = false, int step = 10,
             random::Mode mode = random::Mode::ParkMiller) {
    Session session({mode, 79, step});
    DialogData dialog;
    if (full_auto) SETFLAG(g_MatrixMap->m_Flags, MMFLAG_FULLAUTO);
    MG_CHECK(session.remove(player_loses ? 1 : 3));
    MG_CHECK(session.remove(player_loses ? 2 : 4));
    if (simultaneous) { MG_CHECK(session.remove(3)); MG_CHECK(session.remove(4)); }
    session.advance();
    MG_CHECK(!g_MatrixMap->IsPaused());
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !player_loses);
    MG_CHECK(side().GetStatus() == (player_loses ? SS_NONE : SS_ACTIVE));
    const int frozen_loss_time = side().GetStatValue(STAT_TIME);
    advance_to(session, step == 10 ? 2010 : 2016);
    MG_CHECK(!(g_Flags & GFLAG_EXITLOOP));
    MG_CHECK(!g_MatrixMap->IsPaused());
    const DWORD wall_before = timeGetTime();
    session.advance();
    const DWORD wall_after = timeGetTime();
    if (full_auto) {
        MG_CHECK(g_ExitState == 1 && FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
        MG_CHECK(!g_MatrixMap->IsPaused());
    } else {
        MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (player_loses ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        if (player_loses) MG_CHECK(side().GetStatValue(STAT_TIME) == frozen_loss_time);
        else {
            // Statistics use the native clock, not the fixed battle schedule.
            // Bound the negated elapsed value by the clock readings around Takt.
            const DWORD start = g_MatrixMap->GetStartTime();
            // Match the production legacy wrap arithmetic, including its off-by-one.
            const auto elapsed = [start](DWORD now) {
                return start <= now ? now - start : 0xffffffffu - start + now;
            };
            const DWORD measured = 0u - static_cast<DWORD>(side().GetStatValue(STAT_TIME));
            MG_CHECK(measured - elapsed(wall_before) <= elapsed(wall_after) - elapsed(wall_before));
        }
        g_MatrixMap->Pause(false);
        MG_CHECK(g_MatrixMap->IsPaused()); // A result dialog owns pause until it closes.
        g_MatrixMap->LeaveDialogMode();
        MG_CHECK(!g_MatrixMap->IsPaused());
    }
}
void outcome_profiles(bool loss, bool automatic, bool simultaneous = false) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) outcome(loss, automatic, simultaneous, step, mode);
}
void victory() { outcome_profiles(false, false); }
void defeat() { outcome_profiles(true, false); }
void simultaneous_loss() { outcome_profiles(true, false, true); }
void automatic_results() { outcome_profiles(false, true); outcome_profiles(true, true); }
void pause_resume() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        CFormMatrixGame form;
        session.ground_command(500, 300);
        for (int i = 0; i < 30; ++i) session.advance();
        press(form, VK_PAUSE);
        MG_CHECK(g_MatrixMap->IsPaused());
        const auto before = session.robot(1);
        const int time = session.time();
        g_MatrixMap->SetMaintenanceTime(1500);
        for (int i = 0; i < 30; ++i) session.advance();
        MG_CHECK(session.time() == time);
        MG_CHECK(session.robot(1).x == before.x && session.robot(1).y == before.y);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1500);
        press(form, VK_PAUSE);
        MG_CHECK(!g_MatrixMap->IsPaused());
        session.advance();
        MG_CHECK(session.time() == time + step);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1500 - step);
        for (int i = 0; i < 1470; ++i) session.advance();
        MG_CHECK(session.robot(1).in_place && session.robot(1).x > 400);
    }
}
void special_survival_flags() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (DWORD flag : {MMFLAG_TERRON_DEAD, MMFLAG_SPECIAL_BROKEN}) {
        Session session({mode, 79, step});
        DialogData dialog;
        SETFLAG(g_MatrixMap->m_Flags, flag);
        MG_CHECK(session.remove(1) && session.remove(2));
        advance_to(session, step == 10 ? 2020 : 2032);
        MG_CHECK(side().GetStatus() == SS_ACTIVE);
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        MG_CHECK(!g_MatrixMap->IsPaused());
        MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
    }
}
// SetMusicVolume and RestoreMusicVolume are the calls made by the Terron death and the MUSIC console
// command. Without a host they fade the standalone music, one thousandth of full level per millisecond.
void music_fade_profile(random::Mode mode, int step) {
    Session session({mode, 79, step});
    std::cout << session.metadata("synthetic-music-fade-v2") << '\n';
    MG_CHECK(SoundBridge::available() && !SoundBridge::music_available());
    g_MatrixMap->SetMusicVolume(0);  // Nothing to fade without music, as before.
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_MUSIC_VOL_CHANGING));
    SoundBridge::install_music({L"synthetic"}, [](const std::wstring &) { return std::vector<uint8_t>{1}; },
        [](std::vector<uint8_t>) -> std::unique_ptr<Audio::Decoder> {
            return std::make_unique<audio_fixture::FakeDecoder>(audio_fixture::TrackSpec{1, 8000, 100000, 0});
        });
    SoundBridge::update_music();
    MG_CHECK(SoundBridge::music_available() && SoundBridge::music_level() == 1.0f);
    const auto fade = [&](float target) {
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_MUSIC_VOL_CHANGING));
        const int start = session.time();
        while (FLAG(g_MatrixMap->m_Flags, MMFLAG_MUSIC_VOL_CHANGING) && session.time() < start + 2000) {
            const float before = SoundBridge::music_level();
            session.advance();
            MG_CHECK(std::fabs(SoundBridge::music_level() - before) <= step * 0.001f + 0.0001f);
            MG_CHECK(target == 0.0f ? SoundBridge::music_level() <= before : SoundBridge::music_level() >= before);
        }
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_MUSIC_VOL_CHANGING) && SoundBridge::music_level() == target);
        const int duration = session.time() - start;
        std::cout << "fade target=" << target << " duration=" << duration << '\n';
        MG_CHECK(duration == (step == 10 ? 1010 : 1024));
    };
    g_MatrixMap->SetMusicVolume(0);
    fade(0.0f);
    g_MatrixMap->RestoreMusicVolume();
    fade(1.0f);
}
void music_fade() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
        music_fade_profile(mode, step);
}
// The game form refills music before anything else in a frame, so a lost device that skips the rest
// of the frame does not starve it.
void music_form_refill_profile(random::Mode mode, int step) {
    Session session({mode, 79, step});
    std::cout << session.metadata("synthetic-music-refill-v2") << '\n';
    auto audio = std::make_shared<audio_fixture::DeviceState>();
    SoundBridge::install(std::make_unique<Audio::Service>(std::make_unique<audio_fixture::FakeDevice>(audio),
        Audio::Mapping{{L"Sound.Synthetic", L"synthetic.wav"}},
        [](const std::wstring &) { return audio_fixture::wave(); },
        [](const std::wstring &, const std::string &error) { throw std::runtime_error(error); }));
    SoundBridge::install_music({L"synthetic"}, [](const std::wstring &) { return std::vector<uint8_t>{1}; },
        [](std::vector<uint8_t>) -> std::unique_ptr<Audio::Decoder> {
            return std::make_unique<audio_fixture::FakeDecoder>(audio_fixture::TrackSpec{1, 8000, 100000, 0});
        });
    CFormMatrixGame form;
    MG_CHECK(audio->streams.empty());  // Nothing plays before the first frame.
    session.device_lost(true);
    const int time = session.time();
    const auto draws = random::draw_count();
    form.Takt(step);
    MG_CHECK(session.time() == time);  // The lost device skipped the map.
    MG_CHECK(random::draw_count() == draws);
    MG_CHECK(audio->streams.size() == 1 && audio->streams[0]->buffers.size() == 4 && audio->streams[0]->plays == 1);
    // Continued device loss must not starve the already playing stream either.
    audio->streams[0]->queued = 0;
    form.Takt(step);
    MG_CHECK(session.time() == time && random::draw_count() == draws);
    MG_CHECK(audio->streams.size() == 1 && audio->streams[0]->buffers.size() == 8);
    session.device_lost(false);
    // Resources count as present, so the recovered device needs nothing recreated.
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_VIDEO_RESOURCES_READY);
    audio->streams[0]->queued = 0;
    form.Takt(step);
    MG_CHECK(session.time() == time + step);
    MG_CHECK(audio->streams.size() == 1 && audio->streams[0]->buffers.size() == 12);
}
void music_form_refill() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
        music_form_refill_profile(mode, step);
}
// The game form checks the audio device first in a frame; a directly installed service is released
// after a device error, even when a lost device skips the rest of the frame.
void form_audio_device_error_profile(random::Mode mode, int step) {
    Session session({mode, 79, step});
    std::cout << session.metadata("synthetic-audio-device-error-v2") << '\n';
    auto audio = std::make_shared<audio_fixture::DeviceState>();
    SoundBridge::install(std::make_unique<Audio::Service>(std::make_unique<audio_fixture::FakeDevice>(audio),
        Audio::Mapping{{L"Sound.Synthetic", L"synthetic.wav"}},
        [](const std::wstring &) { return audio_fixture::wave(); },
        [](const std::wstring &, const std::string &error) { throw std::runtime_error(error); }));
    CFormMatrixGame form;
    session.device_lost(true);
    const int time = session.time();
    const auto draws = random::draw_count();
    form.Takt(step);
    MG_CHECK(SoundBridge::available() && !audio->destroyed);
    MG_CHECK(session.time() == time && random::draw_count() == draws);
    audio->critical_error = "Synthetic critical error";
    form.Takt(step);
    MG_CHECK(!SoundBridge::available() && audio->destroyed);
    MG_CHECK(session.time() == time && random::draw_count() == draws);
    form.Takt(step);
    MG_CHECK(!SoundBridge::available() && session.time() == time && random::draw_count() == draws);
    session.device_lost(false);
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_VIDEO_RESOURCES_READY);
    form.Takt(step);
    MG_CHECK(!SoundBridge::available() && session.time() == time + step);
}
void form_audio_device_error() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
        form_audio_device_error_profile(mode, step);
}
void full_auto_escape() {
    Session session;
    DialogData dialog;
    CFormMatrixGame form;
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_FULLAUTO);
    press(form, VK_ESCAPE);
    MG_CHECK(g_ExitState == 1 && FLAG(g_Flags, GFLAG_EXITLOOP));
    MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
    MG_CHECK(!g_MatrixMap->IsPaused());
}
void legacy_loss_cheat() {
    Session session;
    DialogData dialog;
    side().SetStatValue(STAT_TIME, 1234);
    bool handled = false;
    for (char key : std::string("IAMLOOSER")) handled = Cheats::processInput(key);
    MG_CHECK(handled);
    MG_CHECK(g_ExitState == 3);
    MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN);
    MG_CHECK(g_MatrixMap->IsPaused());
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(side().GetStatValue(STAT_TIME) == 1234);
    MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
}
void statistics_formatting() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (DWORD flag : {MMFLAG_STAT_DIALOG, MMFLAG_STAT_DIALOG_D}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("synthetic-statistics-formatting-v2") << " flag=" << flag << '\n';
        DialogData dialog;
        auto *replacements = g_MatrixData->BlockGetAdd(PAR_REPLACE);
        g_MatrixData->BlockGetAdd(L"Side")->ParSetAdd(L"2", L"Red");
        auto &enemy = *g_MatrixMap->GetSideById(2);
        side().SetStatValue(STAT_TIME, -3661000);
        enemy.SetStatValue(STAT_TIME, 125000);
        const EStat counters[] = {STAT_ROBOT_BUILD, STAT_ROBOT_KILL, STAT_TURRET_BUILD,
                                      STAT_TURRET_KILL, STAT_BUILDING_KILL};
        for (int i = 0; i < 5; ++i) {
            side().SetStatValue(counters[i], 11 + i);
            enemy.SetStatValue(counters[i], 21 + i);
        }
        // Start at the statistics-request boundary; no confirmation callback is substituted.
        SETFLAG(g_MatrixMap->m_Flags, flag);
        session.advance();
        MG_CHECK(replacements->ParGet(L"_p_c6") == L"<color=0,255,0>01:01:01</color>");
        MG_CHECK(replacements->ParGet(L"_r_c6") == L"00:02:05");
        for (int i = 1; i <= 5; ++i) {
            MG_CHECK(replacements->ParGet(L"_p_c" + std::to_wstring(i)) == std::to_wstring(10 + i));
            MG_CHECK(replacements->ParGet(L"_r_c" + std::to_wstring(i)) == std::to_wstring(20 + i));
        }
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (flag == MMFLAG_STAT_DIALOG ? TEMPLATE_DIALOG_STATISTICS : TEMPLATE_DIALOG_STATISTICS_D));
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG | MMFLAG_STAT_DIALOG_D));
        MG_CHECK(g_MatrixMap->IsPaused() && session.time() == 0);
        MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
        g_MatrixMap->LeaveDialogMode();
        // An unused side clears time only: previously formatted counters remain in replacements.
        side().SetStatValue(STAT_TIME, 0);
        enemy.SetStatValue(STAT_TIME, -999);
        SETFLAG(g_MatrixMap->m_Flags, flag);
        session.advance();
        MG_CHECK(replacements->ParGet(L"_p_c6").empty());
        MG_CHECK(replacements->ParGet(L"_p_c1") == L"11");
        MG_CHECK(replacements->ParGet(L"_r_c6") == L"00:00:00");
        MG_CHECK(session.time() == 0);
    }
}
#ifdef _TRACE
void trace_result_callbacks() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (uint8_t shift : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT}) {
        const bool native_key = shift == VK_SHIFT;
        const bool loss = shift == VK_LSHIFT;
        Session session({mode, 85, step, 1000, false});
        std::cout << session.metadata("synthetic-trace-result-v2") << " trace_build=1 shift=" << int(shift) << '\n';
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        session.ground_command(500, 300);
        for (int i = 0; i < 25; ++i) session.advance();
        const std::array moving{session.robot(1), session.robot(2)};
        MG_CHECK(moving[0].order == mpo_MoveTo && moving[0].x > 100);
        side().SetStatValue(STAT_TIME, 1234);
        g_MatrixMap->GetSideById(2)->SetStatValue(STAT_TIME, 5678);
        const auto draws = random::draw_count();
        const int time = session.time();
        const auto check_state = [&] {
            MG_CHECK(session.time() == time && random::draw_count() == draws);
            MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
            MG_CHECK(side().GetStatus() == SS_ACTIVE);
            MG_CHECK(side().GetStatValue(STAT_TIME) == 1234);
            MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_TIME) == 5678);
            for (const auto &robot : moving) {
                const auto now = session.robot(robot.id);
                MG_CHECK(now.alive && now.x == robot.x && now.y == robot.y);
                MG_CHECK(now.order == robot.order && now.group == robot.group);
                MG_CHECK(now.destination_x == robot.destination_x && now.destination_y == robot.destination_y);
            }
        };
        // WinProc forwards generic VK_SHIFT. Side-specific codes are synthetic
        // input to the otherwise unreachable diagnostic result branches.
        form.Keyboard(true, shift);
        press(form, VK_ESCAPE);
        form.Keyboard(false, shift);
        MG_CHECK(!Input::isVKeyPressed(shift) && !Input::isVKeyPressed(VK_ESCAPE));
        if (native_key) {
            MG_CHECK(g_ExitState == 0);
            MG_CHECK(g_MatrixMap->m_DialogModeName &&
                std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_MENU);
            MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG | MMFLAG_STAT_DIALOG_D));
            MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
            for (int i = 0; i < 10; ++i) session.advance();
            check_state();
            MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
            press(form, VK_ESCAPE);
            MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
            MG_CHECK(g_MatrixMap->m_DialogModeHints.empty());
            MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
            check_state();
            continue;
        }
        MG_CHECK(g_ExitState == (loss ? 2 : 3));
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) ==
            (loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        for (int i = 0; i < 10; ++i) session.advance();
        check_state();
        press(form, VK_RETURN);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        session.advance();
        MG_CHECK(g_MatrixMap->m_DialogModeName &&
            std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS);
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        check_state();
        press(form, VK_RETURN);
        MG_CHECK(g_ExitState == (loss ? 2 : 3) && FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.empty());
        check_state();
    }
}
#endif
void scheduled_capture() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        CaptureData fixture;
        auto &building = *fixture.building;
        // Keep the fixture's capturer marker to suppress candidate publication;
        // the independent nearest-robot scanner still obtains its own live target.
        building.m_CaptureSeekRobotNextTime = 0;
        int first_paint = 0, completed = 0;
        for (int i = 0; i < 900 && !completed; ++i) {
            session.advance();
            building.LogicTakt(step); // The production acquisition scanner invokes Capture.
            if (!first_paint && building.m_TrueColor.m_ColoredCnt) first_paint = session.time();
            if (building.GetSide() == PLAYER_SIDE) completed = session.time();
        }
        MG_CHECK(first_paint == (step == 10 ? 120 : 128));
        MG_CHECK(completed == (step == 10 ? 7050 : 7072));
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
        MG_CHECK(building.m_TrueColor.m_Color == 0xff00ff00);
    }
}
void acquisition_after_removal() {
    for (bool substitute : {false, true})
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=acquisition_after_removal" << '\n';
        CaptureData fixture;
        auto &building = *fixture.building;
        building.m_CaptureSeekRobotNextTime = 0;
        if (substitute) building.m_Pos = D3DXVECTOR2(100, 235);
        while (session.time() < (step == 10 ? 600 : 608)) { session.advance(); building.LogicTakt(step); }
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 2);
        // Remove the currently nearest robot; the next scan must search the live list again.
        // Preserve that fixture marker on a live object; it is not the scanner's target.
        if (!substitute) building.SetCapturedBy(side().GetCurGroup()->GetObjectByN(1));
        MG_CHECK(session.remove(substitute ? 2 : 1));
        while (session.time() < (step == 10 ? 7500 : 7504)) { session.advance(); building.LogicTakt(step); }
        MG_CHECK(building.GetSide() == (substitute ? PLAYER_SIDE : 0));
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == (substitute ? MAX_ZAHVAT_POINTS : 0));
    }
}
void enemy_capture_rollback() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=enemy_capture_rollback" << '\n';
        CaptureData fixture(2);
        auto &building = *fixture.building;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 13);
        building.LogicTakt(1250);
        MG_CHECK(building.m_InCaptureTime == 0 && building.m_TrueColor.m_ColoredCnt == 13);
        session.advance();
        building.LogicTakt(step);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
        MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_Color == 0xffff0000);
    }
}
void capture_queue_refund() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-component-v2") << " case=capture_queue_refund" << '\n';
        CaptureData fixture(2);
        auto &building = *fixture.building;
        building.m_Kind = BUILDING_BASE;
        auto &enemy = *g_MatrixMap->GetSideById(2);
        const ERes resources[] = {TITAN, ELECTRONICS, ENERGY, PLASMA};
        for (int i = 0; i < 4; ++i) {
            g_Config.m_Price[CHASSIS2_TITAN + i] = 11 + i;
            enemy.SetResourceAmount(resources[i], 100);
            side().SetResourceAmount(resources[i], 200);
        }
        for (int i = 0; i < 2; ++i) {
            auto robot = tests::make_engine_object<CMatrixRobotAI>();
            robot->m_Side = 2;
            robot->UnitInsert(0, MRT_CHASSIS, RUK_CHASSIS_WHEEL);
            building.m_BS.AddItem(robot.get());
            robot.release(); // The production queue owns deletion and refund.
        }
        MG_CHECK(building.m_BS.GetItemsCnt() == 2);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        while (session.time() < 10500 / step * step) { session.advance(); fixture.capture(); }
        MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_ColoredCnt == 0);
        MG_CHECK(building.m_BS.GetItemsCnt() == 2);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.GetSide() == 0 && building.m_BS.GetItemsCnt() == 0);
        MG_CHECK(building.m_BS.GetTopItem() == nullptr);
        for (int i = 0; i < 4; ++i) {
            MG_CHECK(enemy.GetResourcesAmount(resources[i]) == 100 + 2 * (11 + i));
            MG_CHECK(side().GetResourcesAmount(resources[i]) == 200);
        }
        for (int i = 0; i < 1000 && building.GetSide() != PLAYER_SIDE; ++i) {
            session.advance(); fixture.capture();
        }
        MG_CHECK(building.GetSide() == PLAYER_SIDE);
        for (int i = 0; i < 4; ++i) {
            MG_CHECK(enemy.GetResourcesAmount(resources[i]) == 100 + 2 * (11 + i));
            MG_CHECK(side().GetResourcesAmount(resources[i]) == 200);
        }
    }
}
void capture_income_continuity() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-income-continuity-v1") << '\n';
        CaptureData fixture(2);
        auto &building = *fixture.building;
        auto &enemy = *g_MatrixMap->GetSideById(2);
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            enemy.SetResourceAmount(static_cast<ERes>(r), 100);
            side().SetResourceAmount(static_cast<ERes>(r), 200);
        }
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        const int neutral_at = step == 10 ? 10510 : 10512;
        const int captured_at = step == 10 ? 18020 : 18016;
        const int income_at = step == 10 ? 18510 : 18592;
        int neutral = 0, captured = 0, first_player_income = 0;
        int previous_enemy = 100, previous_player = 200;
        while (session.time() < 20000) {
            const int previous_owner = building.GetSide();
            if (session.time() == neutral_at - step) MG_CHECK(previous_owner == 2);
            if (session.time() == captured_at - step) MG_CHECK(previous_owner == 0);
            if (session.time() == income_at - step) MG_CHECK(side().GetResourcesAmount(TITAN) == 200);
            session.advance();
            if (!captured) fixture.capture();
            if (!neutral && building.GetSide() == 0) neutral = session.time();
            if (!captured && building.GetSide() == PLAYER_SIDE) captured = session.time();
            building.LogicTakt(step);
            const int enemy_now = enemy.GetResourcesAmount(TITAN);
            const int player_now = side().GetResourcesAmount(TITAN);
            if (building.GetSide() == 2) MG_CHECK(player_now == 200);
            if (building.GetSide() == 0) MG_CHECK(enemy_now == previous_enemy && player_now == 200);
            if (building.GetSide() == PLAYER_SIDE) MG_CHECK(enemy_now == previous_enemy);
            if (!first_player_income && player_now != 200) first_player_income = session.time();
            if (previous_owner != building.GetSide())
                MG_CHECK(enemy_now == previous_enemy && player_now == previous_player);
            for (int r = 1; r < MAX_RESOURCES; ++r) {
                MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(r)) == 100);
                MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(r)) == 200);
            }
            previous_enemy = enemy_now;
            previous_player = player_now;
        }
        std::cout << "income neutral=" << neutral << " captured=" << captured
                  << " first-player=" << first_player_income << " enemy=" << previous_enemy
                  << " player=" << previous_player << '\n';
        MG_CHECK(neutral == neutral_at && captured == captured_at && first_player_income == income_at);
        MG_CHECK(previous_enemy == 200 && previous_player == 220);
    }
}
void capture_turret_refund() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("capture-turret-refund-v1") << '\n';
        CaptureData fixture(2);
        auto &building = *fixture.building;
        auto &enemy = *g_MatrixMap->GetSideById(2);
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            g_Config.m_CannonsProps[0].m_Resources[r] = 17 + r;
            enemy.SetResourceAmount(static_cast<ERes>(r), 100);
            side().SetResourceAmount(static_cast<ERes>(r), 200);
        }
        for (int i = 0; i < 2; ++i) {
            auto *turret = g_MatrixMap->StaticAdd<CMatrixCannon>();
            turret->m_Num = 1;
            turret->SetSide(2);
            turret->m_CurrState = CANNON_UNDER_CONSTRUCTION;
            turret->InitMaxHitpoint(1000);
            turret->SetHitPoint(0);
            turret->m_ParentBuilding = &building;
            ++building.m_TurretsHave;
            building.m_BS.AddItem(turret);
        }
        MG_CHECK(building.m_BS.GetItemsCnt() == 2 && building.m_BS.GetRobotsCnt() == 0);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        while (session.time() < 10500 / step * step) {
            session.advance();
            fixture.capture();
            MG_CHECK(building.m_BS.GetItemsCnt() == 2 && building.m_TurretsHave == 2);
            for (int r = 0; r < MAX_RESOURCES; ++r)
                MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(r)) == 100);
        }
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.GetSide() == 0 && building.m_TrueColor.m_Color == 0);
        MG_CHECK(building.m_BS.GetItemsCnt() == 0 && building.m_BS.GetTopItem() == nullptr);
        MG_CHECK(building.m_TurretsHave == 0);
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(r)) == 100 + 2 * (17 + r));
            MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(r)) == 200);
        }
        while (building.GetSide() != PLAYER_SIDE) { session.advance(); fixture.capture(); }
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
        for (int r = 0; r < MAX_RESOURCES; ++r)
            MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(r)) == 100 + 2 * (17 + r));
    }
}
void paused_orders() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step});
        std::cout << session.metadata("synthetic-paused-orders-v2") << '\n';
        CFormMatrixGame form;
        press(form, VK_PAUSE);
        session.ground_command(500, 300);
        const auto ordered = session.robot(1);
        MG_CHECK(ordered.order == mpo_MoveTo && ordered.destination_x == 48 && ordered.destination_y == 28);
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(session.time() == 0 && session.robot(1).x == ordered.x && session.robot(1).y == ordered.y);
        press(form, 'S');
        MG_CHECK(session.robot(1).order == mpo_Stop);
        session.ground_command(120, 500);
        MG_CHECK(session.robot(1).destination_x == 10 && session.robot(1).destination_y == 48);
        press(form, VK_PAUSE);
        advance_to(session, 16000);
        MG_CHECK(session.robot(1).in_place && session.robot(1).y > 400 && session.robot(1).x < 200);
    }
}
void pending_result_pause() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (bool loss : {false, true}) {
        Session session({mode, 79, step});
        DialogData dialog;
        CFormMatrixGame form;
        MG_CHECK(session.remove(loss ? 1 : 3) && session.remove(loss ? 2 : 4));
        const int pending_time = step == 10 ? 2010 : 2016;
        advance_to(session, pending_time);
        press(form, VK_PAUSE);
        for (int i = 0; i < 100; ++i) session.advance();
        MG_CHECK(session.time() == pending_time && g_MatrixMap->m_DialogModeName == nullptr);
        press(form, VK_PAUSE);
        session.advance();
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        press(form, VK_ESCAPE);
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        std::cout << session.metadata("synthetic-pending-result-pause-v2") << " loss=" << loss << '\n';
    }
}
void pending_result_late_elimination() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (bool loss_first : {false, true}) {
        Session session({mode, 79, step});
        DialogData dialog;
        MG_CHECK(session.remove(loss_first ? 1 : 3) && session.remove(loss_first ? 2 : 4));
        session.advance();
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !loss_first);
        // The opposite army disappears after the first status decision but before the dialog.
        MG_CHECK(session.remove(loss_first ? 3 : 1) && session.remove(loss_first ? 4 : 2));
        advance_to(session, step == 10 ? 2010 : 2016);
        MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
        session.advance();
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (loss_first ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !loss_first);
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        std::cout << session.metadata("synthetic-late-elimination-v2") << " loss_first=" << loss_first << '\n';
    }
}
CMatrixBuilding *register_building(int owner, EBuildingType kind, float x, float y) {
    auto *donor = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    auto owned = tests::make_battle_building(*donor->m_Unit[0].m_Graph->VO(), owner, kind, x, y);
    auto *building = owned.get();
    side().m_Color = 0x0000ff00;
    g_MatrixMap->GetSideById(2)->m_Color = 0x00ff0000;
    building->m_TrueColor.m_Color = owner ? 0xff000000 | g_MatrixMap->GetSideColor(owner) : 0;
    building->m_TrueColor.m_ColoredCnt = owner ? MAX_ZAHVAT_POINTS : 0;
    // Income presentation needs artwork; keep its first tick outside these bounded profiles.
    g_Config.m_Timings[RESOURCE_BASE] = 100000;
    g_Config.m_Timings[RESOURCE_TITAN] = 100000;
    g_Config.m_Timings[RESOURCE_ELECTRONICS] = 100000;
    g_Config.m_Timings[RESOURCE_ENERGY] = 100000;
    g_Config.m_Timings[RESOURCE_PLASMA] = 100000;
    g_MatrixMap->AddObject(building, true);
    owned.release();
    // Map loading registers buildings before the buffered starting robots.
    // Match that initial logic-list order; TaktPL capture validation depends on it.
    std::vector<CMatrixRobotAI *> robots;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot()) robots.push_back(object->AsRobot());
    for (auto *robot : robots) { robot->DelLT(); robot->AddLT(); }
    return building;
}
enum class CaptureRoute { Context, Keyboard, Button };
void submit_capture(CMatrixBuilding *building, CaptureRoute route) {
    g_MatrixMap->m_TraceStopObj = building;
    g_MatrixMap->m_TraceStopPos = building->GetGeoCenter();
    if (route == CaptureRoute::Context) {
        side().OnRButtonDown(CPoint(0, 0));
    } else {
        if (route == CaptureRoute::Keyboard) {
            CFormMatrixGame form;
            press(form, 'K');
            MG_CHECK(!Input::isVKeyPressed('K'));
        } else {
            auto button = tests::make_engine_object<CIFaceElement>();
            button->m_strName = IF_ORDER_CAPTURE;
            g_IFaceList->PlayerAction(button.get());
        }
        MG_CHECK((g_IFaceList->m_IfListFlags & (PREORDER_CAPTURE | ORDERING_MODE)) ==
                 (PREORDER_CAPTURE | ORDERING_MODE));
        side().OnLButtonDown(CPoint(0, 0));
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_CAPTURE | ORDERING_MODE));
    }
}
void registered_capture_profile(int owner, CaptureRoute route = CaptureRoute::Context) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step, 3000, false});
        std::cout << session.metadata("capture-route-completion-v1") << " route=" << int(route) << '\n';
        auto *building = register_building(owner, BUILDING_TITAN, 160, 200);
        auto *robot = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
        submit_capture(building, route);
        const auto &group = side().m_PlayerGroup[robot->GetGroupLogic()];
        MG_CHECK(group.Order() == mpo_Capture && group.m_Obj == building);
        int assigned = 0;
        for (int i = 0; i < 2; ++i)
            if (side().GetCurGroup()->GetObjectByN(i)->AsRobot()->FindOrderLikeThat(ROT_CAPTURE_FACTORY)) ++assigned;
        MG_CHECK(assigned == 1);
        int first_paint = 0, neutralized = 0, completed = 0;
        for (int i = 0; i < 2000 && !completed; ++i) {
            session.advance();
            if (!first_paint && building->m_TrueColor.m_ColoredCnt != (owner ? MAX_ZAHVAT_POINTS : 0)) first_paint = session.time();
            if (owner && !neutralized && building->GetSide() == 0) neutralized = session.time();
            if (building->GetSide() == PLAYER_SIDE) completed = session.time();
        }
        std::cout << "registered capture mode=" << int(mode) << " step=" << step
                  << " route=" << int(route) << " owner=" << owner << " progress=" << first_paint << " neutralized=" << neutralized << " completed=" << completed << '\n';
        MG_CHECK(first_paint == (step == 10 ? 630 : 640));
        if (!owner) MG_CHECK(completed == (step == 10 ? 7530 : 7536));
        else {
            MG_CHECK(neutralized == (step == 10 ? 11030 : 11040));
            MG_CHECK(completed == (step == 10 ? 18540 : 18544));
        }
        MG_CHECK(building->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
        MG_CHECK(building->m_TrueColor.m_Color == 0xff00ff00);
        MG_CHECK(!g_MatrixMap->IsPaused() && side().GetStatus() == SS_ACTIVE);
        for (int i = 0; i < 100; ++i) session.advance();
        MG_CHECK(building->GetSide() == PLAYER_SIDE);
        MG_CHECK(!robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    }
}
void registered_capture() { registered_capture_profile(0); }
void registered_enemy_capture() { registered_capture_profile(2); }
void capture_move_reassignment() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step, 6000, false});
        std::cout << session.metadata("capture-move-reassignment-v1") << '\n';
        auto *building = register_building(0, BUILDING_TITAN, 160, 200);
        auto *robot = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
        submit_capture(building, CaptureRoute::Context);
        advance_to(session, step == 10 ? 1000 : 1008);
        MG_CHECK(building->m_Capturer == robot && robot->GetCaptureFactory() == building);
        MG_CHECK(building->m_TrueColor.m_ColoredCnt == 1 && building->GetSide() == 0);
        session.ground_command(120, 500);
        MG_CHECK(robot->GetCaptureFactory() == nullptr && building->m_Capturer == nullptr);
        MG_CHECK(!robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
        MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].Order() == mpo_MoveTo);
        const int cleared_at = step == 10 ? 7810 : 7856;
        const int completed_at = step == 10 ? 35290 : 35248;
        int cleared = 0;
        while (session.time() < 10000) {
            if (session.time() == cleared_at - step) MG_CHECK(building->m_TrueColor.m_ColoredCnt > 0);
            session.advance();
            MG_CHECK(robot->GetCaptureFactory() == nullptr);
            MG_CHECK(!robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
            if (!cleared && building->m_TrueColor.m_ColoredCnt == 0) cleared = session.time();
            MG_CHECK(building->GetSide() == 0);
        }
        std::cout << "move rollback=" << cleared << " paint=" << building->m_TrueColor.m_ColoredCnt << '\n';
        MG_CHECK(cleared == cleared_at && building->m_TrueColor.m_ColoredCnt == 0);
        submit_capture(building, CaptureRoute::Context);
        MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].Order() == mpo_Capture);
        int completed = 0;
        while (session.time() < 50000 && !completed) {
            if (session.time() == completed_at - step) MG_CHECK(building->GetSide() == 0);
            session.advance();
            if (building->GetSide() == PLAYER_SIDE) completed = session.time();
        }
        std::cout << "move recaptured=" << completed << '\n';
        MG_CHECK(completed == completed_at && building->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
        MG_CHECK(building->m_Capturer == nullptr);
        for (int i = 0; i < 100; ++i) session.advance();
        MG_CHECK(session.robot(1).alive && session.robot(2).alive);
        MG_CHECK(!robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    }
}
std::vector<std::string> player_placed_queue_run(random::Mode mode, int step, bool place_turret) {
    tests::BattleCombatAssets assets;
    Session session({mode, 79, step, 3000, false});
    assets.install();
    std::cout << session.metadata("capture-player-placed-queue-v2") << " turret=" << place_turret << '\n';
    auto *base = register_building(PLAYER_SIDE, BUILDING_BASE, 500, 400);
    auto &enemy = *g_MatrixMap->GetSideById(2);
    using HeldCore = std::unique_ptr<SObjectCore, void (*)(SObjectCore *)>;
    const auto release_core = [](SObjectCore *core) { core->Release(); };
    std::array<HeldCore, 2> robot_cores{HeldCore(nullptr, release_core), HeldCore(nullptr, release_core)};
    HeldCore turret_core(nullptr, release_core);
    const auto check_queue = [&] {
        auto *item = base->m_BS.GetTopItem();
        for (const auto &core : robot_cores) {
            MG_CHECK(core->m_Object != nullptr && item == core->m_Object);
            MG_CHECK(item->IsRobot() && item->GetSide() == PLAYER_SIDE);
            item = item->m_NextStackItem;
        }
        if (turret_core) {
            MG_CHECK(turret_core->m_Object != nullptr && item == turret_core->m_Object);
            MG_CHECK(item->IsCannon() && item->GetSide() == PLAYER_SIDE);
            MG_CHECK(item->AsCannon()->m_CurrState == CANNON_UNDER_CONSTRUCTION);
            MG_CHECK(item->AsCannon()->m_ParentBuilding == base);
            item = item->m_NextStackItem;
        }
        MG_CHECK(item == nullptr);
        MG_CHECK(base->m_BS.GetItemsCnt() == (turret_core ? 3 : 2));
        MG_CHECK(base->m_TurretsHave == (turret_core ? 1 : 0));
    };
    std::vector<std::string> trace;
    const auto record = [&] {
        std::string state = session.snapshot() + ":" + std::to_string(base->GetSide()) +
            ":" + std::to_string(base->m_State) + ":" + std::to_string(base->m_BS.GetItemsCnt()) +
            ":" + std::to_string(base->m_TurretsHave) + ":" + std::to_string(base->m_Capturer != nullptr);
        for (const auto &core : robot_cores) state += ":" + std::to_string(core->m_Object != nullptr);
        state += ":" + std::to_string(turret_core && turret_core->m_Object != nullptr);
        for (int r = 0; r < MAX_RESOURCES; ++r)
            state += ":" + std::to_string(side().GetResourcesAmount(static_cast<ERes>(r))) +
                ":" + std::to_string(enemy.GetResourcesAmount(static_cast<ERes>(r)));
        trace.push_back(std::move(state));
    };
    g_Config.m_Timings[UNIT_ROBOT] = 100000;
    g_Config.m_Timings[UNIT_TURRET] = 100000;
    for (auto &cannon : g_Config.m_CannonsProps)
        for (int &cost : cannon.m_Resources) cost = 1000;
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.weapon = WEAPON_CANNON2;
    props.m_Hitpoint = 1000;
    props.m_Strength = 10;
    props.seek_radius = 300;
    props.max_da = 0.1f;
    props.max_top_angle = 1.5f;
    props.max_bottom_angle = -1.5f;
    for (int r = 0; r < MAX_RESOURCES; ++r) {
        g_Config.m_Price[CHASSIS2_TITAN + r] = 11 + r;
        props.m_Resources[r] = 17 + r;
        side().SetResourceAmount(static_cast<ERes>(r), 100);
        enemy.SetResourceAmount(static_cast<ERes>(r), 0);
    }
    auto *model = tests::create_battle_cannon(PLAYER_SIDE, 480, 480);
    g_MatrixMap->StaticDelete(model); // Leave the generated cannon geometry cached for real placement.
    base->m_TurretsMax = BASE_TURRETS;
    base->m_TurretsPlacesCnt = 1;
    base->m_TurretsPlaces[0].m_Coord = CPoint(48, 48);
    base->m_TurretsPlaces[0].m_CannonType = 1;
    base->m_TurretsPlaces[0].m_Angle = 0;
    CPoint places[MAX_PLACES];
    MG_CHECK(base->GetPlacesForTurrets(places) == 1 && places[0] == CPoint(48, 48));
    for (int i = 0; i < 2; ++i) {
        auto robot = tests::make_engine_object<CMatrixRobotAI>();
        robot->m_Side = PLAYER_SIDE;
        robot->UnitInsert(0, MRT_CHASSIS, RUK_CHASSIS_WHEEL);
        base->m_BS.AddItem(robot.get());
        MG_CHECK(base->m_BS.GetItemsCnt() == i + 1);
        robot_cores[i].reset(robot->GetCore(DEBUG_CALL_INFO));
        robot.release();
    }
    CMatrixRobotAI *capturer = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && session.matches(3, object->AsRobot())) capturer = object->AsRobot();
    MG_CHECK(capturer != nullptr);
    record();
    check_queue();
    capturer->CaptureFactory(base);
    MG_CHECK(capturer->GetCaptureFactory() == base && capturer->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    while (session.time() < 2000) { session.advance(); record(); }
    std::cout << "entry state=" << base->m_State << " manual=" << capturer->IsDisableManual()
              << " owner=" << base->GetSide() << " captured=" << (base->m_Capturer == capturer) << '\n';
    MG_CHECK(base->m_State == BASE_OPENED && base->m_Capturer == capturer && capturer->IsDisableManual());
    MG_CHECK(capturer->GetCaptureFactory() == base && capturer->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    if (place_turret) {
        side().m_ActiveObject = base; // Synthetic initial building selection.
        side().m_CurrSel = BASE_SELECTED;
        g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(480, 480, 0);
        g_IFaceList->BeginBuildTurret(1);
        MG_CHECK(side().m_CurrentAction == BUILDING_TURRET && side().m_CannonForBuild.m_Cannon != nullptr);
        g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
        auto direction = D3DXVECTOR3(400, 400, 0) - g_MatrixMap->m_Camera.GetFrustumCenter();
        D3DXVec3Normalize(&g_MatrixMap->m_MouseDir, &direction);
        session.advance(); // Production side logic rejects a ray outside the declared slot.
        MG_CHECK(side().m_CannonForBuild.m_CanBuildFlag == 0);
        side().OnLButtonDown(CPoint(0, 0));
        MG_CHECK(base->m_BS.GetItemsCnt() == 2 && side().m_CannonForBuild.m_Cannon != nullptr);
        direction = D3DXVECTOR3(480, 480, 0) - g_MatrixMap->m_Camera.GetFrustumCenter();
        D3DXVec3Normalize(&g_MatrixMap->m_MouseDir, &direction);
        session.advance();
        MG_CHECK(side().m_CannonForBuild.m_CanBuildFlag == 1);
        MG_CHECK(side().m_CannonForBuild.m_Cannon->m_Place == g_MatrixMap->m_RN.FindInPL(CPoint(48, 48)));
        g_MatrixMap->Pause(true);
        side().OnLButtonDown(CPoint(0, 0));
        MG_CHECK(base->m_BS.GetItemsCnt() == 2 && base->m_TurretsHave == 0);
        for (int r = 0; r < MAX_RESOURCES; ++r)
            MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(r)) == 100);
        g_MatrixMap->Pause(false);
        side().OnLButtonDown(CPoint(0, 0));
        MG_CHECK(side().m_CurrentAction == NOTHING_SPECIAL && side().m_CannonForBuild.m_Cannon == nullptr);
        MG_CHECK(base->m_BS.GetItemsCnt() == 3 && base->m_BS.GetRobotsCnt() == 2);
        MG_CHECK(base->m_TurretsHave == 1);
        auto *queued = base->m_BS.GetTopItem()->m_NextStackItem->m_NextStackItem;
        MG_CHECK(queued->IsCannon() && queued->GetSide() == PLAYER_SIDE);
        MG_CHECK(queued->AsCannon()->m_CurrState == CANNON_UNDER_CONSTRUCTION);
        MG_CHECK(queued->AsCannon()->m_ParentBuilding == base);
        MG_CHECK(queued->AsCannon()->m_Place == g_MatrixMap->m_RN.FindInPL(CPoint(48, 48)));
        turret_core.reset(queued->GetCore(DEBUG_CALL_INFO));
        for (int r = 0; r < MAX_RESOURCES; ++r)
            MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(r)) == 100 - (17 + r));
    } else {
        session.advance();
        session.advance();
    }
    MG_CHECK(session.time() == (step == 10 ? 2020 : 2032));
    check_queue();
    record();
    const int transfer_at = step == 10 ? 5580 : 5600;
    int transferred = 0;
    while (session.time() < 15000 && !transferred) {
        if (session.time() == transfer_at - step) {
            MG_CHECK(base->GetSide() == PLAYER_SIDE && base->m_State == BASE_CLOSING);
            MG_CHECK(session.robot(3).alive && (!turret_core || turret_core->m_Object != nullptr));
            MG_CHECK(base->m_Capturer == capturer && capturer->GetCaptureFactory() == base);
            MG_CHECK(capturer->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
        }
        session.advance();
        record();
        if (base->GetSide() == 2) transferred = session.time();
        else {
            check_queue();
            for (int r = 0; r < MAX_RESOURCES; ++r)
                MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(r)) == 100 - (place_turret ? 17 + r : 0));
        }
    }
    std::cout << "player base transfer=" << transferred << " queue=" << base->m_BS.GetItemsCnt() << '\n';
    MG_CHECK(transferred == transfer_at && base->m_State == BASE_CLOSED);
    MG_CHECK(base->m_Capturer == nullptr && !session.robot(3).alive && session.robot(4).alive);
    MG_CHECK(base->m_BS.GetItemsCnt() == 0 && base->m_BS.GetTopItem() == nullptr && base->m_TurretsHave == 0);
    for (const auto &core : robot_cores) MG_CHECK(core->m_Object == nullptr && core->m_Ref == 1);
    MG_CHECK(!turret_core || (turret_core->m_Object == nullptr && turret_core->m_Ref == 1));
    for (int r = 0; r < MAX_RESOURCES; ++r) {
        MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(r)) == 100 + 2 * (11 + r));
        MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(r)) == 0);
    }
    for (int i = 0; i < 100; ++i) { session.advance(); record(); }
    MG_CHECK(base->GetSide() == 2 && base->m_State == BASE_CLOSED && !g_MatrixMap->IsPaused());
    for (int r = 0; r < MAX_RESOURCES; ++r)
        MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(r)) == 100 + 2 * (11 + r));
    return trace;
}
void capture_player_placed_queue() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool place_turret : {false, true}) {
        const auto first = player_placed_queue_run(mode, step, place_turret);
        MG_CHECK(first == player_placed_queue_run(mode, step, place_turret));
    }
}
void registered_base_capture_profile(CaptureRoute route) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) for (int owner : {0, 2}) {
        Session session({mode, 79, step, 3000, false});
        std::cout << session.metadata("base-capture-route-completion-v2") << " route=" << int(route) << '\n';
        auto *building = register_building(owner, BUILDING_BASE, 100, 100);
        auto &enemy = *g_MatrixMap->GetSideById(2);
        g_Config.m_Timings[UNIT_ROBOT] = 100000;
        for (int i = 0; i < MAX_RESOURCES; ++i) {
            g_Config.m_Price[CHASSIS2_TITAN + i] = 11 + i;
            enemy.SetResourceAmount(static_cast<ERes>(i), 100);
            side().SetResourceAmount(static_cast<ERes>(i), 200);
        }
        if (owner) for (int i = 0; i < 2; ++i) {
            auto queued = tests::make_engine_object<CMatrixRobotAI>();
            queued->m_Side = owner;
            queued->UnitInsert(0, MRT_CHASSIS, RUK_CHASSIS_WHEEL);
            const int count = building->m_BS.GetItemsCnt();
            building->m_BS.AddItem(queued.get());
            const bool accepted = building->m_BS.GetItemsCnt() == count + 1;
            if (accepted) queued.release();
            MG_CHECK(accepted);
        }
        submit_capture(building, route);
        EntityId capturer = 0;
        for (int i = 0; i < 2; ++i) {
            auto *robot = side().GetCurGroup()->GetObjectByN(i)->AsRobot();
            if (robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY)) {
                MG_CHECK(capturer == 0);
                capturer = session.matches(1, robot) ? 1 : 2;
            }
        }
        MG_CHECK(capturer != 0);
        const int closing_at = step == 10 ? 4330 : 4336;
        const int captured_at = step == 10 ? 5580 : 5600;
        int opened = 0, closing = 0, captured = 0;
        for (int i = 0; i < 2000 && !captured; ++i) {
            session.advance();
            if (session.time() == 1280 - step) MG_CHECK(building->m_State == BASE_OPENING);
            if (session.time() == closing_at - step) MG_CHECK(building->m_State == BASE_OPENED);
            if (session.time() == captured_at - step) MG_CHECK(building->m_State == BASE_CLOSING);
            if (!opened && building->m_State == BASE_OPENED) opened = session.time();
            if (!closing && opened && building->m_State == BASE_CLOSING) closing = session.time();
            if (building->GetSide() == PLAYER_SIDE) captured = session.time();
            else {
                MG_CHECK(session.robot(capturer).alive);
                MG_CHECK(building->m_BS.GetItemsCnt() == (owner ? 2 : 0));
                for (int resource = 0; resource < MAX_RESOURCES; ++resource)
                    MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(resource)) == 100);
            }
        }
        std::cout << "base capture mode=" << int(mode) << " step=" << step << " route=" << int(route) << " owner=" << owner
                  << " opened=" << opened << " closing=" << closing << " captured=" << captured << '\n';
        MG_CHECK(opened == 1280);
        MG_CHECK(closing == closing_at && captured == captured_at);
        MG_CHECK(building->GetSide() == PLAYER_SIDE && building->m_State == BASE_CLOSED);
        MG_CHECK(!session.robot(capturer).alive);
        MG_CHECK(session.robot(capturer == 1 ? 2 : 1).alive);
        MG_CHECK(building->m_Capturer == nullptr);
        MG_CHECK(building->m_BS.GetItemsCnt() == 0 && building->m_BS.GetTopItem() == nullptr);
        for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
            MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(resource)) == 100 + (owner ? 2 * (11 + resource) : 0));
            MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(resource)) == 200);
        }
        MG_CHECK(!g_MatrixMap->IsPaused() && side().GetStatus() == SS_ACTIVE);
        for (int i = 0; i < 100; ++i) session.advance();
        MG_CHECK(building->GetSide() == PLAYER_SIDE && building->m_State == BASE_CLOSED);
    }
}
void registered_base_capture() { registered_base_capture_profile(CaptureRoute::Context); }
void capture_keyboard_completion() {
    registered_capture_profile(0, CaptureRoute::Keyboard);
    registered_capture_profile(2, CaptureRoute::Keyboard);
    registered_base_capture_profile(CaptureRoute::Keyboard);
}
void capture_button_completion() {
    registered_capture_profile(0, CaptureRoute::Button);
    registered_capture_profile(2, CaptureRoute::Button);
    registered_base_capture_profile(CaptureRoute::Button);
}
void capture_target_removal() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step, 1000, false});
        std::cout << session.metadata("capture-target-removal-v2") << '\n';
        auto *building = register_building(0, BUILDING_TITAN, 160, 200);
        auto *robot = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
        g_MatrixMap->m_TraceStopObj = building;
        side().OnRButtonDown(CPoint(0, 0));
        advance_to(session, step == 10 ? 200 : 208);
        MG_CHECK(robot->GetCaptureFactory() == building);
        MG_CHECK(building->m_Capturer == robot && building->GetSide() == 0);
        g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
        g_MatrixMap->StaticDelete(building);
        MG_CHECK(robot->GetCaptureFactory() == nullptr);
        MG_CHECK(!robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
        const auto &group = side().m_PlayerGroup[robot->GetGroupLogic()];
        const int released_at = step == 10 ? 210 : 240;
        while (session.time() < released_at) {
            MG_CHECK(group.Order() == mpo_Capture && group.m_Obj != nullptr);
            session.advance();
        }
        MG_CHECK(group.Order() == mpo_Stop && group.m_Obj == nullptr);
        advance_to(session, 400);
        MG_CHECK(group.Order() == mpo_Stop && group.m_Obj == nullptr);
        MG_CHECK(session.robot(1).alive && session.robot(2).alive);
        MG_CHECK(!g_MatrixMap->IsPaused());
    }
}
void capture_candidate_lifetime() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 79, step, 1000, false});
        std::cout << session.metadata("capture-candidate-lifetime-v2") << '\n';
        auto *building = register_building(0, BUILDING_TITAN, 100, 200);
        auto *robot = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
        std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(building->GetCore(DEBUG_CALL_INFO),
            [](SObjectCore *value) { value->Release(); });
        const int refs = core->m_Ref;
        MG_CHECK(!robot->IsCaptureInformed());
        session.advance();
        MG_CHECK(robot->IsCaptureInformed());
        MG_CHECK(core->m_Ref == refs + 3); // Both nearby player robots and one enemy receive candidates.
        advance_to(session, step == 10 ? 1020 : 1024); // Re-publication must not duplicate candidate references.
        MG_CHECK(core->m_Ref == refs + 3);
        MG_CHECK(!robot->FindOrderLikeThat(ROT_CAPTURE_FACTORY)); // Manual player robots do not auto-claim candidates.
        MG_CHECK(building->m_TrueColor.m_ColoredCnt > 0 && building->GetSide() == 0);
        g_MatrixMap->StaticDelete(building);
        MG_CHECK(core->m_Ref == 1 && core->m_Object == nullptr);
        MG_CHECK(robot->GetCaptureFactory() == nullptr);
        session.advance();
        MG_CHECK(core->m_Ref == 1 && robot->IsLiveRobot());
    }
}
void base_survival() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool player : {false, true})
    for (int step : {10, 16}) {
        Session session({mode, 79, step, 1000, false});
        DialogData dialog;
        std::cout << "player=" << player << ' ' << session.metadata("synthetic-base-survival-v2") << '\n';
        auto *building = register_building(player ? PLAYER_SIDE : 2, BUILDING_BASE, 100, 100);
        MG_CHECK(session.remove(player ? 1 : 3) && session.remove(player ? 2 : 4));
        advance_to(session, step == 10 ? 2020 : 2032);
        MG_CHECK(g_MatrixMap->GetSideById(player ? PLAYER_SIDE : 2)->GetStatus() == SS_ACTIVE);
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        MG_CHECK(!g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeName == nullptr);
        g_MatrixMap->StaticDelete(building);
        // The next status scan eliminates the side; the following one opens the result.
        advance_to(session, (step == 10 ? 4030 : 4048) - step);
        MG_CHECK(g_MatrixMap->GetSideById(player ? PLAYER_SIDE : 2)->GetStatus() == SS_ACTIVE);
        session.advance();
        MG_CHECK(g_MatrixMap->GetSideById(player ? PLAYER_SIDE : 2)->GetStatus() == SS_NONE);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !player);
        MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
        advance_to(session, (step == 10 ? 6040 : 6064) - step);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        session.advance();
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (player ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
    }
}
void factory_does_not_prevent_elimination() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool player : {false, true})
    for (int step : {10, 16}) {
        Session session({mode, 79, step, 1000, false});
        DialogData dialog;
        std::cout << "player=" << player << ' ' << session.metadata("synthetic-factory-elimination-v2") << '\n';
        auto *building = register_building(player ? PLAYER_SIDE : 2, BUILDING_TITAN, 400, 100);
        MG_CHECK(session.remove(player ? 1 : 3) && session.remove(player ? 2 : 4));
        session.advance();
        MG_CHECK(g_MatrixMap->GetSideById(player ? PLAYER_SIDE : 2)->GetStatus() == SS_NONE);
        MG_CHECK(building->GetSide() == 0 && building->m_TrueColor.m_ColoredCnt == 0);
        MG_CHECK(building->m_TrueColor.m_Color == 0);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !player);
        advance_to(session, (step == 10 ? 2020 : 2032) - step);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        session.advance();
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (player ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
    }
}
// Robot death creates a crater as well as ordinary combat effects. Restore its
// borrowed static properties only after Session has retired the effects.
struct CaptureEffectAccess : CMatrixEffect { using CMatrixEffect::m_SpotProperties; };
struct CaptureCraterAssets {
    SSpotProperties previous = CaptureEffectAccess::m_SpotProperties[SPOT_VORONKA];
    void install() {
        CaptureEffectAccess::m_SpotProperties[SPOT_VORONKA] = CaptureEffectAccess::m_SpotProperties[SPOT_PLASMA_HIT];
        CaptureEffectAccess::m_SpotProperties[SPOT_VORONKA].func = SpotTaktVoronka;
    }
    ~CaptureCraterAssets() { CaptureEffectAccess::m_SpotProperties[SPOT_VORONKA] = previous; }
};
std::vector<std::string> rival_capture_run(random::Mode mode, int step, bool enemy_first) {
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 4000, false, true, false, tests::NavigationLayout::Open});
    assets.install();
    crater.install();
    std::cout << session.metadata("rival-armed-capture-v1") << " enemy_first=" << enemy_first << '\n';
    auto *factory = register_building(0, BUILDING_TITAN, 300, 300);
    g_Config.m_Timings[RESOURCE_TITAN] = 1000;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 180;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {20, 0, 0};
    g_Config.m_WeaponStrengthAI[RUK_WEAPON_LASER] = 10;
    g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
    g_Config.m_Overheat[WEAPON_LASER_COOL] = 10;
    g_Config.m_Overheat[WCP_LASER] = 100;
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    std::array<CMatrixRobotAI *, 4> robots{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot()) for (EntityId id = 1; id <= 4; ++id)
            if (session.matches(id, object->AsRobot())) robots[id - 1] = object->AsRobot();
    for (auto *robot : robots) MG_CHECK(robot != nullptr);
    const auto position = [&](CMatrixRobotAI *robot, float x, float y, bool sentinel) {
        robot->m_PosX = x; robot->m_PosY = y;
        if (sentinel) { robot->SetMaxSpeed(0); robot->m_GroupSpeed = 0; }
        robot->MapPosCalc(); robot->RChange(MR_Matrix); robot->RNeed(MR_Matrix);
        robot->m_CalcBoundsLastTime = g_MatrixMap->GetTime() - 100;
        robot->JoinToGroup();
    };
    position(robots[0], 180, 280, false);
    position(robots[2], 420, 320, false);
    // Live, unarmed sentinels keep this encounter from ending the battle.
    position(robots[1], 50, 600, true);
    position(robots[3], 600, 50, true);
    for (auto *robot : {robots[0], robots[2]}) {
        tests::arm_battle_robot(*robot, RUK_WEAPON_LASER);
        robot->InitMaxHitpoint(200);
        robot->CalcStrength();
    }
    // Preserve the loader's buildings-before-robots invariant. Reverse rival logic
    // and spatial registration, not allocation/UID creation or command submission.
    for (auto *robot : robots) { robot->DelLT(); robot->UnjoinGroup(); }
    const std::array<int, 4> order = enemy_first ? std::array<int, 4>{2, 0, 1, 3} : std::array<int, 4>{0, 2, 1, 3};
    for (int index : order) { robots[index]->AddLT(); robots[index]->JoinToGroup(); }
    MG_CHECK(CMatrixMapStatic::GetFirstLogic() == factory);
    MG_CHECK(factory->GetNextLogic() == robots[order[0]]);
    auto *selection = side().GetCurSelGroup();
    selection->RemoveAll();
    selection->AddObject(robots[0], -4);
    side().SetCurGroup(selection);
    side().m_CurrSel = ROBOT_SELECTED;
    for (int r = 0; r < MAX_RESOURCES; ++r) {
        side().SetResourceAmount(static_cast<ERes>(r), 0);
        g_MatrixMap->GetSideById(2)->SetResourceAmount(static_cast<ERes>(r), 0);
    }
    submit_capture(factory, CaptureRoute::Context);
    // The enemy capture order is a declared scenario input, not an AI-planner claim.
    robots[2]->CaptureFactory(factory);
    MG_CHECK(robots[0]->GetCaptureFactory() == factory && robots[2]->GetCaptureFactory() == factory);
    const int group = robots[0]->GetGroupLogic();
    MG_CHECK(side().m_PlayerGroup[group].Order() == mpo_Capture && side().m_PlayerGroup[group].m_Obj == factory);
    const auto release = [](SObjectCore *core) { core->Release(); };
    std::unique_ptr<SObjectCore, decltype(release)> player_core(robots[0]->GetCore(DEBUG_CALL_INFO), release);
    std::unique_ptr<SObjectCore, decltype(release)> enemy_core(robots[2]->GetCore(DEBUG_CALL_INFO), release);
    const std::array<SObjectCore *, 2> cores{player_core.get(), enemy_core.get()};
    const int winner_index = enemy_first ? 1 : 0, loser_index = 1 - winner_index;
    auto *winner = robots[winner_index * 2];
    const int winner_side = winner->GetSide();
    const DWORD winner_color = 0xff000000 | g_MatrixMap->GetSideColor(winner_side);
    struct Expected {
        int paint, capture, income, death, retired;
        std::array<int, 2> targeted, firing, hits;
    };
    const bool park = mode == random::Mode::ParkMiller;
    const Expected expected = enemy_first
        ? (step == 10 ? Expected{2160, 9060, 10060, 1910, park ? 4830 : 6250, {910, 820}, {90, 92}, {10, 9}}
                      : Expected{2176, 9072, 10080, 1920, park ? 4848 : 6272, {912, 816}, {56, 58}, {10, 9}})
        : (step == 10 ? Expected{1650, 8550, 9550, 3000, park ? 7320 : 6580, {2010, 2020}, {100, 98}, {8, 10}}
                      : Expected{1664, 8560, 9568, 3024, park ? 7360 : 6608, {2032, 2048}, {63, 61}, {8, 10}});
    std::array<float, 2> health{20, 20};
    std::array<int, 2> hits{}, firing{}, targeted{}, death{}, retired{};
    int painted = 0, captured = 0, income = 0, released = 0, forgotten = 0;
    std::vector<std::string> trace;
    while (session.time() <= expected.capture + 2000) {
        session.advance();
        const int time = session.time();
        MG_CHECK(cores[winner_index]->m_Object == winner);
        if (factory->m_Capturer) {
            MG_CHECK((factory->m_Capturer == robots[0] && cores[0]->m_Object == robots[0]) ||
                     (factory->m_Capturer == robots[2] && cores[1]->m_Object == robots[2]));
        }
        for (int i = 0; i < 2; ++i) {
            auto *robot = robots[i * 2];
            if (cores[i]->m_Object) {
                MG_CHECK(cores[i]->m_Object == robot);
                if (!targeted[i] && (robot->GetEnv()->m_Target == robots[(1 - i) * 2] ||
                    robot->GetEnv()->m_TargetAttack == robots[(1 - i) * 2])) targeted[i] = time;
                // This is the number of observed firing-order boundaries, not shot count.
                if (robot->FindOrderLikeThat(ROT_FIRE)) ++firing[i];
                if (robot->GetHitPoint() < health[i]) { ++hits[i]; health[i] = robot->GetHitPoint(); }
                if (!death[i] && !robot->IsLiveRobot()) death[i] = time;
            } else if (!retired[i]) retired[i] = time;
        }
        if (!painted && factory->m_TrueColor.m_ColoredCnt) {
            painted = time;
            MG_CHECK(factory->m_TrueColor.m_ColoredCnt == 1);
            // Capture-order execution stores its robot, while nearest-robot scanning
            // determines the paint source. Those identities need not agree.
            MG_CHECK(factory->m_Capturer == robots[2]);
        }
        if (!captured && factory->GetSide()) captured = time;
        if (!income && (side().GetResourcesAmount(TITAN) || g_MatrixMap->GetSideById(2)->GetResourcesAmount(TITAN)))
            income = time;
        if (!released && factory->GetSide() && !winner->GetCaptureFactory() && !factory->m_Capturer) released = time;
        if (!forgotten && death[loser_index] && !winner->GetEnv()->m_Target &&
            !winner->GetEnv()->m_TargetAttack && !winner->GetEnv()->GetEnemyCnt()) forgotten = time;
        MG_CHECK(winner->IsLiveRobot());
        MG_CHECK(factory->GetSide() == (time < expected.capture ? 0 : winner_side));
        MG_CHECK((factory->m_TrueColor.m_ColoredCnt > 0) == (time >= expected.paint));
        if (time >= expected.paint) MG_CHECK(factory->m_TrueColor.m_Color == winner_color);
        MG_CHECK(bool(cores[loser_index]->m_Object) == (time < expected.retired));
        if (time < expected.death) MG_CHECK(robots[loser_index * 2]->IsLiveRobot());
        if (time == expected.death) MG_CHECK(!robots[loser_index * 2]->IsLiveRobot());
        for (int side_id : {PLAYER_SIDE, 2}) {
            auto *owner = g_MatrixMap->GetSideById(side_id);
            MG_CHECK(owner->GetStatValue(STAT_ROBOT_KILL) == (time >= expected.death && side_id == winner_side ? 1 : 0));
            for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
                const int amount = owner->GetResourcesAmount(static_cast<ERes>(resource));
                if (resource != TITAN || side_id != winner_side || time < expected.income) MG_CHECK(amount == 0);
                else MG_CHECK(amount == (time < expected.income + (step == 10 ? 1000 : 1008) ? 10 : 20));
            }
        }
        if (time == expected.capture - step) {
            MG_CHECK(factory->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
            MG_CHECK(winner->GetCaptureFactory() == factory);
        }
        if (time >= expected.capture) {
            MG_CHECK(factory->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
            MG_CHECK(!winner->GetCaptureFactory() && !factory->m_Capturer);
            MG_CHECK(!winner->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
        }
        trace.push_back(session.snapshot() + ":" + std::to_string(factory->GetSide()) + ":" +
            std::to_string(factory->m_TrueColor.m_ColoredCnt) + ":" + std::to_string(factory->m_TrueColor.m_Color) + ":" +
            std::to_string(factory->m_Capturer ? factory->m_Capturer->GetSide() : 0) + ":" +
            std::to_string(health[0]) + ":" + std::to_string(health[1]) + ":" +
            std::to_string(side().GetResourcesAmount(TITAN)) + ":" +
            std::to_string(g_MatrixMap->GetSideById(2)->GetResourcesAmount(TITAN)));
    }
    MG_CHECK(painted == expected.paint && captured == expected.capture && income == expected.income);
    MG_CHECK(targeted == expected.targeted && firing == expected.firing && hits == expected.hits);
    MG_CHECK(death[loser_index] == expected.death && retired[loser_index] == expected.retired);
    MG_CHECK(death[winner_index] == 0 && retired[winner_index] == 0);
    MG_CHECK(health[loser_index] == 0 && health[winner_index] == (enemy_first ? 2 : 4));
    MG_CHECK(forgotten == expected.death && released == expected.capture);
    MG_CHECK(!winner->GetEnv()->m_Target && !winner->GetEnv()->m_TargetAttack && !winner->GetEnv()->GetEnemyCnt());
    MG_CHECK(side().m_PlayerGroup[group].m_RobotCnt == (enemy_first ? 0 : 1));
    // An empty group retains its dormant order; a surviving player group stops normally.
    MG_CHECK(side().m_PlayerGroup[group].Order() == (enemy_first ? mpo_Capture : mpo_Stop));
    MG_CHECK(side().m_PlayerGroup[group].m_Obj == (enemy_first ? factory : nullptr));
    MG_CHECK(session.robot(2).alive && session.robot(4).alive);
    std::cout << "rival owner=" << winner_side << " paint=" << painted << " captured=" << captured << " income=" << income
              << " target=" << targeted[0] << ',' << targeted[1] << " firing_boundaries=" << firing[0] << ',' << firing[1]
              << " damage_boundaries=" << hits[0] << ',' << hits[1] << " death=" << death[loser_index]
              << " retired=" << retired[loser_index] << " references_released=" << released << '\n';
    return trace;
}
void rival_armed_capture() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool enemy_first : {false, true}) {
        const auto first = rival_capture_run(mode, step, enemy_first);
        MG_CHECK(first == rival_capture_run(mode, step, enemy_first));
    }
}
std::vector<std::string> capture_death_run(random::Mode mode, int step) {
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 1500, false});
    std::cout << session.metadata("capture-death-v2") << '\n';
    assets.install();
    crater.install();
    auto *building = register_building(0, BUILDING_TITAN, 160, 200);
    auto *capturer = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    g_MatrixMap->m_TraceStopObj = building;
    side().OnRButtonDown(CPoint(0, 0));
    advance_to(session, step == 10 ? 1000 : 1008);
    MG_CHECK(capturer->GetCaptureFactory() == building && building->m_Capturer == capturer);
    MG_CHECK(building->m_TrueColor.m_ColoredCnt == 1 && building->GetSide() == 0);
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(capturer->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {10000, 0, 0};
    MG_CHECK(capturer->Damage(WEAPON_LASER, capturer->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
    MG_CHECK(capturer->m_CurrState == ROBOT_DIP);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
    MG_CHECK(building->m_Capturer == nullptr);
    const int replacement_at = step == 10 ? 1020 : 1040;
    const int captured_at = step == 10 ? 7530 : 7536;
    const int removed_at = mode == random::Mode::ParkMiller ? (step == 10 ? 5430 : 5440) : (step == 10 ? 4110 : 4112);
    int replacement = 0, captured = 0, removed = 0;
    std::vector<std::string> trace;
    while (session.time() < 11000) {
        session.advance();
        if (session.time() == replacement_at - step) MG_CHECK(building->m_Capturer == nullptr);
        if (session.time() == captured_at - step) MG_CHECK(building->GetSide() == 0);
        if (session.time() == removed_at - step) MG_CHECK(core->m_Object != nullptr);
        if (!replacement && building->m_Capturer) replacement = session.time();
        if (!captured && building->GetSide() == PLAYER_SIDE) captured = session.time();
        if (!removed && !core->m_Object) removed = session.time();
        trace.push_back(session.snapshot() + ":" + std::to_string(building->GetSide()) + ":" +
            std::to_string(building->m_TrueColor.m_ColoredCnt) + ":" + std::to_string(core->m_Object != nullptr));
    }
    std::cout << "capture death mode=" << int(mode) << " owner=" << building->GetSide()
              << " paint=" << building->m_TrueColor.m_ColoredCnt << " replacement=" << replacement << " captured=" << captured << " removed=" << removed << " core=" << bool(core->m_Object) << '\n';
    MG_CHECK(replacement == replacement_at && captured == captured_at && removed == removed_at);
    MG_CHECK(building->GetSide() == PLAYER_SIDE && building->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
    MG_CHECK(building->m_TrueColor.m_Color == 0xff00ff00);
    MG_CHECK(!session.robot(1).alive && session.robot(2).alive);
    MG_CHECK(core->m_Object == nullptr);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
    MG_CHECK(!g_MatrixMap->IsPaused() && side().GetStatus() == SS_ACTIVE);
    return trace;
}
void capture_combat_death() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = capture_death_run(mode, step);
        MG_CHECK(first == capture_death_run(mode, step));
    }
}
std::vector<std::string> base_interruption_run(random::Mode mode, int step, int interrupt_at) {
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 1500, false});
    std::cout << session.metadata("base-interruption-v2") << " interrupt=" << interrupt_at << '\n';
    assets.install();
    crater.install();
    auto *building = register_building(0, BUILDING_BASE, 100, 100);
    auto *capturer = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    g_MatrixMap->m_TraceStopObj = building;
    side().OnRButtonDown(CPoint(0, 0));
    const int command_at = ((interrupt_at + step - 1) / step) * step;
    advance_to(session, command_at);
    MG_CHECK(building->m_State == (interrupt_at == 200 ? BASE_OPENING : interrupt_at == 2000 ? BASE_OPENED : BASE_CLOSING));
    MG_CHECK(capturer->GetCaptureFactory() == building && building->m_Capturer == capturer);
    MG_CHECK(capturer->IsDisableManual() == (interrupt_at != 200));
    CFormMatrixGame form;
    press(form, 'S');
    MG_CHECK(capturer->GetCaptureFactory() == nullptr && building->m_Capturer == nullptr);
    MG_CHECK(session.robot(1).order == mpo_Stop);
    MG_CHECK(capturer->IsMustDie() == (interrupt_at != 200));
    const int closed_at = interrupt_at == 200 ? (step == 10 ? 370 : 400) :
        interrupt_at == 2000 ? (step == 10 ? 3260 : 3264) : (step == 10 ? 5580 : 5600);
    const int death_at = interrupt_at == 200 ? 0 :
        interrupt_at == 2000 ? (step == 10 ? 2010 : 2016) : (step == 10 ? 4510 : 4528);
    MG_CHECK(session.robot(1).alive);
    int closed = 0, died = 0;
    std::vector<std::string> trace;
    while (session.time() < 10000) {
        session.advance();
        if (session.time() == closed_at - step) MG_CHECK(building->m_State == BASE_CLOSING);
        if (!closed && building->m_State == BASE_CLOSED) closed = session.time();
        if (!died && !session.robot(1).alive) died = session.time();
        MG_CHECK(building->GetSide() == 0);
        trace.push_back(session.snapshot() + ":" + std::to_string(building->m_State) + ":" + std::to_string(building->m_Capturer != nullptr));
    }
    std::cout << "base interruption mode=" << int(mode) << " step=" << step << " command=" << command_at
              << " owner=" << building->GetSide() << " state=" << building->m_State
              << " robot1=" << session.robot(1).alive << " robot2=" << session.robot(2).alive
              << " closed=" << closed << " died=" << died << '\n';
    MG_CHECK(closed == closed_at && died == death_at);
    MG_CHECK(building->m_State == BASE_CLOSED && building->GetSide() == 0);
    MG_CHECK(session.robot(1).alive == (interrupt_at == 200) && session.robot(2).alive);
    MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(!g_MatrixMap->IsPaused());
    return trace;
}
void base_capture_interruption() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) for (int interrupt_at : {200, 2000, 4500}) {
        const auto first = base_interruption_run(mode, step, interrupt_at);
        MG_CHECK(first == base_interruption_run(mode, step, interrupt_at));
    }
}
void turret_does_not_prevent_elimination() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool player : {false, true})
    for (bool constructing : {false, true}) for (int step : {10, 16}) {
        tests::BattleCombatAssets assets;
        Session session({mode, 85, step, 1000, false});
        assets.install();
        DialogData dialog;
        std::cout << "player=" << player << " constructing=" << constructing << ' '
                  << session.metadata("synthetic-turret-elimination-v2") << '\n';
        auto &props = g_Config.m_CannonsProps[0];
        props = {};
        props.m_Hitpoint = 100;
        props.weapon = WEAPON_CANNON2;
        props.seek_radius = 1;
        auto *cannon = tests::create_battle_cannon(player ? PLAYER_SIDE : 2, 400, 100);
        cannon->m_CurrState = constructing ? CANNON_UNDER_CONSTRUCTION : CANNON_IDLE;
        MG_CHECK(session.remove(player ? 1 : 3) && session.remove(player ? 2 : 4));
        session.advance();
        MG_CHECK(g_MatrixMap->GetSideById(player ? PLAYER_SIDE : 2)->GetStatus() == SS_NONE);
        MG_CHECK(cannon->IsLiveCannon() && cannon->GetSide() == (player ? PLAYER_SIDE : 2));
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !player);
        advance_to(session, (step == 10 ? 2020 : 2032) - step);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        session.advance();
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (player ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
    }
}
Profile transport_outcome_profile(random::Mode mode, int step) {
    Profile profile;
    profile.mode = mode;
    profile.seed = 85;
    profile.step_ms = step;
    profile.audio = false;
    profile.initially_selected = false;
    profile.unavailable_icon_targets = true;
    return profile;
}
using OutcomeCore = std::unique_ptr<SObjectCore, void (*)(SObjectCore *)>;
OutcomeCore retain_core(CMatrixMapStatic &object) {
    return OutcomeCore(object.GetCore(DEBUG_CALL_INFO), [](SObjectCore *core) { core->Release(); });
}
CMatrixFlyer *order_outcome_transport(int owner, bool armed = false) {
    tests::install_battle_transport(100, armed);
    g_MatrixMap->GetSideById(owner)->OrderFlyer(D3DXVECTOR2(200, 300), FO_GIVE_BOT, 0, 0, CPoint(0, 0), 0);
    MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1);
    auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
    MG_CHECK(carrier->GetSide() == owner && carrier->CarryingRobot());
    // Non-player requests are a Debug-F-only legacy quirk: ApplyOrder still creates player cargo.
    MG_CHECK(carrier->GetCarryingRobot()->GetSide() == PLAYER_SIDE);
    return carrier;
}
void empty_flyer_survival_profile(bool armed) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool player : {false, true})
    for (int step : {10, 16}) {
        tests::BattleMaterialProfile materials;
        Session session(transport_outcome_profile(mode, step));
        DialogData dialog;
        std::cout << session.metadata(armed ? "weapon-attached-empty-carrier-v1" : "synthetic-empty-carrier-outcome-v2") << '\n';
        const int owner = player ? PLAYER_SIDE : 2;
        auto *carrier = order_outcome_transport(owner, armed);
        auto core = retain_core(*carrier);
        // Scheduled removal isolates the flyer from the live robot it carried.
        g_MatrixMap->StaticDelete(carrier->GetCarryingRobot());
        MG_CHECK(!carrier->CarryingRobot());
        MG_CHECK(session.remove(player ? 1 : 3) && session.remove(player ? 2 : 4));
        session.advance();
        MG_CHECK(g_MatrixMap->GetSideById(owner)->GetStatus() == SS_NONE);
        MG_CHECK(core->m_Object == carrier && carrier->GetSide() == owner);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !player);
        advance_to(session, step == 10 ? 2010 : 2016);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        session.advance();
        MG_CHECK(core->m_Object == carrier && !carrier->CarryingRobot());
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(g_MatrixMap->m_DialogModeName != nullptr);
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) ==
                 (player ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
    }
}
void flyer_does_not_prevent_elimination() { empty_flyer_survival_profile(false); }
void armed_flyer_does_not_prevent_elimination() { empty_flyer_survival_profile(true); }
void transported_robot_survival_profile(bool armed) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        tests::BattleMaterialProfile materials;
        Session session(transport_outcome_profile(mode, step));
        DialogData dialog;
        std::cout << session.metadata(armed ? "weapon-attached-transport-survival-v1" : "synthetic-last-transported-robot-v2") << '\n';
        auto *carrier = order_outcome_transport(PLAYER_SIDE, armed);
        auto carrier_core = retain_core(*carrier);
        auto *robot = carrier->GetCarryingRobot()->AsRobot();
        auto robot_core = retain_core(*robot);
        MG_CHECK(session.remove(1) && session.remove(2));
        int released = 0, landed = 0, retired = 0;
        while (session.time() < 20000) {
            session.advance();
            MG_CHECK(side().GetStatus() == SS_ACTIVE && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
            MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
            MG_CHECK(robot_core->m_Object == robot && robot->IsLiveRobot());
            if (!released && robot->m_CurrState != ROBOT_CARRYING) {
                released = session.time();
                MG_CHECK(robot->m_CurrState == ROBOT_FALLING);
            }
            if (!landed && released && robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) landed = session.time();
            MG_CHECK(robot->m_CurrState == (!released ? ROBOT_CARRYING :
                !landed ? ROBOT_FALLING : ROBOT_SUCCESSFULLY_BUILD));
            if (carrier_core->m_Object) MG_CHECK(carrier->CarryingRobot() == !released);
            if (!retired && !carrier_core->m_Object) retired = session.time();
            if (session.time() == (step == 10 ? 1000 : 1008)) {
                MG_CHECK(carrier_core->m_Object == carrier);
                carrier->RNeed(MR_Graph | MR_Matrix);
                // This ray misses the body box and intersects the attached weapon
                // box. Use the actual carrier transform, independent of its yaw.
                D3DXVECTOR3 start(30, 0, 100), direction(0, 0, -1), world_start, world_direction;
                D3DXVec3TransformCoord(&world_start, &start, &carrier_core->m_Matrix);
                D3DXVec3TransformNormal(&world_direction, &direction, &carrier_core->m_Matrix);
                float distance = 0;
                MG_CHECK(carrier->Pick(world_start, world_direction, &distance) == armed);
                if (armed) MG_CHECK(std::abs(distance - 80.0f) < 0.01f);
            }
        }
        std::cout << "Last robot release=" << released << " landing=" << landed << " retirement=" << retired << '\n';
        MG_CHECK(released == (mode == random::Mode::ParkMiller ? (step == 10 ? 6100 : 6112) : (step == 10 ? 4940 : 4944)));
        MG_CHECK(landed == (mode == random::Mode::ParkMiller ? (step == 10 ? 6420 : 6432) : (step == 10 ? 5260 : 5264)));
        MG_CHECK(retired == (mode == random::Mode::ParkMiller ? (step == 10 ? 11960 : 11968) : (step == 10 ? 9670 : 9664)));
        MG_CHECK(carrier_core->m_Object == nullptr && g_MatrixMap->m_AD_Obj_cnt == 0);
        MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0 && g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    }
}
void transported_robot_prevents_elimination() { transported_robot_survival_profile(false); }
void armed_transport_survival() { transported_robot_survival_profile(true); }
struct OutcomeMapObservation : CMatrixMap {
    static int status_scan(const CMatrixMap &map) { return map.* &OutcomeMapObservation::m_PrevTimeCheckStatus; }
};
std::vector<std::string> falling_cargo_scan_run(random::Mode mode, int step, bool remove) {
    tests::BattleMaterialProfile materials;
    Session session(transport_outcome_profile(mode, step));
    DialogData dialog;
    std::cout << "remove=" << remove << ' ' << session.metadata("falling-cargo-status-scan-v1") << '\n';
    // Delay the delivery request so a natural side-status scan occurs during
    // the short falling interval, rather than only before and after landing.
    const int launch = mode == random::Mode::ParkMiller ? 1800 : 3000;
    advance_to(session, ((launch + step - 1) / step) * step);
    auto *carrier = order_outcome_transport(PLAYER_SIDE);
    auto carrier_core = retain_core(*carrier);
    auto *cargo = carrier->GetCarryingRobot()->AsRobot();
    auto cargo_core = retain_core(*cargo);
    MG_CHECK(session.remove(1) && session.remove(2));
    int released = 0, landed = 0, falling_scan = 0, eliminated = 0, result = 0;
    std::vector<std::string> trace;
    while (session.time() < 12000 && !g_MatrixMap->IsPaused()) {
        const int previous_scan = OutcomeMapObservation::status_scan(*g_MatrixMap);
        session.advance();
        if (cargo_core->m_Object && !released && cargo->m_CurrState == ROBOT_FALLING) {
            released = session.time();
            MG_CHECK(!carrier->CarryingRobot());
            if (remove) g_MatrixMap->StaticDelete(cargo);
        }
        const bool scanned = previous_scan != OutcomeMapObservation::status_scan(*g_MatrixMap);
        if (cargo_core->m_Object) {
            MG_CHECK(cargo->IsLiveRobot());
            if (cargo->m_CurrState == ROBOT_FALLING && scanned) {
                falling_scan = session.time();
                MG_CHECK(side().GetStatus() == SS_ACTIVE);
            }
            if (!landed && released && cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) landed = session.time();
        }
        if (side().GetStatus() == SS_NONE && !eliminated) {
            eliminated = session.time();
            MG_CHECK(remove && scanned && cargo_core->m_Object == nullptr);
        }
        if (g_MatrixMap->IsPaused()) {
            result = session.time();
            MG_CHECK(remove && g_MatrixMap->m_DialogModeName &&
                std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
        }
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        trace.push_back(session.snapshot() + ":" + std::to_string(cargo_core->m_Object ? cargo->m_CurrState : -1)
            + ":" + std::to_string(OutcomeMapObservation::status_scan(*g_MatrixMap)));
    }
    std::cout << "release=" << released << " landed=" << landed << " falling_scan=" << falling_scan
              << " eliminated=" << eliminated << " result=" << result << '\n';
    const int expected_release = mode == random::Mode::ParkMiller ? (step == 10 ? 7900 : 7920)
        : (step == 10 ? 7940 : 7952);
    const int expected_scan = step == 10 ? 8050 : 8080;
    MG_CHECK(released == expected_release);
    if (remove) {
        MG_CHECK(eliminated == expected_scan && result == (step == 10 ? 10060 : 10096));
        MG_CHECK(!landed && !falling_scan);
    } else {
        MG_CHECK(falling_scan == expected_scan && landed == expected_release + 320);
        MG_CHECK(!eliminated && !result);
        MG_CHECK(cargo_core->m_Object == cargo && side().GetStatus() == SS_ACTIVE);
    }
    MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0 && g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    return trace;
}
void falling_cargo_status_scan() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool remove : {false, true}) {
        const auto first = falling_cargo_scan_run(mode, step, remove);
        MG_CHECK(first == falling_cargo_scan_run(mode, step, remove));
    }
}
void transported_robot_removal_outcome() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int remove_at : {1000, 7000})
    for (int step : {10, 16}) {
        tests::BattleMaterialProfile materials;
        Session session(transport_outcome_profile(mode, step));
        DialogData dialog;
        std::cout << "remove_at=" << remove_at << ' ' << session.metadata("synthetic-transport-cargo-removal-v2") << '\n';
        auto *carrier = order_outcome_transport(PLAYER_SIDE);
        auto carrier_core = retain_core(*carrier);
        auto *robot = carrier->GetCarryingRobot()->AsRobot();
        auto robot_core = retain_core(*robot);
        MG_CHECK(session.remove(1) && session.remove(2));
        advance_to(session, ((remove_at + step - 1) / step) * step);
        MG_CHECK(robot_core->m_Object == robot && carrier_core->m_Object == carrier);
        MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(robot->m_CurrState == (remove_at == 1000 ? ROBOT_CARRYING : ROBOT_SUCCESSFULLY_BUILD));
        MG_CHECK(carrier_core->m_Object == carrier && carrier->CarryingRobot() == (remove_at == 1000));
        g_MatrixMap->StaticDelete(robot);
        MG_CHECK(robot_core->m_Object == nullptr && !carrier->CarryingRobot());
        const int eliminated_at = remove_at == 1000 ? (step == 10 ? 2020 : 2032) : (step == 10 ? 8050 : 8080);
        advance_to(session, eliminated_at - step);
        MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->IsPaused());
        session.advance();
        MG_CHECK(side().GetStatus() == SS_NONE && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        MG_CHECK(carrier_core->m_Object == carrier);
        advance_to(session, eliminated_at + (step == 10 ? 2010 : 2016) - step);
        MG_CHECK(!g_MatrixMap->m_DialogModeName);
        session.advance();
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
        MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0 && g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    }
}
void transported_robot_damage_outcome() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int hit_at : {1000, 7000})
    for (int step : {10, 16}) {
        tests::BattleMaterialProfile materials;
        tests::BattleCombatAssets assets;
        CaptureCraterAssets crater;
        Session session(transport_outcome_profile(mode, step));
        assets.install();
        crater.install();
        DialogData dialog;
        std::cout << "hit_at=" << hit_at << ' ' << session.metadata("synthetic-transport-cargo-damage-v2") << '\n';
        auto *carrier = order_outcome_transport(PLAYER_SIDE);
        auto carrier_core = retain_core(*carrier);
        auto *robot = carrier->GetCarryingRobot()->AsRobot();
        auto robot_core = retain_core(*robot);
        MG_CHECK(session.remove(1) && session.remove(2));
        advance_to(session, ((hit_at + step - 1) / step) * step);
        MG_CHECK(robot_core->m_Object == robot && carrier_core->m_Object == carrier);
        MG_CHECK(robot->m_CurrState == (hit_at == 1000 ? ROBOT_CARRYING : ROBOT_SUCCESSFULLY_BUILD));
        MG_CHECK(carrier->CarryingRobot() == (hit_at == 1000));
        g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = 1;
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {100000, 0, 0};
        MG_CHECK(robot->Damage(WEAPON_LASER, robot->GetGeoCenter(), D3DXVECTOR3(0, 0, 1), 2, nullptr));
        MG_CHECK(robot_core->m_Object == robot && robot->m_CurrState == ROBOT_DIP);
        MG_CHECK(carrier_core->m_Object == carrier && !carrier->CarryingRobot());
        MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
        const int eliminated_at = hit_at == 1000 ? (step == 10 ? 2020 : 2032) : (step == 10 ? 8050 : 8080);
        advance_to(session, eliminated_at - step);
        MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->IsPaused());
        session.advance();
        MG_CHECK(side().GetStatus() == SS_NONE && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        MG_CHECK(!g_MatrixMap->m_DialogModeName);
        advance_to(session, eliminated_at + (step == 10 ? 2010 : 2016) - step);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        session.advance();
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
    }
}
void carrier_loss_outcome(bool damage) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int remove_at : {1000, 7000})
    for (int step : {10, 16}) {
        tests::BattleMaterialProfile materials;
        tests::BattleCombatAssets assets;
        CaptureCraterAssets crater;
        Session session(transport_outcome_profile(mode, step));
        assets.install();
        crater.install();
        DialogData dialog;
        std::cout << (damage ? "hit_at=" : "remove_at=") << remove_at << ' ' << session.metadata(damage ? "synthetic-carrier-damage-outcome-v2" : "synthetic-carrier-removal-outcome-v2") << '\n';
        auto *carrier = order_outcome_transport(PLAYER_SIDE);
        auto carrier_core = retain_core(*carrier);
        auto *robot = carrier->GetCarryingRobot()->AsRobot();
        auto robot_core = retain_core(*robot);
        MG_CHECK(session.remove(1) && session.remove(2));
        advance_to(session, ((remove_at + step - 1) / step) * step);
        MG_CHECK(robot_core->m_Object == robot && carrier_core->m_Object == carrier);
        MG_CHECK(robot->m_CurrState == (remove_at == 1000 ? ROBOT_CARRYING : ROBOT_SUCCESSFULLY_BUILD));
        if (damage) {
            const auto position = carrier->GetPos();
            MG_CHECK(carrier->GetHitPoint() == 1000);
            g_Config.m_FlyerDamages[Weap2Index(WEAPON_LASER)] = {1, 0, 0};
            MG_CHECK(!carrier->Damage(WEAPON_LASER, position, D3DXVECTOR3(0, 0, 1), 2, nullptr));
            MG_CHECK(carrier_core->m_Object == carrier && carrier->GetHitPoint() == 999);
            MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->m_DialogModeName);
            g_Config.m_FlyerDamages[Weap2Index(WEAPON_LASER)] = {999, 0, 0};
            MG_CHECK(carrier->Damage(WEAPON_LASER, position, D3DXVECTOR3(0, 0, 1), 2, nullptr));
        } else {
            g_MatrixMap->StaticDelete(carrier);
        }
        MG_CHECK(carrier_core->m_Object == nullptr && g_MatrixMap->m_AD_Obj_cnt == 0);
        MG_CHECK(robot_core->m_Object == robot && robot->IsMustDie() == (remove_at == 1000));
        MG_CHECK(robot->m_CurrState == (remove_at == 1000 ? ROBOT_FALLING : ROBOT_SUCCESSFULLY_BUILD));
        session.advance();
        MG_CHECK(robot_core->m_Object == robot);
        MG_CHECK(robot->m_CurrState == (remove_at == 1000 ? ROBOT_DIP : ROBOT_SUCCESSFULLY_BUILD));
        if (remove_at == 1000) {
            advance_to(session, step == 10 ? 2010 : 2016);
            MG_CHECK(side().GetStatus() == SS_ACTIVE);
            session.advance();
            MG_CHECK(side().GetStatus() == SS_NONE && !g_MatrixMap->m_DialogModeName);
            advance_to(session, (step == 10 ? 4030 : 4048) - step);
            MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
            session.advance();
            MG_CHECK(g_MatrixMap->IsPaused());
            MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
        } else {
            advance_to(session, 10000);
            MG_CHECK(robot_core->m_Object == robot && robot->IsLiveRobot());
            MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->m_DialogModeName);
        }
        MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0 && g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    }
}
void carrier_removal_outcome() { carrier_loss_outcome(false); }
void carrier_damage_outcome() { carrier_loss_outcome(true); }
struct TransportHit {
    int calls = 0;
    CMatrixMapStatic *target = nullptr;
    static void record(CMatrixMapStatic *target, const D3DXVECTOR3 &, uintptr_t context, DWORD) {
        auto &hit = *reinterpret_cast<TransportHit *>(context);
        ++hit.calls;
        hit.target = target;
    }
};
struct TransportWeaponDelete { void operator()(CMatrixEffectWeapon *weapon) const { weapon->Release(); } };
std::vector<std::string> transport_projectile_run(random::Mode mode, int step, bool hit_cargo, bool after_landing) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session(transport_outcome_profile(mode, step));
    assets.install();
    crater.install();
    DialogData dialog;
    std::cout << session.metadata("transport-projectile-outcome-v1") << " cargo=" << hit_cargo
              << " after_landing=" << after_landing << '\n';
    auto *carrier = order_outcome_transport(PLAYER_SIDE, true);
    auto carrier_core = retain_core(*carrier);
    auto *cargo = carrier->GetCarryingRobot()->AsRobot();
    auto cargo_core = retain_core(*cargo);
    MG_CHECK(session.remove(1) && session.remove(2));
    const int fire_at = after_landing ? 7000 : 1000;
    advance_to(session, ((fire_at + step - 1) / step) * step);
    MG_CHECK(carrier_core->m_Object == carrier && cargo_core->m_Object == cargo);
    MG_CHECK(cargo->GetSide() == PLAYER_SIDE && carrier->GetSide() == PLAYER_SIDE);
    MG_CHECK(cargo->m_CurrState == (after_landing ? ROBOT_SUCCESSFULLY_BUILD : ROBOT_CARRYING));
    CMatrixRobotAI *attacker = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && session.matches(3, object->AsRobot())) attacker = object->AsRobot();
    MG_CHECK(attacker != nullptr);
    auto *target = hit_cargo ? static_cast<CMatrixMapStatic *>(cargo) : static_cast<CMatrixMapStatic *>(carrier);
    target->RNeed(MR_Graph | MR_Matrix);
    auto target_core = retain_core(*target);
    D3DXVECTOR3 local_center(0, 0, 10), aim;
    D3DXVec3TransformCoord(&aim, &local_center, &target_core->m_Matrix);
    D3DXVECTOR3 start = aim + D3DXVECTOR3(40, 40, 0), direction = aim - start;
    D3DXVec3Normalize(&direction, &direction);
    D3DXVECTOR3 hit_position;
    MG_CHECK(g_MatrixMap->Trace(&hit_position, start, aim, TRACE_ALL, attacker) == target);
    const int wi = Weap2Index(WEAPON_PLASMA);
    g_Config.m_WeaponRadius[wi] = 200;
    g_Config.m_WeaponCooldown[wi] = 1000;
    g_Config.m_FlyerDamages[wi] = {2000, 0, 0};
    g_Config.m_RobotDamages[wi] = {2000, 0, 0};
    g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = 1;
    TransportHit hit;
    std::unique_ptr<CMatrixEffectWeapon, TransportWeaponDelete> weapon(static_cast<CMatrixEffectWeapon *>(
        CMatrixEffect::CreateWeapon(start, direction, reinterpret_cast<uintptr_t>(&hit), TransportHit::record, WEAPON_PLASMA)));
    MG_CHECK(weapon != nullptr);
    weapon->SetOwner(attacker);
    auto attacker_core = retain_core(*attacker);
    MG_CHECK(weapon->GetOwner() == attacker && weapon->GetSideStorage() == 2);
    weapon->FireBegin(D3DXVECTOR3(0, 0, 0), attacker);
    weapon->Takt(1);
    weapon->FireEnd();
    MG_CHECK(weapon->GetFireCount() == 1 && hit.calls == 0);
    std::vector<std::string> trace;
    auto record = [&] {
        MG_CHECK(!carrier_core->m_Object || carrier_core->m_Object == carrier);
        MG_CHECK(!cargo_core->m_Object || cargo_core->m_Object == cargo);
        MG_CHECK(attacker_core->m_Object == attacker && hit.calls <= 1);
        MG_CHECK(attacker->IsLiveRobot());
        if (cargo_core->m_Object) MG_CHECK(cargo->GetSide() == PLAYER_SIDE);
        if (hit.calls && carrier_core->m_Object) MG_CHECK(!carrier->CarryingRobot());
        trace.push_back(session.snapshot() + ":carrier=" + std::to_string(carrier_core->m_Object != nullptr)
            + ":cargo=" + std::to_string(cargo_core->m_Object ? cargo->m_CurrState : -1)
            + ":hits=" + std::to_string(hit.calls));
    };
    int impact = 0;
    while (session.time() < fire_at + 1000 && !impact) {
        session.advance();
        record();
        if (hit.calls) impact = session.time();
        MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->IsPaused());
        MG_CHECK(!g_MatrixMap->m_DialogModeName);
    }
    std::cout << "impact=" << impact << " callbacks=" << hit.calls << '\n';
    MG_CHECK(impact > fire_at && hit.calls == 1 && hit.target == TRACE_STOP_NONE);
    const int expected_impact = !after_landing ? (step == 10 ? 1080 : 1088)
        : !hit_cargo ? (step == 10 ? 7080 : 7088)
        : step == 10 ? 7100 : mode == random::Mode::ParkMiller ? 7120 : 7104;
    MG_CHECK(impact == expected_impact);
    if (hit_cargo) {
        MG_CHECK(cargo_core->m_Object == cargo && cargo->m_CurrState == ROBOT_DIP);
        MG_CHECK(carrier_core->m_Object == carrier && !carrier->CarryingRobot());
    } else {
        MG_CHECK(carrier_core->m_Object == nullptr && g_MatrixMap->m_AD_Obj_cnt == 0);
        MG_CHECK(cargo_core->m_Object == cargo);
        MG_CHECK(cargo->m_CurrState == (after_landing ? ROBOT_SUCCESSFULLY_BUILD : ROBOT_FALLING));
        MG_CHECK(cargo->IsMustDie() == !after_landing);
    }
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == int(hit_cargo));
    const bool loss = hit_cargo || !after_landing;
    const int elimination = after_landing ? (step == 10 ? 8050 : 8080) : (step == 10 ? 2020 : 2032);
    const int deadline = elimination + (step == 10 ? 2010 : 2016);
    while (session.time() < deadline) {
        session.advance();
        record();
        if (loss && cargo_core->m_Object) MG_CHECK(cargo->m_CurrState == ROBOT_DIP);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatus() == SS_ACTIVE);
        MG_CHECK((side().GetStatus() == SS_NONE) == (loss && session.time() >= elimination));
        MG_CHECK(g_MatrixMap->IsPaused() == (loss && session.time() == deadline));
    }
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(hit.calls == 1 && weapon->GetFireCount() == 1);
    MG_CHECK(bool(carrier_core->m_Object) == (hit_cargo && (!after_landing || mode == random::Mode::ParkMiller)));
    if (loss) MG_CHECK(g_MatrixMap->m_DialogModeName &&
                       std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
    else MG_CHECK(cargo_core->m_Object == cargo && cargo->IsLiveRobot() && !g_MatrixMap->m_DialogModeName);
    MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == int(hit_cargo));
    return trace;
}
std::string transport_context_rejection_run(random::Mode mode, int step, bool after_landing) {
    tests::BattleMaterialProfile materials;
    Session session(transport_outcome_profile(mode, step));
    std::cout << session.metadata("transport-context-rejection-v1") << " after_landing=" << after_landing << '\n';
    auto *carrier = order_outcome_transport(2, true);
    auto carrier_core = retain_core(*carrier);
    auto *cargo = carrier->GetCarryingRobot()->AsRobot();
    auto cargo_core = retain_core(*cargo);
    advance_to(session, after_landing ? (step == 10 ? 7000 : 7008) : (step == 10 ? 1000 : 1008));
    MG_CHECK(carrier_core->m_Object == carrier && cargo_core->m_Object == cargo);
    MG_CHECK(carrier->GetSide() == 2 && cargo->GetSide() == PLAYER_SIDE);
    MG_CHECK(cargo->m_CurrState == (after_landing ? ROBOT_SUCCESSFULLY_BUILD : ROBOT_CARRYING));
    CMatrixRobotAI *selected = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && session.matches(1, object->AsRobot())) selected = object->AsRobot();
    MG_CHECK(selected != nullptr);
    auto *selection = side().GetCurSelGroup();
    selection->RemoveAll();
    selection->AddObject(selected, -4);
    side().SetCurGroup(selection);
    side().m_CurrSel = ROBOT_SELECTED;
    g_MatrixMap->m_TraceStopObj = carrier;
    g_MatrixMap->m_TraceStopPos = carrier->GetGeoCenter();
    side().OnRButtonDown(CPoint(0, 0));
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
    const int group = selected->GetGroupLogic();
    MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
    // Flyers are not IsLiveUnit targets: context input falls back to a ground Move.
    MG_CHECK(side().m_PlayerGroup[group].Order() == mpo_MoveTo);
    MG_CHECK(side().m_PlayerGroup[group].m_Obj == nullptr);
    MG_CHECK(carrier_core->m_Object == carrier && cargo_core->m_Object == cargo);
    MG_CHECK(carrier->GetHitPoint() == 1000 && cargo->IsLiveRobot());
    MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->m_DialogModeName);
    return session.snapshot();
}
void transport_context_rejection() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool after_landing : {false, true}) {
        const auto first = transport_context_rejection_run(mode, step, after_landing);
        MG_CHECK(first == transport_context_rejection_run(mode, step, after_landing));
    }
}
enum class TransportOpponent { Delivery, EmptyCarrier, GroundRobot };
std::vector<std::string> transport_ai_acquisition_run(random::Mode mode, int step, TransportOpponent opponent) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    Session session(transport_outcome_profile(mode, step));
    assets.install();
    DialogData dialog;
    MG_CHECK(session.remove(1) && session.remove(4));
    CMatrixRobotAI *attacker = nullptr, *sentinel = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) {
            if (session.matches(3, object->AsRobot())) attacker = object->AsRobot();
            if (session.matches(2, object->AsRobot())) sentinel = object->AsRobot();
        }
    MG_CHECK(attacker && sentinel);
    auto attacker_core = retain_core(*attacker);
    auto sentinel_core = retain_core(*sentinel);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 500;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {1, 0, 0};
    // One heat unit per emission, no cooling or overheat within this bounded run.
    // IsFireWas() is a consuming query used by production; never sample it here.
    g_Config.m_Overheat[WEAPON_LASER_HEAT] = 1;
    g_Config.m_Overheat[WEAPON_LASER_COOL] = 0;
    g_Config.m_Overheat[WCP_LASER] = 100;
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = 1;
    tests::arm_battle_robot(*attacker, RUK_WEAPON_LASER);
    auto place = [](CMatrixRobotAI &robot, float x, float y) {
        robot.m_PosX = x; robot.m_PosY = y;
        robot.SetMaxSpeed(0); robot.m_GroupSpeed = 0;
        robot.MapPosCalc(); robot.RChange(MR_Matrix); robot.RNeed(MR_Matrix);
        robot.m_CalcBoundsLastTime = -100; robot.JoinToGroup();
    };
    place(*attacker, 80, 100);
    // Keep the player active in the empty-carrier control, outside perception range.
    place(*sentinel, 600, 600);
    const float sentinel_health = sentinel->GetHitPoint();
    auto *carrier = order_outcome_transport(PLAYER_SIDE, true);
    auto carrier_core = retain_core(*carrier);
    auto *cargo = carrier->GetCarryingRobot()->AsRobot();
    auto cargo_core = retain_core(*cargo);
    MG_CHECK(cargo->m_AimProtect == 0 && carrier->GetSide() != attacker->GetSide());
    CMatrixRobotAI *target = cargo;
    if (opponent != TransportOpponent::Delivery) {
        g_MatrixMap->StaticDelete(cargo); // Initial negative-control fixture, before the first tick.
        MG_CHECK(!cargo_core->m_Object && !carrier->CarryingRobot());
        target = nullptr;
        if (opponent == TransportOpponent::GroundRobot) {
            target = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 200, 300);
            target->InitMaxHitpoint(1500);
            place(*target, 200, 300);
        }
    }
    auto target_core = target ? retain_core(*target) : OutcomeCore(nullptr, [](SObjectCore *core) { core->Release(); });
    if (target) MG_CHECK(target->m_AimProtect == 0);
    const float health = target ? target->GetHitPoint() : 0;
    int acquired = 0, targeted = 0, fired = 0, emitted = 0, damaged = 0, released = 0, landed = 0;
    int previous_heat = 0;
    float max_attacker_displacement = 0, max_sentinel_displacement = 0;
    std::vector<std::string> trace;
    std::cout << session.metadata("transport-ai-acquisition-v1") << " opponent=" << int(opponent) << std::endl;
    auto role = [&](CMatrixMapStatic *object) {
        if (!object) return 0;
        if (object == target) return 1;
        if (object == carrier) return 2;
        if (object == sentinel) return 3;
        MG_CHECK(false); // No unclassified or stale target is accepted as null.
        return -1;
    };
    while (session.time() < (opponent == TransportOpponent::Delivery ? 12000 : 2000)) {
        session.advance();
        MG_CHECK(!carrier_core->m_Object || carrier_core->m_Object == carrier);
        MG_CHECK(attacker_core->m_Object == attacker && sentinel_core->m_Object == sentinel);
        MG_CHECK(attacker->IsLiveRobot() && sentinel->IsLiveRobot());
        max_attacker_displacement = std::max(max_attacker_displacement,
            std::max(std::abs(attacker->m_PosX - 80), std::abs(attacker->m_PosY - 100)));
        max_sentinel_displacement = std::max(max_sentinel_displacement,
            std::max(std::abs(sentinel->m_PosX - 600), std::abs(sentinel->m_PosY - 600)));
        MG_CHECK(sentinel->GetHitPoint() == sentinel_health);
        MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        auto &env = *attacker->GetEnv();
        MG_CHECK(!env.SearchEnemy(carrier) && env.m_TargetAttack != carrier && env.m_Target != carrier);
        MG_CHECK(!env.SearchEnemy(sentinel));
        if (target) {
            MG_CHECK(target_core->m_Object == target && target->IsLiveRobot());
            MG_CHECK(env.m_EnemyCnt == int(env.SearchEnemy(target) != nullptr));
            if (!acquired && env.SearchEnemy(target)) acquired = session.time();
            if (!targeted && env.m_TargetAttack == target) targeted = session.time();
            if (!damaged && target->GetHitPoint() < health) damaged = session.time();
            if (opponent == TransportOpponent::Delivery) {
                MG_CHECK(cargo->m_CurrState == ROBOT_CARRYING || cargo->m_CurrState == ROBOT_FALLING ||
                         cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
                if (carrier_core->m_Object)
                    MG_CHECK(carrier->CarryingRobot() == (cargo->m_CurrState == ROBOT_CARRYING));
                if (!released && cargo->m_CurrState != ROBOT_CARRYING) released = session.time();
                if (!landed && cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) landed = session.time();
                if (landed) MG_CHECK(cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
            }
        }
        if (!fired && attacker->FindOrderLikeThat(ROT_FIRE)) fired = session.time();
        const int heat = attacker->GetWeapon(0).m_Heat;
        MG_CHECK(heat >= previous_heat && heat < WEAPON_MAX_HEAT);
        if (!emitted && heat > 0) emitted = session.time();
        previous_heat = heat;
        std::ostringstream frame;
        frame << session.snapshot() << ":enemy=" << env.m_EnemyCnt << ":attack=" << role(env.m_TargetAttack)
              << ":target=" << role(env.m_Target) << ":heat=" << heat;
        auto vector = [&](const D3DXVECTOR3 &value) {
            frame << ':' << std::bit_cast<uint32_t>(value.x) << ',' << std::bit_cast<uint32_t>(value.y)
                  << ',' << std::bit_cast<uint32_t>(value.z);
        };
        for (int i = 0; i < attacker->GetOrdersInPool(); ++i) {
            const auto *order = attacker->GetOrder(i);
            if (order->m_OrderType != ROT_FIRE) continue;
            MG_CHECK(target && env.m_Target == target && target_core->m_Object == target);
            const auto aim = target->GetGeoCenter() + D3DXVECTOR3(0, 0, 5);
            MG_CHECK(order->m_Param1 == aim.x && order->m_Param2 == aim.y && order->m_Param3 == aim.z);
            vector(aim);
        }
        if (target) {
            vector(target->GetGeoCenter());
            frame << ":health=" << std::bit_cast<uint32_t>(target->GetHitPoint()) << ":state=" << target->m_CurrState;
        }
        frame << ":carrier=" << bool(carrier_core->m_Object);
        if (carrier_core->m_Object) {
            vector(carrier->GetPos());
            frame << ":carrying=" << carrier->CarryingRobot() << ":health=" << std::bit_cast<uint32_t>(carrier->GetHitPoint());
        }
        trace.push_back(frame.str());
    }
    std::cout << "acquired=" << acquired << " targeted=" << targeted << " fired=" << fired << " damaged=" << damaged
              << " released=" << released << " landed=" << landed << " health=" << (target ? target->GetHitPoint() : 0)
              << " carrier=" << bool(carrier_core->m_Object) << " emitted=" << emitted << " heat=" << previous_heat
              << " attacker_displacement=" << max_attacker_displacement << " sentinel_displacement=" << max_sentinel_displacement << std::endl;
    if (target) {
        MG_CHECK(acquired && targeted && fired && emitted && damaged);
        MG_CHECK(acquired == (100 / step + 1) * step && targeted == acquired + step);
        MG_CHECK(targeted == fired && fired < emitted && emitted == damaged);
        if (opponent == TransportOpponent::Delivery) {
            MG_CHECK(targeted < released && released < landed && landed < emitted && emitted < landed + 1000);
            MG_CHECK(!carrier_core->m_Object && cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        } else {
            MG_CHECK(emitted < 1000);
        }
    } else {
        MG_CHECK(!acquired && !targeted && !fired && !emitted && !damaged && previous_heat == 0);
        MG_CHECK(carrier_core->m_Object == carrier && attacker->GetEnv()->m_EnemyCnt == 0);
    }
    MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    return trace;
}
void transport_ai_acquisition() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (auto opponent : {TransportOpponent::Delivery, TransportOpponent::EmptyCarrier, TransportOpponent::GroundRobot}) {
        const auto first = transport_ai_acquisition_run(mode, step, opponent);
        MG_CHECK(first == transport_ai_acquisition_run(mode, step, opponent));
    }
}
void transport_projectile_outcomes() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool cargo : {false, true}) for (bool after_landing : {false, true}) {
        const auto first = transport_projectile_run(mode, step, cargo, after_landing);
        MG_CHECK(first == transport_projectile_run(mode, step, cargo, after_landing));
    }
}
std::vector<std::string> capture_target_damage_run(random::Mode mode, int step, bool base) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 1500, false});
    std::cout << session.metadata("capture-target-damage-v2") << '\n';
    assets.install();
    crater.install();
    const auto kind = base ? BUILDING_BASE : BUILDING_TITAN;
    const auto texture = std::wstring(OBJECT_PATH_BUILDINGS_RUINS) + L"b" + std::to_wstring(int(kind));
    tests::install_battle_texture(texture.c_str());
    tests::load_battle_material_mesh({}, (texture + L".vo").c_str(), texture.c_str());
    if (!base) {
        const auto secondary = texture + L"?Trans";
        tests::install_battle_texture(secondary.c_str());
        tests::load_battle_material_mesh({}, (texture + L"p.vo").c_str(), secondary.c_str());
    }
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 20;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_CannonDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    auto *building = register_building(0, kind, base ? 100 : 160, base ? 100 : 200);
    auto *capturer = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    g_MatrixMap->m_TraceStopObj = building;
    side().OnRButtonDown(CPoint(0, 0));
    const int group = capturer->GetGroupLogic();
    MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
    advance_to(session, base ? 2000 : step == 10 ? 1000 : 1008);
    MG_CHECK(capturer->GetCaptureFactory() == building && building->m_Capturer == capturer);
    if (base) MG_CHECK(building->m_State == BASE_OPENED && capturer->IsDisableManual());
    else MG_CHECK(building->m_TrueColor.m_ColoredCnt == 1);
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(building->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_LASER)] = {10000, 0, 0};
    MG_CHECK(building->Damage(WEAPON_LASER, building->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
    MG_CHECK(building->m_State == BUILDING_DIP);
    MG_CHECK(capturer->GetBase() == nullptr && capturer->IsMustDie() == base);
    MG_CHECK(capturer->GetCaptureFactory() == nullptr && !capturer->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_BUILDING_KILL) == 1);
    const int removed_at = base ? (step == 10 ? 5010 : 5008) : (step == 10 ? 2010 : 2016);
    const int cleared_at = base ? (step == 10 ? 2010 : 2032) : (step == 10 ? 1010 : 1024);
    MG_CHECK(side().m_PlayerGroup[group].m_Obj == building);
    int removed = 0, target_cleared = 0;
    std::vector<std::string> trace;
    while (session.time() < 10000) {
        session.advance();
        if (session.time() == removed_at - step) MG_CHECK(core->m_Object != nullptr);
        if (session.time() == cleared_at - step) MG_CHECK(side().m_PlayerGroup[group].m_Obj == building);
        if (!removed && !core->m_Object) removed = session.time();
        if (!target_cleared && side().m_PlayerGroup[group].m_Obj == nullptr) target_cleared = session.time();
        if (core->m_Object) MG_CHECK(building->GetSide() == 0);
        trace.push_back(session.snapshot() + ":" + std::to_string(core->m_Object != nullptr));
    }
    std::cout << "capture target base=" << base << " removed=" << removed << " target=" << target_cleared
              << " alive=" << session.robot(1).alive << '\n';
    MG_CHECK(removed == removed_at && target_cleared == cleared_at);
    MG_CHECK(side().m_PlayerGroup[group].m_Obj == nullptr && side().m_PlayerGroup[group].Order() == mpo_Stop);
    MG_CHECK(session.robot(1).alive == !base);
    MG_CHECK(core->m_Object == nullptr);
    MG_CHECK(session.robot(2).alive && !g_MatrixMap->IsPaused());
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_BUILDING_KILL) == 1);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    return trace;
}
void capture_target_damage() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) for (bool base : {false, true}) {
        const auto first = capture_target_damage_run(mode, step, base);
        MG_CHECK(first == capture_target_damage_run(mode, step, base));
    }
}
std::wstring objective_descriptor(const std::wstring &behavior) {
    std::array<std::wstring, OTP_COUNT> fields{};
    fields[OTP_PATH] = L"synthetic-objective/";
    fields[OTP_VO] = L"box";
    fields[OTP_TEXTURE] = L"surface";
    fields[OTP_TEXTURE_SCROLL] = L"0,0";
    fields[OTP_SHADOW] = L"Off";
    fields[OTP_BEHAVIOUR] = behavior;
    fields[OTP_BIAS] = L"0";
    fields[OTP_INVLOGIC] = L"0";
    std::wstring result;
    for (const auto &field : fields) {
        if (!result.empty()) result += L"*";
        result += field;
    }
    return result;
}
void install_objective_descriptors(std::span<const std::wstring> behaviors, bool terron) {
    MG_CHECK(g_MatrixMap->m_IdsCnt == 1);
    const auto map_name = g_MatrixMap->m_Ids[0];
    g_MatrixMap->m_Ids[0].~basic_string();
    HFree(g_MatrixMap->m_Ids, g_MatrixHeap);
    std::vector<std::wstring> entries;
    for (const auto &behavior : behaviors) entries.push_back(objective_descriptor(behavior));
    entries.push_back(map_name);
    g_MatrixMap->m_Ids = static_cast<std::wstring *>(HAlloc(entries.size() * sizeof(std::wstring), g_MatrixHeap));
    g_MatrixMap->m_IdsCnt = 0;
    for (const auto &entry : entries) {
        new(g_MatrixMap->m_Ids + g_MatrixMap->m_IdsCnt) std::wstring(entry);
        ++g_MatrixMap->m_IdsCnt;
    }
    tests::install_battle_texture(L"synthetic-objective/surface");
    const wchar_t *terron_animations[] = {L"Main", L"Pain", L"Death"};
    tests::load_battle_material_mesh({}, L"synthetic-objective/box.vo", L"synthetic-objective/surface", false,
        terron ? std::span<const wchar_t *const>(terron_animations) : std::span<const wchar_t *const>{});
}
void install_objective_catalog(const std::wstring &behavior, bool terron = false) {
    const std::array<std::wstring, 2> behaviors{behavior, L"Static"};
    install_objective_descriptors(behaviors, terron);
}
CMatrixMapObject *create_objective(float x, int type = 0, bool special = true) {
    auto *object = g_MatrixMap->StaticAdd<CMatrixMapObject>();
    object->Init(type);
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(object->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    core->m_Matrix._41 = x;
    core->m_Matrix._42 = 100;
    core->m_Matrix._43 = g_MatrixMap->GetZ(x, 100);
    object->RNeed(MR_Matrix | MR_Graph);
    object->JoinToGroup();
    MG_CHECK(object->IsSpecial() == special && object->m_BreakHitPoint == 10);
    return object;
}
std::vector<std::string> special_object_run(random::Mode mode, int family, int step) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 1500, false});
    assets.install();
    crater.install();
    DialogData dialog;
    std::cout << session.metadata("synthetic-special-outcome-v2") << " family=" << family << '\n';
    install_objective_catalog(family == 2 ? L"+Anim,0:0:10:1#1:0:0:-1" :
        family == 1 ? L"+Break,1,10,Explode" : L"+Break,1,10,None");
    auto *first = create_objective(400);
    auto *last = create_objective(450);
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 2);
    const auto hit = [&](CMatrixMapObject *object, int attacker) {
        object->Damage(WEAPON_LASER, object->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), attacker, nullptr);
    };
    auto &damage = g_Config.m_ObjectDamages[Weap2Index(WEAPON_LASER)];
    damage = {5, 0, 5};
    const auto draws = random::draw_count();
    hit(first, 2);
    MG_CHECK(first->m_BreakHitPoint == 10 && first->m_PB == nullptr);
    MG_CHECK(random::draw_count() == draws && g_MatrixMap->m_BeforeWinCount == 2);
    hit(first, PLAYER_SIDE);
    MG_CHECK(first->m_BreakHitPoint == 5 && first->m_PB != nullptr);
    hit(first, PLAYER_SIDE);
    MG_CHECK(first->m_BreakHitPoint == 5 && first->IsSpecial());
    damage.mindamage = 4;
    const int effects_before_break = g_MatrixMap->GetEffectCount();
    hit(first, PLAYER_SIDE);
    if (family == 1) MG_CHECK(g_MatrixMap->GetEffectCount() > effects_before_break);
    else MG_CHECK(g_MatrixMap->GetEffectCount() == effects_before_break);
    MG_CHECK(!first->IsSpecial() && g_MatrixMap->m_BeforeWinCount == 1);
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 1);
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN));
    MG_CHECK(first->m_PB == nullptr);
    if (family == 2) MG_CHECK(first->m_AnimState == 1 && first->m_BreakHitPoint == 2000000000);
    else MG_CHECK(first->m_Type == 1 && first->m_BehFlag == BEHF_STATIC);
    // Losing the enemy army is insufficient while one objective remains.
    MG_CHECK(session.remove(3) && session.remove(4));
    advance_to(session, step == 10 ? 2020 : 2032);
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) && !g_MatrixMap->IsPaused());
    MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
    damage = {10, 0, 0};
    hit(last, PLAYER_SIDE);
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 0 && !last->IsSpecial());
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN));
    MG_CHECK(side().GetStatus() == SS_JUST_WIN && side().GetStatValue(STAT_BUILDING_KILL) == 2);
    hit(last, PLAYER_SIDE);
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 0 && side().GetStatValue(STAT_BUILDING_KILL) == 2);
    // The earned objective victory survives loss of every player robot.
    MG_CHECK(session.remove(1) && session.remove(2));
    std::vector<std::string> trace;
    while (!g_MatrixMap->IsPaused() && session.time() < 7000) {
        MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
        session.advance();
        trace.push_back(session.snapshot() + ":" + std::to_string(side().GetStatus()) + ":" +
            std::to_string(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN)));
    }
    std::cout << "objective family=" << family << " result=" << session.time() << '\n';
    MG_CHECK(session.time() == (step == 10 ? 6040 : 6064));
    MG_CHECK(g_MatrixMap->IsPaused() && FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN);
    MG_CHECK(side().GetStatus() == SS_ACTIVE && !FLAG(g_Flags, GFLAG_EXITLOOP));
    return trace;
}
template<int Family> void special_object_destruction() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = special_object_run(mode, Family, step);
        MG_CHECK(first == special_object_run(mode, Family, step));
    }
}
enum class ObjectiveCommandRoute { Context, Keyboard, Button };
void objective_preorder(CFormMatrixGame &form, ObjectiveCommandRoute route, bool bomb, bool capture = false) {
    MG_CHECK(route != ObjectiveCommandRoute::Context);
    if (route == ObjectiveCommandRoute::Keyboard) press(form, capture ? 'K' : bomb ? 'E' : 'A');
    else {
        auto button = tests::make_engine_object<CIFaceElement>();
        button->m_strName = capture ? IF_ORDER_CAPTURE : bomb ? IF_ORDER_BOMB : IF_ORDER_FIRE;
        g_IFaceList->PlayerAction(button.get());
    }
    const DWORD flag = capture ? PREORDER_CAPTURE : bomb ? PREORDER_BOMB : PREORDER_FIRE;
    MG_CHECK((g_IFaceList->m_IfListFlags & (flag | ORDERING_MODE)) == (flag | ORDERING_MODE));
}
void objective_click(CMatrixMapStatic &object, ObjectiveCommandRoute route) {
    g_MatrixMap->m_TraceStopObj = &object;
    g_MatrixMap->m_TraceStopPos = object.GetGeoCenter();
    if (route == ObjectiveCommandRoute::Context) side().OnRButtonDown(CPoint(0, 0));
    else side().OnLButtonDown(CPoint(0, 0));
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
}
std::vector<std::string> special_command_run(random::Mode mode, int step, bool animation, bool bomb,
                                            ObjectiveCommandRoute route, bool stop = false) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 80, step, 6000, false, true});
    assets.install(); crater.install();
    DialogData dialog;
    CFormMatrixGame form;
    std::cout << session.metadata("special-object-command-v1") << " animation=" << animation
              << " bomb=" << bomb << " route=" << int(route) << " stop=" << stop << std::endl;
    install_objective_catalog(animation ? L"+Anim,0:0:10:1#1:0:0:-1" : L"+Break,1,10,None");
    auto *objective = create_objective(400);
    auto *spare = create_objective(600);
    auto objective_core = retain_core(*objective);
    auto spare_core = retain_core(*spare);
    auto *actor = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    auto actor_core = retain_core(*actor);
    const int wi = Weap2Index(bomb ? WEAPON_BIGBOOM : WEAPON_LASER);
    g_Config.m_WeaponRadius[wi] = bomb ? 150 : 120;
    g_Config.m_WeaponCooldown[wi] = 100;
    g_Config.m_ObjectDamages[wi] = {bomb ? 10 : 5, 0, 0};
    g_Config.m_RobotDamages[wi] = {};
    g_Config.m_Overheat[WEAPON_LASER_HEAT] = 1;
    g_Config.m_Overheat[WEAPON_LASER_COOL] = 0;
    g_Config.m_Overheat[WCP_LASER] = 100;
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    tests::arm_battle_robot(*actor, bomb ? RUK_WEAPON_BOMB : RUK_WEAPON_LASER);
    side().CreateGroupFromCurrent(actor);
    side().Select(ROBOT, nullptr);
    MG_CHECK(objective->InLT() && objective->IsSpecial() && !objective->IsLive());
    const auto before = session.snapshot();
    if (route != ObjectiveCommandRoute::Context) {
        objective_preorder(form, route, bomb);
        press(form, 'X');
        objective_click(*objective, route);
        MG_CHECK(!IS_PREORDERING && session.snapshot() == before && !actor->IsMustDie());
        objective_preorder(form, route, bomb);
    }
    objective_click(*objective, route);
    MG_CHECK(!IS_PREORDERING);
    int group = actor->GetGroupLogic();
    MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
    MG_CHECK(side().m_PlayerGroup[group].Order() == (bomb ? mpo_Bomb : mpo_Attack));
    // Q: input accepts IsSpecial, but synchronous TaktPL validates IsLive only.
    // Keep this characterization distinct from a corrected gameplay contract.
    MG_CHECK(side().m_PlayerGroup[group].m_Obj == nullptr);
    const auto destination = side().m_PlayerGroup[group].m_To;
    const auto target_center = objective->GetGeoCenter();
    MG_CHECK(destination.x == int(target_center.x / GLOBAL_SCALE_MOVE) &&
             destination.y == int(target_center.y / GLOBAL_SCALE_MOVE));
    int damaged = 0, completed = 0, dying = 0, retired = 0, stopped = 0, requested = 0;
    int stop_at = 0;
    int fire_order_at = 0, emitted_at = 0, heat = 0;
    int first_fire_target = 0, first_emission_target = 0;
    float last_x = 0, last_y = 0;
    std::vector<std::string> trace;
    const auto target_role = [&](CMatrixMapStatic *candidate) {
        if (!candidate) return 0;
        if (candidate == objective) return 100;
        if (candidate == spare) return 101;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object == candidate && object->IsRobot())
                for (EntityId id = 1; id <= 4; ++id) if (session.matches(id, object->AsRobot())) return int(id);
        return -1;
    };
    const auto observe = [&] {
        const auto &order = side().m_PlayerGroup[group];
        auto frame = session.snapshot() + ":hp=" + std::to_string(objective->m_BreakHitPoint) +
            ":special=" + std::to_string(objective->IsSpecial()) + ":type=" + std::to_string(objective->m_Type) +
            ":animation=" + std::to_string(objective->m_AnimState) + ":count=" + std::to_string(g_MatrixMap->m_BeforeWinCount) +
            ":actor=" + std::to_string(actor_core->m_Object != nullptr) + ":order=" + std::to_string(order.Order()) +
            ":null=" + std::to_string(order.m_Obj == nullptr) + ":dest=" + std::to_string(order.m_To.x) + ',' + std::to_string(order.m_To.y);
        if (actor_core->m_Object) {
            MG_CHECK(actor_core->m_Object == actor);
            frame += ":must_die=" + std::to_string(actor->IsMustDie());
            MG_CHECK(actor->GetEnv()->m_Target != objective && actor->GetEnv()->m_Target != spare);
            MG_CHECK(actor->GetEnv()->m_TargetAttack != objective && actor->GetEnv()->m_TargetAttack != spare);
            frame += ":attack_target=" + std::to_string(target_role(actor->GetEnv()->m_TargetAttack));
            frame += ":fire_target=" + std::to_string(target_role(actor->GetEnv()->m_Target));
            if (!bomb) frame += ":heat=" + std::to_string(actor->GetWeapon(0).m_Heat);
            for (int i = 0; i < actor->GetOrdersInPool(); ++i) {
                const auto *fire = actor->GetOrder(i);
                if (fire->m_OrderType == ROT_FIRE) frame += ":aim=" + std::to_string(std::bit_cast<uint32_t>(fire->m_Param1)) + ',' +
                    std::to_string(std::bit_cast<uint32_t>(fire->m_Param2)) + ',' + std::to_string(std::bit_cast<uint32_t>(fire->m_Param3));
            }
        }
        trace.push_back(std::move(frame));
    };
    observe();
    while (session.time() < 30000) {
        if (stop && !stop_at && session.time() >= 500) {
            MG_CHECK(actor_core->m_Object == actor && actor->IsLiveRobot() && !actor->IsMustDie());
            MG_CHECK(std::abs(actor->m_PosX - 100) + std::abs(actor->m_PosY - 200) > 5);
            if (route == ObjectiveCommandRoute::Button) {
                auto button = tests::make_engine_object<CIFaceElement>();
                button->m_strName = IF_ORDER_STOP;
                g_IFaceList->PlayerAction(button.get());
            } else press(form, 'S');
            group = actor->GetGroupLogic();
            MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
            stop_at = session.time();
        }
        session.advance();
        MG_CHECK(objective_core->m_Object == objective && spare_core->m_Object == spare);
        MG_CHECK(!actor_core->m_Object || actor_core->m_Object == actor);
        MG_CHECK(spare->IsSpecial() && spare->m_BreakHitPoint == 10);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(side().GetStatus() == SS_ACTIVE);
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN | MMFLAG_WIN));
        if (!damaged && objective->m_BreakHitPoint != 10) damaged = session.time();
        if (!completed && !objective->IsSpecial()) completed = session.time();
        if (actor_core->m_Object) {
            if (!requested && actor->IsMustDie()) requested = session.time();
            if (!dying && actor->m_CurrState == ROBOT_DIP) dying = session.time();
            if (!bomb) {
                if (!fire_order_at && actor->FindOrderLikeThat(ROT_FIRE)) {
                    fire_order_at = session.time();
                    first_fire_target = target_role(actor->GetEnv()->m_Target);
                }
                heat = actor->GetWeapon(0).m_Heat;
                if (!emitted_at && heat) {
                    emitted_at = session.time();
                    first_emission_target = target_role(actor->GetEnv()->m_Target);
                }
            }
        } else if (!retired) retired = session.time();
        const auto &order = side().m_PlayerGroup[group];
        MG_CHECK(order.m_Obj == nullptr);
        if (!bomb || (stop && stop_at)) {
            MG_CHECK(actor_core->m_Object == actor && actor->IsLiveRobot() && !actor->IsMustDie());
            MG_CHECK(objective->IsSpecial() && objective->m_BreakHitPoint == 10);
            MG_CHECK(order.Order() == (stop_at ? mpo_Stop : mpo_Attack));
            if (!stop_at) MG_CHECK(order.m_To.x == destination.x && order.m_To.y == destination.y);
            if (stop_at && session.time() >= 25000)
                MG_CHECK(actor->m_PosX == last_x && actor->m_PosY == last_y);
            last_x = actor->m_PosX; last_y = actor->m_PosY;
        }
        if (!stopped && order.Order() == mpo_Stop) stopped = session.time();
        observe();
    }
    std::cout << "damaged=" << damaged << " completed=" << completed << " dying=" << dying << " retired=" << retired
              << " stopped=" << stopped << " hp=" << objective->m_BreakHitPoint << " kills=" << side().GetStatValue(STAT_BUILDING_KILL)
              << " before_win=" << g_MatrixMap->m_BeforeWinCount << " pos=" << (actor_core->m_Object ? actor->m_PosX : -1) << std::endl;
    std::cout << "fire_order_at=" << fire_order_at << " emitted_at=" << emitted_at << " heat=" << heat
              << " first_fire_target=" << first_fire_target << " first_emission_target=" << first_emission_target << std::endl;
    MG_CHECK((completed != 0) == (bomb && !stop));
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == (bomb && !stop ? 1 : 2));
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == int(bomb && !stop));
    MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0 && g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    if (bomb && !stop) {
        MG_CHECK(damaged == completed && requested == stopped && stopped && dying > requested && completed > dying && retired > completed);
        MG_CHECK(!actor_core->m_Object && (animation ? objective->m_AnimState == 1 : objective->m_Type == 1));
    } else {
        MG_CHECK(!damaged && !requested && !dying && !retired);
        if (stop) MG_CHECK(stop_at && stopped == stop_at + step);
        else {
            MG_CHECK(!stopped && fire_order_at && emitted_at > fire_order_at && heat > 0);
            MG_CHECK((first_fire_target == 3 || first_fire_target == 4) &&
                     (first_emission_target == 3 || first_emission_target == 4));
            MG_CHECK(std::abs(actor->m_PosX - target_center.x) + std::abs(actor->m_PosY - target_center.y) < 120);
        }
        // Recover player control after the bounded non-completion or canceled bomb.
        press(form, 'S');
        MG_CHECK(session.robot(1).order == mpo_Stop);
        session.ground_command(120, 500);
        group = actor->GetGroupLogic();
        MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
        for (int i = 0; i < 20000 / step + 1; ++i) { session.advance(); observe(); }
        MG_CHECK(actor_core->m_Object == actor && actor->IsLiveRobot());
        MG_CHECK(session.robot(1).in_place && actor->m_PosY > 450 && actor->m_PosX < 200);
        MG_CHECK(objective->IsSpecial() && objective->m_BreakHitPoint == 10 && !g_MatrixMap->IsPaused());
    }
    return trace;
}
template<random::Mode Mode, int Step> void special_attack_routes() {
    for (bool animation : {false, true}) for (auto route : {ObjectiveCommandRoute::Context,
        ObjectiveCommandRoute::Keyboard, ObjectiveCommandRoute::Button}) {
        const auto first = special_command_run(Mode, Step, animation, false, route);
        MG_CHECK(first == special_command_run(Mode, Step, animation, false, route));
    }
}
template<bool Stop, random::Mode Mode, int Step> void special_bomb_routes() {
    for (bool animation : {false, true}) for (auto route : {ObjectiveCommandRoute::Keyboard, ObjectiveCommandRoute::Button}) {
        const auto first = special_command_run(Mode, Step, animation, true, route, Stop);
        MG_CHECK(first == special_command_run(Mode, Step, animation, true, route, Stop));
    }
}
std::vector<std::string> ordinary_object_command_run(random::Mode mode, int step, ObjectiveCommandRoute route,
                                                   bool capture, bool live_control) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    Session session({mode, 80, step, 3000, false, true});
    assets.install();
    CFormMatrixGame form;
    std::cout << session.metadata("ordinary-object-command-v1") << " route=" << int(route)
              << " capture=" << capture << " live_control=" << live_control << std::endl;
    const std::array<std::wstring, 3> descriptors{L"+Break,1,10,None", L"Static", L"Break,1,10,None"};
    install_objective_descriptors(descriptors, false);
    auto *ordinary = create_objective(400, 2, false);
    auto *spare = create_objective(600);
    auto ordinary_core = retain_core(*ordinary);
    auto spare_core = retain_core(*spare);
    auto *actor = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    auto actor_core = retain_core(*actor);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 120;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {1, 0, 0};
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    tests::arm_battle_robot(*actor, RUK_WEAPON_LASER);
    side().CreateGroupFromCurrent(actor);
    side().Select(ROBOT, nullptr);
    CMatrixRobotAI *enemy = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && session.matches(3, object->AsRobot())) enemy = object->AsRobot();
    MG_CHECK(enemy);
    auto enemy_core = retain_core(*enemy);
    if (live_control) {
        MG_CHECK(!capture);
        enemy->m_PosX = 400; enemy->m_PosY = 200;
        enemy->SetMaxSpeed(0); enemy->m_GroupSpeed = 0;
        enemy->InitMaxHitpoint(10000);
        enemy->MapPosCalc(); enemy->RChange(MR_Matrix); enemy->RNeed(MR_Matrix);
        enemy->m_CalcBoundsLastTime = -100; enemy->JoinToGroup();
    }
    const float enemy_health = enemy->GetHitPoint();
    MG_CHECK(!ordinary->IsLive() && !ordinary->IsSpecial() && ordinary->InLT());
    const auto before = session.snapshot();
    if (route != ObjectiveCommandRoute::Context) objective_preorder(form, route, false, capture);
    objective_click(live_control ? static_cast<CMatrixMapStatic &>(*enemy) : *ordinary, route);
    const int group = actor->GetGroupLogic();
    if (capture) {
        MG_CHECK(route != ObjectiveCommandRoute::Context);
        MG_CHECK(IS_PREORDERING && session.snapshot() == before);
        press(form, 'X');
        objective_click(*ordinary, route);
        MG_CHECK(!IS_PREORDERING && session.snapshot() == before);
    } else {
        MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
        const auto &order = side().m_PlayerGroup[group];
        MG_CHECK(!IS_PREORDERING);
        MG_CHECK(order.Order() == (route == ObjectiveCommandRoute::Context && !live_control ? mpo_MoveTo : mpo_Attack));
        MG_CHECK(order.m_Obj == (live_control ? static_cast<CMatrixMapStatic *>(enemy) : nullptr));
        if (!live_control) {
            const auto center = ordinary->GetGeoCenter();
            MG_CHECK(order.m_To.x == Float2Int(center.x / GLOBAL_SCALE_MOVE) - ROBOT_MOVECELLS_PER_SIZE / 2);
            MG_CHECK(order.m_To.y == Float2Int(center.y / GLOBAL_SCALE_MOVE) - ROBOT_MOVECELLS_PER_SIZE / 2);
        }
    }
    std::vector<std::string> trace{session.snapshot()};
    if (!live_control) {
        press(form, 'S');
        session.ground_command(120, 500);
    }
    const int deadline = live_control ? 10000 : 20000;
    while (session.time() < deadline) {
        session.advance();
        MG_CHECK(actor_core->m_Object == actor && actor->IsLiveRobot());
        MG_CHECK(enemy_core->m_Object == enemy && enemy->IsLiveRobot());
        MG_CHECK(ordinary_core->m_Object == ordinary && spare_core->m_Object == spare);
        MG_CHECK(!ordinary->IsSpecial() && ordinary->m_BreakHitPoint == 10);
        MG_CHECK(spare->IsSpecial() && spare->m_BreakHitPoint == 10 && g_MatrixMap->m_BeforeWinCount == 1);
        MG_CHECK(!g_MatrixMap->IsPaused());
        if (live_control) MG_CHECK(side().m_PlayerGroup[group].Order() == mpo_Attack &&
                                   side().m_PlayerGroup[group].m_Obj == enemy);
        trace.push_back(session.snapshot() + ":ordinary=" + std::to_string(ordinary->m_BreakHitPoint));
    }
    if (live_control) MG_CHECK(enemy->GetHitPoint() < enemy_health);
    else MG_CHECK(session.robot(1).in_place && actor->m_PosY > 450 && actor->m_PosX < 200);
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 0 && side().GetStatValue(STAT_ROBOT_KILL) == 0);
    return trace;
}
template<random::Mode Mode, int Step> void ordinary_object_commands() {
    for (auto route : {ObjectiveCommandRoute::Context, ObjectiveCommandRoute::Keyboard, ObjectiveCommandRoute::Button})
    for (int variant : {0, 1, 2}) {
        const bool capture = variant == 1, live_control = variant == 2;
        if (capture && route == ObjectiveCommandRoute::Context) continue;
        const auto first = ordinary_object_command_run(Mode, Step, route, capture, live_control);
        MG_CHECK(first == ordinary_object_command_run(Mode, Step, route, capture, live_control));
    }
}
void terron_destruction_run(bool collateral, int step, int blast_damage = 600) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool lose_army : {false, true}) {
        tests::BattleMaterialProfile materials;
        tests::BattleCombatAssets assets;
        CaptureCraterAssets crater;
        Session session({mode, 85, step, 1500, false});
        assets.install(); crater.install();
        DialogData dialog;
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = collateral ? 150 : 20;
        if (collateral) g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {blast_damage, 0, 7};
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
        install_objective_catalog(L"+Break,1,10,Terron", true);
        auto *terron = create_objective(collateral ? 400 : 600);
        std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> owner_core(terron->GetCore(DEBUG_CALL_INFO),
            [](SObjectCore *value) { value->Release(); });
        MG_CHECK(terron->m_BehFlag == BEHF_TERRON && g_MatrixMap->m_BeforeWinCount == 1);
        std::cout << session.metadata(collateral ? "synthetic-terron-collateral-v2" : "synthetic-terron-outcome-v1") << " lose_army=" << lose_army << " blast_damage=" << blast_damage << '\n';
        const auto hit = [&](int attacker) {
            terron->Damage(WEAPON_LASER, terron->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), attacker, nullptr);
        };
        g_Config.m_ObjectDamages[Weap2Index(WEAPON_LASER)] = {4, 0, 0};
        const auto draws = random::draw_count();
        hit(2);
        MG_CHECK(terron->m_BreakHitPoint == 10 && terron->m_PB == nullptr);
        MG_CHECK(random::draw_count() == draws && !FLAG(g_MatrixMap->m_Flags, MMFLAG_TERRON_DEAD));
        hit(PLAYER_SIDE);
        MG_CHECK(terron->m_BreakHitPoint == 6 && terron->m_PB != nullptr);
        MG_CHECK(terron->m_Graph->IsAnim(L"Pain"));
        g_Config.m_ObjectDamages[Weap2Index(WEAPON_LASER)] = {6, 0, 0};
        hit(PLAYER_SIDE);
        MG_CHECK(terron->m_BreakHitPoint == 0 && terron->m_PB == nullptr);
        MG_CHECK(terron->m_Graph->IsAnim(L"Death"));
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_TERRON_DEAD));
        const auto repeated_hit = [&] {
            const auto before = random::draw_count();
            hit(PLAYER_SIDE);
            MG_CHECK(random::draw_count() == before);
            MG_CHECK(terron->m_Graph->IsAnim(L"Death"));
            MG_CHECK(terron->m_BreakHitPoint == 0 && g_MatrixMap->m_BeforeWinCount == 1);
        };
        repeated_hit();
        if (lose_army) MG_CHECK(session.remove(1) && session.remove(2));
        advance_to(session, 2000);
        repeated_hit(); // Must not restart the remaining death countdown.
        advance_to(session, step == 10 ? 5000 : 4992);
        MG_CHECK(terron->IsSpecial() && terron->m_BehFlag == BEHF_TERRON);
        MG_CHECK(g_MatrixMap->m_BeforeWinCount == 1 && side().GetStatValue(STAT_BUILDING_KILL) == 0);
        MG_CHECK(side().GetStatus() == SS_ACTIVE && !g_MatrixMap->IsPaused());
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        session.advance();
        std::cout << "Terron conversion at " << session.time() << " ms\n";
        MG_CHECK(session.time() == (step == 10 ? 5010 : 5008));
        MG_CHECK(!terron->IsSpecial() && terron->m_Type == 1 && terron->m_BehFlag == BEHF_STATIC);
        MG_CHECK(g_MatrixMap->m_BeforeWinCount == 0 && side().GetStatValue(STAT_BUILDING_KILL) == 1);
        MG_CHECK(side().GetStatus() == SS_JUST_WIN && FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        // Measured against the pre-fix Release implementation. Counts include the
        // complete map tick, not just the damage callback. Army removal uses no RNG.
        const bool park = mode == random::Mode::ParkMiller;
        const bool lethal = blast_damage == 1200;
        const std::array<std::uint64_t, 4> expected_draws = step == 10
            ? (park ? std::array<std::uint64_t, 4>{129201, 131865, lethal ? 132497u : 132031u, lethal ? 186432u : 157146u}
                    : std::array<std::uint64_t, 4>{152426, 157794, lethal ? 158571u : 158108u, lethal ? 238824u : 210111u})
            : (park ? std::array<std::uint64_t, 4>{79842, 80998, lethal ? 81602u : 81136u, lethal ? 113529u : 94537u}
                    : std::array<std::uint64_t, 4>{81944, 84362, lethal ? 85079u : 84616u, lethal ? 127433u : 108444u});
        const auto observe = [&](unsigned checkpoint) {
            const bool after_hit = checkpoint >= 2;
            MG_CHECK(random::draw_count() == expected_draws[checkpoint]);
            MG_CHECK(terron->GetSide() == 0);
            MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0);
            MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
            for (EntityId id = 1; id <= 4; ++id) {
                const bool alive = !(lose_army && id <= 2) && !(after_hit && lethal && id == 3);
                MG_CHECK(session.robot(id).alive == alive);
                if (!alive) continue;
                CMatrixRobotAI *robot = nullptr;
                for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
                    if (object->IsRobot() && session.matches(id, object->AsRobot())) robot = object->AsRobot();
                MG_CHECK(robot != nullptr);
                MG_CHECK(robot->GetHitPoint() == (after_hit && id == 3 ? 40.0f : 100.0f));
            }
            std::cout << "blast checkpoint=" << checkpoint << " time=" << session.time()
                      << " draws=" << random::draw_count() << " robot_kills=0,0\n";
        };
        if (collateral) {
            observe(0);
            advance_to(session, step == 10 ? 5200 : 5184);
            MG_CHECK(!terron->InLT());
            MG_CHECK(owner_core->m_Object == terron);
#if defined(_DEBUG) && !defined(_RELDEBUG)
            MG_CHECK(g_MatrixMap->DebugContainsObject(terron));
            MG_CHECK(!g_MatrixMap->DebugContainsObject(nullptr));
            // A valid object not owned by this map must still be rejected.
            { CMatrixMapObject unregistered; MG_CHECK(!g_MatrixMap->DebugContainsObject(&unregistered)); }
#endif
            observe(1);
            session.advance();
            MG_CHECK(session.time() == (step == 10 ? 5210 : 5200));
            observe(2);
        }
        advance_to(session, step == 10 ? 8040 : 8064);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        session.advance();
        std::cout << "Terron dialog at " << session.time() << " ms\n";
        MG_CHECK(session.time() == (step == 10 ? 8050 : 8080));
        MG_CHECK(g_MatrixMap->IsPaused() && FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN);
        MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 1);
        if (!collateral) MG_CHECK(session.robot(3).alive && session.robot(4).alive);
        else observe(3);
    }
}

void terron_destruction() { for (int step : {10, 16}) terron_destruction_run(false, step); }
void terron_blast_after_conversion() {
    for (int step : {10, 16}) for (int damage : {600, 1200}) terron_destruction_run(true, step, damage);
}

struct ObjectiveIdentity {
    CMatrixMapObject *object;
    SObjectCore *core;
    explicit ObjectiveIdentity(CMatrixMapObject *value) : object(value), core(value->GetCore(DEBUG_CALL_INFO)) {}
    ObjectiveIdentity(const ObjectiveIdentity &) = delete;
    ObjectiveIdentity &operator=(const ObjectiveIdentity &) = delete;
    ~ObjectiveIdentity() { core->Release(); }
};
// Stable object order in these snapshots is fixture identity, never a pointer or UID.
std::string mixed_objective_snapshot(Session &session, std::span<const ObjectiveIdentity> objects) {
    std::string state = session.snapshot() + ":" + std::to_string(g_MatrixMap->m_BeforeWinCount) + ":" +
        std::to_string(g_MatrixMap->m_Flags & (MMFLAG_WIN | MMFLAG_SPECIAL_BROKEN | MMFLAG_TERRON_DEAD)) + ":" +
        std::to_string(side().GetStatus()) + ":" + std::to_string(g_MatrixMap->GetSideById(2)->GetStatus()) + ":" +
        std::to_string(side().GetStatValue(STAT_BUILDING_KILL)) + ":" + std::to_string(side().GetStatValue(STAT_ROBOT_KILL)) + ":" +
        std::to_string(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL)) + ":" +
        std::to_string(g_MatrixMap->IsPaused()) + ":" +
        std::to_string(g_MatrixMap->m_DialogModeName ?
            (std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN ? 1 : 2) : 0);
    for (const auto &identity : objects) {
        MG_CHECK(identity.core->m_Object == identity.object);
        const auto *object = identity.object;
        state += ":" + std::to_string(object->m_Type) + ":" + std::to_string(object->m_BehFlag) + ":" +
            std::to_string(object->IsSpecial()) + ":" + std::to_string(object->m_BreakHitPoint) + ":" +
            std::to_string(object->m_BehFlag == BEHF_ANIM ? object->m_AnimState : -1) + ":" +
            std::to_string(object->m_PB != nullptr);
    }
    return state;
}
void hit_objective(CMatrixMapObject *object, int attacker = PLAYER_SIDE) {
    object->Damage(WEAPON_LASER, object->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), attacker, nullptr);
}
void lose_last_objective_robot(Session &session) {
    MG_CHECK(!session.robot(1).alive && session.robot(2).alive);
    CMatrixRobotAI *last = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && session.matches(2, object->AsRobot())) last = object->AsRobot();
    MG_CHECK(last != nullptr);
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {10000, 0, 0};
    MG_CHECK(last->Damage(WEAPON_LASER, last->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
    MG_CHECK(!session.robot(2).alive);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
}
void check_objective_threshold(CMatrixMapObject *object) {
    auto &damage = g_Config.m_ObjectDamages[Weap2Index(WEAPON_LASER)];
    damage = {4, 0, 6};
    const auto rejected_draws = random::draw_count();
    hit_objective(object, 2);
    MG_CHECK(object->m_BreakHitPoint == 10 && object->m_PB == nullptr);
    MG_CHECK(random::draw_count() == rejected_draws);
    hit_objective(object);
    MG_CHECK(object->m_BreakHitPoint == 6 && object->m_PB != nullptr);
    const auto threshold_draws = random::draw_count();
    hit_objective(object);
    MG_CHECK(object->m_BreakHitPoint == 6 && object->IsSpecial());
    MG_CHECK(random::draw_count() == threshold_draws);
}
std::vector<std::string> mixed_special_order_run(random::Mode mode, int step, bool animation_first, bool loss_before) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 1500, false});
    assets.install(); crater.install();
    DialogData dialog;
    const std::array<std::wstring, 3> descriptors{L"+Break,1,10,None", L"Static", L"+Anim,0:0:10:1#1:0:0:-1"};
    install_objective_descriptors(descriptors, false);
    auto *broken = create_objective(600);
    auto *animated = create_objective(550, 2);
    const std::array objects{ObjectiveIdentity(broken), ObjectiveIdentity(animated)};
    auto *first = animation_first ? animated : broken;
    auto *last = animation_first ? broken : animated;
    MG_CHECK(session.remove(1));
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 2);
    std::cout << session.metadata("synthetic-mixed-special-order-v2") << " animation_first=" << animation_first
              << " loss_before=" << loss_before << '\n';
    std::vector<std::string> trace;
    const auto observe = [&] { trace.push_back(mixed_objective_snapshot(session, objects)); };
    const auto tick_to = [&](int until) {
        while (session.time() < until) { MG_CHECK(!g_MatrixMap->IsPaused()); session.advance(); observe(); }
        MG_CHECK(session.time() == until);
    };
    observe();
    for (const auto &identity : objects) { check_objective_threshold(identity.object); observe(); }
    g_Config.m_ObjectDamages[Weap2Index(WEAPON_LASER)] = {6, 0, 0};
    hit_objective(first); observe();
    MG_CHECK(!first->IsSpecial() && last->IsSpecial());
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 1 && side().GetStatValue(STAT_BUILDING_KILL) == 1);
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN | MMFLAG_WIN));
    MG_CHECK(side().GetStatus() == SS_ACTIVE);
    if (loss_before) {
        tick_to(step == 10 ? 2010 : 2016);
        lose_last_objective_robot(session); observe();
    }
    tick_to(step == 10 ? 2020 : 2032);
    MG_CHECK(!g_MatrixMap->m_DialogModeName && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(side().GetStatus() == (loss_before ? SS_NONE : SS_ACTIVE));
    hit_objective(last); observe();
    MG_CHECK(!last->IsSpecial() && g_MatrixMap->m_BeforeWinCount == 0);
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN) && side().GetStatus() == SS_JUST_WIN);
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 2);
    MG_CHECK(broken->m_Type == 1 && broken->m_BehFlag == BEHF_STATIC && broken->m_PB == nullptr);
    MG_CHECK(animated->m_Type == 2 && animated->m_BehFlag == BEHF_ANIM && animated->m_AnimState == 1);
    MG_CHECK(animated->m_BreakHitPoint == 2000000000 && animated->m_PB == nullptr);
    if (!loss_before) { lose_last_objective_robot(session); observe(); }
    hit_objective(first); hit_objective(last); observe();
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 0 && side().GetStatValue(STAT_BUILDING_KILL) == 2);
    tick_to(step == 10 ? 4020 : 4032);
    MG_CHECK(side().GetStatus() == SS_JUST_WIN && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    tick_to(step == 10 ? 4030 : 4048);
    if (loss_before) {
        // A pending defeat is not replaced by a later special-object win request.
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN) && side().GetStatus() == SS_JUST_WIN);
        MG_CHECK(!session.robot(1).alive && !session.robot(2).alive && session.robot(3).alive && session.robot(4).alive);
        std::cout << "mixed scan-first loss=" << session.time() << " draws=" << random::draw_count() << '\n';
        return trace;
    }
    MG_CHECK(side().GetStatus() == SS_ACTIVE && FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(!g_MatrixMap->m_DialogModeName);
    tick_to(step == 10 ? 6030 : 6048);
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
    session.advance(); observe();
    std::cout << "mixed result=" << session.time() << " draws=" << random::draw_count() << '\n';
    MG_CHECK(session.time() == (step == 10 ? 6040 : 6064));
    MG_CHECK(g_MatrixMap->IsPaused() && FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN);
    MG_CHECK(side().GetStatus() == SS_ACTIVE && side().GetStatValue(STAT_BUILDING_KILL) == 2);
    MG_CHECK(!session.robot(1).alive && !session.robot(2).alive && session.robot(3).alive && session.robot(4).alive);
    return trace;
}
void mixed_special_order() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
        for (bool animation_first : {false, true}) for (bool loss_before : {false, true}) {
            const auto first = mixed_special_order_run(mode, step, animation_first, loss_before);
            MG_CHECK(first == mixed_special_order_run(mode, step, animation_first, loss_before));
        }
}
std::vector<std::string> ordinary_objective_control_run(random::Mode mode, int step) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 1500, false});
    assets.install(); crater.install();
    DialogData dialog;
    const std::array<std::wstring, 3> descriptors{L"+Break,1,10,None", L"Static", L"Break,1,10,None"};
    install_objective_descriptors(descriptors, false);
    auto *special = create_objective(600);
    auto *ordinary = create_objective(550, 2, false);
    const std::array objects{ObjectiveIdentity(special), ObjectiveIdentity(ordinary)};
    MG_CHECK(session.remove(1) && session.remove(3) && session.remove(4));
    std::cout << session.metadata("synthetic-ordinary-objective-control-v1") << '\n';
    std::vector<std::string> trace;
    const auto observe = [&] { trace.push_back(mixed_objective_snapshot(session, objects)); };
    observe();
    g_Config.m_ObjectDamages[Weap2Index(WEAPON_LASER)] = {10, 0, 0};
    hit_objective(ordinary, 2); observe();
    MG_CHECK(ordinary->m_Type == 1 && ordinary->m_BehFlag == BEHF_STATIC && !ordinary->IsSpecial());
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 1 && special->IsSpecial() && special->m_BreakHitPoint == 10);
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 0 && ordinary->m_PB == nullptr);
    hit_objective(ordinary); observe();
    while (session.time() < (step == 10 ? 2020 : 2032)) { session.advance(); observe(); }
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName && side().GetStatus() == SS_ACTIVE);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatus() == SS_NONE);
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN | MMFLAG_SPECIAL_BROKEN | MMFLAG_TERRON_DEAD));
    lose_last_objective_robot(session); observe();
    while (session.time() < (step == 10 ? 6030 : 6048)) {
        MG_CHECK(!g_MatrixMap->IsPaused()); session.advance(); observe();
        if (session.time() == (step == 10 ? 4020 : 4032)) MG_CHECK(side().GetStatus() == SS_ACTIVE);
        if (session.time() == (step == 10 ? 4030 : 4048)) MG_CHECK(side().GetStatus() == SS_NONE);
        MG_CHECK(!g_MatrixMap->m_DialogModeName && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    }
    MG_CHECK(!g_MatrixMap->m_DialogModeName);
    session.advance(); observe();
    std::cout << "ordinary result=" << session.time() << " draws=" << random::draw_count() << '\n';
    MG_CHECK(session.time() == (step == 10 ? 6040 : 6064));
    MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN | MMFLAG_SPECIAL_BROKEN));
    MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
    MG_CHECK(special->IsSpecial() && g_MatrixMap->m_BeforeWinCount == 1);
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 0 && side().GetStatus() == SS_NONE);
    return trace;
}
void ordinary_objective_control() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = ordinary_objective_control_run(mode, step);
        MG_CHECK(first == ordinary_objective_control_run(mode, step));
    }
}
std::vector<std::string> mixed_terron_objective_run(random::Mode mode, int step, bool loss_before, bool scan_before_lethal = false) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    CaptureCraterAssets crater;
    Session session({mode, 85, step, 1500, false});
    assets.install(); crater.install();
    DialogData dialog;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 20;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    const std::array<std::wstring, 3> descriptors{L"+Break,1,10,Terron", L"Static", L"+Break,1,10,None"};
    install_objective_descriptors(descriptors, true);
    auto *terron = create_objective(600);
    auto *survivor = create_objective(550, 2);
    const std::array objects{ObjectiveIdentity(terron), ObjectiveIdentity(survivor)};
    MG_CHECK(session.remove(1));
    std::cout << session.metadata("synthetic-mixed-terron-objective-v2") << " loss_before=" << loss_before
              << " scan_before_lethal=" << scan_before_lethal << '\n';
    std::vector<std::string> trace;
    const auto observe = [&] { trace.push_back(mixed_objective_snapshot(session, objects)); };
    observe();
    check_objective_threshold(terron); observe();
    g_Config.m_ObjectDamages[Weap2Index(WEAPON_LASER)] = {6, 0, 0};
    if (scan_before_lethal) {
        while (session.time() < (step == 10 ? 2010 : 2016)) { session.advance(); observe(); }
        lose_last_objective_robot(session); observe();
        MG_CHECK(side().GetStatus() == SS_ACTIVE);
        session.advance(); observe();
        MG_CHECK(session.time() == (step == 10 ? 2020 : 2032));
        MG_CHECK(side().GetStatus() == SS_NONE && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_TERRON_DEAD | MMFLAG_WIN));
    }
    hit_objective(terron); observe();
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_TERRON_DEAD) && terron->m_Graph->IsAnim(L"Death"));
    if (scan_before_lethal) {
        // Terron's protection starts too late to cancel the already pending defeat.
        MG_CHECK(side().GetStatus() == SS_NONE);
        while (session.time() < (step == 10 ? 4020 : 4032)) {
            session.advance(); observe();
            MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        }
        session.advance(); observe();
        MG_CHECK(session.time() == (step == 10 ? 4030 : 4048));
        MG_CHECK(g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeName);
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_LOOSE);
        MG_CHECK(side().GetStatus() == SS_NONE && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN | MMFLAG_SPECIAL_BROKEN));
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_TERRON_DEAD));
        MG_CHECK(terron->IsSpecial() && terron->m_Type == 0 && terron->m_Graph->IsAnim(L"Death"));
        MG_CHECK(terron->m_BreakHitPoint == 0 && survivor->IsSpecial() && survivor->m_BreakHitPoint == 10);
        MG_CHECK(g_MatrixMap->m_BeforeWinCount == 2 && side().GetStatValue(STAT_BUILDING_KILL) == 0);
        std::cout << "Terron scan-first loss=" << session.time() << " draws=" << random::draw_count() << '\n';
        return trace;
    }
    while (session.time() < (step == 10 ? 5000 : 4992)) {
        session.advance(); observe();
        if (session.time() == 2000) {
            const auto draws = random::draw_count();
            hit_objective(terron); observe();
            MG_CHECK(random::draw_count() == draws && terron->m_Graph->IsAnim(L"Death"));
            MG_CHECK(terron->m_BreakHitPoint == 0 && g_MatrixMap->m_BeforeWinCount == 2);
        }
    }
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 2 && terron->IsSpecial() && survivor->IsSpecial());
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN | MMFLAG_SPECIAL_BROKEN));
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 0);
    if (loss_before) { lose_last_objective_robot(session); observe(); }
    session.advance(); observe();
    MG_CHECK(session.time() == (step == 10 ? 5010 : 5008));
    MG_CHECK(terron->m_Type == 1 && terron->m_BehFlag == BEHF_STATIC && !terron->IsSpecial());
    MG_CHECK(survivor->m_Type == 2 && survivor->m_BehFlag == BEHF_BREAK && survivor->IsSpecial());
    MG_CHECK(survivor->m_BreakHitPoint == 10 && g_MatrixMap->m_BeforeWinCount == 1);
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 1 && side().GetStatus() == SS_JUST_WIN);
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) && !FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN));
    if (!loss_before) { lose_last_objective_robot(session); observe(); }
    hit_objective(terron); observe();
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 1 && side().GetStatValue(STAT_BUILDING_KILL) == 1);
    while (session.time() < (step == 10 ? 8040 : 8064)) {
        MG_CHECK(!g_MatrixMap->IsPaused()); session.advance(); observe();
        if (session.time() == (step == 10 ? 6030 : 6048)) MG_CHECK(side().GetStatus() == SS_JUST_WIN);
        if (session.time() == (step == 10 ? 6040 : 6064)) MG_CHECK(side().GetStatus() == SS_ACTIVE);
        MG_CHECK(!g_MatrixMap->m_DialogModeName && FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    }
    MG_CHECK(!g_MatrixMap->m_DialogModeName);
    session.advance(); observe();
    std::cout << "mixed Terron result=" << session.time() << " draws=" << random::draw_count() << '\n';
    MG_CHECK(session.time() == (step == 10 ? 8050 : 8080));
    MG_CHECK(g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN);
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) && !FLAG(g_MatrixMap->m_Flags, MMFLAG_SPECIAL_BROKEN));
    MG_CHECK(g_MatrixMap->m_BeforeWinCount == 1 && survivor->IsSpecial() && survivor->m_BreakHitPoint == 10);
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 1 && side().GetStatus() == SS_ACTIVE);
    MG_CHECK(!session.robot(1).alive && !session.robot(2).alive && session.robot(3).alive && session.robot(4).alive);
    return trace;
}
void mixed_terron_objective() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
        for (bool loss_before : {false, true}) {
            const auto first = mixed_terron_objective_run(mode, step, loss_before);
            MG_CHECK(first == mixed_terron_objective_run(mode, step, loss_before));
        }
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = mixed_terron_objective_run(mode, step, true, true);
        MG_CHECK(first == mixed_terron_objective_run(mode, step, true, true));
    }
}

void repeated_sessions() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (int run = 0; run < 3; ++run) {
        outcome(run % 2 == 0, true, false, step, mode);
        MG_CHECK(g_MatrixMap == nullptr && g_IFaceList == nullptr && g_MatrixHeap == nullptr);
        MG_CHECK(CMatrixMapStatic::GetFirstLogic() == nullptr);
        MG_CHECK(!Input::isVKeyPressed(VK_CONTROL));
        Session fresh({mode, 79, step});
        std::cout << fresh.metadata("synthetic-repeated-outcomes-v2") << " run=" << run << '\n';
        MG_CHECK(fresh.time() == 0 && fresh.frame() == 0);
        MG_CHECK(side().GetStatus() == SS_ACTIVE);
        MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 2);
        MG_CHECK(fresh.robot(1).group == -1 && fresh.robot(2).group == -1);
        MG_CHECK(!g_MatrixMap->IsPaused());
        MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
    }
}
void result_confirmation_callbacks() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (bool loss : {false, true}) {
        Session session({mode, 85, step, 1000, false});
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        MG_CHECK(session.remove(loss ? 1 : 3) && session.remove(loss ? 2 : 4));
        advance_to(session, step == 10 ? 2020 : 2032);
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == (loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 1);
        MG_CHECK(g_MatrixMap->m_DialogModeHints[0]->GetCopyPosCnt() == 2);
        const int time = session.time();
        const int score_time = side().GetStatValue(STAT_TIME);
        press(form, VK_ESCAPE); // Escape cannot dismiss a battle result.
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) ==
            (loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        MG_CHECK(g_ExitState == 0);
        MG_CHECK(g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeHints.size() == 1);
        press(form, VK_RETURN);
        MG_CHECK(g_ExitState == (loss ? 2 : 3));
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        MG_CHECK(!g_MatrixMap->m_DialogModeName && g_MatrixMap->m_DialogModeHints.empty());
        MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
        session.advance();
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS);
        MG_CHECK(g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeHints.size() == 1);
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        MG_CHECK(side().GetStatValue(STAT_TIME) == score_time);
        press(form, VK_RETURN);
        MG_CHECK(g_ExitState == (loss ? 2 : 3) && FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!g_MatrixMap->m_DialogModeName && g_MatrixMap->m_DialogModeHints.empty());
        MG_CHECK(!g_MatrixMap->IsPaused());
        std::cout << session.metadata("synthetic-dialog-callbacks-v1") << " loss=" << loss << " result_ms=" << time << '\n';
    }
}
void menu_confirmation_cancel() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (uint8_t key : {'E', 'R', 'S'}) {
        Session session({mode, 85, step, 1000, false});
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        session.ground_command(500, 300);
        press(form, VK_ESCAPE);
        MG_CHECK(g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeHints.size() == 1);
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_MENU);
        auto *menu = g_MatrixMap->m_DialogModeHints[0];
        press(form, key);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 2 && g_MatrixMap->m_DialogModeHints[0] == menu);
        const int confirmation_height = key == 'E' ? 2 : key == 'R' ? 3 : 4;
        MG_CHECK(g_MatrixMap->m_DialogModeHints[1]->m_Height == confirmation_height);
        MG_CHECK(g_MatrixMap->m_DialogModeHints[1]->m_Width == confirmation_height + 4);
        const auto first = session.robot(1), second = session.robot(2);
        const int paused_time = session.time();
        const auto draws = random::draw_count();
        for (int i = 0; i < 10; ++i) session.advance();
        MG_CHECK(session.time() == paused_time && random::draw_count() == draws);
        for (const auto before : {first, second}) {
            const auto after = session.robot(before.id);
            MG_CHECK(after.alive && after.x == before.x && after.y == before.y);
            MG_CHECK(after.order == before.order && after.group == before.group);
            MG_CHECK(after.destination_x == before.destination_x && after.destination_y == before.destination_y);
        }
        press(form, VK_ESCAPE);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 1 && g_MatrixMap->m_DialogModeHints[0] == menu);
        MG_CHECK(g_MatrixMap->IsPaused() && g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        press(form, VK_ESCAPE);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName && g_MatrixMap->m_DialogModeHints.empty());
        MG_CHECK(session.robot(1).alive && session.robot(2).alive);
        const auto before_resume = session.robot(1);
        for (int i = 0; i < 50; ++i) session.advance();
        MG_CHECK(session.robot(1).order == mpo_MoveTo && session.robot(1).x > before_resume.x);
        std::cout << session.metadata("synthetic-menu-confirmation-v2") << " canceled_key=" << int(key) << '\n';
    }
}
void check_retained_move(const Session &session, const RobotState &before) {
    const auto after = session.robot(before.id);
    MG_CHECK(after.alive && after.x == before.x && after.y == before.y);
    MG_CHECK(after.order == mpo_MoveTo && after.order == before.order && after.group == before.group);
    MG_CHECK(after.destination_x == before.destination_x && after.destination_y == before.destination_y);
    MG_CHECK(after.target == before.target && after.target_is_null == before.target_is_null);
}
void menu_exit_surrender_callbacks() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (bool surrender : {false, true}) {
        Session session({mode, 85, step, 1000, false});
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        session.ground_command(500, 300);
        const auto start = session.robot(1);
        for (int i = 0; i < 25; ++i) session.advance();
        MG_CHECK(session.robot(1).x > start.x && session.robot(1).order == mpo_MoveTo);
        const std::array moving{session.robot(1), session.robot(2)};
        const int paused_time = session.time();
        const auto draws = random::draw_count();
        const auto check_paused = [&] {
            for (int i = 0; i < 5; ++i) session.advance();
            MG_CHECK(g_MatrixMap->IsPaused() && session.time() == paused_time);
            MG_CHECK(random::draw_count() == draws);
            for (const auto &robot : moving) check_retained_move(session, robot);
            MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
        };
        side().SetStatValue(STAT_TIME, 1000);
        auto &enemy = *g_MatrixMap->GetSideById(2);
        enemy.SetStatValue(STAT_TIME, 2000);
        press(form, VK_ESCAPE);
        check_paused();
        press(form, surrender ? 'S' : 'E');
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 2);
        check_paused();
        press(form, VK_RETURN);
        MG_CHECK(g_ExitState == (surrender ? 4 : 1));
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        MG_CHECK(enemy.GetStatValue(STAT_TIME) == (surrender ? -2000 : 2000));
        MG_CHECK(side().GetStatValue(STAT_TIME) == 1000);
        MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP) && g_MatrixMap->m_DialogModeHints.empty());
        session.advance();
        MG_CHECK(g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeHints.size() == 1);
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS);
        check_paused();
        press(form, VK_RETURN);
        MG_CHECK(g_ExitState == (surrender ? 4 : 1) && FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.empty());
        MG_CHECK(session.time() == paused_time && random::draw_count() == draws);
        for (const auto &robot : moving) check_retained_move(session, robot);
        std::cout << session.metadata("synthetic-menu-result-callbacks-v2") << " surrender=" << surrender << '\n';
    }
}
void statistics_cheat_dismissal() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 85, step, 1000, false});
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        session.ground_command(500, 300);
        bool handled = false;
        for (char key : std::string("STATS")) handled = Cheats::processInput(key);
        MG_CHECK(handled && FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG_D));
        session.advance();
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS_D);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 1 && g_MatrixMap->IsPaused());
        press(form, VK_RETURN);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.empty());
        MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
        session.ground_command(500, 300);
        const auto before = session.robot(1);
        for (int i = 0; i < 50; ++i) session.advance();
        MG_CHECK(session.robot(1).order == mpo_MoveTo && session.robot(1).x > before.x);
        std::cout << session.metadata("synthetic-statistics-dismissal-v2") << '\n';
    }
}
void legacy_loss_confirmation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 85, step, 1000, false});
        std::cout << session.metadata("synthetic-legacy-result-continuation-v1") << '\n';
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        session.ground_command(500, 300);
        for (int i = 0; i < 25; ++i) session.advance();
        const std::array moving{session.robot(1), session.robot(2)};
        MG_CHECK(moving[0].order == mpo_MoveTo && moving[0].x > 100);
        const int time = session.time();
        side().SetStatValue(STAT_TIME, 1234);
        const auto draws = random::draw_count();
        bool handled = false;
        for (char key : std::string("IAMLOOSER")) handled = Cheats::processInput(key);
        MG_CHECK(handled && g_ExitState == 3);
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN);
        const auto check_legacy_state = [&] {
            MG_CHECK(session.time() == time && random::draw_count() == draws);
            MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
            MG_CHECK(side().GetStatValue(STAT_TIME) == 1234 && g_ExitState == 3);
            for (const auto &robot : moving) check_retained_move(session, robot);
        };
        for (int i = 0; i < 5; ++i) session.advance();
        check_legacy_state();
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        press(form, VK_RETURN);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        MG_CHECK(!g_MatrixMap->m_DialogModeName && g_MatrixMap->m_DialogModeHints.empty());
        session.advance();
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS);
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        check_legacy_state();
        press(form, VK_RETURN);
        MG_CHECK(FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.empty());
        check_legacy_state();
    }
}
void pending_result_menu() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (bool loss : {false, true}) for (bool exit : {false, true}) {
        Session session({mode, 85, step, 1000, false});
        std::cout << session.metadata("synthetic-pending-result-menu-v1")
                  << " loss=" << loss << " confirmed_exit=" << exit << '\n';
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        MG_CHECK(session.remove(loss ? 1 : 3) && session.remove(loss ? 2 : 4));
        const int pending_time = step == 10 ? 2010 : 2016;
        advance_to(session, pending_time);
        MG_CHECK(!g_MatrixMap->m_DialogModeName && !g_MatrixMap->IsPaused());
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !loss);
        const int score_time = side().GetStatValue(STAT_TIME);
        press(form, VK_ESCAPE);
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_MENU);
        press(form, 'E');
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 2);
        const auto draws = random::draw_count();
        for (int i = 0; i < 10; ++i) session.advance();
        MG_CHECK(session.time() == pending_time && random::draw_count() == draws);
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_MENU);
        MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(g_ExitState == 0 && side().GetStatValue(STAT_TIME) == score_time);
        if (exit) {
            press(form, VK_RETURN);
            MG_CHECK(g_ExitState == 1 && FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
            session.advance();
            MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS);
            MG_CHECK(session.time() == pending_time && g_MatrixMap->IsPaused());
            MG_CHECK(side().GetStatValue(STAT_TIME) == score_time);
            press(form, VK_RETURN);
            MG_CHECK(g_ExitState == 1 && FLAG(g_Flags, GFLAG_EXITLOOP));
            MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
            MG_CHECK(session.time() == pending_time && random::draw_count() == draws);
        } else {
            press(form, VK_ESCAPE); // Cancel Exit, then resume the pending battle.
            MG_CHECK(g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeHints.size() == 1);
            press(form, VK_ESCAPE);
            MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
            session.advance();
            MG_CHECK(session.time() == (step == 10 ? 2020 : 2032));
            MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) ==
                (loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
            MG_CHECK(g_MatrixMap->IsPaused() && g_ExitState == 0);
            MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
        }
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !loss);
    }
}
void typed_statistics_continuation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 85, step, 1000, false});
        std::cout << session.metadata("synthetic-typed-statistics-v1") << '\n';
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        form.Keyboard(true, VK_CONTROL);
        press(form, '1');
        form.Keyboard(false, VK_CONTROL);
        for (char key : std::string("STATS")) press(form, key);
        MG_CHECK(bool(FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG_D)) == bool(MATRIXGAME_SCENARIO_CHEATS));
        session.advance();
#if MATRIXGAME_SCENARIO_CHEATS
        MG_CHECK(g_MatrixMap->m_DialogModeName &&
            std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS_D);
        MG_CHECK(g_MatrixMap->IsPaused() && session.time() == 0);
        MG_CHECK(side().m_CurrSel == NOTHING_SELECTED);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 1);
        const auto draws = random::draw_count();
        for (int i = 0; i < 10; ++i) session.advance();
        MG_CHECK(session.time() == 0 && random::draw_count() == draws);
        press(form, VK_RETURN);
#else
        // These letters also contain normal command shortcuts. Only cheat delivery is disabled.
        MG_CHECK(!g_MatrixMap->m_DialogModeName && session.time() == step);
#endif
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.empty());
        MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
        press(form, 'X'); // Cancel any normal command preordering from the typed letters.
        press(form, '1'); // Recall the saved group; only cheats-enabled dialog entry clears selection.
        MG_CHECK(side().GetCurGroup() && side().GetCurGroup()->GetRobotsCnt() == 2);
        session.ground_command(500, 300);
        const auto start = session.robot(1);
        MG_CHECK(start.order == mpo_MoveTo);
        for (int i = 0; i < 50; ++i) session.advance();
        MG_CHECK(session.robot(1).x > start.x && session.robot(1).order == mpo_MoveTo);
    }
}
void result_command_boundary() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) for (bool loss : {false, true}) {
        Session session({mode, 85, step, 1000, false});
        std::cout << session.metadata("synthetic-result-command-boundary-v1") << " loss=" << loss << '\n';
        CallbackDialogs dialogs;
        CFormMatrixGame form;
        MG_CHECK(session.remove(loss ? 1 : 3) && session.remove(loss ? 2 : 4));
        advance_to(session, step == 10 ? 2010 : 2016);
        MG_CHECK(!g_MatrixMap->m_DialogModeName && !g_MatrixMap->IsPaused());
        const auto command = [&](float x, float y) {
            // Enemy RobotObservation omits orders; snapshot includes every raw order queue.
            const auto before = loss ? session.snapshot() : std::string{};
            // Supply a world hit, as the other headless command cases do; no screen-picking claim.
            g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
            g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(x, y, 0);
            g_IFaceList->m_InFocus = UNKNOWN;
            form.MouseKey(B_DOWN, VK_RBUTTON, 0, 0);
            form.MouseKey(B_UP, VK_RBUTTON, 0, 0);
            if (loss) MG_CHECK(session.snapshot() == before);
        };
        const auto selection_before = side().m_CurrSel;
        const int members_before = side().GetCurGroup() ? side().GetCurGroup()->GetRobotsCnt() : 0;
        const auto check_loss_command = [&] {
            if (!loss) return;
            MG_CHECK(side().m_CurrSel == selection_before);
            MG_CHECK((side().GetCurGroup() ? side().GetCurGroup()->GetRobotsCnt() : 0) == members_before);
        };
        command(500, 300);
        if (!loss) MG_CHECK(session.robot(1).order == mpo_MoveTo && session.robot(1).destination_x == 48);
        check_loss_command();
        command(120, 500);
        if (!loss) MG_CHECK(session.robot(1).destination_x == 10 && session.robot(1).destination_y == 48);
        check_loss_command();
        session.advance();
        const int result_time = step == 10 ? 2020 : 2032;
        MG_CHECK(session.time() == result_time && g_MatrixMap->IsPaused());
        const auto *dialog = loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN;
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == dialog);
        const std::array before{session.robot(loss ? 3 : 1), session.robot(loss ? 4 : 2)};
        MG_CHECK(side().m_CurrSel == NOTHING_SELECTED);
        const auto draws = random::draw_count();
        command(500, 300);
        if (!loss) MG_CHECK(session.robot(1).destination_x == 10 && session.robot(1).destination_y == 48);
        command(300, 100);
        if (!loss) MG_CHECK(session.robot(1).destination_x == 10 && session.robot(1).destination_y == 48);
        MG_CHECK(random::draw_count() == draws);
        for (int i = 0; i < 10; ++i) session.advance();
        MG_CHECK(session.time() == result_time && random::draw_count() == draws);
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == dialog);
        for (const auto &robot : before) {
            const auto after = session.robot(robot.id);
            MG_CHECK(after.alive && after.x == robot.x && after.y == robot.y);
            MG_CHECK(after.order == robot.order && after.group == robot.group);
            MG_CHECK(after.destination_x == robot.destination_x && after.destination_y == robot.destination_y);
        }
        if (loss) MG_CHECK(!session.robot(1).alive && !session.robot(2).alive);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == !loss);
        MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
        press(form, VK_RETURN);
        session.advance();
        MG_CHECK(g_MatrixMap->m_DialogModeName &&
            std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS);
        MG_CHECK(session.time() == result_time && g_ExitState == (loss ? 2 : 3));
        press(form, VK_RETURN);
        MG_CHECK(FLAG(g_Flags, GFLAG_EXITLOOP) && g_ExitState == (loss ? 2 : 3));
    }
}
enum class LoopResultRoute { Exit, Surrender, LegacyLoss, Statistics, GenericShift, LeftShift, RightShift };
#ifdef _TRACE
constexpr bool result_trace_build = true;
#else
constexpr bool result_trace_build = false;
#endif
struct ResultLoopForm : CFormMatrixGame {
    Session &session;
    CForm *previous = g_FormCur;
    std::vector<int> ticks;
    std::vector<std::string> trace;
    std::array<RobotState, 2> moving{};
    int frozen_time{};
    uint64_t frozen_draws{};
    unsigned draws{}, paused_ticks{};
    bool frozen{};
    explicit ResultLoopForm(Session &value) : session(value) {
        g_FormCur = this;
        SETFLAG(g_Flags, GFLAG_APPACTIVE);
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_VIDEO_RESOURCES_READY);
    }
    ~ResultLoopForm() { g_FormCur = previous; }
    std::string snapshot() const {
        std::ostringstream out;
        out << session.snapshot() << ':' << g_ExitState << ':' << g_MatrixMap->IsPaused()
            << ':' << bool(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN)) << ':' << side().m_CurrSel;
        if (g_MatrixMap->m_DialogModeName)
            for (const wchar_t *name = g_MatrixMap->m_DialogModeName; *name; ++name) out << ':' << unsigned(*name);
        // Active side ticks update scores from timeGetTime(), outside the scripted clock.
        if (g_MatrixMap->IsPaused() || FLAG(g_Flags, GFLAG_EXITLOOP))
            out << ':' << side().GetStatValue(STAT_TIME) << ':' << g_MatrixMap->GetSideById(2)->GetStatValue(STAT_TIME);
        return out.str();
    }
    void freeze() {
        moving = {session.robot(1), session.robot(2)};
        for (const auto &robot : moving) MG_CHECK(robot.alive && robot.order == mpo_MoveTo && robot.x > 100);
        frozen_time = session.time();
        frozen_draws = random::draw_count();
        frozen = true;
    }
    void check_frozen() const {
        MG_CHECK(frozen && session.time() == frozen_time && random::draw_count() == frozen_draws);
        for (const auto &robot : moving) check_retained_move(session, robot);
    }
    void Takt(int delta) override {
        MG_CHECK(draws == ticks.size());
#if defined(__MINGW32__)
        MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
        const int before = session.time();
        const bool paused = g_MatrixMap->IsPaused() ||
            FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG | MMFLAG_STAT_DIALOG_D);
        ticks.push_back(delta);
        CFormMatrixGame::Takt(delta);
        MG_CHECK(session.time() == before + (paused ? 0 : delta));
        if (g_MatrixMap->IsPaused()) { ++paused_ticks; check_frozen(); }
#if defined(__MINGW32__)
        MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
    }
    void Draw() override {
        MG_CHECK(draws + 1 == ticks.size());
        MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP | GFLAG_APPCLOSE));
        ++draws;
        g_MatrixMap->m_Camera.BeforeDraw();
        trace.push_back(snapshot());
    }
};
void result_loop_key(tests::loop::Round &round, uint8_t key) {
    round.messages.emplace_back(WM_KEYDOWN, key);
    round.messages.emplace_back(WM_KEYUP, key);
}
std::vector<std::string> result_loop_run(random::Mode mode, int step, LoopResultRoute route) {
    constexpr bool cheats = MATRIXGAME_SCENARIO_CHEATS != 0;
    const bool menu = route == LoopResultRoute::Exit || route == LoopResultRoute::Surrender;
    const bool legacy = route == LoopResultRoute::LegacyLoss;
    const bool statistics = route == LoopResultRoute::Statistics;
    const bool shift_input = route == LoopResultRoute::GenericShift ||
        route == LoopResultRoute::LeftShift || route == LoopResultRoute::RightShift;
    const bool forced_trace = result_trace_build &&
        (route == LoopResultRoute::LeftShift || route == LoopResultRoute::RightShift);
    const bool terminal = menu || (legacy && cheats) || forced_trace;
    const bool paused_control = !terminal && (shift_input || (statistics && cheats));
    const int result = route == LoopResultRoute::Exit ? 1 : route == LoopResultRoute::Surrender ? 4 :
        route == LoopResultRoute::LeftShift ? 2 : 3;
    Session session({mode, 85, step, 1000, false});
    CallbackDialogs dialogs;
    ResultLoopForm form(session);
    std::cout << session.metadata("loop-result-continuation-v1") << " route=" << int(route)
              << " trace_build=" << result_trace_build << '\n';
    auto &enemy = *g_MatrixMap->GetSideById(2);
    session.ground_command(500, 300);
    side().PLDropAllActions(); // Keep the Move while ordinary shortcut letters have no selected group.
    MG_CHECK(side().GetCurGroup() == nullptr);
    tests::loop::Script script;
    const int last = terminal ? 29 : 55;
    for (int index = 0; index <= last; ++index) script.rounds.push_back({(index + 1) * step * 1000LL, {}});
    auto &trigger = script.rounds[25];
    if (menu) result_loop_key(trigger, VK_ESCAPE);
    else if (legacy || statistics) {
        for (const char *word = legacy ? "IAMLOOSER" : "STATS"; *word; ++word)
            result_loop_key(trigger, static_cast<uint8_t>(*word));
    } else {
        const uint8_t shift = route == LoopResultRoute::LeftShift ? VK_LSHIFT :
            route == LoopResultRoute::RightShift ? VK_RSHIFT : VK_SHIFT;
        trigger.messages.emplace_back(WM_KEYDOWN, shift);
        result_loop_key(trigger, VK_ESCAPE);
        trigger.messages.emplace_back(WM_KEYUP, shift);
    }
    const auto dialog_is = [&](const wchar_t *name) {
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == name);
    };
    trigger.after_messages = [&] {
        if (terminal || paused_control) {
            side().SetStatValue(STAT_TIME, 1234);
            enemy.SetStatValue(STAT_TIME, 5678);
        }
        form.freeze();
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) && !FLAG(g_Flags, GFLAG_EXITLOOP));
        if (terminal) {
            dialog_is(menu ? TEMPLATE_DIALOG_MENU : result == 2 ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN);
            MG_CHECK(g_ExitState == (menu ? 0 : result) && g_MatrixMap->IsPaused());
        } else if (statistics && cheats) {
            MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG_D) && g_ExitState == 0);
        } else if (paused_control) {
            dialog_is(TEMPLATE_DIALOG_MENU);
            MG_CHECK(g_ExitState == 0 && g_MatrixMap->IsPaused());
        } else MG_CHECK(!g_MatrixMap->m_DialogModeName && !g_MatrixMap->IsPaused() && g_ExitState == 0);
    };
    std::string closed_state;
    if (terminal) {
        if (menu) {
            result_loop_key(script.rounds[26], route == LoopResultRoute::Exit ? 'E' : 'S');
            script.rounds[26].after_messages = [&] {
                MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 2 && !FLAG(g_Flags, GFLAG_EXITLOOP));
                form.check_frozen();
            };
        }
        result_loop_key(script.rounds[27], VK_RETURN);
        script.rounds[27].after_messages = [&] {
            MG_CHECK(g_ExitState == result && FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
            MG_CHECK(!g_MatrixMap->m_DialogModeName && !FLAG(g_Flags, GFLAG_EXITLOOP));
            form.check_frozen();
        };
        script.rounds[28].after_messages = [&] {
            dialog_is(TEMPLATE_DIALOG_STATISTICS);
            MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
            form.check_frozen();
        };
        result_loop_key(script.rounds[29], VK_RETURN);
        script.rounds[29].after_messages = [&] {
            MG_CHECK(FLAG(g_Flags, GFLAG_EXITLOOP) && !FLAG(g_Flags, GFLAG_APPCLOSE));
            MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
            form.check_frozen();
            closed_state = form.snapshot();
        };
    } else {
        if (paused_control) {
            script.rounds[27].after_messages = [&] {
                dialog_is(statistics ? TEMPLATE_DIALOG_STATISTICS_D : TEMPLATE_DIALOG_MENU);
                MG_CHECK(g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
                form.check_frozen();
            };
            result_loop_key(script.rounds[28], statistics ? VK_RETURN : VK_ESCAPE);
            script.rounds[28].after_messages = [&] {
                MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName && g_ExitState == 0);
                MG_CHECK(side().GetStatValue(STAT_TIME) == 1234 && enemy.GetStatValue(STAT_TIME) == 5678);
                form.check_frozen();
            };
        }
        script.rounds[55].messages.emplace_back(WM_CLOSE, 0);
        script.rounds[55].after_messages = [&] { closed_state = form.snapshot(); };
    }
    MG_CHECK(L3GRun(script.platform()) == 1);
    MG_CHECK(!closed_state.empty() && closed_state == form.snapshot());
    MG_CHECK(form.draws == (terminal ? 29u : 55u) && form.ticks.size() == form.draws);
    MG_CHECK(form.paused_ticks == (terminal ? 4u : paused_control ? 3u : 0u));
    MG_CHECK(script.round == script.rounds.size() && script.clock_reads == form.draws + 1 && script.waits == 0);
    for (int delta : form.ticks) MG_CHECK(delta == step);
    MG_CHECK(g_ExitState == (terminal ? result : 0));
    MG_CHECK(bool(FLAG(g_Flags, GFLAG_EXITLOOP)) == terminal && bool(FLAG(g_Flags, GFLAG_APPCLOSE)) == !terminal);
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
    MG_CHECK(g_MatrixMap->m_DialogModeHints.empty() && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(side().GetStatus() == SS_ACTIVE);
    if (terminal) {
        MG_CHECK(side().GetStatValue(STAT_TIME) == 1234);
        MG_CHECK(enemy.GetStatValue(STAT_TIME) == (route == LoopResultRoute::Surrender ? -5678 : 5678));
    }
    MG_CHECK(!Input::isVKeyPressed(VK_SHIFT) && !Input::isVKeyPressed(VK_LSHIFT) && !Input::isVKeyPressed(VK_RSHIFT));
    if (terminal) form.check_frozen();
    else {
        MG_CHECK(session.time() == (paused_control ? 52 : 55) * step);
        for (const auto &before : form.moving) {
            const auto robot = session.robot(before.id);
            MG_CHECK(robot.alive && robot.order == mpo_MoveTo && robot.x > before.x);
        }
    }
    form.trace.push_back(closed_state);
    return form.trace;
}
void loop_menu_result_exit() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (auto route : {LoopResultRoute::Exit, LoopResultRoute::Surrender}) {
        const auto first = result_loop_run(mode, step, route);
        MG_CHECK(first == result_loop_run(mode, step, route));
    }
}
void loop_diagnostic_result_exit() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (auto route : {LoopResultRoute::LegacyLoss, LoopResultRoute::Statistics, LoopResultRoute::GenericShift,
                      LoopResultRoute::LeftShift, LoopResultRoute::RightShift}) {
        const auto first = result_loop_run(mode, step, route);
        MG_CHECK(first == result_loop_run(mode, step, route));
    }
}
int guarded_base_capturers(CMatrixBuilding *base, int group) {
    int count = 0;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && object->GetSide() == PLAYER_SIDE &&
            object->AsRobot()->GetGroupLogic() == group && object->AsRobot()->GetCaptureFactory() == base) ++count;
    return count;
}
std::vector<std::string> guarded_base_run(random::Mode mode, int step, bool destroy) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    Session session({mode, 84, step, 1500, false, true, false, tests::NavigationLayout::Open});
    assets.install();
    std::cout << session.metadata("guarded-base-journey-v1") << " destroy=" << destroy << std::endl;
    const auto texture = std::wstring(OBJECT_PATH_BUILDINGS_RUINS) + L"b" + std::to_wstring(int(BUILDING_BASE));
    tests::install_battle_texture(texture.c_str());
    tests::load_battle_material_mesh({}, (texture + L".vo").c_str(), texture.c_str());
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 20;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_CannonDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    auto *base = register_building(2, BUILDING_BASE, 100, 100);
    auto &enemy = *g_MatrixMap->GetSideById(2);
    g_Config.m_Timings[RESOURCE_BASE] = 1000;
    side().SetResourceForceUp(100);
    enemy.SetResourceForceUp(100);
    for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
        side().SetResourceAmount(static_cast<ERes>(resource), 0);
        enemy.SetResourceAmount(static_cast<ERes>(resource), 0);
    }
    auto *shooter = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    const int wi = Weap2Index(WEAPON_LASER);
    g_Config.m_WeaponRadius[wi] = 400;
    g_Config.m_WeaponCooldown[wi] = 100;
    g_Config.m_CannonDamages[wi] = {5, 0, 0};
    g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
    g_Config.m_Overheat[WEAPON_LASER_HEAT + 2] = 10;
    g_Config.m_Overheat[WEAPON_LASER_HEAT + 1] = 100;
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    tests::arm_battle_robot(*shooter, RUK_WEAPON_LASER);
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.weapon = WEAPON_CANNON2;
    props.m_Hitpoint = 20;
    props.m_Strength = 10;
    props.seek_radius = 300;
    props.max_da = 0.1f;
    props.max_top_angle = 1.5f;
    props.max_bottom_angle = -1.5f;
    for (auto &cannon : g_Config.m_CannonsProps)
        for (int &cost : cannon.m_Resources) cost = 1000;
    g_Config.m_CannonsLogic = true;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = 300;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_CANNON2)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_CANNON2)] = {1, 0, 10};
    // Map loading binds every active cannon to a real navigation place. The
    // shared combat-only helper does not need that binding, but capture movement does.
    auto &road = g_MatrixMap->m_RN;
    MG_CHECK(road.m_PlaceCnt == 4 && road.m_ZoneCnt == 1 && road.m_RegionCnt == 1);
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) MG_CHECK(object->AsRobot()->GetEnv()->m_Place == -1);
    // Reposition an initial place without changing the capacity of prepared navigation buffers.
    road.m_Place[3].m_Pos = CPoint(22, 14);
    road.m_Place[3].m_Cannon = 1;
    road.m_Place[3].m_Move = 1 + 2 + 4 + 8 + 16;
    road.InitPL(g_MatrixMap->m_SizeMove.x, g_MatrixMap->m_SizeMove.y);
    auto *guard = tests::create_battle_cannon(2, 220, 140);
    g_MatrixMap->CalcCannonPlace();
    MG_CHECK(guard->m_Place >= 0 && guard->m_Place < road.m_PlaceCnt);
    MG_CHECK(road.GetPlace(guard->m_Place)->m_Pos == CPoint(22, 14));
    guard->m_ParentBuilding = base;
    base->m_TurretsHave = 1;
    base->m_TurretsPlacesCnt = 1;
    base->m_TurretsPlaces[0].m_Coord = CPoint(22, 14);
    base->m_TurretsPlaces[0].m_CannonType = 1;
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> guard_core(guard->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> base_core(base->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    auto *partner = side().GetCurGroup()->GetObjectByN(1)->AsRobot();
    const std::array<CMatrixRobotAI *, 2> members{shooter, partner};
    MG_CHECK(session.matches(1, shooter) && session.matches(2, partner));
    MG_CHECK(!partner->GetEnv()->SearchEnemy(guard));
    MG_CHECK(!shooter->GetEnv()->SearchEnemy(guard));
    submit_capture(base, CaptureRoute::Context);
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
    const int group_id = shooter->GetGroupLogic();
    auto &group = side().m_PlayerGroup[group_id];
    MG_CHECK(group.Order() == mpo_Capture && group.m_Obj == base && !group.IsWar());
    MG_CHECK(guarded_base_capturers(base, group_id) == 1);
    std::vector<std::string> trace;
    auto record = [&] {
        std::ostringstream out;
        out << session.snapshot() << ":war=" << group.IsWar() << ":intent=" << group.Order()
            << ":objective=" << (group.m_Obj == base) << ":guard=" << (guard_core->m_Object != nullptr)
            << ":base=" << (base_core->m_Object != nullptr);
        if (guard_core->m_Object) out << ':' << guard->m_CurrState << ':' << guard->GetHitPoint();
        if (base_core->m_Object) out << ':' << base->m_State << ':' << base->GetSide() << ':' << base->m_TurretsHave
            << ':' << base->m_TrueColor.m_Color << ':' << base->m_TrueColor.m_ColoredCnt
            << ':' << guarded_base_capturers(base, group_id);
        for (unsigned index = 0; index < members.size(); ++index) if (session.robot(index + 1).alive) {
            auto *robot = members[index];
            out << ":member=" << index + 1 << ':' << (robot->GetCaptureFactory() == base)
                << ':' << (robot->GetEnv()->m_TargetAttack == guard) << ':' << (robot->GetEnv()->m_Target == guard)
                << ':' << (robot->GetEnv()->SearchEnemy(guard) != nullptr);
        }
        for (int resource = 0; resource < MAX_RESOURCES; ++resource)
            out << ':' << side().GetResourcesAmount(static_cast<ERes>(resource))
                << ':' << enemy.GetResourcesAmount(static_cast<ERes>(resource));
        out << ':' << side().GetStatValue(STAT_TURRET_KILL) << ':' << side().GetStatValue(STAT_BUILDING_KILL);
        trace.push_back(out.str());
    };
    record();
    int interrupted = 0, killed = 0, resumed = 0, opened = 0, captured = 0, retired = 0, released = 0;
    int guard_retired = 0, enemy_income = 0, player_income = 0;
    float health = 20;
    std::array<float, 2> robot_health{shooter->GetHitPoint(), partner->GetHitPoint()};
    std::vector<int> impacts, return_fire;

    while (session.time() < 12000) {
        const bool live_base = base_core->m_Object && base->IsLiveBuilding();
        const int owner = live_base ? base->GetSide() : 0;
        if (live_base && (session.time() + step) % (step == 10 ? 1000 : 1008) == 0) {
            if (owner == PLAYER_SIDE) player_income += RESOURCES_INCOME_BASE;
            else if (owner == 2) enemy_income += RESOURCES_INCOME_BASE;
        }
        session.advance();
        if (!interrupted && group.IsWar()) {
            interrupted = session.time();
            MG_CHECK(guarded_base_capturers(base, group_id) == 0);
            int known = 0, targeted = 0;
            for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
                if (object->IsLiveRobot() && object->GetSide() == PLAYER_SIDE) {
                    known += object->AsRobot()->GetEnv()->SearchEnemy(guard) != nullptr;
                    targeted += object->AsRobot()->GetEnv()->m_TargetAttack == guard;
                }
            MG_CHECK(known == 1 && targeted == 1);
        }
        if (guard_core->m_Object && guard->GetHitPoint() != health) {
            MG_CHECK(guard->GetHitPoint() == health - 5);
            health -= 5;
            impacts.push_back(session.time());
            MG_CHECK(group.IsWar() && group.Order() == mpo_Capture && group.m_Obj == base);
        }
        if (!killed && guard_core->m_Object && guard->GetHitPoint() == 0) {
            killed = session.time();
            MG_CHECK(guard->m_CurrState == CANNON_DIP);
            MG_CHECK(base->m_TurretsHave == 0 && base->m_TurretsPlaces[0].m_CannonType == -1);
            MG_CHECK(side().GetStatValue(STAT_TURRET_KILL) == 1 && enemy.GetStatValue(STAT_TURRET_KILL) == 0);
            for (auto *member : members) {
                MG_CHECK(!member->GetEnv()->SearchEnemy(guard));
                MG_CHECK(member->GetEnv()->m_TargetAttack != guard && member->GetEnv()->m_Target != guard);
            }
        }
        if (killed && !resumed && !group.IsWar()) {
            resumed = session.time();
            MG_CHECK(guarded_base_capturers(base, group_id) == 1);
            MG_CHECK(group.Order() == mpo_Capture && group.m_Obj == base);
        }
        for (unsigned index = 0; index < members.size(); ++index) if (session.robot(index + 1).alive) {
            const auto actual = members[index]->GetHitPoint();
            if (actual != robot_health[index]) {
                MG_CHECK(index == 1);
                return_fire.push_back(session.time());
                MG_CHECK(actual == float(1000 - int(return_fire.size())) / 10.0f);
                robot_health[index] = actual;
            }
            if (killed) {
                MG_CHECK(!members[index]->GetEnv()->SearchEnemy(guard));
                MG_CHECK(members[index]->GetEnv()->m_TargetAttack != guard);
            }
        }
        if (!guard_retired && !guard_core->m_Object) guard_retired = session.time();
        if (base_core->m_Object) {
            if (!opened && base->m_State == BASE_OPENED) opened = session.time();
            if (!captured && base->GetSide() == PLAYER_SIDE) {
                captured = session.time();
                MG_CHECK(base->m_State == BASE_CLOSED && base->m_Capturer == nullptr);
                MG_CHECK(g_MatrixMap->GetSideColor(base->GetSide()) == 0x0000ff00);
                MG_CHECK(guarded_base_capturers(base, group_id) == 0);
            }
            // Bases use their side for rendering; factory paint fields are not repainted by base capture.
            MG_CHECK(base->m_TrueColor.m_Color == 0xffff0000 && base->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
            MG_CHECK(guarded_base_capturers(base, group_id) <= 1);
            MG_CHECK(base->m_TurretsHave == int(!killed));
            if (group.IsWar()) MG_CHECK(guarded_base_capturers(base, group_id) == 0);
        } else if (!retired) retired = session.time();
        if (!released && group.m_Obj == nullptr) released = session.time();
        for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
            MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(resource)) == player_income);
            MG_CHECK(enemy.GetResourcesAmount(static_cast<ERes>(resource)) == enemy_income);
        }
        if (destroy && session.time() == 6000) {
            MG_CHECK(base_core->m_Object && resumed && opened && !captured && base->m_State == BASE_OPENED);
            MG_CHECK(base->m_Capturer != nullptr && base->m_Capturer->AsRobot()->IsDisableManual());
            record(); // Preserve the production frame before the explicit objective-damage intervention.
            g_Config.m_BuildingDamages[wi] = {10000, 0, 0};
            MG_CHECK(base->Damage(WEAPON_LASER, base->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), PLAYER_SIDE, nullptr));
            MG_CHECK(base->m_State == BUILDING_DIP && base->m_Capturer == nullptr);
            MG_CHECK(shooter->IsMustDie() && shooter->GetBase() == nullptr && shooter->GetCaptureFactory() == nullptr);
            MG_CHECK(guarded_base_capturers(base, group_id) == 0);
            MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 1);
        }
        record();
    }
    std::cout << "guard outcome mode=" << int(mode) << " step=" << step << " destroy=" << destroy
        << " interrupt=" << interrupted << " kill=" << killed << " resume=" << resumed << " open=" << opened
        << " capture=" << captured << " guard_retired=" << guard_retired << " base_retired=" << retired
        << " release=" << released << " income=" << player_income << ',' << enemy_income
        << " rng=" << random::draw_count() << " robots=" << session.robot(1).alive << ',' << session.robot(2).alive
        << " health=" << robot_health[0] << ',' << robot_health[1] << " return_fire=";
    for (int time : return_fire) std::cout << time << ',';
    std::cout << std::endl;
    MG_CHECK(interrupted == (step == 10 ? 310 : 240));
    MG_CHECK(impacts == (step == 10 ? std::vector<int>{530, 630, 730, 830} : std::vector<int>{480, 576, 688, 784}));
    MG_CHECK(killed == (step == 10 ? 830 : 784) && resumed == (step == 10 ? 910 : 800));
    MG_CHECK(opened == (step == 10 ? 4180 : 3936));
    MG_CHECK(captured == (destroy ? 0 : step == 10 ? 8740 : 8512));
    MG_CHECK(guard_retired == (mode == random::Mode::ParkMiller ? (step == 10 ? 5420 : 4592) : (step == 10 ? 4370 : 5088)));
    MG_CHECK(released == (destroy ? (step == 10 ? 6010 : 6064) : (step == 10 ? 8810 : 8528)));
    MG_CHECK(retired == (destroy ? (step == 10 ? 9010 : 9008) : 0));
    MG_CHECK(return_fire == (step == 10 ? std::vector<int>{220, 320, 420, 520, 620, 720, 820}
                                       : std::vector<int>{224, 320, 432, 528, 624, 720}));
    MG_CHECK(group.Order() == mpo_Stop && group.m_Obj == nullptr && !group.IsWar());
    MG_CHECK(!session.robot(1).alive && session.robot(2).alive);
    MG_CHECK(partner->GetCaptureFactory() == nullptr && !partner->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
    MG_CHECK(session.robot(3).alive && session.robot(4).alive);
    MG_CHECK(side().GetStatValue(STAT_TURRET_KILL) == 1 && side().GetStatValue(STAT_BUILDING_KILL) == int(destroy));
    MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0 && enemy.GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(!g_MatrixMap->IsPaused() && side().GetStatus() == SS_ACTIVE);
    MG_CHECK((base_core->m_Object == nullptr) == destroy);
    MG_CHECK(enemy_income == (destroy ? (step == 10 ? 18 : 15) : 24));
    MG_CHECK(player_income == (destroy ? 0 : step == 10 ? 12 : 9));
    return trace;
}
void guarded_base_profile(bool destroy) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = guarded_base_run(mode, step, destroy);
        MG_CHECK(first == guarded_base_run(mode, step, destroy));
    }
}
void guarded_base_journey() { guarded_base_profile(false); }
void guarded_base_target_destruction() { guarded_base_profile(true); }
std::vector<std::string> last_base_weapon_run(random::Mode mode, int step, bool retain_robot) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    Session session({mode, 85, step, 1500, false, true, false, tests::NavigationLayout::Open});
    DialogData dialog;
    assets.install();
    std::cout << session.metadata("last-base-weapon-outcome-v1") << " retain_robot=" << retain_robot << std::endl;
    // The building aim point (z = 20) is strictly inside this synthetic mesh.
    auto *mesh = tests::load_battle_mesh({}, nullptr, false, 40.0f);
    auto owned = tests::make_battle_building(*mesh, 2, BUILDING_BASE, 200, 200, 50);
    auto *base = owned.get();
    g_MatrixMap->AddObject(owned.release(), true);
    int base_cells = 0;
    for (int x = 0; x < g_MatrixMap->m_Size.x; ++x)
        for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
            if (g_MatrixMap->UnitGet(x, y)->m_Base == base) ++base_cells;
    MG_CHECK(base_cells == 9);
    for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
        g_Config.m_Timings[resource] = 100000;
    const auto texture = std::wstring(OBJECT_PATH_BUILDINGS_RUINS) + L"b" + std::to_wstring(int(BUILDING_BASE));
    tests::install_battle_texture(texture.c_str());
    tests::load_battle_material_mesh({}, (texture + L".vo").c_str(), texture.c_str());
    // The production demolition effect runs; collateral damage is isolated.
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 20;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_CannonDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    const int laser = Weap2Index(WEAPON_LASER);
    g_Config.m_WeaponRadius[laser] = 180;
    g_Config.m_WeaponCooldown[laser] = 100;
    g_Config.m_BuildingDamages[laser] = {30, 0, 0};
    g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
    g_Config.m_Overheat[WEAPON_LASER_HEAT + 2] = 10;
    g_Config.m_Overheat[WEAPON_LASER_HEAT + 1] = 100;
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    auto *factory = register_building(2, BUILDING_TITAN, 500, 100);
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.m_Hitpoint = 100;
    props.weapon = WEAPON_CANNON2;
    props.seek_radius = 1;
    g_Config.m_CannonsLogic = true;
    // Bind the out-of-range turret exactly as the loader binds navigation places.
    auto &road = g_MatrixMap->m_RN;
    MG_CHECK(road.m_PlaceCnt == 4);
    road.m_Place[3].m_Pos = CPoint(50, 50);
    road.m_Place[3].m_Cannon = 1;
    road.InitPL(g_MatrixMap->m_SizeMove.x, g_MatrixMap->m_SizeMove.y);
    auto *cannon = tests::create_battle_cannon(2, 500, 500);
    g_MatrixMap->CalcCannonPlace();
    MG_CHECK(cannon->m_Place >= 0 && cannon->m_Place < road.m_PlaceCnt);
    MG_CHECK(road.GetPlace(cannon->m_Place)->m_Pos == CPoint(50, 50));
    MG_CHECK(session.remove(3));
    if (!retain_robot) MG_CHECK(session.remove(4));
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) object->AsRobot()->SetMaxSpeed(0);
    auto *shooter = side().GetCurGroup()->GetObjectByN(0)->AsRobot();
    MG_CHECK(session.matches(1, shooter));
    tests::arm_battle_robot(*shooter, RUK_WEAPON_LASER);
    auto &enemy = *g_MatrixMap->GetSideById(2);
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(base->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> factory_core(factory->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> cannon_core(cannon->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> shooter_core(shooter->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    const int command_at = step == 10 ? 2020 : 2032;
    const int first_hit_at = step == 10 ? 2130 : 2160;
    const int eliminate_at = step == 10 ? 4030 : 4048;
    const int result_at = step == 10 ? 6040 : 6064;
    std::vector<std::string> trace;
    auto record = [&] {
        MG_CHECK(shooter_core->m_Object == shooter);
        MG_CHECK(factory_core->m_Object == factory && cannon_core->m_Object == cannon);
        std::ostringstream out;
        out << session.snapshot() << ":base=" << (core->m_Object != nullptr);
        if (core->m_Object) out << ':' << base->m_State << ':' << base->GetHitPoint() << ':' << base->GetSide();
        out << ":sides=" << side().GetStatus() << ':' << enemy.GetStatus()
            << ":factory=" << factory->GetSide() << ':' << factory->GetHitPoint() << ':' << factory->m_TrueColor.m_ColoredCnt
            << ":turret=" << cannon->GetSide() << ':' << cannon->m_CurrState << ':' << cannon->GetHitPoint()
            << ":target=" << (shooter->GetEnv()->m_Target == base) << ':' << (shooter->GetEnv()->m_TargetAttack == base)
            << ':' << (shooter->GetEnv()->SearchEnemy(base) != nullptr)
            << ":kills=" << side().GetStatValue(STAT_BUILDING_KILL) << ':' << enemy.GetStatValue(STAT_BUILDING_KILL)
            << ":win=" << FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) << ":paused=" << g_MatrixMap->IsPaused()
            << ":dialog=" << (g_MatrixMap->m_DialogModeName != nullptr);
        trace.push_back(out.str());
    };
    record();
    while (session.time() < command_at) { session.advance(); record(); }
    MG_CHECK(enemy.GetStatus() == SS_ACTIVE && base->IsLiveBuilding() && base->GetHitPoint() == 5);
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 0);
    // Normal player group intent drives robot targeting, weapon update, trace and damage dispatch.
    side().PGOrderAttack(side().SelGroupToLogicGroup(), CPoint(20, 20), base);
    MG_CHECK(shooter_core->m_Object == shooter);
    const int group_id = shooter->GetGroupLogic();
    MG_CHECK(group_id >= 0 && group_id < MAX_LOGIC_GROUP);
    auto &group = side().m_PlayerGroup[group_id];
    MG_CHECK(group.Order() == mpo_Attack && group.m_Obj == base);
    record();
    int dying = 0, exploded = 0, removed = 0, eliminated = 0, result = 0, released = 0;
    std::vector<int> impacts;
    float health = base->GetHitPoint();
    const float factory_health = factory->GetHitPoint(), cannon_health = cannon->GetHitPoint();
    while (session.time() < (retain_robot ? 12000 : result_at)) {
        MG_CHECK(!g_MatrixMap->IsPaused());
        session.advance();
        MG_CHECK(shooter_core->m_Object == shooter);
        MG_CHECK(factory_core->m_Object == factory && cannon_core->m_Object == cannon);
        if (session.time() == first_hit_at)
            MG_CHECK(core->m_Object && base->IsLiveBuilding() && base->GetHitPoint() == 2);
        if (core->m_Object && base->GetHitPoint() != health) {
            if (base->IsLiveBuilding()) {
                impacts.push_back(session.time());
                MG_CHECK(base->GetHitPoint() == 2 && side().GetStatValue(STAT_BUILDING_KILL) == 0);
            }
            health = base->GetHitPoint();
        }
        if (!dying && core->m_Object && !base->IsLiveBuilding()) {
            dying = session.time();
            MG_CHECK(base->m_State == BUILDING_DIP && base->GetHitPoint() <= 0);
            MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 1 && enemy.GetStatValue(STAT_BUILDING_KILL) == 0);
            MG_CHECK(enemy.GetStatus() == SS_ACTIVE && !FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        }
        if (!exploded && core->m_Object && base->m_State == BUILDING_DIP_EXPLODED) exploded = session.time();
        if (!removed && !core->m_Object) {
            removed = session.time();
            for (int x = 0; x < g_MatrixMap->m_Size.x; ++x)
                for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
                    MG_CHECK(g_MatrixMap->UnitGet(x, y)->m_Base != base);
        }
        if (!released && group.m_Obj == nullptr) released = session.time();
        if (dying) {
            MG_CHECK(!shooter->GetEnv()->SearchEnemy(base));
            MG_CHECK(shooter->GetEnv()->m_TargetAttack != base);
        }
        if (!eliminated && enemy.GetStatus() == SS_NONE) eliminated = session.time();
        if (g_MatrixMap->m_DialogModeName) result = session.time();
        MG_CHECK(enemy.GetStatus() == (retain_robot || session.time() < eliminate_at ? SS_ACTIVE : SS_NONE));
        MG_CHECK(factory->IsLiveBuilding() && factory->GetHitPoint() == factory_health);
        MG_CHECK(factory->GetSide() == (retain_robot || session.time() < eliminate_at ? 2 : 0));
        if (!retain_robot && session.time() >= eliminate_at)
            MG_CHECK(factory->m_TrueColor.m_Color == 0 && factory->m_TrueColor.m_ColoredCnt == 0);
        MG_CHECK(cannon->IsLiveCannon() && cannon->GetSide() == 2 && cannon->GetHitPoint() == cannon_health);
        MG_CHECK(session.robot(1).alive && session.robot(2).alive && !session.robot(3).alive);
        MG_CHECK(session.robot(4).alive == retain_robot);
        MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0 && enemy.GetStatValue(STAT_ROBOT_KILL) == 0);
        MG_CHECK(side().GetStatValue(STAT_TURRET_KILL) == 0 && enemy.GetStatValue(STAT_TURRET_KILL) == 0);
        MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == (dying ? 1 : 0));
        MG_CHECK(enemy.GetStatValue(STAT_BUILDING_KILL) == 0);
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == (!retain_robot && session.time() >= eliminate_at));
        MG_CHECK(g_MatrixMap->IsPaused() == (!retain_robot && session.time() == result_at));
        MG_CHECK((g_MatrixMap->m_DialogModeName != nullptr) == (!retain_robot && session.time() == result_at));
        record();
    }
    std::cout << "last base step=" << step << " retain_robot=" << retain_robot << " dying=" << dying
        << " exploded=" << exploded << " removed=" << removed << " released=" << released
        << " eliminated=" << eliminated << " result=" << result << " rng=" << random::draw_count() << std::endl;
    MG_CHECK(impacts == std::vector<int>{first_hit_at});
    MG_CHECK(dying == (step == 10 ? 2230 : 2256));
    MG_CHECK(removed == (step == 10 ? 5240 : 5264));
    MG_CHECK(exploded == (step == 10 ? 5140 : 5168));
    MG_CHECK(released == (step == 10 ? 2320 : 2256));
    MG_CHECK(eliminated == (retain_robot ? 0 : eliminate_at));
    MG_CHECK(result == (retain_robot ? 0 : result_at));
    MG_CHECK(core->m_Object == nullptr && core->m_Ref == 1 && group.m_Obj == nullptr);
    MG_CHECK(shooter_core->m_Object == shooter);
    MG_CHECK(shooter->GetEnv()->m_Target != base && shooter->GetEnv()->m_TargetAttack != base);
    MG_CHECK(side().GetStatValue(STAT_BUILDING_KILL) == 1 && enemy.GetStatValue(STAT_BUILDING_KILL) == 0);
    if (!retain_robot)
        MG_CHECK(g_MatrixMap->m_DialogModeName && std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_WIN);
    return trace;
}
void last_base_weapon_outcome() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool retain_robot : {false, true}) {
        const auto first = last_base_weapon_run(mode, step, retain_robot);
        MG_CHECK(first == last_base_weapon_run(mode, step, retain_robot));
    }
}
constexpr tests::Case cases[] = {
    {"game.player.special_attack_routes.park_miller10", special_attack_routes<random::Mode::ParkMiller, 10>},
    {"game.player.special_attack_routes.park_miller16", special_attack_routes<random::Mode::ParkMiller, 16>},
    {"game.player.special_attack_routes.legacy10", special_attack_routes<random::Mode::LegacyCRT, 10>},
    {"game.player.special_attack_routes.legacy16", special_attack_routes<random::Mode::LegacyCRT, 16>},
    {"game.player.special_bomb_routes.park_miller10", special_bomb_routes<false, random::Mode::ParkMiller, 10>},
    {"game.player.special_bomb_routes.park_miller16", special_bomb_routes<false, random::Mode::ParkMiller, 16>},
    {"game.player.special_bomb_routes.legacy10", special_bomb_routes<false, random::Mode::LegacyCRT, 10>},
    {"game.player.special_bomb_routes.legacy16", special_bomb_routes<false, random::Mode::LegacyCRT, 16>},
    {"game.player.special_bomb_stop.park_miller10", special_bomb_routes<true, random::Mode::ParkMiller, 10>},
    {"game.player.special_bomb_stop.park_miller16", special_bomb_routes<true, random::Mode::ParkMiller, 16>},
    {"game.player.special_bomb_stop.legacy10", special_bomb_routes<true, random::Mode::LegacyCRT, 10>},
    {"game.player.special_bomb_stop.legacy16", special_bomb_routes<true, random::Mode::LegacyCRT, 16>},
    {"game.player.ordinary_object_commands.park_miller10", ordinary_object_commands<random::Mode::ParkMiller, 10>},
    {"game.player.ordinary_object_commands.park_miller16", ordinary_object_commands<random::Mode::ParkMiller, 16>},
    {"game.player.ordinary_object_commands.legacy10", ordinary_object_commands<random::Mode::LegacyCRT, 10>},
    {"game.player.ordinary_object_commands.legacy16", ordinary_object_commands<random::Mode::LegacyCRT, 16>},
    {"game.loop.menu_result_exit", loop_menu_result_exit},
    {"game.loop.diagnostic_result_exit", loop_diagnostic_result_exit},
#ifdef _TRACE
    {"game.outcome.trace_result_callbacks", trace_result_callbacks},
#endif
    {"game.outcome.typed_statistics_continuation", typed_statistics_continuation},
    {"game.outcome.result_command_boundary", result_command_boundary},
    {"game.outcome.last_base_weapon", last_base_weapon_outcome},
    {"game.ai.guarded_base_journey", guarded_base_journey},
    {"game.ai.guarded_base_target_destruction", guarded_base_target_destruction},
    {"game.outcome.legacy_loss_confirmation", legacy_loss_confirmation},
    {"game.outcome.pending_result_menu", pending_result_menu},
    {"game.outcome.menu_exit_surrender_callbacks", menu_exit_surrender_callbacks},
    {"game.outcome.statistics_cheat_dismissal", statistics_cheat_dismissal},
    {"game.outcome.result_confirmation_callbacks", result_confirmation_callbacks},
    {"game.outcome.menu_confirmation_cancel", menu_confirmation_cancel},
    {"game.player.capture_keyboard_completion", capture_keyboard_completion},
    {"game.player.capture_button_completion", capture_button_completion},
    {"game.outcome.flyer_does_not_prevent_elimination", flyer_does_not_prevent_elimination},
    {"game.outcome.transported_robot_prevents_elimination", transported_robot_prevents_elimination},
    {"game.outcome.falling_cargo_status_scan", falling_cargo_status_scan},
    {"game.outcome.armed_transport_survival", armed_transport_survival},
    {"game.outcome.armed_flyer_does_not_prevent_elimination", armed_flyer_does_not_prevent_elimination},
    {"game.outcome.transport_projectile_outcomes", transport_projectile_outcomes},
    {"game.outcome.transport_context_rejection", transport_context_rejection},
    {"game.outcome.transport_ai_acquisition", transport_ai_acquisition},
    {"game.outcome.transported_robot_removal", transported_robot_removal_outcome},
    {"game.outcome.carrier_removal", carrier_removal_outcome},
    {"game.outcome.carrier_damage", carrier_damage_outcome},
    {"game.outcome.transported_robot_damage", transported_robot_damage_outcome},
    {"game.outcome.capture_target_damage", capture_target_damage},
    {"game.outcome.mixed_special_order", mixed_special_order},
    {"game.outcome.ordinary_objective_control", ordinary_objective_control},
    {"game.outcome.mixed_terron_objective", mixed_terron_objective},
    {"game.outcome.terron_destruction", terron_destruction},
    {"game.outcome.terron_blast_after_conversion", terron_blast_after_conversion},
    {"game.outcome.special_break", special_object_destruction<0>},
    {"game.outcome.special_explosion", special_object_destruction<1>},
    {"game.outcome.special_animation", special_object_destruction<2>},
    {"game.outcome.capture_combat_death", capture_combat_death},
    {"game.outcome.base_capture_interruption", base_capture_interruption},
    {"game.outcome.turret_does_not_prevent_elimination", turret_does_not_prevent_elimination},
    {"game.outcome.registered_enemy_capture", registered_enemy_capture},
    {"game.outcome.capture_target_removal", capture_target_removal},
    {"game.outcome.registered_base_capture", registered_base_capture},
    {"game.outcome.capture_candidate_lifetime", capture_candidate_lifetime},
    {"game.outcome.registered_capture", registered_capture},
    {"game.outcome.base_survival", base_survival},
    {"game.outcome.factory_does_not_prevent_elimination", factory_does_not_prevent_elimination},
    {"game.outcome.neutral_capture", neutral_capture},
    {"game.outcome.enemy_capture", enemy_capture},
    {"game.outcome.capture_rejection", capture_rejection},
    {"game.outcome.capture_rollback", capture_rollback},
    {"game.outcome.competing_capturers", competing_capturers},
    {"game.outcome.competing_color", competing_color},
    {"game.outcome.protected_base_command", protected_base_command},
    {"game.outcome.victory", victory},
    {"game.outcome.defeat", defeat},
    {"game.outcome.simultaneous_loss", simultaneous_loss},
    {"game.outcome.automatic_results", automatic_results},
    {"game.outcome.pause_resume", pause_resume},
    {"game.outcome.repeated_sessions", repeated_sessions},
    {"game.outcome.special_survival_flags", special_survival_flags},
    {"game.outcome.full_auto_escape", full_auto_escape},
    {"game.outcome.music_fade", music_fade},
    {"game.outcome.music_form_refill", music_form_refill},
    {"game.outcome.form_audio_device_error", form_audio_device_error},
    {"game.outcome.legacy_loss_cheat", legacy_loss_cheat},
    {"game.outcome.statistics_formatting", statistics_formatting},
    {"game.outcome.scheduled_capture", scheduled_capture},
    {"game.outcome.acquisition_after_removal", acquisition_after_removal},
    {"game.outcome.enemy_capture_rollback", enemy_capture_rollback},
    {"game.outcome.capture_queue_refund", capture_queue_refund},
    {"game.outcome.capture_income_continuity", capture_income_continuity},
    {"game.outcome.capture_turret_refund", capture_turret_refund},
    {"game.outcome.rival_armed_capture", rival_armed_capture},
    {"game.outcome.capture_player_placed_queue", capture_player_placed_queue},
    {"game.outcome.capture_move_reassignment", capture_move_reassignment},
    {"game.outcome.paused_orders", paused_orders},
    {"game.outcome.pending_result_pause", pending_result_pause},
    {"game.outcome.pending_result_late_elimination", pending_result_late_elimination},
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
