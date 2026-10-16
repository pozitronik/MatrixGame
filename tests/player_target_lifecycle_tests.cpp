// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceElement.h"
#include "input.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace tests::battle;
CMatrixSideUnit &side() { return *g_MatrixMap->GetPlayerSide(); }
CMatrixRobotAI *robot(const Session &session, EntityId id) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && session.matches(id, object->AsRobot())) return object->AsRobot();
    throw std::runtime_error("Expected a registered live robot");
}
void press(CFormMatrixGame &form, uint8_t key) {
    form.Keyboard(true, key);
    form.Keyboard(false, key);
    MG_CHECK(!Input::isVKeyPressed(key));
}
void action(CFormMatrixGame &form, bool button, uint8_t key, const wchar_t *name) {
    if (button) {
        auto element = tests::make_engine_object<CIFaceElement>();
        element->m_strName = name;
        g_IFaceList->PlayerAction(element.get());
    } else press(form, key);
}
void click(CMatrixMapStatic *object) {
    g_MatrixMap->m_TraceStopObj = object;
    g_MatrixMap->m_TraceStopPos = object->GetGeoCenter();
    side().OnLButtonDown(CPoint(0, 0));
}
void ground(float x, float y) {
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(x, y, 0);
    side().OnLButtonDown(CPoint(0, 0));
}
enum class Ending { Complete, Remove, Stop };
std::vector<std::string> repair_run(random::Mode mode, int step, bool button, Ending ending) {
    tests::BattleCombatAssets assets;
    Session session({mode, 80, step, 1000, false, true});
    std::cout << session.metadata("repair-target-lifecycle-v1") << " button=" << button
              << " ending=" << int(ending) << std::endl;
    assets.install();
    CFormMatrixGame form;
    auto *healer = robot(session, 1);
    auto *patient = robot(session, 2);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_REPAIR)] = 600;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_REPAIR)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {20, 20, 0};
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    tests::arm_battle_robot(*healer, RUK_WEAPON_REPAIR);
    patient->InitMaxHitpoint(50);
    patient->SetMaxSpeed(0);
    MG_CHECK(!patient->Damage(WEAPON_GUN, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
    MG_CHECK(patient->GetHitPoint() == 3 && side().GetCurGroup()->GetRepairsCnt() == 1);
    const auto before = session.snapshot();
    action(form, button, 'R', IF_ORDER_REPAIR);
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR));
    press(form, 'X');
    click(patient);
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR | ORDERING_MODE));
    MG_CHECK(session.snapshot() == before);
    action(form, button, 'R', IF_ORDER_REPAIR);
    ground(500, 300);
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR));
    MG_CHECK(session.snapshot() == before);
    click(robot(session, 3));
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR));
    MG_CHECK(session.snapshot() == before);
    click(patient);
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR | ORDERING_MODE));
    MG_CHECK(session.robot(1).order == mpo_Repair && session.robot(1).target == 2);
    int interrupted_at = 0;
    int heals = 0;
    float health = 3;
    std::vector<std::string> states;
    while (session.time() < 2000) {
        if (!interrupted_at && ending != Ending::Complete && session.time() >= 300) {
            MG_CHECK(patient->GetHitPoint() > 3 && patient->NeedRepair());
            MG_CHECK(session.robot(1).order == mpo_Repair && session.robot(1).target == 2);
            if (ending == Ending::Remove) {
                MG_CHECK(session.remove(2));
                patient = nullptr;
            } else {
                action(form, button, 'S', IF_ORDER_STOP);
                MG_CHECK(session.robot(1).order == mpo_Stop && session.robot(1).target_is_null);
            }
            interrupted_at = session.time();
        }
        session.advance();
        if (patient && patient->GetHitPoint() != health) {
            ++heals;
            health = patient->GetHitPoint();
            MG_CHECK(health == float(30 + 4 * heals) / 10.0f);
        }
        if (interrupted_at && ending == Ending::Stop)
            MG_CHECK(session.robot(1).order == mpo_Stop && session.robot(1).target_is_null);
        // TaktPL performs target cleanup at its next >=100 ms boundary.
        if (interrupted_at && ending == Ending::Remove && session.time() >= interrupted_at + 100 + step) {
            MG_CHECK(session.robot(1).order == mpo_Stop && session.robot(1).target_is_null);
            MG_CHECK(healer->GetEnv()->m_Target == nullptr && !healer->FindOrderLikeThat(ROT_FIRE));
        }
        states.push_back(session.snapshot());
    }
    MG_CHECK(session.robot(1).order == mpo_Stop && session.robot(1).target_is_null);
    if (patient) MG_CHECK(heals == 5 && patient->GetHitPoint() == 5 && !patient->NeedRepair());
    else MG_CHECK(heals > 0 && heals < 5 && !session.robot(2).alive);
    return states;
}
void repair_target_lifecycle() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16})
            for (auto ending : {Ending::Complete, Ending::Remove, Ending::Stop}) {
                const auto keyboard = repair_run(mode, step, false, ending);
                // Stop intentionally has different unit-order behavior between its routes.
                const auto button = repair_run(mode, step, true, ending);
                if (ending != Ending::Stop) MG_CHECK(keyboard == button);
            }
}

struct EffectAccess : CMatrixEffect { using CMatrixEffect::m_SpotProperties; };
struct CraterAssets {
    SSpotProperties previous = EffectAccess::m_SpotProperties[SPOT_VORONKA];
    void install() {
        EffectAccess::m_SpotProperties[SPOT_VORONKA] = EffectAccess::m_SpotProperties[SPOT_PLASMA_HIT];
        EffectAccess::m_SpotProperties[SPOT_VORONKA].func = SpotTaktVoronka;
    }
    ~CraterAssets() { EffectAccess::m_SpotProperties[SPOT_VORONKA] = previous; }
};
enum class Target { Friendly, Enemy, Ground };
std::vector<std::string> bomb_run(random::Mode mode, int step, bool button, Target target_kind, Ending ending) {
    tests::BattleCombatAssets assets;
    CraterAssets crater;
    Session session({mode, 80, step, 4000, false, true});
    std::cout << session.metadata("bomb-target-lifecycle-v1") << " button=" << button
              << " target=" << int(target_kind) << " ending=" << int(ending) << std::endl;
    assets.install();
    crater.install();
    CFormMatrixGame form;
    auto *bomber = robot(session, 1);
    auto *target = robot(session, 3);
    if (target_kind == Target::Friendly) {
        target->m_Side = PLAYER_SIDE;
        target->SetGroupLogic(-1);
    }
    target->InitMaxHitpoint(50);
    target->SetMaxSpeed(0);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 150;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
    tests::arm_battle_robot(*bomber, RUK_WEAPON_BOMB);
    MG_CHECK(side().GetCurGroup()->GetBombersCnt() == 1);
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(bomber->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    const auto initial = session.snapshot();
    action(form, button, 'E', IF_ORDER_BOMB);
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB));
    press(form, 'X');
    ground(500, 500);
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB | ORDERING_MODE));
    MG_CHECK(session.snapshot() == initial && !bomber->IsMustDie());
    action(form, button, 'E', IF_ORDER_BOMB);
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB));
    if (target_kind == Target::Ground) ground(500, 500);
    else click(target);
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB | ORDERING_MODE));
    MG_CHECK(session.robot(1).order == mpo_Bomb);
    MG_CHECK(session.robot(1).target == (target_kind == Target::Ground ? 0 : 3));
    const auto destination = side().m_PlayerGroup[session.robot(1).group].m_To;
    int interrupted_at = 0;
    CPoint stop_destination;
    int requested_at = 0, dying_at = 0, retired_at = 0, damage_events = 0;
    float health = target->GetHitPoint();
    const float damaged_health = target_kind == Target::Friendly ? 4.6f : 4.0f;
    float last_x = 0, last_y = 0;
    std::vector<std::string> states;
    while (session.time() < 30000) {
        if (!interrupted_at && ending != Ending::Complete && session.time() >= 1000) {
            MG_CHECK(core->m_Object == bomber && bomber->IsLiveRobot() && !bomber->IsMustDie());
            MG_CHECK(session.robot(1).order == mpo_Bomb);
            MG_CHECK((bomber->m_PosX - 100) * (bomber->m_PosX - 100) +
                     (bomber->m_PosY - 200) * (bomber->m_PosY - 200) > 100);
            if (ending == Ending::Remove) {
                MG_CHECK(target_kind != Target::Ground && session.robot(1).target == 3);
                MG_CHECK(session.remove(3));
                target = nullptr;
            } else {
                action(form, button, 'S', IF_ORDER_STOP);
                MG_CHECK(session.robot(1).order == mpo_Stop && session.robot(1).target_is_null);
                stop_destination = side().m_PlayerGroup[session.robot(1).group].m_To;
            }
            interrupted_at = session.time();
        }
        session.advance();
        if (core->m_Object && bomber->IsMustDie() && !requested_at) {
            requested_at = session.time();
            // All three supplied targets are at (500, 500). Retirement must not
            // turn completion into an explosion at the dispatch/removal position.
            MG_CHECK((bomber->m_PosX - 500) * (bomber->m_PosX - 500) +
                     (bomber->m_PosY - 500) * (bomber->m_PosY - 500) < 150 * 150);
        }
        if (core->m_Object && bomber->m_CurrState == ROBOT_DIP && !dying_at) dying_at = session.time();
        if (!core->m_Object && !retired_at) retired_at = session.time();
        if (target && target->GetHitPoint() != health) {
            ++damage_events;
            MG_CHECK(health == 5 && target->GetHitPoint() == damaged_health);
            health = target->GetHitPoint();
        }
        if (interrupted_at && ending == Ending::Stop) {
            MG_CHECK(core->m_Object == bomber && bomber->IsLiveRobot() && !bomber->IsMustDie());
            MG_CHECK(session.robot(1).order == mpo_Stop && session.robot(1).target_is_null);
            MG_CHECK(damage_events == 0);
            const auto remaining = side().m_PlayerGroup[session.robot(1).group].m_To;
            MG_CHECK(remaining.x == stop_destination.x && remaining.y == stop_destination.y);
            if (session.time() >= 25000)
                MG_CHECK(bomber->m_PosX == last_x && bomber->m_PosY == last_y);
            last_x = bomber->m_PosX;
            last_y = bomber->m_PosY;
        }
        if (interrupted_at && ending == Ending::Remove && !requested_at &&
            session.time() >= interrupted_at + 100 + step) {
            MG_CHECK(session.robot(1).order == mpo_Bomb && session.robot(1).target_is_null);
            const auto remaining = side().m_PlayerGroup[session.robot(1).group].m_To;
            MG_CHECK(remaining.x == destination.x && remaining.y == destination.y);
        }
        states.push_back(session.snapshot());
    }
    std::cout << "requested=" << requested_at << " dying=" << dying_at << " retired=" << retired_at
              << " damage_events=" << damage_events << '\n';
    if (ending == Ending::Stop) MG_CHECK(!requested_at && !dying_at && !retired_at && target && target->GetHitPoint() == 5);
    else {
        MG_CHECK(requested_at > 1000 && dying_at >= requested_at && retired_at > dying_at);
        MG_CHECK(!core->m_Object && !session.robot(1).alive);
        MG_CHECK(session.robot(2).alive && session.robot(2).order == mpo_Stop && session.robot(2).target_is_null);
        if (target) MG_CHECK(damage_events == 1 && target->GetHitPoint() == damaged_health);
        else MG_CHECK(!session.robot(3).alive);
    }
    return states;
}
template<Target target, Ending ending>
void bomb_target_lifecycle() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) {
            const auto keyboard = bomb_run(mode, step, false, target, ending);
            const auto button = bomb_run(mode, step, true, target, ending);
            if (ending != Ending::Stop) MG_CHECK(keyboard == button);
        }
}
const tests::Case cases[] = {
    {"game.player.repair_target_lifecycle", repair_target_lifecycle},
    {"game.player.bomb_friendly_completion", bomb_target_lifecycle<Target::Friendly, Ending::Complete>},
    {"game.player.bomb_enemy_completion", bomb_target_lifecycle<Target::Enemy, Ending::Complete>},
    {"game.player.bomb_ground_completion", bomb_target_lifecycle<Target::Ground, Ending::Complete>},
    {"game.player.bomb_friendly_retirement", bomb_target_lifecycle<Target::Friendly, Ending::Remove>},
    {"game.player.bomb_enemy_retirement", bomb_target_lifecycle<Target::Enemy, Ending::Remove>},
    {"game.player.bomb_target_stop", bomb_target_lifecycle<Target::Enemy, Ending::Stop>},
    {"game.player.bomb_ground_stop", bomb_target_lifecycle<Target::Ground, Ending::Stop>},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Target command lifecycle fixtures leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
