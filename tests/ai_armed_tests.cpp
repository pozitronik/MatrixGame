// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "MatrixObjectCannon.hpp"
#include "Logic/MatrixEnvironment.h"
#include "zero_x_mode.hpp"

namespace tests::ai {
namespace {
CMatrixRobotAI *owner() {
    for (auto *obj = CMatrixMapStatic::GetFirstLogic(); obj; obj = obj->GetNextLogic())
        if (obj->IsLiveRobot() && obj->GetSide() == PLAYER_SIDE) return obj->AsRobot();
    throw std::runtime_error("Missing player robot");
}
void configure() {
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 100;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_PLASMA)] = 300;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_PLASMA)] = 100;
}
void evidence(battle::Session &session, const char *trigger, CMatrixRobotAI *robot, const char *fixture = nullptr) {
    if (!fixture) fixture = robot->HaveRepair() == 2 ? "synthetic-repair-boxes-v1" : "synthetic-armed-boxes-v2";
    std::cout << session.metadata(fixture) << '\n';
    std::cout << "{\"decision\":\"armed-tactical-boundary\",\"trigger\":\"" << trigger
              << "\",\"enemy_count\":" << robot->GetEnv()->GetEnemyCnt()
              << ",\"move_order\":" << (robot->FindOrderLikeThat(ROT_MOVE_TO) ? "true" : "false")
              << ",\"fire_order\":" << (robot->FindOrderLikeThat(ROT_FIRE) ? "true" : "false")
              << ",\"target_present\":" << (robot->GetEnv()->m_Target ? "true" : "false")
              << ",\"attack_target_present\":" << (robot->GetEnv()->m_TargetAttack ? "true" : "false")
              << ",\"rng_draws\":" << random::draw_count() << "}\n";
}
}

void visual_acquisition_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int distance : {109, 110, 111}) {
            BattleCombatAssets assets;
            battle::Session session({mode, 79});
            assets.install(); configure();
            auto *observer = owner();
            arm_battle_robot(*observer, RUK_WEAPON_LASER);
            observer->m_HullForward = D3DXVECTOR3(0, 1, 0);
            auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2,
                observer->m_PosX + distance * 0.6f, observer->m_PosY - distance * 0.8f);
            MG_CHECK(observer->GetMaxFireDist() == 100);
            MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
            observer->GatherInfo(0);
            MG_CHECK(bool(observer->GetEnv()->SearchEnemy(target)) == (distance <= 110));
            observer->GatherInfo(0);
            MG_CHECK(observer->GetEnv()->GetEnemyCnt() == (distance <= 110 ? 1 : 0));
            evidence(session, "visual-range-boundary", observer);
        }
}

void visual_enemy_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 230, 100);
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        // Assembly is an explicit fixture intervention, not a simulated upgrade.
        arm_battle_robot(*target, RUK_WEAPON_PLASMA);
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        MG_CHECK(observer->GetEnv()->GetEnemyCnt() == 1);
        evidence(session, "enemy-weapon-range-increased", observer);
    }
}

void visual_filters() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int variant : {0, 1, 2}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, variant == 0 ? PLAYER_SIDE : 2, 150, 150);
        if (variant == 1) target->m_CurrState = ROBOT_DIP;
        if (variant == 2) observer->GetEnv()->AddIgnore(target);
        observer->GatherInfo(0);
        // Restore setup-only state before assertions and ordinary teardown.
        if (variant == 1) target->m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        MG_CHECK(observer->GetEnv()->GetEnemyCnt() == 0);
        evidence(session, variant == 0 ? "friendly" : variant == 1 ? "dying" : "ignored", observer);
    }
}

void visual_obstruction() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 160, 120);
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 130, 160);
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        MG_CHECK(!observer->GetEnv()->IsIgnore(target));
        g_MatrixMap->StaticDelete(blocker);
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        evidence(session, "visual-blocker-retired", observer);
    }
}

// An enemy straight ahead along the Y axis: the sight line has no X travel. The original trace rejected every
// object hit along such a line, so the observer never saw the enemy.
void visual_axis_aligned() {
    for (bool original : {false, true}) for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::ZeroXMode zero_x(original);
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        observer->m_HullForward = D3DXVECTOR3(0, -1, 0);
        // Another player robot stands further along +Y on x = 100, so the enemy is placed on the -Y side.
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, observer->m_PosX, observer->m_PosY - 60);
        const D3DXVECTOR3 from = observer->GetGeoCenter(), to = target->GetGeoCenter();
        MG_CHECK(from.x == to.x && from.y != to.y);
        MG_CHECK(g_MatrixMap->IsLogicVisible(observer, target, 0.0f) == !original);
        observer->GatherInfo(0);
        MG_CHECK(bool(observer->GetEnv()->SearchEnemy(target)) == !original);
        evidence(session, original ? "visual-axis-original" : "visual-axis-corrected", observer);
    }
}

void visual_retirement_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int distance : {139, 140, 141}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2,
            observer->m_PosX + distance * 0.6f, observer->m_PosY - distance * 0.8f);
        auto &env = *observer->GetEnv();
        env.AddToList(target); // Initial historical sighting, outside acquisition range.
        env.m_Target = target; env.m_TargetAttack = target;
        for (int observation = 1; observation <= 3; ++observation) {
            observer->GatherInfo(0);
            auto *entry = env.SearchEnemy(target);
            if (distance > 140 && observation == 3) {
                MG_CHECK(!entry && !env.m_Target && !env.m_TargetAttack);
            } else {
                MG_CHECK(entry && entry->m_DelSlowly == (distance > 140 ? observation : 0));
                MG_CHECK(env.m_Target == target && env.m_TargetAttack == target);
            }
        }
        evidence(session, "visual-retirement-boundary", observer);
    }
}

void armed_friendly_line() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_PLASMA);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 200, 100);
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 150, 150);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &env = *observer->GetEnv();
        env.AddToList(target);
        const int group = observer->GetGroupLogic();
        const auto intent = session.robot(1);
        MG_CHECK(!side.FirePL(group));
        MG_CHECK(env.m_TargetAttack == target && !observer->FindOrderLikeThat(ROT_FIRE));
        g_MatrixMap->StaticDelete(blocker);
        MG_CHECK(side.FirePL(group));
        MG_CHECK(env.m_TargetAttack == target && env.m_Target == target);
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        const auto after = session.robot(1);
        MG_CHECK(after.order == intent.order && after.destination_x == intent.destination_x &&
                 after.destination_y == intent.destination_y);
        evidence(session, "friendly-blocker-retired", observer);
    }
}

void ineffective_fire_run(bool blocked, bool moving = false, int place_not_found = 0) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool multiple : {false, true})
    for (int target_age : {2000, 2001, 4000, 4001})
    for (int hit_age : {4000, 4001, 6000, 6001, 10000, 10001}) {
        if (place_not_found != 0 && (target_age != 4001 || hit_age != 6001)) continue;
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84};
        profile.initial_time_ms = 12000;
        battle::Session session(profile);
        assets.install(); configure();
        MG_CHECK(session.remove(2));
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_PLASMA);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 200, 100);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &env = *observer->GetEnv();
        // Historical enemy memory and an occupied firing place are initial data.
        // No projectile or hit result is synthesized during the decision call.
        observer->SetGroupLogic(0);
        side.m_PlayerGroup[0].Order(mpo_Stop);
        g_MatrixMap->m_RN.m_Place[0].m_Pos = CPoint(observer->GetMapPosX(), observer->GetMapPosY());
        env.m_Place = 0;
        env.m_PlaceNotFound = place_not_found;
        env.AddToList(target);
        if (multiple) env.AddToList(BattleGeometry::create_robot(*g_MatrixMap, 2, 240, 180));
        env.m_TargetAttack = env.m_TargetLast = target;
        env.m_TargetChange = 12000 - target_age;
        env.m_LastHitTarget = blocked ? 12000 : 12000 - hit_age;
        env.m_LastFire = blocked ? 12000 - hit_age : 12000;
        if (blocked) BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 150, 150);
        MG_CHECK(!observer->FindOrderLikeThat(ROT_MOVE_TO));
        if (moving) observer->MoveTo(observer->GetMapPosX(), observer->GetMapPosY());
        side.WarPL(0);
        const bool stale = !moving && target_age > 4000 && hit_age > 4000;
        const bool abandon_place = !moving && (blocked ? target_age > 4000 && hit_age > 6000 && 12000 - place_not_found > 2000
            : (!multiple && stale) || (target_age > 2000 && hit_age > 10000));
        MG_CHECK(env.m_TargetAttack == (multiple && stale ? nullptr : target));
        MG_CHECK(env.m_Place == (abandon_place ? -1 : 0));
        MG_CHECK(env.IsBadPlace(0) == abandon_place);
        const bool duplicate_abandon = !moving && !blocked && !multiple && stale && hit_age > 10000;
        MG_CHECK(env.m_BadPlaceCnt == (duplicate_abandon ? 2 : abandon_place ? 1 : 0));
        if (abandon_place) MG_CHECK(env.m_BadPlace[0] == 0);
        // Observed quirk: the second rule remembers the already-invalid place.
        if (duplicate_abandon) MG_CHECK(env.m_BadPlace[1] == -1);
        MG_CHECK(env.m_Target == (blocked ? nullptr : target));
        MG_CHECK(env.m_LastFire == (blocked ? 12000 - hit_age : 12000));
        MG_CHECK(env.m_LastHitTarget == (blocked ? 12000 : 12000 - hit_age));
        MG_CHECK(env.m_TargetChange == (moving ? 12000 : 12000 - target_age));
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE) == !blocked);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO) == moving);
        MG_CHECK(side.m_PlayerGroup[0].Order() == mpo_Stop && session.time() == 12000);
        evidence(session, blocked ? "blocked-fire-decision" : "ineffective-fire-decision", observer);
        std::cout << "targets=" << (multiple ? 2 : 1) << " target_age=" << target_age
                  << " place_not_found=" << place_not_found << " pending_move=" << moving << " feedback_age=" << hit_age << " place=" << env.m_Place << '\n';
    }
}

void ineffective_fire_decisions() { ineffective_fire_run(false); }
void blocked_fire_decisions() {
    ineffective_fire_run(true);
    ineffective_fire_run(true, false, 10000); // Exactly 2000 ms since failed place search.
    ineffective_fire_run(true, false, 9999);  // The strict cooldown has expired.
}

void moving_fire_decisions() { ineffective_fire_run(false, true); ineffective_fire_run(true, true); }

void armed_target_retention() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_PLASMA);
        auto *first = BattleGeometry::create_robot(*g_MatrixMap, 2, 220, 100);
        auto *nearer = BattleGeometry::create_robot(*g_MatrixMap, 2, 160, 250);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &env = *observer->GetEnv();
        const int group = observer->GetGroupLogic();
        env.AddToList(first);
        MG_CHECK(side.FirePL(group) && env.m_TargetAttack == first);
        env.AddToList(nearer);
        MG_CHECK(side.FirePL(group) && env.m_TargetAttack == first);
        MG_CHECK(env.m_Target == first && observer->FindOrderLikeThat(ROT_FIRE));
        g_MatrixMap->StaticDelete(first);
        MG_CHECK(!env.m_Target && !env.m_TargetAttack);
        MG_CHECK(side.FirePL(group) && env.m_TargetAttack == nearer);
        MG_CHECK(env.m_Target == nearer && observer->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        evidence(session, "retained-target-deleted", observer);
    }
}
namespace {
CMatrixRobotAI *damaged_friendly(float x, float y) {
    auto *robot = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, x, y);
    robot->InitMaxHitpoint(50);
    g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = 1;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {1, 0, 0};
    MG_CHECK(!robot->Damage(WEAPON_LASER, robot->GetGeoCenter(), D3DXVECTOR3(0, 0, 0), 0, nullptr));
    MG_CHECK(robot->NeedRepair());
    return robot;
}
void require_repair_order(CMatrixRobotAI &robot) {
    int count = 0;
    for (int i = 0; i < robot.GetOrdersInPool(); ++i) {
        const auto &order = *robot.GetOrder(i);
        if (order.m_OrderType != ROT_FIRE) continue;
        MG_CHECK(order.m_Param4 == 2);
        ++count;
    }
    MG_CHECK(count == 1);
}
void configure_repair(CMatrixRobotAI &robot) {
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_REPAIR)] = 100;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_REPAIR)] = 100;
    arm_battle_robot(robot, RUK_WEAPON_REPAIR);
    MG_CHECK(robot.HaveRepair() == 2 && robot.GetRepairDist() == 100);
}
}

void repair_acquisition_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int distance : {99, 100, 101}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install();
        auto *observer = owner();
        configure_repair(*observer);
        auto *patient = damaged_friendly(observer->m_PosX + distance * 0.6f,
                                         observer->m_PosY - distance * 0.8f);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int group = observer->GetGroupLogic();
        const CPoint destination = side.m_PlayerGroup[group].m_To;
        const auto draws = random::draw_count();
        side.RepairPL(group);
        MG_CHECK(observer->GetEnv()->m_Target == (distance < 100 ? patient : nullptr));
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE) == (distance < 100));
        if (distance < 100) require_repair_order(*observer);
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
        MG_CHECK(side.m_PlayerGroup[group].m_To == destination);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        MG_CHECK(random::draw_count() == draws);
        evidence(session, "repair-range-boundary", observer);
    }
}

void repair_target_retention() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install();
        auto *observer = owner();
        configure_repair(*observer);
        auto *first = damaged_friendly(160, 140);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int group = observer->GetGroupLogic();
        const CPoint destination = side.m_PlayerGroup[group].m_To;
        auto &env = *observer->GetEnv();
        side.RepairPL(group);
        MG_CHECK(env.m_Target == first && observer->FindOrderLikeThat(ROT_FIRE));
        require_repair_order(*observer);
        evidence(session, "repair-first-patient-selected", observer);
        auto *nearer = damaged_friendly(130, 160);
        side.RepairPL(group);
        MG_CHECK(env.m_Target == first);
        evidence(session, "repair-nearer-patient-does-not-interrupt", observer);
        // A production health update makes the retained patient ineligible.
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {1, 0, 1};
        MG_CHECK(!first->Damage(WEAPON_REPAIR, first->GetGeoCenter(), D3DXVECTOR3(0, 0, 0), 0, nullptr));
        MG_CHECK(!first->NeedRepair());
        side.RepairPL(group);
        MG_CHECK(env.m_Target == nearer && observer->FindOrderLikeThat(ROT_FIRE));
        require_repair_order(*observer);
        evidence(session, "repair-healed-patient-replaced", observer);
        g_MatrixMap->StaticDelete(nearer);
        MG_CHECK(env.m_Target == nullptr);
        side.RepairPL(group);
        MG_CHECK(env.m_Target == nullptr);
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
        MG_CHECK(side.m_PlayerGroup[group].m_To == destination);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        evidence(session, "repair-patient-healed-and-retired", observer);
    }
}

void repair_explicit_priority() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install();
        auto *observer = owner();
        configure_repair(*observer);
        auto *first = damaged_friendly(160, 140);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int group = observer->GetGroupLogic();
        side.RepairPL(group);
        MG_CHECK(observer->GetEnv()->m_Target == first);
        auto *requested = damaged_friendly(130, 160);
        side.PGOrderRepair(group, requested);
        side.RepairPL(group);
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_Repair);
        MG_CHECK(side.m_PlayerGroup[group].m_Obj == requested);
        MG_CHECK(observer->GetEnv()->m_Target == requested);
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE));
        require_repair_order(*observer);
        evidence(session, "explicit-repair-overrides-retained-patient", observer);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {1, 0, 1};
        MG_CHECK(!requested->Damage(WEAPON_REPAIR, requested->GetGeoCenter(), D3DXVECTOR3(0, 0, 0), 0, nullptr));
        MG_CHECK(!requested->NeedRepair());
        side.TaktPL();
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_Stop);
        MG_CHECK(side.m_PlayerGroup[group].m_Obj == nullptr);
        evidence(session, "completed-explicit-repair-stops-group", observer);
    }
}

namespace {
CMatrixCannon *awareness_cannon(float distance, float range, int side = 2) {
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.m_Hitpoint = 1000;
    props.weapon = WEAPON_CANNON2;
    props.m_Strength = 10;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = range;
    // Low-profile synthetic assembly: the production sight endpoint (ground + 20)
    // must intersect the model's sphere, unlike the taller combat fixture.
    const MeshAttachment basis{20, L"Turret", 0, 0, 10};
    const MeshAttachment turret{20, L"Shaft", 0, 0, 10};
    const MeshAttachment muzzle{50, L"muzzle", 0, 15, 0};
    load_battle_mesh(std::span(&basis, 1), L"Matrix\\Cannon\\Basis.vo", true);
    load_battle_mesh(std::span(&turret, 1), L"Matrix\\Cannon\\Turret1.vo", true);
    load_battle_mesh(std::span(&muzzle, 1), L"Matrix\\Cannon\\Shaft1.vo", true);
    auto *target = create_battle_cannon(side, 100 + distance * 0.6f, 200 - distance * 0.8f);
    MG_CHECK(target->IsLiveCannon() && target->GetFireRadius() == range);
    return target;
}
}
void turret_acquisition_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool longer_turret : {false, true}) for (bool inside : {false, true}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        const float distance = longer_turret ? (inside ? 190 : 210) : (inside ? 95 : 105);
        auto *target = awareness_cannon(distance, longer_turret ? 200 : 50);
        MG_CHECK(g_MatrixMap->IsLogicVisible(observer, target, 0));
        observer->GatherInfo(0);
        MG_CHECK(bool(observer->GetEnv()->SearchEnemy(target)) == inside);
        MG_CHECK(!observer->GetEnv()->IsIgnore(target));
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->GetEnemyCnt() == (inside ? 1 : 0));
        evidence(session, longer_turret ? "turret-own-range" : "observer-range-for-turret", observer, "synthetic-low-turret-v1");
    }
}
void turret_retirement_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool longer_turret : {false, true})
    for (bool outside : {false, true}) for (bool aiming_at_observer : {false, true}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        const float distance = longer_turret ? (outside ? 299 : 297) : (outside ? 147 : 145);
        auto *target = awareness_cannon(distance, longer_turret ? 200 : 50);
        auto &env = *observer->GetEnv();
        env.AddToList(target); // A declared historical sighting, beyond acquisition range.
        if (aiming_at_observer) target->m_TargetCore = observer->GetCore(DEBUG_CALL_INFO);
        for (int scan = 1; scan <= 3; ++scan) {
            observer->GatherInfo(0);
            auto *entry = env.SearchEnemy(target);
            const bool retiring = outside && !aiming_at_observer;
            if (retiring && scan == 3) MG_CHECK(entry == nullptr);
            else MG_CHECK(entry && entry->m_DelSlowly == (retiring ? scan : 0));
        }
        evidence(session, aiming_at_observer ? "turret-aim-retains-memory" : "turret-range-retirement", observer, "synthetic-low-turret-v1");
    }
}
void turret_acquisition_filters() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int filter = 0; filter < 3; ++filter) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = awareness_cannon(95, 100, filter == 0 ? PLAYER_SIDE : 2);
        if (filter == 1) target->m_CurrState = CANNON_UNDER_CONSTRUCTION;
        if (filter == 2) observer->GetEnv()->AddIgnore(target);
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        if (filter == 0) target->SetSide(2);
        if (filter == 1) target->m_CurrState = CANNON_IDLE;
        if (filter == 2) observer->GetEnv()->DelIgnore(target);
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        evidence(session, "turret-filter-removed", observer, "synthetic-low-turret-v1");
    }
}
void turret_visual_obstruction() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = awareness_cannon(95, 100);
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 128.5f, 162);
        MG_CHECK(!g_MatrixMap->IsLogicVisible(observer, target, 0));
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target) && !observer->GetEnv()->IsIgnore(target));
        g_MatrixMap->StaticDelete(blocker);
        MG_CHECK(g_MatrixMap->IsLogicVisible(observer, target, 0));
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        evidence(session, "turret-occluder-retired", observer, "synthetic-low-turret-v1");
    }
}

namespace {
void repair_movement(bool retire) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84, 10, 2000, false, true};
        profile.layout = NavigationLayout::Open;
        battle::Session session(profile);
        assets.install();
        auto *observer = owner();
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
        configure_repair(*observer);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
        auto *patient = damaged_friendly(160, 140);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {17, 17, 0};
        MG_CHECK(!patient->Damage(WEAPON_LASER, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
        MG_CHECK(patient->GetHitPoint() == 3.2f);
        MG_CHECK(observer->GetEnv()->m_Target == nullptr);
        session.ground_command(500, 200);
        const auto intent = session.robot(1);
        MG_CHECK(intent.order == mpo_MoveTo);
        MG_CHECK(observer->GetEnv()->m_Target == patient);
        float health = patient->GetHitPoint();
        int repairs = 0;
        for (int i = 0; i < 1800; ++i) {
            const auto *target_before_tick = observer->GetEnv()->m_Target;
            session.advance();
            const auto state = session.robot(1);
            MG_CHECK(state.order == mpo_MoveTo);
            MG_CHECK(state.destination_x == intent.destination_x && state.destination_y == intent.destination_y);
            if (patient && patient->GetHitPoint() != health) {
                MG_CHECK(observer->m_PosX > 100);
                MG_CHECK(target_before_tick == patient);
                MG_CHECK(patient->GetHitPoint() == float(std::min(50, 32 + 4 * (repairs + 1))) / 10.0f);
                health = patient->GetHitPoint();
                ++repairs;
                if (retire && repairs == 2) {
                    g_MatrixMap->StaticDelete(patient);
                    patient = nullptr;
                    MG_CHECK(observer->GetEnv()->m_Target == nullptr);
                }
            }
        }
        MG_CHECK(repairs == (retire ? 2 : 5));
        if (patient) MG_CHECK(patient->GetHitPoint() == 5 && !patient->NeedRepair());
        MG_CHECK(observer->GetEnv()->m_Target == nullptr);
        MG_CHECK(!observer->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(observer->m_PosX == 500 && observer->m_PosY == 200);
        evidence(session, retire ? "moving-repair-patient-retired" : "moving-repair-completed", observer);
    }
}
}
void repair_movement_completion() { repair_movement(false); }
void repair_movement_retirement() { repair_movement(true); }
namespace {
void repair_competing_run(bool replace_order) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84, step, 3000, false};
        profile.layout = NavigationLayout::Open;
        battle::Session session(profile);
        assets.install();
        auto *observer = owner();
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
        configure_repair(*observer);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
        auto injure = [](float x, float y) {
            auto *patient = damaged_friendly(x, y);
            g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {17, 17, 0};
            MG_CHECK(!patient->Damage(WEAPON_LASER, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
            MG_CHECK(patient->GetHitPoint() == 3.2f);
            return patient;
        };
        auto *first = injure(160, 140);
        auto *second = injure(210, 220);
        const auto release = [](SObjectCore *core) { core->Release(); };
        std::unique_ptr<SObjectCore, decltype(release)> first_core(first->GetCore(DEBUG_CALL_INFO), release);
        session.ground_command(500, 200);
        MG_CHECK(observer->GetEnv()->m_Target == first);
        auto intent = session.robot(1);
        int first_pulses = 0, second_pulses = 0;
        float first_health = first->GetHitPoint(), second_health = second->GetHitPoint();
        bool changed = false;
        while (session.time() < 25000) {
            const auto *target_before = observer->GetEnv()->m_Target;
            session.advance();
            const auto state = session.robot(1);
            MG_CHECK(state.order == mpo_MoveTo);
            MG_CHECK(state.destination_x == intent.destination_x && state.destination_y == intent.destination_y);
            if (first && first->GetHitPoint() != first_health) {
                MG_CHECK(target_before == first);
                ++first_pulses;
                first_health = first->GetHitPoint();
                MG_CHECK(first_health == float(std::min(50, 32 + first_pulses * 4)) / 10.0f);
            }
            if (second->GetHitPoint() != second_health) {
                MG_CHECK(target_before == second);
                MG_CHECK(changed && (!replace_order || first_pulses == 5));
                ++second_pulses;
                second_health = second->GetHitPoint();
                MG_CHECK(second_health == float(std::min(50, 32 + second_pulses * 4)) / 10.0f);
            }
            if (!changed && first_pulses == 2) {
                MG_CHECK(observer->m_PosX > 100 && observer->GetEnv()->m_Target == first);
                MG_CHECK(second_pulses == 0);
                if (replace_order) {
                    session.ground_command(300, 500);
                    intent = session.robot(1);
                    MG_CHECK(intent.destination_x == 28 && intent.destination_y == 48);
                    MG_CHECK(observer->GetEnv()->m_Target == first);
                } else {
                    g_MatrixMap->StaticDelete(first);
                    first = nullptr;
                    MG_CHECK(!first_core->m_Object && !observer->GetEnv()->m_Target);
                }
                changed = true;
            }
            if (changed && !replace_order) MG_CHECK(!first_core->m_Object);
        }
        MG_CHECK(changed && first_pulses == (replace_order ? 5 : 2) && second_pulses == 5);
        MG_CHECK(second->GetHitPoint() == 5 && !second->NeedRepair());
        if (replace_order) MG_CHECK(first_core->m_Object == first && first->GetHitPoint() == 5 && !first->NeedRepair());
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(observer->m_PosX == (replace_order ? 300 : 500));
        MG_CHECK(observer->m_PosY == (replace_order ? 500 : 200));
        MG_CHECK(!observer->GetEnv()->m_Target && !observer->FindOrderLikeThat(ROT_FIRE));
        std::cout << session.metadata("competing-moving-repair-v1") << " replace=" << replace_order
                  << " first_pulses=" << first_pulses << " second_pulses=" << second_pulses << '\n';
    }
}
}
void repair_competing_retirement() { repair_competing_run(false); }
void repair_replacement_move() { repair_competing_run(true); }

}
