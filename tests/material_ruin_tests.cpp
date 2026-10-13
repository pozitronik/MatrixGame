// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_material.hpp"
#include "battle_mesh.hpp"
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixMapGroup.hpp"
#include "MatrixObject.hpp"
#include "MatrixSkinManager.hpp"
#include "Effects/MatrixEffect.hpp"
#include <algorithm>
#include <bit>
#include <set>
#include <sstream>

namespace {
struct ReleaseCore {
    void operator()(SObjectCore *core) const { core->Release(); }
};
using CoreLease = std::unique_ptr<SObjectCore, ReleaseCore>;

std::set<CMatrixMapObject *> ruins() {
    std::set<CMatrixMapObject *> found;
    for (int x = 0; x < g_MatrixMap->m_GroupSize.x; ++x)
        for (int y = 0; y < g_MatrixMap->m_GroupSize.y; ++y) {
            auto *group = g_MatrixMap->GetGroupByIndex(x, y);
            if (!group) continue;
            for (int i = 0; i < group->ObjectsCnt(); ++i) {
                auto *object = group->GetObject(i);
                if (object->GetObjectType() == OBJECT_TYPE_MAPOBJECT)
                    found.insert(static_cast<CMatrixMapObject *>(object));
            }
        }
    return found;
}

int footprint_cells(const CMatrixBuilding *building) {
    int count = 0;
    for (int x = 0; x < g_MatrixMap->m_Size.x; ++x)
        for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
            count += g_MatrixMap->UnitGet(x, y)->m_Base == building;
    return count;
}

std::vector<std::string> demolish(EBuildingType kind, random::Mode mode) {
    tests::BattleMaterialProfile material_profile;
    tests::BattleCombatAssets effects;
    tests::battle::Session session({mode, 83, 10, 500, false, false});
    effects.install();
    const std::wstring texture_name = std::wstring(OBJECT_PATH_BUILDINGS_RUINS) + L"b" + std::to_wstring(int(kind));
    const std::wstring mesh_name = texture_name + L".vo";
    auto *texture = tests::install_battle_texture(texture_name.c_str());
    auto *mesh = tests::load_battle_material_mesh({}, mesh_name.c_str(), texture_name.c_str());
    CVectorObject *secondary_mesh = nullptr;
    CTextureManaged *secondary_texture = nullptr;
    if (kind != BUILDING_BASE) {
        const auto secondary_name = texture_name + L"?Trans";
        secondary_texture = tests::install_battle_texture(secondary_name.c_str());
        secondary_mesh = tests::load_battle_material_mesh({}, (texture_name + L"p.vo").c_str(), secondary_name.c_str());
    }
    MG_CHECK(ruins().empty());
    for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
        g_Config.m_Timings[resource] = 100000;
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_LASER)] = {100, 0, 0};
    // The base's final explosion is real, but this fixture is not a blast-damage
    // contract. Give it a finite radius and isolate the surrounding units.
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 20;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_BuildingDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    g_Config.m_CannonDamages[Weap2Index(WEAPON_BIGBOOM)] = {};
    auto building = tests::make_battle_building(*mesh, 2, kind, 200, 200, 50);
    auto *patient = building.get();
    CoreLease core(patient->GetCore(DEBUG_CALL_INFO));
    g_MatrixMap->AddObject(patient, true);
    building.release();
    const int initial_cells = footprint_cells(patient);
    MG_CHECK(initial_cells == (kind == BUILDING_BASE ? 9 : 0));
    MG_CHECK(patient->Damage(WEAPON_LASER, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), PLAYER_SIDE, nullptr));
    MG_CHECK(patient->m_State == BUILDING_DIP);
    MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL) == 1);

    std::vector<std::string> observations;
    int exploded_at = 0;
    int removed_at = 0;
    for (int i = 0; i < 400 && core->m_Object; ++i) {
        session.advance();
        std::ostringstream observation;
        observation << session.time() << ':' << random::draw_count();
        if (core->m_Object) {
            MG_CHECK(core->m_Object == patient);
            MG_CHECK(patient->m_State == BUILDING_DIP || patient->m_State == BUILDING_DIP_EXPLODED);
            if (patient->m_State == BUILDING_DIP_EXPLODED && !exploded_at) exploded_at = session.time();
            observation << ':' << int(patient->m_State) << ':' << std::bit_cast<uint32_t>(patient->GetHitPoint());
            MG_CHECK(footprint_cells(patient) == initial_cells);
            MG_CHECK(ruins().empty());
        } else {
            removed_at = session.time();
            observation << ":deleted";
        }
        observations.push_back(observation.str());
    }
    MG_CHECK(core->m_Object == nullptr);
    MG_CHECK(removed_at == (kind == BUILDING_BASE ? 3010 : 1010));
    MG_CHECK(exploded_at == (kind == BUILDING_BASE ? 2910 : 0));
    MG_CHECK(footprint_cells(patient) == 0);
    for (auto *live = CMatrixMapStatic::GetFirstLogic(); live; live = live->GetNextLogic())
        MG_CHECK(live != patient);

    const auto replacements = ruins();
    MG_CHECK(replacements.size() == (kind == BUILDING_BASE ? 1u : 2u));
    int primary_count = 0;
    int secondary_count = 0;
    for (auto *ruin : replacements) {
        MG_CHECK(!ruin->InLT());
        MG_CHECK(ruin->m_Graph && ruin->m_Graph->GetSkin());
        MG_CHECK(ruin->GetMatrix()._41 == 200 && ruin->GetMatrix()._42 == 200);
        MG_CHECK(ruin->GetMatrix()._43 == g_MatrixMap->GetZ(200, 200));
        MG_CHECK(ruin->m_AngleX == 0 && ruin->m_AngleY == 0 && ruin->m_AngleZ == 0 && ruin->m_Scale == 1);
        MG_CHECK(ruin->m_ShadowProj == nullptr && ruin->m_ShadowStencil == nullptr);
        const auto *skin = static_cast<const SMatrixSkin *>(ruin->m_Graph->GetSkin());
        if (ruin->m_ShadowType == SHADOW_PROJ_DYNAMIC) {
            ++primary_count;
            MG_CHECK(ruin->m_Graph->VO() == mesh && skin->m_Tex == texture);
        } else {
            ++secondary_count;
            MG_CHECK(ruin->m_ShadowType == SHADOW_OFF);
            MG_CHECK(ruin->m_Graph->VO() == secondary_mesh && skin->m_Tex == secondary_texture);
        }
    }
    MG_CHECK(primary_count == 1 && secondary_count == (kind == BUILDING_BASE ? 0 : 1));

    // Placement intersects synthetic geometry with random rays. Preserve exact
    // repeatability within one build without imposing a cross-compiler hit count.
    const int smoke_count = g_MatrixMap->m_EffectSpawnersCnt;
    MG_CHECK(smoke_count > 0 && smoke_count <= 49);
    std::ostringstream smoke_observation;
    smoke_observation << "smoke:" << smoke_count;
    for (int i = 0; i < smoke_count; ++i) {
        const auto *props = g_MatrixMap->m_EffectSpawners[i].Props();
        MG_CHECK(props->m_Type == EFFECT_SMOKE && props->m_Size == sizeof(SpawnEffectSmoke));
        const auto *smoke = static_cast<const SpawnEffectSmoke *>(props);
        MG_CHECK(smoke->m_ttl >= 700 && smoke->m_ttl < 1200);
        MG_CHECK(smoke->m_puffttl == 2400 && smoke->m_spawntime == 60);
        MG_CHECK(!smoke->m_intense && smoke->m_color == 0x78000000);
        smoke_observation << ':' << std::bit_cast<uint32_t>(props->m_Pos.x)
                          << ':' << std::bit_cast<uint32_t>(props->m_Pos.y)
                          << ':' << std::bit_cast<uint32_t>(props->m_Pos.z)
                          << ':' << std::bit_cast<uint32_t>(smoke->m_ttl);
    }
    observations.push_back(smoke_observation.str());
    for (int i = 0; i < 5; ++i) session.advance();
    MG_CHECK(core->m_Object == nullptr && ruins() == replacements);
    MG_CHECK(g_MatrixMap->m_EffectSpawnersCnt == smoke_count);
    MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL) == 1);
    for (int i = 0; i < smoke_count; ++i) {
        auto *owner = g_MatrixMap->m_EffectSpawners[i].GetUnder();
        MG_CHECK(owner != patient);
        MG_CHECK(owner == TRACE_STOP_LANDSCAPE ||
            std::any_of(replacements.begin(), replacements.end(), [owner](auto *ruin) { return ruin == owner; }));
    }
    return observations;
}

template<EBuildingType Kind>
void demolition() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto first = demolish(Kind, mode);
        MG_CHECK(first == demolish(Kind, mode));
    }
}

constexpr tests::Case cases[] = {
    {"game.harness.materials.demolish_base", demolition<BUILDING_BASE>},
    {"game.harness.materials.demolish_titan", demolition<BUILDING_TITAN>},
    {"game.harness.materials.demolish_plasma", demolition<BUILDING_PLASMA>},
    {"game.harness.materials.demolish_electronics", demolition<BUILDING_ELECTRONIC>},
    {"game.harness.materials.demolish_energy", demolition<BUILDING_ENERGY>},
    {"game.harness.materials.demolish_repair", demolition<BUILDING_REPAIR>},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Material ruin fixture leaked tracked allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
