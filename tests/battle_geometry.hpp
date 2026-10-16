// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <stdexcept>

class CMatrixMapLogic;
class CMatrixRobotAI;
enum ERobotUnitKind : unsigned int;

namespace tests {

enum class NavigationLayout { CentralObstacle, Open, Corridor, DeadEnd, Disconnected, MultiRegion };

inline const char *navigation_layout_name(NavigationLayout layout) {
    switch (layout) {
    case NavigationLayout::CentralObstacle: return "central-obstacle-v1";
    case NavigationLayout::Open: return "open-v1";
    case NavigationLayout::Corridor: return "corridor-v1";
    case NavigationLayout::DeadEnd: return "dead-end-v1";
    case NavigationLayout::Disconnected: return "disconnected-v1";
    case NavigationLayout::MultiRegion: return "multi-region-v1";
    }
    throw std::runtime_error("Unsupported synthetic navigation layout");
}


// Initial fixture data only. Commands, navigation and ticks remain production code.
class BattleGeometry {
public:
    static constexpr int movement_size = 64;
    static constexpr int obstacle_left = 28;
    static constexpr int obstacle_right = 36;
    static constexpr int obstacle_top = 20;
    static constexpr int obstacle_bottom = 44;

    // Requires the active map/heap/config aliases. All allocations become map-owned.
    static void configure(CMatrixMapLogic &map, NavigationLayout layout = NavigationLayout::CentralObstacle);
    // Coordinates are world units. Returned units belong to map.StaticClear().
    static CMatrixRobotAI *create_robot(CMatrixMapLogic &map, int side, float x, float y);
    // Non-wheel movement needs its synthetic effect catalog. Pneumatic callers
    // must build and own the production gait catalog before the first tick.
    static CMatrixRobotAI *create_robot(CMatrixMapLogic &map, int side, float x, float y, ERobotUnitKind chassis);
};

}
