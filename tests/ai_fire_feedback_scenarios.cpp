// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Logic/MatrixEnvironment.h"
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
enum class Engagement { Ineffective, Obstructed };

bool ally_on_ray(CMatrixRobotAI &observer, CMatrixRobotAI &ally, CMatrixRobotAI &target) {
    const auto from = observer.GetGeoCenter();
    auto aim = target.GetGeoCenter();
    auto center = ally.GetGeoCenter();
    aim.z += 5;
    center.z += 5;
    auto direction = aim - from;
    const float length = D3DXVec3Length(&direction);
    MG_CHECK(length > 0);
    direction *= 1 / length;
    float intersection = 0;
    return IsIntersectSphere(center, 25, from, direction, intersection) && intersection >= 0 && intersection < length;
}

struct Result {
    std::vector<std::string> states;
    int first_bad = 0;
    int first_recovery_hit = 0;
    int first_relocation = 0;
    float final_health = 0;
};

Result journey(random::Mode mode, int step, Engagement engagement, bool intervention, int replay) {
    const bool obstructed = engagement == Engagement::Obstructed;
    BattleCombatAssets assets;
    Profile profile{mode, 84, step, 3000, false};
    profile.layout = NavigationLayout::Open;
    Session session(profile);
    assets.install();
    auto &map = *g_MatrixMap;
    auto &side = *map.GetPlayerSide();
    std::array<CMatrixRobotAI *, 3> robots{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot())
            for (unsigned id = 1; id <= robots.size(); ++id)
                if (session.matches(id, object->AsRobot())) robots[id - 1] = object->AsRobot();
    for (auto *robot : robots) MG_CHECK(robot);
    MG_CHECK(session.remove(4));
    auto *observer = robots[0], *ally = robots[1], *target = robots[2];
    const auto position = [&](CMatrixRobotAI &robot, float x, float y) {
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
    position(*ally, 220, 40);
    position(*target, obstructed ? 400 : 240, obstructed ? 280 : 400);
    target->SetMaxSpeed(obstructed ? .1f : intervention ? .35f : 0);
    target->m_GroupSpeed = target->GetMaxSpeed();
    target->InitMaxHitpoint(1000);
    const CPoint places[]{{8, 8}, {56, 8}, {20, 2}, {56, 56}};
    for (int i = 0; i < 4; ++i) {
        auto &place = map.m_RN.m_Place[i];
        place.m_Pos = places[i];
        place.m_NearCnt = 0;
        for (int j = 0; j < 4; ++j)
            if (i != j) place.m_Near[place.m_NearCnt++] = j;
        MG_CHECK(map.CanMoveFromTo(RUK_CHASSIS_WHEEL - 1, 4, 8, 8, places[i].x, places[i].y, nullptr));
    }
    map.m_RN.InitPL(64, 64);
    const EWeapon weapon = obstructed ? WEAPON_LASER : WEAPON_PLASMA;
    g_Config.m_WeaponRadius[Weap2Index(weapon)] = 600;
    g_Config.m_WeaponCooldown[Weap2Index(weapon)] = 100;
    g_Config.m_RobotDamages[Weap2Index(weapon)] = {2, 0, 0};
    g_Config.m_Overheat[obstructed ? WEAPON_LASER_HEAT : WEAPON_PLASMA_HEAT] = 10;
    g_Config.m_Overheat[obstructed ? WEAPON_LASER_COOL : WEAPON_PLASMA_COOL] = 10;
    g_Config.m_Overheat[obstructed ? WCP_LASER : WCP_PLASMA] = 100;
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = .04f;
    arm_battle_robot(*observer, obstructed ? RUK_WEAPON_LASER : RUK_WEAPON_PLASMA);
    g_MatrixData->BlockGet(L"Chars")->BlockGet(L"ChassisSounds")->BlockGet(L"2")->ParSetAdd(L"Patrol", L"Sound.Synthetic");

    const auto name = std::string(obstructed ? "fire-obstructed" : "fire-ineffective") +
        (intervention ? "-moving" : "-control");
    const auto directory = std::filesystem::current_path() / "scenario-traces";
    std::filesystem::create_directories(directory);
    const auto path = directory / (name + (mode == random::Mode::ParkMiller ? "-pm-" : "-crt-") +
        std::to_string(step) + "-" + std::to_string(replay) + ".jsonl");
    std::ofstream file(path, std::ios::trunc);
    MG_CHECK(file.good());
    auto header = session.metadata("moving-target-fire-feedback-v1");
    header.pop_back();
    header += ",\"scenario\":\"" + name + "\",\"target_speed_bits\":" +
        std::to_string(std::bit_cast<uint32_t>(target->GetMaxSpeed())) + "}";
    file << header << '\n';
    file.flush();
    std::cerr << "Fire feedback profile: " << header << "\nTrace: " << path.string() << std::endl;

    Result result;
    const auto id_of = [&](CMatrixMapStatic *object) {
        if (!object) return 0;
        for (unsigned id = 1; id <= robots.size(); ++id)
            if (object == robots[id - 1]) return int(id);
        return -1;
    };
    const auto record = [&] {
        const auto draws = random::draw_count();
        std::ostringstream out;
        out << "{\"type\":\"snapshot\",\"state\":" << session.snapshot() << ",\"feedback\":[";
        for (unsigned i = 0; i < robots.size(); ++i) {
            auto &robot = *robots[i];
            auto &env = *robot.GetEnv();
            CPoint move(-1, -1);
            const bool moving = robot.GetMoveToCoords(move);
            if (i) out << ',';
            out << "{\"id\":" << i + 1 << ",\"place\":" << env.m_Place
                << ",\"place_not_found\":" << env.m_PlaceNotFound << ",\"bad_places\":[";
            MG_CHECK(env.m_BadPlaceCnt >= 0 && env.m_BadPlaceCnt <= 8);
            for (int j = 0; j < env.m_BadPlaceCnt; ++j) {
                if (j) out << ',';
                out << env.m_BadPlace[j];
            }
            out << "],\"enemy_count\":" << env.GetEnemyCnt() << ",\"target\":" << id_of(env.m_Target)
                << ",\"attack_target\":" << id_of(env.m_TargetAttack) << ",\"target_change\":" << env.m_TargetChange
                << ",\"last_fire\":" << env.m_LastFire << ",\"last_hit\":" << env.m_LastHitTarget
                << ",\"health_bits\":" << std::bit_cast<uint32_t>(robot.GetHitPoint())
                << ",\"moving\":" << (moving ? "true" : "false")
                << ",\"move\":[" << move.x << ',' << move.y << "]} ";
        }
        out << "],\"war\":" << (observer->GetGroupLogic() >= 0 &&
            side.m_PlayerGroup[observer->GetGroupLogic()].IsWar() ? "true" : "false")
            << ",\"ally_on_ray\":" << (ally_on_ray(*observer, *ally, *target) ? "true" : "false") << '}';
        MG_CHECK(random::draw_count() == draws);
        result.states.push_back(out.str());
        file << result.states.back() << '\n';
        file.flush();
        MG_CHECK(file.good());
    };
    const auto move_ally = [&](float x, float y, const char *label) {
        const auto before = random::draw_count();
        side.CreateGroupFromCurrent(ally);
        side.Select(ROBOT, nullptr);
        session.ground_command(x, y);
        MG_CHECK(session.robot(2).order == mpo_MoveTo);
        MG_CHECK(session.robot(2).destination_x == int(x / 10) - 2);
        MG_CHECK(session.robot(2).destination_y == int(y / 10) - 2);
        side.CreateGroupFromCurrent(observer);
        side.Select(ROBOT, nullptr);
        file << "{\"type\":\"command\",\"time_ms\":" << session.time() << ",\"name\":\"" << label
            << "\",\"world\":[" << x << ',' << y << "],\"rng_draws_before\":" << before
            << ",\"rng_draws_after\":" << random::draw_count() << "}\n";
    };
    move_ally(220, 40, "hold-alternative-place");
    session.ground_command(100, 100);
    record();
    while (session.time() < 200) { session.advance(); record(); }
    MG_CHECK(observer->GetEnv()->m_Place == 0 && ally->GetEnv()->m_Place == 2);
    side.PGOrderPatrol(side.RobotToLogicGroup(observer), CPoint(48, 8));
    const auto &initial = side.m_PlayerGroup[observer->GetGroupLogic()];
    MG_CHECK(initial.Order() == mpo_Patrol);
    MG_CHECK(initial.m_From == CPoint(8, 8) && initial.m_To == CPoint(48, 8));
    file << "{\"type\":\"command\",\"time_ms\":" << session.time()
        << ",\"name\":\"patrol\",\"from\":[8,8],\"to\":[48,8]}\n";
    record();

    bool commanded = false, alternate_reached = false;
    int ineffective_frames = 0, blocked_frames = 0;
    float health_at_bad = 0, blocked_health = -1;
    const auto target_start = D3DXVECTOR2(target->m_PosX, target->m_PosY);
    while (session.time() < 25000) {
        auto &env = *observer->GetEnv();
        if (intervention && !commanded && (obstructed ? session.time() >= 1000 : result.first_bad != 0)) {
            if (obstructed) {
                MG_CHECK(env.m_TargetAttack == target && env.m_LastFire > 0 && env.m_LastHitTarget > 0);
                MG_CHECK(target->GetHitPoint() < 100);
            }
            move_ally(obstructed ? 220 : 100, obstructed ? 140 : 500,
                obstructed ? "cross-firing-line" : "free-alternative-place");
            commanded = true;
            record();
        }
        const int previous_place = env.m_Place;
        const int previous_bad = env.m_BadPlaceCnt;
        const bool was_in_place = observer->PLIsInPlace();
        const bool blocked_ray = ally_on_ray(*observer, *ally, *target);
        session.advance();
        MG_CHECK(session.robot(1).alive && session.robot(2).alive && session.robot(3).alive);
        for (const auto *robot : robots) {
            MG_CHECK(std::isfinite(robot->m_PosX) && std::isfinite(robot->m_PosY));
            MG_CHECK(robot->m_PosX >= 0 && robot->m_PosX < 640 && robot->m_PosY >= 0 && robot->m_PosY < 640);
        }
        MG_CHECK(side.m_PlayerGroup[observer->GetGroupLogic()].Order() == mpo_Patrol);
        if (session.time() >= 3500) MG_CHECK(side.m_PlayerGroup[observer->GetGroupLogic()].IsWar());
        if (session.time() >= 3500 && !result.first_bad && env.m_BadPlaceCnt == 0) {
            MG_CHECK(env.m_TargetAttack == target && env.GetEnemyCnt() == 1);
            MG_CHECK(observer->PLIsInPlace() && env.m_Place == 0);
            if (session.time() - env.m_LastFire <= 120 && env.m_LastHitTarget == 0) {
                ++ineffective_frames;
                MG_CHECK(target->GetHitPoint() == 100);
                MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE));
            }
            if (blocked_ray && session.time() - env.m_LastFire > 120) {
                ++blocked_frames;
                if (blocked_health < 0) blocked_health = target->GetHitPoint();
                MG_CHECK(target->GetHitPoint() == blocked_health);
            }
        }
        if (env.m_BadPlaceCnt > previous_bad && !result.first_bad) {
            result.first_bad = session.time();
            health_at_bad = target->GetHitPoint();
            MG_CHECK(std::hypot(target->m_PosX - target_start.x, target->m_PosY - target_start.y) > 50);
            MG_CHECK(previous_place == 0 && was_in_place);
            MG_CHECK(env.m_BadPlaceCnt == 1 && env.m_BadPlace[0] == 0 && env.m_Place == -1);
            MG_CHECK(env.m_TargetAttack == target && env.GetEnemyCnt() == 1);
            MG_CHECK(session.time() - env.m_TargetChange > 4000);
            if (obstructed && intervention) {
                MG_CHECK(blocked_ray && blocked_frames * step >= 4000);
                MG_CHECK(session.time() - env.m_LastFire > 6000);
                MG_CHECK(session.time() - env.m_LastFire <= 6500);
                MG_CHECK(env.m_LastHitTarget > 0 && health_at_bad < 100 && health_at_bad == blocked_health);
            }
            else {
                MG_CHECK(session.time() - env.m_LastFire <= 120);
                MG_CHECK(session.time() - env.m_LastHitTarget > 4000);
                if (!obstructed) {
                    MG_CHECK(intervention && ineffective_frames * step >= 3000);
                    MG_CHECK(env.m_LastHitTarget == 0 && health_at_bad == 100);
                }
            }
        }
        if (result.first_bad && intervention && session.time() > result.first_bad) {
            CPoint move;
            if (!result.first_relocation && observer->GetMoveToCoords(move)) {
                result.first_relocation = session.time();
                MG_CHECK(env.m_Place == 2 && move == places[2]);
            }
            if (env.m_Place == 2 && observer->PLIsInPlace()) {
                MG_CHECK(std::abs(observer->m_PosX - 220) < .01f && std::abs(observer->m_PosY - 40) < .01f);
                alternate_reached = true;
            }
            if (!result.first_recovery_hit && env.m_LastHitTarget > result.first_bad && target->GetHitPoint() < health_at_bad) {
                MG_CHECK(result.first_relocation != 0);
                MG_CHECK(std::hypot(observer->m_PosX - 100, observer->m_PosY - 100) > .1f);
                if (obstructed) MG_CHECK(!ally_on_ray(*observer, *ally, *target));
                result.first_recovery_hit = session.time();
            }
        }
        if (!intervention && session.time() <= 12000) MG_CHECK(env.m_BadPlaceCnt == 0);
        record();
    }
    result.final_health = target->GetHitPoint();
    if (intervention) {
        MG_CHECK(commanded && result.first_bad > 6000 && result.first_bad < 10000);
        MG_CHECK(result.first_relocation > result.first_bad && alternate_reached);
        MG_CHECK(result.first_recovery_hit > result.first_relocation && result.first_recovery_hit < 20000);
        MG_CHECK(result.final_health > 0 && result.final_health < health_at_bad);
    }
    else if (!obstructed) {
        MG_CHECK(result.first_bad == 0 && observer->GetEnv()->m_Place == 0);
        MG_CHECK(observer->GetEnv()->m_LastHitTarget > 24000 && result.final_health < 60);
        MG_CHECK(target->m_PosX == target_start.x && target->m_PosY == target_start.y);
    }
    if (obstructed || intervention)
        MG_CHECK(std::hypot(target->m_PosX - target_start.x, target->m_PosY - target_start.y) > 50);
    session.check_running();
    std::cout << "Feedback milestones: scenario=" << name << " first_bad=" << result.first_bad
        << " relocation=" << result.first_relocation << " recovery_hit=" << result.first_recovery_hit
        << " final_health=" << result.final_health << '\n';
    return result;
}

template<random::Mode Mode, int Step, Engagement EngagementType> void run() {
    const auto main = journey(Mode, Step, EngagementType, true, 0);
    MG_CHECK(main.states == journey(Mode, Step, EngagementType, true, 1).states);
    const auto control = journey(Mode, Step, EngagementType, false, 0);
    MG_CHECK(control.states == journey(Mode, Step, EngagementType, false, 1).states);
    if constexpr (EngagementType == Engagement::Obstructed) {
        // Initial setup and every boundary before the ordinary ally command agree exactly.
        const std::size_t prefix = (1000 + Step - 1) / Step + 2;
        MG_CHECK(main.states.size() > prefix && control.states.size() > prefix);
        MG_CHECK(std::equal(main.states.begin(), main.states.begin() + prefix, control.states.begin()));
        MG_CHECK(control.first_bad == 0 || control.first_bad > main.first_bad + 4000);
    }
}

template<random::Mode Mode, int Step, Engagement EngagementType> void control() {
    const auto first = journey(Mode, Step, EngagementType, false, 2);
    MG_CHECK(first.states == journey(Mode, Step, EngagementType, false, 3).states);
}

constexpr Case cases[]{
#define FIRE_CASE(mode, label, step, kind, name) \
    {"game.ai.fire_feedback." name "_" label "_" #step, run<random::Mode::mode, step, Engagement::kind>}
#define FIRE_PROFILE(mode, label, step) \
    FIRE_CASE(mode, label, step, Ineffective, "ineffective"), FIRE_CASE(mode, label, step, Obstructed, "obstructed")
    FIRE_PROFILE(ParkMiller, "park_miller", 10), FIRE_PROFILE(ParkMiller, "park_miller", 16),
    FIRE_PROFILE(LegacyCRT, "legacy_crt", 10), FIRE_PROFILE(LegacyCRT, "legacy_crt", 16),
#define FIRE_CONTROL(mode, label, step, kind, name) \
    {"game.ai.fire_feedback." name "_control_" label "_" #step, control<random::Mode::mode, step, Engagement::kind>}
#define CONTROL_PROFILE(mode, label, step) \
    FIRE_CONTROL(mode, label, step, Ineffective, "ineffective"), FIRE_CONTROL(mode, label, step, Obstructed, "obstructed")
    CONTROL_PROFILE(ParkMiller, "park_miller", 10), CONTROL_PROFILE(ParkMiller, "park_miller", 16),
    CONTROL_PROFILE(LegacyCRT, "legacy_crt", 10), CONTROL_PROFILE(LegacyCRT, "legacy_crt", 16)
#undef CONTROL_PROFILE
#undef FIRE_CONTROL
#undef FIRE_PROFILE
#undef FIRE_CASE
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
