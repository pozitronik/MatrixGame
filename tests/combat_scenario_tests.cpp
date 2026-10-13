// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixRobot.hpp"
#include <bit>
#include <cmath>
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
                CMatrixCannon *cannon = nullptr, SObjectCore *retired_owner = nullptr) {
        const auto draws = random::draw_count();
        std::ostringstream out;
        out << "{\"type\":\"snapshot\",\"state\":" << session.snapshot()
            << ",\"combat\":{\"health_bits\":" << std::bit_cast<uint32_t>(target->GetHitPoint());
        if (shooter) out << ",\"heat\":" << shooter->GetWeapon(0).m_Heat;
        if (cannon) out << ",\"cannon_target_is_patient\":" <<
            (cannon->m_TargetCore && cannon->m_TargetCore->m_Object == target ? "true" : "false")
            << ",\"yaw_bits\":" << std::bit_cast<uint32_t>(cannon->m_Angle)
            << ",\"pitch_bits\":" << std::bit_cast<uint32_t>(cannon->m_AngleX);
        if (retired_owner) out << ",\"owner_present\":" << (retired_owner->m_Object ? "true" : "false");
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
struct Family {
    const char *name;
    EWeapon effect;
    ERobotUnitKind unit;
    int heat;
    std::vector<std::pair<int, int>> impacts;
    uint64_t pm_draws, crt_draws;
};
const Family volcano{"volcano", WEAPON_VOLCANO, RUK_WEAPON_MACHINEGUN, WEAPON_VOLCANO_HEAT,
    {{200,48},{700,46},{1200,44},{1700,42},{2200,40},{2700,38}}, 263, 263};
const Family electric{"electric", WEAPON_LIGHTENING, RUK_WEAPON_ELECTRIC, WEAPON_LIGHTENING_HEAT,
    {{200,48},{700,46},{1200,44},{1700,42},{2200,40},{2700,38}}, 43841, 42476};
const Family flame{"flame", WEAPON_FLAMETHROWER, RUK_WEAPON_FLAMETHROWER, WEAPON_FLAMETHROWER_HEAT,
    {{1820,48},{1850,46},{1880,44},{1910,42},{1940,40},{1970,38},{2000,36}}, 18770, 18758};
const Family gun{"gun", WEAPON_GUN, RUK_WEAPON_CANNON, WEAPON_GUN_HEAT,
    {{410,46},{900,44},{1390,42},{1890,38},{2380,36},{2880,32}}, 90177, 91297};
const Family missile{"missile", WEAPON_HOMING_MISSILE, RUK_WEAPON_MISSILE, WEAPON_HOMING_MISSILE_HEAT,
    {{980,48},{1470,46},{1950,44},{2430,42},{2920,40},{3410,38}}, 76405, 76405};
const Family mortar{"mortar", WEAPON_BOMB, RUK_WEAPON_MORTAR, WEAPON_BOMB_HEAT,
    {{1590,48},{2080,46},{2580,44},{3080,42},{3580,40}}, 41251, 41203};

std::vector<std::string> family_run(const Family &family, random::Mode mode, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    configure_weapon(family.effect, family.heat);
    g_Config.m_WeaponCooldown[Weap2Index(family.effect)] = 500;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_ABLAZE)] = {0, 0, 10};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_SHORTED)] = {0, 0, 10};
    auto *shooter = robot(PLAYER_SIDE);
    auto *target = robot(2);
    tests::arm_battle_robot(*shooter, family.unit);
    target->InitMaxHitpoint(50);
    target->SetMaxSpeed(0);
    Trace trace(session, profile, family.name, run);
    trace.command(session, "context-target(3)", [&] { return session.target_command(3); });
    float health = target->GetHitPoint();
    size_t impact = 0;
    for (int i = 0; i < 400; ++i) {
        trace.record(session, target, shooter);
        session.advance();
        if (target->GetHitPoint() != health) {
            health = target->GetHitPoint();
            MG_CHECK(impact < family.impacts.size());
            MG_CHECK(session.time() == family.impacts[impact].first);
            check_health(target, family.impacts[impact].second);
            ++impact;
        }
    }
    MG_CHECK(impact == family.impacts.size());
    MG_CHECK(random::draw_count() == (mode == random::Mode::ParkMiller ? family.pm_draws : family.crt_draws));
    trace.record(session, target, shooter);
    trace.complete(session);
    return trace.states;
}
void family_replay(const Family &family) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto reference = family_run(family, mode, 0);
        compare(reference, family_run(family, mode, 1));
    }
}
void volcano_firing() { family_replay(volcano); }
void electric_firing() { family_replay(electric); }
void flame_firing() { family_replay(flame); }
void gun_firing() { family_replay(gun); }
void missile_firing() { family_replay(missile); }
void mortar_firing() { family_replay(mortar); }

std::vector<std::string> repair_run(random::Mode mode, bool retire, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_REPAIR)] = 600;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_REPAIR)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {20, 20, 0};
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    auto *shooter = robot(PLAYER_SIDE);
    auto *target = robot(PLAYER_SIDE, 1);
    tests::arm_battle_robot(*shooter, RUK_WEAPON_REPAIR);
    target->InitMaxHitpoint(50);
    target->SetMaxSpeed(0);
    MG_CHECK(!target->Damage(WEAPON_GUN, target->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
    check_health(target, 30);
    auto *side = g_MatrixMap->GetPlayerSide();
    Trace trace(session, profile, retire ? "repair-retire" : "repair-full", run);
    trace.command(session, "order-repair(2)", [&] {
        side->PGOrderRepair(side->SelGroupToLogicGroup(), target);
        return session.robot(1).order == mpo_Repair;
    });
    float health = target->GetHitPoint();
    int repairs = 0;
    for (int i = 0; i < 200; ++i) {
        if (retire && i == 30) {
            trace.command(session, "remove-patient(2)", [&] { return session.remove(2); });
            target = nullptr;
        }
        trace.record(session, target ? target : shooter, shooter);
        session.advance();
        if (target && health != target->GetHitPoint()) {
            health = target->GetHitPoint();
            ++repairs;
            MG_CHECK(session.time() == 10 + repairs * 100);
            check_health(target, 30 + repairs * 4);
        }
        if (!target && session.time() >= 400) {
            MG_CHECK(session.robot(1).target_is_null);
            MG_CHECK(!shooter->FindOrderLikeThat(ROT_FIRE));
        }
    }
    MG_CHECK(repairs == (retire ? 2 : 5));
    if (target) {
        MG_CHECK(!target->NeedRepair());
        check_health(target, 50);
    }
    trace.record(session, target ? target : shooter, shooter);
    trace.complete(session);
    return trace.states;
}
void repair_firing() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool retire : {false, true}) {
        const auto reference = repair_run(mode, retire, 0);
        compare(reference, repair_run(mode, retire, 1));
    }
}
void heat_boundary() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int heat : {500, 501}) {
        tests::BattleCombatAssets assets;
        const Profile profile{mode, 82, 10, 1000, false, true};
        Session session(profile);
        assets.install();
        configure_weapon(WEAPON_LASER, WEAPON_LASER_HEAT);
        g_Config.m_Overheat[WEAPON_LASER_HEAT] = heat;
        g_Config.m_Overheat[WEAPON_LASER_COOL] = 100;
        auto *shooter = robot(PLAYER_SIDE);
        auto *target = robot(2);
        tests::arm_battle_robot(*shooter, RUK_WEAPON_LASER);
        target->InitMaxHitpoint(50);
        target->SetMaxSpeed(0);
        Trace trace(session, profile, heat == 500 ? "heat-500" : "heat-501", 0);
        trace.command(session, "context-target(3)", [&] { return session.target_command(3); });
        float health = target->GetHitPoint();
        int max_heat = 0;
        std::vector<int> impacts;
        for (int i = 0; i < 200; ++i) {
            trace.record(session, target, shooter);
            session.advance();
            max_heat = std::max(max_heat, shooter->GetWeapon(0).m_Heat);
            if (health != target->GetHitPoint()) {
                health = target->GetHitPoint();
                impacts.push_back(session.time());
                check_health(target, 50 - int(impacts.size()) * 2);
            }
        }
        MG_CHECK(impacts == (heat == 500 ? std::vector<int>{200, 790, 1380, 1970} : std::vector<int>{}));
        check_health(target, heat == 500 ? 42 : 50);
        MG_CHECK(max_heat == (heat == 500 ? 500 : 0));
        trace.record(session, target, shooter);
        trace.complete(session);
    }
}

struct CoreRelease { void operator()(SObjectCore *core) const { core->Release(); } };
using CoreLease = std::unique_ptr<SObjectCore, CoreRelease>;
std::vector<std::string> bomb_run(random::Mode mode, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, false};
    Session session(profile);
    assets.install();
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 150;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
    auto *bomber = robot(PLAYER_SIDE);
    auto *neighbor = robot(PLAYER_SIDE, 1);
    auto *enemy = robot(2);
    neighbor->InitMaxHitpoint(50);
    neighbor->SetMaxSpeed(0);
    enemy->InitMaxHitpoint(50);
    enemy->SetMaxSpeed(0);
    tests::arm_battle_robot(*bomber, RUK_WEAPON_BOMB);
    CoreLease core(bomber->GetCore(DEBUG_CALL_INFO));
    Trace trace(session, profile, "bomb-manual", run);
    trace.command(session, "detonate(1)", [&] { bomber->BigBoom(); return bomber->IsMustDie(); });
    session.advance();
    MG_CHECK(bomber->m_CurrState == ROBOT_DIP);
    int removed_at = 0;
    float health = neighbor->GetHitPoint();
    for (int i = 0; i < 600; ++i) {
        trace.record(session, neighbor, nullptr, nullptr, core.get());
        session.advance();
        if (health != neighbor->GetHitPoint()) {
            health = neighbor->GetHitPoint();
            MG_CHECK(session.time() == 310);
            check_health(neighbor, 46);
        }
        if (!core->m_Object && !removed_at) removed_at = session.time();
    }
    MG_CHECK(removed_at == (mode == random::Mode::ParkMiller ? 4080 : 4140));
    check_health(neighbor, 46);
    MG_CHECK(g_MatrixMap->GetSideById(PLAYER_SIDE)->GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    check_health(enemy, 50);
    trace.record(session, neighbor, nullptr, nullptr, core.get());
    trace.complete(session);
    return trace.states;
}
void bomb_detonation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto reference = bomb_run(mode, 0);
        compare(reference, bomb_run(mode, 1));
    }
}

std::vector<std::string> cannon_projectile_run(random::Mode mode, EWeapon weapon, bool destroy, int run) {
            tests::BattleCombatAssets assets;
            const Profile profile{mode, 82, 10, 1000, false, false};
            Session session(profile);
            assets.install();
            auto &props = g_Config.m_CannonsProps[0];
            props = {};
            props.m_Hitpoint = 1000;
            props.weapon = weapon;
            props.seek_radius = 300;
            props.max_da = 0.1f;
            props.max_top_angle = 1.5f;
            props.max_bottom_angle = -1.5f;
            props.m_Strength = 10;
            g_Config.m_CannonsLogic = true;
            g_Config.m_WeaponRadius[Weap2Index(weapon)] = 300;
            g_Config.m_WeaponCooldown[Weap2Index(weapon)] = 250;
            g_Config.m_RobotDamages[Weap2Index(weapon)] = {2, 0, 10};
            g_Config.m_CannonDamages[Weap2Index(WEAPON_GUN)] = {1000, 1000, 0};
            auto *target = robot(PLAYER_SIDE);
            target->InitMaxHitpoint(50);
            target->SetMaxSpeed(0);
            auto *cannon = tests::create_battle_cannon(2, 80, 100);
            CoreLease core(cannon->GetCore(DEBUG_CALL_INFO));
            const std::string name = "cannon-projectile-" + std::to_string(int(weapon)) + (destroy ? "-destroy" : "-fire");
            Trace trace(session, profile, name.c_str(), run);
            std::vector<std::pair<int, int>> impacts;
            float health = target->GetHitPoint();
            int removed = 0;
            for (int i = 0; i < (destroy ? 650 : 100); ++i) {
                if (destroy && i == 22) {
                    MG_CHECK(cannon->m_Weapons[0]->GetFireCount() == 1);
                    check_health(target, 50);
                    trace.command(session, "destroy-cannon", [&] {
                        return cannon->Damage(WEAPON_GUN, cannon->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), PLAYER_SIDE, nullptr);
                    });
                    MG_CHECK(cannon->m_CurrState == CANNON_DIP);
                }
                trace.record(session, target, nullptr, core->m_Object ? cannon : nullptr, core.get());
                session.advance();
                if (!core->m_Object && !removed) removed = session.time();
                if (target->GetHitPoint() != health) {
                    health = target->GetHitPoint();
                    impacts.emplace_back(session.time(), int(std::lround(health * 10)));
                }
            }
            const std::vector<std::pair<int, int>> expected = weapon == WEAPON_CANNON0 ?
                std::vector<std::pair<int, int>>{{240, 48}, {490, 46}, {740, 44}, {990, 42}} :
                weapon == WEAPON_CANNON1 ? std::vector<std::pair<int, int>>{{250, 46}, {500, 42}, {750, 38}, {1000, 34}} :
                std::vector<std::pair<int, int>>{{520, 48}, {770, 46}};
            MG_CHECK(impacts == (destroy ? std::vector{expected.front()} : expected));
            check_health(target, destroy ? expected.front().second : expected.back().second);
            if (destroy) {
                MG_CHECK(removed == (weapon == WEAPON_CANNON3 ?
                    (mode == random::Mode::ParkMiller ? 4250 : 4010) :
                    (mode == random::Mode::ParkMiller ? 3730 : 4730)));
                MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_TURRET_KILL) == 1);
            }
            trace.record(session, target, nullptr, core->m_Object ? cannon : nullptr, core.get());
            trace.complete(session);
            return trace.states;
}
void cannon_projectiles() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (auto weapon : {WEAPON_CANNON0, WEAPON_CANNON1, WEAPON_CANNON3}) for (bool destroy : {false, true}) {
            const auto reference = cannon_projectile_run(mode, weapon, destroy, 0);
            compare(reference, cannon_projectile_run(mode, weapon, destroy, 1));
        }
}

std::vector<std::string> cannon_boundary_run(random::Mode mode, int radius, float pitch, int run) {
            tests::BattleCombatAssets assets;
            const Profile profile{mode, 82, 10, 1000, false, false};
            Session session(profile);
            assets.install();
            MG_CHECK(session.remove(2));
            auto &props = g_Config.m_CannonsProps[0];
            props = {};
            props.m_Hitpoint = 1000;
            props.weapon = WEAPON_CANNON2;
            props.seek_radius = 300;
            props.max_da = 0.1f;
            props.max_top_angle = 1.5f;
            props.max_bottom_angle = pitch;
            props.m_Strength = 10;
            g_Config.m_CannonsLogic = true;
            g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = float(radius);
            g_Config.m_WeaponCooldown[Weap2Index(WEAPON_CANNON2)] = 100;
            g_Config.m_RobotDamages[Weap2Index(WEAPON_CANNON2)] = {2, 0, 10};
            auto *target = robot(PLAYER_SIDE);
            target->InitMaxHitpoint(50);
            target->SetMaxSpeed(0);
            auto *cannon = tests::create_battle_cannon(2, 70, 200);
            Trace trace(session, profile, ("cannon-range-" + std::to_string(radius) + "-pitch-" + std::to_string(int(pitch * 10))).c_str(), run);
            std::vector<int> impacts;
            int shots = 0;
            float health = target->GetHitPoint();
            for (int i = 0; i < 100; ++i) {
                trace.record(session, target, nullptr, cannon);
                session.advance();
                shots += cannon->m_Weapons[0]->GetFireCount();
                MG_CHECK(cannon->m_AngleX >= pitch && cannon->m_AngleX <= props.max_top_angle);
                auto delta = target->GetGeoCenter() - cannon->GetGeoCenter();
                MG_CHECK(D3DXVec3LengthSq(&delta) == 2500);
                if (target->GetHitPoint() != health) {
                    health = target->GetHitPoint();
                    impacts.push_back(session.time());
                    check_health(target, 50 - int(impacts.size()) * 2);
                }
            }
            MG_CHECK(cannon->m_TargetCore && cannon->m_TargetCore->m_Object == target);
            MG_CHECK(impacts == (radius >= 50 && pitch < -0.5f ?
                std::vector<int>{330, 430, 530, 630, 730, 830, 930} : std::vector<int>{}));
            MG_CHECK(shots == (radius >= 50 && pitch < 0 ? 7 : 0));
            // AngleDist convergence stops three float32 ULPs above -0.5.
            if (pitch >= -0.5f) MG_CHECK(std::bit_cast<uint32_t>(cannon->m_AngleX) ==
                (pitch == 0 ? 0U : 0xbefffffdU));
            check_health(target, impacts.empty() ? 50 : 36);
            trace.record(session, target, nullptr, cannon);
            trace.complete(session);
            return trace.states;
}
void cannon_boundaries() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (int radius : {49, 50, 51}) {
            const auto reference = cannon_boundary_run(mode, radius, -1.5f, 0);
            compare(reference, cannon_boundary_run(mode, radius, -1.5f, 1));
        }
        for (float pitch : {0.0f, -0.5f}) {
            const auto reference = cannon_boundary_run(mode, 50, pitch, 0);
            compare(reference, cannon_boundary_run(mode, 50, pitch, 1));
        }
    }
}

std::vector<std::string> cannon_obstruction_run(random::Mode mode, int variant, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, false};
    Session session(profile);
    assets.install();
    MG_CHECK(session.remove(2));
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
    target->InitMaxHitpoint(50);
    target->SetMaxSpeed(0);
    auto *cannon = tests::create_battle_cannon(2, 80, 100);
    CMatrixCannon *blocker = nullptr;
    if (variant == 1) {
        blocker = tests::create_battle_cannon(2, 90, 150);
        blocker->m_CurrState = CANNON_UNDER_CONSTRUCTION;
    }
    if (variant == 2) cannon->m_CurrState = CANNON_UNDER_CONSTRUCTION;
    Trace trace(session, profile, ("cannon-obstruction-" + std::to_string(variant)).c_str(), run);
    float health = target->GetHitPoint();
    std::vector<int> impacts;
    for (int i = 0; i < 100; ++i) {
        if (variant == 1 && i == 50) {
            MG_CHECK(!cannon->m_TargetCore);
            trace.command(session, "remove-obstruction", [&] { g_MatrixMap->StaticDelete(blocker); blocker = nullptr; return true; });
        }
        trace.record(session, target, nullptr, cannon);
        session.advance();
        if (variant == 2 || (variant == 1 && i < 50)) MG_CHECK(!cannon->m_TargetCore);
        if (target->GetHitPoint() != health) {
            health = target->GetHitPoint();
            impacts.push_back(session.time());
            check_health(target, 50 - int(impacts.size()) * 2);
        }
    }
    const std::vector<int> expected = variant == 0 ? std::vector<int>{220, 320, 420, 520, 620, 720, 820, 920} :
        variant == 1 ? std::vector<int>{660, 760, 860, 960} : std::vector<int>{};
    MG_CHECK(impacts == expected);
    check_health(target, 50 - int(expected.size()) * 2);
    trace.record(session, target, nullptr, cannon);
    trace.complete(session);
    return trace.states;
}
void cannon_obstruction() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int variant : {0, 1, 2}) {
        const auto reference = cannon_obstruction_run(mode, variant, 0);
        compare(reference, cannon_obstruction_run(mode, variant, 1));
    }
}

const tests::Case cases[] = {{"game.combat.cannon_firing", cannon_firing},
    {"game.combat.robot_firing", robot_firing}, {"game.combat.plasma_flight", plasma_flight},
    {"game.combat.axis_aligned_trace", axis_aligned_trace},
    {"game.combat.volcano_firing", volcano_firing}, {"game.combat.electric_firing", electric_firing},
    {"game.combat.flame_firing", flame_firing}, {"game.combat.gun_firing", gun_firing},
    {"game.combat.missile_firing", missile_firing}, {"game.combat.mortar_firing", mortar_firing},
    {"game.combat.repair_firing", repair_firing}, {"game.combat.heat_boundary", heat_boundary}, {"game.combat.bomb_detonation", bomb_detonation},
    {"game.combat.cannon_projectiles", cannon_projectiles}, {"game.combat.cannon_boundaries", cannon_boundaries}, {"game.combat.cannon_obstruction", cannon_obstruction}};
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
