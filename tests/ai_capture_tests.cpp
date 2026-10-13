// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "MatrixObjectCannon.hpp"
#include "Logic/MatrixEnvironment.h"
#include <vector>

namespace tests::ai {
namespace {
struct CaptureScene {
    BattleCombatAssets assets;
    battle::Session session;
    CMatrixRobotAI *observer;
    CMatrixBuilding *base;
    CMatrixCannon *guard;
    int group;
    explicit CaptureScene(random::Mode mode) : session({mode, 79}) {
        assets.install();
        auto &side = *g_MatrixMap->GetPlayerSide();
        observer = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_PLASMA)] = 400;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_PLASMA)] = 100;
        arm_battle_robot(*observer, RUK_WEAPON_PLASMA);
        auto owned = make_battle_building(*load_battle_mesh({}), 2, BUILDING_BASE, 400, 400);
        base = owned.get();
        g_MatrixMap->AddObject(owned.release(), true);
        // ReloadDynamics loads buildings before robots. Match that initial
        // scheduler order: capture validation stops at an existing capturer.
        std::vector<CMatrixMapStatic *> robots;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsRobot()) robots.push_back(object);
        for (auto *object : robots) { object->DelLT(); object->AddLT(); }
        MG_CHECK(CMatrixMapStatic::GetFirstLogic() == base);
        auto &props = g_Config.m_CannonsProps[0];
        props = {};
        props.weapon = WEAPON_CANNON2;
        props.m_Hitpoint = 1000;
        props.m_Strength = 10;
        props.seek_radius = 300;
        props.max_da = 0.1f;
        props.max_top_angle = 1.5f;
        props.max_bottom_angle = -1.5f;
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = 300;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_CANNON2)] = 100;
        guard = create_battle_cannon(2, 220, 140);
        MG_CHECK(guard->GetHitPoint() > 0 && guard->GetStrength() == 10);
        guard->m_ParentBuilding = base;
        base->m_TurretsHave = 1;
        base->m_TurretsPlacesCnt = 1;
        base->m_TurretsPlaces[0].m_Coord = CPoint(22, 14);
        base->m_TurretsPlaces[0].m_CannonType = 1;
        group = side.SelGroupToLogicGroup();
        MG_CHECK(group >= 0);
    }
    int capturers() const {
        int count = 0;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsLiveRobot() && object->GetSide() == PLAYER_SIDE &&
                object->AsRobot()->GetGroupLogic() == group && object->AsRobot()->GetCaptureFactory() == base)
                ++count;
        return count;
    }
    void check_intent() const {
        const auto &order = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group];
        MG_CHECK(order.Order() == mpo_Capture && order.m_Obj == base);
    }
    void evidence(const char *trigger) const {
        const auto &order = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group];
        std::cout << session.metadata("synthetic-capture-guard-v1") << '\n';
        std::cout << "{\"decision\":\"capture-guard-priority\",\"trigger\":\"" << trigger
                  << "\",\"group_order\":" << int(order.Order())
                  << ",\"war\":" << (order.IsWar() ? "true" : "false")
                  << ",\"capturers\":" << capturers()
                  << ",\"guard_count\":" << base->m_TurretsHave
                  << ",\"rng_draws\":" << random::draw_count() << "}\n";
    }
};
}
void capture_guard_interruption() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        CaptureScene scene(mode);
        auto &side = *g_MatrixMap->GetPlayerSide();
        side.PGOrderCapture(scene.group, scene.base);
        scene.check_intent();
        MG_CHECK(!side.m_PlayerGroup[scene.group].IsWar() && scene.capturers() == 1);
        scene.evidence("unknown-guard-does-not-interrupt");
        scene.observer->GetEnv()->AddToList(scene.guard);
        side.TaktPL();
        scene.check_intent();
        MG_CHECK(side.m_PlayerGroup[scene.group].IsWar() && scene.capturers() == 0);
        MG_CHECK(scene.observer->GetEnv()->m_TargetAttack == scene.guard);
        scene.evidence("known-attached-guard-interrupts");
        g_MatrixMap->StaticDelete(scene.guard);
        scene.guard = nullptr;
        MG_CHECK(scene.base->m_TurretsHave == 0);
        MG_CHECK(scene.observer->GetEnv()->m_TargetAttack == nullptr);
        side.TaktPL();
        scene.check_intent();
        MG_CHECK(!side.m_PlayerGroup[scene.group].IsWar() && scene.capturers() == 1);
        scene.evidence("retired-guard-resumes-capture");
    }
}
void capture_guard_filters() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool constructing : {false, true}) {
        CaptureScene scene(mode);
        auto &side = *g_MatrixMap->GetPlayerSide();
        if (constructing) {
            scene.guard->m_CurrState = CANNON_UNDER_CONSTRUCTION;
            scene.observer->GetEnv()->AddToList(scene.guard);
        } else {
            auto *unrelated = create_battle_cannon(2, 180, 120);
            scene.observer->GetEnv()->AddToList(unrelated);
        }
        side.PGOrderCapture(scene.group, scene.base);
        scene.check_intent();
        MG_CHECK(!side.m_PlayerGroup[scene.group].IsWar() && scene.capturers() == 1);
        scene.evidence(constructing ? "unfinished-guard-does-not-interrupt" : "unrelated-cannon-does-not-interrupt");
    }
}
}
