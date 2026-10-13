// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "command_world.hpp"
#include "MatrixObjectBuilding.hpp"
#include <cmath>
#include <stdexcept>

namespace tests {

// The caller owns the unregistered building and must destroy it before the map
// and donor geometry. AddObject transfers ownership to the map when desired.
// This assembles real graph nodes, not the production building file loader.
inline EnginePtr<CMatrixBuilding> make_battle_building(CVectorObject &geometry,
    int side, EBuildingType kind, float x, float y, float hitpoints = 1000) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
        x >= g_MatrixMap->m_Size.x * GLOBAL_SCALE || y >= g_MatrixMap->m_Size.y * GLOBAL_SCALE)
        throw std::invalid_argument("Synthetic building coordinates are outside the map");
    if (kind == BUILDING_BASE) {
        const int cleanup_x = Float2Int(x * INVERT(GLOBAL_SCALE));
        const int px = Float2Int((x - GLOBAL_SCALE / 2) * INVERT(GLOBAL_SCALE)) - 1;
        const int py = Float2Int((y - GLOBAL_SCALE / 2) * INVERT(GLOBAL_SCALE)) - 1;
        for (int dx = 0; dx < 3; ++dx)
            for (int dy = 0; dy < 3; ++dy) {
                // The production destructor currently uses the x window on both
                // axes. Reject unsafe footprints before OnLoad registers cells.
                // Follow-up: https://github.com/pozitronik/MatrixGame/issues/130
                if (!g_MatrixMap->UnitGetTest(px + dx, py + dy) ||
                    px + dx < cleanup_x - 4 || px + dx >= cleanup_x + 4 ||
                    py + dy < cleanup_x - 4 || py + dy >= cleanup_x + 4)
                    throw std::invalid_argument("Synthetic base footprint exceeds its destructor cleanup window");
            }
    }
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
