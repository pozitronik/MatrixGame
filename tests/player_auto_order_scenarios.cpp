// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "input.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
using namespace tests;
using namespace tests::battle;
enum class AutoOrder { Capture, Attack, Defence };
constexpr DWORD auto_flags = AUTO_FROBOT_ON | AUTO_CAPTURE_ON | AUTO_PROTECT_ON;
const char *name(AutoOrder order) {
    switch (order) {
    case AutoOrder::Capture: return "capture";
    case AutoOrder::Attack: return "attack";
    case AutoOrder::Defence: return "defence";
    }
    throw std::logic_error("Unknown auto order");
}
void press(CFormMatrixGame &form, uint8_t key) {
    form.Keyboard(true, key);
    form.Keyboard(false, key);
    MG_CHECK(!Input::isVKeyPressed(key));
}
struct Result {
    std::vector<std::string> states;
    std::size_t prefix = 0;
    int paint = 0, ownership = 0, stopped = 0;
    int hit = 0, retired = 0, replacement = 0;
    int override_time = 0, arrival = 0;
    float final_x = 0, final_y = 0;
};

Result journey(random::Mode mode, int step, AutoOrder order, bool override_order, int replay) {
    const bool capture = order == AutoOrder::Capture;
    const uint8_t key = capture ? 'C' : order == AutoOrder::Attack ? 'U' : 'D';
    const auto automatic_order = capture ? mpo_AutoCapture : order == AutoOrder::Attack ? mpo_AutoAttack : mpo_AutoDefence;
    const DWORD initial_flag = capture ? AUTO_CAPTURE_ON : order == AutoOrder::Attack ? AUTO_FROBOT_ON : AUTO_PROTECT_ON;
    const int deadline = capture ? 9000 : 1600;
    BattleCombatAssets assets;
    Profile profile{mode, 80, step, 3000, false, true, false, NavigationLayout::Open};
    Session session(profile);
    assets.install();
    CFormMatrixGame form;
    auto &map = *g_MatrixMap;
    auto &side = *map.GetPlayerSide();
    std::array<CMatrixRobotAI *, 4> robots{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot())
            for (unsigned id = 1; id <= robots.size(); ++id)
                if (session.matches(id, object->AsRobot())) robots[id - 1] = object->AsRobot();
    for (auto *robot : robots) MG_CHECK(robot);
    auto *actor = robots[0], *target = robots[2];
    const auto relocate = [&](CMatrixRobotAI &robot, float x, float y) {
        robot.m_PosX = x;
        robot.m_PosY = y;
        robot.MapPosCalc();
        robot.RChange(MR_Matrix);
        robot.RNeed(MR_Matrix);
        robot.m_CalcBoundsLastTime = map.GetTime() - 100;
        robot.JoinToGroup();
        MG_CHECK(std::abs(robot.GetGeoCenter().x - x) < .001f);
        MG_CHECK(std::abs(robot.GetGeoCenter().y - y) < .001f);
    };
    relocate(*robots[1], 300, 40);
    for (auto *robot : {robots[2], robots[3]}) {
        robot->SetMaxSpeed(0);
        robot->m_GroupSpeed = 0;
    }
    CMatrixBuilding *factory = nullptr;
    if (capture) {
        auto owned = make_battle_building(*actor->m_Unit[0].m_Graph->VO(), 0, BUILDING_TITAN, 160, 200);
        factory = owned.get();
        side.m_Color = 0x0000ff00;
        map.GetSideById(2)->m_Color = 0x00ff0000;
        factory->m_TrueColor.m_Color = 0;
        factory->m_TrueColor.m_ColoredCnt = 0;
        for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_ELECTRONICS, RESOURCE_ENERGY, RESOURCE_PLASMA})
            g_Config.m_Timings[resource] = 100000;
        map.AddObject(owned.release(), true);
        // Match the production loader's buildings-before-starting-robots order.
        for (auto *robot : robots) { robot->DelLT(); robot->AddLT(); }
    }
    else {
        relocate(*target, 260, 200);
        target->InitMaxHitpoint(200);
        robots[3]->InitMaxHitpoint(2000);
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 220;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {2, 0, 0};
        g_Config.m_WeaponStrengthAI[RUK_WEAPON_LASER] = 10;
        g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
        g_Config.m_Overheat[WEAPON_LASER_COOL] = 10;
        g_Config.m_Overheat[WCP_LASER] = 100;
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = .04f;
        arm_battle_robot(*actor, RUK_WEAPON_LASER);
    }
    const auto release = [](SObjectCore *core) { core->Release(); };
    std::unique_ptr<SObjectCore, decltype(release)> target_core(target->GetCore(DEBUG_CALL_INFO), release);
    side.CreateGroupFromCurrent(actor);
    side.Select(ROBOT, nullptr);

    const auto directory = std::filesystem::current_path() / "scenario-traces";
    std::filesystem::create_directories(directory);
    const auto path = directory / (std::string("player-auto-") + name(order) +
        (override_order ? "-override-" : "-control-") + (mode == random::Mode::ParkMiller ? "pm-" : "crt-") +
        std::to_string(step) + "-" + std::to_string(replay) + ".jsonl");
    std::ofstream file(path, std::ios::trunc);
    MG_CHECK(file.good());
    auto header = session.metadata("player-auto-order-continuation-v1");
    header.pop_back();
    header += ",\"auto_order\":\"" + std::string(name(order)) + "\",\"auto_key\":" +
        std::to_string(key) + ",\"comparison_boundary_ms\":" + std::to_string(deadline) +
        ",\"manual_destination\":[100,400],\"stationary_opponents\":true,\"manual_override\":" +
        (override_order ? "true}" : "false}");
    file << header << '\n';
    file.flush();
    std::cerr << "Auto-order profile: " << header << "\nTrace: " << path.string() << std::endl;
    Result result;
    const auto id_of = [&](CMatrixMapStatic *object) {
        if (!object) return 0;
        if (factory && object == factory) return 5;
        for (unsigned i = 0; i < robots.size(); ++i)
            if (object == robots[i]) return i == 2 && !target_core->m_Object ? -3 : int(i + 1);
        return -1;
    };
    const auto record = [&] {
        const auto draws = random::draw_count();
        std::ostringstream out;
        out << std::boolalpha << "{\"type\":\"snapshot\",\"state\":" << session.snapshot()
            << ",\"auto_flags\":" << (g_IFaceList->m_IfListFlags & auto_flags) << ",\"selected\":[";
        auto *selection = side.GetCurGroup();
        if (selection)
            for (int i = 0; i < selection->GetObjectsCnt(); ++i) {
                if (i) out << ',';
                out << id_of(selection->GetObjectByN(i));
            }
        auto &env = *actor->GetEnv();
        CPoint move(-1, -1);
        const bool moving = actor->GetMoveToCoords(move);
        const int group = actor->GetGroupLogic();
        out << "],\"action\":{\"target\":" << id_of(env.m_Target)
            << ",\"attack_target\":" << id_of(env.m_TargetAttack) << ",\"enemies\":" << env.GetEnemyCnt()
            << ",\"place\":" << env.m_Place << ",\"additional_place\":[" << env.m_PlaceAdd.x << ',' << env.m_PlaceAdd.y
            << "],\"last_fire\":" << env.m_LastFire << ",\"last_hit\":" << env.m_LastHitTarget
            << ",\"target_change\":" << env.m_TargetChange << ",\"failed_place\":" << env.m_PlaceNotFound
            << ",\"moving\":" << moving << ",\"move\":[" << move.x << ',' << move.y << "]"
            << ",\"fire_order\":" << actor->FindOrderLikeThat(ROT_FIRE)
            << ",\"group_target\":" << (group >= 0 ? id_of(side.m_PlayerGroup[group].m_Obj) : 0)
            << ",\"war\":" << (group >= 0 && side.m_PlayerGroup[group].IsWar()) << '}';
        if (factory)
            out << ",\"factory\":{\"owner\":" << factory->GetSide() << ",\"paint\":" << factory->m_TrueColor.m_ColoredCnt
                << ",\"color\":" << factory->m_TrueColor.m_Color << ",\"capturer\":" << id_of(factory->m_Capturer)
                << ",\"actor_capture\":" << (actor->GetCaptureFactory() == factory)
                << ",\"opponent_capture\":" << (target->GetCaptureFactory() == factory)
                << ",\"opponent_capture_order\":" << target->FindOrderLikeThat(ROT_CAPTURE_FACTORY) << '}';
        out << ",\"resources\":[";
        for (int side_id = 1; side_id <= 2; ++side_id)
            for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
                if (side_id != 1 || resource) out << ',';
                out << map.GetSideById(side_id)->GetResourcesAmount(static_cast<ERes>(resource));
            }
        out << "]}";
        MG_CHECK(random::draw_count() == draws);
        result.states.push_back(out.str());
        file << result.states.back() << '\n';
        file.flush();
        MG_CHECK(file.good());
    };
    const auto event = [&](const char *action, std::uint64_t before) {
        file << "{\"type\":\"command\",\"frame\":" << session.frame()
            << ",\"accepted\":true,\"time_ms\":" << session.time() << ",\"name\":\"" << action
            << "\",\"rng_draws_before\":" << before << ",\"rng_draws_after\":" << random::draw_count() << "}\n";
    };
    record();
    const auto before_auto = random::draw_count();
    press(form, key);
    MG_CHECK(session.robot(1).order == automatic_order);
    MG_CHECK((g_IFaceList->m_IfListFlags & auto_flags) == initial_flag);
    event("enable-auto-order", before_auto);
    record();
    bool boundary = false;
    while (session.time() < 25000) {
        MG_CHECK(!map.IsPaused());
        session.advance();
        MG_CHECK(session.robot(1).alive && session.robot(2).alive && session.robot(4).alive);
        for (unsigned id = 1; id <= robots.size(); ++id) {
            const auto state = session.robot(id);
            if (!state.alive) continue;
            MG_CHECK(std::isfinite(state.x) && std::isfinite(state.y));
            MG_CHECK(state.x >= 0 && state.x < 640 && state.y >= 0 && state.y < 640);
        }
        MG_CHECK(robots[1]->m_PosX == 300 && robots[1]->m_PosY == 40);
        if (capture) {
            MG_CHECK(session.robot(3).alive);
            MG_CHECK(factory->GetSide() == 0 || factory->GetSide() == PLAYER_SIDE);
            if (!result.paint && factory->m_TrueColor.m_ColoredCnt) {
                result.paint = session.time();
                MG_CHECK(factory->m_TrueColor.m_ColoredCnt == 1);
                record();
                MG_CHECK(actor->GetCaptureFactory() == factory);
                MG_CHECK(target->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
                MG_CHECK(target->GetCaptureFactory() == factory);
                MG_CHECK(factory->m_Capturer == target);
            }
            if (!result.ownership && factory->GetSide() == PLAYER_SIDE) {
                result.ownership = session.time();
                MG_CHECK(result.paint > 0 && factory->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
            }
            if (result.ownership) {
                MG_CHECK(factory->GetSide() == PLAYER_SIDE && factory->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
                MG_CHECK(factory->m_TrueColor.m_Color == (0xff000000 | map.GetSideColor(PLAYER_SIDE)));
                if (!result.stopped && session.robot(1).order == mpo_Stop && !actor->GetCaptureFactory())
                    result.stopped = session.time();
            }
        }
        else {
            if (!result.hit && target->GetHitPoint() < 20) {
                result.hit = session.time();
                MG_CHECK(result.hit < 1000 && target->GetHitPoint() > 0);
                MG_CHECK(side.m_PlayerGroup[actor->GetGroupLogic()].IsWar());
                MG_CHECK(actor->GetEnv()->m_TargetAttack == target && actor->GetEnv()->m_LastHitTarget > 0);
                MG_CHECK(actor->FindOrderLikeThat(ROT_FIRE));
                record();
                const auto before_remove = random::draw_count();
                MG_CHECK(session.remove(3));
                result.retired = session.time();
                MG_CHECK(target_core->m_Object == nullptr);
                event("retire-hit-enemy", before_remove);
                record();
            }
            if (result.retired) {
                MG_CHECK(target_core->m_Object == nullptr && !session.robot(3).alive);
                MG_CHECK(actor->GetEnv()->m_Target != target && actor->GetEnv()->m_TargetAttack != target);
                if (!result.replacement && session.robot(1).target == 4) result.replacement = session.time();
            }
        }
        if (result.override_time) {
            const auto state = session.robot(1);
            MG_CHECK(state.order == mpo_MoveTo && state.target_is_null);
            MG_CHECK(state.destination_x == 8 && state.destination_y == 38);
            MG_CHECK(!side.m_PlayerGroup[state.group].IsWar());
            if (!capture) MG_CHECK(robots[3]->GetHitPoint() == 200);
            if (!result.arrival && state.in_place && std::abs(state.x - 100) < .01f && std::abs(state.y - 400) < .01f)
                result.arrival = session.time();
            if (result.arrival) MG_CHECK(state.in_place && std::abs(state.x - 100) < .01f && std::abs(state.y - 400) < .01f);
        }
        record();
        if (!boundary && session.time() >= deadline) {
            boundary = true;
            if (capture) {
                MG_CHECK(result.paint > 0 && result.ownership > result.paint && result.stopped >= result.ownership);
                MG_CHECK(session.robot(1).order == mpo_Stop && !actor->GetCaptureFactory());
                MG_CHECK(target->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
                MG_CHECK(target->GetCaptureFactory() == factory);
                MG_CHECK(factory->m_Capturer == target);
            }
            else {
                MG_CHECK(result.hit > 0 && result.retired == result.hit && result.replacement > result.retired);
                MG_CHECK(session.robot(1).order == automatic_order && session.robot(1).target == 4);
                MG_CHECK(robots[3]->GetHitPoint() == 200);
            }
            result.prefix = result.states.size();
            if (override_order) {
                const auto before_selection = random::draw_count();
                side.Select(NOTHING, nullptr);
                side.ResetSystemSelection();
                MG_CHECK(!side.GetCurGroup() || side.GetCurGroup()->GetObjectsCnt() == 0);
                event("clear-selection", before_selection);
                record();
                const auto before_select = random::draw_count();
                side.CreateGroupFromCurrent(actor);
                side.Select(ROBOT, nullptr);
                MG_CHECK(side.GetCurGroup()->GetObjectsCnt() == 1 && side.GetCurGroup()->GetObjectByN(0) == actor);
                event("reselect-surviving-robot", before_select);
                record();
                const auto before_move = random::draw_count();
                session.ground_command(100, 400);
                result.override_time = session.time();
                MG_CHECK(session.robot(1).order == mpo_MoveTo);
                MG_CHECK(session.robot(1).destination_x == 8 && session.robot(1).destination_y == 38);
                event("manual-move-100-400", before_move);
                record();
            }
        }
        if (boundary && !override_order) MG_CHECK(session.robot(1).order == (capture ? mpo_Stop : automatic_order));
        MG_CHECK(side.GetStatValue(STAT_ROBOT_KILL) == 0 && map.GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    }
    MG_CHECK(boundary);
    result.final_x = actor->m_PosX;
    result.final_y = actor->m_PosY;
    if (override_order) {
        MG_CHECK(result.override_time >= deadline && result.override_time < deadline + step);
        MG_CHECK(result.arrival > result.override_time && result.arrival < 20000);
        MG_CHECK(session.time() - result.arrival >= 600);
    }
    else MG_CHECK(std::hypot(result.final_x - 100, result.final_y - 400) > 50);
    session.check_running();
    file << "{\"type\":\"complete\",\"frame\":" << session.frame() << ",\"time_ms\":" << session.time()
        << ",\"paint_ms\":" << result.paint << ",\"ownership_ms\":" << result.ownership
        << ",\"stopped_ms\":" << result.stopped << ",\"hit_ms\":" << result.hit
        << ",\"retired_ms\":" << result.retired << ",\"replacement_ms\":" << result.replacement
        << ",\"command_ms\":" << result.override_time << ",\"arrival_ms\":" << result.arrival
        << ",\"terminal_order\":" << session.robot(1).order
        << ",\"terminal_x_bits\":" << std::bit_cast<uint32_t>(result.final_x)
        << ",\"terminal_y_bits\":" << std::bit_cast<uint32_t>(result.final_y) << "}\n";
    file.flush();
    MG_CHECK(file.good());
    std::cout << "Auto-order milestones: " << name(order) << " override=" << override_order
        << " paint=" << result.paint << " ownership=" << result.ownership << " stopped=" << result.stopped
        << " hit=" << result.hit << " retired=" << result.retired << " replacement=" << result.replacement
        << " command=" << result.override_time << " arrival=" << result.arrival << '\n';
    return result;
}

template<random::Mode Mode, int Step, AutoOrder Order, bool Control> void run() {
    if constexpr (Control) {
        const auto control = journey(Mode, Step, Order, false, 2);
        MG_CHECK(control.states == journey(Mode, Step, Order, false, 3).states);
    }
    else {
        const auto main = journey(Mode, Step, Order, true, 0);
        MG_CHECK(main.states == journey(Mode, Step, Order, true, 1).states);
        const auto control = journey(Mode, Step, Order, false, 0);
        MG_CHECK(control.states == journey(Mode, Step, Order, false, 1).states);
        MG_CHECK(main.prefix == control.prefix && main.prefix > 0);
        MG_CHECK(std::equal(main.states.begin(), main.states.begin() + main.prefix, control.states.begin()));
        MG_CHECK(main.states.back() != control.states.back());
    }
}
constexpr Case cases[]{
#define AUTO_CASE(mode, label, step, kind, text) \
    {"game.player.auto_order." text "_" label "_" #step, run<random::Mode::mode, step, AutoOrder::kind, false>}, \
    {"game.player.auto_order." text "_control_" label "_" #step, run<random::Mode::mode, step, AutoOrder::kind, true>}
#define AUTO_PROFILE(mode, label, step) \
    AUTO_CASE(mode, label, step, Capture, "capture"), AUTO_CASE(mode, label, step, Attack, "attack"), \
    AUTO_CASE(mode, label, step, Defence, "defence")
    AUTO_PROFILE(ParkMiller, "park_miller", 10), AUTO_PROFILE(ParkMiller, "park_miller", 16),
    AUTO_PROFILE(LegacyCRT, "legacy_crt", 10), AUTO_PROFILE(LegacyCRT, "legacy_crt", 16)
#undef AUTO_PROFILE
#undef AUTO_CASE
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
