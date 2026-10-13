// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "command_world.hpp"
#include "MatrixObjectBuilding.hpp"

namespace tests {

// The caller owns the unregistered building and must destroy it before the map
// and donor geometry. AddObject transfers ownership to the map when desired.
// This assembles real graph nodes, not the production building file loader.
inline EnginePtr<CMatrixBuilding> make_battle_building(CVectorObject &geometry,
    int side, EBuildingType kind, float x, float y, float hitpoints = 1000) {
    auto building = make_engine_object<CMatrixBuilding>();
    building->m_Side = side;
    building->m_Kind = kind;
    building->m_Pos = D3DXVECTOR2(x, y);
    building->m_State = BASE_CLOSED;
    building->m_BaseFloor = 0;
    building->m_ShadowType = SHADOW_OFF;
    building->InitMaxHitpoint(hitpoints);
    building->OnLoad();
    const int parts = kind == BUILDING_BASE ? 3 : 1;
    for (int id = 1; id <= parts; ++id) {
        auto *unit = building->m_GGraph->Add();
        unit->m_Id = id;
        unit->m_Obj->Init(&geometry, nullptr);
        unit->m_Obj->FirstFrame();
    }
    building->RNeed(MR_Matrix | MR_Graph);
    building->JoinToGroup();
    return building;
}

} // namespace tests
