// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_flyer.hpp"
#include "battle_combat_geometry.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include "test_support.hpp"
#include <cmath>
#include <memory>
#include <array>

namespace tests::navigation {
namespace {
using battle::Session;
struct CraterAccess : CMatrixEffect { using CMatrixEffect::m_SpotProperties; };
struct CraterCatalog {
    SSpotProperties previous = CraterAccess::m_SpotProperties[SPOT_VORONKA];
    ~CraterCatalog() { CraterAccess::m_SpotProperties[SPOT_VORONKA] = previous; }
    void install() {
        auto &spot = CraterAccess::m_SpotProperties[SPOT_VORONKA];
        spot = {};
        spot.color = 0xffffffff;
        spot.func = SpotTaktConstant;
        spot.texture = static_cast<CTextureManaged *>(g_Cache->Get(CacheClass::TextureManaged, L"synthetic-combat"));
        spot.ttl = 30000;
    }
};
struct ReleaseCore { void operator()(SObjectCore *core) const { core->Release(); } };
using CoreLease = std::unique_ptr<SObjectCore, ReleaseCore>;
battle::Profile profile(random::Mode mode, int step) {
    battle::Profile p;
    p.mode = mode;
    p.seed = 81;
    p.step_ms = step;
    p.max_frames = 6000;
    p.audio = false;
    p.initially_selected = false;
    p.layout = NavigationLayout::Open;
    p.unavailable_icon_targets = true;
    return p;
}
struct Delivery {
    CMatrixFlyer *carrier;
    CMatrixRobotAI *cargo;
    CoreLease carrier_core;
    CoreLease cargo_core;
    explicit Delivery(float angle) {
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0);
        // Place 3 is a declared free place in the open navigation map.
        MG_CHECK(g_MatrixMap->m_RN.m_PlaceCnt > 3);
        const CPoint destination = g_MatrixMap->m_RN.m_Place[3].m_Pos;
        const D3DXVECTOR2 landing(float(destination.x * GLOBAL_SCALE_MOVE),
                                 float(destination.y * GLOBAL_SCALE_MOVE));
        g_MatrixMap->GetPlayerSide()->OrderFlyer(landing, FO_GIVE_BOT, angle, 3, destination, 0);
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1);
        carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
        MG_CHECK(carrier->CarryingRobot());
        cargo = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
        carrier_core.reset(carrier->GetCore(DEBUG_CALL_INFO));
        cargo_core.reset(cargo->GetCore(DEBUG_CALL_INFO));
        MG_CHECK(cargo->m_CurrState == ROBOT_CARRYING && cargo->GetEnv()->m_Place == 3);
        const int group = cargo->GetGroupLogic();
        MG_CHECK(group >= 0);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].Order() == mpo_Attack);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].m_To == destination);
    }
};
void finite(const D3DXVECTOR3 &p) {
    MG_CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
}
void finite(const CMatrixRobotAI &r) {
    MG_CHECK(std::isfinite(r.m_PosX) && std::isfinite(r.m_PosY) && std::isfinite(r.GetMatrix()._43));
}
}
void delivery_flights() {
    // Recorded production lifecycle boundaries for seed 81 and the declared
    // geometries; rows are PM/10, PM/16, CRT/10 and CRT/16, then variants 0..3.
    constexpr int timing[4][4][3] = {
        {{3420, 3740, 6690}, {5610, 6280, 10650}, {4760, 5140, 8740}, {6110, 6810, 11610}},
        {{3424, 3744, 6704}, {5600, 6272, 10656}, {4768, 5152, 8752}, {6112, 6800, 11616}},
        {{5410, 5730, 10600}, {7760, 8420, 14860}, {6500, 6880, 12150}, {8120, 8820, 15560}},
        {{5408, 5728, 10608}, {7760, 8432, 14864}, {6496, 6880, 12160}, {8128, 8832, 15552}},
    };
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int step : {10, 16}) for (int variant = 0; variant < 4; ++variant) {
        BattleMaterialProfile materials;
        Session session(profile(mode, step));
        auto &order = install_battle_transport();
        // Pair approach direction with distinct declared flight geometry.
        order.ParSetAdd(L"Distance", variant % 2 ? L"450" : L"300");
        order.ParSetAdd(L"Height", variant >= 2 ? L"160" : L"100");
        order.ParSetAdd(L"LandHeight", variant % 2 ? L"70" : L"40");
        MG_CHECK(session.remove(2) && session.remove(4));
        Delivery delivery(float(variant) * float(M_PI / 2));
        const D3DXVECTOR3 start = delivery.carrier->GetPos();
        int release = -1, landing = -1, retired = -1;
        float released_x = 0, released_y = 0;
        float previous_z = 0;
        int falling_frames = 0;
        while (session.time() < 30000) {
            session.advance();
            MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
            finite(*delivery.cargo);
            if (delivery.carrier_core->m_Object) finite(delivery.carrier->GetPos());
            else if (retired < 0) retired = session.time();
            if (release < 0 && delivery.cargo->m_CurrState != ROBOT_CARRYING) {
                release = session.time();
                MG_CHECK(delivery.cargo->m_CurrState == ROBOT_FALLING);
                MG_CHECK(delivery.carrier_core->m_Object && !delivery.carrier->CarryingRobot());
                released_x = delivery.cargo->m_PosX;
                released_y = delivery.cargo->m_PosY;
                previous_z = delivery.cargo->GetMatrix()._43;
                MG_CHECK(previous_z > g_MatrixMap->GetZ(released_x, released_y));
                MG_CHECK(delivery.carrier->GetPos() != start);
            }
            if (release >= 0 && delivery.cargo->m_CurrState == ROBOT_FALLING) {
                ++falling_frames;
                MG_CHECK(delivery.cargo->m_PosX == released_x && delivery.cargo->m_PosY == released_y);
                MG_CHECK(delivery.cargo->GetMatrix()._43 <= previous_z);
                previous_z = delivery.cargo->GetMatrix()._43;
            }
            if (landing < 0 && release >= 0 && delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD)
                landing = session.time();
        }
        std::cout << session.metadata("synthetic-navigation-delivery-v1") << '\n';
        std::cout << "variant=" << variant << " release=" << release << " landing=" << landing
                  << " retirement=" << retired << " position=" << delivery.cargo->m_PosX << ','
                  << delivery.cargo->m_PosY << " in_place=" << delivery.cargo->PLIsInPlace() << '\n';
        MG_CHECK(release > 0 && landing > release && retired > landing && falling_frames > 1);
        const int row = (mode == random::Mode::LegacyCRT ? 2 : 0) + (step == 16 ? 1 : 0);
        MG_CHECK(release == timing[row][variant][0] && landing == timing[row][variant][1]);
        MG_CHECK(retired == timing[row][variant][2]);
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && !delivery.carrier_core->m_Object);
        MG_CHECK(delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        MG_CHECK(delivery.cargo->PLIsInPlace());
        MG_CHECK(delivery.cargo->m_PosX == 500 && delivery.cargo->m_PosY == 500);
        MG_CHECK(session.rejected_icon_targets() == 1);
    }
}
void delivery_carrier_damage() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int stage = 0; stage < 3; ++stage) {
        BattleMaterialProfile materials;
        BattleCombatAssets effects;
        CraterCatalog crater;
        Session session(profile(mode, 10));
        effects.install();
        crater.install();
        install_battle_transport();
        MG_CHECK(session.remove(2) && session.remove(4));
        Delivery delivery(0);
        for (;;) {
            MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
            MG_CHECK(delivery.carrier_core->m_Object == delivery.carrier);
            const bool ready = stage == 0 ? session.time() >= 1000 :
                stage == 1 ? delivery.cargo->m_CurrState != ROBOT_CARRYING :
                             delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD;
            if (ready || session.time() >= 10000) break;
            session.advance();
        }
        MG_CHECK(delivery.carrier_core->m_Object == delivery.carrier);
        MG_CHECK(stage == 0 ? delivery.cargo->m_CurrState == ROBOT_CARRYING :
                 stage == 1 ? delivery.cargo->m_CurrState == ROBOT_FALLING :
                              delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        const D3DXVECTOR3 position = delivery.carrier->GetPos();
        const float hp = delivery.carrier->GetHitPoint();
        MG_CHECK(hp == 1000);
        const auto draws = random::draw_count();
        MG_CHECK(!delivery.carrier->Damage(WEAPON_REPAIR, position, D3DXVECTOR3(0, 0, 1), 2, nullptr));
        MG_CHECK(delivery.carrier->GetHitPoint() == hp && random::draw_count() == draws);
        g_Config.m_FlyerDamages[Weap2Index(WEAPON_LASER)] = {1000, 0, 0};
        MG_CHECK(delivery.carrier->Damage(WEAPON_LASER, position, D3DXVECTOR3(0, 0, 1), 2, nullptr));
        MG_CHECK(!delivery.carrier_core->m_Object && g_MatrixMap->m_AD_Obj_cnt == 0);
        MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
        MG_CHECK(delivery.cargo->IsMustDie() == (stage == 0));
        const int hit_time = session.time();
        while (session.time() < hit_time + 20000) session.advance();
        std::cout << session.metadata("synthetic-navigation-carrier-damage-v1") << '\n';
        std::cout << "damage_stage=" << stage << " hit_time=" << hit_time
                  << " cargo_survived=" << bool(delivery.cargo_core->m_Object) << '\n';
        if (stage == 0) MG_CHECK(!delivery.cargo_core->m_Object);
        else {
            MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
            MG_CHECK(delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD && !delivery.cargo->IsMustDie());
            MG_CHECK(delivery.cargo->PLIsInPlace());
            MG_CHECK(delivery.cargo->m_PosX == 500 && delivery.cargo->m_PosY == 500);
        }
    }
}
}
