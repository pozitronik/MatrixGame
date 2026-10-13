// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_mesh.hpp"
#include "battle_geometry.hpp"
#include "battle_building.hpp"
#include "battle_material.hpp"
#include "MatrixMapGroup.hpp"
#include "MatrixObject.hpp"
#include <set>
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
// Forming protected member pointers in a derived observer needs no downcast of live objects.
struct StatusObservation : CMatrixMapStatic {
    static int ablaze(CMatrixMapStatic &target) { return (target.* &StatusObservation::GetAblazeTTL)(); }
    static int shorted(CMatrixMapStatic &target) { return (target.* &StatusObservation::GetShortedTTL)(); }
    static bool is_ablaze(CMatrixMapStatic &target) { return (target.* &StatusObservation::IsAblaze)(); }
    static bool is_shorted(CMatrixMapStatic &target) { return (target.* &StatusObservation::IsShorted)(); }
};
std::set<CMatrixMapObject *> building_ruins() {
    std::set<CMatrixMapObject *> result;
    for (int x = 0; x < g_MatrixMap->m_GroupSize.x; ++x)
        for (int y = 0; y < g_MatrixMap->m_GroupSize.y; ++y) {
            auto *group = g_MatrixMap->GetGroupByIndex(x, y);
            if (!group) continue;
            for (int i = 0; i < group->ObjectsCnt(); ++i) {
                auto *object = group->GetObject(i);
                if (object->GetObjectType() == OBJECT_TYPE_MAPOBJECT)
                    result.insert(static_cast<CMatrixMapObject *>(object));
            }
        }
    return result;
}
struct Trace {
    std::ofstream file;
    std::vector<std::string> states;
    Trace(Session &session, Profile p, const char *name, int run,
          const char *fixture = "synthetic-armed-boxes-v2") {
        const auto directory = std::filesystem::current_path() / "scenario-traces";
        std::filesystem::create_directories(directory);
        const auto path = directory / (std::string(name) + "-" +
            (p.mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" +
            std::to_string(p.step_ms) + "-" + std::to_string(run) + ".jsonl");
        file.open(path, std::ios::trunc);
        MG_CHECK(file.good());
        std::string header = session.metadata(fixture);
        header.pop_back();
        header += ",\"scenario\":\"" + std::string(name) + "\"}";
        file << header << '\n';
        file.flush();
        MG_CHECK(file.good());
        std::cerr << "Combat profile: " << header << "\nTrace: " << path.string() << std::endl;
    }
    void record(Session &session, CMatrixRobotAI *target, CMatrixRobotAI *shooter = nullptr,
                CMatrixCannon *cannon = nullptr, SObjectCore *retired_owner = nullptr, CMatrixBuilding *building = nullptr, bool demolition = false) {
        const auto draws = random::draw_count();
        std::ostringstream out;
        out << "{\"type\":\"snapshot\",\"state\":" << session.snapshot()
            << ",\"combat\":{\"health_bits\":" << std::bit_cast<uint32_t>(target->GetHitPoint())
            << ",\"ablaze_ttl\":" << StatusObservation::ablaze(*target) << ",\"shorted_ttl\":" << StatusObservation::shorted(*target)
            << ",\"robot_state\":" << target->m_CurrState;
        if (shooter) out << ",\"heat\":" << shooter->GetWeapon(0).m_Heat;
        if (cannon) out << ",\"cannon_target_is_patient\":" <<
            (cannon->m_TargetCore && cannon->m_TargetCore->m_Object == target ? "true" : "false")
            << ",\"yaw_bits\":" << std::bit_cast<uint32_t>(cannon->m_Angle)
            << ",\"pitch_bits\":" << std::bit_cast<uint32_t>(cannon->m_AngleX)
            << ",\"turret_yaw_bits\":" << std::bit_cast<uint32_t>(cannon->m_Unit[1].u1.s1.m_Angle)
            << ",\"cannon_health_bits\":" << std::bit_cast<uint32_t>(cannon->GetHitPoint());
        if (retired_owner) out << ",\"owner_present\":" << (retired_owner->m_Object ? "true" : "false");
        if (building) out << ",\"building_health_bits\":" << std::bit_cast<uint32_t>(building->GetHitPoint())
            << ",\"building_state\":" << building->m_State;
        if (demolition) out << ",\"ruin_count\":" << building_ruins().size()
            << ",\"smoke_count\":" << g_MatrixMap->m_EffectSpawnersCnt;
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
    MG_CHECK(session.matches(1, shooter) && session.matches(3, target));
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
    MG_CHECK(session.matches(1, target) && session.matches(2, next));
    target->InitMaxHitpoint(50);
    next->InitMaxHitpoint(50);
    auto *cannon = tests::create_battle_cannon(2, 80, 100);
    const float initial_yaw = cannon->m_Unit[1].u1.s1.m_Angle;
    bool yaw_changed = false;
    Trace trace(session, profile, "cannon-retarget", run);
    std::vector<int> impacts;
    float health = target->GetHitPoint();
    for (int i = 0; i < 100; ++i) {
        trace.record(session, target, nullptr, cannon);
        session.advance();
        yaw_changed |= cannon->m_Unit[1].u1.s1.m_Angle != initial_yaw;
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
        yaw_changed |= cannon->m_Unit[1].u1.s1.m_Angle != initial_yaw;
        if (next->GetHitPoint() != health) {
            health = next->GetHitPoint();
            impacts.push_back(session.time());
            check_health(next, 50 - int(impacts.size()) * 2);
        }
    }
    MG_CHECK((impacts == std::vector<int>{1120, 1220, 1320, 1420, 1520, 1620, 1720, 1820, 1920}));
    MG_CHECK(cannon->m_TargetCore && cannon->m_TargetCore->m_Object == next);
    MG_CHECK(next->GetHitPoint() < 5);
    MG_CHECK(yaw_changed && cannon->m_Angle == 0);
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
// Unregistered correctness probe, not a behavior-preservation oracle.
// Defect tracking: https://github.com/pozitronik/MatrixGame/issues/129
// Source/reproduction: docs/testing/armed-fixtures.md, "Axis-aligned trace defect".
void axis_aligned_trace() {
    bool all_hit = true;
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode, 82, 10, 1000, false, false});
        auto *target = robot(PLAYER_SIDE);
        MG_CHECK(session.matches(1, target));
        const D3DXVECTOR3 end = target->GetGeoCenter();
        const DWORD masks[]{TRACE_ROBOT | TRACE_OBJECTSPHERE, TRACE_ROBOT};
        for (DWORD mask : masks) {
            // Both rays intersect the same object; test sphere and real mesh Pick.
            MG_CHECK(g_MatrixMap->Trace(nullptr, D3DXVECTOR3(80, 100, 50), end, mask, nullptr) == target);
            const bool hit = g_MatrixMap->Trace(nullptr, D3DXVECTOR3(end.x, 100, 50), end, mask, nullptr) == target;
            std::cerr << "Axis trace mode=" << int(mode) << " sphere=" << bool(mask & TRACE_OBJECTSPHERE)
                << " hit=" << hit << " expected=1\n";
            all_hit &= hit;
        }
    }
    MG_CHECK(all_hit);
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
            g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = 1;
            g_Config.m_WeaponCooldown[Weap2Index(WEAPON_CANNON2)] = 100;
            g_Config.m_RobotDamages[Weap2Index(WEAPON_CANNON2)] = {2, 0, 10};
            auto *target = robot(PLAYER_SIDE);
            target->InitMaxHitpoint(50);
            target->SetMaxSpeed(0);
            auto *cannon = tests::create_battle_cannon(2, 70, 200);
            const auto initial_delta = target->GetGeoCenter() - cannon->GetGeoCenter();
            const float distance_squared = D3DXVec3LengthSq(&initial_delta);
            float eligible = std::sqrt(distance_squared);
            // Bracket the actual production float comparison, including D3DX rounding.
            while (eligible * eligible < distance_squared) eligible = std::nextafter(eligible, INFINITY);
            while (std::nextafter(eligible, 0.f) * std::nextafter(eligible, 0.f) >= distance_squared)
                eligible = std::nextafter(eligible, 0.f);
            const float range = radius < 50 ? std::nextafter(eligible, 0.f) :
                radius == 50 ? eligible : std::nextafter(eligible, INFINITY);
            cannon->m_Weapons[0]->ModifyDist(range - 1);
            MG_CHECK(cannon->m_Weapons[0]->GetWeaponDist() == range);
            std::cout << "Cannon boundary distance/range bits: " << std::bit_cast<uint32_t>(distance_squared)
                << '/' << std::bit_cast<uint32_t>(range) << '\n';
            MG_CHECK((range * range >= distance_squared) == (radius >= 50));
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
                MG_CHECK(D3DXVec3LengthSq(&delta) == distance_squared);
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
            // Production rotation stops within its 0.001-radian convergence threshold.
            if (pitch >= -0.5f) MG_CHECK(cannon->m_AngleX >= pitch && cannon->m_AngleX < pitch + .001f);
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

std::vector<std::string> delayed_damage_run(random::Mode mode, int variant, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, false};
    Session session(profile);
    assets.install();
    g_Config.m_RobotDamages[Weap2Index(WEAPON_FLAMETHROWER)] = {0, 0, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LIGHTENING)] = {0, 0, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_ABLAZE)] = {1, 1, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_SHORTED)] = {2, 2, 0};
    auto *target = robot(PLAYER_SIDE);
    target->InitMaxHitpoint(100);
    target->SetMaxSpeed(0);
    Trace trace(session, profile, ("delayed-damage-" + std::to_string(variant)).c_str(), run);
    auto apply = [&](EWeapon weapon) {
        return !target->Damage(weapon, target->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr);
    };
    trace.command(session, "apply-status", [&] {
        if (variant != 1) MG_CHECK(apply(WEAPON_FLAMETHROWER));
        if (variant != 0) MG_CHECK(apply(WEAPON_LIGHTENING));
        return true;
    });
    MG_CHECK(StatusObservation::ablaze(*target) == (variant == 1 ? 0 : 300));
    MG_CHECK(StatusObservation::shorted(*target) == (variant == 0 ? 0 : 500));
    float health = target->GetHitPoint();
    std::vector<std::pair<int, int>> impacts;
    for (int i = 0; i < 100; ++i) {
        trace.record(session, target);
        session.advance();
        MG_CHECK(StatusObservation::ablaze(*target) == (variant == 1 ? 0 : std::max(0, 300 - session.time())));
        MG_CHECK(StatusObservation::shorted(*target) == (variant == 0 ? 0 : std::max(0, 500 - session.time())));
        MG_CHECK(StatusObservation::is_ablaze(*target) == (StatusObservation::ablaze(*target) > 0));
        MG_CHECK(StatusObservation::is_shorted(*target) == (StatusObservation::shorted(*target) > 0));
        if (target->GetHitPoint() != health) {
            health = target->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(health * 10)));
        }
    }
    const std::vector<std::pair<int, int>> expected = variant == 0 ?
        std::vector<std::pair<int, int>>{{10,91}, {100,82}, {190,73}, {280,64}} : variant == 1 ?
        std::vector<std::pair<int, int>>{{10,98}, {60,96}, {110,94}, {160,92}, {210,90}, {260,88}, {310,86}, {360,84}, {410,82}, {460,80}} :
        std::vector<std::pair<int, int>>{{10,89}, {60,87}, {100,78}, {110,76}, {160,74}, {190,65}, {210,63}, {260,61}, {280,52}, {310,50}, {360,48}, {410,46}, {460,44}};
    MG_CHECK(impacts == expected);
    check_health(target, expected.back().second);
    trace.record(session, target);
    trace.complete(session);
    return trace.states;
}
void delayed_damage() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int variant : {0, 1, 2}) {
        const auto reference = delayed_damage_run(mode, variant, 0);
        compare(reference, delayed_damage_run(mode, variant, 1));
    }
}

std::vector<std::string> bomb_order_run(random::Mode mode, bool targeted, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 4000, false, true};
    Session session(profile);
    assets.install();
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 150;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
    auto *bomber = robot(PLAYER_SIDE);
    auto *target = robot(2);
    target->InitMaxHitpoint(50);
    target->SetMaxSpeed(0);
    tests::arm_battle_robot(*bomber, RUK_WEAPON_BOMB);
    CoreLease core(bomber->GetCore(DEBUG_CALL_INFO));
    Trace trace(session, profile, targeted ? "bomb-arrival-target" : "bomb-arrival-ground", run);
    auto *side = g_MatrixMap->GetPlayerSide();
    trace.command(session, "order-bomb(3)", [&] {
        side->PGOrderBomb(side->SelGroupToLogicGroup(), CPoint(50, 50), targeted ? target : nullptr);
        return session.robot(1).order == mpo_Bomb;
    });
    int dying_at = 0, removed_at = 0;
    float health = target->GetHitPoint();
    std::vector<std::pair<int, int>> impacts;
    for (int i = 0; i < 3000; ++i) {
        trace.record(session, target, nullptr, nullptr, core.get());
        session.advance();
        if (core->m_Object && bomber->m_CurrState == ROBOT_DIP && !dying_at) dying_at = session.time();
        if (!core->m_Object && !removed_at) removed_at = session.time();
        if (target->GetHitPoint() != health) {
            health = target->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(health * 10)));
        }
    }
    MG_CHECK(dying_at == (targeted ? 7920 : 11820));
    MG_CHECK(removed_at == dying_at + (mode == random::Mode::ParkMiller ? 3150 : 4140));
    const std::vector<std::pair<int, int>> expected{{targeted ? 8210 : 12110, 40}};
    MG_CHECK(impacts == expected);
    check_health(target, 40);
    MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    trace.record(session, target, nullptr, nullptr, core.get());
    trace.complete(session);
    return trace.states;
}
void bomb_order() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (bool targeted : {false, true}) {
            const auto reference = bomb_order_run(mode, targeted, 0);
            compare(reference, bomb_order_run(mode, targeted, 1));
        }
    }
}

std::vector<std::string> repair_death_run(random::Mode mode, bool kill_healer, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_REPAIR)] = 600;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_REPAIR)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {20, 20, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {1000, 1000, 0};
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    auto *healer = robot(PLAYER_SIDE);
    auto *target = robot(PLAYER_SIDE, 1);
    tests::arm_battle_robot(*healer, RUK_WEAPON_REPAIR);
    target->InitMaxHitpoint(50);
    target->SetMaxSpeed(0);
    MG_CHECK(!target->Damage(WEAPON_LASER, target->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
    auto *victim = kill_healer ? healer : target;
    CoreLease core(victim->GetCore(DEBUG_CALL_INFO));
    auto *side = g_MatrixMap->GetPlayerSide();
    Trace trace(session, profile, kill_healer ? "repair-kill-healer" : "repair-kill-patient", run);
    trace.command(session, "order-repair(2)", [&] {
        side->PGOrderRepair(side->SelGroupToLogicGroup(), target);
        return session.robot(1).order == mpo_Repair;
    });
    int removed_at = 0;
    for (int i = 0; i < 650; ++i) {
        if (i == 30) {
            check_health(target, 38);
            trace.command(session, "lethal-damage", [&] {
                return victim->Damage(WEAPON_GUN, victim->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr);
            });
            MG_CHECK(victim->m_CurrState == ROBOT_DIP);
        }
        auto *observed = kill_healer || i < 30 ? target : healer;
        trace.record(session, observed, nullptr, nullptr, core.get());
        session.advance();
        if (!core->m_Object && !removed_at) removed_at = session.time();
        if (i >= 30 && kill_healer) check_health(target, 38);
        if (i >= 40 && !kill_healer) {
            MG_CHECK(session.robot(1).target_is_null);
            MG_CHECK(!healer->FindOrderLikeThat(ROT_FIRE));
        }
    }
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
    MG_CHECK(removed_at == (kill_healer ? (mode == random::Mode::ParkMiller ? 4780 : 3460) :
        (mode == random::Mode::ParkMiller ? 3980 : 2810)));
    trace.record(session, kill_healer ? target : healer, nullptr, nullptr, core.get());
    trace.complete(session);
    return trace.states;
}
void repair_death() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool kill_healer : {false, true}) {
        const auto reference = repair_death_run(mode, kill_healer, 0);
        compare(reference, repair_death_run(mode, kill_healer, 1));
    }
}

// Reassemble only within fixture setup; reinitializing weapons prevents modifier accumulation.
void install_robot_modules(CMatrixRobotAI &robot, ERobotUnitKind armor, ERobotUnitKind head) {
    MG_CHECK(robot.m_UnitCnt == 3 && robot.m_Unit[1].m_Type == MRT_ARMOR);
    const_cast<SBotWeapon &>(robot.GetWeapon(0)).Release();
    robot.m_Unit[1].u1.s1.m_Kind = armor;
    if (head != RUK_UNKNOWN) {
        robot.UnitInsert(3, MRT_HEAD, head);
        robot.m_Unit[3].m_Graph = HNew(g_MatrixHeap) CVectorObjectAnim;
        const tests::MeshAttachment terminal{1, L"terminal", 0, 0, 0};
        robot.m_Unit[3].m_Graph->Init(tests::load_battle_mesh(std::span(&terminal, 1)), nullptr);
        robot.m_Unit[3].m_Graph->FirstFrame();
    }
    robot.RobotWeaponInit();
    robot.CalcRobotMass();
    robot.RChange(MR_Matrix);
    robot.RNeed(MR_Matrix);
    robot.m_CalcBoundsLastTime=g_MatrixMap->GetTime()-100;
    robot.JoinToGroup();
}
void configure_module_heads() {
    const wchar_t *names[] = {L"S", L"D", L"L", L"F"};
    const wchar_t *fields[] = {L"HIT_POINT_ADD", L"COOL_DOWN", L"FIRE_DISTANCE", L"OVERHEAT", L"CHASSIS_SPEED", L"RADAR_DISTANCE", L"LIGHT_PROTECT", L"BOMB_PROTECT", L"AIM_PROTECT"};
    const int values[][9] = {{25,-50,50,50,25,50,50,25,75}, {50,50,-50,-50,-50,-50,0,0,0},
        {75,0,0,0,0,0,120,-20,120}, {100,-150,-150,-150,-150,-150,-20,120,-20}};
    auto *heads = g_MatrixData->BlockGet(L"Chars")->BlockGet(L"Heads");
    for (int n = 0; n < 4; ++n) for (int f = 0; f < 9; ++f)
        heads->BlockGetAdd(names[n])->ParSetAdd(fields[f], std::to_wstring(values[n][f]));
    g_Config.m_RobotRadarR = 400;
    const int structures[] = {ARMOR1_STRUCTURE, ARMOR2_STRUCTURE, ARMOR3_STRUCTURE, ARMOR4_STRUCTURE, ARMOR5_STRUCTURE, ARMOR6_STRUCTURE};
    const int rotations[] = {ARMOR1_ROTATION_SPEED, ARMOR2_ROTATION_SPEED, ARMOR3_ROTATION_SPEED, ARMOR4_ROTATION_SPEED, ARMOR5_ROTATION_SPEED, ARMOR6_ROTATION_SPEED};
    for (int n = 0; n < 6; ++n) {
        g_Config.m_ItemChars[structures[n]] = float(10 * (n + 1));
        g_Config.m_ItemChars[rotations[n]] = 0.04f;
    }
}
void module_modifiers() {
    const int health[][6] = {{1035,1045,1055,1065,1075,1085}, {1060,1070,1080,1090,1100,1110},
        {1085,1095,1105,1115,1125,1135}, {1110,1120,1130,1140,1150,1160}};
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int head = 1; head <= 4; ++head)
        for (int armor = 1; armor <= 6; ++armor) {
            tests::BattleCombatAssets assets;
            Session session({mode, 82, 10, 1000, false, false});
            assets.install();
            configure_weapon(WEAPON_LASER, WEAPON_LASER_HEAT);
            configure_module_heads();
            auto *unit = robot(PLAYER_SIDE);
            tests::arm_battle_robot(*unit, RUK_WEAPON_LASER);
            install_robot_modules(*unit, ERobotUnitKind(armor), ERobotUnitKind(head));
            check_health(unit, health[head - 1][armor - 1]);
            MG_CHECK(unit->GetMaxHitPoint() == unit->GetHitPoint());
            const float ranges[] = {900, 300, 600, 0}, speeds[] = {.75f, .3f, .6f, 0}, radars[] = {600,200,400,0};
            const int heat[] = {15,5,10,0};
            const float light[] = {.5f,0,1,0}, bomb[] = {.25f,0,0,1}, aim[] = {.75f,0,1,0};
            MG_CHECK(unit->GetWeapon(0).GetWeaponDist() == ranges[head - 1]);
            MG_CHECK(unit->GetWeapon(0).m_HeatMod == heat[head - 1]);
            MG_CHECK(unit->GetMaxSpeed() == speeds[head - 1]);
            MG_CHECK(unit->m_RadarRadius == radars[head - 1]);
            MG_CHECK(unit->m_LightProtect == light[head - 1]);
            MG_CHECK(unit->m_BombProtect == bomb[head - 1]);
            MG_CHECK(unit->m_AimProtect == aim[head - 1]);
        }
}
std::vector<std::string> module_firing_run(random::Mode mode, int head, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode,82,10,1000,false,true};
    Session session(profile);
    assets.install();
    configure_weapon(WEAPON_LASER, WEAPON_LASER_HEAT);
    configure_module_heads();
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 1200;
    auto *shooter = robot(PLAYER_SIDE);
    auto *target = robot(2);
    tests::arm_battle_robot(*shooter, RUK_WEAPON_LASER);
    install_robot_modules(*shooter, RUK_ARMOR_BIREX, ERobotUnitKind(head));
    target->InitMaxHitpoint(100);
    target->SetMaxSpeed(0);
    Trace trace(session, profile, ("head-firing-" + std::to_string(head)).c_str(), run);
    trace.command(session, "context-target(3)", [&] { return session.target_command(3); });
    float health = target->GetHitPoint();
    std::vector<int> impacts;
    for (int i = 0; i < 200; ++i) {
        trace.record(session, target, shooter);
        session.advance();
        if (health != target->GetHitPoint()) {
            health = target->GetHitPoint();
            impacts.push_back(session.time());
            check_health(target, 100 - int(impacts.size()) * 2);
        }
    }
    MG_CHECK(impacts.size() == (head == 0 ? 19U : head == 1 ? 37U : 13U));
    const int interval = head == 0 ? 100 : head == 1 ? 50 : 150;
    for (size_t i = 0; i < impacts.size(); ++i) MG_CHECK(impacts[i] == 200 + int(i) * interval);
    check_health(target, head == 0 ? 62 : head == 1 ? 26 : 74);
    trace.record(session, target, shooter);
    trace.complete(session);
    return trace.states;
}
void module_firing() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int head : {0,1,2}) {
        const auto reference = module_firing_run(mode, head, 0);
        compare(reference, module_firing_run(mode, head, 1));
    }
}

std::vector<std::string> splash_boundary_run(random::Mode mode, int offset, int run, float friendly = 1) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode,82,10,1000,false,false};
    Session session(profile);
    assets.install();
    g_MatrixMap->m_Difficulty.k_friendly_fire = friendly;
    auto *bomber = robot(PLAYER_SIDE);
    auto *patient = robot(PLAYER_SIDE, 1);
    patient->InitMaxHitpoint(50);
    patient->SetMaxSpeed(0);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 1;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
    tests::arm_battle_robot(*bomber, RUK_WEAPON_BOMB);
    // Use the loaded spheres after assembly, then set the real effect's exact radius.
    auto delta = patient->GetGeoCenter() - bomber->GetGeoCenter();
    const float radius = D3DXVec3Length(&delta) - patient->GetRadius() + float(offset);
    const_cast<SBotWeapon &>(bomber->GetWeapon(0)).ModifyDist(radius-1);
    MG_CHECK(bomber->GetWeapon(0).GetWeaponDist()==radius);
    Trace trace(session, profile, ("splash-boundary-" + std::to_string(offset) + "-friendly-" + std::to_string(friendly)).c_str(), run);
    trace.command(session, "detonate(1)", [&] { bomber->BigBoom(); return bomber->IsMustDie(); });
    float health = patient->GetHitPoint();
    std::vector<int> impacts;
    for (int i = 0; i < 100; ++i) {
        trace.record(session, patient);
        session.advance();
        if (health != patient->GetHitPoint()) { health = patient->GetHitPoint(); impacts.push_back(session.time()); }
    }
    MG_CHECK(impacts == (offset == 1 && friendly > 0 ? std::vector<int>{310} : std::vector<int>{}));
    check_health(patient, offset == 1 ? 50 - int(4 * friendly) : 50);
    trace.record(session, patient);
    trace.complete(session);
    return trace.states;
}
void splash_boundary() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int offset : {-1,0,1}) {
        const auto reference = splash_boundary_run(mode, offset, 0);
        compare(reference, splash_boundary_run(mode, offset, 1));
    }
}

void position_fixture_robot(CMatrixRobotAI &robot, float x, float y) {
    robot.m_PosX = x;
    robot.m_PosY = y;
    robot.SetMaxSpeed(0);
    robot.MapPosCalc();
    robot.RChange(MR_Matrix);
    robot.RNeed(MR_Matrix);
    robot.m_CalcBoundsLastTime=g_MatrixMap->GetTime()-100;
    robot.JoinToGroup();
}
std::vector<std::string> chained_bombs_run(random::Mode mode, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode,82,10,1000,false,false};
    Session session(profile);
    assets.install();
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 150;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
    g_Config.m_WeaponStrengthAI[RUK_WEAPON_LASER] = 10;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 0;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100000;
    auto *first = robot(PLAYER_SIDE);
    auto *patient = robot(PLAYER_SIDE, 1);
    auto *second = robot(2);
    auto *observer = robot(2, 1);
    observer->SetMaxSpeed(0);
    auto *survivor = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 500, 600);
    survivor->SetMaxSpeed(0);
    tests::arm_battle_robot(*first, RUK_WEAPON_BOMB);
    tests::arm_battle_robot(*second, RUK_WEAPON_BOMB);
    tests::arm_battle_robot(*patient, RUK_WEAPON_LASER);
    position_fixture_robot(*second, 100, 260);
    position_fixture_robot(*patient, 100, 320);
    second->InitMaxHitpoint(10);
    patient->InitMaxHitpoint(10);
    patient->CalcStrength();
    MG_CHECK(patient->GetStrength() == 10);
    CoreLease first_core(first->GetCore(DEBUG_CALL_INFO)), second_core(second->GetCore(DEBUG_CALL_INFO)), patient_core(patient->GetCore(DEBUG_CALL_INFO));
    Trace trace(session, profile, "chained-bombs", run);
    trace.command(session, "detonate(1)", [&] { first->BigBoom(); return first->IsMustDie(); });
    int second_dying = 0, patient_dying = 0, first_removed = 0, second_removed = 0, patient_removed = 0;
    float patient_health = patient->GetHitPoint();
    std::vector<std::pair<int, int>> impacts;
    for (int i = 0; i < 650; ++i) {
        trace.record(session, observer, nullptr, nullptr, second_core.get());
        session.advance();
        if (second_core->m_Object && second->m_CurrState == ROBOT_DIP && !second_dying) second_dying = session.time();
        if (patient_core->m_Object && patient->m_CurrState == ROBOT_DIP && !patient_dying) patient_dying = session.time();
        if (!first_core->m_Object && !first_removed) first_removed = session.time();
        if (!second_core->m_Object && !second_removed) second_removed = session.time();
        if (!patient_core->m_Object && !patient_removed) patient_removed = session.time();
        if (patient_core->m_Object && patient_health != patient->GetHitPoint()) {
            patient_health = patient->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(patient_health * 10)));
        }
    }
    MG_CHECK(second_dying == 310 && patient_dying == 610);
    MG_CHECK(first_removed == (mode == random::Mode::ParkMiller ? 4080 : 4140));
    MG_CHECK(second_removed == (mode == random::Mode::ParkMiller ? 3140 : 4290));
    MG_CHECK(patient_removed == (mode == random::Mode::ParkMiller ? 5520 : 4440));
    const std::vector<std::pair<int, int>> expected{{310, 6}, {610, -4}};
    MG_CHECK(impacts == expected);
    MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL) == 1);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
    check_health(observer, 1000);
    check_health(survivor, 1000);
    trace.record(session, observer, nullptr, nullptr, second_core.get());
    trace.complete(session);
    return trace.states;
}
void chained_bombs() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto reference = chained_bombs_run(mode, 0);
        compare(reference, chained_bombs_run(mode, 1));
    }
}

std::vector<std::string> repair_redirect_run(random::Mode mode, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_REPAIR)] = 600;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_REPAIR)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {20, 20, 0};
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = .04f;
    auto *healer = robot(PLAYER_SIDE);
    auto *original = robot(PLAYER_SIDE, 1);
    auto *replacement = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 160, 260);
    tests::arm_battle_robot(*healer, RUK_WEAPON_REPAIR);
    for (auto *unit : {original, replacement}) {
        unit->InitMaxHitpoint(50);
        unit->SetMaxSpeed(0);
        MG_CHECK(!unit->Damage(WEAPON_GUN, unit->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
        check_health(unit, 30);
    }
    Trace trace(session, profile, "repair-redirection", run);
    auto *side = g_MatrixMap->GetPlayerSide();
    auto order = [&](CMatrixRobotAI *target) {
        side->PGOrderRepair(side->SelGroupToLogicGroup(), target);
        return session.robot(1).order == mpo_Repair;
    };
    trace.command(session, "order-repair(2)", [&] { return order(original); });
    std::vector<std::pair<int, int>> impacts;
    float health = replacement->GetHitPoint(), previous_health = original->GetHitPoint();
    std::vector<std::pair<int, int>> original_impacts;
    for (int i = 0; i < 150; ++i) {
        if (i == 15) {
            check_health(original, 34);
            trace.command(session, "redirect-repair", [&] { return order(replacement); });
        }
        trace.record(session, replacement, healer);
        session.advance();

        if (original->GetHitPoint() != previous_health) {
            previous_health = original->GetHitPoint();
            original_impacts.emplace_back(session.time(), int(std::lround(previous_health * 10)));
        }
        if (replacement->GetHitPoint() != health) {
            health = replacement->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(health * 10)));
        }
    }
    const std::vector<std::pair<int, int>> expected{{410, 34}, {510, 38}, {610, 42}, {710, 46}, {810, 50}};
    const std::vector<std::pair<int, int>> expected_original{{110, 34}, {210, 38}, {1010, 42}, {1110, 46}, {1210, 50}};
    MG_CHECK(impacts == expected && original_impacts == expected_original);
    check_health(replacement, 50);
    check_health(original, 50);
    trace.record(session, replacement, healer);
    trace.complete(session);
    return trace.states;
}
void repair_redirect() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto reference = repair_redirect_run(mode, 0);
        compare(reference, repair_redirect_run(mode, 1));
    }
}
std::vector<std::string> cannon_patient_run(random::Mode mode, bool plasma, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    MG_CHECK(session.remove(3));
    const EWeapon weapon = plasma ? WEAPON_PLASMA : WEAPON_LASER;
    configure_weapon(weapon, plasma ? WEAPON_PLASMA_HEAT : WEAPON_LASER_HEAT);
    g_Config.m_CannonDamages[Weap2Index(weapon)] = {2, 0, 10};
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.m_Hitpoint = 50;
    props.weapon = WEAPON_CANNON2;
    props.seek_radius = 300;
    props.max_da = .1f;
    props.max_top_angle = 1.5f;
    props.max_bottom_angle = -1.5f;
    auto *shooter = robot(PLAYER_SIDE);
    tests::arm_battle_robot(*shooter, plasma ? RUK_WEAPON_PLASMA : RUK_WEAPON_LASER);
    auto *patient = tests::create_battle_cannon(2, 500, 500);
    patient->m_CurrState = CANNON_UNDER_CONSTRUCTION;
    Trace trace(session, profile, plasma ? "cannon-patient-plasma" : "cannon-patient-laser", run);
    auto *side = g_MatrixMap->GetPlayerSide();
    trace.command(session, "order-attack-cannon", [&] {
        side->PGOrderAttack(side->SelGroupToLogicGroup(), CPoint(50, 50), patient);
        return session.robot(1).order == mpo_Attack;
    });
    float health = patient->GetHitPoint();
    std::vector<std::pair<int, int>> impacts;
    for (int i = 0; i < 200; ++i) {
        trace.record(session, shooter, shooter, patient);
        session.advance();
        if (patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(health)));
        }
    }
    const std::vector<std::pair<int, int>> expected =
            plasma ? std::vector<std::pair<int, int>>{{1130, 48}, {1220, 46}, {1320, 44}, {1430, 42}, {1530, 40}}
                   : std::vector<std::pair<int, int>>{{200, 48}, {300, 46}, {400, 44}, {500, 42}, {600, 40}};
    MG_CHECK(impacts == expected);
    MG_CHECK(health == 40 && patient->m_CurrState == CANNON_UNDER_CONSTRUCTION);
    trace.record(session, shooter, shooter, patient);
    trace.complete(session);
    return trace.states;
}
void cannon_patient() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (bool plasma : {false, true}) {
            const auto reference = cannon_patient_run(mode, plasma, 0);
            compare(reference, cannon_patient_run(mode, plasma, 1));
        }
}

std::vector<std::string> building_patient_run(random::Mode mode, bool plasma, EBuildingType kind, bool lethal, int run) {
    tests::BattleMaterialProfile material_profile;
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    MG_CHECK(session.remove(3));
    position_fixture_robot(*robot(2), 620, 620);
    const EWeapon weapon = plasma ? WEAPON_PLASMA : WEAPON_LASER;
    configure_weapon(weapon, plasma ? WEAPON_PLASMA_HEAT : WEAPON_LASER_HEAT);
    g_Config.m_BuildingDamages[Weap2Index(weapon)] = {lethal ? 100 : 2, 0, 0};
    for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
        g_Config.m_Timings[resource] = 100000;
    auto *shooter = robot(PLAYER_SIDE);
    tests::arm_battle_robot(*shooter, plasma ? RUK_WEAPON_PLASMA : RUK_WEAPON_LASER);
    // Building aiming fixes z at terrain + 20. Put that aim point inside the
    // target, rather than on the default mesh roof where roundoff changes hits.
    if (lethal) {
        const auto texture = std::wstring(OBJECT_PATH_BUILDINGS_RUINS) + L"b" + std::to_wstring(int(kind));
        tests::install_battle_texture(texture.c_str());
        tests::load_battle_material_mesh({}, (texture + L".vo").c_str(), texture.c_str());
        if (kind != BUILDING_BASE) {
            const auto secondary = texture + L"?Trans";
            tests::install_battle_texture(secondary.c_str());
            tests::load_battle_material_mesh({}, (texture + L"p.vo").c_str(), secondary.c_str());
        }
        // The final base blast runs, while its damage is isolated from the
        // weapon-driven lifetime contract; splash damage has separate checks.
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 20;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
        g_Config.m_BuildingDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
        g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
        g_Config.m_CannonDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    }
    auto *building_mesh = tests::load_battle_mesh({}, nullptr, false, 40.0f);
    MG_CHECK(building_mesh->GetFrame(0)->m_Min.z < 20 && building_mesh->GetFrame(0)->m_Max.z > 20);
    auto owned = tests::make_battle_building(*building_mesh, 2, kind, 200, 200, 50);
    auto *patient = owned.get();
    g_MatrixMap->AddObject(patient, true);
    owned.release();
    Trace trace(session, profile, (std::string("building-patient-") + (plasma ? "plasma-" : "laser-") +
        std::to_string(int(kind)) + (lethal ? "-lethal" : "-surviving")).c_str(), run, lethal ? "synthetic-armed-demolition-v1" : "synthetic-armed-buildings-v2");
    auto *side = g_MatrixMap->GetPlayerSide();
    trace.command(session, "order-attack-building", [&] {
        side->PGOrderAttack(side->SelGroupToLogicGroup(), CPoint(20, 20), patient);
        return session.robot(1).order == mpo_Attack;
    });
    float health = patient->GetHitPoint();
    std::vector<std::pair<int, int>> impacts;
    CoreLease core(patient->GetCore(DEBUG_CALL_INFO));
    int dying = 0, target_cleared = 0, exploded = 0, removed = 0;
    MG_CHECK(building_ruins().empty());
    for (int i = 0; i < (lethal ? 450 : 200); ++i) {
        trace.record(session, shooter, shooter, nullptr, core.get(), core->m_Object ? patient : nullptr, lethal);
        session.advance();
        if (!core->m_Object) {
            if (!removed) removed = session.time();
            MG_CHECK(lethal && dying);
            MG_CHECK(building_ruins().size() == (kind == BUILDING_BASE ? 1u : 2u));
            MG_CHECK(session.robot(1).target_is_null);
            MG_CHECK(side->GetStatValue(STAT_BUILDING_KILL) == 1);
            continue;
        }
        MG_CHECK(building_ruins().empty());
        if (!target_cleared && session.robot(1).target_is_null) target_cleared = session.time();
        if (patient->m_State == BUILDING_DIP || patient->m_State == BUILDING_DIP_EXPLODED) {
            if (!dying) dying = session.time();
            if (!exploded && patient->m_State == BUILDING_DIP_EXPLODED) exploded = session.time();
        } else if (patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(health * 10)));
        }
    }
    std::cout << "building " << int(kind) << " plasma=" << plasma << " lethal=" << lethal << " dying=" << dying << " impacts:";
    for (const auto &[time, hp] : impacts) std::cout << ' ' << time << ':' << hp;
    std::cout << '\n';
    if (lethal) {
        MG_CHECK(dying == (plasma ? 160 : 20));
        std::cout << "exploded=" << exploded << " removed=" << removed << '\n';
        MG_CHECK(core->m_Object == nullptr);
        MG_CHECK(removed == dying + (plasma ? 10 : 0) + (kind == BUILDING_BASE ? 3000 : 1000));
        MG_CHECK(exploded == (kind == BUILDING_BASE ? dying + (plasma ? 10 : 0) + 2900 : 0));
        for (auto *ruin : building_ruins()) {
            MG_CHECK(!ruin->InLT() && ruin->m_Graph && ruin->m_Graph->GetSkin());
            MG_CHECK(ruin->GetMatrix()._41 == 200 && ruin->GetMatrix()._42 == 200);
        }
        for (int x = 0; x < g_MatrixMap->m_Size.x; ++x)
            for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
                MG_CHECK(g_MatrixMap->UnitGet(x, y)->m_Base != patient);
        MG_CHECK(target_cleared == (plasma ? 210 : 110));
        MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL) == 1);
    } else {
        const std::vector<int> expected_times = plasma
            ? std::vector<int>{160, 260, 350, 450, 550, 650, 750, 850, 960, 1060, 1160, 1260, 1370, 1470, 1570, 1670, 1770, 1880, 1980}
            : std::vector<int>{20, 120, 220, 320, 420, 520, 620, 720, 820, 920, 1020, 1120, 1220, 1320, 1420, 1520, 1620, 1720, 1820, 1920};
        std::vector<std::pair<int, int>> expected;
        for (size_t i = 0; i < expected_times.size(); ++i)
            expected.emplace_back(expected_times[i], 50 - 2 * int(i + 1));
        MG_CHECK(impacts == expected);
        MG_CHECK(health == float(expected.back().second) / 10.0f);
        MG_CHECK(patient->IsLiveBuilding() && !target_cleared);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL) == 0);
    }
    trace.record(session, shooter, shooter, nullptr, core.get(), core->m_Object ? patient : nullptr, lethal);
    trace.complete(session);
    return trace.states;
}
std::vector<int> building_family_impacts(EWeapon weapon) {
    switch (weapon) {
    case WEAPON_VOLCANO:
    case WEAPON_LIGHTENING: return {20, 520, 1020, 1520, 2020, 2520, 3020, 3520};
    case WEAPON_FLAMETHROWER: return {200, 230, 260, 290, 320, 700, 730, 760, 1230, 1260, 1290,
        1760, 1790, 1820, 2260, 2290, 2320, 2350, 2820, 2850, 2880, 3410, 3440, 3470};
    case WEAPON_GUN: return {50, 550, 1050, 1550, 2050, 2560, 3060, 3580};
    case WEAPON_HOMING_MISSILE: return {290, 780, 1290, 1810, 2320, 2840, 3390, 3930};
    case WEAPON_BOMB: return {1320, 2820, 3330, 3850};
    default: throw std::runtime_error("Missing building weapon-family oracle");
    }
}
std::vector<std::string> building_family_run(const Family &family, random::Mode mode, EBuildingType kind, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    MG_CHECK(session.remove(3));
    position_fixture_robot(*robot(2), 620, 620);
    configure_weapon(family.effect, family.heat);
    g_Config.m_WeaponCooldown[Weap2Index(family.effect)] = 500;
    g_Config.m_BuildingDamages[Weap2Index(family.effect)] = {2, 0, 0};
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_ABLAZE)] = {};
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_SHORTED)] = {};
    for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
        g_Config.m_Timings[resource] = 100000;
    auto *shooter = robot(PLAYER_SIDE);
    tests::arm_battle_robot(*shooter, family.unit);
    auto *mesh = tests::load_battle_mesh({}, nullptr, false, 40.0f);
    auto owned = tests::make_battle_building(*mesh, 2, kind, 200, 200, 100);
    auto *patient = owned.get();
    g_MatrixMap->AddObject(patient, true);
    owned.release();
    Trace trace(session, profile, (std::string("building-family-") + family.name + "-" + std::to_string(int(kind))).c_str(),
        run, "synthetic-armed-building-families-v1");
    auto *side = g_MatrixMap->GetPlayerSide();
    trace.command(session, "order-attack-building", [&] {
        side->PGOrderAttack(side->SelGroupToLogicGroup(), CPoint(20, 20), patient);
        return session.robot(1).order == mpo_Attack;
    });
    float health = patient->GetHitPoint();
    std::vector<std::pair<int, int>> impacts;
    for (int i = 0; i < 400; ++i) {
        trace.record(session, shooter, shooter, nullptr, nullptr, patient);
        session.advance();
        if (patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(health * 10)));
        }
        MG_CHECK(patient->IsLiveBuilding() && patient->GetSide() == 2);
        MG_CHECK(side->GetStatValue(STAT_BUILDING_KILL) == 0);
    }
    std::cout << "building family=" << family.name << " kind=" << int(kind) << " mode=" << int(mode) << " impacts:";
    for (const auto &[time, hp] : impacts) std::cout << ' ' << time << ':' << hp;
    std::cout << '\n';
    const auto times = building_family_impacts(family.effect);
    std::vector<std::pair<int, int>> expected;
    for (size_t i = 0; i < times.size(); ++i) expected.emplace_back(times[i], 100 - 2 * int(i + 1));
    MG_CHECK(impacts == expected);
    const float expected_health = float(expected.back().second) / 10.0f;
    MG_CHECK(std::bit_cast<uint32_t>(health) == std::bit_cast<uint32_t>(expected_health));
    MG_CHECK(!session.robot(1).target_is_null && session.robot(1).order == mpo_Attack);
    trace.record(session, shooter, shooter, nullptr, nullptr, patient);
    trace.complete(session);
    return trace.states;
}
void building_family_replay(const Family &family) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (auto kind : {BUILDING_BASE, BUILDING_TITAN}) {
        const auto first = building_family_run(family, mode, kind, 0);
        compare(first, building_family_run(family, mode, kind, 1));
    }
}
void building_volcano() { building_family_replay(volcano); }
void building_electric() { building_family_replay(electric); }
void building_flame() { building_family_replay(flame); }
void building_gun() { building_family_replay(gun); }
void building_missile() { building_family_replay(missile); }
void building_mortar() { building_family_replay(mortar); }
void building_patient() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (bool plasma : {false, true})
            for (auto kind : {BUILDING_BASE, BUILDING_TITAN, BUILDING_PLASMA, BUILDING_ELECTRONIC, BUILDING_ENERGY, BUILDING_REPAIR}) {
                const auto reference = building_patient_run(mode, plasma, kind, false, 0);
                compare(reference, building_patient_run(mode, plasma, kind, false, 1));
            }
}
void building_dying() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (bool plasma : {false, true})
            for (auto kind : {BUILDING_BASE, BUILDING_TITAN, BUILDING_PLASMA, BUILDING_ELECTRONIC, BUILDING_ENERGY, BUILDING_REPAIR}) {
                const auto reference = building_patient_run(mode, plasma, kind, true, 0);
                compare(reference, building_patient_run(mode, plasma, kind, true, 1));
            }
}

std::vector<std::string> cannon_acquisition_run(random::Mode mode, int variant, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, false};
    Session session(profile);
    assets.install();
    MG_CHECK(session.remove(2));
    auto *patient = robot(PLAYER_SIDE);
    patient->InitMaxHitpoint(100);
    patient->SetMaxSpeed(0);
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.m_Hitpoint = 1000;
    props.weapon = WEAPON_CANNON2;
    props.seek_radius = 300;
    props.max_da = variant == 3 ? .05f : variant == 4 ? 0 : .1f;
    props.max_top_angle = variant == 5 ? -1.2f : 1.5f;
    props.max_bottom_angle = -1.5f;
    g_Config.m_CannonsLogic = true;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = 51;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_CANNON2)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_CANNON2)] = {2, 0, 10};
    auto *cannon = tests::create_battle_cannon(2, 70, 200);
    const auto acquisition_delta = patient->GetGeoCenter() - cannon->GetGeoCenter();
    if (variant < 3)
        props.seek_radius = D3DXVec3Length(&acquisition_delta) - patient->GetRadius() + float(variant - 1);
    Trace trace(session, profile, ("cannon-acquisition-" + std::to_string(variant)).c_str(), run);
    std::vector<int> impacts;
    float health = patient->GetHitPoint();
    int acquired_at = 0, shots = 0;
    for (int i = 0; i < 200; ++i) {
        const float yaw = cannon->m_Unit[1].u1.s1.m_Angle;
        const float pitch = cannon->m_AngleX;
        trace.record(session, patient, nullptr, cannon);
        session.advance();
        if (cannon->m_TargetCore && !acquired_at)
            acquired_at = session.time();
        if (props.max_da > 0) {
            MG_CHECK(std::abs(cannon->m_Unit[1].u1.s1.m_Angle - yaw) <= props.max_da + .001f);
            MG_CHECK(std::abs(cannon->m_AngleX - pitch) <= props.max_da + .001f);
        }
        if (i > 40)
            MG_CHECK(cannon->m_AngleX <= props.max_top_angle + .000001f);
        shots += cannon->m_Weapons[0]->GetFireCount();
        if (patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            impacts.push_back(session.time());
        }
    }
    MG_CHECK(acquired_at == (variant < 2 ? 0 : 110));
    MG_CHECK(shots == (variant < 2 ? 0 : variant == 3 ? 16 : variant == 4 ? 12 : 17));
    std::vector<int> expected;
    if (variant >= 2 && variant != 5) {
        const int first = variant == 3 ? 440 : variant == 4 ? 880 : 330;
        for (int t = first; t <= 2000; t += 100)
            expected.push_back(t);
    }
    MG_CHECK(impacts == expected);
    check_health(patient, 100 - int(expected.size()) * 2);
    trace.record(session, patient, nullptr, cannon);
    trace.complete(session);
    return trace.states;
}
void cannon_acquisition() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int variant = 0; variant < 6; ++variant) {
            const auto reference = cannon_acquisition_run(mode, variant, 0);
            compare(reference, cannon_acquisition_run(mode, variant, 1));
        }
}
std::vector<std::string> robot_boundary_run(random::Mode mode, int variant, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, true};
    Session session(profile);
    assets.install();
    configure_weapon(WEAPON_LASER, WEAPON_LASER_HEAT);
    constexpr float range_bracket[] = {300, 301.25f, 302};
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = variant < 3 ? range_bracket[variant] : 600;
    if (variant >= 3)
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0;
    auto *shooter = robot(PLAYER_SIDE);
    auto *patient = robot(2);
    tests::arm_battle_robot(*shooter, RUK_WEAPON_LASER);
    shooter->SetMaxSpeed(0);
    shooter->m_GroupSpeed = 0;
    position_fixture_robot(*patient, 400, 224);
    patient->InitMaxHitpoint(100);
    if (variant >= 3) {
        const float angle = std::atan2(24.f, 300.f) + (variant == 3 ? 13.f : 17.f) * float(M_PI / 180);
        shooter->m_HullForward = D3DXVECTOR3(std::cos(angle), std::sin(angle), 0);
        shooter->RChange(MR_Matrix);
        shooter->RNeed(MR_Matrix);
    }
    Trace trace(session, profile, ("robot-boundary-" + std::to_string(variant)).c_str(), run);
    trace.command(session, "context-target(3)", [&] { return session.target_command(3); });
    std::vector<int> impacts;
    float health = patient->GetHitPoint();
    for (int i = 0; i < 100; ++i) {
        trace.record(session, patient, shooter);
        session.advance();
        auto delta = patient->GetGeoCenter() - shooter->GetGeoCenter();
        MG_CHECK(std::abs(D3DXVec3LengthSq(&delta) - 90676) < .1f);
        MG_CHECK(shooter->m_PosX == 100 && shooter->m_PosY == 200);
        MG_CHECK(patient->m_PosX == 400 && patient->m_PosY == 224);
        if (patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            impacts.push_back(session.time());
        }
    }
    std::vector<int> expected;
    if (variant == 1 || variant == 2 || variant == 3)
        for (int t = variant == 3 ? 120 : 200; t <= 1000; t += 100)
            expected.push_back(t);
    MG_CHECK(impacts == expected);
    check_health(patient, 100 - int(expected.size()) * 2);
    trace.record(session, patient, shooter);
    trace.complete(session);
    return trace.states;
}
void robot_boundaries() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int variant = 0; variant < 5; ++variant) {
            const auto reference = robot_boundary_run(mode, variant, 0);
            compare(reference, robot_boundary_run(mode, variant, 1));
        }
}

std::vector<std::string> status_refresh_run(random::Mode mode, bool electrical, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, false};
    Session session(profile);
    assets.install();
    g_Config.m_RobotDamages[Weap2Index(WEAPON_FLAMETHROWER)] = {0, 0, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LIGHTENING)] = {0, 0, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_ABLAZE)] = {1, 1, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_SHORTED)] = {2, 2, 0};
    auto *patient = robot(PLAYER_SIDE);
    patient->InitMaxHitpoint(100);
    patient->SetMaxSpeed(0);
    const int duration = electrical ? 500 : 300;
    const auto weapon = electrical ? WEAPON_LIGHTENING : WEAPON_FLAMETHROWER;
    Trace trace(session, profile, electrical ? "status-refresh-electric" : "status-refresh-flame", run);
    auto apply = [&] { return !patient->Damage(weapon, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr); };
    trace.command(session, "apply-status", apply);
    std::vector<std::pair<int, int>> impacts;
    float health = patient->GetHitPoint();
    for (int i = 0; i < 120; ++i) {
        if (session.time() == duration - 10)
            trace.command(session, "refresh-status", apply);
        trace.record(session, patient);
        session.advance();
        const int expires = session.time() < duration ? duration : duration * 2;
        const int ttl = electrical ? StatusObservation::shorted(*patient) : StatusObservation::ablaze(*patient);
        MG_CHECK(ttl == std::max(0, expires - session.time()));
        if (patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            impacts.emplace_back(session.time(), int(std::lround(health * 10)));
        }
    }
    std::vector<std::pair<int, int>> expected;
    if (electrical) {
        int remaining = 100;
        for (int t = 10; t < 500; t += 50)
            expected.emplace_back(t, remaining -= 2);
        for (int t = 500; t < 1000; t += 50)
            expected.emplace_back(t, remaining -= 2);
    }
    else
        expected = {{10, 91}, {100, 82}, {190, 73}, {280, 64}, {300, 55}, {390, 46}, {480, 37}, {570, 28}};
    MG_CHECK(impacts == expected);
    check_health(patient, electrical ? 60 : 28);
    trace.record(session, patient);
    trace.complete(session);
    return trace.states;
}
void status_refresh() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (bool electrical : {false, true}) {
            const auto reference = status_refresh_run(mode, electrical, 0);
            compare(reference, status_refresh_run(mode, electrical, 1));
        }
}
std::vector<std::string> status_lethal_run(random::Mode mode, bool electrical, bool later, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, false};
    Session session(profile);
    assets.install();
    g_Config.m_RobotDamages[Weap2Index(WEAPON_FLAMETHROWER)] = {0, 0, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LIGHTENING)] = {0, 0, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_ABLAZE)] = {1, 1, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_SHORTED)] = {2, 2, 0};
    auto *patient = robot(PLAYER_SIDE);
    auto *observer = robot(PLAYER_SIDE, 1);
    patient->InitMaxHitpoint((electrical ? 2 : 1) * (later ? 2 : 1));
    patient->SetMaxSpeed(0);
    CoreLease core(patient->GetCore(DEBUG_CALL_INFO));
    Trace trace(session, profile, ("status-lethal-" + std::to_string(electrical) + "-" + std::to_string(later)).c_str(),
                run);
    trace.command(session, "apply-status", [&] {
        return !patient->Damage(electrical ? WEAPON_LIGHTENING : WEAPON_FLAMETHROWER, patient->GetGeoCenter(),
                                D3DXVECTOR3(1, 0, 0), 2, nullptr);
    });
    int dying = 0, removed = 0;
    for (int i = 0; i < 650; ++i) {
        trace.record(session, core->m_Object ? patient : observer, nullptr, nullptr, core.get());
        session.advance();
        if (core->m_Object && patient->m_CurrState == ROBOT_DIP && !dying)
            dying = session.time();
        if (!core->m_Object && !removed)
            removed = session.time();
    }
    MG_CHECK(dying == (electrical && later ? 60 : 10));
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == (later ? 0 : 1));
    MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL) == 0);
    const int expected_removed = electrical ? (later ? (mode == random::Mode::ParkMiller ? 3790 : 3200)
                                                     : (mode == random::Mode::ParkMiller ? 4300 : 4410))
                                            : (mode == random::Mode::ParkMiller ? 4040 : 2460);
    MG_CHECK(removed == expected_removed);
    trace.record(session, observer, nullptr, nullptr, core.get());
    trace.complete(session);
    return trace.states;
}
void status_lethal() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (bool electrical : {false, true})
            for (bool later : {false, true}) {
                const auto reference = status_lethal_run(mode, electrical, later, 0);
                compare(reference, status_lethal_run(mode, electrical, later, 1));
            }
}

void friendly_difficulty() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (float factor : {0.f, .5f, 1.f, 2.f}) {
            const auto reference = splash_boundary_run(mode, 1, 0, factor);
            compare(reference, splash_boundary_run(mode, 1, 1, factor));
        }
}
std::vector<std::string> cannon_difficulty_run(random::Mode mode, float factor, int run) {
    tests::BattleCombatAssets assets;
    const Profile profile{mode, 82, 10, 1000, false, false};
    Session session(profile);
    assets.install();
    MG_CHECK(session.remove(2));
    g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = factor;
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.m_Hitpoint = 1000;
    props.weapon = WEAPON_CANNON2;
    props.seek_radius = 300;
    props.max_da = .1f;
    props.max_top_angle = 1.5f;
    props.max_bottom_angle = -1.5f;
    g_Config.m_CannonsLogic = true;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = 51;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_CANNON2)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_CANNON2)] = {2, 0, 0};
    auto *patient = robot(PLAYER_SIDE);
    patient->InitMaxHitpoint(50);
    patient->SetMaxSpeed(0);
    auto *cannon = tests::create_battle_cannon(2, 70, 200);
    Trace trace(session, profile, ("cannon-difficulty-" + std::to_string(factor)).c_str(), run);
    int shots = 0;
    float health = patient->GetHitPoint();
    std::vector<int> impacts;
    for (int i = 0; i < 100; ++i) {
        trace.record(session, patient, nullptr, cannon);
        session.advance();
        shots += cannon->m_Weapons[0]->GetFireCount();
        if (patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            impacts.push_back(session.time());
        }
    }
    MG_CHECK(shots == 7);
    MG_CHECK(impacts == (factor > 0 ? std::vector<int>{330, 430, 530, 630, 730, 830, 930} : std::vector<int>{}));
    check_health(patient, 50 - int(14 * factor));
    trace.record(session, patient, nullptr, cannon);
    trace.complete(session);
    return trace.states;
}
void cannon_difficulty() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (float factor : {0.f, .5f, 1.f, 2.f}) {
            const auto reference = cannon_difficulty_run(mode, factor, 0);
            compare(reference, cannon_difficulty_run(mode, factor, 1));
        }
}

const tests::Case cases[] = {
        {"game.combat.cannon_firing", cannon_firing},         {"game.combat.robot_firing", robot_firing},
        {"game.combat.plasma_flight", plasma_flight},         {"probe.combat.axis_aligned_trace", axis_aligned_trace},
        {"game.combat.volcano_firing", volcano_firing},       {"game.combat.electric_firing", electric_firing},
        {"game.combat.flame_firing", flame_firing},           {"game.combat.gun_firing", gun_firing},
        {"game.combat.missile_firing", missile_firing},       {"game.combat.mortar_firing", mortar_firing},
        {"game.combat.repair_firing", repair_firing},         {"game.combat.heat_boundary", heat_boundary},
        {"game.combat.bomb_detonation", bomb_detonation},     {"game.combat.cannon_projectiles", cannon_projectiles},
        {"game.combat.cannon_boundaries", cannon_boundaries}, {"game.combat.cannon_obstruction", cannon_obstruction},
        {"game.combat.delayed_damage", delayed_damage},       {"game.combat.bomb_order", bomb_order},
        {"game.combat.repair_death", repair_death},           {"game.combat.module_modifiers", module_modifiers},
        {"game.combat.module_firing", module_firing},         {"game.combat.splash_boundary", splash_boundary},
        {"game.combat.chained_bombs", chained_bombs},         {"game.combat.repair_redirect", repair_redirect},
        {"game.combat.cannon_patient", cannon_patient},       {"game.combat.cannon_acquisition", cannon_acquisition},
        {"game.combat.robot_boundaries", robot_boundaries},   {"game.combat.status_refresh", status_refresh},
        {"game.combat.status_lethal", status_lethal},         {"game.combat.friendly_difficulty", friendly_difficulty},
        {"game.combat.cannon_difficulty", cannon_difficulty},
        {"game.combat.building_volcano", building_volcano}, {"game.combat.building_electric", building_electric},
        {"game.combat.building_flame", building_flame}, {"game.combat.building_gun", building_gun},
        {"game.combat.building_missile", building_missile}, {"game.combat.building_mortar", building_mortar},
        {"game.combat.building_patient", building_patient}, {"game.combat.building_dying", building_dying}};
}  // namespace
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
