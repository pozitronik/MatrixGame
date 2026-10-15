// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
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
void evidence(battle::Session &session, const char *trigger, CMatrixRobotAI *robot) {
    std::cout << session.metadata(robot->HaveRepair() == 2 ? "synthetic-repair-boxes-v1" : "synthetic-armed-boxes-v2") << '\n';
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

void ineffective_fire_run(bool blocked, bool moving = false) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool multiple : {false, true})
    for (int target_age : {2000, 2001, 4000, 4001})
    for (int hit_age : {4000, 4001, 6000, 6001, 10000, 10001}) {
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
        const bool abandon_place = !moving && (blocked ? target_age > 4000 && hit_age > 6000
            : (!multiple && stale) || (target_age > 2000 && hit_age > 10000));
        MG_CHECK(env.m_TargetAttack == (multiple && stale ? nullptr : target));
        MG_CHECK(env.m_Place == (abandon_place ? -1 : 0));
        MG_CHECK(env.IsBadPlace(0) == abandon_place);
        MG_CHECK(env.m_Target == (blocked ? nullptr : target));
        MG_CHECK(env.m_LastFire == (blocked ? 12000 - hit_age : 12000));
        MG_CHECK(env.m_LastHitTarget == (blocked ? 12000 : 12000 - hit_age));
        MG_CHECK(env.m_TargetChange == (moving ? 12000 : 12000 - target_age));
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE) == !blocked);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO) == moving);
        MG_CHECK(side.m_PlayerGroup[0].Order() == mpo_Stop && session.time() == 12000);
        evidence(session, blocked ? "blocked-fire-decision" : "ineffective-fire-decision", observer);
        std::cout << "targets=" << (multiple ? 2 : 1) << " target_age=" << target_age
                  << " pending_move=" << moving << " feedback_age=" << hit_age << " place=" << env.m_Place << '\n';
    }
}

void ineffective_fire_decisions() { ineffective_fire_run(false); }
void blocked_fire_decisions() { ineffective_fire_run(true); }

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

}
