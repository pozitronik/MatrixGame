// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_combat_geometry.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixRobot.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include <array>
#include <algorithm>

namespace tests {
namespace {
struct EffectAccess : CMatrixEffect {
    using CMatrixEffect::m_Debris;
    using CMatrixEffect::m_DebrisCnt;
    using CMatrixEffect::m_BBTextures;
    using CMatrixEffect::m_SpotProperties;
};
}
struct BattleCombatAssets::Impl {
    std::array<SBillboardTextureArrayElement, BBT_LAST> previous;
    SSpotProperties previous_spot = EffectAccess::m_SpotProperties[SPOT_PLASMA_HIT];
    Impl() { std::copy_n(EffectAccess::m_BBTextures, BBT_LAST, previous.begin()); }
    ~Impl() {
        std::copy(previous.begin(), previous.end(), EffectAccess::m_BBTextures);
        EffectAccess::m_SpotProperties[SPOT_PLASMA_HIT] = previous_spot;
    }
};
BattleCombatAssets::BattleCombatAssets() : impl(std::make_unique<Impl>()) {}
BattleCombatAssets::~BattleCombatAssets() = default;
void BattleCombatAssets::install() {
    MG_CHECK(EffectAccess::m_Debris == nullptr && EffectAccess::m_DebrisCnt == 0);
    auto *mesh = load_battle_mesh({});
    EffectAccess::m_Debris = static_cast<CDebris *>(HAllocClear(sizeof(CDebris) * 2, g_MatrixHeap));
    for (int i = 0; i < 2; ++i) {
        auto *debris = new(EffectAccess::m_Debris + i) CDebris;
        ++EffectAccess::m_DebrisCnt;
        debris->Init(mesh, nullptr);
        debris->FirstFrame();
        debris->SetDebType(i);
    }
    auto *texture = static_cast<CTextureManaged *>(g_Cache->Get(CacheClass::TextureManaged, L"synthetic-combat"));
    auto &spot = EffectAccess::m_SpotProperties[SPOT_PLASMA_HIT];
    spot = {};
    spot.color = 0xffffffff;
    spot.func = SpotTaktPlasmaHit;
    spot.texture = texture;
    spot.ttl = 1000;
    for (int id = 0; id < BBT_LAST; ++id) {
        EffectAccess::m_BBTextures[id].flags = BBT_FLAG_INTENSE;
        EffectAccess::m_BBTextures[id].tex = texture;
    }
}
CMatrixCannon *create_battle_cannon(int side, float x, float y) {
    const MeshAttachment basis{20, L"Turret", 0, 0, 20};
    const MeshAttachment turret{20, L"Shaft", 0, 0, 20};
    const MeshAttachment muzzle{50, L"muzzle", 0, 15, 0};
    if (!g_Cache->Find(CacheClass::VO, L"Matrix\\Cannon\\Basis.vo")) {
        load_battle_mesh(std::span(&basis, 1), L"Matrix\\Cannon\\Basis.vo", true);
        load_battle_mesh(std::span(&turret, 1), L"Matrix\\Cannon\\Turret1.vo", true);
        load_battle_mesh(std::span(&muzzle, 1), L"Matrix\\Cannon\\Shaft1.vo", true);
    }
    auto *cannon = g_MatrixMap->StaticAdd<CMatrixCannon>();
    cannon->m_Num = 1;
    cannon->SetSide(side);
    cannon->m_Pos = D3DXVECTOR2(x, y);
    cannon->m_ShadowType = SHADOW_OFF;
    cannon->RNeed(MR_Graph | MR_Matrix);
    cannon->JoinToGroup();
    MG_CHECK(cannon->m_UnitCnt == 3 && cannon->m_WeaponCnt == 1);
    return cannon;
}
void arm_battle_robot(CMatrixRobotAI &robot, ERobotUnitKind weapon) {
    MG_CHECK(robot.m_UnitCnt == 1);
    MG_CHECK(weapon == RUK_WEAPON_LASER || weapon == RUK_WEAPON_PLASMA ||
             weapon == RUK_WEAPON_BOMB || weapon == RUK_WEAPON_REPAIR);
    const MeshAttachment armor_links[] = {{1, L"head", 0, 0, 20}, {2, L"weapon", 0, 0, 0}};
    const MeshAttachment muzzles[] = {{1, L"muzzle", 0, 15, 0}, {2, L"repair-end", 0, 25, 0}};
    auto *armor = load_battle_mesh(armor_links);
    auto *barrel = load_battle_mesh(std::span(muzzles, weapon == RUK_WEAPON_REPAIR ? 2 : 1), nullptr, true);
    robot.UnitInsert(1, MRT_ARMOR, RUK_ARMOR_BIREX);
    robot.UnitInsert(2, MRT_WEAPON, weapon);
    robot.m_Unit[2].u1.s1.m_LinkMatrix = 2;
    for (int i = 1; i <= 2; ++i) {
        robot.m_Unit[i].m_Graph = HNew(g_MatrixHeap) CVectorObjectAnim;
        robot.m_Unit[i].m_Graph->Init(i == 1 ? armor : barrel, nullptr);
        robot.m_Unit[i].m_Graph->FirstFrame();
    }
    robot.RobotWeaponInit();
    robot.CalcRobotMass();
    robot.RChange(MR_Matrix);
    robot.RNeed(MR_Matrix);
    // Assembly changes the mesh bounds at the same map time as chassis creation.
    robot.m_CalcBoundsLastTime = g_MatrixMap->GetTime() - 100;
    robot.JoinToGroup();
    MG_CHECK(robot.m_CalcBoundMax.z - robot.m_CalcBoundMin.z > 39.99f);
    MG_CHECK(robot.m_CalcBoundMax.z - robot.m_CalcBoundMin.z < 40.01f);
}
}
