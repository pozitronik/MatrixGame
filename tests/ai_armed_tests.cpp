// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Logic/MatrixEnvironment.h"

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
    std::cout << session.metadata("synthetic-armed-boxes-v2") << '\n';
    std::cout << "{\"decision\":\"armed-tactical-boundary\",\"trigger\":\"" << trigger
              << "\",\"enemy_count\":" << robot->GetEnv()->GetEnemyCnt()
              << ",\"move_order\":" << (robot->FindOrderLikeThat(ROT_MOVE_TO) ? "true" : "false")
              << ",\"fire_order\":" << (robot->FindOrderLikeThat(ROT_FIRE) ? "true" : "false")
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
}
