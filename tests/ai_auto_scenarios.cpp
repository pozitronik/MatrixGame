// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_flyer.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "cheats.hpp"
#include "test_support.hpp"
#include <sstream>
#include <vector>

namespace {
using namespace tests::battle;
void type_command(std::string_view command) {
    for (int i = 0; i < 16; ++i) MG_CHECK(!Cheats::processInput('Z'));
    for (unsigned i = 0; i < command.size(); ++i)
        MG_CHECK(Cheats::processInput(command[i]) == (i + 1 == command.size()));
}
Profile auto_profile(random::Mode mode, int step) {
    Profile p{mode, 84, step, 2000, false, true, true, tests::NavigationLayout::Open};
    return p;
}
std::vector<std::string> auto_activation_run(random::Mode mode, int step, bool eligible) {
    tests::BattleMaterialProfile materials;
    Session session(auto_profile(mode, step));
    tests::install_battle_transport();
    auto &side = *g_MatrixMap->GetPlayerSide();
    auto *player = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) {
            object->AsRobot()->GetEnv()->m_Place = -1;
            object->AsRobot()->SetTeam(0);
        }
    auto owned = tests::make_battle_building(*player->m_Unit[0].m_Graph->VO(), eligible ? PLAYER_SIDE : 2,
        BUILDING_TITAN, 100, 100);
    auto *factory = owned.get();
    g_MatrixMap->AddObject(owned.release(), true);
    for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
        g_Config.m_Timings[resource] = 100000;
    g_Config.m_MaintenanceTime = 20000;
    g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
    g_MatrixMap->SetMaintenanceTime(0);
    MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
    std::vector<std::string> trace;
    auto record = [&] {
        std::ostringstream out;
        out << session.snapshot() << ":flags=" << (g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM))
            << ":cooldown=" << g_MatrixMap->BeforeMaintenanceTime() << ":carrier=" << g_MatrixMap->m_AD_Obj_cnt
            << ":factory=" << factory->GetSide() << ':' << factory->m_TrueColor.m_ColoredCnt
            << ":capture=" << (player->GetCaptureFactory() == factory) << ":break=" << player->CanBreakOrder();
        if (player->GetGroupLogic() >= 0) {
            const auto &logic = side.m_LogicGroup[player->GetGroupLogic()];
            out << ":logic=" << logic.m_Action.m_Type << ':' << logic.RobotsCnt();
        }
        out << ":team=" << side.m_Team[0].m_Action.m_Type << ':' << side.m_Team[0].m_RobotCnt;
        if (g_MatrixMap->m_AD_Obj_cnt) {
            auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
            out << ":carrying=" << carrier->CarryingRobot();
            if (carrier->CarryingRobot()) {
                auto *cargo = carrier->GetCarryingRobot()->AsRobot();
                out << ':' << cargo->GetSide() << ':' << cargo->m_CurrState << ':' << cargo->GetHitPoint();
            }
        }
        trace.push_back(out.str());
    };
    record();
    session.ground_command(500, 200);
    record();
    while (session.time() < 200) { session.advance(); record(); }
    std::cout << session.metadata("ordinary-auto-v1") << " eligible=" << eligible << " decision_seed=79" << std::endl;
    std::cout << "before=" << session.time() << " player=" << player->m_PosX << ',' << player->m_PosY
        << " group=" << player->GetGroupLogic() << " order=" << session.robot(1).order << std::endl;
    MG_CHECK(session.time() == (step == 10 ? 200 : 208));
    MG_CHECK(session.robot(1).order == mpo_MoveTo && player->m_PosX > 110 && player->m_PosY == 200);
    MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && g_MatrixMap->BeforeMaintenanceTime() == 0);
    // Replay starts with this declared decision seed after the ordinary-control prelude.
    random::seed(79, mode);
    type_command("AUTO");
    MG_CHECK(random::draw_count() == 0);
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE));
    MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
    record();
    int request = 0;
    while (session.time() < 1200) {
        session.advance();
        if (!request && g_MatrixMap->m_AD_Obj_cnt) {
            request = session.time();
            auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
            MG_CHECK(carrier->CarryingRobot());
            auto *cargo = carrier->GetCarryingRobot()->AsRobot();
            MG_CHECK(cargo->GetSide() == PLAYER_SIDE && cargo->m_CurrState == ROBOT_CARRYING && cargo->GetHitPoint() == 150);
            MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 20000);
        }
        MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
        record();
    }
    const auto &group = side.m_LogicGroup[player->GetGroupLogic()];
    std::cout << "after=" << session.time() << " request=" << request << " action=" << group.m_Action.m_Type
        << " team=" << side.m_Team[0].m_Action.m_Type << " group=" << player->GetGroupLogic()
        << " count=" << group.RobotsCnt() << " player=" << player->m_PosX << ',' << player->m_PosY
        << " capture=" << (player->GetCaptureFactory() == factory) << " break=" << player->CanBreakOrder()
        << " cooldown=" << g_MatrixMap->BeforeMaintenanceTime() << " rng=" << random::draw_count() << std::endl;
    MG_CHECK(request == (eligible ? (step == 10 ? 210 : 224) : 0));
    MG_CHECK(group.m_Action.m_Type == (eligible ? mlat_Defence : mlat_Capture));
    MG_CHECK(side.m_Team[0].m_Action.m_Type == group.m_Action.m_Type);
    MG_CHECK(group.RobotsCnt() == (eligible ? 3 : 2));
    MG_CHECK((player->GetCaptureFactory() == factory) == !eligible && player->CanBreakOrder());
    MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == (eligible ? (step == 10 ? 19010 : 19024) : 0));
    MG_CHECK(random::draw_count() == (eligible ? (step == 10 ? 113u : 111u) : (step == 10 ? 110u : 108u)));
    const auto decision_draws = random::draw_count();
    type_command("AUTO");
    MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
    MG_CHECK(random::draw_count() == decision_draws);
    record();
    session.ground_command(120, 500);
    MG_CHECK(session.robot(1).order == mpo_MoveTo);
    int released = player->GetCaptureFactory() == nullptr ? session.time() : 0;
    record();
    while (session.time() < 4000) {
        session.advance();
        if (!released && player->GetCaptureFactory() == nullptr) released = session.time();
        record();
    }
    std::cout << "retake=" << session.time() << " released=" << released << " order=" << session.robot(1).order
        << " capture=" << (player->GetCaptureFactory() == factory) << " factory=" << factory->GetSide()
        << " player=" << player->m_PosX << ',' << player->m_PosY << " rng=" << random::draw_count() << std::endl;
    MG_CHECK(released == 1200 && player->GetCaptureFactory() == nullptr);
    MG_CHECK(session.robot(1).order == mpo_MoveTo && player->m_PosY > 300);
    MG_CHECK(factory->GetSide() == (eligible ? PLAYER_SIDE : 2));
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
    MG_CHECK(random::draw_count() == (eligible ? (mode == random::Mode::ParkMiller ? (step == 10 ? 372u : 371u)
        : (step == 10 ? 392u : 390u)) : (step == 10 ? 139u : 134u)));
    return trace;
}
void auto_activation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool eligible : {false, true}) {
        const auto first = auto_activation_run(mode, step, eligible);
        MG_CHECK(first == auto_activation_run(mode, step, eligible));
    }
}
constexpr tests::Case cases[] = {{"game.ai.auto_activation", auto_activation}};
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
