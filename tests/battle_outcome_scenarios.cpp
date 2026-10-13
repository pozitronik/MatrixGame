// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "input.hpp"
#include "cheats.hpp"
#include <climits>
#include <string>

namespace {
using namespace tests::battle;
CMatrixSideUnit &side() { return *g_MatrixMap->GetPlayerSide(); }
void press(CFormMatrixGame &form, uint8_t key) { form.Keyboard(true, key); form.Keyboard(false, key); }
void advance_to(Session &session, int time) {
    MG_CHECK(time >= session.time());
    while (session.time() < time) session.advance();
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
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CaptureData fixture;
        auto &building = *fixture.building;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 0 && building.GetSide() == 0);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 1);
        advance_to(session, 500);
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 1);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 2);
        while (session.time() < 6500) { session.advance(); fixture.capture(); }
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 13 && building.GetSide() == 0);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.m_TrueColor.m_ColoredCnt == 14 && building.GetSide() == 0);
        advance_to(session, 7000);
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
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CaptureData fixture(2);
        auto &building = *fixture.building;
        MG_CHECK(fixture.capture() == CAPTURE_BUSY);
        for (int i = 0; i < 975; ++i) { session.advance(); fixture.capture(); }
        MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_ColoredCnt == 1);
        session.advance();
        MG_CHECK(fixture.capture() == CAPTURE_INPROGRESS);
        MG_CHECK(building.GetSide() == 2 && building.m_TrueColor.m_ColoredCnt == 0);
        advance_to(session, 10500);
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
    Session session;
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
void capture_rollback() {
    Session session;
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
    building.LogicTakt(10);
    MG_CHECK(building.m_TrueColor.m_ColoredCnt == 0 && building.GetSide() == 0);
}
void outcome(bool player_loses, bool full_auto, bool simultaneous = false) {
    Session session;
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
    advance_to(session, 2010);
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
void victory() { outcome(false, false); }
void defeat() { outcome(true, false); }
void simultaneous_loss() { outcome(true, false, true); }
void automatic_results() { outcome(false, true); outcome(true, true); }
void pause_resume() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
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
        MG_CHECK(session.time() == time + 10);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1490);
        for (int i = 0; i < 1470; ++i) session.advance();
        MG_CHECK(session.robot(1).in_place && session.robot(1).x > 400);
    }
}
void special_survival_flags() {
    for (DWORD flag : {MMFLAG_TERRON_DEAD, MMFLAG_SPECIAL_BROKEN}) {
        Session session;
        DialogData dialog;
        SETFLAG(g_MatrixMap->m_Flags, flag);
        MG_CHECK(session.remove(1) && session.remove(2));
        advance_to(session, 2020);
        MG_CHECK(side().GetStatus() == SS_ACTIVE);
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN));
        MG_CHECK(!g_MatrixMap->IsPaused());
        MG_CHECK(g_MatrixMap->m_DialogModeName == nullptr);
    }
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
void repeated_sessions() {
    for (int run = 0; run < 3; ++run) {
        outcome(run % 2 == 0, true);
        MG_CHECK(g_MatrixMap == nullptr && g_IFaceList == nullptr && g_MatrixHeap == nullptr);
        MG_CHECK(CMatrixMapStatic::GetFirstLogic() == nullptr);
        MG_CHECK(!Input::isVKeyPressed(VK_CONTROL));
        Session fresh;
        MG_CHECK(fresh.time() == 0 && fresh.frame() == 0);
        MG_CHECK(side().GetStatus() == SS_ACTIVE);
        MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 2);
        MG_CHECK(fresh.robot(1).group == -1 && fresh.robot(2).group == -1);
        MG_CHECK(!g_MatrixMap->IsPaused());
        MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
    }
}
constexpr tests::Case cases[] = {
    {"game.outcome.neutral_capture", neutral_capture},
    {"game.outcome.enemy_capture", enemy_capture},
    {"game.outcome.capture_rejection", capture_rejection},
    {"game.outcome.capture_rollback", capture_rollback},
    {"game.outcome.victory", victory},
    {"game.outcome.defeat", defeat},
    {"game.outcome.simultaneous_loss", simultaneous_loss},
    {"game.outcome.automatic_results", automatic_results},
    {"game.outcome.pause_resume", pause_resume},
    {"game.outcome.repeated_sessions", repeated_sessions},
    {"game.outcome.special_survival_flags", special_survival_flags},
    {"game.outcome.full_auto_escape", full_auto_escape},
    {"game.outcome.legacy_loss_cheat", legacy_loss_cheat},
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
