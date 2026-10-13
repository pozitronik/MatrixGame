// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixRobot.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
using tests::battle::Session;
using tests::battle::Profile;
CMatrixRobotAI *robot(int side, int ordinal = 0) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && object->GetSide() == side && ordinal-- == 0) return object->AsRobot();
    throw std::runtime_error("Missing fixture robot");
}
struct Trace {
    std::ofstream file;
    std::vector<std::string> states;
    Trace(Session &session, Profile p, const char *name, int run) {
        const auto directory = std::filesystem::current_path() / "scenario-traces";
        std::filesystem::create_directories(directory);
        const auto path = directory / (std::string(name) + "-" +
            (p.mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" +
            std::to_string(p.step_ms) + "-" + std::to_string(run) + ".jsonl");
        file.open(path, std::ios::trunc);
        MG_CHECK(file.good());
        std::string header = session.metadata();
        const std::string base_fixture = "unarmed-chassis-obstacle-v1";
        const auto at = header.find(base_fixture);
        MG_CHECK(at != std::string::npos);
        header.replace(at, base_fixture.size(), "synthetic-armed-boxes-v1");
        header.pop_back();
        header += ",\"scenario\":\"" + std::string(name) + "\"}";
        file << header << '\n';
        file.flush();
        MG_CHECK(file.good());
        std::cerr << "Combat profile: " << header << "\nTrace: " << path.string() << std::endl;
    }
    void record(Session &session, CMatrixRobotAI *target, CMatrixRobotAI *shooter = nullptr,
                CMatrixCannon *cannon = nullptr) {
        const auto draws = random::draw_count();
        std::ostringstream out;
        out << "{\"type\":\"snapshot\",\"state\":" << session.snapshot()
            << ",\"combat\":{\"health_bits\":" << std::bit_cast<uint32_t>(target->GetHitPoint());
        if (shooter) out << ",\"heat\":" << shooter->GetWeapon(0).m_Heat;
        if (cannon) out << ",\"cannon_target_is_patient\":" <<
            (cannon->m_TargetCore && cannon->m_TargetCore->m_Object == target ? "true" : "false")
            << ",\"yaw_bits\":" << std::bit_cast<uint32_t>(cannon->m_Angle)
            << ",\"pitch_bits\":" << std::bit_cast<uint32_t>(cannon->m_AngleX);
        out << "}}";
        states.push_back(out.str());
        file << states.back() << '\n';
        file.flush();
        MG_CHECK(file.good() && draws == random::draw_count());
    }
    template<class Action>
    void command(Session &session, const char *name, Action action) {
        const auto draws = random::draw_count();
        const bool accepted = action();
        const auto resulting = session.robot(1);
        std::ostringstream out;
        out << "{\"type\":\"command\",\"frame\":" << session.frame() << ",\"name\":\"" << name
            << "\",\"rng_draws_before\":" << draws << ",\"rng_draws_after\":" << random::draw_count()
            << ",\"accepted\":" << (accepted ? "true" : "false")
            << ",\"resulting_order\":" << resulting.order << ",\"resulting_target\":" << resulting.target
            << ",\"resulting_destination\":[" << resulting.destination_x << ',' << resulting.destination_y << "]}";
        states.push_back(out.str());
        file << states.back() << '\n';
        file.flush();
        MG_CHECK(file.good() && accepted);
    }
    void complete(Session &session) {
        file << "{\"type\":\"complete\",\"frame\":" << session.frame()
             << ",\"time_ms\":" << session.time() << "}\n";
        file.flush();
        MG_CHECK(file.good());
    }
};
void compare(const std::vector<std::string> &expected, const std::vector<std::string> &actual) {
    MG_CHECK(expected.size() == actual.size());
    for (size_t i = 0; i < expected.size(); ++i)
        if (expected[i] != actual[i]) throw std::runtime_error("Combat divergence at boundary " + std::to_string(i) +
            "\nExpected: " + expected[i] + "\nActual: " + actual[i]);
}
void check_health(CMatrixRobotAI *target, int internal) {
    const float expected = float(internal) / 10.0f;
    MG_CHECK(std::bit_cast<uint32_t>(target->GetHitPoint()) == std::bit_cast<uint32_t>(expected));
}
void configure_weapon(EWeapon kind, int heat_index) {
    g_Config.m_WeaponRadius[Weap2Index(kind)] = 600;
    g_Config.m_WeaponCooldown[Weap2Index(kind)] = 100;
    g_Config.m_RobotDamages[Weap2Index(kind)] = {2, 0, 10};
    g_Config.m_Overheat[heat_index] = 10;
    g_Config.m_Overheat[heat_index + 2] = 10;
    g_Config.m_Overheat[heat_index + 1] = 100;
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
}
std::vector<std::string> robot_run(Profile profile, bool plasma, int run) {
    tests::BattleCombatAssets assets;
    Session session(profile);
    assets.install();
    configure_weapon(plasma ? WEAPON_PLASMA : WEAPON_LASER, plasma ? WEAPON_PLASMA_HEAT : WEAPON_LASER_HEAT);
    auto *shooter = robot(PLAYER_SIDE);
    auto *target = robot(2);
    tests::arm_battle_robot(*shooter, plasma ? RUK_WEAPON_PLASMA : RUK_WEAPON_LASER);
    target->InitMaxHitpoint(50);
    if (plasma) target->SetMaxSpeed(0);
    Trace trace(session, profile, plasma ? "plasma-flight" : "laser-attack", run);
    trace.command(session, "context-target(3)", [&] { return session.target_command(3); });
    float health = target->GetHitPoint();
    std::vector<int> impacts;
    for (int i = 0; i < (plasma ? 1600 : 1000) / profile.step_ms; ++i) {
        trace.record(session, target, shooter);
        session.advance();
        MG_CHECK(shooter->GetWeapon(0).m_Heat == 0 || shooter->GetWeapon(0).m_Heat == 10);
        if (target->GetHitPoint() != health) {
            health = target->GetHitPoint();
            impacts.push_back(session.time());
            check_health(target, 50 - int(impacts.size()) * 2);
        }
    }
    MG_CHECK(health < 5 && health > 1);
    trace.command(session, "remove-shooter(1)", [&] { return session.remove(1); });
    const float at_removal = target->GetHitPoint();
    for (int i = 0; i < 2000 / profile.step_ms; ++i) {
        trace.record(session, target);
        session.advance();
        if (target->GetHitPoint() != health) {
            health = target->GetHitPoint();
            impacts.push_back(session.time());
            check_health(target, 50 - int(impacts.size()) * 2);
        }
    }
    const std::vector<int> expected = plasma ?
        std::vector<int>{1120, 1220, 1310, 1410, 1510, 1600, 1690, 1790, 1890, 1980, 2070, 2160, 2260, 2350, 2450} :
        profile.step_ms == 10 ? std::vector<int>{200, 300, 400, 500, 600, 700, 800, 900, 1000} :
        std::vector<int>{224, 320, 432, 528, 624, 720, 832, 928};
    MG_CHECK(impacts == expected);
    if (plasma) MG_CHECK(target->GetHitPoint() < at_removal);
    else MG_CHECK(target->GetHitPoint() == at_removal);
    trace.record(session, target);
    session.check_running();
    trace.complete(session);
    return trace.states;
}
void robot_firing() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) {
            const Profile p{mode, 82, step, 1000, false, true};
            const auto a = robot_run(p, false, 0);
            compare(a, robot_run(p, false, 1));
        }
}
void plasma_flight() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const Profile p{mode, 82, 10, 1000, false, true};
        const auto a = robot_run(p, true, 0);
        compare(a, robot_run(p, true, 1));
    }
}
std::vector<std::string> cannon_run(random::Mode mode, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 3000, false, false};
    Session session(profile);
    assets.install();
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.m_Hitpoint = 1000;
    props.weapon = WEAPON_CANNON2;
    props.seek_radius = 300;
    props.max_da = 0.1f;
    props.max_top_angle = 1.5f;
    props.max_bottom_angle = -1.5f;
    props.m_Strength = 10;
    g_Config.m_CannonsLogic = true;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = 300;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_CANNON2)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_CANNON2)] = {2, 0, 10};
    auto *target = robot(PLAYER_SIDE);
    auto *next = robot(PLAYER_SIDE, 1);
    target->InitMaxHitpoint(50);
    next->InitMaxHitpoint(50);
    auto *cannon = tests::create_battle_cannon(2, 80, 100);
    Trace trace(session, profile, "cannon-retarget", run);
    std::vector<int> impacts;
    float health = target->GetHitPoint();
    for (int i = 0; i < 100; ++i) {
        trace.record(session, target, nullptr, cannon);
        session.advance();
        if (target->GetHitPoint() != health) {
            health = target->GetHitPoint();
            impacts.push_back(session.time());
            check_health(target, 50 - int(impacts.size()) * 2);
        }
    }
    MG_CHECK(cannon->m_TargetCore && cannon->m_TargetCore->m_Object == target);
    MG_CHECK((impacts == std::vector<int>{220, 320, 420, 520, 620, 720, 820, 920}));
    trace.command(session, "remove-target(1)", [&] { return session.remove(1); });
    impacts.clear();
    health = next->GetHitPoint();
    for (int i = 0; i < 100; ++i) {
        trace.record(session, next, nullptr, cannon);
        session.advance();
        if (next->GetHitPoint() != health) {
            health = next->GetHitPoint();
            impacts.push_back(session.time());
            check_health(next, 50 - int(impacts.size()) * 2);
        }
    }
    MG_CHECK((impacts == std::vector<int>{1120, 1220, 1320, 1420, 1520, 1620, 1720, 1820, 1920}));
    MG_CHECK(cannon->m_TargetCore && cannon->m_TargetCore->m_Object == next);
    MG_CHECK(next->GetHitPoint() < 5);
    trace.record(session, next, nullptr, cannon);
    session.check_running();
    trace.complete(session);
    return trace.states;
}
void cannon_firing() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto a = cannon_run(mode, 0);
        compare(a, cannon_run(mode, 1));
    }
}
void axis_aligned_trace() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode, 82, 10, 1000, false, false});
        auto *target = robot(PLAYER_SIDE);
        const D3DXVECTOR3 end = target->GetGeoCenter();
        const DWORD mask = TRACE_OBJECTSPHERE | TRACE_ROBOT;
        // Current ScanLandscapeGroup initializes its upper interval to zero and
        // only expands it for nonzero X travel. Preserve this observed asymmetry.
        MG_CHECK(g_MatrixMap->Trace(nullptr, D3DXVECTOR3(end.x, 100, 50), end, mask, nullptr) == TRACE_STOP_NONE);
        MG_CHECK(g_MatrixMap->Trace(nullptr, D3DXVECTOR3(80, 100, 50), end, mask, nullptr) == target);
    }
}
const tests::Case cases[] = {{"game.combat.cannon_firing", cannon_firing},
    {"game.combat.robot_firing", robot_firing}, {"game.combat.plasma_flight", plasma_flight},
    {"game.combat.axis_aligned_trace", axis_aligned_trace}};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Combat scenario leaked tracked allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
