// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "Logic/MatrixEnvironment.h"
#include "Interface/CConstructor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

namespace {
using namespace tests::battle;
struct Definitions {
    Definitions() {
        MG_CHECK(!SSpecialBot::m_AIRobotTypeList && SSpecialBot::m_AIRobotTypeCnt == 0);
        std::fill(std::begin(g_Config.m_Price), std::end(g_Config.m_Price), 0);
        std::fill(std::begin(g_Config.m_WeaponStrengthAI), std::end(g_Config.m_WeaponStrengthAI), 0);
        g_Config.m_WeaponStrengthAI[RUK_WEAPON_MACHINEGUN] = 50;
        for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_MACHINEGUN - 1) * MAX_RESOURCES + resource] = 60;
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_REPAIR - 1) * MAX_RESOURCES + resource] = 60;
            for (auto &cannon : g_Config.m_CannonsProps) cannon.m_Resources[resource] = 1000;
        }
        for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
            g_Config.m_Timings[resource] = 100000;
        std::array<SRobotWeaponMatrix, ROBOT_ARMOR_CNT> capacities{};
        capacities[RUK_ARMOR_MONOSTACK - 1].common = 3;
        CBlockPar input;
        input.ParAdd(L"1", L"P,1,GGR");
        input.ParAdd(L"1", L"P,1,GR");
        SSpecialBot::LoadAIRobotType(input, capacities);
        MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 2);
        for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_Resources[resource] == 180);
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[1].m_Resources[resource] == 120);
        }
    }
    ~Definitions() { SSpecialBot::ClearAIRobotType(); }
};
CMatrixBuilding *building(int owner, EBuildingType kind, float x, float y) {
    auto owned = tests::make_battle_building(*tests::load_battle_mesh({}), owner, kind, x, y);
    auto *result = owned.get();
    result->m_TrueColor.m_Color = owner ? 0xff000000 | g_MatrixMap->GetSideColor(owner) : 0;
    result->m_TrueColor.m_ColoredCnt = owner ? MAX_ZAHVAT_POINTS : 0;
    g_MatrixMap->AddObject(owned.release(), true);
    return result;
}

struct ReleaseCore { void operator()(SObjectCore *core) const { core->Release(); } };
using Core = std::unique_ptr<SObjectCore, ReleaseCore>;
enum class Loss { None, Objective, Home };

struct Journey {
    std::vector<std::string> frames;
    int primary_paint = -1;
    int primary_order = -1;
    int primary_owned = -1;
    int backup_owned = -1;
    int replacement = -1;
};

Journey capture_journey(random::Mode mode, int step, Loss loss) {
    Profile profile{mode, 84, step, 48000 / step, false, false};
    profile.layout = tests::NavigationLayout::MultiRegion;
    Session session(profile);
    Definitions definitions;
    auto &map = *g_MatrixMap;
    auto &side = *map.GetSideById(2);
    map.GetPlayerSide()->m_Color = 0x0000ff00;
    side.m_Color = 0x00ff0000;
    for (int resource = 0; resource < MAX_RESOURCES; ++resource)
        side.SetResourceAmount(static_cast<ERes>(resource), 0);
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsRobot()) continue;
        auto *robot = object->AsRobot();
        if (robot->GetSide() == PLAYER_SIDE) {
            robot->SetMaxSpeed(0);
            robot->m_GroupSpeed = 0;
        }
    }
    auto *home = building(2, BUILDING_BASE, 500, 100);
    auto *primary = building(0, BUILDING_TITAN, 100, 500);
    auto *backup = building(0, BUILDING_ENERGY, 200, 100);
    Core primary_core(primary->GetCore(DEBUG_CALL_INFO));
    Core home_core(home->GetCore(DEBUG_CALL_INFO));
    Core backup_core(backup->GetCore(DEBUG_CALL_INFO));
    std::vector<SMatrixMapUnit *> home_cells;
    for (int y = 0; y < map.m_Size.y; ++y)
        for (int x = 0; x < map.m_Size.x; ++x)
            if (map.UnitGet(x, y)->m_Base == home) home_cells.push_back(map.UnitGet(x, y));
    MG_CHECK(home_cells.size() == 9);
    std::vector<CMatrixMapStatic *> robots;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot()) robots.push_back(object);
    MG_CHECK(robots.size() == 4 && side.m_TeamCnt == 3);
    // Match ReloadDynamics' building-before-robot scheduler order. Positions
    // stay untouched: factories use cached geometric centres for proximity.
    for (auto *robot : robots) {
        MG_CHECK(std::abs(robot->GetGeoCenter().x - robot->AsRobot()->m_PosX) < 0.001f);
        MG_CHECK(std::abs(robot->GetGeoCenter().y - robot->AsRobot()->m_PosY) < 0.001f);
        robot->DelLT(); robot->AddLT();
    }
    MG_CHECK(CMatrixMapStatic::GetFirstLogic() == home);
    std::cout << session.metadata("strategic-capture-journey-v1") << std::endl;
    std::cout << "variant=" << int(loss) << " loss_time_ms=6000 horizon_ms=48000\n";
    Journey result;
    result.frames.reserve(profile.max_frames);
    std::array<bool, 4> original_capturer{};
    std::array<int, 4> ungrouped_since{};
    bool forward = false, capture = false, crossed = false, defence = false;
    int first_backup_order = -1;
    int first_capture_action = -1;
    while (session.frame() < profile.max_frames) {
        if (session.time() == 6000) {
            int assigned = 0;
            for (size_t i = 0; i < robots.size(); ++i) {
                auto *robot = robots[i]->AsRobot();
                original_capturer[i] = robot->GetSide() == 2 && robot->GetCaptureFactory() == primary;
                assigned += original_capturer[i];
            }
            MG_CHECK(assigned > 0 && primary->m_TrueColor.m_ColoredCnt == 0 && primary->GetSide() == 0);
            MG_CHECK(forward && capture && crossed);
            MG_CHECK(result.primary_order > 0 && result.primary_order <= first_capture_action);
            if (mode == random::Mode::LegacyCRT && step == 10)
                MG_CHECK(result.primary_order < first_capture_action);
            if (loss == Loss::Objective) map.StaticDelete(primary);
            if (loss == Loss::Home) map.StaticDelete(home);
            MG_CHECK(bool(primary_core->m_Object) == (loss != Loss::Objective));
            MG_CHECK(bool(home_core->m_Object) == (loss != Loss::Home));
            for (auto *cell : home_cells) MG_CHECK(cell->m_Base == (loss == Loss::Home ? nullptr : home));
        }
        try { session.advance(); }
        catch (const Base::CException &error) {
            std::wcerr << error.Info() << std::endl;
            throw std::runtime_error("Engine exception at frame " + std::to_string(session.frame()));
        }
        MG_CHECK(!map.IsPaused());
        const auto draws_before_observation = random::draw_count();
        int live_robots = 0;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            MG_CHECK(!object->IsCannon());
            if (object->IsRobot()) ++live_robots;
        }
        MG_CHECK(live_robots == 4 && backup_core->m_Object == backup);
        for (int resource = 0; resource < MAX_RESOURCES; ++resource)
            MG_CHECK(side.GetResourcesAmount(static_cast<ERes>(resource)) == 0);
        if (home_core->m_Object) MG_CHECK(home->m_BS.GetItemsCnt() == 0);
        std::ostringstream decision;
        decision << "{\"time_ms\":" << session.time() << ",\"teams\":[";
        for (int t = 0; t < side.m_TeamCnt; ++t) {
            const auto &team = side.m_Team[t];
            MG_CHECK(team.m_Action.m_RegionPathCnt >= 0 && team.m_Action.m_RegionPathCnt <= REGION_PATH_MAX_CNT);
            if (t) decision << ',';
            decision << '[' << int(team.m_Action.m_Type) << ',' << team.m_Action.m_Region << ','
                     << team.m_RegionMass << ',' << team.m_RegionNerestBase << ','
                     << team.m_RobotCnt << ',' << team.m_GroupCnt << ',' << team.m_Action.m_RegionPathCnt;
            for (int p = 0; p < team.m_Action.m_RegionPathCnt; ++p)
                decision << ',' << team.m_Action.m_RegionPath[p];
            decision << ']';
            if (!team.m_RobotCnt) continue;
            if (team.m_Action.m_Type == mlat_Forward) {
                forward = true;
                MG_CHECK(team.m_Action.m_Region == 0);
            }
            capture |= team.m_Action.m_Type == mlat_Capture;
            if (team.m_RobotCnt && team.m_Action.m_Type == mlat_Capture && first_capture_action < 0)
                first_capture_action = session.time();
            defence |= session.time() > 40000 && team.m_Action.m_Type == mlat_Defence;
            if (loss == Loss::Home && session.time() >= 6500)
                MG_CHECK(team.m_RegionNerestBase == -1);
        }
        if (session.frame() == 1) {
            const auto &team = side.m_Team[0];
            MG_CHECK(team.m_Action.m_Type == mlat_Forward && team.m_RegionMass == 1);
            MG_CHECK(team.m_Action.m_RegionPathCnt == 2);
            MG_CHECK(team.m_Action.m_RegionPath[0] == 1 && team.m_Action.m_RegionPath[1] == 0);
            MG_CHECK(team.m_RoadPath && team.m_RoadPath->m_ListCnt > 0);
        }
        decision << "],\"members\":[";
        for (size_t i = 0; i < robots.size(); ++i) {
            auto *robot = robots[i]->AsRobot();
            MG_CHECK(session.matches(static_cast<EntityId>(i + 1), robot));
            MG_CHECK(session.robot(static_cast<EntityId>(i + 1)).alive);
            MG_CHECK(std::isfinite(robot->m_PosX) && std::isfinite(robot->m_PosY));
            MG_CHECK(robot->m_PosX >= 0 && robot->m_PosX < 640 && robot->m_PosY >= 0 && robot->m_PosY < 640);
            const int group = robot->GetGroupLogic();
            if (i) decision << ',';
            auto *target = robot->GetCaptureFactory();
            if (session.time() >= 45000) MG_CHECK(target == nullptr);
            const int target_id = !target ? 0 : target == primary_core->m_Object ? 1 :
                                  target == backup ? 2 : target == home_core->m_Object ? 3 : -1;
            MG_CHECK(target_id >= 0);
            decision << '[' << i + 1 << ',' << robot->GetTeam() << ',' << group << ',' << robot->GetRegion()
                     << ',' << target_id;
            if (robot->GetSide() == 2) {
                MG_CHECK(robot->GetTeam() >= 0 && robot->GetTeam() < side.m_TeamCnt);
                MG_CHECK(group >= -1 && group < MAX_LOGIC_GROUP);
                // Empty-team redistribution clears the old group; Regroup on
                // the next scheduled high-level pass installs its replacement.
                if (group < 0) {
                    if (!ungrouped_since[i]) ungrouped_since[i] = session.time();
                    MG_CHECK(session.time() - ungrouped_since[i] < 200);
                } else {
                    ungrouped_since[i] = 0;
                    const auto &logic = side.m_LogicGroup[group];
                    decision << ',' << logic.RobotsCnt() << ',' << logic.m_Team << ','
                             << int(logic.m_Action.m_Type) << ',' << logic.m_Action.m_Region;
                }
                crossed |= robot->GetRegion() == 0;
                if (target == primary_core->m_Object && target && result.primary_order < 0)
                    result.primary_order = session.time();
                if (target == backup && first_backup_order < 0) first_backup_order = session.time();
                if (original_capturer[i] && target == backup && result.replacement < 0)
                    result.replacement = session.time();
            }
            decision << ']';
        }
        decision << "],\"buildings\":[";
        int b = 0;
        for (auto *core : {primary_core.get(), backup_core.get(), home_core.get()}) {
            if (b++) decision << ',';
            auto *object = core->m_Object;
            if (!object) { decision << "null"; continue; }
            auto *factory = object->AsBuilding();
            decision << '[' << factory->GetSide() << ',' << factory->m_TrueColor.m_ColoredCnt << ','
                     << factory->m_TrueColor.m_Color << ']';
        }
        decision << "]}";
        const std::string observed = decision.str();
        result.frames.push_back(session.snapshot() + "\n" + observed);
        MG_CHECK(random::draw_count() == draws_before_observation);
        const int paint = primary_core->m_Object ? primary->m_TrueColor.m_ColoredCnt : -1;
        if (paint > 0 && result.primary_paint < 0) result.primary_paint = session.time();
        if (primary_core->m_Object && primary->GetSide() == 2 && result.primary_owned < 0)
            result.primary_owned = session.time();
        if (backup->GetSide() == 2 && result.backup_owned < 0) result.backup_owned = session.time();
        if (session.time() % 1000 == 0) std::cout << observed << '\n';
        if (session.time() >= 45000) {
            for (auto *core : {primary_core.get(), backup_core.get()}) if (core->m_Object) {
                auto *factory = core->m_Object->AsBuilding();
                MG_CHECK(factory->GetSide() == 2 && factory->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
                MG_CHECK(factory->m_TrueColor.m_Color == (0xff000000 | side.m_Color));
            }
        }
    }
    MG_CHECK(forward && capture && crossed && defence);
    MG_CHECK(first_backup_order > 0 && result.backup_owned > first_backup_order);
    if (loss == Loss::Objective) {
        MG_CHECK(result.primary_paint == -1 && result.primary_owned == -1);
        MG_CHECK(result.replacement > 6000 && result.replacement < result.backup_owned);
    } else {
        MG_CHECK(result.primary_paint > 6000 && result.primary_owned > result.primary_paint);
    }
    std::cout << "milestones capture_action=" << first_capture_action << " primary_order=" << result.primary_order
              << " primary_paint=" << result.primary_paint << " primary_owned=" << result.primary_owned
              << " backup_order=" << first_backup_order << " backup_owned=" << result.backup_owned
              << " replacement=" << result.replacement << '\n';
    return result;
}
template<random::Mode Mode, int Step, Loss Reason>
void run() {
    const auto first = capture_journey(Mode, Step, Reason);
    const auto replay = capture_journey(Mode, Step, Reason);
    MG_CHECK(first.frames == replay.frames);
    if constexpr (Reason != Loss::None) {
        const auto control = capture_journey(Mode, Step, Loss::None);
        const size_t common_frames = 6000 / Step;
        MG_CHECK(std::equal(first.frames.begin(), first.frames.begin() + common_frames, control.frames.begin()));
        MG_CHECK(first.frames.back() != control.frames.back());
        if constexpr (Reason == Loss::Home) {
            MG_CHECK(first.primary_paint == control.primary_paint);
            MG_CHECK(first.primary_owned == control.primary_owned);
        }
    }
}

template<int Step>
void rng_sensitivity() {
    const auto park = capture_journey(random::Mode::ParkMiller, Step, Loss::None);
    const auto legacy = capture_journey(random::Mode::LegacyCRT, Step, Loss::None);
    // Compare an actual gameplay event, not merely the RNG state in snapshots.
    MG_CHECK(park.backup_owned != legacy.backup_owned);
}

constexpr tests::Case cases[]{
    {"game.ai.strategic_capture.control.park_miller10", run<random::Mode::ParkMiller, 10, Loss::None>},
    {"game.ai.strategic_capture.objective_loss.park_miller10", run<random::Mode::ParkMiller, 10, Loss::Objective>},
    {"game.ai.strategic_capture.home_loss.park_miller10", run<random::Mode::ParkMiller, 10, Loss::Home>},
    {"game.ai.strategic_capture.control.park_miller16", run<random::Mode::ParkMiller, 16, Loss::None>},
    {"game.ai.strategic_capture.objective_loss.park_miller16", run<random::Mode::ParkMiller, 16, Loss::Objective>},
    {"game.ai.strategic_capture.home_loss.park_miller16", run<random::Mode::ParkMiller, 16, Loss::Home>},
    {"game.ai.strategic_capture.control.legacy10", run<random::Mode::LegacyCRT, 10, Loss::None>},
    {"game.ai.strategic_capture.objective_loss.legacy10", run<random::Mode::LegacyCRT, 10, Loss::Objective>},
    {"game.ai.strategic_capture.home_loss.legacy10", run<random::Mode::LegacyCRT, 10, Loss::Home>},
    {"game.ai.strategic_capture.control.legacy16", run<random::Mode::LegacyCRT, 16, Loss::None>},
    {"game.ai.strategic_capture.objective_loss.legacy16", run<random::Mode::LegacyCRT, 16, Loss::Objective>},
    {"game.ai.strategic_capture.home_loss.legacy16", run<random::Mode::LegacyCRT, 16, Loss::Home>},
    {"game.ai.strategic_capture.rng_sensitivity.step10", rng_sensitivity<10>},
    {"game.ai.strategic_capture.rng_sensitivity.step16", rng_sensitivity<16>},
};
}  // namespace

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
